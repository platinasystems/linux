/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

enum {
	xeth_upper_rxqs = 1,
	xeth_upper_txqs = 1,
	xeth_upper_link_n_stats =
		sizeof(struct rtnl_link_stats64)/sizeof(__u64),
	xeth_upper_et_stats_sz = sizeof(u64) * xeth_max_et_stats,
};

struct xeth_upper_priv {
	struct hlist_node __rcu	node;
	struct net_device *nd;
	struct rcu_head rcu;
	struct xeth_platform_priv *xpp;
	struct xeth_atomic_link_stats link_stats;
	struct ethtool_link_ksettings et_settings;
	struct i2c_client *qsfp;
	int qsfp_bus;
	u32 xid;
	u32 et_priv_flags;
	u8 kind;
	atomic64_t et_stats[];
};

int xeth_upper_qsfp_bus(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	return xup->qsfp_bus;
}

struct i2c_client *xeth_upper_qsfp(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	return xup->qsfp;
}

void xeth_upper_set_qsfp(struct net_device *upper, struct i2c_client *qsfp)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	xup->qsfp = qsfp;
}

struct net_device *xeth_upper_with_qsfp_bus(struct xeth_platform_priv *xpp,
					    int nr)
{
	struct xeth_upper_priv *xup = NULL;
	struct net_device *upper = NULL;
	struct hlist_head __rcu *head;
	int bkt;

	rcu_read_lock();
	head = xeth_indexed_upper_head(xpp, 0);
	for (bkt = 0; head; head = xeth_indexed_upper_head(xpp, ++bkt))
		hlist_for_each_entry_rcu(xup, head, node)
			if (!upper && xup->qsfp_bus == nr)
				upper = xup->nd;
	rcu_read_unlock();
	return upper;
}

struct net_device *xeth_upper_lookup_rcu(struct xeth_platform_priv *xpp,
					 u32 xid)
{
	struct xeth_upper_priv *xup = NULL;
	struct hlist_head __rcu *head;

	head = xeth_hashed_upper_head(xpp, xid);
	if (head)
		hlist_for_each_entry_rcu(xup, head, node)
			if (xup->xid == xid)
				return xup->nd;
	return NULL;
}

static s64 xeth_upper_search(struct xeth_platform_priv *xpp,
			     u32 base, u32 range)
{
	u32 xid, limit;

	for (xid = base, limit = base + range; xid < limit; xid++)
		if (!xeth_debug_rcu(xeth_upper_lookup_rcu(xpp, xid)))
			return xid;
	return -ENOSPC;
}

static int xeth_upper_add_rcu(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	struct hlist_head __rcu *head =
		xeth_hashed_upper_head(xup->xpp, xup->xid);
	xeth_add_node(xup->xpp, &xup->node, head);
	return 0;
}

static void xeth_upper_drop_carrier_cb(struct rcu_head *rcu)
{
	struct xeth_upper_priv *xup =
		container_of(rcu, struct xeth_upper_priv, rcu);
	netif_carrier_off(xup->nd);
}

static void xeth_upper_dump_ifinfo_cb(struct rcu_head *rcu)
{
	struct xeth_upper_priv *xup =
		container_of(rcu, struct xeth_upper_priv, rcu);
	struct in_device *in_dev;
	struct inet6_dev *in6_dev;
	struct in_ifaddr *ifa;
	struct inet6_ifaddr *ifa6;

	xeth_sbtx_ifinfo(xup->xpp, xup->nd, xup->kind, xup->xid, 0,
			 XETH_IFINFO_REASON_DUMP);

	if (xup->kind == XETH_DEV_KIND_PORT) {
		xeth_sbtx_et_settings(xup->xpp, xup->xid,
				      &xup->et_settings);
		if (xup->et_priv_flags)
			xeth_sbtx_et_flags(xup->xpp, xup->xid,
					   xup->et_priv_flags);
	}

	in_dev = in_dev_get(xup->nd);
	if (in_dev) {
		for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next)
			xeth_sbtx_ifa(xup->xpp, ifa, xup->xid, NETDEV_UP);
		in_dev_put(in_dev);
	}

	in6_dev = in6_dev_get(xup->nd);
	if (in6_dev) {
		read_lock_bh(&in6_dev->lock);
		list_for_each_entry(ifa6, &in6_dev->addr_list, if_list)
			xeth_sbtx_ifa6(xup->xpp, ifa6, xup->xid, NETDEV_UP);
		read_unlock_bh(&in6_dev->lock);
		in6_dev_put(in6_dev);
	}
}

static void xeth_upper_dump_lowers_cb(struct rcu_head *rcu)
{
	struct xeth_upper_priv *xup =
		container_of(rcu, struct xeth_upper_priv, rcu);
	struct net_device *lower;
	struct list_head *iter;

	netdev_for_each_lower_dev(xup->nd, lower, iter) {
		struct xeth_upper_priv *xlp = netdev_priv(lower);
		xeth_sbtx_change_upper(xup->xpp, xup->xid, xlp->xid, true);
	}
}

static void xeth_upper_reset_stats_cb(struct rcu_head *rcu)
{
	struct xeth_upper_priv *xup =
		container_of(rcu, struct xeth_upper_priv, rcu);
	int i;

	xeth_reset_link_stats(&xup->link_stats);
	if (xup->kind == XETH_DEV_KIND_PORT)
		for (i = 0; i < xup->xpp->n_et_stats; i++)
			atomic64_set(&xup->et_stats[0], 0LL);
}

