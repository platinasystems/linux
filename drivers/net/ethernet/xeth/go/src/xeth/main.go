/* A sample XETH controller.
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
	"io/ioutil"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"unsafe"
)

func Main() {
	var once sync.Once
	name := filepath.Base(os.Args[0])
	args := os.Args[1:]
	usage := fmt.Sprint("usage:",
		"\t", name, " carrier DEVICE CARRIER ...\n",
		"\t", name, " dump [forever] DB ...\n",
		"\t", name, " set DEVICE speed COUNT\n",
		"\t", name, " set DEVICE STAT COUNT\n",
		"\t", name, " FILE | - ...\n",
		`
CARRIER	{ on | off }
COUNT	unsigned number
DB	{ ifinfo | fdb }
DEVICE	an interface name or its ifindex
STAT	an 'ip link' or 'ethtool' statistic
FILE,-	receive an exception frame from FILE or STDIN`)
	err := Start(strings.TrimPrefix(name, "sample-"))
	defer func() {
		r := recover()
		if err := Stop(); r == nil {
			r = err
		}
		if r != nil {
			fmt.Fprint(os.Stderr, name, ": ", r, "\n")
			os.Exit(1)
		}
	}()
	if err != nil {
		panic(err)
	}
	if len(args) == 0 {
		fmt.Println(usage)
		return
	}
	for len(args) > 0 {
		switch args[0] {
		case "help", "-help", "--help", "-h":
			fmt.Println(usage)
			return
		case "carrier", "-carrier", "--carrier":
			var flag CarrierFlag
			switch len(args) {
			case 1:
				panic(fmt.Errorf("missing DEVICE\n%s", usage))
			case 2:
				panic(fmt.Errorf("missing CARRIER\n%s", usage))
			}
			switch args[2] {
			case "on":
				flag = XETH_CARRIER_ON
			case "off":
				flag = XETH_CARRIER_OFF
			default:
				panic(fmt.Errorf("CARRIER %q unknown\n%s",
					args[2], usage))
			}
			once.Do(CacheIfinfo)
			Carrier(Interface.Named(args[1]).Index, flag)
			args = args[3:]
		case "dump", "-dump", "--dump":
			var sig chan os.Signal
			for {
				args = args[1:]
				if len(args) == 0 {
					panic(fmt.Errorf("missing DB\n%s",
						usage))
				}
				if args[0] == "forever" {
					sig = make(chan os.Signal)
					signal.Notify(sig, syscall.SIGINT,
						syscall.SIGTERM)
				} else {
					break
				}
			}
			switch args[0] {
			case "ifinfo":
				DumpIfinfo()
				if err = UntilBreak(show); err != nil {
					panic(err)
				}
			case "fib":
				DumpIfinfo()
				if err = UntilBreak(show); err != nil {
					panic(err)
				}
				DumpFib()
				if err = UntilBreak(show); err != nil {
					panic(err)
				}
			default:
				panic(fmt.Errorf("DB %q unknown\n%s",
					args[0], usage))
			}
			args = args[1:]
			if sig != nil {
				if err = UntilSig(sig, show); err != nil {
					panic(err)
				}
			}
		case "set", "-set", "--set":
			var count uint64
			switch len(args) {
			case 1:
				panic(fmt.Errorf("missing DEVICE\n%s", usage))
			case 2:
				panic(fmt.Errorf("missing STAT | %q\n%s",
					"speed", usage))
			case 3:
				panic(fmt.Errorf("missing COUNT\n%s", usage))
			}
			_, err := fmt.Sscan(args[3], &count)
			if err != nil {
				panic(fmt.Errorf("COUNT %q %v",
					args[3], err))
			}
			once.Do(CacheIfinfo)
			ifindex := Interface.Named(args[1]).Index
			if args[2] == "speed" {
				err = Speed(ifindex, count)
			} else {
				err = SetStat(ifindex, args[2], count)
			}
			if err != nil {
				panic(err)
			}
			args = args[4:]
		case "-":
			buf, err := ioutil.ReadAll(os.Stdin)
			if err != nil {
				panic(err)
			}
			Tx(buf)
			args = args[1:]
		default:
			buf, err := ioutil.ReadFile(args[0])
			if err != nil {
				panic(err)
			}
			Tx(buf)
			args = args[1:]
		}
	}
}

func show(buf []byte) error {
	ptr := unsafe.Pointer(&buf[0])
	switch kind := KindOf(buf); kind {
	case XETH_MSG_KIND_LINK_STAT:
		fmt.Print((*MsgLinkStat)(ptr))
	case XETH_MSG_KIND_ETHTOOL_STAT:
		fmt.Print((*MsgEthtoolStat)(ptr))
	case XETH_MSG_KIND_ETHTOOL_FLAGS:
		fmt.Print((*MsgEthtoolFlags)(ptr))
	case XETH_MSG_KIND_ETHTOOL_SETTINGS:
		fmt.Print((*MsgEthtoolSettings)(ptr))
	case XETH_MSG_KIND_IFINFO:
		fmt.Print((*MsgIfinfo)(ptr))
	case XETH_MSG_KIND_IFA:
		fmt.Print((*MsgIfa)(ptr))
	case XETH_MSG_KIND_FIBENTRY:
		fmt.Print((*MsgFibentry)(ptr))
	case XETH_MSG_KIND_NEIGH_UPDATE:
		fmt.Print((*MsgNeighUpdate)(ptr))
	default:
		fmt.Println(kind)
	}
	return nil
}
