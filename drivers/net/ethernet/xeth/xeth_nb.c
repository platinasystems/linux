/**
 * XETH notifiers
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_nb.h"
#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_sbtx.h"
#include "xeth_debug.h"
#include <net/ip_fib.h>

static int xeth_nb_fib(struct notifier_block *fib,
		       unsigned long event, void *ptr)
{
	struct fib_notifier_info *info = ptr;
	struct xeth_nb *nb;
	struct net_device *mux;

	if (fib->notifier_call != xeth_nb_fib)
		return NOTIFY_DONE;
	if (nb = xeth_debug_container_of(fib, struct xeth_nb, fib), IS_ERR(nb))
		return NOTIFY_DONE;
	if (mux = xeth_mux_of_nb(nb), IS_ERR(mux))
		return NOTIFY_DONE;
	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_APPEND:
	case FIB_EVENT_ENTRY_ADD:
	case FIB_EVENT_ENTRY_DEL:
		switch (info->family) {
		case AF_INET:
			do {
				struct fib_entry_notifier_info *feni =
					container_of(info, typeof(*feni), info);
				xeth_sbtx_fib_entry(mux, feni, event);
			} while(0);
			break;
		case AF_INET6:
			do {
				struct fib6_entry_notifier_info *feni =
					container_of(info, typeof(*feni), info);
				xeth_sbtx_fib6_entry(mux, feni, event);
			} while(0);
			break;
		}
		break;
	case FIB_EVENT_RULE_ADD:
	case FIB_EVENT_RULE_DEL:
	case FIB_EVENT_NH_ADD:
	case FIB_EVENT_NH_DEL:
	case FIB_EVENT_VIF_ADD:
	case FIB_EVENT_VIF_DEL:
		break;
	default:
		pr_err("%s: unknown fib event: %ld", __func__, event);
	}
	return NOTIFY_DONE;
}

static int xeth_nb_inetaddr(struct notifier_block *inetaddr,
			    unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct xeth_nb *nb;
	struct net_device *mux, *nd;
	struct xeth_proxy *proxy;

	if (inetaddr->notifier_call != xeth_nb_inetaddr)
		return NOTIFY_DONE;
	nb = xeth_debug_container_of(inetaddr, struct xeth_nb, inetaddr);
	if (IS_ERR(nb))
		return NOTIFY_DONE;
	if (mux = xeth_mux_of_nb(nb), IS_ERR(mux))
		return NOTIFY_DONE;
	if (!ifa->ifa_dev)
		return NOTIFY_DONE;
	nd = ifa->ifa_dev->dev;
	proxy = xeth_mux_proxy_of_nd(mux, nd);
	if (proxy && proxy->xid && proxy->mux == mux)
		xeth_sbtx_ifa(mux, ifa, event, proxy->xid);
	return NOTIFY_DONE;
}

static int xeth_nb_inet6addr(struct notifier_block *inet6addr,
			     unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa6 = (struct inet6_ifaddr *)ptr;
	struct xeth_nb *nb;
	struct net_device *mux, *nd;
	struct xeth_proxy *proxy;

	if (inet6addr->notifier_call != xeth_nb_inet6addr)
		return NOTIFY_DONE;
	nb = xeth_debug_container_of(inet6addr, struct xeth_nb, inet6addr);
	if (IS_ERR(nb))
		return NOTIFY_DONE;
	if (mux = xeth_mux_of_nb(nb), IS_ERR(mux))
		return NOTIFY_DONE;
	if (!ifa6->idev)
		return NOTIFY_DONE;
	nd = ifa6->idev->dev;
	proxy = xeth_mux_proxy_of_nd(mux, nd);
	if (proxy && proxy->xid && proxy->mux == mux)
		xeth_sbtx_ifa6(mux, ifa6, event, proxy->xid);
	return NOTIFY_DONE;
}

static int xeth_nb_netdevice(struct notifier_block *netdevice,
			     unsigned long event, void *ptr)
{
	struct xeth_nb *nb;
	struct net_device *mux, *nd;
	struct xeth_proxy *proxy;

	if (netdevice->notifier_call != xeth_nb_netdevice)
		return NOTIFY_DONE;
	nb = xeth_debug_container_of(netdevice, struct xeth_nb, netdevice);
	if (IS_ERR(nb))
		return NOTIFY_DONE;
	mux = xeth_mux_of_nb(nb);
	if (IS_ERR(mux))
		return NOTIFY_DONE;
	nd = netdev_notifier_info_to_dev(ptr);
	if (nd->ifindex == 1) {
		/*
		 * ifindex(1) is the loopback port anf register/unregister of
		 * loopback corresponds to netns creation/destruction.
		 */
		struct net *ndnet = dev_net(nd);
		switch (event) {
		case NETDEV_REGISTER:
			xeth_sbtx_netns(mux, ndnet, true);
			break;
		case NETDEV_UNREGISTER:
			xeth_sbtx_netns(mux, ndnet, false);
			break;
		}
		return NOTIFY_DONE;
	}
	proxy = xeth_mux_proxy_of_nd(mux, nd);
	switch (event) {
	case NETDEV_REGISTER:
		/* also notifies dev_change_net_namespace */
		if (proxy && proxy->xid && proxy->mux)
			xeth_sbtx_ifinfo(proxy, 0, XETH_IFINFO_REASON_REG);
		break;
	case NETDEV_UNREGISTER:
		/* lgnored here, handled by @xeth_UPPER_dellink() */
		break;
	case NETDEV_CHANGEMTU:
		if (dev_get_iflink(nd) == nd->ifindex) {
			/**
			 * this is a real dev; if it's one of the mux lowers,
			 * we may need to change the mtu for all of the uppers.
			if (xeth_mux_is_lower_rcu(nd)) {
				xeth_upper_changemtu(xpp, nd->mtu, nd->max_mtu);
			}
			 */
		}
		break;
	case NETDEV_CHANGEUPPER:
		/* ignore here, handled by @xeth_UPPER_add_slave() */
		break;
	case NETDEV_CHANGELOWERSTATE:
		xeth_mux_check_lower_carrier(mux);
		break;
	}
	return NOTIFY_DONE;
}

