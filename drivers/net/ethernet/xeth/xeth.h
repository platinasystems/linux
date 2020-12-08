/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_H
#define __NET_ETHERNET_XETH_H

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <linux/etherdevice.h>
#include <linux/hashtable.h>
#include <linux/i2c.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <net/sock.h>
#include <net/rtnetlink.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/netevent.h>
#include <uapi/linux/un.h>
#include <uapi/linux/xeth.h>
#include <net/addrconf.h>
#include <linux/onie.h>

#if defined(KBUILD_MODNAME)
# define xeth_name KBUILD_MODNAME
#else	/* KBUILD_MODNAME */
# define xeth_name "xeth"
#endif	/* KBUILD_MODNAME */

#define xeth_version "3.0"

#include <xeth_debug.h>

enum {
	xeth_max_et_flags = 32,
	xeth_max_et_stats = 512,
	xeth_drvr_kind_sz = 32,
	xeth_et_stat_names_sz = xeth_max_et_stats * ETH_GSTRING_LEN,
	xeth_mux_upper_hash_bits = 4,
	xeth_mux_upper_hash_bkts = 1 << xeth_mux_upper_hash_bits,
	xeth_mux_lower_hash_bits = 4,
	xeth_mux_lower_hash_bkts = 1 << xeth_mux_lower_hash_bits,
};

enum xeth_encap {
	XETH_ENCAP_VLAN = 0,
};

enum xeth_link_stat_index {
	xeth_link_stat_rx_packets_index,
	xeth_link_stat_tx_packets_index,
	xeth_link_stat_rx_bytes_index,
	xeth_link_stat_tx_bytes_index,
	xeth_link_stat_rx_errors_index,
	xeth_link_stat_tx_errors_index,
	xeth_link_stat_rx_dropped_index,
	xeth_link_stat_tx_dropped_index,
	xeth_link_stat_multicast_index,
	xeth_link_stat_collisions_index,
	xeth_link_stat_rx_length_errors_index,
	xeth_link_stat_rx_over_errors_index,
	xeth_link_stat_rx_crc_errors_index,
	xeth_link_stat_rx_frame_errors_index,
	xeth_link_stat_rx_fifo_errors_index,
	xeth_link_stat_rx_missed_errors_index,
	xeth_link_stat_tx_aborted_errors_index,
	xeth_link_stat_tx_carrier_errors_index,
	xeth_link_stat_tx_fifo_errors_index,
	xeth_link_stat_tx_heartbeat_errors_index,
	xeth_link_stat_tx_window_errors_index,
	xeth_link_stat_rx_compressed_index,
	xeth_link_stat_tx_compressed_index,
	xeth_link_stat_rx_nohandler_index,
};

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
	xeth_mux_counter_sbtx_ticks,
	xeth_mux_n_counters,
};

enum xeth_mux_priv_flag {
	xeth_mux_priv_flag_main_task,
	xeth_mux_priv_flag_sb_listen,
	xeth_mux_priv_flag_sb_connected,
	xeth_mux_priv_flag_sbrx_task,
	xeth_mux_priv_flag_fib_notifier,
	xeth_mux_priv_flag_inetaddr_notifier,
	xeth_mux_priv_flag_inet6addr_notifier,
	xeth_mux_priv_flag_netdevice_notifier,
	xeth_mux_priv_flag_netevent_notifier,
	xeth_mux_n_priv_flags,
};

struct xeth_atomic_link_stats {
	atomic64_t	rx_packets;
	atomic64_t	tx_packets;
	atomic64_t	rx_bytes;
	atomic64_t	tx_bytes;
	atomic64_t	rx_errors;
	atomic64_t	tx_errors;
	atomic64_t	rx_dropped;
	atomic64_t	tx_dropped;
	atomic64_t	multicast;
	atomic64_t	collisions;
	atomic64_t	rx_length_errors;
	atomic64_t	rx_over_errors;
	atomic64_t	rx_crc_errors;
	atomic64_t	rx_frame_errors;
	atomic64_t	rx_fifo_errors;
	atomic64_t	rx_missed_errors;
	atomic64_t	tx_aborted_errors;
	atomic64_t	tx_carrier_errors;
	atomic64_t	tx_fifo_errors;
	atomic64_t	tx_heartbeat_errors;
	atomic64_t	tx_window_errors;
	atomic64_t	rx_compressed;
	atomic64_t	tx_compressed;
	atomic64_t	rx_nohandler;
};

