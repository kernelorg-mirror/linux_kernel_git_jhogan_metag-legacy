/*
 * Imagination Technologies Panel Display Interface (PDI).
 *
 * Copyright (C) 2012 Imagination Technologies
 *
 * Based on platform_lcd.c:
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/backlight.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <video/imgpdi_lcd.h>

/* Register offsets from base address */

#define PDI_SETUP		0x0000
#define PDI_TIMING0		0x0004
#define PDI_TIMING1		0x0008
#define PDI_COREID		0x0040
#define PDI_COREREVISION	0x0050

/* Register field descriptions */

#define PDI_SETUP_BLNKLVL	(1 << 6)	/* panel clock level during blanking */
#define PDI_SETUP_BLNK		(1 << 5)	/* clock blanking enable */
#define PDI_SETUP_PWR		(1 << 4)	/* panel power enable */
#define PDI_SETUP_EN		(1 << 3)	/* panel enable */
#define PDI_SETUP_GDEN		(1 << 2)	/* GD enable (active mode) */
#define PDI_SETUP_NFEN		(1 << 1)	/* NF enable (active mode) */
#define PDI_SETUP_CR		(1 << 0)	/* conversion mode active */

#define PDI_TIMING0_PWRSVGD	(0xf << 24)	/* delay in PCLKs-1 HSYNC to PWRSV,GD */
#define PDI_TIMING0_PWRSVGD_S	24
#define PDI_TIMING0_LSDEL	(0x7f << 16)	/* delay in PCLKs-1 HSYNC to LS */
#define PDI_TIMING0_LSDEL_S	16
#define PDI_TIMING0_PWRSV2GD2	(0x3ff << 0)	/* delay in PCLKs-1 LS to end of PWRSV,GD */
#define PDI_TIMING0_PWRSV2GD2_S	0

#define PDI_TIMING1_NLDEL	(0xf << 16)	/* delay in PCLKs-1 HSYNC to NL */
#define PDI_TIMING1_NLDEL_S	16
#define PDI_TIMING1_ACBDEL	(0x3ff << 0)	/* delay in PCLKs-1 NL to end of ACB */
#define PDI_TIMING1_ACBDEL_S	0

#define PDI_COREID_GROUPID	(0xff << 24)	/* identifies IMG IP family group */
#define PDI_COREID_GROUPID_S	24
#define PDI_COREID_COREID	(0xff << 16)	/* identifies member of IP group */
#define PDI_COREID_COREID_S	16
#define PDI_COREID_CONFIG	(0xffff << 0)	/* configuration of core */
#define PDI_COREID_CONFIG_S	0

#define PDI_COREREV_MAJOR	(0xff << 16)	/* family major release revision */
#define PDI_COREREV_MAJOR_S	16
#define PDI_COREREV_MINOR	(0xff << 8)	/* core minor release revision */
#define PDI_COREREV_MINOR_S	8
#define PDI_COREREV_MAINT	(0xff << 0)	/* maintenance release revision */
#define PDI_COREREV_MAINT_S	0

struct imgpdi_lcd {
	struct device		*us;
	struct lcd_device	*lcd;
	struct imgpdi_lcd_pdata	*pdata;
	void __iomem		*base;
	struct clk		*clk;

	unsigned int		 power;
	unsigned int		 suspended:1;
};

static struct imgpdi_lcd *to_our_lcd(struct lcd_device *lcd)
{
	return lcd_get_data(lcd);
}

static void imgpdi_write(struct imgpdi_lcd *plcd,
			 unsigned int reg_offs, unsigned int data)
{
	iowrite32(data, plcd->base + reg_offs);
}

static unsigned int imgpdi_read(struct imgpdi_lcd *plcd,
				unsigned int reg_offs)
{
	return ioread32(plcd->base + reg_offs);
}

static void imgpdi_configure_en(struct imgpdi_lcd *plcd)
{
	struct imgpdi_lcd_pdata *pdata = plcd->pdata;
	struct imgpdi_lcd_timings *active = pdata->active;
	unsigned int pdi_setup, pdi_timing0, pdi_timing1;

	dev_dbg(plcd->us, "enable\n");

	/* activate pdi */
	if (active)
		pdi_setup = PDI_SETUP_CR; /* active */
	else
		pdi_setup = 0; /* bypass */
	imgpdi_write(plcd, PDI_SETUP, pdi_setup);

	if (active) {
		/* set up signal delays if in active mode */
		pdi_timing0 = (active->pwrsvgd  - 1) << PDI_TIMING0_PWRSVGD_S |
			      (active->ls       - 1) << PDI_TIMING0_LSDEL_S   |
			      (active->pwrsvgd2 - 1) << PDI_TIMING0_PWRSV2GD2_S;
		pdi_timing1 = (active->nl       - 1) << PDI_TIMING1_NLDEL_S   |
			      (active->acb      - 1) << PDI_TIMING1_ACBDEL_S;
		imgpdi_write(plcd, PDI_TIMING0, pdi_timing0);
		imgpdi_write(plcd, PDI_TIMING1, pdi_timing1);

		if (active->gatedriver_en)
			pdi_setup |= PDI_SETUP_GDEN;
		if (active->newframe_en)
			pdi_setup |= PDI_SETUP_NFEN;
		if (active->blanking_en) {
			pdi_setup |= PDI_SETUP_BLNK;
			if (active->blanking_level)
				pdi_setup |= PDI_SETUP_BLNKLVL;
		}
		imgpdi_write(plcd, PDI_SETUP, pdi_setup);
	} else {
		/* enable panel */
		pdi_setup |= PDI_SETUP_EN;
		imgpdi_write(plcd, PDI_SETUP, pdi_setup);
	}
}

