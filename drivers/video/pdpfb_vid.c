/*
 * PDP Scaled Video Framebuffer
 *
 * Copyright (c) 2008-2012 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/fb.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <asm/soc-chorus2/pdp.h>
#include <video/pdpfb.h>
#include "pdpfb.h"
#include "pdpfb_regs.h"
#include "pdpfb_vid.h"

/* warn if scale coefficients aren't normalised */
/*#define CHECK_SCALE_COEFFS*/
/*#define DEBUG_SCALE_COEFFS*/

#define THIS_STREAM (&pdpfb_vid_stream.stream)
#define THIS_PRIV (&pdpfb_vid_stream)

static struct pdpfb_vid_stream_priv {
	struct pdpfb_stream stream;
	/* horizontal scaler */
	int hdecimation;
	int hs_taps;
	int hscoeffs[33];
	int hs_oldsize, hs_oldres;
	/* vertical scaler */
	int vs_taps;
	int vscoeffs[17];
	int vs_oldsize, vs_oldres;
	/* pixel format info */
	struct pdpfb_vid_csc csc;
	int planar_override;
	int planar_ov_virtres[2];
	int planar_ov_pixfmt;
	struct pdpfb_vid_planar planar;
	int pixfmt;
	int nonstd2pixfmt[PDP_VID_PIXFMT_MAX];
	/* fields to update on vevent */
#ifdef PDP_SHARED_BASE
	spinlock_t base_addr_lock;
	unsigned int base_addr_y;
	unsigned int base_addr_u;
	unsigned int base_addr_v;
	unsigned int hskip;
	unsigned int vskip;
#endif
} pdpfb_vid_stream;

static struct pdpfb_vid_csc_coefs pdpfb_vid_csc_presets[] = {
	[PDP_VID_CSCPRESET_HDTV] = {
		.ry = 298,	.rv = 459,	.ru = 0,
		.gy = 298,	.gv = -137,	.gu = -55,
		.by = 298,	.bv = 0,	.bu = 541,
	},
	[PDP_VID_CSCPRESET_SDTV] = {
		.ry = 298,	.rv = 409,	.ru = 0,
		.gy = 298,	.gv = -208,	.gu = -100,
		.by = 298,	.bv = 0,	.bu = 517,
	},
	[PDP_VID_CSCPRESET_LEGACYHDTV] = {
		.ry = 298,	.rv = 459,	.ru = 0,
		.gy = 298,	.gv = -139,	.gu = -66,
		.by = 298,	.bv = 0,	.bu = 532,
	},
	[PDP_VID_CSCPRESET_LEGACYSDTV] = {
		.ry = 298,	.rv = 409,	.ru = 0,
		.gy = 298,	.gv = -207,	.gu = -97,
		.by = 298,	.bv = 0,	.bu = 519,
	},
};

/* fixed point arithmetic */

static inline int fix(int p, int a)
{
	return a << p;
}

static inline int fix_div(int p, int n, int d)
{
	return (n << p) / d;
}

static inline int fix_divl(int p, int n, int d)
{
	return ((u64)n << p) / d;
}

static inline int fix_mul(int p, int a, int b)
{
	return (a * b) >> p;
}

static inline int fix_sin(int p, int x)
{
	/* tailor expansion */
	int pow;
	int sum;

	/* in [-pi,pi] */
	int pi2 = fix_divl(p, 6283185, 1000000);
	int pi = pi2/2;
	x = x % pi2;
	if (x > pi)
		x -= pi2;
	else if (x < -pi)
		x += pi2;

	sum = x;

	/* x represents x^2 from now on */
	x = fix_mul(p, x, x);

	/* -x^3/3! */
	pow = fix_mul(p, sum, x);
	sum -= pow/6;
	/* +x^5/5! */
	pow = fix_mul(p, pow, x);
	sum += pow/120;
	/* -x^7/7! */
	pow = fix_mul(p, pow, x);
	sum -= pow/5040;
	/* +x^9/9! */
	pow = fix_mul(p, pow, x);
	sum += pow/362880;

	return sum;
}

static inline int fix_cos(int p, int x)
{
	/* tailor expansion */
	int pow;
	int sum = fix(p, 1);

	/* in [-pi,pi] */
	int pi2 = fix_divl(p, 6283185, 1000000);
	int pi = pi2/2;
	x = x % pi2;
	if (x > pi)
		x -= pi2;
	else if (x < -pi)
		x += pi2;

	/* x represents x^2 from now on */
	x = fix_mul(p, x, x);

	/* -x^2/2! */
	pow = x;
	sum -= pow/2;
	/* +x^4/4! */
	pow = fix_mul(p, pow, x);
	sum += pow/24;
	/* -x^6/6! */
	pow = fix_mul(p, pow, x);
	sum -= pow/720;
	/* +x^8/8! */
	pow = fix_mul(p, pow, x);
	sum += pow/40320;
	/* -x^10/10! */
	pow = fix_mul(p, pow, x);
	sum -= pow/3628800;

	return sum;
}

/* returns negative for invalid nonstd */
static int pdpfb_vid_nonstd_to_pixfmt(u32 nonstd)
{
	if (nonstd <= 0 || nonstd >= ARRAY_SIZE(THIS_PRIV->nonstd2pixfmt))
		return -1;
	return THIS_PRIV->nonstd2pixfmt[nonstd];
}

static int pdpfb_vid_scale_coeffs_stale(u32 new_res,  u32 old_res,
					u32 new_size, u32 old_size,
					int new_T,    int old_T,
					int new_I,    int old_I)
{
	/* has number of taps/interpolation points changed? */
	if (new_T != old_T || new_I != old_I)
		return 1;
	/* scale factor clamped to max of 1 */
	if (new_size >= new_res && old_size >= old_res)
		return 0;
	/* has scale factor changed? */
	return new_size * old_res != old_size * new_res;
}

