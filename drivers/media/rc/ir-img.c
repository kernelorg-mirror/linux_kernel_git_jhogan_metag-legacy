/*
 * ImgTec IR Decoder found in PowerDown Controller.
 *
 * Copyright 2010,2011,2012 Imagination Technologies Ltd.
 *
 * This ties into the input subsystem using the IR-core. When some more IR
 * remote control specific interfaces go upstream this driver should be updated
 * accodingly (e.g. for changing protocol).
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <media/rc-core.h>
#include <asm/soc-tz1090/clock.h>
#include "ir-img.h"

#ifndef CONFIG_IR_IMG_RAW
/* Decoder list */
static DEFINE_SPINLOCK(img_ir_decoders_lock);
static struct img_ir_decoder *img_ir_decoders;
static struct img_ir_priv *img_ir_privs;

#define IMG_IR_F_FILTER		0x00000001	/* enable filtering */
#define IMG_IR_F_WAKE		0x00000002	/* enable waking */

enum img_ir_mode {
	IMG_IR_M_NORMAL,
	IMG_IR_M_REPEATING,
#ifdef CONFIG_PM_SLEEP
	IMG_IR_M_WAKE,
#endif
};
#endif

/* code type quirks */

#define IMG_IR_QUIRK_CODE_BROKEN	0x1	/* Decode is broken */
#define IMG_IR_QUIRK_CODE_LEN_INCR	0x2	/* Bit length needs increment */

/**
 * struct img_ir_priv - Private driver data.
 * @next:		Next IR device's private driver data (to form a linked
 *			list).
 * @dev:		Platform device.
 * @irq:		IRQ number.
 * @reg_base:		Iomem base address of IR register block.
 * @lock:		Protects IR registers and variables in this struct.
 * @rdev:		Remote control device
 * @ct_quirks:		Quirk bits for each code type.
 * @last_status:	Last raw status bits.
 * @end_timer:		Timer until repeat timeout.
 * @clk_nb:		Notifier block for clock notify events.
 * @decoder:		Current decoder settings.
 * @enabled_protocols:	Currently enabled protocols.
 * @clk_hz:		Current clock rate in Hz.
 * @flags:		IMG_IR_F_*.
 * @filter:		HW filter for normal events (derived from sc_filter).
 * @wake_filter:	HW filter for wake event (derived from sc_wake_filter).
 * @sc_filter:		Current scancode filter.
 * @sc_wake_filter:	Current scancode filter for wake events.
 * @mode:		Current decode mode.
 */
struct img_ir_priv {
	/* this priv sits in a global list protected by img_ir_decoders_lock */
	struct img_ir_priv *next;

	struct device *dev;
	int irq;
	void __iomem *reg_base;
	spinlock_t lock;
	struct rc_dev *rdev;
	unsigned int ct_quirks[4];

#ifdef CONFIG_IR_IMG_RAW
	u32 last_status;
#else
	struct notifier_block clk_nb;
	struct timer_list end_timer;
	struct img_ir_decoder *decoder;
	u64 enabled_protocols;
	unsigned long clk_hz;
	unsigned int flags;
	struct img_ir_filter filter;
	struct img_ir_filter wake_filter;

	/* filters in terms of scancodes */
	struct img_ir_sc_filter sc_filter;
	struct img_ir_sc_filter sc_wake_filter;

	enum img_ir_mode mode;
#endif
};

/* Hardware access */

static inline void img_ir_write(struct img_ir_priv *priv,
				unsigned int reg_offs, unsigned int data)
{
	iowrite32(data, priv->reg_base + reg_offs);
}

static inline unsigned int img_ir_read(struct img_ir_priv *priv,
				       unsigned int reg_offs)
{
	return ioread32(priv->reg_base + reg_offs);
}

#ifndef CONFIG_IR_IMG_RAW

/* functions for preprocessing timings, ensuring max is set */

static void img_ir_timing_preprocess(struct img_ir_timing_range *range,
				     unsigned int unit)
{
	if (range->max < range->min)
		range->max = range->min;
	if (unit) {
		/* multiply by unit and convert to microseconds */
		range->min = (range->min*unit)/1000;
		range->max = (range->max*unit + 999)/1000; /* round up */
	}
}

static void img_ir_symbol_timing_preprocess(struct img_ir_symbol_timing *timing,
					    unsigned int unit)
{
	img_ir_timing_preprocess(&timing->pulse, unit);
	img_ir_timing_preprocess(&timing->space, unit);
}

static void img_ir_timings_preprocess(struct img_ir_timings *timings,
				      unsigned int unit)
{
	img_ir_symbol_timing_preprocess(&timings->ldr, unit);
	img_ir_symbol_timing_preprocess(&timings->s00, unit);
	img_ir_symbol_timing_preprocess(&timings->s01, unit);
	img_ir_symbol_timing_preprocess(&timings->s10, unit);
	img_ir_symbol_timing_preprocess(&timings->s11, unit);
	/* default s10 and s11 to s00 and s01 if no leader */
	if (unit)
		/* multiply by unit and convert to microseconds (round up) */
		timings->ft.ft_min = (timings->ft.ft_min*unit + 999)/1000;
}

/* functions for filling empty fields with defaults */

static void img_ir_timing_defaults(struct img_ir_timing_range *range,
				   struct img_ir_timing_range *defaults)
{
	if (!range->min)
		range->min = defaults->min;
	if (!range->max)
		range->max = defaults->max;
}

static void img_ir_symbol_timing_defaults(struct img_ir_symbol_timing *timing,
					  struct img_ir_symbol_timing *defaults)
{
	img_ir_timing_defaults(&timing->pulse, &defaults->pulse);
	img_ir_timing_defaults(&timing->space, &defaults->space);
}

static void img_ir_timings_defaults(struct img_ir_timings *timings,
				    struct img_ir_timings *defaults)
{
	img_ir_symbol_timing_defaults(&timings->ldr, &defaults->ldr);
	img_ir_symbol_timing_defaults(&timings->s00, &defaults->s00);
	img_ir_symbol_timing_defaults(&timings->s01, &defaults->s01);
	img_ir_symbol_timing_defaults(&timings->s10, &defaults->s10);
	img_ir_symbol_timing_defaults(&timings->s11, &defaults->s11);
	if (!timings->ft.ft_min)
		timings->ft.ft_min = defaults->ft.ft_min;
}

