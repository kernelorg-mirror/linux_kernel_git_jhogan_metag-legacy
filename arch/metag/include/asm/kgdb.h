#ifndef __ASM_METAG_KGDB_H
#define __ASM_METAG_KGDB_H

#include <asm/cacheflush.h>
#include <asm/ptrace.h>
#include <asm/switch.h>

enum regnames {
	/* units */
	GDB_CT		= 0,
	GDB_D0		= GDB_CT + 32,
	GDB_D1		= GDB_D0 + 32,
	GDB_A0		= GDB_D1 + 32,
	GDB_A1		= GDB_A0 + 16,
	GDB_PC		= GDB_A1 + 16,
	GDB_RA		= GDB_PC + 2,
	GDB_TR		= GDB_RA + 0,
	GDB_TT		= GDB_TR + 8,
	GDB_FX		= GDB_TT + 5,
	GDB_NUM_REGS	= GDB_FX + 16,

	/* Control unit registers (CT.x) */
	GDB_TXMODE	= GDB_CT + 1,
	GDB_TXSTATUS	= GDB_CT + 2,
	GDB_TXRPT	= GDB_CT + 3,
	GDB_TXBPOBITS	= GDB_CT + 11,
	GDB_TXDIVTIME	= GDB_CT + 28,

	/* Address unit registers (AX.y) */
	GDB_A0StP	= GDB_A0 + 0,

};

#define NUMREGBYTES    (GDB_NUM_REGS * 4)

static inline void arch_kgdb_breakpoint(void)
{
	asm volatile ("SWITCH #%c0" : : "i" (__METAG_SW_PERM_BREAK));
}

#define BUFMAX                 2048

#define CACHE_FLUSH_IS_SAFE	1
#define BREAK_INSTR_SIZE	4

#endif /* __ASM_METAG_KGDB_H */
