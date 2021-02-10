/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_lb.h"
#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_port.h"
#include "xeth_lag.h"
#include "xeth_sbtx.h"
#include "xeth_version.h"
#include <linux/xeth.h>
#include <linux/netdevice.h>

static const char xeth_lb_drvname[] = "xeth-lb";

struct xeth_lb_priv {
	struct xeth_proxy proxy;
	u8 chan;
};

u8 xeth_lb_chan(struct net_device *nd)
{
	struct xeth_lb_priv *priv = netdev_priv(nd);
	return priv->chan;
}

static int xeth_lb_get_iflink(const struct net_device *nd)
{
	struct xeth_lb_priv *priv = netdev_priv(nd);
	return priv->proxy.mux->ifindex;
}

static const struct net_device_ops xeth_lb_ndo = {
	.ndo_init = xeth_proxy_init,
	.ndo_uninit = xeth_proxy_uninit,
	.ndo_open = xeth_proxy_open,
	.ndo_stop = xeth_proxy_stop,
	.ndo_start_xmit = xeth_proxy_start_xmit,
	.ndo_get_iflink = xeth_lb_get_iflink,
	.ndo_get_stats64 = xeth_proxy_get_stats64,
	.ndo_change_mtu = xeth_proxy_change_mtu,
	.ndo_fix_features = xeth_proxy_fix_features,
	.ndo_set_features = xeth_proxy_set_features,
};

static void xeth_lb_get_drvinfo(struct net_device *nd,
				struct ethtool_drvinfo *drvinfo)
{
	struct xeth_lb_priv *priv = netdev_priv(nd);
	strlcpy(drvinfo->driver, xeth_lb_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u:%u",
		  priv->chan, priv->proxy.xid);
}

static const struct ethtool_ops xeth_lb_eto = {
	.get_drvinfo = xeth_lb_get_drvinfo,
};

static void xeth_lb_setup(struct net_device *nd)
{
	struct xeth_lb_priv *priv = netdev_priv(nd);

	xeth_proxy_setup(nd);
	priv->proxy.kind = XETH_DEV_KIND_VLAN;
	nd->netdev_ops = &xeth_lb_ndo;
	nd->ethtool_ops = &xeth_lb_eto;
	nd->rtnl_link_ops = &xeth_lb_lnko;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	ether_setup(nd);
	eth_hw_addr_random(nd);
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
	nd->priv_flags |= IFF_DONT_BRIDGE;
}

static int xeth_lb_validate(struct nlattr *tb[], struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing xeth mux link");
		return -EINVAL;
	}
	if (tb[IFLA_ADDRESS]) {
		NL_SET_ERR_MSG(extack, "can't set mac addr");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int xeth_lb_newlink(struct net *src_net, struct net_device *nd,
			   struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct xeth_lb_priv *priv = netdev_priv(nd);
	int err;

	priv->proxy.nd = nd;

	rcu_read_lock();
	priv->proxy.mux =
		dev_get_by_index_rcu(dev_net(nd), nla_get_u32(tb[IFLA_LINK]));
	rcu_read_unlock();
	if (IS_ERR_OR_NULL(priv->proxy.mux)) {
		NL_SET_ERR_MSG(extack, "unkown mux");
		return PTR_ERR(priv->proxy.mux);
	}
	if (!is_xeth_mux(priv->proxy.mux)) {
		NL_SET_ERR_MSG(extack, "link isn't an xeth mux");
		return -EINVAL;
	}

	nd->min_mtu = priv->proxy.mux->min_mtu;
	nd->max_mtu = priv->proxy.mux->max_mtu;

	if (data && data[XETH_LB_IFLA_CHANNEL])
		priv->chan  = nla_get_u16(data[XETH_LB_IFLA_CHANNEL]);

	for (priv->proxy.xid = 3000;
	     xeth_mux_proxy_of_xid(priv->proxy.mux, priv->proxy.xid);
	     priv->proxy.xid++)
		if (priv->proxy.xid == 4095) {
			NL_SET_ERR_MSG(extack, "failed xid alloc");
			return -ENODEV;
		}
	xeth_mux_add_proxy(&priv->proxy);

	err = register_netdevice(nd);
	if (!err)
		err = xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_NEW);
	if (err)
		xeth_mux_del_proxy(&priv->proxy);
	return err;
}

static void xeth_lb_dellink(struct net_device *nd, struct list_head *unregq)
{
	unregister_netdevice_queue(nd, unregq);
}

static struct net *xeth_lb_get_link_net(const struct net_device *nd)
{
	return dev_net(nd);
}

static const struct nla_policy xeth_lb_nla_policy[XETH_LB_N_IFLA] = {
	[XETH_LB_IFLA_CHANNEL] = { .type = NLA_U8 },
};

struct rtnl_link_ops xeth_lb_lnko = {
	.kind		= xeth_lb_drvname,
	.priv_size	= sizeof(struct xeth_lb_priv),
	.setup		= xeth_lb_setup,
	.validate	= xeth_lb_validate,
	.newlink	= xeth_lb_newlink,
	.dellink	= xeth_lb_dellink,
	.get_link_net	= xeth_lb_get_link_net,
	.policy		= xeth_lb_nla_policy,
	.maxtype	= XETH_LB_N_IFLA - 1,
};