static netdev_tx_t xeth_upper_encap_vlan(struct net_device *upper,
					 struct sk_buff *skb)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	u16 tpid = cpu_to_be16(ETH_P_8021Q);
	const u16 vidmask = (1 << 12) - 1;

	if (xup->kind == XETH_DEV_KIND_VLAN) {
		u16 vid = (u16)(xup->xid / VLAN_N_VID) & vidmask;
		skb = vlan_insert_tag_set_proto(skb, tpid, vid);
		if (skb) {
			tpid = cpu_to_be16(ETH_P_8021AD);
			vid = (u16)(xup->xid) & vidmask;
			skb = vlan_insert_tag_set_proto(skb, tpid, vid);
		}
	} else {
		u16 vid = (u16)(xup->xid) & vidmask;
		skb = vlan_insert_tag_set_proto(skb, tpid, vid);
	}
	return skb ? xeth_mux_queue_xmit(skb) : NETDEV_TX_OK;
}

static int xeth_upper_ndo_open(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	int err = dev_open(xup->xpp->mux.nd, NULL);
	if (err)
		return err;
	netif_carrier_off(upper);
	return xeth_sbtx_ifinfo(xup->xpp, upper, xup->kind, xup->xid,
				upper->flags, XETH_IFINFO_REASON_UP);
}

static int xeth_upper_ndo_stop(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);

	/* netif_carrier_off() through xeth_sbrx_carrier() */
	return xeth_sbtx_ifinfo(xup->xpp, upper, xup->kind, xup->xid,
				upper->flags, XETH_IFINFO_REASON_DOWN);
}

static netdev_tx_t xeth_upper_ndo_xmit(struct sk_buff *skb,
				       struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);

	if (netif_carrier_ok(upper))
		switch (xup->xpp->config->encap) {
		case XETH_ENCAP_VLAN:
			return xeth_upper_encap_vlan(upper, skb);
		}
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int xeth_upper_ndo_get_iflink(const struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);

	return xup->xpp->mux.nd->ifindex;
}

static int xeth_upper_ndo_get_iflink_vlan(const struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	u32 stag = xup->xid & ((1 << xup->xpp->config->n_mux_bits) - 1);
	struct net_device *iflink = xeth_upper_lookup_rcu(xup->xpp, stag);
	return iflink ? iflink->ifindex : 0;
}

static void xeth_upper_ndo_get_stats64(struct net_device *upper,
				       struct rtnl_link_stats64 *dst)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	xeth_get_link_stats(dst, &xup->link_stats);
}

static int xeth_upper_ndo_add_lower(struct net_device *upper,
				    struct net_device *lower,
				    struct netlink_ext_ack *extack)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	struct xeth_upper_priv *xlp;
	int err = 0;

	if (!xeth_upper_check(lower)) {
		NL_SET_ERR_MSG(extack, "This device may only enslave another xeth");
		return -EOPNOTSUPP;
	}
	xlp = netdev_priv(lower);
	if (xlp->kind == XETH_DEV_KIND_BRIDGE) {
		NL_SET_ERR_MSG(extack, "Cannot enslave a bridge");
		return -EOPNOTSUPP;
	}
	if (netdev_master_upper_dev_get(lower))
		return -EBUSY;

	call_netdevice_notifiers(NETDEV_JOIN, lower);

	err = netdev_master_upper_dev_link(lower, upper, NULL, NULL, extack);
	if (err)
		return err;

	lower->flags |= IFF_SLAVE;

	return xeth_sbtx_change_upper(xup->xpp, xup->xid, xlp->xid, true);
}

static int xeth_upper_ndo_del_lower(struct net_device *upper,
				    struct net_device *lower)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	struct xeth_upper_priv *xlp;
	int err = 0;

	err = xeth_debug_nd_err(lower, !xeth_upper_check(lower) ? -EINVAL : 0);
	if (err) {
		return err;
	}
	xlp = netdev_priv(lower);
	lower->priv_flags &= ~(IFF_BRIDGE_PORT|IFF_TEAM_PORT);
	lower->flags &= ~IFF_SLAVE;
	netdev_upper_dev_unlink(lower, upper);
	netdev_update_features(upper);
	return xeth_sbtx_change_upper(xup->xpp, xup->xid, xlp->xid, false);
}

static int xeth_upper_ndo_change_mtu(struct net_device *upper, int mtu)
{
	upper->mtu = mtu;
	return 0;
}

static const struct net_device_ops xeth_upper_ndo_port = {
	.ndo_open = xeth_upper_ndo_open,
	.ndo_stop = xeth_upper_ndo_stop,
	.ndo_start_xmit = xeth_upper_ndo_xmit,
	.ndo_get_iflink = xeth_upper_ndo_get_iflink,
	.ndo_get_stats64 = xeth_upper_ndo_get_stats64,
	.ndo_change_mtu = xeth_upper_ndo_change_mtu,
};

static const struct net_device_ops xeth_upper_ndo_vlan = {
	.ndo_open = xeth_upper_ndo_open,
	.ndo_stop = xeth_upper_ndo_stop,
	.ndo_start_xmit = xeth_upper_ndo_xmit,
	.ndo_get_iflink = xeth_upper_ndo_get_iflink_vlan,
	.ndo_get_stats64 = xeth_upper_ndo_get_stats64,
	.ndo_change_mtu = xeth_upper_ndo_change_mtu,
};

