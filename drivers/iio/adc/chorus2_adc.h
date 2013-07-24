/*
 * c2_adc.h
 * Chorus 2 ADC/SCP driver register locations
 *
 * Copyright (C) 2010,2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef C2_ADC_H_
#define C2_ADC_H_

#define SCP_BASE		0x02005000
#define SCP_DMA_IN_PERIPH	5
#define SCP_DMA_OUT_PERIPH	6

/* ADC Clock select */
#define ADC_CLK_SEL_ADDR	0x02000034
#define ADC_CLK_8192M		0
#define ADC_CLK_12288M		1
#define ADC_CLK_24576M		2

/* Digital ADC clock enable (for SCP) */
#define DCXO_CLK_ENABLE	0x020000B4
#define DCXO_CLK_ADC_DISABLE	0
#define DCXO_CLK_ADC_ENABLE	1

/* SCP input if_clk */
#define SCP_IF_PIN_CTRL		0x02024018
#define USE_ON_CHIP_IF_CLK	0x02

/* SCP Control register */
#define SCP_CONTROL_OFFSET	0x00
#define SCP_CTRL_BYPASS		0x20000000
#define SCP_CTRL_DMA_SYNC_EN	0x02000000
#define SCP_CTRL_DMA_SYNC_BP	0x01000000
#define SCP_CTRL_PWR_ON		0x00300000
#define SCP_CTRL_PWR_OFF	0x00400000
#define SCP_CTRL_RESETN		0x00010000
/* For testing only */
#define SCP_CTRL_SRC_INPUT	0x00000400

/* Status register */
#define SCP_STATUS_OFFSET	0x04
#define SCP_STAT_IRQ_SCP	0x80000000
#define SCP_STAT_FRAME_A	0x00000000
#define SCP_STAT_FRAME_B	0x40000000
#define SCP_STAT_IRQ_DMA_OP	0x20000000

/* SCP Output sample */
#define SCP_OUTPUT_ADDR		(SCP_BASE + SCP_OUTPUT_OFFSET)
#define SCP_OUTPUT_OFFSET	0x14

/* DMA output interrupt control */
#define SCP_DMA_OP_INT_OFFSET	0x18

#define scp_write(val, reg) \
	writel((val), SCP_BASE + SCP_##reg##_OFFSET)
#define scp_read(reg) \
	readl(SCP_BASE + SCP_##reg##_OFFSET)

#endif /* C2_ADC_H_ */
