/*
 * Chorus2 specific DMA code.
 *
 * Copyright (C) 2007,2008 Imagination Technologies Ltd.
 *
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/global_lock.h>
#include <asm/img_dma.h>
#include <asm/soc-chorus2/dma.h>



static DEFINE_SPINLOCK(dma_spin_lock);

static unsigned int dma_channels[MAX_DMA_CHANNELS] = {
#ifdef CONFIG_SOC_CHORUSX_DMA0
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA1
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA2
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA3
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA4
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA5
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA6
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA7
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA8
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA9
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA10
	IMG_DMA_CHANNEL_AVAILABLE,
#else
	IMG_DMA_CHANNEL_RESERVED,
#endif
#ifdef CONFIG_SOC_CHORUSX_DMA11
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
	unsigned int shift, val, mask;
	int flags;
	unsigned int *reg = NULL;

	if ((dmanr >= 0) && (dmanr < 4))
		reg = (unsigned int *)DMA_CHAN_SEL_0_3;
	else if ((dmanr >= 4) && (dmanr < 8))
		reg = (unsigned int *)DMA_CHAN_SEL_4_7;
	else if ((dmanr >= 8) && (dmanr < 12))
		reg = (unsigned int *)DMA_CHAN_SEL_8_11;

	BUG_ON(!reg);

	/* Peripherals are specified using 6 bits but have an 8 bit stride
	   in the register. */
	shift = (dmanr & 0x3) * 8;

	mask = ~(DMAC_PERIPH_MASK << shift);

	periph = periph << shift;

	__global_lock2(flags);

	val = ioread32(reg);
	val &= mask;
	val |= periph;
	iowrite32(val, reg);

	__global_unlock2(flags);
}

int img_request_dma(int dmanr, unsigned int periph)
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
EXPORT_SYMBOL(img_request_dma);

int img_free_dma(int dmanr)
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
EXPORT_SYMBOL(img_free_dma);

unsigned int *get_dma_regs(int dmanr)
{
	return (unsigned int *)(DMAC_HWBASE + (dmanr * DMAC_CHANNEL_STRIDE));
}
EXPORT_SYMBOL(get_dma_regs);

#ifdef CONFIG_PROC_FS

static const char *periph_names[] = {
	"Local Bus",
	"Reed Solomon Input",
	"Reed Solomon Output",
	"Memory Stick",
	"4",
	"SCP input",
	"SCP output",
	"7",
	"ECP output",
	"ATAPI",
	"SPI 1 output",
	"SPI 1 input",
	"SPI 2 output",
	"SPI 2 input",
	"Noise Shaper",
	"LCD",
	"I2S out",
	"I2S in",
	"SPDIF out",
	"SPDIF in",
	"I2S out",
	"Core/SCP",
	"Core/Local",
	"Core",
	"RDI",
	"NAND",
};

const static char *state_names[] = {
	"Reserved",
	"Available",
	"In Use",
};

static int proc_dma_show_reg(struct seq_file *m, unsigned int val, int chan)
{
	int i;

	for (i = 0; i < 4; i++, chan++) {
		int periph = (val >> (i * 8)) & DMAC_PERIPH_MASK;
		if (periph < sizeof(periph_names))
			seq_printf(m, "%2d: %s (%s)\n", chan,
				   periph_names[periph],
				   state_names[dma_channels[chan]]);
		else
			seq_printf(m, "%2d: %d (%s)\n", chan, periph,
				   state_names[dma_channels[chan]]);
	}
	return chan;
}

static int proc_dma_show(struct seq_file *m, void *v)
{
	int chan = 0;
	unsigned int *reg;

	reg = (unsigned int *)DMA_CHAN_SEL_0_3;
	chan = proc_dma_show_reg(m, *reg, chan);

	reg = (unsigned int *)DMA_CHAN_SEL_4_7;
	chan = proc_dma_show_reg(m, *reg, chan);

	reg = (unsigned int *)DMA_CHAN_SEL_8_11;
	chan = proc_dma_show_reg(m, *reg, chan);

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

__initcall(proc_dma_init);
#endif
