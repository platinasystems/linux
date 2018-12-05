/* Platina Systems XETH driver for the MK1 top of rack ethernet switch
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/if_vlan.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/property.h>
#include <uapi/linux/xeth.h>

#include "platina_mk1.h"
#include "platina_mk1_flags.h"
#include "platina_mk1_stats.h"

static const char *const platina_mk1_flags[] = {
	PLATINA_MK1_FLAGS
};

static const char *const platina_mk1_stats[] = {
	PLATINA_MK1_STATS
};

enum {
	platina_mk1_n_ports = 32,
	platina_mk1_n_rxqs = 1,
	platina_mk1_n_txqs = 1,
	platina_mk1_n_flags = ARRAY_SIZE(platina_mk1_flags) - 1,
	platina_mk1_n_stats = ARRAY_SIZE(platina_mk1_stats) - 1,
	platina_mk1_priv_size = sizeof(struct xeth_priv) +
		(platina_mk1_n_stats * sizeof(u64)),
};

static bool alpha;
static int provision[platina_mk1_n_ports];
static const char * const platina_mk1_eth1_akas[] = {
	"eth1", "enp3s0f0", NULL
};
static const char * const platina_mk1_eth2_akas[] = {
	"eth2", "enp3s0f1", NULL
};
static const char * const * const platina_mk1_iflinks_akas[] = {
	platina_mk1_eth1_akas,
	platina_mk1_eth2_akas,
	NULL,
};
struct xeth xeth = {
	.provision = provision,
	.iflinks_akas = platina_mk1_iflinks_akas,
	.name = "platina-mk1",
	.ports = platina_mk1_n_ports,
	.rxqs = platina_mk1_n_rxqs,
	.txqs = platina_mk1_n_txqs,
	.priv_size = platina_mk1_priv_size,
	.encap.init = xeth_vlan_init,
	.encap.exit = xeth_vlan_exit,
	.ethtool.n.flags = platina_mk1_n_flags,
	.ethtool.n.stats = platina_mk1_n_stats,
	.ethtool.flags = platina_mk1_flags,
	.ethtool.stats = platina_mk1_stats,
	.init_ethtool_settings = platina_mk1_ethtool_init_settings,
	.validate_speed = platina_mk1_ethtool_validate_speed,

};

static void platina_mk1_end(void)
{
	platina_mk1_i2c_exit();
	if (xeth.kset)
		kset_unregister(xeth.kset);
}

static int __init platina_mk1_init(void)
{
	int err;

	/* FIXME set xeth.base from i2c eeprom instead of module parameter */
	xeth.base = alpha ? 0 : 1;
	xeth.kset = kset_create_and_add(xeth.name, NULL, kernel_kobj);
	if (!xeth.kset)
		return -ENOMEM;
	err = platina_mk1_i2c_init();
	if (!err)
		err = xeth_init();
	if (err)
		platina_mk1_end();
	return err;
}

static void __exit platina_mk1_exit(void)
{
	xeth_exit();
	platina_mk1_end();
}

module_init(platina_mk1_init);
module_exit(platina_mk1_exit);
module_param(alpha, bool, false);
module_param_array(provision, int, NULL, 0644);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("XETH for Platina Systems MK1 TOR Ethernet Switch");
MODULE_PARM_DESC(alpha, "zero based ports and subports");
MODULE_PARM_DESC(provision, "1, 2, or 4 subports per port, default 1");
