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
	if (priv->ethtool_stats) {
		kfree(priv->ethtool_stats);
		priv->ethtool_stats = NULL;
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
	nd->mtu = ETH_MAX_MTU - xeth.n.encap;
	nd->min_mtu = 0;
	nd->max_mtu = ETH_MAX_MTU - xeth.n.encap;
	/* FIXME netif_keep_dst(nd); */
	eth_zero_addr(nd->broadcast);
}

static int xeth_link_new(struct net *src_net, struct net_device *nd,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink;
	char ifname[IFNAMSIZ+1];
	int err;

	priv->ethtool_stats = kcalloc(xeth.n.ethtool_stats, sizeof(u64),
				      GFP_KERNEL);
	if (!priv->ethtool_stats)
		return -ENOMEM;
	if (tb[IFLA_IFNAME] == NULL)
		return xeth_pr_nd_val(nd, "%d, missing name", -EINVAL);
	if (tb[IFLA_ADDRESS] != NULL)
		return xeth_pr_nd_val(nd, "%d, can't change lladdr", -EINVAL);
	nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);
	ifname[IFNAMSIZ] = '\0';
	err = xeth_pr_nd_true_val(nd, "%d",
				  xeth.ops.parse_name(ifname,
						      &priv->id,
						      &priv->ndi,
						      &priv->iflinki));
	if (err)
		return err;
	xeth.ndi_by_id[priv->id] = priv->ndi;
	err = xeth_pr_nd_true_val(nd, "%d", xeth.ops.assert_iflinks());
	if (err)
		return err;
	err = xeth_pr_nd_true_val(nd, "%d", xeth.ops.set_lladdr(nd));
	if (err)
		return err;
	iflink = xeth_priv_iflink(priv);
	if (is_zero_ether_addr(nd->broadcast))
		memcpy(nd->broadcast, iflink->broadcast, nd->addr_len);
	nd->mtu = iflink->mtu - xeth.n.encap;
	nd->max_mtu = iflink->max_mtu - xeth.n.encap;
	nd->flags  = iflink->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI |
				       IFF_MASTER | IFF_SLAVE);
	nd->state  = (iflink->state & ((1<<__LINK_STATE_NOCARRIER) |
				       (1<<__LINK_STATE_DORMANT))) |
		(1<<__LINK_STATE_PRESENT);

	nd->hw_features = NETIF_F_HW_CSUM | NETIF_F_SG | NETIF_F_FRAGLIST |
		NETIF_F_GSO_SOFTWARE | NETIF_F_HIGHDMA | NETIF_F_SCTP_CRC |
		NETIF_F_ALL_FCOE;
	nd->features |= nd->hw_features | NETIF_F_LLTX;

	nd->vlan_features = iflink->vlan_features & ~NETIF_F_ALL_FCOE;
	/* ipv6 shared card related stuff */
	/* FIXME dev->dev_id = real_dev->dev_id; */
	/* FIXME SET_NETDEV_DEVTYPE(nd, &vlan_type); */

	err = xeth_pr_nd_true_val(nd, "%d", register_netdevice(nd));
	if (err)
		return err;
	xeth_priv_set_nd(priv, nd);
	if (xeth.ops.ethtool.get_link)
		nd->ethtool_ops = &xeth.ops.ethtool;
	return 0;
}

static void xeth_link_del(struct net_device *nd, struct list_head *head)
{
	struct xeth_priv *priv = netdev_priv(nd);

	xeth_reset_nds(priv->ndi);
	xeth_pr_nd_void(nd, unregister_netdevice_queue(nd, head));
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
	struct net_device *iflink = xeth_priv_iflink(priv);
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

	xeth.ops.rtnl.priv_size         = sizeof(struct xeth_priv);
	xeth.ops.rtnl.setup             = xeth_link_setup;
	xeth.ops.rtnl.newlink           = xeth_link_new;
	xeth.ops.rtnl.dellink           = xeth_link_del;
	xeth.ops.rtnl.validate          = xeth_link_validate;
	xeth.ops.rtnl.get_link_net      = xeth_link_get_net;
	xeth.ops.rtnl.get_num_rx_queues = xeth_link_get_num_rx_queues;
	xeth.ops.rtnl.get_num_tx_queues = xeth_link_get_num_tx_queues;	
	err = xeth_pr_true_val("%d", rtnl_link_register(&xeth.ops.rtnl));
	if (err)
		xeth_link_reset();
	return err;
}

void xeth_link_exit(void)
{
	rtnl_link_unregister(&xeth.ops.rtnl);
	xeth_link_reset();
} 
