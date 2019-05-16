/* kounter - a sysfs accessible atomic counter
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_KOUNTER_H
#define __NET_ETHERNET_XETH_KOUNTER_H

struct kounters {
	struct kobject kobj;
	struct kobj_type ktype;
};

struct kounter {
	struct attribute attr;
	atomic64_t count;
};

/**
 * init_kounters - initialize a set of sysfs accessible atomic counters
 * @kounters: pointer to the kobject container to initialize
 * @parent: pointer to the parent of this kobject.
 * @name: subdir containing the counter files.
 * @...: NULL terminated list of kounter pointers.
 *
 * The user must set the @attr.name of each @kounter before calling
 * @init_kounters.  This will then create respectively named files in
 * /sys/@parent/@name. Each file read returns an ascii descimal format current
 * value of the counter object.
 */
int init_kounters(struct kounters *kounters, struct kobject *parent,
		  const char *name, ...);

static inline s64 kount(struct kounter *kounter)
{
	return atomic64_read(&kounter->count);
}

static inline void kounter_add(s64 i, struct kounter *kounter)
{
	atomic64_add(i, &kounter->count);
}

static inline void kounter_dec(struct kounter *kounter)
{
	atomic64_dec(&kounter->count);
}

static inline void kounter_inc(struct kounter *kounter)
{
	atomic64_inc(&kounter->count);
}

static inline s64 kounter_inc_return(struct kounter *kounter)
{
	return atomic64_inc_return(&kounter->count);
}

static inline void kounter_set(struct kounter *kounter, s64 i)
{
	atomic64_set(&kounter->count, i);
}

static inline void kounter_reset(struct kounter *kounter)
{
	kounter_set(kounter, 0);
}

static inline void kounter_sub(s64 i, struct kounter *kounter)
{
	atomic64_sub(i, &kounter->count);
}

#endif /* __NET_ETHERNET_XETH_KOUNTER_H */
