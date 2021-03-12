/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_vlan.h"
#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_port.h"
#include "xeth_lag.h"
#include "xeth_sbtx.h"
#include "xeth_version.h"
#include "xeth_debug.h"
#include <linux/netdevice.h>

static const char xeth_vlan_drvname[] = "xeth-vlan";

struct xeth_vlan_priv {
	struct xeth_proxy proxy;
	struct net_device *link;
};

bool xeth_vlan_has_link(const struct net_device *nd,
			const struct net_device *link)
{
	struct xeth_vlan_priv *priv = netdev_priv(nd);
	return priv->link == link;
}

static int xeth_vlan_get_iflink(const struct net_device *nd)
{
	struct xeth_vlan_priv *priv = netdev_priv(nd);
	return priv->link ? priv->link->ifindex: nd->ifindex;
}

static int xeth_vlan_open(struct net_device *nd)
{
	struct xeth_vlan_priv *priv = netdev_priv(nd);
	if (netif_carrier_ok(priv->link))
		netif_carrier_on(nd);
	return xeth_proxy_open(nd);
}

static int xeth_vlan_stop(struct net_device *nd)
{
	netif_carrier_off(nd);
	return xeth_proxy_stop(nd);
}

static const struct net_device_ops xeth_vlan_ndo = {
	.ndo_init = xeth_proxy_init,
	.ndo_uninit = xeth_proxy_uninit,
	.ndo_open = xeth_vlan_open,
	.ndo_stop = xeth_vlan_stop,
	.ndo_start_xmit = xeth_proxy_start_xmit,
	.ndo_get_iflink = xeth_vlan_get_iflink,
	.ndo_get_stats64 = xeth_proxy_get_stats64,
	.ndo_change_mtu = xeth_proxy_change_mtu,
	.ndo_fix_features = xeth_proxy_fix_features,
	.ndo_set_features = xeth_proxy_set_features,
};

static void xeth_vlan_get_drvinfo(struct net_device *nd,
				  struct ethtool_drvinfo *drvinfo)
{
	struct xeth_vlan_priv *priv = netdev_priv(nd);
	u32 xid;
	u16 vid;

	switch (xeth_mux_encap(priv->proxy.mux)) {
	case XETH_ENCAP_VLAN:
		xid = priv->proxy.xid & XETH_ENCAP_VLAN_VID_MASK;
		vid = priv->proxy.xid >> XETH_ENCAP_VLAN_VID_BIT;
		break;
	case XETH_ENCAP_VPLS:
		xid = priv->proxy.xid & XETH_ENCAP_VPLS_VID_MASK;
		vid = priv->proxy.xid >> XETH_ENCAP_VPLS_VID_BIT;
		break;
	default:
		xid = 0;
		vid = 0;
		break;
	}
	strlcpy(drvinfo->driver, xeth_vlan_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u,%u", xid, vid);
}

static const struct ethtool_ops xeth_vlan_eto = {
	.get_drvinfo = xeth_vlan_get_drvinfo,
};

static void xeth_vlan_setup(struct net_device *nd)
{
	struct xeth_vlan_priv *priv = netdev_priv(nd);

	xeth_proxy_setup(nd);
	priv->proxy.kind = XETH_DEV_KIND_VLAN;
	nd->netdev_ops = &xeth_vlan_ndo;
	nd->ethtool_ops = &xeth_vlan_eto;
	nd->rtnl_link_ops = &xeth_vlan_lnko;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	ether_setup(nd);
	eth_hw_addr_random(nd); /* steal hw_addr from port in newlink */
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
	nd->priv_flags |= IFF_DONT_BRIDGE;
}

static int xeth_vlan_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing xeth port or lag link");
		return -EINVAL;
	}
	if (!tb[IFLA_IFNAME]) {
		NL_SET_ERR_MSG(extack, "missing ifname");
		return -EINVAL;
	}
	if (tb[IFLA_ADDRESS]) {
		NL_SET_ERR_MSG(extack, "can't set mac addr");
		return -EOPNOTSUPP;
	}
	if (data && data[XETH_VLAN_IFLA_VID]) {
		u16 vid = nla_get_u16(data[XETH_VLAN_IFLA_VID]);
		if (vid < 1 || vid >= VLAN_N_VID) {
			xeth_debug("vid %d out-of-range", vid);
			NL_SET_ERR_MSG(extack, "out-of-range VID");
			return -ERANGE;
		}
	}
	return 0;
}