static int pdpfb_vid_calc_scale_coeffs(u32 res, u32 size,
				       int T,	/* taps */
				       int I,	/* interpolation points */
				       int *coeffs, int count)
{
	int midpoint = T*I/2;
#define SCALE_PRECISION 8
#define RESULT_PRECISION 6
#define DIV(a, b)	fix_div(SCALE_PRECISION, a, b)
#define DIVL(a, b)	fix_divl(SCALE_PRECISION, a, b)
#define MUL(a, b)	fix_mul(SCALE_PRECISION, a, b)
#define SIN(a)		fix_sin(SCALE_PRECISION, a)
#define COS(a)		fix_cos(SCALE_PRECISION, a)
#define FIX(a)		fix(SCALE_PRECISION, a)
#define RESULT(a)	((a) >> (SCALE_PRECISION - RESULT_PRECISION))
	int fpi = DIVL(3141593, 1000000);
	int fS = min(FIX(1), /* scale factor clamped to 1 */
		DIV(size, res));
	int fA = DIV(54, 100);
	int c, i;

	int fpiS = MUL(fpi, fS);
	int f2piSoT = fpiS*2/T;
	int f1mA = FIX(1) - fA;
	int fT = FIX(T);
	int ftotal = 0;

	/* Calculate coefficients */
	for (c = 0; c <= midpoint; ++c) {
		/* t + i/I */
		int ftpioI = FIX(c)/I;
		int fx = MUL(fpiS, fT/2 - ftpioI);
		int fco = fA - MUL(f1mA, COS(MUL(f2piSoT, ftpioI)));
		if (fx > DIV(1, 10))
			fco = MUL(DIV(SIN(fx), fx), fco);
		ftotal += fco;
		coeffs[c] = fco;
	}

	/*
	 * Interpolation points I/2+1 to I-1 have same values as 1 to I/2-1,
	 * but in opposite order, so we only need to normalise the first half
	 * of the interpolation points taking mirroring into account.
	 */
	for (i = 0; i <= I/2; ++i) {
		int sum, err, dir = 0;

		/*
		 * Normalise coefficients in each interpolation point and
		 * convert into result format.
		 */
		sum = 0;
		for (c = i; c <= midpoint; c += I)
			sum += coeffs[c];
		for (; c < midpoint*2; c += I)
			sum += coeffs[midpoint*2 - c];
		/* Careful not to modify a coefficient twice */
		for (c = i; c <= midpoint; c += I)
			coeffs[c] = RESULT(DIV(coeffs[c], sum));
		if (i & (I/2 - 1))
			for (c = I-i; c <= midpoint; c += I)
				coeffs[c] = RESULT(DIV(coeffs[c], sum));

		/*
		 * Find fixed point error from normalisation.
		 */
		sum = 0;
		for (c = i; c <= midpoint; c += I)
			sum += coeffs[c];
		for (; c < midpoint*2; c += I)
			sum += coeffs[midpoint*2 - c];
		err = sum - (1 << RESULT_PRECISION);
		if (err > 0)
			dir = -1;
		else if (err < 0)
			dir = 1;
		else
			continue;

#ifdef DEBUG_SCALE_COEFFS
		printk(KERN_DEBUG "pdp: i=%d, err=%d\n", i, err);
#define SCALE_DEBUG(C, N) \
		printk(KERN_DEBUG "pdp:   adj*%d [%d] to 0x%x (err=%d)\n", \
		       (N), (C), coeffs[(C)], err)
#else
#define SCALE_DEBUG(C, N) do {} while (0)
#endif

		/*
		 * Distribute the error over the tap coefficients, preferring
		 * to change central coefficients.
		 */
		if (i == 0) {
			/*
			 * Special case: contains midpoint. Work out from
			 * center until no error or an odd value (which can be
			 * fixed by adjusting midpoint again).
			 */
			while (err > 1 || err < -1) {
				coeffs[midpoint] += dir;
				err += dir;
				SCALE_DEBUG(midpoint, 1);
				for (c = midpoint - I;
				     (err > 1 || err < -1) && c >= I;
				     c -= I) {
					coeffs[c] += dir;
					err += dir*2;
					SCALE_DEBUG(c, 2);
				}
			}
			if (err & 1) {
				coeffs[midpoint] += dir;
				err += dir;
				SCALE_DEBUG(midpoint, 1);
			}
		} else if (i == I/2) {
			/*
			 * Special case: tap coeffs mirror. This also means err
			 * is always even with this interpolation point.
			 */
			int last = i + I*(T/2-1);
#ifdef CHECK_SCALE_COEFFS
			WARN(err & 1, "err (=%d) should be even for i=%d\n",
					err, i);
#endif
			while (err) {
				for (c = last; err && c >= 0; c -= I) {
					coeffs[c] += dir;
					err += dir*2;
					SCALE_DEBUG(c, 2);
				}
			}
		} else {
			/*
			 * No tap coefficient mirrors another. Work out from
			 * center adjusting values.
			 */
			int last = midpoint - i;
			int offset = i*2 - I;
			while (err) {
				for (c = last; err && c >= 0; c -= I) {
					coeffs[c] += dir;
					err += dir;
					SCALE_DEBUG(c, 1);
					if (!err)
						break;
					coeffs[c+offset] += dir;
					err += dir;
					SCALE_DEBUG(c+offset, 1);
				}
			}
		}
#ifdef CHECK_SCALE_COEFFS
		sum = 0;
		for (c = i; c <= midpoint; c += I)
			sum += coeffs[c];
		for (; c < midpoint*2; c += I)
			sum += coeffs[midpoint*2 - c];
		err = sum - (1 << RESULT_PRECISION);
		WARN_ONCE(err, "Scale coefficients not normalised"
			       " (i=%d, err=%d)\n", i, err);
#endif
	}

	/* reflect about midpoint in spare coeffs */
	if (count > midpoint*2)
		count = midpoint*2;
	for (c = midpoint+1; c < count; ++c)
		coeffs[c] = coeffs[2*midpoint - c];

	return 0;
#undef DIV
#undef MUL
#undef SIN
#undef COS
#undef FIX
#undef RESULT
}

static int pdpfb_vid_calc_hscale(struct pdpfb_priv *priv)
{
	u32 geomw = THIS_STREAM->geom.w;
	if (!geomw)
		geomw = THIS_STREAM->info.var.xres;

	/* don't recalculate coefficients unless something has changed */
	if (!pdpfb_vid_scale_coeffs_stale(THIS_STREAM->info.var.xres,
					  THIS_PRIV->hs_oldres,
					  geomw,
					  THIS_PRIV->hs_oldsize,
					  8, THIS_PRIV->hs_taps,
					  8, 8))
		return 0;

	THIS_PRIV->hs_taps = 8;
	THIS_PRIV->hs_oldres = THIS_STREAM->info.var.xres;
	THIS_PRIV->hs_oldsize = geomw;
	return pdpfb_vid_calc_scale_coeffs(THIS_STREAM->info.var.xres,
					geomw,
					THIS_PRIV->hs_taps,
					8,
					THIS_PRIV->hscoeffs,
					ARRAY_SIZE(THIS_PRIV->hscoeffs));
}

