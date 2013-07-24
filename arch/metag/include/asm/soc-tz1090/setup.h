/*
 *  arch/metag/include/asm/soc-tz1090/setup.h
 *
 *  Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOC_TZ1090_SETUP_H__
#define __SOC_TZ1090_SETUP_H__

bool comet_is_evaluation_silicon(void);

void comet_init_early(void);
void comet_init_irq(void);
void comet_init_machine(void);

#define TZ1090_MACHINE_DEFAULTS			\
	.init_early	= comet_init_early,	\
	.init_irq	= comet_init_irq,	\
	.init_machine	= comet_init_machine

#endif /* __SOC_TZ1090_SETUP_H__ */
