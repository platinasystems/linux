/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#if !defined(pr_expr_err)

#if !defined(pr_prefix)
# if defined(KBUILD_MODNAME)
#  define pr_prefix KBUILD_MODNAME
# else /* !KBUILD_MODNAME */
#  define pr_prefix ""
#  define pr_prefix_colon ""
# endif /* KBUILD_MODNAME */
#endif /* pr_prefix */

#if !defined(pr_prefix_colon)
# define pr_prefix_colon ":"
#endif /* pr_prefix_colon */

#define pr_expr_err(expr)						\
({									\
	int _err = (expr);						\
	if (_err)							\
		pr_err("%s%s%s:%s: %d", pr_prefix, pr_prefix_colon,	\
		       __func__, #expr, _err);				\
	(_err);								\
})

#define pr_nd_expr_err(nd, expr)					\
({									\
	int _err = (expr);						\
	if (_err)							\
		pr_err("%s%s%s:%s:%s: %d", pr_prefix, pr_prefix_colon,	\
		       __func__, netdev_name(nd), #expr, _err);		\
	(_err);								\
})

#define pr_ptr_err(expr)						\
({									\
	void *_ptr = (expr);						\
	if (!_ptr) {							\
		pr_err("%s%s%s:%s: NULL", pr_prefix, pr_prefix_colon,	\
		       __func__, #expr);				\
		_ptr = ERR_PTR(-ENOMEM);				\
	} else if (IS_ERR(_ptr))					\
		pr_err("%s%s%s:%s: %ld", pr_prefix, pr_prefix_colon,	\
		       __func__, #expr, PTR_ERR(_ptr));			\
	(_ptr);								\
})

#define pr_nd_ptr_err(nd, expr)						\
({									\
	void *_ptr = (expr);						\
	if (!_ptr) {							\
		pr_err("%s%s%s:%s:%s: %s", pr_prefix, pr_prefix_colon,\
		       __func__, netdev_name(nd), #expr, "NULL");	\
		_ptr = ERR_PTR(-ENOMEM);				\
	} else if (IS_ERR(_ptr))					\
		pr_err("%s%s%s:%s:%s: %ld", pr_prefix, pr_prefix_colon,	\
		       __func__, netdev_name(nd), #expr, PTR_ERR(_ptr));\
	(_ptr);								\
})

#define no_pr_debug(format, args...)					\
do {									\
	no_printk(KERN_DEBUG pr_fmt(format), ##args);			\
} while(0)

#define no_netdev_dbg(nd, format, args...)				\
do {									\
	no_printk(KERN_DEBUG pr_fmt(format), ##args);			\
} while(0)

#define pr_skb(skb)							\
do {									\
	char _txt[64];							\
	snprintf(_txt, sizeof(_txt), "%s%s%s:%s: ",			\
		 pr_prefix, pr_prefix_colon, __func__,			\
		 netdev_name(skb->dev));				\
	print_hex_dump_bytes(_txt, DUMP_PREFIX_NONE,			\
			     skb->data,	skb->len);			\
} while(0)

#define no_pr_skb(skb)	do ; while(0)

#define pr_buf(buf, len)						\
do {									\
	char _txt[64];							\
	snprintf(_txt, sizeof(_txt), "%s%s%s: ",			\
		 pr_prefix, pr_prefix_colon, __func__);			\
	print_hex_dump_bytes(_txt, DUMP_PREFIX_NONE, buf, len);		\
} while(0)

#define no_pr_buf(buf, len)	do ; while(0)

static inline int xeth_pr_expr_err_test(void)
{
	return -EINVAL;
}

static inline void xeth_pr_test(void)
{
	struct net_device *lo;
	const char buf[] = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	size_t n = ARRAY_SIZE(buf);

	pr_debug("begin pr tests...");
	pr_expr_err(xeth_pr_expr_err_test());
	pr_ptr_err(NULL);
	pr_ptr_err(ERR_PTR(-ENOMEM));
	pr_buf(buf, n);
	lo = pr_ptr_err(dev_get_by_name(&init_net, "lo"));
	if (!IS_ERR(lo)) {
		struct sk_buff *skb;

		pr_nd_expr_err(lo, xeth_pr_expr_err_test());
		pr_nd_ptr_err(lo, NULL);
		pr_nd_ptr_err(lo, ERR_PTR(-ENOMEM));
		skb = pr_nd_ptr_err(lo, netdev_alloc_skb(lo, n));
		if (!IS_ERR(skb)) {
			skb_put(skb, n);
			memcpy(skb->data, buf, n);
			pr_skb(skb);
			kfree_skb(skb);
		}
		dev_put(lo);
	}
	pr_debug("...end pr tests");
}

#endif /* pr_expr_err */
