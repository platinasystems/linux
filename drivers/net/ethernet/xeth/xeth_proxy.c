/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_proxy.h"
#include "xeth_mux.h"
#include "xeth_port.h"
#include "xeth_sbtx.h"
#include "xeth_debug.h"

void xeth_proxy_dump_ifa(struct xeth_proxy *proxy)
{
	struct in_ifaddr *ifa;
	struct in_device *in_dev = in_dev_get(proxy->nd);
	if (!in_dev)
		return;
	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next)
		xeth_sbtx_ifa(proxy->mux, ifa, proxy->xid, NETDEV_UP);
	in_dev_put(in_dev);
}

void xeth_proxy_dump_ifa6(struct xeth_proxy *proxy)
{
	struct inet6_ifaddr *ifa6;
	struct inet6_dev *in6_dev = in6_dev_get(proxy->nd);
	if (!in6_dev)
		return;
	read_lock_bh(&in6_dev->lock);
	list_for_each_entry(ifa6, &in6_dev->addr_list, if_list)
		xeth_sbtx_ifa6(proxy->mux, ifa6, proxy->xid, NETDEV_UP);
	read_unlock_bh(&in6_dev->lock);
	in6_dev_put(in6_dev);
}

void xeth_proxy_dump_ifinfo(struct xeth_proxy *proxy)
{
	if (!proxy->xid || !proxy->mux)
		return;

	xeth_sbtx_ifinfo(proxy, 0, XETH_IFINFO_REASON_DUMP);

	if (proxy->kind == XETH_DEV_KIND_PORT) {
		xeth_sbtx_et_settings(proxy->mux, proxy->xid,
				      xeth_port_ethtool_ksettings(proxy->nd));
		xeth_sbtx_et_flags(proxy->mux, proxy->xid,
				   xeth_port_ethtool_priv_flags(proxy->nd));
	}
	if (!(proxy->nd->flags & IFF_SLAVE)) {
		xeth_proxy_dump_ifa(proxy);
		xeth_proxy_dump_ifa6(proxy);
	}
	if (proxy->kind == XETH_DEV_KIND_LAG ||
	    proxy->kind == XETH_DEV_KIND_BRIDGE) {
		struct net_device *lower;
		struct list_head *lowers;

		rcu_read_lock();
		netdev_for_each_lower_dev(proxy->nd, lower, lowers) {
			struct xeth_proxy *lproxy = netdev_priv(lower);
			xeth_sbtx_change_upper(proxy->mux, proxy->xid,
					       lproxy->xid, true);
		}
		rcu_read_unlock();
	}
}

void xeth_proxy_link_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	if (index < XETH_N_LINK_STAT)
		atomic64_set(&proxy->link_stats[index], count);
	else
		xeth_mux_inc_sbrx_invalid(proxy->mux);
}

int xeth_proxy_init(struct net_device *nd)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	spin_lock_init(&proxy->defer.mutex);
	nd->hw_features = NETIF_F_HW_L2FW_DOFFLOAD;
	nd->features |= NETIF_F_VLAN_CHALLENGED;
	nd->features &= ~NETIF_F_SOFT_FEATURES;
	nd->features |= NETIF_F_HW_L2FW_DOFFLOAD;
	netif_carrier_off(nd);
	return 0;
}

void xeth_proxy_uninit(struct net_device *nd)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	xeth_mux_del_proxy(proxy);
}

int xeth_proxy_open(struct net_device *nd)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	return (proxy->xid && proxy->mux) ?
		xeth_sbtx_ifinfo(proxy, nd->flags, XETH_IFINFO_REASON_UP) : 0;
}

int xeth_proxy_stop(struct net_device *nd)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	return xeth_sbtx_ifinfo(proxy, nd->flags, XETH_IFINFO_REASON_DOWN);
}

netdev_tx_t xeth_proxy_start_xmit(struct sk_buff *skb, struct net_device *nd)
{
	if (netif_carrier_ok(nd))
		return xeth_mux_encap_xmit(skb, nd);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

int xeth_proxy_get_iflink(const struct net_device *nd)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	return proxy->mux ? proxy->mux->ifindex: nd->ifindex;
}

void xeth_proxy_get_stats64(struct net_device *nd,
			    struct rtnl_link_stats64 *dst)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	xeth_link_stats(dst, proxy->link_stats);
}

int xeth_proxy_change_mtu(struct net_device *nd, int mtu)
{
	nd->mtu = mtu;
	return 0;
}

netdev_features_t xeth_proxy_fix_features(struct net_device *nd,
					  netdev_features_t features)
{
	features &= ~NETIF_F_SOFT_FEATURES;
	return features;
}

int xeth_proxy_set_features(struct net_device *nd, netdev_features_t features)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	return (proxy->xid && proxy->mux) ?
		xeth_sbtx_ifinfo(proxy, 0, XETH_IFINFO_REASON_FEATURES) : 0;
}
