/* XETH devfs file.
 *
 * This creates /dev/net/DRIVER through which an XETH control daemon (switchd)
 * may forward exception frames (e.g. ARP, TTL == 1) to the respective netdev
 * demuxed from the vlan or mpls header.
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

#include <linux/miscdevice.h>

static int xeth_devfs_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int xeth_devfs_close(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static ssize_t xeth_devfs_read(struct file *filp, char __user *buf,
			       size_t len, loff_t *ppos)
{
	return 0;
}

static ssize_t xeth_devfs_write(struct file *filp, const char __user *buf,
				size_t len, loff_t *ppos)
{
	struct sk_buff *skb = netdev_alloc_skb(xeth.iflinks[0], len);
	if (!skb)
		return -ENOMEM;
	skb_put(skb, len);
	if (xeth_pr_true_val("%lu", copy_from_user(skb->data, buf, len))) {
		kfree_skb(skb);
		return -ENOSPC;
	}
	return xeth_pr_val("%zd", xeth.ops.side_band_rx(skb));
}

static const struct file_operations xeth_devfs_fops = {
	.owner	 = THIS_MODULE,
	.open	 = xeth_devfs_open,
	.release = xeth_devfs_close,
	.read    = xeth_devfs_read,
	.write	 = xeth_devfs_write,
	.llseek	 = noop_llseek,
};

static char xeth_devfs_nodename[64];

static struct miscdevice xeth_devfs_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.nodename = xeth_devfs_nodename,
	.fops = &xeth_devfs_fops,
};

static int xeth_devfs_register_err;

void xeth_devfs_init(const char *name)
{
	xeth_devfs_miscdev.name = name;
	sprintf(xeth_devfs_nodename, "net/%s", name);
	xeth_devfs_register_err =
		xeth_pr_val("%d", misc_register(&xeth_devfs_miscdev));
}

void xeth_devfs_exit(void)
{
	if (!xeth_devfs_register_err)
		misc_deregister(&xeth_devfs_miscdev);
}
