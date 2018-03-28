/* An XETH driver uses VLAN or MPLS encapsulations to multiplex virtual
 * ethernet devices through one or more iflinks to a switch asic. Such drivers
 * also have a companion control daemon (aka. switchd) that may use sysfs or a
 * netlink protocol for sideband driver communication and  may also use an
 * alternate link, data encapsulation or vfio/pci for asic information.
 *
 * A typlical use case:
 *
 * switchd
 *  |   |
 *  |   |
 *  |   |   virtuals  e.g. eth-[CHASSIS-][SLOT-]PORT-SUBPORT
 *  |   |   | .... |
 *  |   |<->  xeth
 *  |          ||
 *  |          ||     iflinks
 *  |          ||
 *  |<----->  asic    (on-board or remote)
 *          | .... |
 *        switch ports
 *
 *
 * Copyright(c) 2018 Platina Systems, Inc.
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

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <net/rtnetlink.h>

#ifndef XETH_VERSION
#define XETH_VERSION unknown
#endif

struct	xeth_priv {
	struct	mutex link_mutex;
	struct	mutex ethtool_mutex;
	struct	rtnl_link_stats64 link_stats;
	u64	*ethtool_stats;
	u16	id, ndi, iflinki;
};

struct xeth {
	struct	xeth_ops {
		int (*assert_iflinks)(void);
		int (*parse_name)(const char *name,
				  u16 *id, u16 *ndi, u16 *iflinki);
		int (*set_lladdr)(struct net_device *nd);
		rx_handler_result_t (*rx_handler)(struct sk_buff **pskb);
		ssize_t (*side_band_rx)(struct sk_buff *skb);
		void (*destructor)(struct net_device *nd);
		struct rtnl_link_ops rtnl;
		struct net_device_ops ndo;
		struct ethtool_ops ethtool;
	} ops;

	struct	mutex sb_mutex;
	struct	notifier_block notifier;
	const	char * const *ethtool_stats;

	struct	xeth_sizes {
		size_t	iflinks;
		size_t	nds;
		size_t	ids;
		size_t	encap;	/* e.g. VLAN_HLEN */
		size_t	ethtool_stats;
	} n;

	u16	*ndi_by_id;

	/* RCU protected pointers */
	struct	net_device **iflinks;
	struct	net_device **nds;
	struct	socket *sb;
};

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

static inline struct net_device *xeth_iflinks(int i)
{
	return (i < xeth.n.iflinks) ? rtnl_dereference(xeth.iflinks[i]) : NULL;
}

static inline void xeth_reset_iflinks(int i)
{
	if (i < xeth.n.iflinks)
		RCU_INIT_POINTER(xeth.iflinks[i], NULL);
}

static inline void xeth_set_iflinks(int i, struct net_device *iflink)
{
	if (i < xeth.n.iflinks)
		rcu_assign_pointer(xeth.iflinks[i], iflink);
}

static inline struct net_device *xeth_nds(int i)
{
	return (i < xeth.n.nds) ? rtnl_dereference(xeth.nds[i]) : NULL;
}

static inline struct net_device *xeth_find_nd(u64 ifindex)
{
	int i;

	for (i = 0; i < xeth.n.ids; i++) {
		struct net_device *nd = xeth_nds(i);
		if (nd != NULL && nd->ifindex == (int)ifindex)
			return nd;
	}
	return NULL;
}

static inline struct net_device *to_xeth_nd(u16 id)
{
	return (id < xeth.n.ids) ? xeth_nds(xeth.ndi_by_id[id]) : NULL;
}

static inline void xeth_reset_nds(int i)
{
	if (i < xeth.n.iflinks)
		RCU_INIT_POINTER(xeth.nds[i], NULL);
}

static inline void xeth_set_nds(int i, struct net_device *nd)
{
	if (i < xeth.n.nds)
		rcu_assign_pointer(xeth.nds[i], nd);
}

static inline struct net_device *xeth_priv_iflink(struct xeth_priv *priv)
{
	return xeth_iflinks(priv->iflinki);
}

static inline void xeth_priv_set_nd(struct xeth_priv *priv,
				    struct net_device *nd)
{
	xeth_set_nds(priv->ndi, nd);
}

static inline void xeth_reset_sb(void)
{
	RCU_INIT_POINTER(xeth.sb, NULL);
}

static inline void xeth_assign_sb(struct socket *sb)
{
	mutex_lock(&xeth.sb_mutex);
	rcu_assign_pointer(xeth.sb, sb);
	mutex_unlock(&xeth.sb_mutex);
}

static inline struct socket *xeth_dereference_sb(void)
{
	struct socket *sb;
	mutex_lock(&xeth.sb_mutex);
	sb = rcu_dereference(xeth.sb);
	mutex_unlock(&xeth.sb_mutex);
	return sb;
}
#endif /* __XETH_H */
