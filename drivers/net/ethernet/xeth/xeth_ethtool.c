/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

static void xeth_ethtool_get_drvinfo(struct net_device *nd,
				     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, xeth_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, xeth_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "n/a", sizeof(drvinfo->bus_info));
	drvinfo->n_priv_flags = xeth_n_ethtool_flags;
}

static int xeth_ethtool_get_sset_count(struct net_device *nd, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return xeth_n_ethtool_stats;
	case ETH_SS_PRIV_FLAGS:
		return xeth_n_ethtool_flags;
	default:
		return -EOPNOTSUPP;
	}
}

static void xeth_ethtool_get_strings(struct net_device *nd, u32 sset, u8 *data)
{
	char *p = (char *)data;
	unsigned int i;

	switch (sset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		for (i = 0; xeth_config->ethtool.stats[i]; i++) {
			strlcpy(p, xeth_config->ethtool.stats[i],
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; xeth_config->ethtool.flags[i]; i++) {
			strlcpy(p, xeth_config->ethtool.flags[i],
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xeth_ethtool_get_ethtool_stats(struct net_device *nd,
					   struct ethtool_stats *stats,
					   u64 *data)
{
	struct xeth_priv *priv = netdev_priv(nd);
	xeth_priv_lock_ethtool(priv);
	memcpy(data, priv->ethtool_stats, xeth_n_ethtool_stats*sizeof(u64));
	xeth_priv_unlock_ethtool(priv);
}

static u32 xeth_ethtool_get_priv_flags(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	u32 flags;
	xeth_priv_lock_ethtool(priv);
	flags = priv->ethtool.flags;
	xeth_priv_unlock_ethtool(priv);
	return flags;
}

static int xeth_ethtool_set_priv_flags(struct net_device *nd, u32 flags)
{
	struct xeth_priv *priv = netdev_priv(nd);
	if (flags >= (1 << xeth_n_ethtool_flags))
		return -EINVAL;
	xeth_priv_lock_ethtool(priv);
	priv->ethtool.flags = flags;
	xeth_priv_unlock_ethtool(priv);
	return xeth_sb_send_ethtool_flags(nd);
}

static int xeth_ethtool_get_link_ksettings(struct net_device *nd,
					   struct ethtool_link_ksettings *res)
{
	struct xeth_priv *priv = netdev_priv(nd);
	xeth_priv_lock_ethtool(priv);
	memcpy(res, &priv->ethtool.settings,
	       sizeof(struct ethtool_link_ksettings));
	xeth_priv_unlock_ethtool(priv);
	return 0;
}

static int xeth_ethtool_validate_duplex(struct net_device *nd,
					const struct ethtool_link_ksettings
					*req)
{
	switch (req->base.duplex) {
	case DUPLEX_HALF:
	case DUPLEX_FULL:
	case DUPLEX_UNKNOWN:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int xeth_ethtool_set_link_ksettings(struct net_device *nd,
					   const struct ethtool_link_ksettings
					   *req)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *settings = &priv->ethtool.settings;
	int err = 0;
	xeth_priv_lock_ethtool(priv);
	if (req->base.autoneg == AUTONEG_DISABLE) {
		u32 speed = req->base.speed;
		if (xeth_config->ethtool.validate_speed)
			err = xeth_config->ethtool.validate_speed(speed);
		if (!err)
			err = xeth_ethtool_validate_duplex(nd, req);
		if (!err) {
			settings->base.autoneg = req->base.autoneg;
			settings->base.speed = req->base.speed;
			settings->base.duplex = req->base.duplex;
		}
	} else {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(res);
		if (bitmap_andnot(res,
				  req->link_modes.advertising,
				  settings->link_modes.supported,
				  __ETHTOOL_LINK_MODE_MASK_NBITS)) {
			err = -EINVAL;
		} else {
			int err;
			err = xeth_ethtool_validate_duplex(nd, req);
			if (!err) {
				bitmap_copy(settings->link_modes.advertising,
					    req->link_modes.advertising,
					    __ETHTOOL_LINK_MODE_MASK_NBITS);
				settings->base.autoneg = AUTONEG_ENABLE;
				settings->base.speed = 0;
				settings->base.duplex = req->base.duplex;
			}
		}
	}
	if (!err)
		err = xeth_sb_send_ethtool_settings(nd);
	xeth_priv_unlock_ethtool(priv);
	return err;
}

struct ethtool_ops xeth_ethtool_ops = {
	.get_drvinfo	    = xeth_ethtool_get_drvinfo,
	.get_link	    = ethtool_op_get_link,
	.get_sset_count	    = xeth_ethtool_get_sset_count,
	.get_strings	    = xeth_ethtool_get_strings,
	.get_ethtool_stats  = xeth_ethtool_get_ethtool_stats,
	.get_priv_flags	    = xeth_ethtool_get_priv_flags,
	.set_priv_flags     = xeth_ethtool_set_priv_flags,
	.get_link_ksettings = xeth_ethtool_get_link_ksettings,
	.set_link_ksettings = xeth_ethtool_set_link_ksettings,
};
