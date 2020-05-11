/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_EXAMPLE)
int xeth_example_probe(struct platform_device *);
#endif

#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA)
int xeth_platina_probe(struct platform_device *);
#endif

static const struct {
	const char *name;
	int (*probe)(struct platform_device *);
} const xeth_vendors[] = {
#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_EXAMPLE)
	{ "example", xeth_example_probe },
#endif
#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA)
	{ "Platina", xeth_platina_probe },
#endif
	{ /* END */ },
};

static int _xeth_vendor_remove(struct platform_device *pdev)
{
	return -ENOSYS;
}

static int _xeth_vendor_init(struct platform_device *pdev) {
	return -ENOSYS;
}

int (*xeth_vendor_remove)(struct platform_device *pdev) = _xeth_vendor_remove;
int (*xeth_vendor_init)(struct platform_device *pdev) = _xeth_vendor_init;

int xeth_vendor_probe(struct platform_device *pdev)
{
	int i;
	char *vendor = xeth_onie_vendor();
	for (i = 0; xeth_vendors[i].name; i++)
		if (!strcasecmp(vendor, xeth_vendors[i].name))
			return xeth_vendors[i].probe(pdev);
	pr_err("no matching vendor");
	return -ENOENT;
}
