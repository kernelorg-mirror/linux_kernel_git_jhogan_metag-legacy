/*
 *  clock.c
 *
 *  Copyright (C) 2009 Imagination Technologies Ltd.
 *
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/syscore_ops.h>
#include <linux/mutex.h>
#include <asm/clock.h>
#include <asm/global_lock.h>
#include <asm/soc-tz1090/clock.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/pdc.h>

static ATOMIC_NOTIFIER_HEAD(clk32k_notifier_list);

unsigned long get_xtal1(void)
{

	u32 xtal_bits = (readl(CR_PERIP_RESET_CFG)
			& CR_PERIP_RESET_CFG_FXTAL_BITS) >>
			CR_PERIP_RESET_CFG_FXTAL_SHIFT;

	switch (xtal_bits) {

	case 0:		return 16384000;
	case 1:		return 19200000;
	case 2:		return 24000000;
	case 3:		return 24576000;
	case 4:		return 26000000;
	case 5:		return 36000000;
	case 6:		return 36864000;
	case 7:		return 38400000;
	case 8:		return 40000000;

	default:
			printk(KERN_ERR"Comet Clocks:"
				"Invalid XTAL1 selected\n");
			BUG();
	}
}

/*
 * Return the XTAL 2 clock. On Comet this is not detectable
 * but the recommended value is 12MHz, the method is declared weak so it can
 * be overridden by a board specific function if necessary.
 */
unsigned long __weak get_xtal2(void)
{
	return COMET_XTAL2;
}

/*
 * Return the XTAL 3 clock. On Comet this is 32.768KHz for the RTC.
 */
unsigned long __weak get_xtal3(void)
{
	return COMET_XTAL3;
}

/**
 * get_32kclock() - Get frequency of 32.768KHz clock.
 *
 * Returns:	The frequency of the 32KHz clock in Hz. This can be derived from
 *		a 32.768KHz oscillator in XTAL3, or XTAL1 divided down.
 */
unsigned long get_32kclock(void)
{
	unsigned int soc_gpio0;
	unsigned int divider;

	soc_gpio0 = readl(PDC_BASE_ADDR + PDC_SOC_GPIO_CONTROL0);
	if (soc_gpio0 & PDC_SOC_GPIO0_RTC_SW) {
		return get_xtal3();
	} else {
		divider = 1 + ((soc_gpio0 & PDC_SOC_GPIO0_XTAL1_DIV)
					>> PDC_SOC_GPIO0_XTAL1_DIV_SHIFT);
		return get_xtal1() / divider;
	}
}
EXPORT_SYMBOL_GPL(get_32kclock);

/**
 * clk32k_register_notify() - Register a 32.768KHz clock notifier callback.
 * @nb:		pointer to the notifier block for the callback events.
 *
 * These occur when the frequency is changed.
 *
 * Returns:	0 on success.
 */
