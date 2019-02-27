/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __XETH_H
#define __XETH_H

#define XETH_KIND "xeth"

extern struct xeth xeth;
extern struct rtnl_link_ops xeth_link_ops;
extern struct net_device_ops xeth_ndo_ops;
extern struct ethtool_ops xeth_ethtool_ops;

#include <count.h>
#include <pr.h>
#include <priv.h>

#include <linux/etherdevice.h>
#include <linux/hashtable.h>
#include <linux/inetdevice.h>
#include <net/rtnetlink.h>
#include <net/ip_fib.h>
#include <net/netevent.h>

#ifndef XETH_VERSION
#define XETH_VERSION unknown
#endif

enum {
	xeth_ht_bits = 4,
};

struct xeth {
	struct hlist_head __rcu	ht[1<<xeth_ht_bits];
	struct list_head  __rcu	uppers;
	struct {
		struct list_head  __rcu	uppers;
		struct list_head  __rcu	vids;
	} free;
	int	base;		/*  0 or 1 based port and subport */
	int	*provision;	/* list of 1, 2, or 4 subports per port */
	/* a NULL terminated list of NULL terminated iflink aliases */
	const char * const * const *iflinks_akas;
	const char 	*name;
	const size_t	ports, rxqs, txqs;
	const size_t	priv_size;
	struct {
		size_t	size;	/* e.g. VLAN_HLEN */
		int	(*init)(void);
		void	(*exit)(void);
		int	(*id)(struct net_device *nd);
		int	(*new_dev)(struct net_device *nd);
		int	(*associate_dev)(struct net_device *nd);
		void	(*dump_associate_devs)(void);
		void	(*disassociate_dev)(struct net_device *nd);
		void	(*changemtu)(struct net_device *iflink);
		rx_handler_result_t
			(*rx)(struct sk_buff **pskb);
		void	(*sb)(const char *buf, size_t n);
		netdev_tx_t
			(*tx)(struct sk_buff *skb, struct net_device *nd);
	} encap;
	struct {
		u64	base;
		u8	assign_type;
	} ea;
	struct {
		struct {
			struct	task_struct *main;
			struct	task_struct *rx;
			struct	{
				char	rx[IFNAMSIZ];
			} name;
		} task;
		struct {
			struct	mutex	mutex;
			struct	list_head
				queue;
		} tx;
		char	*rxbuf;
	} sb;

	struct {
		struct {
			const size_t flags, stats;
		} n;
		const char * const *flags;
		const char * const *stats;
	} ethtool;
	void	(*init_ethtool_settings)(struct xeth_priv *priv);
	int	(*validate_speed)(struct net_device *, u32);
	atomic64_t	count[n_xeth_count];
	struct	kset	*kset;	/* /sys/kernel/<xeth.name> */
	struct	kobject	kobj;	/* /sys/kernel/<xeth.name>/xeth */
};

#define xeth_for_each_iflink(IFLINK)	\
	for ((IFLINK) = 0; xeth.iflinks_akas[IFLINK]; (IFLINK)++)

#define xeth_for_each_iflink_aka(IFLINK,AKA)				\
	for ((AKA) = 0; xeth.iflinks_akas[IFLINK][AKA]; (AKA)++)

int xeth_init(void);
int xeth_dev_init(void);
int xeth_ethtool_init(void);
int xeth_iflink_init(void);
int xeth_link_init(void);
int xeth_ndo_init(void);
int xeth_notifier_init(void);
int xeth_sb_init(void);
int xeth_sysfs_init(void);
int xeth_vlan_init(void);
int xeth_create_links(void);

void xeth_exit(void);
void xeth_dev_exit(void);
void xeth_ethtool_exit(void);
void xeth_iflink_exit(void);
void xeth_link_exit(void);
void xeth_ndo_exit(void);
void xeth_notifier_exit(void);
void xeth_sb_exit(void);
void xeth_sysfs_exit(void);
void xeth_vlan_exit(void);

struct net_device *xeth_iflink(int i);
int xeth_iflink_index(u16 id);
void xeth_iflink_reset(int i);

void xeth_ndo_send_vids(struct net_device *nd);

int xeth_notifier_register_fib(void);
void xeth_notifier_unregister_fib(void);

