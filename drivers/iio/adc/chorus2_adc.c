/*
 * c2_adc.c
 * Driver for the Chorus2 ADC block.
 * Copyright (C) 2010,2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/semaphore.h>
#include <linux/mm.h>

#include <asm/soc-chorus2/c2_irqnums.h>
#include <asm/irq.h>
#include <asm/img_dma.h>

#include "chorus2_adc.h"

#define DRV_VERSION	"1.0"
#define DRV_NAME	"c2_adc"

/* Size of the buffer */
#define BUFFER_SIZE	(1 << 16)		/* 32kb */
#define MAX_BUF_OUT	(BUFFER_SIZE >> 1)	/* half BUFFER_SIZE */

struct c2_adc_priv {
	/* Device pointer */
	struct miscdevice	*dev;
	int			irq;

	/* User lock */
	spinlock_t		user_lock;
	struct semaphore	read_sem;

	/* User count & id */
	int			user_count;
	int			user_id;

	/* Current speed of ADC clock (8.192 - 24.576 MHz) */
	int			adc_clk_speed;

	/* DMA buffer */
	int			chan_out;
	void			*buf_virt;
	dma_addr_t		buf_phys;
	unsigned int		*dma_desc;
	dma_addr_t		dma_desc_phys;

	/* Double buffers */
	int			buffer_number;
	void			*buf1;
	void			*buf2;
	void			*current_buffer;
	spinlock_t		buffer_lock;

	/* Register block */
	unsigned int		*reg_base;
};

static struct c2_adc_priv c2_adc_dev;

/* Reset the clocks  */
static void c2_adc_reset(void)
{
	unsigned int flags;

	/* Bring ADC/SCP to known initial values */
	TBI_LOCK(flags);

	/* Put the SCP into reset */
	scp_write(SCP_CTRL_RESETN, CONTROL);

	/* Turn off digital ADC clock */
	writel(DCXO_CLK_ADC_DISABLE, DCXO_CLK_ENABLE);

	/* Set ADC Clock to 8.192MHz */
	writel(ADC_CLK_8192M, ADC_CLK_SEL_ADDR);

	/* Enable digital ADC clock for SCP */
	writel(DCXO_CLK_ADC_ENABLE, DCXO_CLK_ENABLE);

	/* Set SCP input clock to use clock gen */
	writel(USE_ON_CHIP_IF_CLK, SCP_IF_PIN_CTRL);

	/* Initialise the SCP to a known state (out of reset) */
	scp_write(SCP_CTRL_BYPASS | SCP_CTRL_DMA_SYNC_EN | SCP_CTRL_PWR_ON,
			CONTROL);

	TBI_UNLOCK(flags);
}

/* Initialise the DMA block */
static void c2_setup_dma(void)
{
	unsigned int *c2_adc_dma_desc = c2_adc_dev.dma_desc;

	/* Setup the dma list descriptor */
	c2_adc_dma_desc[0] = 1 << 30;
	c2_adc_dma_desc[1] = (1 << 30) | ((MAX_BUF_OUT >> 2) & 0xffff);
	c2_adc_dma_desc[2] = SCP_OUTPUT_ADDR >> 2;
	c2_adc_dma_desc[3] = 4 << 26;
	c2_adc_dma_desc[4] = 0;
	c2_adc_dma_desc[5] = 0;
	c2_adc_dma_desc[6] = c2_adc_dev.buf_phys;
	c2_adc_dma_desc[7] = c2_adc_dev.dma_desc_phys;

	/* Setup the DMA to be ready to get going */
	img_dma_set_list_addr(c2_adc_dev.chan_out, c2_adc_dev.dma_desc_phys);
	wmb();
}

/*
 * Open the device.
 * This will only allow one file descriptor to be open for this device.
 */
static int c2_adc_open(struct inode *i, struct file *f)
{
	int ret = 0;

	/* Single-user exclusivity */
	spin_lock(&(c2_adc_dev.user_lock));

	/* Allow user, an 'su' user and root to access the device */
	if (c2_adc_dev.user_count &&
			(c2_adc_dev.user_id != current->real_cred->uid) &&
			(c2_adc_dev.user_id != current->real_cred->euid) &&
			!capable(CAP_DAC_OVERRIDE)) {
		ret = -EBUSY;
		goto done;
	}

	/* Ensure we have read access */
	if ((f->f_flags & O_ACCMODE) == O_WRONLY) {
		ret = -EINVAL;
		goto done;
	}

	if (!c2_adc_dev.user_count) {
		c2_adc_dev.user_id = current->real_cred->uid;
		spin_unlock(&c2_adc_dev.user_lock);

		memset(c2_adc_dev.buf1, 0, MAX_BUF_OUT);
		memset(c2_adc_dev.buf2, 0, MAX_BUF_OUT);
		img_dma_start_list(c2_adc_dev.chan_out);

		spin_lock(&c2_adc_dev.user_lock);
	}

	c2_adc_dev.user_count++;
done:
	spin_unlock(&c2_adc_dev.user_lock);

	return ret;
}

