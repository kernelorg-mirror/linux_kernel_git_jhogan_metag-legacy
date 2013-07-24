/*
 * TZ1090 Aux-DAC based Backlight Driver
 *
 * Copyright (C) 2012 Imagination Technologies
 *
 * Based on generic_bl.c:
 * Copyright (c) 2004-2008 Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <video/tz1090_auxdac_bl.h>
#include <asm/soc-tz1090/afe.h>

static int tz1090_auxdac_bl_intensity;
static struct backlight_device *tz1090_auxdac_bl_dev;
static struct tz1090_auxdac_bl_pdata *bl_machinfo;

static int tz1090_auxdac_bl_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;
	int blank = bd->props.fb_blank;

	/* allow sysfs to override blank of framebuffer */
	if (bd->props.power > blank)
		blank = bd->props.power;

	if (blank != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_FBBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_SUSPENDED)
		intensity = 0;

	dev_dbg(&tz1090_auxdac_bl_dev->dev, "intensity=%d, blank=%d\n",
		intensity, blank);

	/* adjust Aux-DAC value */
	comet_afe_auxdac_set_value(intensity);
	comet_afe_auxdac_set_standby(blank > FB_BLANK_NORMAL);
	comet_afe_auxdac_set_power(blank != FB_BLANK_POWERDOWN);

	/* allow platform data to control backlight power */
	bl_machinfo->set_bl_power(blank);

	tz1090_auxdac_bl_intensity = intensity;

	return 0;
}

static int tz1090_auxdac_bl_get_intensity(struct backlight_device *bd)
{
	return tz1090_auxdac_bl_intensity;
}

static int tz1090_auxdac_bl_check_fb(struct backlight_device *bd,
				    struct fb_info *info)
{
	if (bl_machinfo->match_fb)
		return bl_machinfo->match_fb(bl_machinfo, info);

	return 0;
}

static const struct backlight_ops tz1090_auxdac_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= tz1090_auxdac_bl_send_intensity,
	.get_brightness	= tz1090_auxdac_bl_get_intensity,
	.check_fb	= tz1090_auxdac_bl_check_fb,
};

static int tz1090_auxdac_bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct tz1090_auxdac_bl_pdata *machinfo = pdev->dev.platform_data;
	const char *name = "tz1090-auxdac-bl";
	struct backlight_device *bd;
	int err;

	/* get control of the Aux DAC which controls the backlight intensity */
	err = comet_afe_auxdac_get();
	if (err) {
		dev_err(&pdev->dev, "Could not get Comet Aux-DAC\n");
		return err;
	}

	bl_machinfo = machinfo;
	if (machinfo->name)
		name = machinfo->name;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 0xff;
	bd = backlight_device_register(name, &pdev->dev, NULL,
				       &tz1090_auxdac_bl_ops, &props);
	if (IS_ERR(bd)) {
		err = PTR_ERR(bd);
		dev_err(&pdev->dev, "Could not register backlight device (err=%d)\n",
			err);
		goto err;
	}

	tz1090_auxdac_bl_dev = bd;
	platform_set_drvdata(pdev, bd);

	bd->props.power = FB_BLANK_UNBLANK;
	/* detect the current settings, which may have been set by bootloader */
	bd->props.fb_blank = FB_BLANK_POWERDOWN;
	bd->props.brightness = machinfo->default_intensity;
	if (comet_afe_auxdac_get_power()) {
		if (comet_afe_auxdac_get_standby()) {
			bd->props.fb_blank = FB_BLANK_NORMAL;
		} else {
			bd->props.fb_blank = FB_BLANK_UNBLANK;
			bd->props.brightness = comet_afe_auxdac_get_value();
		}
	}
	backlight_update_status(bd);

	dev_info(&pdev->dev, "TZ1090 Aux-DAC backlight driver initialized\n");
	return 0;
err:
	comet_afe_auxdac_put();
	return err;
}

static int tz1090_auxdac_bl_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	comet_afe_auxdac_put();
	return 0;
}

static struct platform_driver tz1090_auxdac_bl_driver = {
	.probe		= tz1090_auxdac_bl_probe,
	.remove		= tz1090_auxdac_bl_remove,
	.driver		= {
		.name	= "tz1090-auxdac-bl",
	},
};

module_platform_driver(tz1090_auxdac_bl_driver);

MODULE_AUTHOR("Imagination Technologies");
MODULE_DESCRIPTION("TZ1090 Aux-DAC Backlight Driver");
MODULE_LICENSE("GPL");