/* functions for converting timings to register values */

/**
 * img_ir_control() - Convert control struct to control register value.
 * @control:	Control data
 *
 * Returns:	The control register value equivalent of @control.
 */
static u32 img_ir_control(struct img_ir_control *control)
{
	u32 ctrl = control->code_type << IMG_IR_CODETYPE_SHIFT;
	if (control->decoden)
		ctrl |= IMG_IR_DECODEN;
	if (control->hdrtog)
		ctrl |= IMG_IR_HDRTOG;
	if (control->ldrdec)
		ctrl |= IMG_IR_LDRDEC;
	if (control->decodinpol)
		ctrl |= IMG_IR_DECODINPOL;
	if (control->bitorien)
		ctrl |= IMG_IR_BITORIEN;
	if (control->d1validsel)
		ctrl |= IMG_IR_D1VALIDSEL;
	if (control->bitinv)
		ctrl |= IMG_IR_BITINV;
	if (control->decodend2)
		ctrl |= IMG_IR_DECODEND2;
	if (control->bitoriend2)
		ctrl |= IMG_IR_BITORIEND2;
	if (control->bitinvd2)
		ctrl |= IMG_IR_BITINVD2;
	return ctrl;
}

/**
 * img_ir_timing_range_convert() - Convert microsecond range.
 * @out:	Output timing range in clock cycles with a shift.
 * @in:		Input timing range in microseconds.
 * @tolerance:	Tolerance as a fraction of 128 (roughly percent).
 * @clock_hz:	IR clock rate in Hz.
 * @shift:	Shift of output units.
 *
 * Converts min and max from microseconds to IR clock cycles, applies a
 * tolerance, and shifts for the register, rounding in the right direction.
 * Note that in and out can safely be the same object.
 */
static void img_ir_timing_range_convert(struct img_ir_timing_range *out,
					const struct img_ir_timing_range *in,
					unsigned int tolerance,
					unsigned long clock_hz,
					unsigned int shift)
{
	unsigned int min = in->min;
	unsigned int max = in->max;
	/* add a tolerance */
	min = min - (min*tolerance >> 7);
	max = max + (max*tolerance >> 7);
	/* convert from microseconds into clock cycles */
	min = min*clock_hz / 1000000;
	max = (max*clock_hz + 999999) / 1000000; /* round up */
	/* apply shift and copy to output */
	out->min = min >> shift;
	out->max = (max + ((1 << shift) - 1)) >> shift; /* round up */
}

/**
 * img_ir_symbol_timing() - Convert symbol timing struct to register value.
 * @timing:	Symbol timing data
 * @tolerance:	Timing tolerance where 0-128 represents 0-100%
 * @clock_hz:	Frequency of source clock in Hz
 * @pd_shift:	Shift to apply to symbol period
 * @w_shift:	Shift to apply to symbol width
 *
 * Returns:	Symbol timing register value based on arguments.
 */
static u32 img_ir_symbol_timing(const struct img_ir_symbol_timing *timing,
				unsigned int tolerance,
				unsigned long clock_hz,
				unsigned int pd_shift,
				unsigned int w_shift)
{
	struct img_ir_timing_range hw_pulse, hw_period;
	/* we calculate period in hw_period, then convert in place */
	hw_period.min = timing->pulse.min + timing->space.min;
	hw_period.max = timing->pulse.max + timing->space.max;
	img_ir_timing_range_convert(&hw_period, &hw_period,
			tolerance, clock_hz, pd_shift);
	img_ir_timing_range_convert(&hw_pulse, &timing->pulse,
			tolerance, clock_hz, w_shift);
	/* construct register value */
	return	(hw_period.max	<< IMG_IR_PD_MAX_SHIFT)	|
		(hw_period.min	<< IMG_IR_PD_MIN_SHIFT)	|
		(hw_pulse.max	<< IMG_IR_W_MAX_SHIFT)	|
		(hw_pulse.min	<< IMG_IR_W_MIN_SHIFT);
}

/**
 * img_ir_free_timing() - Convert free time timing struct to register value.
 * @timing:	Free symbol timing data
 * @clock_hz:	Source clock frequency in Hz
 *
 * Returns:	Free symbol timing register value.
 */
static u32 img_ir_free_timing(const struct img_ir_free_timing *timing,
			      unsigned long clock_hz)
{
	unsigned int minlen, maxlen, ft_min;
	/* minlen is only 5 bits, and round minlen to multiple of 2 */
	if (timing->minlen < 30)
		minlen = timing->minlen & -2;
	else
		minlen = 30;
	/* maxlen has maximum value of 48, and round maxlen to multiple of 2 */
	if (timing->maxlen < 48)
		maxlen = (timing->maxlen + 1) & -2;
	else
		maxlen = 48;
	/* convert and shift ft_min, rounding upwards */
	ft_min = (timing->ft_min*clock_hz + 999999) / 1000000;
	ft_min = (ft_min + 7) >> 3;
	/* construct register value */
	return	(timing->maxlen	<< IMG_IR_MAXLEN_SHIFT)	|
		(timing->minlen	<< IMG_IR_MINLEN_SHIFT)	|
		(ft_min		<< IMG_IR_FT_MIN_SHIFT);
}

/**
 * img_ir_free_timing_dynamic() - Update free time register value.
 * @st_ft:	Static free time register value from img_ir_free_timing.
 * @filter:	Current filter which may additionally restrict min/max len.
 *
 * Returns:	Updated free time register value based on the current filter.
 */
static u32 img_ir_free_timing_dynamic(u32 st_ft, struct img_ir_filter *filter)
{
	unsigned int minlen, maxlen, newminlen, newmaxlen;

	/* round minlen, maxlen to multiple of 2 */
	newminlen = filter->minlen & -2;
	newmaxlen = (filter->maxlen + 1) & -2;
	/* extract min/max len from register */
	minlen = (st_ft & IMG_IR_MINLEN) >> IMG_IR_MINLEN_SHIFT;
	maxlen = (st_ft & IMG_IR_MAXLEN) >> IMG_IR_MAXLEN_SHIFT;
	/* if the new values are more restrictive, update the register value */
	if (newminlen > minlen) {
		st_ft &= ~IMG_IR_MINLEN;
		st_ft |= newminlen << IMG_IR_MINLEN_SHIFT;
	}
	if (newmaxlen < maxlen) {
		st_ft &= ~IMG_IR_MAXLEN;
		st_ft |= newmaxlen << IMG_IR_MAXLEN_SHIFT;
	}
	return st_ft;
}

