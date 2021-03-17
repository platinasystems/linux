/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_port.h"
#include "xeth_qsfp.h"
#include "xeth_sbtx.h"
#include "xeth_version.h"
#include "xeth_debug.h"

enum {
	xeth_port_top_vid = 3999,
};

static const char xeth_port_drvname[] = "xeth-port";
static ssize_t xeth_port_subports(size_t port);

struct xeth_port_ext {
	struct i2c_client *qsfp;
	u32 priv_flags;
	atomic64_t stats[xeth_mux_max_stats];
};

struct xeth_port_priv {
	struct xeth_proxy proxy;
	int port, subport;
	struct ethtool_link_ksettings ksettings;
	/* @ext: only included w/ subport[0] */
	struct xeth_port_ext ext[];
};

int xeth_port_of(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->port;
}

int xeth_port_subport(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->subport;
}

static void xeth_port_uninit(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	if (priv->subport <= 0 && priv->ext[0].qsfp) {
		i2c_unregister_device(priv->ext[0].qsfp);
		priv->ext[0].qsfp = NULL;
	}
	xeth_proxy_uninit(nd);
}

static int xeth_port_open(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct gpio_desc *lpmode_gpio;

	if (!(priv->proxy.mux->flags & IFF_UP))
		dev_open(priv->proxy.mux, NULL);
	lpmode_gpio = xeth_mux_qsfp_lpmode_gpio(priv->proxy.mux, priv->port);
	if (lpmode_gpio)
		gpiod_set_value_cansleep(lpmode_gpio, 0);
	return xeth_proxy_open(nd);
}

static int xeth_port_stop(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct gpio_desc *lpmode_gpio;

	lpmode_gpio = xeth_mux_qsfp_lpmode_gpio(priv->proxy.mux, priv->port);
	if (lpmode_gpio)
		gpiod_set_value_cansleep(lpmode_gpio, 1);
	return xeth_proxy_stop(nd);
}

u32 xeth_port_ethtool_priv_flags(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->subport <= 0 ? priv->ext[0].priv_flags : 0;
}

const struct ethtool_link_ksettings *
xeth_port_ethtool_ksettings(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return &priv->ksettings;
}

void xeth_port_reset_ethtool_stats(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	int i;

	if (priv->subport <= 0)
		for (i = 0; i < ARRAY_SIZE(priv->ext[0].stats); i++)
			atomic64_set(&priv->ext[0].stats[i], 0LL);
}

void xeth_port_ethtool_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_port_priv *priv = netdev_priv(nd);

	if (index < ARRAY_SIZE(priv->ext[0].stats))
		atomic64_set(&priv->ext[0].stats[index], count);
	else
		xeth_mux_inc_sbrx_invalid(priv->proxy.mux);
}

void xeth_port_speed(struct net_device *nd, u32 mbps)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	priv->ksettings.base.speed = mbps;
}

const struct net_device_ops xeth_port_ndo = {
	.ndo_init = xeth_proxy_init,
	.ndo_uninit = xeth_port_uninit,
	.ndo_open = xeth_port_open,
	.ndo_stop = xeth_port_stop,
	.ndo_start_xmit = xeth_proxy_start_xmit,
	.ndo_get_iflink = xeth_proxy_get_iflink,
	.ndo_get_stats64 = xeth_proxy_get_stats64,
	.ndo_change_mtu = xeth_proxy_change_mtu,
	.ndo_fix_features = xeth_proxy_fix_features,
	.ndo_set_features = xeth_proxy_set_features,
};

static void xeth_port_get_drvinfo(struct net_device *nd,
				  struct ethtool_drvinfo *drvinfo)
{
	struct xeth_port_priv *priv = netdev_priv(nd);

	strlcpy(drvinfo->driver, xeth_port_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	drvinfo->n_priv_flags = xeth_mux_n_priv_flags(priv->proxy.mux);
	drvinfo->n_stats = xeth_mux_n_stats(priv->proxy.mux);
	scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u:%u",
		  priv->port, priv->proxy.xid);
}

static void xeth_subport_get_drvinfo(struct net_device *nd,
				     struct ethtool_drvinfo *drvinfo)
{
	struct xeth_port_priv *priv = netdev_priv(nd);

	strlcpy(drvinfo->driver, xeth_port_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	drvinfo->n_priv_flags = 0;
	drvinfo->n_stats = 0;
	scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u-%u:%u",
		  priv->port, priv->subport, priv->proxy.xid);
}

