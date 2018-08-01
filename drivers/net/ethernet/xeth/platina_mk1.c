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

static const char *platina_mk1_iflink_names[] = { "eth1", "eth2" };

static inline int _platina_mk1_assert_iflinks(void)
{
	int i, err;
	u64 ea_iflink[platina_mk1_n_iflinks] = { 0, 0 };
	struct net_device *iflink[platina_mk1_n_iflinks] = { NULL, NULL };
	bool registered_iflink[platina_mk1_n_iflinks] = { false, false };

	for (i=0; i<platina_mk1_n_iflinks; i++) {
		const char *ifname = platina_mk1_iflink_names[i];
		iflink[i] = dev_get_by_name(&init_net, ifname);
		if (!iflink[i]) {
			err = -ENOENT;
			goto egress;
		}
		err = netdev_rx_handler_register(iflink[i],
						 xeth.ops.rx_handler,
						 &xeth);
		if (err)
			goto egress;
		else
			registered_iflink[i] = true;
		ea_iflink[i] = ether_addr_to_u64(iflink[i]->dev_addr);
	}
	if (ea_iflink[0] < ea_iflink[1]) {
		xeth_set_iflink(0, iflink[0]);
		xeth_set_iflink(1, iflink[1]);
	} else {
		xeth_set_iflink(1, iflink[0]);
		xeth_set_iflink(0, iflink[1]);
	}
	return 0;
egress:
	for (i=0; i<platina_mk1_n_iflinks; i++) {
		if (iflink[i]) {
			if (registered_iflink[i])
				netdev_rx_handler_unregister(iflink[i]);
			dev_put(iflink[i]);
		}
	}
	return err;
}

static int platina_mk1_assert_iflinks(void)
{
	static struct mutex platina_mk1_iflinks_mutex;
	int err = 0;

	if (xeth_iflink(0))
		return err;
	mutex_lock(&platina_mk1_iflinks_mutex);
	if (!xeth_iflink(0))
		err = _platina_mk1_assert_iflinks();
	mutex_unlock(&platina_mk1_iflinks_mutex);
	return err;
}

static int platina_mk1_parse_eth(const char *name, struct xeth_priv *priv)
{
	int base = alpha ? 0 : 1;
	u16 port, subport;
	const char *p = name + 4;

	if (xeth_pr_true_expr(!*p, "[%s] incomplete", name))
		return -EINVAL;
	if (xeth_pr_true_expr(sscanf(p, "%hu-%hu", &port, &subport) != 2,
			      "[%s] invalid eth-PORT-SUBPORT", name))
		return -EINVAL;
	if (xeth_pr_true_expr((port >= (platina_mk1_n_ports + base)) ||
			      (port < base),
			      "[%s] out-of-range PORT %u", name, port))
		return -EINVAL;
	port -= base;
	if (xeth_pr_true_expr((subport >= (platina_mk1_n_subports + base)) ||
			      (subport < base),
			      "[%s] out-of-range SUBPORT %u", name, subport))
		return -EINVAL;
	subport -= base;
	priv->portid = 1 + ((port ^ 1) * platina_mk1_n_subports) + subport + 1;
	priv->id = priv->portid;
	priv->ndi = (port * platina_mk1_n_subports) + subport;
	priv->iflinki = port >= (platina_mk1_n_ports / 2) ? 1 : 0;
	priv->porti = port;
	priv->subporti = subport;
	priv->devtype = XETH_DEVTYPE_PORT;
	return 0;
}

static int platina_mk1_parse_xethbr(const char *name, struct xeth_priv *priv)
{
	u16 u;
	const char *p = name + 7;

	if (xeth_pr_true_expr(!*p, "[%s] incomplete", name))
		return -EINVAL;
	if (xeth_pr_true_expr(sscanf(p, "%hu", &u) != 1,
			      "[%s] invalid BRIDGE", name))
		return -EINVAL;
	if (xeth_pr_true_expr(1 > u || u >= platina_mk1_xeth_base_port_id,
			      "[%s] out-of-range ID %u", name, u))
		return -EINVAL;
	priv->porti = -1;
	priv->subporti = -1;
	priv->portid = -1;
	priv->id = u;
	priv->ndi = priv->id;
	priv->iflinki = priv->id & 1;
	priv->devtype = XETH_DEVTYPE_BRIDGE;
	return 0;
}

