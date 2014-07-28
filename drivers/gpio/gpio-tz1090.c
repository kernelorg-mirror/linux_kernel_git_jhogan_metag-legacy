/*
 * Toumaz Xenif TZ1090 GPIO handling.
 *
 * Copyright (C) 2008-2013 Imagination Technologies Ltd.
 *
 *  Based on ARM PXA code and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <asm/global_lock.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/gpio.h>

/* Register offsets from bank base address */
#define REG_GPIO_DIR		0x00
#define REG_GPIO_SELECT		0x10
#define REG_GPIO_IRQ_PLRT	0x20
#define REG_GPIO_IRQ_TYPE	0x30
#define REG_GPIO_IRQ_EN		0x40
#define REG_GPIO_IRQ_STS	0x50
#define REG_GPIO_BIT_EN		0x60
#define REG_GPIO_DIN		0x70
#define REG_GPIO_DOUT		0x80
#define REG_GPIO_PU_PD		0xa0

/* REG_GPIO_IRQ_PLRT */
#define REG_GPIO_IRQ_PLRT_LOW	0
#define REG_GPIO_IRQ_PLRT_HIGH	1

/* REG_GPIO_IRQ_TYPE */
#define REG_GPIO_IRQ_TYPE_LEVEL	0
#define REG_GPIO_IRQ_TYPE_EDGE	1

struct comet_gpio_pullup {
	unsigned char index;
	unsigned char offset;
};

/**
 * struct tz1090_gpio_bank - GPIO bank private data
 * @chip:	Generic GPIO chip for GPIO bank
 * @domain:	IRQ domain for GPIO bank (may be NULL)
 * @reg:	Base of registers, offset for this GPIO bank
 * @irq:	IRQ number for GPIO bank
 * @label:	Debug GPIO bank label, used for storage of chip->label
 *
 * This is the main private data for a GPIO bank. It encapsulates a gpio_chip,
 * and the callbacks for the gpio_chip can access the private data with the
 * to_bank() macro below.
 */
struct tz1090_gpio_bank {
	struct gpio_chip chip;
	struct irq_domain *domain;
	void __iomem *reg;
	int irq;
	char label[16];
};
#define to_bank(c)	container_of(c, struct tz1090_gpio_bank, chip)

/**
 * struct tz1090_gpio - Overall GPIO device private data
 * @dev:	Device (from platform device)
 * @reg:	Base of GPIO registers
 *
 * Represents the overall GPIO device. This structure is actually only
 * temporary, and used during init.
 */
struct tz1090_gpio {
	struct device *dev;
	void __iomem *reg;
};

/**
 * struct tz1090_gpio_bank_info - Temporary registration info for GPIO bank
 * @priv:	Overall GPIO device private data
 * @node:	Device tree node specific to this GPIO bank
 * @index:	Index of bank in range 0-2
 */
struct tz1090_gpio_bank_info {
	struct tz1090_gpio *priv;
	struct device_node *node;
	unsigned int index;
};

/* Convenience register accessors */
static inline void tz1090_gpio_write(struct tz1090_gpio_bank *bank,
			      unsigned int reg_offs, u32 data)
{
	iowrite32(data, bank->reg + reg_offs);
}

static inline u32 tz1090_gpio_read(struct tz1090_gpio_bank *bank,
			    unsigned int reg_offs)
{
	return ioread32(bank->reg + reg_offs);
}

/* caller must hold LOCK2 */
static inline void _tz1090_gpio_clear_bit(struct tz1090_gpio_bank *bank,
					  unsigned int reg_offs,
					  unsigned int offset)
{
	u32 value;

	value = tz1090_gpio_read(bank, reg_offs);
	value &= ~BIT(offset);
	tz1090_gpio_write(bank, reg_offs, value);
}

static void tz1090_gpio_clear_bit(struct tz1090_gpio_bank *bank,
				  unsigned int reg_offs,
				  unsigned int offset)
{
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_clear_bit(bank, reg_offs, offset);
	__global_unlock2(lstat);
}

