#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dw_mmc.h>
#include <video/pdpfb.h>
#include <asm/global_lock.h>
#include <asm/mach/arch.h>
#include <asm/soc-tz1090/sdhost.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/pdp.h>
#include <asm/soc-tz1090/pdc.h>
#include <asm/soc-tz1090/setup.h>
#include <asm/soc-tz1090/hdmi-video.h>
#include <asm/soc-tz1090/hdmi-audio.h>
#include <asm/soc-tz1090/usb.h>

#ifdef CONFIG_TZ1090_01XX_GPIO_KEYS
extern int __init comet_01xx_init_gpio_buttons(void);
#endif

static int __init comet_01xx_init_tft(void)
{
	int err;

	/*
	 * The board has the wrong polarity, so the TFT power can be
	 * controlled instead using a GPIO signal.
	 */

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
		gpio_free(GPIO_TFT_PWRSAVE);
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

static void comet_01xx_enable_vbus(void)
{
	gpio_set_value(GPIO_PDM_D, 1);
}

static void comet_01xx_disable_vbus(void)
{
	gpio_set_value(GPIO_PDM_D, 0);
}

static struct dwc_otg_board comet_01xx_usb_board = {
	.enable_vbus = comet_01xx_enable_vbus,
	.disable_vbus = comet_01xx_disable_vbus,
};

static int __init comet_01xx_init_usb(void)
{
	int err;

	err = gpio_request(GPIO_PDM_D, "USB VBus");
	if (err) {
		pr_err("Failed to request PDM_D GPIO\n");
		return err;
	}

	gpio_direction_output(GPIO_PDM_D, 1);
	gpio_set_value(GPIO_PDM_D, 0);

	comet_usb_setup(&comet_01xx_usb_board);

	return 0;
}

/* Allocate all SDIO GPIOs and drive them low. */
static struct gpio sd_temp_gpios[] = {
	{ GPIO_SDIO_CMD,	GPIOF_OUT_INIT_LOW, "SDIO_CMD"},
	{ GPIO_SDIO_CLK,	GPIOF_OUT_INIT_LOW, "SDIO_CLK"},
	{ GPIO_SDIO_D0,		GPIOF_OUT_INIT_LOW, "SDIO_D<0>"},
	{ GPIO_SDIO_D1,		GPIOF_OUT_INIT_LOW, "SDIO_D<1>"},
	{ GPIO_SDIO_D2,		GPIOF_OUT_INIT_LOW, "SDIO_D<2>"},
	{ GPIO_SDIO_D3,		GPIOF_OUT_INIT_LOW, "SDIO_D<3>"},
};

/*
 * Toggle the power by switching off the power line, and driving the SD pins
 * low to ensure it switches off. NB switch only on rev2 onwards.
 */
static void mci_setpower(u32 slot_id, u32 volt)
{
	int err;
	if (volt) {
		gpio_free_array(sd_temp_gpios, ARRAY_SIZE(sd_temp_gpios));

		gpio_set_value(GPIO_PDC_GPIO0, 1);
	} else {
		gpio_set_value(GPIO_PDC_GPIO0, 0);

		err = gpio_request_array(sd_temp_gpios,
			ARRAY_SIZE(sd_temp_gpios));
		if (err)
			pr_warn("SDIO pins already allocated. Can not pull low.\n");
	}
}

/*
 * Initialise power control and initialise the SD host. The SD host configures
 * the slot power and does not rely on it's initial state, so don't change it
 * here.
 */
static void __init comet_01xx_init_sdhost(void)
{
	int err;

	err = gpio_request_one(GPIO_PDC_GPIO0, GPIOF_OUT_INIT_LOW, "PDC_GPIO0");
	if (err)
		pr_warn("Cannot request PDC_GPIO0 GPIO for SD Card power.\n");

	comet_mci_platform_data.setpower = mci_setpower;

	comet_sdhost_init();
}


#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
static void __init comet_01xx_init_audio(void)
{
	u32 flags;

	comet_gpio_disable_block(GPIO_I2S_FIRST, GPIO_I2S_LAST);
	__global_lock2(flags);
	writel(0x01000000, CR_AUDIO_HP_CTRL);
	__global_unlock2(flags);
}
#endif

static void __init comet_01xx_init(void)
{
	comet_init_machine();

	/* SPI0 header */
	comet_01xx_init_sdhost();


#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	comet_01xx_init_audio();
#endif

	comet_01xx_init_tft();
	comet_01xx_init_usb();

#ifdef CONFIG_TZ1090_01XX_GPIO_KEYS
	comet_01xx_init_gpio_buttons();
#endif
}

/* Comet MetaMorph */

static const char *comet_01sp_boards_compat[] __initdata = {
	"img,tz1090-01sp",
	NULL,
};

MACHINE_START(COMET_01SP, "01SP Comet METAmorph")
	.dt_compat	= comet_01sp_boards_compat,
	TZ1090_MACHINE_DEFAULTS,
	.init_machine	= comet_01xx_init,
MACHINE_END

/* Comet MiniMorph */

static const char *comet_01tt_boards_compat[] __initdata = {
	"img,tz1090-01tt",
	NULL,
};

MACHINE_START(COMET_01TT, "01TT Comet MiniMorph")
	.dt_compat	= comet_01tt_boards_compat,
	TZ1090_MACHINE_DEFAULTS,
	.init_machine	= comet_01xx_init,
MACHINE_END