/**
 * img_ir_timings_convert() - Convert timings to register values
 * @regs:	Output timing register values
 * @timings:	Input timing data
 * @tolerance:	Timing tolerance where 0-128 represents 0-100%
 * @clock_hz:	Source clock frequency in Hz
 */
static void img_ir_timings_convert(struct img_ir_timing_regvals *regs,
				   const struct img_ir_timings *timings,
				   unsigned int tolerance,
				   unsigned int clock_hz)
{
	/* leader symbol timings are divided by 16 */
	regs->ldr = img_ir_symbol_timing(&timings->ldr, tolerance, clock_hz,
			4, 4);
	/* other symbol timings, pd fields only are divided by 2 */
	regs->s00 = img_ir_symbol_timing(&timings->s00, tolerance, clock_hz,
			1, 0);
	regs->s01 = img_ir_symbol_timing(&timings->s01, tolerance, clock_hz,
			1, 0);
	regs->s10 = img_ir_symbol_timing(&timings->s10, tolerance, clock_hz,
			1, 0);
	regs->s11 = img_ir_symbol_timing(&timings->s11, tolerance, clock_hz,
			1, 0);
	regs->ft = img_ir_free_timing(&timings->ft, clock_hz);
}

/**
 * img_ir_decoder_preprocess() - Preprocess timings in decoder.
 * @decoder:	Decoder to be preprocessed.
 *
 * Ensures that the symbol timing ranges are valid with respect to ordering, and
 * does some fixed conversion on them.
 */
static void img_ir_decoder_preprocess(struct img_ir_decoder *decoder)
{
	/* fill in implicit fields */
	img_ir_timings_preprocess(&decoder->timings, decoder->unit);

	/* do the same for repeat timings if applicable */
	if (decoder->repeat) {
		img_ir_timings_preprocess(&decoder->rtimings, decoder->unit);
		img_ir_timings_defaults(&decoder->rtimings, &decoder->timings);
	}

	/* calculate control value */
	decoder->reg_ctrl = img_ir_control(&decoder->control);
}

/**
 * img_ir_decoder_convert() - Generate internal timings in decoder.
 * @decoder:	Decoder to be converted to internal timings.
 * @tolerance:	Tolerance as percent.
 * @clock_hz:	IR clock rate in Hz.
 *
 * Fills out the repeat timings and timing register values for a specific
 * tolerance and clock rate.
 */
static void img_ir_decoder_convert(struct img_ir_decoder *decoder,
				   unsigned int tolerance,
				   unsigned int clock_hz)
{
	tolerance = tolerance * 128 / 100;

	/* record clock rate in case timings need recalculating */
	decoder->clk_hz = clock_hz;

	/* fill in implicit fields and calculate register values */
	img_ir_timings_convert(&decoder->reg_timings, &decoder->timings,
			       tolerance, clock_hz);

	/* do the same for repeat timings if applicable */
	if (decoder->repeat)
		img_ir_timings_convert(&decoder->reg_rtimings,
				       &decoder->rtimings, tolerance, clock_hz);
}

/**
 * img_ir_decoder_check_timings() - Check if decoder timings need updating.
 * @priv:	IR private data.
 */
static void img_ir_check_timings(struct img_ir_priv *priv)
{
	struct img_ir_decoder *dec = priv->decoder;
	if (dec->clk_hz != priv->clk_hz) {
		dev_dbg(priv->dev, "converting tolerance=%d%%, clk=%lu\n",
			10, priv->clk_hz);
		img_ir_decoder_convert(dec, 10, priv->clk_hz);
	}
}

/**
 * img_ir_write_timings() - Write timings to the hardware now
 * @priv:	IR private data
 * @regs:	Timing register values to write
 * @filter:	Current filter data or NULL
 *
 * Write timing register values @regs to the hardware, taking into account the
 * current filter pointed to by @filter which may impose restrictions on the
 * length of the expected data.
 */
static void img_ir_write_timings(struct img_ir_priv *priv,
				 struct img_ir_timing_regvals *regs,
				 struct img_ir_filter *filter)
{
	/* filter may be more restrictive to minlen, maxlen */
	u32 ft = regs->ft;
	if (filter)
		ft = img_ir_free_timing_dynamic(regs->ft, filter);
	/* write to registers */
	img_ir_write(priv, IMG_IR_LEAD_SYMB_TIMING, regs->ldr);
	img_ir_write(priv, IMG_IR_S00_SYMB_TIMING, regs->s00);
	img_ir_write(priv, IMG_IR_S01_SYMB_TIMING, regs->s01);
	img_ir_write(priv, IMG_IR_S10_SYMB_TIMING, regs->s10);
	img_ir_write(priv, IMG_IR_S11_SYMB_TIMING, regs->s11);
	img_ir_write(priv, IMG_IR_FREE_SYMB_TIMING, ft);
	dev_dbg(priv->dev, "timings: ldr=%#x, s=[%#x, %#x, %#x, %#x], ft=%#x\n",
		regs->ldr, regs->s00, regs->s01, regs->s10, regs->s11, ft);
}

/**
 * img_ir_write_timings_normal() - Write normal timings to the hardware now
 * @priv:	IR private data
 * @regs:	Normal timing register values to write
 *
 * Write the normal (non-wake) timing register values @regs to the hardware,
 * taking into account the current filter.
 */
static void img_ir_write_timings_normal(struct img_ir_priv *priv,
					struct img_ir_timing_regvals *regs)
{
	struct img_ir_filter *filter = NULL;
	if (priv->flags & IMG_IR_F_FILTER)
		filter = &priv->filter;
	img_ir_write_timings(priv, regs, filter);
}

#ifdef CONFIG_PM_SLEEP
/**
 * img_ir_write_timings_wake() - Write wake timings to the hardware now
 * @priv:	IR private data
 * @regs:	Wake timing register values to write
 *
 * Write the wake timing register values @regs to the hardware, taking into
 * account the current wake filter.
 */
