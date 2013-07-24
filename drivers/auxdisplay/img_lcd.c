/*
 * IMG LCD controller driver.
 *
 * Copyright (C) 2006,2007,2008,2009,2010,2012 Imagination Technologies Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/img_mdc_dma.h>

#include <linux/img_lcd.h>

#define LCD_REG0		0x0
#define LCD_REG1		0x4
#define LCD_REG2		0x8
#define LCD_REG3		0xC
#define LCD_ISTAT_REG		0x10
#define LCD_IENABLE_REG		0x14
#define LCD_ICLEAR_REG		0x18
#define LCD_DMA_STATUS_REG	0x1C
#define LCD_DMA_ENABLE_REG	0x20
#define LCD_DMA_CLEAR_REG	0x24
#define LCD_DMA_READ_REG	0x28
#define LCD_DMA_SIZE_REG	0x2C

#define REG0_L_H_DELAY_SHIFT	28
#define REG0_P_H_WIDTH_SHIFT	24
#define REG0_P_H_DELAY_SHIFT	20
#define REG0_D_DELAY_SHIFT	16
#define REG0_D_PERIOD_SHIFT	0

#define REG1_T_DIV_SHIFT	28
#define REG1_L_TRIGGER_SHIFT	16
#define REG1_F_H_WIDTH_SHIFT	8
#define REG1_F_H_DELAY_SHIFT	4
#define REG1_L_H_WIDTH_SHIFT	0

#define REG2_CMD		0x00000000
#define REG2_DATA		0x80000000
#define REG2_MODE_ALPH		0x00000000
#define REG2_MODE_GRAPH		0x40000000
#define REG2_MODE_BIT		0x20000000
#define REG2_MODE_BYTE		0x10000000
#define REG2_MODE_NIBBLE	0x00000000
#define REG2_DB_WIDTH_SHIFT	28
#define REG2_ORDER		0x08000000
#define REG2_L_PER_F_SHIFT	16
#define REG2_M_DELAY_SHIFT	12
#define REG2_D_PER_L_SHIFT	0

#define REG3_BUSY		4

#define DMA_INT_EMPTY		1
#define ALL_OP_FIN		0x4

#define MAX_BLOCK_SIZE		PAGE_SIZE

#define IDLE_TIMEOUT		2

enum etimings {
	data,
	instruction,
};

static struct alpha_speed_struct cmd_speed = {
	/* Default works for an 8bit KS0066U 2x16 display */
	13517,			/* d_period */
	3,			/* p_h_width */
	0,			/* p_h_delay */
	2			/* t_div */
};

static struct alpha_speed_struct data_speed = {
	/* Default works for an 8bit KS0066U 2x16 display */
	375,			/* d_period */
	3,			/* p_h_width */
	0,			/* p_h_delay */
	2			/* t_div */
};

struct alpha_device {
	struct mutex mutex;
	int irq;
	struct dma_chan *dma_channel;
	void *buf;
	dma_addr_t dmabuf;
	struct completion dma_complete;
	/* Default to sending LSB first */
	unsigned int msb_choice;
	/* Default to talking in bytes.  Can be modified by ioctl */
	unsigned int width_bits;
	void __iomem *regs_base;
	void (*enable_cs)(void);
	void (*disable_cs)(void);
	struct device *dev;
};

static struct alpha_device alpha_device;

static void program_timing(struct alpha_speed_struct *timing)
{
	unsigned int v;

	v = timing->t_div << REG1_T_DIV_SHIFT;

	writel(v, alpha_device.regs_base + LCD_REG1);

	v = timing->p_h_delay << REG0_P_H_DELAY_SHIFT;
	v |= timing->p_h_width << REG0_P_H_WIDTH_SHIFT;
	v |= timing->d_period << REG0_D_PERIOD_SHIFT;

	writel(v, alpha_device.regs_base + LCD_REG0);
}

static void instr_timing(void)
{
	program_timing(&cmd_speed);
}

static void data_timing(void)
{
	program_timing(&data_speed);
}

