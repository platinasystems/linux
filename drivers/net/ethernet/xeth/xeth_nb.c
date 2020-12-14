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

int xeth_nb_fib(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct fib_notifier_info *info = ptr;
	struct xeth_platform_priv *xpp;

	if (nb->notifier_call != xeth_nb_fib)
		goto xeth_nb_fib_done;
	xpp = xeth_platform_priv_of_nb(nb, fib);
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
				xeth_sbtx_fib_entry(xpp, feni, event);
			} while(0);
			break;
		case AF_INET6:
			do {
				struct fib6_entry_notifier_info *feni =
					container_of(info, typeof(*feni), info);
				xeth_sbtx_fib6_entry(xpp, feni, event);
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
		xeth_debug("unknown fib event: %ld", event);
	}
xeth_nb_fib_done:
	return NOTIFY_DONE;
}

int xeth_nb_inetaddr(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct xeth_platform_priv *xpp;
	struct net_device *nd;
	u32 xid;

	if (nb->notifier_call != xeth_nb_inetaddr)
		goto xeth_nb_inetaddr_done;
	xpp = xeth_platform_priv_of_nb(nb, inetaddr);
	if (!ifa->ifa_dev)
		goto xeth_nb_inetaddr_done;
	nd = ifa->ifa_dev->dev;
	if (!xeth_is_upper(nd))
		goto xeth_nb_inetaddr_done;
	xid = xeth_upper_xid(nd);
	if (!xid)
		goto xeth_nb_inetaddr_done;
	xeth_sbtx_ifa(xpp, ifa, event, xid);
xeth_nb_inetaddr_done:
	return NOTIFY_DONE;
}

int xeth_nb_inet6addr(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa6 = (struct inet6_ifaddr *)ptr;
	struct xeth_platform_priv *xpp;
	struct net_device *nd;
	u32 xid;

	if (nb->notifier_call != xeth_nb_inet6addr)
		goto xeth_nb_inet6addr_done;
	xpp = xeth_platform_priv_of_nb(nb, inet6addr);
	if (!ifa6->idev)
		goto xeth_nb_inet6addr_done;
	nd = ifa6->idev->dev;
	if (!xeth_is_upper(nd))
		goto xeth_nb_inet6addr_done;
	xid = xeth_upper_xid(nd);
	if (!xid)
		goto xeth_nb_inet6addr_done;
	xeth_sbtx_ifa6(xpp, ifa6, event, xid);
xeth_nb_inet6addr_done:
	return NOTIFY_DONE;
}

int xeth_nb_netdevice(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct xeth_platform_priv *xpp;
	struct net_device *nd;
	enum xeth_dev_kind kind;
	u32 xid;

	if (nb->notifier_call != xeth_nb_netdevice)
		goto xeth_nb_netdevice_done;
	xpp = xeth_platform_priv_of_nb(nb, netdevice);
	nd = netdev_notifier_info_to_dev(ptr);
	if (nd->ifindex == 1) {
		struct net *ndnet = dev_net(nd);
		switch (event) {
		case NETDEV_REGISTER:
			xeth_sbtx_netns(xpp, ndnet, true);
			break;
		case NETDEV_UNREGISTER:
			xeth_sbtx_netns(xpp, ndnet, false);
			break;
		}
		goto xeth_nb_netdevice_done;
	}
	if (!xeth_is_upper(nd))
		goto xeth_nb_netdevice_done;
	kind = xeth_upper_kind(nd);
	xid = xeth_upper_xid(nd);
	if (!xid)
		goto xeth_nb_netdevice_done;
	switch (event) {
	case NETDEV_REGISTER:
		/* also notifies dev_change_net_namespace */
		xeth_sbtx_ifinfo(xpp, nd, kind, xid, 0, XETH_IFINFO_REASON_REG);
		break;
	case NETDEV_UNREGISTER:
		/* lgnored here, handled by @xeth_upper_lnko_del() */
		break;
	case NETDEV_CHANGEMTU:
		if (dev_get_iflink(nd) == nd->ifindex) {
			/**
			 * this is a real dev; if it's one of the mux lowers,
			 * we may need to change the mtu for all of the uppers.
			 */
			if (xeth_mux_is_lower_rcu(nd)) {
				xeth_upper_changemtu(xpp, nd->mtu, nd->max_mtu);
			}
		}
		break;
	case NETDEV_CHANGEUPPER:
		/* ignore here, handled by @xeth_upper_ndo_add_slave() */
		break;
	}
xeth_nb_netdevice_done:
	return NOTIFY_DONE;
}

int xeth_nb_netevent(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct xeth_platform_priv *xpp;

	if (nb->notifier_call != xeth_nb_netevent)
		goto xeth_nb_netevent_done;
	xpp = xeth_platform_priv_of_nb(nb, netevent);
	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		xeth_sbtx_neigh_update(xpp, ptr);
		break;
	}
xeth_nb_netevent_done:
	return NOTIFY_DONE;
}
