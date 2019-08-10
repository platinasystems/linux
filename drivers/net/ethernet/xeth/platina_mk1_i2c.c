/* Platina Systems XETH driver for the MK1 top of rack ethernet switch
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/onie.h>

#include "platina_mk1.h"

enum { platina_mk1_i2c_eeprom_pgsz = 32 };

static const unsigned short platina_mk1_i2c_eeprom_addrs[] = { 0x53, 0x51 };

static size_t platina_mk1_i2c_n_eeprom_addrs(void)
{
	return ARRAY_SIZE(platina_mk1_i2c_eeprom_addrs);
}

static const struct property_entry platina_mk1_i2c_eeprom_properties[] = {
	PROPERTY_ENTRY_U32("pagesize", platina_mk1_i2c_eeprom_pgsz),
	PROPERTY_ENTRY_BOOL("no-read-rollover"),
	{},
};

static struct i2c_board_info platina_mk1_i2c_eeprom_info = {
	.type = "24c04",
	.properties = platina_mk1_i2c_eeprom_properties,
};

static struct {
	struct i2c_adapter *adapter;
	struct {
		const struct i2c_board_info info;
		struct i2c_client *client;
	} mezzanine_temp;
	struct {
		struct i2c_client *client;
		u8 data[onie_max_data];
		void *onie;
	} eeprom;
} platina_mk1_i2c = {
	.mezzanine_temp = {
		.info = {
			I2C_BOARD_INFO("lm75", 0X4f),
		},
	},
};

static ssize_t platina_mk1_i2c_eeprom_load(unsigned short addr);
static int platina_mk1_i2c_eeprom_rewind(void);
static ssize_t platina_mk1_i2c_eeprom_read(size_t offset, size_t count);

#define platina_mk1_i2c_get_adapter(NR)					\
({									\
	platina_mk1_i2c.adapter = i2c_get_adapter(NR);			\
	(platina_mk1_i2c.adapter ? 0 : -ENODEV);			\
})

#define platina_mk1_i2c_new_device(NAME)				\
({									\
	platina_mk1_i2c.NAME.client =					\
		i2c_new_device(platina_mk1_i2c.adapter,			\
			       &platina_mk1_i2c.NAME.info);		\
	(platina_mk1_i2c.NAME.client ? 0 : -ENODEV);			\
})

int __init platina_mk1_i2c_init(void)
{
	struct kobject *parent, *onie;
	ssize_t sz;
	int i, err;
	u8 v[ETH_ALEN];

	err = platina_mk1_i2c_get_adapter(0);
	if (err)
		goto platina_mk1_i2c_init_err;

	err = platina_mk1_i2c_new_device(mezzanine_temp);
	if (err)
		goto platina_mk1_i2c_init_err;

	for (i = 0; i < platina_mk1_i2c_n_eeprom_addrs(); i++) {
		unsigned short ua = platina_mk1_i2c_eeprom_addrs[i];
		sz = platina_mk1_i2c_eeprom_load(ua);
		if (sz > 0)
			break;
	}
	if (sz < 0) {
		err = sz;
		goto platina_mk1_i2c_init_err;
	}

	parent = &platina_mk1_i2c.eeprom.client->dev.kobj;
	onie = onie_create(parent, platina_mk1_i2c.eeprom.data, NULL);
	if (IS_ERR(platina_mk1_i2c.eeprom.onie)) {
		err = PTR_ERR(onie);
		goto platina_mk1_i2c_init_err;
	}
	platina_mk1_i2c.eeprom.onie = onie;

	sz = onie_value(onie, onie_type_mac_base, ETH_ALEN, v);
	if (sz == ETH_ALEN) {
		xeth.ea.base = 4 + ether_addr_to_u64(v);
		xeth.ea.assign_type = NET_ADDR_PERM;
	} else {
		eth_random_addr(v);
		xeth.ea.base = ether_addr_to_u64(v);
		xeth.ea.assign_type = NET_ADDR_RANDOM;
	}

	sz = onie_value(onie, onie_type_device_version, sizeof(u8), v);
	if (sz == sizeof(u8) && v[0] == 0)
		xeth.base = 0;
	return 0;

platina_mk1_i2c_init_err:
	platina_mk1_i2c_exit();
	return err;
}

void platina_mk1_i2c_exit(void)
{
	if (platina_mk1_i2c.eeprom.onie)
		onie_delete(platina_mk1_i2c.eeprom.onie);
	platina_mk1_i2c.eeprom.onie = NULL;
	if (platina_mk1_i2c.eeprom.client)
		i2c_unregister_device(platina_mk1_i2c.eeprom.client);
	platina_mk1_i2c.eeprom.client = NULL;
	if (platina_mk1_i2c.mezzanine_temp.client)
		i2c_unregister_device(platina_mk1_i2c.mezzanine_temp.client);
	platina_mk1_i2c.mezzanine_temp.client = NULL;
	if (platina_mk1_i2c.adapter)
		i2c_put_adapter(platina_mk1_i2c.adapter);
	platina_mk1_i2c.adapter = NULL;
}

static ssize_t platina_mk1_i2c_eeprom_load(unsigned short addr)
{
	u8 *data = platina_mk1_i2c.eeprom.data;
	int err, tries;
	ssize_t rem, sz;

	platina_mk1_i2c_eeprom_info.addr = addr;
	platina_mk1_i2c.eeprom.client =
		i2c_new_device(platina_mk1_i2c.adapter,
			       &platina_mk1_i2c_eeprom_info);
	if (!platina_mk1_i2c.eeprom.client)
		return -ENODEV;
	/* we retry b/c the EEPROM driver probe interferes w/ first read */
	for (tries = 0; tries < 3; tries++) {
		msleep(10);
		err = platina_mk1_i2c_eeprom_rewind();
		if (err)
			continue;
		msleep(10);
		sz = max_t(size_t, platina_mk1_i2c_eeprom_pgsz, onie_min_data);
		err = platina_mk1_i2c_eeprom_read(0, sz);
		if (err < 0)
			return err;
		rem = onie_validate(data, sz);
		if (rem < 0)
			continue;
		if (rem == 0)
			break;
		err = platina_mk1_i2c_eeprom_read(sz, rem);
		if (err < 0)
			return err;
		sz = onie_validate(data, 0);
		if (sz > 0)
			return sz;
	}
	return 0;
}

static int platina_mk1_i2c_eeprom_rewind()
{
	return i2c_smbus_write_byte_data(platina_mk1_i2c.eeprom.client, 0, 0);
}

static ssize_t platina_mk1_i2c_eeprom_read(size_t offset, size_t count)
{
	int i;
	s32 ret;

	for (i = 0; i < count; i++) {
		ret = i2c_smbus_read_byte(platina_mk1_i2c.eeprom.client);
		if (ret < 0)
			return ret;
		platina_mk1_i2c.eeprom.data[offset+i] = ret;
	}
	return count;
}
