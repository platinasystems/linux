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

static struct {
	struct notifier_block fib, inetaddr, inet6addr, netdevice, netevent;
} xeth_nb;

static int xeth_nb_fib(struct notifier_block *nb, unsigned long event,
		       void *ptr)
{
	struct fib_notifier_info *info = ptr;

	if (nb != &xeth_nb.fib)
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
				xeth_sbtx_fib_entry(event, feni);
			} while(0);
			break;
		case AF_INET6:
			do {
				struct fib6_entry_notifier_info *feni =
					container_of(info, typeof(*feni), info);
				xeth_sbtx_fib6_entry(event, feni);
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
		xeth_debug("fib event %ld unknown", event);
	}
	return NOTIFY_DONE;
}

static void xeth_nb_fib_cb(struct notifier_block *nb)
{
	xeth_debug("register_fib_cb");
}

static int xeth_nb_inetaddr(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	if (nb == &xeth_nb.inetaddr && ifa->ifa_dev) {
		struct net_device *nd = ifa->ifa_dev->dev;
		if (xeth_upper_check(nd)) {
			u32 xid = xeth_upper_xid(nd);
			if (xid)
				xeth_sbtx_ifa(ifa, xid, event);
		}
	}
	return NOTIFY_DONE;
}

static int xeth_nb_inet6addr(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct inet6_ifaddr *ifa6 = (struct inet6_ifaddr *)ptr;

	if (nb == &xeth_nb.inet6addr && ifa6->idev) {
		struct net_device *nd = ifa6->idev->dev;
		if (xeth_upper_check(nd)) {
			u32 xid = xeth_upper_xid(nd);
			if (xid)
				xeth_sbtx_ifa6(ifa6, xid, event);
		}
	}
	return NOTIFY_DONE;
}

static int xeth_nb_netdevice(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct net_device *nd;
	enum xeth_dev_kind kind;
	u32 xid;

	if (nb != &xeth_nb.netdevice)
		return NOTIFY_DONE;
	nd = netdev_notifier_info_to_dev(ptr);
	if (nd->ifindex == 1) {
		struct net *ndnet = dev_net(nd);
		switch (event) {
		case NETDEV_REGISTER:
			xeth_sbtx_netns(ndnet, true);
			break;
		case NETDEV_UNREGISTER:
			xeth_sbtx_netns(ndnet, false);
			break;
		}
		return NOTIFY_DONE;
	}
	if (!xeth_upper_check(nd))
		return NOTIFY_DONE;
	kind = xeth_upper_kind(nd);
	xid = xeth_upper_xid(nd);
	if (xid == 0)
		return NOTIFY_DONE;
	switch (event) {
	case NETDEV_REGISTER:
		/* also notifies dev_change_net_namespace */
		xeth_sbtx_ifinfo(nd, xid, kind, 0, XETH_IFINFO_REASON_REG);
		break;
	case NETDEV_UNREGISTER:
		/* lgnored here, handled by @xeth_upper_lnko_del() */
		break;
	case NETDEV_CHANGEMTU:
		if (dev_get_iflink(nd) == nd->ifindex) {
			/**
			 * this is a real rev; if it's one of the mux lowers,
			 * we may need to change the mtu for all of the uppers.
			 */
			if (xeth_mux_is_lower(nd)) {
				xeth_upper_changemtu(nd->mtu, nd->max_mtu);
			}
		}
		break;
	case NETDEV_CHANGEUPPER:
		/* ignore here, handled by @xeth_upper_ndo_add_slave() */
		break;
	}
	return NOTIFY_DONE;
}

static int xeth_nb_netevent(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	if (nb != &xeth_nb.netevent)
		return NOTIFY_DONE;
	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		xeth_sbtx_neigh_update(ptr);
		break;
	}
	return NOTIFY_DONE;
}

#define xeth_nb_start(NAME)						\
({									\
	int _err = 0;							\
	if (!xeth_flag(NAME##_notifier)) {				\
		xeth_nb.NAME.notifier_call = xeth_nb_##NAME;		\
		_err = register_##NAME##_notifier(&xeth_nb.NAME);	\
		if (!_err)						\
			xeth_flag_set(NAME##_notifier);			\
	}								\
	(_err);								\
})

int xeth_nb_start_fib(void)
{
	int err = 0;

	if (!xeth_flag(fib_notifier)) {
		xeth_nb.fib.notifier_call = xeth_nb_fib,
		err = register_fib_notifier(&xeth_nb.fib, xeth_nb_fib_cb);
		if (!err)
			xeth_flag_set(fib_notifier);
	}
	return err;
}

int xeth_nb_start_inetaddr(void)
{
	return xeth_nb_start(inetaddr);
}

int xeth_nb_start_inet6addr(void)
{
	return xeth_nb_start(inet6addr);
}

int xeth_nb_start_netdevice(void)
{
	return xeth_nb_start(netdevice);
}

int xeth_nb_start_netevent(void)
{
	return xeth_nb_start(netevent);
}

#define xeth_nb_stop(NAME)						\
do {									\
	if (xeth_flag(NAME##_notifier)) {				\
		unregister_##NAME##_notifier(&xeth_nb.NAME);		\
		xeth_flag_clear(NAME##_notifier);			\
	}								\
} while(0)

void xeth_nb_stop_fib(void)
{
	xeth_nb_stop(fib);
}

void xeth_nb_stop_inetaddr(void)
{
	xeth_nb_stop(inetaddr);
}

void xeth_nb_stop_inet6addr(void)
{
	xeth_nb_stop(inet6addr);
}

void xeth_nb_stop_netdevice(void)
{
	xeth_nb_stop(netdevice);
}

void xeth_nb_stop_netevent(void)
{
	xeth_nb_stop(netevent);
}
