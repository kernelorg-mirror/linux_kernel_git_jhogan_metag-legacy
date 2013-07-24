/*
 * ImgTec PowerDown Controller Watchdog Timer as found in Meta SoCs.
 *
 * Copyright 2010-2012 Imagination Technologies Ltd.
 *
 * Parts derived from mpcore_wdt.
 */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

/* registers */

#define PDC_WD_SW_RESET			0x000
#define PDC_WD_CONFIG			0x004
#define PDC_WD_TICKLE1			0x008
#define PDC_WD_TICKLE2			0x00c
#define PDC_WD_IRQ_STATUS		0x010
#define PDC_WD_IRQ_CLEAR		0x014
#define PDC_WD_IRQ_EN			0x018

/* field masks */

#define PDC_WD_CONFIG_ENABLE		0x80000000
#define PDC_WD_CONFIG_REMIND		0x00001f00
#define PDC_WD_CONFIG_REMIND_SHIFT	8
#define PDC_WD_CONFIG_DELAY		0x0000001f
#define PDC_WD_CONFIG_DELAY_SHIFT	0
#define PDC_WD_TICKLE_NEW		0x00000010
#define PDC_WD_TICKLE_STATUS		0x00000007
#define PDC_WD_TICKLE_STATUS_SHIFT	0
#define PDC_WD_IRQ_REMIND		0x00000001

/* constants */

#define PDC_WD_TICKLE1_MAGIC		0xabcd1234
#define PDC_WD_TICKLE2_MAGIC		0x4321dcba
#define PDC_WD_TICKLE_STATUS_HRESET	0x0	/* Hard reset */
#define PDC_WD_TICKLE_STATUS_TIMEOUT	0x1	/* Timeout */
#define PDC_WD_TICKLE_STATUS_TICKLE	0x2	/* Tickled incorrectly */
#define PDC_WD_TICKLE_STATUS_SRESET	0x3	/* Soft reset */
#define PDC_WD_TICKLE_STATUS_USER	0x4	/* User reset */

#define TIMER_MIN	(-14)
#define TIMER_MAX	17

#define TIMER_DELAY_SHIFT		6	/* 64 seconds */
static int delay_shift = TIMER_DELAY_SHIFT;
module_param(delay_shift, int, 0);
MODULE_PARM_DESC(delay_shift,
	"Log2 of PDC watchdog timer delay in seconds ("
	"-14 <= delay_shift <= 17, "
	"default=" __MODULE_STRING(TIMER_DELAY_SHIFT) ")");

#define TIMER_REMIND_SHIFT		TIMER_MAX	/* disabled */
static int remind_shift = TIMER_REMIND_SHIFT;
module_param(remind_shift, int, 0);
MODULE_PARM_DESC(remind_shift,
	"Log2 of PDC watchdog timer remind in seconds ("
	"-14 <= remind_shift <= 17, "
	"default=" __MODULE_STRING(TIMER_REMIND_SHIFT) ")");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct pdc_wdt_priv {
	struct device *dev;
	int irq;
	void __iomem *reg_base;
	unsigned long timer_alive;
	char expect_close;
	char card_reset;	/* whether last reset by the WD */
	spinlock_t lock;
};

static struct platform_device *pdc_wdt_dev;

/* Hardware access */

static void pdc_wdt_write(struct pdc_wdt_priv *wdt,
			  unsigned int reg_offs, unsigned int data)
{
	iowrite32(data, wdt->reg_base + reg_offs);
}

static unsigned int pdc_wdt_read(struct pdc_wdt_priv *wdt,
				 unsigned int reg_offs)
{
	return ioread32(wdt->reg_base + reg_offs);
}

static void pdc_wdt_keepalive(struct pdc_wdt_priv *wdt)
{
	spin_lock(&wdt->lock);
	pdc_wdt_write(wdt, PDC_WD_TICKLE1, PDC_WD_TICKLE1_MAGIC);
	pdc_wdt_write(wdt, PDC_WD_TICKLE2, PDC_WD_TICKLE2_MAGIC);
	spin_unlock(&wdt->lock);
}

