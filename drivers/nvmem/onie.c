/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
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
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>

static const struct onie_match onie_registry[] = {
	ONIE_MATCH("platina-mk1", "Platina,Platina Systems",
		   "BT77O759.00,PS-3001-32C,PSW-3001-32C",
		   onie_type_part_number),
};

#define onie_foreach_registry_entry(i)					\
	for ((i) = 0; (i) < ARRAY_SIZE(onie_registry); (i)++)

#define ONIE_HEADER_ID	"TlvInfo"

static const char onie_header_id[] = ONIE_HEADER_ID;

enum { onie_header_version = 1 };

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

struct __attribute__((packed)) onie {
	struct onie_header header;
	struct onie_tlv tlv;
};

struct onie_priv {
	/* ops must be first priv member */
	struct	onie_ops const	*ops;
	struct	nvmem_cell	*cell;
	struct	platform_device	*client;

	struct	platform_device_info client_info;

	struct {
		struct	mutex	mutex;
		u8	data[onie_max_data];
	} cache;
	struct {
		struct	mutex mutex;
		u8	data[onie_max_data];
	} writeback;
};

static struct onie_priv *onie_priv(struct device *dev)
{
	dev = onie_dev(dev);
	if (!dev)
		return NULL;
	return dev_get_drvdata(dev);
}

static u16 onie_u16(const void *p)
{
	return get_unaligned_be16(p);
}

static u32 onie_u32(const void *p)
{
	return get_unaligned_be32(p);
}

static void onie_set_u16(u16 u, void *p)
{
	put_unaligned_be16(u, p);
}

static void onie_set_u32(u32 u, void *p)
{
	put_unaligned_be32(u, p);
}

static struct onie_tlv *onie_next(struct onie_tlv *tlv)
{
	return (struct onie_tlv *)((u8*)tlv + onie_min_tlv + tlv->l);
}

static u32 onie_crc(u8 *data, size_t sz)
{
	return crc32_le(~0, data, sz) ^ ~0;
}

#define until_onie_type_crc(tlv)					\
	for (; tlv->t != onie_type_crc; tlv = onie_next(tlv))

