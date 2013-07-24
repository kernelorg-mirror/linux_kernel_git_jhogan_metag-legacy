/*
 * arch/metag/include/asm/soc-chorus2/setup.h
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOC_CHORUS2_SETUP_H__
#define __SOC_CHORUS2_SETUP_H__

/* for chorus2_meta_clocks */
#include <asm/soc-chorus2/clock.h>

void chorus2_init_irq(void);
void chorus2_init_machine(void);

#define CHORUS2_MACHINE_DEFAULTS		\
	.clocks		= &chorus2_meta_clocks,	\
	.init_irq	= chorus2_init_irq,	\
	.init_machine	= chorus2_init_machine

#endif /* __SOC_CHORUS2_SETUP_H__ */
