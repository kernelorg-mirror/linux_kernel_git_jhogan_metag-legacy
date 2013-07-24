/*
 * ImgTec IR Decoder setup for NEC protocol.
 *
 * Copyright 2010,2011,2012 Imagination Technologies Ltd.
 */

#include <linux/module.h>

#include "ir-img.h"

/* Convert NEC data to a scancode */
static int img_ir_nec_scancode(int len, u64 raw, u64 protocols)
{
	unsigned int addr, addr_inv, data, data_inv;
	int scancode;
	/* a repeat code has no data */
	if (!len)
		return IMG_IR_REPEATCODE;
	if (len != 32)
		return IMG_IR_ERR_INVALID;
	addr     = (raw >>  0) & 0xff;
	addr_inv = (raw >>  8) & 0xff;
	data     = (raw >> 16) & 0xff;
	data_inv = (raw >> 24) & 0xff;
	/* Validate data */
	if ((data_inv ^ data) != 0xff)
		return IMG_IR_ERR_INVALID;

	if ((addr_inv ^ addr) != 0xff) {
		/* Extended NEC */
		scancode = addr     << 16 |
			   addr_inv <<  8 |
			   data;
	} else {
		/* Normal NEC */
		scancode = addr << 8 |
			   data;
	}
	return scancode;
}

/* Convert NEC scancode to NEC data filter */
static int img_ir_nec_filter(const struct img_ir_sc_filter *in,
			     struct img_ir_filter *out, u64 protocols)
{
	unsigned int addr, addr_inv, data, data_inv;
	unsigned int addr_m, addr_inv_m, data_m;

	data     = in->data & 0xff;
	data_m   = in->mask & 0xff;
	data_inv = data ^ 0xff;

	if (in->data & 0xff000000)
		return -EINVAL;

	if (in->data & 0x00ff0000) {
		/* Extended NEC */
		addr       = (in->data >> 16) & 0xff;
		addr_m     = (in->mask >> 16) & 0xff;
		addr_inv   = (in->data >>  8) & 0xff;
		addr_inv_m = (in->mask >>  8) & 0xff;
	} else {
		/* Normal NEC */
		addr       = (in->data >>  8) & 0xff;
		addr_m     = (in->mask >>  8) & 0xff;
		addr_inv   = addr ^ 0xff;
		addr_inv_m = addr_m;
	}

	out->data = data_inv << 24 |
		    data     << 16 |
		    addr_inv <<  8 |
		    addr;
	out->mask = data_m     << 24 |
		    data_m     << 16 |
		    addr_inv_m <<  8 |
		    addr_m;
	return 0;
}

/*
 * NEC decoder
 * See also http://www.sbprojects.com/knowledge/ir/nec.php
 *        http://wiki.altium.com/display/ADOH/NEC+Infrared+Transmission+Protocol
 */
static struct img_ir_decoder img_ir_nec = {
	.type = RC_BIT_NEC,
	.control = {
		.decoden = 1,
		.code_type = IMG_IR_CODETYPE_PULSEDIST,
	},
	/* main timings */
	.unit = 562500, /* 562.5 us */
	.timings = {
		/* leader symbol */
		.ldr = {
			.pulse = { 16	/* 9ms */ },
			.space = { 8	/* 4.5ms */ },
		},
		/* 0 symbol */
		.s00 = {
			.pulse = { 1	/* 562.5 us */ },
			.space = { 1	/* 562.5 us */ },
		},
		/* 1 symbol */
		.s01 = {
			.pulse = { 1	/* 562.5 us */ },
			.space = { 3	/* 1687.5 us */ },
		},
		/* free time */
		.ft = {
			.minlen = 32,
			.maxlen = 32,
			.ft_min = 10,	/* 5.625 ms */
		},
	},
	/* repeat codes */
	.repeat = 108,			/* 108 ms */
	.rtimings = {
		/* leader symbol */
		.ldr = {
			.space = { 4	/* 2.25 ms */ },
		},
		/* free time */
		.ft = {
			.minlen = 0,	/* repeat code has no data */
			.maxlen = 0,
		},
	},
	/* scancode logic */
	.scancode = img_ir_nec_scancode,
	.filter = img_ir_nec_filter,
};

static int __init img_ir_nec_init(void)
{
	return img_ir_register_decoder(&img_ir_nec);
}
module_init(img_ir_nec_init);

static void __exit img_ir_nec_exit(void)
{
	img_ir_unregister_decoder(&img_ir_nec);
}
module_exit(img_ir_nec_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("ImgTec IR NEC protocol support");
MODULE_LICENSE("GPL");