int clk32k_register_notify(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&clk32k_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(clk32k_register_notify);

/**
 * clk32k_unregister_notify() - Unregister a 32.768KHz clock notifier callback.
 * @nb:		pointer to the notifier block for the callback events.
 *
 * clk32k_register_notify() must have been previously called for this function
 * to work properly.
 *
 * Returns:	0 on success.
 */
int clk32k_unregister_notify(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&clk32k_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(clk32k_unregister_notify);

/**
 * set_32kclock_src() - Set source of 32.768KHz clock.
 * @xtal1:	Whether to derive from XTAL1 rather than XTAL3.
 * @xtal1_div:	Divider to use when deriving from XTAL1.
 *
 * Returns:	The resulting frequency in Hz.
 */
unsigned long set_32kclock_src(int xtal1, unsigned int xtal1_div)
{
	struct clk32k_change_freq change;
	unsigned int soc_gpio0;
	unsigned int divider;
	unsigned int lstat;

	__global_lock2(lstat);
	change.old_freq = get_32kclock();

	soc_gpio0 = readl(PDC_BASE_ADDR + PDC_SOC_GPIO_CONTROL0);
	if (xtal1) {
		soc_gpio0 &= ~PDC_SOC_GPIO0_RTC_SW;
		divider = (xtal1_div - 1) << PDC_SOC_GPIO0_XTAL1_DIV_SHIFT;
		soc_gpio0 &= ~PDC_SOC_GPIO0_XTAL1_DIV;
		soc_gpio0 |= divider & PDC_SOC_GPIO0_XTAL1_DIV;
	} else {
		soc_gpio0 |= PDC_SOC_GPIO0_RTC_SW;
	}
	writel(soc_gpio0, PDC_BASE_ADDR + PDC_SOC_GPIO_CONTROL0);

	change.new_freq = get_32kclock();
	__global_unlock2(lstat);

	/*
	 * Inform users so they can make the necessary adjustments to their
	 * timings.
	 */
	if (change.old_freq != change.new_freq)
		atomic_notifier_call_chain(&clk32k_notifier_list,
					   CLK32K_CHANGE_FREQUENCY, &change);

	return change.new_freq;
}

unsigned long get_sysclock_x2_undeleted(void)
{
	u64 f_in;
	u64 f_out;
	u16 clk_f;    /* Feedback divide */
	u8 clk_od;    /* Output Divider */
	u8 clk_r;     /* Reference divider */
	u32 pll_ctrl0 = readl(CR_TOP_SYSPLL_CTL0);
	u32 pll_ctrl1 = readl(CR_TOP_SYSPLL_CTL1);
	u32 sys_clk_div = readl(CR_TOP_SYSCLK_DIV) & 0xFF;

	if ((readl(CR_TOP_CLKSWITCH) & 0x2) == 0)
		return get_xtal1();

	if (readl(CR_TOP_CLKSWITCH) & 0x1)
		f_in = get_xtal2();
	else
		f_in = get_xtal1();

	if (pll_ctrl1 & (1<<25)) { /* bypass bit set */
		f_out = f_in;
	} else {
		unsigned long divisor;
		/* Get Divider Values */
		clk_f = (pll_ctrl0 >> 4) & 0x1FFF;
		clk_od = pll_ctrl0 & 0x7;
		clk_r = pll_ctrl1 & 0x3F;

		/*
		 *  formula:
		 *  fout = (fin / (clkr + 1)) * (((clkf/2) + 0.5)/(clkod + 1))
		 *
		 *  note the equation has been re-arranged to avoid loss of
		 *  precision due to integer maths.
		 */
		f_out = (u64)f_in * (clk_f + 1);
		divisor = (2 * (clk_od + 1) * (clk_r + 1));
		f_out = div_u64(f_out, divisor);
	}

	if (sys_clk_div)
		return (unsigned long)f_out / (sys_clk_div + 1);
	else
		return (unsigned long)f_out;
}

unsigned long get_sysclock_undeleted(void)
{
#ifdef CONFIG_SOC_COMET_ES1
	if (readl(CR_TOP_META_CLKDIV) & 0x1)
		return get_sysclock_x2_undeleted() / 2;
	else
		return get_sysclock_x2_undeleted();
#else
	u32 div = readl(CR_TOP_META_CLKDIV);

	return get_sysclock_x2_undeleted() / (div + 1);
#endif
}

unsigned long get_sdhostclock(void)
{
	u8 clk_div = readl(CR_TOP_SDHOSTCLK_DIV);

	return get_sysclock_undeleted() / (clk_div + 1);

}

unsigned long set_sdhostclock(unsigned long f)
{
	/*
	 * Note we only modify the SDHosts own clock divider to try
	 * and match the closest requested frequency - we do not
	 * modify the main PLL, we return the value we achieved
	 */
	unsigned long f_in = get_sysclock_undeleted();
	unsigned long f_out;
	int lstat;
	u32 temp;

	if (f > f_in) {
		writel(0, CR_TOP_SDHOSTCLK_DIV);
		f_out = f_in;
	} else {
		u8 divider = min_t(u32, ((f_in+f-1)/f), 0xFFU);
		divider = divider ? divider : 1;
		writel(divider-1, CR_TOP_SDHOSTCLK_DIV);
		f_out = f_in / divider;
	}

	/* Top level clk enable */
	__global_lock2(lstat);
	temp = readl(CR_PERIP_CLK_EN);
	temp |= (1<<CR_PERIP_SDHOST_CLK_EN_BIT);
	writel(temp, CR_PERIP_CLK_EN);
	__global_unlock2(lstat);

	return f_out;
}


void pix_clk_set_limits(unsigned long min, unsigned long max)
{
}

unsigned long get_ddrclock(void)
{
	u8 clk_div = readl(CR_TOP_DDR_CLKDIV);

	return get_sysclock_x2_undeleted() / (clk_div + 1);
}

#ifdef CONFIG_METAG_SUSPEND_MEM

/* ======== UART clocks ======== */

/* Global UART */
#define CLKENAB_UART		(1 << CR_TOP_UART_EN_BIT)
#define CLKSWITCH_UART		(1 << CR_TOP_UART_SW_BIT)
#define UARTCLKDIV		(~0)

/* UART0 */
#define PERIPCLKEN_UART0	(1 << CR_PERIP_UART0_CLK_EN_BIT)

/* UART1 */
#define PERIPCLKEN_UART1	(1 << CR_PERIP_UART1_CLK_EN_BIT)

/* ======== SCB (I2C) clocks ======== */

/* Global SCB */
#define CLKENAB_SCB		(1 << CR_TOP_SCB_EN_BIT)
#define CLKSWITCH_SCB		(1 << CR_TOP_SCB_SW_BIT)

/* SCB0 */
#define PERIPCLKEN_SCB0		(1 << CR_PERIP_I2C0_CLK_EN_BIT)

/* SCB1 */
#define PERIPCLKEN_SCB1		(1 << CR_PERIP_I2C1_CLK_EN_BIT)

/* SCB2 */
#define PERIPCLKEN_SCB2		(1 << CR_PERIP_I2C2_CLK_EN_BIT)

/* ======== SPI clocks ======== */
#define PERIPCLKEN_SPIM1	(1 << CR_PERIP_SPIM1_CLK_EN_BIT)
#define SPI1CLKDIV		(~0)

/* ======== I2S clocks ======== */
#define PERIPCLKEN_I2S		(1 << CR_PERIP_I2SOUT_CLK_EN_BIT)
#define I2SCLKDIV		(~0)

/* ======== PDP / PDI clock ======== */

#define CLKENAB2_PIXEL		(1 << CR_TOP_PIXEL_CLK_2_EN_BIT)
#define HEPCLKEN_PDP		CR_PDP_PDI_CLK_EN
#define PIXELCLKDIV		(~0)

/* ======== 2D clock ======== */
#define HEPCLKEN_2D		CR_2D_CLK_EN


/* ======== Accumulated clock bits to preserve ======== */
#define CLKSWITCH_ALL		(CLKSWITCH_UART		| \
				 CLKSWITCH_SCB)
#define CLKENAB_ALL		(CLKENAB_UART		| \
				 CLKENAB_SCB)
