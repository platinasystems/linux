/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_qsfp.h"
#include "xeth_port.h"
#include "xeth_proxy.h"
#include "xeth_debug.h"
#include <linux/mutex.h>

static const char xeth_qsfp_drvname[] = "xeth-qsfp";

enum xeth_qsfp_module_id {
	xeth_qsfp_module_id_sfp		= 0x3,
	xeth_qsfp_module_id_qsfp	= 0xC,
	xeth_qsfp_module_id_qsfp_plus	= 0xD,
	xeth_qsfp_module_id_qsfp28	= 0x11,
};

static const char * const xeth_qsfp_module_name[] = {
	[xeth_qsfp_module_id_sfp]	= "sfp",
	[xeth_qsfp_module_id_qsfp]	= "qsfp",
	[xeth_qsfp_module_id_qsfp_plus]	= "qsfp+",
	[xeth_qsfp_module_id_qsfp28]	= "qsfp28",
};

static const struct i2c_device_id xeth_qsfp_id_table[] = {
	{ "qsfp", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, xeth_qsfp_id_table);

static int xeth_qsfp_detect(struct i2c_client *, struct i2c_board_info *);
static int xeth_qsfp_probe(struct i2c_client *);
static int xeth_qsfp_remove(struct i2c_client *);
static int xeth_qsfp_load(struct i2c_client *, u8 *, u32, u32);
static int xeth_qsfp_bload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n);
static int xeth_qsfp_xload(struct i2c_client *qsfp, u8 *data, u32 o, u32 n);
static struct i2c_client *xeth_qsfp_scan(int port);
static int xeth_qsfp_peek(int port, struct i2c_adapter *, unsigned short);

static DEFINE_MUTEX(xeth_qsfp_mutex);
static void xeth_qsfp_lock(void)	{ mutex_lock(&xeth_qsfp_mutex); }
static void xeth_qsfp_unlock(void)	{ mutex_unlock(&xeth_qsfp_mutex); }

static struct xeth_qsfp {
	struct i2c_driver driver;
	struct i2c_client **client;
	const int *nrs;
	int ports;
} xeth_qsfp = {
	.driver = {
		.driver = {
			.name = xeth_qsfp_drvname,
		},
		.class = I2C_CLASS_HWMON | I2C_CLASS_DDC | I2C_CLASS_SPD,
		.probe_new = xeth_qsfp_probe,
		.remove = xeth_qsfp_remove,
		.id_table = xeth_qsfp_id_table,
		.detect = xeth_qsfp_detect,
	},
};

/**
 * xeth_qsfp_register_driver() - add qsfp i2c driver
 * @nrs: a -1 terminated list of bus number per port
 * @addrs: a NULL terminated list, e.g. I2C_ADDRS(0x50, 0x51)
 */
void xeth_qsfp_register(struct i2c_client **clients, const int *nrs,
			const unsigned short *addrs)
{
	int err;

	xeth_qsfp.client = clients;
	xeth_qsfp.nrs = nrs;
	xeth_qsfp.driver.address_list = addrs;

	for (xeth_qsfp.ports = 0;
	     xeth_qsfp.nrs[xeth_qsfp.ports] > 0;
	     xeth_qsfp.ports++);

	err = xeth_debug_err(i2c_add_driver(&xeth_qsfp.driver));
	if (err <0)
		xeth_qsfp.ports = err;
}

void xeth_qsfp_unregister(void)
{
	int port;

	xeth_qsfp_lock();
	for (port = 0; port < xeth_qsfp.ports; port++)
		if (xeth_qsfp.client[port])
			i2c_unregister_device(xeth_qsfp.client[port]);
	xeth_qsfp_unlock();
	i2c_del_driver(&xeth_qsfp.driver);
}

int xeth_qsfp_get_module_info(int port, struct ethtool_modinfo *emi)
{
	struct i2c_client *cl;
	u8 b[2];
	u32 n = ARRAY_SIZE(b);
	int err;

	if (port >= xeth_qsfp.ports)
		return -ENXIO;
	if (cl = xeth_qsfp_scan(port), IS_ERR(cl))
		return PTR_ERR(cl);

	/* Read first 2 bytes to get Module & REV ID */
	err = xeth_qsfp_load(cl, b, 0, n);
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
		err = -EINVAL;
	}
	return err;
}

int xeth_qsfp_get_module_eeprom(int port, struct ethtool_eeprom *ee, u8 *data)
{
	struct i2c_client *cl;
	int err;

	if (port >= xeth_qsfp.ports)
		return -ENXIO;
	if (cl = xeth_qsfp_scan(port), IS_ERR(cl))
		return PTR_ERR(cl);
	if (ee->cmd != ETHTOOL_GMODULEEEPROM)
		return -EOPNOTSUPP;
	if (!ee->len)
		return -EINVAL;
	err = xeth_qsfp_load(cl, data, ee->offset, ee->len);
	return err < 0 ? err : 0;
}

