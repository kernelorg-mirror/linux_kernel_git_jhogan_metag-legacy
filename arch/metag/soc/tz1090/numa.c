/*
 *  Multiple memory node support for Comet SoCs.
 *
 *  Copyright (C) 2010  Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/mmzone.h>

#include <asm/mmzone.h>

void __init soc_mem_setup(void)
{
	setup_bootmem_node(1, 0xe0200200, 0xe0260000);
}
