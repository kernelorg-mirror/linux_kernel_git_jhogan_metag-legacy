#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>
#include <linux/i2c.h>
#include <linux/i2c/pca953x.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <asm/soc-chorus2/gpio.h>

#define WRITE_PROTECT_PIN  GPIO_EXP_PIN(12)
#define CARD_DETECT_PIN    GPIO_EXP_PIN(13)
/* LED used to signal card detect */
#define LED_PIN            GPIO_EXP_PIN(14)
/* Interrupt mask pin needs to be driven high to enable interrupt */
#define INT_MASK_PIN       GPIO_EXP_PIN(15)

static irqreturn_t (*mmc_spi_handler)(int, void *);
static int mmc_init;

static irqreturn_t mmc_detect_check(int irq, void *mmc)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t mmc_detect_irq(int irq, void *mmc)
{
	/* Reading the value acks the interrupt */
	if (gpio_get_value_cansleep(CARD_DETECT_PIN) == 0)
		gpio_set_value_cansleep(LED_PIN, 0);
	else
		gpio_set_value_cansleep(LED_PIN, 1);

	return mmc_spi_handler(irq, mmc);
}

static int mmc_spi_init(struct device *dev,
			irqreturn_t (*handler)(int, void *),
			void *mmc)
{
	int err = 0;
	int irq;
	err |= gpio_request(WRITE_PROTECT_PIN, "SD write protect");
	err |= gpio_request(CARD_DETECT_PIN, "SD card detect");
	err |= gpio_request(INT_MASK_PIN, "SD card detect (int mask)");
	err |= gpio_request(LED_PIN, "LED");
	err |= gpio_request(GPIO_H_PIN(5), "mmc card detect irq");

	if (err) {
		printk(KERN_WARNING "request for mmc card detect gpios failed\n");
		return 0;
	}

	gpio_direction_input(WRITE_PROTECT_PIN);
	gpio_direction_input(CARD_DETECT_PIN);

	gpio_direction_output(INT_MASK_PIN, 1);
	gpio_direction_output(LED_PIN, 1);

	if (gpio_get_value_cansleep(CARD_DETECT_PIN) == 0)
		gpio_set_value_cansleep(LED_PIN, 0);

	irq = gpio_to_irq(GPIO_H_PIN(5));
	gpio_direction_input(GPIO_H_PIN(5));

	mmc_spi_handler = handler;

	if (request_threaded_irq(irq, mmc_detect_check, mmc_detect_irq,
				 IRQF_TRIGGER_FALLING, "mmc_card_detect",
				 mmc)) {
		printk(KERN_WARNING "failed to get mmc card detect irq\n");
	}

	mmc_init = 1;

	return 0;
}

static int mmc_spi_get_ro(struct device *dev)
{
	if (mmc_init)
		return gpio_get_value_cansleep(WRITE_PROTECT_PIN);
	return 1;
}

struct mmc_spi_platform_data mmc_spi_data = {
	.init = &mmc_spi_init,
	.get_ro = &mmc_spi_get_ro,
};

static struct spi_board_info spi_device_info[] __initdata = {
#if 0
	{
		.modalias       = "mtd_dataflash",
		.max_speed_hz   = 12500000,
		.chip_select    = 0,
	},
#endif
	{
		.modalias       = "mmc_spi",
		.max_speed_hz   = 12500000,
		.chip_select    = 2,
		.platform_data  = &mmc_spi_data,
	},
};

static struct pca953x_platform_data pca9555_data = {
	.gpio_base = GPIO_EXP_BASE,
};

static struct i2c_board_info __initdata atp_dp_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pca953x", 0x20),
		.type = "pca9555",
		.platform_data = &pca9555_data,
	},
};

static void __init atp_dp_i2c_init(void)
{
	i2c_register_board_info(1, atp_dp_i2c_devices,
				ARRAY_SIZE(atp_dp_i2c_devices));
}

static int __init atp_dp_init(void)
{
	spi_register_board_info(spi_device_info, ARRAY_SIZE(spi_device_info));
	atp_dp_i2c_init();
	return 0;
}

device_initcall(atp_dp_init);
