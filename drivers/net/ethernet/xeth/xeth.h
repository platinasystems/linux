/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_H
#define __NET_ETHERNET_XETH_H

#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/etherdevice.h>
#include <linux/hashtable.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <linux/xeth.h>
#include <net/rtnetlink.h>
#include <net/ip_fib.h>
#include <net/netevent.h>
#include <xeth_pr.h>
#include <xeth_priv.h>

#if defined(KBUILD_MODNAME)
# define xeth_name KBUILD_MODNAME
#else	/* KBUILD_MODNAME */
# define XETH_NAME "xeth"
#endif	/* KBUILD_MODNAME */

#define xeth_version "2.0"

#ifdef XETH_MAIN
#define xeth_extern(args...)	args
#else
#define xeth_extern(args...)	extern args
#endif

enum {
	xeth_hlist_bits = 4,
	xeth_encap_size = VLAN_HLEN,
};

enum {
	xeth_count_rx_invalid,
	xeth_count_rx_no_dev,
	xeth_count_sb_connections,
	xeth_count_sb_invalid,
	xeth_count_sb_no_dev,
	xeth_count_sb_from_user_msgs,
	xeth_count_sb_from_user_ticks,
	xeth_count_sb_to_user_msgs,
	xeth_count_sb_to_user_no_mem,
	xeth_count_sb_to_user_queued,
	xeth_count_sb_to_user_retries,
	xeth_count_sb_to_user_ticks,
	n_xeth_count,
};

xeth_extern(const struct xeth_config *xeth_config);
xeth_extern(size_t xeth_priv_size);
xeth_extern(size_t xeth_n_ethtool_flags);
xeth_extern(size_t xeth_n_ethtool_stats);
xeth_extern(struct hlist_head __rcu xeth_priv_by_ifindex[1<<xeth_hlist_bits]);
xeth_extern(struct spinlock xeth_priv_by_ifindex_mutex);
xeth_extern(struct list_head __rcu xeth_uppers);
xeth_extern(struct spinlock xeth_uppers_mutex);
xeth_extern(atomic64_t	__xeth_counters[n_xeth_count]);

extern struct ethtool_ops xeth_ethtool_ops;
extern struct net_device_ops xeth_ndo_ops;

#define xeth_for_each_iflink(index)					\
	for (index = 0; xeth_config && xeth_config->iflinks_akas[index]; index++)

#define xeth_for_each_iflink_aka(index,aka)				\
	for (aka = 0; xeth_config->iflinks_akas[index][aka]; aka++)

int __init xeth_sb_init(void);
int xeth_sb_deinit(int err);
void __exit xeth_sb_exit(void);

int __init xeth_notifier_init(void);
int xeth_notifier_deinit(int err);
void __exit xeth_notifier_exit(void);

struct kobject *xeth_sysfs_start(struct kobject *parent);
void xeth_sysfs_stop(struct kobject *kobj);

int xeth_iflink_start(void);
void xeth_iflink_stop(void);

int xeth_dev_start(void);
void xeth_dev_stop(void);

int xeth_sb_start(void);
void xeth_sb_stop(void);

void xeth_vlan_stop(void);

struct net_device *xeth_iflink(int i);
int xeth_iflink_index(u16 id);
void xeth_iflink_reset(int i);

int xeth_notifier_register_fib(void);
void xeth_notifier_unregister_fib(void);

void xeth_sb_dump_common_ifinfo(struct net_device *nd);
int xeth_sb_send_change_upper(int upper, int lower, bool linking);
int xeth_sb_send_ethtool_flags(struct net_device *nd);
int xeth_sb_send_ethtool_settings(struct net_device *nd);
int xeth_sb_send_ifa(struct net_device *nd, unsigned long event,
		     struct in_ifaddr *ifa);
int xeth_sb_send_ifinfo(struct net_device *nd, unsigned int iff, u8 reason);
int xeth_sb_send_fib_entry(unsigned long event,
			   struct fib_notifier_info *info);
int xeth_sb_send_neigh_update(struct neighbour *neigh);

int xeth_sysfs_priv_add(struct xeth_priv *priv);
void xeth_sysfs_priv_del(struct xeth_priv *priv);

int xeth_vlan_id(struct net_device *nd);
int xeth_vlan_new_dev(struct net_device *nd);
int xeth_vlan_associate_dev(struct net_device *nd);
void xeth_vlan_dump_associate_devs(void);
void xeth_vlan_disassociate_dev(struct net_device *nd);
void xeth_vlan_changemtu(struct net_device *iflink);
rx_handler_result_t xeth_vlan_rx(struct sk_buff **pskb);
void xeth_vlan_sb(const char *buf, size_t n);
netdev_tx_t xeth_vlan_tx(struct sk_buff *skb, struct net_device *nd);

