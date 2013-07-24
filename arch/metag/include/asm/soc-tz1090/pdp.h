/*
 * pdp.h
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 */

#ifndef _TZ1090_PDP_H_
#define _TZ1090_PDP_H_

#include <linux/init.h>

struct fb_videomode;
struct pdp_lcd_size_cfg;
struct imgpdi_lcd_pdata;
struct pdp_sync_cfg;
struct pdp_hwops;

void comet_pdp_set_shared_base(unsigned long pa);

extern void __init comet_pdp_set_limits(unsigned long min, unsigned long max);
extern int __init comet_pdp_setup(const struct fb_videomode *fbvm,
		struct pdp_lcd_size_cfg *pslc, struct imgpdi_lcd_pdata *pdic,
		struct pdp_sync_cfg *psc, struct pdp_hwops *hwops);

#endif
