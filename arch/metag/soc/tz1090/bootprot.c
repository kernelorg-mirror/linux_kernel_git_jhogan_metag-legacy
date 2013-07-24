/*
 * bootprot.c
 *
 * Boot protocol abstraction API (DFU, resume from suspend-to-RAM etc).
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 */

#include <linux/io.h>
#include <asm/soc-tz1090/bootprot.h>

#define BOOT_REG		0x0
#define BOOT_ATTEMPTS		0x00000003
#define BOOT_DFU		0x00000010
/* if !BOOT_DFU { */
#define BOOT_BOOTMODE		0x00000fe0
#define BOOT_BOOTMODE_S	5
#define BOOT_BOOTMODE_NORMAL   (0 << BOOT_BOOTMODE_S)
#define BOOT_BOOTMODE_RESUME   (1 << BOOT_BOOTMODE_S)
/*   if BOOT_BOOTMODE_RESUME { */
#define RESUME_BOOT_CHECKSUM	0x00001000
#define RESUME_BOOT_MASK	0x00001ff3
#define RESUME_FUNCTION_REG	0x4
#define RESUME_DATA_REG		0x8
#define RESUME_CHECKSUM_REG	0xc
/*   } */
/* } */

/**
 * bootprot_normal_boot() - set up a normal boot.
 * @swprot0:	Address of protected registers.
 *
 * Set up protected registers for a normal boot.
 */
void bootprot_normal_boot(unsigned long swprot0)
{
	writel(0, swprot0 + BOOT_REG);
}

/**
 * bootprot_normal_suspend_ram() - set up for resume from suspend-to-RAM.
 * @swprot0:	Address of protected registers.
 * @resume:	Resume function.
 * @data:	Data to pass to resume function.
 *
 * Set up protected registers for a resume from suspend-to-RAM.
 */
void bootprot_suspend_ram(unsigned long swprot0,
			  int (*resume)(void *),
			  void *data)
{
	unsigned long boot = BOOT_BOOTMODE_RESUME;
	writel(boot,			swprot0 + BOOT_REG);
	writel((unsigned long)resume,	swprot0 + RESUME_FUNCTION_REG);
	writel((unsigned long)data,	swprot0 + RESUME_DATA_REG);
}

/**
 * bootprot_resume_ram() - unset the suspend state.
 * @swprot0:	Address of protected registers.
 *
 * Undo set up protected registers for resume from suspend-to-RAM.
 */
void bootprot_resume_ram(unsigned long swprot0)
{
	bootprot_normal_boot(swprot0);
	writel(0, swprot0 + RESUME_FUNCTION_REG);
	writel(0, swprot0 + RESUME_DATA_REG);
}