/* caller must hold LOCK2 */
static inline void _tz1090_gpio_set_bit(struct tz1090_gpio_bank *bank,
					unsigned int reg_offs,
					unsigned int offset)
{
	u32 value;

	value = tz1090_gpio_read(bank, reg_offs);
	value |= BIT(offset);
	tz1090_gpio_write(bank, reg_offs, value);
}

static void tz1090_gpio_set_bit(struct tz1090_gpio_bank *bank,
				unsigned int reg_offs,
				unsigned int offset)
{
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_set_bit(bank, reg_offs, offset);
	__global_unlock2(lstat);
}

/* caller must hold LOCK2 */
static inline void _tz1090_gpio_mod_bit(struct tz1090_gpio_bank *bank,
					unsigned int reg_offs,
					unsigned int offset,
					bool val)
{
	u32 value;

	value = tz1090_gpio_read(bank, reg_offs);
	value &= ~BIT(offset);
	if (val)
		value |= BIT(offset);
	tz1090_gpio_write(bank, reg_offs, value);
}

static void tz1090_gpio_mod_bit(struct tz1090_gpio_bank *bank,
				unsigned int reg_offs,
				unsigned int offset,
				bool val)
{
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_mod_bit(bank, reg_offs, offset, val);
	__global_unlock2(lstat);
}

static inline int tz1090_gpio_read_bit(struct tz1090_gpio_bank *bank,
				       unsigned int reg_offs,
				       unsigned int offset)
{
	return tz1090_gpio_read(bank, reg_offs) & BIT(offset);
}

/* GPIO chip callbacks */

static int tz1090_gpio_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);
	tz1090_gpio_set_bit(bank, REG_GPIO_DIR, offset);

	return 0;
}

static int tz1090_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int output_value)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_mod_bit(bank, REG_GPIO_DOUT, offset, output_value);
	_tz1090_gpio_clear_bit(bank, REG_GPIO_DIR, offset);
	__global_unlock2(lstat);

	return 0;
}

/*
 * Return GPIO level
 */
static int tz1090_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);

	return tz1090_gpio_read_bit(bank, REG_GPIO_DIN, offset);
}

/*
 * Set output GPIO level
 */
static void tz1090_gpio_set(struct gpio_chip *chip, unsigned offset,
			    int output_value)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);

	tz1090_gpio_mod_bit(bank, REG_GPIO_DOUT, offset, output_value);
}

static int tz1090_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);
	int ret;

	ret = pinctrl_request_gpio(chip->base + offset);
	if (ret)
		return ret;

	tz1090_gpio_set_bit(bank, REG_GPIO_DIR, offset);
	tz1090_gpio_set_bit(bank, REG_GPIO_BIT_EN, offset);

	return 0;
}

static void tz1090_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);

	pinctrl_free_gpio(chip->base + offset);

	tz1090_gpio_clear_bit(bank, REG_GPIO_BIT_EN, offset);
}

static int tz1090_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct tz1090_gpio_bank *bank = to_bank(chip);

	if (!bank->domain)
		return -EINVAL;

	return irq_create_mapping(bank->domain, offset);
}

static struct tz1090_gpio_bank *comet_gpio_chip[3];

