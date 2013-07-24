/*
 * PDP Desktop Graphics Framebuffer
 *
 * Copyright (c) 2008 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/fb.h>
#include <linux/platform_device.h>

#include <asm/soc-chorus2/pdp.h>
#include "pdpfb.h"
#include "pdpfb_regs.h"
#include "pdpfb_gfx.h"

#define THIS_STREAM (&pdpfb_gfx_stream.stream)
#define THIS_PRIV (&pdpfb_gfx_stream)

static struct {
	/* information for setting up pdp */
	u32 id;
	int uselut;
	/* information for matching */
	int bpp, grayscale;
	struct fb_bitfield a, r, g, b;
} pixfmts[] = {
	{
		.id = PDP_STR1SURF_PIXFMT_RGB8,
		.uselut = 1,
		.bpp = 8,
		.r = { .offset = 0,  .length = 8 },
		.g = { .offset = 0,  .length = 8 },
		.b = { .offset = 0,  .length = 8 },
	},
	{
		.id = PDP_STR1SURF_PIXFMT_RGB8,
		.bpp = 8,
		.grayscale = 1,
		.r = { .offset = 0,  .length = 8 },
		.g = { .offset = 0,  .length = 8 },
		.b = { .offset = 0,  .length = 8 },
	},
	{
		.id = PDP_STR1SURF_PIXFMT_RGB565,
		.bpp = 16,
		.r = { .offset = 11, .length = 5 },
		.g = { .offset = 5,  .length = 6 },
		.b = { .offset = 0,  .length = 5 },
	},
	{
		.id = PDP_STR1SURF_PIXFMT_ARGB4444,
		.bpp = 16,
		.a = { .offset = 12, .length = 4 },
		.r = { .offset = 8,  .length = 4 },
		.g = { .offset = 4,  .length = 4 },
		.b = { .offset = 0,  .length = 4 },
	},
	{
		.id = PDP_STR1SURF_PIXFMT_ARGB1555,
		.bpp = 16,
		.a = { .offset = 15, .length = 1 },
		.r = { .offset = 10, .length = 5 },
		.g = { .offset = 5,  .length = 5 },
		.b = { .offset = 0,  .length = 5 },
	},
#if PDP_REV >= 0x010001
	{
		.id = PDP_STR1SURF_PIXFMT_RGB888,
		.bpp = 24,
		.r = { .offset = 16, .length = 8 },
		.g = { .offset = 8,  .length = 8 },
		.b = { .offset = 0,  .length = 8 },
	},
	{
		.id = PDP_STR1SURF_PIXFMT_ARGB8888,
		.bpp = 32,
		.a = { .offset = 24, .length = 8 },
		.r = { .offset = 16, .length = 8 },
		.g = { .offset = 8,  .length = 8 },
		.b = { .offset = 0,  .length = 8 },
	},
#endif
};

static struct pdpfb_gfx_stream_priv {
	struct pdpfb_stream stream;
	int pixfmt; /* index into pixfmts */
	/* fields to update on vevent */
#ifdef PDP_SHARED_BASE
	unsigned int base_addr;
#endif
} pdpfb_gfx_stream;

enum {
	pixfmt_match_none = 0,
	pixfmt_match_bpp,
	pixfmt_match_alpha,
	pixfmt_match_bitsorgray,
	pixfmt_match_exact,
};

static int pdpfb_gfx_match_var(struct pdpfb_priv *priv,
				struct fb_var_screeninfo *var)
{
	int i;
	int best_pixfmt = -1;
	int best_rating = pixfmt_match_none;

	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
#if PDP_REV < 0x010001
	else
		var->bits_per_pixel = 16;
#else
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else
		var->bits_per_pixel = 32;
#endif

#define PIXFMT_MATCH(rating, cond) \
		if (!(cond)) \
			continue; \
		if (best_rating < (rating)) { \
			best_rating = (rating); \
			best_pixfmt = i; \
			if ((rating) == pixfmt_match_exact) \
				break; \
		}

