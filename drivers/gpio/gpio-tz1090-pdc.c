/*
 * Toumaz Xenif TZ1090 PDC GPIO handling.
 *
 * Copyright (C) 2012-2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <asm/global_lock.h>
#include <asm/soc-tz1090/gpio.h>

/* Register offsets from SOC_GPIO_CONTROL0 */
#define REG_SOC_GPIO_CONTROL0	0x00
#define REG_SOC_GPIO_CONTROL1	0x04
#define REG_SOC_GPIO_CONTROL2	0x08
#define REG_SOC_GPIO_CONTROL3	0x0c
#define REG_SOC_GPIO_STATUS	0x80

/* out of PDC gpios, only syswakes have irqs */
#define NR_PDC_GPIO_IRQS	3

/**
 * struct tz1090_pdc_gpio - GPIO bank private data
 * @chip:	Generic GPIO chip for GPIO bank
 * @reg:	Base of registers, offset for this GPIO bank
 * @irq:	IRQ numbers for Syswake GPIOs
 *
 * This is the main private data for the PDC GPIO driver. It encapsulates a
 * gpio_chip, and the callbacks for the gpio_chip can access the private data
 * with the to_pdc() macro below.
 */
struct tz1090_pdc_gpio {
	struct gpio_chip chip;
	void __iomem *reg;
	int irq[NR_PDC_GPIO_IRQS];
};
#define to_pdc(c)	container_of(c, struct tz1090_pdc_gpio, chip)

/* Register accesses into the PDC MMIO area */

static inline void pdc_write(struct tz1090_pdc_gpio *priv, unsigned int reg_offs,
		      unsigned int data)
{
	writel(data, priv->reg + reg_offs);
}

static inline unsigned int pdc_read(struct tz1090_pdc_gpio *priv,
			     unsigned int reg_offs)
{
	return readl(priv->reg + reg_offs);
}

/* Generic GPIO interface */

static int tz1090_pdc_gpio_direction_input(struct gpio_chip *chip,
					   unsigned offset)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	u32 value;
	int lstat;

	__global_lock2(lstat);
	value = pdc_read(priv, REG_SOC_GPIO_CONTROL1);
	value |= BIT(offset);
	pdc_write(priv, REG_SOC_GPIO_CONTROL1, value);
	__global_unlock2(lstat);

	return 0;
}

static int tz1090_pdc_gpio_direction_output(struct gpio_chip *chip,
					    unsigned offset, int output_value)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	u32 value;
	int lstat;

	__global_lock2(lstat);
	/* EXT_POWER doesn't seem to have an output value bit */
	if (offset < 6) {
		value = pdc_read(priv, REG_SOC_GPIO_CONTROL0);
		if (output_value)
			value |= BIT(offset);
		else
			value &= ~BIT(offset);
		pdc_write(priv, REG_SOC_GPIO_CONTROL0, value);
	}

	value = pdc_read(priv, REG_SOC_GPIO_CONTROL1);
	value &= ~BIT(offset);
	pdc_write(priv, REG_SOC_GPIO_CONTROL1, value);
	__global_unlock2(lstat);

	return 0;
}

static int tz1090_pdc_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	return pdc_read(priv, REG_SOC_GPIO_STATUS) & BIT(offset);
}

static void tz1090_pdc_gpio_set(struct gpio_chip *chip, unsigned offset,
				int output_value)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	u32 value;
	int lstat;

	/* EXT_POWER doesn't seem to have an output value bit */
	if (offset >= 6)
		return;

	__global_lock2(lstat);
	value = pdc_read(priv, REG_SOC_GPIO_CONTROL0);
	if (output_value)
		value |= BIT(offset);
	else
		value &= ~BIT(offset);
	pdc_write(priv, REG_SOC_GPIO_CONTROL0, value);
	__global_unlock2(lstat);
}

static int tz1090_pdc_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void tz1090_pdc_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int tz1090_pdc_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	unsigned int syswake = GPIO_PDC_PIN(offset) - GPIO_SYS_WAKE0;
	int irq;

	/* only syswakes have irqs */
	if (syswake >= NR_PDC_GPIO_IRQS)
		return -EINVAL;

	irq = priv->irq[syswake];
	if (!irq)
		return -EINVAL;

	return irq;
}

struct tz1090_pdc_gpio *pdc_gpio;