/* The mapping of GPIO to pull register index and offset */
static struct comet_gpio_pullup gpio_pullup_table[NR_BUILTIN_GPIO] = {
	{5, 22}, /*  0 - GPIO_SDIO_CLK */
	{0, 14}, /*  1 - GPIO_SDIO_CMD */
	{0,  6}, /*  2 - GPIO_SDIO_D0 */
	{0,  8}, /*  3 - GPIO_SDIO_D1 */
	{0, 10}, /*  4 - GPIO_SDIO_D2 */
	{0, 12}, /*  5 - GPIO_SDIO_D3 */
	{0,  2}, /*  6 - GPIO_SDH_CD */
	{0,  4}, /*  7 - GPIO_SDH_WP */
	{0, 16}, /*  8 - GPIO_SPI0_MCLK */
	{0, 18}, /*  9 - GPIO_SPI0_CS0 */
	{0, 20}, /* 10 - GPIO_SPI0_CS1 */
	{0, 22}, /* 11 - GPIO_SPI0_CS2 */
	{0, 24}, /* 12 - GPIO_SPI0_DOUT */
	{0, 26}, /* 13 - GPIO_SPI0_DIN */
	{0, 28}, /* 14 - GPIO_SPI1_MCLK */
	{0, 30}, /* 15 - GPIO_SPI1_CS0 */
	{1,  0}, /* 16 - GPIO_SPI1_CS1 */
	{1,  2}, /* 17 - GPIO_SPI1_CS2 */
	{1,  4}, /* 18 - GPIO_SPI1_DOUT */
	{1,  6}, /* 19 - GPIO_SPI1_DIN */
	{1,  8}, /* 20 - GPIO_UART0_RXD */
	{1, 10}, /* 21 - GPIO_UART0_TXD */
	{1, 12}, /* 22 - GPIO_UART0_CTS */
	{1, 14}, /* 23 - GPIO_UART0_RTS */
	{1, 16}, /* 24 - GPIO_UART1_RXD */
	{1, 18}, /* 25 - GPIO_UART1_TXD */
	{1, 20}, /* 26 - GPIO_SCB0_SDAT */
	{1, 22}, /* 27 - GPIO_SCB0_SCLK */
	{1, 24}, /* 28 - GPIO_SCB1_SDAT */
	{1, 26}, /* 29 - GPIO_SCB1_SCLK */

	{1, 28}, /* 30 - GPIO_SCB2_SDAT */
	{1, 30}, /* 31 - GPIO_SCB2_SCLK */
	{2,  0}, /* 32 - GPIO_I2S_MCLK */
	{2,  2}, /* 33 - GPIO_I2S_BCLK_OUT */
	{2,  4}, /* 34 - GPIO_I2S_LRCLK_OUT */
	{2,  6}, /* 35 - GPIO_I2S_DOUT0 */
	{2,  8}, /* 36 - GPIO_I2S_DOUT1 */
	{2, 10}, /* 37 - GPIO_I2S_DOUT2 */
	{2, 12}, /* 38 - GPIO_I2S_DIN */
	{4, 12}, /* 39 - GPIO_PDM_A */
	{4, 14}, /* 40 - GPIO_PDM_B */
	{4, 18}, /* 41 - GPIO_PDM_C */
	{4, 20}, /* 42 - GPIO_PDM_D */
	{2, 14}, /* 43 - GPIO_TFT_RED0 */
	{2, 16}, /* 44 - GPIO_TFT_RED1 */
	{2, 18}, /* 45 - GPIO_TFT_RED2 */
	{2, 20}, /* 46 - GPIO_TFT_RED3 */
	{2, 22}, /* 47 - GPIO_TFT_RED4 */
	{2, 24}, /* 48 - GPIO_TFT_RED5 */
	{2, 26}, /* 49 - GPIO_TFT_RED6 */
	{2, 28}, /* 50 - GPIO_TFT_RED7 */
	{2, 30}, /* 51 - GPIO_TFT_GREEN0 */
	{3,  0}, /* 52 - GPIO_TFT_GREEN1 */
	{3,  2}, /* 53 - GPIO_TFT_GREEN2 */
	{3,  4}, /* 54 - GPIO_TFT_GREEN3 */
	{3,  6}, /* 55 - GPIO_TFT_GREEN4 */
	{3,  8}, /* 56 - GPIO_TFT_GREEN5 */
	{3, 10}, /* 57 - GPIO_TFT_GREEN6 */
	{3, 12}, /* 58 - GPIO_TFT_GREEN7 */
	{3, 14}, /* 59 - GPIO_TFT_BLUE0 */