static int set_width(struct alpha_width_struct __user *addr)
{
	struct alpha_width_struct s;

	if (copy_from_user(&s, addr, sizeof(s)))
		return -EFAULT;

	/* Set the mask here, which is then used in all 'chats' to reg2 */
	switch (s.width) {
	case 1:
		alpha_device.width_bits = REG2_MODE_BIT;
		break;
	case 4:
		alpha_device.width_bits = REG2_MODE_NIBBLE;
		break;
	case 8:
		alpha_device.width_bits = REG2_MODE_BYTE;
		break;
	default:
		return -ERANGE;
	}

	if (s.msb)
		alpha_device.msb_choice = REG2_ORDER;
	else
		alpha_device.msb_choice = 0;

	return 0;
}

static int validate_speed_struct(struct alpha_speed_struct __user *addr,
				 struct alpha_speed_struct *s)
{
	if (copy_from_user(s, addr, sizeof(*s)))
		return -EFAULT;

	/* Do some verification according to the rules on the last page of
	 * the LCD C2 module TRM.
	 */
	if (s->p_h_width + s->p_h_delay >= s->d_period) {
		dev_warn(alpha_device.dev,
		       "alpha data timing breaks d_period rule\n");
		return -EINVAL;
	}

	dev_dbg(alpha_device.dev, "d_period %d\n", s->d_period);
	dev_dbg(alpha_device.dev, "p_h_width %d\n", s->p_h_width);
	dev_dbg(alpha_device.dev, "p_h_delay %d\n", s->p_h_delay);
	dev_dbg(alpha_device.dev, "t_div %d\n", s->t_div);

	return 0;
}

static int set_data_speed(struct alpha_speed_struct __user *addr)
{
	int ret;
	struct alpha_speed_struct s;

	ret = validate_speed_struct(addr, &s);

	if (!ret)
		data_speed = s;

	return ret;
}


static int set_cmd_speed(struct alpha_speed_struct __user *addr)
{
	int ret;
	struct alpha_speed_struct s;

	ret = validate_speed_struct(addr, &s);

	if (!ret)
		cmd_speed = s;

	return ret;
}

static int wait_for_idle(void __iomem *regs_base)
{
	unsigned long start_time = jiffies;

	while (!time_after_eq(jiffies, start_time + IDLE_TIMEOUT)) {
		unsigned int status = readl(regs_base + LCD_REG3);
		if (!(status & REG3_BUSY))
			return 0;
	}
	return 1;
}

static int send_byte(unsigned char c, enum etimings type)
{
	void __iomem *regs_base = alpha_device.regs_base;
	unsigned int v;

	/* Check for busy and tell the user */
	if (wait_for_idle(regs_base))
		return -EIO;

	v = alpha_device.msb_choice | alpha_device.width_bits |	REG2_MODE_ALPH;

	init_completion(&alpha_device.dma_complete);

	/* Put the byte in the FIFO */
	writel(1, regs_base + LCD_DMA_SIZE_REG);
	writel(c, regs_base + LCD_DMA_READ_REG);

	/* Enable all operation complete interrupt*/
	writel(ALL_OP_FIN, regs_base + LCD_ICLEAR_REG); /*clear first*/
	writel(ALL_OP_FIN, regs_base + LCD_IENABLE_REG);

	/* Setup the clocks and kick off the transfer */
	if (type == data) {
		data_timing();
		v |= REG2_DATA;
	} else {
		instr_timing();
		v |= REG2_CMD;
	}
	if (alpha_device.enable_cs)
		alpha_device.enable_cs();

	writel(v, regs_base + LCD_REG2);

	if (wait_for_completion_interruptible(&alpha_device.dma_complete))
		return -ERESTARTSYS;

	return 0;
}

#define BURST_SIZE 4

