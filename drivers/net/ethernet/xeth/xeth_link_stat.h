/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_LINK_STAT_H
#define __NET_ETHERNET_XETH_LINK_STAT_H

#include <linux/atomic.h>
#include <linux/if_link.h>
#include <uapi/linux/xeth.h>

static inline void xeth_link_stat_init(atomic64_t *t)
{
	enum xeth_link_stat s;
	for (s = 0; s < XETH_N_LINK_STAT; s++, t++)
		atomic64_set(t, 0LL);
}

#define xeth_link_stat_ops(NAME)					\
static inline long long xeth_get_##NAME(atomic64_t *t)			\
{									\
	return atomic64_read(&t[XETH_LINK_STAT_##NAME]);		\
}									\
static inline void xeth_add_##NAME(atomic64_t *t, s64 n)		\
{									\
	atomic64_add(n, &t[XETH_LINK_STAT_##NAME]);			\
}									\
static inline void xeth_inc_##NAME(atomic64_t *t)			\
{									\
	atomic64_inc(&t[XETH_LINK_STAT_##NAME]);			\
}									\
static inline void xeth_set_##NAME(atomic64_t *t, s64 n)		\
{									\
	atomic64_set(&t[XETH_LINK_STAT_##NAME], n);			\
}

xeth_link_stat_ops(RX_PACKETS)
xeth_link_stat_ops(TX_PACKETS)
xeth_link_stat_ops(RX_BYTES)
xeth_link_stat_ops(TX_BYTES)
xeth_link_stat_ops(RX_ERRORS)
xeth_link_stat_ops(TX_ERRORS)
xeth_link_stat_ops(RX_DROPPED)
xeth_link_stat_ops(TX_DROPPED)
xeth_link_stat_ops(MULTICAST)
xeth_link_stat_ops(COLLISIONS)
xeth_link_stat_ops(RX_LENGTH_ERRORS)
xeth_link_stat_ops(RX_OVER_ERRORS)
xeth_link_stat_ops(RX_CRC_ERRORS)
xeth_link_stat_ops(RX_FRAME_ERRORS)
xeth_link_stat_ops(RX_FIFO_ERRORS)
xeth_link_stat_ops(RX_MISSED_ERRORS)
xeth_link_stat_ops(TX_ABORTED_ERRORS)
xeth_link_stat_ops(TX_CARRIER_ERRORS)
xeth_link_stat_ops(TX_FIFO_ERRORS)
xeth_link_stat_ops(TX_HEARTBEAT_ERRORS)
xeth_link_stat_ops(TX_WINDOW_ERRORS)
xeth_link_stat_ops(RX_COMPRESSED)
xeth_link_stat_ops(TX_COMPRESSED)
xeth_link_stat_ops(RX_NOHANDLER)

static inline void xeth_link_stats(struct rtnl_link_stats64 *dst,
				   atomic64_t *src)
{
	dst->rx_packets = xeth_get_RX_PACKETS(src);
	dst->tx_packets = xeth_get_TX_PACKETS(src);
	dst->rx_bytes = xeth_get_RX_BYTES(src);
	dst->tx_bytes = xeth_get_TX_BYTES(src);
	dst->rx_errors = xeth_get_RX_ERRORS(src);
	dst->tx_errors = xeth_get_TX_ERRORS(src);
	dst->rx_dropped = xeth_get_RX_DROPPED(src);
	dst->tx_dropped = xeth_get_TX_DROPPED(src);
	dst->multicast = xeth_get_MULTICAST(src);
	dst->collisions = xeth_get_COLLISIONS(src);
	dst->rx_length_errors = xeth_get_RX_LENGTH_ERRORS(src);
	dst->rx_over_errors = xeth_get_RX_OVER_ERRORS(src);
	dst->rx_crc_errors = xeth_get_RX_CRC_ERRORS(src);
	dst->rx_frame_errors = xeth_get_RX_FRAME_ERRORS(src);
	dst->rx_fifo_errors = xeth_get_RX_FIFO_ERRORS(src);
	dst->rx_missed_errors = xeth_get_RX_MISSED_ERRORS(src);
	dst->tx_aborted_errors = xeth_get_TX_ABORTED_ERRORS(src);
	dst->tx_carrier_errors = xeth_get_TX_CARRIER_ERRORS(src);
	dst->tx_fifo_errors = xeth_get_TX_FIFO_ERRORS(src);
	dst->tx_heartbeat_errors = xeth_get_TX_HEARTBEAT_ERRORS(src);
	dst->tx_window_errors = xeth_get_TX_WINDOW_ERRORS(src);
	dst->rx_compressed = xeth_get_RX_COMPRESSED(src);
	dst->tx_compressed = xeth_get_TX_COMPRESSED(src);
	dst->rx_nohandler = xeth_get_RX_NOHANDLER(src);
}

#endif	/* __NET_ETHERNET_XETH_LINK_STAT_H */
