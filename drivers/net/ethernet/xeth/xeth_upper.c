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
	xeth_upper_ethtool_flags = 32,
	xeth_upper_ethtool_stats = 512,
	xeth_upper_link_stats = sizeof(struct rtnl_link_stats64)/sizeof(__u64),
};

enum xeth_upper_rcu {
	xeth_upper_rcu_carrier_off,
	xeth_upper_rcu_reset_stats,
	xeth_upper_rcu_dump_ifinfo,
};

struct xeth_upper_priv {
	struct hlist_node __rcu	node;
	struct spinlock mutex;
	u32 xid;
	u8 kind;
	struct {
		struct rcu_head carrier_off, dump_ifinfo, reset_stats;
	} rcu;
	struct {
		struct spinlock mutex;
		struct ethtool_link_ksettings settings;
		struct {
			struct xeth_kstrs names;
			u64 counters[xeth_upper_ethtool_stats];
		} stat;
		struct {
			struct xeth_kstrs names;
			u32 bits;
		} flag;
	} ethtool;
	struct {
		struct spinlock mutex;
		struct rtnl_link_stats64 stats;
	} link;
};

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
	struct hlist_head __rcu *head;
	
	head = xeth_mux_upper_head_hashed(priv->xid);
	xeth_mux_lock();
	hlist_add_head_rcu(&priv->node, head);
	xeth_mux_unlock();
	return 0;
}

static void xeth_upper_cb_carrier_off(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu.carrier_off);
	struct net_device *nd = xeth_netdev(priv);
	netif_carrier_off(nd);
}

static void xeth_upper_cb_dump_ifinfo(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu.dump_ifinfo);
	struct net_device *nd = xeth_netdev(priv);
	struct in_ifaddr *ifa;

	xeth_sbtx_ifinfo(nd, priv->xid, priv->kind, 0, XETH_IFINFO_REASON_DUMP);
	xeth_sbtx_ethtool_flags(priv->xid, priv->ethtool.flag.bits);
	xeth_sbtx_ethtool_settings(priv->xid, &priv->ethtool.settings);
	if (nd->ip_ptr)
		for(ifa = nd->ip_ptr->ifa_list; ifa; ifa = ifa->ifa_next)
			xeth_sbtx_ifa(ifa, priv->xid, NETDEV_UP);
}

static void xeth_upper_cb_reset_stats(struct rcu_head *rcu)
{
	struct xeth_upper_priv *priv =
		container_of(rcu, struct xeth_upper_priv, rcu.reset_stats);
	u64 *link_stat = (u64*)&priv->link.stats;
	int i;

	spin_lock(&priv->link.mutex);
	for (i = 0; i < xeth_upper_link_stats; i++)
		link_stat[i] = 0;
	spin_unlock(&priv->link.mutex);
	spin_lock(&priv->ethtool.mutex);
	for (i = 0; i < xeth_upper_ethtool_stats; i++)
		priv->ethtool.stat.counters[i] = 0;
	spin_unlock(&priv->ethtool.mutex);
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
	/* FIXME conditioned by mux */
	netif_carrier_on(nd);
	xeth_debug_nd(nd, "opened");
	return 0;
}

static int xeth_upper_ndo_stop(struct net_device *nd)
{
	netif_carrier_off(nd);
	xeth_debug_nd(nd, "stopped");
	return 0;
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
	struct xeth_upper_priv *priv = netdev_priv(nd);

	/* FIXME should we show the port of vlan proxies? */
	if (priv->xid > (1 << xeth_mux_bits())) {
		u32 xid = priv->xid & ((1 << xeth_mux_bits()) - 1);
		struct net_device *iflink =
			xeth_debug_rcu(xeth_upper_lookup_rcu(xid));
		return iflink ? iflink->ifindex : 0;
	}
	return xeth_mux_ifindex();
}

static void xeth_upper_ndo_get_stats64(struct net_device *nd,
				       struct rtnl_link_stats64 *dst)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	spin_lock(&priv->link.mutex);
	memcpy(dst, &priv->link.stats, sizeof(*dst));
	spin_unlock(&priv->link.mutex);
}

