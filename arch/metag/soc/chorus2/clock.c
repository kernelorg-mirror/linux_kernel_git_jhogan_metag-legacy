
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <asm/clock.h>
#include <asm/global_lock.h>
#include <asm/soc-chorus2/clock.h>

/* report pixel clock settings */
/*#define DEBUG_PIXEL_CLOCK*/

/* Clock control */
#define CCR_ADDR		0x02000064
#define CCR_CLK_SEL		0x80000000
#define CCR_CLK_DEL		0x7FE00000
#define CCR_CLK_DEL_SHIFT	21
#define CCR_CLK_DIV		0x001C0000
#define CCR_CLK_DIV_SHIFT	18
#define CCR_CLK_PD		0x00020000
#define CCR_CLK_BP		0x00010000
#define CCR_CLK_OEB		0x00008000
#define CCR_CLK_OD		0x00006000
#define CCR_CLK_OD_SHIFT	13
#define CCR_CLK_NR		0x00001F00
#define CCR_CLK_NR_SHIFT	8
#define CCR_CLK_NF		0x000000FF
#define CCR_CLK_NF_SHIFT	0

/* Pixel clock control */
#define PCC_ADDR		0x020000A8
#define PCC_DIVR1		0x00000038
#define PCC_DIVR1_SHIFT		3
#define PCC_DIVR1_MAX		0x5
#define PCC_ENABLE		0x00000004
#define PCC_ENABLE_SHIFT	2
#define PCC_DIV_SOURCE		0x00000003
#define PCC_DIV_SOURCE_SHIFT	0
#define PCC_DIV_SOURCE_REFCLK	0x0
#define PCC_DIV_SOURCE_PLL	0x1
#define PCC_DIV_SOURCE_SYS	0x2

/* Misc clock control */
#define MCC_ADDR		0x020000BC
#define MCC_PIX_DIVR2		0x000000F8
#define MCC_PIX_DIVR2_SHIFT	3
#define MCC_PIX_DIVR2_MAX	(1 << 5)

/*
 * Return the system clock (xtal/oscillator). On Chorus2, this
 * is (almost) always a 24.576MHz oscillator. This is defined as weak so
 * can be overridden on a board by board basis if necessary.
 */
unsigned long __weak get_sysclock(void)
{
	return 24576000;
}

/* Determine what speed the PLL output is. */
static unsigned long get_pllclock(void)
{
	unsigned long sclk = get_sysclock();
	u32 ccr;
	u32 od, nr, nf;
	u32 no;
	unsigned long fout;

	ccr = ioread32((void *)CCR_ADDR);

	/* Is the PLL bypassed */
	if (ccr & CCR_CLK_BP)
		return sclk;

	od = (ccr & CCR_CLK_OD) >> CCR_CLK_OD_SHIFT;
	nr = (ccr & CCR_CLK_NR) >> CCR_CLK_NR_SHIFT;
	/* Note the 2*, hidden in the PLL manual */
	nf = 2 * ((ccr & CCR_CLK_NF) >> CCR_CLK_NF_SHIFT);

	/* 2bit decimal encoding of binary series */
	if (od == 3)
		no = 4;
	else
		no = od;

	fout = (sclk * nf) / (nr * no);

	return fout;
}

/* Determine what speed we are running the CPU at. Ask the board what speed
 * the main oscillator is running at (normally 24.576MHz for a Chorus2), and
 * then examine the PLL to see how that is being manipulated.
 */
unsigned long chorus2_get_coreclock(void)
{
	u32 ccr;
	u32 del, div;
	unsigned long fout;

	ccr = ioread32((void *)CCR_ADDR);

	del = (ccr & CCR_CLK_DEL) >> CCR_CLK_DEL_SHIFT;
	div = (ccr & CCR_CLK_DIV) >> CCR_CLK_DIV_SHIFT;

	WARN_ON_ONCE(del);

	/* Is the PLL selected */
	if (!(ccr & CCR_CLK_SEL))
		return get_sysclock();

	/* The PLL is in use, so it cannot be powered down, or we would not be
	 * running. Thus, do not even check the PD bit.
	 */

	fout = get_pllclock();

	switch (div) {
		/* Output turned off - how are we running then ? */
	case 0:
	case 7:
		WARN_ON_ONCE(1);
		break;
		/* no divide */
	case 1:
		break;
		/* Must be one of the dividers then */
	default:
		fout = fout / (1 << (div - 1));
		break;
	}

	return fout;
}

struct meta_clock_desc chorus2_meta_clocks __initdata = {
	.get_core_freq = chorus2_get_coreclock,
};

/* Clock framework types */

struct clk {
	const char *id;
	struct clk_ops *ops;
	spinlock_t lock;
	unsigned int refcount;
};

struct clk_ops {
	/* combined enable/disable */
	int (*mode)(struct clk *clk, int enabled);
	unsigned long (*get_rate)(struct clk *clk);
	int (*set_rate)(struct clk *clk, unsigned long rate);
};

#define INIT_CLK(NAME, OPS) {				\
		.id = (NAME),				\
		.ops = &(OPS),				\
		.lock = __SPIN_LOCK_UNLOCKED(clk.lock),	\
	}

