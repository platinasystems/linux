/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_qsfp.h"
#include <linux/mutex.h>

static DEFINE_MUTEX(xeth_qsfp_mutex);

static void xeth_qsfp_lock(void)	{ mutex_lock(&xeth_qsfp_mutex); }
static void xeth_qsfp_unlock(void)	{ mutex_unlock(&xeth_qsfp_mutex); }

static int xeth_qsfp_bload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n)
{
	int i;

	xeth_qsfp_lock();
	for (i = 0; i < n; i++) {
		s32 r = i2c_smbus_read_byte_data(qsfp, o + i);
		if (r < 0)
			break;
		data[i] = r;
	}
	xeth_qsfp_unlock();
	return i;
}

static int xeth_qsfp_xload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n)
{
	u8 o8 = o;
	struct i2c_msg msgs[] = {
		{
			.addr = qsfp->addr,
			.flags = 0,
			.len = 1,
			.buf = &o8,
		},
		{
			.addr = qsfp->addr,
			.flags = I2C_M_RD,
			.len = n,
			.buf = data,
		},
	};
	int nmsgs = ARRAY_SIZE(msgs);
	int err;

	xeth_qsfp_lock();
	err = i2c_transfer(qsfp->adapter, msgs, nmsgs);
	xeth_qsfp_unlock();

	return err < 0 ? err : err == nmsgs ? n : 0;
}

static int xeth_qsfp_load(struct i2c_client *qsfp, u8 *data, u32 o, u32 n)
{
	return (qsfp->adapter->algo->master_xfer) ?
		xeth_qsfp_xload(qsfp, data, o, n) :
		xeth_qsfp_bload(qsfp, data, o, n);
}

int xeth_qsfp_get_module_info(struct i2c_client *qsfp,
			      struct ethtool_modinfo *emi)
{
	u8 b[2];
	u32 n = ARRAY_SIZE(b);
	int err;

	/* Read first 2 bytes to get Module & REV ID */
	err = xeth_qsfp_load(qsfp, b, 0, n);
	if (err < 0)
		return err;
	err = 0;
	switch (b[0]) {
	case 0x03:	/* SFP    */
		emi->type = ETH_MODULE_SFF_8472;
		emi->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case 0x0C:	/* QSFP   */
		emi->type = ETH_MODULE_SFF_8436;
		emi->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case 0x0D:	/* QSFP+  */
		if (b[1] >= 0x3) { /* revision id */
			emi->type = ETH_MODULE_SFF_8636;
			emi->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			emi->type = ETH_MODULE_SFF_8436;
			emi->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		}
		break;
	case 0x11:	/* QSFP28 */
		emi->type = ETH_MODULE_SFF_8636;
		emi->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

int xeth_qsfp_get_module_eeprom(struct i2c_client *qsfp,
				struct ethtool_eeprom *ee, u8 *data)
{
	int err;

	if (ee->cmd != ETHTOOL_GMODULEEEPROM)
		return -EOPNOTSUPP;
	if (!ee->len)
		return -EINVAL;
	err = xeth_qsfp_load(qsfp, data, ee->offset, ee->len);
	return err < 0 ? err : 0;
}
