#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <asm/coremem.h>
#include <asm/mach/arch.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/pdc.h>
#include <asm/soc-tz1090/pm.h>
#include <asm/soc-tz1090/setup.h>
#include <asm/soc-tz1090/usb.h>
#include "gpio.h"
#include "poweroff.h"
#include "setup.h"

#define GPIO_KEY_DEBOUNCE_MSEC		5	/* ms */

/* No XTAL3 (32.768KHz) oscillator is fitted */
unsigned long get_xtal3(void)
{
	return 0;
}

/*
 * Backlight is powered by boost converter on USB power switch chip. and must be
 * turned on for VBUS to be powered.
 */
static DEFINE_SPINLOCK(bl_boost_lock);
#define POLARIS_BL_BOOST_SETUP_M	(1 << 31)
static unsigned int bl_boost_users;

static int polaris_bl_boost_setup(void)
{
	unsigned long flags;
	int err = 0;
	spin_lock_irqsave(&bl_boost_lock, flags);

	/* is it already set up? */
	if (bl_boost_users & POLARIS_BL_BOOST_SETUP_M)
		goto out;

	/* get control of EN_TFT_BL */
	err = gpio_request(EN_TFT_BL, "EN_TFT_BL");
	if (err) {
		pr_err("Couldn't get EN_TFT_BL gpio!\n");
		goto out;
	}

	err = gpio_direction_output(EN_TFT_BL, 1);
	if (err) {
		pr_err("Couldn't make EN_TFT_BL an output!\n");
		gpio_free(EN_TFT_BL);
		goto out;
	}

	/* record that the GPIO is configured */
	bl_boost_users |= POLARIS_BL_BOOST_SETUP_M;

out:
	spin_unlock_irqrestore(&bl_boost_lock, flags);
	return err;
}

int polaris_bl_boost_set(unsigned int user, int en)
{
	unsigned long flags;
	int ret;

	if (user >= 31)
		return -EINVAL;

	/* ensure backlight is setup */
	ret = polaris_bl_boost_setup();

	spin_lock_irqsave(&bl_boost_lock, flags);
	if (en)
		bl_boost_users |= (1 << user);
	else
		bl_boost_users &= ~(1 << user);
	/* update backlight boost power */
	if (!ret)
		gpio_set_value(EN_TFT_BL,
			       bl_boost_users & ~POLARIS_BL_BOOST_SETUP_M);
	spin_unlock_irqrestore(&bl_boost_lock, flags);
	return ret;
}

/*------------------------------------ USB/VBUS ------------------------------*/
static void polaris_enable_vbus(void)
{
	polaris_bl_boost_set(POLARIS_BL_BOOST_VBUS, 1);
	gpio_set_value(EN_VBUS, 1);
}

static void polaris_disable_vbus(void)
{
	gpio_set_value(EN_VBUS, 0);
	polaris_bl_boost_set(POLARIS_BL_BOOST_VBUS, 0);
}

static struct dwc_otg_board polaris_usb_board = {
	.enable_vbus = polaris_enable_vbus,
	.disable_vbus = polaris_disable_vbus,
};

static int __init polaris_init_usb(void)
{
	if (gpio_request(EN_VBUS, "EN_VBUS"))
		pr_err("Failed to request EN_VBUS GPIO\n");

	gpio_direction_output(EN_VBUS, 1);

	comet_usb_setup(&polaris_usb_board);

	return 0;
}

/*--------------------------- GPIO Buttons -----------------------------------*/
static struct gpio_keys_button polaris_gpio_buttons[] = {
	{
		.code			= KEY_VOLUMEDOWN,
		.gpio			= GPIO_PLL_ON,
		.desc			= "Vol-",
		.type			= EV_KEY,
		.debounce_interval	= GPIO_KEY_DEBOUNCE_MSEC,
		.can_disable		= true,
	},
	{
		.code			= KEY_VOLUMEUP,
		.gpio			= GPIO_PA_ON,
		.desc			= "Vol+",
		.type			= EV_KEY,
		.debounce_interval	= GPIO_KEY_DEBOUNCE_MSEC,
		.can_disable		= true,
	},
	{
		.code			= KEY_POWER,
		.gpio			= GPIO_SYS_WAKE0,
		.desc			= "Power",
		.type			= EV_KEY,
		.wakeup			= true,
		.debounce_interval	= GPIO_KEY_DEBOUNCE_MSEC,
		.can_disable		= true,
	},
};

static struct gpio_keys_platform_data polaris_gpio_buttons_data = {
	.buttons	= polaris_gpio_buttons,
	.nbuttons	= ARRAY_SIZE(polaris_gpio_buttons),
	.rep		= false, /* enable auto repeat */
};

static struct platform_device polaris_gpio_buttons_device = {
	.name		= "gpio-keys",
	.dev		= {
		.platform_data	= &polaris_gpio_buttons_data,
	}
};

