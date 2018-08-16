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

void xeth_iflink_reset(int i)
{
	if (i < xeth.n.iflinks) {
		RCU_INIT_POINTER(xeth.iflink.nd[i], NULL);
		xeth.iflink.ea[i] = 0;
	}
}

void xeth_iflink_set(int i, struct net_device *iflink)
{
	if (i < xeth.n.iflinks) {
		rcu_assign_pointer(xeth.iflink.nd[i], iflink);
		xeth.iflink.ea[i] = ether_addr_to_u64(iflink->dev_addr);
	}
}

int xeth_iflink_init(void)
{
	int i, err;

	rtnl_lock();
	for (i = 0; i < xeth.n.iflinks; i++) {
		const char *ifname = xeth.iflink.name[i];
		struct net_device *iflink = dev_get_by_name(&init_net, ifname);
		if (xeth_pr_true_expr(!iflink, "%s not found", ifname))
			return -ENOENT;
		err = netdev_rx_handler_register(iflink,
						 xeth.ops.encap.rx,
						 &xeth);
		if (err) {
			dev_put(iflink);
			break;
		}
		xeth_iflink_set(i, iflink);
		xeth.iflink.registered[i] = true;
	}
	rtnl_unlock();
	return err;
}

void xeth_iflink_exit(void)
{
	int i;

	for (i = 0; i < xeth.n.iflinks; i++) {
		struct net_device *iflink = xeth_iflink_nd(i);
		if (iflink) {
			xeth_iflink_reset(i);
			if (xeth.iflink.registered[i]) {
				rtnl_lock();
				netdev_rx_handler_unregister(iflink);
				rtnl_unlock();
			}
			dev_put(iflink);
		}
	}
}
