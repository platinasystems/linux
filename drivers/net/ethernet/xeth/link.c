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

void xeth_link_setup(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth_ndo_ops;
	nd->ethtool_ops = &xeth_ethtool_ops;
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
	return xeth.rxqs;
}

static unsigned int xeth_link_get_num_tx_queues(void)
{
	return xeth.txqs;
}


struct rtnl_link_ops xeth_link_ops = {
	.kind		   = XETH_KIND,
	.setup		   = xeth_link_setup,
	.newlink	   = NULL,
	.dellink	   = NULL,
	.validate	   = xeth_link_validate,
	.get_link_net	   = xeth_link_get_net,
	.get_num_rx_queues = xeth_link_get_num_rx_queues,
	.get_num_tx_queues = xeth_link_get_num_tx_queues,
};

int xeth_link_init(void)
{
	int err;
	xeth_link_ops.priv_size = xeth.priv_size;
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
