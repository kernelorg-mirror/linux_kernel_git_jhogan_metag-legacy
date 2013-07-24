/*
 *  dwc_otg_tz1090.h
 *
 *  @file
 *
 *  This file contains the Platform Specific constants, interfaces
 * (functions and macros) for Comet SoC.
 *
 *  Copyright (C) 2010 Imagination Technologies Ltd.
 *
 */

#if !defined(__DWC_OTG_TZ1090_H__)
#define __DWC_OTG_TZ1090_H__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/io.h>

#if !defined(CONFIG_SOC_TZ1090)
#error "The contents of this file are Comet processor specific!!!"
#endif


/**
 * Reads the content of a register.
 *
 * @param _reg address of register to read.
 * @return contents of the register.
 *
 */
static inline
u32 dwc_read_reg32(u32 __iomem *reg)
{
	return ioread32((void __iomem *)reg);
};

/**
 * Writes a register with a 32 bit value.
 *
 * @param _reg address of register to read.
 * @param _value to write to _reg.
 *
 */
static inline void
dwc_write_reg32(u32 __iomem *reg, const u32 _value)
{
	iowrite32(_value, (void __iomem *)reg);
};

/**
 * This function modifies bit values in a register.  Using the
 * algorithm: (reg_contents & ~clear_mask) | set_mask.
 *
 * @param _reg address of register to read.
 * @param _clear_mask bit mask to be cleared.
 * @param _set_mask bit mask to be set.
 *
 */
static inline void
dwc_modify_reg32(u32 __iomem *reg, const u32 _clear_mask,
		const u32 _set_mask)
{
	iowrite32((ioread32((void __iomem *)reg) & ~_clear_mask) |
			_set_mask , reg);
};

static inline void
dwc_write_datafifo32(u32 __iomem *reg, const u32 _value)
{
	iowrite32(_value, (void __iomem *)reg);
};

static inline u32
dwc_read_datafifo32(u32 __iomem *reg)
{
	return ioread32((void __iomem *)reg);
};

#endif
