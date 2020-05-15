/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

#include <linux/onie.h>

#define new_xeth_onie_str(NAME)						\
static char _xeth_onie_##NAME[onie_max_tlv];				\
									\
char *xeth_onie_##NAME(void)						\
{									\
	if (!_xeth_onie_##NAME[0]) {					\
		ssize_t sz = onie_tlv_get(onie_type_##NAME,		\
					  onie_max_tlv,			\
					  _xeth_onie_##NAME);		\
		if (sz < 0)						\
			return ERR_PTR(sz);				\
		_xeth_onie_##NAME[sz] = '\0';				\
	}								\
	return _xeth_onie_##NAME;					\
}

new_xeth_onie_str(product_name)
new_xeth_onie_str(part_number)
new_xeth_onie_str(serial_number)
new_xeth_onie_str(manufacture_date)
new_xeth_onie_str(label_revision)
new_xeth_onie_str(platform_name)
new_xeth_onie_str(manufacturer)
new_xeth_onie_str(country_code)
new_xeth_onie_str(vendor)
new_xeth_onie_str(diag_version)
new_xeth_onie_str(service_tag)

module_param_string(onie_product_name, _xeth_onie_product_name,
		    onie_max_tlv, 0);
module_param_string(onie_part_number, _xeth_onie_part_number,
		    onie_max_tlv, 0);
module_param_string(onie_serial_number, _xeth_onie_serial_number,
		    onie_max_tlv, 0);
module_param_string(onie_manufacture_date, _xeth_onie_manufacture_date,
		    onie_max_tlv, 0);
module_param_string(onie_label_revision, _xeth_onie_label_revision,
		    onie_max_tlv, 0);
module_param_string(onie_platform_name, _xeth_onie_platform_name,
		    onie_max_tlv, 0);
module_param_string(onie_manufacturer, _xeth_onie_manufacturer,
		    onie_max_tlv, 0);
module_param_string(onie_country_code, _xeth_onie_country_code,
		    onie_max_tlv, 0);
module_param_string(onie_vendor, _xeth_onie_vendor,
		    onie_max_tlv, 0);
module_param_string(onie_diag_version, _xeth_onie_diag_version,
		    onie_max_tlv, 0);
module_param_string(onie_service_tag, _xeth_onie_service_tag,
		    onie_max_tlv, 0);

static u64 _xeth_onie_mac_base;
static char _xeth_onie_mac_base_str[ETH_ALEN*3];

module_param_string(onie_mac_base, _xeth_onie_mac_base_str,
		    ARRAY_SIZE(_xeth_onie_mac_base_str), 0);
MODULE_PARM_DESC(onie_mac_base, "format XX:XX:XX:XX:XX:XX");

u64 xeth_onie_mac_base(void)
{
	u8 v[ETH_ALEN];

	if (_xeth_onie_mac_base)
		return _xeth_onie_mac_base;
	if (_xeth_onie_mac_base_str[0]) {
		unsigned int uv[ETH_ALEN];
		int err = sscanf(_xeth_onie_mac_base_str,
				 "%x:%x:%x:%x:%x:%x%*c",
				 &uv[0], &uv[1], &uv[2],
				 &uv[3], &uv[4], &uv[5]);
		if (!err) {
			int i;
			for (i = 0; i < ETH_ALEN; i++)
				v[i] = uv[i] & U8_MAX;
			_xeth_onie_mac_base = ether_addr_to_u64(v);
		}
	} else {
		ssize_t sz = 0;
		sz = onie_tlv_get(onie_type_mac_base, ETH_ALEN, v);
		if (sz != ETH_ALEN)
			eth_random_addr(v);
		_xeth_onie_mac_base = ether_addr_to_u64(v);
	}
	return _xeth_onie_mac_base;
}

static u16 _xeth_onie_num_macs;
module_param_named(onie_num_macs, _xeth_onie_num_macs, ushort, 0);

u16 xeth_onie_num_macs(void)
{
	if (!_xeth_onie_num_macs) {
		u8 v[2];
		ssize_t sz = onie_tlv_get(onie_type_num_macs, sizeof(u16), v);
		if (sz == sizeof(u16))
			_xeth_onie_num_macs = (v[0] << 8) | v[1];
	}
	return _xeth_onie_num_macs;
}

static u8 _xeth_onie_device_version;
module_param_named(onie_device_version, _xeth_onie_device_version, byte, 0);

u8 xeth_onie_device_version(void)
{
	if (!_xeth_onie_device_version) {
		u8 v[1];
		ssize_t sz = onie_tlv_get(onie_type_device_version,
					   sizeof(u8), v);
		if (sz == sizeof(u8))
			_xeth_onie_device_version = v[0];
	}
	return _xeth_onie_device_version;
}

bool xeth_onie_match(enum onie_type t, void *data, size_t ln)
{
}