#ifdef PDP_VID_VSCALE
static int pdpfb_vid_calc_vscale(struct pdpfb_priv *priv)
{
	struct pdp_info *pdata = pdpfb_get_platform_data(priv);
	int vs_taps;

	/* 2-tap (bilinear) filtering when scaling down beyond threshold */
	if ((u32)THIS_STREAM->geom.h * pdata->vpitch_bilinear_threshold
	    < ((u32)THIS_STREAM->info.var.yres << PDPFB_PDATA_FIX_SHIFT))
		vs_taps = 2;
	else
		vs_taps = 4;

	/* don't recalculate coefficients unless something has changed */
	if (!pdpfb_vid_scale_coeffs_stale(THIS_STREAM->info.var.yres,
						THIS_PRIV->vs_oldres,
					  THIS_STREAM->geom.h,
						THIS_PRIV->vs_oldsize,
					  vs_taps, THIS_PRIV->vs_taps,
					  8, 8))
		return 0;

	THIS_PRIV->vs_taps = vs_taps;
	THIS_PRIV->vs_oldres = THIS_STREAM->info.var.yres;
	THIS_PRIV->vs_oldsize = THIS_STREAM->geom.h;
	return pdpfb_vid_calc_scale_coeffs(THIS_STREAM->info.var.yres,
					THIS_STREAM->geom.h,
					vs_taps,
					8,
					THIS_PRIV->vscoeffs,
					ARRAY_SIZE(THIS_PRIV->vscoeffs));
}
#else
static inline int pdpfb_vid_calc_vscale(struct pdpfb_priv *priv)
{
	return 0;
}
#endif

static int pdpfb_vid_set_bpp(struct fb_var_screeninfo *var)
{
	if (pdpfb_vid_nonstd_to_pixfmt(var->nonstd) < 0)
		var->nonstd = 0;

	switch (var->nonstd) {
	case PDP_VID_PIXFMT_420_PL8:
	case PDP_VID_PIXFMT_420_PL8IVU:
	case PDP_VID_PIXFMT_420_PL8IUV:
		var->bits_per_pixel = 12;
		break;
	case PDP_VID_PIXFMT_422_UY0VY1_8888:
	case PDP_VID_PIXFMT_422_VY0UY1_8888:
	case PDP_VID_PIXFMT_422_Y0UY1V_8888:
	case PDP_VID_PIXFMT_422_Y0VY1U_8888:
		var->bits_per_pixel = 16;
		break;
	default:
		var->bits_per_pixel = 16;
		var->nonstd = PDP_VID_PIXFMT_422_UY0VY1_8888;
		break;
	}

	var->red.offset = 0;
	var->red.length = 0;
	var->green.offset = 0;
	var->green.length = 0;
	var->blue.offset = 0;
	var->blue.length = 0;
	var->transp.offset = 0;
	var->transp.length = 0;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	return 0;
}

static int pdpfb_vid_check_geom(struct pdpfb_priv *priv,
				struct pdpfb_geom *geom)
{
	struct fb_info *info = &THIS_STREAM->info;

	if (!geom->w) {
		THIS_PRIV->hdecimation = 0;
	} else if (geom->w == info->var.xres) {
		geom->w = 0;
	} else if (geom->w > info->var.xres*8) {
		THIS_PRIV->hdecimation = 0;
		geom->w = info->var.xres*8;
	} else if (geom->w*4 < info->var.xres) {
		THIS_PRIV->hdecimation = 1;
		if (geom->w*8 < info->var.xres)
			geom->w = (info->var.xres+7)/8;
#if PDP_REV < 0x010001
		++geom->w; /* round up */
#endif
	} else {
		THIS_PRIV->hdecimation = 0;
	}
#if PDP_REV < 0x010001
	/* geom->w must be even because it's in YUV */
	geom->w &= ~0x1;
	/* pan granularity depends on horizontal decimation */
	if (THIS_PRIV->hdecimation)
		info->fix.xpanstep = 4;
	else
		info->fix.xpanstep = 2;
#else
	info->fix.xpanstep = 1;
#endif

	/* avoid HSKIP problems by restricting pan step */
	if (info->var.nonstd == PDP_VID_PIXFMT_420_PL8IVU ||
	    info->var.nonstd == PDP_VID_PIXFMT_420_PL8IUV)
		info->fix.xpanstep = 16;

	if (geom->h) {
#ifndef PDP_VID_VSCALE
		/* only doubling and halving */
		if (geom->h*4 <= info->var.yres*3)
			geom->h = info->var.yres / 2;
		else if (geom->h*2 >= info->var.yres*3)
			geom->h = info->var.yres * 2;
		else
			geom->h = 0;
#else
		if (geom->h == info->var.yres)
			geom->h = 0;
		else if (geom->h > info->var.yres*8)
			geom->h = info->var.yres*8;
		else if (geom->h*8 < info->var.yres)
			geom->h = (info->var.yres+7)/8;
#endif
	}
	return 0;
}

static int pdpfb_vid_set_geom(struct pdpfb_priv *priv)
{
	struct pdpfb_geom *geom = &THIS_STREAM->geom;

	pdpfb_vid_calc_hscale(priv);
	if (geom->h)
		pdpfb_vid_calc_vscale(priv);
	return 0;
}

static unsigned long pdpfb_vid_required_mem(struct fb_var_screeninfo *var)
{
	switch (var->nonstd) {
	case PDP_VID_PIXFMT_420_PL8:
	case PDP_VID_PIXFMT_420_PL8IVU:
	case PDP_VID_PIXFMT_420_PL8IUV:
		return 3 * pdpfb_get_line_length(var->xres_virtual/2, 8)
			* var->yres_virtual;
	default:
		return pdpfb_get_line_length(var->xres_virtual,
						var->bits_per_pixel)
			* var->yres_virtual;
	};
}

static int pdpfb_vid_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;

#if PDP_REV < 0x010001
	/* xres must be even, round up */
	var->xres = (var->xres + 1) & ~0x1;
#endif

	if (var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual)
		var->yres_virtual = var->yres;

	if (var->xres_virtual < var->xoffset + var->xres)
		var->xres_virtual = var->xoffset + var->xres;
	if (var->yres_virtual < var->yoffset + var->yres)
		var->yres_virtual = var->yoffset + var->yres;

	pdpfb_vid_set_bpp(var);

	/* Memory limit */
	if (pdpfb_vid_required_mem(var) > THIS_STREAM->videomem_len)
		return -ENOMEM;

	return 0;
}

static int pdpfb_vid_configure_csc(struct pdpfb_priv *priv)
{
	struct pdpfb_vid_csc_coefs *coefs = &THIS_PRIV->csc.coefs;

	pdpfb_write(priv, PDP_CSCCOEFF0,
		PLACE_FIELD(PDP_CSCCOEFF0_RU, coefs->ru) |
		PLACE_FIELD(PDP_CSCCOEFF0_RY, coefs->ry));
	pdpfb_write(priv, PDP_CSCCOEFF1,
		PLACE_FIELD(PDP_CSCCOEFF1_GY, coefs->gy) |
		PLACE_FIELD(PDP_CSCCOEFF1_RV, coefs->rv));
	pdpfb_write(priv, PDP_CSCCOEFF2,
		PLACE_FIELD(PDP_CSCCOEFF2_GV, coefs->gv) |
		PLACE_FIELD(PDP_CSCCOEFF2_GU, coefs->gu));
	pdpfb_write(priv, PDP_CSCCOEFF3,
		PLACE_FIELD(PDP_CSCCOEFF3_BU, coefs->bu) |
		PLACE_FIELD(PDP_CSCCOEFF3_BY, coefs->by));
	pdpfb_write(priv, PDP_CSCCOEFF4,
		PLACE_FIELD(PDP_CSCCOEFF4_BV, coefs->bv));

	return 0;
}

