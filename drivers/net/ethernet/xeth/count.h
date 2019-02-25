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

#ifndef __XETH_COUNT_H
#define __XETH_COUNT_H

enum {
	xeth_count_rx_invalid,
	xeth_count_rx_no_dev,
	xeth_count_sb_connections,
	xeth_count_sb_invalid,
	xeth_count_sb_no_dev,
	xeth_count_sb_from_user_msgs,
	xeth_count_sb_from_user_ticks,
	xeth_count_sb_to_user_alloc,
	xeth_count_sb_to_user_free,
	xeth_count_sb_to_user_msgs,
	xeth_count_sb_to_user_no_mem,
	xeth_count_sb_to_user_retries,
	xeth_count_sb_to_user_ticks,
	n_xeth_count,
};

#define xeth_counter(name)		xeth.count[xeth_count_##name]
#define xeth_count(name)		atomic64_read(&xeth_counter(name))
#define xeth_count_add(n, name)		atomic64_add(n, &xeth_counter(name))
#define xeth_count_dec(name)		atomic64_dec(&xeth_counter(name))
#define xeth_count_inc(name)		atomic64_inc(&xeth_counter(name))
#define xeth_count_set(name, n)		atomic64_set(&xeth_counter(name), n)

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

#endif /* __XETH_COUNT_H */
