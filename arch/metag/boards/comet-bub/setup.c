#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <asm/mach/arch.h>
#include <asm/soc-tz1090/sdhost.h>
#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/setup.h>
#include <asm/soc-tz1090/usb.h>

/* If XTAL3 isn't used, override get_xtal3 */
#ifndef CONFIG_COMET_BUB_XTAL3
unsigned long get_xtal3(void)
{
	return 0;
}
#endif

static int __init comet_bub_init_tft(void)
{
	int err;

	/*
	 * The bringup board has the wrong polarity, so the TFT power can be
	 * controlled instead using a GPIO signal.
	 */

	comet_gpio_disable_block(GPIO_TFT_FIRST, GPIO_TFT_LAST);
	err = gpio_request(GPIO_TFT_PWRSAVE, "TFT pwrsave");
	if (err) {
		printk(KERN_WARNING "TFT_PWRSAVE GPIO request failed: %d",
					err);
		return -EINVAL;
	}
	err = gpio_direction_output(GPIO_TFT_PWRSAVE, 0);
	if (err) {
		printk(KERN_WARNING "TFT_PWRSAVE GPIO set direction failed: %d",
					err);
		return -EINVAL;
	}

	/*
	 * Switch the screen off until it wants to be used.
	 */

	gpio_set_value(GPIO_TFT_PWRSAVE, 0);

	return 0;
}

/*
 * USB setup and VBUS control
 */

static void comet_bub_enable_vbus(void)
{
	gpio_set_value(GPIO_PDM_D, 1);
}

static void comet_bub_disable_vbus(void)
{
	gpio_set_value(GPIO_PDM_D, 0);
}

static struct dwc_otg_board comet_bub_usb_board = {
	.enable_vbus = comet_bub_enable_vbus,
	.disable_vbus = comet_bub_disable_vbus,
};

static int __init comet_bub_init_usb(void)
{
	if (gpio_request(GPIO_PDM_D, "USB VBus"))
		pr_err("Failed to request PDM_D GPIO\n");

	gpio_direction_output(GPIO_PDM_D, 1);
	gpio_set_value(GPIO_PDM_D, 0);

	comet_usb_setup(&comet_bub_usb_board);

	return 0;
}

static void __init comet_bub_init(void)
{
	comet_init_machine();

	comet_bub_init_tft();
	comet_bub_init_usb();
	comet_sdhost_init();
}

/* Comet Bring-Up-Board */

static const char *comet_bub_boards_compat[] __initdata = {
	"img,tz1090-01ry",
	NULL,
};

MACHINE_START(PURE_01XK, "01RY Comet Bring-Up-Board")
	.dt_compat	= comet_bub_boards_compat,
	TZ1090_MACHINE_DEFAULTS,
	.init_machine	= comet_bub_init,
MACHINE_END
