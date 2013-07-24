/*
 *  tansen_gti.c - GTI port support for the Tansen Codec.
 *
 *  Copyright (C) 2010-2013 Imagination Technologies
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <sound/tansen.h>
#include "tansen_gti.h"

__u32 gti_read(void __iomem *port, unsigned long reg)
{
	int i;
	unsigned long data_in = 0, data_out = 0, ctrl, bit, ret;

	/* Construct the data input to the port */
	data_in |= ((reg & GTI_CTRL_REG_ADDR_MASK) << GTI_CTRL_REG_ADDR_SHIFT);
	data_in |= ((0 & GTI_CTRL_DATA_MASK) << GTI_CTRL_DATA_SHIFT);
	data_in |= ((GTI_CTRL_OP_READ & GTI_CTRL_OP_MASK) << GTI_CTRL_OP_SHIFT);

	/* Ensure GTI port is in the right state */
	ctrl = GTI_MAKE_CTRL_REG(0, GTI_CLK_LO, GTI_NOT_IN_RESET,
				 GTI_NOT_TEST_MODE);
	iowrite32(ctrl, port);
	udelay(1);

	/* Clock out & in the bits, starting with the MSB */
	for (i = 0; i < 32; i++) {
		bit = (data_in & 0x80000000) >> 31;
		data_in <<= 1;

		ctrl = GTI_MAKE_CTRL_REG(bit, GTI_CLK_HI, GTI_NOT_IN_RESET,
					 GTI_NOT_TEST_MODE);
		iowrite32(ctrl, port);
		udelay(1);
		ctrl = GTI_MAKE_CTRL_REG(bit, GTI_CLK_LO, GTI_NOT_IN_RESET,
					 GTI_NOT_TEST_MODE);
		iowrite32(ctrl, port);
		udelay(1);

		bit = ioread32(port + 4);
		data_out = (data_out << 1) | bit;
	}

	ret = (data_out >> GTI_CTRL_DATA_SHIFT) & GTI_CTRL_DATA_MASK;

	return ret;
}
EXPORT_SYMBOL_GPL(gti_read);

void gti_write(void __iomem *port, unsigned long reg, unsigned long value)
{
	int i;
	unsigned long data_in = 0, ctrl, bit;

	/* Construct the data input to the port */
	data_in |= ((reg & GTI_CTRL_REG_ADDR_MASK) << GTI_CTRL_REG_ADDR_SHIFT);
	data_in |= ((value & GTI_CTRL_DATA_MASK) << GTI_CTRL_DATA_SHIFT);
	data_in |= ((GTI_CTRL_OP_WRITE & GTI_CTRL_OP_MASK)
		    << GTI_CTRL_OP_SHIFT);

	/* Ensure GTI port is in the right state */
	ctrl = GTI_MAKE_CTRL_REG(0, GTI_CLK_LO, GTI_NOT_IN_RESET,
				 GTI_NOT_TEST_MODE);
	iowrite32(ctrl, port);
	udelay(1);

	/* Clock out & in the bits, starting with the MSB */
	for (i = 0; i < 32; i++) {
		bit = (data_in & 0x80000000) >> 31;
		data_in <<= 1;

		ctrl = GTI_MAKE_CTRL_REG(bit, GTI_CLK_HI, GTI_NOT_IN_RESET,
					 GTI_NOT_TEST_MODE);
		iowrite32(ctrl, port);
		udelay(1);
		ctrl = GTI_MAKE_CTRL_REG(bit, GTI_CLK_LO, GTI_NOT_IN_RESET,
					 GTI_NOT_TEST_MODE);
		iowrite32(ctrl, port);
		udelay(1);
	}
}
EXPORT_SYMBOL_GPL(gti_write);

void gti_reset(void __iomem *port, int reset)
{
	unsigned long ctrl;
	ctrl = GTI_MAKE_CTRL_REG(0, GTI_CLK_LO,
				 reset ? GTI_IN_RESET : GTI_NOT_IN_RESET,
				 GTI_NOT_TEST_MODE);
	iowrite32(ctrl, port);
}
EXPORT_SYMBOL_GPL(gti_reset);
