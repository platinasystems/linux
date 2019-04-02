/* Platina Systems MK1 top of rack switch platform driver.
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/onie.h>
#include <linux/xeth.h>

MODULE_DESCRIPTION("Platina Systems MK1 TOR Switch platform driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION("2.0");

#if defined(CONFIG_DYNAMIC_DEBUG)
#define platina_mk1_dynamic_debug	true
#else
#define platina_mk1_dynamic_debug	false
#endif

#define NAME "platina-mk1"

#define platina_mk1_pr_egress(err, format, args...)			\
({									\
	int _err = (err);						\
	if (_err)							\
		pr_err(NAME ": %d, " format "\n", (_err), ##args);	\
	else if (platina_mk1_dynamic_debug)				\
		pr_debug(format "\n", ##args);				\
	else								\
		no_printk(KERN_DEBUG pr_fmt(format), ##args);		\
	(platina_mk1_egress(_err));					\
})

enum {
	platina_mk1_eeprom_pgsz = 32,
	platina_mk1_n_ports = 32,
	platina_mk1_n_rxqs = 1,
	platina_mk1_n_txqs = 1,
};

static int provision[platina_mk1_n_ports];
module_param_array(provision, int, NULL, 0644);
MODULE_PARM_DESC(provision, " 1, 2, or 4 subports per port, default 1");

static int __init platina_mk1_init(void);
static void __exit platina_mk1_exit(void);
module_init(platina_mk1_init);
module_exit(platina_mk1_exit);

static const unsigned short platina_mk1_eeprom_addrs[];
static struct i2c_board_info platina_mk1_eeprom_info;
static const struct property_entry platina_mk1_eeprom_properties[];
static const struct i2c_board_info platina_mk1_mezzanine_info;
static const char * const * const platina_mk1_iflinks_akas[];
static const char * const platina_mk1_ethtool_flags[];
static const char * const platina_mk1_ethtool_stats[];
static const struct xeth_config platina_mk1_default_xeth_config;

static void platina_mk1_ethtool_init_settings(struct ethtool_link_ksettings *);
static int platina_mk1_ethtool_validate_speed(u32 speed);
static size_t platina_mk1_n_eeprom_addrs(void);

static struct {
	struct	platform_device	*pdev;
	struct	i2c_adapter	*adapter;
	struct	i2c_client	*mezzanine, *eeprom;
	u8	eeprom_cache[onie_max_data];
	void	*onie;
	struct {
		struct	xeth_config	config;
		struct	kobject		*kobj;
	} xeth;
} *platina_mk1;

static int platina_mk1_rewind_eeprom(void)
{
	return i2c_smbus_write_byte_data(platina_mk1->eeprom, 0, 0);
}

static ssize_t platina_mk1_read_eeprom(size_t offset, size_t count)
{
	int i;
	s32 ret;

	for (i = 0; i < count; i++) {
		ret = i2c_smbus_read_byte(platina_mk1->eeprom);
		if (ret < 0)
			return ret;
		platina_mk1->eeprom_cache[offset+i] = ret;
	}
	return count;
}

static ssize_t platina_mk1_egress_eeprom(ssize_t err)
{
	if (platina_mk1->eeprom) {
		i2c_unregister_device(platina_mk1->eeprom);
		platina_mk1->eeprom = NULL;
	}
	return err;
}

static ssize_t platina_mk1_load_eeprom(unsigned short addr)
{
	u8 *data = platina_mk1->eeprom_cache;
	int err, tries;
	ssize_t rem, sz;

	platina_mk1_eeprom_info.addr = addr;
	platina_mk1->eeprom = i2c_new_device(platina_mk1->adapter,
					     &platina_mk1_eeprom_info);
	if (!platina_mk1->eeprom)
		return -ENODEV;
	/* we retry b/c the EEPROM driver probe interferes w/ first read */
	for (tries = 0; tries < 3; tries++) {
		msleep(10);
		err = platina_mk1_rewind_eeprom();
		if (err)
			continue;
		msleep(10);
		sz = max_t(size_t, platina_mk1_eeprom_pgsz, onie_min_data);
		err = platina_mk1_read_eeprom(0, sz);
		if (err < 0)
			return platina_mk1_egress_eeprom(err);
		rem = onie_validate(data, sz);
		if (rem < 0)
			continue;
		if (rem == 0)
			break;
		err = platina_mk1_read_eeprom(sz, rem);
		if (err < 0)
			return platina_mk1_egress_eeprom(err);
		sz = onie_validate(data, 0);
		if (sz > 0)
			return sz;
	}
	return platina_mk1_egress_eeprom(0);
}