	for (i = 0; i < ARRAY_SIZE(pixfmts); ++i) {
		int match_bits, match_gray;

		PIXFMT_MATCH(pixfmt_match_bpp,
			var->bits_per_pixel == pixfmts[i].bpp);
		PIXFMT_MATCH(pixfmt_match_alpha,
			(var->transp.length != 0)
			== (pixfmts[i].a.length != 0));

		match_bits = (var->red.offset == pixfmts[i].r.offset &&
			var->red.length == pixfmts[i].r.length &&
			var->green.offset == pixfmts[i].g.offset &&
			var->green.length == pixfmts[i].g.length &&
			var->blue.offset == pixfmts[i].b.offset &&
			var->blue.length == pixfmts[i].b.length &&
			var->transp.offset == pixfmts[i].a.offset &&
			var->transp.length == pixfmts[i].a.length);
		match_gray = (var->grayscale == pixfmts[i].grayscale);

		PIXFMT_MATCH(pixfmt_match_bitsorgray, match_bits || match_gray);
		PIXFMT_MATCH(pixfmt_match_exact, match_bits && match_gray);
	}

#undef PIXFMT_MATCH

	var->bits_per_pixel = pixfmts[best_pixfmt].bpp;
	var->red = pixfmts[best_pixfmt].r;
	var->green = pixfmts[best_pixfmt].g;
	var->blue = pixfmts[best_pixfmt].b;
	var->transp = pixfmts[best_pixfmt].a;
	var->grayscale = pixfmts[best_pixfmt].grayscale;
	return best_pixfmt;
}

static int pdpfb_gfx_check_geom(struct pdpfb_priv *priv,
				struct pdpfb_geom *geom)
{
	struct fb_info *info = &THIS_STREAM->info;

	/* only doubling and halving */
	if (geom->w) {
		if (geom->w*4 <= info->var.xres*3)
			geom->w = info->var.xres / 2;
		else if (geom->w*2 >= info->var.xres*3)
			geom->w = info->var.xres * 2;
		else
			geom->w = 0;
	}
	if (geom->h) {
		if (geom->h*4 <= info->var.yres*3)
			geom->h = info->var.yres / 2;
		else if (geom->h*2 >= info->var.yres*3)
			geom->h = info->var.yres * 2;
		else
			geom->h = 0;
	}
	return 0;
}

static int pdpfb_gfx_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct pdpfb_priv *priv = dev_get_drvdata(info->device);
	const struct fb_videomode *mode;
	u_long line_length;

	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;

	/* if not fixed to native res, don't mode match unless mode master */
#ifndef CONFIG_FB_PDP_GFX_FIX_NATIVE_RES
	if (THIS_STREAM->mode_master)
#endif
	{
		mode = fb_find_best_mode(var, &info->modelist);
		if (mode) {
			unsigned int xres_virtual = var->xres_virtual;
			unsigned int yres_virtual = var->yres_virtual;
			unsigned int xoffset = var->xoffset;
			unsigned int yoffset = var->yoffset;
			unsigned int pixclock = var->pixclock;
			fb_videomode_to_var(var, mode);
			var->xres_virtual = xres_virtual;
			var->yres_virtual = yres_virtual;
			var->xoffset = xoffset;
			var->yoffset = yoffset;
			/* on a fixed screen, stick to the requested pixclock */
			if (!THIS_STREAM->mode_master)
				var->pixclock = pixclock;
		} else {
			return -EINVAL;
		}
	}

	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	pdpfb_gfx_match_var(priv, var);

	/* Memory limit */
	line_length = pdpfb_get_line_length(var->xres_virtual,
						var->bits_per_pixel);
	if (line_length * var->yres_virtual > THIS_STREAM->videomem_len)
		return -ENOMEM;

	return 0;
}

static int pdpfb_gfx_ioctl(struct fb_info *info, unsigned int cmd,
				unsigned long arg)
{
	struct pdpfb_priv *priv = dev_get_drvdata(info->device);
	return pdpfb_str_ioctl(priv, THIS_STREAM, cmd, arg);
}