	{3, 16}, /* 60 - GPIO_TFT_BLUE1 */
	{3, 18}, /* 61 - GPIO_TFT_BLUE2 */
	{3, 20}, /* 62 - GPIO_TFT_BLUE3 */
	{3, 22}, /* 63 - GPIO_TFT_BLUE4 */
	{3, 24}, /* 64 - GPIO_TFT_BLUE5 */
	{3, 26}, /* 65 - GPIO_TFT_BLUE6 */
	{3, 28}, /* 66 - GPIO_TFT_BLUE7 */
	{3, 30}, /* 67 - GPIO_TFT_VDDEN_GD */
	{4,  0}, /* 68 - GPIO_TFT_PANELCLK */
	{4,  2}, /* 69 - GPIO_TFT_BLANK_LS */
	{4,  4}, /* 70 - GPIO_TFT_VSYNC_NS */
	{4,  6}, /* 71 - GPIO_TFT_HSYNC_NR */
	{4,  8}, /* 72 - GPIO_TFT_VD12ACB */
	{4, 10}, /* 73 - GPIO_TFT_PWRSAVE */
	{4, 24}, /* 74 - GPIO_TX_ON */
	{4, 26}, /* 75 - GPIO_RX_ON */
	{4, 28}, /* 76 - GPIO_PLL_ON */
	{4, 30}, /* 77 - GPIO_PA_ON */
	{5,  0}, /* 78 - GPIO_RX_HP */
	{5,  6}, /* 79 - GPIO_GAIN0 */
	{5,  8}, /* 80 - GPIO_GAIN1 */
	{5, 10}, /* 81 - GPIO_GAIN2 */
	{5, 12}, /* 82 - GPIO_GAIN3 */
	{5, 14}, /* 83 - GPIO_GAIN4 */
	{5, 16}, /* 84 - GPIO_GAIN5 */
	{5, 18}, /* 85 - GPIO_GAIN6 */
	{5, 20}, /* 86 - GPIO_GAIN7 */
	{5,  2}, /* 87 - GPIO_ANT_SEL0 */
	{5,  4}, /* 88 - GPIO_ANT_SEL1 */
	{0,  0}, /* 89 - GPIO_SDH_CLK_IN */
};

static int comet_gpio_to_chip(unsigned int gpio)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(comet_gpio_chip); i++) {
		if (!comet_gpio_chip[i])
			continue;
		if (gpio >= comet_gpio_chip[i]->chip.base &&
		    gpio < (comet_gpio_chip[i]->chip.base +
			    comet_gpio_chip[i]->chip.ngpio)) {
				return i;
		}
	}
	return -EINVAL;
}

int comet_gpio_disable(unsigned int gpio)
{
	struct tz1090_gpio_bank *bank = NULL;
	unsigned int offset = 0;
	int idx;

	idx = comet_gpio_to_chip(gpio);

	if (idx < 0)
		return -EINVAL;

	bank = comet_gpio_chip[idx];
	offset = gpio - bank->chip.base;

	tz1090_gpio_clear_bit(bank, REG_GPIO_SELECT, offset);

	return 0;
}
EXPORT_SYMBOL(comet_gpio_disable);

int comet_gpio_enable(unsigned int gpio)
{
	struct tz1090_gpio_bank *bank = NULL;
	unsigned int offset = 0;
	int idx;

	idx = comet_gpio_to_chip(gpio);

	if (idx < 0)
		return -EINVAL;

	bank = comet_gpio_chip[idx];
	offset = gpio - bank->chip.base;

	tz1090_gpio_set_bit(bank, REG_GPIO_SELECT, offset);

	return 0;
}
EXPORT_SYMBOL(comet_gpio_enable);


int comet_gpio_disable_block(unsigned int first, unsigned int last)
{
	struct tz1090_gpio_bank *bank = NULL;
	unsigned int offset = 0;
	int gpio;
	int idx;
	int lstat;
	int result = 0;

	__global_lock2(lstat);
	for (gpio = first; gpio <= last; ++gpio) {
		idx = comet_gpio_to_chip(gpio);

		if (idx < 0) {
			result = -EINVAL;
			goto out;
		}
		++result;

		bank = comet_gpio_chip[idx];
		offset = gpio - bank->chip.base;

		_tz1090_gpio_clear_bit(bank, REG_GPIO_SELECT, offset);
	}
out:
	__global_unlock2(lstat);

	return result;
}
EXPORT_SYMBOL(comet_gpio_disable_block);

