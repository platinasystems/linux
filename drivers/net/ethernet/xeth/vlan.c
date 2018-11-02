/* XETH driver's VLAN encapsulations.
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

#include <linux/if_vlan.h>

enum {
	xeth_vlan_first_dev = 4094,
	xeth_vlan_first_associate_dev = 1,
	xeth_vlan_n_nd = xeth_vlan_first_dev + 1,
};

/* The vlan_nd table contains two types of devices: from the top (4094) down
 * are the xeth ports and subports, from the bottom (1) up are the user devices
 * (e.g. bridge and tunnels) that master or associate with xeth devices.
 */
static struct net_device *xeth_vlan_nd[xeth_vlan_n_nd];

static int xeth_vlan_next_dev = xeth_vlan_first_dev;

#define xeth_vlan_for_each_dev(index) \
	for ((index) = xeth_vlan_first_dev; \
	     (index) > xeth_vlan_next_dev; \
	     (index)--)

static int xeth_vlan_next_associate_dev = xeth_vlan_first_associate_dev;

#define xeth_vlan_for_each_associate_dev(index) \
	for ((index) = xeth_vlan_first_associate_dev; \
	     (index) < xeth_vlan_next_associate_dev; \
	     (index)++)

static inline bool xeth_vlan_is_8021X(__be16 proto)
{
	return  proto == cpu_to_be16(ETH_P_8021Q) ||
		proto == cpu_to_be16(ETH_P_8021AD);
}

static inline struct net_device *xeth_vlan_get_nd(int i)
{
	return rtnl_dereference(xeth_vlan_nd[i]);
}

static inline int xeth_vlan_set_nd(int i, struct net_device *nd)
{
	rcu_assign_pointer(xeth_vlan_nd[i], nd);
	return i;
}

static inline void xeth_vlan_reset_nd(int i)
{
	RCU_INIT_POINTER(xeth_vlan_nd[i], NULL);
}

static int xeth_vlan_id(struct net_device *nd)
{
	int i;

	if (netif_is_xeth(nd)) {
		struct xeth_priv *priv = netdev_priv(nd);
		return priv->id;
	}
	xeth_vlan_for_each_associate_dev(i)
		if (xeth_vlan_get_nd(i) == nd)
			return i;
	return -ENODEV;
}

static int xeth_vlan_new_dev(struct net_device *nd)
{
	int i;
	struct xeth_priv *priv = netdev_priv(nd);
	if (xeth_vlan_next_dev == xeth_vlan_next_associate_dev)
		return -ENOSPC;
	i = xeth_vlan_set_nd(xeth_vlan_next_dev--, nd);
	priv->id = i;
	return i;
}

static int xeth_vlan_hold_dev(struct net_device *nd, int i)
{
	dev_hold(nd);
	return xeth_vlan_set_nd(i, nd);
}

static int xeth_vlan_associate_dev(struct net_device *nd)
{
	int i;
	if (xeth_vlan_next_associate_dev < xeth_vlan_next_dev)
		return xeth_vlan_hold_dev(nd, xeth_vlan_next_associate_dev++);
	/* search list for disassociated entries */
	xeth_vlan_for_each_associate_dev(i)
		if (xeth_vlan_get_nd(i) == NULL)
			return xeth_vlan_hold_dev(nd, i);
	return -ENOSPC;
}

static void xeth_vlan_dump_associate_devs(void) {
	int i;

	xeth_vlan_for_each_associate_dev(i) {
		struct net_device *nd = xeth_vlan_get_nd(i);
		if (nd)
			xeth_sb_dump_ifinfo(nd);
	}
}

static void xeth_vlan_disassociate_dev(struct net_device *nd)
{
	int i;
	if (netif_is_xeth(nd)) {
		return;
	}
	xeth_vlan_for_each_associate_dev(i) {
		if (xeth_vlan_get_nd(i) == nd) {
			xeth_vlan_reset_nd(i);
			dev_put(nd);
			return;
		}
	}
}

