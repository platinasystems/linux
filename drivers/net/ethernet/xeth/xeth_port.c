/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include "xeth_mux.h"
#include "xeth_proxy.h"
#include "xeth_port.h"
#include "xeth_qsfp.h"
#include "xeth_sbtx.h"
#include "xeth_version.h"
#include "xeth_debug.h"
#include <linux/xeth.h>
#include <linux/netdevice.h>

static const char xeth_port_drvname[] = "xeth-port";

struct xeth_port_priv {
	struct xeth_proxy proxy;
	struct {
		struct {
			const int *next;
			atomic64_t counters[XETH_MAX_ET_STATS];
			const char *names;
		} stat;
		struct {
			const char * const *names;
			u32 flags;
		} priv_flag;
		struct ethtool_link_ksettings ksettings;
	} ethtool;
	struct i2c_client *qsfp;
	int port, subport;
};

int xeth_port_of(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->port;
}

int xeth_port_subport(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->subport;
}

static int xeth_port_open(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	if (!(priv->proxy.mux->flags & IFF_UP))
		dev_open(priv->proxy.mux, NULL);
	return xeth_proxy_open(nd);
}

u32 xeth_port_ethtool_priv_flags(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->ethtool.priv_flag.flags;
}

const struct ethtool_link_ksettings *
xeth_port_ethtool_ksettings(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return &priv->ethtool.ksettings;
}

void xeth_port_reset_ethtool_stats(struct net_device *nd)
{
	int i;
	struct xeth_port_priv *priv = netdev_priv(nd);
	for (i = 0; i < XETH_MAX_ET_STATS; i++)
		atomic64_set(&priv->ethtool.stat.counters[i], 0LL);
}

void xeth_port_ethtool_stat(struct net_device *nd, u32 index, u64 count)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	if (index < XETH_MAX_ET_STATS)
		atomic64_set(&priv->ethtool.stat.counters[index], count);
	else
		xeth_mux_inc_sbrx_invalid(priv->proxy.mux);
}

void xeth_port_speed(struct net_device *nd, u32 mbps)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	priv->ethtool.ksettings.base.speed = mbps;
}

const struct net_device_ops xeth_port_ndo = {
	.ndo_init = xeth_proxy_init,
	.ndo_uninit = xeth_proxy_uninit,
	.ndo_open = xeth_port_open,
	.ndo_stop = xeth_proxy_stop,
	.ndo_start_xmit = xeth_proxy_start_xmit,
	.ndo_get_iflink = xeth_proxy_get_iflink,
	.ndo_get_stats64 = xeth_proxy_get_stats64,
	.ndo_change_mtu = xeth_proxy_change_mtu,
	.ndo_fix_features = xeth_proxy_fix_features,
	.ndo_set_features = xeth_proxy_set_features,
};

static u32 xeth_port_n_priv_flags(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	u32 n;
	for (n = 0; priv->ethtool.priv_flag.names[n]; n++);
	return n;
}

static void xeth_port_get_drvinfo(struct net_device *nd,
				  struct ethtool_drvinfo *drvinfo)
{
	struct xeth_port_priv *priv = netdev_priv(nd);

	strlcpy(drvinfo->driver, xeth_port_drvname, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, XETH_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "n/a", ETHTOOL_FWVERS_LEN);
	strlcpy(drvinfo->erom_version, "n/a", ETHTOOL_EROMVERS_LEN);
	drvinfo->n_priv_flags = xeth_port_n_priv_flags(nd);
	drvinfo->n_stats = *priv->ethtool.stat.next;
	if (priv->port < 0)
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u",
			  priv->proxy.xid);
	else if (priv->subport < 0)
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u:%u",
			  priv->port, priv->proxy.xid);
	else
		scnprintf(drvinfo->bus_info, ETHTOOL_BUSINFO_LEN, "%u-%u:%u",
			  priv->port, priv->subport, priv->proxy.xid);
}

static int xeth_port_get_sset_count(struct net_device *nd, int sset)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	switch (sset) {
	case ETH_SS_PRIV_FLAGS:
		return xeth_port_n_priv_flags(nd);
	case ETH_SS_STATS:
		return *priv->ethtool.stat.next;
	case ETH_SS_TEST:
		return 0;
	}
	return -EOPNOTSUPP;
}