static int pdpfb_vid_check_csc(struct pdpfb_vid_csc_coefs *csc)
{
#define CSC_COEF_INVALID(x) (((x) >= (1<<10)) || ((x) < (-1<<10)))
	return	CSC_COEF_INVALID(csc->ry) ||
		CSC_COEF_INVALID(csc->rv) ||
		CSC_COEF_INVALID(csc->ru) ||
		CSC_COEF_INVALID(csc->gy) ||
		CSC_COEF_INVALID(csc->gv) ||
		CSC_COEF_INVALID(csc->gu) ||
		CSC_COEF_INVALID(csc->by) ||
		CSC_COEF_INVALID(csc->bv) ||
		CSC_COEF_INVALID(csc->bu);
#undef CSC_COEF_INVALID
}

static int pdpfb_vid_check_planar(struct fb_info *info,
				  struct pdpfb_vid_planar *pl)
{
	int extra;

	/* check alignment */
	if (unlikely(pl->y_offset & PDP_YADDR_ALIGNMASK)) {
		pl->y_offset &= ~PDP_YADDR_ALIGNMASK;
		return -EINVAL;
	}
	if (unlikely(pl->u_offset & PDP_UADDR_ALIGNMASK)) {
		pl->u_offset &= ~PDP_UADDR_ALIGNMASK;
		return -EINVAL;
	}
	if (unlikely(pl->v_offset & PDP_VADDR_ALIGNMASK)) {
		pl->v_offset &= ~PDP_VADDR_ALIGNMASK;
		return -EINVAL;
	}
	if (unlikely(pl->y_line_length & PDP_YSTRIDE_ALIGNMASK)) {
		/* round up */
		pl->y_line_length = pdpfb_get_line_length(pl->y_line_length, 8);
		return -EINVAL;
	}
	/* u/v strides must be the same */
	if (unlikely(pl->u_line_length != pl->v_line_length)) {
		pl->v_line_length = pl->u_line_length;
		return -EINVAL;
	}
	/* strides must be in range */
	if (unlikely(pl->y_line_length > PDP_YSTRIDE_MAX)) {
		pl->y_line_length = PDP_YSTRIDE_MAX;
		return -EINVAL;
	}
	/* u/v strides must be half or equal to y stride */
	if (unlikely(pl->u_line_length != pl->y_line_length &&
		     pl->u_line_length*2 != pl->y_line_length)) {
		if (pl->u_line_length < pl->y_line_length)
			pl->u_line_length = pl->y_line_length/2;
		else
			pl->u_line_length = pl->y_line_length;
		pl->v_line_length = pl->u_line_length;
		return -EINVAL;
	}

	/* offsets must be in range (don't want to overflow later) */
	if (unlikely(pl->y_offset > PDP_YADDR_MAX ||
		     pl->y_offset >= THIS_STREAM->videomem_len)) {
		pl->y_offset = 0;
		return -ENOMEM;
	}
	if (unlikely(pl->u_offset > PDP_UADDR_MAX ||
		     pl->u_offset >= THIS_STREAM->videomem_len)) {
		pl->u_offset = 0;
		return -ENOMEM;
	}
	if (unlikely(pl->v_offset > PDP_VADDR_MAX ||
		     pl->v_offset >= THIS_STREAM->videomem_len)) {
		pl->v_offset = 0;
		return -ENOMEM;
	}
	/* does y plane fit in memory? */
	extra = info->var.xres_virtual - pl->y_line_length;
	if (extra < 0)
		extra = 0;
	if (pl->y_offset + pl->y_line_length*info->var.yres_virtual + extra
						> THIS_STREAM->videomem_len) {
		pl->y_offset = 0;
		return -ENOMEM;
	}
	/* does u plane fit in memory? */
	if (info->var.nonstd == PDP_VID_PIXFMT_420_PL8)
		extra = info->var.xres_virtual/2 - pl->u_line_length;
	else /* byte interleaved chroma formats */
		extra = info->var.xres_virtual - pl->u_line_length;
	if (extra < 0)
		extra = 0;
	if (pl->u_offset + pl->u_line_length*info->var.yres_virtual/2 + extra
						> THIS_STREAM->videomem_len) {
		pl->u_offset = 0;
		return -ENOMEM;
	}
	/* does v plane fit in memory? (same extra as u plane) */
	if (pl->v_offset + pl->v_line_length*info->var.yres_virtual/2 + extra
						> THIS_STREAM->videomem_len) {
		pl->v_offset = 0;
		return -ENOMEM;
	}

	return 0;
}

static void pdpfb_vid_update_planar(struct pdpfb_priv *priv,
				    struct fb_info *info)
{
	int uv_planar = 1;
	/* handle planar YUV */
	switch (info->var.nonstd) {
	case PDP_VID_PIXFMT_420_PL8IVU:
	case PDP_VID_PIXFMT_420_PL8IUV:
		uv_planar = 0;
		/* fall through */
	case PDP_VID_PIXFMT_420_PL8:
	/* The following 2 pixel formats are unimplemented */
	case PDP_VID_PIXFMT_420_T88CP:
	case PDP_VID_PIXFMT_422_T88CP:
		if (THIS_PRIV->planar_override) {
			/* if overridden planar setup is still ok, leave it */
			if (THIS_PRIV->planar_ov_virtres[0]
					== info->var.xres_virtual &&
			    THIS_PRIV->planar_ov_virtres[1]
					== info->var.yres_virtual &&
			    THIS_PRIV->planar_ov_pixfmt == info->var.nonstd)
				break;
		}

		THIS_PRIV->planar_override = 0;
		THIS_PRIV->planar.y_line_length
			= pdpfb_get_line_length(info->var.xres_virtual, 8);
		THIS_PRIV->planar.u_line_length
			= THIS_PRIV->planar.v_line_length
			= info->var.xres_virtual >> uv_planar;
		THIS_PRIV->planar.y_offset = 0;
		THIS_PRIV->planar.u_offset = THIS_PRIV->planar.y_offset
			+ THIS_PRIV->planar.y_line_length
				* info->var.yres_virtual;
		if (uv_planar)
			THIS_PRIV->planar.v_offset = THIS_PRIV->planar.u_offset
				+ THIS_PRIV->planar.u_line_length
					* info->var.yres_virtual/2;
		else
			THIS_PRIV->planar.v_offset = THIS_PRIV->planar.u_offset;
		break;

	case PDP_VID_PIXFMT_422_UY0VY1_8888:
	case PDP_VID_PIXFMT_422_VY0UY1_8888:
	case PDP_VID_PIXFMT_422_Y0UY1V_8888:
	case PDP_VID_PIXFMT_422_Y0VY1U_8888:
	default:
		THIS_PRIV->planar_override = 0;
		THIS_PRIV->planar.y_line_length
			= THIS_PRIV->planar.u_line_length
			= THIS_PRIV->planar.v_line_length
			= pdpfb_get_line_length(info->var.xres_virtual,
						info->var.bits_per_pixel);
		THIS_PRIV->planar.y_offset = 0;
		THIS_PRIV->planar.u_offset = 0;
		THIS_PRIV->planar.v_offset = 0;
		break;
	}
}

