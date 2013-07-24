/*
 * dma.h
 *
 * Copyright (C) 2007,2008 Imagination Technologies Ltd.
 */

#ifndef _CHORUS2_DMA_H_
#define _CHORUS2_DMA_H_



#define SYSBUS_HWBASE           0x02000000
#define DMAC_HWBASE             0x02001000

#define DMA_CHAN_SEL_0_3        (SYSBUS_HWBASE+0x048)
#define DMA_CHAN_SEL_4_7        (SYSBUS_HWBASE+0x04C)
#define DMA_CHAN_SEL_8_11       (SYSBUS_HWBASE+0x050)

#define MAX_DMA_CHANNELS        12
#define DMAC_CHANNEL_STRIDE     0x20

#define MAX_PERIPH_CHANNELS     63
#define DMAC_PERIPH_MASK        0x3f

unsigned int *get_dma_regs(int dmanr);


#endif /* DMA_H_ */
