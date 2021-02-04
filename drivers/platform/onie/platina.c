/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 *
 * Platina Systems ONIE Platforms.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/etherdevice.h>
#include <linux/i2c.h>
#include <linux/onie.h>
#include <linux/slab.h>
#include <linux/xeth.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("an ONIE vendor platform driver");

static const struct platform_device_id platina_device_ids[] = {
	{ .name = "platina" },
	{},
};

MODULE_DEVICE_TABLE(platform, platina_device_ids);

static int platina_probe(struct platform_device *);
static int platina_remove(struct platform_device *);

static struct platform_driver platina_platform_driver = {
	.driver   = { .name = KBUILD_MODNAME },
	.probe    = platina_probe,
	.remove   = platina_remove,
	.id_table = platina_device_ids,
};

module_platform_driver(platina_platform_driver);

static int platina_provision_param[512], platina_provisioned;
module_param_array_named(provision, platina_provision_param, int,
			 &platina_provisioned, 0644);
MODULE_PARM_DESC(provision, " 1 (default), 2, or 4 subports per port");

enum platina_base {
	platina_base0,
	platina_base1,
};

struct platina_priv {
	const char *mux_ifname;
	enum platina_base base;
	u64 mac_base;
	u16 n_macs;
	struct xeth_vendor vendor;
};

static struct platina_priv *platina_priv(const struct xeth_vendor *vendor)
{
	return container_of(vendor, struct platina_priv, vendor);
}

static void platina_ifname(const struct xeth_vendor *vendor,
			   char *ifname, int port, int subport)
{
	struct platina_priv *priv = platina_priv(vendor);
	if (port < 0)
		strcpy(ifname, priv->mux_ifname);
	else if (subport < 0)
		scnprintf(ifname, IFNAMSIZ, "xeth%d", priv->base + port);
	else
		scnprintf(ifname, IFNAMSIZ, "xeth%d-%d", priv->base + port,
			  priv->base + subport);
}

/**
 * The first 4 platina-mk1 mac addresses are,
 *
 * 	BMC,
 * 	management port (aka. eth0)
 * 	1st mux link (aka. eth1) - stolen by mux
 * 	2nd mux link (aka. eth2)
 */
static void platina_mk1_hw_addr(const struct xeth_vendor *vendor,
				struct net_device *nd, int port, int subport)
{
	struct platina_priv *priv = platina_priv(vendor);
	u64 mac = priv->mac_base;
	nd->addr_assign_type = NET_ADDR_PERM;
	if (port < 0) {
		mac += 2;
		nd->addr_assign_type = NET_ADDR_STOLEN;
	} else if (subport < 0)
		mac += 4 + port;
	else
		mac += 4 + port + (subport * 4);
	u64_to_ether_addr(mac, nd->dev_addr);
}

static inline u32 platina_mk1_xid(const struct xeth_vendor *vendor,
				  int port, int subport)
{
	if (port < 0)
		return 3000;
	else if (subport < 0)
		return 3999 - port;
	else
		return 3999 - port - (vendor->n_ports * subport);
}

static void platina_mk1_port_ksettings(struct ethtool_link_ksettings *ks)
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

static void platina_mk1_subport_ksettings(struct ethtool_link_ksettings *ks)
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

static int platina_mk1_probe(struct platform_device *platina)
{
	static const int n_ports = 32;
	static const char * const port_et_flag_names[] = {
		"copper",
		"fec74",
		"fec91",
		NULL,
	};
	static const char * const eth1_akas[] = {
		"eth1", "enp3s0f0", NULL
	};
	static const char * const eth2_akas[] = {
		"eth2", "enp3s0f1", NULL
	};
	static const char * const *links[] = {
		eth1_akas,
		eth2_akas,
		NULL,
	};
	static const int const port_qsfp_nrs[] = {
		 3,  2,  5,  4,  7,  6,  9,  8,
		12, 11, 14, 13, 16, 15, 18, 17,
		21, 20, 23, 22, 25, 24, 27, 26,
		30, 29, 32, 31, 34, 33, 36, 35,
		-1,
	};
	static const unsigned short const port_qsfp_addrs[] =
		I2C_ADDRS(0x50, 0x51);
	struct device *dev = &platina->dev;
	const struct onie *o = onie(dev);
	struct platina_priv *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(platina, &priv->vendor);

	priv->mux_ifname = "platina-mk1";
	priv->base = o->device_version(o) > 0 ? platina_base1 : platina_base0;
	priv->mac_base = o->mac_base(o);
	priv->n_macs = o->num_macs(o);

	priv->vendor.id = XETH_VENDOR_ID;
	priv->vendor.encap = XETH_ENCAP_VLAN;
	priv->vendor.n_ports = n_ports;
	priv->vendor.n_rxqs = 1;
	priv->vendor.n_txqs = 1;
	priv->vendor.links = links;
	priv->vendor.port.provision.subports = platina_provision_param;
	priv->vendor.port.qsfp.nrs = port_qsfp_nrs;
	priv->vendor.port.qsfp.addrs = port_qsfp_addrs;

	err = xeth_create_port_provision(dev,
					 &priv->vendor.port.provision.attr,
					 "port_provision");
	if (err < 0)
		return err;

	err = xeth_create_port_et_stat_name(dev,
					    &priv->vendor.port.ethtool.stat.attr,
					    "stat_name");
	if (err < 0)
		return err;

	priv->vendor.ifname = platina_ifname;
	priv->vendor.hw_addr = platina_mk1_hw_addr;
	priv->vendor.port.ethtool.flag_names = port_et_flag_names;
	priv->vendor.port_ksettings = platina_mk1_port_ksettings;
	priv->vendor.subport_ksettings = platina_mk1_subport_ksettings;

	priv->vendor.xeth.info.parent = dev;
	priv->vendor.xeth.info.name = "xeth";
	priv->vendor.xeth.info.id = -1;
	priv->vendor.xeth.info.res = NULL;
	priv->vendor.xeth.info.num_res = 0;

	priv->vendor.xeth.dev =
		platform_device_register_full(&priv->vendor.xeth.info);
	if (IS_ERR(priv->vendor.xeth.dev)) {
		err = PTR_ERR(priv->vendor.xeth.dev);
		priv->vendor.xeth.dev = NULL;
		pr_err("platina-mk1 register %s: %d\n",
		       priv->vendor.xeth.info.name, err);
	}
	return err < 0 ? err : 0;
}

static int platina_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	static const struct {
		const char *part_number;
		int (*probe)(struct platform_device *);
	} const probes[] = {
		{ "BT77O759.00", platina_mk1_probe},
		{ "PS-3001-32C", platina_mk1_probe},
		{ "PSW-3001-32C", platina_mk1_probe},
	};

	char part_number[onie_max_tlv];
	ssize_t n = onie_get_tlv(dev, onie_type_part_number,
				 onie_max_tlv, part_number);
	int i;

	if (n < 0)
		return n;
	else if (n == onie_max_tlv)
		n = onie_max_tlv-1;
	part_number[n] = '\0';

	for (i = 0; i < ARRAY_SIZE(probes); i++)
		if (!strcmp(part_number, probes[i].part_number))
			return probes[i].probe(pdev);

	return -ENXIO;
}

static int platina_remove(struct platform_device *pdev)
{
	struct xeth_vendor *vendor = dev_get_drvdata(&pdev->dev);
	if (vendor && vendor->xeth.dev)
		platform_device_unregister(vendor->xeth.dev);
	return 0;
}