static const struct net_device_ops xeth_upper_netdev_ops = {
	.ndo_open = xeth_upper_ndo_open,
	.ndo_stop = xeth_upper_ndo_stop,
	.ndo_start_xmit = xeth_upper_ndo_xmit,
	.ndo_get_iflink = xeth_upper_ndo_get_iflink,
	.ndo_get_stats64 = xeth_upper_ndo_get_stats64,
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
	drvinfo->n_priv_flags = xeth_kstrs_count(&priv->ethtool.flag.names);
	drvinfo->n_stats = xeth_kstrs_count(&priv->ethtool.stat.names);
}

static int xeth_upper_eto_get_sset_count(struct net_device *nd, int sset)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return xeth_kstrs_count(&priv->ethtool.stat.names);
	case ETH_SS_PRIV_FLAGS:
		return xeth_kstrs_count(&priv->ethtool.flag.names);
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
		n = xeth_kstrs_count(&priv->ethtool.stat.names);
		for (i = 0; i < n; i++) {
			xeth_kstrs_string(&priv->ethtool.stat.names, p,
					  ETH_GSTRING_LEN, i);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		n = xeth_kstrs_count(&priv->ethtool.flag.names);
		for (i = 0; i < n; i++) {
			xeth_kstrs_string(&priv->ethtool.flag.names, p,
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
	size_t sz = xeth_kstrs_count(&priv->ethtool.stat.names) *
		sizeof(*priv->ethtool.stat.counters);

	spin_lock(&priv->ethtool.mutex);
	memcpy(data, &priv->ethtool.stat.counters, sz);
	spin_unlock(&priv->ethtool.mutex);
}

static u32 xeth_upper_eto_get_priv_flags(struct net_device *nd)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u32 flags;

	spin_lock(&priv->ethtool.mutex);
	flags = priv->ethtool.flag.bits;
	spin_unlock(&priv->ethtool.mutex);

	return flags;
}

static int xeth_upper_eto_set_priv_flags(struct net_device *nd, u32 flags)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	size_t n = xeth_kstrs_count(&priv->ethtool.flag.names);

	if (flags >= (1 << n))
		return -EINVAL;

	spin_lock(&priv->ethtool.mutex);
	priv->ethtool.flag.bits = flags;
	spin_unlock(&priv->ethtool.mutex);

	xeth_sbtx_ethtool_flags(priv->xid, flags);

	return 0;
}

static int xeth_upper_eto_get_link_ksettings(struct net_device *nd,
					     struct ethtool_link_ksettings *p)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	spin_lock(&priv->ethtool.mutex);
	memcpy(p, &priv->ethtool.settings, sizeof(*p));
	spin_unlock(&priv->ethtool.mutex);

	return 0;
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

	spin_lock(&priv->ethtool.mutex);
	
	settings = &priv->ethtool.settings;
	if (req->base.autoneg == AUTONEG_DISABLE) {
		err = xeth_upper_validate_speed(nd, req->base.speed);
		if (!err)
			err = xeth_upper_validate_duplex(nd, req->base.duplex);
		if (!err) {
			settings->base.autoneg = req->base.autoneg;
			settings->base.speed = req->base.speed;
			settings->base.duplex = req->base.duplex;
		}
	} else {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(res);
		if (bitmap_andnot(res,
				  req->link_modes.advertising,
				  settings->link_modes.supported,
				  __ETHTOOL_LINK_MODE_MASK_NBITS)) {
			err = -EINVAL;
		} else {
			err = xeth_upper_validate_duplex(nd, req->base.duplex);
			if (!err) {
				bitmap_copy(settings->link_modes.advertising,
					    req->link_modes.advertising,
					    __ETHTOOL_LINK_MODE_MASK_NBITS);
				settings->base.autoneg = AUTONEG_ENABLE;
				settings->base.speed = 0;
				settings->base.duplex = req->base.duplex;
			}
		}
	}

	if (!err)
		err = xeth_sbtx_ethtool_settings(priv->xid, settings);

	spin_unlock(&priv->ethtool.mutex);
	return err;
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
};

static void xeth_upper_lnko_setup(struct net_device *nd)
{
	ether_setup(nd);
	nd->netdev_ops = &xeth_upper_netdev_ops;
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
}

static int xeth_upper_lnko_validate(struct nlattr *tb[],
				    struct nlattr *data[],
				    struct netlink_ext_ack *extack)
{
	u8 muxbits = xeth_mux_bits();

