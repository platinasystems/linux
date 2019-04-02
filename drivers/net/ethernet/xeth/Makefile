obj-$(CONFIG_NET_XETH) += xeth.o

xeth-y := xeth_main.o
xeth-y += xeth_dev.o
xeth-y += xeth_ethtool.o
xeth-y += xeth_iflink.o
xeth-y += xeth_ndo.o
xeth-y += xeth_notifier.o
xeth-y += xeth_sb.o
xeth-y += xeth_sysfs.o
xeth-y += xeth_vlan.o

ccflags-y += -I$(src) --include=xeth.h
CFLAGS_xeth_main.o = -DXETH_MAIN
