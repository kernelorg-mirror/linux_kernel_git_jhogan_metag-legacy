/*
 * PDP Scaled Video Framebuffer
 *
 * Copyright (c) 2008 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _PDPFB_VID_H
#define _PDPFB_VID_H

#ifdef CONFIG_FB_PDP_VID

#include "pdpfb.h"

struct pdpfb_stream *pdpfb_vid_get_stream(void);

#endif

#endif