static void img_ir_write_timings_wake(struct img_ir_priv *priv,
				      struct img_ir_timing_regvals *regs)
{
	struct img_ir_filter *filter = NULL;
	if (priv->flags & IMG_IR_F_WAKE)
		filter = &priv->wake_filter;
	img_ir_write_timings(priv, regs, filter);
}
#endif

static void img_ir_write_filter(struct img_ir_priv *priv,
				struct img_ir_filter *filter)
{
	if (filter) {
		dev_dbg(priv->dev, "IR filter=%016llx & %016llx\n",
			(unsigned long long)filter->data,
			(unsigned long long)filter->mask);
		img_ir_write(priv, IMG_IR_IRQ_MSG_DATA_LW, (u32)filter->data);
		img_ir_write(priv, IMG_IR_IRQ_MSG_DATA_UP, (u32)(filter->data
									>> 32));
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_LW, (u32)filter->mask);
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_UP, (u32)(filter->mask
									>> 32));
	} else {
		dev_dbg(priv->dev, "IR clearing filter\n");
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_LW, 0);
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_UP, 0);
	}
}

/* caller must have lock */
static void _img_ir_set_filter(struct img_ir_priv *priv,
			       struct img_ir_filter *filter)
{
	u32 irq_en, irq_on;

	irq_en = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
	if (filter) {
		/* Only use the match interrupt */
		priv->filter = *filter;
		priv->flags |= IMG_IR_F_FILTER;
		irq_on = IMG_IR_IRQ_DATA_MATCH;
		irq_en &= ~(IMG_IR_IRQ_DATA_VALID | IMG_IR_IRQ_DATA2_VALID);
	} else {
		/* Only use the valid interrupt */
		priv->flags &= ~IMG_IR_F_FILTER;
		irq_en &= ~IMG_IR_IRQ_DATA_MATCH;
		irq_on = IMG_IR_IRQ_DATA_VALID | IMG_IR_IRQ_DATA2_VALID;
	}
	irq_en |= irq_on;

	img_ir_write_filter(priv, filter);
	/* clear any interrupts we're enabling so we don't handle old ones */
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, irq_on);
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, irq_en);
}

/* caller must have lock */
static void _img_ir_set_wake_filter(struct img_ir_priv *priv,
				    struct img_ir_filter *filter)
{
	if (filter) {
		/* Enable wake, and copy filter for later */
		priv->wake_filter = *filter;
		priv->flags |= IMG_IR_F_WAKE;
	} else {
		/* Disable wake */
		priv->flags &= ~IMG_IR_F_WAKE;
	}
}

/* caller must have lock */
static void _img_ir_update_filters(struct img_ir_priv *priv)
{
	struct img_ir_filter filter;
	int ret1 = -1, ret2 = -1;

	/* clear raw filters */
	_img_ir_set_filter(priv, NULL);
	_img_ir_set_wake_filter(priv, NULL);

	/* convert scancode filters to raw filters and try to set them */
	if (priv->decoder && priv->decoder->filter) {
		if (priv->sc_filter.mask) {
			filter.minlen = 0;
			filter.maxlen = ~0;
			dev_dbg(priv->dev, "IR scancode filter=%08x & %08x\n",
				priv->sc_filter.data,
				priv->sc_filter.mask);
			ret1 = priv->decoder->filter(&priv->sc_filter, &filter,
						     priv->enabled_protocols);
			if (!ret1) {
				dev_dbg(priv->dev, "IR raw filter=%016llx & %016llx\n",
					(unsigned long long)filter.data,
					(unsigned long long)filter.mask);
				_img_ir_set_filter(priv, &filter);
			}
		}
		if (priv->sc_wake_filter.mask) {
			filter.minlen = 0;
			filter.maxlen = ~0;
			dev_dbg(priv->dev, "IR scancode wake filter=%08x & %08x\n",
				priv->sc_wake_filter.data,
				priv->sc_wake_filter.mask);
			ret2 = priv->decoder->filter(&priv->sc_wake_filter,
						     &filter,
						     priv->enabled_protocols);
			if (!ret2) {
				dev_dbg(priv->dev, "IR raw wake filter=%016llx & %016llx\n",
					(unsigned long long)filter.data,
					(unsigned long long)filter.mask);
				_img_ir_set_wake_filter(priv, &filter);
			}
		}
	}

	/*
	 * if either of the filters couldn't get set, clear the corresponding
	 * scancode filter mask.
	 */
	if (ret1)
		priv->sc_filter.mask = 0;
	if (ret2)
		priv->sc_wake_filter.mask = 0;
}

static void img_ir_update_filters(struct img_ir_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	_img_ir_update_filters(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

/**
 * img_ir_set_decoder() - Set the current decoder.
 * @priv:	IR private data.
 * @decoder:	Decoder to use with immediate effect.
 * @proto:	Protocol bitmap (or 0 to use decoder->type).
 */
static void img_ir_set_decoder(struct img_ir_priv *priv,
			       struct img_ir_decoder *decoder,
			       u64 proto)
{
	unsigned long flags;
	u32 ir_status;
	spin_lock_irqsave(&priv->lock, flags);

	/* switch off and disable interrupts */
	img_ir_write(priv, IMG_IR_CONTROL, 0);
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, 0);
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, IMG_IR_IRQ_ALL);

	/* ack any data already detected */
	ir_status = img_ir_read(priv, IMG_IR_STATUS);
	if (ir_status & (IMG_IR_RXDVAL | IMG_IR_RXDVALD2)) {
		ir_status &= ~(IMG_IR_RXDVAL | IMG_IR_RXDVALD2);
		img_ir_write(priv, IMG_IR_STATUS, ir_status);
	}

	/* always read data to clear buffer if IR wakes the device */
	img_ir_read(priv, IMG_IR_DATA_LW);
	img_ir_read(priv, IMG_IR_DATA_UP);

	/* clear the scancode filters */
	priv->sc_filter.data = 0;
	priv->sc_filter.mask = 0;
	priv->sc_wake_filter.data = 0;
	priv->sc_wake_filter.mask = 0;

	/* update (clear) the raw filters */
	_img_ir_update_filters(priv);

	/* clear the enabled protocols */
	priv->enabled_protocols = 0;

	/* switch decoder */
	priv->decoder = decoder;
	if (!decoder) {
		goto unlock;
	}

	priv->mode = IMG_IR_M_NORMAL;

	/* set the enabled protocols */
	if (!proto)
		proto = decoder->type;
	priv->enabled_protocols = proto;

	/* write the new timings */
	img_ir_check_timings(priv);
	img_ir_write_timings_normal(priv, &decoder->reg_timings);

	/* set up and enable */
	img_ir_write(priv, IMG_IR_CONTROL, decoder->reg_ctrl);


unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
}

