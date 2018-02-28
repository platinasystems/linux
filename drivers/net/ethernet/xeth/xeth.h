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

struct	xeth_priv {
	struct	xeth_priv_mutex {
		struct mutex stats;
	} mutex;
	struct	rtnl_link_stats64 stats;
	struct	kobject kobj;
	int	kobj_err;
	u16	id, ndi, iflinki;
};

#define to_xeth_priv(kobjp)						\
	container_of(kobjp, struct xeth_priv, kobj)

struct xeth {
	struct	xeth_ops {
		int (*assert_iflinks)(void);
		int (*parse_name)(struct xeth_priv *priv, const char *name);
		int (*set_lladdr)(struct net_device *nd);
		rx_handler_result_t (*rx_handler)(struct sk_buff **pskb);
		ssize_t (*side_band_rx)(struct sk_buff *skb);
		void (*destructor)(struct net_device *nd);
		struct rtnl_link_ops rtnl;
		struct net_device_ops ndo;
	} ops;

	struct notifier_block notifier;

	struct	xeth_sizes {
		size_t	iflinks;
		size_t	nds;
		size_t	ids;
		size_t	encap;	/* e.g. VLAN_HLEN */
	} n;

	/* RCU protected pointers */
	struct	net_device **iflinks;
	struct	net_device **nds;

	u16 *ndi_by_id;

	struct	xeth_sysfs {
		struct device *root;
	} sysfs;
};

extern struct xeth xeth;
int xeth_init(void);
void xeth_exit(void);
int xeth_link_init(const char *kind);
void xeth_link_exit(void);
void xeth_ndo_init(void);
void xeth_ndo_exit(void);
void xeth_notifier_init(void);
void xeth_notifier_exit(void);
void xeth_vlan_init(void);
void xeth_vlan_exit(void);
void xeth_sysfs_init(const char *name);
void xeth_sysfs_exit(void);
void xeth_sysfs_priv_init(struct xeth_priv *priv, const char *ifname);
void xeth_sysfs_priv_exit(struct xeth_priv *priv);
void xeth_devfs_init(const char *name);
void xeth_devfs_exit(void);

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

#endif /* __XETH_H */