static void onie_append_crc(u8 *data, size_t sz)
{
	onie_set_u32(onie_crc(data, sz), data + sz);
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

/**
 * onie_cache_validate() - verify ONIE ID, Version, and CRC.
 *
 * Return:
 * * -EBADR	- sz && sz < onie_min_data
 * * -EIDRM	- invalid ID
 * * -EINVAL	- invalid Version
 * * -EFBIG	- header length > max
 * * -EBADF	- CRC mismatch
 * * >0		- total ONIE data length
 */
static ssize_t onie_cache_validate(void *data)
{
	struct onie_header *h = data;
	size_t tlvsz, fullsz, crcsz;
	u32 crc_read, crc_calc;

	if (strcmp(onie_header_id, data))
		return -EIDRM;
	if (h->version != onie_header_version)
		return -EINVAL;
	tlvsz = onie_u16(h->length);
	fullsz = sizeof(*h) + tlvsz;
	if (fullsz > onie_max_data)
		return -EFBIG;
	crcsz = fullsz - onie_sz_crc;
	crc_read = onie_u32(data + crcsz);
	crc_calc = onie_crc(data, crcsz);
#if 0
	pr_debug("crc: 0x%08x vs. 0x%08x\n", crc_read, crc_calc);
	pr_debug("crc32_le:0: 0x%08x\n", crc32_le(0, data, sz));
	pr_debug("crc32_be:0: 0x%08x\n", crc32_be(0, data, sz));
	pr_debug("crc32_le:~0: 0x%08x\n", crc32_le(~0, data, sz));
	pr_debug("crc32_be:~0: 0x%08x\n", crc32_be(~0, data, sz));
	pr_debug("crc32_le:0:^~0: 0x%08x\n", crc32_le(0, data, sz)^~0);
	pr_debug("crc32_be:0:^~0: 0x%08x\n", crc32_be(0, data, sz)^~0);
	pr_debug("crc32_le:~0:^~0: 0x%08x\n", crc32_le(~0, data, sz)^~0);
	pr_debug("crc32_be:~0:^~0: 0x%08x\n", crc32_be(~0, data, sz)^~0);
#endif
	return (crc_read == crc_calc) ? fullsz : -EBADF;
}

static ssize_t onie_priv_get_tlv(struct onie_priv *priv,
				 enum onie_type t, size_t l, u8 *v)
{
	struct onie *o;
	struct onie_tlv *tlv;
	ssize_t n;
	u16 hl;
	u8 *end;

	o = (struct onie *)priv->cache.data;
	mutex_lock(&priv->cache.mutex);
	n = onie_cache_validate(priv->cache.data);
	if (n < 0)
		goto onie_priv_get_tlv_exit;
	n = -ENOMSG;
	hl = onie_u16(o->header.length);
	if (!hl) {
		goto onie_priv_get_tlv_exit;
	}
	end = priv->cache.data + sizeof(o->header) + hl;
	for (tlv = &o->tlv; (u8*)tlv < end; tlv = onie_next(tlv))
		if (tlv->t == t) {
			if (n < 0)
				n = 0;
			if (tlv->l) {
				if (l < n + tlv->l) {
					n = -EINVAL;
					goto onie_priv_get_tlv_exit;
				}
				memcpy(v, tlv->v, tlv->l);
				n += tlv->l;
				v += tlv->l;
			} else	/* may have 0 length values */
				n = 0;
			if (t != onie_type_vendor_extension)
				goto onie_priv_get_tlv_exit;
		}
onie_priv_get_tlv_exit:
	mutex_unlock(&priv->cache.mutex);
	return n;
}

static struct onie_tlv *onie_insert_tlv(struct onie_tlv *dst, enum onie_type t,
					size_t l, const u8 *v, size_t *phl,
					u8 *over)
{
	while (l) {
		size_t n = (l < onie_max_tlv) ? l : onie_max_tlv;
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

static int onie_priv_set_tlv(struct onie_priv *priv,
			     enum onie_type t, size_t l, const u8 *v)
{
	struct onie_header *cache_h,*wb_h;
	struct onie_tlv *cache_tlv, *wb_tlv;
	u8 *over;
	size_t tl, hl = 0;

	cache_h = (struct onie_header *)priv->cache.data;
	wb_h = (struct onie_header *)priv->writeback.data;
	cache_tlv = (struct onie_tlv *)(priv->cache.data + sizeof(*cache_h));
	wb_tlv = (struct onie_tlv *)(priv->writeback.data + sizeof(*wb_h));
	over = priv->writeback.data + onie_max_data;

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
	mutex_unlock(&priv->writeback.mutex);
	return 0;
}

static ssize_t onie_op_get_tlv(struct device *dev, enum onie_type t,
				size_t l, u8 *v)
{
	return onie_priv_get_tlv(onie_priv(dev), t, l, v);
}

static int onie_op_set_tlv(struct device *dev, enum onie_type t,
			    size_t l, const u8 *v)
{
	return onie_priv_set_tlv(onie_priv(dev), t, l, v);
}

static const struct attribute_group *onie_attr_groups[];

static int onie_op_add_attrs(struct device *dev)
{
	return devm_device_add_groups(dev, onie_attr_groups);
}

static bool onie_aka_match(const char *name, const char *akas)
{
	while (akas) {
		const char *comma = strchr(akas, ',');
		if (comma) {
			if (!strncmp(name, akas, comma-akas))
				return true;
			else
				akas = comma+1;
		} else
			return strcmp(name, akas) == 0;
	}
	return false;
}

static const char *onie_priv_match(struct onie_priv *priv)
{
	size_t i;
	ssize_t n;
	char v[onie_max_tlv+1];

	onie_foreach_registry_entry(i) {
		const char *name = onie_registry[i].name;
		const char *vendor = onie_registry[i].vendor;
		const char *match = onie_registry[i].match;
		enum onie_type t = onie_registry[i].type;
		n = onie_priv_get_tlv(priv, onie_type_vendor, onie_max_tlv, v);
		if (n <= 0)
			continue;
		v[n] = 0;
		if (!onie_aka_match(v, vendor))
			continue;
		n = onie_priv_get_tlv(priv, t, onie_max_tlv, v);
		if (n <= 0)
			continue;
		v[n] = 0;
		if (onie_aka_match(v, match))
			return name;
	}
	pr_err("no matching dev");
	return NULL;
}

static int onie_priv_new_client(struct onie_priv *priv, struct device *parent)
{
	const char *name = onie_priv_match(priv);
	if (!name)
		return -ENODEV;
	priv->client_info.name = name;
	priv->client_info.id = -1;
	priv->client_info.res = NULL;
	priv->client_info.num_res = 0;
	priv->client_info.parent = parent;
	priv->client = platform_device_register_full(&priv->client_info);
	if (IS_ERR(priv->client)) {
		int err = PTR_ERR(priv->client);
		priv->client = NULL;
		pr_err("create %s: %d\n", priv->client_info.name, err);
	}
	return 0;
}

static const struct onie_ops onie_ops = {
	onie_op_get_tlv,
	onie_op_set_tlv,
	onie_op_add_attrs,
};

static int onie_probe(struct platform_device *provider)
{
	struct nvmem_cell *cell;
	struct onie_priv *priv;
	size_t n;
	void *data;
	int err;

	cell = nvmem_cell_get(&provider->dev, ONIE_NVMEM_CELL);
	if (IS_ERR(cell)) {
		err = PTR_ERR(cell);
		return err == -EPROBE_DEFER ? err : -ENXIO;
	} else if (!cell)
		return -ENXIO;

	data = nvmem_cell_read(cell, &n);
	if (IS_ERR(data)) {
		err = PTR_ERR(data);
		pr_err("read %s " ONIE_NVMEM_CELL ": %d\n",
		       provider->name, err);
		goto onie_probe_err;
	}
	if (!data) {
		err = -EINVAL;
		pr_err("read %s " ONIE_NVMEM_CELL ": nil\n", provider->name);
		goto onie_probe_err;
	}
	pr_debug("%s read " ONIE_NVMEM_CELL " ok", provider->name);

	priv = devm_kzalloc(&provider->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto onie_probe_err;
	}
	platform_set_drvdata(provider, priv);
	priv->ops = &onie_ops;
	priv->cell = cell;
	if (n > 0)
		memcpy(priv->cache.data, data, n);
	kfree(data);

	return (onie_cache_validate(priv->cache.data) < 0 ||
		onie_priv_new_client(priv, &provider->dev) < 0) ?
		onie_add_attrs(&provider->dev) : 0;

onie_probe_err:
	if (!IS_ERR_OR_NULL(data))
		kfree(data);
	if (cell)
		nvmem_cell_put(cell);
	return err;
}

static int onie_remove(struct platform_device *provider)
{
	struct onie_priv *priv = dev_get_drvdata(&provider->dev);
	if (!priv)
		return -ENOMEM;
	if (priv->cell)
		nvmem_cell_put(priv->cell);
	if (priv->client)
		platform_device_unregister(priv->client);
	return 0;
}

static ssize_t onie_show_default(struct device *dev,
				 enum onie_type t,
				 char *buf)
{
	ssize_t n = onie_get_tlv(dev, t, PAGE_SIZE, buf);
	if (n == 1 && buf[0] == '\0')
		n = 0;
	return n;
}

#define onie_show(NAME)							\
static ssize_t onie_show_##NAME(struct device *dev,			\
				struct device_attribute *attr,		\
				char *buf)				\
{									\
	return onie_show_default(dev, onie_type_##NAME, buf);		\
}

onie_show(product_name)
onie_show(part_number)
onie_show(serial_number)
onie_show(manufacture_date)
onie_show(label_revision)
onie_show(platform_name)
onie_show(onie_version)
onie_show(manufacturer)
onie_show(country_code)
onie_show(vendor)
onie_show(diag_version)
onie_show(service_tag)
onie_show(vendor_extension)

static ssize_t onie_show_cache(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct onie_priv *priv = onie_priv(dev);
	mutex_lock(&priv->cache.mutex);
	memcpy(buf, priv->cache.data, onie_max_data);
	mutex_unlock(&priv->cache.mutex);
	return onie_max_data;
}

static ssize_t onie_show_mac_base(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	u8 v[onie_sz_mac];
	ssize_t n = onie_get_tlv(dev, onie_type_mac_base, onie_sz_mac, v);
	if (n == onie_sz_mac)
		n = scnprintf(buf, PAGE_SIZE, "%pM", v);
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_device_version(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t n = onie_get_tlv(dev, onie_type_device_version,
				     sizeof(u8), buf);
	if (n == sizeof(u8))
		n = scnprintf(buf, PAGE_SIZE, "%u", buf[0]);
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_num_macs(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	ssize_t n = onie_get_tlv(dev, onie_type_num_macs,
				     sizeof(u16), buf);
	if (n == sizeof(u16))
		n = scnprintf(buf, PAGE_SIZE, "%u", be16_to_cpu(*(u16*)buf));
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_crc(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	ssize_t n = onie_get_tlv(dev, onie_type_crc, onie_sz_crc, buf);
	if (n == onie_sz_crc)
		n = scnprintf(buf, PAGE_SIZE, "0x%08x",
			      be32_to_cpu(*(u32*)buf));
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_store_default(struct device *dev,
				  enum onie_type t, size_t sz, const char *buf)
{
	ssize_t l = (sz > 0 && buf[sz-1] == '\n') ? sz-1 : sz;
	int err = onie_set_tlv(dev, t, l, (u8*)(buf));
	return err ? err : sz;
}

#define onie_store(NAME)						\
static ssize_t onie_store_##NAME(struct device *dev,			\
				 struct device_attribute *attr,		\
				 const char *buf, size_t sz)		\
{									\
	return onie_store_default(dev, onie_type_##NAME, sz, buf);	\
}

onie_store(product_name)
onie_store(part_number)
onie_store(serial_number)
onie_store(manufacture_date)
onie_store(label_revision)
onie_store(platform_name)
onie_store(onie_version)
onie_store(manufacturer)
onie_store(country_code)
onie_store(vendor)
onie_store(diag_version)
onie_store(service_tag)
onie_store(vendor_extension)

static ssize_t onie_store_cache(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t sz)
{
	struct onie_priv *priv = onie_priv(dev);
	if (sz >= onie_max_data)
		return -ERANGE;
	mutex_lock(&priv->cache.mutex);
	memcpy(priv->cache.data, buf, sz);
	mutex_unlock(&priv->cache.mutex);
	return sz;
}

static ssize_t onie_store_mac_base(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t sz)
{
	unsigned int uv[onie_sz_mac];
	u8 v[onie_sz_mac];
	int i, err;

	err = sscanf(buf, "%x:%x:%x:%x:%x:%x%*c",
		     &uv[0], &uv[1], &uv[2], &uv[3], &uv[4], &uv[5]);
	if (err < 0)
		return err;
	if (err != onie_sz_mac)
		return -EINVAL;
	for (i = 0; i < onie_sz_mac; i++)
		v[i] = uv[i] & U8_MAX;
	err = onie_set_tlv(dev, onie_type_mac_base, onie_sz_mac, v);
	return err ? err : sz;
}

static ssize_t onie_store_num_macs(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t sz)
{
	unsigned int uv;
	u8 v[sizeof(u16)];
	int err;

	err = kstrtouint(buf, 0, &uv);
	if (err < 0)
		return err;
	if (uv > U16_MAX)
		return -ERANGE;
	*(u16*)v = cpu_to_be16((u16)uv);
	err = onie_set_tlv(dev, onie_type_num_macs, sizeof(u16), v);
	return err ? err : sz;
}

static ssize_t onie_store_device_version(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t sz)
{
	unsigned int uv;
	u8 v[sizeof(u8)];
	int err;

	err = kstrtouint(buf, 0, &uv);
	if (err < 0)
		return err;
	if (uv > U8_MAX)
		return -ERANGE;
	v[0] = uv & U8_MAX;
	err = onie_set_tlv(dev, onie_type_device_version, sizeof(u8), v);
	return err ? err : sz;
}

static ssize_t onie_store_crc(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t sz)
{
	struct onie_priv *priv = onie_priv(dev);
	ssize_t n;

	n = onie_cache_validate(priv->cache.data);
	if (n <= 0)
		return n;
	if (priv->cell)
		n = nvmem_cell_write(priv->cell, priv->cache.data, n);
	else
		n = -ENODEV;
	return (n < 0) ? n : sz;
}

#define onie_attr(NAME) \
struct device_attribute onie_attr_##NAME = \
	__ATTR(NAME, 0644, onie_show_##NAME, onie_store_##NAME)

static onie_attr(cache);
static onie_attr(product_name);
static onie_attr(part_number);
static onie_attr(serial_number);
static onie_attr(mac_base);
static onie_attr(manufacture_date);
static onie_attr(device_version);
static onie_attr(label_revision);
static onie_attr(platform_name);
static onie_attr(onie_version);
static onie_attr(num_macs);
static onie_attr(manufacturer);
static onie_attr(country_code);
static onie_attr(vendor);
static onie_attr(diag_version);
static onie_attr(service_tag);
static onie_attr(vendor_extension);
static onie_attr(crc);

static struct attribute *onie_attrs[] = {
	&onie_attr_cache.attr,
	&onie_attr_product_name.attr,
	&onie_attr_part_number.attr,
	&onie_attr_serial_number.attr,
	&onie_attr_mac_base.attr,
	&onie_attr_manufacture_date.attr,
	&onie_attr_device_version.attr,
	&onie_attr_label_revision.attr,
	&onie_attr_platform_name.attr,
	&onie_attr_onie_version.attr,
	&onie_attr_num_macs.attr,
	&onie_attr_manufacturer.attr,
	&onie_attr_country_code.attr,
	&onie_attr_vendor.attr,
	&onie_attr_diag_version.attr,
	&onie_attr_service_tag.attr,
	&onie_attr_vendor_extension.attr,
	&onie_attr_crc.attr,
	NULL,
};

static const struct attribute_group onie_attr_group = {
	.attrs = onie_attrs,
};

static const struct attribute_group *onie_attr_groups[] = {
	&onie_attr_group,
	NULL,
};

static const struct of_device_id onie_of_match[] = {
	{ .compatible = "linux,onie", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, onie_of_match);

static struct platform_driver onie_platform_driver = {
	.driver = {
		.name = "onie",
		.of_match_table = onie_of_match,
	},
	.probe = onie_probe,
	.remove = onie_remove,
};

module_platform_driver(onie_platform_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("a /sys interface for ONIE format NVMEM");