static void xeth_port_get_strings(struct net_device *nd, u32 sset, u8 *data)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	char *p = (char *)data;
	int i;

	switch (sset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		for (i = 0; i < *priv->ethtool.stat.next; i++) {
			strlcpy(p, priv->ethtool.stat.names +
				(i * ETH_GSTRING_LEN), ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_PRIV_FLAGS:
		if (!priv->ethtool.priv_flag.names)
			break;
		for (i = 0; priv->ethtool.priv_flag.names[i]; i++) {
			strlcpy(p, priv->ethtool.priv_flag.names[i],
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void xeth_port_get_stats(struct net_device *nd,
				struct ethtool_stats *stats, u64 *data)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	int i;

	for (i = 0; i < *priv->ethtool.stat.next; i++)
		data[i] = atomic64_read(&priv->ethtool.stat.counters[i]);
}

static u32 xeth_port_get_priv_flags(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->ethtool.priv_flag.flags;
}

static int xeth_port_set_priv_flags(struct net_device *nd, u32 flags)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	if (flags >= (1 << xeth_port_n_priv_flags(nd)))
		return -EINVAL;

	priv->ethtool.priv_flag.flags = flags;
	xeth_sbtx_et_flags(priv->proxy.mux, priv->proxy.xid, flags);
	return 0;
}

static int xeth_port_get_link_ksettings(struct net_device *nd,
					struct ethtool_link_ksettings *p)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	memcpy(p, &priv->ethtool.ksettings, sizeof(*p));
	return 0;
}

static int xeth_port_validate_port(struct net_device *nd, u8 port)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.ksettings;
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

static int xeth_port_validate_duplex(struct net_device *nd, u8 duplex)
{
	return xeth_debug_nd_err(nd,
				 duplex != DUPLEX_HALF &&
				 duplex != DUPLEX_FULL &&
				 duplex != DUPLEX_UNKNOWN) ?  -EINVAL : 0;
}

static int xeth_port_validate_speed(struct net_device *nd, u32 speed)
{
        return xeth_debug_nd_err(nd,
				 speed != 100000 &&
				 speed != 50000 &&
				 speed != 40000 &&
				 speed != 25000 &&
				 speed != 20000 &&
				 speed != 10000 &&
				 speed != 1000) ?  -EINVAL : 0;
}

static int
xeth_port_set_link_ksettings(struct net_device *nd,
			     const struct ethtool_link_ksettings *req)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.ksettings;
	int err;

	if (req->base.port != ks->base.port) {
		err = xeth_port_validate_port(nd, req->base.port);
		if (err)
			return err;
		ks->base.port = req->base.port;
	}
	if (req->base.autoneg == AUTONEG_DISABLE) {
		err = xeth_port_validate_speed(nd, req->base.speed);
		if (err)
			return err;
		err = xeth_port_validate_duplex(nd, req->base.duplex);
		if (err)
			return err;
		ks->base.autoneg = req->base.autoneg;
		ks->base.speed = req->base.speed;
		ks->base.duplex = req->base.duplex;
	} else {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(res);
		if (bitmap_andnot(res, req->link_modes.advertising,
				  ks->link_modes.supported,
				  __ETHTOOL_LINK_MODE_MASK_NBITS)) {
			return -EINVAL;
		} else {
			err = xeth_port_validate_duplex(nd, req->base.duplex);
			if (err)
				return err;
			bitmap_copy(ks->link_modes.advertising,
				    req->link_modes.advertising,
				    __ETHTOOL_LINK_MODE_MASK_NBITS);
			ks->base.autoneg = AUTONEG_ENABLE;
			ks->base.speed = 0;
			ks->base.duplex = req->base.duplex;
		}
	}

	return xeth_sbtx_et_settings(priv->proxy.mux, priv->proxy.xid, ks);
}

#define xeth_port_ks_supports(ks, mk)					\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, supported, mk));	\
})

#define xeth_port_ks_advertising(ks, mk)				\
({									\
	(ethtool_link_ksettings_test_link_mode(ks, advertising, mk));	\
})

