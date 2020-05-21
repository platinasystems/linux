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
#include <linux/atomic.h>
#include <linux/etherdevice.h>
#include <linux/hashtable.h>
#include <linux/i2c.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>
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

#if defined(KBUILD_MODNAME)
# define xeth_name KBUILD_MODNAME
#else	/* KBUILD_MODNAME */
# define xeth_name "xeth"
#endif	/* KBUILD_MODNAME */

#define xeth_version "2.0"

#include <xeth_debug.h>

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

enum {
	xeth_max_et_flags = 32,
	xeth_max_et_stats = 512,
};

enum {
	xeth_et_stat_names_sz = xeth_max_et_stats * ETH_GSTRING_LEN,
};

extern int xeth_base_xid;
extern int xeth_encap;
extern struct net_device *xeth_mux;
extern const struct i2c_device_id xeth_qsfp_id_table[];
extern const u32 *xeth_qsfp_xids;
extern size_t xeth_qsfp_n_xids;
extern void *xeth_sbrx_buf;
extern size_t xeth_upper_n_et_stat_names;
extern char xeth_upper_et_flag_names[xeth_max_et_flags][ETH_GSTRING_LEN];
extern char *xeth_upper_et_stat_names;
extern int (*xeth_upper_eto_get_module_info)(struct net_device *,
					     struct ethtool_modinfo *);
extern int (*xeth_upper_eto_get_module_eeprom)(struct net_device *,
					       struct ethtool_eeprom *, u8 *);
extern int (*xeth_vendor_init)(void);
extern void (*xeth_vendor_exit)(void);
extern struct net_device **xeth_vendor_lowers;

enum xeth_encap {
	XETH_ENCAP_VLAN = 0,
};

enum xeth_counter {
	xeth_counter_ex_frames,
	xeth_counter_ex_bytes,
	xeth_counter_sb_connections,
	xeth_counter_sbex_invalid,
	xeth_counter_sbex_dropped,
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
	xeth_flag_inet6addr_notifier,
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

#define xeth_supports(ks, mk)						\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, supported, mk));	\
})

#define xeth_advertising(ks, mk)					\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, advertising, mk));	\
})

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
void xeth_mux_exit(void);

int xeth_upper_init(void);
void xeth_upper_exit(void);

u8 xeth_mux_bits(void);
int xeth_mux_add_lowers(struct net_device *lowers[]);
void xeth_mux_add_node(struct hlist_node __rcu *node,
		       struct hlist_head __rcu *head);
void xeth_mux_del_node(struct hlist_node __rcu *node);

long long xeth_mux_counter(enum xeth_counter cnt);
void xeth_mux_counter_add(enum xeth_counter cnt, s64 n);
void xeth_mux_counter_dec(enum xeth_counter cnt);
void xeth_mux_counter_inc(enum xeth_counter cnt);
void xeth_mux_counter_set(enum xeth_counter cnt, s64 n);

rx_handler_result_t xeth_mux_demux(struct sk_buff **pskb);

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

char *xeth_onie_product_name(void);
char *xeth_onie_part_number(void);
char *xeth_onie_serial_number(void);
char *xeth_onie_manufacture_date(void);
char *xeth_onie_label_revision(void);
char *xeth_onie_platform_name(void);
char *xeth_onie_onie_version(void);
char *xeth_onie_manufacturer(void);
char *xeth_onie_country_code(void);
char *xeth_onie_vendor(void);
char *xeth_onie_diag_version(void);
char *xeth_onie_service_tag(void);

u64 xeth_onie_mac_base(void);
u16 xeth_onie_num_macs(void);
u8 xeth_onie_device_version(void);

int xeth_qsfp_detect(struct i2c_client *, struct i2c_board_info *);
int xeth_qsfp_probe(struct i2c_client *);
int xeth_qsfp_remove(struct i2c_client *);
int xeth_qsfp_get_module_info(struct net_device *, struct ethtool_modinfo *);
int xeth_qsfp_get_module_eeprom(struct net_device *, struct ethtool_eeprom *,
				u8 *);

struct task_struct *xeth_sb_start(void);

struct task_struct *xeth_sbrx_fork(struct socket *conn);

int xeth_sbtx_service(struct socket *conn);
int xeth_sbtx_break(void);
int xeth_sbtx_change_upper(u32 upper_xid, u32 lower_xid, bool linking);
int xeth_sbtx_et_flags(u32 xid, u32 flags);
int xeth_sbtx_et_settings(u32 xid, struct ethtool_link_ksettings *);
int xeth_sbtx_fib_entry(unsigned long event,
			struct fib_entry_notifier_info *feni);
int xeth_sbtx_fib6_entry(unsigned long event,
			 struct fib6_entry_notifier_info *feni);
int xeth_sbtx_ifa(struct in_ifaddr *ifa, u32 xid, unsigned long event);
int xeth_sbtx_ifa6(struct inet6_ifaddr *ifa, u32 xid, unsigned long event);
int xeth_sbtx_ifinfo(struct net_device *nd, u32 xid, enum xeth_dev_kind kind,
		     unsigned iff, u8 reason);
int xeth_sbtx_neigh_update(struct neighbour *neigh);

struct net_device *xeth_upper_lookup_rcu(u32 xid);
void xeth_upper_all_carrier_off(void);
void xeth_upper_all_dump_ifinfo(void);
void xeth_upper_all_reset_stats(void);
void xeth_upper_changemtu(int mtu, int max_mtu);
bool xeth_upper_check(struct net_device *nd);
void xeth_upper_et_stat(struct net_device *nd, u32 index, u64 count);
void xeth_upper_link_stat(struct net_device *nd, u32 index, u64 count);
void xeth_upper_queue_unregister(struct hlist_head __rcu *head,
				 struct list_head *q);
void xeth_upper_speed(struct net_device *nd, u32 mbps);
u32 xeth_upper_xid(struct net_device *nd);
enum xeth_dev_kind xeth_upper_kind(struct net_device *nd);
s64 xeth_upper_make(const char *name, u32 xid, u64 ea,
		     void (*ethtool_cb) (struct ethtool_link_ksettings *));
void xeth_upper_delete_port(u32 xid);

struct i2c_client *xeth_upper_qsfp(struct net_device *);
void xeth_upper_set_qsfp(struct net_device *, struct i2c_client *);
void xeth_upper_set_et_flag_names(const char * const names[]);
void xeth_upper_set_et_stat_names(const char * const names[]);

int xeth_vendor_probe(struct pci_dev *, const struct pci_device_id *);

enum {
	xeth_qsfp_i2c_class = I2C_CLASS_HWMON | I2C_CLASS_DDC | I2C_CLASS_SPD,
};

#define new_xeth_qsfp_driver(NAME, ADDRS...)				\
static struct i2c_driver NAME = {					\
	.class		= xeth_qsfp_i2c_class,				\
	.driver = {							\
		.name	= #NAME,					\
	},								\
	.probe_new	= xeth_qsfp_probe,				\
	.remove		= xeth_qsfp_remove,				\
	.id_table	= xeth_qsfp_id_table,				\
	.detect		= xeth_qsfp_detect,				\
	.address_list	= I2C_ADDRS(ADDRS),				\
}

#endif  /* __NET_ETHERNET_XETH_H */
