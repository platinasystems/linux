/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_platform.h"
#include "xeth_prop.h"
#include "xeth_mux.h"
#include "xeth_port.h"
#include "xeth_qsfp.h"
#include "xeth_debug.h"
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/etherdevice.h>

static int xeth_platform_probe(struct platform_device *pd);
static int xeth_platform_remove(struct platform_device *pd);

static const struct of_device_id xeth_platform_of_match[] = {
	{
		.compatible = "platina,mk1",
	},
	{},
};

MODULE_DEVICE_TABLE(of, xeth_platform_of_match);

int xeth_platform_provision[512], xeth_platform_provisioned;

module_param_array_named(provision, xeth_platform_provision, int,
			 &xeth_platform_provisioned, 0644);
MODULE_PARM_DESC(provision, " 1 (default), 2, or 4 subports per port");

static ssize_t provisioned_show(struct device_driver *drv, char *buf)
{
	int port;
	for (port = 0;
	     port < ARRAY_SIZE(xeth_platform_provision) &&
	     xeth_platform_provision[port];
	     port++)
		buf[port] = xeth_platform_provision[port] + '0';
	return port;
}

static DRIVER_ATTR_RO(provisioned);

static struct attribute *xeth_platform_attrs[] = {
	&driver_attr_provisioned.attr,
	NULL,
};

ATTRIBUTE_GROUPS(xeth_platform);

struct platform_driver xeth_platform_driver = {
	.driver		= {
		.name = KBUILD_MODNAME,
		.of_match_table = xeth_platform_of_match,
		.groups = xeth_platform_groups,
	},
	.probe		= xeth_platform_probe,
	.remove		= xeth_platform_remove,
};

ssize_t xeth_platform_subports(size_t port)
{
	return port < ARRAY_SIZE(xeth_platform_provision) ?
		xeth_platform_provision[port] : -EINVAL;
}

static int xeth_platform_probe(struct platform_device *pd)
{
	struct device *dev = &pd->dev;
	struct net_device *mux;
	char ifname[IFNAMSIZ];
	int port, subport;
	u16 ports;
	u8 base_port;

	mux = xeth_mux(dev);
	if (IS_ERR(mux))
		return PTR_ERR(mux);
	if (!mux) {
		pr_err("null mux\n");
		return -ENXIO;
	}
	platform_set_drvdata(pd, mux);

	ports = xeth_prop_ports(dev);
	base_port = xeth_prop_base_port(dev);
	for (port = 0; port < ports; port++) {
		switch (xeth_platform_provision[port]) {
		case 1:
			scnprintf(ifname, IFNAMSIZ, "xeth%d",
				  base_port + port);
			xeth_port(mux, ifname, port, -1);
			break;
		case 2:
		case 4:
			for (subport = 0;
			     subport < xeth_platform_provision[port];
			     subport++) {
				scnprintf(ifname, IFNAMSIZ, "xeth%d-%d",
					  base_port + port,
					  base_port + subport);
				xeth_port(mux, ifname, port, subport);
			}
			break;
		default:
			xeth_platform_provision[port] = 1;
			scnprintf(ifname, IFNAMSIZ, "xeth%d",
				  base_port + port);
			xeth_port(mux, ifname, port, -1);
		}
	}

	return 0;
}

static int xeth_platform_remove(struct platform_device *pd)
{
	struct net_device *mux = platform_get_drvdata(pd);
	LIST_HEAD(q);

	if (!mux)
		return 0;

	platform_set_drvdata(pd, NULL);

	rtnl_lock();
	xeth_mux_lnko.dellink(mux, &q);
	unregister_netdevice_many(&q);
	rtnl_unlock();
	rcu_barrier();

	return 0;
}
