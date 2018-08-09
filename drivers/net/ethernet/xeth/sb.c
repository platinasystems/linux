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

struct xeth_sb_tx_entry {
	size_t	data_len;
	struct	list_head	list;
	unsigned char		data[];
};

static struct list_head __rcu xeth_sb_tx;

static u64 xeth_sb_ns_inum(struct net_device *nd)
{
	struct net *ndnet = dev_net(nd);
	return net_eq(ndnet, &init_net) ? 1 : ndnet->ns.inum;
}

static inline struct xeth_sb_tx_entry *xeth_sb_alloc(size_t data_len)
{
	size_t n = sizeof(struct xeth_sb_tx_entry) + data_len;
	struct xeth_sb_tx_entry *entry = kzalloc(n, GFP_KERNEL);
	if (entry)
		entry->data_len = data_len;
	return entry;
}

static void xeth_sb_free(struct list_head *entries)
{
	while (!list_empty(entries)) {
		struct list_head *next = entries->next;
		struct xeth_sb_tx_entry *entry =
			container_of(next, struct xeth_sb_tx_entry, list);
			list_del(next);
			kfree(entry);
	}
}

static struct xeth_sb_tx_entry *xeth_sb_tx_pop(void)
{
	struct xeth_sb_tx_entry *entry;
	rcu_read_lock();
	entry = list_first_or_null_rcu(&xeth_sb_tx,
				       struct xeth_sb_tx_entry,
				       list);
	if (entry)
		list_del_rcu(&entry->list);
	rcu_read_unlock();
	return entry;
}

static void xeth_sb_tx_push(struct xeth_sb_tx_entry *entry)
{
	rcu_read_lock();
	list_add_rcu(&entry->list, &xeth_sb_tx);
	rcu_read_unlock();
}

static void xeth_sb_tx_queue_rcu(struct xeth_sb_tx_entry *entry)
{
	list_add_tail_rcu(&entry->list, &xeth_sb_tx);
}

static void xeth_sb_tx_flush(void)
{
	LIST_HEAD(free_list);
	rcu_read_lock();
	while (true) {
		struct xeth_sb_tx_entry *entry =
			list_first_or_null_rcu(&xeth_sb_tx,
					       struct xeth_sb_tx_entry,
					       list);
		if (!entry)
			break;
		list_del_rcu(&entry->list);
		list_add(&entry->list, &free_list);
	}
	rcu_read_unlock();
	synchronize_rcu();
	xeth_sb_free(&free_list);
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
	int err;
	char ifname[IFNAMSIZ];
	struct net_device *nd;
	struct xeth_priv priv;

	strlcpy(ifname, msg_ifname, IFNAMSIZ);
	err = xeth.ops.parse(ifname, &priv);
	if (err) {
		xeth_pr("\"%s\" invalid", ifname);
		return NULL;
	}
	nd = to_xeth_nd(priv.id);
	if (!nd) {
		nd = dev_get_by_name(&init_net, msg_ifname);
		if (!nd) {
			xeth_pr("\"%s\" no such device", ifname);
		 } else if (nd->netdev_ops != &xeth.ops.ndo) {
			xeth_pr("\"%s\" not an xeth device", ifname);
			dev_put(nd);
			nd = NULL;
		 }
	}
	return nd;
}

static void xeth_sb_nd_put(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	if (priv->ndi < 0)
		dev_put(nd);
}

static void xeth_sb_carrier(const struct xeth_msg_carrier *msg)
{
	struct net_device *nd = xeth_sb_nd_of(msg->ifname);
	struct xeth_priv *priv;
	if (!nd) {
		xeth_count_inc(sb_no_dev);
		return;
	}
	priv = netdev_priv(nd);
	switch (msg->flag) {
	case XETH_CARRIER_ON:
		netif_carrier_on(nd);
		xeth_count_priv_set(priv, sb_carrier, 1);
		break;
	case XETH_CARRIER_OFF:
		netif_carrier_off(nd);
		xeth_count_priv_set(priv, sb_carrier, 0);
		break;
	}
	xeth_sb_nd_put(nd);
}

static void xeth_sb_link_stat(const struct xeth_msg_stat *msg)
{
	struct net_device *nd;
	struct xeth_priv *priv;
	u64 *stat;

	if (msg->index >= xeth_sb_n_link_stats) {
		xeth_count_inc(sb_invalid);
		return;
	}
	nd = xeth_sb_nd_of(msg->ifname);
	if (!nd) {
		xeth_count_inc(sb_no_dev);
		return;
	}
	priv = netdev_priv(nd);
	stat = (u64*)&priv->link.stats + msg->index;
	mutex_lock(&priv->link.mutex);
	*stat = msg->count;
	mutex_unlock(&priv->link.mutex);
	xeth_count_priv_inc(priv, sb_link_stats);
	xeth_sb_nd_put(nd);
}

