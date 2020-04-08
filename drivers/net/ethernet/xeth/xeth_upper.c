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
};

struct xeth_upper_priv {
	struct hlist_node __rcu	node;
	u32 xid;
	u8 kind;
	struct rcu_head rcu;
	struct {
		struct ethtool_link_ksettings settings;
		u32 priv_flags;
		u16 n_priv_flags;
		u16 n_stats;
		u64 stats[xeth_n_ethtool_stats];
	} ethtool;
	struct xeth_atomic_link_stats link_stats;
};

static int xeth_upper_ethtool_n_priv_flags(struct xeth_upper_priv *priv)
{
	if (!priv->ethtool.n_priv_flags)
		priv->ethtool.n_priv_flags =
			xeth_kstrs_count(&xeth_ethtool_flag_names);
	return priv->ethtool.n_priv_flags;
}

static int xeth_upper_ethtool_n_stats(struct xeth_upper_priv *priv)
{
	if (!priv->ethtool.n_stats)
		priv->ethtool.n_stats =
			xeth_kstrs_count(&xeth_ethtool_stat_names);
	return priv->ethtool.n_stats;
}

struct net_device *xeth_upper_lookup_rcu(u32 xid)
{
	struct hlist_head __rcu *head;
	struct xeth_upper_priv *priv = NULL;

	head = xeth_mux_upper_head_hashed(xid);
	if (head)
		hlist_for_each_entry_rcu(priv, head, node)
			if (priv->xid == xid)
				return xeth_netdev(priv);
	return NULL;
}

static s64 xeth_upper_search(u32 base, u32 range)
{
	u32 xid, limit;

	for (xid = base, limit = base + range; xid < limit; xid++)
		if (!xeth_debug_rcu(xeth_upper_lookup_rcu(xid)))
			return xid;
	return -ENOSPC;
}

static void xeth_upper_steal_mux_addr(struct net_device *nd)
{
	nd->addr_assign_type = NET_ADDR_STOLEN;
	memcpy(nd->dev_addr, xeth_mux->dev_addr, ETH_ALEN);
}

static struct net_device *xeth_upper_link_rcu(u32 ifindex)
{
	int bkt;
	struct xeth_upper_priv *priv = NULL;
	struct hlist_head __rcu *head = xeth_mux_upper_head_indexed(0);

	for (bkt = 0; head; head = xeth_mux_upper_head_indexed(++bkt))
		hlist_for_each_entry_rcu(priv, head, node) {
			struct net_device *nd = xeth_netdev(priv);
			if (nd->ifindex == ifindex)
				return nd;
		}
	return ERR_PTR(-ENODEV);
}

static int xeth_upper_add_rcu(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	struct hlist_head __rcu *head = xeth_mux_upper_head_hashed(priv->xid);
	xeth_mux_add_node(&priv->node, head);
	return 0;
}

static void xeth_upper_cb_carrier_off(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu);
	struct net_device *nd = xeth_netdev(priv);
	netif_carrier_off(nd);
}

static void xeth_upper_cb_dump_ifinfo(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu);
	struct net_device *nd = xeth_netdev(priv);
	struct in_device *in_dev = in_dev_get(nd);
	struct inet6_dev *in6_dev = in6_dev_get(nd);

	xeth_sbtx_ifinfo(nd, priv->xid, priv->kind, 0,
			 XETH_IFINFO_REASON_DUMP);

	if (priv->kind == XETH_DEV_KIND_PORT) {
		xeth_sbtx_ethtool_settings(priv->xid, &priv->ethtool.settings);
		if (priv->ethtool.priv_flags)
			xeth_sbtx_ethtool_flags(priv->xid,
						priv->ethtool.priv_flags);
	}

	if (in_dev) {
		struct in_ifaddr *ifa;

		for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next)
			xeth_sbtx_ifa(ifa, priv->xid, NETDEV_UP);
		in_dev_put(in_dev);
	}

	if (in6_dev) {
		struct inet6_ifaddr *ifa6;

		read_lock_bh(&in6_dev->lock);
		list_for_each_entry(ifa6, &in6_dev->addr_list, if_list)
			xeth_sbtx_ifa6(ifa6, priv->xid, NETDEV_UP);
		read_unlock_bh(&in6_dev->lock);
		in6_dev_put(in6_dev);
	}
}

