/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

enum {
	xeth_mux_txqs = 1,
	xeth_mux_rxqs = 1,
};

struct xeth_mux_priv {
	struct xeth_platform_priv *xpp;
};

static const char *const xeth_mux_counter_names[] = {
#define xeth_mux_name_counter(name)	[xeth_mux_counter_##name] = #name
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
	[xeth_mux_n_counters] = NULL,
};

static const char *const xeth_mux_priv_flag_names[] = {
#define xeth_mux_priv_flag_name(name)	[xeth_mux_priv_flag_##name] = #name
	xeth_mux_priv_flag_name(main_task),
	xeth_mux_priv_flag_name(sb_listen),
	xeth_mux_priv_flag_name(sb_connected),
	xeth_mux_priv_flag_name(sbrx_task),
	xeth_mux_priv_flag_name(fib_notifier),
	xeth_mux_priv_flag_name(inetaddr_notifier),
	xeth_mux_priv_flag_name(inet6addr_notifier),
	xeth_mux_priv_flag_name(netdevice_notifier),
	xeth_mux_priv_flag_name(netevent_notifier),
	[xeth_mux_n_priv_flags] = NULL,
};

static const struct net_device_ops xeth_mux_ndo;
static const struct ethtool_ops xeth_mux_ethtool_ops;
static rx_handler_result_t xeth_mux_demux(struct sk_buff **pskb);

static void xeth_mux_setup(struct net_device *mux)
{
	mux->netdev_ops = &xeth_mux_ndo;
	mux->ethtool_ops = &xeth_mux_ethtool_ops;
	mux->needs_free_netdev = true;
	mux->priv_destructor = NULL;
	ether_setup(mux);
	eth_hw_addr_random(mux);
	mux->flags |= IFF_MASTER;
	mux->priv_flags |= IFF_DONT_BRIDGE;
	mux->priv_flags |= IFF_NO_QUEUE;
	mux->priv_flags &= ~IFF_TX_SKB_SHARING;
	mux->min_mtu = ETH_MIN_MTU;
	mux->max_mtu = ETH_MAX_MTU - VLAN_HLEN;
	mux->mtu = XETH_SIZEOF_JUMBO_FRAME - VLAN_HLEN;

	/* FIXME should we netif_keep_dst(nd) ? */
}

static int xeth_mux_open(struct net_device *mux)
{
	struct net_device *lower;
	struct list_head *lowers;
	int err;

	netdev_for_each_lower_dev(mux, lower, lowers) {
		err = xeth_debug_nd_err(lower, dev_open(lower, NULL));
		if (err)
			return err;
	}

	if (!netif_carrier_ok(mux))
		netif_carrier_on(mux);
	return 0;
}

static int xeth_mux_stop(struct net_device *mux)
{
	struct net_device *lower;
	struct list_head *lowers;

	if (netif_carrier_ok(mux))
		netif_carrier_off(mux);
	netdev_for_each_lower_dev(mux, lower, lowers)
		dev_close(lower);
	return 0;
}

static void xeth_mux_reload_lowers(struct net_device *mux)
{
	struct xeth_mux_priv *xmp = netdev_priv(mux);
	struct net_device *lower;
	struct list_head *lowers;
	int i, n = 1;

	if (!xmp)
		return;
	netdev_for_each_lower_dev(mux, lower, lowers) {
		for (i = n - 1; i < xeth_mux_lower_hash_bkts; i += n)
			xmp->xpp->mux.lower_ht[i] = lower;
		n++;
	}
}

static int xeth_mux_add_lower(struct net_device *mux,
			      struct net_device *lower,
			      struct netlink_ext_ack *extack)
{
	struct xeth_mux_priv *xmp = netdev_priv(mux);
	int (*change_mtu_op)(struct net_device *dev, int new_mtu) =
		lower->netdev_ops->ndo_change_mtu;
	int err;

	err = dev_set_promiscuity(lower, 1);
	if (err < 0)
		return err;

	if (change_mtu_op) {
		err = change_mtu_op(lower, XETH_SIZEOF_JUMBO_FRAME);
		if (err)
			return err;
	}
	if (lower == dev_net(mux)->loopback_dev)
		return -EOPNOTSUPP;

	if (netdev_is_rx_handler_busy(lower))
		return rtnl_dereference(lower->rx_handler) != xeth_mux_demux ?
			-EBUSY : 0;