static const struct net_device_ops xeth_upper_ndo_bridge_or_lag = {
	.ndo_open = xeth_upper_ndo_open,
	.ndo_stop = xeth_upper_ndo_stop,
	.ndo_start_xmit = xeth_upper_ndo_xmit,
	.ndo_get_iflink = xeth_upper_ndo_get_iflink,
	.ndo_get_stats64 = xeth_upper_ndo_get_stats64,
	.ndo_add_slave = xeth_upper_ndo_add_lower,
	.ndo_del_slave = xeth_upper_ndo_del_lower,
};

static void xeth_upper_eto_get_drvinfo(struct net_device *upper,
				       struct ethtool_drvinfo *drvinfo)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	u8 muxbits = xup->xpp->config->n_mux_bits;

	strlcpy(drvinfo->driver, xeth_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, xeth_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	if (xup->xid >= (1 << muxbits))
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, " %u, %u",
			  xup->xid & ((1 << muxbits)-1),
			  xup->xid >> muxbits);
	else
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u",
			  xup->xid);
	drvinfo->n_priv_flags = xup->xpp->config->n_et_flags;
	drvinfo->n_stats = xup->xpp->n_et_stats;
}

static int xeth_upper_eto_get_sset_count(struct net_device *upper, int sset)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);

	if (xup->kind != XETH_DEV_KIND_PORT)
		return 0;
	switch (sset) {
	case ETH_SS_PRIV_FLAGS:
		return xup->xpp->config->n_et_flags;
	case ETH_SS_STATS:
		return xup->xpp->n_et_stats;
	case ETH_SS_TEST:
		return 0;
	}
	return -EOPNOTSUPP;
}

static void xeth_upper_eto_get_strings(struct net_device *upper,
				       u32 sset, u8 *data)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	char *p = (char *)data;
	int i;

	if (xup->kind != XETH_DEV_KIND_PORT)
		return;
	switch (sset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		if (!xup->xpp->n_et_stats)
			break;
		for (i = 0; i < xup->xpp->n_et_stats; i++) {
			strlcpy(p, xup->xpp->et_stat_names +
				(i * ETH_GSTRING_LEN), ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		if (!xup->xpp->config->n_et_flags)
			break;
		for (i = 0;
		     xup->xpp->config->et_flag_names &&
		     	xup->xpp->config->et_flag_names[i];
		     i++) {
			strlcpy(p, xup->xpp->config->et_flag_names[i],
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xeth_upper_eto_get_stats(struct net_device *upper,
				     struct ethtool_stats *stats,
				     u64 *data)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	int i;

	if (xup->kind == XETH_DEV_KIND_PORT)
		for (i = 0; i < xup->xpp->n_et_stats; i++)
			data[i] = atomic64_read(&xup->et_stats[i]);
}

static u32 xeth_upper_eto_get_priv_flags(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);

	return xup->et_priv_flags;
}

static int xeth_upper_eto_set_priv_flags(struct net_device *upper, u32 flags)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);

	if (flags >= (1 << xup->xpp->config->n_et_flags))
		return -EINVAL;

	xup->et_priv_flags = flags;
	xeth_sbtx_et_flags(xup->xpp, xup->xid, flags);

	return 0;
}

static int xeth_upper_eto_get_link_ksettings(struct net_device *upper,
					     struct ethtool_link_ksettings *p)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	memcpy(p, &xup->et_settings, sizeof(*p));
	return 0;
}

static int xeth_upper_validate_port(struct net_device *upper, u8 port)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	struct ethtool_link_ksettings *ks = &xup->et_settings;
	bool t = false;

	switch (port) {
	case PORT_TP:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, TP);
		break;
	case PORT_AUI:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, AUI);
		break;
	case PORT_MII:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, MII);
		break;
	case PORT_FIBRE:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, FIBRE);
		break;
	case PORT_BNC:
		t = ethtool_link_ksettings_test_link_mode(ks, supported, BNC);
		break;
	case PORT_DA:
	case PORT_NONE:
	case PORT_OTHER:
		t = true;
		break;
	}
	return t ? 0 : -EINVAL;
}

static int xeth_upper_validate_duplex(struct net_device *upper, u8 duplex)
{
	return xeth_debug_nd_err(upper,
				 duplex != DUPLEX_HALF &&
				 duplex != DUPLEX_FULL &&
				 duplex != DUPLEX_UNKNOWN) ?
		-EINVAL : 0;
}

static int xeth_upper_validate_speed(struct net_device *upper, u32 speed)
{
        return xeth_debug_nd_err(upper,
				 speed != 100000 &&
				 speed != 50000 &&
				 speed != 40000 &&
				 speed != 25000 &&
				 speed != 20000 &&
				 speed != 10000 &&
				 speed != 1000) ?
		-EINVAL : 0;
}

static int xeth_upper_eto_set_link_ksettings(struct net_device *upper,
					     const struct ethtool_link_ksettings *req)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	struct ethtool_link_ksettings *settings;
	int err;

	settings = &xup->et_settings;
	if (req->base.port != settings->base.port) {
		err = xeth_upper_validate_port(upper, req->base.port);
		if (err)
			return err;
		settings->base.port = req->base.port;
	}
	if (req->base.autoneg == AUTONEG_DISABLE) {
		err = xeth_upper_validate_speed(upper, req->base.speed);
		if (err)
			return err;
		err = xeth_upper_validate_duplex(upper, req->base.duplex);
		if (err)
			return err;
		settings->base.autoneg = req->base.autoneg;
		settings->base.speed = req->base.speed;
		settings->base.duplex = req->base.duplex;
	} else {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(res);
		if (bitmap_andnot(res, req->link_modes.advertising,
				  settings->link_modes.supported,
				  __ETHTOOL_LINK_MODE_MASK_NBITS)) {
			return -EINVAL;
		} else {
			err = xeth_upper_validate_duplex(upper,
							 req->base.duplex);
			if (err)
				return err;
			bitmap_copy(settings->link_modes.advertising,
				    req->link_modes.advertising,
				    __ETHTOOL_LINK_MODE_MASK_NBITS);
			settings->base.autoneg = AUTONEG_ENABLE;
			settings->base.speed = 0;
			settings->base.duplex = req->base.duplex;
		}
	}

	return xeth_sbtx_et_settings(xup->xpp, xup->xid, settings);
}