#ifdef PDP_REMODE_COLOUR_PALETTES
static void pdpfb_gfx_draw_colour_palette(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;

	if (info->var.bits_per_pixel == 8) {
		u8 *buf = (u8 *)THIS_STREAM->videomem;
		int i, j;
		for (j = 0; j < info->var.yres_virtual; ++j) {
			for (i = 0; i < info->var.xres_virtual; ++i) {
				int x, y;
				x = 16*i/info->var.xres_virtual;
				y = 16*j/info->var.yres_virtual;
				buf[j*info->fix.line_length/sizeof(buf[0]) + i]
					= x|(y<<4);
			}
		}
	} else if (info->var.bits_per_pixel == 16) {
		u16 *buf = (u16 *)THIS_STREAM->videomem;
		int i, j;
		for (j = 0; j < info->var.yres_virtual; ++j) {
			for (i = 0; i < info->var.xres_virtual; ++i) {
				int x, y;
				x = 256*i/info->var.xres_virtual;
				y = 256*j/info->var.yres_virtual;
				buf[j*info->fix.line_length/sizeof(buf[0]) + i]
					= (x<<8) | y;
			}
		}
	}
}
#endif

static int pdpfb_gfx_change_mode(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	u32 str1posn;

	info->fix.line_length = pdpfb_get_line_length(info->var.xres_virtual,
						info->var.bits_per_pixel);
	str1posn = pdpfb_read(priv, PDP_STR1POSN);
	SET_FIELD(str1posn, PDP_STRXPOSN_SRCSTRIDE,
			info->fix.line_length / 16 - 1);
	pdpfb_write(priv, PDP_STR1POSN, str1posn);

	if (info->var.bits_per_pixel == 8)
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	else
		info->fix.visual = FB_VISUAL_TRUECOLOR;

	return 0;
}

static int pdpfb_gfx_configure_scale(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	struct pdpfb_geom *geom = &THIS_STREAM->geom;

	u32 blend2;
	u32 line_double = 0;
	u32 line_halve = 0;
	u32 pix_double = 0;
	u32 pix_halve = 0;

	if (geom->w) {
		pix_double = (geom->w > info->var.xres);
		pix_halve = (geom->w < info->var.xres);
	}
	if (geom->h) {
		line_double = (geom->h > info->var.yres);
		line_halve = (geom->h < info->var.yres);
	}

	blend2 = pdpfb_read(priv, THIS_STREAM->regs.blend2);
	SET_FIELD(blend2, PDP_STRXBLEND2_LINEDOUBLE, line_double);
	SET_FIELD(blend2, PDP_STRXBLEND2_LINEHALVE, line_halve);
	SET_FIELD(blend2, PDP_STRXBLEND2_PIXDOUBLE, pix_double);
	SET_FIELD(blend2, PDP_STRXBLEND2_PIXHALVE, pix_halve);
	pdpfb_write(priv, THIS_STREAM->regs.blend2, blend2);

	return 0;
}

#ifdef PDP_SHARED_BASE
static void pdpfb_gfx_apply_addr(void *arg, u32 mask)
{
	struct pdpfb_priv *priv = (struct pdpfb_priv *)arg;
	u32 str_ctrl;

	pdpfb_unregister_isr(pdpfb_gfx_apply_addr, priv,
			     PDPFB_IRQ_VEVENT0);

	str_ctrl = pdpfb_read(priv, PDP_STR1CTRL);
	SET_FIELD(str_ctrl, PDP_STRXCTRL_BASEADDR, THIS_PRIV->base_addr >> 4);
	pdpfb_write(priv, PDP_STR1CTRL, str_ctrl);
}
#endif

