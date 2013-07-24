/*
 * TZ1090 Aux-DAC based Backlight Driver
 *
 * Copyright (C) 2012 Imagination Technologies
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _TZ1090_AUXDAC_BL_H_
#define _TZ1090_AUXDAC_BL_H_

struct tz1090_auxdac_bl_pdata;
struct fb_info;

struct tz1090_auxdac_bl_pdata {
	int	(*match_fb)(struct tz1090_auxdac_bl_pdata *, struct fb_info *);
	void	(*set_bl_power)(int power); /* FB_BLANK_* */

	const char *name;
	int default_intensity;
};

#endif /* _TZ1090_AUXDAC_BL_H_ */
