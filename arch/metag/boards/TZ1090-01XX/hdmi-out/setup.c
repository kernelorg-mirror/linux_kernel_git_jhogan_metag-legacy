/* HDMI Board support */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <video/pdpfb.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/pdp.h>
#include <asm/soc-tz1090/hdmi-video.h>

/*
 * 01SP/01TT HDMI has been rated at a maximum of 82MHz, however it may be capable of
 * higher unsupported frequencies over 100MHz. The HDMI chip supports up to
 * 165MHz.
 */
#define HDMI_MAX_PIXFREQ	82000000

/* HDMI */
static struct pdp_lcd_size_cfg plsc = {
	.dynamic_mode	= 1,
	.width		= 476,
	.height		= 268,
};

static struct pdp_sync_cfg psc = {
	.force_vsyncs	= 0,
	.hsync_dis	= 0,
	.blank_dis	= 0,
	.blank_pol	= PDP_ACTIVE_LOW,
	.clock_pol	= PDP_CLOCK_INVERTED,
};

static struct pdp_hwops hwops = {
	.set_shared_base	= comet_pdp_set_shared_base,
};

static struct hdmi_platform_data hdmi_pdata = {
	.pix_clk	= "pixel",
	.max_pixfreq	= HDMI_MAX_PIXFREQ,
};

static struct i2c_board_info __initdata board_info[] = {
	{
		I2C_BOARD_INFO("sii9022a-tpi", 0x39),
		.platform_data = &hdmi_pdata,
	},
};

static struct fb_videomode fbvm = {
	.name		= "Generic HDMI Monitor (maybe)",
	.refresh	= 60,

	.hsync_len	= 96,
	.left_margin	= 48,
	.xres		= 640,
	.right_margin	= 16,

	.vsync_len	= 2,
	.upper_margin	= 33,
	.yres		= 480,
	.lower_margin	= 10,

	.sync		= 0,
};

static int __init comet_01xx_init_hdmi(void)
{
	int irq = gpio_to_irq(GPIO_SPI1_CS2);
	int err = 0;

	err = gpio_request(GPIO_SPI1_CS2, "HDMI irqn");
	if (err) {
		printk(KERN_WARNING "HDMI IRQ pin request failed: %d\n", err);
		goto out;
	}

	err = irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
	if (err) {
		printk(KERN_WARNING "Unable to setup HDMI IRQ\n");
		goto out;
	}

	/* Set the pixel clock limits */
	comet_pdp_set_limits(20000000, HDMI_MAX_PIXFREQ);

	/* Setup the I2C device */
	board_info[0].irq = irq;
	i2c_register_board_info(2, board_info, ARRAY_SIZE(board_info));
	comet_pdp_setup(&fbvm, &plsc, NULL, &psc, &hwops);

out:
	return err;
}
device_initcall(comet_01xx_init_hdmi);
