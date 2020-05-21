/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

enum xeth_qsfp_module_id {
	xeth_qsfp_module_id_sfp		= 0x3,
	xeth_qsfp_module_id_qsfp	= 0xC,
	xeth_qsfp_module_id_qsfp_plus	= 0xD,
	xeth_qsfp_module_id_qsfp28	= 0x11,
};

const struct i2c_device_id xeth_qsfp_id_table[] = {
	{ "qsfp", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, xeth_qsfp_id_table);

const u32 *xeth_qsfp_xids;
size_t xeth_qsfp_n_xids;

static struct spinlock xeth_qsfp_mutex;

static int xeth_qsfp_load(struct i2c_client *, u8 *, u32, u32);
static struct net_device *xeth_qsfp_nd(struct i2c_client *);

int xeth_qsfp_detect(struct i2c_client *qsfp, struct i2c_board_info *info)
{
	struct net_device *nd = xeth_qsfp_nd(qsfp);
	u8 b[2];
	u32 n = ARRAY_SIZE(b);
	int err = 0;

	if (!i2c_check_functionality(qsfp->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	if (!nd)
		return -ENXIO;

	if (xeth_qsfp_load(qsfp, b, 0, n) < 0)
		return -ENXIO;

	switch (b[0]) {
	case xeth_qsfp_module_id_qsfp:
	case xeth_qsfp_module_id_qsfp_plus:
	case xeth_qsfp_module_id_qsfp28:
	case xeth_qsfp_module_id_sfp:
		strlcpy(info->type, "qsfp", I2C_NAME_SIZE);
		info->addr = qsfp->addr;
		xeth_debug_nd(nd, "detected qsfp type 0x%x", b[0]);
		break;
	default:
		xeth_debug_nd(nd, "detected unsupported qsfp type 0x%x", b[0]);
		err = -ENXIO;
	}
	return err;
}

int xeth_qsfp_probe(struct i2c_client *qsfp)
{
	struct net_device *nd = xeth_qsfp_nd(qsfp);
	u8 b[2];
	u32 n = ARRAY_SIZE(b);
	int err = 0;

	if (!nd)
		return -ENODEV;

	if (xeth_qsfp_load(qsfp, b, 0, n) < 0)
		return -ENXIO;

	switch (b[0]) {
	case xeth_qsfp_module_id_qsfp:
	case xeth_qsfp_module_id_qsfp_plus:
	case xeth_qsfp_module_id_qsfp28:
	case xeth_qsfp_module_id_sfp:
		xeth_upper_set_qsfp(nd, qsfp);
		xeth_debug_nd(nd, "probed qsfp type 0x%x", b[0]);
		break;
	default:
		xeth_debug_nd(nd, "probed unsupported qsfp type 0x%x", b[0]);
		err = -ENXIO;
	}
	return err;
}

int xeth_qsfp_remove(struct i2c_client *qsfp)
{
	struct net_device *nd = xeth_qsfp_nd(qsfp);

	if (!nd)
		return -ENODEV;

	xeth_upper_set_qsfp(nd, NULL);

	xeth_debug_nd(nd, "qsfp removed from bus %d, addr 0x%x",
		      qsfp->adapter->nr, qsfp->addr);
	return 0;
}

int xeth_qsfp_get_module_info(struct net_device *nd,
			      struct ethtool_modinfo *emi)
{
	struct i2c_client *qsfp = xeth_upper_qsfp(nd);
	u8 b[2];
	u32 n = ARRAY_SIZE(b);
	int err;

	if (!qsfp)
		return -ENODEV;
	/* Read first 2 bytes to get Module & REV ID */
	err = xeth_qsfp_load(qsfp, b, 0, n);
	if (err < 0)
		return err;
	err = 0;
	switch (b[0]) {
	case xeth_qsfp_module_id_qsfp:
		emi->type = ETH_MODULE_SFF_8436;
		emi->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case xeth_qsfp_module_id_qsfp_plus:
		if (b[1] >= 0x3) { /* revision id */
			emi->type = ETH_MODULE_SFF_8636;
			emi->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			emi->type = ETH_MODULE_SFF_8436;
			emi->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		}
		break;
	case xeth_qsfp_module_id_qsfp28:
		emi->type = ETH_MODULE_SFF_8636;
		emi->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	case xeth_qsfp_module_id_sfp:
		emi->type = ETH_MODULE_SFF_8472;
		emi->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	default:
		xeth_debug_nd(nd, "unsupported qsfp type 0x%x", b[0]);
		err = -EINVAL;
	}
	return err;
}

int xeth_qsfp_get_module_eeprom(struct net_device *nd,
				struct ethtool_eeprom *ee,
				u8 *data)
{
	struct i2c_client *qsfp = xeth_upper_qsfp(nd);
	int err;

	if (!qsfp)
		return -ENODEV;
	if (ee->cmd != ETHTOOL_GMODULEEEPROM)
		return -EOPNOTSUPP;
	if (!ee->len)
		return -EINVAL;
	err = xeth_qsfp_load(qsfp, data, ee->offset, ee->len);
	return err < 0 ? err : 0;
}

static int xeth_qsfp_bload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n);
static int xeth_qsfp_xload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n);

static int xeth_qsfp_load(struct i2c_client *qsfp, u8 *data, u32 o, u32 n)
{
	return (qsfp->adapter->algo->master_xfer) ?
		xeth_qsfp_xload(qsfp, data, o, n) :
		xeth_qsfp_bload(qsfp, data, o, n);
}

static int xeth_qsfp_bload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n)
{
	int i;

	spin_lock(&xeth_qsfp_mutex);
	for (i = 0; i < n; i++) {
		s32 r = i2c_smbus_read_byte_data(qsfp, o + i);
		if (r < 0)
			break;
		data[i] = r;
	}
	spin_unlock(&xeth_qsfp_mutex);
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

	spin_lock(&xeth_qsfp_mutex);
	err = i2c_transfer(qsfp->adapter, msgs, nmsgs);
	spin_unlock(&xeth_qsfp_mutex);

	return err < 0 ? err : err == nmsgs ? n : 0;
}

static struct net_device *xeth_qsfp_nd(struct i2c_client *qsfp)
{
	struct i2c_adapter *a = qsfp->adapter;
	struct net_device *nd;
	u32 xid;

	if (a->nr >= xeth_qsfp_n_xids)
		return NULL;

	xid = xeth_qsfp_xids[a->nr];

	rcu_read_lock();
	nd = xeth_upper_lookup_rcu(xid);
	rcu_read_unlock();

	return nd;
}
