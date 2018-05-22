/* XETH side-band channel protocol.
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

#ifndef __XETH_UAPI_H
#define __XETH_UAPI_H

#define XETH_SIZEOF_JUMBO_FRAME		9728

enum xeth_op {
	XETH_BREAK_OP,
	XETH_LINK_STAT_OP,
	XETH_ETHTOOL_STAT_OP,
	XETH_ETHTOOL_FLAGS_OP,
	XETH_ETHTOOL_SETTINGS_OP,
	XETH_DUMP_IFINFO_OP,
	XETH_CARRIER_OP,
	XETH_SPEED_OP,
	XETH_IFINDEX_OP,
};

enum xeth_carrier_flag {
	XETH_CARRIER_OFF,
	XETH_CARRIER_ON,
};

#define xeth_msg_name(name)	xeth_##name##_msg

#define xeth_msg(name, ...)						\
struct xeth_msg_name(name) {						\
	struct	xeth_msg_hdr	hdr;					\
	__VA_ARGS__;							\
}

#define xeth_ifmsg(name, ...)						\
struct xeth_msg_name(name) {						\
	struct	xeth_msg_hdr	hdr;					\
	u8	ifname[IFNAMSIZ];					\
	__VA_ARGS__;							\
}

struct xeth_msg_hdr {
	u64	z64;
	u32	z32;
	u16	z16;
	u8	z8;
	u8	op;
};

struct xeth_stat {
	u64 index;
	u64 count;
};

xeth_msg(break);
xeth_ifmsg(stat, struct xeth_stat stat);
xeth_ifmsg(ethtool_flags, u32 flags);
xeth_ifmsg(ethtool_settings, struct ethtool_link_ksettings settings);
xeth_msg(dump_ifinfo);
xeth_ifmsg(carrier, u8 flag);
xeth_ifmsg(speed, u32 mbps);
xeth_ifmsg(ifindex, u64 ifindex);

static inline bool xeth_is_hdr(struct xeth_msg_hdr *p)
{
	return	p->z64 == 0 && p->z32 == 0 && p->z16 == 0 && p->z8 == 0;
}

static inline void xeth_set_hdr(struct xeth_msg_hdr *hdr, u8 op)
{
	hdr->z64 = 0;
	hdr->z32 = 0;
	hdr->z16 = 0;
	hdr->z8 = 0;
	hdr->op = op;
}
#endif /* __XETH_UAPI_H */
