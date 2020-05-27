/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

int xeth_platina_mk1_probe(struct pci_dev *, const struct pci_device_id *);

static const struct {
	const char *str;
	int (*probe)(struct pci_dev *, const struct pci_device_id *);
} const xeth_platina_match[] = {
	{ "BT77O759.00", xeth_platina_mk1_probe },
	{ "PS-3001-32C", xeth_platina_mk1_probe },
	{ "PSW-3001-32C", xeth_platina_mk1_probe },
	{ /* END */ },
};

int xeth_platina_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	int i;
	char *pn;

	pn = xeth_onie_part_number();
	if (IS_ERR(pn))
		return PTR_ERR(pn);
	for (i = 0; xeth_platina_match[i].str; i++) {
		size_t len = strlen(xeth_platina_match[i].str);
		if (!memcmp(pn, xeth_platina_match[i].str, len))
			return xeth_platina_match[i].probe(pci_dev, id);
	}
	xeth_debug("unsupported part number: %s", pn);
	return -ENOENT;
}
