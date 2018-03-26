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
	"fmt"
	"sync"
	"syscall"
)

const SizeofSbHdrSetStat = SizeofSbHdr + SizeofSbSetStat

var SbSetStatPool = sync.Pool{
	New: func() interface{} {
		return make([]byte, SizeofSbHdrSetStat)
	},
}

var PageSize = syscall.Getpagesize()

var PagePool = sync.Pool{
	New: func() interface{} {
		return make([]byte, PageSize)
	},
}

var JumboPool = sync.Pool{
	New: func() interface{} {
		return make([]byte, SizeofJumboFrame)
	},
}

func PoolGet(n int) []byte {
	var buf []byte
	if n < PageSize {
		buf = PagePool.Get().([]byte)
	} else {
		buf = JumboPool.Get().([]byte)
	}
	buf = buf[:n]
	return buf
}

func PoolPut(buf []byte) {
	switch cap(buf) {
	case SizeofSbHdrSetStat:
		SbSetStatPool.Put(buf)
	case PageSize:
		buf = buf[:cap(buf)]
		PagePool.Put(buf)
	case SizeofJumboFrame:
		buf = buf[:cap(buf)]
		JumboPool.Put(buf)
	default:
		panic(fmt.Errorf("unexpected buf cap: %d", cap(buf)))
	}
}
