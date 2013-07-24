/*
 * PDP Framebuffer
 *
 * Copyright (c) 2008-2012 Imagination Technologies Ltd.
 * Parts Copyright (C) 2009 Nokia Corporation
 *	(custom ISR code derived from drivers/video/omap2/dss/dispc.c)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <video/pdpfb.h>
#include "pdpfb.h"
#include "pdpfb_regs.h"
#include "pdpfb_gfx.h"
#include "pdpfb_vid.h"

/* Physical address and length of video memory */
static ulong videomem_base;
module_param(videomem_base, ulong, 0);
static ulong videomem_len;
module_param(videomem_len, ulong, 0);

/* Physical address and length of graphics plane framebuffer */
#ifndef PDP_SHARED_BASE
static ulong gfx_videomem_base;
module_param(gfx_videomem_base, ulong, 0);
#endif
static ulong gfx_videomem_len = CONFIG_FB_PDP_GFX_VIDEOMEM;
module_param(gfx_videomem_len, ulong, 0);

/* Physical address and length of video plane framebuffer */
#ifdef CONFIG_FB_PDP_VID
#ifndef PDP_SHARED_BASE
static ulong vid_videomem_base;
module_param(vid_videomem_base, ulong, 0);
#endif
static ulong vid_videomem_len = CONFIG_FB_PDP_VID_VIDEOMEM;
module_param(vid_videomem_len, ulong, 0);
#endif

/* report number of VBLANKs taken to update LUT */
/*#define LUT_UPD_REPORT_PERF*/

/* Groups of registers that may need updating on VEVENT */
#define PDP_UPDATE_SYNC			0x00000001
#define PDP_UPDATE_CLUT			0x00000002

struct pdpfb_lut {
	spinlock_t lock;
	u32 palette[PDP_PALETTE_NR];	/* hardware specific format */
#ifdef CONFIG_FB_PDP_QUEUE_CLUT
	u16 count;			/* number of items in queue */
	u8 position;			/* next index to update */
	u8 queue[PDP_PALETTE_NR];	/* circular queue */
	u8 rqueue[PDP_PALETTE_NR];	/* reverse of queue */
#else
	u16 min, max;			/* first and last item to update */
#endif
};

struct pdpfb_update_sync {
	u32 hsync1, hsync2, hsync3;
	u32 hde_ctrl;
	u32 str1blend2;
};

struct pdpfb_updates {
	spinlock_t lock;
	u32 updates;			/* PDP_UPDATE_* */
	struct pdpfb_update_sync sync;	/* PDP_UPDATE_SYNC */
};

struct pdpfb_isr_data {
	pdpfb_isr_t	isr;
	void		*arg;
	u32		mask;
};

#define PDPFB_MAX_NR_ISRS 8


#ifdef CONFIG_FB_PDP_VID
#define PDPFB_STREAM_NR 2
#else
#define PDPFB_STREAM_NR 1
#endif

struct pdpfb_rgb_fmt {
	struct fb_bitfield r, g, b;
};

#define PDPFB_MEMPOOLF_KERNEL	0x1	/* Kernel allocated memory */

struct pdpfb_mem_pool {
	unsigned long phys_base;
	void *base;
	unsigned long size;
	unsigned int flags;
	unsigned long allocated;
};

static char *pdpfb_mem_pool_names[] = {
	[PDPFB_MEMPOOL_MEM]	= "videomem",
	[PDPFB_MEMPOOL_GFXMEM]	= "gfx videomem",
	[PDPFB_MEMPOOL_VIDMEM]	= "vid videomem",
	[PDPFB_MEMPOOL_USER]	= "usermem",
};

enum pdpfb_power_state {
	PDPFB_POWER_NA			= -1,
	PDPFB_POWER_BACKLIT		= FB_BLANK_UNBLANK,
	PDPFB_POWER_PANEL_POWERED	= FB_BLANK_NORMAL,
	PDPFB_POWER_SYNC_ENABLED	= FB_BLANK_VSYNC_SUSPEND,
	PDPFB_POWER_PANEL_ENABLED	= FB_BLANK_HSYNC_SUSPEND,
	PDPFB_POWER_FULLY_OFF		= FB_BLANK_POWERDOWN,
	PDPFB_POWER_MAX
};

struct pdpfb_priv {
	void __iomem *base;
	struct pdpfb_mem_pool pools[PDPFB_MEMPOOL_NR_POOLS];
	unsigned int irq;
	struct clk *pdp_clk;
	struct clk *pixel_clk;
	int pdp_clk_en;
	int pixel_clk_en;
	struct device *device;
	struct fb_videomode mode;
	u32 pseudo_palette[PDP_PALETTE_NR];
	enum pdpfb_power_state power;
	struct pdpfb_updates upd;
	struct pdpfb_lut lut;
	struct pdpfb_rgb_fmt palette_fmt;
	u32 bgnd_col;
	struct pdpfb_stream *streams[PDPFB_STREAM_NR];

	spinlock_t irq_lock;
	int isr_disable;
	struct pdpfb_isr_data registered_isr[PDPFB_MAX_NR_ISRS];

	unsigned int vsync_count;

	int final_blank;
	unsigned int reconfiguring:1;
	unsigned int no_blank_emit:1;
};

/* The one and only */
static struct pdpfb_priv *pdpfb_priv;

static int pdpfb_init_mempool(struct pdpfb_priv *priv, int type,
			      unsigned long phys, unsigned long len);

static void pdpfb_pdp_clk_enable(struct pdpfb_priv *priv)
{
	if (!priv->pdp_clk_en) {
		clk_enable(priv->pdp_clk);
		priv->pdp_clk_en = 1;
	}
}

static void pdpfb_pdp_clk_disable(struct pdpfb_priv *priv)
{
	if (priv->pdp_clk_en) {
		clk_disable(priv->pdp_clk);
		priv->pdp_clk_en = 0;
	}
}

static void pdpfb_pixel_clk_enable(struct pdpfb_priv *priv)
{
	if (!priv->pixel_clk_en) {
		clk_prepare_enable(priv->pixel_clk);
		priv->pixel_clk_en = 1;
	}
}

static void pdpfb_pixel_clk_disable(struct pdpfb_priv *priv)
{
	if (priv->pixel_clk_en) {
		clk_disable_unprepare(priv->pixel_clk);
		priv->pixel_clk_en = 0;
	}
}

/* Register access */

#define PDUMP_TAG "pdpfb pdump: "

#ifdef CONFIG_FB_PDP_PDUMP_V1
#define PDUMP_WR_FMT "WRW :REG:%08x %08x\n"
#define PDUMP_RD_FMT "RDW :REG:%08x\n"
#endif

#ifdef CONFIG_FB_PDP_PDUMP_V2
#define PDUMP_WR_FMT "WRW :REGMET_PDP:%#x %#x\n"
#define PDUMP_RD_FMT "RDW :REGMET_PDP:%#x\n"
#endif

void pdpfb_write(struct pdpfb_priv *priv,
		      unsigned int reg_offs, unsigned int data)
{
#ifdef CONFIG_FB_PDP_PDUMP
	printk(PDUMP_TAG PDUMP_WR_FMT,
	       reg_offs, data);
#endif
	iowrite32(data, priv->base + reg_offs);
}

unsigned int pdpfb_read(struct pdpfb_priv *priv,
			     unsigned int reg_offs)
{
#ifdef CONFIG_FB_PDP_PDUMP
	printk(PDUMP_TAG PDUMP_RD_FMT,
	       reg_offs);
#endif
	return ioread32(priv->base + reg_offs);
}

/* LUT queue */

static void pdpfb_lut_init(struct pdpfb_priv *priv)
{
	spin_lock_init(&priv->lut.lock);
#ifdef CONFIG_FB_PDP_QUEUE_CLUT
	priv->lut.count = 0;
#else
	priv->lut.min = PDP_PALETTE_NR;
	priv->lut.max = 0;
#endif
}

/*
 * lut_num_entries: Get number of LUT entries to write.
 * lut_upd_entries: Write LUT updates. priv->lut.lock must be held.
 */
#ifdef CONFIG_FB_PDP_QUEUE_CLUT

static inline int _pdpfb_lut_num_entries(struct pdpfb_priv *priv)
{
	return priv->lut.count;
}

static inline int _pdpfb_lut_upd(struct pdpfb_priv *priv)
{
	int i = priv->lut.count;
	u8 pos = priv->lut.position;
	/* pos intentionally wraps around */
	for (; i; --i, ++pos) {
		u8 id = priv->lut.queue[pos];
		u32 val = priv->lut.palette[id];
		pdpfb_write(priv, PDP_PALETTE1,
				id << PDP_PALETTE1_LUTADDR_OFFSET);
		pdpfb_write(priv, PDP_PALETTE2, val);
		if (unlikely(val != pdpfb_read(priv, PDP_PALETTE2)))
			break;
	}
	priv->lut.count = i;
	priv->lut.position = pos;
	return i;
}

static inline void _pdpfb_queue_lut_upd(struct pdpfb_priv *priv,
					u8 id, u32 palette_rgb)
{
	u8 position = priv->lut.position;
	u16 count = priv->lut.count;
	u8 index = priv->lut.rqueue[id];
	u8 relindex = index - position;

	priv->lut.palette[id] = palette_rgb;
	/* last queue index not pending, or referring to different id */
	if (likely(relindex >= count || priv->lut.queue[index] != id)) {
		priv->lut.rqueue[id] = index = position + count;
		priv->lut.queue[index] = id;
		priv->lut.count = count+1;
	}
}

#else /*CONFIG_FB_PDP_QUEUE_CLUT*/

static inline int _pdpfb_lut_num_entries(struct pdpfb_priv *priv)
{
	if (priv->lut.max < priv->lut.min)
		return 0;
	return 1 + priv->lut.max - priv->lut.min;
}

static inline int _pdpfb_lut_upd(struct pdpfb_priv *priv)
{
	int i;
	for (i = priv->lut.min; i <= priv->lut.max; ++i) {
		pdpfb_write(priv, PDP_PALETTE1,
				i << PDP_PALETTE1_LUTADDR_OFFSET);
		pdpfb_write(priv, PDP_PALETTE2, priv->lut.palette[i]);
	}
	priv->lut.min = PDP_PALETTE_NR;
	priv->lut.max = 0;
	/* read any PDP register, without this we get flickering artifacts */
	pdpfb_read(priv, PDP_SIGNAT);
	return 0;
}

