/*
 * PowerVR SGX 2D block driver.
 * SGX2D driver to allow writing to slave port.
 *
 * Copyright (C) 2010  Imagination Technologies
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/mman.h>
#include <linux/clk.h>
#include <linux/sgx2d.h>
#include <linux/slab.h>

#define NUM_REGIONS 2

#define SGX2D_DEV_BUSY 0

struct sgx2d_dev {
	struct sgx2d_pdata *pdata;
	struct miscdevice misc;
	struct cdev cdev;
	unsigned int irq;
	struct clk *twod_clk;
	unsigned long flags;	/* SGX2D_DEV_* */
	void __iomem *mem_io;
	unsigned long mmap_phys;
	unsigned int mmap_len;
	/* 0: slave port, 1: reg base */
	unsigned int mmap_offset[NUM_REGIONS];

	struct sgx2d_pdata_reg *reg_idle;
	struct sgx2d_pdata_reg *reg_srst;
	struct sgx2d_pdata_reg *reg_base;

	wait_queue_head_t idleq;

	/* suspend state */
	u32 suspend_base;
};

static u32 sgx2d_read_reg(struct sgx2d_dev *dev, struct sgx2d_pdata_reg *reg)
{
	return readl(dev->mem_io + dev->mmap_offset[reg->region] +
			reg->reg.offset);
}

static void sgx2d_write_reg(struct sgx2d_dev *dev, struct sgx2d_pdata_reg *reg,
			    u32 val)
{
	writel(val, dev->mem_io + dev->mmap_offset[reg->region] +
			reg->reg.offset);
}

static u32 sgx2d_read_field(struct sgx2d_dev *dev, struct sgx2d_pdata_reg *reg)
{
	u32 val = sgx2d_read_reg(dev, reg);
	return (val & reg->reg.mask) >> reg->reg.shift;
}

static int sgx2d_idle(struct sgx2d_dev *dev)
{
	return sgx2d_read_field(dev, dev->reg_idle);
}

static int sgx2d_soft_reset(struct sgx2d_dev *dev)
{
	int lstat;
	u32 val;
	struct sgx2d_pdata_reg *reg = dev->reg_srst;

	TBI_LOCK(lstat);
	val = sgx2d_read_reg(dev, reg);
	sgx2d_write_reg(dev, reg, val | reg->reg.mask);
	sgx2d_write_reg(dev, reg, val & ~reg->reg.mask);
	TBI_UNLOCK(lstat);

	wake_up_interruptible(&dev->idleq);

	return 0;
}

static irqreturn_t sgx2d_isr(int irq, void *dev_id)
{
	struct sgx2d_dev *dev = dev_id;

	/* Wake up any processes waiting for idleness */
	if (sgx2d_idle(dev))
		wake_up_interruptible(&dev->idleq);

	return IRQ_HANDLED;
}

static int sgx2d_wait_idle(struct sgx2d_dev *dev, struct file *filp)
{
	while (!sgx2d_idle(dev)) {
		DEFINE_WAIT(wait);

		if (filp && (filp->f_flags & O_NONBLOCK))
			return -EAGAIN;

		prepare_to_wait(&dev->idleq, &wait, TASK_INTERRUPTIBLE);
		if (!sgx2d_idle(dev))
			schedule();
		finish_wait(&dev->idleq, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}
	return 0;
}

static int sgx2d_open(struct inode *inod, struct file *filp)
{
	struct sgx2d_dev *dev = container_of(inod->i_cdev, struct sgx2d_dev,
					     cdev);
	/* one at a time */
	if (test_and_set_bit_lock(SGX2D_DEV_BUSY, &dev->flags))
		return -EBUSY;

	filp->private_data = dev;

	/* enable the 2d clock */
	clk_prepare_enable(dev->twod_clk);
	return 0;
}

static int sgx2d_release(struct inode *inod, struct file *filp)
{
	struct sgx2d_dev *dev = container_of(inod->i_cdev, struct sgx2d_dev,
					     cdev);
	/* disable the 2d clock */
	clk_disable_unprepare(dev->twod_clk);

	clear_bit_unlock(SGX2D_DEV_BUSY, &dev->flags);
	return 0;
}

static int sgx2d_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct sgx2d_dev *dev = filp->private_data;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long len = vma->vm_end - vma->vm_start;

	/* if unmappable, don't even bother */
	if (dev->pdata->unmappable)
		return -ENODEV;

	/* mustn't start after the end */
	if (offset >= dev->mmap_len)
		return -EINVAL;
	/* or end after the end */
	if (offset + len > dev->mmap_len)
		return -EINVAL;

	/* remap_pfn_range will mark the range VM_IO */
	if (remap_pfn_range(vma, vma->vm_start,
			    (dev->mmap_phys + offset) >> PAGE_SHIFT,
			    len, PAGE_SHARED))
		return -EAGAIN;
	return 0;
}

