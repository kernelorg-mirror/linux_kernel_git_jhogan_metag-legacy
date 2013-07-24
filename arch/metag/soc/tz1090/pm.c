/*
 * IMG Comet Power Management
 *
 * Copyright 2010 Imagination Technologies Ltd.
 *
 */

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <asm/core_reg.h>
#include <asm/coremem.h>
#include <asm/global_lock.h>
#include <asm/mmu_context.h>
#include <asm/soc-tz1090/bootprot.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/pdc.h>
#include <asm/soc-tz1090/pm.h>
#include <asm/soc-tz1090/suspend.h>
#include <asm/suspend.h>
#include <asm/tbx.h>

/**
 * comet_safe_mode() - Find whether the SoC is in SAFE mode.
 *
 * Returns:	non zero if SAFE mode is active, 0 otherwise.
 */
static inline int comet_safe_mode(void)
{
	u32 soc_bootstrap = readl(PDC_BASE_ADDR + PDC_SOC_BOOTSTRAP);
	return soc_bootstrap & PDC_SOC_BOOTSTRAP_SAFE_MODE;
}

/**
 * comet_pdc_restart() - Restart SoC using watchdog reset.
 *
 * Watchdog reset the SoC.
 */
void comet_pdc_restart(void)
{
	writel(1, PDC_BASE_ADDR + PDC_WD_SW_RESET);
}

/**
 * comet_pdc_power_off() - Power off the SoC if possible using EXT_POWER.
 *
 * If the SoC isn't in SAFE mode, power off the SoC using the PDC to control
 * EXT_POWER.
 *
 * Returns:	0 on success, -errno on failure.
 */
int comet_pdc_power_off(void)
{
	if (comet_safe_mode()) {
		pr_info("SoC SAFE mode active, cannot power off SoC\n");
		return -ENODEV;
	}

	writel(0, PDC_BASE_ADDR + PDC_SOC_POWER);
	return 0;
}

#ifdef CONFIG_SUSPEND

/* Mask of CR_TOP_CLKENAB that is disabled in comet_pm_standby. */
#define CLKENAB_CLKOUT_MASK ((1 << CR_TOP_CLKOUT1_3_EN_BIT) | \
			     (1 << CR_TOP_CLKOUT0_3_EN_BIT))

int (*board_suspend)(suspend_state_t state);
void (*board_resume)(suspend_state_t state);

/**
 * comet_pm_standby() - Go into standby mode.
 *
 * This copies the standby code into core memory, does some final power saving
 * tasks and jumps into core memory, where the DDR memory is put into self
 * refresh mode and clocked off, and the Meta is clocked down.
 * When a wake interrupt is detected, the Meta is powered back up to it's
 * previous state.
 */
static int comet_pm_standby(void)
{
	struct metag_coremem_region *reg;
	void (*standby_func)(unsigned int txmask);
	unsigned int flags;
	unsigned int perip_clk;
	unsigned int top_clk;
	int thread = hard_processor_id();
	int ret = 0;

	/* Get the address of some core memory */
	reg = metag_coremem_alloc(METAG_COREMEM_ICACHE, metag_comet_standby_sz);
	if (!reg) {
		ret = -ENOMEM;
		goto out_finish;
	}

	/* Copy the standby code into core memory */
	standby_func = metag_coremem_push(reg, metag_comet_standby,
					  metag_comet_standby_sz);
	if (!standby_func) {
		ret = -ENOMEM;
		goto out_finish_coremem;
	}

	__global_lock2(flags);
	/* Disable all peripheral clocks */
	perip_clk = readl(CR_PERIP_CLK_EN);
	writel(0, CR_PERIP_CLK_EN);
	/* Disable clk_out_0 and clk_out_1 */
	/* keep value of top_clk so we can restore those bits */
	top_clk = readl(CR_TOP_CLKENAB);
	writel(top_clk & ~CLKENAB_CLKOUT_MASK, CR_TOP_CLKENAB);
	__global_unlock2(flags);

	/* Run the code from core memory, waking on trigger */
	wmb();
	standby_func(TBI_TRIG_BIT(TBID_SIGNUM_TR2(thread)));

	__global_lock2(flags);
	/* Re-enable clk_out_0 and clk_out_1 */
	writel(top_clk, CR_TOP_CLKENAB);
	/* Re-enable the peripheral clocks */
	writel(perip_clk, CR_PERIP_CLK_EN);
	__global_unlock2(flags);

	/* finish using the core memory */
out_finish_coremem:
	metag_coremem_free(reg);

out_finish:
	return ret;
}

#ifdef CONFIG_METAG_SUSPEND_MEM

#ifdef CONFIG_COMET_SUSPEND_MEM_SAFE
/* if safe suspend support is enabled then we always allow suspend */
#define allow_suspend_mem()	1
#else
/* without safe suspend support we don't support suspend in SAFE mode */
#define allow_suspend_mem()	(!comet_safe_mode())
#endif

/* jump buffer for use by suspend to RAM */
static struct metag_suspend_jmpbuf s2r_jmpbuf;

/**
 * meta_pm_mem_resume() - Main resume entry point.
 * @data:	Data pointer to jump buffer.
 *
 * This is the main entry point back into the kernel on resume.
 *
 * Returns:	1 on error, otherwise never returns.
 */
