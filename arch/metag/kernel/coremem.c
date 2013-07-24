/*
 * Copyright 2010 Imagination Technologies Ltd.
 *
 * These functions allow code to be pushed into core memory so that DDR can be
 * put into self-refresh during standby.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <asm/cacheflush.h>
#include <asm/coremem.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/cachepart.h>
#include <asm/global_lock.h>
#include <asm/metag_isa.h>
#include <asm/metag_mem.h>

/* private coremem region flags */
#define METAG_COREMEM_MASK	0x0000ffff	/* mask of bits to compare */
#define METAG_COREMEM_BUSY_BIT	31		/* busy bit */

struct metag_coremem_region *metag_coremem_alloc(unsigned int flags,
						 unsigned int size)
{
	/*
	 * Look in metag_coremems for regions that we can use.
	 * metag_coremems should be defined by SoC's.
	 */
	int i;
	for (i = 0; i < metag_coremems_sz; ++i) {
		/* METAG_COREMEM_MASK bits of flags must match */
		if (METAG_COREMEM_MASK & (metag_coremems[i].flags ^ flags))
			continue;

		/* must be big enough */
		if (!(flags & METAG_COREMEM_CACHE) &&
		    size > metag_coremems[i].size)
			continue;

		/* one at a time */
		if (test_and_set_bit_lock(METAG_COREMEM_BUSY_BIT,
					  &metag_coremems[i].flags))
			continue;

		return &metag_coremems[i];
	}
	return NULL;
}

void metag_coremem_free(struct metag_coremem_region *region)
{
	/* finish with locked in cache */
	if ((region->flags & METAG_COREMEM_CACHE) && region->start) {
		metag_cache_unlock(region->flags);
		cachepart_restore_iglobal(&region->data);
		region->start = NULL;
	}
	/* reset region and unmark busy flag */
	region->pos = 0;
	clear_bit_unlock(METAG_COREMEM_BUSY_BIT, &region->flags);
}

void *metag_coremem_push(struct metag_coremem_region *region,
			 void *start, unsigned long size)
{
	void *ret;
	if (region->flags & METAG_COREMEM_CACHE) {
		/* only do this once */
		if (region->start)
			return NULL;
		/* try and lock the pushed memory into the cache */
		if (cachepart_min_iglobal(size, &region->data))
			return NULL;
		ret = (void *)metag_cache_lock(region->flags, __pa(start),
						size);
		if (!ret) {
			cachepart_restore_iglobal(&region->data);
			return NULL;
		}
		region->start = ret;
	} else {
		/* don't overflow */
		if (size > region->size - region->pos)
			return NULL;
		/* push at region->pos */
		ret = region->start + region->pos;
		memcpy(ret, start, size);
		region->pos += size;
	}
	return ret;
}

#ifdef CONFIG_METAG_META21

#define MMCU_TnCCM_ICTRL(n)	\
	(MMCU_T0CCM_ICCTRL + MMCU_TnCCM_xxCTRL_STRIDE*(n))
#define MMCU_TnCCM_DCTRL(n)	\
	(MMCU_T0CCM_DCCTRL + MMCU_TnCCM_xxCTRL_STRIDE*(n))
#define MMCU_TnTLBINVALIDATE(n)	\
	(LINSYSCFLUSH_TxMMCU_BASE + LINSYSCFLUSH_TxMMCU_STRIDE*(n))

