/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

static void *xeth_sbrx_buf;

static bool xeth_sbrx_is_msg(void *data)
{
	struct xeth_msg *msg = data;

	return	msg->header.z64 == 0 &&
		msg->header.z32 == 0 &&
		msg->header.z16 == 0;
}

static enum xeth_msg_kind xeth_sbrx_msg_kind(void *data)
{
	struct xeth_msg *msg = data;

	return msg->header.kind;
}

static int xeth_sbrx_msg_version_match(void *data)
{
	struct xeth_msg *msg = data;

	return msg->header.version == XETH_MSG_VERSION ? 0 : -EINVAL;
}

static void xeth_sbrx_carrier(struct xeth_msg_carrier *msg)
{
	struct net_device *nd;
	
	nd = xeth_debug_rcu(xeth_upper_lookup_rcu(msg->xid));
	if (!nd)
		xeth_counter_inc(sbrx_no_dev);
	else
		switch (msg->flag) {
		case XETH_CARRIER_ON:
			netif_carrier_on(nd);
			netdev_notify_peers(nd);
			break;
		case XETH_CARRIER_OFF:
			netif_carrier_off(nd);
			break;
		default:
			xeth_counter_inc(sbrx_invalid);
		}
}

static void xeth_sbrx_ethtool_stat(struct xeth_msg_stat *msg)
{
	struct net_device *nd;
	
	nd = xeth_debug_rcu(xeth_upper_lookup_rcu(msg->xid));
	if (!nd)
		xeth_counter_inc(sbrx_no_dev);
	else
		xeth_upper_ethtool_stat(nd, msg->index, msg->count);
}

static void xeth_sbrx_link_stat(struct xeth_msg_stat *msg)
{
	struct net_device *nd;
	
	nd = xeth_debug_rcu(xeth_upper_lookup_rcu(msg->xid));
	if (!nd)
		xeth_counter_inc(sbrx_no_dev);
	else
		xeth_upper_link_stat(nd, msg->index, msg->count);
}

static void xeth_sbrx_speed(struct xeth_msg_speed *msg)
{
	struct net_device *nd;
	
	nd = xeth_debug_rcu(xeth_upper_lookup_rcu(msg->xid));
	if (!nd)
		xeth_counter_inc(sbrx_no_dev);
	else
		xeth_upper_speed(nd, msg->mbps);
}

static int xeth_sbrx_exception_vlan(const char *buf, size_t n)
{
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)buf;
	__be16 h_vlan_proto = veh->h_vlan_proto;
	__be16 h_vlan_TCI = veh->h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto = veh->h_vlan_encapsulated_proto;
	struct sk_buff *skb = netdev_alloc_skb(xeth_mux, n);
	if (!skb) {
		xeth_counter_inc(sbrx_no_mem);
		return xeth_debug_err(-ENOMEM);
	}
	skb_put(skb, n);
	memcpy(skb->data, buf, n);
	eth_type_trans(skb, xeth_mux);
	skb->vlan_proto = h_vlan_proto;
	skb->vlan_tci = VLAN_TAG_PRESENT | be16_to_cpu(h_vlan_TCI);
	skb->protocol = h_vlan_encapsulated_proto;
	skb_pull_inline(skb, VLAN_HLEN);
	xeth_mux_demux(&skb);
	return 0;
}

static int xeth_sbrx_exception(const char *buf, size_t n)
{
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)buf;

	if (eth_type_vlan(veh->h_vlan_proto)) {
		return xeth_sbrx_exception_vlan(buf, n);
	}
	xeth_counter_inc(sbex_invalid);
	return xeth_debug_err(-EINVAL);
}


// return < 0 if error, 1 if sock closed, and 0 othewise
static int xeth_sbrx_service(struct socket *conn)
{
	struct xeth_msg_header *msg = xeth_sbrx_buf;
	struct msghdr oob = {};
	struct kvec iov = {
		.iov_base = xeth_sbrx_buf,
		.iov_len = XETH_SIZEOF_JUMBO_FRAME,
	};
	int n, err;

	xeth_counter_inc(sbrx_ticks);
	n = kernel_recvmsg(conn, &oob, &iov, 1, iov.iov_len, 0);
	if (n == -EAGAIN) {
		schedule();
		return 0;
	}
	if (n == 0 || n == -ECONNRESET)
		return 1;
	if (n < 0)
		return n;
	xeth_counter_inc(sbrx_msgs);
	if (n < sizeof(*msg))
		return -EINVAL;
	if (!xeth_sbrx_is_msg(msg)) {
		return xeth_sbrx_exception(xeth_sbrx_buf, n);
	}
	err = xeth_debug_err(xeth_sbrx_msg_version_match(msg));
	if (err)
		return err;
	switch (xeth_sbrx_msg_kind(msg)) {
	case XETH_MSG_KIND_CARRIER:
		xeth_sbrx_carrier(xeth_sbrx_buf);
		break;
	case XETH_MSG_KIND_ETHTOOL_STAT:
		xeth_sbrx_ethtool_stat(xeth_sbrx_buf);
		break;
	case XETH_MSG_KIND_LINK_STAT:
		xeth_sbrx_link_stat(xeth_sbrx_buf);
		break;
	case XETH_MSG_KIND_DUMP_IFINFO:
		xeth_upper_all_dump_ifinfo();
		xeth_sbtx_break();
		if (xeth_debug_err(xeth_nb_start_netdevice()) == 0) {
			xeth_debug_err(xeth_nb_start_inetaddr());
		}
		break;
	case XETH_MSG_KIND_SPEED:
		xeth_sbrx_speed(xeth_sbrx_buf);
		break;
	case XETH_MSG_KIND_DUMP_FIBINFO:
		if (xeth_nb_start_fib() == 0) {
			xeth_sbtx_break();
			xeth_nb_start_netevent();
		}
		break;
	default:
		xeth_counter_inc(sbrx_invalid);
		err = -EINVAL;
	}
	return err;
}

static int xeth_sbrx_task(void *data)
{
	struct socket *conn = data;
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 10000,
	};
	int err;
	
	err = xeth_debug_err(kernel_setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
					       (char *)&tv, sizeof(tv)));
	if (err)
		return err;
	allow_signal(SIGKILL);
	while (!err && !kthread_should_stop() && !signal_pending(current))
		err = xeth_sbrx_service(conn);
	xeth_nb_stop_netevent();
	xeth_nb_stop_fib();
	xeth_nb_stop_inetaddr();
	xeth_nb_stop_netdevice();
	xeth_flag_clear(sbrx_task);
	if (err < 0)
		xeth_debug("exit with %d",  err);
	return err;
}

struct task_struct *xeth_sbrx_fork(struct socket *conn)
{
	struct task_struct *t;

	xeth_flag_set(sbrx_task);
	t = xeth_debug_ptr_err(kthread_run(xeth_sbrx_task, conn,
					   "%s-rx", xeth_name));
	if (IS_ERR(t)) {
		xeth_flag_clear(sbrx_task);
		return NULL;
	}
	return t;
}

__init int xeth_sbrx_init(void)
{
	xeth_sbrx_buf = xeth_debug_ptr_err(kzalloc(XETH_SIZEOF_JUMBO_FRAME,
						   GFP_KERNEL));
	return IS_ERR(xeth_sbrx_buf) ? PTR_ERR(xeth_sbrx_buf) : 0;
}

int xeth_sbrx_deinit(int err)
{
	if (!IS_ERR(xeth_sbrx_buf))
		kfree(xeth_sbrx_buf);
	return err;
}