static int xeth_port_get_sset_count(struct net_device *nd, int sset)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	int n;

	switch (sset) {
	case ETH_SS_TEST:
		n = 0;
		break;
	case ETH_SS_STATS:
		n = xeth_mux_n_stats(priv->proxy.mux);
		break;
	case ETH_SS_PRIV_FLAGS:
		n = xeth_mux_n_priv_flags(priv->proxy.mux);
		break;
	default:
		n = -EOPNOTSUPP;
		break;
	}
	return n;
}

static void xeth_port_get_strings(struct net_device *nd, u32 sset, u8 *data)
{
	struct xeth_port_priv *priv = netdev_priv(nd);

	switch (sset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		xeth_mux_stat_names(priv->proxy.mux, data);
		break;
	case ETH_SS_PRIV_FLAGS:
		xeth_mux_priv_flag_names(priv->proxy.mux, data);
		break;
	default:
		break;
	}
}

static void xeth_port_get_stats(struct net_device *nd,
				struct ethtool_stats *stats, u64 *data)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	int i, n = xeth_mux_n_stats(priv->proxy.mux);

	for (i = 0; i < n; i++)
		data[i] = atomic64_read(&priv->ext[0].stats[i]);
}

static u32 xeth_port_get_priv_flags(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->subport <= 0 ? priv->ext[0].priv_flags : 0;
}

static int xeth_port_set_priv_flags(struct net_device *nd, u32 flags)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	if (priv->subport > 0)
		return -EINVAL;
	priv->ext[0].priv_flags = flags;
	xeth_sbtx_et_flags(priv->proxy.mux, priv->proxy.xid, flags);
	return 0;
}

static void xeth_port_ksettings(struct ethtool_link_ksettings *ks)
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

static void xeth_subport_ksettings(struct ethtool_link_ksettings *ks)
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
static int xeth_port_get_link_ksettings(struct net_device *nd,
					struct ethtool_link_ksettings *p)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	memcpy(p, &priv->ksettings, sizeof(*p));
	return 0;
}

static int xeth_port_validate_port(struct net_device *nd, u8 port)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ksettings;
	bool t = false;

	switch (port) {
	case PORT_TP:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, TP);
		break;
	case PORT_AUI:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, AUI);
		break;
	case PORT_MII:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, MII);
		break;
	case PORT_FIBRE:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, FIBRE);
		break;
	case PORT_BNC:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, BNC);
		break;
	case PORT_DA:
	case PORT_NONE:
	case PORT_OTHER:
		t = true;
		break;
	}
	return t ? 0 : -EINVAL;
}

static int xeth_port_validate_duplex(struct net_device *nd, u8 duplex)
{
	return xeth_debug_nd_err(nd,
				 duplex != DUPLEX_HALF &&
				 duplex != DUPLEX_FULL &&
				 duplex != DUPLEX_UNKNOWN) ?  -EINVAL : 0;
}

static int xeth_port_validate_speed(struct net_device *nd, u32 speed)
{
        return xeth_debug_nd_err(nd,
				 speed != 100000 &&
				 speed != 50000 &&
				 speed != 40000 &&
				 speed != 25000 &&
				 speed != 20000 &&
				 speed != 10000 &&
				 speed != 1000) ?  -EINVAL : 0;
}

static int
xeth_port_set_link_ksettings(struct net_device *nd,
			     const struct ethtool_link_ksettings *req)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ksettings;
	int err;

	if (req->base.port != ks->base.port) {
		err = xeth_port_validate_port(nd, req->base.port);
		if (err)
			return err;
		ks->base.port = req->base.port;
	}
	if (req->base.autoneg == AUTONEG_DISABLE) {
		err = xeth_port_validate_speed(nd, req->base.speed);
		if (err)
			return err;
		err = xeth_port_validate_duplex(nd, req->base.duplex);
		if (err)
			return err;
		ks->base.autoneg = req->base.autoneg;
		ks->base.speed = req->base.speed;
		ks->base.duplex = req->base.duplex;
	} else {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(res);
		if (bitmap_andnot(res, req->link_modes.advertising,
				  ks->link_modes.supported,
				  __ETHTOOL_LINK_MODE_MASK_NBITS)) {
			return -EINVAL;
		} else {
			err = xeth_port_validate_duplex(nd, req->base.duplex);
			if (err)
				return err;
			bitmap_copy(ks->link_modes.advertising,
				    req->link_modes.advertising,
				    __ETHTOOL_LINK_MODE_MASK_NBITS);
			ks->base.autoneg = AUTONEG_ENABLE;
			ks->base.speed = 0;
			ks->base.duplex = req->base.duplex;
		}
	}

	return xeth_sbtx_et_settings(priv->proxy.mux, priv->proxy.xid, ks);
}

