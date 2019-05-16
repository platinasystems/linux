/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_H
#define __NET_ETHERNET_XETH_H

#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/etherdevice.h>
#include <linux/hashtable.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <linux/platform_device.h>
#include <linux/if_vlan.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <net/sock.h>
#include <net/rtnetlink.h>
#include <net/ip_fib.h>
#include <net/netevent.h>
#include <uapi/linux/un.h>
#include <uapi/linux/xeth.h>

#if defined(KBUILD_MODNAME)
# define xeth_name KBUILD_MODNAME
#else	/* KBUILD_MODNAME */
# define xeth_name "xeth"
#endif	/* KBUILD_MODNAME */

#define xeth_version "2.0"

#include <xeth_debug.h>
#include <xeth_kstrs.h>

extern int xeth_encap;
extern int xeth_base_xid;

enum xeth_encap {
	XETH_ENCAP_VLAN = 0,
};

enum xeth_counter {
	xeth_counter_sb_connections,
	xeth_counter_sbrx_invalid,
	xeth_counter_sbrx_no_dev,
	xeth_counter_sbrx_no_mem,
	xeth_counter_sbrx_msgs,
	xeth_counter_sbrx_ticks,
	xeth_counter_sbtx_msgs,
	xeth_counter_sbtx_retries,
	xeth_counter_sbtx_no_mem,
	xeth_counter_sbtx_queued,
	xeth_counter_sbtx_ticks,
	xeth_counters,
};

#define xeth_counter(name)						\
({									\
	long long _ll = xeth_mux_counter(xeth_counter_##name);		\
	(_ll);								\
})

#define xeth_counter_add(name,count)					\
do {									\
	xeth_mux_counter_add(xeth_counter_##name, count);		\
} while(0)

#define xeth_counter_dec(name)						\
do {									\
	xeth_mux_counter_dec(xeth_counter_##name);			\
} while(0)

#define xeth_counter_inc(name)						\
do {									\
	xeth_mux_counter_inc(xeth_counter_##name);			\
} while(0)

#define xeth_counter_set(name,count)					\
do {									\
	xeth_mux_counter_set(xeth_counter_##name, count);		\
} while(0)

enum xeth_flag {
	xeth_flag_fib_notifier,
	xeth_flag_inetaddr_notifier,
	xeth_flag_netdevice_notifier,
	xeth_flag_netevent_notifier,
	xeth_flag_sb_task,
	xeth_flag_sb_listen,
	xeth_flag_sb_connected,
	xeth_flag_sbrx_task,
	xeth_flags,
};

#define xeth_flag(name)							\
({									\
	bool _t = xeth_mux_flag(xeth_flag_##name);			\
	(_t);								\
})

#define xeth_flag_clear(name)						\
do {									\
	xeth_mux_flag_clear(xeth_flag_##name);				\
} while(0)

#define xeth_flag_set(name)						\
do {									\
	xeth_mux_flag_set(xeth_flag_##name);				\
} while(0)

static inline void *xeth_netdev(const void *priv)
{
	const size_t offset = ALIGN(sizeof(struct net_device), NETDEV_ALIGN);
	return priv ? (char *)priv - offset : NULL;
}

static inline void xeth_kobject_put(struct kobject *kobj)
{
	if (kobj->parent && kobj->state_initialized)
		kobject_put(kobj);
}

int xeth_mux_init(void);
int xeth_sbrx_init(void);
int xeth_upper_init(void);

int xeth_mux_deinit(int err);
int xeth_sbrx_deinit(int err);
int xeth_upper_deinit(int err);

u8 xeth_mux_bits(void);

void xeth_mux_lock(void);
void xeth_mux_unlock(void);
int xeth_mux_is_locked(void);

long long xeth_mux_counter(enum xeth_counter cnt);
void xeth_mux_counter_add(enum xeth_counter cnt, s64 n);
void xeth_mux_counter_dec(enum xeth_counter cnt);
void xeth_mux_counter_inc(enum xeth_counter cnt);
void xeth_mux_counter_set(enum xeth_counter cnt, s64 n);

void xeth_mux_exception(const char *buf, size_t n);

bool xeth_mux_flag(enum xeth_flag bit);
void xeth_mux_flag_clear(enum xeth_flag bit);
void xeth_mux_flag_set(enum xeth_flag bit);

int xeth_mux_ifindex(void);
bool xeth_mux_is_lower(struct net_device *nd);
bool xeth_mux_is_registered(void);
struct kobject *xeth_mux_kobj(void);
void xeth_mux_reload_lowers(void);
int xeth_mux_queue_xmit(struct sk_buff *skb);
struct hlist_head __rcu *xeth_mux_upper_head_hashed(u32 xid);
struct hlist_head __rcu *xeth_mux_upper_head_indexed(u32 bkt);

int xeth_nb_start_fib(void);
int xeth_nb_start_inetaddr(void);
int xeth_nb_start_netdevice(void);
int xeth_nb_start_netevent(void);

void xeth_nb_stop_fib(void);
void xeth_nb_stop_inetaddr(void);
void xeth_nb_stop_netdevice(void);
void xeth_nb_stop_netevent(void);

struct task_struct __init *xeth_sb_start(void);

struct task_struct *xeth_sbrx_fork(struct socket *conn);

int xeth_sbtx_service(struct socket *conn);
int xeth_sbtx_break(void);
int xeth_sbtx_change_upper(u64 upper, u64 lower, bool linking);
int xeth_sbtx_ethtool_flags(u64 xid, u32 flags);
int xeth_sbtx_ethtool_settings(u64 xid, struct ethtool_link_ksettings *);
int xeth_sbtx_fib_entry(unsigned long event, struct fib_notifier_info *info);
int xeth_sbtx_ifa(struct in_ifaddr *ifa, u64 xid, unsigned long event);
int xeth_sbtx_ifinfo(struct net_device *nd, u64 xid, enum xeth_dev_kind kind,
		     unsigned iff, u8 reason);
int xeth_sbtx_neigh_update(struct neighbour *neigh);


struct net_device *xeth_upper_lookup_rcu(u32 xid);
void xeth_upper_all_carrier_off(void);
void xeth_upper_all_dump_ifinfo(void);
void xeth_upper_all_reset_stats(void);
void xeth_upper_changemtu(int mtu, int max_mtu);
bool xeth_upper_check(struct net_device *nd);
void xeth_upper_ethtool_stat(struct net_device *nd, u32 index, u64 count);
void xeth_upper_link_stat(struct net_device *nd, u32 index, u64 count);
void xeth_upper_queue_unregister(struct hlist_head __rcu *head,
				 struct list_head *q);
void xeth_upper_speed(struct net_device *nd, u32 mbps);
u32 xeth_upper_xid(struct net_device *nd);
enum xeth_dev_kind xeth_upper_kind(struct net_device *nd);

#endif  /* __NET_ETHERNET_XETH_H */
