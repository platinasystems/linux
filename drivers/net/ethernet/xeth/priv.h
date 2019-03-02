/* Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __XETH_PRIV_H
#define __XETH_PRIV_H

#include <linux/atomic.h>
#include <linux/ethtool.h>
#include <uapi/linux/if_link.h>

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
	s16	ndi, iflinki, porti;
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

#endif /* __XETH_PRIV_H */