static void xeth_sb_ethtool_stat(const struct xeth_msg_stat *msg)
{
	struct net_device *nd;
	struct xeth_priv *priv;

	if (msg->index >= xeth.n.ethtool.stats) {
		xeth_count_inc(sb_invalid);
		return;
	}
	nd = xeth_sb_nd_of(msg->ifname);
	if (!nd) {
		xeth_count_inc(sb_no_dev);
		return;
	}
	priv = netdev_priv(nd);
	mutex_lock(&priv->ethtool.mutex);
	priv->ethtool.stats[msg->index] = msg->count;
	mutex_unlock(&priv->ethtool.mutex);
	xeth_count_priv_inc(priv, sb_ethtool_stats);
	xeth_sb_nd_put(nd);
}

int xeth_sb_send_break(void)
{
	struct xeth_sb_tx_entry *entry;
	size_t n = sizeof(struct xeth_msg_break);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry =  xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_msg_set(&entry->data[0], XETH_MSG_KIND_BREAK);
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_ethtool_flags(struct net_device *nd)
{
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ethtool_flags *msg;
	struct xeth_priv *priv = netdev_priv(nd);
	size_t n = sizeof(struct xeth_msg_ethtool_flags);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry =  xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_ETHTOOL_FLAGS, nd->name);
	msg = (struct xeth_msg_ethtool_flags *)&entry->data[0];
	msg->flags = priv->ethtool.flags;
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_ethtool_settings(struct net_device *nd)
{
	int i;
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ethtool_settings *msg;
	struct xeth_priv *priv = netdev_priv(nd);
	size_t n = sizeof(struct xeth_msg_ethtool_settings);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
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
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_ifa(struct net_device *nd, unsigned long event,
		     struct in_ifaddr *ifa)
{
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ifa *msg;
	size_t n = sizeof(struct xeth_msg_ifa);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_IFA, nd->name);
	msg = (struct xeth_msg_ifa *)&entry->data[0];
	msg->event = event;
	msg->address = ifa->ifa_address;
	msg->mask = ifa->ifa_mask;
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_ifdel(struct net_device *nd)
{
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ifdel *msg;
	struct xeth_priv *priv = netdev_priv(nd);
	size_t n = sizeof(struct xeth_msg_ifdel);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_IFINFO, nd->name);
	msg = (struct xeth_msg_ifdel *)&entry->data[0];
	msg->ifindex = nd->ifindex;
	msg->devtype = priv->devtype;
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_ifinfo(struct net_device *nd, unsigned int modiff)
{
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ifinfo *msg;
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_iflink(priv->iflinki);
	size_t n = sizeof(struct xeth_msg_ifinfo);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_IFINFO, nd->name);
	msg = (struct xeth_msg_ifinfo *)&entry->data[0];
	msg->net = xeth_sb_ns_inum(nd);
	msg->ifindex = nd->ifindex;
	msg->iflinkindex = iflink->ifindex;
	msg->flags = modiff ? modiff : nd->flags;
	msg->portid = priv->portid;
	msg->id = priv->id;
	memcpy(msg->addr, nd->dev_addr, ETH_ALEN);
	msg->portindex = priv->porti;
	msg->subportindex = priv->subporti;
	msg->devtype = priv->devtype;
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

static int xeth_sb_send_ifvid(struct net_device *nd, u8 op, u16 vid)
{
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_ifvid *msg;
	size_t n = sizeof(struct xeth_msg_ifvid);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_IFVID, nd->name);
	msg = (struct xeth_msg_ifvid *)&entry->data[0];
	msg->net = xeth_sb_ns_inum(nd);
	msg->ifindex = nd->ifindex;
	msg->vid = vid;
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_if_add_vid(struct net_device *nd, u16 vid)
{
	return xeth_sb_send_ifvid(nd, XETH_IFVID_ADD, vid);
}

int xeth_sb_send_if_del_vid(struct net_device *nd, u16 vid)
{
	return xeth_sb_send_ifvid(nd, XETH_IFVID_DEL, vid);
}

int xeth_sb_send_fibentry(unsigned long event,
			  struct fib_entry_notifier_info *info)
{
	int i;
	struct xeth_sb_tx_entry *entry;
	struct xeth_next_hop *nh;
	struct xeth_msg_fibentry *msg;
	size_t n = sizeof(struct xeth_msg_fibentry) +
		(info->fi->fib_nhs * sizeof(struct xeth_next_hop));

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	msg = (struct xeth_msg_fibentry *)&entry->data[0];
	nh = (struct xeth_next_hop*)&msg->nh[0];
	xeth_msg_set(&entry->data[0], XETH_MSG_KIND_FIBENTRY);
	msg->net = net_eq(info->info.net, &init_net) ? 1 :
		info->info.net->ns.inum;
	msg->address = htonl(info->dst);
	msg->mask = inet_make_mask(info->dst_len);
	msg->event = (u8)event;
	msg->nhs = info->fi->fib_nhs;
	msg->tos = info->tos;
	msg->type = info->type;
	msg->tb_id = info->tb_id;
	for(i = 0; i < msg->nhs; i++) {
		nh[i].ifindex = info->fi->fib_nh[i].nh_dev->ifindex;
		nh[i].weight = info->fi->fib_nh[i].nh_weight;
		nh[i].flags = info->fi->fib_nh[i].nh_flags;
		nh[i].gw = info->fi->fib_nh[i].nh_gw;
		nh[i].scope = info->fi->fib_nh[i].nh_scope;
	}
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

int xeth_sb_send_neigh_update(struct neighbour *neigh)
{
	struct xeth_sb_tx_entry *entry;
	struct xeth_msg_neigh_update *msg;
	size_t n = sizeof(struct xeth_msg_neigh_update);

	if (xeth_count(sb_connections) == 0)
		return 0;
	entry = xeth_sb_alloc(n);
	if (!entry)
		return -ENOMEM;
	xeth_ifmsg_set(&entry->data[0], XETH_MSG_KIND_NEIGH_UPDATE,
		       neigh->dev->name);
	msg = (struct xeth_msg_neigh_update *)&entry->data[0];
	msg->net = xeth_sb_ns_inum(neigh->dev);
	msg->ifindex = neigh->dev->ifindex;
	msg->family = neigh->ops->family;
	msg->len = neigh->tbl->key_len;
	memcpy(msg->dst, neigh->primary_key, neigh->tbl->key_len);
	read_lock_bh(&neigh->lock);
	if (neigh->nud_state & NUD_VALID) {
		char ha[MAX_ADDR_LEN];
		neigh_ha_snapshot(ha, neigh, neigh->dev);
		memcpy(&msg->lladdr[0], ha, ETH_ALEN);
	}
	read_unlock_bh(&neigh->lock);
	xeth_sb_tx_queue_rcu(entry);
	return 0;
}

static inline bool xeth_sb_service_continue(int err)
{
	return !err && !kthread_should_stop() && !signal_pending(xeth.sb.main);
}

// return < 0 if error; 1 if closed, and 0 othewise
static inline int xeth_sb_service_tx(struct socket *sock)
{
	int err = 0;
	LIST_HEAD(sent_list);
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
		list_add(&entry->list, &sent_list);
		if (n < 0)
			err = n;
		else if (n == 0)
			err = 1;
	}
	synchronize_rcu();
	xeth_sb_free(&sent_list);
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
	xeth_sb_send_ifinfo(nd, 0);
	xeth_ndo_send_vids(nd);
	xeth_sb_send_ethtool_flags(nd);
	xeth_sb_send_ethtool_settings(nd);
	return xeth_sb_service_tx(sock);
}

static inline void xeth_sb_speed(const struct xeth_msg_speed *msg)
{
	struct net_device *nd = xeth_sb_nd_of(msg->ifname);
	struct xeth_priv *priv;

	if (!nd) {
		xeth_count_inc(sb_no_dev);
		return;
	}
	priv = netdev_priv(nd);
	mutex_lock(&priv->ethtool.mutex);
	priv->ethtool.settings.base.speed = msg->mbps;
	mutex_unlock(&priv->ethtool.mutex);
	xeth_sb_nd_put(nd);
}


// return < 0 if error, 1 if sock closed, and 0 othewise
static inline int xeth_sb_service_rx(struct socket *sock)
{
	struct xeth_msg *msg = (struct xeth_msg *)(xeth.sb.rxbuf);
	struct msghdr oob = {};
	struct kvec iov = {
		.iov_base = xeth.sb.rxbuf,
		.iov_len = XETH_SIZEOF_JUMBO_FRAME,
	};
	int ret = kernel_recvmsg(sock, &oob, &iov, 1, iov.iov_len, 0);
	if (ret == -EAGAIN)
		return 0;
	if (ret == 0)
		return 1;
	if (ret < 0)
		return ret;
	if (ret < sizeof(struct xeth_msg))
		return -EINVAL;
	if (!xeth_is_msg(msg)) {
		xeth.ops.side_band_rx(xeth.sb.rxbuf, ret);
		return 0;
	}
	switch (msg->kind) {
	case XETH_MSG_KIND_CARRIER:
		xeth_sb_carrier((struct xeth_msg_carrier *)xeth.sb.rxbuf);
		break;
	case XETH_MSG_KIND_LINK_STAT:
		xeth_sb_link_stat((struct xeth_msg_stat *)xeth.sb.rxbuf);
		break;
	case XETH_MSG_KIND_ETHTOOL_STAT:
		xeth_sb_ethtool_stat((struct xeth_msg_stat *)xeth.sb.rxbuf);
		break;
	case XETH_MSG_KIND_DUMP_IFINFO: {
		int err = 0;
		struct xeth_priv *priv;
		list_for_each_entry_rcu(priv, &xeth.list, list) {
			if (!err)
				err = xeth_sb_dump_ifinfo(sock, priv->nd);
		}
		xeth_sb_send_break();
		if (err)
			return err;
	}	break;
	case XETH_MSG_KIND_SPEED:
		xeth_sb_speed((struct xeth_msg_speed *)xeth.sb.rxbuf);
		break;
	case XETH_MSG_KIND_DUMP_FIBINFO: {
		int err = xeth_pr_err(xeth_notifier_register_fib());
		xeth_sb_send_break();
		if (err)
			return err;
	}	break;
	default:
		xeth_count_inc(sb_invalid);
		return -EINVAL;
	}
	return 0;
}

static inline void xeth_sb_service(struct socket *sock)
{
	int err;
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 100000,
	};

	err = xeth_pr_err(kernel_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
					    (char *)&tv,
					    sizeof(struct timeval)));
	if (err < 0)
		return;
	xeth_count_inc(sb_connections);
	while (xeth_sb_service_continue(err)) {
		err = xeth_sb_service_tx(sock);
		err = err ? err : xeth_sb_service_rx(sock);
	}
	xeth_count_dec(sb_connections);
	sock_release(sock);
	xeth_notifier_unregister_fib();
	xeth_foreach_nd(netif_carrier_off);
}