static void xeth_upper_cb_dump_lowers(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu);
	struct net_device *nd = xeth_netdev(priv);
	struct net_device *lower;
	struct list_head *iter;

	netdev_for_each_lower_dev(nd, lower, iter) {
		struct xeth_upper_priv *lower_priv = netdev_priv(lower);
		xeth_sbtx_change_upper(priv->xid, lower_priv->xid, true);
	}
}

static void xeth_upper_cb_reset_stats(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu);
	int ethtool_n_stats = xeth_upper_ethtool_n_stats(priv);
	int i;

	xeth_reset_link_stats(&priv->link_stats);
	for (i = 0; i < ethtool_n_stats; i++)
		priv->ethtool.stats[i] = 0;
}

static netdev_tx_t xeth_upper_encap_vlan(struct sk_buff *skb,
					 struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u16 tpid = cpu_to_be16(ETH_P_8021Q);
	u16 pcp = (u16)(skb->priority) << VLAN_PRIO_SHIFT;
	const u16 vidmask = (1 << 12) - 1;

	if (priv->kind == XETH_DEV_KIND_VLAN) {
		u16 vid = (u16)(priv->xid / VLAN_N_VID) & vidmask;
		skb = vlan_insert_tag_set_proto(skb, tpid, pcp | vid);
		if (skb) {
			tpid = cpu_to_be16(ETH_P_8021AD);
			vid = (u16)(priv->xid) & vidmask;
			skb = vlan_insert_tag_set_proto(skb, tpid, pcp | vid);
		}
	} else {
		u16 vid = (u16)(priv->xid) & vidmask;
		skb = vlan_insert_tag_set_proto(skb, tpid, pcp | vid);
	}
	return skb ? xeth_mux_queue_xmit(skb) : NETDEV_TX_OK;
}

static int xeth_upper_ndo_open(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	int err = xeth_debug_nd_err(xeth_mux, dev_open(xeth_mux));
	if (err)
		return err;
	netif_carrier_off(nd);
	return xeth_sbtx_ifinfo(nd, priv->xid, priv->kind, nd->flags,
				XETH_IFINFO_REASON_UP);
}

static int xeth_upper_ndo_stop(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	/* netif_carrier_off() through xeth_sbrx_carrier() */
	return xeth_sbtx_ifinfo(nd, priv->xid, priv->kind, nd->flags,
				XETH_IFINFO_REASON_DOWN);
}

static netdev_tx_t xeth_upper_ndo_xmit(struct sk_buff *skb,
				       struct net_device *nd)
{
	switch (xeth_encap) {
	case XETH_ENCAP_VLAN:
		return xeth_upper_encap_vlan(skb, nd);
	}
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int xeth_upper_ndo_get_iflink(const struct net_device *nd)
{
	return xeth_mux_ifindex();
}

static int xeth_upper_ndo_get_iflink_vlan(const struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u32 stag = priv->xid & ((1 << xeth_mux_bits()) - 1);
	struct net_device *iflink = xeth_upper_lookup_rcu(stag);
	return iflink ? iflink->ifindex : 0;
}

static void xeth_upper_ndo_get_stats64(struct net_device *nd,
				       struct rtnl_link_stats64 *dst)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	xeth_get_link_stats(dst, &priv->link_stats);
}

static int xeth_upper_ndo_add_lower(struct net_device *upper_nd,
				    struct net_device *lower_nd,
				    struct netlink_ext_ack *extack)
{
	struct xeth_upper_priv *upper_priv = netdev_priv(upper_nd);
	struct xeth_upper_priv *lower_priv;
	int err = 0;

	if (!xeth_upper_check(lower_nd)) {
		NL_SET_ERR_MSG(extack, "This device may only enslave another xeth");
		return -EOPNOTSUPP;
	}
	lower_priv = netdev_priv(lower_nd);
	if (lower_priv->kind == XETH_DEV_KIND_BRIDGE) {
		NL_SET_ERR_MSG(extack, "Cannot enslave a bridge");
		return -EOPNOTSUPP;
	}
	if (netdev_master_upper_dev_get(lower_nd))
		return -EBUSY;

	call_netdevice_notifiers(NETDEV_JOIN, lower_nd);

	err = netdev_master_upper_dev_link(lower_nd, upper_nd, NULL, NULL, extack);
	if (err)
		return err;