static int xeth_qsfp_port(struct i2c_client *cl)
{
	int port;

	if (xeth_qsfp.ports > 0)
		return -ENXIO;

	for (port = 0; xeth_qsfp.nrs[port] >= 0; port++)
		if (xeth_qsfp.nrs[port] == cl->adapter->nr)
			return port;
	return -ENXIO;
}


static int xeth_qsfp_detect(struct i2c_client *cl, struct i2c_board_info *info)
{
	int id, port = xeth_qsfp_port(cl);

	if (port < 0)
		return port;

	id = xeth_qsfp_peek(port, cl->adapter, cl->addr);
	if (id < 0)
		return id;

	strscpy(info->type, "qsfp", I2C_NAME_SIZE);
	xeth_debug("%s port %d, bus %d, addr 0x%02x",
		   xeth_qsfp_module_name[id], port, cl->adapter->nr, cl->addr);
	return 0;
}

static int xeth_qsfp_probe(struct i2c_client *cl)
{
	int id, port = xeth_qsfp_port(cl);

	if (port < 0)
		return port;

	id = xeth_qsfp_peek(port, cl->adapter, cl->addr);
	if (id < 0)
		return id;

	xeth_debug("%s in port %d, bus %d, addr 0x%02x",
		   xeth_qsfp_module_name[id], port, cl->adapter->nr, cl->addr);
	return 0;
}

static int xeth_qsfp_remove(struct i2c_client *cl)
{
	int port;

	xeth_qsfp_lock();
	for (port = 0; xeth_qsfp.nrs[port] >= 0; port++) {
		if (xeth_qsfp.client[port] == cl) {
			xeth_debug("removed qsfp port %d", port);
			break;
		}
	}
	xeth_qsfp_unlock();
	return 0;
}

static int xeth_qsfp_load(struct i2c_client *cl, u8 *data, u32 o, u32 n)
{
	return (cl->adapter->algo->master_xfer) ?
		xeth_qsfp_xload(cl, data, o, n) :
		xeth_qsfp_bload(cl, data, o, n);
}

static int xeth_qsfp_bload(struct i2c_client *cl, u8 *data, u32 o, u32 n)
{
	int i;

	xeth_qsfp_lock();
	for (i = 0; i < n; i++) {
		s32 r = i2c_smbus_read_byte_data(cl, o + i);
		if (r < 0)
			break;
		data[i] = r;
	}
	xeth_qsfp_unlock();
	return i;
}

static int xeth_qsfp_xload(struct i2c_client *cl, u8 *data, u32 o, u32 n)
{
	u8 o8 = o;
	struct i2c_msg msgs[] = {
		{
			.addr = cl->addr,
			.flags = 0,
			.len = 1,
			.buf = &o8,
		},
		{
			.addr = cl->addr,
			.flags = I2C_M_RD,
			.len = n,
			.buf = data,
		},
	};
	int nmsgs = ARRAY_SIZE(msgs);
	int err;

	xeth_qsfp_lock();
	err = i2c_transfer(cl->adapter, msgs, nmsgs);
	xeth_qsfp_unlock();

	return err < 0 ? err : err == nmsgs ? n : 0;
}

static struct i2c_client *xeth_qsfp_scan(int port)
{
	struct i2c_client *cl;
	struct i2c_adapter *adapter;
	struct i2c_board_info info;
	int id, i;

	cl = xeth_qsfp.client[port];
	if (cl)
		return cl;
	memset(&info, 0, sizeof(info));
	strscpy(info.type, "qsfp", sizeof(info.type));
	xeth_qsfp_lock();
	adapter = i2c_get_adapter(xeth_qsfp.nrs[port]);
	for (i = 0;
	     !cl && xeth_qsfp.driver.address_list[i] != I2C_CLIENT_END;
	     i++) {
		id = xeth_qsfp_peek(port, adapter,
				    xeth_qsfp.driver.address_list[i]);
		if (id < 0)
			continue;
		info.addr = xeth_qsfp.driver.address_list[i];
		cl = i2c_new_client_device(adapter, &info);
		if (!IS_ERR(cl)) {
			xeth_qsfp.client[port] = cl;
			xeth_debug("%s port %d, bus %u, addr 0x%02x",
				   xeth_qsfp_module_name[id], port,
				   adapter->nr, info.addr);
		}
	}
	i2c_put_adapter(adapter);
	xeth_qsfp_unlock();
	return cl ? cl : ERR_PTR(-ENXIO);
}

static int xeth_qsfp_peek(int port, struct i2c_adapter *adapter,
			  unsigned short addr)
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
	case xeth_qsfp_module_id_qsfp:
	case xeth_qsfp_module_id_qsfp_plus:
	case xeth_qsfp_module_id_qsfp28:
	case xeth_qsfp_module_id_sfp:
		break;
	default:
		return -ENXIO;
	}
	return data.byte;
}
