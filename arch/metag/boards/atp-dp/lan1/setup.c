
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/smsc911x.h>
#include <asm/io.h>
#include <asm/soc-chorus2/gpio.h>

#define ATP_DP_LAN1_IRQ_PIN	GPIO_D_PIN(10)

#define ATP_DP_LAN1_BASE 0xc2f80000
#define ATP_DP_LAN1_LEN  0x10000

#define LAN1_FLASH_BASE  0xC1C00000
#define LAN1_FLASH_SIZE  0x800000

static struct resource smsc911x_resources[] = {
	[0] = {
		.start  = ATP_DP_LAN1_BASE,
		.end    = (ATP_DP_LAN1_BASE +
			   ATP_DP_LAN1_LEN),
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		/* start and end filled in by atp_dp_lan1_init */
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	}
};

static struct smsc911x_platform_config smsc911x_pdata = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type = SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags = SMSC911X_USE_32BIT,
	.phy_interface = PHY_INTERFACE_MODE_MII,
};

static struct platform_device smsc911x_device = {
	.name           = "smsc911x",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(smsc911x_resources),
	.resource       = smsc911x_resources,
	.dev            = {
		.platform_data = &smsc911x_pdata,
	},
};

static struct physmap_flash_data lan1_flash_data = {
	.width          = 2,
};

static struct resource lan1_flash_resources[] = {
	{
		.start  = LAN1_FLASH_BASE,
		.end    = LAN1_FLASH_BASE + LAN1_FLASH_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	}
};

static struct platform_device lan1_flash = {
	.name           = "physmap-flash",
	.id             = 0,
	.dev            = {
		.platform_data = &lan1_flash_data,
	},
	.resource       = lan1_flash_resources,
	.num_resources  = ARRAY_SIZE(lan1_flash_resources),
};


static struct platform_device *platform_devices[] __initdata = {
	&smsc911x_device,
	&lan1_flash,
};

static int __init atp_dp_lan1_init(void)
{
	int irq;

	/*
	 * Setup the RDI pin to take the ethernet interrupt.
	 * This requires the correct jumper settings on the ATP-dp board.
	 */

	gpio_request(ATP_DP_LAN1_IRQ_PIN, "ethernet irq");
	gpio_direction_input(ATP_DP_LAN1_IRQ_PIN);

	irq = gpio_to_irq(ATP_DP_LAN1_IRQ_PIN);
	smsc911x_resources[1].start = irq;
	smsc911x_resources[1].end = irq;

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	return 0;
}

__initcall(atp_dp_lan1_init);
