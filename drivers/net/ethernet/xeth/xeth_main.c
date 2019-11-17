/**
 * XETH driver, see Documentation/networking/xeth.txt
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

MODULE_DESCRIPTION("mux proxy netdevs with a remote switch");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(xeth_version);

int xeth_encap = XETH_ENCAP_VLAN;
module_param(xeth_encap, int, 0);
MODULE_PARM_DESC(xeth_encap, " 0 - vlan (default and only option)");

int xeth_base_xid = 3000;
module_param(xeth_base_xid, int, 0);
MODULE_PARM_DESC(xeth_base_xid, " of 1st dynamic interface, default 3000");

struct xeth_kstrs xeth_ethtool_flag_names;
struct xeth_kstrs xeth_ethtool_stat_names;

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


static int __init xeth_main_init(void)
{
	int err = 0;

#define xeth_main_sub_init(init, err)					\
({									\
	int __err = (err);						\
	if (!__err) {							\
		__err = xeth_debug_err((init)());			\
	}								\
	(__err);							\
})

	if (false)
		xeth_debug_test();

	err = xeth_main_sub_init(xeth_mux_init, err);
	err = xeth_main_sub_init(xeth_upper_init, err);
	return err ? xeth_main_deinit(err) : 0;
}

module_init(xeth_main_init);

static void __exit xeth_main_exit(void)
{
	xeth_main_deinit(0);
}

module_exit(xeth_main_exit);
