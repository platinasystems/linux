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

static int onie_init(void);
static void onie_exit(void);

module_init(onie_init);
module_exit(onie_exit);

enum onie_param_ {
	onie_nvmem_name_sz = 64,
};

static char onie_param_nvmem[onie_nvmem_name_sz];
module_param_string(nvmem, onie_param_nvmem, onie_nvmem_name_sz, 0);
MODULE_PARM_DESC(nvmem, " use named device or empty data if \".\"; "
		 "otherwise, use \"onie-data\" cell");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("a /sys interface for ONIE format NVMEM");

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
	struct	platform_device *pdev;
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

struct onie_devattr {
	struct device_attribute devattr;
	u8 t;
};

#define to_onie_devattr(a) container_of((a), struct onie_devattr, devattr)

static struct attribute *onie_attributes[];

static const struct attribute_group onie_attribute_group = {
	.name = "data",
	.attrs = onie_attributes,
};

static const struct of_device_id onie_of_match[] = {
	{ .compatible = "linux,onie", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(of, onie_of_match);

static const struct acpi_device_id onie_acpi_ids[] = {
	{ .id = "ONIE", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(acpi, onie_acpi_ids);

static const struct platform_device_id onie_platform_ids[] = {
	{ .name = "onie", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(platform, onie_platform_ids);

static int onie_probe(struct platform_device *pdev);
static int onie_remove(struct platform_device *pdev);

static struct platform_driver onie_platform_driver = {
	.driver = {
		.name = "onie",
		.of_match_table = of_match_ptr(onie_of_match),
		.acpi_match_table = ACPI_PTR(onie_acpi_ids),
	},
	.id_table = onie_platform_ids,
	.probe = onie_probe,
	.remove = onie_remove,
};

static void onie_platform_device_release(struct device *dev);

static struct platform_device onie_platform_device = {
	.name = "onie",
	.id = PLATFORM_DEVID_NONE,
	.dev = {
		.release = onie_platform_device_release,
	},
};

static int __init onie_init(void)
{
	int err = platform_driver_register(&onie_platform_driver);
	if (!err && onie_param_nvmem[0]) {
		err = platform_device_register(&onie_platform_device);
		if (err)
			platform_driver_unregister(&onie_platform_driver);
		pr_debug("ok");
	}
	return err;
}

static void __exit onie_exit(void)
{
	if (onie_platform_device.dev.kobj.parent)
		platform_device_unregister(&onie_platform_device);
	platform_driver_unregister(&onie_platform_driver);
}

static int onie_cache_fill(struct onie_priv *priv);

static ssize_t onie_tlv_get(struct onie_priv *, enum onie_type, size_t, u8 *);
static int onie_tlv_set(struct onie_priv *, enum onie_type, size_t, const u8 *);
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
	struct onie_priv *priv;
	int err;

	pr_debug("pdev %s\n", pdev->name);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	dev_set_drvdata(&pdev->dev, priv);

	if (onie_param_nvmem[0])
		strncpy(priv->nvmem_name, onie_param_nvmem,
			onie_nvmem_name_sz);

	err = sysfs_create_group(&pdev->dev.kobj, &onie_attribute_group);
	if (err)
		pr_debug("create attribute group err: %d", err);

	return onie_cache_fill(priv);
}

static int onie_remove(struct platform_device *pdev)
{
	struct onie_priv *priv = dev_get_drvdata(&pdev->dev);

	pr_debug("pdev %s\n", pdev->name);

	if (priv) {
		dev_set_drvdata(&pdev->dev, NULL);
		kfree(priv);
	}
	return 0;
}

static void onie_platform_device_release(struct device *dev)
{
	do {} while(0);
}

static int onie_cache_check_header_id(struct onie_priv *priv)
{
	return strcmp(onie_header_id, priv->cache.data) ? -EIDRM : 0;
}

static ssize_t onie_cache_validate(struct onie_priv *priv, size_t sz);

static int onie_cache_flush(struct onie_priv *priv)
{
	ssize_t n = onie_cache_validate(priv, 0);
	if (n <= 0)
		return n;
	if (priv->nvmem_cell)
		n = nvmem_cell_write(priv->nvmem_cell, priv->cache.data, n);
	else if (priv->nvmem_dev)
		n = nvmem_device_write(priv->nvmem_dev, 0, n, priv->cache.data);
	else
		n = -ENODEV;
	return n;
}

static int onie_cache_fill(struct onie_priv *priv)
{
	int err = 0;

	if (!strcmp(priv->nvmem_name, "."))
		return 0;
	if (priv->nvmem_name[0]) {
		priv->nvmem_dev = nvmem_device_get(&priv->pdev->dev,
						   priv->nvmem_name);
		if (IS_ERR(priv->nvmem_dev)) {
			priv->nvmem_dev = NULL;
			err = PTR_ERR(priv->nvmem_dev);
			pr_debug("%s get err: %d\n", priv->nvmem_name, err);
		} else if (!priv->nvmem_dev) {
			err = -ENODEV;
			pr_debug("%s is null\n", priv->nvmem_name);
		} else {
			err = nvmem_device_read(priv->nvmem_dev, 0,
						onie_max_data,
						priv->cache.data);
			if (err < 0) {
				nvmem_device_put(priv->nvmem_dev);
				priv->nvmem_dev = NULL;
				pr_debug("%s read err: %d\n",
					      priv->nvmem_name, err);
			} else
				err = 0;
		}
		return err;
	}
	priv->nvmem_cell = nvmem_cell_get(&priv->pdev->dev, ONIE_NVMEM_CELL);
	if (IS_ERR(priv->nvmem_cell)) {
		err = PTR_ERR(priv->nvmem_cell);
		priv->nvmem_cell = NULL;
		pr_debug("%s get err: %d\n", ONIE_NVMEM_CELL, err);
	} else if (!priv->nvmem_cell) {
		err = -ENODEV;
		pr_debug("%s is null\n", ONIE_NVMEM_CELL);
	} else {
		size_t n;
		void *data = nvmem_cell_read(priv->nvmem_cell, &n);
		if (IS_ERR(data)) {
			nvmem_cell_put(priv->nvmem_cell);
			err = PTR_ERR(data);
			pr_debug("%s read err: %d\n",
				      ONIE_NVMEM_CELL, err);
		} else if (!data) {
			err = -EINVAL;
			pr_debug("%s read nil\n", ONIE_NVMEM_CELL);
		} else if (n > 0) {
			memcpy(priv->cache.data, data, n);
			kfree(data);
			strncpy(priv->nvmem_name,
				"cell:" ONIE_NVMEM_CELL,
				onie_nvmem_name_sz);
			err = 0;
		}
	}
	return err;
}

/**
 * onie_cache_validate() - verify ONIE ID, Version, and CRC.
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
 * If @sz is !0, onie_cache_validate() checks the header ID and Version before
 * returning the length of the remaining data. If @sz is 0, this also verifies
 * the trailing CRC before returning the total ONIE data length.
 */
static ssize_t onie_cache_validate(struct onie_priv *priv, size_t sz)
{
	void *data = priv->cache.data;
	struct onie_header *h = data;
	size_t tlvsz, fullsz, crcsz;
	u32 crc_read, crc_calc;
	int err;

	if (sz && sz < onie_min_data)
		return -EBADR;
	err = onie_cache_check_header_id(priv);
	if (err)
		return err;
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
	pr_debug("crc: 0x%08x vs. 0x%08x\n", crc_read, crc_calc);
	pr_debug("crc32_le:0: 0x%08x\n", crc32_le(0, data, sz));
	pr_debug("crc32_be:0: 0x%08x\n", crc32_be(0, data, sz));
	pr_debug("crc32_le:~0: 0x%08x\n", crc32_le(~0, data, sz));
	pr_debug("crc32_be:~0: 0x%08x\n", crc32_be(~0, data, sz));
	pr_debug("crc32_le:0:^~0: 0x%08x\n", crc32_le(0, data, sz)^~0);
	pr_debug("crc32_be:0:^~0: 0x%08x\n", crc32_be(0, data, sz)^~0);
	pr_debug("crc32_le:~0:^~0: 0x%08x\n", crc32_le(~0, data, sz)^~0);
	pr_debug("crc32_be:~0:^~0: 0x%08x\n", crc32_be(~0, data, sz)^~0);
	return -EBADF;
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

static ssize_t onie_show_nvmem_name(struct device *dev,
				    struct device_attribute *kda,
				    char *buf)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s", priv->nvmem_name);
}

static ssize_t onie_show_nvmem_cache(struct device *dev,
				     struct device_attribute *kda,
				     char *buf)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	memcpy(buf, priv->cache.data, onie_max_data);
	return onie_max_data;
}

static ssize_t onie_show_mac_base(struct device *dev,
				  struct device_attribute *kda,
				  char *buf)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	u8 v[onie_sz_mac];
	ssize_t n = onie_tlv_get(priv, oda->t, onie_sz_mac, v);
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
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	ssize_t n = onie_tlv_get(priv, oda->t, sizeof(u8), buf);
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
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	ssize_t n = onie_tlv_get(priv, oda->t, sizeof(u16), buf);
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
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	ssize_t n = onie_tlv_get(priv, oda->t, onie_sz_crc, buf);
	if (n == onie_sz_crc)
		n = scnprintf(buf, PAGE_SIZE, "0x%08x",
			      be32_to_cpu(*(u32*)buf));
	else if (n > 0)
		n = -EFBIG;
	return n;
}

static ssize_t onie_show_default(struct device *dev,
				 struct device_attribute *kda,
				 char *buf)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	ssize_t n = onie_tlv_get(priv, oda->t, PAGE_SIZE, buf);
	if (n == 1 && buf[0] == '\0')
		n = 0;
	return n;
}

#define onie_show_product_name onie_show_default
#define onie_show_part_number onie_show_default
#define onie_show_serial_number onie_show_default
#define onie_show_manufacture_date onie_show_default
#define onie_show_label_revision onie_show_default
#define onie_show_platform_name onie_show_default
#define onie_show_onie_version onie_show_default
#define onie_show_manufacturer onie_show_default
#define onie_show_country_code onie_show_default
#define onie_show_vendor onie_show_default
#define onie_show_diag_version onie_show_default
#define onie_show_service_tag onie_show_default
#define onie_show_vendor_extension onie_show_default

static ssize_t onie_store_nvmem_name(struct device *dev,
				     struct device_attribute *kda,
				     const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);

	if (sz >= onie_nvmem_name_sz)
		return -EINVAL;
	if (priv->nvmem_cell) {
		nvmem_cell_put(priv->nvmem_cell);
		priv->nvmem_cell = NULL;
	} else if (priv->nvmem_cell) {
		nvmem_device_put(priv->nvmem_dev);
		priv->nvmem_dev = NULL;
	}
	strncpy(priv->nvmem_name, buf, sz);
	onie_cache_fill(priv);
	return sz;
}

static ssize_t onie_store_nvmem_cache(struct device *dev,
				      struct device_attribute *kda,
				      const char *buf, size_t sz)
{
	return -ENOSYS;
}

static ssize_t onie_store_mac_base(struct device *dev,
				   struct device_attribute *kda,
				   const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
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
	err = onie_tlv_set(priv, oda->t, onie_sz_mac, v);
	return err ? err : sz;
}

static ssize_t onie_store_num_macs(struct device *dev,
				   struct device_attribute *kda,
				   const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	unsigned int uv;
	u8 v[sizeof(u16)];
	int err;

	err = kstrtouint(buf, 0, &uv);
	if (err < 0)
		return err;
	if (uv > U16_MAX)
		return -ERANGE;
	*(u16*)v = cpu_to_be16((u16)uv);
	err = onie_tlv_set(priv, oda->t, sizeof(u16), v);
	return err ? err : sz;
}

static ssize_t onie_store_device_version(struct device *dev,
					 struct device_attribute *kda,
					 const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	unsigned int uv;
	u8 v[sizeof(u8)];
	int err;

	err = kstrtouint(buf, 0, &uv);
	if (err < 0)
		return err;
	if (uv > U8_MAX)
		return -ERANGE;
	v[0] = uv & U8_MAX;
	err = onie_tlv_set(priv, oda->t, sizeof(u8), v);
	return err ? err : sz;
}

static ssize_t onie_store_crc(struct device *dev,
			      struct device_attribute *kda,
			      const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	int err = onie_cache_flush(priv);
	return err ? err : sz;
}

static ssize_t onie_store_default(struct device *dev,
			       struct device_attribute *kda,
			       const char *buf, size_t sz)
{
	struct onie_priv *priv = dev_get_drvdata(dev);
	struct onie_devattr *oda = to_onie_devattr(kda);
	int err = onie_tlv_set(priv, oda->t,
			       (sz > 0 && buf[sz-1] == '\n') ? sz-1 : sz,
			       buf);
	return err ? err : sz;
}

#define onie_store_product_name onie_store_default
#define onie_store_part_number onie_store_default
#define onie_store_serial_number onie_store_default
#define onie_store_manufacture_date onie_store_default
#define onie_store_label_revision onie_store_default
#define onie_store_platform_name onie_store_default
#define onie_store_onie_version onie_store_default
#define onie_store_manufacturer onie_store_default
#define onie_store_country_code onie_store_default
#define onie_store_vendor onie_store_default
#define onie_store_diag_version onie_store_default
#define onie_store_service_tag onie_store_default
#define onie_store_vendor_extension onie_store_default

#define onie_new_devattr(NAME)						\
static struct onie_devattr onie_devattr_##NAME = {			\
	.devattr.attr.name = __stringify(NAME),				\
	.devattr.attr.mode = (S_IWUSR|S_IRUGO),				\
	.devattr.show = onie_show_##NAME,				\
	.devattr.store = onie_store_##NAME,				\
	.t = onie_type_##NAME,						\
}

