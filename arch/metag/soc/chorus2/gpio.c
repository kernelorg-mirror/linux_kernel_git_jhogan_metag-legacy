/*
 *  Generic Chorus2 GPIO handling.
 *
 *   Based on ARM PXA code and others.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>

#include <asm/global_lock.h>
#include <asm/soc-chorus2/c2_irqnums.h>
#include <asm/soc-chorus2/gpio.h>

#define GPIO_BASE_A 0x0200F000
#define GPIO_BASE_B 0x02010000
#define GPIO_BASE_C 0x02011000
#define GPIO_BASE_D 0x02012000
#define GPIO_BASE_E 0x02013000
#define GPIO_BASE_F 0x02014000
#define GPIO_BASE_G 0x02015000
#define GPIO_BASE_H 0x02016000

#define GPIO_REG_OE_OFFSET         0x00000000
#define GPIO_REG_OUTPUT_OFFSET     0x00000004
#define GPIO_REG_INPUT_OFFSET      0x00000008
#define GPIO_REG_POLARITY_OFFSET   0x0000000C
#define GPIO_REG_INT_TYPE_OFFSET   0x00000010
#define GPIO_REG_INT_ENABLE_OFFSET 0x00000014
#define GPIO_REG_INT_STAT_OFFSET   0x00000018
#define GPIO_REG_MASTER_SEL_OFFSET 0x0000001C
#define GPIO_REG_PULLUP_OFFSET     0x00000020

struct chorus2_gpio_chip {
	struct gpio_chip chip;
	struct irq_domain *domain;
	unsigned int index;
	unsigned int irq;
};

/*
 * Return the GPIO_BASE for the GPIO chip
 */
static unsigned int get_gpio_base(unsigned int index) {
	switch (index) {
	case 0:
		return GPIO_BASE_A;
	case 1:
		return GPIO_BASE_B;
	case 2:
		return GPIO_BASE_C;
	case 3:
		return GPIO_BASE_D;
	case 4:
		return GPIO_BASE_E;
	case 5:
		return GPIO_BASE_F;
	case 6:
		return GPIO_BASE_G;
	case 7:
		return GPIO_BASE_H;
	default:
		/* We should never be here */
		return -EINVAL;
	}
}

static void __iomem *get_output_enable(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) + GPIO_REG_OE_OFFSET);
}

static void __iomem *get_output(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) + GPIO_REG_OUTPUT_OFFSET);
}

static void __iomem *get_input(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) + GPIO_REG_INPUT_OFFSET);
}

static void __iomem *get_master_select(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) +
		GPIO_REG_MASTER_SEL_OFFSET);
}

static void __iomem *get_irq_stat(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) +
		GPIO_REG_INT_STAT_OFFSET);
}

static void __iomem *get_irq_enable(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) +
		GPIO_REG_INT_ENABLE_OFFSET);
}

static void __iomem *get_irq_type(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) +
		GPIO_REG_INT_TYPE_OFFSET);
}

static void __iomem *get_irq_polarity(unsigned int index)
{
	return (void __iomem *) (get_gpio_base(index) +
		GPIO_REG_POLARITY_OFFSET);
}

static int chorus2_gpio_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	u32 value;
	struct chorus2_gpio_chip *chorus2;
	void __iomem *output_enable;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	output_enable = get_output_enable(chorus2->index);

	value = 0x00010000 << offset;
	__raw_writel(value, output_enable);

	return 0;
}

static int chorus2_gpio_direction_output(struct gpio_chip *chip,
					 unsigned offset, int output_value)
{
	u32 value;
	struct chorus2_gpio_chip *chorus2;
	void __iomem *output_enable;
	void __iomem *output;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	output_enable = get_output_enable(chorus2->index);
	output = get_output(chorus2->index);

	value = 0x00010001 << offset;
	__raw_writel(value, output_enable);

	value = (0x00010000 | (output_value & 0x1)) << offset;
	__raw_writel(value, output);

	return 0;
}

/*
 * Return GPIO level
 */
static int chorus2_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct chorus2_gpio_chip *chorus2;
	void __iomem *input;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	input = get_input(chorus2->index);

	return __raw_readl(input) & (1 << offset);
}

/*
 * Set output GPIO level
 */
static void chorus2_gpio_set(struct gpio_chip *chip, unsigned offset,
			     int output_value)
{
	u32 value;
	struct chorus2_gpio_chip *chorus2;
	void __iomem *output;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	output = get_output(chorus2->index);

	value = (0x00010000 | (output_value & 0x1)) << offset;
	__raw_writel(value, output);
}