/**
 * img_ir_decoder_compatable() - Find whether a decoder will work with a device.
 * @priv:	IR private data.
 * @dec:	Decoder to check.
 *
 * Returns:	true if @dec is compatible with the device @priv refers to.
 */
static bool img_ir_decoder_compatible(struct img_ir_priv *priv,
				      struct img_ir_decoder *dec)
{
	unsigned int ct;

	/* don't accept decoders using code types which aren't supported */
	ct = dec->control.code_type;
	if (priv->ct_quirks[ct] & IMG_IR_QUIRK_CODE_BROKEN)
		return false;

	return true;
}

/**
 * img_ir_allowed_protos() - Get allowed protocols from global decoder list.
 * @priv:	IR private data.
 *
 * img_ir_decoders_lock must be locked by caller.
 *
 * Returns:	Mask of protocols supported by the device @priv refers to.
 */
static unsigned long img_ir_allowed_protos(struct img_ir_priv *priv)
{
	unsigned long protos = 0;
	struct img_ir_decoder *dec;

	for (dec = img_ir_decoders; dec; dec = dec->next)
		if (img_ir_decoder_compatible(priv, dec))
			protos |= dec->type;
	return protos;
}

/**
 * img_ir_update_allowed_protos() - Update devices with allowed protocols.
 *
 * img_ir_decoders_lock must be locked by caller.
 */
static void img_ir_update_allowed_protos(void)
{
	struct img_ir_priv *priv;

	for (priv = img_ir_privs; priv; priv = priv->next)
		priv->rdev->allowed_protos = img_ir_allowed_protos(priv);
}

/* Callback for changing protocol using sysfs */
static int img_ir_change_protocol(struct rc_dev *data, u64 *ir_type)
{
	struct img_ir_priv *priv;
	struct img_ir_decoder *dec;
	int ret = -EINVAL;
	priv = data->priv;

	spin_lock(&img_ir_decoders_lock);
	for (dec = img_ir_decoders; dec; dec = dec->next) {
		if (!img_ir_decoder_compatible(priv, dec))
			continue;
		if (*ir_type & dec->type) {
			*ir_type &= dec->type;
			img_ir_set_decoder(priv, dec, *ir_type);
			ret = 0;
			break;
		}
	}
	spin_unlock(&img_ir_decoders_lock);
	return ret;
}

/* Changes ir-core protocol device attribute */
static void img_ir_set_protocol(struct img_ir_priv *priv, u64 proto)
{
	struct rc_dev *ir_dev = priv->rdev;

	unsigned long flags;

	spin_lock_irqsave(&ir_dev->rc_map.lock, flags);
	ir_dev->rc_map.rc_type = proto;
	spin_unlock_irqrestore(&ir_dev->rc_map.lock, flags);
}

/* Register an ir decoder */
int img_ir_register_decoder(struct img_ir_decoder *dec)
{
	struct img_ir_priv *priv;

	/* first preprocess decoder timings */
	img_ir_decoder_preprocess(dec);

	spin_lock(&img_ir_decoders_lock);
	/* add to list */
	dec->next = img_ir_decoders;
	img_ir_decoders = dec;
	img_ir_update_allowed_protos();
	/* if it's the first decoder, start using it */
	for (priv = img_ir_privs; priv; priv = priv->next) {
		if (!priv->decoder && img_ir_decoder_compatible(priv, dec)) {
			img_ir_set_protocol(priv, dec->type);
			img_ir_set_decoder(priv, dec, 0);
		}
	}
	spin_unlock(&img_ir_decoders_lock);
	return 0;
}
EXPORT_SYMBOL(img_ir_register_decoder);

/* Unregister an ir decoder */
void img_ir_unregister_decoder(struct img_ir_decoder *dec)
{
	struct img_ir_priv *priv;

	spin_lock(&img_ir_decoders_lock);
	/* If the decoder is in use, stop it now */
	for (priv = img_ir_privs; priv; priv = priv->next)
		if (priv->decoder == dec) {
			img_ir_set_protocol(priv, 0);
			img_ir_set_decoder(priv, NULL, 0);
		}
	/* Remove from list of decoders */
	if (img_ir_decoders == dec) {
		img_ir_decoders = dec->next;
	} else {
		struct img_ir_decoder *cur;
		for (cur = img_ir_decoders; cur; cur = cur->next)
			if (dec == cur->next) {
				cur->next = dec->next;
				dec->next = NULL;
				break;
			}
	}
	img_ir_update_allowed_protos();
	spin_unlock(&img_ir_decoders_lock);
}
EXPORT_SYMBOL(img_ir_unregister_decoder);

static void img_ir_register_device(struct img_ir_priv *priv)
{
	spin_lock(&img_ir_decoders_lock);
	priv->next = img_ir_privs;
	img_ir_privs = priv;
	priv->rdev->allowed_protos = img_ir_allowed_protos(priv);
	spin_unlock(&img_ir_decoders_lock);
}

static void img_ir_unregister_device(struct img_ir_priv *priv)
{
	struct img_ir_priv *cur;

	spin_lock(&img_ir_decoders_lock);
	if (img_ir_privs == priv)
		img_ir_privs = priv->next;
	else
		for (cur = img_ir_privs; cur; cur = cur->next)
			if (cur->next == priv) {
				cur->next = priv->next;
				break;
			}
	spin_unlock(&img_ir_decoders_lock);
}

#ifdef CONFIG_PM_SLEEP
/**
 * img_ir_enable_wake() - Switch to wake mode.
 * @priv:	IR private data.
 *
 * Returns:	non-zero if the IR can wake the system.
 */
