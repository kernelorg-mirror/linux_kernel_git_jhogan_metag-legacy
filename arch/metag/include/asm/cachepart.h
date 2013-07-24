/*
 * Meta cache partition manipulation.
 *
 * Copyright 2010 Imagination Technologies Ltd.
 */

#ifndef _METAG_CACHEPART_H_
#define _METAG_CACHEPART_H_

/**
 * get_dcache_size() - Get size of data cache.
 */
unsigned int get_dcache_size(void);

/**
 * get_icache_size() - Get size of code cache.
 */
unsigned int get_icache_size(void);

/**
 * get_global_dcache_size() - Get the thread's global dcache.
 *
 * Returns the size of the current thread's global dcache partition.
 */
unsigned int get_global_dcache_size(void);

/**
 * get_global_icache_size() - Get the thread's global icache.
 *
 * Returns the size of the current thread's global icache partition.
 */
unsigned int get_global_icache_size(void);

/**
 * cachepart_min_iglobal() - Ensure we have @min_size global icache.
 * @min_size:	Minimum size of global icache for this thread.
 * @old_val:	Pointer to area to store old cache information so it can be
 *		restored to it's previous setting.
 *
 * If possible, this will take cache from the thread local cache to satisfy the
 * @min_size requirement.  Returns 0 on success, or an error code on failure.
 * The operation can be reversed with cachepart_restore_iglobal().
 */
int cachepart_min_iglobal(unsigned int min_size, unsigned int *old_val);

/**
 * cachepart_restore_iglobal() - Restore global icache to previous setting.
 * @old_val:	Pointer to area to find old cache information so it can be
 *		restored, as given to cachepart_min_iglobal().
 */
void cachepart_restore_iglobal(unsigned int *old_val);

/**
 * check_for_dache_aliasing() - Ensure that the bootloader has configured the
 * dache and icache properly to avoid aliasing
 * @thread_id: Hardware thread ID
 *
 */
void check_for_cache_aliasing(int thread_id);

#endif
