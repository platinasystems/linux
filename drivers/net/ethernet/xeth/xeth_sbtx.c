/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

static struct {
	struct spinlock mutex;
	struct list_head head;
} xeth_sbtx;

struct xeth_sbtx_entry {
	size_t data_len;
	struct list_head list;
	unsigned char data[];
};

static void xeth_sbtx_lock(void)
{
	spin_lock(&xeth_sbtx.mutex);
}

static void xeth_sbtx_unlock(void)
{
	spin_unlock(&xeth_sbtx.mutex);
}

static struct xeth_sbtx_entry *xeth_sbtx_alloc(size_t len)
{
	size_t n = sizeof(struct xeth_sbtx_entry) + len;
	struct xeth_sbtx_entry *entry = kzalloc(n, GFP_KERNEL);
	if (entry)
		entry->data_len = len;
	else
		xeth_counter_inc(sbtx_no_mem);
	return entry;
}

static void xeth_sbtx_msg_set(void *data, enum xeth_msg_kind kind)
{
	struct xeth_msg *msg = data;
	msg->header.z64 = 0;
	msg->header.z32 = 0;
	msg->header.z16 = 0;
	msg->header.version = XETH_MSG_VERSION;
	msg->header.kind = kind;
}

static inline u64 xeth_sbtx_ns_inum(struct net_device *nd)
{
	struct net *ndnet = dev_net(nd);
	return net_eq(ndnet, &init_net) ? 1 : ndnet->ns.inum;
}

static struct xeth_sbtx_entry *xeth_sbtx_pop(void)
{
	struct xeth_sbtx_entry *entry = NULL;

	xeth_sbtx_lock();
	if (!list_empty(&xeth_sbtx.head)) {
		entry = list_first_entry(&xeth_sbtx.head,
					 struct xeth_sbtx_entry,
					 list);
		list_del(&entry->list);
		xeth_counter_dec(sbtx_queued);
	}
	xeth_sbtx_unlock();
	return entry;
}

static void xeth_sbtx_push(struct xeth_sbtx_entry *entry)
{
	xeth_sbtx_lock();
	list_add(&entry->list, &xeth_sbtx.head);
	xeth_sbtx_unlock();
	xeth_counter_inc(sbtx_queued);
}

static void xeth_sbtx_queue(struct xeth_sbtx_entry *entry)
{
	if (xeth_flag(sb_connected) == 0) {
		kfree(entry);
		return;
	}
	xeth_sbtx_lock();
	list_add_tail(&entry->list, &xeth_sbtx.head);
	xeth_sbtx_unlock();
	xeth_counter_inc(sbtx_queued);
}

void xeth_sbtx_flush(void)
{
	struct xeth_sbtx_entry *entry;

	for (entry = xeth_sbtx_pop(); entry; entry = xeth_sbtx_pop())
		kfree(entry);
	xeth_debug_err(xeth_counter(sbtx_queued) > 0);
}

int xeth_sbtx_service(struct socket *conn)
{
	const unsigned int maxms = 320;
	const unsigned int minms = 10;
	unsigned int ms = minms;
	int err = 0;

	spin_lock_init(&xeth_sbtx.mutex);
	INIT_LIST_HEAD(&xeth_sbtx.head);

	while (!err &&
	       xeth_flag(sbrx_task) &&
	       !kthread_should_stop() &&
	       !signal_pending(current)) {
		struct xeth_sbtx_entry *entry;

		xeth_counter_inc(sbtx_ticks);
		entry = xeth_sbtx_pop();
		if (!entry) {
			msleep(ms);
			if (ms < maxms)
				ms *= 2;
		} else {
			int n = 0;
			struct kvec iov = {
				.iov_base = entry->data,
				.iov_len  = entry->data_len,
			};
			struct msghdr msg = {
				.msg_flags = MSG_DONTWAIT,
			};

			ms = minms;
			n = kernel_sendmsg(conn, &msg, &iov, 1, iov.iov_len);
			if (n == -EAGAIN) {
				xeth_counter_inc(sbtx_retries);
				xeth_sbtx_push(entry);
				msleep(ms);
			} else {
				kfree(entry);
				if (n > 0)
					xeth_counter_inc(sbtx_msgs);
				else if (n < 0)
					err = n;
				else	/* EOF */
					err = 1;
			}
		}
	}

	xeth_sbtx_flush();
	return err;
}

int xeth_sbtx_break(void)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_break *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_BREAK);
	xeth_sbtx_queue(entry);
	return 0;
}

int xeth_sbtx_change_upper(u32 upper_xid, u32 lower_xid, bool linking)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_change_upper_xid *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_CHANGE_UPPER_XID);
	msg = (typeof(msg))&entry->data[0];
	msg->upper = upper_xid;
	msg->lower = lower_xid;
	msg->linking = linking ? 1 : 0;
	xeth_sbtx_queue(entry);
	return 0;
}

