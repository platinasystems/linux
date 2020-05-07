/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __ONIE_H
#define __ONIE_H

#define ONIE_NVMEM_CELL	"onie-data"

enum onie_max {
	onie_max_data	= 2048,
	onie_max_tlv	=  255,
};

enum onie_type {
	onie_type_nvmem_name		= 0x11,
	onie_type_nvmem_cache		= 0x12,
	onie_type_product_name		= 0x21,
	onie_type_part_number		= 0x22,
	onie_type_serial_number		= 0x23,
	onie_type_mac_base		= 0x24,
	onie_type_manufacture_date	= 0x25,
	onie_type_device_version	= 0x26,
	onie_type_label_revision	= 0x27,
	onie_type_platform_name		= 0x28,
	onie_type_onie_version		= 0x29,
	onie_type_num_macs		= 0x2a,
	onie_type_manufacturer		= 0x2b,
	onie_type_country_code		= 0x2c,
	onie_type_vendor		= 0x2d,
	onie_type_diag_version		= 0x2e,
	onie_type_service_tag		= 0x2f,
	onie_type_vendor_extension	= 0xfd,
	onie_type_crc			= 0xfe,
};

/**
 * onie_tlv_get() - get cached ONIE EEPROM value.
 * @t: &enum onie_type
 * @sz: sizeof destination
 * @v: destination buffer
 *
 * This expects these @sz sized destinations per @t type::
 *
 *	onie_max_tlv	onie_type_product_name,
 *			onie_type_part_number,
 *			onie_type_serial_number,
 *			onie_type_manufacture_date,
 *			onie_type_label_revision,
 *			onie_type_platform_name,
 *			onie_type_onie_version,
 *			onie_type_manufacturer,
 *			onie_type_country_code,
 *			onie_type_vendor,
 *			onie_type_diag_version,
 *			onie_type_service_tag
 *
 *	ETH_ALEN	onie_type_mac_base
 *
 *	sizeof(u8)	onie_type_device_version
 *
 *	sizeof(u16)	onie_type_num_macs
 *
 *	sizeof(u32)	onie_type_crc
 *
 *	onie_max_data	onie_type_vendor_extension
 *
 * Return:
 * * -ENOMSG	- type not present
 * * -EINVAL	- @sz insufficient for value
 * * >=0	- value length
 */
ssize_t onie_tlv_get(enum onie_type, size_t, u8 *);

#endif /* __ONIE_H */
