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

int xeth_init(void)
{
	int i, err = 0;

	xeth_ht_init();

	INIT_LIST_HEAD_RCU(&xeth.free_vids);

	for (i = 0; i < xeth.ports; i++) {
		int provision = xeth.provision[i];
		if (xeth_pr_true_expr(provision != 0 &&
				      provision != 1 &&
				      provision != 2 &&
				      provision != 4,
				      "provision[%d] == %d",
				      i, provision))
			return -EINVAL;
	}

	xeth_reset_counters();

	err = xeth.encap.init();
	if (!err)
		err = xeth_ndo_init();
	if (!err)
		err = xeth_ethtool_init();
	if (!err)
		err = xeth_sb_init();
	if (!err)
		err = xeth_iflink_init();
	if (!err)
		err = xeth_dev_init();
	if (!err)
		err = xeth_notifier_init();
	if (err)
		xeth_exit();
	return err;
}

void xeth_exit(void)
{
	struct xeth_vid *vid;

	xeth_notifier_exit();
	xeth_sb_exit();
	xeth_ethtool_exit();
	xeth_ndo_exit();
	xeth.encap.exit();
	xeth_iflink_exit();
	xeth_dev_exit();

	while (vid = xeth_vid_pop(&xeth.free_vids), vid != NULL)
		kfree(vid);
}
