obj-$(CONFIG_NET_XETH) += xeth.o
obj-$(CONFIG_NET_XETH) += xeth-pdev.o

xeth-y := xeth_main.o
xeth-y += xeth_kstrs.o
xeth-y += xeth_mux.o
xeth-y += xeth_nb.o
xeth-y += xeth_onie.o
xeth-y += xeth_sb.o
xeth-y += xeth_sbrx.o
xeth-y += xeth_sbtx.o
xeth-y += xeth_upper.o
xeth-y += xeth_vendor.o

xeth-pdev-y := xeth_pdev.o

xeth-$(CONFIG_NET_XETH_VENDOR_PLATINA) += xeth_platina.o
xeth-$(CONFIG_NET_XETH_VENDOR_PLATINA) += xeth_platina_mk1.o

ccflags-y += -I$(src) --include=xeth.h