static int chorus2_gpio_gpio(struct gpio_chip *chip, unsigned offset)
{
	u32 value;
	struct chorus2_gpio_chip *chorus2;
	void __iomem *master_select;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	master_select = get_master_select(chorus2->index);

	/* write select bit and '1' for that pin */
	value = 0x00010001 << offset;
	__raw_writel(value, master_select);

	return chorus2_gpio_direction_input(chip, offset);
}

void chorus2_gpio_mastermode(struct gpio_chip *chip, unsigned offset)
{
	u32 value;
	struct chorus2_gpio_chip *chorus2;
	void __iomem *master_select;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	master_select = get_master_select(chorus2->index);

	/* clear select bit for that pin */
	value = 0x00010000 << offset;
	__raw_writel(value, master_select);
}

static int chorus2_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct chorus2_gpio_chip *chorus2;

	chorus2 = container_of(chip, struct chorus2_gpio_chip, chip);
	return irq_create_mapping(chorus2->domain, offset);
}

static struct chorus2_gpio_chip chorus2_gpio_chip[] = {
	[0] = {
		.index = 0,
		.irq = GPIO_A_IRQ_NUM,
		.chip = {
			.label            = "gpio-A",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 0,
			.ngpio            = 16,
		},
	},
	[1] = {
		.index = 1,
		.irq = GPIO_B_IRQ_NUM,
		.chip = {
			.label            = "gpio-B",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 16,
			.ngpio            = 16,
		},
	},
	[2] = {
		.index = 2,
		.irq = GPIO_C_IRQ_NUM,
		.chip = {
			.label            = "gpio-C",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 32,
			.ngpio            = 16,
		},
	},
	[3] = {
		.index = 3,
		.irq = GPIO_D_IRQ_NUM,
		.chip = {
			.label            = "gpio-D",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 48,
			.ngpio            = 16,
		},
	},
	[4] = {
		.index = 4,
		.irq = GPIO_E_IRQ_NUM,
		.chip = {
			.label            = "gpio-E",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 64,
			.ngpio            = 16,
		},
	},
	[5] = {
		.index = 5,
		.irq = GPIO_F_IRQ_NUM,
		.chip = {
			.label            = "gpio-F",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 80,
			.ngpio            = 16,
		},
	},
	[6] = {
		.index = 6,
		.irq = GPIO_G_IRQ_NUM,
		.chip = {
			.label            = "gpio-G",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 96,
			.ngpio            = 16,
		},
	},
	[7] = {
		.index = 7,
		.irq = GPIO_H_IRQ_NUM,
		.chip = {
			.label            = "gpio-H",
			.direction_input  = chorus2_gpio_direction_input,
			.direction_output = chorus2_gpio_direction_output,
			.get              = chorus2_gpio_get,
			.set              = chorus2_gpio_set,
			.free             = chorus2_gpio_mastermode,
			.request          = chorus2_gpio_gpio,
			.to_irq           = chorus2_gpio_to_irq,
			.base             = 112,
			.ngpio            = 11,
		},
	},
};

/*
 * Return chip number for gpio
 */
static int chorus2_gpio_to_chip(unsigned int gpio)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chorus2_gpio_chip); i++) {
		if (gpio >= chorus2_gpio_chip[i].chip.base &&
		    gpio < (chorus2_gpio_chip[i].chip.base +
			    chorus2_gpio_chip[i].chip.ngpio)) {
				return i;
		}
	}
	return -EINVAL;
}

int chorus2_gpio_disable(unsigned int gpio)
{
	struct chorus2_gpio_chip *chorus2 = NULL;
	unsigned int offset = 0, value;
	void __iomem *master_select;
	int idx;

	idx = chorus2_gpio_to_chip(gpio);

	if (idx < 0)
		return -EINVAL;

	chorus2 = &chorus2_gpio_chip[idx];
	offset = gpio - chorus2_gpio_chip[idx].chip.base;

	master_select = get_master_select(chorus2->index);

	/* clear select bit for that pin */
	value = 0x00010000 << offset;
	__raw_writel(value, master_select);

	return 0;
}
EXPORT_SYMBOL(chorus2_gpio_disable);

/* Get Chorus2 GPIO chip from irq data provided to generic IRQ callbacks */
static struct chorus2_gpio_chip *irqd_to_chorus2_gpio_chip(
						struct irq_data *data)
{
	return (struct chorus2_gpio_chip *)data->domain->host_data;
}

/*
 * Clear interrupt status register for gpio module
 */
