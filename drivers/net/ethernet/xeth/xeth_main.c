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

int xeth_encap = XETH_ENCAP_VLAN;
int xeth_base_xid = 3000;

static int xeth_main_stat_index;

static int xeth_main_probe(struct pci_dev *, const struct pci_device_id *);
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
	.groups = stat_groups,
};

static int __init xeth_main_init(void)
{
	return pci_register_driver(&xeth_main_pci_driver);
}
module_init(xeth_main_init);

static void __exit xeth_main_exit(void)
{
	xeth_upper_exit();
	if (xeth_vendor_exit)
		xeth_vendor_exit();
	xeth_mux_exit();
	pci_unregister_driver(&xeth_main_pci_driver);
}
module_exit(xeth_main_exit);

static int xeth_main_probe(struct pci_dev *pci_dev,
			   const struct pci_device_id *id)
{
	int i, err;

	if (xeth_mux_is_registered())
		return -EBUSY;

	err = xeth_vendor_probe(pci_dev, id);
	if (err)
		return err;

	if (!xeth_vendor_lowers || !xeth_vendor_lowers[0] || !xeth_vendor_init)
		return -EINVAL;

	if (!xeth_upper_et_stat_names) {
		/* stat names may be alloc'd and set w/in vendor probe */
		xeth_upper_et_stat_names = devm_kzalloc(&pci_dev->dev,
							xeth_et_stat_names_sz,
							GFP_KERNEL);
		if (!xeth_upper_et_stat_names)
			return -ENOMEM;
	}
	if (!xeth_sbrx_buf) {
		xeth_sbrx_buf = devm_kzalloc(&pci_dev->dev,
					     XETH_SIZEOF_JUMBO_FRAME,
					     GFP_KERNEL);
		if (!xeth_sbrx_buf)
			return -ENOMEM;
	}

	err = xeth_mux_init();
	if (err)
		return err;

	err = xeth_upper_init();
	if (err) {
		xeth_mux_exit();
		return err;
	}

	rtnl_lock();
	err = xeth_mux_add_lowers(xeth_vendor_lowers);
	rtnl_unlock();

	for (i = 0; xeth_vendor_lowers[i]; i++)
		dev_put(xeth_vendor_lowers[i]);

	if (!err)
		err = xeth_vendor_init();

	if (err < 0 && err != -EBUSY) {
		xeth_upper_exit();
		xeth_mux_exit();
	}
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
	if (xeth_main_stat_index > xeth_max_et_stats)
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
	if (xeth_upper_n_et_stat_names <= xeth_main_stat_index)
		xeth_upper_n_et_stat_names = xeth_main_stat_index + 1;
	return strlcpy(xeth_upper_et_stat_names +
		       (xeth_main_stat_index * ETH_GSTRING_LEN),
		       buf, sz);
}
