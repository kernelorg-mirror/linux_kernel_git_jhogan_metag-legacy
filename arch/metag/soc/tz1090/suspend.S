! Copyright 2010 Imagination Technologies Ltd.
!
! Functions for suspending the comet SoC.

#include <linux/linkage.h>
#include <asm/metag_isa.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/pdc.h>
#include "ddr.inc"

	.text

! Pause for approximately 100*time instructions
! This is modelled on ldlk's MPAUSE command
.macro MPAUSE time
	MOVT	D1Ar5, #HI((\time)*25)
	ADD	D1Ar5, D1Ar5, #LO((\time)*25)
1:
	NOP
	SUB	D1Ar5, D1Ar5, #1
	CMP	D1Ar5, #0
	BNZ	1b
.endm


! Wait for a wake interrupt to fire
! The ret output is the trigger status
! The trig argument is the trigger mask
.macro wait_for_wakeup ret trig
	! set TXMASK to wake trigger mask
	MOV	TXMASK, \trig

	! wait for a wakeup trigger to fire
	MOV     \ret, TXSTAT

	! we don't clear the trigger, otherwise it won't get handled on resume.

	! reset TXMASK
	XOR	TXMASK, D0Re0, D0Re0
.endm

! Instruction cache prefetch instruction
.macro INSTR_ICACHE offset pfcount
	.long (0xae000001	| (((\offset) & 0xffff) << 9) \
				| (((\pfcount) & 0xf) << 1))
.endm

! Prefetch between two markers
.macro INSTR_ICACHE_BETWEEN start end
	INSTR_ICACHE ((\start) - .), \
		      (1 + (((\end) - (\start)) >> ICACHE_LINE_S))
.endm

!================ SIMPLE STANDBY ================!

! Wait for a wake interrupt (txmask in first argument).
! This is pretty much SoC agnostic.
ENTRY(_metag_standby)
	wait_for_wakeup D0Re0, D1Ar1

	MOV	PC, D1RtP
ENDPROC(_metag_standby)

! Make the length available so the code can be copied into core memory.
ENTRY(_metag_standby_sz)
        .long   . - _metag_standby


!================ POWER SAVING STANDBY ================!

! Wait for a wake interrupt (txmask in first argument), with a bit more
! power saving techniques.

#define DDR_POWERDOWN		! saves ~105mA on comet BUB
#define META_PLL_BYPASS		! saves ~50mA at 360MHz on comet BUB
#define META_PLL_PWRDN		! saves ~3mA on comet BUB
#define META_CLKDELETE		! saves <1mA on comet BUB

ENTRY(_metag_comet_standby)
	! Prefetch enough cache lines to cover DDR powerdown
	INSTR_ICACHE_BETWEEN ., $Lddr_powered_back_up
	! Wait for prefetch to complete
	MPAUSE 1000

	!---------------- POWER VARIOUS THINGS DOWN ----------------!

	! powerdown the DDR clock and pads
#ifdef DDR_POWERDOWN
	! put DDR into self refresh

	! do board specific preparation
	COMET_DDR_PREPARE_POWERDOWN()

	! CR_DDRC_SELFREF_EN = 1
	MOVT	D0FrT, #HI(CR_DDRC_SELFREF_EN)
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_SELFREF_EN)
	MOV	D0Ar6, #1
	SETD	[D0FrT], D0Ar6

	! wait until DDRC_OPERATING_MODE is set to self refresh
	MOVT	D0FrT, #HI(CR_DDRC_OPERATING_MODE)
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_OPERATING_MODE)
1:
	GETD	D0Ar6, [D0FrT]
	CMP	D0Ar6, #CR_DDRC_OPERATING_MODE_SELFREF
	BNZ	1b

	! CR_DDR_CTRL |= 1 << CR_DDR_POWERDOWN
	MOVT	D0FrT, #HI(CR_DDR_CTRL)
	ADD	D0FrT, D0FrT, #LO(CR_DDR_CTRL)
	GETD	D0Ar6, [D0FrT]
	OR	D0Ar6, D0Ar6, #(1 << CR_DDR_POWERDOWN_BIT)
	SETD	[D0FrT], D0Ar6
#endif

	! bypass PLL to clock the Meta down and power down the PLL
#ifdef META_PLL_BYPASS
	! Switch system clock to XTAL1
	! CR_TOP_CLKSWITCH &= ~(1 << CR_TOP_SYSCLK1_SW_BIT)
	MOVT	D0FrT, #HI(CR_TOP_CLKSWITCH)
	ADD	D0FrT, D0FrT, #LO(CR_TOP_CLKSWITCH)
	GETD	D0Ar6, [D0FrT]
	AND	D0Ar6, D0Ar6, #(~(1 << CR_TOP_SYSCLK1_SW_BIT))
	SETD	[D0FrT], D0Ar6
	! wait for switch over
	MPAUSE 1000