int comet_gpio_enable_block(unsigned int first, unsigned int last)
{
	struct tz1090_gpio_bank *bank = NULL;
	unsigned int offset = 0;
	int gpio;
	int idx;
	int lstat;
	int result = 0;

	__global_lock2(lstat);
	for (gpio = first; gpio <= last; ++gpio) {
		idx = comet_gpio_to_chip(gpio);

		if (idx < 0) {
			result = -EINVAL;
			goto out;
		}
		++result;

		bank = comet_gpio_chip[idx];
		offset = gpio - bank->chip.base;

		tz1090_gpio_set_bit(bank, REG_GPIO_SELECT, offset);
	}
out:
	__global_unlock2(lstat);

	return result;
}
EXPORT_SYMBOL(comet_gpio_enable_block);

int comet_gpio_pullup_type(unsigned int gpio, unsigned int pullup)
{
	struct tz1090_gpio_bank *bank = NULL;
	struct comet_gpio_pullup *gpio_pullup;
	unsigned int offset = 0, index, value;
	int idx;
	int lstat;

	idx = comet_gpio_to_chip(gpio);

	if (idx < 0)
		return -EINVAL;

	bank = comet_gpio_chip[0];
	gpio_pullup = &gpio_pullup_table[gpio];
	index = gpio_pullup->index << 2;
	offset = gpio_pullup->offset;

	__global_lock2(lstat);
	value = tz1090_gpio_read(bank, REG_GPIO_PU_PD + index);
	value &= ~(0x3 << offset);
	value |= (pullup & 0x3) << offset;
	tz1090_gpio_write(bank, REG_GPIO_PU_PD + index, value);
	__global_unlock2(lstat);

	return 0;
}
EXPORT_SYMBOL(comet_gpio_pullup_type);

/* IRQ chip handlers */

/* Get TZ1090 GPIO chip from irq data provided to generic IRQ callbacks */
static inline struct tz1090_gpio_bank *irqd_to_gpio_bank(struct irq_data *data)
{
	return (struct tz1090_gpio_bank *)data->domain->host_data;
}

static void tz1090_gpio_irq_clear(struct tz1090_gpio_bank *bank,
				  unsigned int offset)
{
	tz1090_gpio_clear_bit(bank, REG_GPIO_IRQ_STS, offset);
}

static void tz1090_gpio_irq_enable(struct tz1090_gpio_bank *bank,
				   unsigned int offset, bool enable)
{
	tz1090_gpio_mod_bit(bank, REG_GPIO_IRQ_EN, offset, enable);
}

static void tz1090_gpio_irq_polarity(struct tz1090_gpio_bank *bank,
				     unsigned int offset, unsigned int polarity)
{
	tz1090_gpio_mod_bit(bank, REG_GPIO_IRQ_PLRT, offset, polarity);
}

static int tz1090_gpio_valid_handler(struct irq_desc *desc)
{
	return desc->handle_irq == handle_level_irq ||
		desc->handle_irq == handle_edge_irq;
}

static void tz1090_gpio_irq_type(struct tz1090_gpio_bank *bank,
				 unsigned int offset, unsigned int type)
{
	tz1090_gpio_mod_bit(bank, REG_GPIO_IRQ_TYPE, offset, type);
}

/* set polarity to trigger on next edge, whether rising or falling */
static void tz1090_gpio_irq_next_edge(struct tz1090_gpio_bank *bank,
				      unsigned int offset)
{
	unsigned int value_p, value_i;
	int lstat;

	/*
	 * Set the GPIO's interrupt polarity to the opposite of the current
	 * input value so that the next edge triggers an interrupt.
	 */
	__global_lock2(lstat);
	value_i = ~tz1090_gpio_read(bank, REG_GPIO_DIN);
	value_p = tz1090_gpio_read(bank, REG_GPIO_IRQ_PLRT);
	value_p &= ~BIT(offset);
	value_p |= value_i & BIT(offset);
	tz1090_gpio_write(bank, REG_GPIO_IRQ_PLRT, value_p);
	__global_unlock2(lstat);
}

static void gpio_ack_irq(struct irq_data *data)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);

	tz1090_gpio_irq_clear(bank, data->hwirq);
}

static void gpio_mask_irq(struct irq_data *data)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);

	tz1090_gpio_irq_enable(bank, data->hwirq, false);
}

static void gpio_unmask_irq(struct irq_data *data)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);

	tz1090_gpio_irq_enable(bank, data->hwirq, true);
}

