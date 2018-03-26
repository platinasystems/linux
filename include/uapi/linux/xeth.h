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

#define XETH_SBNOOP			0
#define XETH_SBOP_SET_NET_STAT		1
#define XETH_SBOP_SET_ETHTOOL_STAT	2

struct xeth_sb_hdr {
	u64 z64;
	u32 z32;
	u16 z16;
	u8  z8;
	u8  op;
};

struct xeth_sb_set_stat {
	u64 ifindex;
	u64 statindex;
	u64 count;
};

#endif /* __XETH_UAPI_H */
