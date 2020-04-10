/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

enum {
	xeth_mux_upper_hash_bits = 4,
	xeth_mux_upper_hash_bkts = 1 << xeth_mux_upper_hash_bits,
	xeth_mux_lower_hash_bits = 4,
	xeth_mux_lower_hash_bkts = 1 << xeth_mux_lower_hash_bits,
	xeth_mux_txqs = 1,
	xeth_mux_rxqs = 1,
};

struct xeth_mux_priv {
	struct spinlock mutex;
	struct hlist_head __rcu upper[xeth_mux_upper_hash_bkts];
	struct net_device *lower[xeth_mux_lower_hash_bkts];
	atomic64_t counter[xeth_counters];
	volatile long unsigned int flags;
	struct xeth_atomic_link_stats link_stats;
	struct task_struct *sb;
};

static const char *const xeth_mux_counter_names[] = {
#define xeth_mux_name_counter(name)	[xeth_counter_##name] = #name
	xeth_mux_name_counter(ex_frames),
	xeth_mux_name_counter(ex_bytes),
	xeth_mux_name_counter(sb_connections),
	xeth_mux_name_counter(sbex_invalid),
	xeth_mux_name_counter(sbex_dropped),
	xeth_mux_name_counter(sbrx_invalid),
	xeth_mux_name_counter(sbrx_no_dev),
	xeth_mux_name_counter(sbrx_no_mem),
	xeth_mux_name_counter(sbrx_msgs),
	xeth_mux_name_counter(sbrx_ticks),
	xeth_mux_name_counter(sbtx_msgs),
	xeth_mux_name_counter(sbtx_retries),
	xeth_mux_name_counter(sbtx_no_mem),
	xeth_mux_name_counter(sbtx_queued),
	xeth_mux_name_counter(sbtx_ticks),
	[xeth_counters] = NULL,
};

static const char *const xeth_mux_flag_names[] = {
#define xeth_mux_name_flag(name)	[xeth_flag_##name] = #name
	xeth_mux_name_flag(fib_notifier),
	xeth_mux_name_flag(inetaddr_notifier),
	xeth_mux_name_flag(inet6addr_notifier),
	xeth_mux_name_flag(netdevice_notifier),
	xeth_mux_name_flag(netevent_notifier),
	xeth_mux_name_flag(sb_task),
	xeth_mux_name_flag(sb_listen),
	xeth_mux_name_flag(sb_connected),
	xeth_mux_name_flag(sbrx_task),
	[xeth_flags] = NULL,
};

static u32 xeth_mux_flags(struct xeth_mux_priv *priv)
{
	u32 flags;
	barrier();
	flags = priv->flags;
	return flags;
}

static const struct net_device_ops xeth_mux_netdev_ops;
static const struct ethtool_ops xeth_mux_ethtool_ops;

struct net_device *xeth_mux;

static struct xeth_mux_priv *xeth_mux_priv(void)
{
	return IS_ERR_OR_NULL(xeth_mux) ? NULL : netdev_priv(xeth_mux);
}

void xeth_mux_add_node(struct hlist_node __rcu *node,
		       struct hlist_head __rcu *head)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv) {
		spin_lock(&priv->mutex);
		hlist_add_head_rcu(node, head);
		spin_unlock(&priv->mutex);
	}
}

void xeth_mux_del_node(struct hlist_node __rcu *node)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv) {
		spin_lock(&priv->mutex);
		hlist_del_rcu(node);
		spin_unlock(&priv->mutex);
	}
}

static void xeth_mux_setup(struct net_device *nd)
{
	struct xeth_mux_priv *priv = netdev_priv(nd);
	int i;

	spin_lock_init(&priv->mutex);
	xeth_reset_link_stats(&priv->link_stats);

	for (i = 0; i < xeth_mux_upper_hash_bkts; i++)
		WRITE_ONCE(priv->upper[i].first, NULL);

	for (i = 0; i < xeth_counters; i++)
		atomic64_set(&priv->counter[i], 0LL);

	nd->netdev_ops = &xeth_mux_netdev_ops;
	nd->ethtool_ops = &xeth_mux_ethtool_ops;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	ether_setup(nd);
	eth_hw_addr_random(nd);
	nd->flags |= IFF_MASTER;
	nd->priv_flags |= IFF_DONT_BRIDGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->min_mtu = ETH_MIN_MTU;
	nd->max_mtu = ETH_MAX_MTU - VLAN_HLEN;
	nd->mtu = XETH_SIZEOF_JUMBO_FRAME - VLAN_HLEN;

	/* FIXME should we do this? netif_keep_dst(nd); */
}

