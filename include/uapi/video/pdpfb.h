/*
 * PDP Desktop Graphics Framebuffer
 *
 * Copyright (c) 2008 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _UAPI_VIDEO_PDPFB_H
#define _UAPI_VIDEO_PDPFB_H


#include <linux/ioctl.h>
#include <linux/types.h>

struct pdpfb_geom {
	__u16 x, y;
	__u16 w, h;	/* 0 for no scaling */
};

struct pdpfb_ckey {
	__u32 ckey;	/* rgb888 */
	__u32 mask;	/* mask of ckey */
};

/* YUV->RGB colour space conversion */
struct pdpfb_vid_csc_coefs {
	__s32 ry, rv, ru;
	__s32 gy, gv, gu;
	__s32 by, bv, bu;
};

struct pdpfb_vid_csc {
	__u32 enable;
	__u32 preset;	/* 0 for custom coefficients */
	__u32 cosited;

	struct pdpfb_vid_csc_coefs coefs;
};

/* Planar pixel formats */
struct pdpfb_vid_planar {
	/* offsets of planes inside framebuffer */
	__u32 y_offset;
	__u32 u_offset;
	__u32 v_offset;
	/* line strides for each plane */
	__u32 y_line_length;
	__u32 u_line_length;
	__u32 v_line_length;
};

/* User provided memory */
struct pdpfb_usermem {
	__u32 phys;
	__u32 len;
	__u16 flags;
};

#define PDP_CKEYMODE_DISABLE	0x0
#define PDP_CKEYMODE_PREVIOUS	0x1	/* compare ckey with previous plane */
#define PDP_CKEYMODE_CURRENT	0x2	/* compare ckey with current plane */

#define PDP_BLENDMODE_NOALPHA	0x0
#define PDP_BLENDMODE_INVERT	0x1
#define PDP_BLENDMODE_GLOBAL	0x2
#define PDP_BLENDMODE_PIXEL	0x3

/* non standard pixel formats */
#define PDP_VID_PIXFMT_420_PL8		1	/* YV12 */
#define PDP_VID_PIXFMT_420_PL8IVU	2	/* interleaved chroma (v lsb) */
#define PDP_VID_PIXFMT_420_PL8IUV	3	/* interleaved chroma (u lsb) */
#define PDP_VID_PIXFMT_422_UY0VY1_8888	4	/* U lsb */
#define PDP_VID_PIXFMT_422_VY0UY1_8888	5	/* V lsb */
#define PDP_VID_PIXFMT_422_Y0UY1V_8888	6	/* Y0 lsb */
#define PDP_VID_PIXFMT_422_Y0VY1U_8888	7	/* Y0 lsb */
#define PDP_VID_PIXFMT_420_T88CP	8	/* unimplemented */
#define PDP_VID_PIXFMT_422_T88CP	9	/* unimplemented */
#define PDP_VID_PIXFMT_MAX		10

#define PDP_VID_CSCPRESET_HDTV		0x1
#define PDP_VID_CSCPRESET_SDTV		0x2	/* default */
#define PDP_VID_CSCPRESET_LEGACYHDTV	0x3
#define PDP_VID_CSCPRESET_LEGACYSDTV	0x4

/* PDPIO_SETUSERMEM flags */
#define PDP_USERMEM_ALLPLANES	0x1

#define PDPIO 0xF2

/* general (0x00 - 0x0F) */
#define PDPIO_GETBGND		_IOR(PDPIO, 0x00, __u32)
#define PDPIO_SETBGND		_IOW(PDPIO, 0x00, __u32)
#define PDPIO_GETSCRGEOM	_IOR(PDPIO, 0x01, struct pdpfb_geom)
#define PDPIO_SETUSERMEM	_IOW(PDPIO, 0x02, struct pdpfb_usermem)

/* general plane (0x10 - 0x1F) */
#define PDPIO_GETEN		_IOR(PDPIO, 0x10, int)
#define PDPIO_SETEN		_IOW(PDPIO, 0x10, int)
#define PDPIO_GETPLANEPOS	_IOR(PDPIO, 0x11, int)
#define PDPIO_SETPLANEPOS	_IOWR(PDPIO, 0x11, int)
#define PDPIO_GETGEOM		_IOR(PDPIO, 0x12, struct pdpfb_geom)
#define PDPIO_SETGEOM		_IOWR(PDPIO, 0x12, struct pdpfb_geom)
#define PDPIO_GETCKEYMODE	_IOR(PDPIO, 0x13, __u32)
#define PDPIO_SETCKEYMODE	_IOW(PDPIO, 0x13, __u32)
#define PDPIO_GETCKEY		_IOR(PDPIO, 0x14, struct pdpfb_ckey)
#define PDPIO_SETCKEY		_IOW(PDPIO, 0x14, struct pdpfb_ckey)
#define PDPIO_GETBLENDMODE	_IOR(PDPIO, 0x15, __u32)
#define PDPIO_SETBLENDMODE	_IOW(PDPIO, 0x15, __u32)
#define PDPIO_GETGALPHA		_IOR(PDPIO, 0x16, __u32)
#define PDPIO_SETGALPHA		_IOWR(PDPIO, 0x16, __u32)

/* graphics plane (0x20 - 0x2F) */

/* video plane (0x30 - 0x3F) */
#define PDPIO_GETCSC		_IOR(PDPIO, 0x30, struct pdpfb_vid_csc)
#define PDPIO_SETCSC		_IOWR(PDPIO, 0x30, struct pdpfb_vid_csc)
#define PDPIO_GETPLANAR		_IOR(PDPIO, 0x31, struct pdpfb_vid_planar)
#define PDPIO_SETPLANAR		_IOWR(PDPIO, 0x31, struct pdpfb_vid_planar)

#endif /* _UAPI_VIDEO_PDPFB_H */