static inline void _pdpfb_queue_lut_upd(struct pdpfb_priv *priv,
					u8 id, u32 palette_rgb)
{
	priv->lut.palette[id] = palette_rgb;
	if (id < priv->lut.min)
		priv->lut.min = id;
	if (id > priv->lut.max)
		priv->lut.max = id;
}

#endif /*CONFIG_FB_PDP_QUEUE_CLUT*/

/*
 * Write any queued LUT updates (for use in VBLANK).
 * Must not hold priv->upd.lock going into this function.
 */
static void pdpfb_lut_upd(struct pdpfb_priv *priv)
{
	int new_count;
#ifdef LUT_UPD_REPORT_PERF
	int init_count;
#endif

	/* Don't bother updating LUT unless it's switched on */
	if (!GET_FIELD(pdpfb_read(priv, PDP_STR1SURF), PDP_STR1SURF_USELUT))
		return;

	spin_lock(&priv->lut.lock);
#ifdef LUT_UPD_REPORT_PERF
	init_count = _pdpfb_lut_num_entries(priv);
#endif

	new_count = _pdpfb_lut_upd(priv);
	if (!new_count) {
		/* Updating CLUT finished */
		spin_lock(&priv->upd.lock);
		priv->upd.updates &= ~PDP_UPDATE_CLUT;
		spin_unlock(&priv->upd.lock);
	}

	spin_unlock(&priv->lut.lock);

#ifdef LUT_UPD_REPORT_PERF
	if (new_count != init_count) {
		static unsigned int vevent_count;
		static unsigned int luti_count;
		++vevent_count;
		luti_count += init_count-new_count;
		if (!new_count) {
			dev_dbg(priv->device,
				"updated %u LUTs in %u VEVENTs (%u each)\n",
				luti_count,
				vevent_count,
				luti_count / vevent_count);
			vevent_count = 0;
			luti_count = 0;
		}
	}
#endif
}

static void pdpfb_sync_upd(struct pdpfb_priv *priv)
{
	pdpfb_write(priv, PDP_HSYNC1, priv->upd.sync.hsync1);
	pdpfb_write(priv, PDP_HSYNC2, priv->upd.sync.hsync2);
	pdpfb_write(priv, PDP_HSYNC3, priv->upd.sync.hsync3);

	pdpfb_write(priv, PDP_HDECTRL, priv->upd.sync.hde_ctrl);

	priv->upd.updates &= ~PDP_UPDATE_SYNC;
}

/* triggered on vevent0 in interrupt context */
static void pdpfb_interrupt_upd(void *arg, u32 mask)
{
	struct pdpfb_priv *priv = (struct pdpfb_priv *)arg;

	/* Update as many colour lookup table entries as possible */
	if (priv->upd.updates & PDP_UPDATE_CLUT)
		pdpfb_lut_upd(priv);
	spin_lock(&priv->upd.lock);
	/* Update fields that would cause artifacts elsewhere */
	if (priv->upd.updates & PDP_UPDATE_SYNC)
		pdpfb_sync_upd(priv);
	/* Disable interrupt if no more updates */
	if (!priv->upd.updates)
		pdpfb_unregister_isr(pdpfb_interrupt_upd, arg,
					PDPFB_IRQ_VEVENT0);
	spin_unlock(&priv->upd.lock);
}

static inline void _pdpfb_start_queue_lut_upd(struct pdpfb_priv *priv,
					      unsigned long *flags)
{
	spin_lock_irqsave(&priv->lut.lock, *flags);
}

/* must correspond to _pdpfb_start_queue_lut_upd */
static inline void _pdpfb_end_queue_lut_upd(struct pdpfb_priv *priv,
					    unsigned long *flags)
{
	/* Start updating CLUT */
	spin_lock(&priv->upd.lock); /* interrupts are already disabled */
	/* If not in lut mode, delay update until it's enabled */
	if (GET_FIELD(pdpfb_read(priv, PDP_STR1SURF), PDP_STR1SURF_USELUT)) {
		priv->upd.updates |= PDP_UPDATE_CLUT;
		pdpfb_register_isr(pdpfb_interrupt_upd, priv,
				   PDPFB_IRQ_VEVENT0);
	}
	spin_unlock(&priv->upd.lock);

	spin_unlock_irqrestore(&priv->lut.lock, *flags);
}

void pdpfb_enable_palette(struct pdpfb_priv *priv)
{
	unsigned long flags;

	/* restart any pending palette updates */
	spin_lock_irqsave(&priv->lut.lock, flags);
	if (_pdpfb_lut_num_entries(priv)) {
		spin_lock(&priv->upd.lock);
		priv->upd.updates |= PDP_UPDATE_CLUT;
		pdpfb_register_isr(pdpfb_interrupt_upd, priv,
				   PDPFB_IRQ_VEVENT0);
		spin_unlock(&priv->upd.lock);
	}
	spin_unlock_irqrestore(&priv->lut.lock, flags);
}

void pdpfb_disable_palette(struct pdpfb_priv *priv)
{
	unsigned long flags;

	/* stop any pending palette updates */
	spin_lock_irqsave(&priv->lut.lock, flags);
	if (_pdpfb_lut_num_entries(priv)) {
		spin_lock(&priv->upd.lock);
		priv->upd.updates &= ~PDP_UPDATE_CLUT;
		if (!priv->upd.updates)
			pdpfb_unregister_isr(pdpfb_interrupt_upd, priv,
					     PDPFB_IRQ_VEVENT0);
		spin_unlock(&priv->upd.lock);
	}
	spin_unlock_irqrestore(&priv->lut.lock, flags);
}

static void pdpfb_ident(struct pdpfb_priv *priv)
{
	unsigned int coreid;
	unsigned int corerev;

	coreid = pdpfb_read(priv, PDP_CORE_ID);
	corerev = pdpfb_read(priv, PDP_CORE_REV);

	dev_info(priv->device,
		 "PDP id: %#x revision: %d.%d.%d\n", coreid,
		 (corerev >> 16) & 0xff,
		 (corerev >> 8) & 0xff,
		 corerev & 0xff);
}

/* Handler for all PDP interrupts */
static irqreturn_t pdpfb_interrupt(int irq, void *dev_id)
{
	struct pdpfb_priv *priv = dev_id;
	u32 int_stat;
	int i;
	struct pdpfb_isr_data *isr_data;
	struct pdpfb_isr_data registered_isr[PDPFB_MAX_NR_ISRS];

	int_stat = pdpfb_read(priv, PDP_INTSTAT);
	pdpfb_write(priv, PDP_INTCLR, int_stat);

	spin_lock(&priv->irq_lock);

	if (priv->isr_disable) {
		spin_unlock(&priv->irq_lock);
		return IRQ_HANDLED;
	}

	/* make a copy and unlock, so that isrs can unregister themselves */
	memcpy(registered_isr, priv->registered_isr,
			sizeof(registered_isr));

	spin_unlock(&priv->irq_lock);

	for (i = 0; i < PDPFB_MAX_NR_ISRS; i++) {
		isr_data = &registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & int_stat)
			isr_data->isr(isr_data->arg, int_stat);
	}

	return IRQ_HANDLED;
}

