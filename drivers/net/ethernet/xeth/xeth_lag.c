/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_lag.h"
#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_port.h"
#include "xeth_sbtx.h"
#include "xeth_version.h"
#include "xeth_debug.h"
#include <linux/netdevice.h>

static const char xeth_lag_drvname[] = "xeth-lag";

struct xeth_lag_priv {
	struct xeth_proxy proxy;
};

static int xeth_lag_init(struct net_device *lag)
{
	int err = xeth_proxy_init(lag);
	if (err)
		return err;
	lag->features |= NETIF_F_NETNS_LOCAL;
	lag->flags |= IFF_MASTER;
	return 0;
}

static int xeth_lag_del_lower(struct net_device *lag, struct net_device *nd)
{
	struct xeth_lag_priv *priv = netdev_priv(lag);
	struct xeth_proxy *lower = netdev_priv(nd);

	nd->priv_flags &= ~IFF_TEAM_PORT;

	netdev_upper_dev_unlink(nd, lag);
	netdev_update_features(lag);

	xeth_sbtx_change_upper(priv->proxy.mux, priv->proxy.xid,
			       lower->xid, false);
	xeth_proxy_dump_ifa(lower);
	xeth_proxy_dump_ifa6(lower);

	return 0;
}

static void xeth_lag_uninit(struct net_device *lag)
{
	struct xeth_proxy *lower, *tmp;
	struct net_device *nd;
	struct list_head *lowers;
	LIST_HEAD(quit);

	rcu_read_lock();
	netdev_for_each_lower_dev(lag, nd, lowers) {
		lower = netdev_priv(nd);
		list_add_tail(&lower->quit, &quit);
	}
	rcu_read_unlock();

	list_for_each_entry_safe(lower, tmp, &quit, quit) {
		xeth_lag_del_lower(lag, lower->nd);
		list_del(&lower->quit);
	}

	xeth_proxy_uninit(lag);
}

static int xeth_lag_n_lowers(struct net_device *lag)
{
	struct net_device *lower;
	struct list_head *lowers;
	int i = 0;

	netdev_for_each_lower_dev(lag, lower, lowers)
		i++;
	return i;
}

static int xeth_lag_ok_lower(struct net_device *nd,
			     struct netlink_ext_ack *extack)
{
	if (!is_xeth_port(nd)) {
		NL_SET_ERR_MSG(extack, "xeth-lag may only bind an xeth-port");
		return -EINVAL;
	}
	if (nd->flags & IFF_SLAVE) {
		NL_SET_ERR_MSG(extack, "already a xeth-bridge member");
		return -EBUSY;
	}
	if (nd->priv_flags & IFF_TEAM_PORT) {
		NL_SET_ERR_MSG(extack, "already a xeth-lag member");
		return -EBUSY;
	}
	if (netdev_master_upper_dev_get(nd)) {
		NL_SET_ERR_MSG(extack, "link busy");
		return -EBUSY;
	}
	return 0;
}

static int xeth_lag_add_lower(struct net_device *lag,
			      struct net_device *nd,
			      struct netlink_ext_ack *extack)
{
	struct xeth_lag_priv *priv = netdev_priv(lag);
	struct xeth_proxy *proxy;
	int err;

	err = xeth_lag_ok_lower(nd, extack);
	if (err)
		return err;
	if (xeth_lag_n_lowers(lag) >= 8) {
		NL_SET_ERR_MSG(extack, "xeth-lag full");
		return -ERANGE;
	}

	proxy = netdev_priv(nd);

	err = netdev_master_upper_dev_link(nd, lag, NULL, NULL, extack);
	if (err)
		return err;

	nd->flags |= IFF_TEAM_PORT;

	xeth_sbtx_change_upper(priv->proxy.mux, priv->proxy.xid,
			       proxy->xid, true);
	return 0;
}

static int xeth_lag_open(struct net_device *nd)
{
	/* FIXME condition this with lowers */
	netif_carrier_on(nd);
	return xeth_proxy_open(nd);
}

static int xeth_lag_stop(struct net_device *nd)
{
	netif_carrier_off(nd);
	return xeth_proxy_stop(nd);
}

static const struct net_device_ops xeth_lag_ndo = {
	.ndo_init = xeth_lag_init,
	.ndo_uninit = xeth_lag_uninit,
	.ndo_open = xeth_lag_open,
	.ndo_stop = xeth_lag_stop,
	.ndo_start_xmit = xeth_proxy_start_xmit,
	.ndo_get_iflink = xeth_proxy_get_iflink,
	.ndo_get_stats64 = xeth_proxy_get_stats64,
	.ndo_add_slave = xeth_lag_add_lower,
	.ndo_del_slave = xeth_lag_del_lower,
	.ndo_change_mtu = xeth_proxy_change_mtu,
	.ndo_fix_features = xeth_proxy_fix_features,
	.ndo_set_features = xeth_proxy_set_features,
};

