/*
 * ImgTec IR Decoder found in PowerDown Controller.
 *
 * Copyright 2010,2011.2012 Imagination Technologies Ltd.
 */

#ifndef _IMG_IR_H_
#define _IMG_IR_H_

#include <linux/kernel.h>
#include <media/rc-core.h>

/* registers */

/* relative to the start of the IR block of registers */
#define IMG_IR_CONTROL		0x00
#define IMG_IR_STATUS		0x04
#define IMG_IR_DATA_LW		0x08
#define IMG_IR_DATA_UP		0x0c
#define IMG_IR_LEAD_SYMB_TIMING	0x10
#define IMG_IR_S00_SYMB_TIMING	0x14
#define IMG_IR_S01_SYMB_TIMING	0x18
#define IMG_IR_S10_SYMB_TIMING	0x1c
#define IMG_IR_S11_SYMB_TIMING	0x20
#define IMG_IR_FREE_SYMB_TIMING	0x24
#define IMG_IR_POW_MOD_PARAMS	0x28
#define IMG_IR_POW_MOD_ENABLE	0x2c
#define IMG_IR_IRQ_MSG_DATA_LW	0x30
#define IMG_IR_IRQ_MSG_DATA_UP	0x34
#define IMG_IR_IRQ_MSG_MASK_LW	0x38
#define IMG_IR_IRQ_MSG_MASK_UP	0x3c
#define IMG_IR_IRQ_ENABLE	0x40
#define IMG_IR_IRQ_STATUS	0x44
#define IMG_IR_IRQ_CLEAR	0x48
#define IMG_IR_IRCORE_ID	0xf0
#define IMG_IR_CORE_REV		0xf4
#define IMG_IR_CORE_DES1	0xf8
#define IMG_IR_CORE_DES2	0xfc


/* field masks */

/* IMG_IR_CONTROL */
#define IMG_IR_DECODEN		0x40000000
#define IMG_IR_CODETYPE		0x30000000
#define IMG_IR_CODETYPE_SHIFT		28
#define IMG_IR_HDRTOG		0x08000000
#define IMG_IR_LDRDEC		0x04000000
#define IMG_IR_DECODINPOL	0x02000000	/* active high */
#define IMG_IR_BITORIEN		0x01000000	/* MSB first */
#define IMG_IR_D1VALIDSEL	0x00008000
#define IMG_IR_BITINV		0x00000040	/* don't invert */
#define IMG_IR_DECODEND2	0x00000010
#define IMG_IR_BITORIEND2	0x00000002	/* MSB first */
#define IMG_IR_BITINVD2		0x00000001	/* don't invert */

/* IMG_IR_STATUS */
#define IMG_IR_RXDVALD2		0x00001000
#define IMG_IR_IRRXD		0x00000400
#define IMG_IR_TOGSTATE		0x00000200
#define IMG_IR_RXDVAL		0x00000040
#define IMG_IR_RXDLEN		0x0000003f
#define IMG_IR_RXDLEN_SHIFT		0

/* IMG_IR_LEAD_SYMB_TIMING, IMG_IR_Sxx_SYMB_TIMING */
#define IMG_IR_PD_MAX		0xff000000
#define IMG_IR_PD_MAX_SHIFT		24
#define IMG_IR_PD_MIN		0x00ff0000
#define IMG_IR_PD_MIN_SHIFT		16
#define IMG_IR_W_MAX		0x0000ff00
#define IMG_IR_W_MAX_SHIFT		8
#define IMG_IR_W_MIN		0x000000ff
#define IMG_IR_W_MIN_SHIFT		0

/* IMG_IR_FREE_SYMB_TIMING */
#define IMG_IR_MAXLEN		0x0007e000
#define IMG_IR_MAXLEN_SHIFT		13
#define IMG_IR_MINLEN		0x00001f00
#define IMG_IR_MINLEN_SHIFT		8
#define IMG_IR_FT_MIN		0x000000ff
#define IMG_IR_FT_MIN_SHIFT		0

/* IMG_IR_POW_MOD_PARAMS */
#define IMG_IR_PERIOD_LEN	0x3f000000
#define IMG_IR_PERIOD_LEN_SHIFT		24
#define IMG_IR_PERIOD_DUTY	0x003f0000
#define IMG_IR_PERIOD_DUTY_SHIFT	16
#define IMG_IR_STABLE_STOP	0x00003f00
#define IMG_IR_STABLE_STOP_SHIFT	8
#define IMG_IR_STABLE_START	0x0000003f
#define IMG_IR_STABLE_START_SHIFT	0

