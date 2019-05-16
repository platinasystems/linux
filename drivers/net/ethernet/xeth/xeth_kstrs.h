/**
 * xeth_kstrs - a sysfs accessible string table
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_KSTRS_H
#define __NET_ETHERNET_XETH_KSTRS_H

struct xeth_kstr {
	struct attribute attr;
	char name[8];	/* "0".."4096" */
	int index;
};

struct xeth_kstrs {
	struct spinlock mutex;
	struct kobject kobj;
	struct kobj_type ktype;
	struct xeth_kstr *kstr;
	char **str;
	size_t n;
};

/**
 * xeth_kstrs_init - initialize a sysfs accessible, NULL terminated string table
 *
 * @kstrs: pointer to the kobject to initialize
 * @parent: pointer to the parent of this kobject.
 * @name: subdir containing the string files.
 * @n: number of string entries (max 4096)
 *
 * This creates @n index named files in /sys/@parent/@name.
 * Any text writen to one of these files is stored in @kstrs->str[@index]
 * and file reads return the current contents.
 * An empty write results in a NULL string entry.
 */
int xeth_kstrs_init(struct xeth_kstrs *kstrs, struct kobject *parent,
		    const char *name, size_t n);

/**
 * xeth_kstr_string - copy string at given index of table.
 *
 * @kstrs: pointer to the initialized kobject
 * @buf: destination buffer
 * @n: buffer size
 * @i: string table index
 *
 * Returns -ERANGE if @i exceeds table and -ENOENT if string isn't set.
 * Otherwise, returns @strlen() of buf which may be 0 or may be truncated to
 * given size minus the always added NUL-terminator.
 */
ssize_t xeth_kstrs_string(struct xeth_kstrs *kstrs, char *buf, size_t n, int i);

/**
 * xeth_kstrs_count - returns the number of string table entries up to the
 * first NULL
 */
size_t xeth_kstrs_count(struct xeth_kstrs *kstrs);

#endif /* __NET_ETHERNET_XETH_KSTRS_H */