static void xeth_lag_get_drvinfo(struct net_device *lag,
				 struct ethtool_drvinfo *drvinfo)
{
	struct xeth_lag_priv *priv = netdev_priv(lag);
	strlcpy(drvinfo->driver, xeth_lag_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN,
		  "%u", priv->proxy.xid);
}

static const struct ethtool_ops xeth_lag_eto = {
	.get_drvinfo = xeth_lag_get_drvinfo,
};

static void xeth_lag_setup(struct net_device *lag)
{
	struct xeth_lag_priv *priv = netdev_priv(lag);
	xeth_proxy_setup(lag);
	priv->proxy.kind = XETH_DEV_KIND_LAG;
	lag->netdev_ops = &xeth_lag_ndo;
	lag->ethtool_ops = &xeth_lag_eto;
	lag->rtnl_link_ops = &xeth_lag_lnko;
	lag->needs_free_netdev = true;
	lag->priv_destructor = NULL;
	ether_setup(lag);
	lag->priv_flags &= ~IFF_TX_SKB_SHARING;
	lag->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	lag->priv_flags |= IFF_NO_QUEUE;
	lag->priv_flags |= IFF_PHONY_HEADROOM;
	lag->priv_flags |= IFF_DONT_BRIDGE;
}

static int xeth_lag_validate(struct nlattr *tb[], struct nlattr *data[],
				struct netlink_ext_ack *extack)
{
	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing link");
		return -EINVAL;
	}
	if (tb[IFLA_ADDRESS]) {
		NL_SET_ERR_MSG(extack, "cannot set mac addr");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int xeth_lag_newlink(struct net *src_net, struct net_device *lag,
			    struct nlattr *tb[], struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct xeth_lag_priv *priv = netdev_priv(lag);
	struct xeth_proxy *lower;
	struct net_device *link;
	int err;

	rcu_read_lock();
	link = dev_get_by_index_rcu(dev_net(lag), nla_get_u32(tb[IFLA_LINK]));
	rcu_read_unlock();
	if (IS_ERR_OR_NULL(link)) {
		err = PTR_ERR(link);
		NL_SET_ERR_MSG(extack, "unkown link");
		return err;
	}
	err = xeth_lag_ok_lower(link, extack);
	if (err)
		return err;

	lower = netdev_priv(link);
	priv->proxy.nd = lag;
	priv->proxy.mux = lower->mux;
	eth_hw_addr_inherit(lag, link);
	lag->addr_assign_type = NET_ADDR_STOLEN;
	lag->min_mtu = link->min_mtu;
	lag->max_mtu = link->max_mtu;

	for (priv->proxy.xid = 3000;
	     xeth_mux_proxy_of_xid(priv->proxy.mux, priv->proxy.xid);
	     priv->proxy.xid++)
		if (priv->proxy.xid == 4095) {
			NL_SET_ERR_MSG(extack, "failed xid alloc");
			return -ENODEV;
		}
	xeth_mux_add_proxy(&priv->proxy);

	if (err = register_netdevice(lag), err < 0) {
		NL_SET_ERR_MSG(extack, "registry failed");
		xeth_mux_del_proxy(&priv->proxy);
		return err;
	}

	xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_NEW);

	if (err = xeth_lag_add_lower(lag, link, extack), err < 0) {
		xeth_mux_del_proxy(&priv->proxy);
		xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_DEL);
		unregister_netdevice(lag);
		return err;
	}

	return 0;
}

static void xeth_lag_dellink(struct net_device *nd, struct list_head *unregq)
{
	struct xeth_lag_priv *priv = netdev_priv(nd);
	xeth_mux_del_vlans(priv->proxy.mux, nd, unregq);
	unregister_netdevice_queue(nd, unregq);
}

static struct net *xeth_lag_get_link_net(const struct net_device *nd)
{
	return dev_net(nd);
}

struct rtnl_link_ops xeth_lag_lnko = {
	.kind		= xeth_lag_drvname,
	.priv_size	= sizeof(struct xeth_lag_priv),
	.setup		= xeth_lag_setup,
	.validate	= xeth_lag_validate,
	.newlink	= xeth_lag_newlink,
	.dellink	= xeth_lag_dellink,
	.get_link_net	= xeth_lag_get_link_net,
};