/* IMG_IR_POW_MOD_ENABLE */
#define IMG_IR_POWER_OUT_EN	0x00000002
#define IMG_IR_POWER_MOD_EN	0x00000001

/* IMG_IR_IRQ_ENABLE, IMG_IR_IRQ_STATUS, IMG_IR_IRQ_CLEAR */
#define IMG_IR_IRQ_DEC2_ERR	0x00000080
#define IMG_IR_IRQ_DEC_ERR	0x00000040
#define IMG_IR_IRQ_ACT_LEVEL	0x00000020
#define IMG_IR_IRQ_FALL_EDGE	0x00000010
#define IMG_IR_IRQ_RISE_EDGE	0x00000008
#define IMG_IR_IRQ_DATA_MATCH	0x00000004
#define IMG_IR_IRQ_DATA2_VALID	0x00000002
#define IMG_IR_IRQ_DATA_VALID	0x00000001
#define IMG_IR_IRQ_ALL		0x000000ff
#define IMG_IR_IRQ_EDGE		(IMG_IR_IRQ_FALL_EDGE | IMG_IR_IRQ_RISE_EDGE)

/* IMG_IR_CORE_ID */
#define IMG_IR_CORE_ID		0x00ff0000
#define IMG_IR_CORE_ID_SHIFT		16
#define IMG_IR_CORE_CONFIG	0x0000ffff
#define IMG_IR_CORE_CONFIG_SHIFT	0

/* IMG_IR_CORE_REV */
#define IMG_IR_DESIGNER		0xff000000
#define IMG_IR_DESIGNER_SHIFT		24
#define IMG_IR_MAJOR_REV	0x00ff0000
#define IMG_IR_MAJOR_REV_SHIFT		16
#define IMG_IR_MINOR_REV	0x0000ff00
#define IMG_IR_MINOR_REV_SHIFT		8
#define IMG_IR_MAINT_REV	0x000000ff
#define IMG_IR_MAINT_REV_SHIFT		0


/* constants */

#define IMG_IR_CODETYPE_PULSELEN	0x0	/* Sony */
#define IMG_IR_CODETYPE_PULSEDIST	0x1	/* NEC, Toshiba, Micom, Sharp */
#define IMG_IR_CODETYPE_BIPHASE		0x2	/* RC-5/6 */
#define IMG_IR_CODETYPE_2BITPULSEPOS	0x3	/* RC-MM */


/* Timing information */

/**
 * struct img_ir_control - Decoder control settings
 * @decoden:	Primary decoder enable
 * @code_type:	Decode type (see IMG_IR_CODETYPE_*)
 * @hdrtog:	Detect header toggle symbol after leader symbol
 * @ldrdec:	Don't discard leader if maximum width reached
 * @decodinpol:	Decoder input polarity (1=active high)
 * @bitorien:	Bit orientation (1=MSB first)
 * @d1validsel:	Decoder 2 takes over if it detects valid data
 * @bitinv:	Bit inversion switch (1=don't invert)
 * @decodend2:	Secondary decoder enable (no leader symbol)
 * @bitoriend2:	Bit orientation (1=MSB first)
 * @bitinvd2:	Secondary decoder bit inversion switch (1=don't invert)
 */
struct img_ir_control {
	unsigned decoden:1;
	unsigned code_type:2;
	unsigned hdrtog:1;
	unsigned ldrdec:1;
	unsigned decodinpol:1;
	unsigned bitorien:1;
	unsigned d1validsel:1;
	unsigned bitinv:1;
	unsigned decodend2:1;
	unsigned bitoriend2:1;
	unsigned bitinvd2:1;
};

/**
 * struct img_ir_timing_range - range of timing values
 * @min:	Minimum timing value
 * @max:	Maximum timing value (if < @min, this will be set to @min during
 *		preprocessing step, so it is normally not explicitly initialised
 *		and is taken care of by the tolerance)
 */
struct img_ir_timing_range {
	u16 min;
	u16 max;
};

/**
 * struct img_ir_symbol_timing - timing data for a symbol
 * @pulse:	Timing range for the length of the pulse in this symbol
 * @space:	Timing range for the length of the space in this symbol
 */
