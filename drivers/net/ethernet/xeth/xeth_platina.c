/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

int xeth_platina_mk1_probe(struct platform_device *);

static const struct {
	const char *pn;
	int (*probe)(struct platform_device *);
} const xeth_platina_pns[] = {
	{ "BT77O759.00", xeth_platina_mk1_probe },
	{ /* END */ },
};

int xeth_platina_probe(struct platform_device *pdev)
{
	int i;
	char *pn = xeth_onie_part_number();
	for (i = 0; xeth_platina_pns[i].pn; i++)
		if (!strcasecmp(pn, xeth_platina_pns[i].pn))
			return xeth_platina_pns[i].probe(pdev);
	pr_err("no matching part number");
	return -ENOENT;
}
