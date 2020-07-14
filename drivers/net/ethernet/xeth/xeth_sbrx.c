/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

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

static void xeth_sbrx_carrier(struct xeth_platform_priv *xpp)
{
	struct xeth_msg_carrier *msg = xpp->sb.rx.buf;
	struct net_device *upper = xeth_upper_lookup_rcu(xpp, msg->xid);
	if (upper)
		switch (msg->flag) {
		case XETH_CARRIER_ON:
			netif_carrier_on(upper);
			netdev_notify_peers(upper);
			break;
		case XETH_CARRIER_OFF:
			netif_carrier_off(upper);
			break;
		default:
			xeth_counter_inc(xpp, sbrx_invalid);
		}
}

static void xeth_sbrx_et_stat(struct xeth_platform_priv *xpp)
{
	struct xeth_msg_stat *msg = xpp->sb.rx.buf;
	struct net_device *upper = xeth_upper_lookup_rcu(xpp, msg->xid);
	if (upper)
		xeth_upper_et_stat(upper, msg->index, msg->count);
}

static void xeth_sbrx_link_stat(struct xeth_platform_priv *xpp)
{
	struct xeth_msg_stat *msg = xpp->sb.rx.buf;
	struct net_device *upper = xeth_upper_lookup_rcu(xpp, msg->xid);
	if (upper)
		xeth_upper_link_stat(upper, msg->index, msg->count);
}

static void xeth_sbrx_speed(struct xeth_platform_priv *xpp)
{
	struct xeth_msg_speed *msg = xpp->sb.rx.buf;
	struct net_device *upper = xeth_upper_lookup_rcu(xpp, msg->xid);
	if (upper)
		xeth_upper_speed(upper, msg->mbps);
}

// return < 0 if error, 1 if sock closed, and 0 othewise
static int xeth_sbrx_service(struct xeth_platform_priv *xpp)
{
	struct xeth_msg_header *msg = xpp->sb.rx.buf;
	struct msghdr oob = {};
	struct kvec iov = {
		.iov_base = xpp->sb.rx.buf,
		.iov_len = XETH_SIZEOF_JUMBO_FRAME,
	};
	int n, err;

	xeth_counter_inc(xpp, sbrx_ticks);
	n = kernel_recvmsg(xpp->sb.conn, &oob, &iov, 1, iov.iov_len, 0);
	if (n == -EAGAIN) {
		schedule();
		return 0;
	}
	if (n == 0 || n == -ECONNRESET)
		return 1;
	if (n < 0)
		return n;
	xeth_counter_inc(xpp, sbrx_msgs);
	if (n < sizeof(*msg) || !xeth_sbrx_is_msg(msg))
		return -EINVAL;
	err = xeth_debug_err(xeth_sbrx_msg_version_match(msg));
	if (err)
		return err;
	switch (xeth_sbrx_msg_kind(msg)) {
	case XETH_MSG_KIND_CARRIER:
		xeth_sbrx_carrier(xpp);
		break;
	case XETH_MSG_KIND_ETHTOOL_STAT:
		xeth_sbrx_et_stat(xpp);
		break;
	case XETH_MSG_KIND_LINK_STAT:
		xeth_sbrx_link_stat(xpp);
		break;
	case XETH_MSG_KIND_DUMP_IFINFO:
		xeth_upper_all_dump_ifinfo(xpp);
		xeth_sbtx_break(xpp);
		if (xeth_debug_err(xeth_start_notifier(xpp, netdevice)) == 0) {
			xeth_debug_err(xeth_start_notifier(xpp, inetaddr));
		}
		break;
	case XETH_MSG_KIND_SPEED:
		xeth_sbrx_speed(xpp);
		break;
	case XETH_MSG_KIND_DUMP_FIBINFO:
		if (xeth_start_fib_notifier(xpp) == 0) {
			xeth_sbtx_break(xpp);
			xeth_start_notifier(xpp, netevent);
		}
		break;
	default:
		xeth_counter_inc(xpp, sbrx_invalid);
		err = -EINVAL;
	}
	return err;
}

static int xeth_sbrx_task(void *data)
{
	struct xeth_platform_priv *xpp = data;
	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 10000,
	};
	int err;

	err = kernel_setsockopt(xpp->sb.conn, SOL_SOCKET, SO_RCVTIMEO_NEW,
				(char *)&tv, sizeof(tv));
	if (err)
		return err;

	allow_signal(SIGKILL);
	while (!err && !kthread_should_stop() && !signal_pending(current))
		err = xeth_sbrx_service(xpp);
	xeth_stop_notifier(xpp, netevent);
	xeth_stop_notifier(xpp, fib);
	xeth_stop_notifier(xpp, inetaddr);
	xeth_stop_notifier(xpp, netdevice);
	xeth_flag_clear(xpp, sbrx_task);
	if (err < 0)
		xeth_debug("exit with %d",  err);
	return err;
}

struct task_struct *xeth_sbrx_fork(struct xeth_platform_priv *xpp)
{
	struct task_struct *t;

	xeth_flag_set(xpp, sbrx_task);
	t = xeth_debug_ptr_err(kthread_run(xeth_sbrx_task, xpp, "%s-rx",
					   xpp->pdev->name));
	if (IS_ERR(t)) {
		xeth_flag_clear(xpp, sbrx_task);
		t = NULL;
	}
	return t;
}
