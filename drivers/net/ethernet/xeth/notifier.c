/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <uapi/linux/xeth.h>

static int xeth_notifier_netdevice(struct notifier_block *nb,
				   unsigned long event, void *ptr);
static int xeth_notifier_inetaddr(struct notifier_block *nb,
				  unsigned long event, void *ptr);
static int xeth_notifier_netevent(struct notifier_block *nb,
				  unsigned long event, void *ptr);
static int xeth_notifier_fib(struct notifier_block *nb,
			     unsigned long event, void *ptr);

static struct notifier_block xeth_notifier_block_netdevice = {
	.notifier_call = xeth_notifier_netdevice,
};
static struct notifier_block xeth_notifier_block_inetaddr = {
	.notifier_call = xeth_notifier_inetaddr,
};
static struct notifier_block xeth_notifier_block_netevent = {
	.notifier_call = xeth_notifier_netevent,
};
static struct notifier_block xeth_notifier_block_fib = {
	.notifier_call = xeth_notifier_fib,
};


static bool xeth_notifier_registered_netdevice = false;
static bool xeth_notifier_registered_inetaddr = false;
static bool xeth_notifier_registered_netevent = false;
static bool xeth_notifier_registered_fib = false;

static int xeth_notifier_netdevice(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *nd;
	int i;

	if (nb != &xeth_notifier_block_netdevice)
		return NOTIFY_DONE;
	nd = netdev_notifier_info_to_dev(ptr);
	switch (event) {
	case NETDEV_REGISTER:
		if (netif_is_bridge_master(nd))
			if (xeth.encap.id(nd) < 0)
				xeth.encap.associate_dev(nd);
		/* also notifies dev_change_net_namespace */
		no_xeth_pr_nd(nd, "registered");
		xeth_sb_send_ifinfo(nd, 0, XETH_IFINFO_REASON_REG);
		break;
	case NETDEV_UNREGISTER:
		xeth_for_each_iflink(i) {
			struct net_device *iflink = xeth_iflink(i);
			if (nd == iflink)
				xeth_iflink_reset(i);
		}
		no_xeth_pr_nd(nd, "unregistered");
		xeth_sb_send_ifinfo(nd, 0, XETH_IFINFO_REASON_UNREG);
		xeth.encap.disassociate_dev(nd);
		break;
	case NETDEV_CHANGEMTU:
		xeth_for_each_iflink(i)
			if (nd == xeth_iflink(i))
				xeth.encap.changemtu(nd);
		break;
	case NETDEV_CHANGEUPPER: {
		struct netdev_notifier_changeupper_info *info = ptr;
		int ui = info->upper_dev->ifindex;
		struct xeth_upper *p = xeth_find_upper(ui, nd->ifindex);
		const char *op = info->linking ? "link" : "unlink";
		const char *upper_name = netdev_name(info->upper_dev);
		no_xeth_pr_nd(nd, "%s upper %s", op, upper_name);
		if (info->linking && p == NULL) {
			xeth_add_upper(ui, nd->ifindex);
			xeth_sb_send_change_upper(ui, nd->ifindex,
						  info->linking);
		} else if (!info->linking && p != NULL) {
			xeth_free_upper(p);
			xeth_sb_send_change_upper(ui, nd->ifindex,
						  info->linking);
		} else
			xeth_pr_nd(nd, "ignored %s upper %s", op, upper_name);
	}	break;
	case NETDEV_UP:
		xeth_pr_nd(nd, "admin %s", "up");
		xeth_sb_send_ifinfo(nd, 0, XETH_IFINFO_REASON_UP);
		break;
	case NETDEV_DOWN:
		xeth_pr_nd(nd, "admin %s", "down");
		xeth_sb_send_ifinfo(nd, 0, XETH_IFINFO_REASON_DOWN);
		break;
	}
	return NOTIFY_DONE;
}

static int xeth_notifier_register_netdevice(void)
{
	int err = register_netdevice_notifier(&xeth_notifier_block_netdevice);
	if (!err)
		xeth_notifier_registered_netdevice = true;
	return err;
}