	err = netdev_rx_handler_register(lower, xeth_mux_demux, xmp->xpp);
	if (err)
		return err;

	spin_lock(&xmp->xpp->mux.mutex);

	lower->flags |= IFF_SLAVE;
	err = netdev_master_upper_dev_link(lower, mux, NULL, NULL, extack);
	if (err)
		lower->flags &= ~IFF_SLAVE;
	else
		xeth_mux_reload_lowers(mux);

	spin_unlock(&xmp->xpp->mux.mutex);

	if (err)
		netdev_rx_handler_unregister(lower);

	return err;
}

static int _xeth_mux_add_all_lowers(struct xeth_platform_priv *xpp)
{
	int i, err;

	for (i = 0; !err && xpp->mux.lowers[i]; i++)
		err = xpp->mux.nd->netdev_ops->ndo_add_slave(xpp->mux.nd,
							     xpp->mux.lowers[i],
							     NULL);
	for (i = 0; xpp->mux.lowers[i]; i++)
		dev_put(xpp->mux.lowers[i]);

	return err;
}

static int xeth_mux_add_all_lowers(struct xeth_platform_priv *xpp)
{
	int err;
	rtnl_lock();
	err = _xeth_mux_add_all_lowers(xpp);
	rtnl_unlock();
	return err;
}

static int xeth_mux_del_lower(struct net_device *mux,
			      struct net_device *lower)
{
	struct xeth_mux_priv *xmp = netdev_priv(mux);

	spin_lock(&xmp->xpp->mux.mutex);

	lower->flags &= ~IFF_SLAVE;
	netdev_upper_dev_unlink(lower, mux);
	xeth_mux_reload_lowers(mux);

	spin_unlock(&xmp->xpp->mux.mutex);

	netdev_rx_handler_unregister(lower);

	dev_set_promiscuity(lower, -1);

	return 0;
}

static void _xeth_mux_del_all_lowers(struct xeth_platform_priv *xpp)
{
	struct net_device *lower;
	struct list_head *lowers;

	netdev_for_each_lower_dev(xpp->mux.nd, lower, lowers)
		xeth_mux_del_lower(xpp->mux.nd, lower);
}

