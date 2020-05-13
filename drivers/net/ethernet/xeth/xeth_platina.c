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
	const char *pn;
	int (*probe)(struct pci_dev *, const struct pci_device_id *);
} const xeth_platina_pns[] = {
	{ "BT77O759.00", xeth_platina_mk1_probe },
	{ /* END */ },
};

int xeth_platina_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	int i;
	char *pn;

	pn = xeth_onie_part_number();
	if (IS_ERR(pn))
		return PTR_ERR(pn);
	for (i = 0; xeth_platina_pns[i].pn; i++)
		if (!strcasecmp(pn, xeth_platina_pns[i].pn))
			return xeth_platina_pns[i].probe(pci_dev, id);
	pr_err("no matching part number");
	return -ENOENT;
}
