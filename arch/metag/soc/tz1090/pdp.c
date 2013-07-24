/*
 * pdp.c - contains block specific initialisation routines
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <video/pdpfb.h>
#include <video/imgpdi_lcd.h>
#include <linux/io.h>
#include <asm/soc-tz1090/pdp.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/clock.h>

void comet_pdp_set_shared_base(unsigned long pa)
{
	writel(pa >> 2, CR_PDP_MEM_BASE_ADDR);
}

static struct resource pdp_resources[] = {
	{
		.start	= PDP_IRQ_NUM,
		/* mapped in comet_pdp_setup() */
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= PDP_BASE_ADDR,
		.end	= PDP_BASE_ADDR + PDP_SIZE,
		.flags	= IORESOURCE_MEM | PDPFB_IORES_PDP,
	},
};

static struct pdp_info pdp_pdata = {
	.bpp		= 16,
#ifdef CONFIG_SOC_COMET_ES1
	.linestore_len = 768,
#else
	.linestore_len = 1024,
#endif
	.vpitch_bilinear_threshold = 3 << PDPFB_PDATA_FIX_SHIFT,
};

static struct platform_device pdp_device = {
	.name		= "pdpfb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pdp_resources),
	.resource	= pdp_resources,
	.dev		= {
		.platform_data	= &pdp_pdata,
	},
};

static struct resource pdi_resources[] = {
	{
		.start	= PDI_BASE_ADDR,
		.end	= PDI_BASE_ADDR + PDI_SIZE,
		.flags	= IORESOURCE_MEM,
	},
};

static struct imgpdi_lcd_timings pdi_timings;
static struct imgpdi_lcd_pdata pdi_pdata;

static int comet_pdi_match_fb(struct imgpdi_lcd_pdata *pdata,
			      struct fb_info *info)
{
	return !strncmp(info->fix.id, "pdp", 16);
}

static struct platform_device pdi_device = {
	.name		= "imgpdi-lcd",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pdi_resources),
	.resource	= pdi_resources,
	.dev		= {
		.platform_data	= &pdi_pdata,
	},
};

static struct platform_device *display_devices[] = {
	&pdi_device,
	&pdp_device,
};

void __init comet_pdp_set_limits(unsigned long min, unsigned long max)
{
	pix_clk_set_limits(min, max);
}

int __init comet_pdp_setup(const struct fb_videomode *fbvm,
		struct pdp_lcd_size_cfg *plsc, struct imgpdi_lcd_pdata *pdic,
		struct pdp_sync_cfg *psc, struct pdp_hwops *hwops)
{
	int irq;

	/* Make sure we're given the necessary info */
	if (!fbvm || !plsc)
		return -ENXIO;

	/* Map the IRQ */
	irq = external_irq_map(pdp_resources[0].start);
	if (irq < 0) {
		pr_err("%s: unable to map PDP irq %u (%d)\n",
		       __func__, pdp_resources[0].start, irq);
		return irq;
	}
	pdp_resources[0].start = irq;
	pdp_resources[0].end = irq;

	memcpy(&pdp_pdata.lcd_cfg, fbvm, sizeof(struct fb_videomode));
	memcpy(&pdp_pdata.lcd_size_cfg, plsc, sizeof(struct pdp_lcd_size_cfg));
	memcpy(&pdp_pdata.sync_cfg, psc, sizeof(struct pdp_sync_cfg));
	memcpy(&pdp_pdata.hwops, hwops, sizeof(struct pdp_hwops));

	/* copy PDI platform data if provided */
	if (pdic)
		memcpy(&pdi_pdata, pdic, sizeof(struct imgpdi_lcd_pdata));
	else
		memset(&pdi_pdata, 0, sizeof(struct imgpdi_lcd_pdata));
	/* if active timings are provided, copy them too */
	if (pdi_pdata.active) {
		memcpy(&pdi_timings, pdi_pdata.active,
		       sizeof(struct imgpdi_lcd_timings));
		pdi_pdata.active = &pdi_timings;
	}
	/* use our own fb matcher */
	pdi_pdata.match_fb = comet_pdi_match_fb;

	return platform_add_devices(display_devices,
				    ARRAY_SIZE(display_devices));
}