static struct sgx2d_pdata_reg *sgx2d_find_reg(struct sgx2d_dev *dev,
					      unsigned int id)
{
	struct sgx2d_pdata *pdata = dev->pdata;
	struct sgx2d_pdata_reg *reg = pdata->regs;
	int i;
	for (i = 0; i < pdata->reg_count; ++i, ++reg)
		if (reg->reg.id == id)
			return reg;
	return NULL;
}

static long sgx2d_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sgx2d_dev *dev = filp->private_data;
	struct sgx2d_pdata *pdata = dev->pdata;
	struct sgx2d_reg reg;
	struct sgx2d_meminfo meminfo;
	struct sgx2d_pdata_reg *pdata_reg;

	switch (cmd) {
	default:
		return -EINVAL;
	case SGX2DIO_WAITIDLE:
		return sgx2d_wait_idle(dev, filp);
	case SGX2DIO_SOFTRST:
		return sgx2d_soft_reset(dev);
	case SGX2DIO_GETVERS:
		if (copy_to_user(argp, &pdata->vers, sizeof(pdata->vers)))
			return -EFAULT;
		break;
	case SGX2DIO_GETREG:
		if (copy_from_user(&reg, argp, sizeof(reg)))
			return -EFAULT;
		if (reg.id == SGX2D_REG_INVALID)
			return -EINVAL;
		if (reg.id & SGX2D_REG_PRIVATE)
			return -EINVAL;

		/* find the actual register information */
		pdata_reg = sgx2d_find_reg(dev, reg.id);
		if (!pdata_reg)
			return -EINVAL;
		reg = pdata_reg->reg;
		reg.offset += dev->mmap_offset[pdata_reg->region];

		if (copy_to_user(argp, &reg, sizeof(reg)))
			return -EFAULT;
		break;
	case SGX2DIO_GETMEM:
		if (pdata->unmappable) {
			meminfo.addr = (void *)dev->mmap_phys;
			/* flags = 0 indicates that mmap shouldn't be used */
			meminfo.flags = 0;
		} else {
			meminfo.addr = NULL;
			meminfo.flags = MAP_SHARED;
		}
		meminfo.len = dev->mmap_len;
		if (copy_to_user(argp, &meminfo, sizeof(meminfo)))
			return -EFAULT;
		break;
	}

	return 0;
}

static const struct file_operations sgx2d_fops = {
	.owner			= THIS_MODULE,
	.open			= sgx2d_open,
	.release		= sgx2d_release,
	.mmap			= sgx2d_mmap,
	.unlocked_ioctl		= sgx2d_ioctl,
};

static struct sgx2d_pdata_reg comet_sgx2d_pdata_regs[] = {
	{
		.reg		= {
			.id	= SGX2D_REG_SLAVEPORT,
			.offset	= 0x00,
		},
		.region		= 0,	/* slave port */
	},
	{
		.reg		= {
			.id	= SGX2D_REG_BLTCOUNT,
			.offset	= 0x0c,		/* CR_2D_STATUS */
			.mask	= 0x0ffffff0,	/* CR_2D_BLIT_STATUS_COMPLETE */
			.shift	= 4,
		},
		.region		= 1,	/* HEP */
	},
	{
		.reg		= {
			.id	= SGX2D_REG_BUSY,
			.offset	= 0x0c,		/* CR_2D_STATUS */
			.mask	= 0x00000004,	/* CR_2D_BLIT_STATUS_BUSY */
			.shift	= 2,
		},
		.region		= 1,	/* HEP */
	},
	{
		.reg		= {
			.id	= SGX2D_REG_IDLE,
			.offset	= 0x0c,		/* CR_2D_STATUS */
			.mask	= 0x00000002,	/* CR_2D_IDLE */
			.shift	= 1,
		},
		.region		= 1,	/* HEP */
	},
	{
		.reg		= {
			.id	= SGX2D_REG_BASEADDR,
			.offset	= 0x24,		/* CR_2D_MEM_BASE_ADDR */
		},
		.region		= 1,	/* HEP */
	},
	{
		.reg		= {
			.id	= SGX2D_REG_SRST,
			.offset	= 0x00,		/* CR_HEP_SRST */
			.mask	= 0x00000002,	/* CR_2D_SOFT_RESET */
			.shift	= 1,
		},
		.region		= 1,	/* HEP */
	},
};