#define xeth_port_ks_supports(ks, mk)					\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, supported, mk));	\
})

#define xeth_port_ks_advertising(ks, mk)				\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, advertising, mk));	\
})

static int xeth_port_get_fecparam(struct net_device *nd,
				  struct ethtool_fecparam *param)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ksettings;
	const u32 fec_both = ETHTOOL_FEC_RS | ETHTOOL_FEC_BASER;

	param->fec = 0;
	param->active_fec = 0;
	if (xeth_port_ks_supports(ks, FEC_NONE))
		param->fec |= ETHTOOL_FEC_OFF;
	if (xeth_port_ks_supports(ks, FEC_RS))
		param->fec |= ETHTOOL_FEC_RS;
	if (xeth_port_ks_supports(ks, FEC_BASER))
		param->fec |= ETHTOOL_FEC_BASER;
	if ((param->fec & fec_both) == fec_both)
		param->fec |= ETHTOOL_FEC_AUTO;
	if (!param->fec)
		param->fec = ETHTOOL_FEC_NONE;
	if (param->fec == ETHTOOL_FEC_NONE)
		param->active_fec = ETHTOOL_FEC_NONE;
	else if (xeth_port_ks_advertising(ks, FEC_NONE))
		param->active_fec = ETHTOOL_FEC_OFF;
	else if (xeth_port_ks_advertising(ks, FEC_RS))
		param->active_fec = xeth_port_ks_advertising(ks, FEC_BASER) ?
			ETHTOOL_FEC_AUTO : ETHTOOL_FEC_RS;
	else if (xeth_port_ks_advertising(ks, FEC_BASER))
		param->fec = ETHTOOL_FEC_BASER;
	return 0;
}

static int xeth_port_set_fecparam(struct net_device *nd,
				  struct ethtool_fecparam *param)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ksettings;
	switch (param->fec) {
	case ETHTOOL_FEC_AUTO:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_RS) ||
		    !ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_BASER)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	case ETHTOOL_FEC_OFF:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_NONE)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	case ETHTOOL_FEC_RS:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_RS)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	case ETHTOOL_FEC_BASER:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_BASER)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	default:
		return -EINVAL;
	}
	return xeth_sbtx_et_settings(priv->proxy.mux, priv->proxy.xid, ks);
}

int xeth_port_get_module_info(struct net_device *nd,
			      struct ethtool_modinfo *emi)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->ext[0].qsfp ?
		xeth_qsfp_get_module_info(priv->ext[0].qsfp, emi) : -ENXIO;
}

int xeth_port_get_module_eeprom(struct net_device *nd,
				struct ethtool_eeprom *ee, u8 *data)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->ext[0].qsfp ?
		xeth_qsfp_get_module_eeprom(priv->ext[0].qsfp, ee, data) :
		-ENXIO;
}

static const struct ethtool_ops xeth_port_eto = {
	.get_drvinfo = xeth_port_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = xeth_port_get_sset_count,
	.get_strings = xeth_port_get_strings,
	.get_ethtool_stats = xeth_port_get_stats,
	.get_priv_flags = xeth_port_get_priv_flags,
	.set_priv_flags = xeth_port_set_priv_flags,
	.get_link_ksettings = xeth_port_get_link_ksettings,
	.set_link_ksettings = xeth_port_set_link_ksettings,
	.get_fecparam = xeth_port_get_fecparam,
	.set_fecparam = xeth_port_set_fecparam,
	.get_module_info = xeth_port_get_module_info,
	.get_module_eeprom = xeth_port_get_module_eeprom,
};

static const struct ethtool_ops xeth_subport_eto = {
	.get_drvinfo = xeth_subport_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = xeth_port_get_link_ksettings,
	.set_link_ksettings = xeth_port_set_link_ksettings,
};

static void xeth_port_setup(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);

	xeth_proxy_setup(nd);
	netif_carrier_off(nd);
	priv->proxy.kind = XETH_DEV_KIND_PORT;
	nd->netdev_ops = &xeth_port_ndo;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	ether_setup(nd);
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
	nd->priv_flags |= IFF_DONT_BRIDGE;
}

static void xeth_port_qsfp(struct xeth_port_priv *priv, u8 bus)
{
	struct gpio_desc *absent_gpio =
		xeth_mux_qsfp_absent_gpio(priv->proxy.mux, priv->port);
	struct gpio_desc *reset_gpio =
		xeth_mux_qsfp_reset_gpio(priv->proxy.mux, priv->port);
	if (!absent_gpio || !reset_gpio)
		return;
	if (gpiod_get_value_cansleep(absent_gpio))
		return;
	gpiod_set_value_cansleep(reset_gpio, 0);
	priv->ext[0].qsfp = xeth_qsfp_client(bus);
	if (!priv->ext[0].qsfp)
		xeth_debug("qsfp[%d] not found @%d", priv->port, bus);
}