static int xeth_mux_open(struct net_device *nd)
{
	struct net_device *lower;
	struct list_head *lowers;
	int err;

	netdev_for_each_lower_dev(nd, lower, lowers) {
		err = xeth_debug_nd_err(lower, dev_open(lower));
		if (err)
			return err;
	}

	/* FIXME condition with lowers carrier? */
	if (!netif_carrier_ok(nd))
		netif_carrier_on(nd);
	return 0;
}

static int xeth_mux_stop(struct net_device *nd)
{
	struct net_device *lower;
	struct list_head *lowers;

	if (netif_carrier_ok(nd))
		netif_carrier_off(nd);
	netdev_for_each_lower_dev(nd, lower, lowers)
		dev_close(lower);
	return 0;
}

static int xeth_mux_add_lower(struct net_device *upper,
			      struct net_device *lower,
			      struct netlink_ext_ack *extack)
{
	struct xeth_mux_priv *priv = netdev_priv(upper);
	int (*change_mtu_op)(struct net_device *dev, int new_mtu) =
		lower->netdev_ops->ndo_change_mtu;
	int err;

	err = xeth_debug_nd_err(lower, dev_set_promiscuity(lower, 1));
	if (err < 0)
		return err;

	if (change_mtu_op) {
		err = xeth_debug_err(change_mtu_op(lower,
						   XETH_SIZEOF_JUMBO_FRAME));
		if (err)
			return err;
	}
	if (xeth_debug_err(lower == dev_net(upper)->loopback_dev))
		return -EOPNOTSUPP;

	if (netdev_is_rx_handler_busy(lower))
		return xeth_debug_err(rtnl_dereference(lower->rx_handler) !=
				      xeth_mux_demux) ? -EBUSY : 0;

	err = xeth_debug_err(netdev_rx_handler_register(lower,
							xeth_mux_demux,
							upper));
	if (err)
		return err;

	spin_lock(&priv->mutex);

	lower->flags |= IFF_SLAVE;
	err = netdev_master_upper_dev_link(lower, upper, NULL, NULL, extack);
	if (err)
		lower->flags &= ~IFF_SLAVE;
	else
		xeth_mux_reload_lowers();

	spin_unlock(&priv->mutex);

	if (err)
		netdev_rx_handler_unregister(lower);

	return err;
}

static int xeth_mux_del_lower(struct net_device *upper,
			      struct net_device *lower)
{
	struct xeth_mux_priv *priv = netdev_priv(upper);

	spin_lock(&priv->mutex);

	lower->flags &= ~IFF_SLAVE;
	netdev_upper_dev_unlink(lower, upper);
	xeth_mux_reload_lowers();

	spin_unlock(&priv->mutex);

	netdev_rx_handler_unregister(lower);

	dev_set_promiscuity(lower, -1);

	return 0;
}

static int xeth_mux_lower_hash(struct sk_buff *skb)
{
	u16 tci;

	/* FIXME
	 * replace this with a ecmp type hash, maybe something like,
	 *	hash_64(ea64, xeth_mux_lower_hash_bits)
	 */
	return vlan_get_tag(skb, &tci) ? 0 : tci & 1;
}

static void xeth_mux_vlan_exception(struct sk_buff *skb)
{
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
	__be16 h_vlan_proto = veh->h_vlan_proto;
	u16 tci = be16_to_cpu(veh->h_vlan_TCI);
	__be16 h_vlan_encapsulated_proto =
		veh->h_vlan_encapsulated_proto;
	xeth_counter_inc(ex_frames);
	xeth_counter_add(ex_bytes, skb->len);
	eth_type_trans(skb, xeth_mux);
	skb->vlan_proto = h_vlan_proto;
	skb->vlan_tci = tci & VLAN_PRIO_MASK;
	skb->protocol = h_vlan_encapsulated_proto;
	skb_pull_inline(skb, VLAN_HLEN);
	xeth_mux_demux(&skb);
}