	if (upper_priv->kind == XETH_DEV_KIND_BRIDGE)
		lower_nd->priv_flags |= IFF_BRIDGE_PORT;
	else if (upper_priv->kind == XETH_DEV_KIND_LAG)
		lower_nd->priv_flags |= IFF_TEAM_PORT;

	lower_nd->flags |= IFF_SLAVE;

	return xeth_sbtx_change_upper(upper_priv->xid, lower_priv->xid, true);
}

static int xeth_upper_ndo_del_lower(struct net_device *upper_nd,
				    struct net_device *lower_nd)
{
	struct xeth_upper_priv *upper_priv = netdev_priv(upper_nd);
	struct xeth_upper_priv *lower_priv;
	int err = 0;

	err = xeth_debug_nd_err(lower_nd,
				!xeth_upper_check(lower_nd) ? -EINVAL : 0);
	if (err) {
		return err;
	}
	lower_priv = netdev_priv(lower_nd);
	lower_nd->priv_flags &= ~(IFF_BRIDGE_PORT|IFF_TEAM_PORT);
	lower_nd->flags &= ~IFF_SLAVE;
	netdev_upper_dev_unlink(lower_nd, upper_nd);
	netdev_update_features(upper_nd);
	return xeth_sbtx_change_upper(upper_priv->xid, lower_priv->xid, false);
}

static int xeth_upper_ndo_change_mtu(struct net_device *upper_nd, int mtu)
{
	upper_nd->mtu = mtu;
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

static void xeth_upper_eto_get_drvinfo(struct net_device *nd,
				       struct ethtool_drvinfo *drvinfo)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u8 muxbits = xeth_mux_bits();

	strlcpy(drvinfo->driver, xeth_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, xeth_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	if (priv->xid >= (1 << muxbits))
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, " %u, %u",
			  priv->xid & ((1 << muxbits)-1),
			  priv->xid >> muxbits);
	else
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u",
			  priv->xid);
	drvinfo->n_priv_flags = xeth_upper_ethtool_n_priv_flags(priv);
	drvinfo->n_stats = xeth_upper_ethtool_n_stats(priv);
}

static int xeth_upper_eto_get_sset_count(struct net_device *nd, int sset)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	switch (sset) {
	case ETH_SS_PRIV_FLAGS:
		return xeth_upper_ethtool_n_priv_flags(priv);
	case ETH_SS_STATS:
		return xeth_upper_ethtool_n_stats(priv);
	case ETH_SS_TEST:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void xeth_upper_eto_get_strings(struct net_device *nd,
				       u32 sset, u8 *data)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	char *p = (char *)data;
	size_t i, n;

	switch (sset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		n = xeth_upper_ethtool_n_stats(priv);
		for (i = 0; i < n; i++) {
			xeth_kstrs_string(&xeth_ethtool_stat_names, p,
					  ETH_GSTRING_LEN, i);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		n = xeth_upper_ethtool_n_priv_flags(priv);
		for (i = 0; i < n; i++) {
			xeth_kstrs_string(&xeth_ethtool_flag_names, p,
					  ETH_GSTRING_LEN, i);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xeth_upper_eto_get_stats(struct net_device *nd,
				     struct ethtool_stats *stats,
				     u64 *data)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	size_t sz = xeth_upper_ethtool_n_stats(priv) *
		sizeof(*priv->ethtool.stats);
	memcpy(data, &priv->ethtool.stats, sz);
}

static u32 xeth_upper_eto_get_priv_flags(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	return priv->ethtool.priv_flags;
}

static int xeth_upper_eto_set_priv_flags(struct net_device *nd, u32 flags)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	size_t n = xeth_upper_ethtool_n_priv_flags(priv);

	if (flags >= (1 << n))
		return -EINVAL;

	priv->ethtool.priv_flags = flags;
	xeth_sbtx_ethtool_flags(priv->xid, flags);

	return 0;
}

static int xeth_upper_eto_get_link_ksettings(struct net_device *nd,
					     struct ethtool_link_ksettings *p)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	memcpy(p, &priv->ethtool.settings, sizeof(*p));

	return 0;
}

static int xeth_upper_validate_port(struct net_device *nd, u8 port)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.settings;
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

static int xeth_upper_validate_duplex(struct net_device *nd, u8 duplex)
{
	return xeth_debug_nd_err(nd,
				 duplex != DUPLEX_HALF &&
				 duplex != DUPLEX_FULL &&
				 duplex != DUPLEX_UNKNOWN) ?
		-EINVAL : 0;
}

static int xeth_upper_validate_speed(struct net_device *nd, u32 speed)
{
        return xeth_debug_nd_err(nd,
				 speed != 100000 &&
				 speed != 50000 &&
				 speed != 40000 &&
				 speed != 25000 &&
				 speed != 20000 &&
				 speed != 10000 &&
				 speed != 1000) ?
		-EINVAL : 0;
}

static int xeth_upper_eto_set_link_ksettings(struct net_device *nd,
					     const struct ethtool_link_ksettings *req)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *settings;
	int err;

	settings = &priv->ethtool.settings;
	if (req->base.port != settings->base.port) {
		err = xeth_upper_validate_port(nd, req->base.port);
		if (err)
			return err;
		settings->base.port = req->base.port;
	}
	if (req->base.autoneg == AUTONEG_DISABLE) {
		err = xeth_upper_validate_speed(nd, req->base.speed);
		if (err)
			return err;
		err = xeth_upper_validate_duplex(nd, req->base.duplex);
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
			err = xeth_upper_validate_duplex(nd, req->base.duplex);
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

	return xeth_sbtx_ethtool_settings(priv->xid, settings);
}

static int xeth_upper_eto_get_fecparam(struct net_device *nd,
				       struct ethtool_fecparam *param)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.settings;
	const u32 fec_both = ETHTOOL_FEC_RS | ETHTOOL_FEC_BASER;

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

static int xeth_upper_eto_set_fecparam(struct net_device *nd,
				       struct ethtool_fecparam *param)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.settings;

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
		xeth_debug_nd(nd, "set fec auto");
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
		xeth_debug_nd(nd, "set fec none");
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
		xeth_debug_nd(nd, "set fec rs");
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
		xeth_debug_nd(nd, "set fec baser");
		break;
	default:
		return -EINVAL;
	}
	return xeth_sbtx_ethtool_settings(priv->xid, ks);
}

