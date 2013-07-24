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
#ifndef _UAPI__IMG_LCD_H
#define _UAPI__IMG_LCD_H

/* Our IOCTL family group */
#define ALPHA_IOCTL	'A'

/* Write an instruction byte */
#define ALPHA_INSTR _IOW(ALPHA_IOCTL, 1, char)

/* Write a data byte */
#define ALPHA_DATA _IOW(ALPHA_IOCTL, 2, char)

struct alpha_width_struct {
	unsigned char width:4;		/* Transmit width of 1:4:8 bits */
	unsigned char msb:1;		/* msb or lsb first (or nibble) */
};

/* Set the interface width (use the ENUMs!) */
#define ALPHA_WIDTH _IOW(ALPHA_IOCTL, 3, struct alpha_width_struct *)

struct alpha_speed_struct {
	unsigned short d_period;
	unsigned short p_h_width:4;
	unsigned short p_h_delay:4;
	unsigned short t_div:2;
};

/* Set the interface speed for data or command phases */
#define ALPHA_DATA_SPEED _IOW(ALPHA_IOCTL, 4, struct alpha_speed_struct *)
#define ALPHA_COMMAND_SPEED _IOW(ALPHA_IOCTL, 5, struct alpha_speed_struct *)

struct alpha_data_block {
	unsigned len;
	char *p;
};

#define ALPHA_DATA_BLOCK _IOW(ALPHA_IOCTL, 6, struct alpha_data_block *)
#define ALPHA_COMMAND_BLOCK _IOW(ALPHA_IOCTL, 7, struct alpha_data_block *)


#endif /* _UAPI__IMG_LCD_H */