struct xeth_config;

struct xeth_platform_priv {
	struct platform_device *pdev;
	const struct xeth_config *config;
	void *priv;
	struct task_struct *main;
	struct {
		struct spinlock mutex;
		struct net_device *nd;
		/* @lowers is a NULL terminated list of link devices */
		struct net_device **lowers;
		struct hlist_head __rcu upper_hl[xeth_mux_upper_hash_bkts];
		struct net_device *lower_ht[xeth_mux_lower_hash_bkts];
		atomic64_t counter[xeth_mux_n_counters];
		volatile long unsigned int flags;
		struct xeth_atomic_link_stats link_stats;
	} mux;
	struct {
		struct socket *conn;
		struct {
			void *buf;
		} rx;
		struct {
			struct spinlock	mutex;
			struct list_head head;
		} tx;
	} sb;
	struct notifier_block
		fib_nb,
		inetaddr_nb,
		inet6addr_nb,
		netdevice_nb,
		netevent_nb;
	struct rtnl_link_ops
		vlan_lnko,
		bridge_lnko,
		lag_lnko;
	char	vlan_kind[xeth_drvr_kind_sz],
		bridge_kind[xeth_drvr_kind_sz],
		lag_kind[xeth_drvr_kind_sz],
		qsfp_kind[xeth_drvr_kind_sz];
	struct i2c_driver qsfp_driver;
	char *et_stat_names;
	/* assign random mac if @base_mac is zero */
	u64 base_mac;
	u16 n_et_stats;
	u16 n_macs;
};

