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

static int xeth_dev_new(char *ifname)
{
	int err;
	struct xeth_priv *priv;
	struct net_device *nd =
		alloc_netdev_mqs(xeth.n.priv_size,
				 ifname, NET_NAME_USER,
				 ether_setup,
				 xeth.n.txqs, xeth.n.rxqs);
	if (IS_ERR(nd))
		return PTR_ERR(nd);
	xeth_link_ops.setup(nd);
	priv = netdev_priv(nd);
	err = xeth_parse_name(ifname, &priv->ref);
	if (!err)
		err = xeth_link_register(nd);
	return err;
}

int xeth_dev_init(void)
{
	char ifname[IFNAMSIZ];
	int port, sub;
	int err = 0;

	rtnl_lock();
	for (port = 0; !err && port < xeth.n.ports; port++) {
		int provision = xeth.dev.provision[port];
		if (provision > 1) {
			for (sub = 0; !err && sub < provision; sub++) {
				sprintf(ifname, "xeth%d-%d",
					port+xeth.n.base, sub+xeth.n.base);
				err = xeth_dev_new(ifname);
			}
		} else {
			sprintf(ifname, "xeth%d", port+xeth.n.base);
			err = xeth_dev_new(ifname);
		}
	}
	rtnl_unlock();
	return err;
}

void xeth_dev_exit(void)
{
	rtnl_lock();
	while (true) {
		struct xeth_priv *priv =
			list_first_or_null_rcu(&xeth.privs,
					       struct xeth_priv,
					       list);
		if (!priv)
			break;
		if (priv->nd->reg_state == NETREG_REGISTERED)
			xeth_link_ops.dellink(priv->nd, NULL);
		else
			list_del_rcu(&priv->list);
	}
	rtnl_unlock();
}