static int img_ir_enable_wake(struct img_ir_priv *priv)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->flags & IMG_IR_F_WAKE) {
		/* interrupt only on a match */
		img_ir_write(priv, IMG_IR_IRQ_ENABLE, IMG_IR_IRQ_DATA_MATCH);
		img_ir_write_filter(priv, &priv->wake_filter);
		img_ir_write_timings_wake(priv, &priv->decoder->reg_timings);
		priv->mode = IMG_IR_M_WAKE;
		ret = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}

/**
 * img_ir_disable_wake() - Switch out of wake mode.
 * @priv:	IR private data
 *
 * Returns:	1 if the hardware should be allowed to wake from a sleep state.
 *		0 otherwise.
 */
static int img_ir_disable_wake(struct img_ir_priv *priv)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->flags & IMG_IR_F_WAKE) {
		/* restore normal filtering */
		if (priv->flags & IMG_IR_F_FILTER) {
			img_ir_write(priv, IMG_IR_IRQ_ENABLE,
				     IMG_IR_IRQ_DATA_MATCH);
			img_ir_write_filter(priv, &priv->filter);
		} else {
			img_ir_write(priv, IMG_IR_IRQ_ENABLE,
				     IMG_IR_IRQ_DATA_VALID |
				     IMG_IR_IRQ_DATA2_VALID);
			img_ir_write_filter(priv, NULL);
		}
		img_ir_write_timings_normal(priv, &priv->decoder->reg_timings);
		priv->mode = IMG_IR_M_NORMAL;
		ret = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}
#endif

/* lock must be held */
static void img_ir_begin_repeat(struct img_ir_priv *priv)
{
	if (priv->mode == IMG_IR_M_NORMAL) {
		struct img_ir_decoder *dec = priv->decoder;

		/* switch to repeat timings */
		img_ir_write(priv, IMG_IR_CONTROL, 0);
		priv->mode = IMG_IR_M_REPEATING;
		img_ir_write_timings_normal(priv, &dec->reg_rtimings);
		img_ir_write(priv, IMG_IR_CONTROL, dec->reg_ctrl);
	}
}

/* lock must be held */
static void img_ir_end_repeat(struct img_ir_priv *priv)
{
	if (priv->mode == IMG_IR_M_REPEATING) {
		struct img_ir_decoder *dec = priv->decoder;

		/* switch to normal timings */
		img_ir_write(priv, IMG_IR_CONTROL, 0);
		priv->mode = IMG_IR_M_NORMAL;
		img_ir_write_timings_normal(priv, &dec->reg_timings);
		img_ir_write(priv, IMG_IR_CONTROL, dec->reg_ctrl);
	}
}

/* lock must be held */
static void img_ir_handle_data(struct img_ir_priv *priv, u32 len, u64 raw)
{
	struct img_ir_decoder *dec = priv->decoder;
	int scancode = IMG_IR_ERR_INVALID;
	if (dec->scancode)
		scancode = dec->scancode(len, raw, priv->enabled_protocols);
	else if (len >= 32)
		scancode = (u32)raw;
	else if (len < 32)
		scancode = (u32)raw & ((1 << len)-1);
	dev_dbg(priv->dev, "data (%u bits) = %#llx\n",
		len, (unsigned long long)raw);
	if (scancode >= 0) {
		dev_dbg(priv->dev, "decoded scan code %#x\n", scancode);
		rc_keydown(priv->rdev, scancode, 0);
		img_ir_end_repeat(priv);
	} else if (scancode == IMG_IR_REPEATCODE) {
		if (priv->mode == IMG_IR_M_REPEATING) {
			dev_dbg(priv->dev, "decoded repeat code\n");
			rc_repeat(priv->rdev);
		} else {
			dev_dbg(priv->dev, "decoded unexpected repeat code, ignoring\n");
		}
	} else {
		return;
	}


	if (dec->repeat) {
		unsigned long interval;

		img_ir_begin_repeat(priv);

		/* update timer, but allowing for 1/8th tolerance */
		interval = dec->repeat + (dec->repeat >> 3);
		mod_timer(&priv->end_timer,
			  jiffies + msecs_to_jiffies(interval));
	}
}

/* timer function to end waiting for repeat. */
static void img_ir_end_timer(unsigned long arg)
{
	unsigned long flags;
	struct img_ir_priv *priv = (struct img_ir_priv *)arg;

	spin_lock_irqsave(&priv->lock, flags);
	img_ir_end_repeat(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

#else /* CONFIG_IR_IMG_RAW */

static void img_ir_register_device(struct img_ir_priv *priv)
{
}

static void img_ir_unregister_device(struct img_ir_priv *priv)
{
}
#endif

static irqreturn_t img_ir_isr(int irq, void *dev_id)
{
	struct img_ir_priv *priv = dev_id;
	u32 irq_status, ir_status;

	spin_lock(&priv->lock);
	/* we have to clear irqs before reading */
	irq_status = img_ir_read(priv, IMG_IR_IRQ_STATUS);
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, irq_status);

#ifdef CONFIG_IR_IMG_RAW
	/* only report the raw signal changes */
	if (irq_status & IMG_IR_IRQ_EDGE) {
		struct rc_dev *rc_dev = priv->rdev;
		int multiple;

		/* find whether both rise and fall was detected */
		multiple = ((irq_status & IMG_IR_IRQ_EDGE) == IMG_IR_IRQ_EDGE);
		/*
		 * If so, we need to see if the level has actually changed.
		 * If it's just noise that we didn't have time to process,
		 * there's no point reporting it.
		 */
		ir_status = img_ir_read(priv, IMG_IR_STATUS) & IMG_IR_IRRXD;
		if (multiple && !(ir_status ^ priv->last_status))
			goto unlock;
		priv->last_status = ir_status;

		/* report the edge to the IR raw decoders */
		if (ir_status) /* low */
			ir_raw_event_store_edge(rc_dev, IR_SPACE);
		else /* high */
			ir_raw_event_store_edge(rc_dev, IR_PULSE);
		ir_raw_event_handle(priv->rdev);
	}
#else
	/* use the decoders */
	if (priv->decoder &&
	    (irq_status & (IMG_IR_IRQ_DATA_MATCH |
			   IMG_IR_IRQ_DATA_VALID |
			   IMG_IR_IRQ_DATA2_VALID))) {
		u32 len, lw, up;
		unsigned int ct;
		ir_status = img_ir_read(priv, IMG_IR_STATUS);
		if (!(ir_status & (IMG_IR_RXDVAL | IMG_IR_RXDVALD2)))
			goto unlock;
		ir_status &= ~(IMG_IR_RXDVAL | IMG_IR_RXDVALD2);
		img_ir_write(priv, IMG_IR_STATUS, ir_status);

		len = (ir_status & IMG_IR_RXDLEN) >> IMG_IR_RXDLEN_SHIFT;
		/* some versions report wrong length for certain code types */
		ct = priv->decoder->control.code_type;
		if (priv->ct_quirks[ct] & IMG_IR_QUIRK_CODE_LEN_INCR)
			++len;

		lw = img_ir_read(priv, IMG_IR_DATA_LW);
		up = img_ir_read(priv, IMG_IR_DATA_UP);
		img_ir_handle_data(priv, len, (u64)up << 32 | lw);
	}
#endif
unlock:
	spin_unlock(&priv->lock);
	return IRQ_HANDLED;
}

static void img_ir_setup(struct img_ir_priv *priv)
{
#ifdef CONFIG_IR_IMG_RAW
	/* raw mode, just enable the interrupts */
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, IMG_IR_IRQ_EDGE);
#else
	struct img_ir_decoder *dec;
	spin_lock(&img_ir_decoders_lock);
	/* Use the first available decoder (or disable stuff if NULL) */
	for (dec = img_ir_decoders; dec; dec = dec->next) {
		if (img_ir_decoder_compatible(priv, dec)) {
			img_ir_set_protocol(priv, dec->type);
			img_ir_set_decoder(priv, dec, 0);
			goto unlock;
		}
	}
	img_ir_set_decoder(priv, NULL, 0);
unlock:
	spin_unlock(&img_ir_decoders_lock);
#endif
}

