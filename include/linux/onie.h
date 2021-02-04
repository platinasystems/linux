/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __ONIE_H
#define __ONIE_H

#include <linux/device.h>

/**
 * %ONIE_ID driver interface identifier
 *
 * Regenerate this after any change to &struct onie using,
 * ``dd if=/dev/urandom bs=8 count=1 status=none |
 * 	hexdump -v -e '"0x" 8/1 "%02x"'``
 */
#define ONIE_ID 0x1197725bde48d46eULL

enum onie_type;

/* struct onie - provider interface */
struct onie {
	u64 id;
	ssize_t (*get_tlv)(const struct onie *,
			   enum onie_type, size_t, u8 *);
	int (*set_tlv)(const struct onie *,
		       enum onie_type, size_t, const u8 *);
	u64 (*mac_base)(const struct onie *);
	u16 (*num_macs)(const struct onie *);
	u8 (*device_version)(const struct onie *);
};

static const char onie_driver_name[] = "nvmem_onie";

static inline bool onie_driver_match(const char *name)
{
	return !memcmp(name, onie_driver_name, sizeof(onie_driver_name));
}

/**
 * onie_provider() - finds the ONIE provider ancestor
 */
static inline struct device *onie_provider(struct device *dev)
{
	for (;dev && dev->driver; dev = dev->parent)
		if (onie_driver_match(dev->driver->name))
			return dev;
	return NULL;
}

/**
 * onie() - returns ONIE ancestor or NULL if missing or mismatched
 */
static inline const struct onie *onie(struct device *dev)
{
	if (dev = onie_provider(dev), dev) {
		const struct onie *o = dev_get_drvdata(dev);
		return o && o->id == ONIE_ID ? o : NULL;
	}
	return NULL;
}

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
 * onie_get_tlv() - get cached ONIE NVMEM value.
 * @dev: onie ancestor or descendant
 * @t: &enum onie_type
 * @l: sizeof destination
 * @v: destination buffer
 *
 * This expects these @l sized destinations per @t type::
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
 * * -ENODEV	- @dev isn't an onie client
 * * -ENOMSG	- type @t unavailable
 * * -EINVAL	- size @l insufficient for value
 * * >=0	- value length
 */
static inline ssize_t onie_get_tlv(struct device *dev, enum onie_type t,
				   size_t l, u8 *v)
{
	const struct onie *o = onie(dev);
	return o ? o->get_tlv(o, t, l, v) : -ENODEV;
}

/**
 * onie_set_tlv() - set ONIE NVMEM value.
 * @dev: onie ancestor or descendant
 * @t: &enum onie_type
 * @l: sizeof destination
 * @v: destination buffer
 *
 * This expects @l sized destinations per @t type as @onie_get_tlv.
 *
 * Return:
 * * -ENODEV	- @dev isn't an onie client
 * * -ERANGE	- @v exceeds range of type @t
 * * -EINVAL	- size @l insufficient for value
 * * >=0	- value length
 */
static inline ssize_t onie_set_tlv(struct device *dev, enum onie_type t,
				   size_t l, u8 *v)
{
	const struct onie *o = onie(dev);
	return o ? o->set_tlv(o, t, l, v) : -ENODEV;
}

/**
 * onie_mac_base() - returns the mac_base field from ONIE
 * @dev: onie ancestor or descendant
 *
 * Returns 0 if unavailable.
 */
static inline u64 onie_mac_base(struct device *dev)
{
	const struct onie *o = onie(dev);
	return o ? o->mac_base(o) : 0;
}

/**
 * onie_num_macs() - returns the num_macs field from ONIE
 * @dev: onie ancestor or descendant
 *
 * Returns 0 if unavailable.
 */
static inline u16 onie_num_macs(struct device *dev)
{
	const struct onie *o = onie(dev);
	return o ? o->num_macs(o) : 0;
}

/**
 * onie_device_version() - returns the device_version field from ONIE
 * @dev: onie ancestor or descendant
 *
 * Returns 0 if unavailable.
 */
static inline u8 onie_device_version(struct device *dev)
{
	const struct onie *o = onie(dev);
	return o ? o->device_version(o) : 0;
}
#endif /* __ONIE_H */
