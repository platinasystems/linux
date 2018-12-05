xeth-$(CONFIG_NET_XETH) := xeth.o
xeth-$(CONFIG_NET_XETH) += dev.o
xeth-$(CONFIG_NET_XETH) += ethtool.o
xeth-$(CONFIG_NET_XETH) += iflink.o
xeth-$(CONFIG_NET_XETH) += ndo.o
xeth-$(CONFIG_NET_XETH) += notifier.o
xeth-$(CONFIG_NET_XETH) += sb.o
xeth-$(CONFIG_NET_XETH) += sysfs.o

vlan-$(CONFIG_NET_XETH) := vlan.o

obj-$(CONFIG_XETH_VENDOR_PLATINA_MK1) += platina-mk1.o

platina-mk1-y := platina_mk1.o
platina-mk1-y += platina_mk1_i2c.o
platina-mk1-y += platina_mk1_ethtool.o
platina-mk1-y += $(xeth-y)
platina-mk1-y += $(vlan-y)

xeth_ver = $(shell cat $(objtree)/include/config/kernel.release 2> /dev/null)
ccflags-y += -DXETH_VERSION="$(if $(xeth_ver),$(xeth_ver),unknown)"
ccflags-y += -I$(src) --include=xeth.h

gopath := $(if $(GO),$(shell $(GO) env GOPATH))
gopath-xeth = $(gopath)/src/github.com/platinasystems/xeth

xeth-go = $(if $(gopath),y)
platina-mk1-go = $(and $(CONFIG_XETH_VENDOR_PLATINA_MK1),$(gopath),y)

extra-$(xeth-go) += godefed.go
extra-$(platina-mk1-go) += platina_mk1_flags.go
extra-$(platina-mk1-go) += platina_mk1_stats.go

hostprogs-$(CONFIG_XETH_VENDOR_PLATINA_MK1) += gen-platina-mk1-flags
hostprogs-$(CONFIG_XETH_VENDOR_PLATINA_MK1) += gen-platina-mk1-stats

gen-platina-mk1-flags-objs = gen_platina_mk1_flags.o
gen-platina-mk1-stats-objs = gen_platina_mk1_stats.o

xeth-go-dest = $(addprefix $(gopath-xeth)/,$(subst _,/,$(@F)))

quiet_cmd_xeth_install	= INSTALL $(xeth-go-dest)
      cmd_xeth_install	= cp -f $@ $(xeth-go-dest)

quiet_cmd_xeth_godefs	= GODEFS  $@
      cmd_xeth_godefs	= $(GO) tool cgo -godefs -- $(LINUXINCLUDE)

quiet_cmd_xeth_gen	= GOGEN   $@
      cmd_xeth_gen	= $<

$(obj)/godefed.go: godefs.go include/uapi/linux/xeth.h
	$(call cmd,xeth_godefs) $< > $@
	$(call cmd,xeth_install)

$(obj)/platina_mk1_flags.go: $(obj)/gen-platina-mk1-flags
	$(call cmd,xeth_gen) > $@
	$(call cmd,xeth_install)

$(obj)/platina_mk1_stats.go: $(obj)/gen-platina-mk1-stats
	$(call cmd,xeth_gen) > $@
	$(call cmd,xeth_install)
