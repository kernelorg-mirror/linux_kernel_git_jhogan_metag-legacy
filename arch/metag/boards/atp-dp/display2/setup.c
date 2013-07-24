
#include <linux/export.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <video/imgpdi_lcd.h>
#include <video/pdpfb.h>
#include <asm/soc-chorus2/pdp.h>
#include <asm/soc-chorus2/c2_irqnums.h>
#include <asm/soc-chorus2/clock.h>

static struct resource pdp_resources[] = {
	{
		.start = PDP_IRQ_NUM,
		/* mapped in display2_device_setup() */
		.flags = IORESOURCE_IRQ,
	},
	{
		.start	= PDP_BASE_ADDR,
		.end	= PDP_BASE_ADDR + PDP_SIZE,
		.flags	= IORESOURCE_MEM | PDPFB_IORES_PDP,
	},
};

static struct pdp_info pdp_platform_data = {
	.bpp = 16,
	.lcd_cfg = {
		.name = "ATP-dp:display2",
		.refresh = 60,

		.hsync_len = 16,
		.left_margin = 138,
		.xres = 640,
		.right_margin = 6,

		.vsync_len = 3,
		.upper_margin = 36,
		.yres = 480,
		.lower_margin = 6,

		/* hsync and vsync are active low */
		.sync = 0,
	},
	.lcd_size_cfg = {
		.width = 115,	/* 115.2mm */
		.height = 86,	/* 86.4mm */
	},
	.sync_cfg = {
		.force_vsyncs = 1,
		.hsync_dis = 0,
		.vsync_dis = 0,
		.blank_dis = 0,
		.blank_pol = PDP_ACTIVE_LOW,
		.clock_pol = PDP_CLOCK_INVERTED,
	},
};

static struct platform_device pdp_device = {
	.name           = "pdpfb",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(pdp_resources),
	.resource 	= pdp_resources,
	.dev            = {
		.platform_data = &pdp_platform_data,
	},
};

static struct resource pdi_resources[] = {
	{
		.start	= PDI_BASE_ADDR,
		.end	= PDI_BASE_ADDR + PDI_SIZE,
		.flags	= IORESOURCE_MEM,
	},
};

static int display2_pdi_match_fb(struct imgpdi_lcd_pdata *pdata,
			      struct fb_info *info)
{
	return !strncmp(info->fix.id, "pdp", 16);
}

static struct imgpdi_lcd_pdata pdi_pdata = {
	.match_fb	= display2_pdi_match_fb,
};

static struct platform_device pdi_device = {
	.name		= "imgpdi-lcd",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pdi_resources),
	.resource	= pdi_resources,
	.dev		= {
		.platform_data	= &pdi_pdata,
	},
};

static struct platform_device *display2_devices[] __initdata = {
	&pdi_device,
	&pdp_device,
};

/* display2 has a CH7012 on I2C bus, at address 117 */

static int display2_ch7012_probe(struct i2c_adapter *adap)
{
	if (i2c_smbus_xfer(adap, 117, 0, 0, 0, I2C_SMBUS_QUICK, NULL) >= 0) {
		printk(KERN_INFO "Detected ATP120-dp:display2\n");
		pix_clk_set_limits(22660000,	/* 22.66MHz */
				   27690000);	/* 27.69MHz */
		platform_add_devices(display2_devices,
				     ARRAY_SIZE(display2_devices));
	}
	return -EIO;
}

static const struct i2c_device_id display2_ch7012_id[] = {
	{ "display2_ch7012", 0 },
	{ },
};

static struct i2c_driver display2_ch7012_driver = {
	.driver.name	= "display2_ch7012",
	.attach_adapter = display2_ch7012_probe,
	.id_table	= display2_ch7012_id,
};

static int __init display2_device_setup(void)
{
	int irq;

	/* Map the IRQ */
	irq = external_irq_map(pdp_resources[0].start);
	if (irq < 0) {
		pr_err("%s: irq map failed (%d)\n",
		       __func__, irq);
		return irq;
	}
	pdp_resources[0].start = irq;
	pdp_resources[0].end = irq;

	i2c_add_driver(&display2_ch7012_driver);
	return 0;
}
device_initcall(display2_device_setup);