static const struct ethtool_ops xeth_upper_ethtool_ops = {
	.get_drvinfo = xeth_upper_eto_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = xeth_upper_eto_get_sset_count,
	.get_strings = xeth_upper_eto_get_strings,
	.get_ethtool_stats = xeth_upper_eto_get_stats,
	.get_priv_flags = xeth_upper_eto_get_priv_flags,
	.set_priv_flags = xeth_upper_eto_set_priv_flags,
	.get_link_ksettings = xeth_upper_eto_get_link_ksettings,
	.set_link_ksettings = xeth_upper_eto_set_link_ksettings,
	.get_fecparam = xeth_upper_eto_get_fecparam,
	.set_fecparam = xeth_upper_eto_set_fecparam,
};

static void xeth_upper_lnko_setup_port(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth_upper_ndo_port;
	nd->ethtool_ops = &xeth_upper_ethtool_ops;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
#ifdef __FIXME__
	nd->features |= NETIF_F_LLTX;
	nd->features |= VETH_FEATURES;

	nd->hw_features = VETH_FEATURES;
	nd->hw_enc_features = VETH_FEATURES;
	nd->mpls_features = NETIF_F_HW_CSUM | NETIF_F_GSO_SOFTWARE;
#endif /* __FIXEME__ */
	nd->min_mtu = xeth_mux->min_mtu;
	nd->max_mtu = xeth_mux->max_mtu;
}

static void xeth_upper_lnko_setup_vlan(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth_upper_ndo_vlan;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
#ifdef __FIXME__
	nd->features |= NETIF_F_LLTX;
	nd->features |= VETH_FEATURES;

	nd->hw_features = VETH_FEATURES;
	nd->hw_enc_features = VETH_FEATURES;
	nd->mpls_features = NETIF_F_HW_CSUM | NETIF_F_GSO_SOFTWARE;
#endif /* __FIXEME__ */
}

