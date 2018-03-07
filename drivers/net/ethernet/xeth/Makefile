obj-$(CONFIG_XETH_VENDOR_PLATINA_MK1) += platina-mk1.o

GIT_VERSION := $(shell git describe --always --long --dirty || echo "unknown")

ccflags-y += -DXETH_VERSION="$(GIT_VERSION)"
ccflags-y += -I$(src) --include=xeth.h --include=debug.h

xeths := xeth.o ethtool.o link.o ndo.o notifier.o vlan.o sysfs.o devfs.o

platina-mk1-y := platina_mk1.o $(xeths)