static int __init polaris_init_gpio_buttons(void)
{
	int err;

	err = platform_device_register(&polaris_gpio_buttons_device);
	if (err) {
		pr_err("gpio-keys: register failed: %d\n", err);
		return err;
	}

	return 0;
}

/*--------------------------- power management -------------------------------*/
static void polaris_power_off(void)
{
	struct metag_coremem_region *reg;
	void (*power_off_func)(void);
	unsigned long flags;

	/*
	 * Ensure power button is set as wake.
	 */
	enable_irq_wake(gpio_to_irq(GPIO_SYS_WAKE0));

	/*
	 * Increase delay after reset and power on for the external regulators.
	 * 0x7 = 128 clock cycles (32kHz clock domain).
	 */
	writel(0x07777777, PDC_BASE_ADDR + PDC_SOC_DELAY);

	/*
	 * Cutting +1v8 power supply kills RAM, so we need to ensure no RAM
	 * accesses occur after this is done. This is guaranteed by executing a
	 * stub of assembly code from locked in cache lines.
	 */

	/* Get the address of some core memory */
	reg = metag_coremem_alloc(METAG_COREMEM_ICACHE,
				  polaris_raw_power_off_sz);
	if (!reg) {
		pr_err("Couldn't allocate core memory for power off\n");
		return;
	}

	/* Copy the power off code into core memory */
	power_off_func = metag_coremem_push(reg, polaris_raw_power_off,
					    polaris_raw_power_off_sz);
	if (!power_off_func) {
		pr_err("Couldn't push power off function to core memory\n");
		metag_coremem_free(reg);
		return;
	}

	/* Run the poweroff code, with other threads stopped */
	__global_lock2(flags);
	wmb();
	power_off_func();

	/* polaris_raw_power_off() should never return */
	BUG();
}

#ifdef CONFIG_PM_SLEEP
static int polaris_suspend(suspend_state_t state)
{
	/*
	 * Increase delay after reset and power on for the external regulators.
	 * 0x7 = 128 clock cycles (32kHz clock domain).
	 */
	writel(0x07777777, PDC_BASE_ADDR + PDC_SOC_DELAY);

	/*
	 * Kill 3v3 supply.
	 */
	gpio_direction_output(KILL_3V3_MAIN, 1);
	udelay(1000);
	gpio_direction_output(KILL_3V3_MAIN, 0);

	return 0;
}

static void polaris_resume(suspend_state_t state)
{
	/*
	 * Standby won't restore 3v3 because EXT_POWER doesn't change, so use
	 * LATCH_3V3_MAIN to turn it back on again.
	 */
	if (state == PM_SUSPEND_STANDBY) {
		gpio_direction_output(LATCH_3V3_MAIN, 1);
		udelay(5000);
		gpio_direction_output(LATCH_3V3_MAIN, 0);
	}
}
#endif	/* CONFIG_PM_SLEEP */

static void polaris_init_pm(void)
{
	int kill_1v8_err, kill_3v3_err, latch_3v3_err;

	/* Request power kill GPIOs */
	kill_1v8_err = gpio_request(KILL_1V8, "KILL_1V8");
	if (kill_1v8_err) {
		pr_err("%s: Could not get KILL_1V8 GPIO", __func__);
		goto err_kill_1v8;
	}

	kill_3v3_err = gpio_request(KILL_3V3_MAIN, "KILL_3V3_MAIN");
	if (kill_3v3_err) {
		pr_err("%s: Could not get KILL_3V3_MAIN GPIO", __func__);
		goto err_kill_3v3;
	}

	latch_3v3_err = gpio_request(LATCH_3V3_MAIN, "LATCH_3V3_MAIN");
	if (latch_3v3_err) {
		pr_err("%s: Could not get LATCH_3V3_MAIN GPIO", __func__);
		goto err_latch_3v3;
	}

	/* Register board callbacks */
	board_power_off = polaris_power_off;
#ifdef CONFIG_PM_SLEEP
	board_suspend = polaris_suspend;
	board_resume = polaris_resume;
#endif

	return;

err_latch_3v3:
	gpio_free(KILL_3V3_MAIN);
err_kill_3v3:
	gpio_free(KILL_1V8);
err_kill_1v8:
	return;
}

/*--------------------------- Init -------------------------------------------*/
static void __init polaris_init(void)
{
	comet_init_machine();

	polaris_init_gpio_buttons();
	polaris_init_usb();
	polaris_init_pm();
}

/* PURE Polaris */

static const char *polaris_boards_compat[] __initdata = {
	"img,tz1090-01xk",
	NULL,
};

MACHINE_START(PURE_01XK, "01XK PURE Polaris")
	.dt_compat	= polaris_boards_compat,
	TZ1090_MACHINE_DEFAULTS,
	.init_machine	= polaris_init,
MACHINE_END