static int pdpfb_gfx_configure_addr(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	u32 buf_offset, buf_start;

	buf_offset = info->var.yoffset * info->fix.line_length
		+ info->var.xoffset * info->var.bits_per_pixel / 8;
	buf_start = buf_offset;
#ifndef PDP_SHARED_BASE
	buf_start += info->fix.smem_start;
	pdpfb_write(priv, PDP_STR1ADDR, buf_start >> 4);
#else
	buf_start += THIS_STREAM->videomem_offset;
	THIS_PRIV->base_addr = buf_start;
	pdpfb_register_isr(pdpfb_gfx_apply_addr, priv,
			   PDPFB_IRQ_VEVENT0);
#endif

	return 0;
}

static int pdpfb_gfx_configure(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	u32 str1surf;
	int pixfmt = THIS_PRIV->pixfmt;

	str1surf = pdpfb_read(priv, PDP_STR1SURF);
	/* enable/disable palette updates if uselut is changing */
	if (pixfmts[pixfmt].uselut) {
		if (!GET_FIELD(str1surf, PDP_STR1SURF_USELUT))
			pdpfb_enable_palette(priv);
	} else if (GET_FIELD(str1surf, PDP_STR1SURF_USELUT)) {
		pdpfb_disable_palette(priv);
	}
	SET_FIELD(str1surf, PDP_STRXSURF_WIDTH, info->var.xres - 1);
	SET_FIELD(str1surf, PDP_STRXSURF_HEIGHT, info->var.yres - 1);
	SET_FIELD(str1surf, PDP_STR1SURF_USELUT, pixfmts[pixfmt].uselut);
	SET_FIELD(str1surf, PDP_STR1SURF_PIXFMT, pixfmts[pixfmt].id);
	pdpfb_write(priv, PDP_STR1SURF, str1surf);

	pdpfb_gfx_change_mode(priv);
	pdpfb_gfx_configure_scale(priv);

#ifdef PDP_REMODE_COLOUR_PALETTES
	pdpfb_gfx_draw_colour_palette(priv);
#endif

	return 0;
}

static int pdpfb_gfx_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct pdpfb_priv *priv;

	if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	    var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	priv = dev_get_drvdata(info->device);
	pdpfb_gfx_configure_addr(priv);

	return 0;
}

static int pdpfb_gfx_set_par(struct fb_info *info)
{
	struct pdpfb_priv *priv = dev_get_drvdata(info->device);
	int xres, yres, w, h;
	u32 pix_clock;

	THIS_PRIV->pixfmt = pdpfb_gfx_match_var(priv, &info->var);
	if (THIS_STREAM->mode_master) {
		pdpfb_set_mode(priv, &info->var);
	} else {
		pdpfb_gfx_configure_addr(priv);
		pdpfb_gfx_configure(priv);

		/*
		 * Update pixel clock if it's changed, otherwise just update
		 * margins.
		 */
		pix_clock = info->var.pixclock;
		if (THIS_STREAM->geom.w || THIS_STREAM->geom.h) {
			xres = info->var.xres;
			yres = info->var.yres;
			w = (THIS_STREAM->geom.w ? THIS_STREAM->geom.w : xres);
			h = (THIS_STREAM->geom.h ? THIS_STREAM->geom.h : yres);
			pix_clock = pix_clock * xres * yres / (w * h);
		}
		if (!pdpfb_update_pixclock(priv, pix_clock))
			pdpfb_update_margins(priv, THIS_STREAM);
	}

	info->fix.xpanstep = 16 * 8 / info->var.bits_per_pixel;

	return 0;
}


static struct fb_fix_screeninfo pdpfb_gfx_fix  = {
	.id =		"pdp",
	.type =		FB_TYPE_PACKED_PIXELS,
	.xpanstep =	8,
	.ypanstep =	1,
	.accel =	FB_ACCEL_IMG_PDP_1,
};

