/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_PROP_H
#define __NET_ETHERNET_PROP_H

#include <linux/property.h>
#include <linux/gpio/consumer.h>
#include <uapi/linux/xeth.h>

enum {
	xeth_prop_max_links = 8,
	xeth_prop_max_flags = 8,
	xeth_prop_max_stats = 512,
	xeth_prop_max_ports = 512,
};

static inline const char *xeth_prop_mux_name(struct device *dev)
{
	/* FIXME change the default after goes-boot-mk1 update */
	const char *val;
	return device_property_read_string(dev, "mux-name", &val) ?
		"platina-mk1" : val;
}

static inline bool xeth_prop_encap_vpls(struct device *dev)
{
	return device_property_present(dev, "encap-vpls");
}

static inline size_t xeth_prop_ports(struct device *dev)
{
	u16 val;
	return device_property_read_u16(dev, "ports", &val) ? 32 : val; 
}

static inline u8 xeth_prop_base_port(struct device *dev)
{
	/* FIXME change to u8 after goes-boot-mk1 update */
	u32 val;
	return device_property_read_u32(dev, "base-port", &val) ?  1 : val;
}

static inline u64 xeth_prop_base_port_addr(struct device *dev)
{
	u64 val;
	return device_property_read_u64(dev, "base-port-addr", &val) ?
		0 : val;
}

static inline size_t xeth_prop_links(struct device *dev, const char **links)
{
	ssize_t n = device_property_read_string_array(dev, "links", links,
						      xeth_prop_max_links);
	if (n < 0) {
		/* FIXME remove these defaults after goes-boot-mk1 update */
		links[0] = "eth1,enp3s0f0";
		links[1] = "eth2,enp3s0f1";
		n = 2;
	}
	return n;
}

static inline size_t xeth_prop_flags(struct device *dev, const char **flags)
{
	ssize_t n = device_property_read_string_array(dev, "flags", flags,
						      xeth_prop_max_flags);
	return n < 0 ? 0 : n;
}

static inline size_t xeth_prop_stats(struct device *dev, const char **stats)
{
	ssize_t n = device_property_read_string_array(dev, "flags", stats,
						      xeth_prop_max_stats);
	return n < 0 ? 0 : n;
}

static inline u8 xeth_prop_mux_qs(struct device *dev, const char *label)
{
	u8 val;
	return device_property_read_u8(dev, label, &val) ?  1 : val;
}

static inline u8 xeth_prop_mux_txqs(struct device *dev)
{
	return xeth_prop_mux_qs(dev, "mux-txqs");
}

static inline u8 xeth_prop_mux_rxqs(struct device *dev)
{
	return xeth_prop_mux_qs(dev, "mux-rxqs");
}

static inline u8 xeth_prop_port_txqs(struct device *dev)
{
	return xeth_prop_mux_qs(dev, "port-txqs");
}

static inline u8 xeth_prop_port_rxqs(struct device *dev)
{
	return xeth_prop_mux_qs(dev, "port-rxqs");
}

/* Use gpiod_put_array() when finished */
static inline struct gpio_descs *
xeth_prop_qsfp_absent_gpios(struct device *dev)
{
	return gpiod_get_array_optional(dev, "absent", 0);
}

static inline struct gpio_descs *
xeth_prop_qsfp_intr_gpios(struct device *dev)
{
	return gpiod_get_array_optional(dev, "int", 0);
}

static inline struct gpio_descs *
xeth_prop_qsfp_lpmode_gpios(struct device *dev)
{
	return gpiod_get_array_optional(dev, "lpmode", 0);
}

static inline struct gpio_descs *
xeth_prop_qsfp_reset_gpios(struct device *dev)
{
	return gpiod_get_array_optional(dev, "reset", 0);
}

static inline int xeth_prop_get_gpios(struct gpio_descs *gpios,
				      unsigned long *val)
{
	return gpiod_get_array_value_cansleep(gpios->ndescs, gpios->desc,
					      gpios->info, val);
}

static inline int xeth_prop_set_gpios(struct gpio_descs *gpios,
				      unsigned long *val)
{
	return gpiod_set_array_value_cansleep(gpios->ndescs, gpios->desc,
					      gpios->info, val);
}

static inline size_t xeth_prop_qsfp_buses(struct device *dev, u8 *buses)
{
	/* FIXME remove this -mk1 default after goes-boot update */
	static const u8 const default_buses[] = {
		 3,  2,  5,  4,  7,  6,  9,  8,
		12, 11, 14, 13, 16, 15, 18, 17,
		21, 20, 23, 22, 25, 24, 27, 26,
		30, 29, 32, 31, 34, 33, 36, 35,
	};
	ssize_t	n = device_property_read_u8_array(dev, "qsfp-i2c-buses",
						  buses, xeth_prop_max_ports);
	if (n < 0)
		for (n = 0; n < ARRAY_SIZE(default_buses); n++)
			buses[n] = default_buses[n];
	return n;
};

#endif /* __NET_ETHERNET_PROP_H */