/* priv->irq_lock has to be locked by the caller */
static void _pdpfb_set_irqs(struct pdpfb_priv *priv)
{
	u32 mask;
	u32 old_mask;
	int i;
	struct pdpfb_isr_data *isr_data;

	if (priv->isr_disable) {
		pdpfb_write(priv, PDP_INTENAB, 0);
		return;
	}

	mask = 0;
	for (i = 0; i < PDPFB_MAX_NR_ISRS; i++) {
		isr_data = &priv->registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	clk_enable(priv->pdp_clk);

	old_mask = pdpfb_read(priv, PDP_INTENAB);
	/* clear the irqstatus for newly enabled irqs */
	pdpfb_write(priv, PDP_INTCLR, (mask ^ old_mask) & mask);

	pdpfb_write(priv, PDP_INTENAB, mask);

	clk_disable(priv->pdp_clk);
}

static void pdpfb_disable_isr(struct pdpfb_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->isr_disable = 1;
	_pdpfb_set_irqs(priv);
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static void pdpfb_enable_isr(struct pdpfb_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->isr_disable = 0;
	_pdpfb_set_irqs(priv);
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

int pdpfb_register_isr(pdpfb_isr_t isr, void *arg, u32 mask)
{
	struct pdpfb_priv *priv = pdpfb_priv;
	int i;
	int ret;
	unsigned long flags;
	struct pdpfb_isr_data *isr_data;

	if (!priv)
		return -ENODEV;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&priv->irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < PDPFB_MAX_NR_ISRS; i++) {
		isr_data = &priv->registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < PDPFB_MAX_NR_ISRS; i++) {
		isr_data = &priv->registered_isr[i];

		if (isr_data->isr != NULL)
			continue;

		isr_data->isr = isr;
		isr_data->arg = arg;
		isr_data->mask = mask;
		ret = 0;

		break;
	}

	_pdpfb_set_irqs(priv);

	spin_unlock_irqrestore(&priv->irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&priv->irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(pdpfb_register_isr);

int pdpfb_unregister_isr(pdpfb_isr_t isr, void *arg, u32 mask)
{
	struct pdpfb_priv *priv = pdpfb_priv;
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct pdpfb_isr_data *isr_data;

	if (!priv)
		return -ENODEV;

	spin_lock_irqsave(&priv->irq_lock, flags);

	for (i = 0; i < PDPFB_MAX_NR_ISRS; i++) {
		isr_data = &priv->registered_isr[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
				isr_data->mask != mask)
			continue;

		/* found the correct isr */

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		ret = 0;
		break;
	}

	if (ret == 0)
		_pdpfb_set_irqs(priv);

	spin_unlock_irqrestore(&priv->irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(pdpfb_unregister_isr);

static void pdpfb_irq_wait_handler(void *data, u32 mask)
{
	complete((struct completion *)data);
}

int pdpfb_wait_for_irq_timeout(u32 irqmask, unsigned long timeout)
{
	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = pdpfb_register_isr(pdpfb_irq_wait_handler, &completion, irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_timeout(&completion, timeout);

	pdpfb_unregister_isr(pdpfb_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}

int pdpfb_wait_for_irq_interruptible_timeout(u32 irqmask,
						unsigned long timeout)
{
	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = pdpfb_register_isr(pdpfb_irq_wait_handler, &completion, irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_interruptible_timeout(&completion,
			timeout);

	pdpfb_unregister_isr(pdpfb_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}

int pdpfb_wait_vsync(void)
{
	unsigned long timeout = msecs_to_jiffies(500);
	u32 irq = PDPFB_IRQ_VEVENT0;

	return pdpfb_wait_for_irq_interruptible_timeout(irq, timeout);
}
EXPORT_SYMBOL(pdpfb_wait_vsync);

#ifndef CONFIG_FB_PDP_PDUMP
static void pdpfb_vsync_isr(void *arg, u32 mask)
{
	struct pdpfb_priv *priv = (struct pdpfb_priv *)arg;
	++priv->vsync_count;
}
#endif

struct pdp_info *pdpfb_get_platform_data(struct pdpfb_priv *priv)
{
	return priv->device->platform_data;
}

u32 *pdpfb_get_pseudo_palette(struct pdpfb_priv *priv)
{
	return priv->pseudo_palette;
}

enum pdpfb_display_mode {
	PDP_FULLY_POWERED_DOWN,
	PDP_PARTIALLY_POWERED_DOWN,
	PDP_ENABLED
};

static void pdpfb_set_display_enabled(struct pdpfb_priv *priv,
					enum pdpfb_display_mode mode)
{
	struct pdp_info *pdata = priv->device->platform_data;
	u32 sync_ctrl;
	u32 op_mask;
	int powerdn = 0;

	if (mode == PDP_FULLY_POWERED_DOWN && pdata->sync_cfg.force_vsyncs)
		mode = PDP_PARTIALLY_POWERED_DOWN;

	switch (mode) {
	case PDP_FULLY_POWERED_DOWN:
		sync_ctrl = pdpfb_read(priv, PDP_SYNCCTRL);
		SET_FIELD(sync_ctrl, PDP_SYNCCTRL_SYNCACTIVE, 0);
		SET_FIELD(sync_ctrl, PDP_SYNCCTRL_POWERDN, 1);
		pdpfb_write(priv, PDP_SYNCCTRL, sync_ctrl);

		pdpfb_disable_isr(priv);

		if (pdata->hwops.set_screen_power)
			pdata->hwops.set_screen_power(0);
		pdpfb_pixel_clk_disable(priv);

		break;
	case PDP_PARTIALLY_POWERED_DOWN:
		powerdn = 1;
	case PDP_ENABLED:
		op_mask = pdpfb_read(priv, PDP_OPMASK);
		SET_FIELD(op_mask, PDP_OPMASK_MASKR, 0x00);
		SET_FIELD(op_mask, PDP_OPMASK_MASKG, 0x00);
		SET_FIELD(op_mask, PDP_OPMASK_MASKB, 0x00);
		SET_FIELD(op_mask, PDP_OPMASK_MASKLEVEL, 0);
		pdpfb_write(priv, PDP_OPMASK, op_mask);

		sync_ctrl = pdpfb_read(priv, PDP_SYNCCTRL);
		SET_FIELD(sync_ctrl, PDP_SYNCCTRL_UPDCTRL, 0);
		SET_FIELD(sync_ctrl, PDP_SYNCCTRL_SYNCACTIVE, 1);
		SET_FIELD(sync_ctrl, PDP_SYNCCTRL_POWERDN, powerdn);
		pdpfb_write(priv, PDP_SYNCCTRL, sync_ctrl);

		pdpfb_pixel_clk_enable(priv);
		if (pdata->hwops.set_screen_power)
			pdata->hwops.set_screen_power(!powerdn);

		pdpfb_enable_isr(priv);

		break;
	default:
		dev_err(priv->device,
			"Unrecognised pdpfb_set_display_enabled parameter %d\n",
			mode);
	}
}

void pdpfb_update_margins(struct pdpfb_priv *priv,
			struct pdpfb_stream *stream)
{
	struct pdp_info *pdata = priv->device->platform_data;
	struct fb_info *info = &stream->info;
	int xres, yres, w, h;
	int xgap[2];
	int ygap[2];

	xres = info->var.xres;
	yres = info->var.yres;
	w = (stream->geom.w ? stream->geom.w : xres);
	h = (stream->geom.h ? stream->geom.h : yres);
	xgap[0] = stream->geom.x;
	ygap[0] = stream->geom.y;
	/* we can't really handle the case of a plane positioned offscreen */
	xgap[1] = max(0, (int)priv->mode.xres - w - xgap[0]);
	ygap[1] = max(0, (int)priv->mode.yres - h - ygap[0]);

	/* physical size of plane */
	info->var.width = pdata->lcd_size_cfg.width * w / priv->mode.xres;
	info->var.height = pdata->lcd_size_cfg.height * h / priv->mode.yres;

	/* measurements in units of plane pixels */
	info->var.pixclock = priv->mode.pixclock * w / xres * h / yres;
	info->var.hsync_len = priv->mode.hsync_len * xres / w;
	info->var.vsync_len = priv->mode.vsync_len * yres / h;
	info->var.left_margin  = (priv->mode.left_margin  + xgap[0])
					* xres / w;
	info->var.upper_margin = (priv->mode.upper_margin + ygap[0])
					* yres / h;
	info->var.right_margin = (priv->mode.right_margin + xgap[1])
					* xres / w;
	info->var.lower_margin = (priv->mode.lower_margin + ygap[1])
					* yres / h;
}

static void pdpfb_update_all_margins(struct pdpfb_priv *priv)
{
	int i;

	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		if (!stream || !stream->probed)
			continue;
		pdpfb_update_margins(priv, stream);
	}
}

/* update variables but not registers */
static void pdpfb_update_refresh_rate(struct pdpfb_priv *priv)
{
	u32 ht, vt;
	unsigned long pix_freq;

	if (priv->mode.pixclock) {
		pix_freq = 1000*PICOS2KHZ(priv->mode.pixclock);
	} else {
		ht = priv->mode.hsync_len
			+ priv->mode.left_margin
			+ priv->mode.xres
			+ priv->mode.right_margin;
		vt = priv->mode.vsync_len
			+ priv->mode.upper_margin
			+ priv->mode.yres
			+ priv->mode.lower_margin;
		pix_freq = ht*vt*priv->mode.refresh;
	}
	clk_set_rate(priv->pixel_clk, pix_freq);

	clk_prepare_enable(priv->pixel_clk);
	pix_freq = clk_get_rate(priv->pixel_clk);
	clk_disable_unprepare(priv->pixel_clk);
	priv->mode.pixclock = KHZ2PICOS(pix_freq/1000);

	pdpfb_update_all_margins(priv);
}

/*
 * Updates pixel clock if it's changed (pixclock is pS).
 * Returns 1 if changed.
 */
int pdpfb_update_pixclock(struct pdpfb_priv *priv, unsigned int pixclock)
{
	if (pixclock != priv->mode.pixclock) {
		priv->mode.pixclock = pixclock;
		pdpfb_update_refresh_rate(priv);
		return 1;
	}
	return 0;
}

unsigned long pdpfb_get_line_length(int xres_virtual, int bpp)
{
#define LINELEN_ALIGNMENT_BYTES	16	/* in bytes, must be power of 2 */
#define LINELEN_ALIGNMENT		(LINELEN_ALIGNMENT_BYTES << 3)
	u_long length;

	/* round up to line length alignment */
	length = xres_virtual * bpp;
	length = (length + (LINELEN_ALIGNMENT-1)) & -LINELEN_ALIGNMENT;
	length >>= 3;
	return length;
}

static void pdpfb_soft_reset(struct pdpfb_priv *priv)
{
	/*
	 * We intentionally include the video plane even if support for it is
	 * not compiled in. This is because it may already have been enabled by
	 * the bootloader and we don't want a green plane on top of the
	 * graphics plane.
	 */
	static const unsigned int strctrls[] = {
		PDP_STR1CTRL,
		PDP_STR2CTRL,
	};
	int i;
	u32 sync_ctrl, ctrl;

	sync_ctrl = pdpfb_read(priv, PDP_SYNCCTRL);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_DISPRST, 1);
	pdpfb_write(priv, PDP_SYNCCTRL, sync_ctrl);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_DISPRST, 0);
	pdpfb_write(priv, PDP_SYNCCTRL, sync_ctrl);

	/* disable all the streams */
	for (i = 0; i < ARRAY_SIZE(strctrls); ++i) {
		ctrl = pdpfb_read(priv, strctrls[i]);
		SET_FIELD(ctrl, PDP_STRXCTRL_STREAMEN, 0);
		SET_FIELD(ctrl, PDP_STRXCTRL_BLENDPOS, i);
		pdpfb_write(priv, strctrls[i], ctrl);
	}
}

static void pdpfb_configure_display(struct pdpfb_priv *priv)
{
	struct pdp_info *pdata = priv->device->platform_data;
	u32 sync_ctrl;
	u32 mem_ctrl;

	sync_ctrl = pdpfb_read(priv, PDP_SYNCCTRL);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_CLKPOL, pdata->sync_cfg.clock_pol);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_VSPOL,
			0 == (priv->mode.sync & FB_SYNC_VERT_HIGH_ACT));
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_HSPOL,
			0 == (priv->mode.sync & FB_SYNC_HOR_HIGH_ACT));
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_BLNKPOL, pdata->sync_cfg.blank_pol);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_VSDIS, pdata->sync_cfg.vsync_dis);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_HSDIS, pdata->sync_cfg.hsync_dis);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_BLNKDIS, pdata->sync_cfg.blank_dis);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_VSSLAVE, pdata->sync_cfg.sync_slave);
	SET_FIELD(sync_ctrl, PDP_SYNCCTRL_HSSLAVE, pdata->sync_cfg.sync_slave);
	pdpfb_write(priv, PDP_SYNCCTRL, sync_ctrl);

	mem_ctrl = pdpfb_read(priv, PDP_MEMCTRL);
	SET_FIELD(mem_ctrl, PDP_MEMCTRL_MEMREFRESH,
			PDP_MEMCTRL_MEMREFRESH_BOTH);
	pdpfb_write(priv, PDP_MEMCTRL, mem_ctrl);
}

