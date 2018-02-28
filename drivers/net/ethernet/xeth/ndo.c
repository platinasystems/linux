/* XETH netdev ops
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

#include <net/rtnetlink.h>

#include "xeth.h"
#include "debug.h"

static int xeth_ndo_open(struct net_device *nd)
{
	if (1)	/* FIXME (iflink->flags & IFF_UP) */
		netif_carrier_on(nd);
	return 0;
}

static int xeth_ndo_stop(struct net_device *nd)
{
	netif_carrier_off(nd);
	return 0;
}

static void xeth_ndo_get_stats64(struct net_device *nd,
					 struct rtnl_link_stats64 *dst)
{
	struct xeth_priv *priv = netdev_priv(nd);
	mutex_lock(&priv->mutex.stats);
	memcpy(dst, &priv->stats, sizeof(struct rtnl_link_stats64));
	mutex_unlock(&priv->mutex.stats);
}

static int xeth_ndo_get_iflink(const struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_priv_iflink(priv);
	return xeth_debug_netdev_false_val(nd, "%d",
					   iflink ? iflink->ifindex : 0);
}

void xeth_ndo_init(void)
{
	xeth.ops.ndo.ndo_open        = xeth_ndo_open;
	xeth.ops.ndo.ndo_stop        = xeth_ndo_stop;
	xeth.ops.ndo.ndo_get_stats64 = xeth_ndo_get_stats64;
	xeth.ops.ndo.ndo_get_iflink  = xeth_ndo_get_iflink;
}

void xeth_ndo_exit(void)
{
	xeth.ops.ndo.ndo_open        = NULL;
	xeth.ops.ndo.ndo_stop        = NULL;
	xeth.ops.ndo.ndo_get_stats64 = NULL;
	xeth.ops.ndo.ndo_get_iflink  = NULL;
}
