/*
 * boards/polaris/tft/setup.c
 *
 * Copyright (C) 2010-2012 Imagination Technologies Ltd.
 *
 */

#include <linux/backlight.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <video/tz1090_auxdac_bl.h>
#include <video/pdpfb.h>
#include <asm/soc-tz1090/clock.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/pdp.h>
#include "../gpio.h"
#include "../setup.h"

static struct fb_videomode polaris_lcd_cfg = {
	.name = "polaris:display",
	.refresh = 60,

	.hsync_len = 20,
	.left_margin = 124,
	.xres = 640,
	.right_margin = 64,

	.vsync_len = 2,
	.upper_margin = 11,
	.yres = 480,
	.lower_margin = 32,

	/* hsync and vsync are active low */
	.sync = 0,
};

static struct pdp_lcd_size_cfg polaris_lcd_sizecfg = {
	.width = 115,	/* 115.2mm */
	.height = 86,	/* 86.4mm */
};

static struct pdp_sync_cfg polaris_lcd_synccfg = {
	.force_vsyncs = 0,
	.hsync_dis = 0,
	.vsync_dis = 0,
	.blank_dis = 0,
	.blank_pol = PDP_ACTIVE_LOW,
	.clock_pol = PDP_CLOCK_INVERTED,
};

static struct pdp_hwops polaris_lcd_hwops = {
	.set_shared_base = comet_pdp_set_shared_base,
};

/* backlight control */

static int polaris_bl_match_fb(struct tz1090_auxdac_bl_pdata *pdata,
			       struct fb_info *info)
{
	return !strncmp(info->fix.id, "pdp", 16);
}

static void polaris_bl_set_power(int power)
{
	polaris_bl_boost_set(POLARIS_BL_BOOST_5V_BL,
			     power != FB_BLANK_POWERDOWN);
}

static struct tz1090_auxdac_bl_pdata polaris_bl_info = {
	.name			= "polaris-bl",
	.default_intensity	= 0xff,
	.match_fb		= polaris_bl_match_fb,
	.set_bl_power		= polaris_bl_set_power,
};

static struct platform_device polaris_bl_dev = {
	.name			= "tz1090-auxdac-bl",
	.dev			= {
		.platform_data	= &polaris_bl_info,
	},
};

static int __init display_device_setup(void)
{
	int err;

	pix_clk_set_limits(6000000,	/* 6MHz */
			   50000000);	/* 50MHz */

	/* set up the PDP */
	err = comet_pdp_setup(&polaris_lcd_cfg, &polaris_lcd_sizecfg,
			      NULL, &polaris_lcd_synccfg,
			      &polaris_lcd_hwops);
	if (err) {
		pr_err("Couldn't set up PDP!\n");
		goto err_pdp_setup;
	}

	/* register the backlight device */
	platform_device_register(&polaris_bl_dev);

	return 0;

err_pdp_setup:
	return err;
}
device_initcall(display_device_setup);
