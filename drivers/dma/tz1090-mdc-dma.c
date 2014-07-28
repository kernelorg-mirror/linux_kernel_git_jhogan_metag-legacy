/*
 * TZ1090 MDC DMA Specific Callbacks
 *
 * Copyright (C) 2010,2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/img_mdc_dma.h>

#include <asm/global_lock.h>
#include <asm/soc-tz1090/defs.h>

#define MAX_PERIPH_CHANNELS     14
#define MAX_DMA_CHANNELS	8

static DEFINE_SPINLOCK(dma_spin_lock);

static unsigned int dma_channels[MAX_DMA_CHANNELS] = {
#ifdef CONFIG_SOC_COMET_DMA0
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA1
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA2
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA3
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA4
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA5
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA6
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_COMET_DMA7
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
};

static int get_free_channel(void)
{
	int i;

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		if (dma_channels[i] == IMG_DMA_CHANNEL_AVAILABLE)
			return i;
	}
	return -1;
}

static void setup_dma_channel(int dmanr, unsigned int periph)
{
	int lstat;
	u32 mux_val;

	__global_lock2(lstat);
	mux_val = readl(CR_PERIP_DMA_ROUTE_SEL2_REG);

	mux_val &= ~(0xf << (dmanr*4));
	mux_val |= (periph & 0xf) << (dmanr*4);

	writel(mux_val, CR_PERIP_DMA_ROUTE_SEL2_REG);
	__global_unlock2(lstat);
}

static int img_is_chan_available(int dmanr)
{
	return dma_channels[dmanr] == IMG_DMA_CHANNEL_AVAILABLE;
}

static int img_request_dma(int dmanr, unsigned int periph)
{
	unsigned long flags;
	int err;

	if (dmanr >= MAX_DMA_CHANNELS)
		return -EINVAL;

	if (periph > MAX_PERIPH_CHANNELS)
		return -EINVAL;

	spin_lock_irqsave(&dma_spin_lock, flags);

	/* If dmanr is -1 we pick the DMA channel to use. */
	if (dmanr == -1) {
		dmanr = get_free_channel();
		if (dmanr == -1) {
			err = -EBUSY;
			goto out;
		}
	}

	if (dma_channels[dmanr] != IMG_DMA_CHANNEL_AVAILABLE) {
		err = -EBUSY;
		goto out;
	}

	dma_channels[dmanr] = IMG_DMA_CHANNEL_INUSE;

	setup_dma_channel(dmanr, periph);

	err = dmanr;
out:
	spin_unlock_irqrestore(&dma_spin_lock, flags);

	return err;
}

static int img_free_dma(int dmanr)
{
	unsigned long flags;

	if (dmanr >= MAX_DMA_CHANNELS)
		return -EINVAL;

	spin_lock_irqsave(&dma_spin_lock, flags);

	if (dma_channels[dmanr] == IMG_DMA_CHANNEL_INUSE)
		dma_channels[dmanr] = IMG_DMA_CHANNEL_AVAILABLE;

	setup_dma_channel(dmanr, 0);

	spin_unlock_irqrestore(&dma_spin_lock, flags);

	return 0;
}

#ifdef CONFIG_METAG_SUSPEND_MEM

/* suspend state */

void *img_dma_suspend(void)
{
	return (void *)readl(CR_PERIP_DMA_ROUTE_SEL2_REG);
}

void img_dma_resume(void *data)
{
	unsigned int lstat, i;
	u32 mux, mask = 0;
	u32 dma_channel_mux = (u32)data;

	/* only restore muxes of unreserved channels */
	for (i = 0; i < MAX_DMA_CHANNELS; ++i)
		if (dma_channels[i] != IMG_DMA_CHANNEL_RESERVED)
			mask |= (0xf << (i*4));

	__global_lock2(lstat);
	mux = readl(CR_PERIP_DMA_ROUTE_SEL2_REG);
	mux &= ~mask;
	mux |= dma_channel_mux & mask;
	writel(mux, CR_PERIP_DMA_ROUTE_SEL2_REG);
	__global_unlock2(lstat);
}

#else
#define img_dma_suspend NULL
#define img_dma_resume NULL
#endif	/* CONFIG_METAG_SUSPEND_MEM */

#ifdef CONFIG_PROC_FS

static const char * const periph_names[] = {
	"Unused",
	"SDIO Wr",
	"SDIO Rd",
	"SPI Master0 Wr",
	"SPI Master0 Rd",
	"SPI Slave Wr",
	"SPI Slave Rd",
	"SPI Master1 Wr",
	"SPI Master1 Rd",
	"I2S Write",
	"I2S Read",
	"LCD",
	"SDHOST Wr",
	"SDHOST Rd",
};

static const char * const state_names[] = {
	"Reserved",
	"Available",
	"In Use",
};

static void proc_dma_show_channels(struct seq_file *m)
{
	int i;

	u32 mux_val = readl(CR_PERIP_DMA_ROUTE_SEL2_REG);

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		int periph = (mux_val >> (i*4)) & 0xF;
		if (periph < sizeof(periph_names)) {
			seq_printf(m, "Channel %2d: %s (%s)\n", i,
				periph_names[periph],
				state_names[dma_channels[i]]);
		} else {
			seq_printf(m, "Channel %2d: %d (%s)\n", i,
				periph,
				state_names[dma_channels[i]]);
		}

	}

}

static int proc_dma_show(struct seq_file *m, void *v)
{
	proc_dma_show_channels(m);

	return 0;
}

static int proc_dma_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_dma_show, NULL);
}

static const struct file_operations proc_dma_operations = {
	.open = proc_dma_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init proc_dma_init(void)
{
	proc_create("dma", 0, NULL, &proc_dma_operations);
	return 0;
}

device_initcall(proc_dma_init);
#endif

static struct img_mdc_soc_callbacks comet_dma_callbacks = {
	.allocate = img_request_dma,
	.available = img_is_chan_available,
	.free = img_free_dma,
	.suspend = img_dma_suspend,
	.resume = img_dma_resume,
};

static const struct of_device_id tz1090_mdc_dma_match[] = {
	{ .compatible = "img,tz1090-mdc-dma", .data = &comet_dma_callbacks, },
	{},
};
MODULE_DEVICE_TABLE(of, tz1090_mdc_dma_match);

static int tz1090_mdc_dma_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct img_mdc_soc_callbacks *c;
	int ret;

	match = of_match_node(tz1090_mdc_dma_match, pdev->dev.of_node);
	c = match->data;

	ret = mdc_dma_probe(pdev, c);

	dev_info(&pdev->dev,
		 "tz1090 mdc dma callback driver probed successfully.\n");

	return ret;
};

static int tz1090_mdc_dma_remove(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
	return 0;
}

static struct platform_driver tz1090_mdc_dma_pltfm_driver = {
	.remove		= tz1090_mdc_dma_remove,
	.driver		= {
		.name		= "tz1090-mdc-dma",
		.of_match_table	= tz1090_mdc_dma_match,
	},
};

static int __init tz1090_mdc_dma_init(void)
{
	return platform_driver_probe(&tz1090_mdc_dma_pltfm_driver,
				     tz1090_mdc_dma_probe);
}
subsys_initcall(tz1090_mdc_dma_init);

static void tz1090_mdc_dma_exit(void)
{
	return platform_driver_unregister(&tz1090_mdc_dma_pltfm_driver);
}
module_exit(tz1090_mdc_dma_exit);

MODULE_DESCRIPTION("TZ1090 Specific MDC DMA Callbacks");
MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("tz1090-mdc-dma");
