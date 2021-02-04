/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_PORT_H
#define __NET_ETHERNET_XETH_PORT_H

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <net/rtnetlink.h>

extern struct rtnl_link_ops xeth_port_lnko;

extern const struct net_device_ops xeth_port_ndo;

static inline bool is_xeth_port(struct net_device *nd)
{
	return nd->netdev_ops == &xeth_port_ndo;
}

struct net_device *xeth_port_probe(struct platform_device *xeth,
				   int port, int subport);

int xeth_port_of(struct net_device *nd);
int xeth_port_subport(struct net_device *nd);

u32 xeth_port_ethtool_priv_flags(struct net_device *nd);
const struct ethtool_link_ksettings *
	xeth_port_ethtool_ksettings(struct net_device *nd);
void xeth_port_ethtool_stat(struct net_device *nd, u32 index, u64 count);
void xeth_port_link_stat(struct net_device *nd, u32 index, u64 count);
void xeth_port_speed(struct net_device *nd, u32 mbps);

void xeth_port_reset_ethtool_stats(struct net_device *);

#endif /* __NET_ETHERNET_XETH_PORT_H */
