/*
 * sound/tansen.h -- GTI port API for the tansen codec
 *
 * Copyright 2013, Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TANSEN_GTI_API
#define __TANSEN_GTI_API

#define GTI_MAKE_CTRL_REG(a, b, c, d) \
	(((a & 1) << 3) | ((b & 1) << 2) | ((c & 1) << 1) | ((d & 1) << 0))

u32 gti_read(void __iomem *port, unsigned long reg);
void gti_write(void __iomem *port, unsigned long reg,
		unsigned long value);
void gti_reset(void __iomem *port, int reset);

#endif /* __TANSEN_GTI_API */