/* Start the watchdog timer (delay should already be set */
static void pdc_wdt_start(struct pdc_wdt_priv *wdt)
{
	u32 config;

	spin_lock(&wdt->lock);
	config = pdc_wdt_read(wdt, PDC_WD_CONFIG);
	config |= PDC_WD_CONFIG_ENABLE;
	pdc_wdt_write(wdt, PDC_WD_CONFIG, config);
	spin_unlock(&wdt->lock);
}

/* Safely stop the watchdog timer */
static void pdc_wdt_stop(struct pdc_wdt_priv *wdt)
{
	u32 config;

	spin_lock(&wdt->lock);
	config = pdc_wdt_read(wdt, PDC_WD_CONFIG);
	config &= ~PDC_WD_CONFIG_ENABLE;
	pdc_wdt_write(wdt, PDC_WD_CONFIG, config);
	spin_unlock(&wdt->lock);

	/* Must tickle to finish the stop */
	pdc_wdt_keepalive(wdt);
}

/* Find whether the watchdog hardware is enabled */
static int pdc_wdt_get_started(struct pdc_wdt_priv *wdt)
{
	return !!(pdc_wdt_read(wdt, PDC_WD_CONFIG) & PDC_WD_CONFIG_ENABLE);
}

static void pdc_wdt_set_delay_shift(struct pdc_wdt_priv *wdt, int delay_sh)
{
	u32 config;

	delay_shift = delay_sh;
	spin_lock(&wdt->lock);
	config = pdc_wdt_read(wdt, PDC_WD_CONFIG);
	/* number of 32.768KHz clocks, 2^(n+1) (14 is 1 sec) */
	config &= ~PDC_WD_CONFIG_DELAY;
	config |= (delay_shift-TIMER_MIN) << PDC_WD_CONFIG_DELAY_SHIFT;
	pdc_wdt_write(wdt, PDC_WD_CONFIG, config);
	spin_unlock(&wdt->lock);
}

static void pdc_wdt_set_remind_shift(struct pdc_wdt_priv *wdt, int remind_sh)
{
	u32 config;

	remind_shift = remind_sh;
	spin_lock(&wdt->lock);
	config = pdc_wdt_read(wdt, PDC_WD_CONFIG);
	/* number of 32.768KHz clocks, 2^(n+1) (14 is 1 sec) */
	config &= ~PDC_WD_CONFIG_REMIND;
	config |= (remind_shift-TIMER_MIN) << PDC_WD_CONFIG_REMIND_SHIFT;
	pdc_wdt_write(wdt, PDC_WD_CONFIG, config);
	spin_unlock(&wdt->lock);
}

static const char *pdc_wdt_tickle_status_str(u32 status)
{
	switch (status) {
	case PDC_WD_TICKLE_STATUS_HRESET:	return "hard reset";
	case PDC_WD_TICKLE_STATUS_TIMEOUT:	return "timeout";
	case PDC_WD_TICKLE_STATUS_TICKLE:	return "incorrect tickle";
	case PDC_WD_TICKLE_STATUS_SRESET:	return "soft reset";
	case PDC_WD_TICKLE_STATUS_USER:		return "user reset";
	default:				return "unknown";
	};
}

