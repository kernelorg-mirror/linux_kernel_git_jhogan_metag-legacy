/*
 * Chorus2 specific interrupt code.
 *
 * Copyright (C) 2005-2012 Imagination Technologies Ltd.
 *
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/metag-ext.h>

#include <asm/global_lock.h>
#include <asm/irq.h>
#include <asm/soc-chorus2/gpio.h>

/*
 * The Chorus 2 SoC has three interrupt status and three interrupt
 * enable registers,
 *
 * sys_interrupt_status1, sys_interrupt_status2, sys_interrupt_status3,
 * sys_interrupt_enable1, sys_interrupt_enable2, sys_interrupt_enable3.
 *
 * Each register is 4 bytes in size and each type of register (status
 * or enable) is at consecutive addresses.
 */

/*
 * The Chorus2 SOC IRQ numbers start at zero, but when they were connected to
 * the Meta Core Vector block they were wired at an offset of 4, irq numbering
 * is based on the Meta Core numbers.
 */
#define SOC_IRQ_OFFSET 4

/*
 * The Chorus 2 SoC has a sys_interrupt_enable register that allows
 * individual interrupt lines to be masked.
 */
static void __iomem *sys_interrupt_enable_addr = (void __iomem *)0x02000070;

/**
 *	chorus2_mask_irq - mask SoC irq @irq
 *	@irq_data:	the data for the IRQ to mask
 *
 *	Mask the IRQ at the Chorus 2-specific sys_interrupt_enable
 *	regiser.
 */
void chorus2_mask_irq(struct irq_data *irq_data)
{
	unsigned int reg, bit, enable;
	unsigned int soc_irq;
	void __iomem *addr;
	int state;

	meta_intc_mask_irq_simple(irq_data);

	/* Scale the irq into the SoC range. */
	soc_irq = irq_data->hwirq - SOC_IRQ_OFFSET;

	reg = (soc_irq >> 5) & 0x3;
	bit = (soc_irq & 0x1f);

	addr = sys_interrupt_enable_addr + (reg << 2);

	__global_lock2(state);
	enable = readl(addr);
	writel(enable & ~(1 << bit), addr);
	__global_unlock2(state);
}

/**
 *	chorus2_unmask_irq - unmask a SoC irq
 *	@irq_data:	the data for the IRQ to unmask
 */
void chorus2_unmask_irq(struct irq_data *irq_data)
{
	unsigned int reg, bit, enable;
	unsigned int soc_irq;
	void __iomem *addr;
	int state;

	meta_intc_unmask_irq_simple(irq_data);

	/* Scale the irq into the SoC range. */
	soc_irq = irq_data->hwirq - SOC_IRQ_OFFSET;

	reg = (soc_irq >> 5) & 0x3;
	bit = (soc_irq & 0x1f);

	addr = sys_interrupt_enable_addr + (reg << 2);

	__global_lock2(state);
	enable = readl(addr);
	writel(enable | (1 << bit), addr);
	__global_unlock2(state);
}

void __init chorus2_init_irq(void)
{
	/* override default masking IRQ callbacks to make use of SoC masks */
	meta_intc_edge_chip.irq_mask = chorus2_mask_irq;
	meta_intc_level_chip.irq_mask = chorus2_mask_irq;
	meta_intc_edge_chip.irq_unmask = chorus2_unmask_irq;
	meta_intc_level_chip.irq_unmask = chorus2_unmask_irq;

	chorus2_init_gpio();
}
