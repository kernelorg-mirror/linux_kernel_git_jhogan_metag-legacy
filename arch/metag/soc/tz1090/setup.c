/*
 * setup.c
 *
 * Copyright (C) 2009-2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/serial_8250.h>
#include <linux/timeriomem-rng.h>
#include <linux/uccp.h>
#include <linux/syscore_ops.h>
#include <linux/of_platform.h>
#include <asm/global_lock.h>
#include <asm/mach/arch.h>
#include <asm/soc-tz1090/clock.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/gpio.h>
#include <asm/soc-tz1090/pdc.h>
#include <asm/soc-tz1090/pm.h>
#include <asm/soc-tz1090/setup.h>

/*---------------------------- RNG Setup -------------------------------------*/

static struct resource rng_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= CR_PERIP_RNG_NUM,
	.end		= CR_PERIP_RNG_NUM + 4 - 1,
};

static struct timeriomem_rng_data rng_data = {
	.period		= 0,
};

static struct platform_device rng_device = {
	.name		= "timeriomem_rng",
	.id		= -1,
	.dev		= {
		.platform_data	= &rng_data,
	},
	.resource	= &rng_resource,
	.num_resources	= 1,
};

static void comet_rng_init(void)
{
	/* start rng without programmable seed */
	unsigned long rng_ctrl;
	rng_ctrl = CR_PERIP_RNG_START_BITS | CR_PERIP_RNG_PSEED_DIS_BITS;
	writel(rng_ctrl, CR_PERIP_RNG_CTRL);
}


/*---------------------------- UCCP Setup ------------------------------------*/

/* regions accessible from all uccp device nodes */
static struct uccp_region comet_uccp_global_regions[] = {
	{
		.type		= UCCP_REGION_ALL,
		.physical	= UCCP_BASE_ADDR,
		.size		= UCCP_SIZE,
	},
	{
		.type		= UCCP_REGION_SYS_INTERNAL,
		.physical	= UCCP_SYSINT_BASE_ADDR,
		.size		= UCCP_SYSINT_SIZE,
	},
};

static struct uccp_region comet_uccp0_regions[] = {
	{
		.type		= UCCP_REGION_MTX,
		.physical	= UCCP0_MTX_BASE_ADDR,
		.size		= UCCP0_MTX_SIZE,
	},
	{
		.type		= UCCP_REGION_MCP_16_BIT,
		.physical	= UCCP0_MCP16BIT_BASE_ADDR,
		.size		= UCCP0_MCP16BIT_SIZE,
	},
	{
		.type		= UCCP_REGION_MCP_24_BIT,
		.physical	= UCCP0_MCP24BIT_BASE_ADDR,
		.size		= UCCP0_MCP24BIT_SIZE,
	},
};

static struct uccp_region comet_uccp1_regions[] = {
	{
		.type		= UCCP_REGION_MTX,
		.physical	= UCCP1_MTX_BASE_ADDR,
		.size		= UCCP1_MTX_SIZE,
	},
};

static struct uccp_core comet_uccp_cores[] = {
	[0] = {
		.regions = comet_uccp0_regions,
		.num_regions = ARRAY_SIZE(comet_uccp0_regions),
		.num_mc_req = UCC0_MC_REQ_MAX,
	},
	[1] = {
		.regions = comet_uccp1_regions,
		.num_regions = ARRAY_SIZE(comet_uccp1_regions),
		.num_mc_req = UCC1_MC_REQ_MAX,
	},
};

static struct uccp_pdata comet_uccp_pdata = {
	.cores		= comet_uccp_cores,
	.num_cores	= ARRAY_SIZE(comet_uccp_cores),
	.regions	= comet_uccp_global_regions,
	.num_regions	= ARRAY_SIZE(comet_uccp_global_regions),
};

