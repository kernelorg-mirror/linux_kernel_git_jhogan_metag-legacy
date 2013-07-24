/*
 * sdhost.c - contains block specific initialisation routines
 *
 * Copyright (C) 2011 Imagination Technologies Ltd.
 *
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/dma-mapping.h>
#include <linux/img_mdc_dma.h>
#include <asm/global_lock.h>
#include <asm/soc-tz1090/sdhost.h>
#include <asm/soc-tz1090/clock.h>
#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/pdc.h>
#include <asm/soc-tz1090/defs.h>
#include <linux/usb/dwc_otg_platform.h>


#define COMET_SDHOST_CLK	100000000UL

int mci_init(u32 slot_id, irq_handler_t irqhdlr, void *data)
{
	/*
	 * Used to setup gpio based card detect interrupt handler
	 * on a per slot basis, we are using the modules built in
	 * card detect functionality, so do nothing (must be implemented).
	 */

	return 0;
}

int mci_get_ocr(u32 slot_id)
{
	return MMC_VDD_32_33 | MMC_VDD_33_34;
}

int mci_get_bus_wd(u32 slot_id)
{
	return 4;
}


static struct resource comet_mci_resources[] = {
	[0] = {
		.start  = SDIO_HOST_BASE_ADDR,
		.end	= SDIO_HOST_BASE_ADDR + SDIO_HOST_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SDIO_HOST_IRQ_NUM,
		/* mapped in comet_sdhost_init() */
		.flags	= IORESOURCE_IRQ,
	},
};

struct dma_pdata {
	unsigned int tx_dma;
	unsigned int rx_dma;
} dma_pdata = {
	.tx_dma			= DMA_MUX_SDIO_HOST_WR,
	.rx_dma			= DMA_MUX_SDIO_HOST_RD,
};


struct block_settings blk_settings = {
	.max_segs	= PAGE_SIZE / sizeof(struct img_dma_mdc_list),
	.max_blk_size	= 65536, /* BLKSIZ is 16 bits, dma could do 24 bits*/
	.max_blk_count	= PAGE_SIZE / sizeof(struct img_dma_mdc_list),
	.max_req_size	= 65536 * PAGE_SIZE / sizeof(struct img_dma_mdc_list),
	.max_seg_size	= 65536 * PAGE_SIZE / sizeof(struct img_dma_mdc_list),
};


static u64 mci_dmamask = DMA_BIT_MASK(32);

struct dw_mci_board comet_mci_platform_data = {
	.detect_delay_ms	= 250,
#ifdef CONFIG_SOC_COMET_ES1
	.quirks			= 0,
#else
	.quirks			= DW_MCI_QUIRK_RETRY_DELAY |
				  DW_MCI_QUIRK_HIGHSPEED |
				  DW_MCI_QUIRK_GPIO_UNLOCK |
				  DW_MCI_QUIRK_BIT_BANG,
#endif
	.clk_pin		= GPIO_SDIO_CLK,
	.cmd_pin		= GPIO_SDIO_CMD,
	.fifo_depth		= 32,
	.init			= mci_init,
	.get_ocr		= mci_get_ocr,
	.get_bus_wd		= mci_get_bus_wd,
	.setpower		= NULL,	/* boards can override this */
#ifndef CONFIG_MMC_DW_IDMAC
	.dma_ops		= &comet_dma_ops,
#endif
	.data			= &dma_pdata,
	.blk_settings		= &blk_settings,
};

static struct platform_device	comet_mci_device = {
	.name		= "dw_mmc",
	.num_resources	= ARRAY_SIZE(comet_mci_resources),
	.dev		= {
		.dma_mask		= &mci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &comet_mci_platform_data,
	},
	.resource	= comet_mci_resources,
};

int __init comet_sdhost_init(void)
{
	unsigned long sdhost_clk = set_sdhostclock(COMET_SDHOST_CLK);
	int irq;

	if (COMET_SDHOST_CLK != sdhost_clk) {
		printk(KERN_WARNING "Comet SD-Host Init: Requested %lu HZ SD "
				"Clock Actual SF CLk = %lu HZ\n",
				COMET_SDHOST_CLK, sdhost_clk);
	}

	/* Set the bus speed to send to the driver */
	comet_mci_platform_data.bus_hz = get_sdhostclock();

	/* map IRQs */
	irq = external_irq_map(comet_mci_resources[1].start);
	if (irq < 0) {
		pr_err("%s: unable to map SD-Host irq %u (%d)\n",
		       __func__, comet_mci_resources[1].start, irq);
		return irq;
	}
	comet_mci_resources[1].start = irq;
	comet_mci_resources[1].end = irq;

	return platform_device_register(&comet_mci_device);
}
