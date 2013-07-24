/*
 * board/polaris/gpio.h
 * Polaris GPIO aliases
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 */

#ifndef _POLARIS_GPIO_H_
#define _POLARIS_GPIO_H_

#include <asm/soc-tz1090/gpio.h>

/* EN_TFT_BL controls power to EN_VBUS and power to the backlight booster */
#define EN_TFT_BL	GPIO_TFT_VD12ACB
#define EN_VBUS		GPIO_SDH_CD
#define KILL_1V8	GPIO_I2S_DOUT2
#define KILL_3V3_MAIN	GPIO_UART0_CTS
#define LATCH_3V3_MAIN	GPIO_PDC_GPIO1

#endif /* _POLARIS_GPIO_H_ */