static struct fb_ops pdpfb_gfx_ops = {
	.fb_setcolreg	= pdpfb_setcolreg,
	.fb_setcmap	= pdpfb_setcmap,
	.fb_blank	= pdpfb_blank,
	.fb_pan_display	= pdpfb_gfx_pan_display,
	.fb_check_var	= pdpfb_gfx_check_var,
	.fb_set_par	= pdpfb_gfx_set_par,
	.fb_ioctl	= pdpfb_gfx_ioctl,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int pdpfb_gfx_probe(struct pdpfb_priv *priv,
				struct platform_device *pdev,
				const struct fb_videomode *mode)
{
	struct fb_info *info = &THIS_STREAM->info;
	struct pdp_info *pdata = pdpfb_get_platform_data(priv);
	int error;
	u_long line_length;

	info->device = &pdev->dev;
	info->fbops = &pdpfb_gfx_ops;
	info->var.xres = info->var.xres_virtual = mode->xres;
	info->var.yres = info->var.yres_virtual = mode->yres;
	info->var.width = pdata->lcd_size_cfg.width;
	info->var.height = pdata->lcd_size_cfg.height;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.bits_per_pixel = pdata->bpp;
	error = THIS_PRIV->pixfmt = pdpfb_gfx_match_var(priv, &info->var);
	if (error < 0)
		goto err0;

	info->var.pixclock = mode->pixclock;
	info->var.hsync_len = mode->hsync_len;
	info->var.left_margin = mode->left_margin;
	info->var.right_margin = mode->right_margin;
	info->var.vsync_len = mode->vsync_len;
	info->var.upper_margin = mode->upper_margin;
	info->var.lower_margin = mode->lower_margin;

	info->fix = pdpfb_gfx_fix;
	info->fix.xpanstep = 16 * 8 / info->var.bits_per_pixel;

	error = pdpfb_str_videomem_alloc(priv, THIS_STREAM);
	if (error)
		goto err0;

	line_length = pdpfb_get_line_length(info->var.xres_virtual,
					info->var.bits_per_pixel);
	if (line_length * info->var.yres_virtual > THIS_STREAM->videomem_len) {
		error = -ENOMEM;
		goto err1;
	}

	info->pseudo_palette = pdpfb_get_pseudo_palette(priv);
	info->flags = FBINFO_FLAG_DEFAULT
			| FBINFO_HWACCEL_XPAN
			| FBINFO_HWACCEL_YPAN;

#ifdef PDP_GAMMA
	THIS_STREAM->caps.gamma = PDP_GAMMA;
#endif

	error = register_framebuffer(info);
	if (error < 0)
		goto err1;

	dev_info(&pdev->dev, "registered graphics framebuffer (len=0x%lx)\n",
		 THIS_STREAM->videomem_len);

	return 0;
err1:
	pdpfb_str_videomem_free(THIS_STREAM);
err0:
	return error;
}

static int pdpfb_gfx_remove(struct pdpfb_priv *priv,
				struct platform_device *pdev)
{
	struct fb_info *info = &THIS_STREAM->info;

	unregister_framebuffer(info);

	pdpfb_str_videomem_free(THIS_STREAM);
	return 0;
}

static struct pdpfb_gfx_stream_priv pdpfb_gfx_stream = {
	.stream = {
		.mem_pool = PDPFB_MEMPOOL_GFXMEM,
		.videomem_len = 0,
		.enable = 1,
		.ckey = {
			.ckey = 0x000000,
			.mask = 0xFFFFFF,
		},
		.global_alpha = 0xFF,
		.ops = {
			.probe = pdpfb_gfx_probe,
			.remove = pdpfb_gfx_remove,
			.check_geom = pdpfb_gfx_check_geom,
			.configure = pdpfb_gfx_configure,
			.configure_addr = pdpfb_gfx_configure_addr,
		},
		.regs = {
			.surf = PDP_STR1SURF,
			.blend = PDP_STR1BLEND,
			.blend2 = PDP_STR1BLEND2,
			.ctrl = PDP_STR1CTRL,
			.posn = PDP_STR1POSN,
#if PDP_REV >= 0x010001
			.gamma = PDP_RGBGAMMA0,
			.gamma_stride = PDP_RGBGAMMA_STRIDE,
#endif
		},
	},
};

struct pdpfb_stream *pdpfb_gfx_get_stream(void)
{
	return THIS_STREAM;
}
