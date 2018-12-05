/* Copyright(c) 2018 Platina Systems, Inc.
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

#include <linux/kobject.h>

struct xeth_sysfs_count_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct xeth_sysfs_count_attr *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj,
			 struct xeth_sysfs_count_attr *attr,
			 const char *buf, size_t count);
	u16	index;
};

#define to_xeth_sysfs_count_attr(x)					\
	container_of((x), struct xeth_sysfs_count_attr, attr)

#define xeth_sysfs_count_priv_flag	(1<<15)
#define xeth_sysfs_count_priv_mask	(xeth_sysfs_count_priv_flag-1)

#define xeth_sysfs_count(name)		xeth_count_##name

#define xeth_sysfs_count_priv(name)					\
	xeth_count_priv_##name | xeth_sysfs_count_priv_flag

#define is_xeth_sysfs_count_priv_attr(x)				\
	((x)->index & xeth_sysfs_count_priv_flag) == xeth_sysfs_count_priv_flag


static atomic64_t *xeth_sysfs_counter(struct kobject *kobj,
				      struct xeth_sysfs_count_attr *attr)
{
	size_t index = attr->index & xeth_sysfs_count_priv_mask;
	return	is_xeth_sysfs_count_priv_attr(attr) ?
		&to_xeth_priv(kobj)->count[index] :
		&xeth.count[index];
}

static ssize_t xeth_sysfs_count_show(struct kobject *kobj,
				     struct xeth_sysfs_count_attr *attr,
				     char *buf)
{
	long long ll = atomic64_read(xeth_sysfs_counter(kobj, attr));
	return scnprintf(buf, PAGE_SIZE, "%llu\n", ll);
}

static ssize_t xeth_sysfs_attr_count_show(struct kobject *kobj,
					  struct attribute *kattr,
					  char *buf)
{
	struct xeth_sysfs_count_attr *attr = to_xeth_sysfs_count_attr(kattr);
	if (attr->show != xeth_sysfs_count_show)
		return -EIO;
	return attr->show(kobj, attr, buf);
}

static ssize_t xeth_sysfs_count_store(struct kobject *kobj,
				      struct xeth_sysfs_count_attr *attr,
				      const char *buf, size_t len)
{
	long long ll;
	int err = kstrtoll(buf, 0, &ll);
	if (err)
		return err;
	atomic64_set(xeth_sysfs_counter(kobj, attr), ll);
	return len;
}

static ssize_t xeth_sysfs_attr_count_store(struct kobject *kobj,
					   struct attribute *kattr,
					   const char *buf, size_t len)
{
	struct xeth_sysfs_count_attr *attr = to_xeth_sysfs_count_attr(kattr);
	if (attr->store != xeth_sysfs_count_store)
		return -EIO;
	return attr->store(kobj, attr, buf, len);
}

static struct sysfs_ops xeth_sysfs_count_ops = {
	.show = xeth_sysfs_attr_count_show,
	.store = xeth_sysfs_attr_count_store,
};

static void xeth_sysfs_count_release(struct kobject *kobj)
{
	do {} while(0);
}

#define xeth_sysfs_new_count_attr(_name)				\
static struct xeth_sysfs_count_attr xeth_sysfs_count_##_name = {	\
	.attr = {							\
		.name = __stringify(_name),				\
		.mode = (S_IWUSR | S_IWGRP | S_IRUGO),			\
	},								\
	.show = xeth_sysfs_count_show,					\
	.store = xeth_sysfs_count_store,				\
	.index = xeth_sysfs_count(_name),				\
}

xeth_sysfs_new_count_attr(rx_invalid);
xeth_sysfs_new_count_attr(rx_no_dev);
xeth_sysfs_new_count_attr(sb_connections);
xeth_sysfs_new_count_attr(sb_invalid);
xeth_sysfs_new_count_attr(sb_no_dev);

static struct attribute *xeth_sysfs_default_attrs[] = {
	&xeth_sysfs_count_rx_invalid.attr,
	&xeth_sysfs_count_rx_no_dev.attr,
	&xeth_sysfs_count_sb_connections.attr,
	&xeth_sysfs_count_sb_invalid.attr,
	&xeth_sysfs_count_sb_no_dev.attr,
};

static struct kobj_type xeth_sysfs_ktype = {
	.sysfs_ops = &xeth_sysfs_count_ops,
	.release = xeth_sysfs_count_release,
	.default_attrs = xeth_sysfs_default_attrs,
};

int xeth_sysfs_init(void)
{
	int err = kobject_init_and_add(&xeth.kobj,
				       &xeth_sysfs_ktype,
				       &xeth.kset->kobj,
				       "%s", "xeth");
	if (!err)
		kobject_uevent(&xeth.kobj, KOBJ_ADD);
	return err;
}

void xeth_sysfs_exit(void)
{
	kobject_put(&xeth.kobj);
}

#define xeth_sysfs_new_count_priv_attr(_name)				\
static struct xeth_sysfs_count_attr xeth_sysfs_count_priv_##_name = {	\
	.attr = {							\
		.name = __stringify(_name),				\
		.mode = (S_IWUSR | S_IWGRP | S_IRUGO),			\
	},								\
	.show = xeth_sysfs_count_show,					\
	.store = xeth_sysfs_count_store,				\
	.index = xeth_sysfs_count_priv(_name),				\
}

xeth_sysfs_new_count_priv_attr(rx_packets);
xeth_sysfs_new_count_priv_attr(rx_bytes);
xeth_sysfs_new_count_priv_attr(rx_nd_mismatch);
xeth_sysfs_new_count_priv_attr(rx_dropped);
xeth_sysfs_new_count_priv_attr(sb_carrier);
xeth_sysfs_new_count_priv_attr(sb_ethtool_stats);
xeth_sysfs_new_count_priv_attr(sb_link_stats);
xeth_sysfs_new_count_priv_attr(sb_packets);
xeth_sysfs_new_count_priv_attr(sb_bytes);
xeth_sysfs_new_count_priv_attr(sb_nomem);
xeth_sysfs_new_count_priv_attr(sb_dropped);
xeth_sysfs_new_count_priv_attr(tx_packets);
xeth_sysfs_new_count_priv_attr(tx_bytes);
xeth_sysfs_new_count_priv_attr(tx_nomem);
xeth_sysfs_new_count_priv_attr(tx_noway);
xeth_sysfs_new_count_priv_attr(tx_no_iflink);
xeth_sysfs_new_count_priv_attr(tx_dropped);

static struct attribute *xeth_sysfs_default_priv_attrs[] = {
	&xeth_sysfs_count_priv_rx_packets.attr,
	&xeth_sysfs_count_priv_rx_bytes.attr,
	&xeth_sysfs_count_priv_rx_nd_mismatch.attr,
	&xeth_sysfs_count_priv_rx_dropped.attr,
	&xeth_sysfs_count_priv_sb_carrier.attr,
	&xeth_sysfs_count_priv_sb_ethtool_stats.attr,
	&xeth_sysfs_count_priv_sb_link_stats.attr,
	&xeth_sysfs_count_priv_sb_packets.attr,
	&xeth_sysfs_count_priv_sb_bytes.attr,
	&xeth_sysfs_count_priv_sb_nomem.attr,
	&xeth_sysfs_count_priv_sb_dropped.attr,
	&xeth_sysfs_count_priv_tx_packets.attr,
	&xeth_sysfs_count_priv_tx_bytes.attr,
	&xeth_sysfs_count_priv_tx_nomem.attr,
	&xeth_sysfs_count_priv_tx_noway.attr,
	&xeth_sysfs_count_priv_tx_no_iflink.attr,
	&xeth_sysfs_count_priv_tx_dropped.attr,
	NULL,
};

static struct kobj_type xeth_sysfs_priv_ktype = {
	.sysfs_ops = &xeth_sysfs_count_ops,
	.release = xeth_sysfs_count_release,
	.default_attrs = xeth_sysfs_default_priv_attrs,
};

int xeth_sysfs_priv_add(struct xeth_priv *priv)
{
	int err = kobject_init_and_add(&priv->kobj,
				       &xeth_sysfs_priv_ktype,
				       &priv->nd->dev.kobj,
				       "%s", "xeth");
	if (!err)
		kobject_uevent(&priv->kobj, KOBJ_ADD);
	return err;
}

void xeth_sysfs_priv_del(struct xeth_priv *priv)
{
	kobject_put(&priv->kobj);
}