static void chorus2_gpio_irq_clear(struct chorus2_gpio_chip *chorus2,
				   unsigned int offset)
{
	unsigned int value;
	void __iomem *int_stat;

	int_stat = get_irq_stat(chorus2->index);
	/*
	 * Clear interrupt that fired without checking
	 * the value of int_stat again as it may contain new
	 * interrupts
	 */
	value = 0x00010000 << offset;
	__raw_writel(value, int_stat);
}

static void chorus2_gpio_irq_enable(struct chorus2_gpio_chip *chorus2,
				    unsigned int offset)
{
	unsigned int value;
	void __iomem *irq_enable;

	irq_enable = get_irq_enable(chorus2->index);

	/* clear select bit for that pin */
	value = 0x00010001 << offset;
	__raw_writel(value, irq_enable);
}

static void chorus2_gpio_irq_disable(struct chorus2_gpio_chip *chorus2,
				     unsigned int offset)
{
	unsigned int value;
	void __iomem *irq_enable;

	irq_enable = get_irq_enable(chorus2->index);

	/* clear select bit for that pin */
	value = 0x00010000 << offset;
	__raw_writel(value, irq_enable);
}

static void chorus2_gpio_irq_polarity(struct chorus2_gpio_chip *chorus2,
				      unsigned int offset,
				      unsigned int polarity)
{
	unsigned int value;
	void __iomem *irq_polarity;

	irq_polarity = get_irq_polarity(chorus2->index);

	value = (0x00010000 | (polarity & 0x1)) << offset;
	__raw_writel(value, irq_polarity);
}

static int chorus2_gpio_valid_handler(struct irq_desc *desc)
{
	return desc->handle_irq == handle_level_irq ||
		desc->handle_irq == handle_edge_irq;
}

static void chorus2_gpio_irq_type(struct chorus2_gpio_chip *chorus2,
				  unsigned int offset, unsigned int type)
{
	unsigned int value;
	void __iomem *irq_type;

	irq_type = get_irq_type(chorus2->index);

	value = (0x00010000 | (type & 0x1)) << offset;
	__raw_writel(value, irq_type);
}

/*
 * set polarity to trigger on next edge, whether rising or falling
 * @chorus2: gpio chip
 * @offset: offset of gpio within chip
 */
static void chorus2_gpio_irq_next_edge(struct chorus2_gpio_chip *chorus2,
				       unsigned int offset)
{
	unsigned int value_p, value_i, int_type;
	void __iomem *irq_polarity, *input;
	int lstat;

	irq_polarity = get_irq_polarity(chorus2->index);
	input = get_input(chorus2->index);
	int_type = __raw_readl(get_irq_type(chorus2->index));

	__global_lock2(lstat);
	value_i = ~(__raw_readl(input));
	value_p = __raw_readl(irq_polarity);
	value_p &= ~(0x1 << offset);
	value_p |= (0x00010000|((value_i >> offset) & 0x1)) << offset;
	__raw_writel(value_p, irq_polarity);
	__global_unlock2(lstat);
}

static void gpio_ack_irq(struct irq_data *data)
{
	struct chorus2_gpio_chip *chorus2 = irqd_to_chorus2_gpio_chip(data);

	chorus2_gpio_irq_clear(chorus2, data->hwirq);
}

static void gpio_mask_irq(struct irq_data *data)
{
	struct chorus2_gpio_chip *chorus2 = irqd_to_chorus2_gpio_chip(data);

	chorus2_gpio_irq_disable(chorus2, data->hwirq);
}

static void gpio_unmask_irq(struct irq_data *data)
{
	struct chorus2_gpio_chip *chorus2 = irqd_to_chorus2_gpio_chip(data);

	chorus2_gpio_irq_enable(chorus2, data->hwirq);
}

static unsigned int gpio_startup_irq(struct irq_data *data)
{
	struct chorus2_gpio_chip *chorus2 = irqd_to_chorus2_gpio_chip(data);
	irq_hw_number_t hw = data->hwirq;
	struct irq_desc *desc = irq_to_desc(data->irq);

	/*
	 * This warning indicates that the type of the irq hasn't been set
	 * before enabling the irq. This would normally be done by passing some
	 * trigger flags to request_irq().
	 */
	WARN(!chorus2_gpio_valid_handler(desc),
		"irq type not set before enabling gpio irq %d", data->irq);

	chorus2_gpio_irq_clear(chorus2, hw);
	chorus2_gpio_irq_enable(chorus2, hw);
	return 0;
}

