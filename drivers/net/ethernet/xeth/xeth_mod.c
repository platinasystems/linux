/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_platform.h"
#include "xeth_bridge.h"
#include "xeth_lag.h"
#include "xeth_vlan.h"
#include "xeth_port.h"
#include "xeth_mux.h"
#include "xeth_version.h"
#include <linux/module.h>

static struct rtnl_link_ops * const xeth_mod_lnkos[] = {
	/*
	 * list bridge before lag and vlan to release all these lowers
	 * before the upper bridge 
	 */
	&xeth_bridge_lnko,
	&xeth_lag_lnko,
	&xeth_vlan_lnko,
	&xeth_port_lnko,
	&xeth_mux_lnko,
	NULL,
};

static int __init xeth_mod_init(void)
{
	struct rtnl_link_ops * const *plnko;
	int err;

	err = platform_driver_register(&xeth_platform_driver);
	if (err < 0)
		return err;
	for (plnko = xeth_mod_lnkos; err >= 0 && (*plnko); plnko++)
		err = rtnl_link_register(*plnko);
	if (err) {
		platform_driver_unregister(&xeth_platform_driver);
		while (plnko != xeth_mod_lnkos)
			rtnl_link_unregister(*(--plnko));
	}
	return err;
}
module_init(xeth_mod_init);

static void __exit xeth_mod_exit(void)
{
	struct rtnl_link_ops * const *plnko;

	platform_driver_unregister(&xeth_platform_driver);
	for (plnko = xeth_mod_lnkos; *plnko; plnko++)
		if ((*plnko)->list.next || (*plnko)->list.prev)
			rtnl_link_unregister(*plnko);
}
module_exit(xeth_mod_exit);

MODULE_DESCRIPTION("mux proxy netdevs with a remote switch");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(XETH_VERSION);