	if (tb[IFLA_ADDRESS]) {
		char *ifla_address = nla_data(tb[IFLA_ADDRESS]);
		if (xeth_debug_err(nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN))
			return -EINVAL;
		if (xeth_debug_err(!is_valid_ether_addr(ifla_address)))
			return -EADDRNOTAVAIL;
	}
	if (!data)
		return 0;
	if (data[XETH_IFLA_XID]) {
		u16 xid = nla_get_u16(data[XETH_IFLA_XID]);
		if (xid == 0 || xid >= (1 << muxbits))
			return -ERANGE;
		if (xeth_debug_rcu(xeth_upper_lookup_rcu(xid)) != NULL)
			return -EBUSY;
	}
	if (data[XETH_IFLA_VID]) {
		u16 vid = nla_get_u16(data[XETH_IFLA_VID]);
		if (vid == 0 || vid >= VLAN_N_VID)
			return -ERANGE;
	}
	if (data[XETH_IFLA_KIND]) {
		switch (nla_get_u8(data[XETH_IFLA_KIND])) {
		case XETH_DEV_KIND_PORT:
		case XETH_DEV_KIND_VLAN:
		case XETH_DEV_KIND_BRIDGE:
		case XETH_DEV_KIND_LAG:
			break;
		default:
			return -ERANGE;
		}
	}
	return 0;
}

/**
 * xeth_upper_lnko_new() - create xeth mux upper proxy of a remote netdev
 *
 * This is called from @rtnl_newlink() in service of the following iproute2
 * commands.  A platform driver may use this through @xeth_add_upper().
 *
 * Here is how to create an upper device to the xeth mux with iproute2,
 *	ip link add [[name ]IFNAME] type xeth [xid XID]
 *
 * Without XID, this searches for an unused XID starting at the @xeth_base_xid
 * module parameter (default 3000).
 *
 * Here is how to create a vlan of an existing upper device,
 *	ip link add [[name ]IFNAME] link UPPER type xeth [vid VID]
 * or,
 *	ip link add [name ]IFNAME.VID link UPPER type xeth
 *
 * Without VID, this searches for an unsued VID beginning with 1.
 * Without IFNAME, this assigns UPPER[.VID].
 */
static int xeth_upper_lnko_new(struct net *src_net, struct net_device *nd,
			       struct nlattr *tb[], struct nlattr *data[],
			       struct netlink_ext_ack *extack)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u8 muxbits = xeth_mux_bits();
	u32 xid, range = (1 << muxbits) - 1;
	struct net_device *xnd;
	int err;
	s64 x;

	spin_lock_init(&priv->mutex);
	spin_lock_init(&priv->ethtool.mutex);
	spin_lock_init(&priv->link.mutex);

	priv->xid = 0;
	if (tb && !tb[IFLA_ADDRESS])
		eth_hw_addr_random(nd);
	if (tb && tb[IFLA_LINK]) {
		u32 li = nla_get_u32(tb[IFLA_LINK]);
		struct net_device *link;
		struct xeth_upper_priv *linkpriv;

		link = xeth_debug_rcu(xeth_upper_link_rcu(li));
		if (IS_ERR_OR_NULL(link))
			return PTR_ERR(link);
		linkpriv = netdev_priv(link);
		if (data && data[XETH_IFLA_VID]) {
			xid  = linkpriv->xid |
				(nla_get_u16(data[XETH_IFLA_VID]) << muxbits);
			xnd = xeth_debug_rcu(xeth_upper_lookup_rcu(xid));
			if (xnd)
				return -EBUSY;
			priv->xid = xid;
		} else {
			unsigned long long ull;
			int i = 0;
			while(true)
				if (i >= IFNAMSIZ) {
					x = linkpriv->xid | (1 << muxbits);
					x = xeth_upper_search(x, range);
					if (x < 0)
						return (int)x;
					priv->xid = x;
					break;
				} else if (nd->name[i] == '.') {
					err = kstrtoull(nd->name+i+1, 0, &ull);
					if (err)
						return err;
					if (!ull || ull > range)
						return -ERANGE;
					priv->xid  = linkpriv->xid |
						(ull << muxbits);
					break;
				} else
					i++;
		}
		if (!tb || !tb[IFLA_IFNAME])
			scnprintf(nd->name, IFNAMSIZ, "%s.%u",
				  link->name, priv->xid >> muxbits);
		priv->kind = XETH_DEV_KIND_VLAN;
	} else {
		if (data) {
			if (data[XETH_IFLA_XID]) {
				xid = nla_get_u16(data[XETH_IFLA_XID]);
				xnd = xeth_debug_rcu(xeth_upper_lookup_rcu(xid));
				if (xnd)
					return -EBUSY;
				priv->xid = xid;
			} else {
				x = xeth_upper_search(xeth_base_xid, range);
				if (x < 0)
					return (int)x;
				priv->xid = x;
			}
			priv->kind = data[XETH_IFLA_KIND] ?
				nla_get_u8(data[XETH_IFLA_XID]) :
				XETH_DEV_KIND_UNSPEC;
		}
		if (!tb || !tb[IFLA_IFNAME])
			scnprintf(nd->name, IFNAMSIZ, "%s-%u",
				  xeth_name, priv->xid);
	}
	xeth_debug_rcu(xeth_upper_add_rcu(nd));
	err = xeth_debug_nd_err(nd, register_netdevice(nd));
	if (err) {
		xeth_mux_lock();
		hlist_del_rcu(&priv->node);
		xeth_mux_unlock();
		return err;
	}
	xeth_debug_err(xeth_kstrs_init(&priv->ethtool.flag.names,
				       &nd->dev.kobj,
				       "ethtool-flag-names",
				       xeth_upper_ethtool_flags));
	xeth_debug_err(xeth_kstrs_init(&priv->ethtool.stat.names,
				       &nd->dev.kobj,
				       "ethtool-stat-names",
				       xeth_upper_ethtool_stats));
	return 0;
}