static void platina_mk1_ethtool_init_settings(struct ethtool_link_ksettings *s)
{
	s->base.speed = 0;
	s->base.duplex = DUPLEX_FULL;
	s->base.autoneg = AUTONEG_ENABLE;
	s->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(s, supported);
	ethtool_link_ksettings_add_link_mode(s, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     100000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     100000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     100000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported,
					     100000baseLR4_ER4_Full);
	ethtool_link_ksettings_add_link_mode(s, supported, TP);
	ethtool_link_ksettings_add_link_mode(s, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(s, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(s, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(s, supported, FEC_BASER);
	bitmap_copy(s->link_modes.advertising,
		    s->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static int platina_mk1_ethtool_validate_speed(u32 mbps)
{
	switch (mbps) {
	case 100000:
	case 50000:
	case 40000:
	case 25000:
	case 20000:
	case 10000:
	case 1000:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct kobject *platina_mk1_create_xeth(void)
{
	ssize_t sz;
	u8 v[ETH_ALEN];

	memcpy(&platina_mk1->xeth.config, &platina_mk1_default_xeth_config,
	       sizeof(platina_mk1_default_xeth_config));
	sz = onie_value(platina_mk1->onie, onie_type_device_version,
			sizeof(u8), v);
	if (sz == sizeof(u8) && v[0] == 0)
		platina_mk1->xeth.config.base = 0;
	sz = onie_value(platina_mk1->onie, onie_type_mac_base,
			ETH_ALEN, v);
	if (sz == ETH_ALEN) {
		platina_mk1->xeth.config.ea.base = 4 + ether_addr_to_u64(v);
		platina_mk1->xeth.config.ea.assign_type = NET_ADDR_PERM;
	} else {
		eth_random_addr(v);
		platina_mk1->xeth.config.ea.base = ether_addr_to_u64(v);
		platina_mk1->xeth.config.ea.assign_type = NET_ADDR_RANDOM;
	}
	return xeth_create(&platina_mk1->pdev->dev.kobj,
			   &platina_mk1->xeth.config);
}

static int platina_mk1_egress(int err)
{
	if (!platina_mk1)
		return err;
	if (platina_mk1->xeth.kobj) {
		xeth_delete(platina_mk1->xeth.kobj);
		platina_mk1->xeth.kobj = NULL;
	}
	if (platina_mk1->onie) {
		onie_delete(platina_mk1->onie);
		platina_mk1->onie = NULL;
	}
	platina_mk1_egress_eeprom(0);
	if (platina_mk1->mezzanine) {
		i2c_unregister_device(platina_mk1->mezzanine);
		platina_mk1->mezzanine = NULL;
	}
	if (platina_mk1->adapter) {
		i2c_put_adapter(platina_mk1->adapter);
		platina_mk1->adapter = NULL;
	}
	if (platina_mk1->pdev) {
		platform_device_unregister(platina_mk1->pdev);
		platina_mk1->pdev = NULL;
	}
	kfree(platina_mk1);
	platina_mk1 = NULL;
	return err;
}

static int __init platina_mk1_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(provision); i++)
		if (provision[i] == 0)
			provision[i] = 1;
		else if (provision[i] != 1 &&
			 provision[i] != 2 &&
			 provision[i] != 4)
			return -EINVAL;

	platina_mk1 = kzalloc(sizeof(*platina_mk1), GFP_KERNEL);
	if (!platina_mk1)
		return platina_mk1_egress(-ENOMEM);
	platina_mk1->pdev = platform_device_register_data(NULL, NAME,
							  PLATFORM_DEVID_NONE,
							  NULL, 0);
	if (IS_ERR(platina_mk1->pdev))
		return platina_mk1_pr_egress(PTR_ERR(platina_mk1->pdev),
					     "can't register device");
	platina_mk1->adapter = i2c_get_adapter(0);
	if (!platina_mk1->adapter)
		return platina_mk1_pr_egress(-ENODEV, "no i2c bus 0");
	platina_mk1->mezzanine = i2c_new_device(platina_mk1->adapter,
						&platina_mk1_mezzanine_info);
	if (!platina_mk1->mezzanine)
		return platina_mk1_pr_egress(-ENODEV,
					     "no mezzanine temp device");
	for (i = 0; i < platina_mk1_n_eeprom_addrs(); i++)
		if (platina_mk1_load_eeprom(platina_mk1_eeprom_addrs[i]) > 0)
			break;
	if (!platina_mk1->eeprom)
		return platina_mk1_pr_egress(-ENODEV, "no onie format eeprom");
	platina_mk1->onie = onie_create(&platina_mk1->pdev->dev.kobj,
					platina_mk1->eeprom_cache,
					NULL);
	if (IS_ERR(platina_mk1->onie))
		return platina_mk1_pr_egress(PTR_ERR(platina_mk1->onie),
					     "can't parse onie eeprom");
	platina_mk1->xeth.kobj = platina_mk1_create_xeth();
	if (IS_ERR(platina_mk1->xeth.kobj))
		return platina_mk1_pr_egress(PTR_ERR(platina_mk1->xeth.kobj),
					     "can't create xeth device");
	return 0;
}

static void __exit platina_mk1_exit(void)
{
	platina_mk1_pr_egress(0, "done");
}

static const struct xeth_config platina_mk1_default_xeth_config = {
	.base = 1,
	.ports = platina_mk1_n_ports,
	.rxqs = platina_mk1_n_rxqs,
	.txqs = platina_mk1_n_txqs,
	.provision = provision,
	.iflinks_akas = platina_mk1_iflinks_akas,
	.ethtool.init_settings = platina_mk1_ethtool_init_settings,
	.ethtool.validate_speed = platina_mk1_ethtool_validate_speed,
	.ethtool.flags = platina_mk1_ethtool_flags,
	.ethtool.stats = platina_mk1_ethtool_stats,
};

static const unsigned short platina_mk1_eeprom_addrs[] = { 0x51, 0x50 };

static size_t platina_mk1_n_eeprom_addrs(void)
{
	return ARRAY_SIZE(platina_mk1_eeprom_addrs);
}

static struct i2c_board_info platina_mk1_eeprom_info = {
	.type = "24c04",
	.properties = platina_mk1_eeprom_properties,
};

static const struct property_entry platina_mk1_eeprom_properties[] = {
	PROPERTY_ENTRY_U32("pagesize", platina_mk1_eeprom_pgsz),
	PROPERTY_ENTRY_BOOL("no-read-rollover"),
	{},
};

static const struct i2c_board_info platina_mk1_mezzanine_info = {
	.type ="lm75",
	.addr = 0x4f,
};

static const char * const platina_mk1_eth1_akas[] = {
	"eth1", "enp3s0f0", NULL
};

static const char * const platina_mk1_eth2_akas[] = {
	"eth2", "enp3s0f1", NULL
};

static const char * const * const platina_mk1_iflinks_akas[] = {
	platina_mk1_eth1_akas,
	platina_mk1_eth2_akas,
	NULL,
};

static const char * const platina_mk1_ethtool_flags[] = {
	"copper",
	"fec74",
	"fec91",
	NULL,
};

static const char * const platina_mk1_ethtool_stats[] = {
	"mmu-multicast-tx-cos0-drop-bytes",
	"mmu-multicast-tx-cos0-drop-packets",
	"mmu-multicast-tx-cos1-drop-bytes",
	"mmu-multicast-tx-cos1-drop-packets",
	"mmu-multicast-tx-cos2-drop-bytes",
	"mmu-multicast-tx-cos2-drop-packets",
	"mmu-multicast-tx-cos3-drop-bytes",
	"mmu-multicast-tx-cos3-drop-packets",
	"mmu-multicast-tx-cos4-drop-bytes",
	"mmu-multicast-tx-cos4-drop-packets",
	"mmu-multicast-tx-cos5-drop-bytes",
	"mmu-multicast-tx-cos5-drop-packets",
	"mmu-multicast-tx-cos6-drop-bytes",
	"mmu-multicast-tx-cos6-drop-packets",
	"mmu-multicast-tx-cos7-drop-bytes",
	"mmu-multicast-tx-cos7-drop-packets",
	"mmu-multicast-tx-qm-drop-bytes",
	"mmu-multicast-tx-qm-drop-packets",
	"mmu-multicast-tx-sc-drop-bytes",
	"mmu-multicast-tx-sc-drop-packets",
	"mmu-rx-threshold-drop-bytes",
	"mmu-rx-threshold-drop-packets",
	"mmu-tx-cpu-cos-0-drop-bytes",
	"mmu-tx-cpu-cos-0-drop-packets",
	"mmu-tx-cpu-cos-10-drop-bytes",
	"mmu-tx-cpu-cos-10-drop-packets",
	"mmu-tx-cpu-cos-11-drop-bytes",
	"mmu-tx-cpu-cos-11-drop-packets",
	"mmu-tx-cpu-cos-12-drop-bytes",
	"mmu-tx-cpu-cos-12-drop-packets",
	"mmu-tx-cpu-cos-13-drop-bytes",
	"mmu-tx-cpu-cos-13-drop-packets",
	"mmu-tx-cpu-cos-14-drop-bytes",
	"mmu-tx-cpu-cos-14-drop-packets",
	"mmu-tx-cpu-cos-15-drop-bytes",
	"mmu-tx-cpu-cos-15-drop-packets",
	"mmu-tx-cpu-cos-16-drop-bytes",
	"mmu-tx-cpu-cos-16-drop-packets",
	"mmu-tx-cpu-cos-17-drop-bytes",
	"mmu-tx-cpu-cos-17-drop-packets",
	"mmu-tx-cpu-cos-18-drop-bytes",
	"mmu-tx-cpu-cos-18-drop-packets",
	"mmu-tx-cpu-cos-19-drop-bytes",
	"mmu-tx-cpu-cos-19-drop-packets",
	"mmu-tx-cpu-cos-1-drop-bytes",
	"mmu-tx-cpu-cos-1-drop-packets",
	"mmu-tx-cpu-cos-20-drop-bytes",
	"mmu-tx-cpu-cos-20-drop-packets",
	"mmu-tx-cpu-cos-21-drop-bytes",
	"mmu-tx-cpu-cos-21-drop-packets",
	"mmu-tx-cpu-cos-22-drop-bytes",
	"mmu-tx-cpu-cos-22-drop-packets",
	"mmu-tx-cpu-cos-23-drop-bytes",
	"mmu-tx-cpu-cos-23-drop-packets",
	"mmu-tx-cpu-cos-24-drop-bytes",
	"mmu-tx-cpu-cos-24-drop-packets",
	"mmu-tx-cpu-cos-25-drop-bytes",
	"mmu-tx-cpu-cos-25-drop-packets",
	"mmu-tx-cpu-cos-26-drop-bytes",
	"mmu-tx-cpu-cos-26-drop-packets",
	"mmu-tx-cpu-cos-27-drop-bytes",
	"mmu-tx-cpu-cos-27-drop-packets",
	"mmu-tx-cpu-cos-28-drop-bytes",
	"mmu-tx-cpu-cos-28-drop-packets",
	"mmu-tx-cpu-cos-29-drop-bytes",
	"mmu-tx-cpu-cos-29-drop-packets",
	"mmu-tx-cpu-cos-2-drop-bytes",
	"mmu-tx-cpu-cos-2-drop-packets",
	"mmu-tx-cpu-cos-30-drop-bytes",
	"mmu-tx-cpu-cos-30-drop-packets",
	"mmu-tx-cpu-cos-31-drop-bytes",
	"mmu-tx-cpu-cos-31-drop-packets",
	"mmu-tx-cpu-cos-32-drop-bytes",
	"mmu-tx-cpu-cos-32-drop-packets",
	"mmu-tx-cpu-cos-33-drop-bytes",
	"mmu-tx-cpu-cos-33-drop-packets",
	"mmu-tx-cpu-cos-34-drop-bytes",
	"mmu-tx-cpu-cos-34-drop-packets",
	"mmu-tx-cpu-cos-35-drop-bytes",
	"mmu-tx-cpu-cos-35-drop-packets",
	"mmu-tx-cpu-cos-36-drop-bytes",
	"mmu-tx-cpu-cos-36-drop-packets",
	"mmu-tx-cpu-cos-37-drop-bytes",
	"mmu-tx-cpu-cos-37-drop-packets",
	"mmu-tx-cpu-cos-38-drop-bytes",
	"mmu-tx-cpu-cos-38-drop-packets",
	"mmu-tx-cpu-cos-39-drop-bytes",
	"mmu-tx-cpu-cos-39-drop-packets",
	"mmu-tx-cpu-cos-3-drop-bytes",
	"mmu-tx-cpu-cos-3-drop-packets",
	"mmu-tx-cpu-cos-40-drop-bytes",
	"mmu-tx-cpu-cos-40-drop-packets",
	"mmu-tx-cpu-cos-41-drop-bytes",
	"mmu-tx-cpu-cos-41-drop-packets",
	"mmu-tx-cpu-cos-42-drop-bytes",
	"mmu-tx-cpu-cos-42-drop-packets",
	"mmu-tx-cpu-cos-43-drop-bytes",
	"mmu-tx-cpu-cos-43-drop-packets",
	"mmu-tx-cpu-cos-44-drop-bytes",
	"mmu-tx-cpu-cos-44-drop-packets",
	"mmu-tx-cpu-cos-45-drop-bytes",
	"mmu-tx-cpu-cos-45-drop-packets",
	"mmu-tx-cpu-cos-46-drop-bytes",
	"mmu-tx-cpu-cos-46-drop-packets",
	"mmu-tx-cpu-cos-47-drop-bytes",
	"mmu-tx-cpu-cos-47-drop-packets",
	"mmu-tx-cpu-cos-4-drop-bytes",
	"mmu-tx-cpu-cos-4-drop-packets",
	"mmu-tx-cpu-cos-5-drop-bytes",
	"mmu-tx-cpu-cos-5-drop-packets",
	"mmu-tx-cpu-cos-6-drop-bytes",
	"mmu-tx-cpu-cos-6-drop-packets",
	"mmu-tx-cpu-cos-7-drop-bytes",
	"mmu-tx-cpu-cos-7-drop-packets",
	"mmu-tx-cpu-cos-8-drop-bytes",
	"mmu-tx-cpu-cos-8-drop-packets",
	"mmu-tx-cpu-cos-9-drop-bytes",
	"mmu-tx-cpu-cos-9-drop-packets",
	"mmu-unicast-tx-cos0-drop-bytes",
	"mmu-unicast-tx-cos0-drop-packets",
	"mmu-unicast-tx-cos1-drop-bytes",
	"mmu-unicast-tx-cos1-drop-packets",
	"mmu-unicast-tx-cos2-drop-bytes",
	"mmu-unicast-tx-cos2-drop-packets",
	"mmu-unicast-tx-cos3-drop-bytes",
	"mmu-unicast-tx-cos3-drop-packets",
	"mmu-unicast-tx-cos4-drop-bytes",
	"mmu-unicast-tx-cos4-drop-packets",
	"mmu-unicast-tx-cos5-drop-bytes",
	"mmu-unicast-tx-cos5-drop-packets",
	"mmu-unicast-tx-cos6-drop-bytes",
	"mmu-unicast-tx-cos6-drop-packets",
	"mmu-unicast-tx-cos7-drop-bytes",
	"mmu-unicast-tx-cos7-drop-packets",
	"mmu-unicast-tx-qm-drop-bytes",
	"mmu-unicast-tx-qm-drop-packets",
	"mmu-unicast-tx-sc-drop-bytes",
	"mmu-unicast-tx-sc-drop-packets",
	"mmu-wred-queue-cos0-drop-packets",
	"mmu-wred-queue-cos1-drop-packets",
	"mmu-wred-queue-cos2-drop-packets",
	"mmu-wred-queue-cos3-drop-packets",
	"mmu-wred-queue-cos4-drop-packets",
	"mmu-wred-queue-cos5-drop-packets",
	"mmu-wred-queue-cos6-drop-packets",
	"mmu-wred-queue-cos7-drop-packets",
	"mmu-wred-queue-qm-drop-packets",
	"mmu-wred-queue-sc-drop-packets",
	"port-rx-1024-to-1518-byte-packets",
	"port-rx-128-to-255-byte-packets",
	"port-rx-1519-to-1522-byte-vlan-packets",
	"port-rx-1519-to-2047-byte-packets",
	"port-rx-1tag-vlan-packets",
	"port-rx-2048-to-4096-byte-packets",
	"port-rx-256-to-511-byte-packets",
	"port-rx-2tag-vlan-packets",
	"port-rx-4096-to-9216-byte-packets",
	"port-rx-512-to-1023-byte-packets",
	"port-rx-64-byte-packets",
	"port-rx-65-to-127-byte-packets",
	"port-rx-802-3-length-error-packets",
	"port-rx-9217-to-16383-byte-packets",
	"port-rx-alignment-error-packets",
	"port-rx-broadcast-packets",
	"port-rx-bytes",
	"port-rx-code-error-packets",
	"port-rx-control-packets",
	"port-rx-crc-error-packets",
	"port-rx-eee-lpi-duration",
	"port-rx-eee-lpi-events",
	"port-rx-false-carrier-events",
	"port-rx-flow-control-packets",
	"port-rx-fragment-packets",
	"port-rx-good-packets",
	"port-rx-jabber-packets",
	"port-rx-mac-sec-crc-matched-packets",
	"port-rx-mtu-check-error-packets",
	"port-rx-multicast-packets",
	"port-rx-oversize-packets",
	"port-rx-packets",
	"port-rx-pfc-packets",
	"port-rx-pfc-priority-0",
	"port-rx-pfc-priority-1",
	"port-rx-pfc-priority-2",
	"port-rx-pfc-priority-3",
	"port-rx-pfc-priority-4",
	"port-rx-pfc-priority-5",
	"port-rx-pfc-priority-6",
	"port-rx-pfc-priority-7",
	"port-rx-promiscuous-packets",
	"port-rx-runt-bytes",
	"port-rx-runt-packets",
	"port-rx-src-address-not-unicast-packets",
	"port-rx-truncated-packets",
	"port-rx-undersize-packets",
	"port-rx-unicast-packets",
	"port-rx-unsupported-dst-address-control-packets",
	"port-rx-unsupported-opcode-control-packets",
	"port-rx-xon-to-xoff-priority-0",
	"port-rx-xon-to-xoff-priority-1",
	"port-rx-xon-to-xoff-priority-2",
	"port-rx-xon-to-xoff-priority-3",
	"port-rx-xon-to-xoff-priority-4",
	"port-rx-xon-to-xoff-priority-5",
	"port-rx-xon-to-xoff-priority-6",
	"port-rx-xon-to-xoff-priority-7",
	"port-tx-1024-to-1518-byte-packets",
	"port-tx-128-to-255-byte-packets",
	"port-tx-1519-to-1522-byte-vlan-packets",
	"port-tx-1519-to-2047-byte-packets",
	"port-tx-1tag-vlan-packets",
	"port-tx-2048-to-4096-byte-packets",
	"port-tx-256-to-511-byte-packets",
	"port-tx-2tag-vlan-packets",
	"port-tx-4096-to-9216-byte-packets",
	"port-tx-512-to-1023-byte-packets",
	"port-tx-64-byte-packets",
	"port-tx-65-to-127-byte-packets",
	"port-tx-9217-to-16383-byte-packets",
	"port-tx-broadcast-packets",
	"port-tx-bytes",
	"port-tx-control-packets",
	"port-tx-eee-lpi-duration",
	"port-tx-eee-lpi-events",
	"port-tx-excessive-collision-packets",
	"port-tx-fcs-errors",
	"port-tx-fifo-underrun-packets",
	"port-tx-flow-control-packets",
	"port-tx-fragments",
	"port-tx-good-packets",
	"port-tx-jabber-packets",
	"port-tx-late-collision-packets",
	"port-tx-multicast-packets",
	"port-tx-multiple-collision-packets",
	"port-tx-multiple-deferral-packets",
	"port-tx-oversize",
	"port-tx-packets",
	"port-tx-pfc-packets",
	"port-tx-pfc-priority-0-packets",
	"port-tx-pfc-priority-1-packets",
	"port-tx-pfc-priority-2-packets",
	"port-tx-pfc-priority-3-packets",
	"port-tx-pfc-priority-4-packets",
	"port-tx-pfc-priority-5-packets",
	"port-tx-pfc-priority-6-packets",
	"port-tx-pfc-priority-7-packets",
	"port-tx-runt-packets",
	"port-tx-single-collision-packets",
	"port-tx-single-deferral-packets",
	"port-tx-system-error-packets",
	"port-tx-total-collisions",
	"port-tx-unicast-packets",
	"punts",
	"rx-bytes",
	"rx-packets",
	"rx-pipe-debug-6",
	"rx-pipe-debug-7",
	"rx-pipe-debug-8",
	"rx-pipe-dst-discard-drops",
	"rx-pipe-ecn-counter",
	"rx-pipe-hi-gig-broadcast-packets",
	"rx-pipe-hi-gig-control-packets",
	"rx-pipe-hi-gig-l2-multicast-packets",
	"rx-pipe-hi-gig-l3-multicast-packets",
	"rx-pipe-hi-gig-unknown-opcode-packets",
	"rx-pipe-ibp-discard-cbp-full-drops",
	"rx-pipe-ip4-header-errors",
	"rx-pipe-ip4-l3-drops",
	"rx-pipe-ip4-l3-packets",
	"rx-pipe-ip4-routed-multicast-packets",
	"rx-pipe-ip6-header-errors",
	"rx-pipe-ip6-l3-drops",
	"rx-pipe-ip6-l3-packets",
	"rx-pipe-ip6-routed-multicast-packets",
	"rx-pipe-l3-interface-bytes",
	"rx-pipe-l3-interface-packets",
	"rx-pipe-multicast-drops",
	"rx-pipe-niv-forwarding-error-drops",
	"rx-pipe-niv-frame-error-drops",
	"rx-pipe-port-table-bytes",
	"rx-pipe-port-table-packets",
	"rx-pipe-rxf-drops",
	"rx-pipe-spanning-tree-state-not-forwarding-drops",
	"rx-pipe-trill-non-trill-drops",
	"rx-pipe-trill-packets",
	"rx-pipe-trill-trill-drops",
	"rx-pipe-tunnel-error-packets",
	"rx-pipe-tunnel-packets",
	"rx-pipe-unicast-packets",
	"rx-pipe-unknown-vlan-drops",
	"rx-pipe-vlan-tagged-packets",
	"rx-pipe-zero-port-bitmap-drops",
	"tx-bytes",
	"tx-packets",
	"tx-pipe-cpu-0x10-bytes",
	"tx-pipe-cpu-0x10-packets",
	"tx-pipe-cpu-0x11-bytes",
	"tx-pipe-cpu-0x11-packets",
	"tx-pipe-cpu-0x12-bytes",
	"tx-pipe-cpu-0x12-packets",
	"tx-pipe-cpu-0x13-bytes",
	"tx-pipe-cpu-0x13-packets",
	"tx-pipe-cpu-0x14-bytes",
	"tx-pipe-cpu-0x14-packets",
	"tx-pipe-cpu-0x15-bytes",
	"tx-pipe-cpu-0x15-packets",
	"tx-pipe-cpu-0x16-bytes",
	"tx-pipe-cpu-0x16-packets",
	"tx-pipe-cpu-0x17-bytes",
	"tx-pipe-cpu-0x17-packets",
	"tx-pipe-cpu-0x18-bytes",
	"tx-pipe-cpu-0x18-packets",
	"tx-pipe-cpu-0x19-bytes",
	"tx-pipe-cpu-0x19-packets",
	"tx-pipe-cpu-0x1a-bytes",
	"tx-pipe-cpu-0x1a-packets",
	"tx-pipe-cpu-0x1b-bytes",
	"tx-pipe-cpu-0x1b-packets",
	"tx-pipe-cpu-0x1c-bytes",
	"tx-pipe-cpu-0x1c-packets",
	"tx-pipe-cpu-0x1d-bytes",
	"tx-pipe-cpu-0x1d-packets",
	"tx-pipe-cpu-0x1e-bytes",
	"tx-pipe-cpu-0x1e-packets",
	"tx-pipe-cpu-0x1f-bytes",
	"tx-pipe-cpu-0x1f-packets",
	"tx-pipe-cpu-0x20-bytes",
	"tx-pipe-cpu-0x20-packets",
	"tx-pipe-cpu-0x21-bytes",
	"tx-pipe-cpu-0x21-packets",
	"tx-pipe-cpu-0x22-bytes",
	"tx-pipe-cpu-0x22-packets",
	"tx-pipe-cpu-0x23-bytes",
	"tx-pipe-cpu-0x23-packets",
	"tx-pipe-cpu-0x24-bytes",
	"tx-pipe-cpu-0x24-packets",
	"tx-pipe-cpu-0x25-bytes",
	"tx-pipe-cpu-0x25-packets",
	"tx-pipe-cpu-0x26-bytes",
	"tx-pipe-cpu-0x26-packets",
	"tx-pipe-cpu-0x27-bytes",
	"tx-pipe-cpu-0x27-packets",
	"tx-pipe-cpu-0x28-bytes",
	"tx-pipe-cpu-0x28-packets",
	"tx-pipe-cpu-0x29-bytes",
	"tx-pipe-cpu-0x29-packets",
	"tx-pipe-cpu-0x2a-bytes",
	"tx-pipe-cpu-0x2a-packets",
	"tx-pipe-cpu-0x2b-bytes",
	"tx-pipe-cpu-0x2b-packets",
	"tx-pipe-cpu-0x2c-bytes",
	"tx-pipe-cpu-0x2c-packets",
	"tx-pipe-cpu-0x2d-bytes",
	"tx-pipe-cpu-0x2d-packets",
	"tx-pipe-cpu-0x2e-bytes",
	"tx-pipe-cpu-0x2e-packets",
	"tx-pipe-cpu-0x2f-bytes",
	"tx-pipe-cpu-0x2f-packets",
	"tx-pipe-cpu-0x4-bytes",
	"tx-pipe-cpu-0x4-packets",
	"tx-pipe-cpu-0x5-bytes",
	"tx-pipe-cpu-0x5-packets",
	"tx-pipe-cpu-0x6-bytes",
	"tx-pipe-cpu-0x6-packets",
	"tx-pipe-cpu-0x7-bytes",
	"tx-pipe-cpu-0x7-packets",
	"tx-pipe-cpu-0x8-bytes",
	"tx-pipe-cpu-0x8-packets",
	"tx-pipe-cpu-0x9-bytes",
	"tx-pipe-cpu-0x9-packets",
	"tx-pipe-cpu-0xa-bytes",
	"tx-pipe-cpu-0xa-packets",
	"tx-pipe-cpu-0xb-bytes",
	"tx-pipe-cpu-0xb-packets",
	"tx-pipe-cpu-0xc-bytes",
	"tx-pipe-cpu-0xc-packets",
	"tx-pipe-cpu-0xd-bytes",
	"tx-pipe-cpu-0xd-packets",
	"tx-pipe-cpu-0xe-bytes",
	"tx-pipe-cpu-0xe-packets",
	"tx-pipe-cpu-0xf-bytes",
	"tx-pipe-cpu-0xf-packets",
	"tx-pipe-cpu-error-bytes",
	"tx-pipe-cpu-error-packets",
	"tx-pipe-cpu-punt-1tag-bytes",
	"tx-pipe-cpu-punt-1tag-packets",
	"tx-pipe-cpu-punt-bytes",
	"tx-pipe-cpu-punt-packets",
	"tx-pipe-cpu-vlan-redirect-bytes",
	"tx-pipe-cpu-vlan-redirect-packets",
	"tx-pipe-debug-a",
	"tx-pipe-debug-b",
	"tx-pipe-ecn-errors",
	"tx-pipe-invalid-vlan-drops",
	"tx-pipe-ip4-unicast-aged-and-dropped-packets",
	"tx-pipe-ip4-unicast-packets",
	"tx-pipe-ip-length-check-drops",
	"tx-pipe-multicast-queue-cos0-bytes",
	"tx-pipe-multicast-queue-cos0-packets",
	"tx-pipe-multicast-queue-cos1-bytes",
	"tx-pipe-multicast-queue-cos1-packets",
	"tx-pipe-multicast-queue-cos2-bytes",
	"tx-pipe-multicast-queue-cos2-packets",
	"tx-pipe-multicast-queue-cos3-bytes",
	"tx-pipe-multicast-queue-cos3-packets",
	"tx-pipe-multicast-queue-cos4-bytes",
	"tx-pipe-multicast-queue-cos4-packets",
	"tx-pipe-multicast-queue-cos5-bytes",
	"tx-pipe-multicast-queue-cos5-packets",
	"tx-pipe-multicast-queue-cos6-bytes",
	"tx-pipe-multicast-queue-cos6-packets",
	"tx-pipe-multicast-queue-cos7-bytes",
	"tx-pipe-multicast-queue-cos7-packets",
	"tx-pipe-multicast-queue-qm-bytes",
	"tx-pipe-multicast-queue-qm-packets",
	"tx-pipe-multicast-queue-sc-bytes",
	"tx-pipe-multicast-queue-sc-packets",
	"tx-pipe-packet-aged-drops",
	"tx-pipe-packets-dropped",
	"tx-pipe-port-table-bytes",
	"tx-pipe-port-table-packets",
	"tx-pipe-purge-cell-error-drops",
	"tx-pipe-spanning-tree-state-not-forwarding-drops",
	"tx-pipe-trill-access-port-drops",
	"tx-pipe-trill-non-trill-drops",
	"tx-pipe-trill-packets",
	"tx-pipe-tunnel-error-packets",
	"tx-pipe-tunnel-packets",
	"tx-pipe-unicast-queue-cos0-bytes",
	"tx-pipe-unicast-queue-cos0-packets",
	"tx-pipe-unicast-queue-cos1-bytes",
	"tx-pipe-unicast-queue-cos1-packets",
	"tx-pipe-unicast-queue-cos2-bytes",
	"tx-pipe-unicast-queue-cos2-packets",
	"tx-pipe-unicast-queue-cos3-bytes",
	"tx-pipe-unicast-queue-cos3-packets",
	"tx-pipe-unicast-queue-cos4-bytes",
	"tx-pipe-unicast-queue-cos4-packets",
	"tx-pipe-unicast-queue-cos5-bytes",
	"tx-pipe-unicast-queue-cos5-packets",
	"tx-pipe-unicast-queue-cos6-bytes",
	"tx-pipe-unicast-queue-cos6-packets",
	"tx-pipe-unicast-queue-cos7-bytes",
	"tx-pipe-unicast-queue-cos7-packets",
	"tx-pipe-unicast-queue-qm-bytes",
	"tx-pipe-unicast-queue-qm-packets",
	"tx-pipe-unicast-queue-sc-bytes",
	"tx-pipe-unicast-queue-sc-packets",
	"tx-pipe-vlan-tagged-packets",
	NULL,
};
