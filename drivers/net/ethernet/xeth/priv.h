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
	struct	list_head __rcu	list;
	struct	net_device	*nd;

	struct {
		struct	mutex	mutex;
		struct	rtnl_link_stats64
			stats;
	} link;

	struct {
		struct	mutex	mutex;
		struct	ethtool_link_ksettings
			settings;
		u64	*stats;
		u32	flags;
	} ethtool;

	struct	kobject	kobj;
	atomic64_t	count[n_xeth_count_priv];

	u16	id;
	s16	ndi, iflinki, porti;
	s8	subporti;
	u8	devtype;
};

#define to_xeth_priv(x)	container_of((x), struct xeth_priv, kobj)

static inline void xeth_priv_reset_counters(struct xeth_priv *priv)
{
	int i;
	for (i = 0; i < n_xeth_count_priv; i++)
		atomic64_set(&priv->count[i], 0);
}

#endif /* __XETH_PRIV_H */
