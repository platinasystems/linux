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

#include "sysfs.h"

struct xeth_sysfs_link_stat {
	struct	attribute attr;
	size_t	offset;
};

static size_t xeth_sysfs_link_stat_offsets[] = {
	offsetof(struct rtnl_link_stats64, rx_packets),
	offsetof(struct rtnl_link_stats64, tx_packets),
	offsetof(struct rtnl_link_stats64, rx_bytes),
	offsetof(struct rtnl_link_stats64, tx_bytes),
	offsetof(struct rtnl_link_stats64, rx_errors),
	offsetof(struct rtnl_link_stats64, tx_errors),
	offsetof(struct rtnl_link_stats64, rx_dropped),
	offsetof(struct rtnl_link_stats64, tx_dropped),
	offsetof(struct rtnl_link_stats64, multicast),
	offsetof(struct rtnl_link_stats64, collisions),
	offsetof(struct rtnl_link_stats64, rx_length_errors),
	offsetof(struct rtnl_link_stats64, rx_over_errors),
	offsetof(struct rtnl_link_stats64, rx_crc_errors),
	offsetof(struct rtnl_link_stats64, rx_frame_errors),
	offsetof(struct rtnl_link_stats64, rx_fifo_errors),
	offsetof(struct rtnl_link_stats64, rx_missed_errors),
	offsetof(struct rtnl_link_stats64, tx_aborted_errors),
	offsetof(struct rtnl_link_stats64, tx_carrier_errors),
	offsetof(struct rtnl_link_stats64, tx_fifo_errors),
	offsetof(struct rtnl_link_stats64, tx_heartbeat_errors),
	offsetof(struct rtnl_link_stats64, tx_window_errors),
	offsetof(struct rtnl_link_stats64, rx_compressed),
	offsetof(struct rtnl_link_stats64, tx_compressed),
	offsetof(struct rtnl_link_stats64, rx_nohandler),
};

static struct attribute xeth_sysfs_link_stat_attrs[] = {
	new_xeth_sysfs_attr(rx_packets),
	new_xeth_sysfs_attr(tx_packets),
	new_xeth_sysfs_attr(rx_bytes),
	new_xeth_sysfs_attr(tx_bytes),
	new_xeth_sysfs_attr(rx_errors),
	new_xeth_sysfs_attr(tx_errors),
	new_xeth_sysfs_attr(rx_dropped),
	new_xeth_sysfs_attr(tx_dropped),
	new_xeth_sysfs_attr(multicast),
	new_xeth_sysfs_attr(collisions),
	new_xeth_sysfs_attr(rx_length_errors),
	new_xeth_sysfs_attr(rx_over_errors),
	new_xeth_sysfs_attr(rx_crc_errors),
	new_xeth_sysfs_attr(rx_frame_errors),
	new_xeth_sysfs_attr(rx_fifo_errors),
	new_xeth_sysfs_attr(rx_missed_errors),
	new_xeth_sysfs_attr(tx_aborted_errors),
	new_xeth_sysfs_attr(tx_carrier_errors),
	new_xeth_sysfs_attr(tx_fifo_errors),
	new_xeth_sysfs_attr(tx_heartbeat_errors),
	new_xeth_sysfs_attr(tx_window_errors),
	new_xeth_sysfs_attr(rx_compressed),
	new_xeth_sysfs_attr(tx_compressed),
	new_xeth_sysfs_attr(rx_nohandler),
};

static size_t xeth_sysfs_link_stat_attr_index(struct attribute *attr)
{
	return (size_t)(attr-xeth_sysfs_link_stat_attrs);
}

#define xeth_sysfs_n_link_stats						\
	sizeof(xeth_sysfs_link_stat_attrs)/sizeof(struct attribute)

static struct attribute
*xeth_sysfs_link_stat_ktype_default_attrs[1+xeth_sysfs_n_link_stats];

