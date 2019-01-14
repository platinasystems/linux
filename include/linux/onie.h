/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __ONIE_H
#define __ONIE_H

/**
 * DOC: Theory of Operation
 *
 * This provides these /sys/bus/i2c/devices/EEPROM/onie interface files for an
 * ONIE format EEPROM::
 *
 *	country_code    mac_base*         part_number    vendor
 *	crc*            manufacture_date  platform_name  vendor_extension*
 *	device_version* manufacturer      product_name
 *	diag_version    num_macs*         serial_number
 *	label_revision  onie_version      service_tag
 *
 * See,
 * https://opencomputeproject.github.io/onie/design-spec/hw_requirements.html
 *
 * Each file has formatted text values of the respective type.  With except
 * those marked, this is simple text with the trailing newline trimmed on
 * write.
 *
 * mac_base
 *	A network link address in 00:01:02:03:04:05 format.
 *
 * num_macs
 *	A decimal, unsigned integer representing the be16_to_cpu() value.
 *
 * device_version
 *	A decimal, unsigned integer representing the 1-byte value.
 *
 * vendor_extension
 *	The concatenated contents of all such TLVs.
 *
 * crc
 *	A read-only, hexadecimal, unsigned integer in 0x%08x format
 *	representing the be32_to_cpu() value.  A write to any other file
 *	results in recalculation of the trailing CRC followed by an EEPROM
 *	update.
 *
 * A file read of a missing respective EEPROM TLV results in ENOMSG.  With an
 * unformatted EEPROM, all files return ENOMSG.
 *
 * A platform or EEPROM driver may add these onie kobjects within its init or
 * reader task like this::
 *
 *	ssize_t rem, sz;
 *	sz = max_t(size_t, EEPROM_PAGESIZE, onie_min_data);
 *	READ_EEPROM(priv->data, sz)
 *	rem = onie_validate(priv->data, sz);
 *	if (rem < 0)
 *		ERROR(rem);
 *	else if (rem)
 *		READ_EEPROM(priv->data + sz, rem)
 *	sz = onie_validate(priv->data, 0);
 *	if (sz < 0)
 *		ERROR(sz);
 *	priv->onie = onie_create(&priv->eeprom->dev.kobj, priv->data, WRITER);
 *
 * If @writer is NULL, all file writes result in -EPERM; otherwise, the CRC is
 * recalculated and the cached contents are copied to the @data given to
 * onie_create() before calling @writer to update the EEPROM.
 *
 * Note that @writer blocks the user write, so, if the EEPROM write may take a
 * while, @writer should copy @data then start another thread to write that
 * copy.  The @writer is always called with the @data given to onie_create() so
 * that it may be used with container_of() to reference the specific instance.
 *
 * See, samples/onie
 */

#define ONIE_HEADER_ID	"TlvInfo"

enum onie_sz {
	onie_sz_header_id	= sizeof(ONIE_HEADER_ID),
	onie_sz_header_version	= sizeof(u8),
	onie_sz_header_length	= sizeof(u16),
	onie_sz_header		= onie_sz_header_id + onie_sz_header_version +
		onie_sz_header_length,
	onie_sz_tlv_type	= sizeof(u8),
	onie_sz_tlv_length	= sizeof(u8),
	onie_sz_crc		= sizeof(u32),
	onie_sz_mac		= 6,
};

enum onie_min {
	onie_min_tlv	= onie_sz_tlv_type + onie_sz_tlv_length,
	onie_min_data	= onie_sz_header + onie_min_tlv + onie_sz_crc,
};

enum onie_max {
	onie_max_data	= 2048,
	onie_max_value	=  255,
};

enum onie_type {
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
 * onie_validate() - verify ONIE ID, Version, and CRC.
 * @data: ONIE EEPROM
 * @sz: if !0, return remainder
 *
 * Return:
 * * -EBADR	- sz && sz < onie_min_data
 * * -EIDRM	- invalid ID
 * * -EINVAL	- invalid Version
 * * -EFBIG	- header length > max
 * * -EBADF	- CRC mismatch
 * * 0		- no remainder
 * * >0		- total ONIE or remaining data length
 *
 * If @sz is !0, onie_validate() checks the header ID and Version before
 * returning the length of the remaining data. If @sz is 0, this also verifies
 * the trailing CRC before returning the total ONIE data length.
 */
extern ssize_t onie_validate(u8 *data, size_t sz);

/**
 * onie_create - make /sys/bus/i2c/devices/EEPROM/onie files.
 * @parent: device kobject
 * @data: ONIE EEPROM buffer (u8[onie_max_data])
 * @writer: updates EEPROM
 *
 * Return:
 * * ERR_PTR(-ENOMEM)	- priv alloc failure
 * * ERR_PTR(-EINVAL)	- kobject_init_and_add() error
 * * @onie		- kobject
 */
extern struct kobject *onie_create(struct kobject *parent, u8 *data,
				   int (*writer)(void*, size_t));

/**
 * onie_delete() - remove ``/sys/bus/i2c/devices/EEPROM/onie`` files.
 * @onie: onie_create() return value
 */
extern void onie_delete(struct kobject *onie);

/**
 * onie_value() - get cached ONIE EEPROM value.
 * @onie: onie_create() return value
 * @t: &enum onie_type
 * @sz: sizeof destination
 * @v: destination buffer
 *
 * This expects these @sz sized destinations per @t type::
 *
 *	onie_max_value	onie_type_product_name,
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
 *	onie_sz_mac	onie_type_mac_base
 *
 *	sizeof(u8)	onie_type_device_version
 *
 *	sizeof(u16)	onie_type_num_macs
 *
 *	onie_max_data	onie_type_vendor_extension
 *
 *	onie_sz_crc	onie_type_crc
 *
 * Return:
 * * -ENOMSG	- type not present
 * * -EINVAL	- @sz insufficient for value
 * * >=0	- value length
 */
extern ssize_t onie_value(struct kobject *onie,
			  enum onie_type t, size_t sz, u8 *v);

#endif /* __ONIE_H */
