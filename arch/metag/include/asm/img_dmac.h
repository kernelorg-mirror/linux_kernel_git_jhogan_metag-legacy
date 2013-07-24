/*
 * IMG DMA Controller (DMAC) specific DMA code.
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
*/

#ifndef IMG_DMAC_H_
#define IMG_DMAC_H_

#define DMAC_SETUPN_REG      0x000
#define DMAC_CNTN_REG        0x004
#define DMAC_PHADDRN_REG     0x008
#define DMAC_IRQSTATN_REG    0x00C
#define DMAC_2DMODEN_REG     0x010

#define DMAC_SETUP_REG       (DMAC_SETUPN_REG >> 2)
#define DMAC_COUNT_REG       (DMAC_CNTN_REG >> 2)
#define DMAC_PERIPH_REG      (DMAC_PHADDRN_REG >> 2)
#define DMAC_IRQSTAT_REG     (DMAC_IRQSTATN_REG >> 2)
#define DMAC_2DMODE_REG      (DMAC_2DMODEN_REG >> 2)

#define DMAC_CNTN_REG_PW_S      27
#define DMAC_CNTN_REG_PW_MASK   (0x3 << DMAC_CNTN_REG_PW_S)
#define DMAC_CNTN_REG_PW_8      (2 << DMAC_CNTN_REG_PW_S)
#define DMAC_CNTN_REG_PW_16     (1 << DMAC_CNTN_REG_PW_S)
#define DMAC_CNTN_REG_PW_32              0
#define DMAC_CNTN_REG_LIEN_BIT  0x80000000
#define DMAC_CNTN_REG_IEN_BIT   0x20000000
#define DMAC_CNTN_REG_DIR_BIT   0x04000000
#define DMAC_CNTN_REG_DREQ_BIT	0x00100000
#define DMAC_CNTN_REG_SRST_BIT  0x00080000
#define DMAC_CNTN_REG_LEN_BIT	0x00040000
#define DMAC_CNTN_REG_EN_BIT    0x00010000
#define DMAC_CNTN_REG_CNT_BITS  0x0000FFFF

#define DMAC_PHADDRN_REG_ADDR_BITS   0x007FFFFF
#define DMAC_PHADDRN_REG_BURST_BITS  0x07000000
#define DMAC_PHADDRN_REG_BURST_S     24
#define DMAC_PHADDRN_REG_ACCDEL_BITS 0xE0000000
#define DMAC_PHADDRN_REG_ACCDEL_S    29

/* The rest of the DMAC internal states should be moved in here as other
 * drivers (SPI for instance) stop accessing the registers directly and
 * use the DMAC API.
 */
#define DMAC_IRQSTATN_REG_FIN_BIT 0x20000

/*List Support*/

struct img_dmac_desc {
	volatile u32 perip_setup;
	volatile u32 len_ints;
	volatile u32 perip_address;
	volatile u32 burst;
	volatile u32 twod;
	volatile u32 twod_addr;
	volatile u32 data_addr;
	volatile u32 next;
};

#define DMAC_LIST_PW_S	 28
#define DMAC_LIST_PW_8	 (2 << DMAC_LIST_PW_S)
#define DMAC_LIST_PW_16  (1 << DMAC_LIST_PW_S)
#define DMAC_LIST_PW_32  (0 << DMAC_LIST_PW_S)
#define DMAC_LIST_PERIP_TO_MEM	(1 << 30)

#define DMAC_LIST_FIN_BIT 	(1 << 31)
#define DMAC_LIST_INT_BIT	(1 << 30)
#define DMAC_LIST_INCR_BIT	(1 << 16)
#define DMAC_LIST_LEN_MASK	0xFFFF

#define DMAC_LIST_ACC_DELAY_S	29
#define DMAC_LIST_BURST_S	26

#endif /* IMG_DMAC_H_ */
