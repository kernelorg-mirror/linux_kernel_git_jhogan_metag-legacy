/*
 * boards/comet-bub/touchscreen/setup.c
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 */

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/input/ts_qt5480.h>

#include <asm/soc-tz1090/gpio.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#include "ts_qt5480_cfg.c"

#if defined(CONFIG_TZ1090_01XX)
#define TS_GPIO_PIN	GPIO_UART0_CTS
#define TS_I2C_BUS	2
#elif defined(CONFIG_COMET_BUB)
#define TS_GPIO_PIN	GPIO_PDM_C
#define TS_I2C_BUS	2
#elif defined(CONFIG_POLARIS)
#define TS_GPIO_PIN	GPIO_SYS_WAKE1
#define TS_I2C_BUS	0
#else
#error Sensia touchscreen enabled for unsupported board?
#endif

static ts_qt5480_mapping_t phy_map = {
	x_sensor_res: 255,
	x_screen_res: 640,
	x_flip: 0,
	x_sensor_size: 117,
	x_screen_size: 120,
	x_sensor_offset: 3,
	y_sensor_res: 255,
	y_screen_res: 480,
	y_flip: 0,
	y_sensor_size: 89,
	y_screen_size: 95,
	y_sensor_offset: 3
};

int qt5480_poll_status(void)
{
	return gpio_get_value(TS_GPIO_PIN);
}

static struct qt5480_platform_data qt5480_data = {
	.poll_status	= qt5480_poll_status,
	.phy_map	= &phy_map,
	.config		= config,
};

static struct i2c_board_info __initdata touchscreen_i2c_devices[] = {
	{
		I2C_BOARD_INFO("ts_qt5480", 0x30),
		.type = "ts_qt5480",
		.platform_data = &qt5480_data,
	},
};

static int __init touchscreen_setup(void)
{
	int irq, ret;

	ret = gpio_request(TS_GPIO_PIN, "ts_qt5480 irq gpio" );
	if (ret) {
		pr_err("ts_qt5480 gpio (%d) not available\n",
		       TS_GPIO_PIN);
		return 1;
	}

	irq = gpio_to_irq(TS_GPIO_PIN);
	if (irq < 0) {
		pr_err("ts_qt5480 gpio irq not available\n");
		return 1;
	}
	gpio_direction_input(TS_GPIO_PIN);
	comet_gpio_pullup_type(TS_GPIO_PIN, GPIO_PULLUP_TRISTATE);
	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
	touchscreen_i2c_devices[0].irq = irq;

	i2c_register_board_info(TS_I2C_BUS, touchscreen_i2c_devices,
				ARRAY_SIZE(touchscreen_i2c_devices));

	return 0;

}
device_initcall(touchscreen_setup);