unsigned long metag_cache_lock(unsigned int flags, unsigned long phys,
			       unsigned long size)
{
	unsigned int hwt = hard_processor_id();
	unsigned long ccmctrl_addr;
	unsigned int ccmctrl;
	unsigned long ccr; /* core cached region in physical memory */
	unsigned int offset;
	unsigned long rounded_size;
	int rounded_size_sh;
	unsigned long i;

	if (!size)
		return 0;
	/* Which cache are we referring to? instruction or data? */
	if (flags & METAG_COREMEM_IMEM) {
		ccmctrl_addr = MMCU_TnCCM_ICTRL(hwt);
		ccr = LINCORE_ICACHE_BASE;
	} else if (flags & METAG_COREMEM_DMEM) {
		ccmctrl_addr = MMCU_TnCCM_DCTRL(hwt);
		ccr = LINCORE_DCACHE_BASE;
	} else {
		return 0;
	}

	/* First take account of offset of memory in page */
	offset = offset_in_page(phys);
	rounded_size = size + offset;
	phys -= offset;
	ccr += offset;

	/* Get rounded up log2 of size */
	rounded_size_sh = ilog2((rounded_size<<1)-1) - MMCU_TnCCM_REGSZ0_POWER;
	if (rounded_size_sh < 0)
		rounded_size_sh = 0;
	rounded_size = (1 << MMCU_TnCCM_REGSZ0_POWER) << rounded_size_sh;

	/* There's a maximum amount of lockable cache */
	if (rounded_size > MMCU_TnCCM_REGSZ_MAXBYTES)
		return 0;
	/* The size of the current global cache partition may be limited too */
	if (rounded_size > ((flags & METAG_COREMEM_IMEM)
				? get_global_icache_size()
				: get_global_dcache_size()))
		return 0;

	ccmctrl = metag_in32(ccmctrl_addr);
	if (ccmctrl & MMCU_TnCCM_ENABLE_BIT)
		/* already enabled */
		return 0;

	/* set the physical mapping (to start of cache data region) */
	ccmctrl ^= (ccmctrl ^ phys) & MMCU_TnCCM_ADDR_BITS;
	/* set the size */
	ccmctrl &= ~MMCU_TnCCM_REGSZ_BITS;
	ccmctrl |= rounded_size_sh << MMCU_TnCCM_REGSZ_S;
	/* WIN3 */
	ccmctrl |= MMCU_TnCCM_WIN3_BIT;
	/* Enable */
	ccmctrl |= MMCU_TnCCM_ENABLE_BIT;
	metag_out32(ccmctrl, ccmctrl_addr);

	if (flags & METAG_COREMEM_DMEM) {
		/*
		 * Lock cache lines and TLB entries, up to size. This is done by
		 * reading from the locked cache at DCACHE_LINE_BYTES intervals.
		 */
		unsigned int pf_start = ccr & -DCACHE_LINE_BYTES;
		unsigned int pf_end = ccr + size;
		for (i = pf_start; i < pf_end; i += DCACHE_LINE_BYTES)
			metag_in32(i);
	}

	return ccr;
}

void metag_cache_unlock(unsigned int flags)
{
	unsigned int hwt = hard_processor_id();
	unsigned int lstat;
	unsigned long ccmctrl_addr;
	unsigned int ccmctrl;
	unsigned int size;

	if (flags & METAG_COREMEM_IMEM)
		ccmctrl_addr = MMCU_TnCCM_ICTRL(hwt);
	else if (flags & METAG_COREMEM_DMEM)
		ccmctrl_addr = MMCU_TnCCM_DCTRL(hwt);
	else
		return;

	__global_lock2(lstat);

	ccmctrl = metag_in32(ccmctrl_addr);
	size = (ccmctrl & MMCU_TnCCM_REGSZ_BITS) >> MMCU_TnCCM_REGSZ_S;
	size = (1 << MMCU_TnCCM_REGSZ0_POWER) << size;

	/* Invalidate cache and TLB */
	if (flags & METAG_COREMEM_IMEM)
		metag_code_cache_flush((void *)LINCORE_ICACHE_BASE, size);
	else if (flags & METAG_COREMEM_DMEM)
		metag_data_cache_flush((void *)LINCORE_DCACHE_BASE, size);
	flush_tlb_all();

	/* Disable mem locked in cache */
	if (ccmctrl & MMCU_TnCCM_ENABLE_BIT) {
		ccmctrl &= ~MMCU_TnCCM_ENABLE_BIT;
		metag_out32(ccmctrl, ccmctrl_addr);
	}
	__global_unlock2(lstat);
}
#endif