static void xeth_upper_lnko_del(struct net_device *nd, struct list_head *q)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	xeth_mux_lock();
	hlist_del_rcu(&priv->node);
	xeth_mux_unlock();

	xeth_kobject_put(&priv->ethtool.flag.names.kobj);
	xeth_kobject_put(&priv->ethtool.stat.names.kobj);

	unregister_netdevice_queue(nd, q);
}

static struct net *xeth_upper_lnko_get_net(const struct net_device *nd)
{
	/* FIXME this should return dev_net of iflink
	 * (e.g. xeth_mux or vlans real dev */
	return dev_net(nd);
}

static const struct nla_policy xeth_upper_nla_policy[] = {
	[XETH_IFLA_XID] = { .type = NLA_U16 },
	[XETH_IFLA_VID] = { .type = NLA_U16 },
	[XETH_IFLA_KIND] = { .type = NLA_U8 },
};

static struct rtnl_link_ops xeth_upper_link_ops = {
	.kind		= xeth_name,
	.priv_size	= sizeof(struct xeth_upper_priv),
	.setup		= xeth_upper_lnko_setup,
	.validate	= xeth_upper_lnko_validate,
	.newlink	= xeth_upper_lnko_new,
	.dellink	= xeth_upper_lnko_del,
	.get_link_net	= xeth_upper_lnko_get_net,
	.policy		= xeth_upper_nla_policy,
	.maxtype	= ARRAY_SIZE(xeth_upper_nla_policy)-1,
};

bool xeth_upper_check(struct net_device *nd)
{
	return	nd->netdev_ops == &xeth_upper_netdev_ops &&
		nd->ethtool_ops == &xeth_upper_ethtool_ops;
}

static bool xeth_upper_lnko_is_registered(void)
{
	return xeth_upper_link_ops.list.next || xeth_upper_link_ops.list.prev;
}

static int xeth_upper_lnko_registered_ok(void)
{
	return xeth_upper_lnko_is_registered() ? 0 : -EINVAL;
}

__init int xeth_upper_init(void)
{
	int err;
	
	err = xeth_debug_err(rtnl_link_register(&xeth_upper_link_ops));
	if (!err)
		err = xeth_debug_err(xeth_upper_lnko_registered_ok());
	return err;
}

int xeth_upper_deinit(int err)
{
	if (xeth_upper_lnko_is_registered()) {
		rtnl_link_unregister(&xeth_upper_link_ops);
	}
	return err;
}