static void xeth_notifier_unregister_netdevice(void)
{
	if (xeth_notifier_registered_netdevice)
		unregister_netdevice_notifier(&xeth_notifier_block_netdevice);
	xeth_notifier_registered_netdevice = false;
}

static int xeth_notifier_inetaddr(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *nd;

	if (nb != &xeth_notifier_block_inetaddr)
		return NOTIFY_DONE;
	if (ifa->ifa_dev == NULL)
		return NOTIFY_DONE;
	nd = ifa->ifa_dev->dev;
	if (nd != NULL)
		xeth_sb_send_ifa(nd, event, ifa);
	return NOTIFY_DONE;
}

static int xeth_notifier_register_inetaddr(void)
{
	int err = register_inetaddr_notifier(&xeth_notifier_block_inetaddr);
	if (!err)
		xeth_notifier_registered_inetaddr = true;
	return err;
}

static void xeth_notifier_unregister_inetaddr(void)
{
	if (xeth_notifier_registered_inetaddr)
		unregister_inetaddr_notifier(&xeth_notifier_block_inetaddr);
	xeth_notifier_registered_inetaddr = false;
}

static int xeth_notifier_netevent(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	if (nb != &xeth_notifier_block_netevent)
		return NOTIFY_DONE;
	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		xeth_sb_send_neigh_update(ptr);
		break;
	}
	return NOTIFY_DONE;
}

static int xeth_notifier_register_netevent(void)
{
	int err = register_netevent_notifier(&xeth_notifier_block_netevent);
	if (!err)
		xeth_notifier_registered_netevent = true;
	return err;
}

static void xeth_notifier_unregister_netevent(void)
{
	if (xeth_notifier_registered_netevent)
		unregister_netevent_notifier(&xeth_notifier_block_netevent);
	xeth_notifier_registered_netevent = false;
}

static int xeth_notifier_fib(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct fib_notifier_info *info = ptr;

	if (nb != &xeth_notifier_block_fib)
		return NOTIFY_DONE;
	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_APPEND:
	case FIB_EVENT_ENTRY_ADD:
	case FIB_EVENT_ENTRY_DEL:
		switch (info->family) {
		case AF_INET:
			xeth_sb_send_fib_entry(event, info);
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
		xeth_pr("fib event %ld unknown", event);
	}
	return NOTIFY_DONE;
}

static void xeth_notifier_fib_cb(struct notifier_block *nb)
{
	xeth_pr("register_fib_cb");
}

int xeth_notifier_register_fib(void)
{
	int err;
	
	if (xeth_notifier_registered_fib)
		return 0;
	err = register_fib_notifier(&xeth_notifier_block_fib,
				    xeth_notifier_fib_cb);
	if (!err) {
		xeth_notifier_registered_fib = true;
		err = xeth_notifier_register_netevent();
	}
	return err;
}

void xeth_notifier_unregister_fib(void)
{
	if (xeth_notifier_registered_fib)
		unregister_fib_notifier(&xeth_notifier_block_fib);
	xeth_notifier_registered_fib = false;
	xeth_notifier_unregister_netevent();
}

int xeth_notifier_init(void)
{
	int (*const registers[])(void) = {
		xeth_notifier_register_netdevice,
		xeth_notifier_register_inetaddr,
		/* xeth_notifier_register_fib then
		 * xeth_notifier_register_netevent
		 * w/ rx of sb dump_fibinfo req instead of init
		 */
		NULL,
	};
	int i;

	for (i = 0; registers[i]; i++) {
		int err = registers[i]();
		if (err) {
			xeth_notifier_exit();
			return err;
		}
	}
	return 0;
}

void xeth_notifier_exit(void)
{
	void (*const unregisters[])(void) = {
		xeth_notifier_unregister_fib,
		xeth_notifier_unregister_netevent,
		xeth_notifier_unregister_inetaddr,
		xeth_notifier_unregister_netdevice,
		NULL,
	};
	int i;

	for (i = 0; unregisters[i]; i++)
		unregisters[i]();
}
