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
	struct	list_head __rcu	list;
	struct {
		int	(*assert_iflinks)(void);
		int	(*parse)(const char *name, struct xeth_priv *priv);
		int	(*set_lladdr)(struct net_device *nd);
		rx_handler_result_t	(*rx_handler)(struct sk_buff **pskb);
		void	(*side_band_rx)(const char *buf, size_t n);
		void	(*destructor)(struct net_device *nd);
		void	(*init_ethtool_settings)(struct net_device *nd);
		int	(*validate_speed)(struct net_device *nd, u32 speed);
		struct	rtnl_link_ops rtnl;
		struct	net_device_ops ndo;
		struct	ethtool_ops ethtool;
	} ops;

	struct {
		const char * const *stats;
		const char * const *flags;
	} ethtool;

	atomic64_t	count[n_xeth_count];

	struct {
		size_t	iflinks;
		size_t	nds;
		size_t	ids;
		size_t	encap;	/* e.g. VLAN_HLEN */
		struct	xeth_sizes_ethtool {
			size_t	stats;
			size_t	flags;
		} ethtool;
	} n;

	u16	*ndi_by_id;
	u64	*ea_iflinks;

	/* RCU protected pointers */
	struct	net_device
		**iflinks, **nds;

	struct {
		struct	list_head __rcu
			tx;
		struct	task_struct
			*main;
		char	*rxbuf;
	} sb;
};

#define to_xeth(x)	container_of((x), struct xeth, kobj)

extern struct xeth xeth;

int xeth_init(void);
int xeth_ethtool_init(void);
int xeth_link_init(void);
int xeth_ndo_init(void);
int xeth_notifier_init(void);
int xeth_sb_init(void);
int xeth_vlan_init(void);

void xeth_exit(void);
void xeth_ethtool_exit(void);
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

static inline struct net_device *xeth_iflink(int i)
{
	return (i < xeth.n.iflinks) ? rtnl_dereference(xeth.iflinks[i]) : NULL;
}

static inline void xeth_reset_iflink(int i)
{
	if (i < xeth.n.iflinks) {
		RCU_INIT_POINTER(xeth.iflinks[i], NULL);
		xeth.ea_iflinks[i] = 0;
	}
}

static inline void xeth_set_iflink(int i, struct net_device *iflink)
{
	if (i < xeth.n.iflinks) {
		rcu_assign_pointer(xeth.iflinks[i], iflink);
		xeth.ea_iflinks[i] = ether_addr_to_u64(iflink->dev_addr);
	}
}

static inline struct net_device *xeth_nd(int i)
{
	return (i < xeth.n.nds) ? rtnl_dereference(xeth.nds[i]) : NULL;
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
	return (id < xeth.n.ids) ? xeth_nd(xeth.ndi_by_id[id]) : NULL;
}

static inline void xeth_reset_nd(int i)
{
	if (0 <= i && i < xeth.n.nds)
		RCU_INIT_POINTER(xeth.nds[i], NULL);
}

static inline void xeth_set_nd(int i, struct net_device *nd)
{
	if (0 <= i && i < xeth.n.nds)
		rcu_assign_pointer(xeth.nds[i], nd);
}

static inline void xeth_reset_counters(void)
{
	int i;
	for (i = 0; i < n_xeth_count; i++)
		atomic64_set(&xeth.count[i], 0);
}

#endif /* __XETH_H */