static int xeth_port_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing mux link");
		return -EINVAL;
	}
	if (data) {
		if (data[XETH_PORT_IFLA_XID]) {
			u16 xid = nla_get_u16(data[XETH_PORT_IFLA_XID]);
			if (xid == 0) {
				NL_SET_ERR_MSG(extack, "out-of-range XID");
				return -ERANGE;
			}
		}
	}
	return 0;
}

static int xeth_port_newlink(struct net *src_net, struct net_device *nd,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	u32 link = nla_get_u32(tb[IFLA_LINK]);
	int err;

	priv->proxy.nd = nd;
	priv->proxy.mux = dev_get_by_index_rcu(dev_net(nd), link);
	nd->min_mtu = priv->proxy.mux->min_mtu;
	nd->max_mtu = priv->proxy.mux->max_mtu;
	priv->port = -1;
	priv->subport = -1;

	if (data && data[XETH_PORT_IFLA_XID])
		priv->proxy.xid = nla_get_u16(data[XETH_PORT_IFLA_XID]);
	else
		for (priv->proxy.xid = xeth_port_top_vid;
		     xeth_mux_proxy_of_xid(priv->proxy.mux, priv->proxy.xid);
		     priv->proxy.xid--);

	if (!tb || !tb[IFLA_IFNAME])
		scnprintf(nd->name, IFNAMSIZ, "%s%u",
			  xeth_port_drvname, priv->proxy.xid);

	xeth_mux_add_proxy(&priv->proxy);
	if (err = register_netdevice(nd), err) {
		xeth_mux_del_proxy(&priv->proxy);
		return err;
	}
	return xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_NEW);
}

static void xeth_port_dellink(struct net_device *nd, struct list_head *unregq)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	xeth_mux_del_vlans(priv->proxy.mux, nd, unregq);
	unregister_netdevice_queue(nd, unregq);
}

static struct net *xeth_port_get_link_net(const struct net_device *nd)
{
	return dev_net(nd);
}

static const struct nla_policy xeth_port_nla_policy[] = {
	[XETH_PORT_IFLA_XID] = { .type = NLA_U16 },
};

struct rtnl_link_ops xeth_port_lnko = {
	.kind		= xeth_port_drvname,
	.priv_size	= sizeof(struct xeth_port_priv),
	.setup		= xeth_port_setup,
	.validate	= xeth_port_validate,
	.newlink	= xeth_port_newlink,
	.dellink	= xeth_port_dellink,
	.get_link_net	= xeth_port_get_link_net,
	.policy		= xeth_port_nla_policy,
	.maxtype	= XETH_PORT_N_IFLA - 1,
};

static bool xeth_port_is_platina_mk1(struct platform_device *pd);

static const char *xeth_port_mux_prop(struct platform_device *pd)
{
	const char *val;
	if (xeth_port_is_platina_mk1(pd))
		val = "platina-mk1";
	else if (device_property_read_string(&pd->dev, "mux", &val))
		val = "xeth-mux";
	return val;
}

static int xeth_port_index_prop(struct platform_device *pd)
{
	u32 val;
	return pd->id >= 0 && !pd->id_auto ? pd->id :
		device_property_read_u32(&pd->dev, "index", &val) ?
		-EINVAL : val;
}

static u64 xeth_port_addr_prop(struct platform_device *pd)
{
	u64 val;
	return device_property_read_u64(&pd->dev, "addr", &val) ?  0 : val;
}

static u8 xeth_port_qs_prop(struct platform_device *pd, const char *label)
{
	u8 val;
	return device_property_read_u8(&pd->dev, label, &val) ?  1 : val;
}

static u8 xeth_port_qsfp_bus_prop(struct platform_device *pd)
{
	u8 val;
	return device_property_read_u8(&pd->dev, "qsfp-bus", &val) ?  0 : val;
}

