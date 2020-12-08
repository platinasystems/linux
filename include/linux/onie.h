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

#define ONIE_NVMEM_CELL	"onie-data"

static const char onie_driver_name[] = "onie";

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
 * onie_match - platform device match for the ONIE probe.
 * @name: matching platform device name
 * @vendor: matching ONIE vendor string followed by comma separated aliases
 * @match: matching field string followed by comma separated aliases
 * @type: matching ONIE field type (e.g. onie_type_product_name or part_number)
 */
struct onie_match {
	const char *name;
	const char *vendor;
	const char *match;
	enum onie_type type;
};

#define ONIE_MATCH(NAME, VENDOR, MATCH, TYPE)				\
{									\
	.name = NAME,							\
	.vendor = VENDOR,						\
	.match = MATCH,							\
	.type = TYPE,							\
}

struct onie_ops {
	ssize_t (*get_tlv)(struct device *, enum onie_type, size_t, u8 *);
	int (*set_tlv)(struct device *, enum onie_type, size_t, const u8 *);
	int (*add_attrs)(struct device *);
};

/**
 * onie_dev() - returns ONIE ancester of given device.
 */
static inline struct device *onie_dev(struct device *dev)
{
	for (;dev && dev->driver; dev = dev->parent)
		if (!strcmp(onie_driver_name, dev->driver->name))
			return dev;
	return NULL;
}

/**
 * onie_get_ops() - returns ops of ONIE ancester.
 */
static inline struct onie_ops const *onie_get_ops(struct device *dev)
{
	struct device *onie = onie_dev(dev);
	struct onie_ops const **pops;

	if (!onie)
		return NULL;
	pops = (struct onie_ops const **)(dev_get_drvdata(onie));
	return pops ? *pops : NULL;
}

/**
 * onie_get_tlv() - get a cached ONIE NVMEM value.
 * @dev: onie client
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
static inline ssize_t onie_get_tlv(struct device *dev,
				   enum onie_type t, size_t l, u8 *v)
{
	struct onie_ops const *ops = onie_get_ops(dev);

	return ops ? ops->get_tlv(dev, t, l, v) : -ENODEV;
}

/**
 * onie_set_tlv() - set a ONIE NVMEM value.
 * @dev: onie client
 * @t: &enum onie_type
 * @l: sizeof destination
 * @v: destination buffer
 *
 * This expects these @l sized destinations per @t type as @onie_get_tlv.
 *
 * Return:
 * * -ENODEV	- @dev isn't an onie client
 * * -ERANGE	- @v exceeds range of type @t
 * * -EINVAL	- size @l insufficient for value
 * * >=0	- value length
 */
static inline ssize_t onie_set_tlv(struct device *dev,
				   enum onie_type t, size_t l, u8 *v)
{
	struct onie_ops const *ops = onie_get_ops(dev);

	return ops ? ops->set_tlv(dev, t, l, v) : -ENODEV;
}

/**
 * onie_add_attrs() - add ONIE sysfs attributes to given client device.
 * @dev: onie client
 *
 * Returns 0 on success or error code from sysfs_create_group on failure.
 */
static inline ssize_t onie_add_attrs(struct device *dev)
{
	struct onie_ops const *ops = onie_get_ops(dev);

	/* add attrs to client dev, not onie provider */
	return ops ? ops->add_attrs(dev) : -ENODEV;
}

#endif /* __ONIE_H */
