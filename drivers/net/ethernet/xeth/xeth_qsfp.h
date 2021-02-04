/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_QSFP_H
#define __NET_ETHERNET_XETH_QSFP_H

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/i2c.h>

void xeth_qsfp_register(struct i2c_client **clients, const int *nrs,
			const unsigned short const *addrs);
void xeth_qsfp_unregister(void);

int xeth_qsfp_get_module_info(int port, struct ethtool_modinfo *emi);
int xeth_qsfp_get_module_eeprom(int port, struct ethtool_eeprom *ee, u8 *data);

#endif /* __NET_ETHERNET_XETH_QSFP_H */