static int xeth_upper_eto_get_fecparam(struct net_device *upper,
				       struct ethtool_fecparam *param)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	const u32 fec_both = ETHTOOL_FEC_RS | ETHTOOL_FEC_BASER;
	struct ethtool_link_ksettings *ks;

	ks = &xup->et_settings;
	param->fec = 0;
	param->active_fec = 0;
	if (xeth_supports(ks, FEC_NONE))
		param->fec |= ETHTOOL_FEC_OFF;
	if (xeth_supports(ks, FEC_RS))
		param->fec |= ETHTOOL_FEC_RS;
	if (xeth_supports(ks, FEC_BASER))
		param->fec |= ETHTOOL_FEC_BASER;
	if ((param->fec & fec_both) == fec_both)
		param->fec |= ETHTOOL_FEC_AUTO;
	if (!param->fec)
		param->fec = ETHTOOL_FEC_NONE;
	if (param->fec == ETHTOOL_FEC_NONE)
		param->active_fec = ETHTOOL_FEC_NONE;
	else if (xeth_advertising(ks, FEC_NONE))
		param->active_fec = ETHTOOL_FEC_OFF;
	else if (xeth_advertising(ks, FEC_RS))
		param->active_fec = xeth_advertising(ks, FEC_BASER) ?
			ETHTOOL_FEC_AUTO : ETHTOOL_FEC_RS;
	else if (xeth_advertising(ks, FEC_BASER))
		param->fec = ETHTOOL_FEC_BASER;
	return 0;
}

static int xeth_upper_eto_set_fecparam(struct net_device *upper,
				       struct ethtool_fecparam *param)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	struct ethtool_link_ksettings *ks;

	ks = &xup->et_settings;
	switch (param->fec) {
	case ETHTOOL_FEC_AUTO:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_RS) ||
		    !ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_BASER)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	case ETHTOOL_FEC_OFF:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_NONE)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	case ETHTOOL_FEC_RS:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_RS)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	case ETHTOOL_FEC_BASER:
		if (!ethtool_link_ksettings_test_link_mode(ks, supported,
							   FEC_BASER)) {
			return -EOPNOTSUPP;
		}
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     FEC_RS);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
		break;
	default:
		return -EINVAL;
	}
	return xeth_sbtx_et_settings(xup->xpp, xup->xid, ks);
}

static const struct ethtool_ops xeth_upper_et_ops = {
	.get_drvinfo = xeth_upper_eto_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = xeth_upper_eto_get_sset_count,
	.get_strings = xeth_upper_eto_get_strings,
	.get_ethtool_stats = xeth_upper_eto_get_stats,
	.get_priv_flags = xeth_upper_eto_get_priv_flags,
	.set_priv_flags = xeth_upper_eto_set_priv_flags,
	.get_module_info = xeth_qsfp_get_module_info,
	.get_module_eeprom = xeth_qsfp_get_module_eeprom,
	.get_link_ksettings = xeth_upper_eto_get_link_ksettings,
	.set_link_ksettings = xeth_upper_eto_set_link_ksettings,
	.get_fecparam = xeth_upper_eto_get_fecparam,
	.set_fecparam = xeth_upper_eto_set_fecparam,
};

static void xeth_upper_lnko_setup_port(struct net_device *upper)
{
	ether_setup(upper);
	upper->netdev_ops = &xeth_upper_ndo_port;
	upper->ethtool_ops = &xeth_upper_et_ops;
	upper->needs_free_netdev = true;
	upper->priv_destructor = NULL;
	upper->priv_flags &= ~IFF_TX_SKB_SHARING;
	upper->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	upper->priv_flags |= IFF_NO_QUEUE;
	upper->priv_flags |= IFF_PHONY_HEADROOM;
}

static void xeth_upper_lnko_setup_vlan(struct net_device *upper)
{
	ether_setup(upper);
	upper->netdev_ops = &xeth_upper_ndo_vlan;
	upper->needs_free_netdev = true;
	upper->priv_destructor = NULL;
	upper->priv_flags &= ~IFF_TX_SKB_SHARING;
	upper->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	upper->priv_flags |= IFF_NO_QUEUE;
	upper->priv_flags |= IFF_PHONY_HEADROOM;
}

static void xeth_upper_lnko_setup_bridge_or_lag(struct net_device *upper)
{
	ether_setup(upper);
	upper->netdev_ops = &xeth_upper_ndo_bridge_or_lag;
	upper->needs_free_netdev = true;
	upper->priv_destructor = NULL;
	upper->flags |= IFF_MASTER;
	upper->priv_flags &= ~IFF_TX_SKB_SHARING;
	upper->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	upper->priv_flags |= IFF_NO_QUEUE;
	upper->priv_flags |= IFF_PHONY_HEADROOM;
	upper->features |= NETIF_F_NETNS_LOCAL;
}

