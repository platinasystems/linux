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
	drvinfo->n_priv_flags = 0;
}

static int xeth_ethtool_get_sset_count(struct net_device *nd, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return xeth.n.ethtool_stats;
	case ETH_SS_PRIV_FLAGS:
		return 0;
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
		for (i = 0; i < xeth.n.ethtool_stats; i++) {
			strlcpy(p, xeth.ethtool_stats[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		break;
	}
}

static void xeth_ethtool_get_ethtool_stats(struct net_device *nd,
					   struct ethtool_stats *stats,
					   u64 *data)
{
	struct xeth_priv *priv = netdev_priv(nd);
	mutex_lock(&priv->ethtool_mutex);
	memcpy(data, priv->ethtool_stats, xeth.n.ethtool_stats*sizeof(u64));
	mutex_unlock(&priv->ethtool_mutex);
}

int xeth_ethtool_init(void)
{
	xeth.ops.ethtool.get_drvinfo	= xeth_ethtool_get_drvinfo;
	xeth.ops.ethtool.get_link	= ethtool_op_get_link;
	xeth.ops.ethtool.get_sset_count	= xeth_ethtool_get_sset_count;
	xeth.ops.ethtool.get_strings	= xeth_ethtool_get_strings;
	xeth.ops.ethtool.get_ethtool_stats =
		xeth_ethtool_get_ethtool_stats;
	return 0;
}

void xeth_ethtool_exit(void)
{
	xeth.ops.ethtool.get_drvinfo	= NULL;
	xeth.ops.ethtool.get_link	= NULL;
	xeth.ops.ethtool.get_sset_count	= NULL;
	xeth.ops.ethtool.get_strings	= NULL;
	xeth.ops.ethtool.get_ethtool_stats = NULL;
}