/*
 * Release the device.
 */
static int c2_adc_release(struct inode *i, struct file *f)
{
	spin_lock(&(c2_adc_dev.user_lock));

	c2_adc_dev.user_count--;
	if (!c2_adc_dev.user_count) {
		spin_unlock(&c2_adc_dev.user_lock);
		img_dma_stop_list(c2_adc_dev.chan_out);
		img_dma_reset(c2_adc_dev.chan_out);
		c2_setup_dma();
		goto done;
	}

	spin_unlock(&c2_adc_dev.user_lock);
done:
	return 0;
}

/*
 * Read data from the device
 */
static ssize_t c2_adc_read(struct file *f, char __user *buf,
	size_t count, loff_t *off)
{
	int ret = 0;
	unsigned int *p = NULL;

	if (down_interruptible(&(c2_adc_dev.read_sem)))
		return -ERESTARTSYS;
	if (*off >= MAX_BUF_OUT)
		*off -= MAX_BUF_OUT;
	if (*off + count > MAX_BUF_OUT)
		count = MAX_BUF_OUT - *off;

	spin_lock(&c2_adc_dev.buffer_lock);
	p = c2_adc_dev.current_buffer + *off;
	spin_unlock(&c2_adc_dev.buffer_lock);

	if (copy_to_user(buf, p, count)) {
		return -EFAULT;
		goto out;
	}
	*off += count;
	ret = count;
out:
	up(&(c2_adc_dev.read_sem));
	return ret;
}

static const struct file_operations adc_fops = {
	.owner		= THIS_MODULE,
	.open		= c2_adc_open,
	.release	= c2_adc_release,
	.read		= c2_adc_read,
};

static struct miscdevice c2_adc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "adc",
	.fops		= &adc_fops,
};

/* IRQ */
static irqreturn_t c2_adc_irq(int irq, void *data)
{
	unsigned int status = 0;

	img_dma_get_int_status(c2_adc_dev.chan_out, &status);

	if (status & 0x20000)
		memcpy(c2_adc_dev.current_buffer, c2_adc_dev.buf_virt,
				MAX_BUF_OUT);

	spin_lock(&(c2_adc_dev.buffer_lock));
	/* Switch buffer to DMA into */
	c2_adc_dev.buffer_number = (c2_adc_dev.buffer_number + 1) & 0x01;
	c2_adc_dev.current_buffer = (c2_adc_dev.buffer_number) ?
			c2_adc_dev.buf1 : c2_adc_dev.buf2;

	spin_unlock(&(c2_adc_dev.buffer_lock));

	img_dma_set_int_status(c2_adc_dev.chan_out, 0);
	return IRQ_HANDLED;
}

