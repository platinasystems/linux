/* XETH sysfs attributes.
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

#include <net/rtnetlink.h>

#include "xeth.h"
#include "debug.h"

struct xeth_sysfs_netstat {
	struct	attribute attr;
	size_t	offset;
};

#define to_xeth_sysfs_netstat(ap)					\
	container_of(ap, struct xeth_sysfs_netstat, attr)

#define new_xeth_sysfs_netstat(member)					\
static struct xeth_sysfs_netstat xeth_sysfs_##member = {		\
	.attr = {							\
		.name = #member,					\
		.mode = S_IRUGO | S_IWUSR,				\
	},								\
	.offset = offsetof(struct rtnl_link_stats64, member),		\
}

new_xeth_sysfs_netstat(rx_packets);
new_xeth_sysfs_netstat(tx_packets);
new_xeth_sysfs_netstat(rx_bytes);
new_xeth_sysfs_netstat(tx_bytes);
new_xeth_sysfs_netstat(rx_errors);
new_xeth_sysfs_netstat(tx_errors);
new_xeth_sysfs_netstat(rx_dropped);
new_xeth_sysfs_netstat(tx_dropped);
new_xeth_sysfs_netstat(multicast);
new_xeth_sysfs_netstat(collisions);
new_xeth_sysfs_netstat(rx_length_errors);
new_xeth_sysfs_netstat(rx_over_errors);
new_xeth_sysfs_netstat(rx_crc_errors);
new_xeth_sysfs_netstat(rx_frame_errors);
new_xeth_sysfs_netstat(rx_fifo_errors);
new_xeth_sysfs_netstat(rx_missed_errors);
new_xeth_sysfs_netstat(tx_aborted_errors);
new_xeth_sysfs_netstat(tx_carrier_errors);
new_xeth_sysfs_netstat(tx_fifo_errors);
new_xeth_sysfs_netstat(tx_heartbeat_errors);
new_xeth_sysfs_netstat(tx_window_errors);
new_xeth_sysfs_netstat(rx_compressed);
new_xeth_sysfs_netstat(tx_compressed);
new_xeth_sysfs_netstat(rx_nohandler);

static struct attribute *xeth_sysfs_netstat_attrs[] = {
	&xeth_sysfs_rx_packets.attr,
	&xeth_sysfs_tx_packets.attr,
	&xeth_sysfs_rx_bytes.attr,
	&xeth_sysfs_tx_bytes.attr,
	&xeth_sysfs_rx_errors.attr,
	&xeth_sysfs_tx_errors.attr,
	&xeth_sysfs_rx_dropped.attr,
	&xeth_sysfs_tx_dropped.attr,
	&xeth_sysfs_multicast.attr,
	&xeth_sysfs_collisions.attr,
	&xeth_sysfs_rx_length_errors.attr,
	&xeth_sysfs_rx_over_errors.attr,
	&xeth_sysfs_rx_crc_errors.attr,
	&xeth_sysfs_rx_frame_errors.attr,
	&xeth_sysfs_rx_fifo_errors.attr,
	&xeth_sysfs_rx_missed_errors.attr,
	&xeth_sysfs_tx_aborted_errors.attr,
	&xeth_sysfs_tx_carrier_errors.attr,
	&xeth_sysfs_tx_fifo_errors.attr,
	&xeth_sysfs_tx_heartbeat_errors.attr,
	&xeth_sysfs_tx_window_errors.attr,
	&xeth_sysfs_rx_compressed.attr,
	&xeth_sysfs_tx_compressed.attr,
	&xeth_sysfs_rx_nohandler.attr,
	NULL,
};

static ssize_t xeth_sysfs_netstat_show(struct kobject *kobj,
				       struct attribute *attr,
				       char *buf)
{
	struct xeth_priv *priv = to_xeth_priv(kobj);
	struct xeth_sysfs_netstat *ns = to_xeth_sysfs_netstat(attr);
	u64 val = 0;
	mutex_lock(&priv->mutex.stats);
	val = *(u64 *)(((u8 *) &priv->stats) + ns->offset);
	mutex_unlock(&priv->mutex.stats);
	return scnprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static ssize_t xeth_sysfs_netstat_store(struct kobject *kobj,
					struct attribute *attr,
					const char *buf, size_t count)
{
	struct xeth_priv *priv = to_xeth_priv(kobj);
	struct xeth_sysfs_netstat *ns = to_xeth_sysfs_netstat(attr);
	u64 val, *p = (u64 *)(((u8 *) &priv->stats) + ns->offset);
	int err = xeth_debug_true_val("%d", kstrtoull(buf, 10, &val));
	if (!err) {
		mutex_lock(&priv->mutex.stats);
		*p = val;
		mutex_unlock(&priv->mutex.stats);
	}
	return count;
}

static const struct sysfs_ops xeth_sysfs_netstat_ops = {
	.show   = xeth_sysfs_netstat_show,
	.store  = xeth_sysfs_netstat_store,
};

static struct kobj_type xeth_sysfs_netstat_ktype = {
	.release        = NULL,
	.sysfs_ops      = &xeth_sysfs_netstat_ops,
	.default_attrs  = xeth_sysfs_netstat_attrs,
};

static ssize_t xeth_sysfs_rcv_write(struct file *f,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf,
				    loff_t pos,
				    size_t len)
{
	struct sk_buff *skb = netdev_alloc_skb(xeth.iflinks[0], len);
	if (!skb)
		return -ENOMEM;
	skb_put(skb, len);
	memcpy(skb->data, buf, len);
	return xeth.ops.side_band_rx(skb);
}

struct bin_attribute xeth_sysfs_rcv_bin_attr = {
	.attr = {
		.name = "receive",
		.mode = S_IWUSR,
	},
	.size = PAGE_SIZE,		/* FIXME MTU */
	.write = xeth_sysfs_rcv_write,
};

void xeth_sysfs_init(const char *name)
{
	xeth.sysfs.root = root_device_register(name);
	if (IS_ERR_OR_NULL(xeth.sysfs.root))
		return;
	if (1)
	xeth_debug_true_val("%d",
			    sysfs_create_bin_file(&xeth.sysfs.root->kobj,
						  &xeth_sysfs_rcv_bin_attr));
}

void xeth_sysfs_exit(void)
{
	if (!IS_ERR_OR_NULL(xeth.sysfs.root)) {
		root_device_unregister(xeth.sysfs.root);
		xeth.sysfs.root = NULL;
	}
}

void xeth_sysfs_priv_init(struct xeth_priv *priv, const char *ifname)
{
	priv->kobj_err = -EINVAL;
	if (IS_ERR_OR_NULL(xeth.sysfs.root))
		return;
	priv->kobj_err = kobject_init_and_add(&priv->kobj,
					      &xeth_sysfs_netstat_ktype,
					      &xeth.sysfs.root->kobj,
					      ifname);
}

void xeth_sysfs_priv_exit(struct xeth_priv *priv)
{
	if (!priv->kobj_err)
		kobject_put(&priv->kobj);
}
