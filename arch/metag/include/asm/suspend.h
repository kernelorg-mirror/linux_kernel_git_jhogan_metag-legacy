/*
 * suspend.h
 * Generic Metag suspend functions defined in kernel/suspend.S
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 */

#ifndef _METAG_SUSPEND_H_
#define _METAG_SUSPEND_H_

#if defined(CONFIG_METAG_SUSPEND_MEM)

#include <linux/kernel.h>

/**
 * struct metag_suspend_jmpbuf - Contains core processor state through suspend.
 * @aregs:	Address unit registers A0StP,A1GbP, and A0FrP,A1LbP.
 * @dregs:	Data unit registers D0.5,D1.5, D0.6,D1.6, and D0.7,D1.7.
 * @d1rtp:	Data unit register D1RtP
 * @pcx:	PC unit register PCX.
 * @txtimer:	Control unit register TXTIMER.
 * @txtimeri:	Control unit register TXTIMERI.
 *
 * This structure stores core processor state that needs to be restored after
 * suspend, and is analagous to jmp_buf in the standard C library.
 */
struct metag_suspend_jmpbuf {
	u64 aregs[2];
	u64 dregs[3];
	u32 d1rtp;
	u32 pcx;
	u32 txtimer;
	u32 txtimeri;
};

/**
 * metag_suspend_setjmp() - Store core processor state.
 * @jmp:	jump buffer to store state to.
 *
 * This stores core processor state to a jump buffer, where it can later be
 * restored with metag_resume_longjmp(), and is analagous to setjmp() in the
 * standard C library.
 */
int metag_suspend_setjmp(struct metag_suspend_jmpbuf *jmp);

/**
 * metag_resume_longjmp() - Restore core processor state.
 * @jmp:	jump buffer containing state.
 * @ret:	return value to return from metag_suspend_setjmp().
 *
 * This restores core processor state from a jump buffer, where the jump buffer
 * had originally been set by metag_suspend_setjmp(), and is analagous to
 * longjmp() in the standard C library.
 */
void __noreturn metag_resume_longjmp(struct metag_suspend_jmpbuf *jmp, int ret);

#endif /* CONFIG_METAG_SUSPEND_MEM */

#endif /* _METAG_SUSPEND_H_ */
