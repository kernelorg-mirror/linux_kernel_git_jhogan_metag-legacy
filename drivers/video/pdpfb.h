/*
 * PDP Framebuffer
 *
 * Copyright (c) 2008 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _PDPFB_H
#define _PDPFB_H

#include <linux/fb.h>
#include <video/pdpfb.h>

/* display colour palettes on mode changes */
/*#define PDP_REMODE_COLOUR_PALETTES*/

#define PDP_PALETTE_NR 256

/* Stream interface */

struct platform_device;
struct pdp_info;

struct pdpfb_priv;
struct pdpfb_stream;

struct pdpfb_stream_ops {
	int (*probe)(struct pdpfb_priv *priv, struct platform_device *pdev,
		     const struct fb_videomode *mode);
	int (*remove)(struct pdpfb_priv *priv, struct platform_device *pdev);
	int (*check_geom)(struct pdpfb_priv *priv, struct pdpfb_geom *geom);
	int (*set_geom)(struct pdpfb_priv *priv);
	int (*configure)(struct pdpfb_priv *priv);
	int (*configure_addr)(struct pdpfb_priv *priv);
};

struct pdpfb_stream_regs {
	u16 surf;
	u16 blend;
	u16 blend2;
	u16 ctrl;
	u16 posn;
	u16 addr[3];
	u16 gamma;
	u16 gamma_stride;
};

struct pdpfb_stream_caps {
	unsigned int gamma;	/* number of gamma entries */
};

struct pdpfb_stream {
	struct fb_info info;
	int probed;

	unsigned char mem_pool;
	void *videomem;
#ifdef PDP_SHARED_BASE
	unsigned long videomem_offset;
#endif
	unsigned long videomem_len;

	int enable;
	int ckey_en;
	int ckey_src;	/* see PDP_STRXCTRL_CKEYSRC_* */
	struct pdpfb_ckey ckey;
	u32 blend_mode;
	u8 global_alpha;
	struct pdpfb_geom geom;
	int mode_master; /* framebuffer controls screen mode */

	struct pdpfb_stream_ops ops;
	struct pdpfb_stream_regs regs;
	struct pdpfb_stream_caps caps;
};

struct pdp_info *pdpfb_get_platform_data(struct pdpfb_priv *priv);
u32 *pdpfb_get_pseudo_palette(struct pdpfb_priv *priv);
void pdpfb_enable_palette(struct pdpfb_priv *priv);
void pdpfb_disable_palette(struct pdpfb_priv *priv);

void pdpfb_str_videomem_free(struct pdpfb_stream *stream);
int pdpfb_str_videomem_alloc(struct pdpfb_priv *priv,
				struct pdpfb_stream *stream);

void pdpfb_update_margins(struct pdpfb_priv *priv,
			struct pdpfb_stream *stream);
int pdpfb_str_ioctl(struct pdpfb_priv *priv, struct pdpfb_stream *stream,
			unsigned int cmd, unsigned long arg);

int pdpfb_setcolreg(u_int regno,
		u_int red, u_int green, u_int blue,
		u_int trans, struct fb_info *info);
int pdpfb_setcmap(struct fb_cmap *cmap, struct fb_info *info);
int pdpfb_blank(int blank, struct fb_info *info);

unsigned long pdpfb_get_line_length(int xres_virtual, int bpp);
int pdpfb_configure_sync(struct pdpfb_priv *priv);
void pdpfb_set_mode(struct pdpfb_priv *priv, struct fb_var_screeninfo *var);
int pdpfb_update_pixclock(struct pdpfb_priv *priv, unsigned int pixclock);

/* Register access */

void pdpfb_clock_write(struct pdpfb_priv *priv,
		      unsigned int reg_offs, unsigned int data);
unsigned int pdpfb_clock_read(struct pdpfb_priv *priv,
			     unsigned int reg_offs);
void pdpfb_write(struct pdpfb_priv *priv,
		      unsigned int reg_offs, unsigned int data);
unsigned int pdpfb_read(struct pdpfb_priv *priv,
			     unsigned int reg_offs);

#endif