/* Initialise the watchdog hardware */
static void pdc_wdt_setup(struct pdc_wdt_priv *wdt)
{
	u32 status;

	/* Enable remind interrupts */
	pdc_wdt_write(wdt, PDC_WD_IRQ_EN, PDC_WD_IRQ_REMIND);
	/* Ensure the watchdog is stopped */
	pdc_wdt_stop(wdt);

	/* Set the timeouts */
	if (delay_shift < TIMER_MIN)
		delay_shift = TIMER_MIN;
	if (delay_shift > TIMER_MAX)
		delay_shift = TIMER_MAX;
	pdc_wdt_set_delay_shift(wdt, delay_shift);

	if (remind_shift < TIMER_MIN)
		remind_shift = TIMER_MIN;
	if (remind_shift > TIMER_MAX)
		remind_shift = TIMER_MAX;
	pdc_wdt_set_remind_shift(wdt, remind_shift);

	/* Find what caused the last reset */
	status = pdc_wdt_read(wdt, PDC_WD_TICKLE1);
	status = (status & PDC_WD_TICKLE_STATUS) >> PDC_WD_TICKLE_STATUS_SHIFT;
	dev_info(wdt->dev,
		 "Watchdog module loaded (last reset due to %s)\n",
		 pdc_wdt_tickle_status_str(status));

	/* Was it the watchdog? (userland may want to know) */
	switch (status) {
	case PDC_WD_TICKLE_STATUS_TICKLE:
	case PDC_WD_TICKLE_STATUS_TIMEOUT:
		wdt->card_reset = 1;
		break;
	default:
		wdt->card_reset = 0;
	}
}

/* round up to power of 2 */
static inline int pdc_wdt_delay_to_shift(int secs)
{
	return order_base_2(secs);
}

/* round down to power of 2 */
static inline int pdc_wdt_remind_to_shift(int secs)
{
	return ilog2(secs);
}

static inline int pdc_wdt_shift_to_secs(int shift)
{
	if (shift >= 0)
		return 1 << shift;
	else
		return 1;
}

static irqreturn_t pdc_wdt_isr(int irq, void *dev_id)
{
	struct pdc_wdt_priv *wdt = dev_id;
	u32 stat = pdc_wdt_read(wdt, PDC_WD_IRQ_STATUS);

	/*
	 * The behaviour of the remind interrupt should depend on what userland
	 * asks for, either do nothing, panic the system or inform userland.
	 * Unfortunately this is not part of the main linux interface, and is
	 * currently implemented only in the ipmi watchdog driver (in
	 * drivers/char). This could be added at a later time.
	 */

	pdc_wdt_write(wdt, PDC_WD_IRQ_CLEAR, stat);
	return IRQ_HANDLED;
}

/* /dev/watchdog handling */

static int pdc_wdt_open(struct inode *inode, struct file *file)
{
	struct pdc_wdt_priv *wdt = platform_get_drvdata(pdc_wdt_dev);

	/* one at a time */
	if (test_and_set_bit(0, &wdt->timer_alive))
		return -EBUSY;
	/* don't unload, there's no way out */
	if (nowayout)
		__module_get(THIS_MODULE);

	file->private_data = wdt;
	pdc_wdt_start(wdt);

	return nonseekable_open(inode, file);
}

static int pdc_wdt_release(struct inode *inode, struct file *file)
{
	struct pdc_wdt_priv *wdt = file->private_data;

	/*
	 * Shut off the timer.
	 * Lock it in if it's a module and we set nowayout
	 */
	if (wdt->expect_close == 42)
		pdc_wdt_stop(wdt);
	else if (pdc_wdt_get_started(wdt)) {
		dev_crit(wdt->dev,
			 "unexpected close, not stopping watchdog!\n");
		pdc_wdt_keepalive(wdt);
	}
	clear_bit(0, &wdt->timer_alive);
	wdt->expect_close = 0;
	return 0;
}

static ssize_t pdc_wdt_fwrite(struct file *file, const char __user *data,
			      size_t len, loff_t *ppos)
{
	struct pdc_wdt_priv *wdt = file->private_data;

	/* Refresh the timer. */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			wdt->expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					wdt->expect_close = 42;
			}
		}
		pdc_wdt_keepalive(wdt);
	}
	return len;
}

static struct watchdog_info pdc_wdt_ident = {
	.options		= WDIOF_SETTIMEOUT |
				  WDIOF_PRETIMEOUT |
				  WDIOF_CARDRESET |
				  WDIOF_MAGICCLOSE,
	.identity = "PDC Watchdog",
};

