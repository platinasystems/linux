/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_MUX_H
#define __NET_ETHERNET_XETH_MUX_H

#include <linux/platform_device.h>
#include <net/rtnetlink.h>
#include <uapi/linux/xeth.h>

extern struct rtnl_link_ops xeth_mux_lnko;

struct net_device *xeth_mux_probe(struct platform_device *);

enum xeth_encap xeth_mux_encap(struct net_device *mux);

netdev_tx_t xeth_mux_encap_xmit(struct sk_buff *, struct net_device *proxy);

atomic64_t *xeth_mux_counters(struct net_device *mux);
volatile unsigned long *xeth_mux_flags(struct net_device *mux);

void xeth_mux_del_vlans(struct net_device *mux, struct net_device *nd,
			struct list_head *unregq);
void xeth_mux_dump_all_ifinfo(struct net_device *);

enum xeth_mux_counter {
	xeth_mux_counter_ex_frames,
	xeth_mux_counter_ex_bytes,
	xeth_mux_counter_sb_connections,
	xeth_mux_counter_sbex_invalid,
	xeth_mux_counter_sbex_dropped,
	xeth_mux_counter_sbrx_invalid,
	xeth_mux_counter_sbrx_no_dev,
	xeth_mux_counter_sbrx_no_mem,
	xeth_mux_counter_sbrx_msgs,
	xeth_mux_counter_sbrx_ticks,
	xeth_mux_counter_sbtx_msgs,
	xeth_mux_counter_sbtx_retries,
	xeth_mux_counter_sbtx_no_mem,
	xeth_mux_counter_sbtx_queued,
	xeth_mux_counter_sbtx_free,
	xeth_mux_counter_sbtx_ticks,
	xeth_mux_n_counters,
};

#define xeth_mux_counter_name(name)	[xeth_mux_counter_##name] = #name

#define xeth_mux_counter_names()					\
	xeth_mux_counter_name(ex_frames),				\
	xeth_mux_counter_name(ex_bytes),				\
	xeth_mux_counter_name(sb_connections),				\
	xeth_mux_counter_name(sbex_invalid),				\
	xeth_mux_counter_name(sbex_dropped),				\
	xeth_mux_counter_name(sbrx_invalid),				\
	xeth_mux_counter_name(sbrx_no_dev),				\
	xeth_mux_counter_name(sbrx_no_mem),				\
	xeth_mux_counter_name(sbrx_msgs),				\
	xeth_mux_counter_name(sbrx_ticks),				\
	xeth_mux_counter_name(sbtx_msgs),				\
	xeth_mux_counter_name(sbtx_retries),				\
	xeth_mux_counter_name(sbtx_no_mem),				\
	xeth_mux_counter_name(sbtx_queued),				\
	xeth_mux_counter_name(sbtx_free),				\
	xeth_mux_counter_name(sbtx_ticks),				\
	[xeth_mux_n_counters] = NULL

static inline void xeth_mux_counter_init(atomic64_t *t)
{
	enum xeth_mux_counter c;
	for (c = 0; c < xeth_mux_n_counters; c++, t++)
		atomic64_set(t, 0LL);
}

