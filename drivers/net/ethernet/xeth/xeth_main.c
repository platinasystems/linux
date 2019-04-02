/* XETH driver, see include/net/xeth.h
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

MODULE_DESCRIPTION("Network device proxy of switch ports and bridges");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(xeth_version);

static int __init xeth_init(void);
static void __exit xeth_exit(void);
module_init(xeth_init);
module_exit(xeth_exit);

static int xeth_deinit(int err)
{
	xeth_sb_deinit(err);
	xeth_notifier_deinit(err);
	return err;
}

static int __init xeth_init(void)
{
	int (*const inits[])(void) = {
		xeth_sb_init,
		xeth_notifier_init,
		NULL,
	};
	int i;

	if (false)
		xeth_pr_test();
	xeth_init_priv_by_ifindex();
	xeth_init_uppers();
	for (i = 0; inits[i]; i++) {
		int err = inits[i]();
		if (err)
			return xeth_deinit(err);
	}
	return 0;
}

static void __exit xeth_exit(void)
{
	xeth_sb_exit();
	xeth_notifier_exit();
}

static struct kobject *xeth_egress(struct kobject *kobj, int err)
{
	xeth_sb_stop();
	xeth_dev_stop();
	xeth_iflink_stop();
	xeth_sysfs_stop(kobj);
	xeth_config_deinit();
	return err ? ERR_PTR(err) : NULL;
}

struct kobject *xeth_create(struct kobject *parent,
			    const struct xeth_config *config)
{
	int err;
	struct kobject *kobj;

	xeth_config_init(config);
	xeth_reset_counters();

	kobj = xeth_sysfs_start(parent);
	if (IS_ERR_OR_NULL(kobj))
		return kobj;

	err = xeth_iflink_start();
	if (err)
		return xeth_egress(kobj, err);
	err = xeth_dev_start();
	if (err)
		return xeth_egress(kobj, err);
	err = xeth_sb_start();
	if (err)
		return xeth_egress(kobj, err);
	return kobj;
}

EXPORT_SYMBOL(xeth_create);

void xeth_delete(struct kobject *kobj)
{
	xeth_egress(kobj, 0);
}

EXPORT_SYMBOL(xeth_delete);
