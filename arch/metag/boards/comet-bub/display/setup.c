/*
 * boards/comet-bub/display/setup.c - board specific initialisation
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
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

static void comet_bub_set_screen_power(int pwr)
{
	/*
	 * This is a work around.
	 * The bringup board has the wrong polarity, so the TFT power can be
	 * controlled instead using a GPIO signal.
	 */
	gpio_set_value(GPIO_TFT_PWRSAVE, !!pwr);
}

/* based on atp-dp:display2 */
static struct fb_videomode fbvm = {
	.name = "Comet-bub:display",
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

static struct pdp_lcd_size_cfg plsc = {
	.width = 115,	/* 115.2mm */
	.height = 86,	/* 86.4mm */
};

static struct pdp_sync_cfg psc = {
	.force_vsyncs = 0,
	.hsync_dis = 0,
	.vsync_dis = 0,
	.blank_dis = 0,
	.blank_pol = PDP_ACTIVE_LOW,
	.clock_pol = PDP_CLOCK_INVERTED,
};

static struct pdp_hwops hwops = {
	.set_screen_power = comet_bub_set_screen_power,
	.set_shared_base = comet_pdp_set_shared_base,
};

static int __init display_device_setup(void)
{
	comet_pdp_set_limits(22660000, 27690000);
	return comet_pdp_setup(&fbvm, &plsc, NULL, &psc, &hwops);
}
device_initcall(display_device_setup);