static int pdpfb_vid_change_mode(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	u32 str2posn;

	/* handle planar YUV */
	switch (info->var.nonstd) {
	case PDP_VID_PIXFMT_420_PL8:
	case PDP_VID_PIXFMT_420_PL8IVU:
	case PDP_VID_PIXFMT_420_PL8IUV:
	/* The following 2 pixel formats are unimplemented */
	case PDP_VID_PIXFMT_420_T88CP:
	case PDP_VID_PIXFMT_422_T88CP:
		info->fix.type = FB_TYPE_PLANES;
		break;

	case PDP_VID_PIXFMT_422_UY0VY1_8888:
	case PDP_VID_PIXFMT_422_VY0UY1_8888:
	case PDP_VID_PIXFMT_422_Y0UY1V_8888:
	case PDP_VID_PIXFMT_422_Y0VY1U_8888:
	default:
		info->fix.type = FB_TYPE_PACKED_PIXELS;
		break;
	}
	info->fix.line_length = THIS_PRIV->planar.y_line_length;
	str2posn = pdpfb_read(priv, PDP_STR2POSN);
	SET_FIELD(str2posn, PDP_STRXPOSN_SRCSTRIDE,
			(info->fix.line_length >> PDP_YSTRIDE_ALIGN) - 1);
	pdpfb_write(priv, PDP_STR2POSN, str2posn);

	return 0;
}

static int pdpfb_vid_configure_hscale(struct pdpfb_priv *priv)
{
	u32 i;
	u32 scale_ctrl, hsinit, scale_size;
	u32 xres = THIS_STREAM->info.var.xres;
	u32 geomw = THIS_STREAM->geom.w;
	u32 hpitch;
	int en = (geomw && geomw != xres);

#if PDP_REV >= 0x010001
	/* we enable the scalar even with 1:1 to get fine control over HSKIP */
	en = 1;
#endif

	scale_ctrl = pdpfb_read(priv, PDP_SCALECTRL);
	SET_FIELD(scale_ctrl, PDP_SCALECTRL_HSCALEBP, !en);
	pdpfb_write(priv, PDP_SCALECTRL, scale_ctrl);

	if (!geomw)
		geomw = xres;
	scale_size = pdpfb_read(priv, PDP_SCALESIZE);
	SET_FIELD(scale_size, PDP_SCALESIZE_SCALEDWIDTH, geomw - 1);
	pdpfb_write(priv, PDP_SCALESIZE, scale_size);

	if (!en)
		return 0;

	hpitch = (fix_div(12, xres, geomw) + 1) >> 1;
	if (THIS_PRIV->hdecimation)
		hpitch /= 2;
	hsinit = pdpfb_read(priv, PDP_HSINIT);
	SET_FIELD(hsinit, PDP_HSINIT_HINITIAL,
			fix_div(11, THIS_PRIV->hs_taps, 2));
	SET_FIELD(hsinit, PDP_HSINIT_HDECIM, THIS_PRIV->hdecimation);
	SET_FIELD(hsinit, PDP_HSINIT_HPITCH, hpitch);
	pdpfb_write(priv, PDP_HSINIT, hsinit);

	for (i = 0; i < 8; ++i) {
		u32 val =  (0xFF & THIS_PRIV->hscoeffs[i<<2])
			| ((0xFF & THIS_PRIV->hscoeffs[(i<<2) + 1]) << 8)
			| ((0xFF & THIS_PRIV->hscoeffs[(i<<2) + 2]) << 16)
			| ((0xFF & THIS_PRIV->hscoeffs[(i<<2) + 3]) << 24);
		pdpfb_write(priv, PDP_HSCOEFF0 + (i<<2), val);
	}
#if PDP_REV < 0x010001
	/* the odd one out, index 32 goes in most significant byte */
	pdpfb_write(priv, PDP_HSCOEFF8,
			(0xFF & THIS_PRIV->hscoeffs[i<<2]) << 24);
#else
	pdpfb_write(priv, PDP_HSCOEFF8, 0xFF & THIS_PRIV->hscoeffs[i<<2]);
#endif
	return 0;
}

#ifndef PDP_VID_VSCALE
static int pdpfb_vid_configure_vscale(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	struct pdpfb_geom *geom = &THIS_STREAM->geom;

	u32 blend2;
	u32 line_double = 0;
	u32 line_halve = 0;

	if (geom->h) {
		line_double = (geom->h > info->var.yres);
		line_halve = (geom->h < info->var.yres);
	}

	blend2 = pdpfb_read(priv, THIS_STREAM->regs.blend2);
	SET_FIELD(blend2, PDP_STRXBLEND2_LINEDOUBLE, line_double);
	SET_FIELD(blend2, PDP_STRXBLEND2_LINEHALVE, line_halve);
	pdpfb_write(priv, THIS_STREAM->regs.blend2, blend2);

	return 0;
}

