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
#include <net/sock.h>
#include <uapi/linux/un.h>
#include <uapi/linux/xeth.h>

static char *xeth_sb_rxbuf;

static bool xeth_sb_is_hdr(struct xeth_sb_hdr *p)
{
	return	p->z64 == 0 && p->z32 == 0 && p->z16 == 0 && p->z8 == 0;
}

static struct net_device *xeth_sb_find_nd(u64 ifindex)
{
	struct net_device *nd;
	rtnl_lock();
	nd = xeth_find_nd(ifindex);
	rtnl_unlock();
	if (!nd) {
		xeth_pr("invalid ifindex: %llu", ifindex);
	}
	return nd;
}

static void xeth_sb_set_net_stat(struct xeth_sb_set_stat *sbsetstat)
{
	struct net_device *nd = xeth_sb_find_nd(sbsetstat->ifindex);
	struct xeth_priv *priv;
	u64 *stat;
	const size_t nstats = sizeof(struct rtnl_link_stats64)/sizeof(u64);

	if (!nd)
		return;
	if (sbsetstat->statindex >= nstats) {
		xeth_pr("invalid net stat index: %llu", sbsetstat->statindex);
		return;
	}
	priv = netdev_priv(nd);
	stat = (u64*)&priv->link_stats + sbsetstat->statindex;
	mutex_lock(&priv->link_mutex);
	*stat = sbsetstat->count;
	mutex_unlock(&priv->link_mutex);
}

static void xeth_sb_set_ethtool_stat(struct xeth_sb_set_stat *sbsetstat)
{
	struct net_device *nd = xeth_sb_find_nd(sbsetstat->ifindex);
	struct xeth_priv *priv;

	if (!nd)
		return;
	if (sbsetstat->statindex >= xeth.n.ethtool_stats) {
		xeth_pr("invalid ethtool stat index: %llu",
			sbsetstat->statindex);
		return;
	}
	priv = netdev_priv(nd);
	mutex_lock(&priv->link_mutex);
	priv->ethtool_stats[sbsetstat->statindex] = sbsetstat->count;
	mutex_unlock(&priv->link_mutex);
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

static int xeth_sb_rx(struct socket *sock)
{
	struct xeth_sb_hdr *sbhdr = (struct xeth_sb_hdr *)(xeth_sb_rxbuf);
	struct xeth_sb_set_stat *sbsetstat =
		(struct xeth_sb_set_stat *)(xeth_sb_rxbuf+
					    sizeof(struct xeth_sb_hdr));
	struct kvec iov = {
		.iov_base = xeth_sb_rxbuf,
		.iov_len = XETH_SIZEOF_MAX_JUMBO_FRAME,
	};
	int err = 0;

	xeth_assign_sb(sock);
	while (!kthread_should_stop()) {
		struct msghdr msg = {};
		int n = kernel_recvmsg(sock, &msg, &iov, 1, iov.iov_len,
				       MSG_DONTWAIT);
		if (kthread_should_stop())
			break;
		if (n == -EAGAIN) {
			schedule();
		} else if (n < sizeof(struct xeth_sb_hdr)) {
			err = n;
			break;
		} else if (xeth_sb_is_hdr(sbhdr)) {
			switch (sbhdr->op) {
			case XETH_SBOP_SET_NET_STAT:
				xeth_sb_set_net_stat(sbsetstat);
				break;
			case XETH_SBOP_SET_ETHTOOL_STAT:
				xeth_sb_set_ethtool_stat(sbsetstat);
				break;
			default:
				xeth_pr("invalid op code: %d", sbhdr->op);
			}
		} else {
			xeth_sb_exception_frame(xeth_sb_rxbuf, n);
		}
	}
	xeth_assign_sb(NULL);
	sock_release(sock);
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
			  "%s.xeth", xeth.ops.rtnl.kind);
	xeth_pr("@%s: listen", addr.sun_path+1);
	err = xeth_pr_true_val("%d", kernel_bind(ln, paddr, n));
	if (err)
		goto xeth_sb_main_egress;
	err = xeth_pr_true_val("%d", kernel_listen(ln, backlog));
	if (err)
		goto xeth_sb_main_egress;
	while (!err) {
		struct socket *conn = NULL;
		err = xeth_pr_true_val("%d",
				       sock_create_lite(ln->sk->sk_family,
							ln->sk->sk_type,
							ln->sk->sk_protocol,
							&conn));
		if (err)
			break;
		conn->ops = ln->ops;
		__module_get(conn->ops->owner);
		while (true) {
			if (kthread_should_stop()) {
				sock_release(conn);
				goto xeth_sb_main_egress;
			}
			err = conn->ops->accept(ln, conn, O_NONBLOCK, true);
			if (err == -EWOULDBLOCK) {
				err = 0;
				schedule();
				continue;
			}
			break;
		}
		if (!err) {
			xeth_pr("@%s: connected", addr.sun_path+1);
			err = xeth_pr_true_val("%d", xeth_sb_rx(conn));
			xeth_pr("@%s: disconnected", addr.sun_path+1);
		}
	}
xeth_sb_main_egress:
	if (ln)
		sock_release(ln);
	xeth_pr("finished");
	return err;
}

static struct task_struct *xeth_sb_task_main;

int xeth_sb_init(void)
{
	xeth_sb_rxbuf = kmalloc(XETH_SIZEOF_MAX_JUMBO_FRAME, GFP_KERNEL);
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