static int gpio_set_irq_type(struct irq_data *data, unsigned int flow_type)
{
	struct chorus2_gpio_chip *chorus2 = irqd_to_chorus2_gpio_chip(data);
	unsigned int type;
	unsigned int polarity;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_BOTH:
		type = GPIO_EDGE_TRIGGERED;
		polarity = GPIO_POLARITY_LOW;
		break;
	case IRQ_TYPE_EDGE_RISING:
		type = GPIO_EDGE_TRIGGERED;
		polarity = GPIO_POLARITY_HIGH;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = GPIO_EDGE_TRIGGERED;
		polarity = GPIO_POLARITY_LOW;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type = GPIO_LEVEL_TRIGGERED;
		polarity = GPIO_POLARITY_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = GPIO_LEVEL_TRIGGERED;
		polarity = GPIO_POLARITY_LOW;
		break;
	default:
		return -EINVAL;
	}

	chorus2_gpio_irq_type(chorus2, data->hwirq, type);
	if (type == GPIO_LEVEL_TRIGGERED)
		__irq_set_handler_locked(data->irq, handle_level_irq);
	else
		__irq_set_handler_locked(data->irq, handle_edge_irq);

	if (flow_type == IRQ_TYPE_EDGE_BOTH)
		chorus2_gpio_irq_next_edge(chorus2, data->hwirq);
	else
		chorus2_gpio_irq_polarity(chorus2, data->hwirq, polarity);

	return 0;
}

/* gpio virtual interrupt functions */
static struct irq_chip gpio_irq_chip = {
	.irq_startup = gpio_startup_irq,
	.irq_ack = gpio_ack_irq,
	.irq_mask = gpio_mask_irq,
	.irq_unmask = gpio_unmask_irq,
	.irq_set_type = gpio_set_irq_type,
};

/*
 * handler for real IRQ lines. Clears the gpio pin that triggered the irq and
 * setup the handler for virtual irq line.
 * @irq: real irq number
 * @desc: irq descriptor
 */
static void chorus2_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	irq_hw_number_t hw;
	unsigned int irq_stat, irq_no;
	struct chorus2_gpio_chip *port;
	struct irq_desc *child_desc;

	port = (struct chorus2_gpio_chip *)irq_desc_get_handler_data(desc);
	/*
	 * for irq to be valid, the gpio pin has to be configured as input,
	 * set as interrupt in interrupt enable register, and triggered in
	 * interrupt status register.
	 */
	irq_stat = __raw_readl(get_irq_stat(port->index)) &
			__raw_readl(get_irq_enable(port->index)) &
			~__raw_readl(get_output_enable(port->index));

	for (hw = 0; irq_stat; irq_stat >>= 1, ++hw) {
		if (!(irq_stat & 1))
			continue;

		irq_no = irq_linear_revmap(port->domain, hw);
		child_desc = irq_to_desc(irq_no);

		/* Toggle edge for pin with both edge triggering enabled */
		if (irqd_get_trigger_type(&child_desc->irq_data)
				== IRQ_TYPE_EDGE_BOTH)
			chorus2_gpio_irq_next_edge(port, hw);

		BUG_ON(!chorus2_gpio_valid_handler(child_desc));
		/* Call the device handler for virtual irq */
		generic_handle_irq_desc(irq_no, child_desc);
	}
}

static int chorus2_gpio_irq_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hw)
{
	irq_set_chip(irq, &gpio_irq_chip);
	return 0;
}

static const struct irq_domain_ops chorus2_gpio_irq_domain_ops = {
	.map = chorus2_gpio_irq_map,
};

void __init chorus2_init_gpio(void)
{
	int i, ret, irq;

	for (i = 0; i < ARRAY_SIZE(chorus2_gpio_chip); i++) {
		ret = gpiochip_add(&chorus2_gpio_chip[i].chip);
		if (ret) {
			pr_warning("gpio: Unable to register gpio block for IRQ %d\n",
				chorus2_gpio_chip[i].irq);
			break;
		}

		irq = external_irq_map(chorus2_gpio_chip[i].irq);
		if (irq < 0) {
			pr_err("%s: unable to map GPIO block %d irq %u (%d)\n",
			       __func__, i, chorus2_gpio_chip[i].irq, irq);
			continue;
		}
		chorus2_gpio_chip[i].irq = irq;

		pr_info("Setting up virtual IRQs for GPIO block %d\n", i);

		/* Add virtual irqs for each gpio */
		chorus2_gpio_chip[i].domain = irq_domain_add_linear(
					NULL,
					chorus2_gpio_chip[i].chip.ngpio,
					&chorus2_gpio_irq_domain_ops,
					&chorus2_gpio_chip[i]);
		/* Setup Chained handler for this gpio block */
		irq_set_handler_data(irq, &chorus2_gpio_chip[i]);
		irq_set_chained_handler(irq, chorus2_gpio_irq_handler);
	}
}