static unsigned int gpio_startup_irq(struct irq_data *data)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);
	irq_hw_number_t hw = data->hwirq;
	struct irq_desc *desc = irq_to_desc(data->irq);

	/*
	 * This warning indicates that the type of the irq hasn't been set
	 * before enabling the irq. This would normally be done by passing some
	 * trigger flags to request_irq().
	 */
	WARN(!tz1090_gpio_valid_handler(desc),
		"irq type not set before enabling gpio irq %d", data->irq);

	tz1090_gpio_irq_clear(bank, hw);
	tz1090_gpio_irq_enable(bank, hw, true);
	return 0;
}

static int gpio_set_irq_type(struct irq_data *data, unsigned int flow_type)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);
	unsigned int type;
	unsigned int polarity;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_BOTH:
		type = REG_GPIO_IRQ_TYPE_EDGE;
		polarity = REG_GPIO_IRQ_PLRT_LOW;
		break;
	case IRQ_TYPE_EDGE_RISING:
		type = REG_GPIO_IRQ_TYPE_EDGE;
		polarity = REG_GPIO_IRQ_PLRT_HIGH;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = REG_GPIO_IRQ_TYPE_EDGE;
		polarity = REG_GPIO_IRQ_PLRT_LOW;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type = REG_GPIO_IRQ_TYPE_LEVEL;
		polarity = REG_GPIO_IRQ_PLRT_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = REG_GPIO_IRQ_TYPE_LEVEL;
		polarity = REG_GPIO_IRQ_PLRT_LOW;
		break;
	default:
		return -EINVAL;
	}

	tz1090_gpio_irq_type(bank, data->hwirq, type);
	if (type == REG_GPIO_IRQ_TYPE_LEVEL)
		__irq_set_handler_locked(data->irq, handle_level_irq);
	else
		__irq_set_handler_locked(data->irq, handle_edge_irq);

	if (flow_type == IRQ_TYPE_EDGE_BOTH)
		tz1090_gpio_irq_next_edge(bank, data->hwirq);
	else
		tz1090_gpio_irq_polarity(bank, data->hwirq, polarity);

	return 0;
}

#ifdef CONFIG_SUSPEND
static int gpio_set_irq_wake(struct irq_data *data, unsigned int on)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);

#ifdef CONFIG_PM_DEBUG
	pr_info("irq_wake irq%d state:%d\n", data->irq, on);
#endif

	/* wake on gpio block interrupt */
	return irq_set_irq_wake(bank->irq, on);
}
#else
#define gpio_set_irq_wake NULL
#endif

/* gpio virtual interrupt functions */
static struct irq_chip gpio_irq_chip = {
	.irq_startup	= gpio_startup_irq,
	.irq_ack	= gpio_ack_irq,
	.irq_mask	= gpio_mask_irq,
	.irq_unmask	= gpio_unmask_irq,
	.irq_set_type	= gpio_set_irq_type,
	.irq_set_wake	= gpio_set_irq_wake,
	.flags		= IRQCHIP_MASK_ON_SUSPEND,
};

static void tz1090_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	irq_hw_number_t hw;
	unsigned int irq_stat, irq_no;
	struct tz1090_gpio_bank *bank;
	struct irq_desc *child_desc;

	bank = (struct tz1090_gpio_bank *)irq_desc_get_handler_data(desc);
	irq_stat = tz1090_gpio_read(bank, REG_GPIO_DIR) &
		   tz1090_gpio_read(bank, REG_GPIO_IRQ_STS) &
		   tz1090_gpio_read(bank, REG_GPIO_IRQ_EN) &
		   0x3FFFFFFF; /* 30 bits only */

	for (hw = 0; irq_stat; irq_stat >>= 1, ++hw) {
		if (!(irq_stat & 1))
			continue;

		irq_no = irq_linear_revmap(bank->domain, hw);
		child_desc = irq_to_desc(irq_no);

		/* Toggle edge for pin with both edges triggering enabled */
		if (irqd_get_trigger_type(&child_desc->irq_data)
				== IRQ_TYPE_EDGE_BOTH)
			tz1090_gpio_irq_next_edge(bank, hw);

		BUG_ON(!tz1090_gpio_valid_handler(child_desc));
		generic_handle_irq_desc(irq_no, child_desc);
	}
}

