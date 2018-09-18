// +build ignore

package xeth

// #include <linux/types.h>
// #include <asm/byteorder.h>
// #define IFNAMSIZ 16
// #define ETH_ALEN 6
// #include "linux/xeth.h"
import "C"

const (
	IFNAMSIZ                 = C.IFNAMSIZ
	ETH_ALEN                 = C.ETH_ALEN
	SizeofJumboFrame         = C.XETH_SIZEOF_JUMBO_FRAME
	SizeofMsg                = C.sizeof_struct_xeth_msg
	SizeofMsgBreak           = C.sizeof_struct_xeth_msg_break
	SizeofMsgCarrier         = C.sizeof_struct_xeth_msg_carrier
	SizeofMsgDumpFibinfo     = C.sizeof_struct_xeth_msg_dump_fibinfo
	SizeofMsgDumpIfinfo      = C.sizeof_struct_xeth_msg_dump_ifinfo
	SizeofMsgEthtoolFlags    = C.sizeof_struct_xeth_msg_ethtool_flags
	SizeofMsgEthtoolSettings = C.sizeof_struct_xeth_msg_ethtool_settings
	SizeofMsgIfa             = C.sizeof_struct_xeth_msg_ifa
	SizeofMsgIfinfo          = C.sizeof_struct_xeth_msg_ifinfo
	SizeofNextHop            = C.sizeof_struct_xeth_next_hop
	SizeofMsgFibentry        = C.sizeof_struct_xeth_msg_fibentry
	SizeofMsgNeighUpdate     = C.sizeof_struct_xeth_msg_neigh_update
	SizeofMsgSpeed           = C.sizeof_struct_xeth_msg_speed
	SizeofMsgStat            = C.sizeof_struct_xeth_msg_stat
)

type Msg C.struct_xeth_msg

type MsgBreak C.struct_xeth_msg_break

type MsgCarrier C.struct_xeth_msg_carrier

type MsgEthtoolFlags C.struct_xeth_msg_ethtool_flags

type MsgEthtoolSettings C.struct_xeth_msg_ethtool_settings

type NextHop C.struct_xeth_next_hop

type MsgFibentry C.struct_xeth_msg_fibentry

type MsgIfa C.struct_xeth_msg_ifa

type MsgIfinfo C.struct_xeth_msg_ifinfo

type MsgNeighUpdate C.struct_xeth_msg_neigh_update

type MsgSpeed C.struct_xeth_msg_speed

type MsgStat C.struct_xeth_msg_stat