static struct sgx2d_pdata comet_sgx2d_pdata = {
	.vers		= {
		.arch_fam	= SGX2D_FAM_COMET,
	},
	.regs		= comet_sgx2d_pdata_regs,
	.reg_count	= ARRAY_SIZE(comet_sgx2d_pdata_regs),
	.unmappable	= 1,
};

static const struct of_device_id sgx2d_match[] = {
	{ .compatible = "img,tz1090-2d", .data = &comet_sgx2d_pdata, },
	{ }
};
MODULE_DEVICE_TABLE(of, sgx2d_match);

static int sgx2d_process_pdata(struct sgx2d_pdata *pdata)
{
	struct sgx2d_pdata_reg *reg = pdata->regs;
	int i;
	for (i = 0; i < pdata->reg_count; ++i, ++reg) {
		if (!reg->reg.mask)
			reg->reg.mask = -1;
		if (reg->region >= NUM_REGIONS)
			reg->reg.id = SGX2D_REG_INVALID;
	}
	return 0;
}

static int sgx2d_probe(struct platform_device *pdev)
{
	struct sgx2d_dev *dev;
	struct sgx2d_pdata *pdata;
	struct resource *res_irq;
	struct resource *res_slaveport;
	struct resource *res_regs;
	int devno;
	int error;
	const struct of_device_id *of_id;
	struct device_node *node = pdev->dev.of_node;

	/* get platform data from match table or pdev */
	if (node && (of_id = of_match_node(sgx2d_match, node)))
		pdata = (struct sgx2d_pdata *)of_id->data;
	else
		pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto err_pdata;
	}

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res_irq == NULL) {
		dev_err(&pdev->dev, "cannot find IRQ resource\n");
		error = -ENOENT;
		goto err_pdata;
	}

	res_slaveport = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_slaveport == NULL) {
		dev_err(&pdev->dev, "cannot find slave port resource\n");
		error = -ENOENT;
		goto err_pdata;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res_regs == NULL) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		error = -ENOENT;
		goto err_pdata;
	}

	/* correct any pdata shortcuts */
	error = sgx2d_process_pdata(pdata);
	if (error)
		goto err_pdata;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		error = -ENOMEM;
		goto err_dev;
	}
	platform_set_drvdata(pdev, dev);
	dev->pdata = pdata;
	init_waitqueue_head(&dev->idleq);

	dev->twod_clk = of_clk_get(node, 0);
	if (IS_ERR(dev->twod_clk)) {
		dev_warn(&pdev->dev, "could not get clock resource\n");
		error = PTR_ERR(dev->twod_clk);
		goto err_clk;
	}

	dev->reg_idle = sgx2d_find_reg(dev, SGX2D_REG_IDLE);
	if (!dev->reg_idle) {
		dev_err(&pdev->dev, "no idle field found in platform data\n");
		error = -EINVAL;
		goto err_find_regs;
	}

	dev->reg_srst = sgx2d_find_reg(dev, SGX2D_REG_SRST);
	if (!dev->reg_srst) {
		dev_err(&pdev->dev,
			"no soft reset field found in platform data\n");
		error = -EINVAL;
		goto err_find_regs;
	}

	dev->reg_base = sgx2d_find_reg(dev, SGX2D_REG_BASEADDR);
	/* it's not the end of the world if there's no base address register */

	/*
	 * lay out the memory resources in the mmapping
	 * each region needs to be on separate pages
	 * regions may start/end midpage
	 */
	dev->mmap_phys = min(res_slaveport->start,
			     res_regs->start) & PAGE_MASK;
	dev->mmap_len = (max(res_slaveport->end, res_regs->end)
			 - dev->mmap_phys + PAGE_SIZE - 1) & PAGE_MASK;
	dev->mmap_offset[0] = res_slaveport->start - dev->mmap_phys;
	dev->mmap_offset[1] = res_regs->start - dev->mmap_phys;

	/* ioremap the memory resources (combined) */
	dev->mem_io = ioremap(dev->mmap_phys, dev->mmap_len);
	if (!dev->mem_io) {
		error = -EIO;
		goto err_memio;
	}

	dev->irq = res_irq->start;
	error = request_irq(dev->irq, sgx2d_isr, IRQF_TRIGGER_RISING, "sgx2d",
			    dev);
	if (error) {
		dev_err(&pdev->dev, "cannot register IRQ %u\n",
			dev->irq);
		error = -EIO;
		goto err_irq;
	}

	dev->misc.minor = MISC_DYNAMIC_MINOR;
	dev->misc.fops = &sgx2d_fops;
	dev->misc.name = "sgx2d";

	error = misc_register(&dev->misc);
	if (error) {
		dev_err(&pdev->dev, "unable to register misc device\n");
		goto err_misc_reg;
	}

	devno = MKDEV(MISC_MAJOR, dev->misc.minor);
	cdev_init(&dev->cdev, &sgx2d_fops);
	error = cdev_add(&dev->cdev, devno, 1);
	if (error) {
		dev_err(&pdev->dev, "unable to add cdev\n");
		goto err_cdev;
	}

	/* initialise the hardware */
	sgx2d_soft_reset(dev);

	dev_info(&pdev->dev, "SGX 2D module loaded\n");

	return 0;