static int xeth_port_get_fecparam(struct net_device *nd,
				  struct ethtool_fecparam *param)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.ksettings;
	const u32 fec_both = ETHTOOL_FEC_RS | ETHTOOL_FEC_BASER;

	param->fec = 0;
	param->active_fec = 0;
	if (xeth_port_ks_supports(ks, FEC_NONE))
		param->fec |= ETHTOOL_FEC_OFF;
	if (xeth_port_ks_supports(ks, FEC_RS))
		param->fec |= ETHTOOL_FEC_RS;
	if (xeth_port_ks_supports(ks, FEC_BASER))
		param->fec |= ETHTOOL_FEC_BASER;
	if ((param->fec & fec_both) == fec_both)
		param->fec |= ETHTOOL_FEC_AUTO;
	if (!param->fec)
		param->fec = ETHTOOL_FEC_NONE;
	if (param->fec == ETHTOOL_FEC_NONE)
		param->active_fec = ETHTOOL_FEC_NONE;
	else if (xeth_port_ks_advertising(ks, FEC_NONE))
		param->active_fec = ETHTOOL_FEC_OFF;
	else if (xeth_port_ks_advertising(ks, FEC_RS))
		param->active_fec = xeth_port_ks_advertising(ks, FEC_BASER) ?
			ETHTOOL_FEC_AUTO : ETHTOOL_FEC_RS;
	else if (xeth_port_ks_advertising(ks, FEC_BASER))
		param->fec = ETHTOOL_FEC_BASER;
	return 0;
}

static int xeth_port_set_fecparam(struct net_device *nd,
				  struct ethtool_fecparam *param)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	struct ethtool_link_ksettings *ks = &priv->ethtool.ksettings;
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
	return xeth_sbtx_et_settings(priv->proxy.mux, priv->proxy.xid, ks);
}

int xeth_port_get_module_info(struct net_device *nd,
			      struct ethtool_modinfo *emi)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->qsfp ? xeth_qsfp_get_module_info(priv->qsfp, emi) : -ENXIO;
}

int xeth_port_get_module_eeprom(struct net_device *nd,
				struct ethtool_eeprom *ee, u8 *data)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	return priv->qsfp ? xeth_qsfp_get_module_eeprom(priv->qsfp, ee, data) :
		-ENXIO;
}

static const struct ethtool_ops xeth_port_eto = {
	.get_drvinfo = xeth_port_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_sset_count = xeth_port_get_sset_count,
	.get_strings = xeth_port_get_strings,
	.get_ethtool_stats = xeth_port_get_stats,
	.get_priv_flags = xeth_port_get_priv_flags,
	.set_priv_flags = xeth_port_set_priv_flags,
	.get_link_ksettings = xeth_port_get_link_ksettings,
	.set_link_ksettings = xeth_port_set_link_ksettings,
	.get_fecparam = xeth_port_get_fecparam,
	.set_fecparam = xeth_port_set_fecparam,
	.get_module_info = xeth_port_get_module_info,
	.get_module_eeprom = xeth_port_get_module_eeprom,
};

static const struct ethtool_ops xeth_subport_eto = {
	.get_drvinfo = xeth_port_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = xeth_port_get_link_ksettings,
	.set_link_ksettings = xeth_port_set_link_ksettings,
};

static void xeth_port_setup(struct net_device *nd)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	int i;

	xeth_proxy_setup(nd);
	for (i = 0; i < XETH_MAX_ET_STATS; i++)
		atomic64_set(&priv->ethtool.stat.counters[i], 0LL);
	netif_carrier_off(nd);
	priv->proxy.kind = XETH_DEV_KIND_PORT;
	nd->netdev_ops = &xeth_port_ndo;
	nd->needs_free_netdev = true;
	nd->priv_destructor = NULL;
	ether_setup(nd);
	nd->priv_flags &= ~IFF_TX_SKB_SHARING;
	nd->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	nd->priv_flags |= IFF_NO_QUEUE;
	nd->priv_flags |= IFF_PHONY_HEADROOM;
	nd->priv_flags |= IFF_DONT_BRIDGE;
}

