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

struct xeth {
	struct	list_head __rcu	privs;
	struct {
		int	base;	/* 0 or 1 base port and subport*/
		int	userids;
		size_t	ports;
		size_t	subports;
		size_t	iflinks;
		size_t	nds;
		size_t	ids;
		size_t	encap;	/* e.g. VLAN_HLEN */
		size_t	rxqs;
		size_t	txqs;
		size_t	priv_size;
		struct	xeth_sizes_ethtool {
			size_t	flags;
			size_t	stats;
		} ethtool;
	} n;
	struct {
		struct	net_device **nd;
		u16	*ndi_by_id;
		int	*provision;
	} dev;
	struct {
		const char * const *name;
		struct	net_device **nd;
		u64	*ea;
		bool	*registered;
	} iflink;
	struct {
		const char * const *flags;
		const char * const *stats;
	} ethtool;
	struct {
		struct {
			int	(*set_lladdr)(struct net_device *nd);
			void	(*init_ethtool_settings)(struct xeth_priv *priv);
			int	(*validate_speed)(struct net_device *, u32);
		} dev;
		struct {
			int	(*init)(void);
			void	(*exit)(void);
			rx_handler_result_t
				(*rx)(struct sk_buff **pskb);
			void	(*sb)(const char *buf, size_t n);
			netdev_tx_t (*tx)(struct sk_buff *skb,
					  struct net_device *nd);
		} encap;
	} ops;
	atomic64_t	count[n_xeth_count];
};

#define to_xeth(x)	container_of((x), struct xeth, kobj)

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

int xeth_notifier_register_fib(void);
void xeth_notifier_unregister_fib(void);

int xeth_sb_send_ethtool_flags(struct net_device *nd);
int xeth_sb_send_ethtool_settings(struct net_device *nd);
int xeth_sb_send_ifa(struct net_device *nd, unsigned long event,
		     struct in_ifaddr *ifa);
int xeth_sb_send_ifdel(struct net_device *nd);
int xeth_sb_send_ifinfo(struct net_device *nd, unsigned int modiff);
int xeth_sb_send_if_add_vid(struct net_device *nd, u16 vid);
int xeth_sb_send_if_del_vid(struct net_device *nd, u16 vid);
int xeth_sb_send_fibentry(unsigned long event,
			  struct fib_entry_notifier_info *info);
int xeth_sb_send_neigh_update(struct neighbour *neigh);

int xeth_sysfs_add(struct xeth_priv *priv);
void xeth_sysfs_del(struct xeth_priv *priv);

void xeth_ndo_free_vids(struct net_device *nd);
void xeth_ndo_send_vids(struct net_device *nd);

int xeth_link_register(struct net_device *nd);
int xeth_parse_name(const char *name, struct xeth_priv_ref *ref);

void xeth_iflink_reset(int i);
void xeth_iflink_set(int i, struct net_device *iflink);

static inline struct net_device *xeth_iflink_nd(int i)
{
	return i < xeth.n.iflinks ? rtnl_dereference(xeth.iflink.nd[i]) : NULL;
}

static inline struct net_device *xeth_nd(int i)
{
	return (i < xeth.n.nds) ? rtnl_dereference(xeth.dev.nd[i]) : NULL;
}

static inline void xeth_foreach_nd(void (*op)(struct net_device *nd))
{
	int i;

	for (i = 0; i < xeth.n.ids; i++) {
		struct net_device *nd = xeth_nd(i);
		if (nd != NULL)
			op(nd);
	}
}

static inline struct net_device *to_xeth_nd(u16 id)
{
	return (id < xeth.n.ids) ? xeth_nd(xeth.dev.ndi_by_id[id]) : NULL;
}

static inline void xeth_reset_nd(int i)
{
	if (0 <= i && i < xeth.n.nds)
		RCU_INIT_POINTER(xeth.dev.nd[i], NULL);
}

static inline void xeth_set_nd(int i, struct net_device *nd)
{
	if (0 <= i && i < xeth.n.nds)
		rcu_assign_pointer(xeth.dev.nd[i], nd);
}

static inline void xeth_reset_counters(void)
{
	int i;
	for (i = 0; i < n_xeth_count; i++)
		atomic64_set(&xeth.count[i], 0);
}

#endif /* __XETH_H */
