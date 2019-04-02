/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __XETH_H
#define __XETH_H

#include <linux/etherdevice.h>

/**
 * DOC: Theory of Operation
 *
 */

/**
 * @base: 0 or 1 based port and subport
 * @ports: number of front panel ports
 * @rxqs: number of receive queues
 * @txqs: number of trnasmit queues
 * @provision: list of 1, 2, or 4 subports per port
 * @iflinks_akas: a NULL terminated list of NULL terminated iflink aliases
 * @ea.base: ethernet address of first port
 * @ea.assign_type: either NET_ADDR_PERM or NET_ADDR_RANDOM
 * @ethtool.init_settings: func that initializes a device ethtool settings
 * @ethtool.validate_speed: func that validates a given Mbps speed
 * @ethtool.flags: NULL terminated list of ethtool flag names
 * @ethtool.stats: NULL terminated list of ethtool stat names
 */
struct xeth_config {
	int base;
	size_t ports, rxqs, txqs;
	int *provision;		/* */
	const char * const * const *iflinks_akas;
	struct {
		u64	base;
		u8	assign_type;
	} ea;
	struct {
		void (*init_settings)(struct ethtool_link_ksettings *);
		int (*validate_speed)(u32);
		const char * const *flags;
		const char * const *stats;
	} ethtool;
};

struct kobject *xeth_create(struct kobject *parent,
			    const struct xeth_config *config);
void xeth_delete(struct kobject *xeth_kobj);

#endif  /* __XETH_H */
