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

int xeth_vendor_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	int i;
	char *vendor;

	vendor = xeth_onie_vendor();
	if (IS_ERR(vendor))
		return PTR_ERR(vendor);
	for (i = 0; xeth_vendors[i].name; i++) {
		size_t len = strlen(xeth_vendors[i].name);
		if (!memcmp(vendor, xeth_vendors[i].name, len))
			return xeth_vendors[i].probe(pci_dev, id);
	}
	xeth_debug("no matching vendor: %s", vendor);
	return -ENOENT;
}
