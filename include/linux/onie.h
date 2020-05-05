/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __ONIE_H
#define __ONIE_H

#include <linux/platform_device.h>

#define ONIE_NVMEM_CELL	"onie-data"

enum onie_max {
	onie_max_data	= 2048,
	onie_max_tlv	=  255,
};

static inline ssize_t oniecpy(void *buf, const char *name, size_t n)
{
	struct kobject *kobj;

	kobj = kset_find_obj(platform_bus.kobj.kset, "onie");
	if (!kobj)
		return -ENODEV;
	/* FIXME */
	return -ENOSYS;
}

#endif /* __ONIE_H */