static ssize_t xeth_sysfs_link_stat_show(struct kobject *kobj,
				       struct attribute *attr,
				       char *buf)
{
	struct xeth_priv *priv = xeth_priv_of_link(kobj);
	size_t index = xeth_sysfs_link_stat_attr_index(attr);
	size_t offset = xeth_sysfs_link_stat_offsets[index];
	u64 val = 0;
	mutex_lock(&priv->link_mutex);
	val = *(u64 *)(((u8 *) &priv->link_stats) + offset);
	mutex_unlock(&priv->link_mutex);
	return scnprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static ssize_t xeth_sysfs_link_stat_store(struct kobject *kobj,
					struct attribute *attr,
					const char *buf, size_t count)
{
	struct xeth_priv *priv = xeth_priv_of_link(kobj);
	size_t index = xeth_sysfs_link_stat_attr_index(attr);
	size_t offset = xeth_sysfs_link_stat_offsets[index];
	u64 val, *p = (u64 *)(((u8 *) &priv->link_stats) + offset);
	int err = xeth_debug_true_val("%d", kstrtoull(buf, 10, &val));
	if (!err) {
		mutex_lock(&priv->link_mutex);
		*p = val;
		mutex_unlock(&priv->link_mutex);
	}
	return count;
}

static const struct sysfs_ops xeth_sysfs_link_stat_ops = {
	.show   = xeth_sysfs_link_stat_show,
	.store  = xeth_sysfs_link_stat_store,
};

static struct kobj_type xeth_sysfs_link_stat_ktype = {
	.release        = NULL,
	.sysfs_ops      = &xeth_sysfs_link_stat_ops,
	.default_attrs  = xeth_sysfs_link_stat_ktype_default_attrs,
};

static ssize_t xeth_sysfs_ethtool_stat_show(struct kobject *kobj,
					    struct attribute *attr,
					    char *buf)
{
	struct xeth_priv *priv = xeth_priv_of_ethtool(kobj);
	size_t index = xeth.ops.ethtool_stat_attr_index(attr);
	u64 val = 0;
	mutex_lock(&priv->ethtool_mutex);
	val = priv->ethtool_stats[index];
	mutex_unlock(&priv->ethtool_mutex);
	return scnprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static ssize_t xeth_sysfs_ethtool_stat_store(struct kobject *kobj,
					     struct attribute *attr,
					     const char *buf, size_t count)
{
	struct xeth_priv *priv = xeth_priv_of_ethtool(kobj);
	size_t index = xeth.ops.ethtool_stat_attr_index(attr);
	u64 val;
	int err = xeth_debug_true_val("%d", kstrtoull(buf, 10, &val));
	if (!err) {
		mutex_lock(&priv->ethtool_mutex);
		priv->ethtool_stats[index] = val;
		mutex_unlock(&priv->ethtool_mutex);
	}
	return count;
}

static const struct sysfs_ops xeth_sysfs_ethtool_stat_ops = {
	.show   = xeth_sysfs_ethtool_stat_show,
	.store  = xeth_sysfs_ethtool_stat_store,
};

void xeth_sysfs_init(const char *name)
{
	int i;

	for (i = 0; i < xeth_sysfs_n_link_stats; i++)
		xeth_sysfs_link_stat_ktype_default_attrs[i] =
			&xeth_sysfs_link_stat_attrs[i];
	xeth_sysfs_link_stat_ktype.default_attrs =
		xeth_sysfs_link_stat_ktype_default_attrs;

	xeth.sysfs.root = root_device_register(name);
	if (IS_ERR_OR_NULL(xeth.sysfs.root))
		return;
	if (xeth.n.ethtool_stats) {
		xeth.ethtool_stat_ktype.release = NULL;
		xeth.ethtool_stat_ktype.sysfs_ops =
			&xeth_sysfs_ethtool_stat_ops;
	}
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
	priv->link_kobj_err = -EINVAL;
	if (IS_ERR_OR_NULL(xeth.sysfs.root))
		return;
	priv->link_kobj_err =
		kobject_init_and_add(&priv->link_kobj,
				     &xeth_sysfs_link_stat_ktype,
				     &xeth.sysfs.root->kobj,
				     ifname);
	if (priv->link_kobj_err)
		return;
	if (!xeth.n.ethtool_stats)
		return;
	priv->ethtool_kobj_err =
		kobject_init_and_add(&priv->ethtool_kobj,
				     &xeth.ethtool_stat_ktype,
				     &priv->link_kobj,
				     "ethtool");
}

void xeth_sysfs_priv_exit(struct xeth_priv *priv)
{
	if (!priv->link_kobj_err)
		kobject_put(&priv->link_kobj);
}
