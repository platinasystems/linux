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

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <net/rtnetlink.h>

#include "xeth.h"
#include "debug.h"

/* from <drivers/net/ethernet/intel/ixgbe> */
#define IXGBE_MAX_JUMBO_FRAME_SIZE 9728

#define platina_mk1_n_ports	32
#define platina_mk1_n_subports	4
#define platina_mk1_n_iflinks	2
#define platina_mk1_n_nds	(platina_mk1_n_ports * platina_mk1_n_subports)
#define platina_mk1_n_ids	(2 + platina_mk1_n_nds)
#define platina_mk1_n_encap	VLAN_HLEN

/* FIXME reset from board version at init */
static bool platina_mk1_one_based = false;

static u64 platina_mk1_eth0_ea64;
static unsigned char platina_mk1_eth0_ea_assign_type;

static inline int _platina_mk1_assert_iflinks(void)
{
	struct net_device *eth1 = dev_get_by_name(&init_net, "eth1");
	struct net_device *eth2 = dev_get_by_name(&init_net, "eth2");
	int err;

	if (!eth1) {
		err = xeth_debug_val("%d, %s", -ENOENT, "eth1");
		goto egress;
	}
	if (!eth2) {
		err = xeth_debug_val("%d, %s", -ENOENT, "eth2");
		goto egress;
	}
	err = xeth_debug_val("%d, %s",
			     netdev_rx_handler_register(eth1,
							xeth.ops.rx_handler,
							&xeth),
			     "eth1");
	if (err)
		goto egress;
	err = xeth_debug_val("%d, %s",
			     netdev_rx_handler_register(eth2,
							xeth.ops.rx_handler,
							&xeth),
			     "eth2");
	if (err) {
		netdev_rx_handler_unregister(eth1);
		goto egress;
	}
	if (true) {	/* FIXME sort by bus address */
		xeth_set_iflinks(0, eth1);
		xeth_set_iflinks(1, eth2);
	} else {
		xeth_set_iflinks(1, eth1);
		xeth_set_iflinks(0, eth2);
	}
	return 0;
egress:
	if (eth2)
		dev_put(eth2);
	if (eth2)
		dev_put(eth2);
	return err;
}

static int platina_mk1_assert_iflinks(void)
{
	static struct mutex platina_mk1_iflinks_mutex;
	int err = 0;

	if (xeth_iflinks(0))
		return err;
	mutex_lock(&platina_mk1_iflinks_mutex);
	if (!xeth_iflinks(0))
		err = _platina_mk1_assert_iflinks();
	mutex_unlock(&platina_mk1_iflinks_mutex);
	return err;
}

static int platina_mk1_parse_name(struct xeth_priv *priv, const char *name)
{
	int base = platina_mk1_one_based ? 1 : 0;
	u16 port, subport;

	if (sscanf(name, "eth-%hu-%hu", &port, &subport) != 2)
		return -EINVAL;
	if ((port > (platina_mk1_n_ports + base)) || (port < base))
		return -EINVAL;
	port -= base;
	if ((subport > (platina_mk1_n_subports + base)) || (subport < base))
		return -EINVAL;
	subport -= base;
	priv->id = 1 + ((port ^ 1) * platina_mk1_n_subports) + subport + 1;
	priv->ndi = (port * platina_mk1_n_subports) + subport;
	priv->iflinki = port >= (platina_mk1_n_ports / 2) ? 1 : 0;
	return 0;
}

static int platina_mk1_set_lladdr(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);

	if (!platina_mk1_eth0_ea64) {
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return xeth_debug_netdev_val(nd, "%d, can't find eth0",
						     -ENOENT);
		platina_mk1_eth0_ea64 = ether_addr_to_u64(eth0->dev_addr);
		platina_mk1_eth0_ea_assign_type = eth0->addr_assign_type;
		dev_put(eth0);
	}
	u64_to_ether_addr(platina_mk1_eth0_ea64 + 3 + priv->ndi, nd->dev_addr);
	nd->addr_assign_type = platina_mk1_eth0_ea_assign_type;
	return 0;
}

static int __init platina_mk1_init(void)
{
	int err;
	
	xeth.n.ids = platina_mk1_n_ids;
	xeth.n.nds = platina_mk1_n_nds,
	xeth.n.iflinks = platina_mk1_n_iflinks;
	xeth.n.encap = platina_mk1_n_encap;
	err = xeth_init();
	if (err)
		return err;

	xeth.ops.assert_iflinks = platina_mk1_assert_iflinks;
	xeth.ops.parse_name = platina_mk1_parse_name;
	xeth.ops.set_lladdr = platina_mk1_set_lladdr;

	err = xeth_link_init("platina-mk1");
	if (err) {
		xeth_exit();
		return err;
	}

	xeth_ndo_init();
	xeth_notifier_init();
	xeth_vlan_init();
	xeth_sysfs_init("platina-mk1");
	xeth_devfs_init("platina-mk1");

	return 0;
}

static void __exit platina_mk1_exit(void)
{
	xeth_notifier_exit();
	xeth_link_exit();
	xeth_ndo_exit();
	xeth_notifier_exit();
	xeth_vlan_exit();
	xeth_exit();
	xeth_sysfs_exit();
	xeth_devfs_exit();
}

module_init(platina_mk1_init);
module_exit(platina_mk1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("XETH for Platina Systems MK1 TOR Ethernet Switch");
