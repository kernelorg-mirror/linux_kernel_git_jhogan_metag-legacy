/*
 * suspend.h
 * Comet suspend functions defined in suspend.S
 *
 * Copyright 2010 Imagination Technologies Ltd.
 *
 */

#ifndef _METAG_TZ1090_SUSPEND_H_
#define _METAG_TZ1090_SUSPEND_H_

#ifdef CONFIG_SUSPEND

/**
 * metag_standby() - Wait for a wake interrupt.
 * @txmask:	TXMASK wake trigger bits.
 *
 * This is the basic version that doesn't make any effort to reduce power
 * consumption while in standby.
 */
extern void metag_standby(unsigned int txmask);

/* Size in bytes of the metag_standby() code. */
extern unsigned int metag_standby_sz;

/**
 * metag_comet_standby() - Comet power saving version of metag_standby().
 * @txmask:	TXMASK wake trigger bits.
 *
 * This adds comet specific power saving techniques on top of metag_standby()
 * such as putting DDR into self refresh so the DDRC can be powered down, and
 * clocking the Meta core down.
 */
extern void metag_comet_standby(unsigned int txmask);

/* Size in bytes of the metag_comet_standby() code. */
extern unsigned int metag_comet_standby_sz;

/**
 * metag_comet_suspend() - SoC power down suspend to RAM.
 * @txmask:	TXMASK wake trigger bits used when faking suspend to RAM.
 *
 * This switches as much of the SoC off as possible, including the Meta core,
 * and puts DDR RAM into self refresh.
 *
 * If SAFE mode is on then suspend will be faked by waiting for the wake trigger
 * and performing a watchdog soft-reset.
 */
extern void metag_comet_suspend(unsigned int txmask);

/* Size in bytes of the metag_comet_suspend() code. */
extern unsigned int metag_comet_suspend_sz;

#endif

#endif
