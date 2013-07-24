/*
 * soc/tz1090/afe.c
 * A simple interface to the Comet AFE.
 * This includes the aux DAC.
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 */

#include <asm/global_lock.h>
#include <asm/io.h>
#include <asm/soc-tz1090/afe.h>
#include <asm/soc-tz1090/defs.h>
#include <linux/syscore_ops.h>

/* bit 0 is set if auxdac is in use */
static unsigned long comet_afe_auxdac_busy;

/**
 * comet_afe_auxdac_get() - claim the auxiliary DAC.
 *
 * Use comet_afe_auxdac_put() to disclaim after use.
 *
 * Returns:	0 on success, -errno on failure.
 */
int comet_afe_auxdac_get(void)
{
	/* one at a time */
	if (test_and_set_bit_lock(0, &comet_afe_auxdac_busy))
		return -EBUSY;

	return 0;
}

/**
 * comet_afe_auxdac_put() - disclaim the auxiliary DAC.
 *
 * Must match a successful call to comet_afe_auxdac_get().
 */
void comet_afe_auxdac_put(void)
{
	clear_bit_unlock(0, &comet_afe_auxdac_busy);
}

/**
 * comet_afe_auxdac_set_power() - power up/down the auxiliary DAC.
 * @power:	1 to power up, 0 to power down.
 *
 * Powers up or down the aux DAC.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 */
void comet_afe_auxdac_set_power(unsigned int power)
{
	long flags;
	u32 afe_ctrl;

	__global_lock2(flags);
	afe_ctrl = readl(CR_AFE_CTRL);
	if (power)
		afe_ctrl &= ~CR_AFE_AUXDACPD;
	else
		afe_ctrl |= CR_AFE_AUXDACPD;
	writel(afe_ctrl, CR_AFE_CTRL);
	__global_unlock2(flags);
}

/**
 * comet_afe_auxdac_get_power() - get power up/down of auxiliary DAC.
 *
 * Gets the current power state of the aux DAC.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 *
 * Returns:	0 if powered down, otherwise powered up.
 */
unsigned int comet_afe_auxdac_get_power(void)
{
	u32 afe_ctrl;

	afe_ctrl = readl(CR_AFE_CTRL);
	return !(afe_ctrl & CR_AFE_AUXDACPD);
}

/**
 * comet_afe_auxdac_set_standby() - put auxiliary DAC in/out of standby.
 * @standby:	1 to put in standby, 0 to take out of standby.
 *
 * Puts the aux DAC in standby, or takes it out of standby.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 */
void comet_afe_auxdac_set_standby(unsigned int standby)
{
	long flags;
	u32 afe_ctrl;

	__global_lock2(flags);
	afe_ctrl = readl(CR_AFE_CTRL);
	if (standby)
		afe_ctrl |= CR_AFE_AUXDACSTBY;
	else
		afe_ctrl &= ~CR_AFE_AUXDACSTBY;
	writel(afe_ctrl, CR_AFE_CTRL);
	__global_unlock2(flags);
}

/**
 * comet_afe_auxdac_get_standby() - get standby state of auxiliary DAC.
 *
 * Gets the current standby state of the aux DAC.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 *
 * Returns:	0 if not in standby, otherwise in standby.
 */
unsigned int comet_afe_auxdac_get_standby(void)
{
	u32 afe_ctrl;

	afe_ctrl = readl(CR_AFE_CTRL);
	return afe_ctrl & CR_AFE_AUXDACSTBY;
}

/**
 * comet_afe_auxdac_set_source() - set the source of the auxiliary DAC output.
 * @source:	auxdac source (CR_AFE_AUXDACSEL_* in asm/soc-tz1090/defs.h)
 *
 * Sets the aux DAC source.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 *
 * Returns:	0 on success, -errno on failure.
 */
