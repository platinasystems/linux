/* XETH driver, see include/net/xeth.h
 *
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

struct xeth xeth;

int xeth_init(void)
{
	int i;
	
	xeth.ndi_by_id = kcalloc(xeth.n.ids, sizeof(u16), GFP_KERNEL);
	if (!xeth.ndi_by_id)
		goto egress;
	xeth.nds = kcalloc(xeth.n.nds, sizeof(struct net_device*), GFP_KERNEL);
	if (!xeth.nds)
		goto egress;
	xeth.iflinks = kcalloc(xeth.n.iflinks, sizeof(struct net_device*),
			       GFP_KERNEL);
	if (!xeth.iflinks)
		goto egress;
	xeth.ea_iflinks = kcalloc(xeth.n.iflinks, sizeof(u64), GFP_KERNEL);
	if (!xeth.ea_iflinks)
		goto egress;
	for (i = 0; i < xeth.n.iflinks; i++)
		xeth_reset_iflink(i);
	for (i = 0; i < xeth.n.nds; i++)
		xeth_reset_nd(i);
	INIT_LIST_HEAD_RCU(&xeth.list);
	xeth_reset_counters();
	return 0;

egress:
	if (xeth.ea_iflinks)
		kfree(xeth.ea_iflinks);
	if (xeth.iflinks)
		kfree(xeth.iflinks);
	if (xeth.nds)
		kfree(xeth.nds);
	if (xeth.ndi_by_id)
		kfree(xeth.ndi_by_id);
	return -ENOMEM;
}

void xeth_exit(void)
{
	if (xeth.ea_iflinks)
		kfree(xeth.ea_iflinks);
	if (xeth.iflinks) {
		int i;
		for (i = 0; i < xeth.n.iflinks; i++) {
			struct net_device *iflink = xeth_iflink(i);
			if (iflink) {
				xeth_reset_iflink(i);
				rtnl_lock();
				netdev_rx_handler_unregister(iflink);
				rtnl_unlock();
				dev_put(iflink);
			}
		}
		kfree(xeth.iflinks);
	}
	if (xeth.nds)
		kfree(xeth.nds);
	if (xeth.ndi_by_id)
		kfree(xeth.ndi_by_id);
}