onie_new_devattr(nvmem_name);
onie_new_devattr(nvmem_cache);
onie_new_devattr(product_name);
onie_new_devattr(part_number);
onie_new_devattr(serial_number);
onie_new_devattr(mac_base);
onie_new_devattr(manufacture_date);
onie_new_devattr(device_version);
onie_new_devattr(label_revision);
onie_new_devattr(platform_name);
onie_new_devattr(onie_version);
onie_new_devattr(num_macs);
onie_new_devattr(manufacturer);
onie_new_devattr(country_code);
onie_new_devattr(vendor);
onie_new_devattr(diag_version);
onie_new_devattr(service_tag);
onie_new_devattr(vendor_extension);
onie_new_devattr(crc);

static struct attribute *onie_attributes[] = {
	&onie_devattr_nvmem_name.devattr.attr,
	&onie_devattr_nvmem_cache.devattr.attr,
	&onie_devattr_product_name.devattr.attr,
	&onie_devattr_part_number.devattr.attr,
	&onie_devattr_serial_number.devattr.attr,
	&onie_devattr_mac_base.devattr.attr,
	&onie_devattr_manufacture_date.devattr.attr,
	&onie_devattr_device_version.devattr.attr,
	&onie_devattr_label_revision.devattr.attr,
	&onie_devattr_platform_name.devattr.attr,
	&onie_devattr_onie_version.devattr.attr,
	&onie_devattr_num_macs.devattr.attr,
	&onie_devattr_manufacturer.devattr.attr,
	&onie_devattr_country_code.devattr.attr,
	&onie_devattr_vendor.devattr.attr,
	&onie_devattr_diag_version.devattr.attr,
	&onie_devattr_service_tag.devattr.attr,
	&onie_devattr_vendor_extension.devattr.attr,
	&onie_devattr_crc.devattr.attr,
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

/**
 * onie_tlv_get() - get cached ONIE EEPROM value.
 * @t: &enum onie_type
 * @sz: sizeof destination
 * @v: destination buffer
 *
 * This expects these @sz sized destinations per @t type::
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
static ssize_t onie_tlv_get(struct onie_priv *priv, enum onie_type t,
			    size_t sz, u8 *v)
{
	struct onie *o = (struct onie *)priv->cache.data;
	ssize_t n;
	u16 hl;;

	mutex_lock(&priv->cache.mutex);
	n = onie_cache_validate(priv, 0);
	if (n < 0) {
		mutex_unlock(&priv->cache.mutex);
		return n;
	}
	n = -ENOMSG;
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

static int onie_tlv_set(struct onie_priv *priv, enum onie_type t, size_t l,
			const u8 *v)
{
	struct onie_header *cache_h = (struct onie_header *)priv->cache.data;
	struct onie_header *wb_h = (struct onie_header *)priv->writeback.data;
	struct onie_tlv *cache_tlv =
		(struct onie_tlv *)(priv->cache.data + sizeof(*cache_h));
	struct onie_tlv *wb_tlv =
		(struct onie_tlv *)(priv->writeback.data + sizeof(*wb_h));
	u8 *over = priv->writeback.data + onie_max_data;
	size_t tl, hl = 0;
	int err;

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
	return err;
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
