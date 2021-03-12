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

#include <linux/ethtool.h>
#include <linux/i2c.h>

int xeth_qsfp_get_module_info(struct i2c_client *qsfp,
			      struct ethtool_modinfo *emi);
int xeth_qsfp_get_module_eeprom(struct i2c_client *qsfp,
				struct ethtool_eeprom *ee, u8 *data);
struct i2c_client *xeth_qsfp_client(int bus);

#endif /* __NET_ETHERNET_XETH_QSFP_H */