#ifdef PDP_CKEY_BY_BLENDPOS
const static struct pdpfb_stream_regs pdpfb_ckey_regs[] = {
	{
		.blend  = PDP_STR1BLEND,
		.blend2 = PDP_STR1BLEND2,
		.ctrl   = PDP_STR1CTRL,
	},
	{
		.blend  = PDP_STR2BLEND,
		.blend2 = PDP_STR2BLEND2,
		.ctrl   = PDP_STR2CTRL,
	},
};
#define CKEY_REGS(STREAM, BLENDPOS)	(pdpfb_ckey_regs[(BLENDPOS)])
#else
#define CKEY_REGS(STREAM, BLENDPOS)	((STREAM)->regs)
#endif

static void pdpfb_configure_streams(struct pdpfb_priv *priv)
{
	u32 ctrl, posn, blend, blend2;
	u32 en, i;

	pdpfb_write(priv, PDP_BGNDCOL, priv->bgnd_col);

	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		const struct pdpfb_stream_regs *ckey_regs;
		if (!stream)
			continue;
		if (stream->probed && stream->ops.configure) {
			en = !stream->ops.configure(priv) && stream->enable;
			if (en && stream->ops.configure_addr)
				stream->ops.configure_addr(priv);
		} else {
			en = 0;
		}

		if (en) {
			/* stream position on screen */
			posn = pdpfb_read(priv, stream->regs.posn);
			SET_FIELD(posn, PDP_STRXPOSN_XSTART, stream->geom.x);
			SET_FIELD(posn, PDP_STRXPOSN_YSTART, stream->geom.y);
			pdpfb_write(priv, stream->regs.posn, posn);
		}

		/* set up blending */

		ctrl = pdpfb_read(priv, stream->regs.ctrl);
		SET_FIELD(ctrl, PDP_STRXCTRL_STREAMEN, en);
		SET_FIELD(ctrl, PDP_STRXCTRL_BLENDMODE, stream->blend_mode);
		SET_FIELD(ctrl, PDP_STRXCTRL_BLENDPOS, i);
		pdpfb_write(priv, stream->regs.ctrl, ctrl);

		blend = pdpfb_read(priv, stream->regs.blend);
		SET_FIELD(blend, PDP_STRXBLEND_GLOBALALPHA,
				stream->global_alpha);
		pdpfb_write(priv, stream->regs.blend, blend);

		/* set up colour keying */

		ckey_regs = &CKEY_REGS(stream, i);

		ctrl = pdpfb_read(priv, ckey_regs->ctrl);
		SET_FIELD(ctrl, PDP_STRXCTRL_CKEYEN, stream->ckey_en);
		SET_FIELD(ctrl, PDP_STRXCTRL_CKEYSRC, stream->ckey_src);
		pdpfb_write(priv, ckey_regs->ctrl, ctrl);

		blend = pdpfb_read(priv, ckey_regs->blend);
		SET_FIELD(blend, PDP_STRXBLEND_COLKEY, stream->ckey.ckey);
		pdpfb_write(priv, ckey_regs->blend, blend);

		blend2 = pdpfb_read(priv, ckey_regs->blend2);
		SET_FIELD(blend2, PDP_STRXBLEND2_COLKEYMASK, stream->ckey.mask);
		pdpfb_write(priv, ckey_regs->blend2, blend2);
	}
}

/* Extract the current sync setup from the hardware */
int pdpfb_extract_sync(struct pdpfb_priv *priv)
{
	u32 hsync1, hde_ctrl;
	u32 vsync1, vde_ctrl;
	u16 vbps, vt, vdes, vdef;
	u16 hbps, ht, hdes, hdef;

	/* read vsync */
	vsync1	= pdpfb_read(priv, PDP_VSYNC1);
	vbps	= GET_FIELD(vsync1, PDP_VSYNC1_VBPS);
	vt	= GET_FIELD(vsync1, PDP_VSYNC1_VT);

	vde_ctrl = pdpfb_read(priv, PDP_VDECTRL);
	vdes	= GET_FIELD(vde_ctrl, PDP_VDECTRL_VDES);
	vdef	= GET_FIELD(vde_ctrl, PDP_VDECTRL_VDEF);

	if (!vbps || vdes <= vbps || vdef <= vdes || vt <= vdef)
		return -EINVAL;

	/* read hsync */
	hsync1	= pdpfb_read(priv, PDP_HSYNC1);
	hbps	= GET_FIELD(hsync1, PDP_HSYNC1_HBPS);
	ht	= GET_FIELD(hsync1, PDP_HSYNC1_HT);

	hde_ctrl = pdpfb_read(priv, PDP_HDECTRL);
	hdes	= GET_FIELD(hde_ctrl, PDP_HDECTRL_HDES);
	hdef	= GET_FIELD(hde_ctrl, PDP_HDECTRL_HDEF);

	if (!hbps || hdes <= hbps || hdef <= hdes || ht <= hdef)
		return -EINVAL;

	/* calculate timings */
	priv->mode.vsync_len = vbps;
	priv->mode.upper_margin = vdes - vbps;
	priv->mode.yres = vdef - vdes;
	priv->mode.lower_margin = vt - vdef;

	priv->mode.hsync_len = hbps;
	priv->mode.left_margin = hdes - hbps;
	priv->mode.xres = hdef - hdes;
	priv->mode.right_margin = ht - hdef;

	return 0;
}

/* Set up sync registers to position the frame buffers on the screen */
int pdpfb_configure_sync(struct pdpfb_priv *priv)
{
	struct pdpfb_update_sync *sync = &priv->upd.sync;
	unsigned long flags;

	u32 sync_ctrl;
	u32 vsync1, vsync2, vsync3;
	u32 vevent;
	u32 vde_ctrl;

	u16 hbps, ht, hfps, hrbs, has, hlbs;
	u16 vbps, vt, vtbs, vfps, vas, vbbs;
	u16 vevent_vevent, vevent_vfetch;
	u16 hdes, hdef;
	u16 vdes, vdef;

	int border[2][2] = { {3, 3}, {3, 3} };

	hbps = priv->mode.hsync_len;
	hdes = hbps + priv->mode.left_margin;
	has = hdes;
	hlbs = has - border[0][0];
	hdef = hdes + priv->mode.xres;
	hrbs = hdef;
	hfps = hrbs + border[0][1];
	ht = hrbs + priv->mode.right_margin;

	vbps = priv->mode.vsync_len;
	vdes = vbps + priv->mode.upper_margin;
	vas = vdes;
	vtbs = vas - border[1][0];
	vdef = vdes + priv->mode.yres;
	vbbs = vdef;
	vfps = vbbs + border[1][1];
	vt = vbbs + priv->mode.lower_margin;

	vevent_vevent = 0;
	vevent_vfetch = vbps;

	/* write registers that shouldn't cause artifacts */

	vsync1	= PLACE_FIELD(PDP_VSYNC1_VBPS, vbps)
		| PLACE_FIELD(PDP_VSYNC1_VT, vt);
	vsync2	= PLACE_FIELD(PDP_VSYNC2_VAS, vas)
		| PLACE_FIELD(PDP_VSYNC2_VTBS, vtbs);
	vsync3	= PLACE_FIELD(PDP_VSYNC3_VFPS, vfps)
		| PLACE_FIELD(PDP_VSYNC3_VBBS, vbbs);

	pdpfb_write(priv, PDP_VSYNC1, vsync1);
	pdpfb_write(priv, PDP_VSYNC2, vsync2);
	pdpfb_write(priv, PDP_VSYNC3, vsync3);

	vevent		= PLACE_FIELD(PDP_VEVENT_VEVENT, vevent_vevent)
			| PLACE_FIELD(PDP_VEVENT_VFETCH, vevent_vfetch);
	vde_ctrl	= PLACE_FIELD(PDP_VDECTRL_VDES, vdes)
			| PLACE_FIELD(PDP_VDECTRL_VDEF, vdef);

	pdpfb_write(priv, PDP_VEVENT, vevent);
	pdpfb_write(priv, PDP_VDECTRL, vde_ctrl);

	/* write registers that would cause artifacts on next VBLANK */

	spin_lock_irqsave(&priv->upd.lock, flags);

	sync->hsync1	= PLACE_FIELD(PDP_HSYNC1_HBPS, hbps)
			| PLACE_FIELD(PDP_HSYNC1_HT, ht);
	sync->hsync2	= PLACE_FIELD(PDP_HSYNC2_HAS, has)
			| PLACE_FIELD(PDP_HSYNC2_HLBS, hlbs);
	sync->hsync3	= PLACE_FIELD(PDP_HSYNC3_HFPS, hfps)
			| PLACE_FIELD(PDP_HSYNC3_HRBS, hrbs);

	sync->hde_ctrl	= PLACE_FIELD(PDP_HDECTRL_HDES, hdes)
			| PLACE_FIELD(PDP_HDECTRL_HDEF, hdef);

	sync_ctrl = pdpfb_read(priv, PDP_SYNCCTRL);
	if (GET_FIELD(sync_ctrl, PDP_SYNCCTRL_SYNCACTIVE)) {
		/* sync enabled, apply on next VSYNC */
		priv->upd.updates |= PDP_UPDATE_SYNC;
		pdpfb_register_isr(pdpfb_interrupt_upd, priv,
				   PDPFB_IRQ_VEVENT0);
	} else {
		/* sync disabled, apply immediately */
		pdpfb_sync_upd(priv);
	}

	spin_unlock_irqrestore(&priv->upd.lock, flags);

	return 0;
}

static void pdpfb_emit_blank_event(struct pdpfb_priv *priv)
{
	struct fb_event event;
	int blank = priv->power;
	int i;

	/* skip blank events if no_blank_emit flag is set */
	if (priv->no_blank_emit)
		return;

	/* skip the final blank if we know it'll be emitted anyway */
	if (priv->final_blank == blank)
		return;

	event.data = &blank;
	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		int flags;
		if (!stream)
			continue;
		event.info = &stream->info;

		/* prevent fbcon responding badly to the blank events */
		flags = event.info->flags;
		event.info->flags |= FBINFO_MISC_USEREVENT;
		fb_notifier_call_chain(FB_EVENT_BLANK, &event);
		event.info->flags = flags;
	}
}

