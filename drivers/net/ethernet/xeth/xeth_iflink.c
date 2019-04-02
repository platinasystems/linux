/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
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

int xeth_iflink_start(void)
{
	int i, aka, err = 0;
	int sz_jumbo_frame = SZ_8K + SZ_1K + xeth_encap_size;

	rtnl_lock();
	xeth_for_each_iflink(i) {
		struct net_device *iflink = NULL;
		if (err)
			break;
		xeth_for_each_iflink_aka(i, aka) {
			const char *ifname;
			ifname = xeth_config->iflinks_akas[i][aka];
			iflink = dev_get_by_name(&init_net, ifname);
			if (!iflink)
				continue;
			err = netdev_rx_handler_register(iflink,
							 xeth_vlan_rx,
							 NULL);
			if (err) {
				dev_put(iflink);
				break;
			}
			rcu_assign_pointer(xeth_iflinks[i], iflink);
			xeth_iflink_registered[i] = true;
			xeth_iflink_index_mask = xeth_iflink_index_masks[i];
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
		xeth_for_each_iflink(i)
			xeth_iflink_reset(i);
	rtnl_unlock();
	return err;
}

void xeth_iflink_stop(void)
{
	int i;
	rtnl_lock();
	xeth_for_each_iflink(i)
		xeth_iflink_reset(i);
	rtnl_unlock();
}
