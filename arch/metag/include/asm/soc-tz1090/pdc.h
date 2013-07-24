/*
 * pdc.h
 * TZ1090 Powerdown Controller (PDC)
 *
 * Copyright 2010 Imagination Technologies Ltd.
 *
 */

#ifndef _TZ1090_PDC_H_
#define _TZ1090_PDC_H_

/* PDC registers */

#define PDC_WD_SW_RESET			0x000

#define PDC_SOC_SW_PROT			0x020

#define PDC_SOC_POWER			0x300
#define PDC_SOC_DELAY			0x30C

#define PDC_SOC_BOOTSTRAP		0x400
#define PDC_SOC_GPIO_CONTROL0		0x500
#define PDC_SOC_GPIO_CONTROL1		0x504
#define PDC_SOC_GPIO_CONTROL2		0x508
#define PDC_SOC_GPIO_CONTROL3		0x50c
#define PDC_SOC_GPIO_STATUS		0x580

#endif
