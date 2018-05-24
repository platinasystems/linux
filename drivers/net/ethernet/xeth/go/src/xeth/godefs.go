// +build ignore

package xeth

// #define IFNAMSIZ 16
// struct ethtool_link_ksettings {};
// #include <linux/types.h>
// #include <asm/byteorder.h>
// #include "linux/xeth.h"
import "C"

const (
	IFNAMSIZ              = C.IFNAMSIZ
	SizeofJumboFrame      = C.XETH_SIZEOF_JUMBO_FRAME
	SizeofMsgHdr          = C.sizeof_struct_xeth_msg_hdr
	SizeofBreakMsg        = C.sizeof_struct_xeth_break_msg
	SizeofStatMsg         = C.sizeof_struct_xeth_stat_msg
	SizeofEthtoolFlagsMsg = C.sizeof_struct_xeth_ethtool_flags_msg
	SizeofDumpIfinfoMsg   = C.sizeof_struct_xeth_dump_ifinfo_msg
	SizeofCarrierMsg      = C.sizeof_struct_xeth_carrier_msg
	SizeofSpeedMsg        = C.sizeof_struct_xeth_speed_msg
)

type Hdr C.struct_xeth_msg_hdr
type Stat C.struct_xeth_stat
type Ifa C.struct_xeth_ifa