int comet_afe_auxdac_set_source(unsigned int source)
{
	long flags;
	u32 afe_auxdac;

	if (source > CR_AFE_AUXDACSEL_UCC0_EXT_CTL_1)
		return -EINVAL;
	__global_lock2(flags);
	afe_auxdac = readl(CR_AFE_AUXDAC);
	afe_auxdac &= ~CR_AFE_AUXDACSEL;
	afe_auxdac |= source << CR_AFE_AUXDACSEL_SHIFT;
	writel(afe_auxdac, CR_AFE_AUXDAC);
	__global_unlock2(flags);

	return 0;
}

/**
 * comet_afe_auxdac_set_value() - set fixed value of the auxiliary DAC output.
 * @value:	value to output from aux DAC in range 0-255.
 *
 * Sets a fixed value output from the aux DAC.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 */
void comet_afe_auxdac_set_value(unsigned int value)
{
	u32 afe_auxdac;
	afe_auxdac = CR_AFE_AUXDACSEL_CR_AFE_AUXDACIN << CR_AFE_AUXDACSEL_SHIFT;
	afe_auxdac |= (value << CR_AFE_AUXDACIN_SHIFT) & CR_AFE_AUXDACIN;
	writel(afe_auxdac, CR_AFE_AUXDAC);
}

/**
 * comet_afe_auxdac_get_value() - get fixed value of the auxiliary DAC output.
 *
 * Gets the fixed value output from the aux DAC, or returns a negative value if
 * the output isn't fixed.
 * The aux DAC should already be claimed with comet_afe_auxdac_get().
 *
 * Returns:	value of output from aux DAC in range 0-255, or < 0 if unfixed.
 */
int comet_afe_auxdac_get_value(void)
{
	u32 afe_auxdac;
	u32 src;

	afe_auxdac = readl(CR_AFE_AUXDAC);
	src = (afe_auxdac & CR_AFE_AUXDACSEL) >> CR_AFE_AUXDACSEL_SHIFT;

	if (src == CR_AFE_AUXDACSEL_CR_AFE_AUXDACIN)
		return (afe_auxdac & CR_AFE_AUXDACIN) >> CR_AFE_AUXDACIN_SHIFT;

	/* we can't work out what the value is */
	return -EIO;

}

#ifdef CONFIG_METAG_SUSPEND_MEM

/* stores state across suspend */
static unsigned int comet_afe_auxdac_state;

/**
 * comet_afe_suspend() - stores hardware state so it can be restored.
 *
 * Stores aux DAC hardware state in comet_afe_auxdac_state.
 */
int comet_afe_suspend(void)
{
	u32 afe_ctrl, afe_auxdac;

	if (comet_afe_auxdac_busy & 0x1) {
		afe_ctrl = readl(CR_AFE_CTRL);
		afe_auxdac = readl(CR_AFE_AUXDAC);

		comet_afe_auxdac_state = (afe_ctrl & (CR_AFE_AUXDACPD |
						      CR_AFE_AUXDACSTBY)) << 24
					| (afe_auxdac & 0xffffff);
	}

	return 0;
}

/**
 * comet_afe_resume() - restores hardware state.
 *
 * Restores aux DAC hardware state from comet_afe_auxdac_state.
 */
void comet_afe_resume(void)
{
	long flags;
	u32 afe_ctrl, afe_auxdac;

	if (comet_afe_auxdac_busy & 0x1) {
		__global_lock2(flags);
		afe_ctrl = readl(CR_AFE_CTRL);
		afe_ctrl &= ~(CR_AFE_AUXDACPD | CR_AFE_AUXDACSTBY);
		afe_ctrl |= comet_afe_auxdac_state >> 24;
		writel(afe_ctrl, CR_AFE_CTRL);
		__global_unlock2(flags);

		afe_auxdac = comet_afe_auxdac_state & 0xffffff;
		writel(afe_auxdac, CR_AFE_AUXDAC);
	}
}

static struct syscore_ops comet_afe_syscore_ops = {
	.suspend = comet_afe_suspend,
	.resume = comet_afe_resume,
};

static int comet_afe_init(void)
{
	register_syscore_ops(&comet_afe_syscore_ops);
	return 0;
}

device_initcall(comet_afe_init);

#endif /* CONFIG_METAG_SUSPEND_MEM */
