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

static int xeth_qsfp_peek(struct i2c_adapter *adapter, unsigned short addr)
{
	int err;
	union i2c_smbus_data data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -ENXIO;
	err = i2c_smbus_xfer(adapter, addr, 0, I2C_SMBUS_READ, 0,
			     I2C_SMBUS_BYTE_DATA, &data);
	if (err < 0)
		return -ENXIO;
	switch (data.byte) {
	case 0x03:	/* SFP    */
	case 0x0C:	/* QSFP   */
	case 0x0D:	/* QSFP+  */
	case 0x11:	/* QSFP28 */
		break;
	default:
		return -ENXIO;
	}
	return data.byte;
}

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

struct i2c_client *xeth_qsfp_client(int nr, const unsigned short const *addrs)
{
	struct i2c_adapter *adapter;
	struct i2c_board_info info;
	struct i2c_client *cl;
	int id, i;

	memset(&info, 0, sizeof(info));
	strscpy(info.type, "qsfp", sizeof(info.type));
	adapter = i2c_get_adapter(nr);
	for (i = 0, cl = NULL; !cl && addrs[i] != I2C_CLIENT_END; i++) {
		id = xeth_qsfp_peek(adapter, addrs[i]);
		if (id > 0) {
			info.addr = addrs[i];
			cl = i2c_new_client_device(adapter, &info);
			if (IS_ERR(cl))
				cl = NULL;
		}
	}
	i2c_put_adapter(adapter);
	return cl;
}
