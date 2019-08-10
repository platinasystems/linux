/**
 * Platina Systems MK1 top of rack switch platform driver.
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
#include <linux/rtnetlink.h>
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
	int _err = platina_mk1_egress(err);				\
	if (_err)							\
		pr_err(NAME ": %d, " format "\n", (_err), ##args);	\
	else if (platina_mk1_dynamic_debug)				\
		pr_debug(format "\n", ##args);				\
	else								\
		no_printk(KERN_DEBUG pr_fmt(format), ##args);		\
	(_err);								\
})

enum {
	platina_mk1_eeprom_pgsz = 32,
	platina_mk1_top_xid = 3999,
	platina_mk1_n_ports = 32,
	platina_mk1_n_rxqs = 1,
	platina_mk1_n_txqs = 1,
};

static int platina_mk1_provision[platina_mk1_n_ports];
static int platina_mk1_n_provision;
module_param_array_named(provision, platina_mk1_provision, int,
			 &platina_mk1_n_provision, 0644);
MODULE_PARM_DESC(provision, " 1, 2, or 4 subports per port, default 1");

static int __init platina_mk1_init(void);
static void __exit platina_mk1_exit(void);
module_init(platina_mk1_init);
module_exit(platina_mk1_exit);

static const unsigned short platina_mk1_eeprom_addrs[];
static struct i2c_board_info platina_mk1_eeprom_info;
static const struct property_entry platina_mk1_eeprom_properties[];
static const struct i2c_board_info platina_mk1_mezzanine_info;

static size_t platina_mk1_n_eeprom_addrs(void);

static struct {
	struct	platform_device	*pdev;
	struct	i2c_adapter	*adapter;
	struct	i2c_client	*mezzanine, *eeprom;
	u8	eeprom_cache[onie_max_data];
	void	*onie;
} *platina_mk1;

static const char *const platina_mk1_flag_names[] = {
	"copper",
	"fec74",
	"fec91",
	NULL,
};

static void platina_mk1_ethtool_port_cb(struct ethtool_link_ksettings *ks)
{
	ks->base.speed = 0;
	ks->base.duplex = DUPLEX_FULL;
	ks->base.autoneg = AUTONEG_ENABLE;
	ks->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported,
					     100000baseLR4_ER4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, TP);
	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void platina_mk1_ethtool_subport_cb(struct ethtool_link_ksettings *ks)
{
	ks->base.speed = 0;
	ks->base.duplex = DUPLEX_FULL;
	ks->base.autoneg = AUTONEG_ENABLE;
	ks->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, TP);
	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
}

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

static int platina_mk1_add_lowers(void)
{
	const char * const eth1_akas[] = {
		"eth1", "enp3s0f0", NULL
	};
	const char * const eth2_akas[] = {
		"eth2", "enp3s0f1", NULL
	};
	const char * const * const akas[] = {
		eth1_akas,
		eth2_akas,
		NULL,
	};
	struct net_device *nds[3] = { NULL, NULL, NULL };
	int ndi, akai, err;

	for (ndi = 0; akas[ndi]; ndi++)
		for (akai = 0; !nds[ndi] && akas[ndi][akai]; akai++)
			nds[ndi] = dev_get_by_name(&init_net, akas[ndi][akai]);

	if (nds[0] && nds[1]) {
		rtnl_lock();
		err = xeth_add_lowers(nds);
		rtnl_unlock();
	} else
		err = -ENODEV;

	for (ndi = 0; akas[ndi]; ndi++)
		if (nds[ndi])
			dev_put(nds[ndi]);
	return err;
}

static int platina_mk1_create_ports(void)
{
	ssize_t sz;
	u8 v[ETH_ALEN];
	u64 ea;
	u8 first_port;
	int port, subport;
	s64 err = 0;
	char name[IFNAMSIZ];

	sz = onie_value(platina_mk1->onie, onie_type_mac_base,
			ETH_ALEN, v);
	ea = (sz == ETH_ALEN) ? 4 + ether_addr_to_u64(v) : 0;
	sz = onie_value(platina_mk1->onie, onie_type_device_version,
			sizeof(u8), v);
	first_port = (sz == sizeof(u8) && v[0] == 0) ? 0 : 1;
	for (port = 0; err >= 0&& port < platina_mk1_n_ports; port++) {
		if (platina_mk1_provision[port] > 1) {
			for (subport = 0;
			     err >= 0 && subport < platina_mk1_provision[port];
			     subport++) {
				u32 o = platina_mk1_n_ports * subport;
				u32 xid = platina_mk1_top_xid - port - o;
				u64 pea = ea ? ea + port + o : 0;
				scnprintf(name, IFNAMSIZ, "xeth%d-%d",
					  port + first_port,
					  subport + first_port);
				err = xeth_create_port(name, xid, pea,
						       platina_mk1_flag_names,
						       platina_mk1_ethtool_subport_cb);
			}
		} else {
			u32 xid = platina_mk1_top_xid - port;
			u64 pea = ea ? ea + port : 0;
			scnprintf(name, IFNAMSIZ, "xeth%d", port + first_port);
			err = xeth_create_port(name, xid, pea,
					       platina_mk1_flag_names,
					       platina_mk1_ethtool_port_cb);
		}
	}
	return (err < 0) ? (int)err : 0;
}

static void platina_mk1_delete_uppers(void)
{
	int port, subport;

	for (port = 0; port < platina_mk1_n_ports; port++)
		if (platina_mk1_provision[port] > 1) {
			for (subport = 0;
			     subport < platina_mk1_provision[port];
			     subport++) {
				u32 o = platina_mk1_n_ports * subport;
				u32 xid = platina_mk1_top_xid - port - o;
				xeth_delete_port(xid);
			}
		} else
			xeth_delete_port(platina_mk1_top_xid - port);
}

static int platina_mk1_egress(int err)
{
	if (!platina_mk1)
		return err;
	platina_mk1_delete_uppers();
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
	int i, err;

	for (i = 0; i < ARRAY_SIZE(platina_mk1_provision); i++)
		if (i >= platina_mk1_n_provision) {
			platina_mk1_provision[i] = 1;
		} else {
			int n = platina_mk1_provision[i];
			if (n != 1 && n != 2 && n != 4)
				return -EINVAL;
		}

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
	err = platina_mk1_add_lowers();
	if (err)
		return platina_mk1_pr_egress(err, "can't add lower links");
	err = platina_mk1_create_ports();
	if (err)
		return platina_mk1_pr_egress(err, "could't create ports");
	return 0;
}

static void __exit platina_mk1_exit(void)
{
	platina_mk1_pr_egress(0, "done");
}

static const unsigned short platina_mk1_eeprom_addrs[] = { 0x53, 0x51 };

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
