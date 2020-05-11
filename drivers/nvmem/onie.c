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

static struct platform_driver onie_platform_driver;
module_platform_driver(onie_platform_driver);

enum onie_param_ {
	onie_nvmem_name_sz = 64,
};

static char onie_nvmem_name[onie_nvmem_name_sz];
module_param_string(nvmem, onie_nvmem_name, onie_nvmem_name_sz, 0);
MODULE_PARM_DESC(nvmem, " use named device or empty data if \".\"; "
		 "otherwise, use \"onie-data\" cell");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("a /sys interface for ONIE format NVMEM");

static struct platform_device *onie_pdev;

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
	struct	nvmem_cell	*nvmem_cell;
	struct	nvmem_device	*nvmem_dev;
	char	nvmem_name[onie_nvmem_name_sz];
	struct {
		struct	mutex	mutex;
		u8	data[onie_max_data];
	} cache;
	struct {
		struct	mutex mutex;
		u8	data[onie_max_data];
	} writeback;
};

static struct attribute *onie_attributes[];

static const struct attribute_group onie_attribute_group = {
	.name = "tlv",
	.attrs = onie_attributes,
};

static const struct attribute_group *onie_attribute_groups[] = {
	&onie_attribute_group,
	NULL,
};

static const struct of_device_id onie_of_match[] = {
	{ .compatible = "linux,onie", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, onie_of_match);

static int onie_probe(struct platform_device *pdev);
static int onie_remove(struct platform_device *pdev);

static struct platform_driver onie_platform_driver = {
	.driver = {
		.name = "onie",
		.of_match_table = onie_of_match,
		.groups = onie_attribute_groups,
	},
	.probe = onie_probe,
	.remove = onie_remove,
};

static int onie_cache_fill(struct platform_device *pdev);

static int onie_tlv_set(struct device *dev,
			enum onie_type, size_t, const u8 *);
static struct onie_tlv *onie_tlv_insert(struct onie_tlv *, enum onie_type,
					size_t, const u8 *, size_t *, u8 *);

static u16 onie_u16(const void *p);
static u32 onie_u32(const void *p);
static void onie_set_u16(u16 u, void *p);
static void onie_set_u32(u32 u, void *p);
static struct onie_tlv *onie_next(struct onie_tlv *tlv);
static u32 onie_crc(u8 *data, size_t sz);
static void onie_append_crc(u8 *data, size_t sz);

#define until_onie_type_crc(tlv)					\
	for (; tlv->t != onie_type_crc; tlv = onie_next(tlv))

static int onie_probe(struct platform_device *pdev)
{
	if (onie_pdev) {
		pr_err("ignoring %s, already using %s\n",
		       pdev->name, onie_pdev->name);
		return -EEXIST;
	}
	return onie_cache_fill(pdev);
}

static int onie_remove(struct platform_device *pdev)
{
	pr_debug("%s\n", pdev->name);
	dev_set_drvdata(&pdev->dev, NULL);
	onie_pdev = NULL;
	return 0;
}

static ssize_t onie_validate(void*);

static int onie_cache_flush(struct device *dev)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	ssize_t n;
	
	n = onie_validate(priv->cache.data);
	if (n <= 0)
		return n;
	if (priv->nvmem_cell)
		n = nvmem_cell_write(priv->nvmem_cell,
				     priv->cache.data, n);
	else if (priv->nvmem_dev)
		n = nvmem_device_write(priv->nvmem_dev, 0, n,
				       priv->cache.data);
	else
		n = -ENODEV;
	return n;
}

static int onie_cache_fill_device(struct platform_device *pdev)
{
	struct onie_priv *priv = dev_get_drvdata(&pdev->dev);
	int err;

	priv->nvmem_dev = nvmem_device_get(&pdev->dev, onie_nvmem_name);
	if (IS_ERR(priv->nvmem_dev)) {
		err = PTR_ERR(priv->nvmem_dev);
		priv->nvmem_dev = NULL;
		pr_err("%s get %s: %d\n", pdev->name, onie_nvmem_name, err);
		return err;
	} else if (!priv->nvmem_dev) {
		pr_err("%s got null %s\n", pdev->name, onie_nvmem_name);
		return -ENODEV;
	}
	err = nvmem_device_read(priv->nvmem_dev, 0, onie_max_data,
				priv->cache.data);
	if (err < 0) {
		nvmem_device_put(priv->nvmem_dev);
		priv->nvmem_dev = NULL;
		pr_debug("%s read %s: %d\n", pdev->name, onie_nvmem_name, err);
		return err;
	}
	onie_pdev = pdev;
	strlcpy(priv->nvmem_name, onie_nvmem_name, onie_nvmem_name_sz);
	pr_debug("%s read %s ok", pdev->name, priv->nvmem_name);
	return 0;
}

