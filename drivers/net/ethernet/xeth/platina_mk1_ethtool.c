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

#include "platina_mk1.h"

void platina_mk1_ethtool_init_settings(struct xeth_priv *priv)
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

int platina_mk1_ethtool_validate_speed(struct net_device *nd, u32 speed)
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