static struct clk_ops dummy_clk_ops = {};
static struct clk dummy_clk = INIT_CLK("dummy", dummy_clk_ops);

/* Pixel clock */

struct pix_clk {
	struct clk clk;
	unsigned long min_rate;
	unsigned long max_rate;
};
#define CLK_TO_PIX_CLK(CLK) container_of((CLK), struct pix_clk, clk)


/*
 * functions for finding the best clock and divider settings.
 * divrs[0] is a power of 2, up to 5 (2^5 = 32).
 * divrs[1] is in range [1,32].
 * pixel_clock = (clock / divrs[1]) >> divrs[0]
 */

static unsigned long (*pixclock_sources[])(void) = {
	[PCC_DIV_SOURCE_REFCLK]	= get_sysclock,
	[PCC_DIV_SOURCE_PLL]	= get_pllclock,
};

static unsigned long pixclock_do_divrs(unsigned long clock_freq, u32 *divrs)
{
	return (clock_freq / divrs[1]) >> divrs[0];
}

static void pixclock_inc_divrs(u32 *divrs)
{
	if (unlikely(divrs[1] >= MCC_PIX_DIVR2_MAX)) {
		if (likely(divrs[0] < PCC_DIVR1_MAX)) {
			++divrs[0];
			divrs[1] = divrs[1] / 2 + 1;
		}
	} else
		++divrs[1];
}

static void pixclock_dec_divrs(u32 *divrs)
{
	if (divrs[0] && divrs[1] <= MCC_PIX_DIVR2_MAX/2) {
		--divrs[0];
		divrs[1] = divrs[1] * 2 - 1;
	} else if (likely(divrs[1] > 1))
		--divrs[1];
}

/* normalise so both values in range */
static void pixclock_norm_divrs(u32 *divrs)
{
	while (divrs[1] > MCC_PIX_DIVR2_MAX) {
		++divrs[0];
		divrs[1] /= 2;
	}

	if (unlikely(divrs[0] > PCC_DIVR1_MAX)) {
		divrs[0] = PCC_DIVR1_MAX;
		divrs[1] = MCC_PIX_DIVR2_MAX;
	} else if (unlikely(divrs[1] < 1)) {
		divrs[0] = 0;
		divrs[1] = 1;
	}
}

/*
 * returns the actual pixel frequency obtained
 * assumes pixclock is in range [min, max]
 */
static unsigned long pixclock_calc_divrs(unsigned long pixclock,
			unsigned long min, unsigned long max,
			unsigned long clock_freq,
			u32 *divrs)
{
	unsigned long result;

	divrs[0] = 0;
	divrs[1] = (pixclock/2 + clock_freq) / pixclock;
	pixclock_norm_divrs(divrs);

	/* don't allow exceeding of hardware limits */
	result = pixclock_do_divrs(clock_freq, divrs);
	if (max && result > max)
		pixclock_inc_divrs(divrs);
	else if (min && result < min)
		pixclock_dec_divrs(divrs);
	else
		return result;
	return pixclock_do_divrs(clock_freq, divrs);
}

static int pixclock_calc_best_src(unsigned long pixclock,
				unsigned long min, unsigned long max,
				u32 *divrs, u32 *src)
{
	int result = 1;
	unsigned long err = ~0;
	u32 i;

#ifdef DEBUG_PIXEL_CLOCK
	printk(KERN_DEBUG "Desired pixel clock: %lu [%lu,%lu]\n",
		pixclock,
		min,
		max);
#endif

	/* try not to exceed frequency limits of screen */
	if (min && pixclock < min)
		pixclock = min;
	else if (max && pixclock > max)
		pixclock = max;

	for (i = 0; i < ARRAY_SIZE(pixclock_sources); ++i) {
		u32 tmp_divrs[2];
		unsigned long tmp_pixclock;
		unsigned long tmp_err;

		if (!pixclock_sources[i])
			continue;

		tmp_pixclock = pixclock_calc_divrs(pixclock, min, max,
					pixclock_sources[i](), tmp_divrs);

#ifdef DEBUG_PIXEL_CLOCK
		printk(KERN_DEBUG "Pixel clock src %d offers: "
				  "%lu (%lu/%u/%u)\n",
			i,
			tmp_pixclock,
			pixclock_sources[i](),
			1 << tmp_divrs[0],
			tmp_divrs[1]);
#endif

		if (unlikely(tmp_pixclock < min || tmp_pixclock > max))
			continue;

		tmp_err = abs(tmp_pixclock - pixclock);
		if (tmp_err <= err) {
			divrs[0] = tmp_divrs[0];
			divrs[1] = tmp_divrs[1];
			*src = i;
			err = tmp_err;
			result = 0;
#ifdef DEBUG_PIXEL_CLOCK
			printk(KERN_DEBUG "Pixel clock src %d chosen\n", i);
#endif
		}
	}

	return result;
}

/*
 * Sets the pixel clock to be as close to pixclock as possible within the limits
 * of min and max. Use get_pixclock to get the actual frequency.
 * A zero min or max means no limit.
 * Returns zero if successful.
 */