static inline void xeth_config_init(const struct xeth_config *config)
{
	xeth_config = config;
	for (xeth_n_ethtool_flags = 0;
	     xeth_config->ethtool.flags[xeth_n_ethtool_flags];
	     xeth_n_ethtool_flags++);
	for (xeth_n_ethtool_stats = 0;
	     xeth_config->ethtool.stats[xeth_n_ethtool_stats];
	     xeth_n_ethtool_stats++);
	xeth_priv_size = sizeof(struct xeth_priv) +
		(xeth_n_ethtool_stats * sizeof(u64));
}

static inline void xeth_config_deinit(void)
{
	xeth_n_ethtool_flags = 0;
	xeth_n_ethtool_stats = 0;
	xeth_priv_size = 0;
	xeth_config = NULL;
}

#define xeth_counter(name)		__xeth_counters[xeth_count_##name]
#define xeth_count_of(index)		__xeth_counters[index]
#define xeth_count(name)		atomic64_read(&xeth_counter(name))
#define xeth_count_add(n, name)		atomic64_add(n, &xeth_counter(name))
#define xeth_count_dec(name)		atomic64_dec(&xeth_counter(name))
#define xeth_count_inc(name)		atomic64_inc(&xeth_counter(name))
#define xeth_count_set(name, n)		atomic64_set(&xeth_counter(name), n)

static inline void xeth_reset_counters(void)
{
	int i;
	for (i = 0; i < n_xeth_count; i++)
		atomic64_set(&__xeth_counters[i], 0);
}

#define xeth_for_each_priv_rcu(priv,bkt)				\
	hash_for_each_rcu(xeth_priv_by_ifindex, (bkt), (priv), node)

static inline void xeth_init_priv_by_ifindex(void)
{
	int i;

	spin_lock_init(&xeth_priv_by_ifindex_mutex);
	for (i = 0; i < (1<<xeth_hlist_bits); i++)
		WRITE_ONCE(xeth_priv_by_ifindex[i].first, NULL);
}

static inline void xeth_lock_priv_by_ifindex(void)
{
	spin_lock(&xeth_priv_by_ifindex_mutex);
}

static inline void xeth_unlock_priv_by_ifindex(void)
{
	spin_unlock(&xeth_priv_by_ifindex_mutex);
}

static inline void xeth_add_priv(struct xeth_priv *priv)
{
	xeth_lock_priv_by_ifindex();
	hash_add_rcu(xeth_priv_by_ifindex, &priv->node, priv->nd->ifindex);
	xeth_unlock_priv_by_ifindex();
}

static inline void xeth_del_priv(struct xeth_priv *priv)
{
	xeth_lock_priv_by_ifindex();
	hash_del_rcu(&priv->node);
	xeth_unlock_priv_by_ifindex();
}

static inline struct xeth_priv *xeth_priv_of(s32 ifindex)
{
	struct xeth_priv *priv;
	int i = hash_min(ifindex, xeth_hlist_bits);

	rcu_read_lock();
	hlist_for_each_entry_rcu(priv, &xeth_priv_by_ifindex[i], node)
		if (priv->nd->ifindex == ifindex) {
			rcu_read_unlock();
			return priv;
		}
	rcu_read_unlock();
	return NULL;
}

static inline bool netif_is_xeth(struct net_device *nd)
{
	return	(nd->netdev_ops == &xeth_ndo_ops) &&
		(nd->ethtool_ops == &xeth_ethtool_ops);
}

struct	xeth_upper {
	struct	list_head __rcu list;
	struct {
		struct	rcu_head	free;
		struct	rcu_head	send_change;
	} rcu;
	struct	{
		s32	lower;
		s32	upper;
	} ifindex;
};

#define xeth_for_each_upper_rcu(upper)					\
	list_for_each_entry_rcu(upper, &xeth_uppers, list)

static inline void xeth_init_uppers(void)
{
	spin_lock_init(&xeth_uppers_mutex);
	INIT_LIST_HEAD_RCU(&xeth_uppers);
}

static inline void xeth_lock_uppers(void)
{
	spin_lock(&xeth_uppers_mutex);
}

static inline void xeth_unlock_uppers(void)
{
	spin_unlock(&xeth_uppers_mutex);
}

static inline int xeth_add_upper(int upper, int lower)
{
	struct xeth_upper *p;

	p = kzalloc(sizeof(struct xeth_upper), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->ifindex.upper = upper;
	p->ifindex.lower = lower;
	xeth_lock_uppers();
	list_add_tail_rcu(&p->list, &xeth_uppers);
	xeth_unlock_uppers();
	return 0;
}

static inline void xeth_del_upper(struct xeth_upper *upper)
{
	xeth_lock_uppers();
	list_del_rcu(&upper->list);
	xeth_unlock_uppers();
	kfree_rcu(upper, rcu.free);
}

static inline struct xeth_upper *xeth_upper_of(int upper, int lower)
{
	struct xeth_upper *p;

	rcu_read_lock();
	list_for_each_entry_rcu(p, &xeth_uppers, list)
		if (p &&
		    p->ifindex.upper == upper &&
		    p->ifindex.lower == lower) {
			rcu_read_unlock();
			return p;
		}
	rcu_read_unlock();
	return NULL;
}

#endif  /* __NET_ETHERNET_XETH_H */