#define CLKENAB2_ALL		(CLKENAB2_PIXEL)
#define PERIPCLKEN_ALL		(PERIPCLKEN_UART0	| \
				 PERIPCLKEN_UART1	| \
				 PERIPCLKEN_SCB0	| \
				 PERIPCLKEN_SCB1	| \
				 PERIPCLKEN_SCB2	| \
				 PERIPCLKEN_SPIM1	| \
				 PERIPCLKEN_I2S)
#define HEPCLKEN_ALL		(HEPCLKEN_PDP		| \
				 HEPCLKEN_2D)

/**
 * enum comet_clk_op - Operations to perform on resume.
 * @CLK_PRESERVE:	Preserve these bits across suspend.
 * @CLK_CLEAR:		Clear these bits.
 */
enum comet_clk_op {
	CLK_PRESERVE = 0,
	CLK_CLEAR,
};

/**
 * struct comet_clk_reg - Clock register field to preserve across suspend.
 * @addr:	Address of 32bit register to preserve.
 * @mask:	Mask of bits to preserve.
 * @op:		Operation to perform on resume (CLK_*).
 */
struct comet_clk_reg {
	u32 addr;
	u32 mask;
	enum comet_clk_op op;
};

static struct comet_clk_reg comet_clk_regs[] = {
	/* Address		Mask				Operation */
	/* turn clocks off first */
	{ CR_TOP_CLKENAB,	CLKENAB_ALL,			CLK_CLEAR },
	{ CR_TOP_CLKENAB2,	CLKENAB2_ALL,			CLK_CLEAR },
	{ CR_PERIP_CLK_EN,	PERIPCLKEN_ALL,			CLK_CLEAR },
	/* restore clock settings */
	{ CR_TOP_CLKSWITCH,	CLKSWITCH_ALL,			CLK_PRESERVE },
	{ CR_TOP_UART_CLK_DIV,	UARTCLKDIV,			CLK_PRESERVE },
	{ CR_TOP_SPI1CLK_DIV,	SPI1CLKDIV,			CLK_PRESERVE },
	{ CR_TOP_I2SCLK_DIV,	I2SCLKDIV,			CLK_PRESERVE },
	{ CR_TOP_PIXEL_CLK_DIV,	PIXELCLKDIV,			CLK_PRESERVE },
	/* finally restore clock switches */
	{ CR_TOP_CLKENAB,	CLKENAB_ALL,			CLK_PRESERVE },
	{ CR_TOP_CLKENAB2,	CLKENAB2_ALL,			CLK_PRESERVE },
	{ CR_PERIP_CLK_EN,	PERIPCLKEN_ALL,			CLK_PRESERVE },
	{ CR_HEP_CLK_EN,	HEPCLKEN_ALL,			CLK_PRESERVE },
};

