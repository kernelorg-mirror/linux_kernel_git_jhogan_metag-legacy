/*
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 * DDR chip specific definitions
 */

#ifndef _METAG_COMET_DDR_INC_
#define _METAG_COMET_DDR_INC_

#ifdef __ASSEMBLY__

/* We may need register addresses */
#include <asm/soc-tz1090/defs.h>

/*
 * Board specific macros used by comet suspend code to control the DDR.
 *
 * COMET_DDR_PREPARE_POWERDOWN()
 * 	This is used before putting DDR into self refresh mode.
 * COMET_DDR_FINISH_POWERDOWN()
 * 	This is used after taking DDR out of self refresh mode.
 *
 * Macros may use D0Ar6, D0Ar4 registers.
 */

/*
 * Some boards don't have pull down on the clock line, so the DDR will come out
 * of self refresh when the controller is powered down. The termination voltage
 * of the DDR pads needs to be disabled to keep the clock from floating high.
 */
#if defined(CONFIG_COMET_BUB) || defined(CONFIG_POLARIS)

#ifdef CONFIG_SOC_COMET_ES1

/*
 * ES1 doesn't allow the terminations of the DDR pads to be set independently
 * of the terminations of the DDR part. We can turn them off by using the
 * extended mode register (EMR).
 */

/* 512Mbit MT47H32M16 */
#include <asm/soc-tz1090/ddr-mt47h.inc>

/* CR_DDR_EMR.DDR_EMR_ODT = DDR_EMR_ODT_DISABLED */
#define COMET_DDR_PREPARE_POWERDOWN() \
	MOVT	D0FrT, #HI(CR_DDRC_EMR); \
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_EMR); \
	GETD	D0Ar6, [D0FrT]; \
	AND	D0Ar6, D0Ar6, #~DDR_EMR_ODT; \
	/* OR	D0Ar6, D0Ar6, #DDR_EMR_ODT_DISABLED; */ \
	SETD	[D0FrT], D0Ar6;

/* CR_DDR_EMR.DDR_EMR_ODT = DDR_EMR_ODT_75_OHM */
#define COMET_DDR_FINISH_POWERDOWN() \
	MOVT	D0FrT, #HI(CR_DDRC_EMR); \
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_EMR); \
	GETD	D0Ar6, [D0FrT]; \
	AND	D0Ar6, D0Ar6, #~DDR_EMR_ODT; \
	OR	D0Ar6, D0Ar6, #DDR_EMR_ODT_75_OHM; \
	SETD	[D0FrT], D0Ar6;

#else	/* CONFIG_SOC_COMET_ES1 */

/*
 * PS1 allows the terminations of the DDR pads to be set independently of the
 * DDR part, using the CR_DDRC_PADS register.
 */

/* CR_DDR_PADS.CR_DDRC_PADS_ODT = CR_DDRC_PADS_ODT_DISABLED */
#define COMET_DDR_PREPARE_POWERDOWN() \
	MOVT	D0FrT, #HI(CR_DDRC_PADS); \
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_PADS); \
	GETD	D0Ar6, [D0FrT]; \
	AND	D0Ar6, D0Ar6, #~CR_DDRC_PADS_ODT; \
	/* OR	D0Ar6, D0Ar6, #CR_DDRC_PADS_ODT_DISABLED; */ \
	SETD	[D0FrT], D0Ar6;

/* CR_DDR_PADS.CR_DDRC_PADS_ODT = CR_DDRC_PADS_ODT_150_OHM */
#define COMET_DDR_FINISH_POWERDOWN() \
	MOVT	D0FrT, #HI(CR_DDRC_PADS); \
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_PADS); \
	GETD	D0Ar6, [D0FrT]; \
	AND	D0Ar6, D0Ar6, #~CR_DDRC_PADS_ODT; \
	OR	D0Ar6, D0Ar6, #CR_DDRC_PADS_ODT_150_OHM; \
	SETD	[D0FrT], D0Ar6;

#endif	/* CONFIG_SOC_COMET_ES1 */

#endif	/* CONFIG_COMET_BUB */

/* Have some sensible defaults for other boards */

#ifndef COMET_DDR_PREPARE_POWERDOWN
#define COMET_DDR_PREPARE_POWERDOWN()
#endif

#ifndef COMET_DDR_FINISH_POWERDOWN
#define COMET_DDR_FINISH_POWERDOWN()
#endif

#endif	/* __ASSEMBLY__ */

#endif	/* _METAG_COMET_DDR_INC_ */