#define xeth_mux_counter_ops(name)					\
static inline long long							\
xeth_mux_get__##name(atomic64_t *t)					\
{									\
	return atomic64_read(&t[xeth_mux_counter_##name]);		\
}									\
static inline long long							\
xeth_mux_get_##name(struct net_device *mux)				\
{									\
	return xeth_mux_get__##name(xeth_mux_counters(mux));		\
}									\
static inline void							\
xeth_mux_add__##name(atomic64_t *t, s64 n)				\
{									\
	atomic64_add(n, &t[xeth_mux_counter_##name]);			\
}									\
static inline void							\
xeth_mux_add_##name(struct net_device *mux, s64 n)			\
{									\
	xeth_mux_add__##name(xeth_mux_counters(mux), n);		\
}									\
static inline void							\
xeth_mux_dec__##name(atomic64_t *t)					\
{									\
	atomic64_dec(&t[xeth_mux_counter_##name]);			\
}									\
static inline void							\
xeth_mux_dec_##name(struct net_device *mux)				\
{									\
	xeth_mux_dec__##name(xeth_mux_counters(mux));			\
}									\
static inline void							\
xeth_mux_inc__##name(atomic64_t *t)					\
{									\
	atomic64_inc(&t[xeth_mux_counter_##name]);			\
}									\
static inline void							\
xeth_mux_inc_##name(struct net_device *mux)				\
{									\
	xeth_mux_inc__##name(xeth_mux_counters(mux));			\
}									\
static inline void							\
xeth_mux_set__##name(atomic64_t *t, s64 n)				\
{									\
	atomic64_set(&t[xeth_mux_counter_##name], n);			\
}									\
static inline void							\
xeth_mux_set_##name(struct net_device *mux, s64 n)			\
{									\
	xeth_mux_set__##name(xeth_mux_counters(mux), n);		\
}

xeth_mux_counter_ops(ex_frames)
xeth_mux_counter_ops(ex_bytes)
xeth_mux_counter_ops(sb_connections)
xeth_mux_counter_ops(sbex_invalid)
xeth_mux_counter_ops(sbex_dropped)
xeth_mux_counter_ops(sbrx_invalid)
xeth_mux_counter_ops(sbrx_no_dev)
xeth_mux_counter_ops(sbrx_no_mem)
xeth_mux_counter_ops(sbrx_msgs)
xeth_mux_counter_ops(sbrx_ticks)
xeth_mux_counter_ops(sbtx_msgs)
xeth_mux_counter_ops(sbtx_retries)
xeth_mux_counter_ops(sbtx_no_mem)
xeth_mux_counter_ops(sbtx_queued)
xeth_mux_counter_ops(sbtx_free)
xeth_mux_counter_ops(sbtx_ticks)

enum xeth_mux_flag {
	xeth_mux_flag_main_task,
	xeth_mux_flag_sb_listen,
	xeth_mux_flag_sb_connection,
	xeth_mux_flag_sbrx_task,
	xeth_mux_flag_fib_notifier,
	xeth_mux_flag_inetaddr_notifier,
	xeth_mux_flag_inet6addr_notifier,
	xeth_mux_flag_netdevice_notifier,
	xeth_mux_flag_netevent_notifier,
	xeth_mux_n_flags,
};

#define xeth_mux_flag_name(name)	[xeth_mux_flag_##name] = #name

#define xeth_mux_flag_names()						\
	xeth_mux_flag_name(main_task),					\
	xeth_mux_flag_name(sb_listen),					\
	xeth_mux_flag_name(sb_connection),				\
	xeth_mux_flag_name(sbrx_task),					\
	xeth_mux_flag_name(fib_notifier),				\
	xeth_mux_flag_name(inetaddr_notifier),				\
	xeth_mux_flag_name(inet6addr_notifier),				\
	xeth_mux_flag_name(netdevice_notifier),				\
	xeth_mux_flag_name(netevent_notifier),				\
	[xeth_mux_n_flags] = NULL,

#define xeth_mux_flag_ops(name)						\
static inline bool xeth_mux_has__##name(volatile unsigned long *flags)	\
{									\
	bool flag;							\
	smp_mb__before_atomic();					\
	flag = variable_test_bit(xeth_mux_flag_##name, flags);		\
	smp_mb__after_atomic();						\
	return flag;							\
}									\
static inline bool xeth_mux_has_##name(struct net_device *mux)		\
{									\
	return xeth_mux_has__##name(xeth_mux_flags(mux));		\
}									\
static inline void xeth_mux_clear__##name(volatile unsigned long *flags) \
{									\
	smp_mb__before_atomic();					\
	clear_bit(xeth_mux_flag_##name, flags);				\
	smp_mb__after_atomic();						\
}									\
static inline void xeth_mux_clear_##name(struct net_device *mux)	\
{									\
	xeth_mux_clear__##name(xeth_mux_flags(mux));			\
}									\
static inline void xeth_mux_set__##name(volatile unsigned long *flags)	\
{									\
	smp_mb__before_atomic();					\
	set_bit(xeth_mux_flag_##name, flags);				\
	smp_mb__after_atomic();						\
}									\
static inline void xeth_mux_set_##name(struct net_device *mux)		\
{									\
	xeth_mux_set__##name(xeth_mux_flags(mux));			\
}									\

xeth_mux_flag_ops(main_task)
xeth_mux_flag_ops(sb_listen)
xeth_mux_flag_ops(sb_connection)
xeth_mux_flag_ops(sbrx_task)
xeth_mux_flag_ops(fib_notifier)
xeth_mux_flag_ops(inetaddr_notifier)
xeth_mux_flag_ops(inet6addr_notifier)
xeth_mux_flag_ops(netdevice_notifier)
xeth_mux_flag_ops(netevent_notifier)

#endif /* __NET_ETHERNET_XETH_MUX_H */