static int tz1090_pdc_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res_regs;
	struct tz1090_pdc_gpio *priv;
	unsigned int i;

	if (!np) {
		dev_err(&pdev->dev, "must be instantiated via devicetree\n");
		return -ENOENT;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_regs) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		return -ENOENT;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to allocate driver data\n");
		return -ENOMEM;
	}

	/* Ioremap the registers */
	priv->reg = devm_ioremap(&pdev->dev, res_regs->start,
				 res_regs->end - res_regs->start);
	if (!priv->reg) {
		dev_err(&pdev->dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}

	pdc_gpio = priv;

	/* Set up GPIO chip */
	priv->chip.label		= "tz1090-pdc-gpio";
	priv->chip.dev			= &pdev->dev;
	priv->chip.direction_input	= tz1090_pdc_gpio_direction_input;
	priv->chip.direction_output	= tz1090_pdc_gpio_direction_output;
	priv->chip.get			= tz1090_pdc_gpio_get;
	priv->chip.set			= tz1090_pdc_gpio_set;
	priv->chip.free			= tz1090_pdc_gpio_free;
	priv->chip.request		= tz1090_pdc_gpio_request;
	priv->chip.to_irq		= tz1090_pdc_gpio_to_irq;
	priv->chip.of_node		= np;

	/* GPIO numbering */
	priv->chip.base			= GPIO_PDC_PIN(0);
	priv->chip.ngpio		= NR_PDC_GPIO;

	/* Map the syswake irqs */
	for (i = 0; i < NR_PDC_GPIO_IRQS; ++i)
		priv->irq[i] = irq_of_parse_and_map(np, i);

	/* Add the GPIO bank */
	gpiochip_add(&priv->chip);

	return 0;
}

static struct of_device_id tz1090_pdc_gpio_of_match[] = {
	{ .compatible = "img,tz1090-pdc-gpio" },
	{ },
};

static struct platform_driver tz1090_pdc_gpio_driver = {
	.driver = {
		.name		= "tz1090-pdc-gpio",
		.owner		= THIS_MODULE,
		.of_match_table	= tz1090_pdc_gpio_of_match,
	},
	.probe		= tz1090_pdc_gpio_probe,
};

static int __init tz1090_pdc_gpio_init(void)
{
	return platform_driver_register(&tz1090_pdc_gpio_driver);
}
postcore_initcall(tz1090_pdc_gpio_init);


#ifdef CONFIG_METAG_SUSPEND_MEM
/*
 * For now we save and restore all GPIO setup.
 */
static const u32 tz1090_pdc_gpio_reg_masks[3] = {
	0x000000ff,	/* SOC_GPIO_CONTROL0 */
	0x0000007f,	/* SOC_GPIO_CONTROL1 */
	0x3fff0000,	/* SOC_GPIO_CONTROL2 */

};
struct tz1090_pdc_gpio_state {
	u32 values[ARRAY_SIZE(tz1090_pdc_gpio_reg_masks)];
};

static struct tz1090_pdc_gpio_state *tz1090_pdc_gpio_state;

static int tz1090_pdc_gpio_suspend(void)
{
	struct tz1090_pdc_gpio *priv = pdc_gpio;
	unsigned long flags;
	unsigned int i;
	struct tz1090_pdc_gpio_state *state;

	if (!priv)
		return 0;

	state = kzalloc(sizeof(*state), GFP_ATOMIC);
	if (!state)
		return -ENOMEM;

	__global_lock2(flags);
	for (i = 0; i < ARRAY_SIZE(tz1090_pdc_gpio_reg_masks); ++i)
		state->values[i] = pdc_read(priv, REG_SOC_GPIO_CONTROL0 + i*4);
	__global_unlock2(flags);

	tz1090_pdc_gpio_state = state;

	return 0;
}

static void tz1090_pdc_gpio_resume(void)
{
	struct tz1090_pdc_gpio *priv = pdc_gpio;
	unsigned long flags;
	unsigned int i;
	u32 value;
	struct tz1090_pdc_gpio_state *state = tz1090_pdc_gpio_state;

	if (!priv || !state)
		return;

	__global_lock2(flags);
	for (i = 0; i < ARRAY_SIZE(tz1090_pdc_gpio_reg_masks); ++i) {
		value = pdc_read(priv, REG_SOC_GPIO_CONTROL0 + i*4);
		value &= ~tz1090_pdc_gpio_reg_masks[i];
		value |= state->values[i] & tz1090_pdc_gpio_reg_masks[i];
		pdc_write(priv, REG_SOC_GPIO_CONTROL0 + i*4, value);
	}
	__global_unlock2(flags);

	tz1090_pdc_gpio_state = NULL;
	kfree(state);
}
#else
#define tz1090_pdc_gpio_suspend NULL
#define tz1090_pdc_gpio_resume NULL
#endif	/* CONFIG_METAG_SUSPEND_MEM */

struct syscore_ops tz1090_pdc_gpio_syscore_ops = {
	.suspend = tz1090_pdc_gpio_suspend,
	.resume = tz1090_pdc_gpio_resume,
};

static int tz1090_pdc_gpio_syscore_init(void)
{
	register_syscore_ops(&tz1090_pdc_gpio_syscore_ops);
	return 0;
}

static void tz1090_pdc_gpio_syscore_exit(void)
{
	unregister_syscore_ops(&tz1090_pdc_gpio_syscore_ops);
}

module_init(tz1090_pdc_gpio_syscore_init);
module_exit(tz1090_pdc_gpio_syscore_exit);