static void xeth_upper_lnko_setup_bridge_or_lag(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth_upper_ndo_bridge_or_lag;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	nd->flags |= IFF_MASTER;
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
#ifdef __FIXME__
	nd->features |= NETIF_F_LLTX;
	nd->features |= VETH_FEATURES;

	nd->hw_features = VETH_FEATURES;
	nd->hw_enc_features = VETH_FEATURES;
	nd->mpls_features = NETIF_F_HW_CSUM | NETIF_F_GSO_SOFTWARE;
#endif /* __FIXEME__ */
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

static int xeth_upper_nd_register(struct net_device *nd)
{
	int err;

	xeth_debug_rcu(xeth_upper_add_rcu(nd));
	err = xeth_debug_nd_err(nd, register_netdevice(nd));
	if (err) {
		struct xeth_upper_priv *priv = netdev_priv(nd);
		xeth_mux_del_node(&priv->node);
	} else {
		struct xeth_upper_priv *priv = netdev_priv(nd);
		err = xeth_sbtx_ifinfo(nd, priv->xid, priv->kind, 0,
				       XETH_IFINFO_REASON_NEW);
	}
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
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u8 muxbits = xeth_mux_bits();
	u32 xid, range = (1 << muxbits) - 1;
	struct net_device *xnd, *link;
	struct xeth_upper_priv *linkpriv;
	int err;
	u32 li;

	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing link");
		return -EINVAL;
	}

	priv->kind = XETH_DEV_KIND_VLAN;

	li = nla_get_u32(tb[IFLA_LINK]);
	link = xeth_debug_rcu(xeth_upper_link_rcu(li));
	if (IS_ERR_OR_NULL(link)) {
		NL_SET_ERR_MSG(extack, "link must be an XETH_PORT");
		return PTR_ERR(link);
	}
	linkpriv = netdev_priv(link);
	nd->addr_assign_type = NET_ADDR_STOLEN;
	memcpy(nd->dev_addr, link->dev_addr, ETH_ALEN);

	if (data && data[XETH_VLAN_IFLA_VID]) {
		xid  = linkpriv->xid |
			(nla_get_u16(data[XETH_VLAN_IFLA_VID]) << muxbits);
		xnd = xeth_debug_rcu(xeth_upper_lookup_rcu(xid));
		if (xnd) {
			NL_SET_ERR_MSG(extack, "VID in use");
			return -EBUSY;
		}
		priv->xid = xid;
	} else {
		unsigned long long ull;
		int i = 0;
		while(true)
			if (i >= IFNAMSIZ) {
				u32 base = linkpriv->xid | (1 << muxbits);
				s64 x = xeth_upper_search(base, range);
				if (x < 0) {
					NL_SET_ERR_MSG(extack,
						       "no VID available");
					return (int)x;
				}
				priv->xid = (u32)x;
				break;
			} else if (nd->name[i] == '.') {
				err = kstrtoull(nd->name+i+1, 0, &ull);
				if (err)
					return err;
				if (!ull || ull > range) {
					NL_SET_ERR_MSG(extack, "invalid name");
					return -ERANGE;
				}
				priv->xid  = linkpriv->xid |
					(ull << muxbits);
				break;
			} else
				i++;
	}
	if (!tb || !tb[IFLA_IFNAME])
		scnprintf(nd->name, IFNAMSIZ, "%s.%u",
			  link->name, priv->xid >> muxbits);
	return xeth_upper_nd_register(nd);
}

static int xeth_upper_new_bridge_or_lag(struct net *src_net,
					struct net_device *nd,
					struct nlattr *tb[],
					struct nlattr *data[],
					struct netlink_ext_ack *extack,
					u8 kind)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u8 muxbits = xeth_mux_bits();
	u32 range = (1 << muxbits) - 1;
	s64 xid_or_err = xeth_upper_search(xeth_base_xid, range);

	if (xid_or_err < 0) {
		NL_SET_ERR_MSG(extack, "all XIDs in use");
		return (int)xid_or_err;
	}
	priv->xid = (u32)xid_or_err;
	priv->kind = kind;
	if (!tb || !tb[IFLA_IFNAME])
		scnprintf(nd->name, IFNAMSIZ, "xeth-%u", priv->xid);
	xeth_upper_steal_mux_addr(nd);
	return xeth_upper_nd_register(nd);
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
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u32 xid = xeth_upper_xid(nd);
	enum xeth_dev_kind kind = xeth_upper_kind(nd);

	xeth_sbtx_ifinfo(nd, xid, kind, 0, XETH_IFINFO_REASON_DEL);
	xeth_mux_del_node(&priv->node);
	unregister_netdevice_queue(nd, q);
}

static void xeth_upper_lnko_del_bridge_or_lag(struct net_device *nd, struct list_head *q)
{
	struct net_device *lower;
	struct list_head *lowers;

	netdev_for_each_lower_dev(nd, lower, lowers)
		xeth_upper_ndo_del_lower(nd, lower);

	xeth_upper_lnko_del(nd, q);
}

