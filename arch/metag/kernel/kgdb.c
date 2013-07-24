/*
 * Meta KGDB support
 *
 * Copyright (C) 2008 - 2009  Paul Mundt
 * Copyright (C) 2011 - 2012  Imagination Technologies Ltd
 *
 * Based on SH version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/disas.h>

#define STEP_OPCODE		0xaf400001

/**
 * get_reg_by_unit() - Get the value of a register by unit.reg in regs.
 * @regs:	Register state.
 * @unit:	Meta unit number.
 * @reg:	Meta register number.
 * @val:	Output value.
 *
 * Puts the value of a register in *val and returns 0, or returns non-zero on
 * failure.
 */
static int get_reg_by_unit(struct pt_regs *regs, int unit, int reg,
			   unsigned long *val)
{
	switch (unit) {
	case UNIT_CT:
		switch (reg) {
		case 1: /* TXMODE */
			*val = regs->ctx.CurrMODE;
			break;
		case 2: /* TXSTATUS */
			*val = regs->ctx.Flags;
			break;
		case 3: /* TXRPT */
			*val = regs->ctx.CurrRPT;
			break;
		case 11: /* TXBPOBITS */
			*val = regs->ctx.CurrBPOBITS;
			break;
		case 28: /* TXDIVTIME */
			*val = regs->ctx.CurrDIVTIME;
			break;
		default:
			return 1;
		}
	case UNIT_D0:
		if (reg < 8)
			*val = regs->ctx.DX[reg].U0;
		else
			return 1;
		break;
	case UNIT_D1:
		if (reg < 8)
			*val = regs->ctx.DX[reg].U1;
		else
			return 1;
		break;
	case UNIT_A0:
		if (reg < 2)
			*val = regs->ctx.AX[reg].U0;
		else if (reg == 2)
			*val = regs->ctx.Ext.AX2.U0;
		else if (reg == 3)
			*val = regs->ctx.AX3[0].U0;
		else
			return 1;
		break;
	case UNIT_A1:
		if (reg < 2)
			*val = regs->ctx.AX[reg].U1;
		else if (reg == 2)
			*val = regs->ctx.Ext.AX2.U1;
		else if (reg == 3)
			*val = regs->ctx.AX3[0].U1;
		else
			return 1;
		break;
	case UNIT_PC:
		if (reg == 0) /* PC */
			*val = regs->ctx.CurrPC;
		else
			return 1;
		break;
	}
	return 0;
}

/**
 * get_step_address() - Calculate the step address (next PC).
 * @linux_regs:		Register state.
 *
 * Returns the next PC to break on when single stepping.
 */