static bool xeth_mux_was_exception(struct sk_buff *skb)
{
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
	if (eth_type_vlan(veh->h_vlan_proto)) {
		u16 tci = be16_to_cpu(veh->h_vlan_TCI);
		const u16 exmk = 7 << VLAN_PRIO_SHIFT;
		if ((tci & VLAN_PRIO_MASK) == exmk) {
			xeth_mux_vlan_exception(skb);
			return true;
		}
	}
	return false;
}

static netdev_tx_t xeth_mux_xmit(struct sk_buff *skb, struct net_device *nd)
{
	struct xeth_mux_priv *priv = netdev_priv(nd);
	struct net_device *lower;

	if (xeth_mux_was_exception(skb))
		return NETDEV_TX_OK;
	lower = priv->lower[xeth_mux_lower_hash(skb)];
	if (lower) {
		if (lower->flags & IFF_UP) {
			skb->dev = lower;
			no_xeth_debug_skb(skb);
			if (dev_queue_xmit(skb)) {
				atomic64_inc(&priv->link_stats.tx_dropped);
			} else {
				atomic64_inc(&priv->link_stats.tx_packets);
				atomic64_add(skb->len, &priv->link_stats.tx_bytes);
			}
		} else {
			atomic64_inc(&priv->link_stats.tx_errors);
			atomic64_inc(&priv->link_stats.tx_heartbeat_errors);
			kfree_skb(skb);
		}
	} else {
		atomic64_inc(&priv->link_stats.tx_errors);
		atomic64_inc(&priv->link_stats.tx_aborted_errors);
		kfree_skb(skb);
	}
	return NETDEV_TX_OK;
}

static void xeth_mux_get_stats64(struct net_device *nd,
					 struct rtnl_link_stats64 *dst)
{
	struct xeth_mux_priv *priv = netdev_priv(nd);
	xeth_get_link_stats(dst, &priv->link_stats);
}

static void xeth_mux_forward(struct net_device *to, struct sk_buff *skb)
{
	struct xeth_mux_priv *priv = netdev_priv(xeth_mux);
	int rx_result;
	
	rx_result = dev_forward_skb(to, skb);
	if (rx_result == NET_RX_SUCCESS) {
		atomic64_inc(&priv->link_stats.rx_packets);
		atomic64_add(skb->len, &priv->link_stats.rx_bytes);
	} else
		atomic64_inc(&priv->link_stats.rx_dropped);
}

static void xeth_mux_demux_vlan(struct sk_buff *skb)
{
	struct xeth_mux_priv *priv = netdev_priv(xeth_mux);
	struct net_device *upper;
	u32 xid;
	
	skb->priority =
		(typeof(skb->priority))(skb->vlan_tci >> VLAN_PRIO_SHIFT);
	xid = skb->vlan_tci & VLAN_VID_MASK;
	if (eth_type_vlan(skb->protocol)) {
		__be16 tci = *(__be16*)(skb->data);
		__be16 proto = *(__be16*)(skb->data+2);
		xid |= VLAN_N_VID * (be16_to_cpu(tci) & VLAN_VID_MASK);
		skb->protocol = proto;
		skb_pull_inline(skb, VLAN_HLEN);
	}
	upper = xeth_debug_rcu(xeth_upper_lookup_rcu(xid));
	if (upper) {
		struct ethhdr *eth;
		unsigned char *mac = skb_mac_header(skb);
		skb_push(skb, ETH_HLEN);
		memmove(skb->data, mac, 2*ETH_ALEN);
		eth = (typeof(eth))skb->data;
		eth->h_proto = skb->protocol;
		skb->vlan_proto = 0;
		skb->vlan_tci = 0;
		xeth_mux_forward(upper, skb);
	} else {
		atomic64_inc(&priv->link_stats.rx_errors);
		atomic64_inc(&priv->link_stats.rx_nohandler);
		dev_kfree_skb(skb);
	}
}

