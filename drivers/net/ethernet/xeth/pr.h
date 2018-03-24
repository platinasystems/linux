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

#define xeth_pr(fmt, ...)	pr_debug(fmt "\n", ##__VA_ARGS__)
#define xeth_pr_is_err_val(ptr)						\
	({								\
		int err = 0;						\
	 	if (!ptr) {						\
	 		err = -ENOMEM;					\
	 		xeth_pr("%s is NULL", #ptr);			\
		} else if (IS_ERR(ptr)) {				\
			err = PTR_ERR(ptr);				\
			xeth_pr("%s error: %d", #ptr, err);		\
			(ptr) = NULL;					\
		}							\
		(err);							\
	})
#define xeth_pr_void(expr)						\
	do {								\
		(expr);							\
		xeth_debug_pr("%s: OK", #expr);				\
	} while(0)
#define xeth_pr_val(fmt, val, ...)					\
	({								\
		typeof(val) _val = (val);				\
		xeth_pr("%s: " fmt, #val, _val, ##__VA_ARGS__);		\
		(_val);							\
	})
#define xeth_pr_true_val(fmt, val, ...)					\
	({								\
		typeof(val) _val = (val);				\
	 	if (!!(_val))						\
			xeth_pr("%s: " fmt, #val, _val,	##__VA_ARGS__);	\
		(_val);							\
	})
#define xeth_pr_false_val(fmt, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
	 	if (!(_val))						\
			xeth_pr("%s: " fmt, #val, _val, ##__VA_ARGS__);	\
		(_val);							\
	})
#define xeth_pr_nd(nd, fmt, ...)					\
	netdev_dbg((nd), fmt "\n", ##__VA_ARGS__)
#define xeth_pr_nd_void(nd, expr)					\
	do {								\
		(expr);							\
		xeth_pr_nd((nd), "%s: OK", #expr);			\
	} while(0)
#define xeth_pr_nd_val(nd, fmt, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
		xeth_pr_nd((nd), "%s: " fmt, #val, _val, ##__VA_ARGS__);\
		(_val);							\
	})
#define xeth_pr_nd_true_val(nd, fmt, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
	 	if (!!(_val))						\
			xeth_pr_nd((nd), "%s: " fmt, #val, _val,	\
				   ##__VA_ARGS__);			\
		(_val);							\
	})
#define xeth_pr_nd_false_val(nd, fmt, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
	 	if (!(_val))						\
			xeth_pr_nd((nd), "%s: " fmt, #val, _val,	\
				   ##__VA_ARGS__);			\
		(_val);							\
	})

static inline void _xeth_pr_skb_hex_dump(const char *func, struct sk_buff *skb)
{
#if defined(CONFIG_DYNAMIC_DEBUG)
	char dev_prefix_str[64];

	snprintf(dev_prefix_str, sizeof(dev_prefix_str), "%s:%s:%s: ",
		 xeth.ops.rtnl.kind, func, netdev_name(skb->dev));
	print_hex_dump_bytes(dev_prefix_str, DUMP_PREFIX_NONE,
			     skb->data,	skb->len);
#endif
}

#define xeth_pr_skb_hex_dump(skb) _xeth_pr_skb_hex_dump(__func__, skb)

static inline void _xeth_pr_buf_hex_dump(const char *func, char *buf, int len)
{
#if defined(CONFIG_DYNAMIC_DEBUG)
	char prefix_str[64];

	snprintf(prefix_str, sizeof(prefix_str), "%s:%s: ",
		 xeth.ops.rtnl.kind, func);
	print_hex_dump_bytes(prefix_str, DUMP_PREFIX_NONE, buf, len);
#endif
}

#define xeth_pr_buf_hex_dump(buf,len) _xeth_pr_buf_hex_dump(__func__, buf, len)

#endif /* __XETH_PR_H */
