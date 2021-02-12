/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_sbtx.h"
#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_lb.h"
#include "xeth_debug.h"

static void xeth_sbtx_msg_set(void *data, enum xeth_msg_kind kind)
{
	struct xeth_msg *msg = data;
	msg->header.z64 = 0;
	msg->header.z32 = 0;
	msg->header.z16 = 0;
	msg->header.version = XETH_MSG_VERSION;
	msg->header.kind = kind;
}

static inline u64 xeth_sbtx_ns_inum(struct net_device *nd)
{
	struct net *ndnet = dev_net(nd);
	return net_eq(ndnet, &init_net) ? 1 : ndnet->ns.inum;
}

int xeth_sbtx_break(struct net_device *mux)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_break *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_BREAK);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_change_upper(struct net_device *mux, u32 upper_xid, u32 lower_xid,
			   bool linking)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_change_upper_xid *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_CHANGE_UPPER_XID);
	msg->upper = upper_xid;
	msg->lower = lower_xid;
	msg->linking = linking ? 1 : 0;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_et_flags(struct net_device *mux, u32 xid, u32 flags)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ethtool_flags *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_ETHTOOL_FLAGS);
	msg->xid = xid;
	msg->flags = flags;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

static int xeth_sbtx_et_link_modes(struct net_device *mux,
				   enum xeth_msg_kind kind, u32 xid,
				   const volatile unsigned long *addr)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ethtool_link_modes *msg;
	int bit;
	const unsigned bits = min(__ETHTOOL_LINK_MODE_MASK_NBITS, 64);

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, kind);
	msg->xid = xid;
	for (bit = 0; bit < bits; bit++)
		if (test_bit(bit, addr))
			msg->modes |= 1ULL<<bit;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

/* Note, this sends speed 0 if autoneg, regardless of base.speed This is to
 * cover controller (e.g. vnet) restart where in it's earlier run it has sent
 * SPEED to note the auto-negotiated speed to ethtool user, but in subsequent
 * run, we don't want the controller to override autoneg.
 */
int xeth_sbtx_et_settings(struct net_device *mux, u32 xid,
			  const struct ethtool_link_ksettings *ks)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ethtool_settings *msg;
	const enum xeth_msg_kind kadv =
		XETH_MSG_KIND_ETHTOOL_LINK_MODES_ADVERTISING;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_ETHTOOL_SETTINGS);
	msg->xid = xid;
	msg->speed = ks->base.autoneg ?  0 : ks->base.speed;
	msg->duplex = ks->base.duplex;
	msg->port = ks->base.port;
	msg->phy_address = ks->base.phy_address;
	msg->autoneg = ks->base.autoneg;
	msg->mdio_support = ks->base.mdio_support;
	msg->eth_tp_mdix = ks->base.eth_tp_mdix;
	msg->eth_tp_mdix_ctrl = ks->base.eth_tp_mdix_ctrl;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return xeth_sbtx_et_link_modes(mux, kadv, xid,
				       ks->link_modes.advertising);
}

static const char * const xeth_sbtx_fib_event_names[] = {
	[FIB_EVENT_ENTRY_REPLACE] "replace",
	[FIB_EVENT_ENTRY_APPEND] "append",
	[FIB_EVENT_ENTRY_ADD] "add",
	[FIB_EVENT_ENTRY_DEL] "del",
};

