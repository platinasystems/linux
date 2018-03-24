// Created by cgo -godefs - DO NOT EDIT
// cgo -godefs -- -I./arch/x86/include -I./arch/x86/include/generated -I./include -I./arch/x86/include/uapi -I./arch/x86/include/generated/uapi -I./include/uapi -I./include/generated/uapi -include ./include/linux/kconfig.h ./drivers/net/ethernet/xeth/go/src/xeth/godefs.go

package xeth

const (
	SizeofMaxJumboFrame	= 0x2600
	SizeofSbHdr		= 0x10
	SizeofSbSetStat		= 0x18
	SbOpSetNetStat		= 0x1
	SbOpSetEthtoolStat	= 0x2
)

type SbHdr struct {
	Z64	uint64
	Z32	uint32
	Z16	uint16
	Z8	uint8
	Op	uint8
}
type SbSetStat struct {
	Ifindex		uint64
	Statindex	uint64
	Count		uint64
}
