/**
 * XETH driver, see Documentation/networking/xeth.txt
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

#include <linux/module.h>

MODULE_DESCRIPTION("mux proxy netdevs with a remote switch;\n"
		   "\t\teach non-empty or non-zero \"onie_*\" parameter takes\n"
		   "\t\tprecedence to the respective TLV of the probed NVMEM");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(xeth_version);
MODULE_SOFTDEP("pre: nvmem-onie");

int xeth_encap = XETH_ENCAP_VLAN;
int xeth_base_xid = 3000;

static int xeth_main_stat_index;

static int xeth_main_probe(struct pci_dev *, const struct pci_device_id *);
static void xeth_main_remove(struct pci_dev *);
static int xeth_main_deinit(int err);

static ssize_t stat_index_show(struct device_driver *, char *);
static ssize_t stat_name_show(struct device_driver *, char *);

static ssize_t stat_index_store(struct device_driver *, const char *buf,
				size_t sz);
static ssize_t stat_name_store(struct device_driver *, const char *buf,
			       size_t sz);

static DRIVER_ATTR_RW(stat_index);
static DRIVER_ATTR_RW(stat_name);

static struct attribute *stat_attrs[] = {
	&driver_attr_stat_index.attr,
	&driver_attr_stat_name.attr,
	NULL,
};

ATTRIBUTE_GROUPS(stat);

static const struct pci_device_id const xeth_main_pci_ids[] = {
	{ PCI_VDEVICE(BROADCOM, 0xb960) },
	{ PCI_VDEVICE(BROADCOM, 0xb961) },
	{ PCI_VDEVICE(BROADCOM, 0xb962) },
	{ PCI_VDEVICE(BROADCOM, 0xb963) },
	{ PCI_VDEVICE(BROADCOM, 0xb965) },
	{ PCI_VDEVICE(BROADCOM, 0xb966) },
	{ PCI_VDEVICE(BROADCOM, 0xb967) },
	{ PCI_VDEVICE(BROADCOM, 0xb968) },
	{ PCI_VDEVICE(BROADCOM, 0xb969) },
	{ },
};
MODULE_DEVICE_TABLE(pci, xeth_main_pci_ids);

static struct pci_driver xeth_main_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = xeth_main_pci_ids,
	.probe = xeth_main_probe,
	.remove = xeth_main_remove,
	.groups = stat_groups,
};

static int __init xeth_main_init(void)
{
	int err;

	xeth_upper_ethtool_flag_names = kzalloc(xeth_ethtool_flag_names_sz,
						GFP_KERNEL);
	if (!xeth_upper_ethtool_flag_names)
		return xeth_main_deinit(-ENOMEM);

	xeth_upper_ethtool_stat_names = kzalloc(xeth_ethtool_stat_names_sz,
						GFP_KERNEL);
	if (!xeth_upper_ethtool_stat_names)
		return xeth_main_deinit(-ENOMEM);

	err = xeth_debug_err(xeth_mux_init());
	if (err)
		return xeth_main_deinit(err);
	err = xeth_debug_err(xeth_upper_init());
	if (err)
		return xeth_main_deinit(err);
	err = xeth_debug_err(pci_register_driver(&xeth_main_pci_driver));
	if (err)
		return xeth_main_deinit(err);
	xeth_debug("ready");
	return 0;
}
module_init(xeth_main_init)

static void __exit xeth_main_exit(void)
{
	xeth_main_deinit(0);
	xeth_debug("done");
}
module_exit(xeth_main_exit);

static int xeth_main_probe(struct pci_dev *pci_dev,
			   const struct pci_device_id *id)
{
	xeth_debug("vendor 0x%x, device 0x%x", id->vendor, id->device);
	return xeth_vendor_probe(pci_dev, id);
}

static void xeth_main_remove(struct pci_dev *pci_dev)
{
	xeth_debug("vendor 0x%x, device 0x%x",
		   pci_dev->vendor, pci_dev->device);
	xeth_vendor_remove(pci_dev);
}

static int xeth_main_deinit(int err)
{
#define xeth_main_sub_deinit(deinit, err)				\
({									\
	int __err = (err);						\
	int ___err = (deinit)(__err);					\
	(__err) ?  (__err) : (___err);					\
})

	xeth_vendor_remove(NULL);
	pci_unregister_driver(&xeth_main_pci_driver);
	if (xeth_mux_is_registered()) {
		err = xeth_main_sub_deinit(xeth_upper_deinit, err);
		err = xeth_main_sub_deinit(xeth_mux_deinit, err);
	}
	if (xeth_upper_ethtool_flag_names)
		kfree(xeth_upper_ethtool_flag_names);
	if (xeth_upper_ethtool_stat_names)
		kfree(xeth_upper_ethtool_stat_names);
	return err;
}

static ssize_t stat_index_show(struct device_driver *drv, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u", xeth_main_stat_index);
}

static ssize_t stat_index_store(struct device_driver *drv,
					  const char *buf,
					  size_t sz)
{
	int err = kstrtouint(buf, 10, &xeth_main_stat_index);
	if (xeth_main_stat_index > xeth_n_ethtool_stats)
		err = -ERANGE;
	if (err)
		xeth_main_stat_index = 0;
	return sz;
}

static ssize_t stat_name_show(struct device_driver *drv, char *buf)
{
	return strlcpy(buf, xeth_upper_ethtool_stat_names +
		       (xeth_main_stat_index * ETH_GSTRING_LEN),
		       ETH_GSTRING_LEN);
}

static ssize_t stat_name_store(struct device_driver *drv, const char *buf,
			       size_t sz)
{
	return strlcpy(xeth_upper_ethtool_stat_names +
		       (xeth_main_stat_index * ETH_GSTRING_LEN),
		       buf, ETH_GSTRING_LEN);
}