static int xeth_sb_main(void *data)
{
	const int backlog = 3;
	struct sockaddr_un addr;
	struct socket *ln = NULL;
	struct sockaddr *paddr = (struct sockaddr *)&addr;
	int n, err;

	no_xeth_pr("start");
	// set_current_state(TASK_INTERRUPTIBLE);
	err = xeth_pr_err(sock_create_kern(current->nsproxy->net_ns, AF_UNIX,
					   SOCK_SEQPACKET, 0, &ln));
	if (err)
		goto xeth_sb_main_egress;
	SOCK_INODE(ln)->i_mode &= ~(S_IRWXG | S_IRWXO);
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	/* Note: This is an abstract namespace w/ addr.sun_path[0] == 0 */
	n = sizeof(sa_family_t) + 1 +
		scnprintf(addr.sun_path+1, UNIX_PATH_MAX-1,
			  "%s", xeth.ops.rtnl.kind);
	no_xeth_pr("@%s: listen", addr.sun_path+1);
	err = xeth_pr_err(kernel_bind(ln, paddr, n));
	if (err)
		goto xeth_sb_main_egress;
	err = xeth_pr_err(kernel_listen(ln, backlog));
	if (err)
		goto xeth_sb_main_egress;
	allow_signal(SIGKILL);
	while (xeth_sb_service_continue(err)) {
		struct socket *conn;
		xeth_sb_tx_flush();
		err = kernel_accept(ln, &conn, O_NONBLOCK);
		if (err == -EAGAIN) {
			err = 0;
			msleep_interruptible(100);
			schedule();
			continue;
		}
		if (!err) {
			xeth_foreach_nd(xeth_sb_reset_nd_stats);
			no_xeth_pr("@%s: connected", addr.sun_path+1);
			xeth_sb_service(conn);
			no_xeth_pr("@%s: disconnected", addr.sun_path+1);
		}
	}
xeth_sb_main_egress:
	if (err)
		xeth_pr("@%s: err %d", addr.sun_path+1, err);
	if (ln)
		sock_release(ln);
	no_xeth_pr("finished");
	xeth.sb.main = NULL;
	return err;
}

int xeth_sb_init(void)
{
	INIT_LIST_HEAD_RCU(&xeth_sb_tx);
	xeth.sb.rxbuf = kmalloc(XETH_SIZEOF_JUMBO_FRAME, GFP_KERNEL);
	if (!xeth.sb.rxbuf)
		return -ENOMEM;
	xeth.sb.main = kthread_run(xeth_sb_main, NULL, xeth.ops.rtnl.kind);
	return xeth_pr_is_err_val(xeth.sb.main);
}

void xeth_sb_exit(void)
{
	if (xeth.sb.main) {
		kthread_stop(xeth.sb.main);
		while (xeth.sb.main) ;
	}
	if (xeth.sb.rxbuf) {
		kfree(xeth.sb.rxbuf);
		xeth.sb.rxbuf = NULL;
	}
}
