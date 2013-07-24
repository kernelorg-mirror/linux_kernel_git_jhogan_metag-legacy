#ifndef __ASM_DISAS_H
#define __ASM_DISAS_H

#define INSN_SIZE		4

/* General decoding */
#define OP_CODE(op)		((op) >> 24)
#define OP_IMM19(op)		(((int)((op) << 8) >> 13) * INSN_SIZE)
#define OP_IMM16(op)		(((op) >> 3) & 0xffff)
#define OP_REG19(op)		(((op) >> 19) & 0x1f)

/* Instruction matching */
#define OP_BCC(op)		(OP_CODE(op) == OPCODE_BCC)
#define OP_CALLR(op)		(OP_CODE(op) == OPCODE_CALLR)
#define OP_JUMP(op)		(OP_CODE(op) == OPCODE_JUMP)

/* Instruction decoding */
#define OP_BCC_CC(op)		(((op) >> 1) & 0xf)
#define OP_BCC_R(op)		((op) & 0x1)
#define OP_BCC_IMM(op)		(OP_IMM19(op) * INSN_SIZE)
#define OP_JUMP_IMM(op)		OP_IMM16(op)
#define OP_JUMP_RS(op)		OP_REG19(op)
#define OP_JUMP_BU(op)		((op) & 0x3)
#define OP_JUMP_UB(op)		metag_bu_map[OP_JUMP_BU(op)]
#define OP_CALLR_IMM(op)	(OP_IMM19(op) * INSN_SIZE)

/* op codes */
enum metag_opcode {
	OPCODE_BCC	= 0xa0,
	OPCODE_CALLR	= 0xab,
	OPCODE_JUMP	= 0xac,
};

/* unit codes */
enum metag_unit {
	UNIT_CT,       /* 0x0 */
	UNIT_D0,
	UNIT_D1,
	UNIT_A0,
	UNIT_A1,       /* 0x4 */
	UNIT_PC,
	UNIT_RA,
	UNIT_TR,
	UNIT_TT,       /* 0x8 */
	UNIT_FX,
	UNIT_MAX,
};

/* condition codes */
enum metag_cc {
	CC_A,  /* 0x0 */
	CC_EQ,
	CC_NE,
	CC_CS,
	CC_CC, /* 0x4 */
	CC_N,
	CC_PL,
	CC_VS,
	CC_VC, /* 0x8 */
	CC_HI,
	CC_LS,
	CC_GE,
	CC_LT, /* 0xC */
	CC_GT,
	CC_LE,
	CC_NV,
};

/* condition flags */
enum metag_cf {
	CF_C = 0x1, /* Carry */
	CF_V = 0x2, /* oVerflow */
	CF_N = 0x4, /* Negative */
	CF_Z = 0x8, /* Zero */
};

/* 2 bit base unit (BU) mapping */
static enum metag_unit metag_bu_map[4] = {
	UNIT_A1,
	UNIT_D0,
	UNIT_D1,
	UNIT_A0,
};

/* decoding operand 2 replace (o2r) */
static enum metag_unit metag_o2r_map[4] = {
	UNIT_A1,
	UNIT_D0,
	UNIT_RA,
	UNIT_A0,
};
static enum metag_unit metag_o2r_alternative = UNIT_D1;

static enum metag_unit decode_o2r(enum metag_unit us1,
				  int *rs2 /* in out */)
{
	enum metag_unit u;
	int us = (*rs2 >> 3) & 0x3;

	*rs2 &= 0x7;
	u = metag_o2r_map[us];
	if (u == us1)
		u = metag_o2r_alternative;
	return u;
}


static inline int test_cc(struct pt_regs *regs, int cc)
{
	uint32_t cflags = regs->ctx.Flags & 0xf;
	switch (cc) {
	case CC_A:  /* 1 */
		return 1;
	case CC_EQ: /* Z */
		return cflags & CF_Z;
	case CC_NE: /* !Z */
		return !(cflags & CF_Z);
	case CC_CS: /* C */
		return cflags & CF_C;
	case CC_CC: /* !C */
		return !(cflags & CF_C);
	case CC_N: /* N */
		return cflags & CF_N;
	case CC_PL: /* !N */
		return !(cflags & CF_N);
	case CC_VS: /* V */
		return cflags & CF_V;
	case CC_VC: /* !V */
		return !(cflags & CF_V);
	case CC_HI: /* !(C | Z) */
		return !((cflags & CF_C) || (cflags & CF_Z));
	case CC_LS: /* C | Z */
		return (cflags & CF_C) || (cflags & CF_Z);
	case CC_GE: /* !(N ^ V) */
		return !(cflags & CF_N) == !(cflags & CF_V);
	case CC_LT: /* N ^ V */
		return !(cflags & CF_N) != !(cflags & CF_V);
	case CC_GT: /* !(Z | (N ^ V)) */
		return !((cflags & CF_Z) ||
			 ((cflags & CF_N) != (cflags & CF_V)));
	case CC_LE: /* Z | (N ^ V) */
		return (cflags & CF_Z) ||
			((cflags & CF_N) != (cflags & CF_V));
	case CC_NV: /* 0 */
	default:
		return 0;
	}
}

#endif
