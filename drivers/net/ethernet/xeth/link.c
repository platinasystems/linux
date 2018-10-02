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
	.setup		   = NULL,
	.newlink	   = NULL,
	.dellink	   = NULL,
	.validate	   = xeth_link_validate,
	.get_link_net	   = xeth_link_get_net,
	.get_num_rx_queues = xeth_link_get_num_rx_queues,
	.get_num_tx_queues = xeth_link_get_num_tx_queues,
};

int xeth_link_init(void)
{
	xeth_link_ops.priv_size = xeth.priv_size;
	return rtnl_link_register(&xeth_link_ops);
}

void xeth_link_exit(void)
{
	if (xeth_link_ops.list.next && xeth_link_ops.list.prev) {
		rtnl_lock();
		list_del(&xeth_link_ops.list);
		rtnl_unlock();
	}
}