static struct net *xeth_upper_lnko_get_net(const struct net_device *nd)
{
	/* FIXME this should return dev_net of iflink
	 * (e.g. xeth_mux or vlans real dev */
	return dev_net(nd);
}

static const struct nla_policy xeth_upper_nla_policy_vlan[XETH_VLAN_N_IFLA] = {
	[XETH_VLAN_IFLA_VID] = { .type = NLA_U16 },
};

static struct rtnl_link_ops xeth_upper_lnko_vlan = {
	.kind		= "xeth-vlan",
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup_vlan,
	.validate	= xeth_upper_lnko_validate_vlan,
	.newlink	= xeth_upper_lnko_new_vlan,
	.dellink	= xeth_upper_lnko_del,
	.get_link_net	= xeth_upper_lnko_get_net,
	.policy		= xeth_upper_nla_policy_vlan,
	.maxtype	= XETH_VLAN_N_IFLA - 1,
};

static struct rtnl_link_ops xeth_upper_lnko_bridge = {
	.kind		= "xeth-bridge",
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup_bridge_or_lag,
	.validate	= xeth_upper_lnko_validate_bridge_or_lag,
	.newlink	= xeth_upper_lnko_new_bridge,
	.dellink	= xeth_upper_lnko_del_bridge_or_lag,
	.get_link_net	= xeth_upper_lnko_get_net,
};

static struct rtnl_link_ops xeth_upper_lnko_lag = {
	.kind		= "xeth-lag",
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup_bridge_or_lag,
	.validate	= xeth_upper_lnko_validate_bridge_or_lag,
	.newlink	= xeth_upper_lnko_new_lag,
	.dellink	= xeth_upper_lnko_del_bridge_or_lag,
	.get_link_net	= xeth_upper_lnko_get_net,
};

bool xeth_upper_check(struct net_device *nd)
{
	return	nd->netdev_ops->ndo_open == xeth_upper_ndo_open &&
		nd->netdev_ops->ndo_stop == xeth_upper_ndo_stop;
}

static bool xeth_upper_lnko_is_registered(struct rtnl_link_ops *lnko)
{
	return lnko->list.next || lnko->list.prev;
}

static int xeth_upper_lnko_register(struct rtnl_link_ops *lnko, int err)
{
	if (err)
		return err;
	err = rtnl_link_register(lnko);
	if (!err)
		err = xeth_upper_lnko_is_registered(lnko) ? 0 : -EINVAL;
	return err;
}

static void xeth_upper_lnko_unregister(struct rtnl_link_ops *lnko)
{
	if (xeth_upper_lnko_is_registered(lnko))
		rtnl_link_unregister(lnko);
}

__init int xeth_upper_init(void)
{
	int err = 0;
	
	err = xeth_upper_lnko_register(&xeth_upper_lnko_bridge, err);
	err = xeth_upper_lnko_register(&xeth_upper_lnko_lag, err);
	err = xeth_upper_lnko_register(&xeth_upper_lnko_vlan, err);
	return err;
}

int xeth_upper_deinit(int err)
{
	/* WARNING must deinit the bridges and lags first so that they
	 * release all vlan lowers before those are unregistered */
	xeth_upper_lnko_unregister(&xeth_upper_lnko_bridge);
	xeth_upper_lnko_unregister(&xeth_upper_lnko_lag);
	xeth_upper_lnko_unregister(&xeth_upper_lnko_vlan);
	return err;
}

static void xeth_upper_call_rcu_for_each_priv(rcu_callback_t cb)
{
	int bkt;
	struct xeth_upper_priv *priv = NULL;
	struct hlist_head __rcu *head = xeth_mux_upper_head_indexed(0);

	rcu_read_lock();
	for (bkt = 0; head; head = xeth_mux_upper_head_indexed(++bkt))
		hlist_for_each_entry_rcu(priv, head, node)
			call_rcu(&priv->rcu, cb);
	rcu_read_unlock();
	rcu_barrier();
}

