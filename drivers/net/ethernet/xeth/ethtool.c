/* XETH ethtool ops
 *
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

static void xeth_ethtool_get_drvinfo(struct net_device *nd,
				     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, xeth.ops.rtnl.kind, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, __stringify(XETH_VERSION),
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "n/a", sizeof(drvinfo->bus_info));
	drvinfo->n_priv_flags = xeth.n.ethtool.flags;
}

static int xeth_ethtool_get_sset_count(struct net_device *nd, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return xeth.n.ethtool.stats;
	case ETH_SS_PRIV_FLAGS:
		return xeth.n.ethtool.flags;
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
		for (i = 0; i < xeth.n.ethtool.stats; i++) {
			strlcpy(p, xeth.ethtool.stats[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < xeth.n.ethtool.flags; i++) {
			strlcpy(p, xeth.ethtool.flags[i], ETH_GSTRING_LEN);
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
	mutex_lock(&priv->ethtool.mutex);
	memcpy(data, priv->ethtool.stats, xeth.n.ethtool.stats*sizeof(u64));
	mutex_unlock(&priv->ethtool.mutex);
}

static u32 xeth_ethtool_get_priv_flags(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	u32 flags;
	mutex_lock(&priv->ethtool.mutex);
	flags = priv->ethtool.flags;
	mutex_unlock(&priv->ethtool.mutex);
	return flags;
}

static int xeth_ethtool_set_priv_flags(struct net_device *nd, u32 flags)
{
	struct xeth_priv *priv = netdev_priv(nd);
	if (flags >= (1 << xeth.n.ethtool.flags))
		return -EINVAL;
	mutex_lock(&priv->ethtool.mutex);
	priv->ethtool.flags = flags;
	mutex_unlock(&priv->ethtool.mutex);
	return xeth_sb_send_ethtool_flags(nd);
}

static int xeth_ethtool_get_link_ksettings(struct net_device *nd,
					   struct ethtool_link_ksettings *res)
{
	struct xeth_priv *priv = netdev_priv(nd);
	mutex_lock(&priv->ethtool.mutex);
	memcpy(res, &priv->ethtool.settings,
	       sizeof(struct ethtool_link_ksettings));
	mutex_unlock(&priv->ethtool.mutex);
	return 0;
}

static int xeth_ethtool_set_link_ksettings(struct net_device *nd,
					   const struct ethtool_link_ksettings
					   *req)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *settings = &priv->ethtool.settings;
	int err = 0;
	mutex_lock(&priv->ethtool.mutex);
	if (req->base.autoneg == AUTONEG_DISABLE) {
		if (xeth.ops.validate_speed)
			err = xeth.ops.validate_speed(nd, req->base.speed);
		switch (req->base.duplex) {
		case DUPLEX_HALF:
		case DUPLEX_FULL:
		case DUPLEX_UNKNOWN:
			break;
		default:
			xeth_pr_nd(nd, "invalid duplex: %d: ",
				   req->base.duplex);
			err = -EINVAL;
		}
		if (!err) {
			settings->base.autoneg = req->base.autoneg;
			settings->base.speed = req->base.speed;
			settings->base.duplex = req->base.duplex;
		}
	} else {
		int i;
		const int n = sizeof(req->link_modes.advertising);
		for (i = 0; i < req->base.link_mode_masks_nwords; i++) {
			if (req->link_modes.advertising[i] &
			    ~settings->link_modes.supported[i]) {
				xeth_pr_nd(nd, "can't advertise: %*pbl: ", n,
					   req->link_modes.advertising);
				err = -EINVAL;
				break;
			}
		}
		if (!err)
			memcpy(settings->link_modes.advertising,
			       req->link_modes.advertising,
			       sizeof(settings->link_modes.advertising));
	}
	if (!err)
		err = xeth_sb_send_ethtool_settings(nd);
	mutex_unlock(&priv->ethtool.mutex);
	return err;
}

int xeth_ethtool_init(void)
{
	xeth.ops.ethtool.get_drvinfo	= xeth_ethtool_get_drvinfo;
	xeth.ops.ethtool.get_link	= ethtool_op_get_link;
	xeth.ops.ethtool.get_sset_count	= xeth_ethtool_get_sset_count;
	xeth.ops.ethtool.get_strings	= xeth_ethtool_get_strings;
	xeth.ops.ethtool.get_ethtool_stats =
		xeth_ethtool_get_ethtool_stats;
	xeth.ops.ethtool.get_priv_flags	= xeth_ethtool_get_priv_flags;
	xeth.ops.ethtool.set_priv_flags	= xeth_ethtool_set_priv_flags;
	xeth.ops.ethtool.get_link_ksettings =
		xeth_ethtool_get_link_ksettings;
	xeth.ops.ethtool.set_link_ksettings =
		xeth_ethtool_set_link_ksettings;
	return 0;
}

void xeth_ethtool_exit(void)
{
	xeth.ops.ethtool.get_drvinfo	= NULL;
	xeth.ops.ethtool.get_link	= NULL;
	xeth.ops.ethtool.get_sset_count	= NULL;
	xeth.ops.ethtool.get_strings	= NULL;
	xeth.ops.ethtool.get_ethtool_stats = NULL;
	xeth.ops.ethtool.get_priv_flags	= NULL;
	xeth.ops.ethtool.set_priv_flags	= NULL;
	xeth.ops.ethtool.get_link_ksettings	= NULL;
	xeth.ops.ethtool.set_link_ksettings	= NULL;
}
