/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_PROXY_H
#define __NET_ETHERNET_XETH_PROXY_H

#include "xeth_link_stat.h"
#include <linux/netdevice.h>

/**
 * struct xeth_proxy -	first member of each xeth proxy device priv
 * 			{ port, vlan, bridge, lag }
 */
struct xeth_proxy {
	struct net_device *nd, *mux;
	/* @node: XID hash entry */
	struct hlist_node __rcu	node;
	/* @kin: other proxies of the same kind */
	struct list_head kin;
	/* @defer: pending task */
	struct {
		struct spinlock mutex;
		struct list_head list;
	} defer;
	atomic64_t link_stats[XETH_N_LINK_STAT];
	enum xeth_dev_kind kind;
	u32 xid;
};

#define xeth_proxy_of_rcu(ptr)						\
	container_of(ptr, struct xeth_proxy, rcu)

#define xeth_proxy_of_kin(ptr)						\
	container_of(ptr, struct xeth_proxy, kin)

struct xeth_proxy *xeth_mux_proxy_of_xid(struct net_device *mux, u32 xid);
struct xeth_proxy *xeth_mux_proxy_of_nd(struct net_device *mux,
					struct net_device *nd);

void xeth_mux_add_proxy(struct xeth_proxy *);
void xeth_mux_del_proxy(struct xeth_proxy *);

void xeth_proxy_dump_ifa(struct xeth_proxy *);
void xeth_proxy_dump_ifa6(struct xeth_proxy *);
void xeth_proxy_dump_ifinfo(struct xeth_proxy *);

static inline void xeth_proxy_reset_link_stats(struct xeth_proxy *proxy)
{
	xeth_link_stat_init(proxy->link_stats);
}

static inline void xeth_proxy_setup(struct net_device *nd)
{
	struct xeth_proxy *proxy = netdev_priv(nd);
	INIT_LIST_HEAD(&proxy->kin);
	xeth_link_stat_init(proxy->link_stats);
}

int xeth_proxy_init(struct net_device *nd);
void xeth_proxy_uninit(struct net_device *nd);
int xeth_proxy_open(struct net_device *nd);
int xeth_proxy_stop(struct net_device *nd);
netdev_tx_t xeth_proxy_start_xmit(struct sk_buff *skb, struct net_device *nd);
int xeth_proxy_get_iflink(const struct net_device *nd);
int xeth_proxy_change_mtu(struct net_device *nd, int mtu);
void xeth_proxy_link_stat(struct net_device *nd, u32 index, u64 count);
void xeth_proxy_get_stats64(struct net_device *, struct rtnl_link_stats64 *);
netdev_features_t xeth_proxy_fix_features(struct net_device *,
					  netdev_features_t);
int xeth_proxy_set_features(struct net_device *, netdev_features_t);

#endif	/* __NET_ETHERNET_XETH_PROXY_H */
