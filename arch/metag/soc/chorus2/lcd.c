/*
 * Copyright (C) 2010 Imagination Technologies
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/global_lock.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/soc-chorus2/c2_irqnums.h>

#define CHORUS2_LCD_BASE	0x0200E000
#define CHORUS2_LCD_DMA		15

#define C2_CLOCK_ENABLE         0x020000B8
#define C2_CLOCK_LCD_BIT        0x00000010

#define C2_PIN_CONTROLLER_BASE	0x02024000
#define PIN_CONTROL_LCD_OFFSET	0x8
#define PIN_CONTROL_LCD_ON	1

static struct resource lcd_resources[] = {
	[0] = {
		.start = CHORUS2_LCD_BASE,
		.end = CHORUS2_LCD_BASE + 0x30,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = LCD_IRQ_NUM,
		/* mapped in chorus2_lcd_init() */
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.name = "dma_periph",
		.start = CHORUS2_LCD_DMA,
		.end = CHORUS2_LCD_DMA,
		.flags = IORESOURCE_DMA,
	},
};

static void __init chorus2_lcd_init(void)
{
	unsigned int clock_enable;
	int lstat, irq;

	/* Map the IRQ */
	irq = external_irq_map(lcd_resources[1].start);
	if (irq < 0) {
		pr_err("%s: irq map failed (%d)\n",
		       __func__, irq);
		return;
	}
	lcd_resources[1].start = irq;
	lcd_resources[1].end = irq;

	/* Set up the pin controller */
	writel(PIN_CONTROL_LCD_ON, C2_PIN_CONTROLLER_BASE +
	       PIN_CONTROL_LCD_OFFSET);

	__global_lock2(lstat);

	/* Turn on the clock */
	clock_enable = readl(C2_CLOCK_ENABLE);
	if (!(clock_enable & C2_CLOCK_LCD_BIT))
		writel(clock_enable | C2_CLOCK_LCD_BIT, C2_CLOCK_ENABLE);

	__global_unlock2(lstat);
}

static u64 lcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device lcd_device = {
	.name           = "img-lcd",
	.id             = -1,
	.num_resources = ARRAY_SIZE(lcd_resources),
	.resource = lcd_resources,
	.dev = {
		.dma_mask = &lcd_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(64),
	},
};

static struct platform_device *chorus2_lcd_devices[] __initdata = {
	&lcd_device,
};

static int __init chorus2_lcd_devices_setup(void)
{
	chorus2_lcd_init();
	return platform_add_devices(chorus2_lcd_devices,
				ARRAY_SIZE(chorus2_lcd_devices));
}
__initcall(chorus2_lcd_devices_setup);
