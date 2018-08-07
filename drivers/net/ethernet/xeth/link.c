/* XETH driver rtnl_link_ops
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

static void xeth_destructor(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	if (priv->ethtool.stats) {
		kfree(priv->ethtool.stats);
		priv->ethtool.stats = NULL;
	}
}

static void xeth_link_setup(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth.ops.ndo;
	nd->needs_free_netdev = true;
	nd->priv_destructor = xeth_destructor;
	nd->priv_flags |= IFF_NO_QUEUE;
	/* FIXME nd->priv_flags |= IFF_UNICAST_FLT; */
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->mtu = ETH_DATA_LEN;
	nd->min_mtu = ETH_MIN_MTU;
	nd->max_mtu = ETH_MAX_MTU;
	/* FIXME netif_keep_dst(nd); */
	eth_zero_addr(nd->broadcast);
}

static int xeth_link_new(struct net *src_net, struct net_device *nd,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	int err;
	char ifname[IFNAMSIZ+1];
	struct net_device *iflink;
	struct xeth_priv *priv = netdev_priv(nd);

	priv->ethtool.stats = kcalloc(xeth.n.ethtool.stats, sizeof(u64),
				      GFP_KERNEL);
	if (!priv->ethtool.stats)
		return -ENOMEM;
	if (xeth_pr_true_expr(tb[IFLA_IFNAME] == NULL, "missing name"))
		return -EINVAL;
	if (xeth_pr_true_expr(tb[IFLA_ADDRESS] != NULL, "can't change lladdr"))
		return -EINVAL;
	nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);
	ifname[IFNAMSIZ] = '\0';
	err = xeth.ops.parse(ifname, priv);
	if (err)
		return err;
	if (priv->ndi > 0)
		xeth.ndi_by_id[priv->id] = priv->ndi;
	err = xeth_pr_nd_err(nd, xeth.ops.assert_iflinks());
	if (err)
		return err;
	err = xeth_pr_nd_err(nd, xeth.ops.set_lladdr(nd));
	if (err)
		return err;
	iflink = xeth_iflink(priv->iflinki);
	if (is_zero_ether_addr(nd->broadcast))
		memcpy(nd->broadcast, iflink->broadcast, nd->addr_len);
	nd->mtu = iflink->mtu;
	nd->max_mtu = iflink->max_mtu - xeth.n.encap;
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
	if (err)
		return err;
	xeth_set_nd(priv->ndi, nd);
	if (xeth.ops.ethtool.get_link)
		nd->ethtool_ops = &xeth.ops.ethtool;
	if (xeth.ops.init_ethtool_settings)
		xeth.ops.init_ethtool_settings(nd);
	priv->nd = nd;
	list_add_tail_rcu(&priv->list, &xeth.list);
	xeth_priv_reset_counters(priv);
	INIT_LIST_HEAD_RCU(&priv->vids);
	return xeth_sysfs_add(priv);
}

static void xeth_link_del(struct net_device *nd, struct list_head *head)
{
	struct xeth_priv *priv = netdev_priv(nd);

	xeth_ndo_free_vids(nd);
	xeth_sb_send_ifdel(nd);
	xeth_sysfs_del(priv);
	priv->nd = NULL;
	list_del_rcu(&priv->list);
	xeth_reset_nd(priv->ndi);
	unregister_netdevice_queue(nd, head);
}

static int xeth_link_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	/* FIXME compare with links expected lladdr */
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static struct net *xeth_link_get_net(const struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_iflink(priv->iflinki);
	return iflink ? dev_net(iflink) : &init_net;
}

static unsigned int xeth_link_get_num_rx_queues(void)
{
	/* FIXME use iflinks num_rx_queues ? */
	return 2;
}

static unsigned int xeth_link_get_num_tx_queues(void)
{
	/* FIXME use iflink's num_tx_queues ? */
	return 2;
}

static void xeth_link_reset(void)
{
	xeth.ops.rtnl.priv_size         = 0;
	xeth.ops.rtnl.kind              = NULL;
	xeth.ops.rtnl.setup             = NULL;
	xeth.ops.rtnl.newlink           = NULL;
	xeth.ops.rtnl.dellink           = NULL;
	xeth.ops.rtnl.validate          = NULL;
	xeth.ops.rtnl.get_link_net      = NULL;
	xeth.ops.rtnl.get_num_rx_queues = NULL;
	xeth.ops.rtnl.get_num_tx_queues = NULL;	
}

int xeth_link_init(void)
{
	int err;
	xeth.ops.rtnl.kind		= "xeth";
	xeth.ops.rtnl.priv_size         = sizeof(struct xeth_priv);
	xeth.ops.rtnl.setup             = xeth_link_setup;
	xeth.ops.rtnl.newlink           = xeth_link_new;
	xeth.ops.rtnl.dellink           = xeth_link_del;
	xeth.ops.rtnl.validate          = xeth_link_validate;
	xeth.ops.rtnl.get_link_net      = xeth_link_get_net;
	xeth.ops.rtnl.get_num_rx_queues = xeth_link_get_num_rx_queues;
	xeth.ops.rtnl.get_num_tx_queues = xeth_link_get_num_tx_queues;	
	err = xeth_pr_err(rtnl_link_register(&xeth.ops.rtnl));
	if (err)
		xeth_link_reset();
	return err;
}

void xeth_link_exit(void)
{
	rtnl_link_unregister(&xeth.ops.rtnl);
	xeth_link_reset();
} 
