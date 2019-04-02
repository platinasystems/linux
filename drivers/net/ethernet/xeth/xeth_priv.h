/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_PRIV_H
#define __NET_ETHERNET_XETH_PRIV_H

#include <linux/atomic.h>
#include <linux/ethtool.h>
#include <uapi/linux/if_link.h>

enum {
	xeth_count_priv_rx_packets,
	xeth_count_priv_rx_bytes,
	xeth_count_priv_rx_nd_mismatch,
	xeth_count_priv_rx_dropped,
	xeth_count_priv_sb_carrier,
	xeth_count_priv_sb_ethtool_stats,
	xeth_count_priv_sb_link_stats,
	xeth_count_priv_sb_packets,
	xeth_count_priv_sb_bytes,
	xeth_count_priv_sb_no_mem,
	xeth_count_priv_sb_dropped,
	xeth_count_priv_tx_packets,
	xeth_count_priv_tx_bytes,
	xeth_count_priv_tx_no_mem,
	xeth_count_priv_tx_no_way,
	xeth_count_priv_tx_no_iflink,
	xeth_count_priv_tx_dropped,
	n_xeth_count_priv,
};

#define xeth_priv_counter(priv, name)	priv->count[xeth_count_priv_##name]
#define xeth_count_priv(priv, name)					\
	atomic64_read(&xeth_priv_counter(priv, name)
#define xeth_count_priv_add(priv, n, name)				\
	atomic64_add(n, &xeth_priv_counter(priv, name))
#define xeth_count_priv_dec(priv, name)					\
	atomic64_dec(&xeth_priv_counter(priv, name))
#define xeth_count_priv_inc(priv, name)					\
	atomic64_inc(&xeth_priv_counter(priv, name))
#define xeth_count_priv_set(priv, name, n)				\
	atomic64_set(&xeth_priv_counter(priv, name), n)

struct	xeth_priv {
	struct	hlist_node __rcu	node;
	struct {
		struct	rcu_head	dump_ifinfo;
		struct	rcu_head	reset_stats;
		struct	rcu_head	carrier_off;
	} rcu;
	struct {
		struct	list_head __rcu	list;
		struct	spinlock	lock;
	} vids;
	struct	net_device	*nd;

	u16	id;
	s16	portid;
	s16	iflinki, porti;
	s8	subporti;
	u8	devtype;

	struct {
		struct	rtnl_link_stats64 stats;
		struct	spinlock	lock;
	} link;

	struct	kobject	kobj;
	atomic64_t	count[n_xeth_count_priv];

	struct {
		struct	ethtool_link_ksettings settings;
		struct	spinlock	lock;
		u32	flags;
	} ethtool;
	u64	ethtool_stats[];
};

#define to_xeth_priv(x)	container_of((x), struct xeth_priv, kobj)

static inline void xeth_priv_reset_counters(struct xeth_priv *priv)
{
	int i;
	for (i = 0; i < n_xeth_count_priv; i++)
		atomic64_set(&priv->count[i], 0);
}

struct	xeth_priv_vid {
	struct	list_head __rcu list;
	struct {
		struct	rcu_head	free;
	} rcu;
	__be16	proto;
	u16	id;
};

#define xeth_priv_for_each_vid_rcu(priv,vid)				\
	list_for_each_entry_rcu((vid), &(priv)->vids.list, list)

static inline void xeth_priv_init_vids(struct xeth_priv *priv)
{
	spin_lock_init(&priv->vids.lock);
	INIT_LIST_HEAD_RCU(&priv->vids.list);
}

static inline void xeth_priv_lock_vids(struct xeth_priv *priv)
{
	spin_lock(&priv->vids.lock);
}

static inline void xeth_priv_unlock_vids(struct xeth_priv *priv)
{
	spin_unlock(&priv->vids.lock);
}

static inline int xeth_priv_add_vid(struct xeth_priv *priv,
				    __be16 proto, u16 id)
{
	struct xeth_priv_vid *p;

	p = kzalloc(sizeof(struct xeth_priv_vid), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->proto = proto;
	p->id = id;
	xeth_priv_lock_vids(priv);
	list_add_tail_rcu(&p->list, &priv->vids.list);
	xeth_priv_unlock_vids(priv);
	return 0;
}

static inline void xeth_priv_del_vid(struct xeth_priv *priv,
				     struct xeth_priv_vid *vid)
{
	xeth_priv_lock_vids(priv);
	list_del_rcu(&vid->list);
	xeth_priv_unlock_vids(priv);
	kfree_rcu(vid, rcu.free);
}

static inline struct xeth_priv_vid *xeth_priv_vid_rcu(struct xeth_priv *priv,
						      __be16 proto, u16 id)
{
	struct xeth_priv_vid *vid;

	list_for_each_entry_rcu(vid, &priv->vids.list, list)
		if (vid->proto == proto && vid->id == id)
			return vid;
	return NULL;
}

static inline void xeth_priv_init_link_lock(struct xeth_priv *priv)
{
	spin_lock_init(&priv->link.lock);
}

static inline void xeth_priv_lock_link(struct xeth_priv *priv)
{
	spin_lock(&priv->link.lock);
}

static inline void xeth_priv_unlock_link(struct xeth_priv *priv)
{
	spin_unlock(&priv->link.lock);
}

static inline void xeth_priv_init_ethtool_lock(struct xeth_priv *priv)
{
	spin_lock_init(&priv->ethtool.lock);
}

static inline void xeth_priv_lock_ethtool(struct xeth_priv *priv)
{
	spin_lock(&priv->ethtool.lock);
}

static inline void xeth_priv_unlock_ethtool(struct xeth_priv *priv)
{
	spin_unlock(&priv->ethtool.lock);
}

#endif /* __NET_ETHERNET_XETH_PRIV_H */
