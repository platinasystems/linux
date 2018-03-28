xeth-$(CONFIG_NET_XETH) := xeth.o
xeth-$(CONFIG_NET_XETH) += ethtool.o
xeth-$(CONFIG_NET_XETH) += link.o
xeth-$(CONFIG_NET_XETH) += ndo.o
xeth-$(CONFIG_NET_XETH) += notifier.o
xeth-$(CONFIG_NET_XETH) += sb.o

vlan-$(CONFIG_NET_XETH) := vlan.o

obj-$(CONFIG_XETH_VENDOR_PLATINA_MK1) += platina-mk1.o

platina-mk1-y := platina_mk1.o platina_mk1_stats.o $(xeth-y) $(vlan-y)

GIT_VERSION := $(shell git describe --always --long --dirty || echo "unknown")

ccflags-y += -DXETH_VERSION="$(GIT_VERSION)"
ccflags-y += -I$(src) --include=xeth.h --include=pr.h

go-platina-mk1 := $(if $(CONFIG_XETH_VENDOR_PLATINA_MK1),$(CONFIG_SAMPLE_XETH))
extra-$(go-platina-mk1) += platina-mk1
hostprogs-$(go-platina-mk1) += gen-platina-mk1-stats
gen-platina-mk1-stats-objs := gen_platina_mk1_stats.o platina_mk1_stats.o

GOPATH := $(realpath $(srctree)/$(src)/go)
export GOPATH

go-list-go = $(foreach pkg,$(1),$(addprefix $(src)/go/src/$(pkg)/,$(shell\
	go list -f '{{ join .GoFiles " "}}' $(srctree)/$(src)/go/src/$(pkg))))
quiet_cmd_gobuild = GOBUILD $@
      cmd_gobuild = go build -o $@

platina-mk1-deps := $(call go-list-go,platina-mk1 xeth)
platina-mk1-deps += $(src)/go/src/xeth/godefed.go

$(obj)/platina-mk1: $(platina-mk1-deps)
	$(call cmd,gobuild) platina-mk1

quiet_cmd_genstats = GOGEN   $@
      cmd_genstats = $(obj)/gen-platina-mk1-stats

$(src)/go/src/platina-mk1/stats.go: $(obj)/gen-platina-mk1-stats
	$(call cmd,genstats) > $@

quiet_cmd_godefs  = GODEFS  $@
      cmd_godefs  = go tool cgo -godefs -- $(LINUXINCLUDE)

xeth-godefed-deps := $(src)/go/src/xeth/godefs.go
xeth-godefed-deps += $(srctree)/include/uapi/linux/xeth.h

$(src)/go/src/xeth/godefed.go: $(xeth-godefed-deps)
	$(call cmd,godefs) $(srctree)/$< > $@