static void imgpdi_configure_dis(struct imgpdi_lcd *plcd)
{
	dev_dbg(plcd->us, "disable\n");

	/* disable panel */
	imgpdi_write(plcd, PDI_SETUP, 0);
}

static void imgpdi_configure_pwr_en(struct imgpdi_lcd *plcd)
{
	struct imgpdi_lcd_pdata *pdata = plcd->pdata;
	unsigned int pdi_setup;

	if (!pdata->active) {
		dev_dbg(plcd->us, "power enable\n");

		/* after 20ms enable power */
		msleep(20);
		pdi_setup = imgpdi_read(plcd, PDI_SETUP);
		pdi_setup |= PDI_SETUP_PWR;
		imgpdi_write(plcd, PDI_SETUP, pdi_setup);
	}
}

static void imgpdi_configure_pwr_dis(struct imgpdi_lcd *plcd)
{
	struct imgpdi_lcd_pdata *pdata = plcd->pdata;
	unsigned int pdi_setup;

	if (!pdata->active) {
		dev_dbg(plcd->us, "power disable\n");

		pdi_setup = imgpdi_read(plcd, PDI_SETUP);
		pdi_setup &= ~PDI_SETUP_PWR;
		imgpdi_write(plcd, PDI_SETUP, pdi_setup);
		/* wait 20ms before disabling panel */
		msleep(20);
	}
}

static int imgpdi_lcd_get_power(struct lcd_device *lcd)
{
	struct imgpdi_lcd *plcd = to_our_lcd(lcd);

	return plcd->power;
}

static int imgpdi_lcd_set_power(struct lcd_device *lcd, int power)
{
	struct imgpdi_lcd *plcd = to_our_lcd(lcd);

	if (plcd->suspended)
		power = FB_BLANK_POWERDOWN;

	/*
	 * We use the following blank state mapping:
	 * 0:	FB_BLANK_UNBLANK:		en + power
	 * 1:	FB_BLANK_NORMAL:		en + power
	 * 2:	FB_BLANK_VSYNC_SUSPEND:		en
	 * 3:	FB_BLANK_HSYNC_SUSPEND:		en
	 * 4:	FB_BLANK_POWERDOWN:		(off)
	 */
	if (power < plcd->power) {
		/* power up */
		switch (plcd->power) {
		case FB_BLANK_POWERDOWN:
			if (power >= FB_BLANK_POWERDOWN)
				break;
			imgpdi_configure_en(plcd);
			/* fall through */
		case FB_BLANK_HSYNC_SUSPEND:
		case FB_BLANK_VSYNC_SUSPEND:
			if (power >= FB_BLANK_VSYNC_SUSPEND)
				break;
			imgpdi_configure_pwr_en(plcd);
		default:
			break;
		};
	} else {
		/* power down */
		switch (plcd->power) {
		case FB_BLANK_UNBLANK:
		case FB_BLANK_NORMAL:
			if (power <= FB_BLANK_NORMAL)
				break;
			imgpdi_configure_pwr_dis(plcd);
			/* fall through */
		case FB_BLANK_VSYNC_SUSPEND:
		case FB_BLANK_HSYNC_SUSPEND:
			if (power <= FB_BLANK_HSYNC_SUSPEND)
				break;
			imgpdi_configure_dis(plcd);
		default:
			break;
		};
	}
	plcd->power = power;

	return 0;
}

static int imgpdi_lcd_match(struct lcd_device *lcd, struct fb_info *info)
{
	struct imgpdi_lcd *plcd = to_our_lcd(lcd);
	struct imgpdi_lcd_pdata *pdata = plcd->pdata;

	if (pdata->match_fb)
		return pdata->match_fb(pdata, info);

	return plcd->us->parent == info->device;
}

static struct lcd_ops imgpdi_lcd_ops = {
	.get_power	= imgpdi_lcd_get_power,
	.set_power	= imgpdi_lcd_set_power,
	.check_fb	= imgpdi_lcd_match,
};

static void imgpdi_detect_state(struct imgpdi_lcd *plcd)
{
	unsigned int pdi_setup;

	pdi_setup = imgpdi_read(plcd, PDI_SETUP);

	/* see imgpdi_lcd_set_power() for how blanking states map to PWR, EN */
	if (pdi_setup & PDI_SETUP_PWR)
		plcd->power = FB_BLANK_UNBLANK;
	else if (pdi_setup & PDI_SETUP_EN)
		plcd->power = FB_BLANK_HSYNC_SUSPEND;
	else
		plcd->power = FB_BLANK_POWERDOWN;

	dev_dbg(plcd->us, "initial power = %d\n", plcd->power);
}

