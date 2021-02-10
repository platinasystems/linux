/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_LB_H
#define __NET_ETHERNET_XETH_LB_H

#include <net/rtnetlink.h>

extern struct rtnl_link_ops xeth_lb_lnko;

static inline bool is_xeth_lb(struct net_device *nd)
{
	return nd->rtnl_link_ops == &xeth_lb_lnko;
}

u8 xeth_lb_chan(struct net_device *nd);

#endif /* __NET_ETHERNET_XETH_LB_H */
