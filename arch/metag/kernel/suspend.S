! Copyright (C) 2012 Imagination Technologies Ltd.
!
! Generic Metag functions to help with suspend to RAM.

#include <linux/linkage.h>

!================ SUSPEND TO RAM ================!

! int metag_suspend_setjmp(struct metag_suspend_jmpbuf *jmp)
ENTRY(_metag_suspend_setjmp)
	! D1Ar1 is buffer
	ADD	A0.2, D1Ar1, #16
	MSETL	[D1Ar1++], A0StP, A0FrP
	MSETL	[A0.2++], D0.5, D0.6, D0.7
	SETD	[A0.2++], D1RtP
	SETD	[A0.2++], PCX
	SETD	[A0.2++], TXTIMER
	SETD	[A0.2++], TXTIMERI
	MOV	D0Re0, #0
	MOV	PC, D1RtP
ENDPROC(_metag_suspend_setjmp)

! void metag_resume_longjmp(struct metag_suspend_jmpbuf *jmp, int ret)
ENTRY(_metag_resume_longjmp)
	! D1Ar1 is buffer
	! D0Ar2 is return value

	ADD	A0.2, D1Ar1, #16
	MGETL	A0StP, A0FrP, [D1Ar1++]
	MGETL	D0.5, D0.6, D0.7, [A0.2++]
	GETD	D1RtP, [A0.2++]
	GETD	PCX, [A0.2++]
	GETD	TXTIMER, [A0.2++]
	GETD	TXTIMERI, [A0.2++]

	MOV	D0Re0, D0Ar2
	MOV	PC, D1RtP
ENDPROC(_metag_resume_longjmp)

