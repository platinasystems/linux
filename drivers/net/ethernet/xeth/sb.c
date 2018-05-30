/* XETH side-band channel.
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <net/sock.h>
#include <uapi/linux/un.h>
#include <uapi/linux/xeth.h>

struct xeth_sb_tx_entry {
	size_t	data_len;
	struct	list_head	list;
	unsigned char		data[];
};

static inline struct xeth_sb_tx_entry *xeth_sb_tx_entry_alloc(size_t data_len)
{
	size_t n = sizeof(struct xeth_sb_tx_entry) + data_len;
	struct xeth_sb_tx_entry *entry = kzalloc(n, GFP_KERNEL);
	if (entry)
		entry->data_len = data_len;
	return entry;
}

static const char *const xeth_sb_link_stats[] = {
	"rx-packets",
	"tx-packets",
	"rx-bytes",
	"tx-bytes",
	"rx-errors",
	"tx-errors",
	"rx-dropped",
	"tx-dropped",
	"multicast",
	"collisions",
	"rx-length-errors",
	"rx-over-errors",
	"rx-crc-errors",
	"rx-frame-errors",
	"rx-fifo-errors",
	"rx-missed-errors",
	"tx-aborted-errors",
	"tx-carrier-errors",
	"tx-fifo-errors",
	"tx-heartbeat-errors",
	"tx-window-errors",
	"rx-compressed",
	"tx-compressed",
	"rx-nohandler",
};

static const size_t xeth_sb_n_link_stats =
	sizeof(struct rtnl_link_stats64)/sizeof(u64);

static char *xeth_sb_rxbuf = NULL;
static struct task_struct *xeth_sb_task_main = NULL;


static struct {
	struct	mutex		mutex;
	struct	list_head	tx;
	bool	connected;
} xeth_sb;

#define xeth_sb_lock()							\
	mutex_lock(&xeth_sb.mutex)
#define xeth_sb_unlocked(val)						\
	({								\
		typeof(val) _val = (val);				\
		mutex_unlock(&xeth_sb.mutex);				\
		(_val);							\
	})

static void xeth_sb_connection(bool val)
{
	mutex_lock(&xeth_sb.mutex);
	xeth_sb.connected = val;
	mutex_unlock(&xeth_sb.mutex);
}

static inline struct xeth_sb_tx_entry *xeth_sb_tx_pop(void)
{
	struct list_head *next = NULL;
	struct xeth_sb_tx_entry *entry = NULL;
	mutex_lock(&xeth_sb.mutex);
	if (!list_empty(&xeth_sb.tx)) {
		next = list_next_rcu(&xeth_sb.tx);
		list_del_rcu(next);
		entry = container_of(next, struct xeth_sb_tx_entry, list);
	}
	mutex_unlock(&xeth_sb.mutex);
	return entry;
}

static inline void xeth_sb_tx_push(struct xeth_sb_tx_entry *entry)
{
	mutex_lock(&xeth_sb.mutex);
	list_add_rcu(&entry->list, &xeth_sb.tx);
	mutex_unlock(&xeth_sb.mutex);
}

static void xeth_sb_reset_nd_stats(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	u64 *link_stat = (u64*)&priv->link.stats;
	int i;

	mutex_lock(&priv->link.mutex);
	mutex_lock(&priv->ethtool.mutex);
	for (i = 0; i < xeth_sb_n_link_stats; i++)
		link_stat[i] = 0;
	for (i = 0; i < xeth.n.ethtool.stats; i++)
		priv->ethtool.stats[i] = 0;
	mutex_unlock(&priv->link.mutex);
	mutex_unlock(&priv->ethtool.mutex);
}

static struct net_device *xeth_sb_nd_of(const char *msg_ifname)
{
	struct net_device *nd;
	char ifname[IFNAMSIZ];
	int err;
	u16 id, ndi, iflinki;

	strlcpy(ifname, msg_ifname, IFNAMSIZ);
	err = xeth.ops.parse_name(ifname, &id, &ndi, &iflinki);
	if (err) {
		xeth_pr("\"%s\" invalid", ifname);
		return NULL;
	}
	nd = to_xeth_nd(id);
	if (!nd)
		xeth_pr("\"%s\" no such device", ifname);
	return nd;
}

static void xeth_sb_carrier(const struct xeth_msg_carrier *msg)
{
	const char *onoff = "invalid";
	struct net_device *nd = xeth_sb_nd_of(msg->ifname);
	if (!nd)
		return;
	switch (msg->flag) {
	case XETH_CARRIER_ON:
		netif_carrier_on(nd);
		onoff = "on";
		break;
	case XETH_CARRIER_OFF:
		netif_carrier_off(nd);
		onoff = "off";
		break;
	}
	xeth_pr_nd(nd, "carrier %s", onoff);
}

static void xeth_sb_link_stat(const struct xeth_msg_stat *msg)
{
	struct net_device *nd;
	struct xeth_priv *priv;
	u64 *stat;

	if (msg->index >= xeth_sb_n_link_stats) {
		xeth_pr("invalid link stat index: %llu", msg->index);
		return;
	}
	nd = xeth_sb_nd_of(msg->ifname);
	if (!nd)
		return;
	priv = netdev_priv(nd);
	stat = (u64*)&priv->link.stats + msg->index;
	mutex_lock(&priv->link.mutex);
	*stat = msg->count;
	mutex_unlock(&priv->link.mutex);
	xeth_pr_nd(nd, "%s: %llu", xeth_sb_link_stats[msg->index], msg->count);
}

static void xeth_sb_ethtool_stat(const struct xeth_msg_stat *msg)
{
	struct net_device *nd;
	struct xeth_priv *priv;

	if (msg->index >= xeth.n.ethtool.stats) {
		xeth_pr("invalid ethtool stat index: %llu", msg->index);
		return;
	}
	nd = xeth_sb_nd_of(msg->ifname);
	if (!nd)
		return;
	priv = netdev_priv(nd);
	mutex_lock(&priv->ethtool.mutex);
	priv->ethtool.stats[msg->index] = msg->count;
	mutex_unlock(&priv->ethtool.mutex);
	xeth_pr_nd(nd, "%s: %llu", xeth.ethtool.stats[msg->index], msg->count);
}

static void xeth_sb_exception_frame(const char *buf, int n)
{
	struct sk_buff *skb = netdev_alloc_skb(xeth.iflinks[0], n);
	if (xeth_pr_is_err_val(skb))
		return;
	skb_put(skb, n);
	memcpy(skb->data, buf, n);
	xeth_pr_val("%zd", xeth.ops.side_band_rx(skb));
}

int xeth_sb_send_break(void)
{
	size_t n = sizeof(struct xeth_msg_break);
	struct xeth_sb_tx_entry *entry;

	xeth_sb_lock();
	if (!xeth_sb.connected)
		return xeth_sb_unlocked(0);
	entry = xeth_sb_tx_entry_alloc(n);
	if (!entry)
		return xeth_sb_unlocked(-ENOMEM);
	xeth_msg_set(&entry->data[0], XETH_MSG_KIND_BREAK);
	list_add_tail_rcu(&entry->list, &xeth_sb.tx);
	return xeth_sb_unlocked(0);
}

int xeth_sb_send_ethtool_flags(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	size_t n = sizeof(struct xeth_msg_ethtool_flags);
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ethtool_flags *msg;

	xeth_sb_lock();
	if (!xeth_sb.connected)
		return xeth_sb_unlocked(0);
	entry = xeth_sb_tx_entry_alloc(n);
	if (!entry)
		return xeth_sb_unlocked(-ENOMEM);
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_ETHTOOL_FLAGS, nd->name);
	msg = (struct xeth_msg_ethtool_flags *)&entry->data[0];
	msg->flags = priv->ethtool.flags;
	list_add_tail_rcu(&entry->list, &xeth_sb.tx);
	return xeth_sb_unlocked(0);
}

int xeth_sb_send_ethtool_settings(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	size_t n = sizeof(struct xeth_msg_ethtool_settings);
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ethtool_settings *msg;
	int i;

	xeth_sb_lock();
	if (!xeth_sb.connected)
		return xeth_sb_unlocked(0);
	entry = xeth_sb_tx_entry_alloc(n);
	if (!entry)
		return xeth_sb_unlocked(-ENOMEM);
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_ETHTOOL_SETTINGS,
		       nd->name);
	msg = (struct xeth_msg_ethtool_settings *)&entry->data[0];
	msg->speed = priv->ethtool.settings.base.speed;
	msg->duplex = priv->ethtool.settings.base.duplex;
	msg->port = priv->ethtool.settings.base.port;
	msg->phy_address = priv->ethtool.settings.base.phy_address;
	msg->autoneg = priv->ethtool.settings.base.autoneg;
	msg->mdio_support = priv->ethtool.settings.base.mdio_support;
	msg->eth_tp_mdix = priv->ethtool.settings.base.eth_tp_mdix;
	msg->eth_tp_mdix_ctrl = priv->ethtool.settings.base.eth_tp_mdix_ctrl;
	msg->link_mode_masks_nwords =
		sizeof(priv->ethtool.settings.link_modes.supported) /
		sizeof(unsigned long);
	for (i = 0; i < msg->link_mode_masks_nwords; i++)
		msg->link_modes_supported[i] =
			priv->ethtool.settings.link_modes.supported[i];
	for (i = 0; i < msg->link_mode_masks_nwords; i++)
		msg->link_modes_advertising[i] =
			priv->ethtool.settings.link_modes.advertising[i];
	for (i = 0; i < msg->link_mode_masks_nwords; i++)
		msg->link_modes_lp_advertising[i] =
			priv->ethtool.settings.link_modes.lp_advertising[i];
	list_add_tail_rcu(&entry->list, &xeth_sb.tx);
	return xeth_sb_unlocked(0);
}

int xeth_sb_send_ifa(struct net_device *nd, unsigned long event,
		     struct in_ifaddr *ifa)
{
	size_t n = sizeof(struct xeth_msg_ifa);
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ifa *msg;

	xeth_sb_lock();
	if (!xeth_sb.connected)
		return xeth_sb_unlocked(0);
	entry = xeth_sb_tx_entry_alloc(n);
	if (!entry)
		return xeth_sb_unlocked(-ENOMEM);
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_IFA, nd->name);
	msg = (struct xeth_msg_ifa *)&entry->data[0];
	msg->event = event;
	msg->address = ifa->ifa_address;
	msg->mask = ifa->ifa_mask;
	list_add_tail_rcu(&entry->list, &xeth_sb.tx);
	return xeth_sb_unlocked(0);
}

int xeth_sb_send_ifindex(struct net_device *nd)
{
	size_t n = sizeof(struct xeth_msg_ifindex);
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ifindex *msg;

	xeth_sb_lock();
	if (!xeth_sb.connected)
		return xeth_sb_unlocked(0);
	entry = xeth_sb_tx_entry_alloc(n);
	if (!entry)
		return xeth_sb_unlocked(-ENOMEM);
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_IFINDEX, nd->name);
	msg = (struct xeth_msg_ifindex *)&entry->data[0];
	msg->ifindex = (u64)nd->ifindex;
	msg->net = (u64)dev_net(nd);
	list_add_tail_rcu(&entry->list, &xeth_sb.tx);
	return xeth_sb_unlocked(0);
}

static inline bool xeth_sb_service_continue(int err)
{
	return !err && !kthread_should_stop() &&
		!signal_pending(xeth_sb_task_main);
}

// return < 0 if error; 1 if closed, and 0 othewise
static inline int xeth_sb_service_tx(struct socket *sock)
{
	int err = 0;
	LIST_HEAD(free);
	while (xeth_sb_service_continue(err)) {
		struct msghdr msg = {
			.msg_flags = MSG_DONTWAIT,
		};
		struct kvec iov;
		struct xeth_sb_tx_entry *entry = xeth_sb_tx_pop();
		int n;

		if (!entry)
			break;
		iov.iov_base = entry->data,
		iov.iov_len  = entry->data_len,
#if 0
		iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, &iov, 1, n);
#endif
		n = kernel_sendmsg(sock, &msg, &iov, 1, entry->data_len);
		if (n == -EAGAIN) {
			xeth_sb_tx_push(entry);
			break;
		}
		list_add(&entry->list, &free);
		if (n < 0)
			err = n;
		else if (n == 0)
			err = 1;
	}
	if (!list_empty(&free)) {
		synchronize_rcu();
		while (!list_empty(&free)) {
			struct list_head *next = free.next;
			struct xeth_sb_tx_entry *entry =
				container_of(next, struct xeth_sb_tx_entry,
					     list);
			list_del(next);
			kfree(entry);
		}
	}
	return err;
}

static inline int xeth_sb_dump_ifinfo(struct socket *sock,
				      struct net_device *nd)
{
	if (nd->ip_ptr) {
		struct in_ifaddr *ifa;
		for(ifa = nd->ip_ptr->ifa_list; ifa; ifa = ifa->ifa_next)
			xeth_sb_send_ifa(nd, NETDEV_UP, ifa);
	}
	xeth_sb_send_ifindex(nd);
	xeth_sb_send_ethtool_flags(nd);
	xeth_sb_send_ethtool_settings(nd);
	return xeth_sb_service_tx(sock);
}

static inline void xeth_sb_speed(const struct xeth_msg_speed *msg)
{
	struct net_device *nd = xeth_sb_nd_of(msg->ifname);
	struct xeth_priv *priv;

	if (!nd)
		return;
	priv = netdev_priv(nd);
	mutex_lock(&priv->ethtool.mutex);
	priv->ethtool.settings.base.speed = msg->mbps;
	mutex_unlock(&priv->ethtool.mutex);
	xeth_pr_nd(nd, "speed %uMb/s", msg->mbps);
}


// return < 0 if error, 1 if sock closed, and 0 othewise
static inline int xeth_sb_service_rx(struct socket *sock)
{
	struct xeth_msg *msg = (struct xeth_msg *)(xeth_sb_rxbuf);
	struct msghdr oob = {};
	struct kvec iov = {
		.iov_base = xeth_sb_rxbuf,
		.iov_len = XETH_SIZEOF_JUMBO_FRAME,
	};
	int ret = kernel_recvmsg(sock, &oob, &iov, 1, iov.iov_len, 0);
	if (ret == -EAGAIN)
		return 0;
	if (ret == 0)
		return 1;
	if (ret < 0)
		return xeth_pr_true_val("%d", ret);
	if (ret < sizeof(struct xeth_msg))
		return xeth_pr_true_val("%d", -EINVAL);
	if (!xeth_is_msg(msg)) {
		xeth_sb_exception_frame(xeth_sb_rxbuf, ret);
		return 0;
	}
	switch (msg->kind) {
	case XETH_MSG_KIND_CARRIER:
		xeth_sb_carrier((struct xeth_msg_carrier *)xeth_sb_rxbuf);
		break;
	case XETH_MSG_KIND_LINK_STAT:
		xeth_sb_link_stat((struct xeth_msg_stat *)xeth_sb_rxbuf);
		break;
	case XETH_MSG_KIND_ETHTOOL_STAT:
		xeth_sb_ethtool_stat((struct xeth_msg_stat *)xeth_sb_rxbuf);
		break;
	case XETH_MSG_KIND_DUMP_IFINFO: {
		int i, err = 0;

		for (i = 0; !err && i < xeth.n.ids; i++) {
			struct net_device *nd = xeth_nds(i);
			if (nd != NULL)
				err = xeth_sb_dump_ifinfo(sock, nd);
		}
		if (err)
			return err;
		xeth_sb_send_break();
	}	break;
	case XETH_MSG_KIND_SPEED:
		xeth_sb_speed((struct xeth_msg_speed *)xeth_sb_rxbuf);
		break;
	default:
		xeth_pr("invalid: %d", msg->kind);
		return -EINVAL;
	}
	return 0;
}

// return < 0 if error and 0 otherwise
static inline int xeth_sb_service(struct socket *sock)
{
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 100000,
	};
	int err = xeth_pr_true_val("%d",
				   kernel_setsockopt(sock, SOL_SOCKET,
						     SO_RCVTIMEO,
						     (char *)&tv,
						     sizeof(struct timeval)));
	if (err < 0)
		return err;
	xeth_sb_connection(true);
	while (xeth_sb_service_continue(err)) {
		err = xeth_sb_service_tx(sock);
		err = err ? err : xeth_sb_service_rx(sock);
	}
	xeth_sb_connection(false);
	sock_release(sock);
	if (err > 0) {
		xeth_pr("closed");
		err = 0;
	}
	return err;
}

static int xeth_sb_main(void *data)
{
	const int backlog = 3;
	struct sockaddr_un addr;
	struct socket *ln = NULL;
	struct sockaddr *paddr = (struct sockaddr *)&addr;
	int n, err;
	
	xeth_pr("start");
	// set_current_state(TASK_INTERRUPTIBLE);
	err = xeth_pr_true_val("%d",
			       sock_create_kern(current->nsproxy->net_ns,
						AF_UNIX, SOCK_SEQPACKET, 0,
						&ln));
	if (err)
		goto xeth_sb_main_egress;
	SOCK_INODE(ln)->i_mode &= ~(S_IRWXG | S_IRWXO);
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	/* Note: This is an abstract namespace w/ addr.sun_path[0] == 0 */
	n = sizeof(sa_family_t) + 1 +
		scnprintf(addr.sun_path+1, UNIX_PATH_MAX-1,
			  "%s/xeth", xeth.ops.rtnl.kind);
	xeth_pr("@%s: listen", addr.sun_path+1);
	err = xeth_pr_true_val("%d", kernel_bind(ln, paddr, n));
	if (err)
		goto xeth_sb_main_egress;
	err = xeth_pr_true_val("%d", kernel_listen(ln, backlog));
	if (err)
		goto xeth_sb_main_egress;
	allow_signal(SIGKILL);
	while (xeth_sb_service_continue(err)) {
		struct socket *conn;
		err = kernel_accept(ln, &conn, O_NONBLOCK);
		if (err == -EAGAIN) {
			err = 0;
			msleep_interruptible(100);
			schedule();
			continue;
		}
		if (!err) {
			xeth_pr("@%s: connected", addr.sun_path+1);
			xeth_foreach_nd(xeth_sb_reset_nd_stats);
			err = xeth_pr_true_val("%d", xeth_sb_service(conn));
			xeth_pr("@%s: disconnected", addr.sun_path+1);
		}
	}
xeth_sb_main_egress:
	if (err)
		xeth_pr("@%s: err %d", addr.sun_path+1, err);
	if (ln)
		sock_release(ln);
	xeth_pr("finished");
	return err;
}

int xeth_sb_init(void)
{
	xeth_sb.connected = false;
	INIT_LIST_HEAD(&xeth_sb.tx);
	xeth_sb_rxbuf = kmalloc(XETH_SIZEOF_JUMBO_FRAME, GFP_KERNEL);
	if (!xeth_sb_rxbuf)
		return -ENOMEM;
	xeth_sb_task_main = kthread_run(xeth_sb_main, NULL, xeth.ops.rtnl.kind);
	return xeth_pr_is_err_val(xeth_sb_task_main);
}

void xeth_sb_exit(void)
{
	if (xeth_sb_task_main) {
		kthread_stop(xeth_sb_task_main);
	}
	if (xeth_sb_rxbuf) {
		kfree(xeth_sb_rxbuf);
		xeth_sb_rxbuf = NULL;
	}
}
