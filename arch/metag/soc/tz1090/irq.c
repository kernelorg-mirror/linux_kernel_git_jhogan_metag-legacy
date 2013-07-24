/*
 * Comet specific interrupt code.
 *
 * Copyright (C) 2009-2012 Imagination Technologies Ltd.
 *
 */

#include <linux/irqchip/metag-ext.h>

#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/setup.h>

void __init comet_init_irq(void)
{
	/* Evaluation silicon has no mask registers */
	if (comet_is_evaluation_silicon())
		meta_intc_no_mask();
}
