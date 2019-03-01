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

#include <linux/if_vlan.h>
#include <uapi/linux/xeth.h>

static int xeth_ndo_open(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_iflink(priv->iflinki);
	unsigned long iflink_flags;

	if (!iflink)
		return -ENODEV;
	iflink_flags = dev_get_flags(iflink);
	if (!(iflink_flags & IFF_UP)) {
		 int err = dev_change_flags(iflink, iflink_flags | IFF_UP);
		 if (err < 0)
			 return err;
	}
	if (!iflink->promiscuity) {
		int err = dev_set_promiscuity(iflink, 1);
		if (err)
			return err;
	}
	xeth_sb_send_ifinfo(nd, nd->flags | IFF_UP, XETH_IFINFO_REASON_UP);
	return 0;
}

static int xeth_ndo_stop(struct net_device *nd)
{
	netif_carrier_off(nd);
	xeth_sb_send_ifinfo(nd, nd->flags & ~IFF_UP, XETH_IFINFO_REASON_DOWN);
	return 0;
}

static int xeth_ndo_change_carrier(struct net_device *nd, bool new_carrier)
{
	if (new_carrier)
		netif_carrier_on(nd);
	else
		netif_carrier_off(nd);
	return 0;
}

static void xeth_ndo_get_stats64(struct net_device *nd,
					 struct rtnl_link_stats64 *dst)
{
	struct xeth_priv *priv = netdev_priv(nd);
	xeth_priv_lock_link(priv);
	memcpy(dst, &priv->link.stats, sizeof(struct rtnl_link_stats64));
	xeth_priv_unlock_link(priv);
}

static int xeth_ndo_get_iflink(const struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_iflink(priv->iflinki);
	return iflink ? iflink->ifindex : 0;
}

static int xeth_ndo_vlan_rx_add_vid(struct net_device *nd,
				    __be16 proto, u16 id)
{
	return xeth_priv_add_vid(netdev_priv(nd), proto, id);
}

static int xeth_ndo_vlan_rx_del_vid(struct net_device *nd,
				    __be16 proto, u16 id)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct xeth_priv_vid *vid;

	rcu_read_lock();
	vid = xeth_priv_vid_rcu(priv, proto, id);
	rcu_read_unlock();
	if (vid)
		xeth_priv_del_vid(priv, vid);
	return 0;
}

struct net_device_ops xeth_ndo_ops = {
	.ndo_open		= xeth_ndo_open,
	.ndo_stop		= xeth_ndo_stop,
	.ndo_change_carrier	= xeth_ndo_change_carrier,
	.ndo_get_stats64	= xeth_ndo_get_stats64,
	.ndo_get_iflink		= xeth_ndo_get_iflink,
	.ndo_vlan_rx_add_vid	= xeth_ndo_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= xeth_ndo_vlan_rx_del_vid,
};

int xeth_ndo_init(void)
{
	xeth_ndo_ops.ndo_start_xmit = xeth.encap.tx;
	return 0;
}

void xeth_ndo_exit(void) {}
