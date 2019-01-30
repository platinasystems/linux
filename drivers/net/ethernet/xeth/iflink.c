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

enum {
	xeth_max_iflinks = 16,
};

static struct net_device *xeth_iflinks[xeth_max_iflinks];
static bool xeth_iflink_registered[xeth_max_iflinks];
static const int const xeth_iflink_index_masks[xeth_max_iflinks] = {
	[000] = 001,
	[001] = 001,
	[002] = 001,
	[003] = 003,
	[004] = 003,
	[005] = 003,
	[006] = 003,
	[007] = 007,
	[010] = 007,
	[011] = 007,
	[012] = 007,
	[013] = 007,
	[014] = 007,
	[015] = 007,
	[016] = 007,
	[017] = 017,
};
static int xeth_iflink_index_mask;

struct net_device *xeth_iflink(int i)
{
	return i < xeth_max_iflinks ? rtnl_dereference(xeth_iflinks[i]) : NULL;
}

int xeth_iflink_index(u16 id)
{
	return id & xeth_iflink_index_mask;
}

void xeth_iflink_reset(int i)
{
	if (i >= xeth_max_iflinks)
		return;
	if (xeth_iflink_registered[i]) {
		struct net_device *iflink = xeth_iflink(i);
		if (iflink) {
			netdev_rx_handler_unregister(iflink);
			dev_put(iflink);
		}
		xeth_iflink_registered[i] = false;
	}
	RCU_INIT_POINTER(xeth_iflinks[i], NULL);
}

int xeth_iflink_init(void)
{
	int iflinki, akai, err = 0;
	int sz_jumbo_frame = SZ_8K + SZ_1K + xeth.encap.size;

	rtnl_lock();
	xeth_for_each_iflink(iflinki) {
		struct net_device *iflink = NULL;
		if (err)
			break;
		xeth_for_each_iflink_aka(iflinki, akai) {
			const char *ifname = xeth.iflinks_akas[iflinki][akai];
			iflink = dev_get_by_name(&init_net, ifname);
			if (xeth_pr_true_expr(!iflink, "%s not found", ifname))
				continue;
			err = netdev_rx_handler_register(iflink,
							 xeth.encap.rx,
							 &xeth);
			if (xeth_pr_true_expr(err,
					      "rx_handler_register(%s), %d",
					      ifname, err)) {
				dev_put(iflink);
				break;
			}
			rcu_assign_pointer(xeth_iflinks[iflinki], iflink);
			xeth_iflink_registered[iflinki] = true;
			xeth_iflink_index_mask = xeth_iflink_index_masks[iflinki];
			dev_set_mtu(iflink, sz_jumbo_frame);
			break;
		}
		if (err)
			break;
		if (!iflink) {
			err = -ENOENT;
			break;
		}
	}
	if (err)
		xeth_for_each_iflink(iflinki)
			xeth_iflink_reset(iflinki);
	rtnl_unlock();
	return err;
}

void xeth_iflink_exit(void)
{
	int i;
	rtnl_lock();
	xeth_for_each_iflink(i)
		xeth_iflink_reset(i);
	rtnl_unlock();
}