static long get_step_address(struct pt_regs *linux_regs)
{
	long pc = linux_regs->ctx.CurrPC;
	long addr;
	unsigned long op = __raw_readl((unsigned long *)pc);
	unsigned long opcode, group;

	/* Bcc */
	if (OP_BCC(op)) {
		if (!test_cc(linux_regs, OP_BCC_CC(op)))
			goto no_jump;
		/* branch repeat automatically uses TXRPT as counter */
		if (OP_BCC_R(op) && !linux_regs->ctx.CurrRPT)
			goto no_jump;
		return pc + OP_BCC_IMM(op);
	/* JUMP/CALL */
	} else if (OP_JUMP(op)) {
		if (get_reg_by_unit(linux_regs, OP_JUMP_UB(op), OP_JUMP_RS(op),
				    &addr))
			goto unknown;
		return (addr + OP_JUMP_IMM(op)) & -INSN_SIZE;
	/* CALLR */
	} else if (OP_CALLR(op)) {
		return pc + OP_CALLR_IMM(op);
	}

	/* MOV PC,... */
	opcode = op >> 24;
	group = opcode >> 4;
	/* add,and,or,xor,shift, aadd, mul */
	if (group < 6 || group == 8 || (opcode & 0xfc) == 0x84) {
		/*
		 * conditional form
		 * unit (0xf << 4) == PC
		 * reg (0x1f << 19) == 0
		 */
		if ((op & 0x06f801e0) == 0x040000a0) {
			int cc = (op >> 1) & 0xf;
			if (!test_cc(linux_regs, cc))
				goto no_jump;
			/* address unit add */
			if (group == 8) {
				int pm = op & (1 << 27);
				int au = (op >> 24) & 0x1;
				int pc1 = op & (1 << 18);
				int rs1 = (op >> 14) & 0xf;
				int pc2 = op & (1 << 13);
				int rs2 = (op >> 9) & 0x1f;
				int o2r = op & (1 << 0);
				int us1, us2;
				unsigned long src1, src2;

				us1 = UNIT_A0 + au;
				if (o2r) {
					us2 = decode_o2r(us1, &rs2);
					pc2 = 0;
				} else {
					us2 = us1;
				}

				if (!pc1 && get_reg_by_unit(linux_regs, us1,
							    rs1, &src1))
					goto unknown;
				if (!pc2 && get_reg_by_unit(linux_regs, us2,
							    rs2, &src2))
					goto unknown;

				if (pc1 && pc2) {
					/* src1 = src2 = pc (silly) */
					if (pm)
						return 0;
					else
						return pc * 2;
				} else if (pc1) {
					/* src1 = pc */
					if (pm)
						return pc - src2;
					else
						return pc + src2;
				} else if (pc2) {
					/* src2 = pc */
					if (pm)
						return src1 - pc;
					else
						return src1 + pc;
				} else {
					if (pm)
						return src1 - src2;
					else
						return src1 + src2;
				}
			}
			pr_debug("KGDB: Confused, arithmetic conditional with PC destination @%08lx: %08lx\n",
				 pc, op);
			goto unknown;
		}
		/* conditional immediate form with condition always
		 * ca (1 << 5) == 1
		 * unit (0xf << 1) == PC
		 * reg (0x1f << 19) == 0
		 */
		if ((op & 0x06f8003e) == 0x0600002a) {
			pr_debug("KGDB: Confused, arithmetic immediate with PC destination @%08lx: %08lx\n",
				 pc, op);
			goto unknown;
		}
	}

	/* RTH */
	if (op == 0xa37fffff) {
		/*
		 * Well it probably will jump, but the point of this is to
		 * disambiguate the conflict with TTMOV.
		 */
		goto no_jump;
	}

	/* SWAP/MOV/TTMOV */
	if ((op & 0xfff83e01) == 0xa3001600 || /* SWAP ..., PC */
	    (op & 0xff07c1e1) == 0xa30000a0 || /* SWAP/MOV PC, ... */
	    (op & 0xff07c3e1) == 0xa30002a1) { /* TTMOV PC, ... */
		int cc = (op >> 1) & 0xf;
		int swap = 0;
		int ud, rd;
		unsigned long addr;
		if (!test_cc(linux_regs, cc))
			goto no_jump;
		/* if not TT, read swap bit */
		if (!(op & 1))
			swap = op & (1 << 9);
		/* read source register */
		ud = (op >> 10) & 0xf;
		rd = (op >> 19) & 0x1f;
		/* if swap and source is PC, read destination opead */
		if (swap && (ud == UNIT_PC || !rd)) {
			ud = (op >> 5) & 0xf;
			rd = (op >> 14) & 0x1f;
		}
		/* read the value of the register */
		if (get_reg_by_unit(linux_regs, ud, rd, &addr))
			goto unknown;
		return addr & -INSN_SIZE;
	}

	/* DEFRcc PC, ...*/
	if ((op & 0xfff83fe1) == 0xa30020a1) {
		int cc = (op >> 1) & 0xf;
		if (!test_cc(linux_regs, cc))
			goto no_jump;
		pr_debug("KGDB: Confused, DEFRcc PC,... @%08lx: %08lx\n",
			 pc, op);
		goto unknown;
	}
	/* KICKcc PC, ... */
	if ((op & 0xfff83fe1) == 0xa30000a1) {
		int cc = (op >> 1) & 0xf;
		if (!test_cc(linux_regs, cc))
			goto no_jump;
		pr_debug("KGDB: Confused, KICKcc PC,... @%08lx: %08lx\n",
			 pc, op);
		goto unknown;
	}

	/* GETD PC, [Rx.y] */
	if ((op & 0xfff8001e) == 0xc600000a) {
		int rb = (op >> 14) & 0x1f;
		int imm = ((int)(op << 18) >> 26) << 2;
		int bu = (op >> 5) & 0x3;
		int pp = op & (1 << 0);
		unsigned long addr;
		int ub;

		ub = metag_bu_map[bu];
		pr_debug("KGDB: GETD PC, [%d.%d+%d]\n", ub, rb, imm);
		if (get_reg_by_unit(linux_regs, ub, rb, &addr))
			goto unknown;

		if (!pp)
			addr += imm;

		pr_debug("KGDB: GETD attempting to read [%08lx]\n",
			 addr);
		addr = __raw_readl((unsigned long *)addr);
		pr_debug("KGDB: GETD PC, %08lx\n", addr);
		return addr & -INSN_SIZE;
	}

no_jump:
	return pc + INSN_SIZE;
unknown:
	return 0;
}

