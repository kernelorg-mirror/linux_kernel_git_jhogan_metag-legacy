/*
 * Copyright 2010 Imagination Technologies Ltd.
 *
 * Core memory regions.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <asm/coremem.h>

struct metag_coremem_region metag_coremems[] = {
	{
		.flags	= METAG_COREMEM_IMEM,
		.start	= (char *)0x80000000,
		.size	= 0x10000,
	},
	{
		.flags	= METAG_COREMEM_DMEM,
		.start	= (char *)0x82000000,
		.size	= 0x10000,
	},
	{
		.flags	= METAG_COREMEM_ICACHE,
	},
	{
		.flags	= METAG_COREMEM_DCACHE,
	},
};

unsigned int metag_coremems_sz = ARRAY_SIZE(metag_coremems);
