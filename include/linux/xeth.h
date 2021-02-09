/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __LINUX_XETH_H
#define __LINUX_XETH_H

#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/i2c.h>

enum xeth_encap {
	XETH_ENCAP_VLAN = 0,
	XETH_ENCAP_VPLS,
};

enum {
	XETH_MAX_ET_STATS = 512,
};

/**
 * %XETH_VENDOR_ID: driver interface identifier
 *
 * Regenerate this after any change to &struct xeth_vendor using:
 * ``dd if=/dev/urandom bs=8 count=1 status=none |
 *	hexdump -v -e '"0x" 8/1 "%02x"'``
 */
#define XETH_VENDOR_ID 0x805b45639e70b8faULL

/**
 * struct xeth_vendor - Kernel Program Interface of the XETH parent device
 */
struct xeth_vendor {
	/**
	 * @id: XETH_VENDOR_ID
	 */
	u64 id;
	enum xeth_encap encap;
	/**
	 * @n_ports:
	 *	An xeth platform must have 1 or more proxy ports
	 */
	u8 n_ports;
	/**
	 * @n_rxqs, @n_txqs:
	 * 	Number of allocated RX and TX subqueues (default 1)
	 */
	u8 n_rxqs, n_txqs;
	/**
	 * @links:
	 *	A NULL terminate list of NULL terminated aliases, e.g.
	 *	* { "eth1", "enp3s0f0", NULL },
	 *	* { "eth2", "enp3s0f1", NULL },
	 *	* NULL
	 */
	const char * const * const *links;
	struct xeth_vendor_port {
		struct xeth_vendor_port_provision {
			/**
			 * @subports:
			 * 	A list of subports per port
			 */
			int *subports;
			struct device_attribute attr;
		} provision;
		struct xeth_vendor_port_ethtool {
			/**
			 * @flag_names:
			 *	A NULL terminated list of ethtool priv flags
			 */
			const char * const *flag_names;
			struct xeth_vendor_port_ethtool_stat {
				/**
				 * @next:
				 *	The index of next unassigned stat name
				 */
				int next;
				/**
				 * @names:
				 *	An array of ETH_GSTRING_LEN strings
				 *	with the first zero length element
				 *	(e.g. port_et_stat_names[i][0] == '\0')
				 *	marking the end
				 */
				char names[XETH_MAX_ET_STATS][ETH_GSTRING_LEN];
				struct device_attribute attr;
			} stat;
		} ethtool;
	} port;
	/**
	 * @ifname:
	 *	Name of indexed (port, subport) or mux if port is -1.
	 */
	void (*ifname)(const struct xeth_vendor *vendor,
		       char *ifname, int port, int subport);
	/**
	 * @hw_addr:
	 *	Assign HW addr (aka MAC) of given netdev at (port, subport)
	 *	or mux if port is -1.
	 */
	void (*hw_addr)(const struct xeth_vendor *vendor,
			struct net_device *nd, int port, int subport);
	/**
	 * @xid:
	 *	Return mux id of (port, subport) or alloc from free list if
	 *	port is -1.
	 */
	u32 (*xid)(const struct xeth_vendor *vendor, int port, int subport);
	/**
	 * @qsfp: returns i2c_client of port's QSFP or NULL if none.
	 */
	struct i2c_client *(*qsfp)(const struct xeth_vendor *vendor, int port);
	/**
	 * @port_ksettings:
	 *	Assigns port's @ethtool_link_ksettings
	 */
	void (*port_ksettings)(struct ethtool_link_ksettings *);
	/**
	 * @subport_ksettings:
	 *	Assigns subport's @ethtool_link_ksettings
	 */
	void (*subport_ksettings)(struct ethtool_link_ksettings *);
	struct {
		struct	platform_device *pd;
		struct	platform_device_info info;
	} xeth;
};

/**
 * xeth_vendor() - kpi of xeth vendors
 * @xeth: xeth mux platform device
 *
 * Returns kpi of parent or NULL if missing or mismatched id
 */
static inline const struct xeth_vendor *
xeth_vendor(const struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = dev_get_drvdata(xeth->dev.parent);
	return (vendor && vendor->id == XETH_VENDOR_ID) ? vendor : NULL;
	return NULL;
};

static inline void
xeth_vendor_ifname(const struct platform_device *xeth,
		   char *ifname, int port, int subport)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	if (vendor && vendor->ifname)
		vendor->ifname(vendor, ifname, port, subport);
	else if (port < 0)
		strcpy(ifname, "xeth-mux");
	else if (subport < 0)
		scnprintf(ifname, IFNAMSIZ, "xeth%d", port);
	else
		scnprintf(ifname, IFNAMSIZ, "xeth%d.%d", port, subport);
}