static int xeth_nb_netevent(struct notifier_block *netevent,
			    unsigned long event, void *ptr)
{
	struct xeth_nb *nb;
	struct net_device *mux;

	if (netevent->notifier_call != xeth_nb_netevent)
		return NOTIFY_DONE;
	nb = xeth_debug_container_of(netevent, struct xeth_nb, netevent);
	if (IS_ERR(nb))
		return NOTIFY_DONE;
	if (mux = xeth_mux_of_nb(nb), IS_ERR(mux))
		return NOTIFY_DONE;
	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		xeth_sbtx_neigh_update(mux, ptr);
		break;
	}
	return NOTIFY_DONE;
}

static void xeth_mux_fib_cb(struct notifier_block *fib)
{
	struct xeth_nb *nb = xeth_debug_container_of(fib, struct xeth_nb, fib);
	struct net_device *mux = xeth_mux_of_nb(nb);
	if (!IS_ERR(mux))
		xeth_debug_nd(mux, "registered fib notifier");
}

#define xeth_nb_register_fib(NB) register_fib_notifier(NB, xeth_mux_fib_cb)
#define xeth_nb_register_inetaddr(NB)	register_inetaddr_notifier(NB)
#define xeth_nb_register_inet6addr(NB)	register_inet6addr_notifier(NB)
#define xeth_nb_register_netdevice(NB)	register_netdevice_notifier(NB)
#define xeth_nb_register_netevent(NB)	register_netevent_notifier(NB)

#define xeth_nb_start(NB)						\
int xeth_nb_start_##NB(struct net_device *mux)				\
{									\
	int err;							\
	struct xeth_nb *nb = xeth_mux_nb(mux);				\
	if (xeth_mux_has_##NB##_notifier(mux))				\
		return -EBUSY;						\
	nb->NB.notifier_call = xeth_nb_##NB;				\
	err = xeth_nb_register_##NB(&nb->NB);				\
	if (!err)							\
		xeth_mux_set_##NB##_notifier(mux);			\
	return err;							\
}

xeth_nb_start(fib)
xeth_nb_start(inetaddr)
xeth_nb_start(inet6addr)
xeth_nb_start(netdevice)
xeth_nb_start(netevent)

#define xeth_nb_stop(NB)						\
void xeth_nb_stop_##NB(struct net_device *mux)				\
{									\
	struct xeth_nb *nb = xeth_mux_nb(mux);				\
	if (xeth_mux_has_##NB##_notifier(mux)) {			\
		unregister_##NB##_notifier(&nb->NB);			\
		xeth_mux_clear_##NB##_notifier(mux);			\
	}								\
}

xeth_nb_stop(fib)
xeth_nb_stop(inetaddr)
xeth_nb_stop(inet6addr)
xeth_nb_stop(netdevice)
xeth_nb_stop(netevent)

