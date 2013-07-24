
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
		/* mapped in display1_device_setup() */
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
		.name = "ATP-dp:display1",
		.refresh = 60,

		.hsync_len = 8,
		.left_margin = 9,
		.xres = 240,
		.right_margin = 11,

		.vsync_len = 4,
		.upper_margin = 4,
		.yres = 320,
		.lower_margin = 5,

		/* hsync is active high, vsync is active low */
		.sync = FB_SYNC_HOR_HIGH_ACT,
	},
	.lcd_size_cfg = {
		.width = 54,	/* 53.64mm */
		.height = 72,	/* 71.52mm */
	},
	.sync_cfg = {
		.force_vsyncs = 1,
		.hsync_dis = 0,
		.vsync_dis = 0,
		.blank_dis = 1,
		.blank_pol = PDP_ACTIVE_LOW,
		.clock_pol = PDP_CLOCK_INVERTED,
		.sync_slave = 1,
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

static int display1_pdi_match_fb(struct imgpdi_lcd_pdata *pdata,
			      struct fb_info *info)
{
	return !strncmp(info->fix.id, "pdp", 16);
}

struct imgpdi_lcd_timings pdi_timings = {
	.pwrsvgd	= 16,
	.ls		= 16,
	.pwrsvgd2	= 246,
	.nl		= 7,
	.acb		= 256,

	.newframe_en	= 1,
	.gatedriver_en	= 1,
};

static struct imgpdi_lcd_pdata pdi_pdata = {
	.match_fb	= display1_pdi_match_fb,
	.active		= &pdi_timings,
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

static struct platform_device *display1_devices[] __initdata = {
	&pdi_device,
	&pdp_device,
};

/* display1 has a PCA9533 on I2C bus, at address 99 */

static int display1_pca9533_probe(struct i2c_adapter *adap)
{
	if (i2c_smbus_xfer(adap, 99, 0, 0, 0, I2C_SMBUS_QUICK, NULL) >= 0) {
		printk(KERN_INFO "Detected ATP120-dp:display1\n");
		pix_clk_set_limits(4500000,	/* 4.5MHz */
				   6800000);	/* 6.8MHz */
		platform_add_devices(display1_devices,
				     ARRAY_SIZE(display1_devices));
	}
	return -EIO;
}

static const struct i2c_device_id display1_pca9533_id[] = {
	{ "display1_pca9533", 0 },
	{ },
};

static struct i2c_driver display1_pca9533_driver = {
	.driver.name	= "display1_pca9533",
	.attach_adapter = display1_pca9533_probe,
	.id_table	= display1_pca9533_id,
};

static int __init display1_device_setup(void)
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

	i2c_add_driver(&display1_pca9533_driver);
	return 0;
}
device_initcall(display1_device_setup);
