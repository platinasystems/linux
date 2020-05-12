/**
 * Platina Systems MK1 top of rack switch.
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/onie.h>

enum {
	xeth_platina_mk1_top_xid = 3999,
	xeth_platina_mk1_n_ports = 32,
	xeth_platina_mk1_n_rxqs = 1,
	xeth_platina_mk1_n_txqs = 1,
};

static int xeth_platina_mk1_provision[xeth_platina_mk1_n_ports];
static int xeth_platina_mk1_n_provision;
module_param_array_named(platina_mk1_provision,
			 xeth_platina_mk1_provision, int,
			 &xeth_platina_mk1_n_provision, 0644);
MODULE_PARM_DESC(platina_mk1_provision,
		 "*1, 2, or 4 subports per port");

static int xeth_platina_mk1_remove(struct platform_device *pdev);
static int xeth_platina_mk1_init(struct platform_device *);
static int xeth_platina_mk1_add_lowers(void);
static int xeth_platina_mk1_make_uppers(void);
static void xeth_platina_mk1_et_port_cb(struct ethtool_link_ksettings *ks);
static void xeth_platina_mk1_et_subport_cb(struct ethtool_link_ksettings *ks);

static const char * const xeth_platina_mk1_ethtool_flag_names[] = {
	"copper",
	"fec75",
	"fec91",
	NULL,
};

int xeth_platina_mk1_probe(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xeth_platina_mk1_provision); i++)
		if (i >= xeth_platina_mk1_n_provision) {
			xeth_platina_mk1_provision[i] = 1;
		} else {
			int n = xeth_platina_mk1_provision[i];
			if (n != 1 && n != 2 && n != 4)
				return -EINVAL;
		}
	xeth_vendor_remove = xeth_platina_mk1_remove;
	xeth_vendor_init = xeth_platina_mk1_init;
	xeth_upper_set_ethtool_flag_names(xeth_platina_mk1_ethtool_flag_names);
	return 0;
}

static int xeth_platina_mk1_remove(struct platform_device *pdev)
{
	int port, subport;

	for (port = 0; port < xeth_platina_mk1_n_ports; port++)
		if (xeth_platina_mk1_provision[port] > 1) {
			for (subport = 0;
			     subport < xeth_platina_mk1_provision[port];
			     subport++) {
				u32 o = xeth_platina_mk1_n_ports * subport;
				u32 xid = xeth_platina_mk1_top_xid - port - o;
				xeth_upper_delete_port(xid);
			}
		} else
			xeth_upper_delete_port(xeth_platina_mk1_top_xid - port);
	return 0;
}

static int xeth_platina_mk1_init(struct platform_device *pdev)
{
	int err = xeth_platina_mk1_add_lowers();
	return err ? err : xeth_platina_mk1_make_uppers();
}

static int xeth_platina_mk1_add_lowers(void)
{
	const char * const eth1_akas[] = {
		"eth1", "enp3s0f0", NULL
	};
	const char * const eth2_akas[] = {
		"eth2", "enp3s0f1", NULL
	};
	const char * const * const akas[] = {
		eth1_akas,
		eth2_akas,
		NULL,
	};
	struct net_device *nds[3] = { NULL, NULL, NULL };
	int ndi, akai, err;

	for (ndi = 0; akas[ndi]; ndi++)
		for (akai = 0; !nds[ndi] && akas[ndi][akai]; akai++)
			nds[ndi] = dev_get_by_name(&init_net, akas[ndi][akai]);

	if (nds[0] && nds[1]) {
		rtnl_lock();
		err = xeth_mux_add_lowers(nds);
		rtnl_unlock();
	} else
		err = -ENODEV;

	for (ndi = 0; akas[ndi]; ndi++)
		if (nds[ndi])
			dev_put(nds[ndi]);
	return err;
}

static int xeth_platina_mk1_make_uppers(void)
{
	u64 ea;
	u8 first_port;
	int port, subport;
	s64 err = 0;
	char name[IFNAMSIZ];

	ea = xeth_onie_mac_base();
	first_port = xeth_onie_device_version() > 0 ? 1 : 0;
	for (port = 0; err >= 0&& port < xeth_platina_mk1_n_ports; port++) {
		if (xeth_platina_mk1_provision[port] > 1) {
			void (*cb)(struct ethtool_link_ksettings *) =
				xeth_platina_mk1_et_subport_cb;
			for (subport = 0;
			     err >= 0 &&
				     subport < xeth_platina_mk1_provision[port];
			     subport++) {
				u32 o = xeth_platina_mk1_n_ports * subport;
				u32 xid = xeth_platina_mk1_top_xid - port - o;
				u64 pea = ea ? ea + port + o : 0;
				scnprintf(name, IFNAMSIZ, "xeth%d-%d",
					  port + first_port,
					  subport + first_port);
				err = xeth_upper_make(name, xid, pea, cb);
			}
		} else {
			u32 xid = xeth_platina_mk1_top_xid - port;
			u64 pea = ea ? ea + port : 0;
			void (*cb)(struct ethtool_link_ksettings *) =
				xeth_platina_mk1_et_port_cb;
			scnprintf(name, IFNAMSIZ, "xeth%d", port + first_port);
			err = xeth_upper_make(name, xid, pea, cb);
		}
	}
	return (err < 0) ? (int)err : 0;
}

static void xeth_platina_mk1_et_port_cb(struct ethtool_link_ksettings *ks)
{
	ks->base.speed = 0;
	ks->base.duplex = DUPLEX_FULL;
	ks->base.autoneg = AUTONEG_ENABLE;
	ks->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported,
					     100000baseLR4_ER4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, TP);
	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
	/* disable FEC_NONE so that the vner-platina-mk1 interprets
	 * FEC_RS|FEC_BASER as FEC_AUTO */
	ethtool_link_ksettings_del_link_mode(ks, advertising, FEC_NONE);
}

static void xeth_platina_mk1_et_subport_cb(struct ethtool_link_ksettings *ks)
{
	ks->base.speed = 0;
	ks->base.duplex = DUPLEX_FULL;
	ks->base.autoneg = AUTONEG_ENABLE;
	ks->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, TP);
	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
}
