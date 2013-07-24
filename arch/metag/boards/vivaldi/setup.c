#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/libertas_spi.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/soc-chorus2/gpio.h>

#define WIFI_IRQ_PIN		GPIO_G_PIN(9)
#define WIFI_POWERDOWN_PIN	GPIO_G_PIN(8)
#define WIFI_RESET_PIN		GPIO_G_PIN(7)

static int vivaldi_libertas_setup(struct spi_device *spi)
{
	int err = 0;

	/* Assign the GPIO pin to get the WIFI interrupts */
	err = gpio_request(WIFI_IRQ_PIN, "wifi_irq");
	if (err)
		goto out;
	gpio_direction_input(WIFI_IRQ_PIN);

	/* Drive the powerdown pin */
	err = gpio_request(WIFI_POWERDOWN_PIN, "wifi_powerdown");
	if (err)
		goto out;
	gpio_direction_output(WIFI_POWERDOWN_PIN, 0);
	udelay(500);
	gpio_direction_output(WIFI_POWERDOWN_PIN, 1);
	udelay(1500);

	/* Drive the reset pin */
	err = gpio_request(WIFI_RESET_PIN, 0);
	if (err)
		goto out;
	gpio_direction_output(WIFI_RESET_PIN, 0);
	udelay(100);
	gpio_direction_output(WIFI_RESET_PIN, 1);

	spi->bits_per_word = 16;
	spi_setup(spi);

out:
	return err;
}

static int vivaldi_libertas_teardown(struct spi_device *spi)
{
	gpio_free(WIFI_RESET_PIN);
	gpio_free(WIFI_POWERDOWN_PIN);
	gpio_free(WIFI_IRQ_PIN);

	return 0;
}

/* Libertas board data */
static struct libertas_spi_platform_data vivaldi_libertas_pdata = {
	.use_dummy_writes	= 0,
	.setup			= vivaldi_libertas_setup,
	.teardown		= vivaldi_libertas_teardown,
};

static struct spi_board_info spi_device_info[] __initdata = {
	{
		.modalias		= "mtd_dataflash",
		.max_speed_hz		= 12500000,
		.chip_select		= 0,
	},
	{
		.modalias		= "libertas_spi",
		.max_speed_hz		= 12500000,
		.chip_select		= 1,
		.platform_data		= &vivaldi_libertas_pdata,
	},
};

static int __init vivaldi_init(void)
{
	spi_device_info[1].irq = gpio_to_irq(WIFI_IRQ_PIN);
	spi_register_board_info(spi_device_info, ARRAY_SIZE(spi_device_info));
	return 0;
}

device_initcall(vivaldi_init);