err_cdev:
	misc_deregister(&dev->misc);
err_misc_reg:
	free_irq(dev->irq, dev);
err_irq:
	iounmap(dev->mem_io);
err_memio:
err_find_regs:
	clk_put(dev->twod_clk);
err_clk:
	kfree(dev);
err_dev:
err_pdata:
	return error;
}

static int sgx2d_remove(struct platform_device *pdev)
{
	struct sgx2d_dev *dev = platform_get_drvdata(pdev);

	misc_deregister(&dev->misc);
	free_irq(dev->irq, dev);
	iounmap(dev->mem_io);
	clk_put(dev->twod_clk);
	kfree(dev);

	return 0;
}

#ifdef CONFIG_PM
static int sgx2d_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sgx2d_dev *dev = platform_get_drvdata(pdev);

	/* preserve the base address */
	if (dev->reg_base)
		dev->suspend_base = sgx2d_read_reg(dev, dev->reg_base);

	/* disable the 2d clock if the device file is open */
	if (test_and_set_bit_lock(SGX2D_DEV_BUSY, &dev->flags)) {
		/*
		 * Wait for the block to become idle
		 * If a command is half written there's not a lot we can do
		 * unfortunately.
		 */
		sgx2d_wait_idle(dev, NULL);

		clk_disable_unprepare(dev->twod_clk);
	} else {
		clear_bit_unlock(SGX2D_DEV_BUSY, &dev->flags);
	}
	return 0;
}

static int sgx2d_resume(struct platform_device *pdev)
{
	struct sgx2d_dev *dev = platform_get_drvdata(pdev);

	/* re-enable the 2d clock if the device file is open */
	if (test_and_set_bit_lock(SGX2D_DEV_BUSY, &dev->flags))
		clk_prepare_enable(dev->twod_clk);
	else
		clear_bit_unlock(SGX2D_DEV_BUSY, &dev->flags);

	/* initialise the hardware */
	sgx2d_soft_reset(dev);

	/* restore the base address */
	if (dev->reg_base)
		sgx2d_write_reg(dev, dev->reg_base, dev->suspend_base);
	return 0;
}
#else
#define sgx2d_suspend NULL
#define sgx2d_resume NULL
#endif

static struct platform_driver sgx2d_driver = {
	.driver = {
		.name		= "sgx2d",
		.owner		= THIS_MODULE,
		.of_match_table	= sgx2d_match,
	},
	.probe		= sgx2d_probe,
	.remove		= sgx2d_remove,
	.suspend	= sgx2d_suspend,
	.resume		= sgx2d_resume,
};

static int __init sgx2d_init(void)
{
	return platform_driver_register(&sgx2d_driver);
}

static void __exit sgx2d_exit(void)
{
	platform_driver_unregister(&sgx2d_driver);
}

module_init(sgx2d_init);
module_exit(sgx2d_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("PowerVR SGX 2D Driver");
MODULE_LICENSE("GPL");