rx_handler_result_t xeth_mux_demux(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct xeth_mux_priv *priv;

	if (IS_ERR_OR_NULL(xeth_mux)) {
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	priv = netdev_priv(xeth_mux);

	if (eth_type_vlan(skb->vlan_proto)) {
		xeth_mux_demux_vlan(skb);
	} else {
		atomic64_inc(&priv->link_stats.rx_errors);
		atomic64_inc(&priv->link_stats.rx_frame_errors);
		dev_kfree_skb(skb);
	}

	return RX_HANDLER_CONSUMED;
}

static const struct net_device_ops xeth_mux_netdev_ops = {
	.ndo_open	= xeth_mux_open,
	.ndo_stop	= xeth_mux_stop,
	.ndo_start_xmit	= xeth_mux_xmit,
	.ndo_add_slave	= xeth_mux_add_lower,
	.ndo_del_slave	= xeth_mux_del_lower,
	.ndo_get_stats64= xeth_mux_get_stats64,
};

static void xeth_mux_eto_get_drvinfo(struct net_device *nd,
				     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, xeth_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, xeth_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	strlcpy(drvinfo->bus_info, "n/a", ETHTOOL_BUSINFO_LEN);
	drvinfo->n_priv_flags = xeth_flags;
	drvinfo->n_stats = xeth_counters;
}

static int xeth_mux_eto_get_sset_count(struct net_device *nd, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return xeth_counters;
	case ETH_SS_PRIV_FLAGS:
		return xeth_flags;
	default:
		return -EOPNOTSUPP;
	}
}

static void xeth_mux_eto_get_strings(struct net_device *nd, u32 sset, u8 *data)
{
	char *p = (char *)data;
	int i;

	switch (sset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		for (i = 0; xeth_mux_counter_names[i]; i++) {
			strlcpy(p, xeth_mux_counter_names[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; xeth_mux_flag_names[i]; i++) {
			strlcpy(p, xeth_mux_flag_names[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xeth_mux_eto_get_stats(struct net_device *nd,
					   struct ethtool_stats *stats,
					   u64 *data)
{
	struct xeth_mux_priv *priv = netdev_priv(nd);
	int i;

	for (i = 0; i < xeth_counters; i++)
		*data++ = atomic64_read(&priv->counter[i]);
}

static u32 xeth_mux_eto_get_priv_flags(struct net_device *nd)
{
	return xeth_mux_flags(netdev_priv(nd));
}

static const struct ethtool_ops xeth_mux_ethtool_ops = {
	.get_drvinfo = xeth_mux_eto_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = xeth_mux_eto_get_sset_count,
	.get_strings = xeth_mux_eto_get_strings,
	.get_ethtool_stats = xeth_mux_eto_get_stats,
	.get_priv_flags = xeth_mux_eto_get_priv_flags,
};

/**
 * xeth_mux_init() - creates the xeth multiplexor.
 *
 * A platform driver may reference the mux netdev with,
 *	xeth = dev_get_by_name(&init_net, "xeth");
 * It may then assign lower netdevs with,
 * 	xeth->netdev_ops->ndo_add_lower(xeth, LOWER, NULL);
 * or through user space with,
 *	ip link set LOWER master xeth
 *
 * See @xeth_upper_init() for how to create the proxy netdevs multiplexed by
 * this device.
 */
__init int xeth_mux_init(void)
{
	struct xeth_mux_priv *priv;
	int err;

	err = xeth_sbrx_init();
	if (err)
		return err;
	xeth_mux = xeth_debug_ptr_err(alloc_netdev_mqs(sizeof(*priv),
						       xeth_name,
						       NET_NAME_USER,
						       xeth_mux_setup,
						       xeth_mux_txqs,
						       xeth_mux_rxqs));
	if (IS_ERR(xeth_mux))
		return PTR_ERR(xeth_mux);

	rtnl_lock();
	err = xeth_debug_err(register_netdevice(xeth_mux));
	rtnl_unlock();

	if (err) {
		kfree(xeth_mux);
		return err;
	}

	priv = netdev_priv(xeth_mux);
	priv->sb = xeth_sb_start();

	if (IS_ERR(priv->sb)) {
		err = PTR_ERR(priv->sb);
		kfree(xeth_mux);
		return err;
	}

	xeth_debug_err(xeth_kstrs_init(&xeth_ethtool_flag_names,
				       &xeth_mux->dev.kobj,
				       "ethtool-flag-names",
				       xeth_n_ethtool_flags));
	xeth_debug_err(xeth_kstrs_init(&xeth_ethtool_stat_names,
				       &xeth_mux->dev.kobj,
				       "ethtool-stat-names",
				       xeth_n_ethtool_stats));

	return 0;
}

int xeth_mux_deinit(int err)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	struct net_device *lower;
	struct list_head *lowers;
	LIST_HEAD(q);
	int bkt;

	if (!priv)
		return err;
	if (xeth_flag(sb_task)) {
		if (!IS_ERR_OR_NULL(priv->sb)) {
			kthread_stop(priv->sb);
			priv->sb = NULL;
		}
		while (xeth_flag(sb_task)) ;
	}

	rtnl_lock();

	netdev_for_each_lower_dev(xeth_mux, lower, lowers)
		xeth_mux_del_lower(xeth_mux, lower);

	rcu_read_lock();
	for (bkt = 0; bkt < xeth_mux_upper_hash_bkts; bkt++)
		xeth_upper_queue_unregister(&priv->upper[bkt], &q);
	rcu_read_unlock();

	unregister_netdevice_many(&q);

	rtnl_unlock();

	rcu_barrier();

	unregister_netdev(xeth_mux);
	xeth_mux = NULL;

	return xeth_sbrx_deinit(err);
}

u8 xeth_mux_bits(void)
{
	switch (xeth_encap) {
	case XETH_ENCAP_VLAN:
		return 12;
	}
	return 0;
}

long long xeth_mux_counter(enum xeth_counter index)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	return priv ? atomic64_read(&priv->counter[index]) : 0LL;
}

void xeth_mux_counter_add(enum xeth_counter index, s64 n)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv)
		atomic64_add(n, &priv->counter[index]);
}

void xeth_mux_counter_dec(enum xeth_counter index)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv)
		atomic64_dec(&priv->counter[index]);
}

void xeth_mux_counter_inc(enum xeth_counter index)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv)
		atomic64_inc(&priv->counter[index]);
}