static int platina_mk1_parse_xeth(const char *name, struct xeth_priv *priv)
{
	int n;
	u16 u;
	const char *p = name + 4;

	if (xeth_pr_true_expr(!*p, "[%s] incomplete", name))
		return -EINVAL;
	if (xeth_pr_true_expr(sscanf(p, "%hu%n", &u, &n) != 1,
			      "[%s] invalid PORT [%s]", name, p))
		return -EINVAL;
	p += n;
	if (!alpha)
		u -= 1;
	if (xeth_pr_true_expr(u >= platina_mk1_n_ports,
			      "[%s] out-of-range PORT %u", name, u))
		return -EINVAL;
	priv->porti = u;
	priv->subporti = -1;
	priv->portid = 4094 - u;
	priv->id = priv->portid;
	priv->devtype = XETH_DEVTYPE_PORT;
	if (*p == '-') {
		p++;
		if (xeth_pr_true_expr(sscanf(p, "%hu%n", &u, &n) != 1,
				      "[%s] invalid SUBPORT [%s]", name, p))
			return -EINVAL;
		if (!alpha)
			u -= 1;
		if (xeth_pr_true_expr(u >= platina_mk1_n_subports,
				      "[%s] out-of-range SUBPORT %u", name, u))
			return -EINVAL;
		p += n;
		priv->subporti = u;
		priv->portid -= (u * platina_mk1_n_ports);
		priv->id = priv->portid;
	}
	if (*p == '.') {
		p++;
		if (xeth_pr_true_expr(sscanf(p, "%hu%n", &u, &n) != 1,
				      "[%s] invalid ID [%s]", name, p))
			return -EINVAL;
		if (xeth_pr_true_expr(1 > u ||
				      u >= platina_mk1_xeth_base_port_id,
				      "[%s] out-of-range ID %u", name, u))
			return -EINVAL;
		p += n;
		switch (*p) {
		case 't':
			priv->devtype = XETH_DEVTYPE_TAGGED_BRIDGE_PORT;
			break;
		case 'u':
			priv->devtype = XETH_DEVTYPE_UNTAGGED_BRIDGE_PORT;
			break;
		default:
			xeth_pr("[%s] invalid suffix [%s]", name, p);
			return -EINVAL;
		}
		p++;
		priv->id = u;
		priv->ndi = -1;
	} else {
		priv->ndi = priv->id;
	}
	if (xeth_pr_true_expr(*p, "[%s] invalid suffix [%s]", name, p))
		return -EINVAL;
	priv->iflinki = priv->id & 1;
	return 0;
}

static int platina_mk1_parse(const char *name, struct xeth_priv *priv)
{
	if (memcmp(name, "eth-", 4) == 0)
		return platina_mk1_parse_eth(name, priv);
	else if (memcmp(name, "xethbr.", 7) == 0)
		return platina_mk1_parse_xethbr(name, priv);
	else if (memcmp(name, "xeth", 4) == 0)
		return platina_mk1_parse_xeth(name, priv);
	xeth_pr("'%s' invalid ifname", name);
	return -EINVAL;
}

static int platina_mk1_set_lladdr(struct net_device *nd)
{
	u64 ea;
	struct xeth_priv *priv = netdev_priv(nd);
	if (!platina_mk1_eth0_ea) {
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return -ENOENT;
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
	xeth.ethtool.flags = platina_mk1_flags;
	for (xeth.n.ethtool.flags = 0;
	     platina_mk1_flags[xeth.n.ethtool.flags];
	     xeth.n.ethtool.flags++);
	xeth.ops.assert_iflinks = platina_mk1_assert_iflinks;
	xeth.ops.parse = platina_mk1_parse;
	xeth.ops.set_lladdr = platina_mk1_set_lladdr;
	xeth.ops.init_ethtool_settings = platina_mk1_init_ethtool_settings;
	xeth.ops.validate_speed = platina_mk1_validate_speed;
	for (i = 0; inits[i]; i++) {
		int err = inits[i]();
		if (err) {
			platina_mk1_egress();
			return err;
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