static void imgpdi_show_id(struct imgpdi_lcd *plcd)
{
	unsigned int coreid, corerev;

	coreid = imgpdi_read(plcd, PDI_COREID);
	corerev = imgpdi_read(plcd, PDI_COREREVISION);

	dev_info(plcd->us, "IMG PDI (id: %02x:%02x:%04x, revision: %u.%u.%u) probed successfully\n",
		 (coreid  & PDI_COREID_GROUPID) >> PDI_COREID_GROUPID_S,
		 (coreid  & PDI_COREID_COREID)  >> PDI_COREID_COREID_S,
		 (coreid  & PDI_COREID_CONFIG)  >> PDI_COREID_CONFIG_S,
		 (corerev & PDI_COREREV_MAJOR)  >> PDI_COREREV_MAJOR_S,
		 (corerev & PDI_COREREV_MINOR)  >> PDI_COREREV_MINOR_S,
		 (corerev & PDI_COREREV_MAINT)  >> PDI_COREREV_MAINT_S);
}

static int imgpdi_lcd_probe(struct platform_device *pdev)
{
	struct imgpdi_lcd_pdata *pdata;
	struct imgpdi_lcd *plcd;
	struct device *dev = &pdev->dev;
	struct resource *regs;
	int err;

	/* get register memory */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "no register memory resource\n");
		return -EINVAL;
	}

	/* get platform data */
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(dev, "no platform data supplied\n");
		return -EINVAL;
	}

	/* create private data */
	plcd = devm_kzalloc(&pdev->dev, sizeof(struct imgpdi_lcd),
			    GFP_KERNEL);
	if (!plcd) {
		dev_err(dev, "no memory for state\n");
		return -ENOMEM;
	}
	plcd->us = dev;
	plcd->pdata = pdata;

	/* ioremap register memory */
	plcd->base = devm_ioremap(dev, regs->start, regs->end - regs->start);
	if (!plcd->base) {
		dev_err(dev, "could not ioremap register memory\n");
		return -ENOMEM;
	}

	/* get the clock */
	plcd->clk = clk_get(dev, "pdi");
	if (IS_ERR(plcd->clk)) {
		dev_err(&pdev->dev, "could not get pdi clock resource\n");
		return PTR_ERR(plcd->clk);
	}
	clk_prepare_enable(plcd->clk);

	/* detect initial state */
	imgpdi_detect_state(plcd);

	/* register lcd device */
	plcd->lcd = lcd_device_register(dev_name(dev), dev,
					plcd, &imgpdi_lcd_ops);
	if (IS_ERR(plcd->lcd)) {
		dev_err(dev, "cannot register lcd device\n");
		err = PTR_ERR(plcd->lcd);
		goto err;
	}

	platform_set_drvdata(pdev, plcd);

	imgpdi_show_id(plcd);

	return 0;

 err:
	clk_disable_unprepare(plcd->clk);
	clk_put(plcd->clk);
	return err;
}

static int imgpdi_lcd_remove(struct platform_device *pdev)
{
	struct imgpdi_lcd *plcd = platform_get_drvdata(pdev);

	lcd_device_unregister(plcd->lcd);
	clk_disable_unprepare(plcd->clk);
	clk_put(plcd->clk);

	return 0;
}

#ifdef CONFIG_PM
static int imgpdi_lcd_suspend(struct platform_device *pdev, pm_message_t st)
{
	struct imgpdi_lcd *plcd = platform_get_drvdata(pdev);

	plcd->suspended = 1;
	imgpdi_lcd_set_power(plcd->lcd, plcd->power);
	clk_disable_unprepare(plcd->clk);

	return 0;
}

static int imgpdi_lcd_resume(struct platform_device *pdev)
{
	struct imgpdi_lcd *plcd = platform_get_drvdata(pdev);

	plcd->suspended = 0;
	clk_prepare_enable(plcd->clk);
	imgpdi_lcd_set_power(plcd->lcd, plcd->power);

	return 0;
}
#else
#define imgpdi_lcd_suspend NULL
#define imgpdi_lcd_resume NULL
#endif

static struct platform_driver imgpdi_lcd_driver = {
	.driver		= {
		.name	= "imgpdi-lcd",
		.owner	= THIS_MODULE,
	},
	.probe		= imgpdi_lcd_probe,
	.remove		= imgpdi_lcd_remove,
	.suspend        = imgpdi_lcd_suspend,
	.resume         = imgpdi_lcd_resume,
};

module_platform_driver(imgpdi_lcd_driver);

MODULE_AUTHOR("Imagination Technologies");
MODULE_DESCRIPTION("ImgTec Panel Display Interface (PDI) LCD Driver");
MODULE_LICENSE("GPL v2");
