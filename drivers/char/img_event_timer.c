/*
 * IMG Event Timer Driver interface
 *
 * Copyright (C) 2011  Imagination Technologies
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/img_event_timer.h>

#include <asm/core_reg.h>

#define COUNT_CLK_REG		0x00
#define CCR_CLKBITS_MASK	0x60000000
#define CCR_CLKBITS_SHIFT	29
#define CCR_ENABLE_MASK		0x80000000
#define CCR_ENABLE_SHIFT	31

#define EVENT_FLAGS_REG		0x04
#define EVENT_FLAGSCLR_REG	0x08
#define EVENT0_SELECT_REG	0x0C
#define EVENT0_TIMESTAMP_REG	0x24


static unsigned long base_addr;
static struct miscdevice miscdev;
static struct cdev cdev;

int img_evt_setclock(u32 src)
{
	u32 temp;

	switch (src) {

	case 0:
		temp = ioread32((void *)base_addr+COUNT_CLK_REG);
		temp &= ~CCR_CLKBITS_MASK;
		temp |= (0 <<  CCR_CLKBITS_SHIFT);
		iowrite32(temp, (void *)base_addr+COUNT_CLK_REG);
		break;

	case 1:
		temp = ioread32((void *)base_addr+COUNT_CLK_REG);
		temp &= ~CCR_CLKBITS_MASK;
		temp |= (2 <<  CCR_CLKBITS_SHIFT);
		iowrite32(temp, (void *)base_addr+COUNT_CLK_REG);
		break;

	case 2:
		temp = ioread32((void *)base_addr+COUNT_CLK_REG);
		temp &= ~CCR_CLKBITS_MASK;
		temp |= (3 <<  CCR_CLKBITS_SHIFT);
		iowrite32(temp, (void *)base_addr+COUNT_CLK_REG);
		break;

	default:
		return -ERANGE;

	}

	return 0;

}

int img_evt_setevent(struct evt_event event)
{
	if (event.counter < EVT_MAX_COUNTERS) {
		iowrite32(event.source, (void *)base_addr + EVENT0_SELECT_REG +
						event.counter * 4);
		return 0;
	} else {
		return -ERANGE;
	}

}

int img_evt_gettimestamp(struct evt_event *event)
{
	unsigned long flags;
	struct timespec timeofday;

	if (event->counter < EVT_MAX_COUNTERS) {
		local_irq_save(flags);
		/*todo is this order important ?*/
		event->txtimer = __core_reg_get(TXTIMER);
		event->timestamp = ioread32((void *)base_addr +
						    EVENT0_TIMESTAMP_REG +
						    event->counter * 4);
		 ktime_get_ts(&timeofday);
		 event->timeofday_sec = timeofday.tv_sec;
		 event->timeofday_ns = timeofday.tv_nsec;

		local_irq_restore(flags);
		return 0;
	} else {
		return -ERANGE;
	}
}


static long
ioctl_img_evt(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct evt_clock clock;
	struct evt_event event;
	int ret;

	switch (cmd) {
	default:
		return -ENOTTY;

	case EVTIO_SETCLOCK:
		if (copy_from_user(&clock, argp, sizeof(clock)))
			return -EFAULT;

		ret = img_evt_setclock(clock.src);
		if (ret)
			return ret;

		break;

	case EVTIO_SETEVENTSRC:
		if (copy_from_user(&event, argp, sizeof(event)))
			return -EFAULT;

		ret = img_evt_setevent(event);
		if (ret)
			return ret;

		break;

	case EVTIO_GETEVTS:
		if (copy_from_user(&event, argp, sizeof(event)))
			return -EFAULT;

		ret = img_evt_gettimestamp(&event);
		if (ret)
			return ret;

		if (copy_to_user(argp, &event, sizeof(event)))
			return -EFAULT;

		break;
	}

	return 0;
}

static const struct file_operations img_evt_fops = {
	.unlocked_ioctl		= ioctl_img_evt,
};

static int __init img_evt_probe(struct platform_device *pdev)
{
	struct resource *mem_resource;
	int devno;
	int error;

	mem_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem_resource) {
		dev_info(&pdev->dev, "failed to get resources\n");
		return -ENODEV;
	}

	base_addr = mem_resource->start;

	/*enable module*/
	iowrite32(CCR_ENABLE_MASK, (void *)base_addr+COUNT_CLK_REG);

	miscdev.minor = MISC_DYNAMIC_MINOR;
	miscdev.fops = &img_evt_fops;
	miscdev.name = "img-evt";

	error = misc_register(&miscdev);
	if (error) {
		dev_err(&pdev->dev, "unable to register misc device\n");
		goto err_misc_reg;
	}

	devno = MKDEV(MISC_MAJOR, miscdev.minor);
	cdev_init(&cdev, &img_evt_fops);
	error = cdev_add(&cdev, devno, 1);
	if (error) {
		dev_err(&pdev->dev, "unable to add cdev\n");
		goto err_cdev;
	}

	dev_info(&pdev->dev, "Event Timer Registered\n");

	return 0;

err_cdev:
	misc_deregister(&miscdev);
err_misc_reg:
	return error;
}

static int img_evt_remove(struct platform_device *pdev)
{
	cdev_del(&cdev);
	misc_deregister(&miscdev);
	return 0;
}

MODULE_ALIAS("img-eventtimer");	/* for platform bus hotplug */
static struct platform_driver img_evt_driver = {
	.driver	= {
		.name	= "img-eventtimer",
		.owner	= THIS_MODULE,
	},
	.remove		= img_evt_remove,
};

static int __init img_evt_init(void)
{
	return platform_driver_probe(&img_evt_driver, img_evt_probe);
}
module_init(img_evt_init);

static void __exit img_evt_exit(void)
{
	platform_driver_unregister(&img_evt_driver);
}
module_exit(img_evt_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("IMG Event Timer");
MODULE_LICENSE("GPL");