int xeth_sbtx_ethtool_flags(u32 xid, u32 flags)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_ethtool_flags *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_ETHTOOL_FLAGS);
	msg = (typeof(msg))&entry->data[0];
	msg->xid = xid;
	msg->flags = flags;
	xeth_sbtx_queue(entry);
	return 0;
}

static int xeth_sbtx_ethtool_link_modes(enum xeth_msg_kind kind, u32 xid,
					const volatile unsigned long *addr)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_ethtool_link_modes *msg;
	int bit;
	const unsigned bits = min(__ETHTOOL_LINK_MODE_MASK_NBITS, 64);

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], kind);
	msg = (typeof(msg))&entry->data[0];
	msg->xid = xid;
	for (bit = 0; bit < bits; bit++)
		if (test_bit(bit, addr))
			msg->modes |= 1ULL<<bit;
	xeth_sbtx_queue(entry);
	return 0;
}

/* Note, this sends speed 0 if autoneg, regardless of base.speed This is to
 * cover controller (e.g. vnet) restart where in it's earlier run it has sent
 * SPEED to note the auto-negotiated speed to ethtool user, but in subsequent
 * run, we don't want the controller to override autoneg.
 */
int xeth_sbtx_ethtool_settings(u32 xid, struct ethtool_link_ksettings *ks)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_ethtool_settings *msg;
	const enum xeth_msg_kind kadv =
		XETH_MSG_KIND_ETHTOOL_LINK_MODES_ADVERTISING;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_ETHTOOL_SETTINGS);
	msg = (typeof(msg))&entry->data[0];
	msg->xid = xid;
	msg->speed = ks->base.autoneg ?  0 : ks->base.speed;
	msg->duplex = ks->base.duplex;
	msg->port = ks->base.port;
	msg->phy_address = ks->base.phy_address;
	msg->autoneg = ks->base.autoneg;
	msg->mdio_support = ks->base.mdio_support;
	msg->eth_tp_mdix = ks->base.eth_tp_mdix;
	msg->eth_tp_mdix_ctrl = ks->base.eth_tp_mdix_ctrl;
	xeth_sbtx_queue(entry);
	return xeth_sbtx_ethtool_link_modes(kadv, xid,
					    ks->link_modes.advertising);
}

static const char * const xeth_sbtx_fib_event_names[] = {
	[FIB_EVENT_ENTRY_REPLACE] "replace",
	[FIB_EVENT_ENTRY_APPEND] "append",
	[FIB_EVENT_ENTRY_ADD] "add",
	[FIB_EVENT_ENTRY_DEL] "del",
};

int xeth_sbtx_fib_entry(unsigned long event,
			struct fib_entry_notifier_info *feni)
{
	int i, nhs = 0;
	struct xeth_sbtx_entry *entry;
	struct xeth_next_hop *nh;
	struct xeth_msg_fibentry *msg;
	size_t n = sizeof(*msg);

	if (feni->fi->fib_nhs > 0) {
		nhs = feni->fi->fib_nhs;
		n += (nhs * sizeof(struct xeth_next_hop));
	}
	entry = xeth_sbtx_alloc(n);
	if (!entry)
		return -ENOMEM;
	msg = (typeof(msg))&entry->data[0];
	nh = (typeof(nh))&msg->nh[0];
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_FIBENTRY);
	msg->net = net_eq(feni->info.net, &init_net) ? 1 :
		feni->info.net->ns.inum;
	msg->address = htonl(feni->dst);
	msg->mask = inet_make_mask(feni->dst_len);
	msg->event = (u8)event;
	msg->nhs = nhs;
	msg->tos = feni->tos;
	msg->type = feni->type;
	msg->table = feni->tb_id;
	for(i = 0; i < msg->nhs; i++) {
		nh[i].ifindex = feni->fi->fib_nh[i].nh_dev ?
			feni->fi->fib_nh[i].nh_dev->ifindex : 0;
		nh[i].weight = feni->fi->fib_nh[i].nh_weight;
		nh[i].flags = feni->fi->fib_nh[i].nh_flags;
		nh[i].gw = feni->fi->fib_nh[i].nh_gw;
		nh[i].scope = feni->fi->fib_nh[i].nh_scope;
	}
	no_xeth_debug("%s %pI4/%d w/ %d nexhop(s)",
		      xeth_sbtx_fib_event_names[event],
		      &msg->address, feni->dst_len, nhs);
	xeth_sbtx_queue(entry);
	return 0;
}

int xeth_sbtx_fib6_entry(unsigned long event,
			 struct fib6_entry_notifier_info *feni)
{
	struct fib6_info *rt = xeth_debug_ptr_err(feni->rt);
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_fib6entry *msg;
	struct xeth_next_hop6 *sibling;
	size_t n = sizeof(*msg);
	struct fib6_info *iter;
	int i;

