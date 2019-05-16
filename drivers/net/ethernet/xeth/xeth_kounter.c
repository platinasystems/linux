/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

static struct kounters *kounters_of(struct kobject *kobj)
{
	return container_of(kobj, struct kounters, kobj);
}

static struct kounter *kounter_of(struct attribute *attr)
{
	return container_of(attr, struct kounter, attr);
}

static ssize_t kounters_show(struct kobject *kobj, struct attribute *kattr,
			     char *buf)
{
	struct kounter *kounter = kounter_of(kattr);
	long long ll;

	ll = kount(kounter);
	return scnprintf(buf, PAGE_SIZE, "%llu\n", ll);
}

static ssize_t kounters_store(struct kobject *kobj, struct attribute *kattr,
			      const char *buf, size_t len)
{
	struct kounter *kounter = kounter_of(kattr);
	long long ll;
	int err;
	
	err = kstrtoll(buf, 0, &ll);
	if (err)
		return err;
	kounter_set(kounter, (s64)ll);
	return len;
}

static struct sysfs_ops kounters_sysfs_ops = {
	.show = kounters_show,
	.store = kounters_store,
};

static void kounters_release(struct kobject *kobj)
{
	struct kounters *kounters = kounters_of(kobj);

	if (kounters->ktype.default_attrs) {
		kfree(kounters->ktype.default_attrs);
		kounters->ktype.default_attrs = NULL;
	}
}

static struct attribute **kounters_attrs(va_list kounters)
{
	int i;
	va_list clone;

	va_copy(clone, kounters);
	for (i = 0; va_arg(clone, struct kounter *); i++)
		;
	va_end(clone);
	return kcalloc(i+1, sizeof(struct kounter *), GFP_KERNEL);
}

int init_kounters(struct kounters *kounters, struct kobject *parent,
		 const char *name, ...)
{
	va_list args;
	struct kounter *kounter;
	struct attribute **attrs;

	kounters->ktype.sysfs_ops = &kounters_sysfs_ops,
	kounters->ktype.release = kounters_release,
	va_start(args, name);
	attrs = kounters_attrs(args);
	if (attrs) {
		kounters->ktype.default_attrs = attrs;
		while (kounter = va_arg(args, struct kounter *), kounter) {
			kounter->attr.mode = 0644;
			*attrs++ = &kounter->attr;
		}
	}
	va_end(args);
	if (!kounters->ktype.default_attrs)
		return -ENOMEM;
	return kobject_init_and_add(&kounters->kobj, &kounters->ktype, parent,
				    "%s", name);
}
