/*
 * IMG DMA Controller (DMAC) specific DMA code.
 *
 * Copyright (C) 2010,2012 Imagination Technologies Ltd.
*/
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/img_dma.h>
#include <asm/img_dmac.h>

#if defined(CONFIG_SOC_CHORUS2)
#include <asm/soc-chorus2/dma.h>
#include <asm/soc-chorus2/c2_irqnums.h>
#else
#error /*Add your SoC here*/
#endif

int img_dma_reset(int dmanr)
{
	unsigned int *dmac_base;

	if (dmanr >= MAX_DMA_CHANNELS)
		return -EINVAL;

	dmac_base = (unsigned int *)(DMAC_HWBASE +
				     (dmanr * DMAC_CHANNEL_STRIDE));

	/* Set SRST bit in DMAC_COUNT_REG */
	iowrite32(DMAC_CNTN_REG_SRST_BIT, dmac_base+DMAC_COUNT_REG);

	/* Clear all other registers */
	iowrite32(0, dmac_base+DMAC_SETUP_REG);
	iowrite32(0, dmac_base+DMAC_PERIPH_REG);
	iowrite32(0, dmac_base+DMAC_IRQSTAT_REG);
	iowrite32(0, dmac_base+DMAC_2DMODE_REG);

	/* Finally release the SRST bit */
	iowrite32(0, dmac_base+DMAC_COUNT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_reset);

int img_dma_get_irq(int dmanr)
{
	return external_irq_map(DMA0_IRQ_NUM + dmanr);
}
EXPORT_SYMBOL(img_dma_get_irq);

int img_dma_set_data_address(int dmanr, u32 address)
{
	u32 *base;

	base = get_dma_regs(dmanr);
	iowrite32(address, base+DMAC_SETUP_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_data_address);

int img_dma_set_length(int dmanr, unsigned long bytes, unsigned int width)
{
	u32 c;
	u32 *base;
	u16 count = (u16)bytes;

	base = get_dma_regs(dmanr);
	c = ioread32(base+DMAC_COUNT_REG);

	c &= ~DMAC_CNTN_REG_PW_MASK;

	switch (width) {
	case IMG_DMA_WIDTH_32:
		c |= DMAC_CNTN_REG_PW_32;
		count /= 4;
		break;

	case IMG_DMA_WIDTH_16:
		c |= DMAC_CNTN_REG_PW_16;
		count /= 2;
		break;

	case IMG_DMA_WIDTH_8:
		c |= DMAC_CNTN_REG_PW_8;
		break;

	default:
		BUG();
		break;
	}

	c |= count;
	iowrite32(c, base+DMAC_COUNT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_length);

int img_dma_set_io_address(int dmanr, u32 address,
				unsigned int burst_size)
{
	u32 *base;
	u32 addr, periph;

	base = get_dma_regs(dmanr);

	periph = ioread32(base + DMAC_PERIPH_REG);
	/* We set all the non-list mode bits here. */
	addr = (address & DMAC_PHADDRN_REG_ADDR_BITS) >> 2;
	addr |= ((burst_size << DMAC_PHADDRN_REG_BURST_S)
		 & DMAC_PHADDRN_REG_BURST_BITS);

	periph &= ~(DMAC_PHADDRN_REG_ADDR_BITS|DMAC_PHADDRN_REG_BURST_BITS);
	periph |= addr;

	iowrite32(periph, base + DMAC_PERIPH_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_io_address);

int img_dma_set_direction(int dmanr, enum img_dma_direction direction)
{
	u32 *base;
	u32 c;

	base = get_dma_regs(dmanr);
	c = ioread32(base+DMAC_COUNT_REG);

	switch (direction) {
	case DMA_FROM_DEVICE:
		c |= DMAC_CNTN_REG_DIR_BIT;
		break;

	case DMA_TO_DEVICE:
		c &= ~DMAC_CNTN_REG_DIR_BIT;
		break;

	default:
		BUG();
		break;
	}

	iowrite32(c, base+DMAC_COUNT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_direction);

int img_dma_start(int dmanr)
{
	u32 *base;
	u32 c;
	u32 s;

	base = get_dma_regs(dmanr);

	/* Clear down the done bit */
	s = ioread32(base+DMAC_IRQSTAT_REG);
	s &= ~DMAC_IRQSTATN_REG_FIN_BIT;
	iowrite32(s, base+DMAC_IRQSTAT_REG);

	c = ioread32(base+DMAC_COUNT_REG);

	c &= ~DMAC_CNTN_REG_SRST_BIT;
	iowrite32(c, base+DMAC_COUNT_REG);

	c |= DMAC_CNTN_REG_EN_BIT;
	iowrite32(c, base+DMAC_COUNT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_start);

int img_dma_is_finished(int dmanr)
{
	u32 *base;
	u32 s;

	base = get_dma_regs(dmanr);

	s = ioread32(base+DMAC_IRQSTAT_REG);
	return s & DMAC_IRQSTATN_REG_FIN_BIT;
}
EXPORT_SYMBOL(img_dma_is_finished);

int img_dma_set_list_addr(int dma_channel, u32 address)
{
	u32 temp;
	u32 *base;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	iowrite32(address, base+DMAC_SETUP_REG);

	temp = ioread32(base+DMAC_COUNT_REG);
	temp |= DMAC_CNTN_REG_LIEN_BIT;
	iowrite32(temp, base+DMAC_COUNT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_list_addr);

int img_dma_start_list(int dma_channel)
{
	u32 temp;
	u32 *base;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	temp = ioread32(base+DMAC_COUNT_REG);
	temp |= DMAC_CNTN_REG_LEN_BIT;
	iowrite32(temp, base+DMAC_COUNT_REG);

	return 0;

}
EXPORT_SYMBOL(img_dma_start_list);

int img_dma_stop_list(int dma_channel)
{
	u32 temp;
	u32 *base;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	temp = ioread32(base+DMAC_COUNT_REG);
	temp &= ~DMAC_CNTN_REG_LEN_BIT;
	iowrite32(temp, base+DMAC_COUNT_REG);

	return 0;

}
EXPORT_SYMBOL(img_dma_stop_list);

int img_dma_get_int_status(int dma_channel, u32 *status)
{
	u32 *base;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	*status = ioread32(base + DMAC_IRQSTAT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_get_int_status);

int img_dma_set_int_status(int dma_channel, u32 status)
{

	u32 *base;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	iowrite32(status, base + DMAC_IRQSTAT_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_int_status);

int img_dma_has_request(int dma_channel)
{
	u32 *base;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	return !!(ioread32(base + DMAC_COUNT_REG) & DMAC_CNTN_REG_DREQ_BIT);
}
EXPORT_SYMBOL(img_dma_has_request);

/**
 * img_dma_set_access_delay() - sets the access delay
 * @dma_channel:	DMA channel number
 * @delay:		Delay in range 0-7, where actual delay = @delay * 256
 *			cycles
 *
 */
int img_dma_set_access_delay(int dma_channel, u8 delay)
{
	u32 *base;
	u32 paddr;

	if (dma_channel >= MAX_DMA_CHANNELS)
		return -EINVAL;
	if (delay > 7)
		return -EINVAL;

	base = get_dma_regs(dma_channel);

	paddr = ioread32(base + DMAC_PERIPH_REG);
	paddr |= delay << DMAC_PHADDRN_REG_ACCDEL_S;
	iowrite32(paddr, base + DMAC_PERIPH_REG);

	return 0;
}
EXPORT_SYMBOL(img_dma_set_access_delay);