#ifdef META_PLL_PWRDN
	! CR_TOP_SYSPLL_CTL1 |= (1 << CR_TOP_SYSPLL_PWRDN_BIT)
	MOVT	D0FrT, #HI(CR_TOP_SYSPLL_CTL1)
	ADD	D0FrT, D0FrT, #LO(CR_TOP_SYSPLL_CTL1)
	GETD	D0Ar6, [D0FrT]
	MOVT	D0Ar4, #HI(1 << CR_TOP_SYSPLL_PWRDN_BIT)
	OR	D0Ar6, D0Ar6, D0Ar4
	SETD	[D0FrT], D0Ar6
#endif
#endif

	! delete 1023/1024 system clock cycles
#ifdef META_CLKDELETE
	! CR_TOP_META_CLKDELETE = 1023
	MOVT	D0FrT, #HI(CR_TOP_META_CLKDELETE)
	ADD	D0FrT, D0FrT, #LO(CR_TOP_META_CLKDELETE)
	MOV	D0Ar6, #1023
	SETD	[D0FrT], D0Ar6
#endif


	!---------------- STANDBY UNTIL TRIGGER FIRES ----------------!

	wait_for_wakeup D0Re0, D1Ar1

	!---------------- POWER VARIOUS THINGS BACK UP AGAIN ----------------!

	! delete 0/1024 system clock cycles
#ifdef META_CLKDELETE
	! CR_TOP_META_CLKDELETE = 0
	MOVT	D0FrT, #HI(CR_TOP_META_CLKDELETE)
	ADD	D0FrT, D0FrT, #LO(CR_TOP_META_CLKDELETE)
	MOV	D0Ar6, #0
	SETD	[D0FrT], D0Ar6
#endif

#ifdef META_PLL_BYPASS
	! power up the PLL
#ifdef META_PLL_PWRDN
	! CR_TOP_SYSPLL_CTL1 ^= (1 << CR_TOP_SYSPLL_PWRDN_BIT)
	MOVT	D0FrT, #HI(CR_TOP_SYSPLL_CTL1)
	ADD	D0FrT, D0FrT, #LO(CR_TOP_SYSPLL_CTL1)
	GETD	D0Ar6, [D0FrT]
	MOVT	D0Ar4, #HI(1 << CR_TOP_SYSPLL_PWRDN_BIT)
	XOR	D0Ar6, D0Ar6, D0Ar4
	SETD	[D0FrT], D0Ar6
	! wait for PLL to power back up and sort itself out
	MPAUSE 2000
#endif

	! Switch system clock back to PLL
	! CR_TOP_CLKSWITCH |= (1 << CR_TOP_SYSCLK1_SW_BIT)
	MOVT	D0FrT, #HI(CR_TOP_CLKSWITCH)
	ADD	D0FrT, D0FrT, #LO(CR_TOP_CLKSWITCH)
	GETD	D0Ar6, [D0FrT]
	OR	D0Ar6, D0Ar6, #(1 << CR_TOP_SYSCLK1_SW_BIT)
	SETD	[D0FrT], D0Ar6
	! wait for switch back
	MPAUSE 5000
#endif

	! powerup the DDR clock and pads
#ifdef DDR_POWERDOWN
	! CR_DDR_CTRL &= ~(1 << CR_DDR_POWERDOWN_BIT)
	MOVT	D0FrT, #HI(CR_DDR_CTRL)
	ADD	D0FrT, D0FrT, #LO(CR_DDR_CTRL)
	GETD	D0Ar6, [D0FrT]
	AND	D0Ar6, D0Ar6, #(~(1 << CR_DDR_POWERDOWN_BIT))
	SETD	[D0FrT], D0Ar6

	! take DDR out of self refresh
	! CR_DDRC_SELFREF_EN = 0
	MOVT	D0FrT, #HI(CR_DDRC_SELFREF_EN)
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_SELFREF_EN)
	MOV	D0Ar6, #0
	SETD	[D0FrT], D0Ar6

	! undo board specific preparation
	COMET_DDR_FINISH_POWERDOWN()
#endif
$Lddr_powered_back_up:


	!---------------- FINISHED ----------------!

	MOV	PC, D1RtP
ENDPROC(_metag_comet_standby)

! Make the length available so the code can be copied into core memory.
ENTRY(_metag_comet_standby_sz)
        .long   . - _metag_comet_standby

!================ POWER SAVING SUSPEND TO RAM =================================!

#ifdef CONFIG_METAG_SUSPEND_MEM

! Power down the main power island and let the Powerdown Controller power the
! system back up again when it receives a wake interrupt.

