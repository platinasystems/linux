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
#include "sysfs.h"

#define platina_mk1_n_ports	32
#define platina_mk1_n_subports	4
#define platina_mk1_n_iflinks	2
#define platina_mk1_n_nds	(platina_mk1_n_ports * platina_mk1_n_subports)
#define platina_mk1_n_ids	(2 + platina_mk1_n_nds)
#define platina_mk1_n_encap	VLAN_HLEN

extern const char *const platina_mk1_stats[];
extern const char *const platina_mk1_flags[];

static bool alpha = false;
module_param(alpha, bool, false);
MODULE_PARM_DESC(alpha, "a 0 based, pre-production system");

static u64 platina_mk1_eth0_ea64;
static unsigned char platina_mk1_eth0_ea_assign_type;

static inline int _platina_mk1_assert_iflinks(void)
{
	struct net_device *eth1 = dev_get_by_name(&init_net, "eth1");
	struct net_device *eth2 = dev_get_by_name(&init_net, "eth2");
	int err;

	if (!eth1) {
		err = xeth_pr_val("%d, %s", -ENOENT, "eth1");
		goto egress;
	}
	if (!eth2) {
		err = xeth_pr_val("%d, %s", -ENOENT, "eth2");
		goto egress;
	}
	err = xeth_pr_val("%d, eth1",
			  netdev_rx_handler_register(eth1, xeth.ops.rx_handler,
						     &xeth));
	if (err)
		goto egress;
	err = xeth_pr_val("%d, eth2",
			  netdev_rx_handler_register(eth2, xeth.ops.rx_handler,
						     &xeth));
	if (err) {
		netdev_rx_handler_unregister(eth1);
		goto egress;
	}
	if (true) {	/* FIXME sort by bus address */
		xeth_set_iflinks(0, eth1);
		xeth_set_iflinks(1, eth2);
	} else {
		xeth_set_iflinks(1, eth1);
		xeth_set_iflinks(0, eth2);
	}
	return 0;
egress:
	if (eth2)
		dev_put(eth2);
	if (eth2)
		dev_put(eth2);
	return err;
}

static int platina_mk1_assert_iflinks(void)
{
	static struct mutex platina_mk1_iflinks_mutex;
	int err = 0;

	if (xeth_iflinks(0))
		return err;
	mutex_lock(&platina_mk1_iflinks_mutex);
	if (!xeth_iflinks(0))
		err = _platina_mk1_assert_iflinks();
	mutex_unlock(&platina_mk1_iflinks_mutex);
	return err;
}

static int platina_mk1_parse_name(const char *name,
				  u16 *id, u16 *ndi, u16 *iflinki)
{
	int base = alpha ? 0 : 1;
	u16 port, subport;

	if (sscanf(name, "eth-%hu-%hu", &port, &subport) != 2)
		return -EINVAL;
	if ((port > (platina_mk1_n_ports + base)) || (port < base))
		return -EINVAL;
	port -= base;
	if ((subport > (platina_mk1_n_subports + base)) || (subport < base))
		return -EINVAL;
	subport -= base;
	*id = 1 + ((port ^ 1) * platina_mk1_n_subports) + subport + 1;
	*ndi = (port * platina_mk1_n_subports) + subport;
	*iflinki = port >= (platina_mk1_n_ports / 2) ? 1 : 0;
	return 0;
}

static int platina_mk1_set_lladdr(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);

	if (!platina_mk1_eth0_ea64) {
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return xeth_pr_nd_val(nd, "%d, can't find eth0",
					      -ENOENT);
		platina_mk1_eth0_ea64 = ether_addr_to_u64(eth0->dev_addr);
		platina_mk1_eth0_ea_assign_type = eth0->addr_assign_type;
		dev_put(eth0);
	}
	u64_to_ether_addr(platina_mk1_eth0_ea64 + 3 + priv->ndi, nd->dev_addr);
	nd->addr_assign_type = platina_mk1_eth0_ea_assign_type;
	return 0;
}

static void platina_mk1_init_ethtool_settings(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
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
	memcpy(settings->link_modes.advertising,
	       settings->link_modes.supported,
	       sizeof(settings->link_modes.supported));
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
		xeth_pr_nd(nd, "invalid speed: %dMb/s", speed);
		return -EINVAL;
	}
	return 0;
}

static void platina_mk1_egress(void)
{
	void (*const exits[])(void) = {
		xeth_sb_exit,
		xeth_ethtool_exit,
		xeth_notifier_exit,
		xeth_link_exit,
		xeth_ndo_exit,
		xeth_notifier_exit,
		xeth_vlan_exit,
		xeth_exit,
		NULL,
	};
	int i;

	for (i = 0; exits[i]; i++)
		exits[i]();
}

static int __init platina_mk1_init(void)
{
	int (*const inits[])(void) = {
		xeth_init,
		xeth_vlan_init,
		xeth_link_init,
		xeth_ndo_init,
		xeth_notifier_init,
		xeth_ethtool_init,
		xeth_sb_init,
		NULL,
	};
	int i;

	xeth.n.ids = platina_mk1_n_ids;
	xeth.n.nds = platina_mk1_n_nds,
	xeth.n.iflinks = platina_mk1_n_iflinks;
	xeth.n.encap = platina_mk1_n_encap;
	xeth.ethtool.stats = platina_mk1_stats;
	for (xeth.n.ethtool.stats = 0;
	     platina_mk1_stats[xeth.n.ethtool.stats];
	     xeth.n.ethtool.stats++);
	xeth_pr("%zd", xeth.n.ethtool.stats);
	xeth.ethtool.flags = platina_mk1_flags;
	for (xeth.n.ethtool.flags = 0;
	     platina_mk1_flags[xeth.n.ethtool.flags];
	     xeth.n.ethtool.flags++);
	xeth_pr("%zd", xeth.n.ethtool.flags);
	xeth.ops.assert_iflinks = platina_mk1_assert_iflinks;
	xeth.ops.parse_name = platina_mk1_parse_name;
	xeth.ops.set_lladdr = platina_mk1_set_lladdr;
	xeth.ops.rtnl.kind = "platina-mk1";
	xeth.ops.init_ethtool_settings = platina_mk1_init_ethtool_settings;
	xeth.ops.validate_speed = platina_mk1_validate_speed;
	for (i = 0; inits[i]; i++) {
		int err = inits[i]();
		if (err) {
			platina_mk1_egress();
			return xeth_pr_true_val("%d", err);
		}
	}
	return 0;
}

static void __exit platina_mk1_exit(void)
{
	platina_mk1_egress();
}

module_init(platina_mk1_init);
module_exit(platina_mk1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("XETH for Platina Systems MK1 TOR Ethernet Switch");
