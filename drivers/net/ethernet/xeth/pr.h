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

#ifndef __XETH_PR_H
#define __XETH_PR_H

#if defined(CONFIG_DYNAMIC_DEBUG)
#define XETH_PR_DYNAMIC_DEBUG	true
#else
#define XETH_PR_DYNAMIC_DEBUG	false
#endif

#define __xeth_pr(qualifier, format, args...)				\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG)				\
		pr_debug(format "\n", ##args);				\
	else								\
		no_printk(KERN_DEBUG pr_fmt(format), ##args);		\
} while(0)

#define __xeth_pr_nd(qualifier, nd, format, args...)			\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG)				\
		netdev_dbg(nd, format "\n", ##args);			\
} while(0)

#define __xeth_pr_err(qualifier, expr)					\
({									\
	int _err = (expr);						\
	if (_err)							\
		__xeth_pr(qualifier, "%s == %d", #expr, _err);		\
	(_err);								\
})

#define __xeth_pr_nd_err(qualifier, nd, expr)				\
({									\
	int _err = (expr);						\
	if (_err)							\
		__xeth_pr_nd(qualifier, nd, "%s == %d", #expr, _err);	\
	(_err);								\
})

#define __xeth_pr_skb_hex_dump(qualifier, skb)				\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG) {			\
		char prefix[64];					\
		snprintf(prefix, sizeof(prefix), "%s:%s:%s: ",		\
			 xeth_link_ops.kind, __func__,			\
			 netdev_name(skb->dev));			\
		print_hex_dump_bytes(prefix, DUMP_PREFIX_NONE,		\
				     skb->data,	skb->len);		\
	}								\
} while(0)

#define __xeth_pr_buf_hex_dump(qualifier, buf, len)			\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG) {			\
		char prefix[64];					\
		snprintf(prefix, sizeof(prefix), "%s:%s: ",		\
			 xeth_link_ops.kind, __func__);			\
		print_hex_dump_bytes(prefix, DUMP_PREFIX_NONE,		\
				     buf, len);				\
	}								\
} while(0)

#define __xeth_pr_is_err_val(qualifier, ptr)				\
({									\
	int err = 0;							\
	if (!ptr) {							\
		err = -ENOMEM;						\
		xeth_pr("%s is NULL", #ptr);				\
	} else if (IS_ERR(ptr)) {					\
		err = PTR_ERR(ptr);					\
		__xeth_pr(qualifier, "%s error: %d", #ptr, err);	\
		(ptr) = NULL;						\
	}								\
	(err);								\
})

#define __xeth_pr_true_expr(qualifier, expr, format, args...)		\
({									\
	bool _res = (expr);						\
	if (_res)							\
		__xeth_pr(qualifier, "%s%s" format, #expr,		\
			  ", ", ##args);				\
	(_res);								\
})

#define __xeth_pr_nd_true_expr(qualifier, nd, expr, format, args...)	\
({									\
	bool _res = (expr);						\
 	if (_res)							\
		__xeth_pr_nd(qualifier, nd, "%s%s" format, #expr,	\
			     ", ", ##args);				\
	(_res);								\
})

#define __xeth_pr_count(qualifier, counter)				\
do {									\
	u64 n = xeth_count_##counter;					\
	if (n) __xeth_pr(qualifier, "%s: %llu", #counter, n);		\
} while(0)

#define __xeth_pr_count_priv(qualifier, priv,counter)			\
do {									\
	u64 n = xeth_count_priv_##counter;				\
	if (n)								\
		__xeth_pr_nd(qualifier, (priv)->nd, "%s: %llu",		\
			     #counter, n);				\
} while(0)

#define xeth_pr(format, args...)					\
	__xeth_pr(true, format, ##args)
#define no_xeth_pr(format, args...)					\
	__xeth_pr(false, format, ##args)
#define xeth_pr_nd(nd, format, args...)					\
	__xeth_pr_nd(true, nd, format, ##args)
#define no_xeth_pr_nd(nd, format, args...)				\
	__xeth_pr_nd(false, nd, format, ##args)
#define xeth_pr_err(expr)						\
	__xeth_pr_err(true, expr)
#define no_xeth_pr_err(expr)						\
	__xeth_pr_err(false, expr)
#define xeth_pr_nd_err(nd, expr)					\
	__xeth_pr_nd_err(true, nd, expr)
#define no_xeth_pr_nd_err(nd, expr)					\
	__xeth_pr_nd_err(false, nd, expr)
#define xeth_pr_skb_hex_dump(skb)					\
	__xeth_pr_skb_hex_dump(true, skb)
#define no_xeth_pr_skb_hex_dump(skb)					\
	__xeth_pr_skb_hex_dump(false, skb)
#define xeth_pr_buf_hex_dump(buf, len)					\
	__xeth_pr_buf_hex_dump(true, buf, len)
#define no_xeth_pr_buf_hex_dump(buf, len)				\
	__xeth_pr_buf_hex_dump(false, buf, len)
#define xeth_pr_is_err_val(ptr)						\
	__xeth_pr_is_err_val(true, ptr)
#define no_xeth_pr_is_err_val(ptr)					\
	__xeth_pr_is_err_val(false, ptr)
#define xeth_pr_true_expr(expr, format, args...)			\
	__xeth_pr_true_expr(true, expr, format, ##args)
#define no_xeth_pr_true_expr(expr, format, args...)			\
	__xeth_pr_true_expr(false, expr, format, ##args)
#define xeth_pr_nd_true_expr(nd, expr, format, args...)			\
	__xeth_pr_nd_true_expr(true, nd, expr, format, ##args)
#define no_xeth_pr_nd_true_expr(nd, expr, format, args...)		\
	__xeth_pr_nd_true_expr(false, nd, expr, format, ##args)
#define xeth_pr_count(counter)						\
	__xeth_pr_count(true, counter)
#define no_xeth_pr_count(counter)					\
	__xeth_pr_count(false, counter)
#define xeth_pr_count_priv(priv, counter)				\
	__xeth_pr_count_priv(true, priv, counter)
#define no_xeth_pr_count_priv(priv, counter)				\
	__xeth_pr_count_priv(false, priv, counter)

#endif /* __XETH_PR_H */
