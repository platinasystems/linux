/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

#ifndef __NET_ETHERNET_XETH_PLATINA_H
#define __NET_ETHERNET_XETH_PLATINA_H

#if IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA)

#define xeth_platina_extern	extern

#else /* !IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA) */

#define xeth_platina_extern	static

#endif /* IS_BUILTIN(CONFIG_NET_XETH_VENDOR_PLATINA) */

#define xeth_platina_device_ids						\
	{ .name = "platina-mk1" }

xeth_platina_extern const struct xeth_config xeth_platina_mk1_config;

#define xeth_platina_configs						\
	&xeth_platina_mk1_config

#endif /* __NET_ETHERNET_XETH_PLATINA_H */