static int xeth_upper_lnko_validate_vlan(struct nlattr *tb[],
					 struct nlattr *data[],
					 struct netlink_ext_ack *extack)
{
	if (tb && tb[IFLA_ADDRESS]) {
		NL_SET_ERR_MSG(extack, "cannot set mac addr");
		return -EOPNOTSUPP;
	}
	if (data && data[XETH_VLAN_IFLA_VID]) {
		u16 vid = nla_get_u16(data[XETH_VLAN_IFLA_VID]);
		if (vid == 0 || vid >= VLAN_N_VID) {
			NL_SET_ERR_MSG(extack, "out-of-range VID");
			return -ERANGE;
		}
	}
	return 0;
}

static int xeth_upper_lnko_validate_bridge_or_lag(struct nlattr *tb[],
						  struct nlattr *data[],
						  struct netlink_ext_ack *ack)
{
	if (tb && tb[IFLA_ADDRESS]) {
		NL_SET_ERR_MSG(ack, "cannot set mac addr");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int xeth_upper_register(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	int err;

	xeth_debug_rcu(xeth_upper_add_rcu(upper));
	err = register_netdevice(upper);
	if (err)
		xeth_del_node(xup->xpp, &xup->node);
	else
		err = xeth_sbtx_ifinfo(xup->xpp, upper, xup->kind, xup->xid,
				       0, XETH_IFINFO_REASON_NEW);
	return err;
}

/**
 * xeth_upper_lnko_new_vlan() - create xeth mux upper proxy of a remote netdev
 *
 * Here is how to create a vlan of an existing XETH_PORT device,
 *	ip link add [[name ]xethPORT[-SUBPORT][.VID]] link xethPORT[-SUBPORT] \
 *		type xeth-vlan [vid VID]
 *
 * Without VID, this searches for an unused VID beginning with 1.
 * Without IFNAME it's dubbed xethPORT[-SUBPORT].VID.
 *
 */
static int xeth_upper_lnko_new_vlan(struct net *src_net, struct net_device *nd,
				    struct nlattr *tb[], struct nlattr *data[],
				    struct netlink_ext_ack *extack)
{
	struct xeth_upper_priv *xup = netdev_priv(nd);
	struct net_device *xnd, *link;
	struct xeth_upper_priv *linkpriv;
	u8 muxbits;
	u32 li, xid, range;
	int i, err;
	unsigned long long ull;


	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing link");
		return -EINVAL;
	}

	xup->nd = nd;
	xup->kind = XETH_DEV_KIND_VLAN;
	xup->xpp = xeth_platform_priv_of_lnko(nd->rtnl_link_ops, vlan);
	nd->min_mtu = xup->xpp->mux.nd->min_mtu;
	nd->max_mtu = xup->xpp->mux.nd->max_mtu;

	li = nla_get_u32(tb[IFLA_LINK]);
	link = dev_get_by_index_rcu(dev_net(nd), li);
	if (IS_ERR_OR_NULL(link)) {
		NL_SET_ERR_MSG(extack, "link must be an XETH_PORT");
		return PTR_ERR(link);
	}
	linkpriv = netdev_priv(link);
	muxbits = xup->xpp->config->n_mux_bits;
	range = (1 << muxbits) - 1;
	nd->addr_assign_type = NET_ADDR_STOLEN;
	memcpy(nd->dev_addr, link->dev_addr, ETH_ALEN);

	if (data && data[XETH_VLAN_IFLA_VID])
		xid  = linkpriv->xid |
			(nla_get_u16(data[XETH_VLAN_IFLA_VID]) << muxbits);
	else
		for (i = xid = 0; !xid; i++)
			if (i >= IFNAMSIZ) {
				u32 base = linkpriv->xid | (1 << muxbits);
				s64 x = xeth_upper_search(xup->xpp,
							  base, range);
				if (x < 0) {
					NL_SET_ERR_MSG(extack,
						       "no VID available");
					return (int)x;
				}
				xid = (u32)x;
			} else if (nd->name[i] == '.') {
				err = kstrtoull(nd->name+i+1, 0, &ull);
				if (err)
					return err;
				if (!ull || ull > range) {
					NL_SET_ERR_MSG(extack, "invalid name");
					return -ERANGE;
				}
				xid  = linkpriv->xid | (ull << muxbits);
			}
	xnd = xeth_debug_rcu(xeth_upper_lookup_rcu(xup->xpp, xid));
	if (xnd) {
		NL_SET_ERR_MSG(extack, "VID in use");
		return -EBUSY;
	}
	xup->xid = xid;
	if (!tb || !tb[IFLA_IFNAME])
		scnprintf(nd->name, IFNAMSIZ, "%s.%u",
			  link->name, xup->xid >> muxbits);
	return xeth_upper_register(nd);
}

static int xeth_upper_new_bridge_or_lag(struct net *src_net,
					struct net_device *nd,
					struct nlattr *tb[],
					struct nlattr *data[],
					struct netlink_ext_ack *extack,
					u8 kind)
{
	struct xeth_upper_priv *xup = netdev_priv(nd);
	const struct rtnl_link_ops *lnko = nd->rtnl_link_ops;
	u32 range = (1 << xup->xpp->config->n_mux_bits) - 1;
	s64 xid_or_err =
		xeth_upper_search(xup->xpp, xup->xpp->config->base_xid, range);

	xup->nd = nd;
	if (kind == XETH_DEV_KIND_BRIDGE)
		xup->xpp = xeth_platform_priv_of_lnko(lnko, bridge);
	else
		xup->xpp = xeth_platform_priv_of_lnko(lnko, lag);
	if (xid_or_err < 0) {
		NL_SET_ERR_MSG(extack, "all XIDs in use");
		return (int)xid_or_err;
	}
	xup->xid = (u32)xid_or_err;
	xup->kind = kind;
	nd->min_mtu = xup->xpp->mux.nd->min_mtu;
	nd->max_mtu = xup->xpp->mux.nd->max_mtu;
	if (!tb || !tb[IFLA_IFNAME])
		scnprintf(nd->name, IFNAMSIZ, "xeth-%u", xup->xid);
	nd->addr_assign_type = NET_ADDR_STOLEN;
	memcpy(nd->dev_addr, xup->xpp->mux.nd->dev_addr, ETH_ALEN);
	return xeth_upper_register(nd);
}

/*
 * Here is how to add a bridge device to the xeth mux with iproute2,
 *	ip link add [[name ]IFNAME] type xeth-bridge
 *
 * Without an IFNAME it's dubbed xeth-XID.
 */
static int xeth_upper_lnko_new_bridge(struct net *src_net,
				      struct net_device *nd,
				      struct nlattr *tb[],
				      struct nlattr *data[],
				      struct netlink_ext_ack *extack)
{
	return xeth_upper_new_bridge_or_lag(src_net, nd, tb, data, extack,
					    XETH_DEV_KIND_BRIDGE);
}

/*
 * Here is how to add a bridge device to the xeth mux with iproute2,
 *	ip link add [[name ]IFNAME] type xeth-lag
 *
 * Without an IFNAME it's dubbed xeth-XID.
 */
static int xeth_upper_lnko_new_lag(struct net *src_net,
				   struct net_device *nd,
				   struct nlattr *tb[],
				   struct nlattr *data[],
				   struct netlink_ext_ack *extack)
{
	return xeth_upper_new_bridge_or_lag(src_net, nd, tb, data, extack,
					    XETH_DEV_KIND_LAG);
}

static void xeth_upper_lnko_del(struct net_device *nd, struct list_head *q)
{
	struct xeth_upper_priv *xup = netdev_priv(nd);
	u32 xid = xeth_upper_xid(nd);
	enum xeth_dev_kind kind = xeth_upper_kind(nd);
	struct net_device *upper;
	struct list_head *uppers;

	netdev_for_each_upper_dev_rcu(nd, upper, uppers)
		xeth_upper_ndo_del_lower(upper, nd);

	xeth_sbtx_ifinfo(xup->xpp, nd, kind, xid, 0, XETH_IFINFO_REASON_DEL);
	xeth_del_node(xup->xpp, &xup->node);
	unregister_netdevice_queue(nd, q);
}

static void xeth_upper_lnko_del_bridge_or_lag(struct net_device *nd,
					      struct list_head *q)
{
	struct net_device *lower;
	struct list_head *lowers;