#else
static int pdpfb_vid_configure_vscale(struct pdpfb_priv *priv)
{
	struct pdp_info *pdata = pdpfb_get_platform_data(priv);
	u32 i;
	u32 scale_ctrl, scale_size, vsinit;
	u32 yres = THIS_STREAM->info.var.yres;
	u32 geomh = THIS_STREAM->geom.h;
	u32 vpitch = fix_div(11, yres, geomh);
	int en = (geomh && geomh != yres);
	int hs_before_vs = (THIS_STREAM->info.var.yres > pdata->linestore_len);

	scale_ctrl = pdpfb_read(priv, PDP_SCALECTRL);
	SET_FIELD(scale_ctrl, PDP_SCALECTRL_VSCALEBP, !en);
	SET_FIELD(scale_ctrl, PDP_SCALECTRL_HSBEFOREVS, hs_before_vs);
	SET_FIELD(scale_ctrl, PDP_SCALECTRL_VSURUNCTRL, 1);
	SET_FIELD(scale_ctrl, PDP_SCALECTRL_VORDER, THIS_PRIV->vs_taps-1);
	SET_FIELD(scale_ctrl, PDP_SCALECTRL_VPITCH, vpitch);
	pdpfb_write(priv, PDP_SCALECTRL, scale_ctrl);

	if (!geomh)
		geomh = yres;
	scale_size = pdpfb_read(priv, PDP_SCALESIZE);
	SET_FIELD(scale_size, PDP_SCALESIZE_SCALEDHEIGHT, geomh - 1);
	pdpfb_write(priv, PDP_SCALESIZE, scale_size);

	if (!en)
		return 0;

	vsinit = pdpfb_read(priv, PDP_VSINIT);
	SET_FIELD(vsinit, PDP_VSINIT_INITIAL1,
			fix_div(11, THIS_PRIV->vs_taps, 2));
	pdpfb_write(priv, PDP_VSINIT, vsinit);

	for (i = 0; i < 4; ++i) {
		u32 val =  (0xFF & THIS_PRIV->vscoeffs[i<<2])
			| ((0xFF & THIS_PRIV->vscoeffs[(i<<2) + 1]) << 8)
			| ((0xFF & THIS_PRIV->vscoeffs[(i<<2) + 2]) << 16)
			| ((0xFF & THIS_PRIV->vscoeffs[(i<<2) + 3]) << 24);
		pdpfb_write(priv, PDP_VSCOEFF0 + (i<<2), val);
	}
	pdpfb_write(priv, PDP_VSCOEFF4, 0xFF & THIS_PRIV->vscoeffs[i<<2]);
	return 0;
}
#endif

#ifdef PDP_SHARED_BASE
static void pdpfb_vid_apply_addr(void *arg, u32 mask)
{
	struct pdpfb_priv *priv = (struct pdpfb_priv *)arg;
	u32 tmp;

	spin_lock(&THIS_PRIV->base_addr_lock);

	tmp = pdpfb_read(priv, PDP_STR2CTRL);
	SET_FIELD(tmp, PDP_STRXCTRL_BASEADDR,
				THIS_PRIV->base_addr_y >> PDP_YADDR_ALIGN);
	pdpfb_write(priv, PDP_STR2CTRL, tmp);

	tmp = pdpfb_read(priv, PDP_STR2UADDR);
	SET_FIELD(tmp, PDP_STR2UADDR_UBASEADDR,
				THIS_PRIV->base_addr_u >> PDP_UADDR_ALIGN);
	pdpfb_write(priv, PDP_STR2UADDR, tmp);

	tmp = pdpfb_read(priv, PDP_STR2VADDR);
	SET_FIELD(tmp, PDP_STR2VADDR_VBASEADDR,
				THIS_PRIV->base_addr_v >> PDP_VADDR_ALIGN);
	pdpfb_write(priv, PDP_STR2VADDR, tmp);

	pdpfb_unregister_isr(pdpfb_vid_apply_addr, priv,
			     PDPFB_IRQ_VEVENT0);

	tmp = pdpfb_read(priv, PDP_SKIPCTRL);
	SET_FIELD(tmp, PDP_SKIPCTRL_HSKIP, THIS_PRIV->hskip);
#if PDP_REV >= 0x010001
	SET_FIELD(tmp, PDP_SKIPCTRL_VSKIP, THIS_PRIV->vskip);
#endif
	pdpfb_write(priv, PDP_SKIPCTRL, tmp);

	spin_unlock(&THIS_PRIV->base_addr_lock);
}
#endif

static int pdpfb_vid_configure_addr(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	u32 ybuf_offset, ybuf_start;
	u32 ubuf_offset, ubuf_start;
	u32 vbuf_offset, vbuf_start;
	u32 hskip, xoff, vskip, yoff;
	u32 str_ctrl;
	u32 uv_half_stride = THIS_PRIV->planar.u_line_length
				< THIS_PRIV->planar.y_line_length;
#ifdef PDP_SHARED_BASE
	u32 u_addr;
	unsigned long flags;
#else
	u32 skip_ctrl;
#endif

	str_ctrl = pdpfb_read(priv, PDP_STR2CTRL);
#ifndef PDP_SHARED_BASE
	SET_FIELD(str_ctrl, PDP_STR2CTRL_UVHALFSTR, uv_half_stride);
#endif
	pdpfb_write(priv, PDP_STR2CTRL, str_ctrl);

	switch (info->var.nonstd) {
	case PDP_VID_PIXFMT_420_PL8:
		hskip = info->var.xoffset & 0x1f;
		xoff = info->var.xoffset - hskip;
		vskip = info->var.yoffset & 0x1;
		yoff = info->var.yoffset - vskip;

		ybuf_offset = xoff;
		ubuf_offset = xoff >> 1;
		vbuf_offset = ubuf_offset;

		ybuf_offset += THIS_PRIV->planar.y_line_length * yoff ;
		ubuf_offset += THIS_PRIV->planar.u_line_length * (yoff >> 1);
		vbuf_offset += THIS_PRIV->planar.v_line_length * (yoff >> 1);
		break;
	case PDP_VID_PIXFMT_420_PL8IVU:
	case PDP_VID_PIXFMT_420_PL8IUV:
		hskip = info->var.xoffset & 0xf;
		xoff = info->var.xoffset - hskip;
		vskip = info->var.yoffset & 0x1;
		yoff = info->var.yoffset - vskip;

		ybuf_offset = xoff + THIS_PRIV->planar.y_line_length * yoff;
		ubuf_offset = xoff +
				THIS_PRIV->planar.y_line_length * (yoff >> 1);
		vbuf_offset = ubuf_offset;
		break;
	default:
		xoff = info->var.xoffset * info->var.bits_per_pixel / 8;
		hskip = (xoff & 0xf) * 8 / info->var.bits_per_pixel;
		vskip = 0;
		yoff = info->var.yoffset;

		ybuf_offset = xoff + info->fix.line_length * yoff;
		ubuf_offset = ybuf_offset;
		vbuf_offset = ybuf_offset;
		break;
	}
	ybuf_start = THIS_PRIV->planar.y_offset + ybuf_offset;
	ubuf_start = THIS_PRIV->planar.u_offset + ubuf_offset;
	vbuf_start = THIS_PRIV->planar.v_offset + vbuf_offset;

#if PDP_REV < 0x010001
	if (THIS_PRIV->hdecimation)
		hskip &= ~0x3;
	else
		hskip &= ~0x1;
#endif

#ifndef PDP_SHARED_BASE
	ybuf_start += info->fix.smem_start;
	ubuf_start += info->fix.smem_start;
	vbuf_start += info->fix.smem_start;
	pdpfb_write(priv, PDP_STR2ADDR, ybuf_start >> PDP_YADDR_ALIGN);
	pdpfb_write(priv, PDP_STR2UADDR, ubuf_start >> PDP_UADDR_ALIGN);
	pdpfb_write(priv, PDP_STR2VADDR, vbuf_start >> PDP_VADDR_ALIGN);

	skip_ctrl = pdpfb_read(priv, PDP_SKIPCTRL);
	SET_FIELD(skip_ctrl, PDP_SKIPCTRL_HSKIP, hskip);
#if PDP_REV >= 0x010001
	SET_FIELD(skip_ctrl, PDP_SKIPCTRL_VSKIP, vskip);
#endif
	pdpfb_write(priv, PDP_SKIPCTRL, skip_ctrl);

#else
	ybuf_start += THIS_STREAM->videomem_offset;
	ubuf_start += THIS_STREAM->videomem_offset;
	vbuf_start += THIS_STREAM->videomem_offset;

	spin_lock_irqsave(&THIS_PRIV->base_addr_lock, flags);

	u_addr = pdpfb_read(priv, PDP_STR2UADDR);
	SET_FIELD(u_addr, PDP_STR2UADDR_UVHALFSTR, uv_half_stride);
	pdpfb_write(priv, PDP_STR2UADDR, u_addr);

	THIS_PRIV->base_addr_y = ybuf_start;
	THIS_PRIV->base_addr_u = ubuf_start;
	THIS_PRIV->base_addr_v = vbuf_start;
	THIS_PRIV->hskip = hskip;
	THIS_PRIV->vskip = vskip;
	pdpfb_register_isr(pdpfb_vid_apply_addr, priv,
			   PDPFB_IRQ_VEVENT0);

	spin_unlock_irqrestore(&THIS_PRIV->base_addr_lock, flags);
#endif

	return 0;
}