/* FULLY_OFF -> PANEL_ENABLED */
static void pdpfb_power_panel_en(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "panel_en\n");

	pdpfb_soft_reset(priv);
	pdpfb_write(priv, PDP_INTCLR, 0xFFFFFFFF);

	pdpfb_emit_blank_event(priv);
}
/* PANEL_ENABLED -> FULLY_OFF */
static void pdpfb_power_panel_dis(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "panel_dis\n");

	pdpfb_emit_blank_event(priv);
}

/* PANEL_ENABLED -> SYNC_ENABLED */
static void pdpfb_power_sync_en(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "sync_en\n");

	pdpfb_update_refresh_rate(priv);
	pdpfb_configure_display(priv);
	pdpfb_configure_sync(priv);
	pdpfb_set_display_enabled(priv, PDP_PARTIALLY_POWERED_DOWN);

	pdpfb_emit_blank_event(priv);
}
/* SYNC_ENABLED -> PANEL_ENABLED */
static void pdpfb_power_sync_dis(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "sync_dis\n");

	pdpfb_set_display_enabled(priv, PDP_FULLY_POWERED_DOWN);

	pdpfb_emit_blank_event(priv);
}

/* SYNC_ENABLED -> PANEL_POWERED */
static void pdpfb_power_panelpwr_en(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "panelpwr_en\n");

	pdpfb_emit_blank_event(priv);

	pdpfb_configure_streams(priv);
}
/* PANEL_POWERED -> SYNC_ENABLED */
static void pdpfb_power_panelpwr_dis(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "panelpwr_dis\n");

	pdpfb_emit_blank_event(priv);
}

/* PANEL_POWERED -> BACKLIT */
static void pdpfb_power_bl_en(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "bl_en\n");

	pdpfb_set_display_enabled(priv, PDP_ENABLED);
	pdpfb_emit_blank_event(priv);
}
/* BACKLIT -> PANEL_POWERED */
static void pdpfb_power_bl_dis(struct pdpfb_priv *priv)
{
	dev_dbg(priv->device, "bl_dis\n");

	pdpfb_emit_blank_event(priv);
	/* enable blanking after vsync to avoid tearing */
	pdpfb_wait_vsync();
	pdpfb_set_display_enabled(priv, PDP_PARTIALLY_POWERED_DOWN);
}

/* power transition functions */
typedef void (*pdpfb_power_change)(struct pdpfb_priv *priv);
static const pdpfb_power_change pdpfb_power_fn[PDPFB_POWER_MAX][2] = {
	[PDPFB_POWER_BACKLIT]
		= { pdpfb_power_bl_en,		pdpfb_power_bl_dis },
	[PDPFB_POWER_PANEL_POWERED]
		= { pdpfb_power_panelpwr_en,	pdpfb_power_panelpwr_dis },
	[PDPFB_POWER_SYNC_ENABLED]
		= { pdpfb_power_sync_en,	pdpfb_power_sync_dis },
	[PDPFB_POWER_PANEL_ENABLED]
		= { pdpfb_power_panel_en,	pdpfb_power_panel_dis },
};

/* move to prestate (lower power) then to state (higher power) */
static int pdpfb_reconfigure(struct pdpfb_priv *priv,
			     enum pdpfb_power_state prestate,
			     enum pdpfb_power_state state)
{
	WARN_CONSOLE_UNLOCKED();

	if (prestate >= PDPFB_POWER_MAX || state <= PDPFB_POWER_NA)
		return -EINVAL;

	/* ignore blank events that we emitted ourselves */
	if (priv->reconfiguring)
		return -EBUSY;
	priv->reconfiguring = 1;

	while (priv->power < prestate) {
		++priv->power;
		pdpfb_power_fn[priv->power - 1][1](priv);
	}
	while (priv->power > state) {
		--priv->power;
		pdpfb_power_fn[priv->power][0](priv);
	}

	priv->reconfiguring = 0;
	return 0;
}

/* move to a different power state */
static int pdpfb_configure(struct pdpfb_priv *priv,
			   enum pdpfb_power_state state)
{
	return pdpfb_reconfigure(priv, state, state);
}

void pdpfb_set_mode(struct pdpfb_priv *priv, struct fb_var_screeninfo *var)
{
	/*
	 * Blanking events confuse fbcon, and aren't strictly necessary when
	 * we're just changing mode.
	 */
	priv->no_blank_emit = 1;
	if (var->activate & FB_ACTIVATE_FORCE) {
		fb_var_to_videomode(&priv->mode, var);
		pdpfb_reconfigure(priv, PDPFB_POWER_PANEL_ENABLED, priv->power);
	} else {
		struct fb_videomode old_mode = priv->mode;
		fb_var_to_videomode(&priv->mode, var);
		if (!fb_mode_is_equal(&priv->mode, &old_mode))
			pdpfb_reconfigure(priv, PDPFB_POWER_PANEL_ENABLED,
					  priv->power);
		else
			pdpfb_configure_streams(priv);
	}
	priv->no_blank_emit = 0;
}

static inline u32 _pdpfb_convert_palette(struct fb_bitfield *red_bits,
					 struct fb_bitfield *green_bits,
					 struct fb_bitfield *blue_bits,
					 u_int red, u_int green, u_int blue)
{
	u32 rgb;
	rgb  = MOVE_FIELD(red, 0, 16,
			  red_bits->offset,
			  red_bits->length);
	rgb |= MOVE_FIELD(green, 0, 16,
			  green_bits->offset,
			  green_bits->length);
	rgb |= MOVE_FIELD(blue, 0, 16,
			  blue_bits->offset,
			  blue_bits->length);
	return rgb;
}

static inline void _pdpfb_set_pseudo_palette(struct pdpfb_priv *priv,
					     struct fb_info *info, u_int regno,
					     u_int red, u_int green,
					     u_int blue, u_int trans)
{
	priv->pseudo_palette[regno] = _pdpfb_convert_palette(
			&info->var.red, &info->var.green, &info->var.blue,
			red, green, blue);
}

static inline u32 _pdpfb_hw_palette(struct pdpfb_priv *priv, u_int regno,
				    u_int red, u_int green,
				    u_int blue, u_int trans)
{
	return _pdpfb_convert_palette(&priv->palette_fmt.r,
				      &priv->palette_fmt.g,
				      &priv->palette_fmt.b,
				      red, green, blue);
}

int pdpfb_setcolreg(u_int regno,
		 u_int red, u_int green, u_int blue,
		 u_int trans, struct fb_info *info)
{
	struct pdpfb_priv *priv;
	u32 palette_rgb;
	unsigned long flags;

	if (regno > 255)
		return -EINVAL;

	priv = dev_get_drvdata(info->device);

	/* update pseudo palette used by fbcon */
	_pdpfb_set_pseudo_palette(priv, info, regno, red, green, blue, trans);
	/* queue hardware palette update */
	palette_rgb = _pdpfb_hw_palette(priv, regno, red, green, blue, trans);
	_pdpfb_start_queue_lut_upd(priv, &flags);
	_pdpfb_queue_lut_upd(priv, regno, palette_rgb);
	_pdpfb_end_queue_lut_upd(priv, &flags);

	return 0;
}

int pdpfb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	struct pdpfb_priv *priv;
	int i, start;
	u16 *red, *green, *blue, *transp;
	u_int hred, hgreen, hblue, htransp = 0xffff;
	unsigned long flags;
	u32 palette_rgb;

	priv = dev_get_drvdata(info->device);

	red = cmap->red;
	green = cmap->green;
	blue = cmap->blue;
	transp = cmap->transp;
	start = cmap->start;
	if (start + cmap->len > PDP_PALETTE_NR)
		return -EINVAL;

	_pdpfb_start_queue_lut_upd(priv, &flags);
	for (i = 0; i < cmap->len; i++) {
		hred = *red++;
		hgreen = *green++;
		hblue = *blue++;
		if (transp)
			htransp = *transp++;

		/* update pseudo palette used by fbcon */
		_pdpfb_set_pseudo_palette(priv, info, i,
				hred, hgreen, hblue, htransp);
		/* queue hardware palette update */
		palette_rgb = _pdpfb_hw_palette(priv, i,
				hred, hgreen, hblue, htransp);
		_pdpfb_queue_lut_upd(priv, i, palette_rgb);
	}
	_pdpfb_end_queue_lut_upd(priv, &flags);
	return 0;
}

int pdpfb_blank(int blank, struct fb_info *info)
{
	struct pdpfb_priv *priv = dev_get_drvdata(info->device);
	int err, prev_final_blank;

	prev_final_blank = priv->final_blank;
	priv->final_blank = blank;
	err = pdpfb_configure(priv, blank);
	priv->final_blank = prev_final_blank;
	return err;
}

static void pdpfb_str_get_vblank(struct pdpfb_priv *priv,
				 struct pdpfb_stream *stream,
				 struct fb_vblank *vblank)
{
	int line_no, geomy, geomh;

	line_no = pdpfb_read(priv, PDP_LINESTAT);
	line_no = GET_FIELD(line_no, PDP_LINESTAT_LINENO_STAT);

	geomy = priv->mode.vsync_len + priv->mode.upper_margin + stream->geom.y;
	geomh = stream->geom.h;
	if (!geomh)
		geomh = stream->info.var.yres;

	memset(vblank, 0, sizeof(*vblank));

	vblank->flags = FB_VBLANK_HAVE_VBLANK	|
			FB_VBLANK_HAVE_COUNT	|
			FB_VBLANK_HAVE_VCOUNT	|
			FB_VBLANK_HAVE_VSYNC;
	if (line_no < geomy ||
	    line_no >= geomy + geomh)
		vblank->flags |= FB_VBLANK_VBLANKING;
	if (line_no < priv->mode.vsync_len)
		vblank->flags |= FB_VBLANK_VSYNCING;
	vblank->count = priv->vsync_count;
	vblank->vcount = line_no * stream->info.var.yres / geomh;
}

static int pdpfb_str_get_plane_pos(struct pdpfb_priv *priv,
		struct pdpfb_stream *stream)
{
	int i;
	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		if (priv->streams[i] == stream)
			return i;
	}
	return -EINVAL;
}

