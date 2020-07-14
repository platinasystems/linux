/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 *
 * Platina Systems MK1 top of rack switch.
 */

struct xeth_platina_mk1_priv {
	/* @first_port must be 0 or 1 */
	u16 first_port;
};

static const char * const xeth_platina_mk1_et_flag_names[] = {
	"copper",
	"fec74",
	"fec91",
	NULL,
};

static const int const xeth_platina_mk1_qsfp_bus[] = {
	 2,  3,  4,  5,  6,  7,  8,  9,
	11, 12, 13, 14, 15, 16, 16, 18,
	20, 21, 22, 23, 24, 25, 26, 27,
	29, 30, 31, 32, 33, 34, 35, 36,
	-1,
};

static int xeth_platina_mk1_setup(struct xeth_platform_priv *);
static void xeth_platina_mk1_port_label(struct xeth_platform_priv *xpp,
					char *ifname,
					u16 port);
static void xeth_platina_mk1_port_setup(struct ethtool_link_ksettings *ks);
static void xeth_platina_mk1_subport_setup(struct ethtool_link_ksettings *ks);

const struct xeth_config xeth_platina_mk1_config = {
	.driver_name = {
		.main = "platina-mk1",
		.vlan = "platina-mk1-vlan",
		.bridge = "platina-mk1-bridge",
		.lag = "platina-mk1-lag",
		.qsfp = "platina-mk1-qsfp",
	},
	.setup = xeth_platina_mk1_setup,
	.port_label = xeth_platina_mk1_port_label,
	.port_setup = xeth_platina_mk1_port_setup,
	.subport_setup = xeth_platina_mk1_subport_setup,
	.et_flag_names = xeth_platina_mk1_et_flag_names,
	.qsfp_bus = xeth_platina_mk1_qsfp_bus,
	.qsfp_i2c_address_list = I2C_ADDRS(0x50, 0x51),
	.top_xid = 3999,
	.base_xid = 3000,
	.max_et_stats = 512,
	.n_ports = 32,
	.n_mux_bits = 12,
	.n_rxqs = 1,
	.n_txqs = 1,
	.n_et_flags = 3,
	.encap = XETH_ENCAP_VLAN,
};

static struct net_device *xeth_platina_mk1_get_lower(const char * const akas[])
{
	int i;

	for (i = 0; akas[i]; i++) {
		struct net_device *nd = dev_get_by_name(&init_net, akas[i]);
		if (nd)
			return nd;
	}
	return NULL;
}

static int xeth_platina_mk1_setup(struct xeth_platform_priv *xpp)
{
	const char * const eth1_akas[] = {
		"eth1", "enp3s0f0", NULL
	};
	const char * const eth2_akas[] = {
		"eth2", "enp3s0f1", NULL
	};
	struct xeth_platina_mk1_priv *priv;

	priv = devm_kzalloc(&xpp->pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	xpp->priv = priv;

	xpp->mux.lowers = devm_kzalloc(&xpp->pdev->dev,
				       3 * sizeof(struct net_device*),
				       GFP_KERNEL);
	if (!xpp->mux.lowers)
		return -ENOMEM;

	xpp->mux.lowers[0] = xeth_platina_mk1_get_lower(eth1_akas);
	xpp->mux.lowers[1] = xeth_platina_mk1_get_lower(eth2_akas);

	priv->first_port =
		xeth_onie_device_version(&xpp->pdev->dev) > 0 ?  1 : 0;
	xpp->base_mac = xeth_onie_mac_base(&xpp->pdev->dev) + 4;
	xpp->n_macs = xeth_onie_num_macs(&xpp->pdev->dev);

	return 0;
}

static void xeth_platina_mk1_port_label(struct xeth_platform_priv *xpp,
					char *ifname,
					u16 port)
{
	struct xeth_platina_mk1_priv *priv = xpp->priv;
	scnprintf(ifname, IFNAMSIZ, "xeth%d", priv->first_port + port);
}

static void xeth_platina_mk1_port_setup(struct ethtool_link_ksettings *ks)
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

static void xeth_platina_mk1_subport_setup(struct ethtool_link_ksettings *ks)
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
