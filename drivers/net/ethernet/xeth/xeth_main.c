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
static struct pci_driver xeth_main_pci_driver;
module_pci_driver(xeth_main_pci_driver);

int xeth_encap = XETH_ENCAP_VLAN;
int xeth_base_xid = 3000;

/* a NULL terminated list */
struct net_device **xeth_main_lowers;

void (*xeth_main_remove)(struct pci_dev *);
int (*xeth_main_make_uppers)(void);

static int xeth_main_stat_index;

static int xeth_main_probe(struct pci_dev *, const struct pci_device_id *);
static void _xeth_main_remove(struct pci_dev *);
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
	.remove = _xeth_main_remove,
	.groups = stat_groups,
};

static int xeth_main_probe(struct pci_dev *pci_dev,
			   const struct pci_device_id *id)
{
	int i, err;

	if (xeth_mux_is_registered())
		return -EBUSY;

	err = xeth_vendor_probe(pci_dev, id);
	if (err)
		return err;

	if (!xeth_main_lowers || !xeth_main_lowers[0] || !xeth_main_make_uppers)
		return -EINVAL;

	xeth_upper_et_stat_names =
		devm_kzalloc(&pci_dev->dev, xeth_et_stat_names_sz, GFP_KERNEL);
	if (!xeth_upper_et_stat_names)
		return -ENOMEM;

	err = xeth_debug_err(xeth_mux_init());
	if (err)
		return err;
	err = xeth_debug_err(xeth_upper_init());
	if (err)
		return xeth_mux_deinit(err);

	rtnl_lock();
	err = xeth_mux_add_lowers(xeth_main_lowers);
	rtnl_unlock();
	for (i = 0; xeth_main_lowers[i]; i++)
		dev_put(xeth_main_lowers[i]);
	if (err)
		return xeth_upper_deinit(xeth_mux_deinit(err));

	err = xeth_main_make_uppers();
	if (err < 0 && err != -EBUSY)
		return xeth_mux_deinit(xeth_upper_deinit(err));

	return err;
}

static void _xeth_main_remove(struct pci_dev *pci_dev)
{
	if (xeth_main_remove)
		xeth_main_remove(pci_dev);
	if (xeth_mux_is_registered())
		xeth_mux_deinit(xeth_upper_deinit(0));
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
	if (xeth_main_stat_index > xeth_n_et_stats)
		err = -ERANGE;
	if (err)
		xeth_main_stat_index = 0;
	return sz;
}

static ssize_t stat_name_show(struct device_driver *drv, char *buf)
{
	if (!xeth_upper_et_stat_names)
		return -ENOMEM;
	return strlcpy(buf, xeth_upper_et_stat_names +
		       (xeth_main_stat_index * ETH_GSTRING_LEN),
		       ETH_GSTRING_LEN);
}

static ssize_t stat_name_store(struct device_driver *drv, const char *buf,
			       size_t sz)
{
	if (sz > ETH_GSTRING_LEN)
		sz = ETH_GSTRING_LEN;
	if (!xeth_upper_et_stat_names)
		return -ENOMEM;
	return strlcpy(xeth_upper_et_stat_names +
		       (xeth_main_stat_index * ETH_GSTRING_LEN),
		       buf, sz);
}