int xeth_sbtx_fib_entry(struct net_device *mux,
			struct fib_entry_notifier_info *feni,
			unsigned long event)
{
	int i, nhs = 0;
	struct xeth_sbtxb *sbtxb;
	struct xeth_next_hop *nh;
	struct xeth_msg_fibentry *msg;
	size_t n = sizeof(*msg);

	if (feni->fi->fib_nhs > 0) {
		nhs = feni->fi->fib_nhs;
		n += (nhs * sizeof(struct xeth_next_hop));
	}
	sbtxb = xeth_mux_alloc_sbtxb(mux, n);
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	nh = (typeof(nh))&msg->nh[0];
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_FIBENTRY);
	msg->net = net_eq(feni->info.net, &init_net) ? 1 :
		feni->info.net->ns.inum;
	msg->address = htonl(feni->dst);
	msg->mask = inet_make_mask(feni->dst_len);
	msg->event = (u8)event;
	msg->nhs = nhs;
	msg->tos = feni->tos;
	msg->type = feni->type;
	msg->table = feni->tb_id;
	for(i = 0; i < msg->nhs; i++) {
		nh[i].ifindex = feni->fi->fib_nh[i].fib_nh_dev ?
			feni->fi->fib_nh[i].fib_nh_dev->ifindex : 0;
		nh[i].weight = feni->fi->fib_nh[i].fib_nh_weight;
		nh[i].flags = feni->fi->fib_nh[i].fib_nh_flags;
		nh[i].gw = feni->fi->fib_nh[i].fib_nh_gw4;
		nh[i].scope = feni->fi->fib_nh[i].fib_nh_scope;
	}
	no_xeth_debug("%s %pI4/%d w/ %d nexhop(s)",
		      xeth_sbtx_fib_event_names[event],
		      &msg->address, feni->dst_len, nhs);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_fib6_entry(struct net_device *mux,
			 struct fib6_entry_notifier_info *feni,
			 unsigned long event)
{
	struct fib6_info *rt = xeth_debug_ptr_err(feni->rt);
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_fib6entry *msg;
	struct xeth_next_hop6 *sibling;
	size_t n = sizeof(*msg);
	struct fib6_info *iter;
	int i;

	if (IS_ERR(rt))
		return PTR_ERR(rt);
	if (rt->fib6_nsiblings)
		n += (rt->fib6_nsiblings * sizeof(struct xeth_next_hop6));
	sbtxb = xeth_mux_alloc_sbtxb(mux, n);
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	sibling = (typeof(sibling))&msg->siblings[0];
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_FIB6ENTRY);
	msg->net = net_eq(feni->info.net, &init_net) ? 1 :
		feni->info.net->ns.inum;
	memcpy(msg->address, &rt->fib6_dst.addr, 16);
	msg->length = rt->fib6_dst.plen;
	msg->event = (u8)event;
	msg->nsiblings = rt->fib6_nsiblings;
	msg->type = rt->fib6_type;
	msg->table = rt->fib6_table->tb6_id;
	msg->nh.ifindex = rt->fib6_nh->fib_nh_dev ?
		rt->fib6_nh->fib_nh_dev->ifindex : 0;
	msg->nh.weight = rt->fib6_nh->fib_nh_weight;
	msg->nh.flags = rt->fib6_nh->fib_nh_flags;
	memcpy(msg->nh.gw, &rt->fib6_nh->fib_nh_gw6, 16);
	i = 0;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (i == rt->fib6_nsiblings)
			break;
		sibling->ifindex = iter->fib6_nh->fib_nh_dev ?
			iter->fib6_nh->fib_nh_dev->ifindex : 0;
		sibling->weight = iter->fib6_nh->fib_nh_weight;
		sibling->flags = iter->fib6_nh->fib_nh_flags;
		memcpy(sibling->gw, &iter->fib6_nh->fib_nh_gw6, 16);
		i++;
		sibling++;
	}
	no_xeth_debug("fib6 %s %pI6c/%d w/ %d nexhop(s)",
		      xeth_sbtx_fib_event_names[event],
		      &rt->fib6_dst.addr, rt->fib6_dst.plen,
		      1+i);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_ifa(struct net_device *mux, struct in_ifaddr *ifa,
		  unsigned long event, u32 xid)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ifa *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_IFA);
	msg->xid = xid;
	msg->event = event;
	msg->address = ifa->ifa_address;
	msg->mask = ifa->ifa_mask;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_ifa6(struct net_device *mux, struct inet6_ifaddr *ifa6,
		   unsigned long event, u32 xid)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ifa6 *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_IFA6);
	msg->xid = xid;
	msg->event = event;
	memcpy(msg->address, &ifa6->addr, 16);
	msg->length = ifa6->prefix_len;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_ifinfo(struct xeth_proxy *proxy, unsigned iff,
		     enum xeth_msg_ifinfo_reason reason)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ifinfo *msg;

	if (!proxy->xid || !proxy->mux)
		return 0;
	sbtxb = xeth_mux_alloc_sbtxb(proxy->mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_IFINFO);
	strlcpy(msg->ifname, proxy->nd->name, IFNAMSIZ);
	msg->net = xeth_sbtx_ns_inum(proxy->nd);
	msg->ifindex = proxy->nd->ifindex;
	msg->xid = proxy->xid;
	if (proxy->kind == XETH_DEV_KIND_LB)
		msg->kdata = xeth_lb_chan(proxy->nd);
	msg->flags = iff ? iff : proxy->nd->flags;
	memcpy(msg->addr, proxy->nd->dev_addr, ETH_ALEN);
	msg->kind = proxy->kind;
	msg->reason = reason;
	msg->features = proxy->nd->features;
	xeth_mux_queue_sbtx(proxy->mux, sbtxb);
	return 0;
}

int xeth_sbtx_neigh_update(struct net_device *mux, struct neighbour *neigh)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_neigh_update *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_NEIGH_UPDATE);
	msg->ifindex = neigh->dev->ifindex;
	msg->net = xeth_sbtx_ns_inum(neigh->dev);
	msg->ifindex = neigh->dev->ifindex;
	msg->family = neigh->ops->family;
	msg->len = neigh->tbl->key_len;
	memcpy(msg->dst, neigh->primary_key, neigh->tbl->key_len);
	read_lock_bh(&neigh->lock);
	if ((neigh->nud_state & NUD_VALID) && !neigh->dead) {
		char ha[MAX_ADDR_LEN];
		neigh_ha_snapshot(ha, neigh, neigh->dev);
		if ((neigh->nud_state & NUD_VALID) && !neigh->dead)
			memcpy(&msg->lladdr[0], ha, ETH_ALEN);
	}
	read_unlock_bh(&neigh->lock);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_netns(struct net_device *mux, struct net *ndnet, bool add)
{
	uint64_t net = net_eq(ndnet, &init_net) ? 1 : ndnet->ns.inum;
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_netns *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, add ?
			  XETH_MSG_KIND_NETNS_ADD : XETH_MSG_KIND_NETNS_DEL);
	msg->net = net;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}
