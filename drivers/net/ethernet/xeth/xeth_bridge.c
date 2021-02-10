/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_bridge.h"
#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_port.h"
#include "xeth_vlan.h"
#include "xeth_lag.h"
#include "xeth_sbtx.h"
#include "xeth_version.h"
#include "xeth_debug.h"
#include <linux/xeth.h>
#include <linux/netdevice.h>

static const char xeth_bridge_drvname[] = "xeth-bridge";

struct xeth_bridge_priv {
	struct xeth_proxy proxy;
};

static int xeth_bridge_init(struct net_device *br)
{
	int err = xeth_proxy_init(br);
	if (err)
		return err;
	br->features |= NETIF_F_NETNS_LOCAL;
	br->flags |= IFF_MASTER;
	return 0;
}

static int xeth_bridge_del_lower(struct net_device *br, struct net_device *nd)
{
	struct xeth_bridge_priv *priv = netdev_priv(br);
	struct xeth_proxy *lower = netdev_priv(nd);

	nd->priv_flags &= ~IFF_BRIDGE_PORT;
	nd->flags &= ~IFF_SLAVE;

	netdev_upper_dev_unlink(nd, br);
	netdev_update_features(br);

	xeth_sbtx_change_upper(priv->proxy.mux, priv->proxy.xid,
			       lower->xid, false);
	xeth_proxy_dump_ifa(lower);
	xeth_proxy_dump_ifa6(lower);

	return 0;
}

void xeth_bridge_uninit(struct net_device *br)
{
	struct xeth_proxy *lower, *tmp;
	struct net_device *nd;
	struct list_head *lowers;
	LIST_HEAD(defer);

	rcu_read_lock();
	netdev_for_each_lower_dev(br, nd, lowers) {
		lower = netdev_priv(nd);
		spin_lock(&lower->defer.mutex);
		list_add_tail(&lower->defer.list, &defer);
	}
	rcu_read_unlock();

	list_for_each_entry_safe(lower, tmp, &defer, defer.list) {
		xeth_bridge_del_lower(br, lower->nd);
		list_del(&lower->defer.list);
		spin_unlock(&lower->defer.mutex);
	}

	xeth_proxy_uninit(br);
}

static int xeth_bridge_add_lower(struct net_device *br,
				 struct net_device *nd,
				 struct netlink_ext_ack *extack)
{
	struct xeth_bridge_priv *priv = netdev_priv(br);
	struct xeth_proxy *lower;
	int err;

	if (!is_xeth_port(nd) && !is_xeth_vlan(nd) && !is_xeth_lag(nd)) {
		NL_SET_ERR_MSG(extack, "xeth-bridge may only bind "
			       "xeth-{port,vlan,lag}");
		return -EINVAL;
	}
	if (netdev_master_upper_dev_get(nd)) {
		NL_SET_ERR_MSG(extack, "link busy");
		return -EBUSY;
	}

	lower = netdev_priv(nd);

	call_netdevice_notifiers(NETDEV_JOIN, nd);

	err = netdev_master_upper_dev_link(nd, br, NULL, NULL, extack);
	if (err <= 0) {
		NL_SET_ERR_MSG(extack, "link failed");
		return err;
	}

	nd->flags |= IFF_SLAVE;

	xeth_sbtx_change_upper(priv->proxy.mux, priv->proxy.xid,
			       lower->xid, true);
	return 0;
}

static const struct net_device_ops xeth_bridge_ndo = {
	.ndo_init = xeth_bridge_init,
	.ndo_uninit = xeth_bridge_uninit,
	.ndo_open = xeth_proxy_open,
	.ndo_stop = xeth_proxy_stop,
	.ndo_start_xmit = xeth_proxy_start_xmit,
	.ndo_get_iflink = xeth_proxy_get_iflink,
	.ndo_get_stats64 = xeth_proxy_get_stats64,
	.ndo_add_slave = xeth_bridge_add_lower,
	.ndo_del_slave = xeth_bridge_del_lower,
	.ndo_change_mtu = xeth_proxy_change_mtu,
	.ndo_fix_features = xeth_proxy_fix_features,
	.ndo_set_features = xeth_proxy_set_features,
};