	netdev_for_each_lower_dev(nd, lower, lowers)
		xeth_upper_ndo_del_lower(nd, lower);

	xeth_upper_lnko_del(nd, q);
}

static struct net *xeth_upper_lnko_get_net(const struct net_device *nd)
{
	return dev_net(nd);
}

static const struct nla_policy xeth_upper_nla_policy_vlan[XETH_VLAN_N_IFLA] = {
	[XETH_VLAN_IFLA_VID] = { .type = NLA_U16 },
};

const static struct rtnl_link_ops xeth_upper_lnko_vlan = {
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup_vlan,
	.validate	= xeth_upper_lnko_validate_vlan,
	.newlink	= xeth_upper_lnko_new_vlan,
	.dellink	= xeth_upper_lnko_del,
	.get_link_net	= xeth_upper_lnko_get_net,
	.policy		= xeth_upper_nla_policy_vlan,
	.maxtype	= XETH_VLAN_N_IFLA - 1,
};

const static struct rtnl_link_ops xeth_upper_lnko_bridge = {
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup_bridge_or_lag,
	.validate	= xeth_upper_lnko_validate_bridge_or_lag,
	.newlink	= xeth_upper_lnko_new_bridge,
	.dellink	= xeth_upper_lnko_del_bridge_or_lag,
	.get_link_net	= xeth_upper_lnko_get_net,
};

const static struct rtnl_link_ops xeth_upper_lnko_lag = {
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup_bridge_or_lag,
	.validate	= xeth_upper_lnko_validate_bridge_or_lag,
	.newlink	= xeth_upper_lnko_new_lag,
	.dellink	= xeth_upper_lnko_del_bridge_or_lag,
	.get_link_net	= xeth_upper_lnko_get_net,
};

static bool xeth_upper_lnko_registered(struct rtnl_link_ops *lnko)
{
	return lnko->list.next || lnko->list.prev;
}

#define xeth_upper_lnko_register(xpp,k)					\
({									\
	struct rtnl_link_ops *lnko = &xpp->k##_lnko;			\
	int _err;							\
	memcpy(lnko, &xeth_upper_lnko_##k, sizeof(*lnko));		\
	scnprintf(xpp->k##_kind, xeth_drvr_kind_sz, "%s-%s",		\
		  xpp->config->name, #k);				\
	lnko->kind = xpp->k##_kind;					\
	_err = rtnl_link_register(lnko);				\
	if (!_err)							\
		_err = xeth_upper_lnko_registered(lnko) ?  0 : -EINVAL;	\
	(_err);								\
})

int xeth_upper_register_drivers(struct xeth_platform_priv *xpp)
{
	int err = xeth_upper_lnko_register(xpp, bridge);
	if (!err)
		err = xeth_upper_lnko_register(xpp, lag);
	if (!err)
		err = xeth_upper_lnko_register(xpp, vlan);
	return err;
}

#define xeth_upper_lnko_unregister(xpp,k)				\
do {									\
	if (xeth_upper_lnko_registered(&xpp->k##_lnko))			\
		rtnl_link_unregister(&xpp->k##_lnko);			\
} while(0)

void xeth_upper_unregister_drivers(struct xeth_platform_priv *xpp)
{
	/* WARNING must deinit the bridges and lags first so that they
	 * release all vlan lowers before those are unregistered */
	xeth_upper_lnko_unregister(xpp, bridge);
	xeth_upper_lnko_unregister(xpp, lag);
	xeth_upper_lnko_unregister(xpp, vlan);
}

bool xeth_upper_check(struct net_device *nd)
{
	return	nd->netdev_ops->ndo_open == xeth_upper_ndo_open &&
		nd->netdev_ops->ndo_stop == xeth_upper_ndo_stop;
}

static void xeth_upper_call_for_all(struct xeth_platform_priv *xpp,
				    rcu_callback_t cb)
{
	int bkt;
	struct hlist_head __rcu *head;

	rcu_read_lock();
	head = xeth_indexed_upper_head(xpp, 0);
	for (bkt = 0; head; head = xeth_indexed_upper_head(xpp, ++bkt)) {
		struct xeth_upper_priv *xup = NULL;
		hlist_for_each_entry_rcu(xup, head, node)
			call_rcu(&xup->rcu, cb);
	}
	rcu_read_unlock();
	rcu_barrier();
}

static void xeth_upper_call_for_each_kind(struct xeth_platform_priv *xpp,
					  rcu_callback_t cb,
					  u8 kind)
{
	int bkt;
	struct hlist_head __rcu *head;

	rcu_read_lock();
	head = xeth_indexed_upper_head(xpp, 0);
	for (bkt = 0; head; head = xeth_indexed_upper_head(xpp, ++bkt)) {
		struct xeth_upper_priv *xup = NULL;
		hlist_for_each_entry_rcu(xup, head, node)
			if (xup->kind == kind)
				call_rcu(&xup->rcu, cb);
	}
	rcu_read_unlock();
	rcu_barrier();
}

void xeth_upper_drop_all_carrier(struct xeth_platform_priv *xpp)
{
	xeth_upper_call_for_all(xpp, xeth_upper_drop_carrier_cb);
}

void xeth_upper_dump_all_ifinfo(struct xeth_platform_priv *xpp)
{
	/* We *must* dump in this order: ports, lags, vlans, then bridges as
	 * the control daemon needs to know all of the ports before the vlans
	 * that link those ports. Similar for lags that aggregate ports but may
	 * then be muxed to vlans. And finally, bridges that may have any of
	 * these kinds as members.
	 */
	xeth_upper_call_for_each_kind(xpp, xeth_upper_dump_ifinfo_cb,
				      XETH_DEV_KIND_PORT);
	xeth_upper_call_for_each_kind(xpp, xeth_upper_dump_ifinfo_cb,
				      XETH_DEV_KIND_LAG);
	xeth_upper_call_for_each_kind(xpp, xeth_upper_dump_ifinfo_cb,
					  XETH_DEV_KIND_VLAN);
	xeth_upper_call_for_each_kind(xpp, xeth_upper_dump_ifinfo_cb,
				      XETH_DEV_KIND_BRIDGE);
	xeth_upper_call_for_each_kind(xpp, xeth_upper_dump_lowers_cb,
				      XETH_DEV_KIND_LAG);
	xeth_upper_call_for_each_kind(xpp, xeth_upper_dump_lowers_cb,
				      XETH_DEV_KIND_BRIDGE);
}

void xeth_upper_reset_all_stats(struct xeth_platform_priv *xpp)
{
	xeth_upper_call_for_all(xpp, xeth_upper_reset_stats_cb);
}

void xeth_upper_changemtu(struct xeth_platform_priv *xpp, int mtu, int max_mtu)
{
	/* FIXME should we relay or nack mtu changes */
}

void xeth_upper_et_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_upper_priv *xup = netdev_priv(nd);

	if (index < xup->xpp->n_et_stats)
		atomic64_set(&xup->et_stats[index], count);
	else
		xeth_counter_inc(xup->xpp, sbrx_invalid);
}

void xeth_upper_link_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_upper_priv *xup = netdev_priv(nd);

	switch (index) {
	case xeth_link_stat_rx_packets_index:
		atomic64_set(&xup->link_stats.rx_packets, count);
		break;
	case xeth_link_stat_tx_packets_index:
		atomic64_set(&xup->link_stats.tx_packets, count);
		break;
	case xeth_link_stat_rx_bytes_index:
		atomic64_set(&xup->link_stats.rx_bytes, count);
		break;
	case xeth_link_stat_tx_bytes_index:
		atomic64_set(&xup->link_stats.tx_bytes, count);
		break;
	case xeth_link_stat_rx_errors_index:
		atomic64_set(&xup->link_stats.rx_errors, count);
		break;
	case xeth_link_stat_tx_errors_index:
		atomic64_set(&xup->link_stats.tx_errors, count);
		break;
	case xeth_link_stat_rx_dropped_index:
		atomic64_set(&xup->link_stats.rx_dropped, count);
		break;
	case xeth_link_stat_tx_dropped_index:
		atomic64_set(&xup->link_stats.tx_dropped, count);
		break;
	case xeth_link_stat_multicast_index:
		atomic64_set(&xup->link_stats.multicast, count);
		break;
	case xeth_link_stat_collisions_index:
		atomic64_set(&xup->link_stats.collisions, count);
		break;
	case xeth_link_stat_rx_length_errors_index:
		atomic64_set(&xup->link_stats.rx_length_errors, count);
		break;
	case xeth_link_stat_rx_over_errors_index:
		atomic64_set(&xup->link_stats.rx_over_errors, count);
		break;
	case xeth_link_stat_rx_crc_errors_index:
		atomic64_set(&xup->link_stats.rx_crc_errors, count);
		break;
	case xeth_link_stat_rx_frame_errors_index:
		atomic64_set(&xup->link_stats.rx_frame_errors, count);
		break;
	case xeth_link_stat_rx_fifo_errors_index:
		atomic64_set(&xup->link_stats.rx_fifo_errors, count);
		break;
	case xeth_link_stat_rx_missed_errors_index:
		atomic64_set(&xup->link_stats.rx_missed_errors, count);
		break;
	case xeth_link_stat_tx_aborted_errors_index:
		atomic64_set(&xup->link_stats.tx_aborted_errors, count);
		break;
	case xeth_link_stat_tx_carrier_errors_index:
		atomic64_set(&xup->link_stats.tx_carrier_errors, count);
		break;
	case xeth_link_stat_tx_fifo_errors_index:
		atomic64_set(&xup->link_stats.tx_fifo_errors, count);
		break;
	case xeth_link_stat_tx_heartbeat_errors_index:
		atomic64_set(&xup->link_stats.tx_heartbeat_errors, count);
		break;
	case xeth_link_stat_tx_window_errors_index:
		atomic64_set(&xup->link_stats.tx_window_errors, count);
		break;
	case xeth_link_stat_rx_compressed_index:
		atomic64_set(&xup->link_stats.rx_compressed, count);
		break;
	case xeth_link_stat_tx_compressed_index:
		atomic64_set(&xup->link_stats.tx_compressed, count);
		break;
	case xeth_link_stat_rx_nohandler_index:
		atomic64_set(&xup->link_stats.rx_nohandler, count);
		break;
	default:
		xeth_counter_inc(xup->xpp, sbrx_invalid);
	}
}

void xeth_upper_speed(struct net_device *upper, u32 mbps)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	xup->et_settings.base.speed = mbps;
}

void xeth_upper_queue_unregister(struct hlist_head __rcu *head,
				 struct list_head *q)
{
	struct xeth_upper_priv *xup = NULL;
	hlist_for_each_entry_rcu(xup, head, node)
		xeth_upper_lnko_del(xup->nd, q);
}

u32 xeth_upper_xid(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	return xup->xid;
}

enum xeth_dev_kind xeth_upper_kind(struct net_device *upper)
{
	struct xeth_upper_priv *xup = netdev_priv(upper);
	return xup->kind;
}

/**
 * xeth_upper_new - create and add an upper port proxy to the xeth mux
 *
 * @name:	IFNAMSIZ buffer
 * @xid:	A unique and immutable xeth device identifier; if zero,
 *		the device is assigned the next available xid
 * @ea:		Ethernet Address, if zero, it's assigned a random address
 * @setup	An initialization call-back
 *
 * Returns 0 on success or <0 error.
 */
int xeth_upper_new_port(struct xeth_platform_priv *xpp,
			const char *name,
			u32 xid,
			u64 ea,
			void (*setup)(struct ethtool_link_ksettings *),
			int qsfp_bus)
{
	struct net_device *port;	/* or subport */
	struct xeth_upper_priv *xup;
	size_t priv_sz = sizeof(*xup) + xeth_upper_et_stats_sz;
	int err;

	if (IS_ERR(xeth_indexed_upper_head(xpp, 0)))
		return -ENODEV;

	if (xeth_upper_lookup_rcu(xpp, xid) != NULL)
		return -EEXIST;

	port = alloc_netdev_mqs(priv_sz, name, NET_NAME_USER,
				 xeth_upper_lnko_setup_port,
				 xeth_upper_txqs,
				 xeth_upper_rxqs);
	if (IS_ERR(port))
		return PTR_ERR(port);

	port->min_mtu = xpp->mux.nd->min_mtu;
	port->max_mtu = xpp->mux.nd->max_mtu;

	xup = netdev_priv(port);
	xup->nd = port;
	xup->xpp = xpp;
	xup->xid = xid;
	xup->kind = XETH_DEV_KIND_PORT;
	xup->qsfp_bus = qsfp_bus;

	if (ea) {
		u64_to_ether_addr(ea, port->dev_addr);
		port->addr_assign_type = NET_ADDR_PERM;
	} else
		eth_hw_addr_random(port);

	setup(&xup->et_settings);

	xeth_debug_rcu(xeth_upper_add_rcu(port));

	rtnl_lock();
	err = register_netdevice(port);
	rtnl_unlock();

	if (err < 0) {
		xeth_del_node(xup->xpp, &xup->node);
		return err;
	}

	no_xeth_debug_nd(port, "new: xid %u, mac %pM", xid, port->dev_addr);

	return 0;
}

void xeth_upper_delete_port(struct xeth_platform_priv *xpp, u32 xid)
{
	struct net_device *port;
	struct xeth_upper_priv *xup;

	port = xeth_upper_lookup_rcu(xpp, xid);
	if (IS_ERR_OR_NULL(port))
		return;
	xup = netdev_priv(port);
	xeth_debug("%s xid %u", port->name, xid);
	xeth_del_node(xpp, &xup->node);
	unregister_netdev(port);
}
