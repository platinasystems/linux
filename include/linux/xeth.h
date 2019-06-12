/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __XETH_H
#define __XETH_H

#include <linux/etherdevice.h>
#include <uapi/linux/xeth.h>

/**
 * xeth_add_lowers - add a NULL terminated list of lower netdevs to xeth mux
 *
 * Returns ENODEV if the xeth mux is unavailable; otherwise, returns non-zero
 * status of its ndo_add_slave or zero if all were added successfully. Also
 * returns zero if all are already lowers of xeth mux.
 */
static inline int xeth_add_lowers(struct net_device *lowers[])
{
	struct net_device *xeth_mux = dev_get_by_name(&init_net, "xeth");
	int (*ndo_add_slave)(struct net_device *upper,
			    struct net_device *lower,
			    struct netlink_ext_ack *extack);
	int i, err = 0;

	if (IS_ERR_OR_NULL(xeth_mux))
		return -ENODEV;
	ndo_add_slave = xeth_mux->netdev_ops->ndo_add_slave;
	for (i = 0; !err && lowers[i]; i++)
		err = ndo_add_slave(xeth_mux, lowers[i], NULL);
	dev_put(xeth_mux);
	return err;
}

/**
 */

/**
 * xeth_create_port - create and add an upper port proxy to the xeth mux
 *
 * @name:	IFNAMSIZ buffer
 * @xid:	A unique and immutable xeth device identifier; if zero,
 *		the device is assigned the next available xid
 * @ea:		Ethernet Address, if zero, it's assigned a random address
 * @ethtool_flag_names
 *		A NULL terminate list of flag names
 * @ethtool_cb
 *		An initialization call-back
 *
 * Returns a non-zero, negative number on error; otherwise, returns the
 * non-zero, poistive xid.
 */
s64 xeth_create_port(const char *name, u32 xid, u64 ea,
		     const char *const ethtool_flag_names[],
		     void (*ethtool_cb) (struct ethtool_link_ksettings *));

void xeth_delete_port(u32 xid);

#endif  /* __XETH_H */