static void xeth_bridge_get_drvinfo(struct net_device *br,
				  struct ethtool_drvinfo *drvinfo)
{
	struct xeth_bridge_priv *priv = netdev_priv(br);
	strlcpy(drvinfo->driver, xeth_bridge_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN,
		  "%u", priv->proxy.xid);
}

static const struct ethtool_ops xeth_bridge_eto = {
	.get_drvinfo = xeth_bridge_get_drvinfo,
};

static void xeth_bridge_setup(struct net_device *br)
{
	struct xeth_bridge_priv *priv = netdev_priv(br);
	xeth_proxy_setup(br);
	priv->proxy.kind = XETH_DEV_KIND_BRIDGE;
	br->netdev_ops = &xeth_bridge_ndo;
	br->ethtool_ops = &xeth_bridge_eto;
	br->rtnl_link_ops = &xeth_bridge_lnko;
	br->needs_free_netdev = true;
	br->priv_destructor = NULL;
	ether_setup(br);
	br->priv_flags &= ~IFF_TX_SKB_SHARING;
	br->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	br->priv_flags |= IFF_NO_QUEUE;
	br->priv_flags |= IFF_PHONY_HEADROOM;
	br->priv_flags |= IFF_DONT_BRIDGE;
}

static int xeth_bridge_validate(struct nlattr *tb[], struct nlattr *data[],
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

static int xeth_bridge_newlink(struct net *src_net, struct net_device *br,
			       struct nlattr *tb[], struct nlattr *data[],
			       struct netlink_ext_ack *extack)
{
	struct xeth_bridge_priv *priv = netdev_priv(br);
	struct xeth_proxy *lower;
	struct net_device *link;
	int err;

	rcu_read_lock();
	link = dev_get_by_index_rcu(dev_net(br), nla_get_u32(tb[IFLA_LINK]));
	rcu_read_unlock();
	if (IS_ERR_OR_NULL(link)) {
		err = PTR_ERR(link);
		NL_SET_ERR_MSG(extack, "unkown link");
		return err;
	}
	if (!is_xeth_port(link) && !is_xeth_vlan(link) && !is_xeth_lag(link)) {
		NL_SET_ERR_MSG(extack, "xeth-bridge may only bind "
			       "an xeth-{port, vlan, or lag}");
		return -EINVAL;
	}

	lower = netdev_priv(link);
	priv->proxy.nd = br;
	priv->proxy.mux = lower->mux;
	eth_hw_addr_inherit(br, link);
	br->addr_assign_type = NET_ADDR_STOLEN;
	br->min_mtu = link->min_mtu;
	br->max_mtu = link->max_mtu;

	for (priv->proxy.xid = 3000;
	     xeth_mux_proxy_of_xid(priv->proxy.mux, priv->proxy.xid);
	     priv->proxy.xid++)
		if (priv->proxy.xid == 4095) {
			NL_SET_ERR_MSG(extack, "failed xid alloc");
			return -ENODEV;
		}
	xeth_mux_add_proxy(&priv->proxy);

	if (err = register_netdevice(br), err < 0) {
		NL_SET_ERR_MSG(extack, "registry failed");
		xeth_mux_del_proxy(&priv->proxy);
		return err;
	}

	xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_NEW);

	if (err = xeth_bridge_add_lower(br, link, extack), err < 0) {
		unregister_netdevice(br);
		xeth_mux_del_proxy(&priv->proxy);
		xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_DEL);
		return err;
	}

	return 0;
}

static void xeth_bridge_dellink(struct net_device *br, struct list_head *q)
{
	unregister_netdevice_queue(br, q);
}

static struct net *xeth_bridge_get_link_net(const struct net_device *br)
{
	return dev_net(br);
}

struct rtnl_link_ops xeth_bridge_lnko = {
	.kind		= xeth_bridge_drvname,
	.priv_size	= sizeof(struct xeth_bridge_priv),
	.setup		= xeth_bridge_setup,
	.validate	= xeth_bridge_validate,
	.newlink	= xeth_bridge_newlink,
	.dellink	= xeth_bridge_dellink,
	.get_link_net	= xeth_bridge_get_link_net,
};
