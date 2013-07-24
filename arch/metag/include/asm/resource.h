#ifndef _METAG_RESOURCE_H
#define _METAG_RESOURCE_H

/* Chorus2 needs to lock a lot of pages, so set no locked page limit */
#ifdef CONFIG_SOC_CHORUS2
#undef MLOCK_LIMIT
#define MLOCK_LIMIT	RLIM_INFINITY
#endif

#include <uapi/asm/resource.h>

#endif