static int pdpfb_vid_configure(struct pdpfb_priv *priv)
{
	struct fb_info *info = &THIS_STREAM->info;
	u32 str2surf;

	str2surf = pdpfb_read(priv, PDP_STR2SURF);
	SET_FIELD(str2surf, PDP_STRXSURF_WIDTH, info->var.xres - 1);
	SET_FIELD(str2surf, PDP_STRXSURF_HEIGHT, info->var.yres - 1);
	SET_FIELD(str2surf, PDP_STR2SURF_PIXFMT, THIS_PRIV->pixfmt);
	SET_FIELD(str2surf, PDP_STR2SURF_USECSC, THIS_PRIV->csc.enable);
	SET_FIELD(str2surf, PDP_STR2SURF_COSITED, THIS_PRIV->csc.cosited);
	pdpfb_write(priv, PDP_STR2SURF, str2surf);

	pdpfb_vid_change_mode(priv);

	pdpfb_vid_configure_hscale(priv);
	pdpfb_vid_configure_vscale(priv);

	return 0;
}

static int pdpfb_vid_ioctl(struct fb_info *info, unsigned int cmd,
				unsigned long arg)
{
	struct pdpfb_priv *priv = dev_get_drvdata(info->device);
	void __user *argp = (void __user *)arg;
	struct pdpfb_vid_csc csc;
	struct pdpfb_vid_planar pl;
	int err;

	switch (cmd) {
	/* get colour space conversion settings */
	case PDPIO_GETCSC:
		if (copy_to_user(argp, &THIS_PRIV->csc,
				sizeof(THIS_PRIV->csc)))
			return -EFAULT;
		return 0;
	/* set colour space conversion settings */
	case PDPIO_SETCSC:
		if (copy_from_user(&csc, argp, sizeof(csc)))
			return -EFAULT;
		csc.enable = (csc.enable != 0);
		csc.cosited = (csc.cosited != 0);
		if (csc.preset) {
			if (csc.preset < ARRAY_SIZE(pdpfb_vid_csc_presets))
				csc.coefs = pdpfb_vid_csc_presets[csc.preset];
			else
				return -EINVAL;
		} else if (pdpfb_vid_check_csc(&csc.coefs))
			return -EINVAL;
		THIS_PRIV->csc = csc;
		pdpfb_vid_configure_csc(priv);
		if (copy_to_user(argp, &THIS_PRIV->csc,
				sizeof(THIS_PRIV->csc)))
			return -EFAULT;
		return 0;
	/* get planar YUV information */
	case PDPIO_GETPLANAR:
		if (copy_to_user(argp, &THIS_PRIV->planar,
				sizeof(THIS_PRIV->planar)))
			return -EFAULT;
		return 0;
	/* set planar YUV information */
	case PDPIO_SETPLANAR:
		if (copy_from_user(&pl, argp, sizeof(pl)))
			return -EFAULT;
		/* pixel format must be planar */
		if (unlikely(info->fix.type != FB_TYPE_PLANES))
			return -EINVAL;

		err = pdpfb_vid_check_planar(info, &pl);
		if (copy_to_user(argp, &pl, sizeof(pl)))
			return -EFAULT;
		if (err < 0)
			return err;

		/* override the planar setup */
		THIS_PRIV->planar = pl;
		THIS_PRIV->planar_override = 1;
		THIS_PRIV->planar_ov_virtres[0] = info->var.xres_virtual;
		THIS_PRIV->planar_ov_virtres[1] = info->var.yres_virtual;
		THIS_PRIV->planar_ov_pixfmt = info->var.nonstd;

		/* start using the new setup */
		pdpfb_vid_update_planar(priv, info);
		pdpfb_vid_change_mode(priv);
		pdpfb_vid_configure_addr(priv);
		return 0;
	default:
		return pdpfb_str_ioctl(priv, THIS_STREAM, cmd, arg);
	}
	return 0;
}

static int pdpfb_vid_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct pdpfb_priv *priv;

	if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	    var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	priv = dev_get_drvdata(info->device);
	pdpfb_vid_configure_addr(priv);

	return 0;
}

static int pdpfb_vid_set_par(struct fb_info *info)
{
	struct pdpfb_priv *priv = dev_get_drvdata(info->device);

	/* resolution may have changed, so scaling may need alteration */
	pdpfb_vid_check_geom(priv, &THIS_STREAM->geom);
	pdpfb_vid_set_geom(priv);

	/* pixel format maps from nonstd, so may need updating */
	THIS_PRIV->pixfmt = pdpfb_vid_nonstd_to_pixfmt(info->var.nonstd);

	pdpfb_vid_update_planar(priv, info);
	pdpfb_vid_configure(priv);
	pdpfb_vid_configure_addr(priv);
	pdpfb_update_margins(priv, THIS_STREAM);

	return 0;
}

