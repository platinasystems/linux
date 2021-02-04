/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_SBRX_H
#define __NET_ETHERNET_XETH_SBRX_H

#include <linux/netdevice.h>

int xeth_sbrx(struct net_device *mux, struct socket *conn, void *data);

#endif	/* __NET_ETHERNET_XETH_SBRX_H */
