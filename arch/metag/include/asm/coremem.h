#ifndef _METAG_COREMEM_H_
#define _METAG_COREMEM_H_

#ifdef CONFIG_METAG_COREMEM

/* Core memory region flags. */
#define METAG_COREMEM_IMEM	0x0001	/* instruction memory */
#define METAG_COREMEM_DMEM	0x0002	/* data memory */
#define METAG_COREMEM_CACHE	0x0100	/* cache memory */
#define METAG_COREMEM_ICACHE	(METAG_COREMEM_IMEM | METAG_COREMEM_CACHE)
#define METAG_COREMEM_DCACHE	(METAG_COREMEM_DMEM | METAG_COREMEM_CACHE)

/**
 * struct metag_coremem_region - A region of core memory.
 * @flags:	Bitwise OR of METAG_COREMEM_*.
 * @start:	Start of region (if not cache).
 * @size:	Size of region (if not cache).
 * @pos:	Current position in region.
 * @data:	Private data.
 */
struct metag_coremem_region {
	unsigned long flags;
	char *start;
	unsigned int size;
	unsigned int pos;
	unsigned int data;
};

/**
 * metag_coremem_alloc() - Initialise access to core memory.
 * @flags:	Bitwise or of METAG_COREMEM_* flags.
 * @size:	Minimum size required.
 *
 * This gets a coremem resource. Use metag_coremem_free() to indicate that
 * the memory has been finished with. Returns NULL on failure.
 */
struct metag_coremem_region *metag_coremem_alloc(unsigned int flags,
						 unsigned int size);

/**
 * metag_coremem_free() - Finish using some core memory.
 * @region:	Coremem region returned from metag_coremem_alloc().
 *
 * Indicates that the core memory region has been finished with and can be
 * reused again by the system (e.g. for cache).
 */
void metag_coremem_free(struct metag_coremem_region *region);

/**
 * metag_coremem_push() - Push code or data into a coremem section.
 * @region:	Coremem region returned from metag_coremem_alloc().
 * @start:  	Start address of memory to copy from.
 * @size:   	Size of memory to copy.
 *
 * The specified memory is pushed onto the next free area in the coremem region.
 * The address of the location in coremem where the memory has been copied to is
 * returned, or NULL if there isn't enough space left in the region.
 *
 * If the METAG_COREMEM_ICACHE flags were specified, then the region won't be
 * locked into the cache until the code is executed or icache prefetched.
 */
void *metag_coremem_push(struct metag_coremem_region *region,
			 void *start, unsigned long size);


/*
 * SoC specific coremem regions.
 * Each SoC that uses suspend should define this.
 */
extern struct metag_coremem_region metag_coremems[];

/* ARRAY_SIZE(metag_coremems) */
extern unsigned int metag_coremems_sz;

/**
 * metag_cache_lock() - Lock memory into core cache for this hw thread.
 * @flags:	Which cache, METAG_COREMEM_IMEM or METAG_COREMEM_DMEM.
 * @phys:	Physical start address.
 * @size:	Amount of memory to lock into cache.
 *
 * Locks the specified physical memory into core cache mode so that it can be
 * accessed and modified directly in the cache. The physical address returned
 * points to where the cache can be found. Modifications here will not modify
 * the original memory. When the memory has been finished with, call
 * metag_cache_unlock().
 */
unsigned long metag_cache_lock(unsigned int flags, unsigned long phys,
			       unsigned long size);

/**
 * metag_cache_unlock() - Unlock core cache locked memory for this hw thread.
 * @flags:	Which cache, METAG_COREMEM_IMEM or METAG_COREMEM_DMEM.
 *
 * Unlocks either instruction or data locked into core cache.
 */
void metag_cache_unlock(unsigned int flags);

#endif

#endif