static void xeth_vlan_changemtu(struct net_device *iflink)
{
	int i;
	xeth_vlan_for_each_dev(i) {
		struct net_device *nd = xeth_vlan_get_nd(i);
		if (dev_get_iflink(nd) == iflink->ifindex)
			dev_set_mtu(nd, iflink->mtu);
	}
}

/* If tagged, pop dev index  and skb priority from outer VLAN;
 * otherwise, RX_HANDLER_PASS through to upper protocols.
 */
static rx_handler_result_t xeth_vlan_rx(struct sk_buff **pskb)
{
	u16 vid;
	int r, bytes;
	struct net_device *nd;
	struct sk_buff *skb = *pskb;
	
	if (!xeth_vlan_is_8021X(skb->vlan_proto)) {
		xeth_count_inc(rx_invalid);
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}
	skb->priority =
		(typeof(skb->priority))(skb->vlan_tci >> VLAN_PRIO_SHIFT);
	vid = skb->vlan_tci & VLAN_VID_MASK;
	nd = xeth_vlan_get_nd(vid);
	if (!nd) {
		xeth_count_inc(rx_no_dev);
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}
	if (netif_is_xeth(nd)) {
		struct xeth_priv *priv = netdev_priv(nd);
		struct net_device *iflink = xeth_iflink(priv->iflinki);
		if (vid != priv->id || skb->dev != iflink) {
			xeth_count_priv_inc(priv, rx_nd_mismatch);
			kfree_skb(skb);
			return RX_HANDLER_CONSUMED;
		}
	}
	if (skb->protocol == cpu_to_be16(ETH_P_8021Q)) {
		struct vlan_hdr *iv = (struct vlan_hdr *)skb->data;
		skb->vlan_proto = skb->protocol;
		skb->vlan_tci = VLAN_TAG_PRESENT | be16_to_cpu(iv->h_vlan_TCI);
		skb->protocol = iv->h_vlan_encapsulated_proto;
		skb_pull_rcsum(skb, VLAN_HLEN);
		/* make DST, SRC address precede encapsulated protocol */
		memmove(skb->data-ETH_HLEN, skb->data-(ETH_HLEN+VLAN_HLEN),
			2*ETH_ALEN);
	} else {
		skb->vlan_proto = 0;
		skb->vlan_tci = 0;
	}
	skb_push(skb, ETH_HLEN);
	skb->dev = nd;
	no_xeth_pr_skb_hex_dump(skb);
	bytes = skb->len;
	r = dev_forward_skb(nd, skb);
	if (netif_is_xeth(nd)) {
		struct xeth_priv *priv = netdev_priv(nd);
		if (r == NET_RX_SUCCESS) {
			xeth_count_priv_inc(priv, rx_packets);
			xeth_count_priv_add(priv, bytes, rx_bytes);
		} else {
			xeth_count_priv_inc(priv, rx_dropped);
		}
	}
	return RX_HANDLER_CONSUMED;
}