static int onie_cache_fill_cell(struct platform_device *pdev)
{
	struct onie_priv *priv = dev_get_drvdata(&pdev->dev);
	size_t n;
	void *data;
	int err;

	priv->nvmem_cell = nvmem_cell_get(&pdev->dev, ONIE_NVMEM_CELL);
	if (IS_ERR(priv->nvmem_cell)) {
		err = PTR_ERR(priv->nvmem_cell);
		priv->nvmem_cell = NULL;
		pr_err("%s get "ONIE_NVMEM_CELL ": %d\n", pdev->name, err);
		return err;
	} else if (!priv->nvmem_cell) {
		pr_err("%s got nil " ONIE_NVMEM_CELL "\n", pdev->name);
		return -ENODEV;
	}
	data = nvmem_cell_read(priv->nvmem_cell, &n);
	if (IS_ERR(data)) {
		nvmem_cell_put(priv->nvmem_cell);
		err = PTR_ERR(data);
		pr_err("%s read " ONIE_NVMEM_CELL ": %d\n", pdev->name, err);
		return err;
	}
	if (!data) {
		nvmem_cell_put(priv->nvmem_cell);
		pr_err("%s read " ONIE_NVMEM_CELL ": nil\n", pdev->name);
		return -EINVAL;
	}
	if (n > 0) {
		memcpy(priv->cache.data, data, n);
		kfree(data);
		strcpy(priv->nvmem_name, "cell:"ONIE_NVMEM_CELL);
	}
	onie_pdev = pdev;
	pr_debug("%s read " ONIE_NVMEM_CELL " ok", pdev->name);
	return 0;
}

static int onie_cache_fill(struct platform_device *pdev)
{
	struct onie_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, priv);

	if (!strcmp(onie_nvmem_name, ".")) {
		strcpy(priv->nvmem_name, onie_nvmem_name);
		onie_pdev = pdev;
		return 0;
	}
	return onie_nvmem_name[0] ?
		onie_cache_fill_device(pdev) :
		onie_cache_fill_cell(pdev);
}

/**
 * onie_validate() - verify ONIE ID, Version, and CRC.
 *
 * Return:
 * * -EBADR	- sz && sz < onie_min_data
 * * -EIDRM	- invalid ID
 * * -EINVAL	- invalid Version
 * * -EFBIG	- header length > max
 * * -EBADF	- CRC mismatch
 * * >0		- total ONIE data length
 */
static ssize_t onie_validate(void *data)
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

static ssize_t onie_show_default(u8 t, char *buf)
{
	ssize_t n = onie_tlv_get(t, PAGE_SIZE, buf);
	if (n == 1 && buf[0] == '\0')
		n = 0;
	return n;
}

#define new_onie_show(NAME)						\
static ssize_t onie_show_##NAME(struct device *dev,			\
				struct device_attribute *kda,		\
				char *buf)				\
{									\
	return onie_show_default(onie_type_##NAME, buf);		\
}

new_onie_show(product_name)
new_onie_show(part_number)
new_onie_show(serial_number)
new_onie_show(manufacture_date)
new_onie_show(label_revision)
new_onie_show(platform_name)
new_onie_show(onie_version)
new_onie_show(manufacturer)
new_onie_show(country_code)
new_onie_show(vendor)
new_onie_show(diag_version)
new_onie_show(service_tag)
new_onie_show(vendor_extension)

static ssize_t onie_show_cache(struct device *dev,
			       struct device_attribute *kda,
			       char *buf)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	mutex_lock(&priv->cache.mutex);
	memcpy(buf, priv->cache.data, onie_max_data);
	mutex_unlock(&priv->cache.mutex);
	return onie_max_data;
}

static ssize_t onie_show_nvmem(struct device *dev,
			       struct device_attribute *kda,
			       char *buf)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s", priv->nvmem_name);
}

