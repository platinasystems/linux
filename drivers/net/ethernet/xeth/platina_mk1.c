/* Platina Systems XETH driver for the MK1 top of rack ethernet switch
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/if_vlan.h>
#include <linux/module.h>
#include <uapi/linux/xeth.h>

#include "platina_mk1_flags.h"
#include "platina_mk1_stats.h"

static const char *const platina_mk1_flags[] = {
	PLATINA_MK1_FLAGS
};

static const char *const platina_mk1_stats[] = {
	PLATINA_MK1_STATS
};

enum {
	platina_mk1_n_ports = 32,
	platina_mk1_n_subports = 4,
	platina_mk1_n_portdevs = platina_mk1_n_ports * platina_mk1_n_subports,
	platina_mk1_n_iflinks = 2,
	platina_mk1_n_encap = VLAN_HLEN,
	platina_mk1_n_rxqs = 1,
	platina_mk1_n_txqs = 1,
	platina_mk1_n_nds = 4096,
	platina_mk1_n_ids = 4096,
	platina_mk1_n_userids = platina_mk1_n_ids - platina_mk1_n_portdevs,
	platina_mk1_n_flags = (sizeof(platina_mk1_flags) / sizeof(char*)) - 1,
	platina_mk1_n_stats = (sizeof(platina_mk1_stats) / sizeof(char*)) - 1,
};

static bool alpha = false;
static int provision[platina_mk1_n_ports];

static struct net_device *platina_mk1_dev_nds[platina_mk1_n_nds];
static u16 platina_mk1_dev_ndi_by_id[platina_mk1_n_ids];
static const char * const platina_mk1_iflink_names[] = { "eth1", "eth2" };
static struct net_device *platina_mk1_iflink_nds[platina_mk1_n_iflinks];
static u64 platina_mk1_iflink_eas[platina_mk1_n_iflinks];
static bool platina_mk1_iflink_registered[platina_mk1_n_iflinks];
static int platina_mk1_set_lladdr(struct net_device *nd);
static void platina_mk1_init_ethtool_settings(struct xeth_priv *priv);
static int platina_mk1_validate_speed(struct net_device *nd, u32 speed);

struct xeth xeth = {
	.n.ports = platina_mk1_n_ports,
	.n.subports = platina_mk1_n_subports,
	.n.userids = platina_mk1_n_userids,
	.n.iflinks = platina_mk1_n_iflinks,
	.n.nds = platina_mk1_n_nds,
	.n.ids = platina_mk1_n_ids,
	.n.encap = platina_mk1_n_encap,
	.n.rxqs = platina_mk1_n_rxqs,
	.n.txqs = platina_mk1_n_txqs,
	.n.ethtool.flags = platina_mk1_n_flags,
	.n.ethtool.stats = platina_mk1_n_stats,
	.n.priv_size = sizeof(struct xeth_priv) +
		(platina_mk1_n_stats * sizeof(u64)),
	.dev.nd = platina_mk1_dev_nds,
	.dev.ndi_by_id = platina_mk1_dev_ndi_by_id,
	.dev.provision = provision,
	.iflink.name = platina_mk1_iflink_names,
	.iflink.nd = platina_mk1_iflink_nds,
	.iflink.ea = platina_mk1_iflink_eas,
	.iflink.registered = platina_mk1_iflink_registered,
	.ethtool.flags = platina_mk1_flags,
	.ethtool.stats = platina_mk1_stats,
	.ops.dev.set_lladdr = platina_mk1_set_lladdr,
	.ops.dev.init_ethtool_settings = platina_mk1_init_ethtool_settings,
	.ops.dev.validate_speed = platina_mk1_validate_speed,
	.ops.encap.init = xeth_vlan_init,
	.ops.encap.exit = xeth_vlan_exit,
};

static int __init platina_mk1_init(void)
{
	xeth.n.base = alpha ? 0 : 1;
	return xeth_init();
}

static void __exit platina_mk1_exit(void)
{
	xeth_exit();
}

module_init(platina_mk1_init);
module_exit(platina_mk1_exit);
module_param(alpha, bool, false);
module_param_array(provision, int, NULL, 0644);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("XETH for Platina Systems MK1 TOR Ethernet Switch");
MODULE_PARM_DESC(alpha, "zero based ports and subports");
MODULE_PARM_DESC(provision, "1, 2, or 4 subports per port, default 1");

static int platina_mk1_set_lladdr(struct net_device *nd)
{
	u64 ea;
	struct xeth_priv *priv = netdev_priv(nd);
	static u64 platina_mk1_eth0_ea;
	static unsigned char platina_mk1_eth0_ea_assign_type;

	if (!platina_mk1_eth0_ea) {
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return -ENOENT;
		platina_mk1_eth0_ea = ether_addr_to_u64(eth0->dev_addr);
		platina_mk1_eth0_ea_assign_type = eth0->addr_assign_type;
		dev_put(eth0);
	}
	if (memcmp(nd->name, "eth-", 4) == 0)
		ea = (u64)(platina_mk1_eth0_ea + 3 + priv->ref.ndi);
	else if (priv->ref.ndi < 0 || priv->ref.id <= xeth.n.userids)
		ea = xeth.iflink.ea[priv->ref.iflinki];
	else if (priv->ref.id >= xeth.n.userids)
		ea = (u64)(platina_mk1_eth0_ea + 3 + (4094 - priv->ref.ndi));
	else
		ea = platina_mk1_eth0_ea;
	u64_to_ether_addr(ea, nd->dev_addr);
	nd->addr_assign_type = platina_mk1_eth0_ea_assign_type;
	return 0;
}

static void platina_mk1_init_ethtool_settings(struct xeth_priv *priv)
{
	struct ethtool_link_ksettings *settings = &priv->ethtool.settings; 
	settings->base.speed = 0;
	settings->base.duplex = DUPLEX_FULL;
	settings->base.autoneg = AUTONEG_ENABLE;
	settings->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(settings, supported);
	ethtool_link_ksettings_add_link_mode(settings, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     100000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     100000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     100000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(settings, supported,
					     100000baseLR4_ER4_Full);
	bitmap_copy(settings->link_modes.advertising,
		    settings->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static int platina_mk1_validate_speed(struct net_device *nd, u32 speed)
{
	switch (speed) {
	case 100000:
	case 50000:
	case 40000:
	case 25000:
	case 20000:
	case 10000:
	case 1000:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
