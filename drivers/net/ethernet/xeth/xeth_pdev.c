/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/module.h>
#include <linux/platform_device.h>

static void xeth_pdev_release(struct device *dev)
{
	do {} while(0);
}

static struct platform_device xeth_pdev = {
	.name = "xeth",
	.id = PLATFORM_DEVID_NONE,
	.dev = {
		.release = xeth_pdev_release,
	},
};

static int __init xeth_init(void)
{
	return platform_device_register(&xeth_pdev);
}

static void __exit xeth_exit(void)
{
	platform_device_unregister(&xeth_pdev);
}

module_init(xeth_init);
module_exit(xeth_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("a platform device for XETH driver if DT or ACPI"
		   " aren't configured to provide one");
