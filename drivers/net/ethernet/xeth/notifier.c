/* XETH notifier
 *
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

static int xeth_notifier_netdevice(struct notifier_block *nb,
				   unsigned long event, void *ptr);
static int xeth_notifier_inetaddr(struct notifier_block *nb,
				  unsigned long event, void *ptr);
static int xeth_notifier_fib(struct notifier_block *nb,
			     unsigned long event, void *ptr);

static struct notifier_block xeth_notifier_block_netdevice = {
	.notifier_call = xeth_notifier_netdevice,
};
static struct notifier_block xeth_notifier_block_inetaddr = {
	.notifier_call = xeth_notifier_inetaddr,
};
static struct notifier_block xeth_notifier_block_fib = {
	.notifier_call = xeth_notifier_fib,
};

static bool xeth_notifier_registered_netdevice = false;
static bool xeth_notifier_registered_inetaddr = false;
static bool xeth_notifier_registered_fib = false;

static int xeth_notifier_netdevice(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *nd;
	int iflinki, ndi;

	if (nb != &xeth_notifier_block_netdevice)
		return NOTIFY_DONE;
	nd = netdev_notifier_info_to_dev(ptr);
	switch (event) {
	case NETDEV_UNREGISTER:
		for (iflinki = 0; iflinki < xeth.n.iflinks; iflinki++) {
			struct net_device *iflink = xeth_iflinks(iflinki);
			if (nd == iflink) {
				xeth_reset_iflinks(iflinki);
				netdev_rx_handler_unregister(iflink);
				dev_put(iflink);
			}
		}
		break;
	case NETDEV_CHANGEMTU:
		for (iflinki = 0; iflinki < xeth.n.iflinks; iflinki++) {
			struct net_device *iflink = xeth_iflinks(iflinki);
			if (nd == iflink) {
				for (ndi = 0; ndi < xeth.n.ids; ndi++) {
					struct net_device *xnd = xeth_nds(ndi);
					if (xnd != NULL) {
						struct xeth_priv *priv =
							netdev_priv(xnd);
						if (priv->iflinki == iflinki) {
							dev_set_mtu(xnd,
								    nd->mtu);
						}
					}
				}
			}
		}
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
	int i;

	if (nb != &xeth_notifier_block_inetaddr)
		return NOTIFY_DONE;
	if (ifa->ifa_dev == NULL)
		return NOTIFY_DONE;
	nd = ifa->ifa_dev->dev;
	if (nd == NULL)
		return NOTIFY_DONE;
	for (i = 0; i < xeth.n.ids; i++) {
		if (nd == xeth_nds(i)) {
			break;
		}
	}
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

static int xeth_notifier_fib(struct notifier_block *nb,
			     unsigned long event, void *ptr)
{
	if (nb != &xeth_notifier_block_fib)
		return NOTIFY_DONE;
	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_APPEND:
	case FIB_EVENT_ENTRY_ADD:
	case FIB_EVENT_ENTRY_DEL: {
		struct fib_entry_notifier_info *info =
			(struct fib_entry_notifier_info *)ptr;
		xeth_pr_true_val("%d", xeth_sb_send_fibentry(event, info));
	};	break;
	default:
		xeth_pr("unsupported fib event %ld", event);
	}
	return NOTIFY_DONE;
}

static void xeth_notifier_fib_cb(struct notifier_block *nb)
{
	xeth_pr("register_fib_cb");
}

int xeth_notifier_register_fib(void)
{
	int err = register_fib_notifier(&xeth_notifier_block_fib,
					xeth_notifier_fib_cb);
	if (!err)
		xeth_notifier_registered_fib = true;
	return err;
}

void xeth_notifier_unregister_fib(void)
{
	if (xeth_notifier_registered_fib)
		unregister_fib_notifier(&xeth_notifier_block_fib);
	xeth_notifier_registered_fib = false;
}

int xeth_notifier_init(void)
{
	int (*const registers[])(void) = {
		xeth_notifier_register_netdevice,
		xeth_notifier_register_inetaddr,
		/* xeth_notifier_register_fib w/ sb command instead of init */
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
		xeth_notifier_unregister_netdevice,
		xeth_notifier_unregister_inetaddr,
		xeth_notifier_unregister_fib,
		NULL,
	};
	int i;

	for (i = 0; unregisters[i]; i++)
		unregisters[i]();
}
