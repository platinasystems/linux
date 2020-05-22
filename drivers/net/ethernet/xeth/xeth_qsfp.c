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

/* @xeth_qsfp_bus: a -1 terminated list */
const int *xeth_qsfp_bus;

static const struct i2c_device_id xeth_qsfp_id_table[] = {
	{ "qsfp", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, xeth_qsfp_id_table);

enum {
	xeth_qsfp_i2c_class = I2C_CLASS_HWMON | I2C_CLASS_DDC | I2C_CLASS_SPD,
};

static int xeth_qsfp_detect(struct i2c_client *, struct i2c_board_info *);
static int xeth_qsfp_probe(struct i2c_client *);
static int xeth_qsfp_remove(struct i2c_client *);
static int xeth_qsfp_load(struct i2c_client *, u8 *, u32, u32);
static int xeth_qsfp_bload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n);
static int xeth_qsfp_xload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n);
static struct i2c_client *xeth_qsfp_scan(struct net_device *);
static int xeth_qsfp_peek(struct i2c_adapter *, unsigned short);

static struct i2c_driver xeth_qsfp_driver = {
	.class		= xeth_qsfp_i2c_class,
	.driver = {
		.name	= "xeth-platina-mk1-qsfp",
	},
	.probe_new	= xeth_qsfp_probe,
	.remove		= xeth_qsfp_remove,
	.id_table	= xeth_qsfp_id_table,
	.detect		= xeth_qsfp_detect,
	.address_list	= I2C_ADDRS(0x50, 0x51),
};

static struct spinlock xeth_qsfp_mutex;

int xeth_qsfp_register_driver(void)
{
	return i2c_add_driver(&xeth_qsfp_driver);
}

void xeth_qsfp_unregister_driver(void)
{
	i2c_del_driver(&xeth_qsfp_driver);
}

int xeth_qsfp_get_module_info(struct net_device *nd,
			      struct ethtool_modinfo *emi)
{
	struct i2c_client *qsfp;
	u8 b[2];
	u32 n = ARRAY_SIZE(b);
	int err;

	qsfp = xeth_upper_qsfp(nd);
	if (!qsfp) {
		qsfp = xeth_qsfp_scan(nd);
		if (!qsfp)
			return -ENODEV;
	}
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

static int xeth_qsfp_detect(struct i2c_client *qsfp,
			    struct i2c_board_info *info)
{
	int i;

	if (!xeth_qsfp_bus)
		return -ENXIO;

	for (i = 0; xeth_qsfp_bus[i] != qsfp->adapter->nr; i++)
		if (xeth_qsfp_bus[i] < 0)
			return -ENXIO;

	if (!xeth_qsfp_peek(qsfp->adapter, qsfp->addr))
		return -ENXIO;

	strscpy(info->type, "qsfp", sizeof(info->type));
	xeth_debug("detected qsfp %d, 0x%x", qsfp->adapter->nr, qsfp->addr);
	return 0;
}

static int xeth_qsfp_probe(struct i2c_client *qsfp)
{
	struct net_device *nd;

	nd = xeth_upper_with_qsfp_bus(qsfp->adapter->nr);
	if (!nd)
		return -ENODEV;

	if (!xeth_qsfp_peek(qsfp->adapter, qsfp->addr))
		return -ENXIO;

	xeth_upper_set_qsfp(nd, qsfp);
	xeth_debug_nd(nd, "probed qsfp %d, 0x%x", qsfp->adapter->nr, qsfp->addr);
	return 0;
}

static int xeth_qsfp_remove(struct i2c_client *qsfp)
{
	struct net_device *nd;

	nd = xeth_upper_with_qsfp_bus(qsfp->adapter->nr);
	if (!nd)
		return -ENODEV;

	xeth_upper_set_qsfp(nd, NULL);
	xeth_debug_nd(nd, "removed qsfp %d, 0x%x", qsfp->adapter->nr, qsfp->addr);
	return 0;
}

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

static struct i2c_client *xeth_qsfp_scan(struct net_device *nd)
{
	struct i2c_client *qsfp = NULL;
	struct i2c_adapter *adapter;
	struct i2c_board_info info;
	int i, nr;

	nr = xeth_upper_qsfp_bus(nd);
	adapter = i2c_get_adapter(nr);
	memset(&info, 0, sizeof(info));
	strscpy(info.type, "qsfp", sizeof(info.type));
	for (i = 0; xeth_qsfp_driver.address_list[i] != I2C_CLIENT_END; i++) {
		info.addr = xeth_qsfp_driver.address_list[i];
		if (xeth_qsfp_peek(adapter, info.addr)) {
			qsfp = i2c_new_device(adapter, &info);
			break;
		}
	}
	i2c_put_adapter(adapter);
	xeth_debug_nd(nd, "scan bus %d %s", nr, qsfp ? "ok" : "failed");
	return qsfp;
}

/* Returns 1 with matching supported module ID and 0 otherwise. */
static int xeth_qsfp_peek(struct i2c_adapter *adapter, unsigned short addr)
{
	int err;
	union i2c_smbus_data data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return 0;
	err = i2c_smbus_xfer(adapter, addr, 0, I2C_SMBUS_READ, 0,
			     I2C_SMBUS_BYTE_DATA, &data);
	if (err < 0)
		return 0;
	switch (data.byte) {
	case xeth_qsfp_module_id_qsfp:
	case xeth_qsfp_module_id_qsfp_plus:
	case xeth_qsfp_module_id_qsfp28:
	case xeth_qsfp_module_id_sfp:
		err = 1;
		break;
	default:
		err = 0;
	}
	return err;
}
