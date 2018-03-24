/* XETH driver sideband control.
 *
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
package xeth

import (
	"fmt"
	"net"
	"strings"
	"syscall"
	"unsafe"
)

const (
	indexofStatRxPackets uint64 = iota
	indexofStatTxPackets
	indexofStatRxBytes
	indexofStatTxBytes
	indexofStatRxErrors
	indexofStatTxErrors
	indexofStatRxDropped
	indexofStatTxDropped
	indexofStatMulticast
	indexofStatCollisions
	indexofStatRxLengthErrors
	indexofStatRxOverErrors
	indexofStatRxCrcErrors
	indexofStatRxFrameErrors
	indexofStatRxFifoErrors
	indexofStatRxMissedErrors
	indexofStatTxAbortedErrors
	indexofStatTxCarrierErrors
	indexofStatTxFifoErrors
	indexofStatTxHeartbeatErrors
	indexofStatTxWindowErrors
	indexofStatRxCompressed
	indexofStatTxCompressed
	indexofStatRxNohandler
)

var indexofStat = map[string]uint64{
	"rx-packets":          indexofStatRxPackets,
	"tx-packets":          indexofStatTxPackets,
	"rx-bytes":            indexofStatRxBytes,
	"tx-bytes":            indexofStatTxBytes,
	"rx-errors":           indexofStatRxErrors,
	"tx-errors":           indexofStatTxErrors,
	"rx-dropped":          indexofStatRxDropped,
	"tx-dropped":          indexofStatTxDropped,
	"multicast":           indexofStatMulticast,
	"collisions":          indexofStatCollisions,
	"rx-length-errors":    indexofStatRxLengthErrors,
	"rx-over-errors":      indexofStatRxOverErrors,
	"rx-crc-errors":       indexofStatRxCrcErrors,
	"rx-frame-errors":     indexofStatRxFrameErrors,
	"rx-fifo-errors":      indexofStatRxFifoErrors,
	"rx-missed-errors":    indexofStatRxMissedErrors,
	"tx-aborted-errors":   indexofStatTxAbortedErrors,
	"tx-carrier-errors":   indexofStatTxCarrierErrors,
	"tx-fifo-errors":      indexofStatTxFifoErrors,
	"tx-heartbeat-errors": indexofStatTxHeartbeatErrors,
	"tx-window-errors":    indexofStatTxWindowErrors,
	"rx-compressed":       indexofStatRxCompressed,
	"tx-compressed":       indexofStatTxCompressed,
	"rx-nohandler":        indexofStatRxNohandler,
}

type Xeth struct {
	*net.UnixConn
	driver      string
	indexofStat map[string]uint64
}

func MapIndexofStats(stats []string) map[string]uint64 {
	m := make(map[string]uint64)
	for i, s := range stats {
		m[s] = uint64(i)
	}
	return m
}

func New(driver string, indexofStat map[string]uint64) (*Xeth, error) {
	const netname = "unixpacket"
	sockname := fmt.Sprintf("@%s.xeth", driver)
	sockaddr, err := net.ResolveUnixAddr(netname, sockname)
	if err != nil {
		return nil, err
	}
	sockconn, err := net.DialUnix(netname, nil, sockaddr)
	if err != nil {
		return nil, err
	}
	return &Xeth{sockconn, driver, indexofStat}, nil
}

func (xeth *Xeth) Shutdown() error {
	const (
		SHUT_RD = iota
		SHUT_WR
		SHUT_RDWR
	)
	f, err := xeth.File()
	if err == nil {
		err = syscall.Shutdown(int(f.Fd()), SHUT_RDWR)
	}
	xeth.Close()
	xeth.indexofStat = nil
	return err
}

func (xeth *Xeth) Set(ifindex uint64, stat string, count uint64) error {
	var statindex uint64
	var found bool
	var op uint8
	oob := []byte{}
	modstat := strings.Replace(strings.Replace(stat, "_", "-", -1),
		".", "-", -1)
	if statindex, found = indexofStat[modstat]; found {
		op = SbOpSetNetStat
	} else if statindex, found = xeth.indexofStat[modstat]; found {
		op = SbOpSetEthtoolStat
	} else {
		return fmt.Errorf("STAT %q unknown", stat)
	}
	buf := make([]byte, SizeofSbHdr+SizeofSbSetStat)
	sbhdr := (*SbHdr)(unsafe.Pointer(&buf[0]))
	sbhdr.Op = op
	sbstat := (*SbSetStat)(unsafe.Pointer(&buf[SizeofSbHdr]))
	sbstat.Ifindex = ifindex
	sbstat.Statindex = statindex
	sbstat.Count = count
	_, _, err := xeth.WriteMsgUnix(buf, oob, nil)
	return err
}

func (xeth *Xeth) RxExceptionFrame(buf []byte) error {
	oob := []byte{}
	_, _, err := xeth.WriteMsgUnix(buf, oob, nil)
	return err
}