	if (IS_ERR(rt))
		return PTR_ERR(rt);
	if (rt->fib6_nsiblings)
		n += (rt->fib6_nsiblings * sizeof(struct xeth_next_hop6));
	entry = xeth_sbtx_alloc(n);
	if (!entry)
		return -ENOMEM;
	msg = (typeof(msg))&entry->data[0];
	sibling = (typeof(sibling))&msg->siblings[0];
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_FIB6ENTRY);
	msg->net = net_eq(feni->info.net, &init_net) ? 1 :
		feni->info.net->ns.inum;
	memcpy(msg->address, &rt->fib6_dst.addr, 16);
	msg->length = rt->fib6_dst.plen;
	msg->event = (u8)event;
	msg->nsiblings = rt->fib6_nsiblings;
	msg->type = rt->fib6_type;
	msg->table = rt->fib6_table->tb6_id;
	msg->nh.ifindex = rt->fib6_nh.nh_dev->ifindex;
	msg->nh.weight = rt->fib6_nh.nh_weight;
	msg->nh.flags = rt->fib6_nh.nh_flags;
	memcpy(msg->nh.gw, &rt->fib6_nh.nh_gw, 16);
	i = 0;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (i == rt->fib6_nsiblings)
			break;
		sibling->ifindex = iter->fib6_nh.nh_dev->ifindex;
		sibling->weight = iter->fib6_nh.nh_weight;
		sibling->flags = iter->fib6_nh.nh_flags;
		memcpy(sibling->gw, &iter->fib6_nh.nh_gw, 16);
		i++;
		sibling++;
	}
	xeth_debug("fib6 %s %pI6c/%d w/ %d nexhop(s)",
		      xeth_sbtx_fib_event_names[event],
		      &rt->fib6_dst.addr, rt->fib6_dst.plen,
		      1+i);
	xeth_sbtx_queue(entry);
	return 0;
}

int xeth_sbtx_ifa(struct in_ifaddr *ifa, u32 xid, unsigned long event)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_ifa *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_IFA);
	msg = (typeof(msg))&entry->data[0];
	msg->xid = xid;
	msg->event = event;
	msg->address = ifa->ifa_address;
	msg->mask = ifa->ifa_mask;
	xeth_sbtx_queue(entry);
	return 0;
}

int xeth_sbtx_ifa6(struct inet6_ifaddr *ifa6, u32 xid, unsigned long event)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_ifa6 *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_IFA6);
	msg = (typeof(msg))&entry->data[0];
	msg->xid = xid;
	msg->event = event;
	memcpy(msg->address, &ifa6->addr, 16);
	msg->length = ifa6->prefix_len;
	xeth_sbtx_queue(entry);
	return 0;
}


int xeth_sbtx_ifinfo(struct net_device *nd, u32 xid, enum xeth_dev_kind kind,
		     unsigned iff, u8 reason)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_ifinfo *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_IFINFO);
	msg = (typeof(msg))&entry->data[0];
	strlcpy(msg->ifname, nd->name, IFNAMSIZ);
	msg->net = xeth_sbtx_ns_inum(nd);
	msg->ifindex = nd->ifindex;
	msg->xid = xid;
	msg->flags = iff ? iff : nd->flags;
	memcpy(msg->addr, nd->dev_addr, ETH_ALEN);
	msg->kind = kind;
	msg->reason = reason;
	xeth_sbtx_queue(entry);
	return 0;
}

int xeth_sbtx_neigh_update(struct neighbour *neigh)
{
	struct xeth_sbtx_entry *entry;
	struct xeth_msg_neigh_update *msg;

	entry = xeth_sbtx_alloc(sizeof(*msg));
	if (!entry)
		return -ENOMEM;
	xeth_sbtx_msg_set(&entry->data[0], XETH_MSG_KIND_NEIGH_UPDATE);
	msg = (typeof(msg))&entry->data[0];
	msg->ifindex = neigh->dev->ifindex;
	msg->net = xeth_sbtx_ns_inum(neigh->dev);
	msg->ifindex = neigh->dev->ifindex;
	msg->family = neigh->ops->family;
	msg->len = neigh->tbl->key_len;
	memcpy(msg->dst, neigh->primary_key, neigh->tbl->key_len);
	read_lock_bh(&neigh->lock);
	if ((neigh->nud_state & NUD_VALID) && !neigh->dead) {
		char ha[MAX_ADDR_LEN];
		neigh_ha_snapshot(ha, neigh, neigh->dev);
		if ((neigh->nud_state & NUD_VALID) && !neigh->dead)
			memcpy(&msg->lladdr[0], ha, ETH_ALEN);
	}
	read_unlock_bh(&neigh->lock);
	xeth_sbtx_queue(entry);
	return 0;
}
