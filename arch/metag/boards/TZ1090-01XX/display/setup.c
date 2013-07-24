/*
 * boards/TZ1090-01XX/display/setup.c
 *
 * Copyright (C) 2010-2011 Imagination Technologies Ltd.
 *
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <video/pdpfb.h>
#include <asm/soc-tz1090/pdp.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/clock.h>

static void comet_01xx_set_screen_power(int pwr)
{
	/*
	 * This is a work around.
	 * The board has the wrong polarity, so the TFT power can be
	 * controlled instead using a GPIO signal.
	 */
	gpio_set_value(GPIO_TFT_PWRSAVE, !!pwr);
}

/* based on atp-dp:display2 */
static struct fb_videomode comet01xx_lcd_cfg = {
	.name = "01SP/01TT:display",
	.refresh = 60,

	.hsync_len = 30,
	.left_margin = 114,
	.xres = 640,
	.right_margin = 16,

	.vsync_len = 3,
	.upper_margin = 29,
	.yres = 480,
	.lower_margin = 10,

	/* hsync and vsync are active low */
	.sync = 0,
};

static struct pdp_lcd_size_cfg comet01xx_lcd_sizecfg = {
	.width = 115,	/* 115.2mm */
	.height = 86,	/* 86.4mm */
};

static struct pdp_sync_cfg comet01xx_lcd_synccfg = {
	.force_vsyncs = 0,
	.hsync_dis = 0,
	.vsync_dis = 0,
	.blank_dis = 0,
	.blank_pol = PDP_ACTIVE_LOW,
	.clock_pol = PDP_CLOCK_INVERTED,
};

static struct pdp_hwops comet01xx_lcd_hwops = {
	.set_screen_power = comet_01xx_set_screen_power,
	.set_shared_base = comet_pdp_set_shared_base,
};

static int __init display_device_setup(void)
{
	pix_clk_set_limits(22660000,	/* 22.66MHz */
	                   27690000);	/* 27.69MHz */

	return comet_pdp_setup(&comet01xx_lcd_cfg, &comet01xx_lcd_sizecfg,
			       NULL, &comet01xx_lcd_synccfg,
			       &comet01xx_lcd_hwops);
}
device_initcall(display_device_setup);
