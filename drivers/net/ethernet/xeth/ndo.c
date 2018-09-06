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
	mutex_lock(&priv->link.mutex);
	memcpy(dst, &priv->link.stats, sizeof(struct rtnl_link_stats64));
	mutex_unlock(&priv->link.mutex);
}

static int xeth_ndo_get_iflink(const struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_iflink(priv->iflinki);
	return iflink ? iflink->ifindex : 0;
}

static int xeth_ndo_vlan_rx_add_vid(struct net_device *nd, __be16 proto, u16 id)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct xeth_vid *vid = xeth_vid_pop(&xeth.free_vids);
	if (!vid) {
		vid = kzalloc(sizeof(struct xeth_vid), GFP_KERNEL);
		if (!vid)
			return -ENOMEM;
	}
	vid->proto = proto;
	vid->id = id;
	list_add_tail_rcu(&vid->list, &priv->vids);
	return 0;
}

static int xeth_ndo_vlan_rx_del_vid(struct net_device *nd, __be16 proto, u16 id)
{
	struct xeth_vid *vid;
	struct xeth_priv *priv = netdev_priv(nd);
	list_for_each_entry_rcu(vid, &priv->vids, list) {
		if (vid->proto == proto && vid->id == id) {
			list_del_rcu(&vid->list);
			list_add_tail_rcu(&vid->list, &xeth.free_vids);
			break;
		}
	}
	return 0;
}

void xeth_ndo_send_vids(struct net_device *nd)
{
	struct xeth_vid *vid;
	struct xeth_priv *priv = netdev_priv(nd);
	list_for_each_entry(vid, &priv->vids, list) {
		struct net_device *vnd = __vlan_find_dev_deep_rcu(nd,
								  vid->proto,
								  vid->id);
		if (vnd)
			xeth_sb_send_ifinfo(vnd, 0, XETH_IFINFO_REASON_DUMP);
		else
			xeth_pr_nd(nd, "can't find vlan (%u, %u)",
				   be16_to_cpu(vid->proto), vid->id);
	}
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
