/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/onie.h>

#define sample_onie_pr_debug(format, args...)				\
	pr_debug(format "\n", ##args);

#define sample_onie_pr_err(format, args...)				\
	pr_err("sample-onie:" format "\n", ##args);

static struct sample_onie_priv {
	struct {
		struct i2c_adapter *adapter;
		struct i2c_board_info info;
		struct i2c_client *client;
	} i2c;
	struct {
		struct task_struct *create;
		struct task_struct *write;
	} task;
	u8	data[onie_max_data];
	u8	update[onie_max_data];
	size_t	sz;
	void	*onie;
} *sample_onie_priv;

static int sample_onie_init(void);
static void sample_onie_exit(void);
static void sample_onie_egress(struct sample_onie_priv *);
static int sample_onie_create(void*);
static int sample_onie_write(void*,size_t);
static int sample_onie_write_task(void*);
static ssize_t sample_onie_eeprom_load(struct sample_onie_priv *);
static int sample_onie_eeprom_rewind(struct sample_onie_priv *);
static ssize_t sample_onie_eeprom_read(struct sample_onie_priv *,
				       size_t offset, size_t count);
static int sample_onie_delay(void);

static struct {
	uint	use_kthreads;
	uint	i2c_adapter;
	uint	eeprom_addr;
	uint	eeprom_delay;
	uint	eeprom_pagesize;
	uint	eeprom_stub;
	char	*eeprom_type;
} sample_onie_param = {
	.use_kthreads = 0,
	.i2c_adapter = 0,
	.eeprom_addr = 0x51,
	.eeprom_delay = 10,
	.eeprom_pagesize = 32,
	.eeprom_stub = 0,
	.eeprom_type = "24c04",
};

module_param_named(use_kthreads, sample_onie_param.use_kthreads, uint, 0644);
module_param_named(i2c_adapter, sample_onie_param.i2c_adapter, uint, 0644);
module_param_named(eeprom_stub, sample_onie_param.eeprom_stub, uint, 0644);
module_param_named(eeprom_addr, sample_onie_param.eeprom_addr, uint, 0644);
module_param_named(eeprom_delay, sample_onie_param.eeprom_delay, uint, 0644);
module_param_named(eeprom_pagesize, sample_onie_param.eeprom_pagesize, uint,
		   0644);
module_param_named(eeprom_type, sample_onie_param.eeprom_type, charp, 0644);

module_init(sample_onie_init);
module_exit(sample_onie_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("misc/eeprom/onie example");
MODULE_PARM_DESC(use_kthreads, " default 0 [no]");
MODULE_PARM_DESC(i2c_adapter, " default 0");
MODULE_PARM_DESC(eeprom_addr, " default 0x51");
MODULE_PARM_DESC(eeprom_delay, " default 10 [ms] before/after offset change");
MODULE_PARM_DESC(eeprom_pagesize, " default 32");
MODULE_PARM_DESC(eeprom_stub, " default 0 [no]");
MODULE_PARM_DESC(eeprom_type, " default 24c04");

static int __init sample_onie_init(void)
{
	struct sample_onie_priv *priv;
	int err;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto sample_onie_init_err;
	}
	if (!sample_onie_param.eeprom_stub) {
		priv->i2c.adapter =
			i2c_get_adapter(sample_onie_param.i2c_adapter);
		if (!priv->i2c.adapter) {
			err = -ENOENT;
			goto sample_onie_init_err;
		}
		strncpy(priv->i2c.info.type, sample_onie_param.eeprom_type,
		       I2C_NAME_SIZE);
		priv->i2c.info.addr = sample_onie_param.eeprom_addr;
		priv->i2c.client = i2c_new_device(priv->i2c.adapter,
						  &priv->i2c.info);
		if (!priv->i2c.client) {
			err = -ENODEV;
			goto sample_onie_init_err;
		}
	}
	if (!sample_onie_param.use_kthreads) {
		err = sample_onie_create(priv);
		if (err)
			goto sample_onie_init_err;
	} else
		priv->task.create = kthread_run(sample_onie_create, priv,
						"sample-onie-create");
	sample_onie_priv = priv;
	sample_onie_pr_debug("ok");
	return 0;
sample_onie_init_err:
	sample_onie_egress(priv);
	sample_onie_pr_err("init: %d", err);
	return err;
}

static void __exit sample_onie_exit(void)
{
	sample_onie_egress(sample_onie_priv);
	sample_onie_pr_debug("ok");
}

static void sample_onie_egress(struct sample_onie_priv *priv)
{
	if (!priv)
		return;
	if (sample_onie_param.use_kthreads) {
		if (priv->task.write)
			kthread_stop(priv->task.write);
		if (priv->task.create)
			kthread_stop(priv->task.create);
		do {} while (priv->task.write || priv->task.create);
	}
	if (priv->onie)
		onie_delete(priv->onie);
	if (priv->i2c.client)
		i2c_unregister_device(priv->i2c.client);
	if (priv->i2c.adapter)
		i2c_put_adapter(priv->i2c.adapter);
	priv->onie = NULL;
	priv->i2c.client = NULL;
	priv->i2c.adapter = NULL;
	sample_onie_priv = NULL;
	kfree(priv);
}

static int sample_onie_create(void *p)
{
	struct sample_onie_priv *priv = p;
	struct kobject *parent = kernel_kobj;
	int err;

	if (!sample_onie_param.eeprom_stub) {
		err = sample_onie_eeprom_load(p);
		if (err)
			return err;
		parent = &priv->i2c.client->dev.kobj;
	}
	priv->onie = onie_create(parent, priv->data, sample_onie_write);
	if (IS_ERR(priv->onie))
		err = PTR_ERR(priv->onie);
	priv->task.create = NULL;
	return err;
}

static int sample_onie_write(void *p, size_t sz)
{
	struct sample_onie_priv *priv =
		container_of(p, struct sample_onie_priv, data);

	if (!sample_onie_param.use_kthreads) {
		print_hex_dump_bytes("sample-onie:write: ", DUMP_PREFIX_NONE,
				     priv->data, sz);
		return 0;
	}
	if (priv->task.write)
		return -EBUSY;
	memcpy(priv->update, priv->data, sz);
	priv->sz = sz;
	priv->task.write = kthread_run(sample_onie_write_task, p,
					"sample-onie-write");
	return 0;
}

static int sample_onie_write_task(void *p)
{
	struct sample_onie_priv *priv =
		container_of(p, struct sample_onie_priv, data);

	print_hex_dump_bytes("sample-onie:write:task: ", DUMP_PREFIX_NONE,
			     priv->update, priv->sz);
	priv->task.write = NULL;
	return 0;
}

static ssize_t sample_onie_eeprom_load(struct sample_onie_priv *priv)
{
	int err, tries;
	ssize_t rem, sz;

	/* we retry b/c on some systems the EEPROM driver probe interferes w/
	 * the first read */
	for (tries = 0; tries < 3; tries++) {
		err = sample_onie_delay();
		if (err)
			return err;
		err = sample_onie_eeprom_rewind(priv);
		if (err)
			continue;
		err = sample_onie_delay();
		if (err)
			return err;
		sz = max_t(size_t, onie_min_data,
			   sample_onie_param.eeprom_pagesize);
		err = sample_onie_eeprom_read(priv, 0, sz);
		if (err < 0)
			return err;
		rem = onie_validate(priv->data, sz);
		if (rem < 0)
			continue;
		if (rem == 0)
			break;
		err = sample_onie_eeprom_read(priv, sz, rem);
		if (err < 0)
			return err;
		sz = onie_validate(priv->data, 0);
		if (sz > 0)
			return sz;
	}
	return 0;
}

static int sample_onie_eeprom_rewind(struct sample_onie_priv *priv)
{
	int err = i2c_smbus_write_byte_data(priv->i2c.client, 0, 0);
	if (err)
		sample_onie_pr_err("rewind: %d", err);
	return err;
}

static ssize_t sample_onie_eeprom_read(struct sample_onie_priv *priv,
				       size_t offset, size_t count)
{
	int i;
	s32 ret;

	for (i = 0; i < count; i++) {
		ret = i2c_smbus_read_byte(priv->i2c.client);
		if (ret < 0) {
			sample_onie_pr_err("read: %d", ret);
			return ret;
		}
		priv->data[offset+i] = ret;
	}
	return count;
}

static int sample_onie_delay(void)
{
	if (!sample_onie_param.eeprom_delay)
		return 0;
	if (sample_onie_param.use_kthreads) {
		msleep_interruptible(sample_onie_param.eeprom_delay);
		schedule();
		if (kthread_should_stop())
			return -EINTR;
	} else
		msleep(sample_onie_param.eeprom_delay);
	return 0;
}
