/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA)
int xeth_platina_probe(struct pci_dev *, const struct pci_device_id *);
#endif

static const struct {
	const char *name;
	int (*probe)(struct pci_dev *, const struct pci_device_id *);
} const xeth_vendors[] = {
#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA)
	{ "Platina", xeth_platina_probe },
#endif
	{ },
};

static void _xeth_vendor_remove(struct pci_dev *pci_dev)
{
	do {} while(0);
}

void (*xeth_vendor_remove)(struct pci_dev *) = _xeth_vendor_remove;

int xeth_vendor_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	int i;
	char *vendor;

	vendor = xeth_onie_vendor();
	if (IS_ERR(vendor)) {
		int err = PTR_ERR(vendor);
		return (err == -ENOMSG) ? -EPROBE_DEFER : err;
	}
	for (i = 0; xeth_vendors[i].name; i++)
		if (!strcasecmp(vendor, xeth_vendors[i].name))
			return xeth_vendors[i].probe(pci_dev, id);
	pr_err("no matching vendor");
	return -ENOENT;
}
