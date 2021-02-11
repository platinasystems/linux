/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_mux.h"
#include "xeth_nb.h"
#include "xeth_proxy.h"
#include "xeth_sbrx.h"
#include "xeth_sbtx.h"
#include "xeth_port.h"
#include "xeth_debug.h"

static bool xeth_sbrx_is_msg(void *data)
{
	struct xeth_msg *msg = data;

	return	msg->header.z64 == 0 &&
		msg->header.z32 == 0 &&
		msg->header.z16 == 0;
}

static void xeth_sbrx_carrier(struct net_device *mux,
			      struct xeth_msg_carrier *msg)
{
	struct xeth_proxy *proxy = xeth_mux_proxy_of_xid(mux, msg->xid);
	if (proxy && proxy->kind == XETH_DEV_KIND_PORT)
		xeth_mux_change_carrier(mux, proxy->nd,
					msg->flag == XETH_CARRIER_ON);
	else
		xeth_mux_inc_sbrx_invalid(mux);
}

static void xeth_sbrx_et_stat(struct net_device *mux,
			      struct xeth_msg_stat *msg)
{
	struct xeth_proxy *proxy = xeth_mux_proxy_of_xid(mux, msg->xid);
	if (proxy && proxy->kind == XETH_DEV_KIND_PORT)
		xeth_port_ethtool_stat(proxy->nd, msg->index, msg->count);
}

static void xeth_sbrx_link_stat(struct net_device *mux,
				struct xeth_msg_stat *msg)
{
	struct xeth_proxy *proxy = xeth_mux_proxy_of_xid(mux, msg->xid);
	xeth_proxy_link_stat(proxy->nd, msg->index, msg->count);
}

static void xeth_sbrx_speed(struct net_device *mux,
			    struct xeth_msg_speed *msg)
{
	struct xeth_proxy *proxy = xeth_mux_proxy_of_xid(mux, msg->xid);
	if (proxy && proxy->kind == XETH_DEV_KIND_PORT)
		xeth_port_speed(proxy->nd, msg->mbps);
}

// return < 0 if error, 1 if sock closed, and 0 othewise
int xeth_sbrx(struct net_device *mux, struct socket *conn, void *data)
{
	struct xeth_msg_header *msg = data;
	struct msghdr oob = {};
	struct kvec iov = {
		.iov_base = data,
		.iov_len = XETH_SIZEOF_JUMBO_FRAME,
	};
	int n, err;

	xeth_mux_inc_sbrx_ticks(mux);
	n = kernel_recvmsg(conn, &oob, &iov, 1, iov.iov_len, 0);
	if (n == -EAGAIN) {
		schedule();
		return 0;
	}
	if (n == 0 || n == -ECONNRESET)
		return 1;
	if (n < 0)
		return n;
	xeth_mux_inc_sbrx_msgs(mux);
	if (n < sizeof(*msg) || !xeth_sbrx_is_msg(msg))
		return -EINVAL;
	if (msg->version != XETH_MSG_VERSION)
		return -EINVAL;
	switch (msg->kind) {
	case XETH_MSG_KIND_DUMP_IFINFO:
		xeth_mux_dump_all_ifinfo(mux);
		xeth_sbtx_break(mux);
		xeth_debug_err(xeth_nb_start_netdevice(mux));
		xeth_debug_err(xeth_nb_start_inetaddr(mux));
		break;
	case XETH_MSG_KIND_DUMP_FIBINFO:
		xeth_debug_err(xeth_nb_start_fib(mux));
		xeth_sbtx_break(mux);
		xeth_debug_err(xeth_nb_start_netevent(mux));
		break;
	case XETH_MSG_KIND_CARRIER:
		xeth_sbrx_carrier(mux, data);
		break;
	case XETH_MSG_KIND_ETHTOOL_STAT:
		xeth_sbrx_et_stat(mux, data);
		break;
	case XETH_MSG_KIND_LINK_STAT:
		xeth_sbrx_link_stat(mux, data);
		break;
	case XETH_MSG_KIND_SPEED:
		xeth_sbrx_speed(mux, data);
		break;
	default:
		xeth_mux_inc_sbrx_invalid(mux);
		err = -EINVAL;
	}
	return 0;
}