void xeth_mux_counter_set(enum xeth_counter index, s64 n)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv)
		atomic64_set(&priv->counter[index], n);
}

bool xeth_mux_flag(enum xeth_flag bit)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	bool flag = false;
	
	if (priv) {
		smp_mb__before_atomic();
		flag = variable_test_bit(bit, &priv->flags);
		smp_mb__after_atomic();
	}
	return flag;
}

void xeth_mux_flag_clear(enum xeth_flag bit)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv) {
		smp_mb__before_atomic();
		clear_bit(bit, &priv->flags);
		smp_mb__after_atomic();
	}
}

void xeth_mux_flag_set(enum xeth_flag bit)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	if (priv) {
		smp_mb__before_atomic();
		set_bit(bit, &priv->flags);
		smp_mb__after_atomic();
	}
}

int xeth_mux_ifindex(void)
{
	return IS_ERR_OR_NULL(xeth_mux) ? 0 : xeth_mux->ifindex;
}

bool xeth_mux_is_registered(void)
{
	return !IS_ERR_OR_NULL(xeth_mux) &&
		xeth_mux->reg_state == NETREG_REGISTERED;
}

bool xeth_mux_is_lower(struct net_device *nd)
{
	struct net_device *lower;
	struct list_head *lowers;

	netdev_for_each_lower_dev(xeth_mux, lower, lowers)
		if (lower == nd)
			return true;
	return false;
}

struct kobject *xeth_mux_kobj(void)
{
	return IS_ERR_OR_NULL(xeth_mux) ? NULL : &xeth_mux->dev.kobj;
}

void xeth_mux_reload_lowers(void)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	struct net_device *lower;
	struct list_head *lowers;
	int i, n = 1;

	if (!priv)
		return;
	netdev_for_each_lower_dev(xeth_mux, lower, lowers) {
		for (i = n - 1; i < ARRAY_SIZE(priv->lower); i += n) {
			priv->lower[i] = lower;
		}
		n++;
	}
}

int xeth_mux_queue_xmit(struct sk_buff *skb)
{
	if (xeth_mux->flags & IFF_UP) {
		skb->dev = xeth_mux;
		dev_queue_xmit(skb);
	} else {
		struct xeth_mux_priv *priv = netdev_priv(xeth_mux);
		kfree_skb_list(skb);
		atomic64_inc(&priv->link_stats.tx_errors);
		atomic64_inc(&priv->link_stats.tx_carrier_errors);
	}
	return NETDEV_TX_OK;
}

struct hlist_head __rcu *xeth_mux_upper_head_hashed(u32 xid)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	return !priv ? NULL :
		&priv->upper[hash_min(xid, xeth_mux_upper_hash_bits)];
}

struct hlist_head __rcu *xeth_mux_upper_head_indexed(u32 index)
{
	struct xeth_mux_priv *priv = xeth_mux_priv();
	return (!priv || index >= xeth_mux_upper_hash_bkts) ?
		NULL : &priv->upper[index];
}
