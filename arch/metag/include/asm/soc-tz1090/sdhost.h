/*
 * sdhost.h
 *
 * Copyright (C) 2011 Imagination Technologies Ltd.
 *
 */

#ifndef _TZ1090_SDHOST_H_
#define _TZ1090_SDHOST_H_

#include <linux/init.h>
#include <linux/interrupt.h>

extern int mci_get_ocr(u32 slot_id);
extern int mci_get_bus_wd(u32 slot_id);
extern int mci_init(u32 slot_id, irq_handler_t irqhdlr, void *data);
extern int __init comet_sdhost_init(void);

extern struct dw_mci_board comet_mci_platform_data;
extern struct block_settings blk_settings;
extern struct dma_pdata dma_pdata;

#ifndef CONFIG_MMC_DW_IDMAC
extern struct dw_mci_dma_ops comet_dma_ops;
#endif

#endif
