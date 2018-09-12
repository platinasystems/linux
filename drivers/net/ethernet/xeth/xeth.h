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

#ifndef __XETH_H
#define __XETH_H

#define XETH_KIND "xeth"

#include <count.h>
#include <pr.h>
#include <priv.h>

#include <linux/etherdevice.h>
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
	struct list_head  __rcu	free_vids;
	int	base;		/*  0 or 1 based port and subport */
	int	*provision;	/* list of 1, 2, or 4 subports per port */
	const char * const *iflinks;	/* a NULL terminated ifname list */
	const size_t	ports, rxqs, txqs;
	const size_t	priv_size;
	struct {
		size_t	size;	/* e.g. VLAN_HLEN */
		int	(*init)(void);
		void	(*exit)(void);
		int	(*id)(struct net_device *nd);
		int	(*new_dev)(struct net_device *nd);
		int	(*associate_dev)(struct net_device *nd);
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
			const size_t flags, stats;
		} n;
		const char * const *flags;
		const char * const *stats;
	} ethtool;
	void	(*init_ethtool_settings)(struct xeth_priv *priv);
	int	(*validate_speed)(struct net_device *, u32);
	atomic64_t	count[n_xeth_count];
};

#define xeth_for_each_iflink(index)	\
	for ((index) = 0; xeth.iflinks[(index)]; (index)++)

extern struct xeth xeth;
extern struct rtnl_link_ops xeth_link_ops;
extern struct net_device_ops xeth_ndo_ops;
extern struct ethtool_ops xeth_ethtool_ops;

int xeth_init(void);
int xeth_dev_init(void);
int xeth_ethtool_init(void);
int xeth_iflink_init(void);
int xeth_link_init(void);
int xeth_ndo_init(void);
int xeth_notifier_init(void);
int xeth_sb_init(void);
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
void xeth_vlan_exit(void);

struct net_device *xeth_iflink(int i);
int xeth_iflink_index(u16 id);
void xeth_iflink_reset(int i);

void xeth_link_setup(struct net_device *nd);

void xeth_ndo_send_vids(struct net_device *nd);

int xeth_notifier_register_fib(void);
void xeth_notifier_unregister_fib(void);

int xeth_sb_send_ethtool_flags(struct net_device *nd);
int xeth_sb_send_ethtool_settings(struct net_device *nd);
int xeth_sb_send_ifa(struct net_device *nd, unsigned long event,
		     struct in_ifaddr *ifa);
void xeth_sb_dump_ifinfo(struct net_device *nd);
int xeth_sb_send_ifinfo(struct net_device *nd, unsigned int iff, u8 reason);
int xeth_sb_send_fibentry(unsigned long event,
			  struct fib_entry_notifier_info *info);
int xeth_sb_send_neigh_update(struct neighbour *neigh);

int xeth_sysfs_add(struct xeth_priv *priv);

void xeth_sysfs_del(struct xeth_priv *priv);

static inline void xeth_ht_init(void)
{
	int i;
	for (i = 0; i < (1<<xeth_ht_bits); i++)
		WRITE_ONCE(xeth.ht[i].first, NULL);
}

static inline int xeth_ht_key(const char *ifname)
{
	return full_name_hash(&xeth, ifname, strnlen(ifname, IFNAMSIZ));
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
	return	(nd->rtnl_link_ops == &xeth_link_ops) &&
		(nd->netdev_ops == &xeth_ndo_ops) &&
		(nd->ethtool_ops == &xeth_ethtool_ops);
}

struct	xeth_vid {
	struct	list_head __rcu list;
	__be16	proto;
	u16	id;
};

static inline struct xeth_vid *xeth_vid_pop(struct list_head __rcu *head)
{
	struct xeth_vid *vid =
		list_first_or_null_rcu(head, struct xeth_vid, list);
	if (vid)
		list_del_rcu(&vid->list);
	return vid;
}

#endif  /* __XETH_H */
