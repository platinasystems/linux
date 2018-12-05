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

#include <linux/i2c.h>

#include "platina_mk1.h"

static struct i2c_adapter *platina_mk1_i2c0;
static const struct i2c_board_info const platina_mk1_i2c_mezzanine_temp_bi = {
	I2C_BOARD_INFO("lm75", 0X4f),
};
static const struct property_entry platina_mk1_i2c_eeprom_properties[] = {
	PROPERTY_ENTRY_U32("pagesize", 64),
	PROPERTY_ENTRY_BOOL("no-read-rollover"),
	{ }
};
static const struct i2c_board_info const platina_mk1_i2c_eeprom_bi = {
	I2C_BOARD_INFO("24c04", 0X51),
	.properties = platina_mk1_i2c_eeprom_properties,
};
static struct i2c_client *platina_mk1_i2c_mezzanine_temp;
static struct i2c_client *platina_mk1_i2c_eeprom;

int __init platina_mk1_i2c_init(void)
{
	int i, err;
	const size_t eeprom_size = 256;
	char *eeprom_cache;

	{
		/* FIXME set xeth.ea from eeprom instead of eth0 */
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return -ENOENT;
		xeth.ea.base = 3 + ether_addr_to_u64(eth0->dev_addr);
		xeth.ea.assign_type = eth0->addr_assign_type;
		dev_put(eth0);
	}

	platina_mk1_i2c0 = i2c_get_adapter(0);
	err = xeth_pr_is_err_val(platina_mk1_i2c0);
	if (err)
		goto egress;

	platina_mk1_i2c_mezzanine_temp =
		i2c_new_device(platina_mk1_i2c0,
			       &platina_mk1_i2c_mezzanine_temp_bi);
	err = xeth_pr_is_err_val(platina_mk1_i2c_mezzanine_temp);
	if (err)
		goto egress;

	platina_mk1_i2c_eeprom =
		i2c_new_device(platina_mk1_i2c0, &platina_mk1_i2c_eeprom_bi);
	err = xeth_pr_is_err_val(platina_mk1_i2c_eeprom);
	if (err)
		goto egress;

	eeprom_cache = kzalloc(eeprom_size, GFP_KERNEL);
	err = xeth_pr_is_err_val(eeprom_cache);
	if (err)
		goto egress;

	i2c_smbus_write_byte_data(platina_mk1_i2c_eeprom, 0, 0);
	msleep(10);
	for (i = 0; i < eeprom_size; i++)
		eeprom_cache[i] = i2c_smbus_read_byte(platina_mk1_i2c_eeprom);

	i2c_smbus_write_byte_data(platina_mk1_i2c_eeprom, 0, 0);

	xeth_pr_buf_hex_dump(eeprom_cache, eeprom_size);

	kfree(eeprom_cache);
	return 0;
egress:
	platina_mk1_i2c_exit();
	return err;
}

void platina_mk1_i2c_exit(void)
{
	if (platina_mk1_i2c_mezzanine_temp)
		i2c_unregister_device(platina_mk1_i2c_mezzanine_temp);
	if (platina_mk1_i2c_eeprom)
		i2c_unregister_device(platina_mk1_i2c_eeprom);
	if (platina_mk1_i2c0)
		i2c_put_adapter(platina_mk1_i2c0);
}