static int xeth_port(struct platform_device *pd, struct net_device *mux,
		     const char *ifname, int port, int subport, u64 addr)
{
	struct net_device *nd;
	struct xeth_port_priv *priv;
	size_t sz;
	int i, err;

	sz = sizeof(*priv);
	if (subport <= 0)
		sz += sizeof(struct xeth_port_ext);
	nd = alloc_netdev_mqs(sz, ifname, NET_NAME_ENUM, xeth_port_setup,
			      xeth_port_qs_prop(pd, "txqs"),
			      xeth_port_qs_prop(pd, "rxqs"));
	if (!nd)
		return -ENOMEM;

	priv = netdev_priv(nd);
	priv->proxy.nd = nd;
	priv->proxy.mux = mux;

	priv->proxy.xid = xeth_port_top_vid - port;
	if (subport >= 0)
		priv->proxy.xid -= (subport * xeth_mux_ports(mux));

	nd->min_mtu = priv->proxy.mux->min_mtu;
	nd->max_mtu = priv->proxy.mux->max_mtu;

	if (addr) {
		u64_to_ether_addr(addr, nd->dev_addr);
		nd->addr_assign_type = NET_ADDR_PERM;
	} else
		eth_hw_addr_random(nd);

	priv->port = port;
	priv->subport = subport;

	if (subport <= 0) {
		u8 bus = xeth_port_qsfp_bus_prop(pd);
		nd->ethtool_ops = &xeth_port_eto;
		for (i = 0; i < ARRAY_SIZE(priv->ext[0].stats); i++)
			atomic64_set(&priv->ext[0].stats[i], 0LL);
		if (bus)
			xeth_port_qsfp(priv, bus);
	} else
		nd->ethtool_ops = &xeth_subport_eto;

	if (subport < 0)
		xeth_port_ksettings(&priv->ksettings);
	else
		xeth_subport_ksettings(&priv->ksettings);

	xeth_mux_add_proxy(&priv->proxy);

	rtnl_lock();
	err = xeth_debug_nd_err(nd, register_netdevice(nd));
	rtnl_unlock();

	if (err) {
		xeth_mux_del_proxy(&priv->proxy);
		free_netdev(nd);
	}
	return err;
}

static int xeth_port_probe(struct platform_device *pd)
{
	struct net_device *mux;
	char name[IFNAMSIZ];
	int port, subport, subports, err;
	u64 addr;
	u8 base_port;

	mux = dev_get_by_name(&init_net, xeth_port_mux_prop(pd));
	if (!mux)
		return -EPROBE_DEFER;

	port = xeth_port_index_prop(pd);
	addr = xeth_port_addr_prop(pd);
	subports = xeth_port_subports(port);
	base_port = xeth_mux_base_port(mux);

	if (subports > 1) {
		for (subport = err = 0; !err && subport < subports; subport++) {
			scnprintf(name, IFNAMSIZ, "xeth%u-%u",
				  port + base_port, subport + base_port);
			err = xeth_port(pd, mux, name, port, subport, addr);
		}
	} else {
		scnprintf(name, IFNAMSIZ, "xeth%u", port + base_port);
		err = xeth_port(pd, mux, name, port, -1, addr);
	}

	dev_put(mux);
	if (err)
		pr_err("can't make %s: %d\n", name, err);
	return err;
}

static int xeth_port_remove(struct platform_device *pd)
{
	/* port netdevs are removed by the mux */
	return 0;
}

int xeth_port_provision[512], xeth_port_provisioned;

module_param_array_named(provision, xeth_port_provision, int,
			 &xeth_port_provisioned, 0644);
MODULE_PARM_DESC(provision, " 1 (default), 2, or 4 subports per port");

static ssize_t xeth_port_subports(size_t port)
{
	return port < ARRAY_SIZE(xeth_port_provision) ?
		xeth_port_provision[port] : -EINVAL;
}

static ssize_t provisioned_show(struct device_driver *drv, char *buf)
{
	int port;
	for (port = 0;
	     port < ARRAY_SIZE(xeth_port_provision) &&
	     xeth_port_provision[port];
	     port++)
		buf[port] = xeth_port_provision[port] + '0';
	return port;
}

static DRIVER_ATTR_RO(provisioned);

static struct attribute *xeth_port_attrs[] = {
	&driver_attr_provisioned.attr,
	NULL,
};

ATTRIBUTE_GROUPS(xeth_port);

static const struct of_device_id xeth_port_of_match[] = {
	{ .compatible = "xeth,port", },
	{},
};

MODULE_DEVICE_TABLE(of, xeth_port_of_match);

static const struct platform_device_id xeth_port_id_match[] = {
	{ .name = "xeth-port" },
	{},
};

MODULE_DEVICE_TABLE(platform, xeth_port_id_match);

static bool xeth_port_is_platina_mk1(struct platform_device *pd)
{
	return pd->id_entry == &xeth_port_id_match[0];

}

struct platform_driver xeth_port_driver = {
	.driver = {
		.name = xeth_port_drvname,
		.of_match_table = xeth_port_of_match,
		.groups = xeth_port_groups,
	},
	.probe = xeth_port_probe,
	.remove = xeth_port_remove,
	.id_table = xeth_port_id_match,
};