static int meta_pm_mem_resume(void *data)
{
	struct metag_suspend_jmpbuf *jmp = data;

#ifdef CONFIG_METAG_DSP
	__core_reg_set(D0.8, 0);
#endif
	setup_priv();

	/* make sure the soft reset protected registers are cleared */
	bootprot_resume_ram(PDC_BASE_ADDR + PDC_SOC_SW_PROT);

	/* jump back to where we suspended from */
	metag_resume_longjmp(jmp, 1);
	/* should never get here, return to bootloader */
	return 1;
}

/*
 * Core suspend data that is needed during successful resume.
 * used by comet_pm_do_suspend().
 * used by comet_pm_resume().
 */
struct comet_pm_suspend_data {
	struct metag_coremem_region *reg;
};

static int comet_pm_do_suspend(volatile struct comet_pm_suspend_data *d)
{
	void (*suspend_func)(unsigned int);
	int thread = hard_processor_id();
	int ret = 0;

	/* Get the address of some core memory */
	d->reg = metag_coremem_alloc(METAG_COREMEM_ICACHE,
				     metag_comet_suspend_sz);
	if (!d->reg) {
		ret = -ENOMEM;
		goto out_finish;
	}

	/* Copy the suspend code into core memory */
	suspend_func = metag_coremem_push(d->reg, metag_comet_suspend,
					  metag_comet_suspend_sz);
	if (!suspend_func) {
		ret = -ENOMEM;
		goto out_finish_coremem;
	}

	/* Run the code from core memory, waking on TR2 */
	wmb();
	suspend_func(TBI_TRIG_BIT(TBID_SIGNUM_TR2(thread)));

	/*
	 * If we've got here then something went wrong, the power down (or reset
	 * in the case of SAFE mode) never happened.
	 */
	pr_err("Power down failed\n");
	ret = -ENODEV;

	/* finish using the core memory */
out_finish_coremem:
	metag_coremem_free(d->reg);

out_finish:
	return ret;
}

/* clean up after a successful suspend */
static noinline void comet_pm_resume(volatile struct comet_pm_suspend_data *d)
{
	metag_coremem_free(d->reg);
}

static int comet_pm_suspend(int (*resume)(void *),
			    void *data,
			    volatile struct comet_pm_suspend_data *d)
{
	int err;

	/*
	 * Set up the PDC soft reset protected registers for resuming form
	 * suspend-to-RAM.
	 */
	bootprot_suspend_ram(PDC_BASE_ADDR + PDC_SOC_SW_PROT, resume, data);

	/* Put DDR RAM into self refresh and switch off the power. */
	err = comet_pm_do_suspend(d);
	if (err) {
		/*
		 * Something went wrong. we don't want to leave the magic values
		 * lying around though.
		 */
		bootprot_resume_ram(PDC_BASE_ADDR + PDC_SOC_SW_PROT);
	}
	return err;
}

static int metag_pm_mem(void)
{
	struct mm_struct *mm = current->active_mm;
	int ret;
	volatile struct comet_pm_suspend_data d;

	comet_prepare_reset();

	ret = traps_save_context();
	if (unlikely(ret))
		goto err_traps;

	/* Store the core registers, so after wakeup we'll jump back to here. */
	if (!metag_suspend_setjmp(&s2r_jmpbuf)) {
		/*
		 * Do SoC specifics to power switch core off safely.
		 * This won't return unless something went wrong.
		 */
		ret = comet_pm_suspend(meta_pm_mem_resume, &s2r_jmpbuf, &d);
		if (unlikely(ret))
			goto err_suspend;
	} else {
		comet_pm_resume(&d);
	}

	/* We've suspended, so resume safely */
	switch_mmu(&init_mm, mm);
err_suspend:
	traps_restore_context();
err_traps:
	return ret;
}

#else /* CONFIG_METAG_SUSPEND_MEM */

#define allow_suspend_mem() 0
#define metag_pm_mem() (-EINVAL)

#endif

/* Begin the suspend process. */
static int comet_pm_begin(suspend_state_t state)
{
#ifdef CONFIG_COMET_SUSPEND_MEM_SAFE
	if (state == PM_SUSPEND_MEM && comet_safe_mode())
		pr_warn("WARNING: SoC SAFE mode active, Suspend to RAM will be faked using watchdog reset\n");
#endif
	return 0;
}

/* Enter a suspend mode. */
static int comet_pm_enter(suspend_state_t state)
{
	int ret;

	if (board_suspend) {
		ret = board_suspend(state);
		if (ret)
			return ret;
	}

	switch (state) {
	case PM_SUSPEND_STANDBY:
		ret = comet_pm_standby();
		break;
	case PM_SUSPEND_MEM:
		ret = metag_pm_mem();
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (board_resume)
		board_resume(state);

	return ret;
}

/* Specify which suspend modes are valid. */
static int comet_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_STANDBY ||
	       (state == PM_SUSPEND_MEM && allow_suspend_mem());
}

static struct platform_suspend_ops comet_pm_ops = {
	.begin		= comet_pm_begin,
	.enter		= comet_pm_enter,
	.valid		= comet_pm_valid,
};

static int __init comet_pm_init(void)
{
	suspend_set_ops(&comet_pm_ops);
	return 0;
}
device_initcall(comet_pm_init);

#endif /* CONFIG_SUSPEND */
