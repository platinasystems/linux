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

import "net"

type InterfaceEntry struct {
	// e.g., "xeth1", "xeth2-1", "xeth2-2"
	Name string
	// positive integer that starts at one,
	// zero is never used
	Index  int
	Iflink int
	MTU    int
	// e.g., FlagUp, FlagLoopback, FlagMulticast
	Flags net.Flags
	// IEEE MAC-48, EUI-48 and EUI-64 form
	HardwareAddr net.HardwareAddr
	// 1 is default; otherwise, inode of of netns w/in /run
	Netns Netns

	Id           uint16
	PortIndex    int16
	SubportIndex int8
	DevType      DevType

	IPNets []*net.IPNet
}

type Ifcache struct {
	index map[int32]*InterfaceEntry
	name  map[string]*InterfaceEntry
}

type Ifindex int32

func (c *Ifcache) Indexed(ifindex int32) *InterfaceEntry {
	if entry, found := c.index[ifindex]; found {
		return entry
	}
	if p, err := net.InterfaceByIndex(int(ifindex)); err == nil {
		return c.clone(p)
	}
	return c.clone(Ifindex(ifindex))
}

func (c *Ifcache) Named(name string) *InterfaceEntry {
	if entry, found := c.name[name]; found {
		return entry
	}
	if p, err := net.InterfaceByName(name); err == nil {
		return c.clone(p)
	}
	return c.clone(name)
}

func (c *Ifcache) clone(args ...interface{}) *InterfaceEntry {
	entry := &InterfaceEntry{
		Name:         "unknown",
		Index:        0,
		Iflink:       0,
		MTU:          1500,
		HardwareAddr: make(net.HardwareAddr, ETH_ALEN),
		Netns:        DefaultNetns,
		DevType:      XETH_DEVTYPE_LINUX_UNKNOWN,
		Id:           0,
		PortIndex:    -1,
		SubportIndex: -1,
	}
	for _, v := range args {
		switch t := v.(type) {
		case *MsgIfinfo:
			entry.Index = int(t.Ifindex)
			entry.Name = (*Ifname)(&t.Ifname).String()
			copy(entry.HardwareAddr, t.HardwareAddr())
			entry.Flags = net.Flags(t.Flags)
			entry.Netns = Netns(t.Net)
			entry.Iflink = int(t.Iflinkindex)
			entry.Id = t.Id
			entry.PortIndex = t.Portindex
			entry.SubportIndex = t.Subportindex
			entry.DevType = DevType(t.Devtype)
		case *net.Interface:
			entry.Index = t.Index
			entry.MTU = t.MTU
			entry.Name = t.Name
			copy(entry.HardwareAddr, t.HardwareAddr)
			entry.Flags = t.Flags
		case Ifindex:
			entry.Index = int(t)
		case string:
			entry.Name = t
		case net.HardwareAddr:
			copy(entry.HardwareAddr, t)
		case net.Flags:
			entry.Flags = t
		case Netns:
			entry.Netns = t
		}
	}
	c.index[int32(entry.Index)] = entry
	c.name[entry.Name] = entry
	return entry
}

func (c *Ifcache) del(ifindex int32) {
	entry := c.Indexed(ifindex)
	if entry != nil {
		delete(c.index, ifindex)
		delete(c.name, entry.Name)
		entry.HardwareAddr = entry.HardwareAddr[:0]
	}
}

func (c *Ifcache) unreg(ifindex int32) {
	entry := Interface.Indexed(ifindex)
	if entry != nil {
		entry.Netns = DefaultNetns
	}
}