static long pdc_wdt_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct pdc_wdt_priv *wdt = file->private_data;
	int ret;
	int delay;
	union {
		struct watchdog_info ident;
		int i;
	} uarg;

	if (_IOC_DIR(cmd) && _IOC_SIZE(cmd) > sizeof(uarg))
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		ret = copy_from_user(&uarg, (void __user *)arg, _IOC_SIZE(cmd));
		if (ret)
			return -EFAULT;
	}

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		uarg.ident = pdc_wdt_ident;
		ret = 0;
		break;

	case WDIOC_GETSTATUS:
		uarg.i = 0;
		ret = 0;
		break;

	case WDIOC_GETBOOTSTATUS:
		uarg.i = 0;
		if (wdt->card_reset)
			uarg.i |= WDIOF_CARDRESET;
		ret = 0;
		break;

	case WDIOC_SETOPTIONS:
		/*
		 * Work around bad definition of WDIOC_SETOPTIONS until it's
		 * fixed. WDIOC_SETOPTIONS is a writing ioctl.
		 */
		if (!(_IOC_DIR(cmd) & _IOC_WRITE)) {
			ret = copy_from_user(&uarg, (void __user *)arg,
					     _IOC_SIZE(cmd));
			if (ret)
				return -EFAULT;
		}
		ret = -EINVAL;
		if (uarg.i & WDIOS_DISABLECARD) {
			pdc_wdt_stop(wdt);
			ret = 0;
		}
		if (uarg.i & WDIOS_ENABLECARD) {
			pdc_wdt_start(wdt);
			ret = 0;
		}
		break;

	case WDIOC_KEEPALIVE:
		pdc_wdt_keepalive(wdt);
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		if (uarg.i <= 0 || uarg.i > pdc_wdt_shift_to_secs(TIMER_MAX))
			return -EINVAL;
		uarg.i = pdc_wdt_delay_to_shift(uarg.i);
		pdc_wdt_set_delay_shift(wdt, uarg.i);
		/* fallthrough */
	case WDIOC_GETTIMEOUT:
		uarg.i = pdc_wdt_shift_to_secs(delay_shift);
		ret = 0;
		break;

	case WDIOC_SETPRETIMEOUT:
		/*
		 * Pretimeout is measured in seconds before main timeout.
		 * Subtract and round it once, and it will effectively change
		 * if the main timeout is changed.
		 */
		delay = pdc_wdt_shift_to_secs(delay_shift);
		if (!uarg.i)
			uarg.i = TIMER_MAX;
		else if (uarg.i > 0 && uarg.i < delay)
			uarg.i = pdc_wdt_remind_to_shift(delay - uarg.i);
		else
			return -EINVAL;
		pdc_wdt_set_remind_shift(wdt, uarg.i);
		/* fallthrough */
	case WDIOC_GETPRETIMEOUT:
		if (remind_shift >= TIMER_MAX)
			uarg.i = 0;
		else
			uarg.i = pdc_wdt_shift_to_secs(delay_shift) -
				pdc_wdt_shift_to_secs(remind_shift);
		ret = 0;
		break;

	default:
		return -ENOTTY;
	}

	if (ret == 0 && (_IOC_DIR(cmd) & _IOC_READ) && cmd != WDIOC_SETOPTIONS) {
		ret = copy_to_user((void __user *)arg, &uarg, _IOC_SIZE(cmd));
		if (ret)
			ret = -EFAULT;
	}
	return ret;
}

/* Kernel interface */

static const struct file_operations pdc_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= pdc_wdt_fwrite,
	.unlocked_ioctl	= pdc_wdt_ioctl,
	.open		= pdc_wdt_open,
	.release	= pdc_wdt_release,
};

static struct miscdevice pdc_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &pdc_wdt_fops,
};