/* register blocks */
static struct resource comet_uccp_resources[] = {
	{
		.start          = UCC0_HOST_BASE_ADDR,
		.end            = UCC0_HOST_BASE_ADDR + UCC0_HOST_SIZE,
		.flags          = IORESOURCE_MEM
				| UCCP_RES(0, UCCP_RES_HOSTSYSBUS),
	},
	{
		.start          = UCC0_MC_BASE_ADDR,
		.end            = UCC0_MC_BASE_ADDR + UCC0_MC_SIZE,
		.flags          = IORESOURCE_MEM
				| UCCP_RES(0, UCCP_RES_MCREQ),
	},
	{
		.start          = UCC1_HOST_BASE_ADDR,
		.end            = UCC1_HOST_BASE_ADDR + UCC1_HOST_SIZE,
		.flags          = IORESOURCE_MEM
				| UCCP_RES(1, UCCP_RES_HOSTSYSBUS),
	},
	{
		.start          = UCC1_MC_BASE_ADDR,
		.end            = UCC1_MC_BASE_ADDR + UCC1_MC_SIZE,
		.flags          = IORESOURCE_MEM
				| UCCP_RES(1, UCCP_RES_MCREQ),
	},
};

static struct platform_device uccp_device = {
	.name		= "uccp",
	.id		= -1,
	.dev		= {
		.platform_data	= &comet_uccp_pdata,
	},
	.resource	= comet_uccp_resources,
	.num_resources	= ARRAY_SIZE(comet_uccp_resources),
};


/*---------------------------- PDC Setup -------------------------------------*/

/* Stores the boot time 32khz clock rate for drivers to access */
unsigned long clk32k_bootfreq;
EXPORT_SYMBOL_GPL(clk32k_bootfreq);

static void comet_pdc_init(void)
{
	unsigned int soc_gpio2, div;
	int lstat;

	/*
	 * The driver needs to know the boot time 32KHz clock frequency so it
	 * can compensate for incorrect clock rates during power down.
	 */
	clk32k_bootfreq = get_32kclock();

	/*
	 * Set up PDC clock input to use XTAL1 with a divider. XTAL1 is
	 * likely less accurate (due to physical oscillator accuracy)
	 * than what might be available as XTAL3, but XTAL3 cannot be
	 * relied upon to reset the system.
	 */
	div = (2 * get_xtal1() / CLK32K_DESIRED_FREQUENCY + 1) / 2;
	set_32kclock_src(1, div);

	/* bypass XTAL3 */
	__global_lock2(lstat);
	soc_gpio2 = readl(PDC_BASE_ADDR + PDC_SOC_GPIO_CONTROL2);
	soc_gpio2 |= PDC_SOC_GPIO2_XTAL3_BYPASS;
	soc_gpio2 &= ~PDC_SOC_GPIO2_XTAL3_EN;
	writel(soc_gpio2, PDC_BASE_ADDR + PDC_SOC_GPIO_CONTROL2);
	__global_unlock2(lstat);
}

/*---------------------- Event/Timestamp counter Setup------------------------*/
#ifdef CONFIG_IMG_EVT
static struct resource comet_evt_resource = {
		.start = EVENT_TS_BASE_ADDR,
		.end = EVENT_TS_BASE_ADDR + EVENT_TS_SIZE,
		.flags = IORESOURCE_MEM,
};

static struct platform_device comet_evt_device = {
		.name = "img-eventtimer",
		.id = -1,
		.resource = &comet_evt_resource,
		.num_resources = 1,
};
#endif

/*-----------------------Platform Devices Setup-------------------------------*/

static struct platform_device *comet_devices[] __initdata = {
	&rng_device,
	&uccp_device,
#ifdef CONFIG_IMG_EVT
	&comet_evt_device,
#endif
};

static void __init comet_clocks_init(void)
{
	unsigned int lstat, temp, clkenab2;
	__global_lock2(lstat);

	/* initialise HEP clocks to off */
	clkenab2 = readl(CR_TOP_CLKENAB2);
	temp = readl(CR_HEP_CLK_EN);
	temp &= ~CR_2D_CLK_EN;
	/* only stop PDP/PDI clock if pixel clock isn't already going */
	if (!(clkenab2 & (1 << CR_TOP_PIXEL_CLK_2_EN_BIT)))
		temp &= ~CR_PDP_PDI_CLK_EN;
	writel(temp, CR_HEP_CLK_EN);

	__global_unlock2(lstat);

}