#define xeth_platform_priv_of_nb(ptr, name)				\
	container_of(ptr, struct xeth_platform_priv, name##_nb)

#define xeth_platform_priv_of_lnko(ptr, name)				\
	container_of(ptr, struct xeth_platform_priv, name##_lnko)

#define xeth_platform_priv_of_qsfp_driver(ptr)				\
	container_of(ptr, struct xeth_platform_priv, qsfp_driver)

struct xeth_config {
	const char *name;
	int (*setup)(struct xeth_platform_priv *);
	void (*port_label)(struct xeth_platform_priv *, char *, u16);
	void (*port_setup)(struct ethtool_link_ksettings *);
	void (*subport_setup)(struct ethtool_link_ksettings *);
	size_t (*provision)(u16);
	/* @et_flag_names is a NULL terminated list */
	const char * const *et_flag_names;
	/* @qsfp_bus is a -1 terminated list */
	const int *qsfp_bus;
	const unsigned short *qsfp_i2c_address_list;
	u16 base_xid, top_xid, max_et_stats, n_ports;
	u8 n_mux_bits, n_rxqs, n_txqs, n_et_flags, n_lag_ports;
	enum xeth_encap encap;
};

static inline long long _xeth_counter(struct xeth_platform_priv *xpp,
				      enum xeth_mux_counter index)
{
	return atomic64_read(&xpp->mux.counter[index]);
}

static inline void _xeth_counter_add(struct xeth_platform_priv *xpp,
				     enum xeth_mux_counter index, s64 n)
{
	atomic64_add(n, &xpp->mux.counter[index]);
}

static inline void _xeth_counter_dec(struct xeth_platform_priv *xpp,
				     enum xeth_mux_counter index)
{
	atomic64_dec(&xpp->mux.counter[index]);
}

static inline void _xeth_counter_inc(struct xeth_platform_priv *xpp,
				     enum xeth_mux_counter index)
{
	atomic64_inc(&xpp->mux.counter[index]);
}

static inline void _xeth_counter_set(struct xeth_platform_priv *xpp,
				     enum xeth_mux_counter index, s64 n)
{
	atomic64_set(&xpp->mux.counter[index], n);
}

#define xeth_counter(xpp,name)						\
({									\
	long long _ll = _xeth_counter(xpp, xeth_mux_counter_##name);	\
	(_ll);								\
})

#define xeth_counter_add(xpp,name,count)				\
do {									\
	_xeth_counter_add(xpp, xeth_mux_counter_##name, count);		\
} while(0)

#define xeth_counter_dec(xpp,name)					\
do {									\
	_xeth_counter_dec(xpp, xeth_mux_counter_##name);		\
} while(0)

#define xeth_counter_inc(xpp,name)					\
do {									\
	_xeth_counter_inc(xpp, xeth_mux_counter_##name);		\
} while(0)

#define xeth_counter_set(xpp,name,count)				\
do {									\
	_xeth_counter_set(xpp, xeth_mux_counter_##name, count);		\
} while(0)

static inline u32 xeth_mux_priv_flags(struct xeth_platform_priv *xpp)
{
	u32 flags;
	barrier();
	flags = xpp->mux.flags;
	return flags;
}

static inline bool _xeth_mux_priv_flag(struct xeth_platform_priv *xpp,
			      enum xeth_mux_priv_flag bit)
{
	bool flag;

	smp_mb__before_atomic();
	flag = variable_test_bit(bit, &xpp->mux.flags);
	smp_mb__after_atomic();
	return flag;
}

static inline void _xeth_mux_priv_flag_clear(struct xeth_platform_priv *xpp,
				    enum xeth_mux_priv_flag bit)
{
	smp_mb__before_atomic();
	clear_bit(bit, &xpp->mux.flags);
	smp_mb__after_atomic();
}

static inline void _xeth_mux_priv_flag_set(struct xeth_platform_priv *xpp,
				  enum xeth_mux_priv_flag bit)
{
	smp_mb__before_atomic();
	set_bit(bit, &xpp->mux.flags);
	smp_mb__after_atomic();
}

#define xeth_flag(xpp,name)						\
({									\
	bool _t = _xeth_mux_priv_flag(xpp, xeth_mux_priv_flag_##name);	\
	(_t);								\
})

#define xeth_flag_clear(xpp,name)					\
do {									\
	_xeth_mux_priv_flag_clear(xpp, xeth_mux_priv_flag_##name);	\
} while(0)

#define xeth_flag_set(xpp,name)						\
do {									\
	_xeth_mux_priv_flag_set(xpp, xeth_mux_priv_flag_##name);	\
} while(0)

#define xeth_supports(ks, mk)						\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, supported, mk));	\
})

#define xeth_advertising(ks, mk)					\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, advertising, mk));	\
})


static inline void xeth_reset_link_stats(struct xeth_atomic_link_stats *ls)
{
	atomic64_set(&ls->rx_packets, 0LL);
	atomic64_set(&ls->tx_packets, 0LL);
	atomic64_set(&ls->rx_bytes, 0LL);
	atomic64_set(&ls->tx_bytes, 0LL);
	atomic64_set(&ls->rx_errors, 0LL);
	atomic64_set(&ls->tx_errors, 0LL);
	atomic64_set(&ls->rx_dropped, 0LL);
	atomic64_set(&ls->tx_dropped, 0LL);
	atomic64_set(&ls->multicast, 0LL);
	atomic64_set(&ls->collisions, 0LL);
	atomic64_set(&ls->rx_length_errors, 0LL);
	atomic64_set(&ls->rx_over_errors, 0LL);
	atomic64_set(&ls->rx_crc_errors, 0LL);
	atomic64_set(&ls->rx_frame_errors, 0LL);
	atomic64_set(&ls->rx_fifo_errors, 0LL);
	atomic64_set(&ls->rx_missed_errors, 0LL);
	atomic64_set(&ls->tx_aborted_errors, 0LL);
	atomic64_set(&ls->tx_carrier_errors, 0LL);
	atomic64_set(&ls->tx_fifo_errors, 0LL);
	atomic64_set(&ls->tx_heartbeat_errors, 0LL);
	atomic64_set(&ls->tx_window_errors, 0LL);
	atomic64_set(&ls->rx_compressed, 0LL);
	atomic64_set(&ls->tx_compressed, 0LL);
	atomic64_set(&ls->rx_nohandler, 0LL);
}

static inline void xeth_get_link_stats(struct rtnl_link_stats64 *dst,
				       struct xeth_atomic_link_stats *src)
{
	dst->rx_packets = atomic64_read(&src->rx_packets);
	dst->tx_packets = atomic64_read(&src->tx_packets);
	dst->rx_bytes = atomic64_read(&src->rx_bytes);
	dst->tx_bytes = atomic64_read(&src->tx_bytes);
	dst->rx_errors = atomic64_read(&src->rx_errors);
	dst->tx_errors = atomic64_read(&src->tx_errors);
	dst->rx_dropped = atomic64_read(&src->rx_dropped);
	dst->tx_dropped = atomic64_read(&src->tx_dropped);
	dst->multicast = atomic64_read(&src->multicast);
	dst->collisions = atomic64_read(&src->collisions);
	dst->rx_length_errors = atomic64_read(&src->rx_length_errors);
	dst->rx_over_errors = atomic64_read(&src->rx_over_errors);
	dst->rx_crc_errors = atomic64_read(&src->rx_crc_errors);
	dst->rx_frame_errors = atomic64_read(&src->rx_frame_errors);
	dst->rx_fifo_errors = atomic64_read(&src->rx_fifo_errors);
	dst->rx_missed_errors = atomic64_read(&src->rx_missed_errors);
	dst->tx_aborted_errors = atomic64_read(&src->tx_aborted_errors);
	dst->tx_carrier_errors = atomic64_read(&src->tx_carrier_errors);
	dst->tx_fifo_errors = atomic64_read(&src->tx_fifo_errors);
	dst->tx_heartbeat_errors = atomic64_read(&src->tx_heartbeat_errors);
	dst->tx_window_errors = atomic64_read(&src->tx_window_errors);
	dst->rx_compressed = atomic64_read(&src->rx_compressed);
	dst->tx_compressed = atomic64_read(&src->tx_compressed);
	dst->rx_nohandler = atomic64_read(&src->rx_nohandler);
}

int xeth_nb_fib(struct notifier_block *, unsigned long, void *);
int xeth_nb_inetaddr(struct notifier_block *, unsigned long, void *);
int xeth_nb_inet6addr(struct notifier_block *, unsigned long, void *);
int xeth_nb_netdevice(struct notifier_block *, unsigned long, void *);
int xeth_nb_netevent(struct notifier_block *, unsigned long event, void *);

#define xeth_start_notifier(xpp,name)					\
({									\
	int _err = 0;							\
	xpp->name##_nb.notifier_call = xeth_nb_##name;			\
	if (!xeth_flag(xpp, name##_notifier))				\
		_err = register_##name##_notifier(&xpp->name##_nb);	\
	if (!_err)							\
		xeth_flag_set(xpp, name##_notifier);			\
	(_err);								\
})

static inline void xeth_fib_notifier_cb(struct notifier_block *nb)
{
	xeth_debug("register_fib_cb");
}

static inline int xeth_start_fib_notifier(struct xeth_platform_priv *xpp)
{
	int err = 0;
	xpp->fib_nb.notifier_call = xeth_nb_fib;
	if (!xeth_flag(xpp, fib_notifier))
		err = register_fib_notifier(&xpp->fib_nb,
					    xeth_fib_notifier_cb);
	if (!err)
		xeth_flag_set(xpp, fib_notifier);
	return err;
}

#define xeth_stop_notifier(xpp,name)					\
do {									\
	if (xeth_flag(xpp, name##_notifier)) {				\
		unregister_##name##_notifier(&xpp->name##_nb);		\
		xeth_flag_clear(xpp, name##_notifier);			\
	}								\
} while(0)

static inline void xeth_add_node(struct xeth_platform_priv *xpp,
				 struct hlist_node __rcu *node,
				 struct hlist_head __rcu *head)
{
	spin_lock(&xpp->mux.mutex);
	hlist_add_head_rcu(node, head);
	spin_unlock(&xpp->mux.mutex);
}

static inline void xeth_del_node(struct xeth_platform_priv *xpp,
				 struct hlist_node __rcu *node)
{
	spin_lock(&xpp->mux.mutex);
	hlist_del_rcu(node);
	spin_unlock(&xpp->mux.mutex);
}

static inline struct hlist_head __rcu *
xeth_hashed_upper_head(struct xeth_platform_priv *xpp, u32 xid)
{
	return &xpp->mux.upper_hl[hash_min(xid, xeth_mux_upper_hash_bits)];
}

static inline struct hlist_head __rcu *
xeth_indexed_upper_head(struct xeth_platform_priv *xpp, u32 index)
{
	return index < xeth_mux_upper_hash_bkts ?
		&xpp->mux.upper_hl[index] : NULL;
}

int xeth_mux_register(struct xeth_platform_priv *);
void xeth_mux_unregister(struct xeth_platform_priv *);

bool xeth_mux_is_lower_rcu(struct net_device *nd);
int xeth_mux_queue_xmit(struct sk_buff *skb);

u64 xeth_onie_mac_base(struct device *);
u16 xeth_onie_num_macs(struct device *);
u8 xeth_onie_device_version(struct device *);

int xeth_qsfp_register_driver(struct xeth_platform_priv *);
void xeth_qsfp_unregister_driver(struct xeth_platform_priv *);

int xeth_qsfp_get_module_info(struct net_device *,
			      struct ethtool_modinfo *);
int xeth_qsfp_get_module_eeprom(struct net_device *,
				struct ethtool_eeprom *, u8 *);

struct task_struct *xeth_sbrx_fork(struct xeth_platform_priv *);

int xeth_sbtx_service(struct xeth_platform_priv *);
int xeth_sbtx_break(struct xeth_platform_priv *);
int xeth_sbtx_change_upper(struct xeth_platform_priv *,
			   u32 upper_xid, u32 lower_xid, bool linking);
int xeth_sbtx_et_flags(struct xeth_platform_priv *,
		       u32 xid, u32 flags);
int xeth_sbtx_et_settings(struct xeth_platform_priv *,
			  u32 xid, struct ethtool_link_ksettings *);
int xeth_sbtx_fib_entry(struct xeth_platform_priv *,
			struct fib_entry_notifier_info *feni,
			unsigned long event);
int xeth_sbtx_fib6_entry(struct xeth_platform_priv *,
			 struct fib6_entry_notifier_info *feni,
			 unsigned long event);
int xeth_sbtx_ifa(struct xeth_platform_priv *,
		  struct in_ifaddr *ifa,
		  unsigned long event,
		  u32 xid);
int xeth_sbtx_ifa6(struct xeth_platform_priv *,
		   struct inet6_ifaddr *ifa,
		   unsigned long event,
		   u32 xid);
int xeth_sbtx_ifinfo(struct xeth_platform_priv *,
		     struct net_device *nd,
		     enum xeth_dev_kind kind,
		     u32 xid,
		     unsigned iff,
		     u8 reason);
int xeth_sbtx_neigh_update(struct xeth_platform_priv *,
			   struct neighbour *neigh);
int xeth_sbtx_netns(struct xeth_platform_priv *,
		    struct net *ndnet, bool add);

int xeth_upper_register_drivers(struct xeth_platform_priv *);
void xeth_upper_unregister_drivers(struct xeth_platform_priv *);

int xeth_upper_new_port(struct xeth_platform_priv *xpp,
			const char *name, u32 xid, u64 ea,
			void (*setup) (struct ethtool_link_ksettings *),
			int qsfp_bus);

struct net_device *xeth_upper_lookup_rcu(struct xeth_platform_priv *xpp,
					 u32 xid);
void xeth_upper_drop_all_carrier(struct xeth_platform_priv *);
void xeth_upper_dump_all_ifinfo(struct xeth_platform_priv *);
void xeth_upper_reset_all_stats(struct xeth_platform_priv *);
void xeth_upper_changemtu(struct xeth_platform_priv *xpp, int mtu, int max_mtu);
bool xeth_upper_check(struct net_device *);
void xeth_upper_et_stat(struct net_device *nd, u32 index, u64 count);
void xeth_upper_link_stat(struct net_device *nd, u32 index, u64 count);
void xeth_upper_queue_unregister(struct hlist_head __rcu *head,
				 struct list_head *q);
void xeth_upper_speed(struct net_device *nd, u32 mbps);
u32 xeth_upper_xid(struct net_device *nd);
enum xeth_dev_kind xeth_upper_kind(struct net_device *nd);
void xeth_upper_delete_port(struct xeth_platform_priv *xpp, u32 xid);

int xeth_upper_qsfp_bus(struct net_device *);

struct i2c_client *xeth_upper_qsfp(struct net_device *);
void xeth_upper_set_qsfp(struct net_device *, struct i2c_client *);
struct net_device *xeth_upper_with_qsfp_bus(struct xeth_platform_priv *xpp,
					    int nr);

void xeth_upper_set_et_stat_names(const char * const names[]);

#endif  /* __NET_ETHERNET_XETH_H */
