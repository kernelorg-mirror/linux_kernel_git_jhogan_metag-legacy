/*
 * Copyright (C) 2009,2010 Imagination Technologies Limited.
 *
 * Quantum TouchScreen Controller driver.
 */

#ifndef TS_QT5480_H
#define TS_QT5480_H

#include <linux/types.h>

/* touchscreen 0 detect bit in general_status_2 */
#define TS0_DET			0x01
/* touchscreen 1 detect bit in general_status_2 */
#define TS1_DET			0x02

/* bitfield flags indicating that new status has been received */
#define KEY_0_UPDATE		0x01
#define KEY_4_UPDATE		0x02
#define TOUCH_0_UPDATE		0x04
#define TOUCH_1_UPDATE		0x08
#define SLIDER_4_UPDATE		0x10
#define GESTURE_0_UPDATE	0x20
#define GESTURE_1_UPDATE	0x40

/* standard touch-screen event */
typedef struct {
	unsigned short pressure;
	unsigned short x;
	unsigned short y;
} ts_event;

#endif