static int pdc_wdt_probe(struct platform_device *pdev)
{
	struct pdc_wdt_priv *wdt;
	struct resource *res_regs;
	int irq, error;

	/* Get resources from platform device */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ resource\n");
		error = irq;
		goto err_pdata;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_regs == NULL) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		error = -ENOENT;
		goto err_pdata;
	}

	/* Private driver data */
	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		error = -ENOMEM;
		goto err_dev;
	}
	platform_set_drvdata(pdev, wdt);
	pdc_wdt_dev = pdev;
	wdt->dev = &pdev->dev;
	spin_lock_init(&wdt->lock);

	/* Ioremap the registers */
	wdt->reg_base = devm_ioremap(&pdev->dev, res_regs->start,
				     res_regs->end - res_regs->start);
	if (!wdt->reg_base) {
		error = -EIO;
		goto err_regs;
	}

	/* Set timeouts before userland has a chance to start the timer */
	pdc_wdt_setup(wdt);

	device_set_wakeup_capable(&pdev->dev, 1);
	pdc_wdt_miscdev.parent = &pdev->dev;
	error = misc_register(&pdc_wdt_miscdev);
	if (error) {
		dev_err(&pdev->dev,
			"cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, error);
		goto err_misc;
	}

	wdt->irq = irq;
	error = devm_request_irq(&pdev->dev, wdt->irq, pdc_wdt_isr, 0,
				 "pdc-wdt", wdt);
	if (error) {
		dev_err(&pdev->dev, "cannot register IRQ %u\n",
			wdt->irq);
		error = -EIO;
		goto err_irq;
	}

	return 0;

err_irq:
	misc_deregister(&pdc_wdt_miscdev);
err_misc:
err_regs:
	pdc_wdt_dev = NULL;
err_dev:
err_pdata:
	return error;
}

static int pdc_wdt_remove(struct platform_device *pdev)
{
	pdc_wdt_dev = NULL;
	misc_deregister(&pdc_wdt_miscdev);

	return 0;
}

/*
 * System shutdown handler.  Turn off the watchdog if we're
 * restarting or halting the system.
 */
static void pdc_wdt_shutdown(struct platform_device *pdev)
{
	struct pdc_wdt_priv *wdt = platform_get_drvdata(pdev);

	if (system_state == SYSTEM_RESTART || system_state == SYSTEM_HALT)
		pdc_wdt_stop(wdt);
}

#ifdef CONFIG_PM
static int pdc_wdt_remind_enabled(struct pdc_wdt_priv *wdt)
{
	return remind_shift != TIMER_MAX;
}

/*
 * During suspend we don't want the watchdog to think we've crashed, so
 * stop the watchdog until resume.
 */
static int pdc_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pdc_wdt_priv *wdt = platform_get_drvdata(pdev);

	/* Only wake if the remind is enabled. */
	if (device_may_wakeup(&pdev->dev) && pdc_wdt_remind_enabled(wdt))
		enable_irq_wake(wdt->irq);
	else if (wdt->timer_alive)
		pdc_wdt_stop(wdt);
	return 0;
}

static int pdc_wdt_resume(struct platform_device *pdev)
{
	struct pdc_wdt_priv *wdt = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev) && pdc_wdt_remind_enabled(wdt))
		disable_irq_wake(wdt->irq);
	else if (wdt->timer_alive)
		pdc_wdt_start(wdt);
	return 0;
}
#else
#define pdc_wdt_suspend NULL
#define pdc_wdt_resume NULL
#endif	/* CONFIG_PM */

static const struct of_device_id pdc_wdt_match[] = {
	{ .compatible = "img,pdc-wdt" },
	{}
};
MODULE_DEVICE_TABLE(of, pdc_wdt_match);

static struct platform_driver pdc_wdt_driver = {
	.driver = {
		.name = "imgpdc-wdt",
		.owner	= THIS_MODULE,
		.of_match_table	= pdc_wdt_match,
	},
	.probe = pdc_wdt_probe,
	.remove = pdc_wdt_remove,
	.shutdown = pdc_wdt_shutdown,
	.suspend = pdc_wdt_suspend,
	.resume = pdc_wdt_resume,
	/* pdc_wdt has shutdown handler too */
};

module_platform_driver(pdc_wdt_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("ImgTec PDC WDT");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