struct net_device *xeth_port_probe(struct platform_device *xeth,
				   int port, int subport)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	struct net_device *nd;
	struct xeth_port_priv *priv;
	char ifname[IFNAMSIZ];
	int err;

	xeth_vendor_ifname(xeth, ifname, port, subport);
	nd = xeth_debug_ptr_err(alloc_netdev_mqs(sizeof(*priv), ifname,
						 NET_NAME_ENUM,
						 xeth_port_setup,
						 xeth_vendor_n_txqs(xeth),
						 xeth_vendor_n_rxqs(xeth)));
	if (IS_ERR(nd))
		return nd;

	nd->ethtool_ops = subport <= 0 ? &xeth_port_eto : &xeth_subport_eto;

	xeth_vendor_hw_addr(xeth, nd, port, subport);

	priv = netdev_priv(nd);
	priv->proxy.nd = nd;
	priv->proxy.mux = dev_get_drvdata(&xeth->dev);
	priv->proxy.xid = xeth_vendor_xid(xeth, port, subport); 
	nd->min_mtu = priv->proxy.mux->min_mtu;
	nd->max_mtu = priv->proxy.mux->max_mtu;

	priv->ethtool.priv_flag.names = vendor->port.ethtool.flag_names;
	priv->ethtool.stat.next = &vendor->port.ethtool.stat.next;
	priv->ethtool.stat.names = &vendor->port.ethtool.stat.names[0][0];

	priv->port = port;
	priv->subport = subport;

	if (subport <= 0)
		priv->qsfp = xeth_vendor_qsfp(xeth, port);

	if (subport < 0)
		xeth_vendor_port_ksettings(xeth, &priv->ethtool.ksettings);
	else
		xeth_vendor_subport_ksettings(xeth, &priv->ethtool.ksettings);

	xeth_mux_add_proxy(&priv->proxy);

	rtnl_lock();
	err = xeth_debug_nd_err(nd, register_netdevice(nd));
	rtnl_unlock();

	if (err) {
		xeth_mux_del_proxy(&priv->proxy);
		free_netdev(nd);
		return ERR_PTR(err);
	}
	return nd;
}

static int xeth_port_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	if (!tb || !tb[IFLA_LINK]) {
		NL_SET_ERR_MSG(extack, "missing mux link");
		return -EINVAL;
	}
	if (data) {
		if (data[XETH_PORT_IFLA_XID]) {
			u16 xid = nla_get_u16(data[XETH_PORT_IFLA_XID]);
			if (xid == 0) {
				NL_SET_ERR_MSG(extack, "out-of-range XID");
				return -ERANGE;
			}
		}
	}
	return 0;
}

static int xeth_port_newlink(struct net *src_net, struct net_device *nd,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	u32 link = nla_get_u32(tb[IFLA_LINK]);
	int err;

	priv->proxy.nd = nd;
	priv->proxy.mux = dev_get_by_index_rcu(dev_net(nd), link);
	nd->min_mtu = priv->proxy.mux->min_mtu;
	nd->max_mtu = priv->proxy.mux->max_mtu;
	priv->port = -1;
	priv->subport = -1;

	if (data && data[XETH_PORT_IFLA_XID])
		priv->proxy.xid = nla_get_u16(data[XETH_PORT_IFLA_XID]);
	else
		for (priv->proxy.xid = 3999;
		     xeth_mux_proxy_of_xid(priv->proxy.mux, priv->proxy.xid);
		     priv->proxy.xid--);

	if (!tb || !tb[IFLA_IFNAME])
		scnprintf(nd->name, IFNAMSIZ, "%s%u",
			  xeth_port_drvname, priv->proxy.xid);

	xeth_mux_add_proxy(&priv->proxy);
	if (err = register_netdevice(nd), err) {
		xeth_mux_del_proxy(&priv->proxy);
		return err;
	}
	return xeth_sbtx_ifinfo(&priv->proxy, 0, XETH_IFINFO_REASON_NEW);
}

static void xeth_port_dellink(struct net_device *nd, struct list_head *unregq)
{
	struct xeth_port_priv *priv = netdev_priv(nd);
	xeth_mux_del_vlans(priv->proxy.mux, nd, unregq);
	unregister_netdevice_queue(nd, unregq);
}

static struct net *xeth_port_get_link_net(const struct net_device *nd)
{
	return dev_net(nd);
}

static const struct nla_policy xeth_port_nla_policy[] = {
	[XETH_PORT_IFLA_XID] = { .type = NLA_U16 },
};

struct rtnl_link_ops xeth_port_lnko = {
	.kind		= xeth_port_drvname,
	.priv_size	= sizeof(struct xeth_port_priv),
	.setup		= xeth_port_setup,
	.validate	= xeth_port_validate,
	.newlink	= xeth_port_newlink,
	.dellink	= xeth_port_dellink,
	.get_link_net	= xeth_port_get_link_net,
	.policy		= xeth_port_nla_policy,
	.maxtype	= XETH_PORT_N_IFLA - 1,
};
