/* uio_pci_generic - generic UIO driver for PCI 2.3 devices
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Since the driver does not declare any device ids, you must allocate
 * id and bind the device to the driver yourself.  For example:
 *
 * # echo "8086 10f5" > /sys/bus/pci/drivers/uio_pci_generic/new_id
 * # echo -n 0000:00:19.0 > /sys/bus/pci/drivers/e1000e/unbind
 * # echo -n 0000:00:19.0 > /sys/bus/pci/drivers/uio_pci_generic/bind
 * # ls -l /sys/bus/pci/devices/0000:00:19.0/driver
 * .../0000:00:19.0/driver -> ../../../bus/pci/drivers/uio_pci_generic
 *
 * Driver won't bind to devices which do not support the Interrupt Disable Bit
 * in the command register. All devices compliant to PCI 2.3 (circa 2002) and
 * all compliant PCI Express devices should support this bit.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/spinlock.h>

#include "extern.h"

#define DRIVER_VERSION	"0.01.0"
#define DRIVER_AUTHOR	"Michael S. Tsirkin <mst@redhat.com>"
#define DRIVER_DESC	"Generic UIO driver for PCI 2.3 devices"

typedef struct {
	u32 offset;
	u32 value;
} uio_pci_dma_interrupt_disable_regs_t;

struct uio_pci_generic_dev {
	struct uio_info info;
	struct pci_dev *pdev;
	volatile void * dev_regs;
	int is_msi;
	int n_regs;
#define MAX_DISABLE_REGS 8
	uio_pci_dma_interrupt_disable_regs_t disable_regs[MAX_DISABLE_REGS];
};

static inline struct uio_pci_generic_dev *
to_uio_pci_generic_dev(struct uio_info *info)
{
	return container_of(info, struct uio_pci_generic_dev, info);
}

/* Interrupt handler.  Disable interrupts user told us about.  That's it. */
static irqreturn_t irqhandler(int irq, struct uio_info *info)
{
	struct uio_pci_generic_dev *gdev = to_uio_pci_generic_dev(info);
	volatile void * regs = gdev->dev_regs;
	int i;
	for (i = 0; i < gdev->n_regs; i++)
		*(volatile u32 *) (regs + gdev->disable_regs[i].offset) = gdev->disable_regs[i].value;
	return IRQ_HANDLED;
}

static int open(struct uio_info *info, struct inode *inode)
{
	struct uio_pci_generic_dev *gdev = to_uio_pci_generic_dev(info);
	struct pci_dev *pdev = gdev->pdev;
	return uio_dma_device_open(&pdev->dev, iminor(inode));
}

static int release(struct uio_info *info, struct inode *inode)
{
	return uio_dma_device_close(iminor(inode));
}

static ssize_t show_disable_regs(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct uio_pci_generic_dev *gdev = pci_get_drvdata (pdev);
	int i, pos = 0;
	for (i = 0; i < gdev->n_regs; i++)
		pos += sprintf(buf+pos, "%x %x\n",
			       gdev->disable_regs[i].offset,
			       gdev->disable_regs[i].value);
	return pos;
}

static ssize_t store_disable_regs(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct uio_pci_generic_dev *gdev = pci_get_drvdata (pdev);
	int i, tmp[2];
	if (gdev->n_regs >= MAX_DISABLE_REGS)
		return -EINVAL;
	sscanf (buf, "%x %x", &tmp[0], &tmp[1]);
	for (i = 0; i < gdev->n_regs; i++) {
		if (tmp[0] == gdev->disable_regs[i].offset) {
			gdev->disable_regs[i].value = tmp[1];
			break;
		}
	}
	if (i >= gdev->n_regs) {
		i = gdev->n_regs++;
		gdev->disable_regs[i].offset = tmp[0];
		gdev->disable_regs[i].value = tmp[1];
	}
	return count;
}
static DEVICE_ATTR(disable_interrupt_regs, S_IRUGO|S_IWUSR|S_IWGRP, show_disable_regs, store_disable_regs);

static int disable_msi = 0, dma_bits = 32;
module_param(disable_msi, int, 0);
module_param(dma_bits, int, 0);

static int probe(struct pci_dev *pdev,
		 const struct pci_device_id *id)
{
	struct uio_pci_generic_dev *gdev;
	int err, is_msi;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "%s: pci_enable_device failed: %d\n",
			__func__, err);
		return err;
	}

	if (!pdev->irq) {
		dev_warn(&pdev->dev, "No IRQ assigned to device: "
			 "no support for interrupts?\n");
		pci_disable_device(pdev);
		return -ENODEV;
	}

	/* Use MSI if we can to avoid sharing interrupts. */
	is_msi = 0;
	err = 0;
#ifdef CONFIG_PCI_MSI
	if (! disable_msi) {
		err = pci_enable_msi (pdev);
		if (err) {
			dev_err(&pdev->dev, "%s: pci_enable_msi failed: %d\n", __func__, err);
		} else {
			is_msi = 1;
		}
	}
#endif

	/* Set PCI master since we'll be doing dma. */
	pci_set_master(pdev);

	if (dma_bits == 64
	    && !dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))
	    && !dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64)))
		;
	else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))
		 && !dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32)))
		;
	else {
		dev_err(&pdev->dev, "%s: failed to set dma mask\n", __func__);
		pci_disable_device(pdev);
		return -ENODEV;
	}

	gdev = kzalloc(sizeof(struct uio_pci_generic_dev), GFP_KERNEL);
	if (!gdev) {
		err = -ENOMEM;
		goto err_alloc;
	}

	gdev->info.name = "uio_pci_dma";
	gdev->info.version = DRIVER_VERSION;
	gdev->info.irq = pdev->irq;
	gdev->info.irq_flags = is_msi ? 0 : IRQF_SHARED;
	gdev->info.handler = irqhandler;
	gdev->info.open = open;
	gdev->info.release = release;
	gdev->pdev = pdev;
	gdev->is_msi = is_msi;

        {
                ulong l = pci_resource_len(pdev, 0);
                if (l > (8 << 20))
                        l = 8 << 20;
                gdev->dev_regs = ioremap(pci_resource_start(pdev, 0), l);
                if (! gdev->dev_regs) {
                        err = -ENOMEM;
                        goto err_ioremap;
                }
        }

	if ((err = uio_register_device(&pdev->dev, &gdev->info)))
		goto err_register;

	if ((err = device_create_file(&pdev->dev, &dev_attr_disable_interrupt_regs)))
		goto err_dev_create;

	pci_set_drvdata(pdev, gdev);
	return 0;

err_dev_create:
	uio_unregister_device(&gdev->info);
err_register:
	iounmap(gdev->dev_regs);
err_ioremap:
	kfree(gdev);
err_alloc:
	pci_disable_device(pdev);
	return err;
}

static void remove(struct pci_dev *pdev)
{
	struct uio_pci_generic_dev *gdev = pci_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_disable_interrupt_regs);
	iounmap(gdev->dev_regs);
	uio_unregister_device(&gdev->info);
	if (gdev->is_msi)
		pci_disable_msi(pdev);
	pci_disable_device(pdev);
	kfree(gdev);
}

static struct pci_driver driver = {
	.name = "uio_pci_dma",
	.id_table = NULL, /* only dynamic id's */
	.probe = probe,
	.remove = remove,
};

int uio_pci_init_module(void)
{
	int e = uio_dma_init_module ();
	if (e)
		return e;
	pr_info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
	return pci_register_driver(&driver);
}

void uio_pci_exit_module(void)
{
	uio_dma_exit_module ();
	pci_unregister_driver(&driver);
}
