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

package xeth

import (
	"bytes"
	"fmt"
	"net"
)

const (
	XETH_IFINFO_REASON_NEW = iota
	XETH_IFINFO_REASON_DEL
	XETH_IFINFO_REASON_UP
	XETH_IFINFO_REASON_DOWN
	XETH_IFINFO_REASON_DUMP
	XETH_IFINFO_REASON_REG
	XETH_IFINFO_REASON_UNREG
	XETH_IFINFO_REASON_VLAN_ADD
	XETH_IFINFO_REASON_VLAN_DEL
	XETH_IFINFO_REASON_VLAN_DUMP
)

type IfinfoReason uint8

func (reason IfinfoReason) String() string {
	var reasons = []string{
		"new",
		"del",
		"up",
		"down",
		"dump",
		"reg",
		"unreg",
		"vlan-add",
		"vlan-del",
		"vlan-dump",
	}
	i := int(reason)
	if i < len(reasons) {
		return reasons[i]
	}
	return fmt.Sprint("@", i)
}

func (info *MsgIfinfo) HardwareAddr() net.HardwareAddr {
	return net.HardwareAddr(info.Addr[:])
}

func (info *MsgIfinfo) String() string {
	buf := new(bytes.Buffer)
	fmt.Fprint(buf, Kind(info.Kind))
	fmt.Fprint(buf, " ", IfinfoReason(info.Reason))
	fmt.Fprint(buf, " ", DevType(info.Devtype))
	fmt.Fprint(buf, " ", (*Ifname)(&info.Ifname))
	if info.Iflinkindex != 0 {
		fmt.Fprint(buf, "@", Interface.Indexed(info.Iflinkindex).Name)
	}
	fmt.Fprint(buf, " ifindex ", info.Ifindex)
	if info.Flags != 0 {
		fmt.Fprint(buf, " <", Iff(info.Flags), ">")
	}
	fmt.Fprint(buf, " ", info.HardwareAddr())
	if info.Id != 0 {
		fmt.Fprint(buf, " id ", info.Id)
	}
	if info.Portindex >= 0 {
		fmt.Fprint(buf, " port ", info.Portindex)
	}
	if info.Subportindex >= 0 {
		fmt.Fprint(buf, " subport ", info.Subportindex)
	}
	if info.Net > 1 {
		fmt.Fprint(buf, " netns ", Netns(info.Net))
	}
	fmt.Fprint(buf, "\n")
	return buf.String()
}

func (info *MsgIfinfo) cache() {
	switch info.Reason {
	case XETH_IFINFO_REASON_NEW:
		Interface.clone(info)
	case XETH_IFINFO_REASON_DEL:
		Interface.del(info.Ifindex)
	case XETH_IFINFO_REASON_UP:
		if p := Interface.Indexed(info.Ifindex); p != nil {
			p.Flags = net.Flags(info.Flags)
		} else {
			Interface.clone(info)
		}
	case XETH_IFINFO_REASON_DOWN:
		if p := Interface.Indexed(info.Ifindex); p != nil {
			p.Flags = net.Flags(info.Flags)
		} else {
			Interface.clone(info)
		}
	case XETH_IFINFO_REASON_DUMP:
		if p := Interface.Indexed(info.Ifindex); p != nil {
			// FIXME Should we Interface.Update(p, info) ?
		} else {
			Interface.clone(info)
		}
	case XETH_IFINFO_REASON_REG:
		if p := Interface.Indexed(info.Ifindex); p != nil {
			p.Netns = Netns(info.Net)
		} else {
			Interface.clone(info)
		}
	case XETH_IFINFO_REASON_UNREG:
		Interface.unreg(info.Ifindex)
	}
}