int xeth_sb_send_change_upper(int upper, int lower, bool linking);
int xeth_sb_send_ethtool_flags(struct net_device *nd);
int xeth_sb_send_ethtool_settings(struct net_device *nd);
int xeth_sb_send_ifa(struct net_device *nd, unsigned long event,
		     struct in_ifaddr *ifa);
void xeth_sb_dump_ifinfo(struct net_device *nd);
int xeth_sb_send_ifinfo(struct net_device *nd, unsigned int iff, u8 reason);
int xeth_sb_send_fib_entry(unsigned long event,
			   struct fib_notifier_info *info);
int xeth_sb_send_neigh_update(struct neighbour *neigh);

int xeth_sysfs_priv_add(struct xeth_priv *priv);
void xeth_sysfs_priv_del(struct xeth_priv *priv);

static inline void xeth_ht_init(void)
{
	int i;
	for (i = 0; i < (1<<xeth_ht_bits); i++)
		WRITE_ONCE(xeth.ht[i].first, NULL);
}

static inline struct net_device *xeth_nd_of(s32 ifindex)
{
	struct xeth_priv *priv;
	int i = hash_min(ifindex, xeth_ht_bits);
	hlist_for_each_entry_rcu(priv, &xeth.ht[i], node)
		if (priv->nd->ifindex == ifindex)
			return priv->nd;
	return NULL;
}

static inline void xeth_reset_counters(void)
{
	int i;
	for (i = 0; i < n_xeth_count; i++)
		atomic64_set(&xeth.count[i], 0);
}

static inline bool netif_is_xeth(struct net_device *nd)
{
	return	(nd->netdev_ops == &xeth_ndo_ops) &&
		(nd->ethtool_ops == &xeth_ethtool_ops);
}

struct	xeth_upper {
	struct	list_head __rcu list;
	struct	{
		s32	lower;
		s32	upper;
	} ifindex;
};

static inline struct xeth_upper *xeth_pop_upper(struct list_head __rcu *head)
{
	struct xeth_upper *p =
		list_first_or_null_rcu(head, struct xeth_upper, list);
	if (p)
		list_del_rcu(&p->list);
	return p;
}

static inline int xeth_add_upper(int upper, int lower)
{
	struct xeth_upper *p = xeth_pop_upper(&xeth.free.uppers);
	if (!p) {
		p = kzalloc(sizeof(struct xeth_upper), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
	}
	p->ifindex.upper = upper;
	p->ifindex.lower = lower;
	list_add_tail_rcu(&p->list, &xeth.uppers);
	return 0;
}

static inline struct xeth_upper *xeth_find_upper(int upper, int lower)
{
	struct xeth_upper *p;
	list_for_each_entry_rcu(p, &xeth.uppers, list)
		if (p &&
		    p->ifindex.upper == upper &&
		    p->ifindex.lower == lower)
			return p;
	return NULL;
}

static inline void xeth_free_upper(struct xeth_upper *p)
{
	p->ifindex.upper = 0;
	p->ifindex.lower = 0;
	list_del_rcu(&p->list);
	list_add_tail_rcu(&p->list, &xeth.free.uppers);
}

struct	xeth_vid {
	struct	list_head __rcu list;
	__be16	proto;
	u16	id;
};

static inline struct xeth_vid *xeth_pop_vid(struct list_head __rcu *head)
{
	struct xeth_vid *p =
		list_first_or_null_rcu(head, struct xeth_vid, list);
	if (p)
		list_del_rcu(&p->list);
	return p;
}

static inline int xeth_add_vid(struct net_device *nd, __be16 proto, u16 id)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct xeth_vid *p = xeth_pop_vid(&xeth.free.vids);
	if (!p) {
		p = kzalloc(sizeof(struct xeth_vid), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
	}
	p->proto = proto;
	p->id = id;
	list_add_tail_rcu(&p->list, &priv->vids);
	return 0;
}

static inline struct xeth_vid *xeth_find_vid(struct net_device *nd,
					     __be16 proto, u16 id)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct xeth_vid *p;
	list_for_each_entry_rcu(p, &priv->vids, list)
		if (p->proto == proto && p->id == id)
			return p;
	return NULL;
}

static inline void xeth_free_vid(struct xeth_vid *p)
{
	p->proto = 0;
	p->id = 0;
	list_del_rcu(&p->list);
	list_add_tail_rcu(&p->list, &xeth.free.vids);
}

#endif  /* __XETH_H */
