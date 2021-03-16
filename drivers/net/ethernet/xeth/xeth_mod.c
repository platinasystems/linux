/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_bridge.h"
#include "xeth_lb.h"
#include "xeth_lag.h"
#include "xeth_vlan.h"
#include "xeth_port.h"
#include "xeth_mux.h"
#include "xeth_version.h"
#include <linux/module.h>

static struct platform_driver * const xeth_mod_drivers[] = {
	&xeth_mux_driver,
	&xeth_port_driver,
	NULL,
};

static struct rtnl_link_ops * const xeth_mod_lnkos[] = {
	/*
	 * list bridge before lag and vlan to release all these lowers
	 * before the upper bridge 
	 */
	&xeth_bridge_lnko,
	&xeth_lag_lnko,
	&xeth_vlan_lnko,
	&xeth_port_lnko,
	&xeth_lb_lnko,
	&xeth_mux_lnko,
	NULL,
};

static int __init xeth_mod_init(void)
{
	struct platform_driver * const *drvr = NULL;
	struct rtnl_link_ops * const *lnko = NULL;
	int err;

	for (drvr = xeth_mod_drivers; err >= 0 && (*drvr); drvr++)
		err = platform_driver_register(*drvr);
	for (lnko = xeth_mod_lnkos; err >= 0 && (*lnko); lnko++)
		err = rtnl_link_register(*lnko);
	if (err) {
		while (drvr != xeth_mod_drivers)
			platform_driver_unregister(*(--drvr));
		while (lnko != xeth_mod_lnkos)
			rtnl_link_unregister(*(--lnko));
	}
	return err;
}
module_init(xeth_mod_init);

static void __exit xeth_mod_exit(void)
{
	struct platform_driver * const *drvr = NULL;
	struct rtnl_link_ops * const *lnko = NULL;

	for (drvr = xeth_mod_drivers; *drvr; drvr++)
		platform_driver_unregister(*drvr);
	for (lnko = xeth_mod_lnkos; *lnko; lnko++)
		if ((*lnko)->list.next || (*lnko)->list.prev)
			rtnl_link_unregister(*lnko);
}
module_exit(xeth_mod_exit);

MODULE_DESCRIPTION("mux proxy netdevs with a remote switch");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(XETH_VERSION);
