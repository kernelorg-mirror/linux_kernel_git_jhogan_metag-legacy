/*
 * Copyright (C) 2010 Imagination Technologies Ltd.
 */

#include <linux/init.h>
#include <asm/tcm.h>

#define CORE_I_TAG	0x1
#define CORE_D_TAG	0x2
#define INTERNAL_TAG	0x3

#define CORE_RAM_I_BASE		0x80000000
#define CORE_RAM_I_SIZE		0x0000ffff
#define CORE_RAM_D_BASE		0x82000000
#define CORE_RAM_D_SIZE		0x0000ffff
#define INTERNAL_RAM_BASE	0xE0200000
#define INTERNAL_RAM_SIZE	0x0005ffff

static struct tcm_region core_i_region = {
	.tag = CORE_I_TAG,
	.res = {
		.name = "Core Code Memory",
		.start = CORE_RAM_I_BASE,
		.end = CORE_RAM_I_BASE + CORE_RAM_I_SIZE,
		.flags = IORESOURCE_MEM,
	}
};

static struct tcm_region core_d_region = {
	.tag = CORE_D_TAG,
	.res = {
		.name = "Core Data Memory",
		.start = CORE_RAM_D_BASE,
		.end = CORE_RAM_D_BASE + CORE_RAM_D_SIZE,
		.flags = IORESOURCE_MEM,
	}
};

static struct tcm_region internal_region = {
	.tag = INTERNAL_TAG,
	.res = {
		.name = "Internal Memory",
		.start = INTERNAL_RAM_BASE,
		.end = INTERNAL_RAM_BASE + INTERNAL_RAM_SIZE,
		.flags = IORESOURCE_MEM,
	}
};

static int __init add_tcm_regions(void)
{
	tcm_add_region(&core_i_region);
	tcm_add_region(&core_d_region);
	tcm_add_region(&internal_region);

	return 0;
}

core_initcall(add_tcm_regions);
