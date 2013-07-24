/*
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 * DDR chip specific definitions for Micron DDR2 512Mbit SDRAMs:
 * MT47H128M4	- 32Mbit*4*4
 * MT47H64M8	- 16Mbit*8*4
 * MT47H32M16	- 8Mbit*16*4
 */

#ifndef _TZ1090_DDR_MT47H_H_
#define _TZ1090_DDR_MT47H_H_

/* Extended mode register */

/* On-Die Terminal (ODT) Effective Resistance */
#define DDR_EMR_ODT		0x0044
#define DDR_EMR_ODT_DISABLED	0x0000
#define DDR_EMR_ODT_75_OHM	0x0004
#define DDR_EMR_ODT_150_OHM	0x0040
#define DDR_EMR_ODT_50_OHM	0x0044

#endif