static void xeth_mux_del_all_lowers(struct xeth_platform_priv *xpp)
{
	rtnl_lock();
	_xeth_mux_del_all_lowers(xpp);
	rtnl_unlock();
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

static void xeth_mux_vlan_exception(struct xeth_platform_priv *xpp,
				    struct sk_buff *skb)
{
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
	__be16 h_vlan_proto = veh->h_vlan_proto;
	u16 tci = be16_to_cpu(veh->h_vlan_TCI);
	__be16 h_vlan_encapsulated_proto =
		veh->h_vlan_encapsulated_proto;
	xeth_counter_inc(xpp, ex_frames);
	xeth_counter_add(xpp, ex_bytes, skb->len);
	eth_type_trans(skb, xpp->mux.nd);
	skb->vlan_proto = h_vlan_proto;
	skb->vlan_tci = tci & ~VLAN_PRIO_MASK;
	skb->protocol = h_vlan_encapsulated_proto;
	skb_pull_inline(skb, VLAN_HLEN);
	xeth_mux_demux(&skb);
}

static bool xeth_mux_was_exception(struct xeth_platform_priv *xpp,
				   struct sk_buff *skb)
{
	const u16 expri = 7 << VLAN_PRIO_SHIFT;
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
	if (eth_type_vlan(veh->h_vlan_proto)) {
		if ((be16_to_cpu(veh->h_vlan_TCI) & VLAN_PRIO_MASK) == expri) {
			xeth_mux_vlan_exception(xpp, skb);
			return true;
		}
	}
	return false;
}

static netdev_tx_t xeth_mux_xmit(struct sk_buff *skb, struct net_device *mux)
{
	struct xeth_mux_priv *xmp = netdev_priv(mux);
	struct xeth_atomic_link_stats *ls = &xmp->xpp->mux.link_stats;
	struct net_device *lower;

	if (xeth_mux_was_exception(xmp->xpp, skb))
		return NETDEV_TX_OK;
	lower = xmp->xpp->mux.lower_ht[xeth_mux_lower_hash(skb)];
	if (lower) {
		if (lower->flags & IFF_UP) {
			skb->dev = lower;
			no_xeth_debug_skb(skb);
			if (dev_queue_xmit(skb)) {
				atomic64_inc(&ls->tx_dropped);
			} else {
				atomic64_inc(&ls->tx_packets);
				atomic64_add(skb->len, &ls->tx_bytes);
			}
		} else {
			atomic64_inc(&ls->tx_errors);
			atomic64_inc(&ls->tx_heartbeat_errors);
			kfree_skb(skb);
		}
	} else {
		atomic64_inc(&ls->tx_errors);
		atomic64_inc(&ls->tx_aborted_errors);
		kfree_skb(skb);
	}
	return NETDEV_TX_OK;
}

static void xeth_mux_get_stats64(struct net_device *mux,
				 struct rtnl_link_stats64 *dst)
{
	struct xeth_mux_priv *xmp = netdev_priv(mux);
	xeth_get_link_stats(dst, &xmp->xpp->mux.link_stats);
}

static void xeth_mux_forward(struct xeth_platform_priv *xpp,
			     struct sk_buff *skb,
			     struct net_device *to)
{
	if (dev_forward_skb(to, skb) == NET_RX_SUCCESS) {
		atomic64_inc(&xpp->mux.link_stats.rx_packets);
		atomic64_add(skb->len, &xpp->mux.link_stats.rx_bytes);
	} else
		atomic64_inc(&xpp->mux.link_stats.rx_dropped);
}

static void xeth_mux_demux_vlan(struct xeth_platform_priv *xpp,
				struct sk_buff *skb)
{
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
	upper = xeth_debug_rcu(xeth_upper_lookup_rcu(xpp, xid));
	if (upper) {
		struct ethhdr *eth;
		unsigned char *mac = skb_mac_header(skb);
		skb_push(skb, ETH_HLEN);
		memmove(skb->data, mac, 2*ETH_ALEN);
		eth = (typeof(eth))skb->data;
		eth->h_proto = skb->protocol;
		skb->vlan_proto = 0;
		skb->vlan_tci = 0;
		xeth_mux_forward(xpp, skb, upper);
	} else {
		no_xeth_debug("no upper for xid %d; tci 0x%x",
			xid, skb->vlan_tci);
		atomic64_inc(&xpp->mux.link_stats.rx_errors);
		atomic64_inc(&xpp->mux.link_stats.rx_nohandler);
		dev_kfree_skb(skb);
	}
}

static rx_handler_result_t xeth_mux_demux(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct xeth_platform_priv *xpp =
		rcu_dereference(skb->dev->rx_handler_data);

	if (eth_type_vlan(skb->vlan_proto)) {
		xeth_mux_demux_vlan(xpp, skb);
	} else {
		atomic64_inc(&xpp->mux.link_stats.rx_errors);
		atomic64_inc(&xpp->mux.link_stats.rx_frame_errors);
		dev_kfree_skb(skb);
	}

	return RX_HANDLER_CONSUMED;
}

static const struct net_device_ops xeth_mux_ndo = {
	.ndo_open	= xeth_mux_open,
	.ndo_stop	= xeth_mux_stop,
	.ndo_start_xmit	= xeth_mux_xmit,
	.ndo_add_slave	= xeth_mux_add_lower,
	.ndo_del_slave	= xeth_mux_del_lower,
	.ndo_get_stats64= xeth_mux_get_stats64,
};

bool xeth_is_mux(struct net_device *nd)
{
	return nd->netdev_ops == &xeth_mux_ndo;
}

static void xeth_mux_eto_get_drvinfo(struct net_device *nd,
				     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, xeth_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, xeth_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	strlcpy(drvinfo->bus_info, "n/a", ETHTOOL_BUSINFO_LEN);
	drvinfo->n_priv_flags = xeth_mux_n_priv_flags;
	drvinfo->n_stats = xeth_mux_n_counters;
}

static int xeth_mux_eto_get_sset_count(struct net_device *nd, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return xeth_mux_n_counters;
	case ETH_SS_PRIV_FLAGS:
		return xeth_mux_n_priv_flags;
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
		for (i = 0; i < xeth_mux_n_counters; i++) {
			strlcpy(p, xeth_mux_counter_names[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < xeth_mux_n_priv_flags; i++) {
			strlcpy(p, xeth_mux_priv_flag_names[i],
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xeth_mux_eto_get_stats(struct net_device *nd,
					   struct ethtool_stats *stats,
					   u64 *data)
{
	struct xeth_mux_priv *xmp = netdev_priv(nd);
	int i;

	for (i = 0; i < xeth_mux_n_counters; i++)
		*data++ = atomic64_read(&xmp->xpp->mux.counter[i]);
}

static u32 xeth_mux_eto_get_priv_flags(struct net_device *nd)
{
	struct xeth_mux_priv *xmp = netdev_priv(nd);
	return xeth_mux_priv_flags(xmp->xpp);
}

static const struct ethtool_ops xeth_mux_ethtool_ops = {
	.get_drvinfo = xeth_mux_eto_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = xeth_mux_eto_get_sset_count,
	.get_strings = xeth_mux_eto_get_strings,
	.get_ethtool_stats = xeth_mux_eto_get_stats,
	.get_priv_flags = xeth_mux_eto_get_priv_flags,
};

int xeth_mux_register(struct xeth_platform_priv *xpp)
{
	struct xeth_mux_priv *xmp;
	int i, err;

	spin_lock_init(&xpp->mux.mutex);
	xeth_reset_link_stats(&xpp->mux.link_stats);
	for (i = 0; i < xeth_mux_upper_hash_bkts; i++)
		WRITE_ONCE(xpp->mux.upper_hl[i].first, NULL);
	for (i = 0; i < xeth_mux_n_counters; i++)
		atomic64_set(&xpp->mux.counter[i], 0LL);

	xpp->mux.nd = alloc_netdev_mqs(sizeof(*xmp),
				       xpp->config->name,
				       NET_NAME_USER,
				       xeth_mux_setup,
				       xeth_mux_txqs,
				       xeth_mux_rxqs);
	if (IS_ERR(xpp->mux.nd)) {
		err = PTR_ERR(xpp->mux.nd);
		xpp->mux.nd = NULL;
		return err;
	}
	xmp = netdev_priv(xpp->mux.nd);
	xmp->xpp = xpp;

	xpp->mux.nd->addr_assign_type = NET_ADDR_STOLEN;
	memcpy(xpp->mux.nd->dev_addr, xpp->mux.lowers[0]->dev_addr, ETH_ALEN);

	rtnl_lock();
	err = register_netdevice(xpp->mux.nd);
	rtnl_unlock();
	if (err) {
		free_netdev(xpp->mux.nd);
		xpp->mux.nd = NULL;
		return err;
	}

	err = xeth_mux_add_all_lowers(xpp);
	if (err) {
		xeth_mux_del_all_lowers(xpp);
		unregister_netdev(xpp->mux.nd);
		xpp->mux.nd = NULL;
	}

	return err;
}

void xeth_mux_unregister(struct xeth_platform_priv *xpp)
{
	LIST_HEAD(q);
	int bkt;

	if (IS_ERR_OR_NULL(xpp->mux.nd) ||
	    xpp->mux.nd->reg_state != NETREG_REGISTERED)
		return;

	rtnl_lock();

	_xeth_mux_del_all_lowers(xpp);

	rcu_read_lock();
	for (bkt = 0; bkt < xeth_mux_upper_hash_bkts; bkt++)
		xeth_upper_queue_unregister(&xpp->mux.upper_hl[bkt], &q);
	rcu_read_unlock();

	unregister_netdevice_many(&q);

	rtnl_unlock();

	rcu_barrier();

	unregister_netdev(xpp->mux.nd);
	xpp->mux.nd = NULL;
}

bool xeth_mux_is_lower_rcu(struct net_device *nd)
{
	struct net_device *upper;
	struct list_head *uppers;

	netdev_for_each_upper_dev_rcu(nd, upper, uppers)
		if (xeth_is_mux(upper))
			return true;
	return false;
}

int xeth_mux_queue_xmit(struct sk_buff *skb)
{
	struct net_device *mux = NULL;
	struct net_device *lower;
	struct list_head *lowers;

	netdev_for_each_lower_dev(skb->dev, lower, lowers)
		if (xeth_is_mux(lower)) {
			mux = lower;
			break;
		}
	if (!mux) {
		kfree_skb_list(skb);
	} else if (mux->flags & IFF_UP) {
		skb->dev = mux;
		dev_queue_xmit(skb);
	} else {
		struct xeth_mux_priv *xmp = netdev_priv(mux);
		atomic64_inc(&xmp->xpp->mux.link_stats.tx_errors);
		atomic64_inc(&xmp->xpp->mux.link_stats.tx_carrier_errors);
		kfree_skb_list(skb);
	}
	return NETDEV_TX_OK;
}
