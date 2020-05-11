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

static struct platform_driver xeth_platform_driver;
module_platform_driver(xeth_platform_driver);
MODULE_DESCRIPTION("mux proxy netdevs with a remote switch;\n"
		   "\t\teach non-empty or non-zero \"onie_*\" parameter takes\n"
		   "\t\tprecedence to the respective TLV of the probed NVMEM");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(xeth_version);
#if 0
MODULE_SOFTDEP("pre: nvmem-onie");
#endif

int xeth_encap = XETH_ENCAP_VLAN;
int xeth_base_xid = 3000;

static int xeth_main_probe(struct platform_device *pdev);
static int xeth_main_remove(struct platform_device *pdev);
static int xeth_main_deinit(int err);
struct xeth_kstrs xeth_ethtool_flag_names;
struct xeth_kstrs xeth_ethtool_stat_names;

static const struct of_device_id xeth_of_match[] = {
	{ .compatible = "linux,xeth", },
	{ /* END */ },
};
MODULE_DEVICE_TABLE(of, xeth_of_match);

static const struct acpi_device_id xeth_acpi_ids[] = {
	{ .id = "xeth", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(acpi, xeth_acpi_ids);

static const struct platform_device_id xeth_platform_ids[] = {
	{ .name = "xeth", },
	{ /* END OF LIST */ },
};
MODULE_DEVICE_TABLE(platform, xeth_platform_ids);

static struct platform_driver xeth_platform_driver = {
	.driver = {
		.name = "onie",
		.of_match_table = of_match_ptr(xeth_of_match),
		.acpi_match_table = ACPI_PTR(xeth_acpi_ids),
#if 0	/* FIXME move flag and stat names to driver attributes */
		.groups = xeth_attribute_groups */
#endif
	},
	.id_table = xeth_platform_ids,
	.probe = xeth_main_probe,
	.remove = xeth_main_remove,
};

static int xeth_main_deinit(int err)
{
#define xeth_main_sub_deinit(deinit, err)				\
({									\
	int __err = (err);						\
	int ___err = (deinit)(__err);					\
	(__err) ?  (__err) : (___err);					\
})

	if (!xeth_mux_is_registered())
		return -ENODEV;

	err = xeth_main_sub_deinit(xeth_upper_deinit, err);
	err = xeth_main_sub_deinit(xeth_mux_deinit, err);

	return err;
}

static int xeth_main_probe(struct platform_device *pdev)
{
	int err = 0;

	err = xeth_vendor_probe(pdev);
	if (err)
		return err;

	if (false)
		xeth_debug_test();

	err = xeth_mux_init(pdev);
	if (!err)
		err = xeth_upper_init(pdev);
	if (!err)
		err = xeth_vendor_init(pdev);
	return err ? xeth_main_deinit(err) : 0;
}

static int xeth_main_remove(struct platform_device *pdev)
{
	return xeth_main_deinit(xeth_vendor_remove(pdev));
}
