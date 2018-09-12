xeth-$(CONFIG_NET_XETH) := xeth.o
xeth-$(CONFIG_NET_XETH) += dev.o
xeth-$(CONFIG_NET_XETH) += ethtool.o
xeth-$(CONFIG_NET_XETH) += iflink.o
xeth-$(CONFIG_NET_XETH) += link.o
xeth-$(CONFIG_NET_XETH) += ndo.o
xeth-$(CONFIG_NET_XETH) += notifier.o
xeth-$(CONFIG_NET_XETH) += sb.o
xeth-$(CONFIG_NET_XETH) += sysfs.o

vlan-$(CONFIG_NET_XETH) := vlan.o

obj-$(CONFIG_XETH_VENDOR_PLATINA_MK1) += platina-mk1.o

platina-mk1-y := platina_mk1.o  
platina-mk1-y += $(xeth-y)
platina-mk1-y += $(vlan-y)

xeth_ver = $(shell cat $(objtree)/include/config/kernel.release 2> /dev/null)
ccflags-y += -DXETH_VERSION="$(if $(xeth_ver),$(xeth_ver),unknown)"
ccflags-y += -I$(src) --include=xeth.h

go-platina-mk1 := $(if $(CONFIG_XETH_VENDOR_PLATINA_MK1),$(CONFIG_SAMPLE_XETH))
extra-$(go-platina-mk1) += sample-platina-mk1
hostprogs-$(go-platina-mk1) += gen-platina-mk1-stats
hostprogs-$(go-platina-mk1) += gen-platina-mk1-flags
gen-platina-mk1-stats-objs := gen_platina_mk1_stats.o
gen-platina-mk1-flags-objs := gen_platina_mk1_flags.o

GOPATH := $(realpath $(srctree)/$(src)/go)
export GOPATH

go-list-go = $(foreach pkg,$(1),$(addprefix $(GOPATH)/src/$(pkg)/,\
	$(shell env GOPATH=$(GOPATH)\
		go list -f '{{ join .GoFiles " "}}' $(pkg))))

quiet_cmd_gobuild = GOBUILD $@
      cmd_gobuild = go build -o $@

sample-platina-mk1-deps := $(call go-list-go,sample-platina-mk1 xeth)
sample-platina-mk1-deps += $(GOPATH)/src/xeth/godefed.go

$(obj)/sample-platina-mk1: $(sample-platina-mk1-deps)
	$(call cmd,gobuild) $(@F)

quiet_cmd_genstats = GOGEN   $@
      cmd_genstats = $(obj)/gen-platina-mk1-stats

quiet_cmd_genflags = GOGEN   $@
      cmd_genflags = $(obj)/gen-platina-mk1-flags

$(GOPATH)/src/sample-platina-mk1/stats.go: $(obj)/gen-platina-mk1-stats
	$(call cmd,genstats) > $@

$(GOPATH)/src/sample-platina-mk1/flags.go: $(obj)/gen-platina-mk1-flags
	$(call cmd,genflags) > $@

quiet_cmd_godefs  = GODEFS  $@
      cmd_godefs  = go tool cgo -godefs -- $(LINUXINCLUDE)

xeth-godefed-deps := $(GOPATH)/src/xeth/godefs.go
xeth-godefed-deps += $(srctree)/include/uapi/linux/xeth.h

$(GOPATH)/src/xeth/godefed.go: $(xeth-godefed-deps)
	$(call cmd,godefs) $< > $@
