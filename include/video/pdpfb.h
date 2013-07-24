/*
 * PDP Desktop Graphics Framebuffer
 *
 * Copyright (c) 2008 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _VIDEO_PDPFB_H
#define _VIDEO_PDPFB_H

#include <uapi/video/pdpfb.h>


/* Determine PDP revision from SOC */

#ifdef CONFIG_SOC_CHORUS2
#define PDP_REV 0x010000
#endif

#ifdef CONFIG_SOC_TZ1090
#define PDP_REV 0x010001
#endif

#ifndef PDP_REV
#error PDP revision unknown
#endif

/* Capabilities of the PDP */

#if PDP_REV >= 0x010001
#define PDP_SHARED_BASE
#define PDP_GAMMA		16
#define PDP_VID_VSCALE
#endif

/*
 * Indicates that colour key fields for stream x actually refer to the stream
 * at blend position x, rather than graphics at stream 1 and video at stream 2.
 */
#define PDP_CKEY_BY_BLENDPOS

#include <linux/fb.h>

struct pdp_lcd_size_cfg { /* width and height of panel in mm */
	int dynamic_mode;	/* the screen accepts different modes */
	unsigned long width;
	unsigned long height;
};

#define PDP_ACTIVE_LOW		1
#define PDP_ACTIVE_HIGH		0
#define PDP_CLOCK_NOT_INVERTED	0
#define PDP_CLOCK_INVERTED	1

struct pdp_sync_cfg {
	unsigned int force_vsyncs:1;
	unsigned int hsync_dis:1;	/* sync_ctrl.hsdis */
	unsigned int vsync_dis:1;	/* sync_ctrl.vsdis */
	unsigned int blank_dis:1;	/* sync_ctrl.blnkdis */
	unsigned int blank_pol:1;	/* sync_ctrl.blnkpol */
	unsigned int clock_pol:1;	/* sync_ctrl.clkpol */
	unsigned int sync_slave:1;	/* sync_ctrl.[hv]sslave */
};

struct pdp_hwops {
	void (*set_screen_power)(int pa);
#ifdef PDP_SHARED_BASE
	void (*set_shared_base)(unsigned long pa);
#endif
};

#define PDPFB_PDATA_FIX_SHIFT 11
struct pdp_info {
	int bpp;
	struct fb_videomode lcd_cfg;
	struct pdp_lcd_size_cfg lcd_size_cfg;
	struct pdp_sync_cfg sync_cfg;
	struct pdp_hwops hwops;
#ifdef PDP_VID_VSCALE
	int linestore_len;
	/*
	 * Vertical pitch threshold to switch to bilinear (2-tap) scaling.
	 * In .PDPFB_PDATA_FIX_SHIFT binary fixed point.
	 */
	unsigned int vpitch_bilinear_threshold;
#endif
};

/* Video memory pools */
#define PDPFB_MEMPOOL_MEM	0x00	/* Combined video memory */
#define PDPFB_MEMPOOL_GFXMEM	0x01	/* Graphics plane memory */
#define PDPFB_MEMPOOL_VIDMEM	0x02	/* Video plane memory */
#define PDPFB_MEMPOOL_USER	0x03	/* User provided memory */
#define PDPFB_MEMPOOL_NR_POOLS	4
#define PDPFB_MEMPOOL_USERPLANE	0xfd	/* User provided memory for one plane */
#define PDPFB_MEMPOOL_KERNEL	0xfe	/* Kernel allocated memory */
#define PDPFB_MEMPOOL_NONE	0xff

/* Platform resource numbering in flags */
#define PDPFB_IORES_MEM		PDPFB_MEMPOOL_MEM
#define PDPFB_IORES_GFXMEM	PDPFB_MEMPOOL_GFXMEM
#define PDPFB_IORES_VIDMEM	PDPFB_MEMPOOL_VIDMEM
#define PDPFB_IORES_PDP		0xf0

/* Internal interface for graphics drivers */

#define PDPFB_IRQ_VEVENT0	0x00004	/* safe update for gfx stream */
#define PDPFB_IRQ_HBLNK0	0x00001	/* blanking for frame/gfx stream */

typedef void (*pdpfb_isr_t) (void *arg, u32 mask);

int pdpfb_register_isr(pdpfb_isr_t isr, void *arg, u32 mask);
int pdpfb_unregister_isr(pdpfb_isr_t isr, void *arg, u32 mask);
int pdpfb_wait_for_irq_timeout(u32 irqmask, unsigned long timeout);
int pdpfb_wait_for_irq_interruptible_timeout(u32 irqmask,
						unsigned long timeout);
int pdpfb_wait_vsync(void);

#endif
