/*
 * Copyright (C) 2007 Imagination Technologies
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/serial_8250.h>
#include <linux/fsl_devices.h>
#include <linux/spi/spi_img.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/soc-chorus2/c2_irqnums.h>
#include <asm/soc-chorus2/gpio.h>
#include <asm/soc-chorus2/setup.h>

#define CHORUS2_UART_REF_CLK 0x020000A4

#define CHORUS2_UART_ENABLE 0x4

/* USB starts at this address, EHCI regs are 0x100 above */
#define CHORUS2_USB_BASE   0x0200C000
#define CHORUS2_USB_LEN    0x00001000

#define EHCI_OFFSET        0x00000100
#define CHORUS2_EHCI_BASE  (CHORUS2_USB_BASE + EHCI_OFFSET)
#define CHORUS2_EHCI_LEN   (CHORUS2_USB_LEN - EHCI_OFFSET)

#define CHORUS2_SPI1_HWBASE    0x02008000

#define CHORUS2_SCBA_BASE  0x0200A000

#define CHORUS2_SCBB_BASE  0x0200B000

#define MISC_CLOCK_CTRL_REG 0x020000bc

#define DMATRIG_SPI1O_NUM       10	/* SPI 1 output DMA */
#define DMATRIG_SPI1I_NUM       11      /* SPI 1 input DMA */

static void __init chorus2_serial_init(void)
{
	chorus2_gpio_disable(GPIO_E_PIN(8));  /* S1 In */
	chorus2_gpio_disable(GPIO_E_PIN(9));  /* S1 Out */

	/* Enable the UART clock - set for the default 1.8432 MHz */
	writel(CHORUS2_UART_ENABLE, CHORUS2_UART_REF_CLK);

}

#ifdef CONFIG_USB_EHCI_HCD
static struct resource usb_resources[] = {
	[0] = {
		.start          = CHORUS2_EHCI_BASE,
		.end            = CHORUS2_EHCI_BASE + CHORUS2_EHCI_LEN - 1,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start          = USB_IRQ_NUM,
		/* mapped in chorus2_usb_init() */
		.flags          = IORESOURCE_IRQ,
	},
};

static u64 ehci_dmamask = DMA_BIT_MASK(32);

static struct platform_device usb_device = {
	.name           = "chorus2-ehci",
	.id             = 0,
	.dev = {
		.dma_mask               = &ehci_dmamask,
		.coherent_dma_mask      = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(usb_resources),
	.resource       = usb_resources,
};

#else

static struct resource usb_resources[] = {
	[0] = {
		.start          = CHORUS2_USB_BASE,
		.end            = CHORUS2_USB_BASE + CHORUS2_USB_LEN - 1,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start          = USB_IRQ_NUM,
		/* mapped in chorus2_usb_init() */
		.flags          = IORESOURCE_IRQ,
	},
};

static struct fsl_usb2_platform_data fsl_usb_data = {
        .operating_mode = FSL_USB2_DR_DEVICE,
        .phy_mode = FSL_USB2_PHY_UTMI,
};

static u64 fsl_usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device usb_device = {
	.name           = "fsl-usb2-udc",
	.id             = 0,
	.dev = {
		.dma_mask               = &fsl_usb_dmamask,
		.coherent_dma_mask      = 0xffffffff,
		.platform_data          = &fsl_usb_data,
	},
	.num_resources  = ARRAY_SIZE(usb_resources),
	.resource       = usb_resources,
};
#endif

static void __init chorus2_usb_init(void)
{
	int irq;
	u32 val;

	/* Map the IRQ */
	irq = external_irq_map(usb_resources[1].start);
	if (irq < 0) {
		pr_err("%s: irq map failed (%d)\n",
		       __func__, irq);
		return;
	}
	usb_resources[1].start = irq;
	usb_resources[1].end = irq;

	/*
	 * Rev C and later silicon require this bit to be set to disable
	 * IDDQ mode.
	 */
	val = readl(MISC_CLOCK_CTRL_REG);
	writel(val | 0x2, MISC_CLOCK_CTRL_REG);

	platform_device_register(&usb_device);
}

static struct resource spi_resources[] = {
	[0] = {
		.start          = SPI1_DMAR_IRQ_NUM,
		/* mapped in chorus2_spi_init() */
		.flags          = IORESOURCE_IRQ,
	},
	[1] = {
		.start          = CHORUS2_SPI1_HWBASE,
		.end            = CHORUS2_SPI1_HWBASE + 0xfff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct img_spi_master spi_platform_data = {
	.num_chipselect = 3,
	.tx_dma_channel_num = -1, /*auto allocate*/
	.tx_dma_peripheral_num = DMATRIG_SPI1O_NUM,
	.rx_dma_channel_num = -1,
	.rx_dma_peripheral_num = DMATRIG_SPI1I_NUM,
};

static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct platform_device spi_master_device = {
	.name = "img-spi",
	.id = 0,
	.num_resources = ARRAY_SIZE(spi_resources),
	.resource = spi_resources,
	.dev = {
		.dma_mask = &spi_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(64),
		.platform_data = &spi_platform_data, /* Passed to driver */
	},
};

static void __init chorus2_spi_init(void)
{
	int irq;

	/* Map the IRQ */
	irq = external_irq_map(spi_resources[0].start);
	if (irq < 0) {
		pr_err("%s: irq map failed (%d)\n",
		       __func__, irq);
		return;
	}
	spi_resources[0].start = irq;
	spi_resources[0].end = irq;

	platform_device_register(&spi_master_device);
}

static struct platform_device asoc_device = {
	.name = "chorus2-pcm-audio",
	.id = -1,
};


static struct platform_device *chorus2_devices[] __initdata = {
	&asoc_device,
};

void __init chorus2_init_machine(void)
{
	chorus2_serial_init();
	chorus2_usb_init();
	chorus2_spi_init();
	platform_add_devices(chorus2_devices, ARRAY_SIZE(chorus2_devices));

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *chorus2_boards_compat[] __initdata = {
	"frontier,chorus2",
	NULL,
};

MACHINE_START(CHORUS2, "Generic Chorus2")
	.dt_compat	= chorus2_boards_compat,
	CHORUS2_MACHINE_DEFAULTS,
MACHINE_END
