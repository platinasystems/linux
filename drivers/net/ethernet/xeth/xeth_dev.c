/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
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

	nd = alloc_netdev_mqs(xeth_priv_size, ifname, NET_NAME_USER,
			      xeth_dev_setup, xeth_config->txqs,
			      xeth_config->rxqs);
	if (IS_ERR(nd))
		return PTR_ERR(nd);
	priv = netdev_priv(nd);
	priv->devtype = XETH_DEVTYPE_XETH_PORT;
	priv->porti = port;
	priv->subporti = sub;
	err = xeth_vlan_new_dev(nd);
	if (err < 0)
		return err;
	priv->iflinki = xeth_iflink_index(priv->id);
	xeth_priv_init_vids(priv);
	xeth_priv_init_link_lock(priv);
	xeth_priv_init_ethtool_lock(priv);
	priv->nd = nd;
	xeth_priv_reset_counters(priv);
	if (xeth_config->ethtool.init_settings)
		xeth_config->ethtool.init_settings(&priv->ethtool.settings);
	u64_to_ether_addr(xeth_config->ea.base + xeth_dev_n++, nd->dev_addr);
	nd->addr_assign_type = xeth_config->ea.assign_type;
	iflink = xeth_iflink(priv->iflinki);
	if (is_zero_ether_addr(nd->broadcast))
		memcpy(nd->broadcast, iflink->broadcast, nd->addr_len);
	nd->max_mtu = iflink->mtu - xeth_encap_size;
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

	err = pr_nd_expr_err(nd, register_netdevice(nd));
	if (!err) {
		xeth_add_priv(priv);
		err = xeth_sysfs_priv_add(priv);
	}
	return err;
}

int xeth_dev_start(void)
{
	int port, err = 0;
	rtnl_lock();
	for (port = 0; !err && port < xeth_config->ports; port++) {
		char ifname[IFNAMSIZ];
		int provision = xeth_config->provision[port];
		if (provision > 1) {
			int sub;
			for (sub = 0; !err && sub < provision; sub++) {
				sprintf(ifname, "xeth%d-%d",
					port+xeth_config->base,
					sub+xeth_config->base);
				err = xeth_dev_new(ifname, port, sub);
			}
		} else {
			sprintf(ifname, "xeth%d", port+xeth_config->base);
			err = xeth_dev_new(ifname, port, -1);
		}
	}
	rtnl_unlock();
	return err;
}

void xeth_dev_stop(void)
{
	int i;
	struct xeth_priv *priv;
	struct xeth_priv_vid *vid;
	LIST_HEAD(list);

	xeth_vlan_stop();
	rtnl_lock();
	rcu_read_lock();
	xeth_for_each_priv_rcu(priv, i) {
		xeth_priv_for_each_vid_rcu(priv, vid)
			xeth_priv_del_vid(priv, vid);
		xeth_del_priv(priv);
		if (priv->nd->reg_state == NETREG_REGISTERED) {
			xeth_sysfs_priv_del(priv);
			unregister_netdevice_queue(priv->nd, &list);
		}
	}
	rcu_read_unlock();
	rcu_barrier();
	unregister_netdevice_many(&list);
	rtnl_unlock();
}