static inline void
xeth_vendor_hw_addr(const struct platform_device *xeth,
		    struct net_device *nd, int port, int subport)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	if (vendor && vendor->hw_addr)
		vendor->hw_addr(vendor, nd, port, subport);
	else
		eth_hw_addr_random(nd);
}

static inline u32
xeth_vendor_xid(const struct platform_device *xeth, int port, int subport)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	if (vendor && vendor->xid)
		return vendor->xid(vendor, port, subport);
	else if (port < 0)
		return 3000;
	else if (subport < 0)
		return 3999 - port;
	else
		return 3999 - port - (vendor->n_ports * subport);
}

static inline struct i2c_client *
xeth_vendor_qsfp(const struct platform_device *xeth, int port)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	return (vendor && vendor->qsfp) ? vendor->qsfp(vendor, port) : NULL;
}

static inline enum xeth_encap
xeth_vendor_encap(const struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	return vendor ? vendor->encap : XETH_ENCAP_VLAN;
}

static inline u8
xeth_vendor_n_ports(const struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	return vendor ? vendor->n_ports : 1;
}

static inline u8
xeth_vendor_n_rxqs(const struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	return vendor && vendor->n_rxqs ? vendor->n_rxqs : 1;
}

static inline u8
xeth_vendor_n_txqs(const struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	return vendor && vendor->n_txqs ? vendor->n_txqs : 1;
}

static inline const char * const * const *
xeth_vendor_links(const struct platform_device *xeth)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	return vendor ? vendor->links : NULL;
}

static inline void
xeth_vendor_port_ksettings(const struct platform_device *xeth,
			   struct ethtool_link_ksettings *ks)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	if (vendor && vendor->port_ksettings)
		vendor->port_ksettings(ks);
}

static inline void
xeth_vendor_subport_ksettings(const struct platform_device *xeth,
			      struct ethtool_link_ksettings *ks)
{
	const struct xeth_vendor *vendor = xeth_vendor(xeth);
	if (vendor && vendor->subport_ksettings)
		vendor->subport_ksettings(ks);
}

static inline ssize_t
xeth_show_port_et_stat_name(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct xeth_vendor_port_ethtool_stat *stat =
		container_of(attr, struct xeth_vendor_port_ethtool_stat,
			     attr);
	if (stat->next > 0) {
		char *name = stat->names[stat->next - 1];
		return strlcpy(buf, name, ETH_GSTRING_LEN);
	}
	return 0;
}

static inline ssize_t
xeth_store_port_et_stat_name(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t sz)
{
	struct xeth_vendor_port_ethtool_stat *stat =
		container_of(attr, struct xeth_vendor_port_ethtool_stat,
			     attr);
	char *name;
	int i;

	if (!sz || buf[0] == '\n') {
		stat->next = 0;
		return sz;
	}
	if (stat->next >= XETH_MAX_ET_STATS)
		return -EINVAL;
	name = stat->names[stat->next];
	for (i = 0; i < ETH_GSTRING_LEN; i++)
		if (buf[i] == '\n' || i == sz) {
			name[i] = '\0';
			break;
		} else {
			name[i] = buf[i];
		}
	stat->next++;
	return sz;
}

static inline int xeth_create_port_et_stat_name(struct device *dev,
						struct device_attribute *attr,
						const char *name)
{
	attr->attr.name = name;
	attr->attr.mode = VERIFY_OCTAL_PERMISSIONS(0644);
	attr->show = xeth_show_port_et_stat_name;
	attr->store = xeth_store_port_et_stat_name;
	return device_create_file(dev, attr);
}

static inline ssize_t
xeth_show_port_provision(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct xeth_vendor_port_provision *provision =
		container_of(attr, struct xeth_vendor_port_provision, attr);
	struct xeth_vendor_port *port =
		container_of(provision, struct xeth_vendor_port, provision);
	struct xeth_vendor *vendor =
		container_of(port, struct xeth_vendor, port);
	int i;
	for (i = 0; i < vendor->n_ports; i++)
		buf[i] = provision->subports[i] + '0';
	return i;
}

static inline int
xeth_create_port_provision(struct device *dev, struct device_attribute *attr,
			   const char *name)
{
	struct xeth_vendor_port_provision *provision =
		container_of(attr, struct xeth_vendor_port_provision, attr);
	struct xeth_vendor_port *port =
		container_of(provision, struct xeth_vendor_port, provision);
	struct xeth_vendor *vendor =
		container_of(port, struct xeth_vendor, port);
	int i;
	for (i = 0; i < vendor->n_ports; i++)
		switch (provision->subports[i]) {
		case 1:
		case 2:
		case 4:
			break;
		default:
			provision->subports[i] = 1;
		}
	attr->attr.name = name;
	attr->attr.mode = VERIFY_OCTAL_PERMISSIONS(0444);
	attr->show = xeth_show_port_provision;
	return device_create_file(dev, attr);
}

#endif	/* __LINUX_XETH_H */