static int tz1090_gpio_irq_map(struct irq_domain *d, unsigned int irq,
			       irq_hw_number_t hw)
{
	irq_set_chip(irq, &gpio_irq_chip);
	return 0;
}

static const struct irq_domain_ops tz1090_gpio_irq_domain_ops = {
	.map	= tz1090_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static int tz1090_gpio_bank_probe(struct tz1090_gpio_bank_info *info)
{
	struct device_node *np = info->node;
	struct device *dev = info->priv->dev;
	struct tz1090_gpio_bank *bank;

	bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
	if (!bank) {
		dev_err(dev, "unable to allocate driver data\n");
		return -ENOMEM;
	}

	/* Offset the main registers to the first register in this bank */
	bank->reg = info->priv->reg + info->index * 4;
	comet_gpio_chip[info->index] = bank;

	/* Set up GPIO chip */
	snprintf(bank->label, sizeof(bank->label), "tz1090-gpio-%u",
		 info->index);
	bank->chip.label		= bank->label;
	bank->chip.dev			= dev;
	bank->chip.direction_input	= tz1090_gpio_direction_input;
	bank->chip.direction_output	= tz1090_gpio_direction_output;
	bank->chip.get			= tz1090_gpio_get;
	bank->chip.set			= tz1090_gpio_set;
	bank->chip.free			= tz1090_gpio_free;
	bank->chip.request		= tz1090_gpio_request;
	bank->chip.to_irq		= tz1090_gpio_to_irq;
	bank->chip.of_node		= np;

	/* GPIO numbering from 0 */
	bank->chip.base			= info->index * 30;
	bank->chip.ngpio		= 30;

	/* Add the GPIO bank */
	gpiochip_add(&bank->chip);

	/* Get the GPIO bank IRQ if provided */
	bank->irq = irq_of_parse_and_map(np, 0);

	/* The interrupt is optional (it may be used by another core on chip) */
	if (bank->irq < 0) {
		dev_info(dev, "IRQ not provided for bank %u, IRQs disabled\n",
			 info->index);
		return 0;
	}

	dev_info(dev, "Setting up IRQs for GPIO bank %u\n",
		 info->index);

	/*
	 * Initialise all interrupts to disabled so we don't get
	 * spurious ones on a dirty boot and hit the BUG_ON in the
	 * handler.
	 */
	tz1090_gpio_write(bank, REG_GPIO_IRQ_EN, 0);

	/* Add a virtual IRQ for each GPIO */
	bank->domain = irq_domain_add_linear(np,
					     bank->chip.ngpio,
					     &tz1090_gpio_irq_domain_ops,
					     bank);

	/* Setup chained handler for this GPIO bank */
	irq_set_handler_data(bank->irq, bank);
	irq_set_chained_handler(bank->irq, tz1090_gpio_irq_handler);

	return 0;
}

static void tz1090_gpio_register_banks(struct tz1090_gpio *priv)
{
	struct device_node *np = priv->dev->of_node;
	struct device_node *node;

	for_each_available_child_of_node(np, node) {
		struct tz1090_gpio_bank_info info;
		u32 addr;
		int ret;

		ret = of_property_read_u32(node, "reg", &addr);
		if (ret) {
			dev_err(priv->dev, "invalid reg on %s\n",
				node->full_name);
			continue;
		}
		if (addr >= 3) {
			dev_err(priv->dev, "index %u in %s out of range\n",
				addr, node->full_name);
			continue;
		}

		info.index = addr;
		info.node = of_node_get(node);
		info.priv = priv;

		ret = tz1090_gpio_bank_probe(&info);
		if (ret) {
			dev_err(priv->dev, "failure registering %s\n",
				node->full_name);
			of_node_put(node);
			continue;
		}
	}
}

static int tz1090_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res_regs;
	struct tz1090_gpio priv;

	if (!np) {
		dev_err(&pdev->dev, "must be instantiated via devicetree\n");
		return -ENOENT;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_regs) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		return -ENOENT;
	}

	priv.dev = &pdev->dev;

	/* Ioremap the registers */
	priv.reg = devm_ioremap(&pdev->dev, res_regs->start,
				 res_regs->end - res_regs->start);
	if (!priv.reg) {
		dev_err(&pdev->dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}

	/* Look for banks */
	tz1090_gpio_register_banks(&priv);

	return 0;
}