static void xeth_upper_call_rcu_for_each_priv_kind(rcu_callback_t cb, u8 kind)
{
	int bkt;
	struct xeth_upper_priv *priv = NULL;
	struct hlist_head __rcu *head = xeth_mux_upper_head_indexed(0);

	rcu_read_lock();
	for (bkt = 0; head; head = xeth_mux_upper_head_indexed(++bkt))
		hlist_for_each_entry_rcu(priv, head, node)
			if (priv->kind == kind)
				call_rcu(&priv->rcu, cb);
	rcu_read_unlock();
	rcu_barrier();
}

void xeth_upper_all_carrier_off(void)
{
	xeth_upper_call_rcu_for_each_priv(xeth_upper_cb_carrier_off);
}

void xeth_upper_all_dump_ifinfo(void)
{
	/* We *must* dump in this order: ports, lags, vlans, then bridges as
	 * the control daemon needs to know all of the ports before the vlans
	 * that link those ports. Similar for lags that aggregate ports but may
	 * then be muxed to vlans. And finally, bridges that may have any of
	 * these kinds as members.
	 */
	xeth_upper_call_rcu_for_each_priv_kind(xeth_upper_cb_dump_ifinfo,
					       XETH_DEV_KIND_PORT);
	xeth_upper_call_rcu_for_each_priv_kind(xeth_upper_cb_dump_ifinfo,
					       XETH_DEV_KIND_LAG);
	xeth_upper_call_rcu_for_each_priv_kind(xeth_upper_cb_dump_ifinfo,
					       XETH_DEV_KIND_VLAN);
	xeth_upper_call_rcu_for_each_priv_kind(xeth_upper_cb_dump_ifinfo,
					       XETH_DEV_KIND_BRIDGE);
	xeth_upper_call_rcu_for_each_priv_kind(xeth_upper_cb_dump_lowers,
					       XETH_DEV_KIND_LAG);
	xeth_upper_call_rcu_for_each_priv_kind(xeth_upper_cb_dump_lowers,
					       XETH_DEV_KIND_BRIDGE);
}

void xeth_upper_all_reset_stats(void)
{
	xeth_upper_call_rcu_for_each_priv(xeth_upper_cb_reset_stats);
}

void xeth_upper_changemtu(int mtu, int max_mtu)
{
#ifdef __FIXME__
#endif /* __FIXME__ */
}

void xeth_upper_ethtool_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	if (index < xeth_upper_ethtool_n_stats(priv))
		priv->ethtool.stats[index] = count;
	else
		xeth_counter_inc(sbrx_invalid);
}

void xeth_upper_link_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	switch (index) {
	case xeth_link_stat_rx_packets_index:
		atomic64_inc(&priv->link_stats.rx_packets);
		break;
	case xeth_link_stat_tx_packets_index:
		atomic64_inc(&priv->link_stats.tx_packets);
		break;
	case xeth_link_stat_rx_bytes_index:
		atomic64_inc(&priv->link_stats.rx_bytes);
		break;
	case xeth_link_stat_tx_bytes_index:
		atomic64_inc(&priv->link_stats.tx_bytes);
		break;
	case xeth_link_stat_rx_errors_index:
		atomic64_inc(&priv->link_stats.rx_errors);
		break;
	case xeth_link_stat_tx_errors_index:
		atomic64_inc(&priv->link_stats.tx_errors);
		break;
	case xeth_link_stat_rx_dropped_index:
		atomic64_inc(&priv->link_stats.rx_dropped);
		break;
	case xeth_link_stat_tx_dropped_index:
		atomic64_inc(&priv->link_stats.tx_dropped);
		break;
	case xeth_link_stat_multicast_index:
		atomic64_inc(&priv->link_stats.multicast);
		break;
	case xeth_link_stat_collisions_index:
		atomic64_inc(&priv->link_stats.collisions);
		break;
	case xeth_link_stat_rx_length_errors_index:
		atomic64_inc(&priv->link_stats.rx_length_errors);
		break;
	case xeth_link_stat_rx_over_errors_index:
		atomic64_inc(&priv->link_stats.rx_over_errors);
		break;
	case xeth_link_stat_rx_crc_errors_index:
		atomic64_inc(&priv->link_stats.rx_crc_errors);
		break;
	case xeth_link_stat_rx_frame_errors_index:
		atomic64_inc(&priv->link_stats.rx_frame_errors);
		break;
	case xeth_link_stat_rx_fifo_errors_index:
		atomic64_inc(&priv->link_stats.rx_fifo_errors);
		break;
	case xeth_link_stat_rx_missed_errors_index:
		atomic64_inc(&priv->link_stats.rx_missed_errors);
		break;
	case xeth_link_stat_tx_aborted_errors_index:
		atomic64_inc(&priv->link_stats.tx_aborted_errors);
		break;
	case xeth_link_stat_tx_carrier_errors_index:
		atomic64_inc(&priv->link_stats.tx_carrier_errors);
		break;
	case xeth_link_stat_tx_fifo_errors_index:
		atomic64_inc(&priv->link_stats.tx_fifo_errors);
		break;
	case xeth_link_stat_tx_heartbeat_errors_index:
		atomic64_inc(&priv->link_stats.tx_heartbeat_errors);
		break;
	case xeth_link_stat_tx_window_errors_index:
		atomic64_inc(&priv->link_stats.tx_window_errors);
		break;
	case xeth_link_stat_rx_compressed_index:
		atomic64_inc(&priv->link_stats.rx_compressed);
		break;
	case xeth_link_stat_tx_compressed_index:
		atomic64_inc(&priv->link_stats.tx_compressed);
		break;
	case xeth_link_stat_rx_nohandler_index:
		atomic64_inc(&priv->link_stats.rx_nohandler);
		break;
	default:
		xeth_counter_inc(sbrx_invalid);
	}
}

