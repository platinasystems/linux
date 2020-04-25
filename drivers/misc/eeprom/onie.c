/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <asm/unaligned.h>
#include <linux/onie.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/nvmem-consumer.h>

#define onie_pr_debug(format, args...)					\
	pr_debug(format "\n", ##args);

static inline u16 onie_u16(const void *p)
{
	return get_unaligned_be16(p);
}

static inline u32 onie_u32(const void *p)
{
	return get_unaligned_be32(p);
}

static inline void onie_set_u16(u16 u, void *p)
{
	put_unaligned_be16(u, p);
}

static inline void onie_set_u32(u32 u, void *p)
{
	put_unaligned_be32(u, p);
}

enum { onie_header_version = 1 };

struct __attribute__((packed)) onie_header {
	u8	id[onie_sz_header_id];
	u8	version;
	u8	length[onie_sz_header_length];
};

struct onie_tlv {
	u8 t;
	u8 l;
	u8 v[];
};

static inline struct onie_tlv *onie_next(struct onie_tlv *tlv)
{
	return (struct onie_tlv *)((u8*)tlv + onie_min_tlv + tlv->l);
}

#define until_onie_type_crc(tlv)					\
	for (; tlv->t != onie_type_crc; tlv = onie_next(tlv))

static inline u32 onie_crc(u8 *data, size_t sz)
{
	return crc32_le(~0, data, sz) ^ ~0;
}

static inline void onie_append_crc(u8 *data, size_t sz)
{
	onie_set_u32(onie_crc(data, sz), data + sz);
}

struct __attribute__((packed)) onie {
	struct onie_header header;
	struct onie_tlv tlv;
};

struct onie_attr {
	struct attribute kattr;
	u8 t;
};

#define onie_new_attr(NAME)						\
	{								\
		.kattr.name = __stringify(NAME),			\
		.kattr.mode = (S_IWUSR|S_IRUGO),			\
		.t = onie_type_##NAME,					\
	}

#define onie_new_ro_attr(NAME)						\
	{								\
		.kattr.name = __stringify(NAME),			\
		.kattr.mode = S_IRUGO,					\
		.t = onie_type_##NAME,					\
	}

static const struct onie_attr const onie_attrs[] = {
	onie_new_attr(product_name),
	onie_new_attr(part_number),
	onie_new_attr(serial_number),
	onie_new_attr(mac_base),
	onie_new_attr(manufacture_date),
	onie_new_attr(device_version),
	onie_new_attr(label_revision),
	onie_new_attr(platform_name),
	onie_new_attr(onie_version),
	onie_new_attr(num_macs),
	onie_new_attr(manufacturer),
	onie_new_attr(country_code),
	onie_new_attr(vendor),
	onie_new_attr(diag_version),
	onie_new_attr(service_tag),
	onie_new_attr(vendor_extension),
	onie_new_ro_attr(crc),
};

#define for_each_onie_attr(index)					\
	for (index = 0; index < ARRAY_SIZE(onie_attrs); index++)

struct onie_priv {
	struct kobject kobj;
	struct kobj_type ktype;
	struct onie_attr attrs[ARRAY_SIZE(onie_attrs)];
	struct attribute *kattrs[ARRAY_SIZE(onie_attrs) + 1];
	struct {
		struct mutex mutex;
		u8 data[onie_max_data];
	} cache;
	struct {
		struct mutex mutex;
		u8 *data;
		int (*writer)(void*, size_t);
	} writeback;
};

static int onie_init(void);
static void onie_exit(void);
static void onie_reset_header(struct onie_header *);
static void onie_reset_crc(struct onie_tlv *);
static void onie_release(struct kobject *);
static ssize_t onie_show(struct kobject *, struct attribute *, char *);
static ssize_t onie_store(struct kobject *, struct attribute *, const char *,
			  size_t);
static int onie_set_tlv(struct kobject *, enum onie_type, size_t, const u8 *);
static struct onie_tlv *onie_insert_tlv(struct onie_tlv *, enum onie_type,
					size_t, const u8 *, size_t *, u8 *);

module_init(onie_init);
module_exit(onie_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("a /sys interface for ONIE format EEPROMs");

static const struct sysfs_ops onie_sysfs_ops = {
	.show = onie_show,
	.store =onie_store,
};

static const char onie_header_id[] = ONIE_HEADER_ID;

ssize_t onie_validate(u8 *data, size_t sz)
{
	struct onie_header *h = (struct onie_header *)data;
	size_t tlvsz, fullsz, crcsz;
	u32 crc_read, crc_calc;

	if (sz && sz < onie_min_data)
		return -EBADR;
	if (strcmp(onie_header_id, h->id))
		return -EIDRM;
	if (h->version != onie_header_version)
		return -EINVAL;
	tlvsz = onie_u16(h->length);
	fullsz = sizeof(*h) + tlvsz;
	if (fullsz > onie_max_data)
		return -EFBIG;
	if (sz)
		return  (sz < fullsz) ? fullsz - sz : 0;
	crcsz = fullsz - onie_sz_crc;
	crc_read = onie_u32(data + crcsz);
	crc_calc = onie_crc(data, crcsz);
	if (crc_read == crc_calc)
		return fullsz;
	onie_pr_debug("crc: 0x%08x vs. 0x%08x", crc_read, crc_calc);
	onie_pr_debug("crc32_le:0: 0x%08x", crc32_le(0, data, sz));
	onie_pr_debug("crc32_be:0: 0x%08x", crc32_be(0, data, sz));
	onie_pr_debug("crc32_le:~0: 0x%08x", crc32_le(~0, data, sz));
	onie_pr_debug("crc32_be:~0: 0x%08x", crc32_be(~0, data, sz));
	onie_pr_debug("crc32_le:0:^~0: 0x%08x", crc32_le(0, data, sz)^~0);
	onie_pr_debug("crc32_be:0:^~0: 0x%08x", crc32_be(0, data, sz)^~0);
	onie_pr_debug("crc32_le:~0:^~0: 0x%08x", crc32_le(~0, data, sz)^~0);
	onie_pr_debug("crc32_be:~0:^~0: 0x%08x", crc32_be(~0, data, sz)^~0);
	return -EBADF;
}

EXPORT_SYMBOL(onie_validate);

struct kobject *onie_create(struct kobject *parent, u8 *data,
			    int (*writer)(void*, size_t))
{
	struct onie_priv *priv;
	int i, err;
	ssize_t fullsz;
	
	priv = kzalloc(sizeof(*priv), GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->ktype.sysfs_ops = &onie_sysfs_ops;
	priv->ktype.release = onie_release;
	priv->ktype.default_attrs = priv->kattrs;

	memcpy(priv->attrs, onie_attrs, sizeof(onie_attrs));
	for_each_onie_attr(i)
		priv->kattrs[i] = &priv->attrs[i].kattr;

	fullsz = onie_validate(data, 0);
	if (fullsz > 0)
		memcpy(priv->cache.data, data, fullsz);

	priv->writeback.data = data;
	priv->writeback.writer = writer;

	err = kobject_init_and_add(&priv->kobj, &priv->ktype, parent,
				   "%s", "onie");
	return (err < 0) ? ERR_PTR(err) : &priv->kobj;
}

EXPORT_SYMBOL(onie_create);

void onie_delete(struct kobject *onie)
{
	if (!IS_ERR_OR_NULL(onie)) {
		struct onie_priv *priv =
			container_of(onie, struct onie_priv, kobj);
		if (priv->kobj.parent)
			kobject_put(&priv->kobj);
		kfree(priv);
	}
}

EXPORT_SYMBOL(onie_delete);

ssize_t onie_value(struct kobject *onie, enum onie_type t, size_t sz, u8 *v)
{
	struct onie_priv *priv	= container_of(onie, struct onie_priv, kobj);
	struct onie *o = (struct onie *)priv->cache.data;
	ssize_t n = -ENOMSG;
	u16 hl;;

	mutex_lock(&priv->cache.mutex);
	hl = onie_u16(o->header.length);
	if (hl != 0) {
		struct onie_tlv *tlv;
		u8 *over = priv->cache.data + sizeof(o->header) + hl;
		for (tlv = &o->tlv; (u8*)tlv < over;  tlv = onie_next(tlv)) {
			if (tlv->t == t) {
				if (n == -ENOMSG)
					n = 0;
				if (tlv->l) {
					if (sz < n + tlv->l) {
						n = -EINVAL;
						break;
					}
					memcpy(v, tlv->v, tlv->l);
					n += tlv->l;
					v += tlv->l;
				} else	/* may have 0 length values */
					n = 0;
				if (t != onie_type_vendor_extension)
					break;
			}
		}
	}
	mutex_unlock(&priv->cache.mutex);
	return n;
}

EXPORT_SYMBOL(onie_value);

static int onie_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	struct nvmem_cell *cell;
	u8 *buf;
	size_t len;
	ssize_t sz;
	struct kobject *onie;
	
	cell = nvmem_cell_get(dev, "onie-raw");
	if (IS_ERR(cell)) {
		return PTR_ERR(cell);
	}

	buf = (u8 *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf)) {
		return PTR_ERR(buf);
	}

	sz = onie_validate(buf, len);
	if (sz < 0) {
		return sz;
	}

	onie = onie_create(&dev->kobj, buf, NULL);
	if (IS_ERR(onie)) {
		return PTR_ERR(onie);
	}
			
	platform_set_drvdata(pdev, onie);
		
	return 0;
}

static int onie_remove(struct platform_device *pdev) {
	struct kobject *onie = platform_get_drvdata(pdev);

	onie_delete(onie);
	return 0;
}

static const struct of_device_id onie_of_match[] = {
	{ .compatible = "linux,onie", },
    	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, onie_of_match);

static struct platform_driver onie_device_driver = {
	.probe = onie_probe,
	.remove = onie_remove,
	.driver = {
		.name = "onie-driver",
		.of_match_table = onie_of_match
	}
};

static int __init onie_init(void)
{
	return platform_driver_register(&onie_device_driver);
}

static void __exit onie_exit(void)
{
	return platform_driver_unregister(&onie_device_driver);
}

static void onie_reset_header(struct onie_header *h)
{
	strcpy(h->id, onie_header_id);
	h->version = onie_header_version;
	onie_set_u16(0, h->length);
}

static void onie_reset_crc(struct onie_tlv *tlv)
{
	tlv->t = onie_type_crc;
	tlv->l = onie_sz_crc;
	memset(tlv->v, 0, onie_sz_crc);
}

static void onie_release(struct kobject *onie)
{
	do {} while(0);
}

static ssize_t onie_show(struct kobject *onie, struct attribute *ka, char *buf)
{
	struct onie_attr *a = container_of(ka, struct onie_attr, kattr);
	ssize_t n;

	switch (a->t) {
	case onie_type_mac_base:
		{
			u8 v[onie_sz_mac];
			n = onie_value(onie, a->t, onie_sz_mac, v);
			if (n == onie_sz_mac)
				n = scnprintf(buf, PAGE_SIZE, "%pM", v);
			else if (n > 0)
				n = -EFBIG;
		}
		break;
	case onie_type_device_version:
		n = onie_value(onie, a->t, sizeof(u8), buf);
		if (n == sizeof(u8))
			n = scnprintf(buf, PAGE_SIZE, "%u", buf[0]);
		else if (n > 0)
			n = -EFBIG;
		break;
	case onie_type_num_macs:
		n = onie_value(onie, a->t, sizeof(u16), buf);
		if (n == sizeof(u16))
			n = scnprintf(buf, PAGE_SIZE, "%u",
				      be16_to_cpu(*(u16*)buf));
		else if (n > 0)
			n = -EFBIG;
		break;
	case onie_type_crc:
		n = onie_value(onie, a->t, onie_sz_crc, buf);
		if (n == onie_sz_crc)
			n = scnprintf(buf, PAGE_SIZE, "0x%08x",
				      be32_to_cpu(*(u32*)buf));
		else if (n > 0)
			n = -EFBIG;
		break;
	default:
		n = onie_value(onie, a->t, PAGE_SIZE, buf);
		if (n == 1 && buf[0] == '\0')
			n = 0;
		break;
	}
	return n;
}

static ssize_t onie_store(struct kobject *onie, struct attribute *ka,
			  const char *buf, size_t sz)
{
	struct onie_attr *a = container_of(ka, struct onie_attr, kattr);
	unsigned int uv[onie_sz_mac];
	u8 v[onie_sz_mac];
	int i, err = 0;

	switch (a->t) {
	case onie_type_mac_base:
		err = sscanf(buf, "%x:%x:%x:%x:%x:%x%*c",
			     &uv[0], &uv[1], &uv[2], &uv[3], &uv[4], &uv[5]);
		if (err < 0)
			break;
		if (err != onie_sz_mac)
			err = -EINVAL;
		else {
			for (i = 0; i < onie_sz_mac; i++)
				v[i] = uv[i] & U8_MAX;
			err = onie_set_tlv(onie, a->t, onie_sz_mac, v);
		}
		break;
	case onie_type_num_macs:
		err = kstrtouint(buf, 0, &uv[0]);
		if (err < 0)
			break;
		if (uv[0] > U16_MAX)
			err = -ERANGE;
		else {
			*(u16*)v = cpu_to_be16((u16)uv[0]);
			err = onie_set_tlv(onie, a->t, sizeof(u16), v);
		}
		break;
	case onie_type_device_version:
		err = kstrtouint(buf, 0, &uv[0]);
		if (err < 0)
			break;
		if (uv[0] > U8_MAX)
			err = -ERANGE;
		else {
			v[0] = uv[0] & U8_MAX;
			err = onie_set_tlv(onie, a->t, sizeof(u8), v);
		}
		break;
	case onie_type_crc:
		err = -EPERM;
		break;
	default:
		err = onie_set_tlv(onie, a->t,
				   (sz > 0 && buf[sz-1] == '\n') ? sz-1 : sz,
				   buf);
		break;
	}
	return err < 0 ? err : sz;
}

static int onie_set_tlv(struct kobject *onie,
			enum onie_type t, size_t l, const u8 *v)
{
	struct onie_priv *priv	= container_of(onie, struct onie_priv, kobj);
	struct onie_header *cache_h = (struct onie_header *)priv->cache.data;
	struct onie_header *wb_h = (struct onie_header *)priv->writeback.data;
	struct onie_tlv *cache_tlv =
		(struct onie_tlv *)(priv->cache.data + sizeof(*cache_h));
	struct onie_tlv *wb_tlv =
		(struct onie_tlv *)(priv->writeback.data + sizeof(*wb_h));
	u8 *over = priv->writeback.data + onie_max_data;
	size_t tl, hl = 0;
	int err;

	if (!priv->writeback.writer)
		return -EPERM;
	mutex_lock(&priv->cache.mutex);
	mutex_lock(&priv->writeback.mutex);
	onie_reset_header(wb_h);
	if (onie_u16(cache_h->length) != 0)
		until_onie_type_crc(cache_tlv) {
			if (cache_tlv->t != t) {
				size_t n = sizeof(*cache_tlv) + cache_tlv->l;
				memcpy(wb_tlv, cache_tlv, n);
				wb_tlv = onie_next(wb_tlv);
				hl += n;
				continue;
			}
			wb_tlv = onie_insert_tlv(wb_tlv, t, l, v, &hl, over);
			if (!wb_tlv) {
				mutex_unlock(&priv->writeback.mutex);
				mutex_unlock(&priv->cache.mutex);
				return -EINVAL;
			} else
				l = 0;
		}
	if (l) {
		wb_tlv = onie_insert_tlv(wb_tlv, t, l, v, &hl, over);
		if (!wb_tlv) {
			mutex_unlock(&priv->writeback.mutex);
			mutex_unlock(&priv->cache.mutex);
			return -EINVAL;
		}
	}
	onie_reset_crc(wb_tlv);
	hl += sizeof(*wb_tlv) + onie_sz_crc;
	onie_set_u16(hl, wb_h->length);
	tl = sizeof(*wb_h) + hl;
	onie_append_crc(priv->writeback.data, tl - onie_sz_crc);
	memcpy(priv->cache.data, priv->writeback.data, tl);
	mutex_unlock(&priv->cache.mutex);
	err = priv->writeback.writer(priv->writeback.data, tl);
	mutex_unlock(&priv->writeback.mutex);
	return err;
}

static struct onie_tlv *onie_insert_tlv(struct onie_tlv *dst, enum onie_type t,
					size_t l, const u8 *v, size_t *phl,
					u8 *over)
{
	while (l) {
		size_t n = (l < onie_max_value) ? l : onie_max_value;
		if ((u8*)dst + sizeof(*dst) + n > over)
			return NULL;
		dst->t = t;
		dst->l = n;
		memcpy(dst->v, v, n);
		dst = onie_next(dst);
		v += n;
		l -= n;
		*phl += sizeof(*dst) + n;
	}
	return dst;
}