static int xeth_vlan_newlink(struct net *src_net, struct net_device *nd,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct xeth_vlan_priv *priv = netdev_priv(nd);
	struct xeth_proxy *proxy;
	int i, err;
	u16 vid;

	priv->proxy.nd = nd;

	rcu_read_lock();
	priv->link =
		dev_get_by_index_rcu(dev_net(nd), nla_get_u32(tb[IFLA_LINK]));
	rcu_read_unlock();

	if (IS_ERR_OR_NULL(priv->link)) {
		NL_SET_ERR_MSG(extack, "unkown link");
		return PTR_ERR(priv->link);
	}
	if (!is_xeth_port(priv->link) && !is_xeth_lag(priv->link)) {
		NL_SET_ERR_MSG(extack, "link not an xeth port or lag");
		return -EINVAL;
	}

	proxy = netdev_priv(priv->link);

	if (data && data[XETH_VLAN_IFLA_VID])
		vid  = nla_get_u16(data[XETH_VLAN_IFLA_VID]);
	else
		for (i = 0; i < IFNAMSIZ; i++)
			if (nd->name[i] == '.') {
				unsigned long long ull;
				int err = kstrtoull(nd->name+i+1, 0, &ull);
				if (err) {
					NL_SET_ERR_MSG(extack, "invalid name");
					return err;
				}
				vid = ull;
				break;
			}

	if (vid < 1 || vid >= VLAN_N_VID) {
		xeth_debug_nd(nd, "vid %d out-of-range", vid);
		NL_SET_ERR_MSG(extack, "out-of-range VID");
		return -ERANGE;
	}

	eth_hw_addr_inherit(nd, priv->link);
	nd->addr_assign_type = NET_ADDR_STOLEN;

	priv->proxy.mux = proxy->mux;
	priv->proxy.xid = proxy->xid;

	switch (xeth_mux_encap(proxy->mux)) {
	case XETH_ENCAP_VLAN:
		priv->proxy.xid |= ((u32)vid << XETH_ENCAP_VLAN_VID_BIT);
		break;
	case XETH_ENCAP_VPLS:
		priv->proxy.xid |= ((u32)vid << XETH_ENCAP_VPLS_VID_BIT);
		break;
	}

	nd->min_mtu = priv->link->min_mtu;
	nd->max_mtu = priv->link->max_mtu;

	xeth_mux_add_proxy(&priv->proxy);

	err = register_netdevice(nd);
	if (!err)
		err = xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_NEW);
	if (err)
		xeth_mux_del_proxy(&priv->proxy);
	return err;
}

static void xeth_vlan_dellink(struct net_device *nd, struct list_head *q)
{
	unregister_netdevice_queue(nd, q);
}

static struct net *xeth_vlan_get_link_net(const struct net_device *nd)
{
	return dev_net(nd);
}

static const struct nla_policy xeth_vlan_nla_policy[XETH_VLAN_N_IFLA] = {
	[XETH_VLAN_IFLA_VID] = { .type = NLA_U16 },
};

struct rtnl_link_ops xeth_vlan_lnko = {
	.kind		= xeth_vlan_drvname,
	.priv_size	= sizeof(struct xeth_vlan_priv),
	.setup		= xeth_vlan_setup,
	.validate	= xeth_vlan_validate,
	.newlink	= xeth_vlan_newlink,
	.dellink	= xeth_vlan_dellink,
	.get_link_net	= xeth_vlan_get_link_net,
	.policy		= xeth_vlan_nla_policy,
	.maxtype	= XETH_VLAN_N_IFLA - 1,
};
