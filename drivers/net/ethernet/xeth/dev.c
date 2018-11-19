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

#include <uapi/linux/xeth.h>

static int xeth_dev_n = 0;

static void xeth_dev_setup(struct net_device *nd)
{
	nd->netdev_ops = &xeth_ndo_ops;
	nd->ethtool_ops = &xeth_ethtool_ops;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	ether_setup(nd);
	nd->priv_flags |= IFF_NO_QUEUE;
	/* FIXME nd->priv_flags |= IFF_UNICAST_FLT; */
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->mtu = ETH_DATA_LEN;
	nd->min_mtu = ETH_MIN_MTU;
	nd->max_mtu = ETH_MAX_MTU;
	/* FIXME netif_keep_dst(nd); */
	eth_zero_addr(nd->broadcast);
}

static int xeth_dev_new(const char *ifname, int port, int sub)
{
	int err;
	struct net_device *nd, *iflink;
	struct xeth_priv *priv;

	nd = alloc_netdev_mqs(xeth.priv_size, ifname, NET_NAME_USER,
			      xeth_dev_setup, xeth.txqs, xeth.rxqs);
	if (IS_ERR(nd))
		return PTR_ERR(nd);
	priv = netdev_priv(nd);
	priv->devtype = XETH_DEVTYPE_XETH_PORT;
	priv->porti = port;
	priv->subporti = sub;
	err = xeth.encap.new_dev(nd);
	if (err < 0)
		return err;
	priv->iflinki = xeth_iflink_index(priv->id);
	INIT_LIST_HEAD_RCU(&priv->vids);
	priv->nd = nd;
	xeth_priv_reset_counters(priv);
	if (xeth.init_ethtool_settings)
		xeth.init_ethtool_settings(priv);
	u64_to_ether_addr(xeth.ea.base + xeth_dev_n++, nd->dev_addr);
	nd->addr_assign_type = xeth.ea.assign_type;
	iflink = xeth_iflink(priv->iflinki);
	if (is_zero_ether_addr(nd->broadcast))
		memcpy(nd->broadcast, iflink->broadcast, nd->addr_len);
	nd->max_mtu = iflink->mtu - xeth.encap.size;
	if (nd->mtu > nd->max_mtu)
		nd->mtu = nd->max_mtu;
	nd->flags  = iflink->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI |
				       IFF_MASTER | IFF_SLAVE);
	nd->state  = (iflink->state & ((1<<__LINK_STATE_NOCARRIER) |
				       (1<<__LINK_STATE_DORMANT))) |
		(1<<__LINK_STATE_PRESENT);

	nd->hw_features = NETIF_F_HW_CSUM | NETIF_F_SG | NETIF_F_FRAGLIST |
		NETIF_F_GSO_SOFTWARE | NETIF_F_HIGHDMA | NETIF_F_SCTP_CRC |
		NETIF_F_ALL_FCOE;
	nd->features |= nd->hw_features | NETIF_F_LLTX |
		NETIF_F_HW_VLAN_CTAG_FILTER;
	nd->vlan_features = iflink->vlan_features & ~NETIF_F_ALL_FCOE;

	/* ipv6 shared card related stuff */
	/* FIXME dev->dev_id = real_dev->dev_id; */
	/* FIXME SET_NETDEV_DEVTYPE(nd, &vlan_type); */

	err = xeth_pr_nd_err(nd, register_netdevice(nd));
	if (!err) {
		hash_add_rcu(xeth.ht, &priv->node, nd->ifindex);
		err = xeth_sysfs_priv_add(priv);
	}
	return err;
}

int xeth_dev_init(void)
{
	int port, err = 0;
	rtnl_lock();
	for (port = 0; !err && port < xeth.ports; port++) {
		char ifname[IFNAMSIZ];
		int provision = xeth.provision[port];
		if (provision > 1) {
			int sub;
			for (sub = 0; !err && sub < provision; sub++) {
				sprintf(ifname, "xeth%d-%d",
					port+xeth.base, sub+xeth.base);
				err = xeth_dev_new(ifname, port, sub);
			}
		} else {
			sprintf(ifname, "xeth%d", port+xeth.base);
			err = xeth_dev_new(ifname, port, -1);
		}
	}
	rtnl_unlock();
	return err;
}

void xeth_dev_exit(void)
{
	int i;
	struct xeth_priv *priv;

	rtnl_lock();
	hash_for_each_rcu(xeth.ht, i, priv, node)
		if (priv->nd->reg_state == NETREG_REGISTERED) {
			struct net_device *nd = priv->nd;
			struct xeth_vid *vid;
			while (vid = xeth_pop_vid(&priv->vids), vid != NULL)
				list_add_tail_rcu(&vid->list, &xeth.free.vids);
			xeth_sysfs_priv_del(priv);
			priv->nd = NULL;
			hash_del_rcu(&priv->node);
			unregister_netdevice_queue(nd, NULL);
		} else {
			priv->nd = NULL;
			hash_del_rcu(&priv->node);
		}
	rtnl_unlock();
}
