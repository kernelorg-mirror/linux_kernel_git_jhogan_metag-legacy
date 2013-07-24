/*
 * boards/comet-bub/lcd/setup.c
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <asm/global_lock.h>
#include <asm/soc-tz1090/clock.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/gpio.h>



static struct resource lcd_resources[] = {
	[0] = {
		.start = LCD_BASE_ADDR,
		.end = LCD_BASE_ADDR + LCD_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = LCD_IRQ_NUM,
		.end = LCD_IRQ_NUM,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.name = "dma_periph",
		.start = DMA_MUX_LCD_WR,
		.end = DMA_MUX_LCD_WR,
		.flags = IORESOURCE_DMA,
	},
};

static u64 lcd_dmamask = DMA_BIT_MASK(64);

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


static void lcd_init(void)
{
	u32 temp;
	int lstat;


	/* Setup Clock */
	struct clk *pixel_clock = clk_get(NULL, "pixel");
	pix_clk_set_limits(22660000,	/* 22.66MHz */
			   27690000);	/* 27.69MHz */
	if (clk_set_rate(pixel_clock, 24576000))
		printk(KERN_WARNING"Failed to set Pixel clock to "
				 "24.576MHZ for LCD");
	clk_prepare_enable(pixel_clock);

	/*set TFT Pin mux to LCD Mode*/
	__global_lock2(lstat);
	temp = readl(CR_IF_CTL0);
	temp &= ~0x7;
	temp |= 0x3; /*LCD is on same mode as trace*/
	writel(temp, CR_IF_CTL0);
	__global_unlock2(lstat);

	/*Set Pins to non gpio mode*/
	comet_gpio_disable_block(GPIO_TFT_RED0, GPIO_TFT_RED5);
	comet_gpio_disable_block(GPIO_TFT_GREEN0, GPIO_TFT_GREEN1);
	comet_gpio_disable_block(GPIO_TFT_PANELCLK, GPIO_TFT_HSYNC_NR);

	/*Enable pixel clock (used by lcd)*/
	__global_lock2(lstat);
	temp = readl(CR_TOP_CLKENAB2);
	temp |= (1 << CR_TOP_PIXEL_CLK_2_EN_BIT);
	writel(temp, CR_TOP_CLKENAB2);
	__global_unlock2(lstat);

	/*Enable LCD clock*/
	__global_lock2(lstat);
	temp = readl(CR_PERIP_CLK_EN);
	temp |= (1 << CR_PERIP_LCD_CLK_EN_BIT);
	writel(temp, CR_PERIP_CLK_EN);
	__global_unlock2(lstat);

#ifdef CONFIG_SOC_TZ1090
	/* map IRQ on Comet */
	lstat = lcd_resources[1].start;
	lstat = external_irq_map(lstat);
	/* store mapped irq */
	lcd_resources[1].start = lstat;
	lcd_resources[1].end = lstat;
#endif

}


static int __init lcd_setup(void)
{
	lcd_init();

	return platform_device_register(&lcd_device);

} device_initcall(lcd_setup);
