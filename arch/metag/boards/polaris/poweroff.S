! Copyright 2012 Imagination Technologies Ltd.
!
! Functions for powering down the Comet SoC on Polaris board.

#include <linux/linkage.h>
#include <asm/metag_isa.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/pdc.h>

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

!================ POWER OFF ===================================================!

! Cut the following power supplies in order:
!  +1v8 (powering DDR etc)
!  3v3_main (powering pads etc)
!  +1v2 (powering most of the SoC peripherals including the Meta)

ENTRY(_polaris_raw_power_off)
	! Prefetch enough cache lines to cover the power off sequence
	INSTR_ICACHE_BETWEEN ., $Licache_extent
	! Wait for prefetch to complete
	MPAUSE 1000

	!---------------- KILL +1V8 SUPPLY (BYE BYE RAM!) ------------!
	! KILL_1V8 is controlled by the I2S_DOUT2 GPIO (GPIO1[7])
#define KILL_1V8_MASK	(1 << 7)

	! DOUT1[7] = 1 (high)
	MOVT	A0.2,#HI(CR_PADS_GPIO_DOUT1)
	ADD	A0.2,A0.2,#LO(CR_PADS_GPIO_DOUT1)
	GETD	D0Re0, [A0.2]
	OR	D0Re0, D0Re0, #LO(KILL_1V8_MASK)
	SETD	[A0.2], D0Re0

	! DIR1[7] = 0 (output)
	MOVT	A0.2,#HI(CR_PADS_GPIO_DIR1)
	ADD	A0.2,A0.2,#LO(CR_PADS_GPIO_DIR1)
	GETD	D0Re0, [A0.2]
	ANDMB	D0Re0, D0Re0, #LO(~KILL_1V8_MASK)
	SETD	[A0.2], D0Re0

	! SELECT1[7] = 1 (gpio)
	MOVT	A0.2,#HI(CR_PADS_GPIO_SELECT1)
	ADD	A0.2,A0.2,#LO(CR_PADS_GPIO_SELECT1)
	GETD	D0Re0, [A0.2]
	OR	D0Re0, D0Re0, #LO(KILL_1V8_MASK)
	SETD	[A0.2], D0Re0

	! Allow some time for it to take effect before turning off pads
	MPAUSE 4000 ! ~1ms

	!---------------- KILL 3V3_MAIN SUPPLY -----------------------!
	! KILL_3V3_MAIN is controlled by the UART0_CTS GPIO (GPIO0[22])
#define KILL_3V3_MAIN_MASK	(1 << 22)

	! DOUT0[22] = 1 (high)
	MOVT	A0.2,#HI(CR_PADS_GPIO_DOUT0)
	ADD	A0.2,A0.2,#LO(CR_PADS_GPIO_DOUT0)
	GETD	D0Re0, [A0.2]
	ORT	D0Re0, D0Re0, #HI(KILL_3V3_MAIN_MASK)
	SETD	[A0.2], D0Re0

	! DIR0[22] = 0 (output)
	MOVT	A0.2,#HI(CR_PADS_GPIO_DIR0)
	ADD	A0.2,A0.2,#LO(CR_PADS_GPIO_DIR0)
	GETD	D0Re0, [A0.2]
	ANDMT	D0Re0, D0Re0, #HI(~KILL_3V3_MAIN_MASK)
	SETD	[A0.2], D0Re0

	! SELECT0[22] = 1 (gpio)
	MOVT	A0.2,#HI(CR_PADS_GPIO_SELECT0)
	ADD	A0.2,A0.2,#LO(CR_PADS_GPIO_SELECT0)
	GETD	D0Re0, [A0.2]
	ORT	D0Re0, D0Re0, #HI(KILL_3V3_MAIN_MASK)
	SETD	[A0.2], D0Re0

	! Allow some time for it to take effect before turning off SoC
	MPAUSE 4000 ! ~1ms

	!---------------- POWER OFF MAIN POWER ISLAND ----------------!

	! PDC_SOC_POWER = 0
	MOVT	A0.2,#HI(PDC_BASE_ADDR + PDC_SOC_POWER)
	ADD	A0.2,A0.2,#LO(PDC_BASE_ADDR + PDC_SOC_POWER)
	MOV	D0Re0, #0
	SETD	[A0.2], D0Re0

	!---------------- STOP THREAD TO ALLOW TIME TO DIE -----------!

#define OP3_EXIT	0
#define HALT_OK		0
	MOV	D1Ar1, #HALT_OK
	MOV	D0Ar6, #OP3_EXIT
	MSETL	[A0StP],D0Ar6,D0Ar4,D0Ar2
1:	SWITCH	#0xC30006
	! Never return
	B	1b

$Licache_extent:
ENDPROC(_polaris_raw_power_off)

! Make the length available so the code can be copied into core memory.
ENTRY(_polaris_raw_power_off_sz)
        .long   . - _polaris_raw_power_off
