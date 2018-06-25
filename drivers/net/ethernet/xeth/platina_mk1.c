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

#define platina_mk1_n_ports	32
#define platina_mk1_n_subports	4
#define platina_mk1_n_iflinks	2
#define platina_mk1_n_encap	VLAN_HLEN
#define platina_mk1_xeth_base_port_id	\
	4094 - (platina_mk1_n_ports * platina_mk1_n_subports)

extern const char *const platina_mk1_stats[];
extern const char *const platina_mk1_flags[];

static bool alpha = false;
module_param(alpha, bool, false);
MODULE_PARM_DESC(alpha, "a 0 based, pre-production system");

static u64 platina_mk1_eth0_ea;
static unsigned char platina_mk1_eth0_ea_assign_type;

static inline int _platina_mk1_assert_iflinks(void)
{
	int err;
	u64 eth1_ea, eth2_ea;
	struct net_device *eth1 = dev_get_by_name(&init_net, "eth1");
	struct net_device *eth2 = dev_get_by_name(&init_net, "eth2");

	if (!eth1) {
		err = xeth_pr_val("%d, %s", -ENOENT, "eth1");
		goto egress;
	}
	if (!eth2) {
		err = xeth_pr_val("%d, %s", -ENOENT, "eth2");
		goto egress;
	}
	err = xeth_pr_true_val("%d, eth1",
			  netdev_rx_handler_register(eth1, xeth.ops.rx_handler,
						     &xeth));
	if (err)
		goto egress;
	err = xeth_pr_true_val("%d, eth2",
			  netdev_rx_handler_register(eth2, xeth.ops.rx_handler,
						     &xeth));
	if (err) {
		netdev_rx_handler_unregister(eth1);
		goto egress;
	}
	eth1_ea = ether_addr_to_u64(eth1->dev_addr);
	eth2_ea = ether_addr_to_u64(eth2->dev_addr);
	if (eth1_ea < eth2_ea) {
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

static int platina_mk1_parse_eth(const char *name, struct xeth_priv *priv)
{
	int base = alpha ? 0 : 1;
	u16 port, subport;

	if (sscanf(name, "eth-%hu-%hu", &port, &subport) != 2)
		return xeth_pr_val("%d: invalid eth-PORT-SUBPORT", -EINVAL);
	if ((port > (platina_mk1_n_ports + base)) || (port < base))
		return xeth_pr_val("%d: invalid PORT", -EINVAL);
	port -= base;
	if ((subport > (platina_mk1_n_subports + base)) || (subport < base))
		return xeth_pr_val("%d: invalid SUBPORT", -EINVAL);
	subport -= base;
	priv->id = 1 + ((port ^ 1) * platina_mk1_n_subports) + subport + 1;
	priv->ndi = (port * platina_mk1_n_subports) + subport;
	priv->iflinki = port >= (platina_mk1_n_ports / 2) ? 1 : 0;
	priv->porti = port;
	priv->subporti = subport;
	priv->devtype = XETH_DEVTYPE_PORT;
	return 0;
}

static inline u16 platina_mk1_xeth_port_vlan(u16 port, u16 subport)
{
	return 4094 - port - (subport * platina_mk1_n_ports);
}

static int platina_mk1_validate_xeth_port(u16 *port)
{
	if (!alpha)
		*port -= 1;
	return (*port >= platina_mk1_n_ports) ?
		xeth_pr_val("%d: invalid PORT", -EINVAL) : 0;
}

static int platina_mk1_validate_xeth_subport(u16 *subport)
{
	if (!alpha)
		*subport -= 1;
	return (*subport >= platina_mk1_n_subports) ?
		xeth_pr_val("%d: invalid SUBPORT", -EINVAL) : 0;
}

static int platina_mk1_validate_xeth_id(u16 id)
{
	return (1 > id || id >= platina_mk1_xeth_base_port_id) ?
		xeth_pr_val("%d: invalid vid", -EINVAL) : 0;
}

static int platina_mk1_parse_xeth(const char *name, struct xeth_priv *priv)
{
	int err;
	u16 port, sub, id;
	if (sscanf(name, "xeth.%hu", &id) == 1) {
		err = platina_mk1_validate_xeth_id(id);
		if (err)
			return err;
		priv->id = id;
		priv->ndi = priv->id;
		priv->iflinki = id & 1;
		priv->porti = -1;
		priv->subporti = -1;
		priv->devtype = XETH_DEVTYPE_BRIDGE;
	} else if (sscanf(name, "xeth%hu-%hu.%huu", &port, &sub, &id) == 3) {
		err = platina_mk1_validate_xeth_port(&port);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_subport(&sub);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_id(id);
		if (err)
			return err;
		priv->id = id;
		priv->ndi = -1;
		priv->iflinki = id & 1;
		priv->porti = port;
		priv->subporti = sub;
		priv->devtype = XETH_DEVTYPE_UNTAGGED_BRIDGE_PORT;
	} else if (sscanf(name, "xeth%hu-%hu.%hut", &port, &sub, &id) == 3) {
		err = platina_mk1_validate_xeth_port(&port);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_subport(&sub);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_id(id);
		if (err)
			return err;
		priv->id = id;
		priv->ndi = -1;
		priv->iflinki = id & 1;
		priv->porti = port;
		priv->subporti = sub;
		priv->devtype = XETH_DEVTYPE_TAGGED_BRIDGE_PORT;
	} else if (sscanf(name, "xeth%hu.%huu", &port, &id) == 2) {
		err = platina_mk1_validate_xeth_port(&port);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_id(id);
		if (err)
			return err;
		priv->id = id;
		priv->ndi = -1;
		priv->iflinki = id & 1;
		priv->porti = port;
		priv->subporti = -1;
		priv->devtype = XETH_DEVTYPE_UNTAGGED_BRIDGE_PORT;
	} else if (sscanf(name, "xeth%hu.%hut", &port, &id) == 2) {
		err = platina_mk1_validate_xeth_port(&port);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_id(id);
		if (err)
			return err;
		priv->id = id;
		priv->ndi = -1;
		priv->iflinki = id & 1;
		priv->porti = port;
		priv->subporti = -1;
		priv->devtype = XETH_DEVTYPE_TAGGED_BRIDGE_PORT;
	} else if (sscanf(name, "xeth%hu-%hu", &port, &sub) == 2) {
		err = platina_mk1_validate_xeth_port(&port);
		if (err)
			return err;
		err = platina_mk1_validate_xeth_subport(&sub);
		if (err)
			return err;
		priv->id = platina_mk1_xeth_port_vlan(port, sub);
		priv->ndi = priv->id;
		priv->iflinki = port & 1;
		priv->porti = port;
		priv->subporti = sub;
		priv->devtype = XETH_DEVTYPE_PORT;
	} else if (sscanf(name, "xeth%hu", &port) == 1) {
		err = platina_mk1_validate_xeth_port(&port);
		if (err)
			return err;
		priv->id = platina_mk1_xeth_port_vlan(port, 0);
		priv->ndi = priv->id;
		priv->iflinki = port & 1;
		priv->porti = port;
		priv->subporti = -1;
		priv->devtype = XETH_DEVTYPE_PORT;
	} else {
		return xeth_pr_val("%d: invalid xeth format", -EINVAL);
	}
	return 0;
}

static int platina_mk1_parse_name(const char *name, struct xeth_priv *priv)
{
	if (memcmp(name, "eth-", 4) == 0)
		return platina_mk1_parse_eth(name, priv);
	else if (strncmp(name, "xeth", 4) == 0)
		return platina_mk1_parse_xeth(name, priv);
	return xeth_pr_val("%d: invalid name", -EINVAL);
}

static int platina_mk1_set_lladdr(struct net_device *nd)
{
	u64 ea;
	struct xeth_priv *priv = netdev_priv(nd);
	if (!platina_mk1_eth0_ea) {
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return xeth_pr_nd_val(nd, "%d, can't find eth0",
					      -ENOENT);
		platina_mk1_eth0_ea = ether_addr_to_u64(eth0->dev_addr);
		platina_mk1_eth0_ea_assign_type = eth0->addr_assign_type;
		dev_put(eth0);
	}
	if (memcmp(nd->name, "eth-", 4) == 0)
		ea = (u64)(platina_mk1_eth0_ea + 3 + priv->ndi);
	else if (priv->ndi < 0 || priv->id < platina_mk1_xeth_base_port_id)
		ea = xeth.ea_iflinks[priv->iflinki];
	else if (priv->id >= platina_mk1_xeth_base_port_id)
		ea = (u64)(platina_mk1_eth0_ea + 3 + (4094 - priv->ndi));
	else
		ea = platina_mk1_eth0_ea;
	u64_to_ether_addr(ea, nd->dev_addr);
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

	xeth.n.ids = 4096;
	xeth.n.nds = 4096;
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
