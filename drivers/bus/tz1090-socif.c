/*
 * TZ1090 SOCIF Error Handling.
 *
 * Copyright (C) 2011-2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

/* Register offsets */
#define REG_TIMEOUT	0x0
#define REG_STATUS	0x4

/* REG_TIMEOUT fields */
#define REG_TIMEOUT_FLUSH_M	0x00002000
#define REG_TIMEOUT_ENABLE_M	0x00001000
#define REG_TIMEOUT_PERIOD_M	0x00000fff
#define REG_TIMEOUT_PERIOD_S	0

/* REG_STATUS fields */
#define REG_STATUS_STATUS_M	0x00000001

/**
 * struct tz1090_socif_priv - Private driver data.
 * @reg:		Base address of SOCIF registers.
 * @err_count:		Count of SOCIF errors.
 */
struct tz1090_socif_priv {
	void __iomem	*reg;
	unsigned int	err_count;
};

/**
 * tz1090_soci_isr() - SOCIF timeout interrupt handler.
 * @irq:	IRQ number
 * @dev_id:	Driver priv pointer
 *
 * It is useful to know where the timeout is coming from. We cannot find the
 * memory address that caused the problem, but can print out the pid, PC, and
 * registers which with some analysis would usually be enough to determine where
 * the bad access is (assuming it's even emanating from the Meta).
 */
static irqreturn_t tz1090_socif_isr(int irq, void *dev_id)
{
	struct tz1090_socif_priv *priv = dev_id;
	struct pt_regs *regs = get_irq_regs();
	unsigned int err_num = priv->err_count++;

	if (printk_ratelimit()) {
		pr_info("%s[%d]: SOCIF error #%u at pc %08x sp %08x",
			current->comm, task_pid_nr(current), err_num,
			regs->ctx.CurrPC, regs->ctx.AX[0].U0);
		print_vma_addr(" in ", regs->ctx.CurrPC);
		print_vma_addr(" rtp in ", regs->ctx.DX[4].U1);
		pr_cont("\n");
		show_regs(regs);
	}

	return IRQ_HANDLED;
}

/**
 * tz1090_socif_setup() - Setup SOCIF to catch timeouts.
 * @priv:	Driver priv pointer
 * @enable:	true to enable timeout, false to disable timeout
 *
 * Timeouts can happen when memory is accessed in a way or at a time when the
 * peripheral doesn't handle it correctly, for example reading from the 2d
 * block register area. It doesn't catch every case, such as accessing the 2d
 * block register area with the 2d clock gated.
 */
static void tz1090_socif_setup(struct tz1090_socif_priv *priv, bool enable)
{
	u32 tmp = 0;

	/* Set up the maximum timeout possible */
	if (enable)
		tmp = REG_TIMEOUT_ENABLE_M | REG_TIMEOUT_PERIOD_M;
	iowrite32(tmp, priv->reg + REG_TIMEOUT);
}

static int tz1090_socif_probe(struct platform_device *pdev)
{
	struct tz1090_socif_priv *priv;
	struct resource *res_regs;
	int irq, err;

	/* allocate private driver data */
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate driver data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);

	/* ioremap the registers */
	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->reg = devm_request_and_ioremap(&pdev->dev, res_regs);
	if (!priv->reg) {
		dev_err(&pdev->dev, "cannot request/ioremap registers\n");
		return -EIO;
	}

	/* get IRQ number */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ resource (%d)\n",
			irq);
		return irq;
	}

	/* set up irq */
	err = devm_request_irq(&pdev->dev, irq, tz1090_socif_isr,
			       IRQF_TRIGGER_RISING, "tz1090-socif", priv);
	if (err) {
		dev_err(&pdev->dev, "cannot register IRQ %u (%d)\n",
			irq, err);
		return err;
	}

	/* set up timeout to trigger interrupt */
	tz1090_socif_setup(priv, true);

	dev_info(&pdev->dev, "SOCIF timeout enabled (%u cycles)\n",
		 REG_TIMEOUT_PERIOD_M);

	return 0;
}

static int tz1090_socif_remove(struct platform_device *pdev)
{
	struct tz1090_socif_priv *priv = platform_get_drvdata(pdev);

	/* disable timeout */
	tz1090_socif_setup(priv, false);

	return 0;
}


#if defined(CONFIG_PM_SLEEP)
static int tz1090_socif_resume(struct device *dev)
{
	struct tz1090_socif_priv *priv = dev_get_drvdata(dev);

	/* enable timeout */
	tz1090_socif_setup(priv, true);
	return 0;
}
#else
#define tz1090_socif_resume NULL
#endif	/* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(tz1090_socif_pmops, NULL, tz1090_socif_resume);

static const struct of_device_id tz1090_socif_match[] = {
	{ .compatible = "img,tz1090-socif" },
	{}
};
MODULE_DEVICE_TABLE(of, tz1090_socif_match);

static struct platform_driver tz1090_socif_driver = {
	.driver = {
		.name		= "tz1090-socif",
		.owner		= THIS_MODULE,
		.of_match_table	= tz1090_socif_match,
		.pm		= &tz1090_socif_pmops,
	},
	.probe = tz1090_socif_probe,
	.remove = tz1090_socif_remove,
};

module_platform_driver(tz1090_socif_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("TZ1090 SOCIF Error Driver");
MODULE_LICENSE("GPL");
