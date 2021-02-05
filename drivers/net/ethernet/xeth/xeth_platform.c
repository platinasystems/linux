/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_platform.h"
#include "xeth_mux.h"
#include "xeth_port.h"
#include "xeth_qsfp.h"
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/xeth.h>

static int xeth_platform_probe(struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	struct net_device *mux, *nd;
	int port, subport;

	if (!vendor)
		return -ENODEV;

	mux = xeth_mux_probe(xeth);
	if (IS_ERR(mux))
		return PTR_ERR(mux);
	if (!mux)
		return -ENODEV;

	dev_set_drvdata(&xeth->dev, mux);

	for (port = 0; port < vendor->n_ports; port++) {
		int subports = vendor->port.provision.subports[port];
		if (subports == 2 || subports == 4) {
			for (subport = 0; subport < subports; subport++)
				xeth_port_probe(xeth, port, subport);
		} else {
			nd = xeth_port_probe(xeth, port, -1);
		}
	}

	return 0;
}

static int xeth_platform_remove(struct platform_device *xeth)
{
	struct net_device *mux = dev_get_drvdata(&xeth->dev);
	LIST_HEAD(q);

	if (!mux)
		return 0;
	rtnl_lock();
	xeth_mux_lnko.dellink(mux, &q);
	unregister_netdevice_many(&q);
	rtnl_unlock();
	rcu_barrier();
	return 0;
}

static const struct platform_device_id xeth_platform_device_ids[] = {
	{ .name = "xeth" },
	{},
};

MODULE_DEVICE_TABLE(platform, xeth_platform_device_ids);

struct platform_driver xeth_platform_driver = {
	.driver		= { .name = KBUILD_MODNAME },
	.probe		= xeth_platform_probe,
	.remove		= xeth_platform_remove,
	.id_table	= xeth_platform_device_ids,
};