/**
 * img_ir_probe_caps() - Probe capabilities of the hardware.
 * @priv:	IR private data.
 */
static void img_ir_probe_caps(struct img_ir_priv *priv)
{
	/*
	 * When a version of the block becomes available without these quirks,
	 * they'll have to depend on the core revision.
	 */
	priv->ct_quirks[IMG_IR_CODETYPE_PULSELEN]
		|= IMG_IR_QUIRK_CODE_LEN_INCR;
	priv->ct_quirks[IMG_IR_CODETYPE_BIPHASE]
		|= IMG_IR_QUIRK_CODE_BROKEN;
	priv->ct_quirks[IMG_IR_CODETYPE_2BITPULSEPOS]
		|= IMG_IR_QUIRK_CODE_BROKEN;
}

static void img_ir_ident(struct img_ir_priv *priv)
{
	u32 core_rev = img_ir_read(priv, IMG_IR_CORE_REV);

	dev_info(priv->dev,
		 "IMG IR Decoder (%d.%d.%d.%d) probed successfully\n",
		 (core_rev & IMG_IR_DESIGNER) >> IMG_IR_DESIGNER_SHIFT,
		 (core_rev & IMG_IR_MAJOR_REV) >> IMG_IR_MAJOR_REV_SHIFT,
		 (core_rev & IMG_IR_MINOR_REV) >> IMG_IR_MINOR_REV_SHIFT,
		 (core_rev & IMG_IR_MAINT_REV) >> IMG_IR_MAINT_REV_SHIFT);
#ifdef CONFIG_IR_IMG_RAW
	dev_info(priv->dev, "IMG IR Decoder in raw mode\n");
#endif
}

/* Kernel interface */

#ifndef CONFIG_IR_IMG_RAW

static ssize_t img_ir_filter_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;

	return sprintf(buf, "%#x\n", priv->sc_filter.data);
}
static ssize_t img_ir_filter_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	priv->sc_filter.data = val;
	img_ir_update_filters(priv);
	return count;
}
static DEVICE_ATTR(filter, S_IRUGO|S_IWUSR,
		   img_ir_filter_show,
		   img_ir_filter_store);

static ssize_t img_ir_filter_mask_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;

	return sprintf(buf, "%#x\n", priv->sc_filter.mask);
}
static ssize_t img_ir_filter_mask_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	priv->sc_filter.mask = val;
	img_ir_update_filters(priv);
	return count;
}
static DEVICE_ATTR(filter_mask, S_IRUGO|S_IWUSR,
		   img_ir_filter_mask_show,
		   img_ir_filter_mask_store);

static ssize_t img_ir_wakeup_filter_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;

	return sprintf(buf, "%#x\n", priv->sc_wake_filter.data);
}
static ssize_t img_ir_wakeup_filter_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	priv->sc_wake_filter.data = val;
	img_ir_update_filters(priv);
	return count;
}
static DEVICE_ATTR(wakeup_filter, S_IRUGO|S_IWUSR,
		   img_ir_wakeup_filter_show,
		   img_ir_wakeup_filter_store);

static ssize_t img_ir_wakeup_filter_mask_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;

	return sprintf(buf, "%#x\n", priv->sc_wake_filter.mask);
}
static ssize_t img_ir_wakeup_filter_mask_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct rc_dev *rdev = container_of(dev, struct rc_dev, dev);
	struct img_ir_priv *priv = rdev->priv;
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	priv->sc_wake_filter.mask = val;
	img_ir_update_filters(priv);
	return count;
}
static DEVICE_ATTR(wakeup_filter_mask, S_IRUGO|S_IWUSR,
		   img_ir_wakeup_filter_mask_show,
		   img_ir_wakeup_filter_mask_store);

static void img_ir_attr_create(struct device *dev)
{
	device_create_file(dev, &dev_attr_filter);
	device_create_file(dev, &dev_attr_filter_mask);
	device_create_file(dev, &dev_attr_wakeup_filter);
	device_create_file(dev, &dev_attr_wakeup_filter_mask);
}

static void img_ir_attr_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_filter);
	device_remove_file(dev, &dev_attr_filter_mask);
	device_remove_file(dev, &dev_attr_wakeup_filter);
	device_remove_file(dev, &dev_attr_wakeup_filter_mask);
}

static void img_ir_change_frequency(struct img_ir_priv *priv,
				    struct clk32k_change_freq *change)
{
	struct img_ir_decoder *dec;
	unsigned long flags;

	dev_dbg(priv->dev, "clk changed %lu HZ -> %lu HZ\n",
		change->old_freq, change->new_freq);