/* Module initialisation */
static int c2_adc_probe(void)
{
	int ret = 0;

	pr_info("%s: Chorus2 Analogue-to-Digital Converter driver V%s\n",
			DRV_NAME, DRV_VERSION);

	/* Remap the peripheral memory */
	c2_adc_dev.reg_base = ioremap(SCP_BASE, 0x0fff);
	if (!c2_adc_dev.reg_base) {
		pr_err("%s: Unable to remap IO registers, aborting\n",
				DRV_NAME);
		ret = -ENXIO;
		goto done_err;
	}

	/* Initial device reset */
	c2_adc_reset();

	/* Initialise the open spinlock */
	spin_lock_init(&(c2_adc_dev.user_lock));
	spin_lock_init(&c2_adc_dev.buffer_lock);
	sema_init(&(c2_adc_dev.read_sem), 1);

	ret = misc_register(&c2_adc_device);
	if (ret) {
		pr_err("%s: Unable to register device node, aborting\n",
				DRV_NAME);
		goto done_unmap;
	}
	c2_adc_dev.dev = &c2_adc_device;

	/* Set coherent DMA mask */
	ret = dma_set_coherent_mask(c2_adc_device.this_device, 0xffffffff);
	if (ret) {
		pr_err("%s: Unable to initialise DMA, aborting\n", DRV_NAME);
		goto done_unreg;
	}

	/* We can now do stuff with our device - first, allocate DMA out */
	ret = img_request_dma(11, SCP_DMA_OUT_PERIPH);
	if (ret < 0) {
		pr_err("%s: Unable to allocate outbound DMA channel, aborting\n",
				DRV_NAME);
		goto done_unreg;
	}
	c2_adc_dev.chan_out = ret;
	img_dma_reset(c2_adc_dev.chan_out);

	/* Allocate the buffer */
	c2_adc_dev.buf_virt = dma_alloc_coherent(c2_adc_device.this_device,
			BUFFER_SIZE, &(c2_adc_dev.buf_phys), GFP_KERNEL);
	if (!c2_adc_dev.buf_virt) {
		pr_err("%s: Unable to allocate outbound DMA buffer, aborting\n",
				DRV_NAME);
		ret = -ENOMEM;
		goto done_free_dma;
	}

	c2_adc_dev.dma_desc = dma_alloc_coherent(c2_adc_device.this_device,
			8 * sizeof(unsigned int),
			&c2_adc_dev.dma_desc_phys, GFP_KERNEL);

	if (!c2_adc_dev.dma_desc) {
		pr_err("%s: Unable to allocate DMA descriptor buffer, aborting\n",
				DRV_NAME);
		ret = -ENOMEM;
		goto done_free_coherent1;
	}

	c2_setup_dma();

	/* Setup the bounce buffers */
	c2_adc_dev.buf1 = kzalloc(MAX_BUF_OUT, GFP_KERNEL);
	if (!c2_adc_dev.buf1) {
		pr_err("%s: Unable to allocate primary buffer, aborting\n",
				DRV_NAME);
		ret = -ENOMEM;
		goto done_free_coherent2;
	}
	c2_adc_dev.buf2 = kzalloc(MAX_BUF_OUT, GFP_KERNEL);
	if (!c2_adc_dev.buf2) {
		pr_err("%s: Unable to allocate secondary buffer, aborting\n",
				DRV_NAME);
		ret = -ENOMEM;
		goto done_free_buffer;
	}

	/*
	 * Initialise bounce pointer - due to the way the DMAC works, this
	 * should be the 2nd buffer (interrupts trigger BEFORE the list
	 * element has been processed)
	 */
	c2_adc_dev.current_buffer = c2_adc_dev.buf1;
	c2_adc_dev.irq = img_dma_get_irq(c2_adc_dev.chan_out);
	if (c2_adc_dev.irq < 0) {
		pr_err("%s: Unable to get DMA IRQ\n", DRV_NAME);
		ret = c2_adc_dev.irq;
		goto done_free_buffer;
	}

	/* Setup the IRQ */
	ret = request_irq(c2_adc_dev.irq, c2_adc_irq, 0, "c2_adc", &c2_adc_dev);
	if (ret) {
		pr_err("%s: Unable to allocate IRQ, aborting\n", DRV_NAME);
		ret = -EBUSY;
		goto done_free_buffer;
	}

	/* We're done now :) */
	pr_info("%s: Device allocated at /dev/adc\n", DRV_NAME);
	goto done;

done_free_buffer:
	kfree(c2_adc_dev.buf2);
	kfree(c2_adc_dev.buf1);
done_free_coherent2:
	dma_free_coherent(c2_adc_device.this_device, 8,
						c2_adc_dev.dma_desc,
						c2_adc_dev.dma_desc_phys);
done_free_coherent1:
	dma_free_coherent(c2_adc_device.this_device, BUFFER_SIZE,
		c2_adc_dev.buf_virt, c2_adc_dev.buf_phys);

done_free_dma:
	img_free_dma(c2_adc_dev.chan_out);
done_unreg:
	misc_deregister(&c2_adc_device);
done_unmap:
	iounmap(c2_adc_dev.reg_base);
done_err:
	pr_err("%s: Error code: %d", DRV_NAME, ret);
done:
	return ret;
}
module_init(c2_adc_probe);

static void c2_adc_remove(void)
{
	free_irq(c2_adc_dev.irq, &c2_adc_dev);
	kfree(c2_adc_dev.buf2);
	kfree(c2_adc_dev.buf1);
	dma_free_coherent(c2_adc_device.this_device, 8,
						c2_adc_dev.dma_desc,
						c2_adc_dev.dma_desc_phys);

	dma_free_coherent(c2_adc_device.this_device, BUFFER_SIZE,
		c2_adc_dev.buf_virt, c2_adc_dev.buf_phys);

	img_free_dma(c2_adc_dev.chan_out);
	misc_deregister(&c2_adc_device);
	iounmap(c2_adc_dev.reg_base);
	return;
}
module_exit(c2_adc_remove);

MODULE_DESCRIPTION("Chorus2 ADC driver");
MODULE_AUTHOR("Imagination Technologies");
MODULE_LICENSE("GPL");
