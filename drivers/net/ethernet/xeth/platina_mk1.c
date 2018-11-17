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

#include <linux/i2c.h>
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

static const struct i2c_board_info const platina_mk1_i2c_board_info[] = {
	{ I2C_BOARD_INFO("lm75", 0X4f) },
};

enum {
	platina_mk1_n_ports = 32,
	platina_mk1_n_rxqs = 1,
	platina_mk1_n_txqs = 1,
	platina_mk1_n_flags = (sizeof(platina_mk1_flags) / sizeof(char*)) - 1,
	platina_mk1_n_stats = (sizeof(platina_mk1_stats) / sizeof(char*)) - 1,
	platina_mk1_priv_size = sizeof(struct xeth_priv) +
		(platina_mk1_n_stats * sizeof(u64)),
	platina_mk1_n_i2c_clients = ARRAY_SIZE(platina_mk1_i2c_board_info),
};

static bool alpha = false;
static int provision[platina_mk1_n_ports];
static const char * const platina_mk1_iflinks[] = { "eth1", "eth2", NULL };
static void platina_mk1_init_ethtool_settings(struct xeth_priv *priv);
static int platina_mk1_validate_speed(struct net_device *nd, u32 speed);

struct xeth xeth = {
	.name = "platina-mk1",
	.provision = provision,
	.iflinks = platina_mk1_iflinks,
	.priv_size = platina_mk1_priv_size,
	.ports = platina_mk1_n_ports,
	.rxqs = platina_mk1_n_rxqs,
	.txqs = platina_mk1_n_txqs,
	.encap.init = xeth_vlan_init,
	.encap.exit = xeth_vlan_exit,
	.ethtool.n.flags = platina_mk1_n_flags,
	.ethtool.n.stats = platina_mk1_n_stats,
	.ethtool.flags = platina_mk1_flags,
	.ethtool.stats = platina_mk1_stats,
	.init_ethtool_settings = platina_mk1_init_ethtool_settings,
	.validate_speed = platina_mk1_validate_speed,
};

static struct i2c_client *platina_mk1_i2c_client[platina_mk1_n_i2c_clients];

static int __init platina_mk1_init(void)
{
	int i;
	struct i2c_adapter *i2c0 = i2c_get_adapter(0);
	struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
	if (eth0 == NULL)
		return -ENOENT;
	xeth.ea.base = 3 + ether_addr_to_u64(eth0->dev_addr);
	xeth.ea.assign_type = eth0->addr_assign_type;
	dev_put(eth0);

	xeth.base = alpha ? 0 : 1;

	for (i = 0; i < platina_mk1_n_i2c_clients; i++)
		platina_mk1_i2c_client[i] =
			i2c_new_device(i2c0, &platina_mk1_i2c_board_info[i]);
	return xeth_init();
}

static void __exit platina_mk1_exit(void)
{
	int i;
	for (i = 0; i < platina_mk1_n_i2c_clients; i++)
		if (platina_mk1_i2c_client[i])
			i2c_unregister_device(platina_mk1_i2c_client[i]);
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