static struct of_device_id tz1090_gpio_of_match[] = {
	{ .compatible = "img,tz1090-gpio" },
	{ },
};

static struct platform_driver tz1090_gpio_driver = {
	.driver = {
		.name		= "tz1090-gpio",
		.owner		= THIS_MODULE,
		.of_match_table	= tz1090_gpio_of_match,
	},
	.probe		= tz1090_gpio_probe,
};

static int __init tz1090_gpio_init(void)
{
	return platform_driver_register(&tz1090_gpio_driver);
}
postcore_initcall(tz1090_gpio_init);

#ifdef CONFIG_METAG_SUSPEND_MEM
/*
 * For now we save and restore all GPIO setup.
 */
static const u8 tz1090_gpio_regs[] = {
	0x00, 0x04, 0x08,	/* GPIO_DIR{0-2} */
	0x10, 0x14, 0x18,	/* GPIO_SELECT{0-2} */
	0x20, 0x24, 0x28,	/* IRQ_PLRT{0-2} */
	0x30, 0x34, 0x38,	/* IRQ_TYPE{0-2} */
	0x40, 0x44, 0x48,	/* IRQ_EN{0-2} */
				/* IRQ_STS{0-2} */
	0x60, 0x64, 0x68,	/* GPIO_BIT_EN{0-2} */
				/* GPIO_DIN{0-2} */
	0x80, 0x84, 0x88,	/* GPIO_DOUT{0-2} */
	0x90,			/* SCHMITT_EN0 */
	0xa0, 0xa4, 0xa8, 0xac,	/* PU_PD_{0-3} */
	0xb0, 0xb4, 0xb8,	/* PU_PD_{4-6} */
	0xc0,			/* SR_0 */
	0xd0,			/* DR_0 */
};
struct tz1090_gpio_state {
	u32 values[ARRAY_SIZE(tz1090_gpio_regs)];
};

static struct tz1090_gpio_state *tz1090_gpio_state;

static int tz1090_gpio_suspend(void)
{
	unsigned long flags;
	unsigned int i;
	struct tz1090_gpio_state *state;

	state = kzalloc(sizeof(*state), GFP_ATOMIC);
	if (!state)
		return -ENOMEM;

	__global_lock2(flags);
	for (i = 0; i < ARRAY_SIZE(tz1090_gpio_regs); ++i)
		state->values[i] = readl(GPIO_CRTOP_BASE_ADDR +
					 tz1090_gpio_regs[i]);
	__global_unlock2(flags);

	tz1090_gpio_state = state;

	return 0;
}

static void tz1090_gpio_resume(void)
{
	unsigned long flags;
	unsigned int i;
	struct tz1090_gpio_state *state = tz1090_gpio_state;

	__global_lock2(flags);
	for (i = 0; i < ARRAY_SIZE(tz1090_gpio_regs); ++i)
		writel(state->values[i],
		       GPIO_CRTOP_BASE_ADDR + tz1090_gpio_regs[i]);
	__global_unlock2(flags);

	tz1090_gpio_state = NULL;
	kfree(state);
}
#else
#define tz1090_gpio_suspend NULL
#define tz1090_gpio_resume NULL
#endif	/* CONFIG_METAG_SUSPEND_MEM */

static struct syscore_ops tz1090_gpio_syscore_ops = {
	.suspend = tz1090_gpio_suspend,
	.resume = tz1090_gpio_resume,
};

static int tz1090_gpio_syscore_init(void)
{
	register_syscore_ops(&tz1090_gpio_syscore_ops);
	return 0;
}

static void tz1090_gpio_syscore_exit(void)
{
	unregister_syscore_ops(&tz1090_gpio_syscore_ops);
}

module_init(tz1090_gpio_syscore_init);
module_exit(tz1090_gpio_syscore_exit);