void xeth_upper_speed(struct net_device *nd, u32 mbps)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	priv->ethtool.settings.base.speed = mbps;
}

void xeth_upper_queue_unregister(struct hlist_head __rcu *head,
				 struct list_head *q)
{
	struct xeth_upper_priv *priv = NULL;
	hlist_for_each_entry_rcu(priv, head, node)
		xeth_upper_lnko_del(xeth_netdev(priv), q);
}

u32 xeth_upper_xid(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	return priv->xid;
}

enum xeth_dev_kind xeth_upper_kind(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	return priv->kind;
}

s64 xeth_create_port(const char *name, u32 xid, u64 ea,
		     void (*cb)(struct ethtool_link_ksettings *))
{
	struct net_device *nd;
	struct xeth_upper_priv *priv;
	s64 x;

	if (IS_ERR(xeth_mux_upper_head_indexed(0)))
		return -ENODEV;

	if (xeth_debug_err(xeth_upper_lookup_rcu(xid) != NULL))
		return -EBUSY;

	nd = xeth_debug_ptr_err(alloc_netdev_mqs(sizeof(*priv),
						 name,
						 NET_NAME_USER,
						 xeth_upper_lnko_setup_port,
						 xeth_upper_txqs,
						 xeth_upper_rxqs));
	if (IS_ERR(nd))
		return PTR_ERR(nd);

	priv = netdev_priv(nd);

	priv->xid = xid;
	priv->kind = XETH_DEV_KIND_PORT;
	xeth_debug_rcu(xeth_upper_add_rcu(nd));

	if (ea) {
		u64_to_ether_addr(ea, nd->dev_addr);
		nd->addr_assign_type = NET_ADDR_PERM;
	} else
		eth_hw_addr_random(nd);

	rtnl_lock();
	x = xeth_debug_err(register_netdevice(nd));
	rtnl_unlock();

	if (x < 0) {
		xeth_mux_del_node(&priv->node);
		return x;
	}

	if (cb)
		cb(&priv->ethtool.settings);

	xeth_debug("%s xid %u mac %pM", name, xid, nd->dev_addr);
	return xid;
}
EXPORT_SYMBOL(xeth_create_port);

void xeth_delete_port(u32 xid)
{
	struct net_device *nd;
	struct xeth_upper_priv *priv;
	
	nd = xeth_upper_lookup_rcu(xid);
	if (IS_ERR_OR_NULL(nd))
		return;
	priv = netdev_priv(nd);
	xeth_debug("%s xid %u", nd->name, xid);
	xeth_mux_del_node(&priv->node);
	unregister_netdev(nd);
}
EXPORT_SYMBOL(xeth_delete_port);

int xeth_set_ethtool_flag_names(const char *const names[])
{
	return xeth_kstrs_copy(&xeth_ethtool_flag_names, names);
}
EXPORT_SYMBOL(xeth_set_ethtool_flag_names);

int xeth_set_ethtool_stat_names(const char *const names[])
{
	return xeth_kstrs_copy(&xeth_ethtool_stat_names, names);
}
EXPORT_SYMBOL(xeth_set_ethtool_stat_names);

