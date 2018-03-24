// A sample XETH sideband controller.
package cmd

import (
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"xeth"
)

type Cmd struct {
	driver      string
	indexofStat map[string]uint64
	xeth        *xeth.Xeth
}

func New(stats []string) *Cmd {
	return &Cmd{
		driver:      filepath.Base(os.Args[0]),
		indexofStat: xeth.MapIndexofStats(stats),
	}
}

// Run sample XETH controller.
func (cmd *Cmd) Main() {
	args := os.Args[1:]
	usage := fmt.Sprint("usage:\t", cmd.driver,
		` { -dump | -set DEVICE STAT COUNT | FILE | - }...

DEVICE	an interface name or its ifindex
STAT	an 'ip link' or 'ethtool' statistic
FILE,-	receive an exception frame from FILE or STDIN`)
	defer cmd.Egress()

	if len(args) == 0 {
		fmt.Println(usage)
		return
	}
	for len(args) > 0 {
		switch args[0] {
		case "help", "-help", "--help":
			fmt.Println(usage)
			return
		case "dump", "-dump", "--dump":
			fmt.Println("FIXME dump recvmsg")
		case "set", "-set", "--set":
			var ifindex, count uint64
			switch len(args) {
			case 1:
				panic(fmt.Errorf("missing DEVICE\n%s", usage))
			case 2:
				panic(fmt.Errorf("missing STAT\n%s", usage))
			case 3:
				panic(fmt.Errorf("missing COUNT\n%s", usage))
			}
			_, err := fmt.Sscan(args[1], &ifindex)
			if err != nil {
				itf, err := net.InterfaceByName(args[1])
				if err != nil {
					panic(fmt.Errorf("DEVICE: %v", err))
				}
				ifindex = uint64(itf.Index)
			}
			_, err = fmt.Sscan(args[3], &count)
			if err != nil {
				panic(fmt.Errorf("COUNT %q %v", args[3], err))
			}
			err = cmd.Xeth().Set(ifindex, args[2], count)
			if err != nil {
				panic(err)
			}
			args = args[4:]
		case "-":
			buf, err := ioutil.ReadAll(os.Stdin)
			if err != nil {
				panic(err)
			}
			if err = cmd.Xeth().RxExceptionFrame(buf); err != nil {
				panic(err)
			}
			args = args[1:]
		default:
			buf, err := ioutil.ReadFile(args[0])
			if err != nil {
				panic(err)
			}
			if err = cmd.Xeth().RxExceptionFrame(buf); err != nil {
				panic(err)
			}
			args = args[1:]
		}
	}
}

// If necessary, open socket to XETH driver.
func (cmd *Cmd) Xeth() *xeth.Xeth {
	if cmd.xeth == nil {
		var err error
		cmd.xeth, err = xeth.New(cmd.driver, cmd.indexofStat)
		if err != nil {
			panic(err)
		}
	}
	return cmd.xeth
}

// Recover, close XETH driver socket, then print and exit if any error.
func (cmd *Cmd) Egress() {
	r := recover()
	if cmd.xeth != nil {
		if err := cmd.xeth.Shutdown(); r == nil {
			r = err
		}
	}
	if r != nil {
		fmt.Fprint(os.Stderr, cmd.driver, ": ", r, "\n")
		os.Exit(1)
	}
}