struct img_ir_symbol_timing {
	struct img_ir_timing_range pulse;
	struct img_ir_timing_range space;
};

/**
 * struct img_ir_free_timing - timing data for free time symbol
 * @minlen:	Minimum number of bits of data
 * @maxlen:	Maximum number of bits of data
 * @ft_min:	Minimum free time after message
 */
struct img_ir_free_timing {
	/* measured in bits */
	u8 minlen;
	u8 maxlen;
	u16 ft_min;
};

/**
 * struct img_ir_timings - Timing values.
 * @ldr:	Leader symbol timing data
 * @s00:	Zero symbol timing data for primary decoder
 * @s01:	One symbol timing data for primary decoder
 * @s10:	Zero symbol timing data for secondary (no leader symbol) decoder
 * @s11:	One symbol timing data for secondary (no leader symbol) decoder
 * @ft:		Free time symbol timing data
 */
struct img_ir_timings {
	struct img_ir_symbol_timing ldr, s00, s01, s10, s11;
	struct img_ir_free_timing ft;
};

/**
 * struct img_ir_sc_filter - Filter scan codes.
 * @data:	Data to match.
 * @mask:	Mask of bits to compare.
 */
struct img_ir_sc_filter {
	unsigned int data;
	unsigned int mask;
};

/**
 * struct img_ir_filter - Filter IR events.
 * @data:	Data to match.
 * @mask:	Mask of bits to compare.
 * @minlen:	Additional minimum number of bits.
 * @maxlen:	Additional maximum number of bits.
 */
struct img_ir_filter {
	u64 data;
	u64 mask;
	u8 minlen;
	u8 maxlen;
};

/**
 * struct img_ir_timing_regvals - Calculated timing register values.
 * @ldr:	Leader symbol timing register value
 * @s00:	Zero symbol timing register value for primary decoder
 * @s01:	One symbol timing register value for primary decoder
 * @s10:	Zero symbol timing register value for secondary decoder
 * @s11:	One symbol timing register value for secondary decoder
 * @ft:		Free time symbol timing register value
 */
struct img_ir_timing_regvals {
	u32 ldr, s00, s01, s10, s11, ft;
};

#define IMG_IR_REPEATCODE	(-1)	/* repeat the previous code */
#define IMG_IR_ERR_INVALID	(-2)	/* not a valid code */

/**
 * struct img_ir_decoder - Decoder settings for an IR protocol.
 * @type:		Protocol types bitmap.
 * @unit:		Unit of timings in nanoseconds (default 1 us).
 * @timings:		Primary timings
 * @rtimings:		Additional override timings while waiting for repeats.
 * @repeat:		Maximum repeat interval (always in milliseconds).
 * @control:		Control flags.
 *
 * @scancode:		Pointer to function to convert the IR data into a
 *			scancode (it must be safe to execute in interrupt
 *			context).
 *			Returns IMG_IR_REPEATCODE to repeat previous code.
 *			Returns IMG_IR_ERR_* on error.
 * @filter:		Pointer to function to convert scancode filter to raw
 *			hardware filter. The minlen and maxlen fields will have
 *			been initialised to the maximum range.
 *
 * @reg_ctrl:		Processed control register value.
 * @clk_hz:		Assumed clock rate in Hz for processed timings.
 * @reg_timings:	Processed primary timings.
 * @reg_rtimings:	Processed repeat timings.
 * @next:		Next IR decoder (to form a linked list).
 */
struct img_ir_decoder {
	/* core description */
	u64				type;
	unsigned int			unit;
	struct img_ir_timings		timings;
	struct img_ir_timings		rtimings;
	unsigned int			repeat;
	struct img_ir_control		control;

	/* scancode logic */
	int (*scancode)(int len, u64 raw, u64 protocols);
	int (*filter)(const struct img_ir_sc_filter *in,
		      struct img_ir_filter *out, u64 protocols);

	/* for internal use only */
	u32				reg_ctrl;
	unsigned long			clk_hz;
	struct img_ir_timing_regvals	reg_timings;
	struct img_ir_timing_regvals	reg_rtimings;
	struct img_ir_decoder		*next;
};

int img_ir_register_decoder(struct img_ir_decoder *dec);
void img_ir_unregister_decoder(struct img_ir_decoder *dec);

#endif