static unsigned long stepped_address;
static unsigned long stepped_opcode;

/**
 * do_single_step() - Set up a single step.
 * @linux_regs:		Register state.
 *
 * Replace the instruction immediately after the current instruction
 * (i.e. next in the expected flow of control) with a trap instruction,
 * so that returning will cause only a single instruction to be executed.
 */
static void do_single_step(struct pt_regs *linux_regs)
{
	/* Determine where the target instruction will send us to */
	long addr = get_step_address(linux_regs);
	unsigned long *paddr = (unsigned long *)addr;

	if (unlikely(addr))
		return;

	stepped_address = addr;

	/* Replace it */
	stepped_opcode = __raw_readl(paddr);
	*paddr = STEP_OPCODE;

	/* Flush and return */
	flush_icache_range(addr, addr + INSN_SIZE);
}

/* Undo a single step */
static void undo_single_step(struct pt_regs *linux_regs)
{
	/* If we have stepped, put back the old instruction */
	/* Use stepped_address in case we stopped elsewhere */
	if (stepped_opcode != 0) {
		__raw_writel(stepped_opcode, (unsigned long *)stepped_address);
		flush_icache_range(stepped_address,
				   stepped_address + INSN_SIZE);
	}

	stepped_opcode = 0;
}

static void tbictx_to_gdb_regs(unsigned long *gdb_regs, TBICTX *regs)
{
	int i;

	gdb_regs[GDB_TXSTATUS]	= regs->Flags;
	gdb_regs[GDB_PC]	= regs->CurrPC;
	for (i = 0; i < 8; ++i) {
		gdb_regs[GDB_D0 + i] = regs->DX[i].U0;
		gdb_regs[GDB_D1 + i] = regs->DX[i].U1;
	}
	gdb_regs[GDB_TXRPT]	= regs->CurrRPT;
	gdb_regs[GDB_TXBPOBITS]	= regs->CurrBPOBITS;
	gdb_regs[GDB_TXMODE]	= regs->CurrMODE;
	gdb_regs[GDB_TXDIVTIME]	= regs->CurrDIVTIME;
	for (i = 0; i < 2; ++i) {
		gdb_regs[GDB_A0 + i] = regs->AX[i].U0;
		gdb_regs[GDB_A1 + i] = regs->AX[i].U1;
	}
	gdb_regs[GDB_A0 + 2]	= regs->Ext.AX2.U0;
	gdb_regs[GDB_A1 + 2]	= regs->Ext.AX2.U1;
	gdb_regs[GDB_A0 + 3]	= regs->AX3[0].U0;
	gdb_regs[GDB_A1 + 3]	= regs->AX3[0].U1;
}