static struct {
	u32 values[ARRAY_SIZE(comet_clk_regs)];
} *comet_clk_state;

/**
 * comet_clk_suspend() - stores hardware state so it can be restored.
 *
 * Stores clock settings into comet_clk_state using comet_clk_regs.
 */
int comet_clk_suspend(void)
{
	unsigned int i;
	u32 val;

	comet_clk_state = kzalloc(sizeof(*comet_clk_state), GFP_ATOMIC);
	if (!comet_clk_state)
		return -ENOMEM;

	/* read the registers that need preserving */
	for (i = 0; i < ARRAY_SIZE(comet_clk_regs); ++i) {
		if (comet_clk_regs[i].op == CLK_PRESERVE) {
			val = readl(comet_clk_regs[i].addr)
				& comet_clk_regs[i].mask;
			comet_clk_state->values[i] = val;
		}
	}

	return 0;
}

/**
 * comet_clk_resume() - restores hardware state.
 *
 * Restores clock settings from comet_clk_state.
 */
void comet_clk_resume(void)
{
	unsigned int i, lstat;
	u32 val;

	/* restore the clocking registers */
	__global_lock2(lstat);
	for (i = 0; i < ARRAY_SIZE(comet_clk_regs); ++i) {
		if (!comet_clk_regs[i].mask)
			continue;
		if (comet_clk_regs[i].mask != ~0u) {
			val = readl(comet_clk_regs[i].addr);
			val &= ~comet_clk_regs[i].mask;
			val |= comet_clk_state->values[i];
		} else {
			val = comet_clk_state->values[i];
		}
		writel(val, comet_clk_regs[i].addr);
	}
	__global_unlock2(lstat);

	kfree(comet_clk_state);
	comet_clk_state = NULL;
}

struct syscore_ops comet_clk_syscore_ops = {
	.suspend = comet_clk_suspend,
	.resume = comet_clk_resume,
};

static int __init comet_clk_init(void)
{
	register_syscore_ops(&comet_clk_syscore_ops);
	return 0;
}

device_initcall(comet_clk_init);

#endif /* CONFIG_METAG_SUSPEND_MEM */