ENTRY(_metag_comet_suspend)
	! Prefetch enough cache lines to cover DDR powerdown
	INSTR_ICACHE_BETWEEN ., $Licache_extent
	! Wait for prefetch to complete
	MPAUSE 1000

	!---------------- POWER VARIOUS THINGS DOWN ----------------!

	! powerdown the DDR clock and pads
	! put DDR into self refresh

	! do board specific preparation
	COMET_DDR_PREPARE_POWERDOWN()

	! CR_DDRC_SELFREF_EN = 1
	MOVT	D0FrT, #HI(CR_DDRC_SELFREF_EN)
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_SELFREF_EN)
	MOV	D0Ar6, #1
	SETD	[D0FrT], D0Ar6

	! wait until DDRC_OPERATING_MODE is set to self refresh
	MOVT	D0FrT, #HI(CR_DDRC_OPERATING_MODE)
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_OPERATING_MODE)
1:	GETD	D0Ar6, [D0FrT]
	CMP	D0Ar6, #CR_DDRC_OPERATING_MODE_SELFREF
	BNZ	1b

	! CR_DDR_CTRL |= 1 << CR_DDR_POWERDOWN
	MOVT	D0FrT, #HI(CR_DDR_CTRL)
	ADD	D0FrT, D0FrT, #LO(CR_DDR_CTRL)
	GETD	D0Ar6, [D0FrT]
	OR	D0Ar6, D0Ar6, #(1 << CR_DDR_POWERDOWN_BIT)
	SETD	[D0FrT], D0Ar6

#ifdef CONFIG_COMET_SUSPEND_MEM_SAFE
	! Read SAFE mode bit from SOC_Bootstrap register.
	! If the SoC is in SAFE mode powering off the main power island won't
	! work, so we can imprecisely emulate the standby with a watchdog reset
	! instead.
	MOVT	D0FrT, #HI(PDC_BASE_ADDR + PDC_SOC_BOOTSTRAP)
	ADD	D0FrT, D0FrT, #LO(PDC_BASE_ADDR + PDC_SOC_BOOTSTRAP)
	GETD	D0Ar6, [D0FrT]
	TST	D0Ar6, #LO(PDC_SOC_BOOTSTRAP_SAFE_MODE)
	BNZ	$Lsafemode_suspend
#endif /* CONFIG_COMET_SUSPEND_MEM_SAFE */

	!---------------- POWER OFF MAIN POWER ISLAND ----------------!

	! PDC_SOC_POWER = 0
	MOVT	A0.2,#HI(PDC_BASE_ADDR + PDC_SOC_POWER)
	ADD	A0.2,A0.2,#LO(PDC_BASE_ADDR + PDC_SOC_POWER)
	MOV	D0Re0, #0
	GETD	D1Re0, [A0.2]
	SETD	[A0.2], D0Re0

	! allow some time for it to work
	MPAUSE 100000

	!--------------- IF IT DIDN'T WORK, CLEANUP AND RETURN ---------------!

	! restore PDC_SOC_POWER
	SETD	[A0.2], D1Re0

#ifdef CONFIG_COMET_SUSPEND_MEM_SAFE
	B	$Lsuspend_failed
$Lsafemode_suspend:
	!---------------- STANDBY UNTIL TRIGGER FIRES ----------------!

	wait_for_wakeup D0Re0, D1Ar1

	!---------------- SOFT RESET THE SYSTEM TO EMULATE RESUME  ------------!

	! PDC_WD_SW_RESET = 1
	MOVT	A0.2,#HI(PDC_BASE_ADDR + PDC_WD_SW_RESET)
	ADD	A0.2,A0.2,#LO(PDC_BASE_ADDR + PDC_WD_SW_RESET)
	MOV	D1Re0, #1
	SETD	[A0.2], D1Re0

	! allow some time for it to work
	MPAUSE 100000

	!--------------- IF IT DIDN'T WORK, RETURN ---------------!
$Lsuspend_failed:
#endif /* CONFIG_COMET_SUSPEND_MEM_SAFE */

	! powerup the DDR clock and pads
	! CR_DDR_CTRL &= ~(1 << CR_DDR_POWERDOWN_BIT)
	MOVT	D0FrT, #HI(CR_DDR_CTRL)
	ADD	D0FrT, D0FrT, #LO(CR_DDR_CTRL)
	GETD	D0Ar6, [D0FrT]
	AND	D0Ar6, D0Ar6, #(~(1 << CR_DDR_POWERDOWN_BIT))
	SETD	[D0FrT], D0Ar6

	! take DDR out of self refresh
	! CR_DDRC_SELFREF_EN = 0
	MOVT	D0FrT, #HI(CR_DDRC_SELFREF_EN)
	ADD	D0FrT, D0FrT, #LO(CR_DDRC_SELFREF_EN)
	MOV	D0Ar6, #0
	SETD	[D0FrT], D0Ar6

	! undo board specific preparation
	COMET_DDR_FINISH_POWERDOWN()
$Licache_extent:

	!---------------- FINISHED ----------------!

	MOV	PC, D1RtP
ENDPROC(_metag_comet_suspend)

! Make the length available so the code can be copied into core memory.
ENTRY(_metag_comet_suspend_sz)
        .long   . - _metag_comet_suspend

#endif /* CONFIG_METAG_SUSPEND_MEM */