/**
 * comet_prepare_reset() - Prepare SoC for reset.
 *
 * This ensures workarounds can take place for reset/poweroff related quirks.
 */
void comet_prepare_reset(void)
{
	/*
	 * Due to a hardware bug the 32.768KHz clock resets to being derived
	 * from XTAL1 with a divider of 768 when the power is cut. Setting
	 * explicitly beforehand triggers the notifier chain so that drivers of
	 * devices that use it can compensate.
	 */
	set_32kclock_src(1, 768);
}

/* board reboot callbacks */
void (*board_restart)(char *cmd) = NULL;
void (*board_halt)(void) = NULL;
void (*board_power_off)(void) = NULL;

static void comet_restart(char *cmd)
{
	comet_prepare_reset();

	if (board_restart)
		board_restart(cmd);

	comet_pdc_restart();
}

static void comet_halt(void)
{
	comet_prepare_reset();

	if (board_halt)
		board_halt();
}

static void comet_power_off(void)
{
	comet_prepare_reset();

	if (board_power_off)
		board_power_off();

	comet_pdc_power_off();
}

#ifdef CONFIG_METAG_SUSPEND_MEM
static int comet_suspend(void)
{
	return 0;
}

static void comet_resume(void)
{
	comet_pdc_init();
	comet_rng_init();
}
#else
#define comet_suspend NULL
#define comet_resume NULL
#endif	/* CONFIG_METAG_SUSPEND_MEM */

static struct syscore_ops comet_syscore_ops = {
	.suspend = comet_suspend,
	.resume  = comet_resume,
};

bool comet_is_evaluation_silicon(void)
{
	unsigned int core_rev = readl(CR_COMET_CORE_REV);
	return unlikely((core_rev & CR_COMET_CORE_REV_MAJOR_BITS)
			== CR_COMET_CORE_REV_MAJOR_ES1);
}

void __init comet_init_early(void)
{
	/*
	 * It's an easy mistake to forget SOC_COMET_ES1.
	 * It is better to panic now rather than mysteriously going wrong later.
	 */
#ifndef CONFIG_SOC_COMET_ES1
	if (comet_is_evaluation_silicon())
		panic("This kernel doesn't support prototype (ES1) silicon. "
		      "Please enable SOC_COMET_ES1.");
#endif
}

void __init comet_init_machine(void)
{
	printk(KERN_INFO"Comet Soc: XTAL1 is %ld HZ\n", get_xtal1());
	printk(KERN_INFO"Comet Soc: XTAL2 is %ld HZ\n", get_xtal2());
	printk(KERN_INFO"Comet Soc: XTAL3 is %ld HZ\n", get_xtal3());
	printk(KERN_INFO"Comet Soc: Clocks:\n");
	printk(KERN_INFO"Comet Soc: \tSys Clock (Pre deleter) %ld HZ\n",
		get_sysclock_undeleted());
	printk(KERN_INFO"Comet Soc: \tDDR Clock %ld HZ\n",
			get_ddrclock());

	/* Temporary backwards compatibility */
	clk_add_alias("pdi",	NULL,	"pdp",	NULL);

	comet_clocks_init();
	comet_rng_init();

	platform_add_devices(comet_devices, ARRAY_SIZE(comet_devices));

	register_syscore_ops(&comet_syscore_ops);

	soc_restart = comet_restart;
	soc_halt = comet_halt;
	pm_power_off = comet_power_off;

	comet_pdc_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *tz1090_boards_compat[] __initdata = {
	"toumaz,tz1090",
	NULL,
};

MACHINE_START(TZ1090, "Generic TZ1090")
	.dt_compat	= tz1090_boards_compat,
	TZ1090_MACHINE_DEFAULTS,
MACHINE_END
