/*
 * IMG LCD controller driver.
 *
 * Copyright (C) 2006, 2007, 2008, 2012 Imagination Technologies Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#ifndef __IMG_LCD_H
#define __IMG_LCD_H

#include <uapi/linux/img_lcd.h>

struct img_lcd_board {
	/* methods for handling CS line */
	void (*enable_cs)(void);
	void (*disable_cs)(void);
};
#endif