static void gdb_regs_to_tbictx(unsigned long *gdb_regs, TBICTX *regs)
{
	int i;

	regs->Flags		= gdb_regs[GDB_TXSTATUS];
	regs->CurrPC		= gdb_regs[GDB_PC];
	for (i = 0; i < 8; ++i) {
		regs->DX[i].U0	= gdb_regs[GDB_D0 + i];
		regs->DX[i].U1	= gdb_regs[GDB_D1 + i];
	}
	regs->CurrRPT		= gdb_regs[GDB_TXRPT];
	regs->CurrBPOBITS	= gdb_regs[GDB_TXBPOBITS];
	regs->CurrMODE		= gdb_regs[GDB_TXMODE];
	regs->CurrDIVTIME	= gdb_regs[GDB_TXDIVTIME];
	for (i = 0; i < 2; ++i) {
		regs->AX[i].U0	= gdb_regs[GDB_A0 + i];
		regs->AX[i].U1	= gdb_regs[GDB_A1 + i];
	}
	regs->Ext.AX2.U0	= gdb_regs[GDB_A0 + 2];
	regs->Ext.AX2.U1	= gdb_regs[GDB_A1 + 2];
	regs->AX3[0].U0		= gdb_regs[GDB_A0 + 3];
	regs->AX3[0].U1		= gdb_regs[GDB_A1 + 3];
}

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	tbictx_to_gdb_regs(gdb_regs, &regs->ctx);
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	gdb_regs_to_tbictx(gdb_regs, &regs->ctx);
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	tbictx_to_gdb_regs(gdb_regs, p->thread.kernel_context);
}

#ifdef CONFIG_SMP
static void kgdb_call_nmi_hook(void *ignored)
{
	kgdb_nmicallback(raw_smp_processor_id(), get_irq_regs());
}

void kgdb_roundup_cpus(unsigned long flags)
{
	local_irq_enable();
	smp_call_function(kgdb_call_nmi_hook, NULL, 0);
	local_irq_disable();
}
#endif

int kgdb_arch_handle_exception(int e_vector, int signo, int err_code,
			       char *remcomInBuffer, char *remcomOutBuffer,
			       struct pt_regs *linux_regs)
{
	unsigned long addr;
	char *ptr;
	unsigned long instr;

	/* Undo any stepping we may have done */
	undo_single_step(linux_regs);

	switch (remcomInBuffer[0]) {
	case 'c':
	case 's':
		/*
		 * Try to read optional parameter, pc unchanged if no parm.
		 * If this was a compiled breakpoint, we need to move to the
		 * next instruction or we will just breakpoint over and over
		 * again.
		 */
		ptr = &remcomInBuffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			linux_regs->ctx.CurrPC = addr;
		else if (!probe_kernel_read(&instr,
					    (void *)linux_regs->ctx.CurrPC, 4)
			 && instr ==  __METAG_SW_ENCODING(PERM_BREAK))
			linux_regs->ctx.CurrPC += 4;
	case 'D':
	case 'k':
		atomic_set(&kgdb_cpu_doing_single_step, -1);

		if (remcomInBuffer[0] == 's') {
			do_single_step(linux_regs);
			kgdb_single_step = 1;

			atomic_set(&kgdb_cpu_doing_single_step,
				   raw_smp_processor_id());
		}

		return 0;
	}

	/* this means that we do not want to exit from the handler: */
	return -1;
}

unsigned long kgdb_arch_pc(int exception, struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->ctx.CurrPC = ip;
}

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	switch (cmd) {
	case DIE_TRAP:
		/*
		 * This means a user thread is single stepping
		 * a system call which should be ignored
		 */
		if (test_thread_flag(TIF_SINGLESTEP))
			return NOTIFY_DONE;

		/* fall through */
	default:
		if (user_mode(regs))
			return NOTIFY_DONE;
	}

	if (kgdb_handle_exception(args->trapnr & 0xff, args->signr, args->err,
				  regs))
		return NOTIFY_DONE;

	return NOTIFY_STOP;
}

static int
kgdb_notify(struct notifier_block *self, unsigned long cmd, void *ptr)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_notify(ptr, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call	= kgdb_notify,

	/*
	 * Lowest-prio notifier priority, we want to be notified last:
	 */
	.priority	= -INT_MAX,
};

int kgdb_arch_init(void)
{
	return register_die_notifier(&kgdb_notifier);
}

void kgdb_arch_exit(void)
{
	unregister_die_notifier(&kgdb_notifier);
}

struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: SWITCH #0x400001 */
	.gdb_bpt_instr		= { 0x01, 0x00, 0x40, 0xaf },
};
