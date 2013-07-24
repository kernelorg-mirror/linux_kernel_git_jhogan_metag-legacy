/*
 * ImgTec IR Decoder setup for Sharp protocol.
 *
 * Copyright 2012 Imagination Technologies Ltd.
 */

#include <linux/module.h>

#include "ir-img.h"

/* Convert Sharp data to a scancode */
static int img_ir_sharp_scancode(int len, u64 raw, u64 protocols)
{
	unsigned int addr, cmd, exp, chk;

	if (len != 15)
		return IMG_IR_ERR_INVALID;

	addr = (raw >>   0) & 0x1f;
	cmd  = (raw >>   5) & 0xff;
	exp  = (raw >>  13) &  0x1;
	chk  = (raw >>  14) &  0x1;

	/* validate data */
	if (!exp)
		return IMG_IR_ERR_INVALID;
	if (chk)
		/* probably the second half of the message */
		return IMG_IR_ERR_INVALID;

	return addr << 8 | cmd;
}

/* Convert Sharp scancode to Sharp data filter */
static int img_ir_sharp_filter(const struct img_ir_sc_filter *in,
			       struct img_ir_filter *out, u64 protocols)
{
	unsigned int addr, cmd, exp = 0, chk = 0;
	unsigned int addr_m, cmd_m, exp_m = 0, chk_m = 0;

	addr   = (in->data >> 8) & 0x1f;
	addr_m = (in->mask >> 8) & 0x1f;
	cmd    = (in->data >> 0) & 0xff;
	cmd_m  = (in->mask >> 0) & 0xff;
	if (cmd_m) {
		/* if filtering commands, we can only match the first part */
		exp   = 1;
		exp_m = 1;
		chk   = 0;
		chk_m = 1;
	}

	out->data = addr        |
		    cmd   <<  5 |
		    exp   << 13 |
		    chk   << 14;
	out->mask = addr_m      |
		    cmd_m <<  5 |
		    exp_m << 13 |
		    chk_m << 14;

	return 0;
}

/*
 * Sharp decoder
 * See also http://www.sbprojects.com/knowledge/ir/sharp.php
 */
static struct img_ir_decoder img_ir_sharp = {
	.type = RC_BIT_SHARP,
	.control = {
		.decoden = 0,
		.decodend2 = 1,
		.code_type = IMG_IR_CODETYPE_PULSEDIST,
		.d1validsel = 1,
	},
	/* main timings */
	.timings = {
		/* 0 symbol */
		.s10 = {
			.pulse = { 320	/* 320 us */ },
			.space = { 680	/* 1 ms period */ },
		},
		/* 1 symbol */
		.s11 = {
			.pulse = { 320	/* 230 us */ },
			.space = { 1680	/* 2 ms period */ },
		},
		/* free time */
		.ft = {
			.minlen = 15,
			.maxlen = 15,
			.ft_min = 5000,	/* 5 ms */
		},
	},
	/* scancode logic */
	.scancode = img_ir_sharp_scancode,
	.filter = img_ir_sharp_filter,
};

static int __init img_ir_sharp_init(void)
{
	return img_ir_register_decoder(&img_ir_sharp);
}
module_init(img_ir_sharp_init);

static void __exit img_ir_sharp_exit(void)
{
	img_ir_unregister_decoder(&img_ir_sharp);
}
module_exit(img_ir_sharp_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("ImgTec IR Sharp protocol support");
MODULE_LICENSE("GPL");
