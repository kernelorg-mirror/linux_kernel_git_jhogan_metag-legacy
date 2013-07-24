/*
 * clock.h
 *
 * Copyright (C) 2009 Imagination Technologies Ltd.
 *
 */

#ifndef _TZ1090_CLOCK_H_
#define _TZ1090_CLOCK_H_

/*
 * Secondary clock input - used if we cant synthesise all frequencies from 1
 * clock
 */
#define COMET_XTAL2	12000000

/* Tertiary clock input - used for real time clock */
#define COMET_XTAL3	32768

unsigned long get_xtal1(void);
unsigned long get_xtal2(void);
unsigned long get_xtal3(void);
unsigned long get_32kclock(void);
unsigned long set_32kclock_src(int xtal1, unsigned int xtal1_div);
unsigned long get_sysclock_x2_undeleted(void);
unsigned long get_sysclock_undeleted(void);
unsigned long get_sdhostclock(void);
unsigned long set_sdhostclock(unsigned long f);
unsigned long get_uartclock(void);
unsigned long set_uartclock(unsigned long f);
unsigned long get_ddrclock(void);

void pix_clk_set_limits(unsigned long min, unsigned long max);

/*
 * The 32k clock should be at 32.768KHz, and is used by the PDC (RTC, IRC, WDT).
 * This can change unexpectedly when powering down due to a hardware quirk, so
 * drivers that use it need to be able to handle frequency changes.
 */

#define CLK32K_DESIRED_FREQUENCY	32768

extern unsigned long clk32k_bootfreq;

#define CLK32K_CHANGE_FREQUENCY 0x0001

struct clk32k_change_freq {
	unsigned long old_freq;
	unsigned long new_freq;
};

struct notifier_block;

int clk32k_register_notify(struct notifier_block *nb);
int clk32k_unregister_notify(struct notifier_block *nb);

#endif /* _TZ1090_CLOCK_H_ */
