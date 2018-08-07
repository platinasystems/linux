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

struct	xeth_ndo_vid_entry {
	struct	list_head __rcu
		list;
	u16	vid;
};

static struct list_head __rcu xeth_ndo_vids;

static struct xeth_ndo_vid_entry *xeth_ndo_alloc_vid(void)
{
	struct xeth_ndo_vid_entry *entry =
		list_first_or_null_rcu(&xeth_ndo_vids,
				       struct xeth_ndo_vid_entry,
				       list);
	if (entry) {
		list_del_rcu(&entry->list);
	} else {
		const size_t n = sizeof(struct xeth_ndo_vid_entry);
		entry = kzalloc(n, GFP_KERNEL);
	}
	return entry;
}

static void xeth_ndo_free_vid(struct xeth_ndo_vid_entry *entry)
{
	list_add_tail_rcu(&entry->list, &xeth_ndo_vids);
}

void xeth_ndo_free_vids(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);

	while (true) {
		struct xeth_ndo_vid_entry *entry =
			list_first_or_null_rcu(&priv->vids,
					       struct xeth_ndo_vid_entry,
					       list);
		if (!entry)
			break;
		list_del_rcu(&entry->list);
		xeth_ndo_free_vid(entry);
	}
}

void xeth_ndo_send_vids(struct net_device *nd)
{
	struct xeth_ndo_vid_entry *entry;
	struct xeth_priv *priv = netdev_priv(nd);
	list_for_each_entry_rcu(entry, &priv->vids, list)
		xeth_sb_send_if_add_vid(nd, entry->vid);
}

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
	xeth_pr_err(xeth_sb_send_ifinfo(nd, nd->flags | IFF_UP));
	return 0;
}

static int xeth_ndo_stop(struct net_device *nd)
{
	netif_carrier_off(nd);
	xeth_pr_err(xeth_sb_send_ifinfo(nd, nd->flags & ~IFF_UP));
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

static int xeth_ndo_vlan_rx_add_vid(struct net_device *nd,
				    __be16 proto, u16 vid)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct xeth_ndo_vid_entry *entry = xeth_ndo_alloc_vid();
	if (!entry)
		return -ENOMEM;
	entry->vid = vid;
	list_add_tail_rcu(&entry->list, &priv->vids);
	return xeth_sb_send_if_add_vid(nd, vid);
}

static int xeth_ndo_vlan_rx_kill_vid(struct net_device *nd,
				     __be16 proto, u16 vid)
{
	struct xeth_ndo_vid_entry *entry;
	struct xeth_priv *priv = netdev_priv(nd);
	list_for_each_entry_rcu(entry, &priv->vids, list) {
		if (entry->vid == vid) {
			list_del_rcu(&entry->list);
			xeth_ndo_free_vid(entry);
			break;
		}
	}
	return xeth_sb_send_if_del_vid(nd, vid);
}

int xeth_ndo_init(void)
{
	xeth.ops.ndo.ndo_open           = xeth_ndo_open;
	xeth.ops.ndo.ndo_stop           = xeth_ndo_stop;
	xeth.ops.ndo.ndo_change_carrier = xeth_ndo_change_carrier;
	xeth.ops.ndo.ndo_get_stats64    = xeth_ndo_get_stats64;
	xeth.ops.ndo.ndo_get_iflink     = xeth_ndo_get_iflink;
	xeth.ops.ndo.ndo_vlan_rx_add_vid	= xeth_ndo_vlan_rx_add_vid;
	xeth.ops.ndo.ndo_vlan_rx_kill_vid	= xeth_ndo_vlan_rx_kill_vid;
	INIT_LIST_HEAD_RCU(&xeth_ndo_vids);
	return 0;
}

void xeth_ndo_exit(void)
{
	while (true) {
		struct xeth_ndo_vid_entry *entry =
			list_first_or_null_rcu(&xeth_ndo_vids,
					       struct xeth_ndo_vid_entry,
					       list);
		if (!entry)
			break;
		list_del_rcu(&entry->list);
		kfree(entry);
	}
	xeth.ops.ndo.ndo_open           = NULL;
	xeth.ops.ndo.ndo_stop           = NULL;
	xeth.ops.ndo.ndo_change_carrier = NULL;
	xeth.ops.ndo.ndo_get_stats64    = NULL;
	xeth.ops.ndo.ndo_get_iflink     = NULL;
	xeth.ops.ndo.ndo_vlan_rx_add_vid	= NULL;
	xeth.ops.ndo.ndo_vlan_rx_kill_vid	= NULL;
}
