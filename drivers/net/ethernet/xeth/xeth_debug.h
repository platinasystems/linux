/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_DEBUG_H
#define __NET_ETHERNET_XETH_DEBUG_H

#if defined(KBUILD_MODNAME)
# define xeth_debug_prefix KBUILD_MODNAME
#else /* !KBUILD_MODNAME */
# define xeth_debug_prefix "xeth"
#endif /* KBUILD_MODNAME */

#define xeth_debug(format, args...)					\
do {									\
	pr_debug(format "\n", ##args);					\
} while(0)

#define no_xeth_debug(format, args...)					\
do {									\
	no_printk(KERN_DEBUG pr_fmt(format), ##args);			\
} while(0)

#define xeth_debug_nd(nd, format, args...)				\
do {									\
	netdev_dbg(nd, format "\n", ##args);				\
} while(0)

#define no_xeth_debug_nd(nd, format, args...)				\
do {									\
	no_printk(KERN_DEBUG pr_fmt(format), ##args);			\
} while(0)

#define xeth_debug_err(expr)						\
({									\
	int _err = (expr);						\
	if (_err < 0)							\
		pr_err("%s:%s:%s: %d\n", xeth_debug_prefix,		\
		       __func__, #expr, _err);				\
	(_err);								\
})

#define xeth_debug_nd_err(nd, expr)					\
({									\
	int _err = (expr);						\
	if (_err < 0)							\
		pr_err("%s:%s:%s:%s: %d", xeth_debug_prefix,		\
		       __func__, netdev_name(nd), #expr, _err);		\
	(_err);								\
})

#define xeth_debug_ptr_err(expr)					\
({									\
	void *_ptr = (expr);						\
	if (!_ptr) {							\
		pr_err("%s:%s:%s: NULL", xeth_debug_prefix,		\
		       __func__, #expr);				\
		_ptr = ERR_PTR(-ENOMEM);				\
	} else if (IS_ERR(_ptr))					\
		pr_err("%s:%s:%s: %ld", xeth_debug_prefix,		\
		       __func__, #expr, PTR_ERR(_ptr));			\
	(_ptr);								\
})

#define xeth_debug_nd_ptr_err(nd, expr)					\
({									\
	void *_ptr = (expr);						\
	if (!_ptr) {							\
		pr_err("%s:%s:%s:%s: %s", xeth_debug_prefix,		\
		       __func__, netdev_name(nd), #expr, "NULL");	\
		_ptr = ERR_PTR(-ENOMEM);				\
	} else if (IS_ERR(_ptr))					\
		pr_err("%s:%s:%s:%s: %ld", xeth_debug_prefix,		\
		       __func__, netdev_name(nd), #expr, PTR_ERR(_ptr));\
	(_ptr);								\
})

#define xeth_debug_skb(skb)						\
do {									\
	char _txt[64];							\
	snprintf(_txt, sizeof(_txt), "%s:%s:%s: ",			\
		 xeth_debug_prefix, __func__, netdev_name(skb->dev));	\
	print_hex_dump_bytes(_txt, DUMP_PREFIX_NONE,			\
			     skb->data,	skb->len);			\
} while(0)

#define no_xeth_debug_skb(skb)	do ; while(0)

#define xeth_debug_buf(buf, len)					\
do {									\
	char _txt[64];							\
	snprintf(_txt, sizeof(_txt), "%s:%s: ",				\
		 xeth_debug_prefix, __func__);				\
	print_hex_dump_bytes(_txt, DUMP_PREFIX_NONE, buf, len);		\
} while(0)

#define no_xeth_debug_buf(buf, len)	do ; while(0)

#define xeth_debug_rcu(expr)						\
({									\
	typeof (expr) _v;						\
	bool held = rcu_read_lock_held();				\
	if (!held) {							\
		rcu_read_lock();					\
		pr_err("%s:%s:%s: unheld rcu", xeth_debug_prefix,	\
		       __func__, #expr);				\
	}								\
	_v = expr;							\
	if (!held)							\
		rcu_read_unlock();					\
	(_v);								\
})

#define xeth_debug_hold_rcu(expr)					\
({									\
	typeof (expr) _v;						\
	bool held = rcu_read_lock_held();				\
	if (!held)							\
		rcu_read_lock();					\
	else								\
		pr_err("%s:%s:%s: held rcu", xeth_debug_prefix,		\
		       __func__, #expr);				\
	_v = expr;							\
	if (!held)							\
		rcu_read_unlock();					\
	(_v);								\
})

static inline int xeth_debug_err_test(void)
{
	return -EINVAL;
}

static inline void xeth_debug_test(void)
{
	struct net_device *lo;
	const char buf[] = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	size_t n = ARRAY_SIZE(buf);

	xeth_debug("begin debug test...");
	xeth_debug_err(xeth_debug_err_test());
	xeth_debug_ptr_err(NULL);
	xeth_debug_ptr_err(ERR_PTR(-ENOMEM));
	xeth_debug_buf(buf, n);
	lo = xeth_debug_ptr_err(dev_get_by_name(&init_net, "lo"));
	if (!IS_ERR(lo)) {
		struct sk_buff *skb;
		xeth_debug_nd_err(lo, xeth_debug_err_test());
		xeth_debug_nd_ptr_err(lo, NULL);
		xeth_debug_nd_ptr_err(lo, ERR_PTR(-ENOMEM));
		skb = xeth_debug_nd_ptr_err(lo, netdev_alloc_skb(lo, n));
		if (!IS_ERR(skb)) {
			skb_put(skb, n);
			memcpy(skb->data, buf, n);
			xeth_debug_skb(skb);
			kfree_skb(skb);
		}
		dev_put(lo);
	}
	if (rcu_read_lock_held()) 
		xeth_debug_hold_rcu(xeth_debug_err_test());
	else
		xeth_debug_rcu(xeth_debug_err_test());
	xeth_debug("...end debug test");
}

#endif /* __NET_ETHERNET_XETH_DEBUG_H */