static void xeth_vlan_sb(const char *buf, size_t n)
{
	u16 vid;
	int bytes;
	struct net_device *nd;
	struct xeth_priv *priv;
	struct sk_buff *skb;
	struct vlan_ethhdr *ve = (struct vlan_ethhdr *)buf;

	no_xeth_pr_buf_hex_dump(buf, n);
	if (!xeth_vlan_is_8021X(ve->h_vlan_proto)) {
		xeth_count_inc(sb_invalid);
		return;
	}
	vid = be16_to_cpu(ve->h_vlan_TCI) & VLAN_VID_MASK;
	nd = xeth_vlan_get_nd(vid);
	if (!nd) {
		xeth_count_inc(sb_no_dev);
		return;
	}
	priv = netdev_priv(nd);
	skb = netdev_alloc_skb(nd, n);
	if (!skb) {
		xeth_count_priv_inc(priv, sb_nomem);
		return;
	}
	skb_put(skb, n);
	memcpy(skb->data, buf, n);
	if (xeth_vlan_is_8021X(ve->h_vlan_encapsulated_proto)) {
		const size_t vesz = sizeof(struct vlan_ethhdr);
		struct vlan_hdr *iv = (struct vlan_hdr *)(skb->data + vesz);
		skb->vlan_proto = ve->h_vlan_encapsulated_proto;
		skb->vlan_tci = VLAN_TAG_PRESENT | be16_to_cpu(iv->h_vlan_TCI);
		skb_pull(skb, 2*VLAN_HLEN);
	} else {
		skb_pull(skb, VLAN_HLEN);
	}
	/* restore mac addrs to beginning of de-encapsulated frame */
	memcpy(skb->data, buf, 2*ETH_ALEN);
	skb->protocol = eth_type_trans(skb, skb->dev);
	skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);
	no_xeth_pr_skb_hex_dump(skb);
	bytes = skb->len;
	if (netif_rx_ni(skb) != NET_RX_SUCCESS) {
		xeth_count_priv_inc(priv, sb_dropped);
	} else {
		xeth_count_priv_inc(priv, sb_packets);
		xeth_count_priv_add(priv, bytes, sb_bytes);
	}
}

/* Push outer VLAN tag with xeth's vid and skb's priority. */
static netdev_tx_t xeth_vlan_tx(struct sk_buff *skb, struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);
	struct net_device *iflink = xeth_iflink(priv->iflinki);
	u16 tpid = xeth_vlan_is_8021X(skb->protocol)
		? cpu_to_be16(ETH_P_8021AD)
		: cpu_to_be16(ETH_P_8021Q);
	u16 pcp = (u16)(skb->priority) << VLAN_PRIO_SHIFT;
	u16 tci = pcp | priv->id;

	skb = vlan_insert_tag_set_proto(skb, tpid, tci);
	if (!skb) {
		xeth_count_priv_inc(priv, tx_nomem);
	} else if (priv->ndi < 0) {
		xeth_count_priv_inc(priv, tx_noway);
	} else if (!iflink) {
		xeth_count_priv_inc(priv, tx_no_iflink);
	} else {
		int bytes = skb->len;
		skb->dev = iflink;
		no_xeth_pr_skb_hex_dump(skb);
		if (dev_queue_xmit(skb)) {
			xeth_count_priv_inc(priv, tx_dropped);
		} else {
			xeth_count_priv_inc(priv, tx_packets);
			xeth_count_priv_add(priv, bytes, tx_bytes);
		}
	}
	return NETDEV_TX_OK;
}

int xeth_vlan_init(void)
{
	xeth.encap.size = VLAN_HLEN;
	xeth.encap.id = xeth_vlan_id;
	xeth.encap.new_dev = xeth_vlan_new_dev;
	xeth.encap.associate_dev = xeth_vlan_associate_dev;
	xeth.encap.dump_associate_devs = xeth_vlan_dump_associate_devs;
	xeth.encap.disassociate_dev = xeth_vlan_disassociate_dev;
	xeth.encap.changemtu = xeth_vlan_changemtu;
	xeth.encap.rx = xeth_vlan_rx;
	xeth.encap.sb = xeth_vlan_sb;
	xeth.encap.tx = xeth_vlan_tx;
	return 0;
}

void xeth_vlan_exit(void)
{
	int i;
	xeth_vlan_for_each_associate_dev(i) {
		struct net_device *nd = xeth_vlan_get_nd(i);
		if (nd) {
			xeth_vlan_reset_nd(i);
			dev_put(nd);
		}
	}
	xeth_vlan_for_each_dev(i) {
		struct net_device *nd = xeth_vlan_get_nd(i);
		if (nd) {
			xeth_vlan_reset_nd(i);
		}
	}
}
