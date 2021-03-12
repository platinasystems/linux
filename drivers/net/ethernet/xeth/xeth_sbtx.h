/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_SBTX_H
#define __NET_ETHERNET_XETH_SBTX_H

#include "xeth_proxy.h"
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/netevent.h>

struct xeth_sbtxb {
	struct list_head list;
	size_t len, sz;
};

enum {
	xeth_sbtxb_size	= ALIGN(sizeof(struct xeth_sbtxb), 32),
};

static inline void *xeth_sbtxb_data(const struct xeth_sbtxb *sbtxb)
{
	return (char *)sbtxb + xeth_sbtxb_size;
}

static inline void xeth_sbtxb_zero(const struct xeth_sbtxb *sbtxb)
{
	memset(xeth_sbtxb_data(sbtxb), 0, sbtxb->len);
}

struct xeth_sbtxb *xeth_mux_alloc_sbtxb(struct net_device *mux, size_t);
void xeth_mux_queue_sbtx(struct net_device *mux, struct xeth_sbtxb *);

int xeth_sbtx_break(struct net_device *);
int xeth_sbtx_change_upper(struct net_device *, u32 upper_xid, u32 lower_xid,
			   bool linking);
int xeth_sbtx_et_flags(struct net_device *, u32 xid, u32 flags);
int xeth_sbtx_et_settings(struct net_device *, u32 xid,
			  const struct ethtool_link_ksettings *);
int xeth_sbtx_fib_entry(struct net_device *,
			struct fib_entry_notifier_info *feni,
			unsigned long event);
int xeth_sbtx_fib6_entry(struct net_device *,
			 struct fib6_entry_notifier_info *feni,
			 unsigned long event);
int xeth_sbtx_ifa(struct net_device *, struct in_ifaddr *ifa,
		  unsigned long event, u32 xid);
int xeth_sbtx_ifa6(struct net_device *, struct inet6_ifaddr *ifa,
		   unsigned long event, u32 xid);
int xeth_sbtx_ifinfo(struct xeth_proxy *, unsigned iff,
		     enum xeth_msg_ifinfo_reason);
int xeth_sbtx_neigh_update(struct net_device *, struct neighbour *neigh);
int xeth_sbtx_netns(struct net_device *, struct net *ndnet, bool add);

#endif	/* __NET_ETHERNET_XETH_SBTX_H */