	spin_lock_irqsave(&priv->lock, flags);
	dec = priv->decoder;
	priv->clk_hz = change->new_freq;
	/* refresh current timings */
	if (priv->decoder) {
		img_ir_check_timings(priv);
		switch (priv->mode) {
		case IMG_IR_M_NORMAL:
			img_ir_write_timings_normal(priv, &dec->reg_timings);
			break;
		case IMG_IR_M_REPEATING:
			img_ir_write_timings_normal(priv, &dec->reg_rtimings);
			break;
#ifdef CONFIG_PM_SLEEP
		case IMG_IR_M_WAKE:
			img_ir_write_timings_wake(priv, &dec->reg_timings);
			break;
#endif
		}
	}
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int img_ir_clk_notify(struct notifier_block *self, unsigned long action,
			     void *data)
{
	struct img_ir_priv *priv;

	priv = container_of(self, struct img_ir_priv, clk_nb);
	switch (action) {
	case CLK32K_CHANGE_FREQUENCY:
		img_ir_change_frequency(priv, data);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

#else /* CONFIG_IR_IMG_RAW */

static void img_ir_attr_create(struct device *dev)
{
}

static void img_ir_attr_remove(struct device *dev)
{
}

#endif

static int img_ir_probe(struct platform_device *pdev)
{
	struct img_ir_priv *priv;
	struct resource *res_regs;
	int irq, error;

	/* Get resources from platform device */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ resource\n");
		return irq;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_regs == NULL) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		return -ENOENT;
	}

	/* Private driver data */
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);
	priv->dev = &pdev->dev;
	spin_lock_init(&priv->lock);

	/* Ioremap the registers */
	priv->reg_base = devm_ioremap(&pdev->dev, res_regs->start,
				 res_regs->end - res_regs->start);
	if (!priv->reg_base)
		return -EIO;

#ifndef CONFIG_IR_IMG_RAW
	/* Set up the end timer */
	init_timer(&priv->end_timer);
	priv->end_timer.function = img_ir_end_timer;
	priv->end_timer.data = (unsigned long)priv;

	/* Register a clock notifier */
	priv->clk_hz = get_32kclock();
	priv->clk_nb.notifier_call = img_ir_clk_notify;
	error = clk32k_register_notify(&priv->clk_nb);
	if (error) {
		dev_err(&pdev->dev, "failed to register clk32k notifier\n");
		return error;
	}
#endif

	priv->rdev = rc_allocate_device();
	if (!priv->rdev) {
		dev_err(&pdev->dev, "cannot allocate input device\n");
		error = -ENOMEM;
		goto err_unregister_clk_notify;
	}
	priv->rdev->priv = priv;
	img_ir_probe_caps(priv);
	priv->rdev->map_name = RC_MAP_EMPTY;
	priv->rdev->input_name = "IMG Infrared Decoder";
#ifdef CONFIG_IR_IMG_RAW
	priv->rdev->driver_type = RC_DRIVER_IR_RAW;
#endif
	/* img_ir_register_device sets rdev->allowed_protos. */
	img_ir_register_device(priv);

	error = rc_register_device(priv->rdev);
	if (error) {
		dev_err(&pdev->dev, "failed to register IR input device\n");
		goto err_input_reg;
	}
#ifndef CONFIG_IR_IMG_RAW
	/*
	 * Set this after rc_register_device as no protocols have been
	 * registered yet.
	 */
	priv->rdev->change_protocol = img_ir_change_protocol;

	device_init_wakeup(&pdev->dev, 1);
#endif

	/* Create custom sysfs attributes */
	img_ir_attr_create(&priv->rdev->dev);

	/* Get the IRQ */
	priv->irq = irq;
	error = devm_request_irq(&pdev->dev, priv->irq, img_ir_isr, 0, "img-ir", priv);
	if (error) {
		dev_err(&pdev->dev, "cannot register IRQ %u\n",
			priv->irq);
		error = -EIO;
		goto err_irq;
	}

	img_ir_ident(priv);
	img_ir_setup(priv);

	return 0;

err_irq:
	img_ir_attr_remove(&priv->rdev->dev);
	rc_unregister_device(priv->rdev);
	goto err_unregister_dev;
err_input_reg:
	rc_free_device(priv->rdev);
err_unregister_dev:
	img_ir_unregister_device(priv);
err_unregister_clk_notify:
#ifndef CONFIG_IR_IMG_RAW
	clk32k_unregister_notify(&priv->clk_nb);
#endif

	return error;
}

static int img_ir_remove(struct platform_device *pdev)
{
	struct img_ir_priv *priv = platform_get_drvdata(pdev);

#ifndef CONFIG_IR_IMG_RAW
	del_timer_sync(&priv->end_timer);
#endif
	img_ir_attr_remove(&priv->rdev->dev);
	rc_unregister_device(priv->rdev);
	img_ir_unregister_device(priv);
#ifndef CONFIG_IR_IMG_RAW
	clk32k_unregister_notify(&priv->clk_nb);
#endif

	return 0;
}

#if defined(CONFIG_PM_SLEEP) && !defined(CONFIG_IR_IMG_RAW)
static int img_ir_suspend(struct device *dev)
{
	struct img_ir_priv *priv = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && img_ir_enable_wake(priv))
		enable_irq_wake(priv->irq);
	return 0;
}

static int img_ir_resume(struct device *dev)
{
	struct img_ir_priv *priv = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && img_ir_disable_wake(priv))
		disable_irq_wake(priv->irq);
	return 0;
}
#else
#define img_ir_suspend NULL
#define img_ir_resume NULL
#endif	/* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(img_ir_pmops, img_ir_suspend, img_ir_resume);

static const struct of_device_id img_ir_match[] = {
	{ .compatible = "img,ir" },
	{}
};
MODULE_DEVICE_TABLE(of, img_ir_match);

static struct platform_driver img_ir_driver = {
	.driver = {
		.name = "img-ir",
		.owner	= THIS_MODULE,
		.of_match_table	= img_ir_match,
		.pm = &img_ir_pmops,
	},
	.probe = img_ir_probe,
	.remove = img_ir_remove,
};

module_platform_driver(img_ir_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("ImgTec IR");
MODULE_LICENSE("GPL");