static int
send_block(struct alpha_data_block __user *addr, enum etimings timings)
{
	void __iomem *regs_base = alpha_device.regs_base;
	struct dma_chan *dma_channel = alpha_device.dma_channel;
	struct alpha_data_block block;
	unsigned int v;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t txcookie;
	struct dma_slave_config conf;

	if (copy_from_user(&block, addr, sizeof(block)))
		return -EFAULT;

	if (!block.len)
		return -EINVAL;

	if (block.len > MAX_BLOCK_SIZE)
		return -EINVAL;

	/* Check for busy and tell the user */
	if (wait_for_idle(regs_base))
		return -EIO;

	if (copy_from_user(alpha_device.buf, block.p, block.len))
		return -EFAULT;

	init_completion(&alpha_device.dma_complete);

	conf.direction = DMA_MEM_TO_DEV;
	conf.dst_addr = (unsigned int)regs_base + LCD_DMA_READ_REG;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	conf.dst_maxburst = BURST_SIZE;

	dmaengine_slave_config(dma_channel, &conf);
	desc = dmaengine_prep_slave_single(dma_channel, alpha_device.dmabuf,
					   block.len, DMA_MEM_TO_DEV,
					   DMA_PREP_INTERRUPT|DMA_CTRL_ACK);

	if (!desc)
		return -ENOMEM;

	if (timings == data)
		data_timing();
	else
		instr_timing();

	writel(block.len, regs_base + LCD_DMA_SIZE_REG);

	/*Enable all operation complete interrupt*/
	writel(ALL_OP_FIN, regs_base + LCD_ICLEAR_REG); /*clear first*/
	writel(ALL_OP_FIN, regs_base + LCD_IENABLE_REG);

	txcookie = dmaengine_submit(desc);
	dma_async_issue_pending(dma_channel);

	/* Kick off the transfer */
	if (timings == data)
		v = alpha_device.msb_choice | REG2_DATA | REG2_MODE_ALPH;
	else
		v = alpha_device.msb_choice | REG2_CMD | REG2_MODE_ALPH;

	v |= alpha_device.width_bits;

	if (alpha_device.enable_cs)
		alpha_device.enable_cs();

	writel(v, regs_base + LCD_REG2);

	if (wait_for_completion_interruptible(&alpha_device.dma_complete))
		return -ERESTARTSYS;

	return 0;
}

static irqreturn_t alpha_dma_irq(int irq, void *dev_id)
{
	void __iomem *regs_base = dev_id;

	writel(0, regs_base + LCD_IENABLE_REG);
	writel(ALL_OP_FIN, regs_base + LCD_ICLEAR_REG);

#ifdef CONFIG_SOC_TZ1090
	{
		u32 rem = readl(regs_base + LCD_DMA_SIZE_REG);

		/*
		 * Check that this is not a spurious interrupt - this can
		 * occur due to a h/w problem whereby FIFO underrun causes
		 * 'Alpha op finished' interrupt
		 */
		if (rem)
			return IRQ_HANDLED;
	}
#endif

	if (alpha_device.disable_cs)
		alpha_device.disable_cs();

	complete(&alpha_device.dma_complete);

	return IRQ_HANDLED;
}

static long alpha_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long result;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != ALPHA_IOCTL)
		return -ENOTTY;

	/* Single ioctl at a time please!
	 */
	if (mutex_lock_interruptible(&alpha_device.mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case ALPHA_INSTR:
		pr_debug(" instr %#x\n", (char)arg);
		result = send_byte((char)arg, instruction);
		pr_debug(" instr done\n");
		break;

	case ALPHA_DATA:
		pr_debug(" data %#x\n", (char)arg);
		result = send_byte((char)arg, data);
		pr_debug(" data done\n");
		break;

	case ALPHA_DATA_BLOCK:
		pr_debug(" data block %#lx\n", arg);
		result = send_block((struct alpha_data_block __user *)arg, data);
		pr_debug(" data block done\n");
		break;

	case ALPHA_WIDTH:
		pr_debug(" width\n");
		result = set_width((struct alpha_width_struct __user *)arg);
		pr_debug(" width done\n");
		break;

	case ALPHA_DATA_SPEED:
		pr_debug(" data speed\n");
		result = set_data_speed((struct alpha_speed_struct __user *)arg);
		pr_debug(" data speed done\n");
		break;

	case ALPHA_COMMAND_SPEED:
		pr_debug(" cmd speed\n");
		result = set_cmd_speed((struct alpha_speed_struct __user *)arg);
		pr_debug(" cmd speed done\n");
		break;

	case ALPHA_COMMAND_BLOCK:
		pr_debug(" cmd block");
		result = send_block((struct alpha_data_block __user *)arg, instruction);
		pr_debug(" cmd block done\n");
		break;

	default:
		result = -EINVAL;
	}

	mutex_unlock(&alpha_device.mutex);

	return result;
}

static const struct file_operations alpha_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= alpha_ioctl,
};

static struct miscdevice alpha_misc_device = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = "alpha_lcd",
	.fops           = &alpha_fops,
};

