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

#include <uapi/linux/xeth.h>

static void xeth_link_setup(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth_ndo_ops;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	nd->priv_flags |= IFF_NO_QUEUE;
	/* FIXME nd->priv_flags |= IFF_UNICAST_FLT; */
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->mtu = ETH_DATA_LEN;
	nd->min_mtu = ETH_MIN_MTU;
	nd->max_mtu = ETH_MAX_MTU;
	/* FIXME netif_keep_dst(nd); */
	eth_zero_addr(nd->broadcast);
	nd->ethtool_ops = &xeth_ethtool_ops;
}

static int xeth_link_new(struct net *src_net, struct net_device *nd,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	int err;
	struct xeth_priv *priv = netdev_priv(nd);

	if (xeth_pr_true_expr(tb[IFLA_IFNAME] == NULL, "missing name"))
		return -EINVAL;
	if (xeth_pr_true_expr(tb[IFLA_ADDRESS] != NULL, "can't change lladdr"))
		return -EINVAL;
	nla_strlcpy(nd->name, tb[IFLA_IFNAME], IFNAMSIZ);
	err = xeth_parse_name(nd->name, &priv->ref);
	if (err)
		return err;
	if (xeth_pr_true_expr(priv->ref.devtype == XETH_DEVTYPE_XETH_PORT,
			      "can't make post provision port"))
		return -EPERM;
	err = xeth_link_register(nd);
	if (!err)
		xeth_sb_send_ifinfo(nd, 0, XETH_IFINFO_REASON_NEW);
	return err;
}

int xeth_link_register(struct net_device *nd)
{
	int err;
	struct net_device *iflink;
	struct xeth_priv *priv = netdev_priv(nd);

	INIT_LIST_HEAD_RCU(&priv->vids);
	priv->nd = nd;
	xeth_priv_reset_counters(priv);
	if (xeth.ops.dev.init_ethtool_settings)
		xeth.ops.dev.init_ethtool_settings(priv);
	if (priv->ref.ndi > 0)
		xeth.dev.ndi_by_id[priv->ref.id] = priv->ref.ndi;
	if (xeth.ops.dev.set_lladdr) {
		err = xeth_pr_nd_err(nd, xeth.ops.dev.set_lladdr(nd));
		if (err)
			return err;
	} else {
		random_ether_addr(nd->dev_addr);
	}
	iflink = xeth_iflink_nd(priv->ref.iflinki);
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

	xeth_set_nd(priv->ref.ndi, nd);
	err = xeth_pr_nd_err(nd, register_netdevice(nd));
	if (!err) {
		list_add_tail_rcu(&priv->list, &xeth.privs);
		err = xeth_sysfs_add(priv);
	}
	return err;
}

static void xeth_link_del(struct net_device *nd, struct list_head *head)
{
	struct xeth_vid *vid;
	struct xeth_priv *priv = netdev_priv(nd);

	while (vid = xeth_vid_pop(&priv->vids), vid != NULL)
		list_add_tail_rcu(&vid->list, &xeth.vids);

	xeth_sb_send_ifinfo(nd, 0, XETH_IFINFO_REASON_DEL);
	xeth_sysfs_del(priv);
	priv->nd = NULL;
	list_del_rcu(&priv->list);
	xeth_reset_nd(priv->ref.ndi);
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
	struct net_device *iflink = xeth_iflink_nd(priv->ref.iflinki);
	return iflink ? dev_net(iflink) : &init_net;
}

static unsigned int xeth_link_get_num_rx_queues(void)
{
	return xeth.n.rxqs;
}

static unsigned int xeth_link_get_num_tx_queues(void)
{
	return xeth.n.txqs;
}


struct rtnl_link_ops xeth_link_ops = {
	.kind		   = "xeth",
	.setup		   = xeth_link_setup,
	.newlink	   = xeth_link_new,
	.dellink	   = xeth_link_del,
	.validate	   = xeth_link_validate,
	.get_link_net	   = xeth_link_get_net,
	.get_num_rx_queues = xeth_link_get_num_rx_queues,
	.get_num_tx_queues = xeth_link_get_num_tx_queues,
};

int xeth_link_init(void)
{
	int err;
	xeth_link_ops.priv_size = xeth.n.priv_size;
	err = xeth_pr_err(rtnl_link_register(&xeth_link_ops));
	if (err)
		xeth_link_ops.priv_size = 0;
	return err;
}

void xeth_link_exit(void)
{
	if (xeth_link_ops.priv_size > 0)
		rtnl_link_unregister(&xeth_link_ops);
} 
