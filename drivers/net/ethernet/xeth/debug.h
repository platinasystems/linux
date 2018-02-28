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

#ifndef __XETH_DEBUG_H
#define __XETH_DEBUG_H

#define xeth_debug_pr(format, ...)					\
	pr_debug(format "\n", ##__VA_ARGS__)
#define xeth_debug_ptr(ptr)						\
	xeth_debug_pr("%s: %p", #ptr, ptr)
#define xeth_debug_netdev_pr(nd, format, ...)				\
	netdev_dbg((nd), format "\n", ##__VA_ARGS__)
#define xeth_debug_void(expr)						\
	do {								\
		(expr);							\
		xeth_debug_pr("%s: OK", #expr);				\
	} while(0)
#define xeth_debug_val(format, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
		xeth_debug_pr("%s: " format, #val,			\
			      _val, ##__VA_ARGS__);			\
		(_val);							\
	})
#define xeth_debug_true_val(format, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
	 	if (!!(_val))						\
			xeth_debug_pr("%s: " format, #val,		\
				      _val, ##__VA_ARGS__);		\
		(_val);							\
	})
#define xeth_debug_false_val(format, val, ...)				\
	({								\
		typeof(val) _val = (val);				\
	 	if (!(_val))						\
			xeth_debug_pr("%s: " format, #val,		\
				      _val, ##__VA_ARGS__);		\
		(_val);							\
	})
#define xeth_debug_netdev_void(nd, expr)				\
	do {								\
		(expr);							\
		xeth_debug_netdev_pr((nd), "%s: OK", #expr);		\
	} while(0)
#define xeth_debug_netdev_val(nd, format, val, ...)			\
	({								\
		typeof(val) _val = (val);				\
		xeth_debug_netdev_pr((nd), "%s: " format, #val,		\
				     _val, ##__VA_ARGS__);		\
		(_val);							\
	})
#define xeth_debug_netdev_true_val(nd, format, val, ...)		\
	({								\
		typeof(val) _val = (val);				\
	 	if (!!(_val))						\
			xeth_debug_netdev_pr((nd), "%s: " format, #val,	\
					     _val, ##__VA_ARGS__);	\
		(_val);							\
	})
#define xeth_debug_netdev_false_val(nd, format, val, ...)		\
	({								\
		typeof(val) _val = (val);				\
	 	if (!(_val))						\
			xeth_debug_netdev_pr((nd), "%s: " format, #val,	\
					     _val, ##__VA_ARGS__);	\
		(_val);							\
	})

static inline void _xeth_debug_hex_dump(const char *func,
					struct sk_buff *skb)
{
#if defined(CONFIG_DYNAMIC_DEBUG)
	char dev_prefix_str[64];

	sprintf(dev_prefix_str, "%s:%s:%s: ", xeth.ops.rtnl.kind, func,
		netdev_name(skb->dev));
	print_hex_dump_bytes(dev_prefix_str, DUMP_PREFIX_NONE,
			     skb->data,	skb->len);
#endif
}

#define xeth_debug_hex_dump(nd)						\
	_xeth_debug_hex_dump(__func__, nd)

#endif /* __XETH_DEBUG_H */