/* returns new position, or negative on error */
static int pdpfb_str_set_plane_pos(struct pdpfb_priv *priv,
		struct pdpfb_stream *stream,
		int pos)
{
	int old_pos = pdpfb_str_get_plane_pos(priv, stream);
	if (unlikely(old_pos < 0))
		return old_pos;
	if (unlikely(pos < 0 || pos >= PDPFB_STREAM_NR))
		return -EINVAL;
	if (pos == old_pos)
		return pos;

	if (pos > old_pos) {
		do {
			priv->streams[old_pos] = priv->streams[old_pos+1];
			++old_pos;
		} while (pos > old_pos);
	} else if (pos < old_pos) {
		do {
			priv->streams[old_pos] = priv->streams[old_pos-1];
			--old_pos;
		} while (pos < old_pos);
	}
	priv->streams[pos] = stream;
	pdpfb_configure_streams(priv);
	return pos;
}

int pdpfb_str_ioctl(struct pdpfb_priv *priv, struct pdpfb_stream *stream,
		unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct pdpfb_geom geom;
	struct pdpfb_ckey ckey;
	struct fb_vblank vblank;
	int plane_pos;
	int error;
	u32 val;
#ifdef CONFIG_FB_PDP_USERMEM
	struct pdpfb_usermem usermem;
	struct pdp_info *pdata;
	int i, ret;
#endif

	switch (cmd) {
	/* get blanking info */
	case FBIOGET_VBLANK:
		pdpfb_str_get_vblank(priv, stream, &vblank);
		if (copy_to_user(argp, &vblank, sizeof(vblank)))
			return -EFAULT;
		return 0;
	/* wait for vsync */
	case FBIO_WAITFORVSYNC:
		if (get_user(val, (u32 __user *)arg))
			return -EFAULT;
		if (val != 0)
			return -ENODEV;
		return pdpfb_wait_vsync();
	/* get background colour */
	case PDPIO_GETBGND:
		if (copy_to_user(argp, &priv->bgnd_col, sizeof(priv->bgnd_col)))
			return -EFAULT;
		return 0;
	/* set background colour */
	case PDPIO_SETBGND:
		if (copy_from_user(&val, argp, sizeof(val)))
			return -EFAULT;
		priv->bgnd_col = val & 0xFFFFFF;
		pdpfb_configure_streams(priv);
		return 0;
	/* get screen geometry */
	case PDPIO_GETSCRGEOM:
		geom.x = geom.y = 0;
		geom.w = priv->mode.xres;
		geom.h = priv->mode.yres;
		if (copy_to_user(argp, &geom, sizeof(geom)))
			return -EFAULT;
		return 0;

	/* get stream enable */
	case PDPIO_GETEN:
		val = stream->enable;
		if (copy_to_user(argp, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	/* set stream enable */
	case PDPIO_SETEN:
		if (copy_from_user(&val, argp, sizeof(val)))
			return -EFAULT;
		stream->enable = (0 != val);
		pdpfb_configure_streams(priv);
		return 0;
	/* get stream plane position */
	case PDPIO_GETPLANEPOS:
		plane_pos = pdpfb_str_get_plane_pos(priv, stream);
		if (unlikely(plane_pos < 0))
			return -EINVAL;
		if (copy_to_user(argp, &plane_pos, sizeof(plane_pos)))
			return -EFAULT;
		return 0;
	/* set stream plane position */
	case PDPIO_SETPLANEPOS:
		if (copy_from_user(&plane_pos, argp, sizeof(plane_pos)))
			return -EFAULT;

		plane_pos = pdpfb_str_set_plane_pos(priv, stream, plane_pos);
		if (plane_pos < 0)
			return plane_pos;

		if (copy_to_user(argp, &plane_pos, sizeof(plane_pos)))
			return -EFAULT;
		return 0;
	/* get stream geometry */
	case PDPIO_GETGEOM:
		geom = stream->geom;
		if (copy_to_user(argp, &geom, sizeof(geom)))
			return -EFAULT;
		return 0;
	/* set stream geometry */
	case PDPIO_SETGEOM:
		if (copy_from_user(&geom, argp, sizeof(geom)))
			return -EFAULT;
		if (geom.x > 2047 || geom.y > 2047)
			return -EINVAL;

		if (stream->ops.check_geom) {
			error = stream->ops.check_geom(priv, &geom);
			if (error)
				return -EINVAL;
		} else {
			geom.w = 0;
			geom.h = 0;
		}
		stream->geom = geom;
		if (stream->ops.set_geom)
			stream->ops.set_geom(priv);

		pdpfb_configure_streams(priv);
		pdpfb_update_margins(priv, stream);

		if (copy_to_user(argp, &geom, sizeof(geom)))
			return -EFAULT;
		return 0;
	/* get stream colour key mode */
	case PDPIO_GETCKEYMODE:
		if (!stream->ckey_en)
			val = PDP_CKEYMODE_DISABLE;
		else if (stream->ckey_src == PDP_STRXCTRL_CKEYSRC_PREV)
			val = PDP_CKEYMODE_PREVIOUS;
		else if (stream->ckey_src == PDP_STRXCTRL_CKEYSRC_CUR)
			val = PDP_CKEYMODE_CURRENT;
		if (copy_to_user(argp, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	/* set stream colour key mode */
	case PDPIO_SETCKEYMODE:
		if (copy_from_user(&val, argp, sizeof(val)))
			return -EFAULT;
		switch (val) {
		case PDP_CKEYMODE_DISABLE:
			stream->ckey_en = 0;
			break;
		case PDP_CKEYMODE_PREVIOUS:
			stream->ckey_en = 1;
			stream->ckey_src = PDP_STRXCTRL_CKEYSRC_PREV;
			break;
		case PDP_CKEYMODE_CURRENT:
			stream->ckey_en = 1;
			stream->ckey_src = PDP_STRXCTRL_CKEYSRC_CUR;
			break;
		default:
			return -EINVAL;
		}
		pdpfb_configure_streams(priv);
		return 0;
	/* get stream colour key */
	case PDPIO_GETCKEY:
		if (copy_to_user(argp, &stream->ckey, sizeof(stream->ckey)))
			return -EFAULT;
		return 0;
	/* set stream colour key */
	case PDPIO_SETCKEY:
		if (copy_from_user(&ckey, argp, sizeof(ckey)))
			return -EFAULT;
		ckey.mask = ckey.mask & 0xFFFFFF;
		ckey.ckey = ckey.ckey & ckey.mask;
		stream->ckey = ckey;
		if (stream->ckey_en)
			pdpfb_configure_streams(priv);
		if (copy_to_user(argp, &stream->ckey, sizeof(stream->ckey)))
			return -EFAULT;
		return 0;
	/* get stream blending mode */
	case PDPIO_GETBLENDMODE:
		val = stream->blend_mode;
		if (copy_to_user(argp, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	/* set stream blending mode */
	case PDPIO_SETBLENDMODE:
		if (copy_from_user(&val, argp, sizeof(val)))
			return -EFAULT;
		if (val > PDP_BLENDMODE_PIXEL)
			return -EINVAL;
		stream->blend_mode = val;
		pdpfb_configure_streams(priv);
		return 0;
	/* gset stream global alpha */
	case PDPIO_GETGALPHA:
		val = stream->global_alpha;
		if (copy_to_user(argp, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	/* set stream global alpha */
	case PDPIO_SETGALPHA:
		if (copy_from_user(&val, argp, sizeof(val)))
			return -EFAULT;
		val &= 0xFF;
		stream->global_alpha = val;
		if (stream->blend_mode != PDP_BLENDMODE_NOALPHA)
			pdpfb_configure_streams(priv);
		if (copy_to_user(argp, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	/* set user provided memory */
	case PDPIO_SETUSERMEM:
#ifdef CONFIG_FB_PDP_USERMEM
		if (copy_from_user(&usermem, argp, sizeof(usermem)))
			return -EFAULT;

		if (usermem.flags & PDP_USERMEM_ALLPLANES) {
			pdpfb_init_mempool(priv, PDPFB_MEMPOOL_USER,
					   usermem.phys, usermem.len);

#ifdef PDP_SHARED_BASE
			pdata = pdpfb_get_platform_data(priv);
			pdata->hwops.set_shared_base(usermem.phys);
#endif
			ret = 0;
			for (i = PDPFB_STREAM_NR - 1; i >= 0; i--) {
				struct pdpfb_stream *stream = priv->streams[i];
				if (!stream || !stream->probed)
					continue;
				pdpfb_str_videomem_free(stream);
			}
			for (i = 0; i < PDPFB_STREAM_NR; i++) {
				struct pdpfb_stream *stream = priv->streams[i];
				if (!stream || !stream->probed)
					continue;
				error = pdpfb_str_videomem_alloc(priv, stream);
				if (error && !ret)
					ret = error;
			}

#ifdef PDP_SHARED_BASE
			if (!ret)
				return 0;

			/*
			 * Allocation from usermem failed, the best we can do
			 * is to reset the usermem pool and attempt allocations
			 * again.
			 */

			for (i = PDPFB_STREAM_NR - 1; i >= 0; i--) {
				struct pdpfb_stream *stream = priv->streams[i];
				if (!stream || !stream->probed)
					continue;
				pdpfb_str_videomem_free(stream);
			}

			priv->pools[PDPFB_MEMPOOL_USER].size = 0;
			priv->pools[PDPFB_MEMPOOL_USER].flags = 0;
			priv->pools[PDPFB_MEMPOOL_USER].phys_base = 0;
			priv->pools[PDPFB_MEMPOOL_USER].base = NULL;

			pdata->hwops.set_shared_base(
				priv->pools[PDPFB_MEMPOOL_MEM].phys_base);

			for (i = 0; i < PDPFB_STREAM_NR; i++) {
				struct pdpfb_stream *stream = priv->streams[i];
				if (!stream || !stream->probed)
					continue;
				pdpfb_str_videomem_alloc(priv, stream);
			}
#endif

			return -ENOMEM;
		}

#ifdef PDP_SHARED_BASE
		/* with a shared base we can only affect all planes */
		return -EINVAL;
#else
		stream->videomem = (void *)usermem.phys;
		stream->videomem_len = usermem.len;
		stream->mem_pool = PDPFB_MEMPOOL_USERPLANE;

		stream->info.screen_base = (char __force __iomem *)stream->videomem;
		stream->info.fix.smem_start = usermem.phys;
		stream->info.fix.smem_len = usermem.len;

		return 0;
#endif

#else /* !CONFIG_FB_PDP_USERMEM */
		return -EINVAL;
#endif /* !CONFIG_FB_PDP_USERMEM */

	default:
		return -ENOTTY;
	}
	return 0;
}

void pdpfb_str_videomem_free(struct pdpfb_stream *stream)
{
	struct fb_info *info = &stream->info;
	struct pdpfb_mem_pool *pool;

	if (stream->mem_pool == PDPFB_MEMPOOL_KERNEL) {
		kfree(stream->videomem);
	} else if (stream->mem_pool < PDPFB_MEMPOOL_NR_POOLS) {
		pool = &pdpfb_priv->pools[stream->mem_pool];

		if ((unsigned long)pool->base + pool->allocated ==
		    (unsigned long)stream->videomem + stream->videomem_len) {
			pool->allocated -= stream->videomem_len;
		} else {
			dev_warn(pdpfb_priv->device,
				"free losing memory in pool '%s'",
				pdpfb_mem_pool_names[stream->mem_pool]);
		}
	}

	stream->videomem = NULL;
	info->screen_base = NULL;
	info->fix.smem_start = 0;
	info->fix.smem_len = 0;
	stream->mem_pool = PDPFB_MEMPOOL_NONE;
}

static int pdpfb_pool_alloc(struct pdpfb_mem_pool *pool,
				struct pdpfb_stream *stream,
				unsigned long len)
{
	struct fb_info *info = &stream->info;
	unsigned long offset;
	unsigned long len_aligned = (len + PAGE_SIZE - 1) & PAGE_MASK;

	offset = pool->allocated;
	if (offset + len > pool->size)
		return -ENOMEM;
	pool->allocated += len_aligned;
	stream->videomem = (void *)((unsigned long)pool->base + offset);
#ifdef PDP_SHARED_BASE
	stream->videomem_offset = offset;
#endif
	stream->videomem_len = len_aligned;

	info->screen_base = (char __force __iomem *)stream->videomem;
	info->fix.smem_start = pool->phys_base + offset;
	info->fix.smem_len = len;

	return 0;
}

int pdpfb_str_videomem_alloc(struct pdpfb_priv *priv,
				struct pdpfb_stream *stream)
{
	struct fb_info *info = &stream->info;
	void *videomem = NULL;
	unsigned long len;

#ifdef CONFIG_FB_PDP_VID
	if (stream->mem_pool == PDPFB_MEMPOOL_GFXMEM)
		len = gfx_videomem_len;
	else
		len = vid_videomem_len;
#else
	len = gfx_videomem_len;
#endif
	if (!len)
		return 0;

	/* Try user memory pool */
	if (priv->pools[PDPFB_MEMPOOL_USER].base) {
		if (!pdpfb_pool_alloc(&priv->pools[PDPFB_MEMPOOL_USER],
				      stream, len)) {
			stream->mem_pool = PDPFB_MEMPOOL_USER;
			return 0;
		}
#ifdef PDP_SHARED_BASE
		goto err_nomem;
#endif
	}
	/* Try default memory pool (depends on stream type) */
	if (stream->mem_pool < PDPFB_MEMPOOL_NR_POOLS &&
	    priv->pools[stream->mem_pool].base) {
		if (pdpfb_pool_alloc(&priv->pools[stream->mem_pool], stream,
				     priv->pools[stream->mem_pool].size))
			goto err_nomem;
		return 0;
	}
	/* Try combined memory pool */
	if (priv->pools[PDPFB_MEMPOOL_MEM].base) {
		if (pdpfb_pool_alloc(&priv->pools[PDPFB_MEMPOOL_MEM],
				     stream, len))
			goto err_nomem;
		stream->mem_pool = PDPFB_MEMPOOL_MEM;
		return 0;
	}
	/* Finally try kernel allocated memory */
	videomem = kzalloc(len, GFP_KERNEL);
	if (!videomem)
		goto err_nomem;
	stream->mem_pool = PDPFB_MEMPOOL_KERNEL;
	stream->videomem = videomem;
	stream->videomem_len = len;
	info->screen_base = (char __force __iomem *)videomem;
	info->fix.smem_start = __pa(videomem);
	info->fix.smem_len = stream->videomem_len;
	return 0;

err_nomem:
	dev_err(priv->device, "unable to allocate video memory\n");
	return -ENOMEM;
}

static int pdpfb_probe_streams(struct pdpfb_priv *priv,
			       struct platform_device *pdev)
{
	int i;
	int error;

	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		if (!stream)
			continue;
		stream->probed = 1;
		error = stream->ops.probe(priv, pdev, &priv->mode);
		if (error)
			stream->probed = 0;
		if (error == -ENOMEM)
			dev_err(priv->device,
				"unable to initialise fb %d, "
				"not enough video memory\n",
				i);
	}

	return 0;
}

static void pdpfb_remove_streams(struct pdpfb_priv *priv,
				struct platform_device *pdev)
{
	int i;

	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		if (stream && stream->probed)
			stream->ops.remove(priv, pdev);
	}
}

static int pdpfb_stop(struct pdpfb_priv *priv)
{
	pdpfb_configure(priv, PDPFB_POWER_FULLY_OFF);

	return 0;
}

static void pdpfb_probe_caps(struct pdpfb_priv *priv)
{
	u32 palette_bpc = ((PDP_REV >= 0x010001) ? 8 : 6);

	priv->palette_fmt.r.length
		= priv->palette_fmt.g.length
		= priv->palette_fmt.b.length
		= palette_bpc;
	priv->palette_fmt.b.offset = 0;
	priv->palette_fmt.g.offset = palette_bpc;
	priv->palette_fmt.r.offset = palette_bpc*2;
}

#ifdef PDP_SHARED_BASE
static int pdpfb_init_mem(struct pdpfb_priv *priv,
			  struct pdp_info *pdata)
{
	struct pdpfb_mem_pool *pool;
	void *videomem;
	unsigned long len;

	if (unlikely(!pdata->hwops.set_shared_base)) {
		dev_err(priv->device, "no set_shared_base in platform data\n");
		return -EINVAL;
	}
	/* If fixed memory pool already exists, don't alloc from kernel mem. */
	pool = &priv->pools[PDPFB_MEMPOOL_MEM];
	if (pool->base) {
		pdata->hwops.set_shared_base(pool->phys_base);
		return 0;
	}

	len	= ((gfx_videomem_len + PAGE_SIZE - 1) & PAGE_MASK)
#ifdef CONFIG_FB_PDP_VID
		+ ((vid_videomem_len + PAGE_SIZE - 1) & PAGE_MASK)
#endif
		;
	videomem = kzalloc(len, GFP_KERNEL);
	if (!videomem) {
		dev_err(priv->device, "unable to allocate video memory\n");
		return -ENOMEM;
	}
	pool->base = videomem;
	pool->phys_base = __pa(videomem);
	pool->size = len;
	pool->flags = PDPFB_MEMPOOLF_KERNEL;
	pdata->hwops.set_shared_base(pool->phys_base);

	return 0;
}
#else
static inline int pdpfb_init_mem(struct pdpfb_priv *priv,
				 struct pdp_info *pdata)
{
	return 0;
}
#endif

static void pdpfb_free_pools(struct pdpfb_priv *priv)
{
	int i;
	for (i = 0; i < PDPFB_MEMPOOL_NR_POOLS; ++i) {
		if (!priv->pools[i].base)
			continue;
		if (priv->pools[i].flags & PDPFB_MEMPOOLF_KERNEL)
			kfree(priv->pools[i].base);
		else
			iounmap((void __force __iomem *)priv->pools[i].base);
	}
}

static int pdpfb_init_mempool(struct pdpfb_priv *priv, int type,
			      unsigned long phys, unsigned long len)
{
	struct pdpfb_mem_pool *pool = &priv->pools[type];
	/* First come first served */
	if (unlikely(pool->base))
		return -EBUSY;
	pool->base = (void __force *)ioremap_cached(phys, len);
	if (unlikely(!pool->base)) {
		dev_err(priv->device, "cannot ioremap %s @0x%08lx:0x%lx\n",
			pdpfb_mem_pool_names[type], phys, len);
		return -ENOMEM;
	}
	dev_info(priv->device, "%s @0x%08lx:0x%lx (ioremapped to 0x%p)\n",
			pdpfb_mem_pool_names[type],
			phys, len, pool->base);
	priv->pools[type].phys_base = phys;
	priv->pools[type].size = len;
	priv->pools[type].flags = 0;
	/* Clear the memory */
	memset(priv->pools[type].base, 0, priv->pools[type].size);
	return 0;
}

static int pdpfb_probe_params(struct platform_device *pdev,
			      struct pdpfb_priv *priv)
{
	if (videomem_base > 0 && videomem_len > 0)
		pdpfb_init_mempool(priv, PDPFB_MEMPOOL_MEM,
				   videomem_base, videomem_len);
#ifndef PDP_SHARED_BASE
	if (gfx_videomem_base > 0 && gfx_videomem_len > 0)
		pdpfb_init_mempool(priv, PDPFB_MEMPOOL_GFXMEM,
				   gfx_videomem_base, gfx_videomem_len);
#ifdef CONFIG_FB_PDP_VID
	if (vid_videomem_base > 0 && vid_videomem_len > 0)
		pdpfb_init_mempool(priv, PDPFB_MEMPOOL_VIDMEM,
				   vid_videomem_base, vid_videomem_len);
#endif
#endif
	return 0;
}

static int pdpfb_probe_state(struct pdpfb_priv *priv)
{
	u32 sync_ctrl;
	unsigned long pix_freq;

	/* try and determine the current state */
	sync_ctrl = pdpfb_read(priv, PDP_SYNCCTRL);
	if (!GET_FIELD(sync_ctrl, PDP_SYNCCTRL_SYNCACTIVE))
		goto done;

	/* sync is active, what about the pixel clock? */
	pix_freq = clk_get_rate(priv->pixel_clk);
	if (!pix_freq)
		goto done;
	dev_dbg(priv->device, "initial pixel frequency = %lu Hz\n", pix_freq);

	/* pixel clock active, try and extract the sync timings */
	if (pdpfb_extract_sync(priv))
		goto done;
	dev_dbg(priv->device, "initial mode %ux%u sync=(%u,%u) margins=(%u,%u),(%u,%u)\n",
		priv->mode.xres, priv->mode.yres,
		priv->mode.vsync_len, priv->mode.hsync_len,
		priv->mode.upper_margin, priv->mode.left_margin,
		priv->mode.lower_margin, priv->mode.right_margin);

	priv->mode.pixclock = KHZ2PICOS(pix_freq/1000);
	pdpfb_pixel_clk_enable(priv);
	if (GET_FIELD(sync_ctrl, PDP_SYNCCTRL_POWERDN))
		priv->power = PDPFB_POWER_SYNC_ENABLED;
	else
		priv->power = PDPFB_POWER_BACKLIT;

done:
	dev_dbg(priv->device, "initial power = %d\n", priv->power);

	return 0;
}

static int pdpfb_probe_mems(struct platform_device *pdev,
			    struct pdpfb_priv *priv)
{
	struct resource *res_mem;
	int i;
	int type;
	unsigned long len;
	for (i = 0; ; ++i) {
		res_mem = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res_mem)
			break;

		type = res_mem->flags & IORESOURCE_BITS;
		len = res_mem->end - res_mem->start;
		if (type < PDPFB_MEMPOOL_NR_POOLS) {
			/* Videomem pool */
#ifdef PDP_SHARED_BASE
			if (unlikely(type != PDPFB_MEMPOOL_MEM))
				continue;
#endif
#ifndef CONFIG_FB_PDP_VID
			if (unlikely(type == PDPFB_MEMPOOL_VIDMEM))
				continue;
#endif
			pdpfb_init_mempool(priv, type,
					   res_mem->start, len);
		} else if (type == PDPFB_IORES_PDP) {
			/* PDP register region */
			if (priv->base) {
				dev_warn(&pdev->dev,
					"multiple pdp register ioresources\n");
				continue;
			}
			priv->base = ioremap_nocache(res_mem->start, len);
			if (!priv->base)
				return -ENOMEM;
		}
	}
	return 0;
}

static int pdpfb_probe(struct platform_device *pdev)
{
	struct pdpfb_priv *priv;
	struct pdp_info *pdata;
	struct resource *res_irq;
	int error;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto err0;
	}

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res_irq == NULL) {
		dev_err(&pdev->dev, "cannot find IRQ resource\n");
		error = -ENOENT;
		goto err0;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		error = -ENOMEM;
		goto err0;
	}
	spin_lock_init(&priv->upd.lock);
	spin_lock_init(&priv->irq_lock);
	pdpfb_lut_init(priv);

	platform_set_drvdata(pdev, priv);
	pdata = pdev->dev.platform_data;
	priv->mode = pdata->lcd_cfg;
	priv->power = PDPFB_POWER_FULLY_OFF;
	priv->final_blank = -1;

	priv->device = &pdev->dev;

	error = pdpfb_probe_params(pdev, priv);
	if (error)
		goto err1;

	error = pdpfb_probe_mems(pdev, priv);
	if (error)
		goto err1;
	if (!priv->base) {
		dev_err(&pdev->dev, "no pdp register ioresource\n");
		error = -ENOMEM;
		goto err1;
	}

	priv->pdp_clk = clk_get(priv->device, "pdp");
	if (IS_ERR(priv->pdp_clk)) {
		dev_err(&pdev->dev, "could not get pdp clock resource\n");
		error = PTR_ERR(priv->pdp_clk);
		goto err1;
	}
	clk_prepare(priv->pdp_clk);

	priv->pixel_clk = clk_get(priv->device, "pixel");
	if (IS_ERR(priv->pixel_clk)) {
		dev_err(&pdev->dev, "could not get pixel clock resource\n");
		error = PTR_ERR(priv->pixel_clk);
		goto err2;
	}

	pdpfb_pdp_clk_enable(priv);

	pdpfb_probe_state(priv);

	error = pdpfb_init_mem(priv, pdata);
	if (error)
		goto err3;

	priv->irq = res_irq->start;
	error = request_irq(priv->irq, pdpfb_interrupt, 0, "pdp", priv);
	if (error) {
		dev_err(&pdev->dev, "cannot register IRQ %d (%d)\n",
				priv->irq,
				error);
		error = -EIO;
		goto err3;
	}

	pdpfb_probe_caps(priv);

	pdpfb_priv = priv;
	/* don't bother counting vsync interrupts with Pdump to reduce noise */
#ifndef CONFIG_FB_PDP_PDUMP
	pdpfb_register_isr(pdpfb_vsync_isr, priv, PDPFB_IRQ_VEVENT0);
#endif

	priv->streams[0] = pdpfb_gfx_get_stream();
	priv->streams[0]->mode_master = pdata->lcd_size_cfg.dynamic_mode;
#ifdef CONFIG_FB_PDP_VID
	priv->streams[1] = pdpfb_vid_get_stream();
#endif

	/*
	 * We need to be able to work out the pixel clock that we can achieve
	 * prior to registering the framebuffer, else fbcon can try and set the
	 * mode and reset the frequency.
	 */
	pdpfb_update_refresh_rate(priv);

	if (pdpfb_probe_streams(priv, pdev))
		goto err4;

	/*
	 * If sync is already enabled, update each stream's margins since it
	 * won't get done by pdpfb_configure
	 */
	if (priv->power <= PDPFB_POWER_SYNC_ENABLED)
		pdpfb_update_all_margins(priv);

	/* Turn everything on in the right sequence */
	console_lock();
	pdpfb_configure(priv, PDPFB_POWER_BACKLIT);
	console_unlock();

	_pdpfb_set_irqs(priv);

	pdpfb_ident(priv);

	return 0;
err4:
#ifndef CONFIG_FB_PDP_PDUMP
	pdpfb_unregister_isr(pdpfb_vsync_isr, priv, PDPFB_IRQ_VEVENT0);
#endif
	pdpfb_priv = NULL;
	free_irq(priv->irq, priv);
err3:
	pdpfb_pixel_clk_disable(priv);
	pdpfb_pdp_clk_disable(priv);
	clk_put(priv->pixel_clk);
err2:
	clk_unprepare(priv->pdp_clk);
	clk_put(priv->pdp_clk);
err1:
	if (priv->base)
		iounmap(priv->base);
	pdpfb_free_pools(priv);

	kfree(priv);
err0:
	return error;
}

static int pdpfb_remove(struct platform_device *pdev)
{
	struct pdpfb_priv *priv = platform_get_drvdata(pdev);

	console_lock();
	pdpfb_stop(priv);
	console_unlock();
	pdpfb_pdp_clk_disable(priv);

#ifndef CONFIG_FB_PDP_PDUMP
	pdpfb_unregister_isr(pdpfb_vsync_isr, priv, PDPFB_IRQ_VEVENT0);
#endif
	pdpfb_priv = NULL;
	pdpfb_remove_streams(priv, pdev);
	free_irq(priv->irq, priv);
	clk_put(priv->pixel_clk);
	clk_unprepare(priv->pdp_clk);
	clk_put(priv->pdp_clk);
	if (priv->base)
		iounmap(priv->base);
	pdpfb_free_pools(priv);

	kfree(priv);
	return 0;
}

static void pdpfb_shutdown(struct platform_device *pdev)
{
	struct pdpfb_priv *priv = platform_get_drvdata(pdev);

	console_lock();
	pdpfb_stop(priv);
	console_unlock();
}

#ifdef CONFIG_PM
static int pdpfb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pdpfb_priv *priv = platform_get_drvdata(pdev);
	int i;

	console_lock();
	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		if (!stream || !stream->probed)
			continue;
		fb_set_suspend(&stream->info, 1);
	}
	pdpfb_stop(priv);
	pdpfb_pdp_clk_disable(priv);
	console_unlock();

	return 0;
}

static int pdpfb_resume(struct platform_device *pdev)
{
	struct pdpfb_priv *priv = platform_get_drvdata(pdev);
#ifdef PDP_SHARED_BASE
	struct pdp_info *pdata = priv->device->platform_data;
#endif
	int i;

	console_lock();
	pdpfb_pdp_clk_enable(priv);
#ifdef PDP_SHARED_BASE
	/* restore shared base pointer */
	if (pdata->hwops.set_shared_base) {
		struct pdpfb_mem_pool *pool;
		if (priv->pools[PDPFB_MEMPOOL_USER].base)
			pool = &priv->pools[PDPFB_MEMPOOL_USER];
		else
			pool = &priv->pools[PDPFB_MEMPOOL_MEM];
		pdata->hwops.set_shared_base(pool->phys_base);
	}
#endif
	pdpfb_configure(priv, PDPFB_POWER_BACKLIT);
	for (i = 0; i < PDPFB_STREAM_NR; ++i) {
		struct pdpfb_stream *stream = priv->streams[i];
		if (!stream || !stream->probed)
			continue;
		fb_set_suspend(&stream->info, 0);
	}
	console_unlock();

	return 0;
}
#else
#define pdpfb_suspend NULL
#define pdpfb_resume NULL
#endif	/* CONFIG_PM */

static struct platform_driver pdpfb_driver = {
	.driver		= {
		.name		= "pdpfb",
		.owner		= THIS_MODULE,
	},
	.probe		= pdpfb_probe,
	.remove		= pdpfb_remove,
	.shutdown	= pdpfb_shutdown,
	.suspend	= pdpfb_suspend,
	.resume		= pdpfb_resume,
};

static int pdpfb_init(void)
{
	return platform_driver_register(&pdpfb_driver);
}

static void pdpfb_exit(void)
{
	platform_driver_unregister(&pdpfb_driver);
}

module_init(pdpfb_init);
module_exit(pdpfb_exit);

MODULE_DESCRIPTION("PDP Framebuffer driver");
MODULE_AUTHOR("Will Newton <will.newton@imgtec.com>");
MODULE_AUTHOR("James Hogan <james.hogan@imgtec.com>");
MODULE_LICENSE("GPL v2");
