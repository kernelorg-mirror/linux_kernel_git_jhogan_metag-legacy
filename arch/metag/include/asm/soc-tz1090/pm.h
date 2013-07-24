/*
 * pdc.h
 * TZ1090 Powerdown Controller (PDC)
 *
 * Copyright 2012 Imagination Technologies Ltd.
 *
 */

#ifndef _TZ1090_PM_H_
#define _TZ1090_PM_H_

/* Set these hooks to call board specific code to restart/halt/power off. */
extern void (*board_restart)(char *cmd);
extern void (*board_halt)(void);
extern void (*board_power_off)(void);

#ifdef CONFIG_PM_SLEEP
#include <linux/suspend.h>

/*
 * Set these hooks to call board specific code to suspend/resume, such as
 * turning off power rails. These are called close to the actual suspend.
 */
extern int (*board_suspend)(suspend_state_t state);
extern void (*board_resume)(suspend_state_t state);
#endif

/* soc/tz1090/setup.c */
void comet_prepare_reset(void);

/* soc/tz1090/pm.c */
void comet_pdc_restart(void);
int comet_pdc_power_off(void);

#endif /* _TZ1090_PM_H_ */