static void xeth_upper_call_rcu_for_each_priv(enum xeth_upper_rcu t,
					      rcu_callback_t cb)
{
	int bkt;
	struct rcu_head *rh = NULL;
	struct xeth_upper_priv *priv = NULL;
	struct hlist_head __rcu *head = xeth_mux_upper_head_indexed(0);

	rcu_read_lock();
	for (bkt = 0; head; head = xeth_mux_upper_head_indexed(++bkt))
		hlist_for_each_entry_rcu(priv, head, node) {
			switch (t) {
			case xeth_upper_rcu_carrier_off:
				rh = &priv->rcu.carrier_off;
				break;
			case xeth_upper_rcu_dump_ifinfo:
				rh = &priv->rcu.dump_ifinfo;
				break;
			case xeth_upper_rcu_reset_stats:
				rh = &priv->rcu.reset_stats;
				break;
			default:
				rh = NULL;
			}
			if (rh)
				call_rcu(rh, cb);
		}
	rcu_read_unlock();
	rcu_barrier();
}

void xeth_upper_all_carrier_off(void)
{
	xeth_upper_call_rcu_for_each_priv(xeth_upper_rcu_carrier_off,
					  xeth_upper_cb_carrier_off);
}

void xeth_upper_all_dump_ifinfo(void)
{
	xeth_upper_call_rcu_for_each_priv(xeth_upper_rcu_dump_ifinfo,
					  xeth_upper_cb_dump_ifinfo);
}

void xeth_upper_all_reset_stats(void)
{
	xeth_upper_call_rcu_for_each_priv(xeth_upper_rcu_reset_stats,
					  xeth_upper_cb_reset_stats);
}

void xeth_upper_changemtu(int mtu, int max_mtu)
{
#ifdef __FIXME__
#endif /* __FIXME__ */
}

void xeth_upper_ethtool_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	if (index >= xeth_upper_ethtool_stats) {
		xeth_counter_inc(sbrx_invalid);
		return;
	}
	spin_lock(&priv->ethtool.mutex);
	priv->ethtool.stat.counters[index] = count;
	spin_unlock(&priv->ethtool.mutex);
}

void xeth_upper_link_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);
	u64 *statp;

	if (index >= xeth_upper_link_stats) {
		xeth_counter_inc(sbrx_invalid);
		return;
	}
	
	statp = (u64*)&priv->link.stats + index;

	spin_lock(&priv->link.mutex);
	*statp = count;
	spin_unlock(&priv->link.mutex);
}

void xeth_upper_speed(struct net_device *nd, u32 mbps)
{
	struct xeth_upper_priv *priv = netdev_priv(nd);

	spin_lock(&priv->ethtool.mutex);
	priv->ethtool.settings.base.speed = mbps;
	spin_unlock(&priv->ethtool.mutex);
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
		     const char *const ethtool_flag_names[],
		     void (*ethtool_cb) (struct ethtool_link_ksettings *))
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
						 xeth_upper_lnko_setup,
						 xeth_upper_txqs,
						 xeth_upper_rxqs));
	if (IS_ERR(nd))
		return PTR_ERR(nd);

	priv = netdev_priv(nd);

	spin_lock_init(&priv->mutex);
	spin_lock_init(&priv->ethtool.mutex);
	spin_lock_init(&priv->link.mutex);

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
		xeth_mux_lock();
		hlist_del_rcu(&priv->node);
		xeth_mux_unlock();
		return x;
	}

	xeth_debug_err(xeth_kstrs_init(&priv->ethtool.flag.names,
				       &nd->dev.kobj,
				       "ethtool-flag-names",
				       xeth_upper_ethtool_flags));
	xeth_debug_err(xeth_kstrs_copy(&priv->ethtool.flag.names,
				       ethtool_flag_names));

	xeth_debug_err(xeth_kstrs_init(&priv->ethtool.stat.names,
				       &nd->dev.kobj,
				       "ethtool-stat-names",
				       xeth_upper_ethtool_stats));
	if (ethtool_cb)
		ethtool_cb(&priv->ethtool.settings);

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
	xeth_mux_lock();
	hlist_del_rcu(&priv->node);
	xeth_mux_unlock();
	unregister_netdev(nd);
}
EXPORT_SYMBOL(xeth_delete_port);