static ssize_t onie_show_mac_base(struct device *dev,
				  struct device_attribute *kda,
				  char *buf)
{
	u8 v[onie_sz_mac];
	ssize_t n = onie_tlv_get(onie_type_mac_base, onie_sz_mac, v);
	if (n == onie_sz_mac)
		n = scnprintf(buf, PAGE_SIZE, "%pM", v);
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_device_version(struct device *dev,
					struct device_attribute *kda,
					char *buf)
{
	ssize_t n = onie_tlv_get(onie_type_device_version, sizeof(u8), buf);
	if (n == sizeof(u8))
		n = scnprintf(buf, PAGE_SIZE, "%u", buf[0]);
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_num_macs(struct device *dev,
				  struct device_attribute *kda,
				  char *buf)
{
	ssize_t n = onie_tlv_get(onie_type_num_macs, sizeof(u16), buf);
	if (n == sizeof(u16))
		n = scnprintf(buf, PAGE_SIZE, "%u", be16_to_cpu(*(u16*)buf));
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_crc(struct device *dev,
			     struct device_attribute *kda,
			     char *buf)
{
	ssize_t n = onie_tlv_get(onie_type_crc, onie_sz_crc, buf);
	if (n == onie_sz_crc)
		n = scnprintf(buf, PAGE_SIZE, "0x%08x",
			      be32_to_cpu(*(u32*)buf));
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_store_default(struct device *dev,
				  u8 t, size_t sz, const char *buf)
{
	ssize_t l = (sz > 0 && buf[sz-1] == '\n') ? sz-1 : sz;
	int err = onie_tlv_set(dev, t, l, buf);
	return err ? err : sz;
}

#define new_onie_store(NAME)						\
static ssize_t onie_store_##NAME(struct device *dev,			\
				 struct device_attribute *kda,		\
				 const char *buf, size_t sz)		\
{									\
	return onie_store_default(dev, onie_type_##NAME, sz, buf);	\
}

new_onie_store(product_name)
new_onie_store(part_number)
new_onie_store(serial_number)
new_onie_store(manufacture_date)
new_onie_store(label_revision)
new_onie_store(platform_name)
new_onie_store(onie_version)
new_onie_store(manufacturer)
new_onie_store(country_code)
new_onie_store(vendor)
new_onie_store(diag_version)
new_onie_store(service_tag)
new_onie_store(vendor_extension)

static ssize_t onie_store_cache(struct device *dev,
				struct device_attribute *kda,
				const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	if (sz >= onie_max_data)
		return -ERANGE;
	mutex_lock(&priv->cache.mutex);
	memcpy(priv->cache.data, buf, sz);
	mutex_unlock(&priv->cache.mutex);
	return sz;
}

static ssize_t onie_store_nvmem(struct device *dev,
				struct device_attribute *kda,
				const char *buf, size_t sz)
{
	return -ENOSYS;
}

static ssize_t onie_store_mac_base(struct device *dev,
				   struct device_attribute *kda,
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
	err = onie_tlv_set(dev, onie_type_mac_base, onie_sz_mac, v);
	return err ? err : sz;
}

static ssize_t onie_store_num_macs(struct device *dev,
				   struct device_attribute *kda,
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
	err = onie_tlv_set(dev, onie_type_num_macs, sizeof(u16), v);
	return err ? err : sz;
}

static ssize_t onie_store_device_version(struct device *dev,
					 struct device_attribute *kda,
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
	err = onie_tlv_set(dev, onie_type_device_version, sizeof(u8), v);
	return err ? err : sz;
}

static ssize_t onie_store_crc(struct device *dev,
			      struct device_attribute *kda,
			      const char *buf, size_t sz)
{
	int err = onie_cache_flush(dev);
	return (err < 0) ? err : sz;
}

#define new_onie_devattr(NAME)						\
static struct device_attribute onie_devattr_##NAME = {			\
	.attr.name = __stringify(NAME),					\
	.attr.mode = (S_IWUSR|S_IRUGO),					\
	.show = onie_show_##NAME,					\
	.store = onie_store_##NAME,					\
};

new_onie_devattr(cache)
new_onie_devattr(nvmem)
new_onie_devattr(product_name)
new_onie_devattr(part_number)
new_onie_devattr(serial_number)
new_onie_devattr(mac_base)
new_onie_devattr(manufacture_date)
new_onie_devattr(device_version)
new_onie_devattr(label_revision)
new_onie_devattr(platform_name)
new_onie_devattr(onie_version)
new_onie_devattr(num_macs)
new_onie_devattr(manufacturer)
new_onie_devattr(country_code)
new_onie_devattr(vendor)
new_onie_devattr(diag_version)
new_onie_devattr(service_tag)
new_onie_devattr(vendor_extension)
new_onie_devattr(crc)

static struct attribute *onie_attributes[] = {
	&onie_devattr_cache.attr,
	&onie_devattr_nvmem.attr,
	&onie_devattr_product_name.attr,
	&onie_devattr_part_number.attr,
	&onie_devattr_serial_number.attr,
	&onie_devattr_mac_base.attr,
	&onie_devattr_manufacture_date.attr,
	&onie_devattr_device_version.attr,
	&onie_devattr_label_revision.attr,
	&onie_devattr_platform_name.attr,
	&onie_devattr_onie_version.attr,
	&onie_devattr_num_macs.attr,
	&onie_devattr_manufacturer.attr,
	&onie_devattr_country_code.attr,
	&onie_devattr_vendor.attr,
	&onie_devattr_diag_version.attr,
	&onie_devattr_service_tag.attr,
	&onie_devattr_vendor_extension.attr,
	&onie_devattr_crc.attr,
	NULL,
};

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

static void onie_append_crc(u8 *data, size_t sz)
{
	onie_set_u32(onie_crc(data, sz), data + sz);
}

ssize_t onie_tlv_get(enum onie_type t, size_t sz, u8 *v)
{
	struct onie_priv *priv;
	struct onie *o;
	struct onie_tlv *tlv;
	ssize_t n;
	u16 hl;
	u8 *end;

	if (!onie_pdev)
		return -ENODEV;
	priv = dev_get_drvdata(&onie_pdev->dev);
	o = (struct onie *)priv->cache.data;
	mutex_lock(&priv->cache.mutex);
	n = onie_validate(priv->cache.data);
	if (n < 0)
		goto onie_tlv_get_exit;
	n = -ENOMSG;
	hl = onie_u16(o->header.length);
	if (!hl) {
		goto onie_tlv_get_exit;
	}
	end = priv->cache.data + sizeof(o->header) + hl;
	for (tlv = &o->tlv; (u8*)tlv < end; tlv = onie_next(tlv))
		if (tlv->t == t) {
			if (n < 0)
				n = 0;
			if (tlv->l) {
				if (sz < n + tlv->l) {
					n = -EINVAL;
					goto onie_tlv_get_exit;
				}
				memcpy(v, tlv->v, tlv->l);
				n += tlv->l;
				v += tlv->l;
			} else	/* may have 0 length values */
				n = 0;
			if (t != onie_type_vendor_extension)
				goto onie_tlv_get_exit;
		}
onie_tlv_get_exit:
	mutex_unlock(&priv->cache.mutex);
	return n;
}
EXPORT_SYMBOL_GPL(onie_tlv_get);

static int onie_tlv_set(struct device *dev,
			enum onie_type t, size_t l, const u8 *v)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_header *cache_h = (struct onie_header *)priv->cache.data;
	struct onie_header *wb_h = (struct onie_header *)priv->writeback.data;
	struct onie_tlv *cache_tlv =
		(struct onie_tlv *)(priv->cache.data + sizeof(*cache_h));
	struct onie_tlv *wb_tlv =
		(struct onie_tlv *)(priv->writeback.data + sizeof(*wb_h));
	u8 *over = priv->writeback.data + onie_max_data;
	size_t tl, hl = 0;

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
			wb_tlv = onie_tlv_insert(wb_tlv, t, l, v, &hl, over);
			if (!wb_tlv) {
				mutex_unlock(&priv->writeback.mutex);
				mutex_unlock(&priv->cache.mutex);
				return -EINVAL;
			} else
				l = 0;
		}
	if (l) {
		wb_tlv = onie_tlv_insert(wb_tlv, t, l, v, &hl, over);
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

static struct onie_tlv *onie_tlv_insert(struct onie_tlv *dst, enum onie_type t,
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