static int __init img_lcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *irq_resource, *mem_resource, *dma_resource;
	struct img_lcd_board *pdata;
	struct mdc_dma_cookie *cookie;
	dma_cap_mask_t mask;
	int result;

	mem_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_resource) {
		dev_err(dev, "mem resource not defined\n");
		result = -ENODEV;
		goto err_resource;
	}

	irq_resource = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_resource) {
		dev_err(dev, "irq resource not defined\n");
		result = -ENODEV;
		goto err_resource;
	}

	dma_resource = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dma_resource) {
		dev_err(dev, "dma resource not defined\n");
		result = -ENODEV;
		goto err_resource;
	}

	mutex_init(&alpha_device.mutex);

	alpha_device.buf = dma_alloc_coherent(dev, MAX_BLOCK_SIZE,
					      &alpha_device.dmabuf, GFP_KERNEL);

	if (alpha_device.buf == NULL) {
		dev_err(&pdev->dev, "failed to allocate DMA buffer\n");
		result = -ENOMEM;
		goto err_resource;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie) {
		result = -ENOMEM;
		goto err_free_dma_buffer;
	}

	cookie->periph = dma_resource->start;
	cookie->req_channel = -1;

	alpha_device.dma_channel = dma_request_channel(mask,
						       &mdc_dma_filter_fn,
						       cookie);
	if (!alpha_device.dma_channel) {
		dev_err(&pdev->dev, "failed to get DMA channel\n");
		result = -EBUSY;
		goto err_free_cookie;
	}

	alpha_device.regs_base = (void __iomem *)mem_resource->start;

	alpha_device.irq = irq_resource->start;

	if (request_irq(alpha_device.irq, alpha_dma_irq, 0, "img-lcd",
			alpha_device.regs_base)) {
		dev_err(&pdev->dev, "failed to get irq\n");
		result = -EBUSY;
		goto err_free_channel;
	}

	alpha_device.width_bits = REG2_MODE_BYTE;
	alpha_device.dev = dev;

	pdata = (struct img_lcd_board *)pdev->dev.platform_data;
	if (pdata) {
		alpha_device.enable_cs = pdata->enable_cs;
		alpha_device.disable_cs = pdata->disable_cs;
	}

	result = misc_register(&alpha_misc_device);

	if (result) {
		dev_err(dev, "misc_register failed\n");
		goto err_free_irq;
	}

	dev_info(dev, "probed successfully\n");

	kfree(cookie);

	return 0;

err_free_irq:
	free_irq(alpha_device.irq, NULL);
err_free_dma_buffer:
	dma_free_coherent(dev, MAX_BLOCK_SIZE, alpha_device.buf,
			  alpha_device.dmabuf);
err_free_channel:
	dma_release_channel(alpha_device.dma_channel);
err_free_cookie:
	kfree(cookie);
err_resource:
	return result;
}

static int __exit img_lcd_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	misc_deregister(&alpha_misc_device);
	free_irq(alpha_device.irq, NULL);
	dma_release_channel(alpha_device.dma_channel);
	dma_free_coherent(dev, MAX_BLOCK_SIZE, alpha_device.buf,
			  alpha_device.dmabuf);
	return 0;
}

#ifdef CONFIG_PM
static int img_lcd_suspend(struct platform_device *pdev,
			       pm_message_t state)
{
	/* FIXME Can we do anything here to power down? */
	return 0;
}

static int img_lcd_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define img_lcd_suspend NULL
#define img_lcd_resume NULL
#endif				/* CONFIG_PM */

MODULE_ALIAS("platform:img-lcd");	/* for platform bus hotplug */
static struct platform_driver img_lcd_driver = {
	.driver	= {
		.name	= "img-lcd",
		.owner	= THIS_MODULE,
	},
	.suspend	= img_lcd_suspend,
	.resume		= img_lcd_resume,
	.remove		= __exit_p(img_lcd_remove),
};

static int __init img_lcd_init(void)
{
	return platform_driver_probe(&img_lcd_driver, img_lcd_probe);
}
module_init(img_lcd_init);

static void __exit img_lcd_exit(void)
{
	platform_driver_unregister(&img_lcd_driver);
}
module_exit(img_lcd_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("IMG LCD controller");
MODULE_LICENSE("GPL");
