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

#define may_xeth_pr(qualifier, format, args...)				\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG)				\
		pr_debug(format "\n", ##args);				\
	else								\
		no_printk(KERN_DEBUG pr_fmt(format), ##args);		\
} while(0)

#define xeth_pr(format, args...)					\
	may_xeth_pr(true, format, ##args)

#define may_xeth_pr_nd(qualifier, nd, format, args...)			\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG)				\
		netdev_dbg(nd, format "\n", ##args);			\
} while(0)

#define xeth_pr_nd(nd, format, args...)					\
	may_xeth_pr_nd(true, nd, format, ##args)

#define may_xeth_pr_skb_hex_dump(qualifier, skb)			\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG) {			\
		char prefix[64];					\
		snprintf(prefix, sizeof(prefix), "%s:%s:%s: ",		\
			 xeth.ops.rtnl.kind, __func__,			\
			 netdev_name(skb->dev));			\
		print_hex_dump_bytes(prefix, DUMP_PREFIX_NONE,		\
				     skb->data,	skb->len);		\
	}								\
} while(0)

#define may_xeth_pr_buf_hex_dump(qualifier, buf, len)			\
do {									\
	if (qualifier && XETH_PR_DYNAMIC_DEBUG) {			\
		char prefix[64];					\
		snprintf(prefix, sizeof(prefix), "%s:%s: ",		\
			 xeth.ops.rtnl.kind, __func__);			\
		print_hex_dump_bytes(prefix, DUMP_PREFIX_NONE,		\
				     buf, len);				\
	}								\
} while(0)

#define may_xeth_pr_return(qualifier, format, args...)			\
do {									\
	may_xeth_pr(qualifier, format, ##args);				\
	return;								\
} while(0)

#define xeth_pr_is_err_val(ptr)						\
({									\
	int err = 0;							\
	if (!ptr) {							\
		err = -ENOMEM;						\
		xeth_pr("%s is NULL", #ptr);				\
	} else if (IS_ERR(ptr)) {					\
		err = PTR_ERR(ptr);					\
		xeth_pr("%s error: %d", #ptr, err);			\
		(ptr) = NULL;						\
	}								\
	(err);								\
})

#define xeth_pr_val(format, val, args...)				\
({									\
	typeof(val) _val = (val);					\
	xeth_pr("%s: " format, #val, _val, ##args);			\
	(_val);								\
})

#define xeth_pr_true_val(format, val, args...)				\
({									\
	typeof(val) _val = (val);					\
	if (!!(_val))							\
		xeth_pr("%s: " format, #val, _val, ##args);		\
	(_val);								\
})

#define xeth_pr_false_val(format, val, args...)				\
({									\
	typeof(val) _val = (val);					\
	if (!(_val))							\
		xeth_pr("%s: " format, #val, _val, ##args);		\
	(_val);								\
})

#define xeth_pr_nd_void(nd, expr)					\
do {									\
	(expr);								\
	xeth_pr_nd((nd), "%s: OK", #expr);				\
} while(0)

#define xeth_pr_nd_val(nd, format, val, args...)			\
({									\
	typeof(val) _val = (val);					\
	xeth_pr_nd((nd), "%s: " format, #val, _val, ##args);		\
	(_val);								\
})

#define xeth_pr_nd_true_val(nd, format, val, args...)				\
({									\
	typeof(val) _val = (val);					\
	if (!!(_val))							\
		xeth_pr_nd((nd), "%s: " format, #val, _val, ##args);	\
	(_val);								\
})

#define xeth_pr_nd_false_val(nd, format, val, args...)			\
({									\
	typeof(val) _val = (val);					\
	if (!(_val))							\
		xeth_pr_nd((nd), "%s: " format, #val, _val, ##args);	\
	(_val);								\
})

#endif /* __XETH_PR_H */