static struct fb_fix_screeninfo pdpfb_vid_fix  = {
	.id =		"pdp_vid",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
#if PDP_REV < 0x010001
	.xpanstep =	2,
	.ypanstep =	2,
#else
	.xpanstep =	1,
	.ypanstep =	1,
#endif
	.accel =	FB_ACCEL_IMG_PDP_1,
};

static struct fb_ops pdpfb_vid_ops = {
	.fb_setcolreg	= pdpfb_setcolreg,
	.fb_blank	= pdpfb_blank,
	.fb_pan_display	= pdpfb_vid_pan_display,
	.fb_check_var	= pdpfb_vid_check_var,
	.fb_set_par	= pdpfb_vid_set_par,
	.fb_ioctl	= pdpfb_vid_ioctl,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static void pdpfb_vid_probe_caps(struct pdpfb_priv *priv)
{
	int *m = THIS_PRIV->nonstd2pixfmt;

#ifdef PDP_GAMMA
	THIS_STREAM->caps.gamma = PDP_GAMMA;
#endif

	/* pixfmt values vary between revisions */
	m[0] = -1;
	m[PDP_VID_PIXFMT_420_PL8] = PDP_STR2SURF_PIXFMT_420_PL8;
	m[PDP_VID_PIXFMT_422_UY0VY1_8888] = PDP_STR2SURF_PIXFMT_422_UY0VY1_8888;
	m[PDP_VID_PIXFMT_422_VY0UY1_8888] = PDP_STR2SURF_PIXFMT_422_VY0UY1_8888;
	m[PDP_VID_PIXFMT_422_Y0UY1V_8888] = PDP_STR2SURF_PIXFMT_422_Y0UY1V_8888;
	m[PDP_VID_PIXFMT_422_Y0VY1U_8888] = PDP_STR2SURF_PIXFMT_422_Y0VY1U_8888;
#if PDP_REV < 0x010001
	m[PDP_VID_PIXFMT_420_PL8IVU] = -1;
	m[PDP_VID_PIXFMT_420_PL8IUV] = -1;
	m[PDP_VID_PIXFMT_420_T88CP] = PDP_STR2SURF_PIXFMT_420_T88CP;
	m[PDP_VID_PIXFMT_422_T88CP] = PDP_STR2SURF_PIXFMT_422_T88CP;
#else
	m[PDP_VID_PIXFMT_420_PL8IVU] = PDP_STR2SURF_PIXFMT_420_PL8IVU;
	m[PDP_VID_PIXFMT_420_PL8IUV] = PDP_STR2SURF_PIXFMT_420_PL8IUV;
	m[PDP_VID_PIXFMT_420_T88CP] = -1;
	m[PDP_VID_PIXFMT_422_T88CP] = -1;
#endif
}

static int pdpfb_vid_probe(struct pdpfb_priv *priv,
			struct platform_device *pdev,
			const struct fb_videomode *mode)
{
	struct fb_info *info = &THIS_STREAM->info;
	struct pdp_info *pdata = pdpfb_get_platform_data(priv);
	int error;

	pdpfb_vid_probe_caps(priv);

	info->device = &pdev->dev;
	info->fbops = &pdpfb_vid_ops;
	info->var.xres = info->var.xres_virtual = (mode->xres/2);
	info->var.yres = info->var.yres_virtual = (mode->yres/2);
	info->var.width = pdata->lcd_size_cfg.width;
	info->var.height = pdata->lcd_size_cfg.height;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.nonstd = PDP_VID_PIXFMT_422_Y0VY1U_8888;
	error = pdpfb_vid_set_bpp(&info->var);
	if (error)
		goto err0;
	THIS_PRIV->pixfmt = pdpfb_vid_nonstd_to_pixfmt(info->var.nonstd);
	pdpfb_vid_update_planar(priv, info);

	info->var.hsync_len = mode->hsync_len;
	info->var.left_margin = mode->left_margin;
	info->var.right_margin = mode->right_margin;
	info->var.vsync_len = mode->vsync_len;
	info->var.upper_margin = mode->upper_margin;
	info->var.lower_margin = mode->lower_margin;

	info->fix = pdpfb_vid_fix;


	error = pdpfb_str_videomem_alloc(priv, THIS_STREAM);
	if (error)
		goto err0;
	if (pdpfb_vid_required_mem(&info->var) > THIS_STREAM->videomem_len) {
		error = -ENOMEM;
		goto err1;
	}

	info->pseudo_palette = pdpfb_get_pseudo_palette(priv);
	info->flags = FBINFO_FLAG_DEFAULT
			| FBINFO_HWACCEL_XPAN
			| FBINFO_HWACCEL_YPAN;

	THIS_PRIV->csc.coefs = pdpfb_vid_csc_presets[PDP_VID_CSCPRESET_SDTV];

	error = register_framebuffer(info);
	if (error < 0)
		goto err1;

	dev_info(&pdev->dev, "registered video framebuffer (len=0x%lx)\n",
		 THIS_STREAM->videomem_len);

	return 0;
err1:
	pdpfb_str_videomem_free(THIS_STREAM);
err0:
	return error;
}

static int pdpfb_vid_remove(struct pdpfb_priv *priv,
			struct platform_device *pdev)
{
	struct fb_info *info = &THIS_STREAM->info;

	unregister_framebuffer(info);

	pdpfb_str_videomem_free(THIS_STREAM);
	return 0;
}

static struct pdpfb_vid_stream_priv pdpfb_vid_stream = {
	.stream = {
		.mem_pool = PDPFB_MEMPOOL_VIDMEM,
		.videomem_len = 0,
		.enable = 0,
		.ckey = {
			.ckey = 0x000000,
			.mask = 0xFFFFFF,
		},
		.global_alpha = 0xFF,
		.ops = {
			.probe = pdpfb_vid_probe,
			.remove = pdpfb_vid_remove,
			.check_geom = pdpfb_vid_check_geom,
			.set_geom = pdpfb_vid_set_geom,
			.configure = pdpfb_vid_configure,
			.configure_addr = pdpfb_vid_configure_addr,
		},
		.regs = {
			.surf = PDP_STR2SURF,
			.blend = PDP_STR2BLEND,
			.blend2 = PDP_STR2BLEND2,
			.ctrl = PDP_STR2CTRL,
			.posn = PDP_STR2POSN,
#if PDP_REV >= 0x010001
			.gamma = PDP_YUVGAMMA0,
			.gamma_stride = PDP_YUVGAMMA_STRIDE,
#endif
		},
	},
	.csc = {
		.enable = 1,
		.preset = PDP_VID_CSCPRESET_SDTV,
		.cosited = 1,
	},
#ifdef PDP_SHARED_BASE
	.base_addr_lock = __SPIN_LOCK_UNLOCKED(pdpfb_vid_stream.base_addr_lock),
#endif
};

struct pdpfb_stream *pdpfb_vid_get_stream(void)
{
	return THIS_STREAM;
}
