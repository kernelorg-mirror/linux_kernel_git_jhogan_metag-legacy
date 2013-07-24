/*
 * Imagination Technologies Panel Display Interface (PDI).
 *
 * Copyright (C) 2012 Imagination Technologies
 *
 * Based on platform_lcd.h:
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _IMGPDI_LCD_H_
#define _IMGPDI_LCD_H_

struct imgpdi_lcd_pdata;
struct fb_info;

struct imgpdi_lcd_timings {
	unsigned int pwrsvgd;	/* HSYNC to PWRSV,GD */
	unsigned int ls;	/* HSYNC to LS (> HAS) */
	unsigned int pwrsvgd2;	/* LS to end of PWRSV,GD */
	unsigned int nl;	/* HSYNC to NL */
	unsigned int acb;	/* NL to end of ACB */

	unsigned int gatedriver_en:1;
	unsigned int newframe_en:1;
	unsigned int blanking_en:1;
	unsigned int blanking_level:1;
};

struct imgpdi_lcd_pdata {
	int	(*match_fb)(struct imgpdi_lcd_pdata *, struct fb_info *);

	/* active mode timings (NULL for bypass) */
	struct imgpdi_lcd_timings *active;
};

#endif /* _IMGPDI_LCD_H_ */