static int set_pixclock(struct clk *clk, unsigned long pixclock)
{
	struct pix_clk *pix_clk = CLK_TO_PIX_CLK(clk);
	u32 src = 0;
	u32 divrs[2] = {0, 1};
	int state;
	u32 pcc, mcc;

	if (!pixclock)
		return -EINVAL;

	if (pixclock_calc_best_src(pixclock,
				   pix_clk->min_rate, pix_clk->max_rate,
				   divrs, &src))
		return -EINVAL;

	pcc = ioread32((void *)PCC_ADDR);
	pcc &= ~(PCC_DIVR1 | PCC_DIV_SOURCE);
	pcc |= divrs[0]		<< PCC_DIVR1_SHIFT;
	pcc |= src		<< PCC_DIV_SOURCE_SHIFT;
	iowrite32(pcc, (void *)PCC_ADDR);

	/*
	 * misc_clock_control has other uses
	 * lock out interrupts and other HW threads
	 */
	__global_lock2(state);
	mcc = ioread32((void *)MCC_ADDR);
	mcc &= ~MCC_PIX_DIVR2;
	mcc |= (divrs[1] - 1)	<< MCC_PIX_DIVR2_SHIFT;
	iowrite32(mcc, (void *)MCC_ADDR);
	__global_unlock2(state);

	return 0;
}

static unsigned long get_pixclock(struct clk *clk)
{
	u32 pcc, mcc;
	unsigned long source;
	u32 divrs[2];

	pcc = ioread32((void *)PCC_ADDR);

	if (!(pcc & PCC_ENABLE))
		return 0;

	mcc = ioread32((void *)MCC_ADDR);
	divrs[0] = (pcc & PCC_DIVR1) >> PCC_DIVR1_SHIFT;
	divrs[1] = 1 + ((mcc & MCC_PIX_DIVR2) >> MCC_PIX_DIVR2_SHIFT);
	source = (pcc & PCC_DIV_SOURCE) >> PCC_DIV_SOURCE_SHIFT;

	if (source >= ARRAY_SIZE(pixclock_sources) || !pixclock_sources[source])
		return 0;
	return pixclock_do_divrs(pixclock_sources[source](), divrs);
}

static int pix_clk_mode(struct clk *clk, int enabled)
{
	unsigned int temp;
	temp = ioread32((void *)PCC_ADDR);
	if (enabled)
		temp |= PCC_ENABLE;
	else
		temp &= ~PCC_ENABLE;
	iowrite32(temp, (void *)PCC_ADDR);
	return 0;
}

static struct clk_ops pix_clk_ops = {
	.get_rate = get_pixclock,
	.set_rate = set_pixclock,
	.mode = pix_clk_mode,
};

/* SPI clock */

static unsigned long get_spiclock(struct clk *clk)
{
	return get_sysclock();
}

static struct clk_ops spi_clk_ops = {
	.get_rate = get_spiclock,
	.mode = pix_clk_mode,
};


/* Clock data */

static char *dummy_clk_ids[] = {
	"pdp",
	"pdi",
};

static struct clk simple_clks[] = {
	INIT_CLK("spi", spi_clk_ops),
};

static struct pix_clk pix_clk = {
	.clk = INIT_CLK("pixel", pix_clk_ops)
};

void pix_clk_set_limits(unsigned long min, unsigned long max)
{
	pix_clk.min_rate = min;
	pix_clk.max_rate = max;
}


/* Clock framework */

struct clk *clk_get(struct device *dev, const char *id)
{
	int i;
	if (!strcmp(id, pix_clk.clk.id))
		return &pix_clk.clk;
	/* simple clocks */
	for (i = 0; i < ARRAY_SIZE(simple_clks); ++i)
		if (!strcmp(id, simple_clks[i].id))
			return &simple_clks[i];
	/* dummy clocks */
	for (i = 0; i < ARRAY_SIZE(dummy_clk_ids); ++i)
		if (!strcmp(id, dummy_clk_ids[i]))
			return &dummy_clk;
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk->lock, flags);
	if (!clk->refcount) {
		if (clk->ops->mode)
			clk->ops->mode(clk, 1);
	}
	++clk->refcount;
	spin_unlock_irqrestore(&clk->lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk->lock, flags);
	--clk->refcount;
	if (!clk->refcount) {
		if (clk->ops->mode)
			clk->ops->mode(clk, 0);
	}
	spin_unlock_irqrestore(&clk->lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long result;
	unsigned long flags;

	spin_lock_irqsave(&clk->lock, flags);
	if (clk->ops->get_rate)
		result = clk->ops->get_rate(clk);
	else
		result = 0;
	spin_unlock_irqrestore(&clk->lock, flags);
	return result;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int result;
	unsigned long flags;

	spin_lock_irqsave(&clk->lock, flags);
	if (clk->ops->set_rate)
		result = clk->ops->set_rate(clk, rate);
	else
		result = -EINVAL;
	spin_unlock_irqrestore(&clk->lock, flags);
	return result;
}
EXPORT_SYMBOL(clk_set_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);
