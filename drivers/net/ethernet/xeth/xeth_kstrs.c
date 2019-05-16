/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

static struct xeth_kstrs *xeth_kstrs_of(struct kobject *kobj)
{
	return container_of((kobj), struct xeth_kstrs, kobj);
}

static struct xeth_kstr *xeth_kstr_of(struct attribute *attr)
{
	return container_of((attr), struct xeth_kstr, attr);
}

static ssize_t xeth_kstrs_show(struct kobject *kobj, struct attribute *kattr,
			  char *buf)
{
	struct xeth_kstrs *kstrs = xeth_kstrs_of(kobj);
	struct xeth_kstr *kstr = xeth_kstr_of(kattr);
	return xeth_kstrs_string(kstrs, buf, PAGE_SIZE, kstr->index);
}

static ssize_t xeth_kstrs_store(struct kobject *kobj, struct attribute *kattr,
				const char *buf, size_t len)
{
	struct xeth_kstrs *kstrs = xeth_kstrs_of(kobj);
	struct xeth_kstr *kstr = xeth_kstr_of(kattr);
	size_t sz = (len > 0 && buf[len-1] == '\n') ? len - 1 : len;
	ssize_t ret;
	char *str; 

	spin_lock(&kstrs->mutex);

	str = kstrs->str[kstr->index];
	if (str) {
		kstrs->str[kstr->index] = NULL;
		kfree(str);
	}
	ret = len;
	if (sz > 0) {
		kstrs->str[kstr->index] = kzalloc(sz+1, GFP_KERNEL);
		if (!kstrs->str[kstr->index])
			ret = -ENOMEM;
		else
			memcpy(kstrs->str[kstr->index], buf, sz);
	}

	spin_unlock(&kstrs->mutex);
	return ret;
}

static struct sysfs_ops xeth_kstrs_sysfs_ops = {
	.show = xeth_kstrs_show,
	.store = xeth_kstrs_store,
};

static void xeth_kstrs_release(struct kobject *kobj)
{
	struct xeth_kstrs *kstrs = xeth_kstrs_of(kobj);
	int i;

	if (kstrs->ktype.default_attrs) {
		kfree(kstrs->ktype.default_attrs);
		kstrs->ktype.default_attrs = NULL;
	}
	if (kstrs->kstr) {
		kfree(kstrs->kstr);
		kstrs->kstr = NULL;
	}
	if (kstrs->str) {
		for (i = 0; i < kstrs->n; i++)
			if (kstrs->str[i]) {
				kfree(kstrs->str[i]);
				kstrs->str[i] = NULL;
			}
		kfree(kstrs->str);
		kstrs->str = NULL;
	}
}

int xeth_kstrs_init(struct xeth_kstrs *kstrs, struct kobject *parent,
		    const char *name, size_t n)
{
	int i, w;

	if (n > 4096)
		return -EINVAL;
	spin_lock_init(&kstrs->mutex);
	kstrs->ktype.sysfs_ops = &xeth_kstrs_sysfs_ops,
	kstrs->ktype.release = xeth_kstrs_release,
	kstrs->ktype.default_attrs =
		kcalloc(n+1, sizeof(struct xeth_kstr *), GFP_KERNEL);
	kstrs->kstr =  kcalloc(n, sizeof(struct xeth_kstr), GFP_KERNEL);
	kstrs->str =  kcalloc(n+1, sizeof(char *), GFP_KERNEL);
	if (!kstrs->ktype.default_attrs || !kstrs->kstr || !kstrs->str) {
		xeth_kstrs_release(&kstrs->kobj);
		return -ENOMEM;
	}
	kstrs->n = n;
	w = 1;
	if (n >= 10)
		w++;
	if (n >= 100)
		w++;
	if (n >= 1000)
		w++;
	for (i = 0; i < n; i++) {
		sprintf(kstrs->kstr[i].name, "%0*d", w, i);
		kstrs->kstr[i].index = i;
		kstrs->kstr[i].attr.name = kstrs->kstr[i].name;
		kstrs->kstr[i].attr.mode = 0644;
		kstrs->ktype.default_attrs[i] = &kstrs->kstr[i].attr;
	}
	return kobject_init_and_add(&kstrs->kobj, &kstrs->ktype, parent,
				    "%s", name);
}

ssize_t xeth_kstrs_string(struct xeth_kstrs *kstrs, char *buf, size_t n, int i)
{
	char *str;
	ssize_t ret;

	if (i >= kstrs->n)
		return -EINVAL;

	spin_lock(&kstrs->mutex);

	str = kstrs->str[i];
	ret = str ? strlcpy(buf, str, n) : -ENOENT;

	spin_unlock(&kstrs->mutex);
	return ret;
}

size_t xeth_kstrs_count(struct xeth_kstrs *kstrs)
{
	size_t n;

	for (n = 0; n < kstrs->n && kstrs->str[n]; n++)
		;
	return n;
}
