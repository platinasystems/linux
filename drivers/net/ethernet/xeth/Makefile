obj-$(CONFIG_NET_XETH) += xeth.o

xeth-y := xeth_main.o
xeth-y += xeth_kstrs.o
xeth-y += xeth_mux.o
xeth-y += xeth_nb.o
xeth-y += xeth_sb.o
xeth-y += xeth_sbrx.o
xeth-y += xeth_sbtx.o
xeth-y += xeth_upper.o

ccflags-y += -I$(src) --include=xeth.h
