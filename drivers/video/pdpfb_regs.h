/*
 * PDP Framebuffer
 *
 * Copyright (c) 2008-2012 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _PDPFB_REGS_H
#define _PDPFB_REGS_H

#include <video/pdpfb.h>

/*
 * Bitfield operations
 * For each argument field, the following preprocessor macros must exist
 * field##_BITS - the number of bits in the bit field
 * field##_OFFSET - offset from the first bit
 */

#define GET_MASK(bits) \
	((0x1<<(bits))-1)

#define PLACE_FIELD(field, val) \
	(((u32)(val) & GET_MASK(field##_BITS)) << field##_OFFSET)

#define ADJ_FIELD(x, field, val) \
	(((x) & ~(GET_MASK(field##_BITS) << field##_OFFSET)) \
	| PLACE_FIELD(field, val))

#define SET_FIELD(x, field, val) \
	(x) = ADJ_FIELD(x, field, val)

#define GET_FIELD(x, field) \
	(((x) >> (field##_OFFSET)) & GET_MASK(field##_BITS))

/* Keeps most significant bits */
#define MOVE_FIELD(x, o1, l1, o2, l2) \
	(((x) >> ((o1) + (l1) - (l2))) << (o2))

/* PDP 1.0.0 */
#define PDP_STR1SURF		0x0000
#define PDP_STR2SURF		0x0004
#define PDP_STR1BLEND		0x0020
#define PDP_STR2BLEND		0x0024
#define PDP_STR1BLEND2		0x0040
#define PDP_STR2BLEND2		0x0044
#define PDP_STR1CTRL		0x0060
#define PDP_STR2CTRL		0x0064
#define PDP_STR1ADDR		0x0068
#define PDP_STR2ADDR		0x006C
#define PDP_STR2UADDR		0x0084
#define PDP_STR2VADDR		0x00A4
#define PDP_STR1POSN		0x00C0
#define PDP_STR2POSN		0x00C4
#define PDP_TILEPARAM1		0x00D0
#define PDP_TILEPARAM2		0x00D4
#define PDP_TILEPARAM3		0x00D8
#define PDP_TILEPARAM4		0x00DC
#define PDP_TILEPARAM5		0x00E0
#define PDP_PALETTE1		0x014C
#define PDP_PALETTE2		0x0150
#define PDP_SYNCCTRL		0x0154
#define PDP_HSYNC1		0x0158
#define PDP_HSYNC2		0x015C
#define PDP_HSYNC3		0x0160
#define PDP_VSYNC1		0x0164
#define PDP_VSYNC2		0x0168
#define PDP_VSYNC3		0x016C
#define PDP_BORDCOL		0x0170
#define PDP_BGNDCOL		0x0174
#define PDP_INTSTAT		0x0178
#define PDP_INTENAB		0x017C
#define PDP_INTCTRL		0x0180
#define PDP_SIGNAT		0x0184
#define PDP_MEMCTRL		0x0188
#define PDP_SKIPCTRL		0x018C
#define PDP_SCALECTRL		0x0190
#define PDP_MEMSTRCTRL1		0x0194
#define PDP_MEMSTRCTRL2		0x0198
#define PDP_PGSZMASK		0x019c
#define PDP_HSINIT		0x01B0
#define PDP_HSCOEFF0		0x01B4
#define PDP_HSCOEFF1		0x01B8
#define PDP_HSCOEFF2		0x01BC
#define PDP_HSCOEFF3		0x01C0
#define PDP_HSCOEFF4		0x01C4
#define PDP_HSCOEFF5		0x01C8
#define PDP_HSCOEFF6		0x01CC
#define PDP_HSCOEFF7		0x01D0
#define PDP_HSCOEFF8		0x01D4
#define PDP_SCALESIZE		0x01D8
#define PDP_REGLD_SPC		0x02FC
#define PDP_REGLD_STAT		0x0300
#define PDP_REGLD_CTRL		0x0304
#define PDP_LINESTAT		0x0308
#define PDP_UPDCTRL		0x030C
#define PDP_VEVENT		0x0310
#define PDP_HDECTRL		0x0314
#define PDP_VDECTRL		0x0318
#define PDP_OPMASK		0x031C
#define PDP_DBGCTRL		0x0320
#define PDP_DBGDATA		0x0324
#define PDP_DBGSIDE		0x0328
#if PDP_REV < 0x010001
#define PDP_INTCLR		0x032C
#else
#define PDP_INTCLR		0x01AC
#endif
#define PDP_CSCCOEFF0		0x0330
#define PDP_CSCCOEFF1		0x0334
#define PDP_CSCCOEFF2		0x0338
#define PDP_CSCCOEFF3		0x033C
#define PDP_CSCCOEFF4		0x0340
#define PDP_CORE_ID		0x04E0
#define PDP_CORE_REV		0x04F0

/* PDP 1.0.1 */
#if PDP_REV >= 0x010001
#define PDP_CURS1ADDR		0x0100
#define PDP_CURS1SIZE		0x0110
#define PDP_CURS1POSN		0x0120
#define PDP_CURS1BLND		0x0130
#define PDP_CURS1BLND2		0x0140
#define PDP_VSCOEFF0		0x0198
#define PDP_VSCOEFF1		0x019C
#define PDP_VSCOEFF2		0x01A0
#define PDP_VSCOEFF3		0x01A4
#define PDP_VSCOEFF4		0x01A8
#define PDP_VSINIT		0x0194
#define PDP_YUVGAMMA0		0x0200
#define PDP_YUVGAMMA_STRIDE	4
#define PDP_RGBGAMMA0		0x0250
#define PDP_RGBGAMMA_STRIDE	4
#endif

/* Pixel format field information */

#define RGB565_RED_BITS		5
#define RGB565_RED_OFFSET	11
#define RGB565_GREEN_BITS	6
#define RGB565_GREEN_OFFSET	5
#define RGB565_BLUE_BITS	5
#define RGB565_BLUE_OFFSET	0
#define PLACE_RGB565(r, g, b) \
	(PLACE_FIELD(RGB565_RED, r) \
	|PLACE_FIELD(RGB565_GREEN, g) \
	|PLACE_FIELD(RGB565_BLUE, b))

/* Register field information */

#define PDP_STR1SURF_USELUT_BITS	1	/* colour lookup table */
#define PDP_STR1SURF_USELUT_OFFSET	31
#define PDP_STR1SURF_PIXFMT_BITS	4	/* graphics pixel format */
#define PDP_STR1SURF_PIXFMT_OFFSET	27
/* PDP 1.0.0 */
#define PDP_STR1SURF_PIXFMT_RGB8	0x0
#define PDP_STR1SURF_PIXFMT_ARGB4444	0x4
#define PDP_STR1SURF_PIXFMT_ARGB1555	0x5
#if PDP_REV < 0x010001
#define PDP_STR1SURF_PIXFMT_RGB565	0x6
/* PDP 1.0.1 */
#else
#define PDP_STR1SURF_PIXFMT_RGB888	0x6
#define PDP_STR1SURF_PIXFMT_RGB565	0x7
#define PDP_STR1SURF_PIXFMT_ARGB8888	0x8
#endif

#define PDP_STR2SURF_PIXFMT_BITS	4	/* video pixel format */
#define PDP_STR2SURF_PIXFMT_OFFSET	27
#define PDP_STR2SURF_USECSC_BITS	1
#define PDP_STR2SURF_USECSC_OFFSET	25
#define PDP_STR2SURF_COSITED_BITS	1
#define PDP_STR2SURF_COSITED_OFFSET	22
/* PDP 1.0.0 */
#define PDP_STR2SURF_PIXFMT_420_PL8		0x9
#if PDP_REV < 0x010001
#define PDP_STR2SURF_PIXFMT_422_UY0VY1_8888	0xA
#define PDP_STR2SURF_PIXFMT_422_VY0UY1_8888	0xB
#define PDP_STR2SURF_PIXFMT_422_Y0UY1V_8888	0xC
#define PDP_STR2SURF_PIXFMT_422_Y0VY1U_8888	0xD
#define PDP_STR2SURF_PIXFMT_420_T88CP		0xE
#define PDP_STR2SURF_PIXFMT_422_T88CP		0xF
/* PDP 1.0.1 */
#else
#define PDP_STR2SURF_PIXFMT_420_PL8IVU		0xA
#define PDP_STR2SURF_PIXFMT_420_PL8IUV		0xB
#define PDP_STR2SURF_PIXFMT_422_UY0VY1_8888	0xC
#define PDP_STR2SURF_PIXFMT_422_VY0UY1_8888	0xD
#define PDP_STR2SURF_PIXFMT_422_Y0UY1V_8888	0xE
#define PDP_STR2SURF_PIXFMT_422_Y0VY1U_8888	0xF
#endif

#define PDP_STRXSURF_WIDTH_BITS		11	/* width - 1 */
#define PDP_STRXSURF_WIDTH_OFFSET	11
#define PDP_STRXSURF_HEIGHT_BITS	11	/* height - 1 */
#define PDP_STRXSURF_HEIGHT_OFFSET	0

#define PDP_STRXBLEND_GLOBALALPHA_BITS		8	/* global alpha */
#define PDP_STRXBLEND_GLOBALALPHA_OFFSET	24
#define PDP_STRXBLEND_COLKEY_BITS		24	/* colour key */
#define PDP_STRXBLEND_COLKEY_OFFSET		0

#define PDP_STRXBLEND2_PIXDOUBLE_BITS		1	/* pixel doubling */
#define PDP_STRXBLEND2_PIXDOUBLE_OFFSET		31
#define PDP_STRXBLEND2_PIXHALVE_BITS		1	/* pixel halving */
#define PDP_STRXBLEND2_PIXHALVE_OFFSET		30
#define PDP_STRXBLEND2_LINEDOUBLE_BITS		1	/* line doubling */
#define PDP_STRXBLEND2_LINEDOUBLE_OFFSET	29
#define PDP_STRXBLEND2_LINEHALVE_BITS		1	/* line halving */
#define PDP_STRXBLEND2_LINEHALVE_OFFSET		28
#define PDP_STRXBLEND2_COLKEYMASK_BITS		24	/* colour key mask */
#define PDP_STRXBLEND2_COLKEYMASK_OFFSET	0

#define PDP_STRXCTRL_STREAMEN_BITS		1	/* stream enable */
#define PDP_STRXCTRL_STREAMEN_OFFSET		31
#define PDP_STRXCTRL_CKEYEN_BITS		1	/* colour key enable */
#define PDP_STRXCTRL_CKEYEN_OFFSET		30
#define PDP_STRXCTRL_CKEYSRC_BITS		1	/* colour key source */
#define PDP_STRXCTRL_CKEYSRC_OFFSET		29
#define PDP_STRXCTRL_CKEYSRC_PREV		0x0
#define PDP_STRXCTRL_CKEYSRC_CUR		0x1
#define PDP_STRXCTRL_BLENDMODE_BITS		2	/* blend mode */
#define PDP_STRXCTRL_BLENDMODE_OFFSET		27
#define PDP_STRXCTRL_BLENDPOS_BITS		3	/* plane position */
#define PDP_STRXCTRL_BLENDPOS_OFFSET		24
#define PDP_STRXCTRL_BASEADDR_BITS		22	/* 25:4 of base addr */
#define PDP_STRXCTRL_BASEADDR_OFFSET		0
#define PDP_STR2CTRL_UVHALFSTR_BITS		1	/* UV half stride */
#define PDP_STR2CTRL_UVHALFSTR_OFFSET		0

#define PDP_STR2UADDR_UVHALFSTR_BITS		1	/* UV half stride */
#define PDP_STR2UADDR_UVHALFSTR_OFFSET		31
#define PDP_STR2UADDR_UBASEADDR_BITS		23	/* U plane base addr */
#define PDP_STR2UADDR_UBASEADDR_OFFSET		0

#define PDP_STR2VADDR_VBASEADDR_BITS		23	/* V plane base addr */
#define PDP_STR2VADDR_VBASEADDR_OFFSET		0

#define PDP_SYNCCTRL_SYNCACTIVE_BITS		1	/* starts sync generator */
#define PDP_SYNCCTRL_SYNCACTIVE_OFFSET		31
#define PDP_SYNCCTRL_DISPRST_BITS		1	/* software reset */
#define PDP_SYNCCTRL_DISPRST_OFFSET		29
#define PDP_SYNCCTRL_POWERDN_BITS		1	/* power down mode */
#define PDP_SYNCCTRL_POWERDN_OFFSET		28
#define PDP_SYNCCTRL_UPDSYNCCTRL_BITS		1	/* display update sync control */
#define PDP_SYNCCTRL_UPDSYNCCTRL_OFFSET		26
#define PDP_SYNCCTRL_UPDINTCTRL_BITS		1	/* display update interrupt control */
#define PDP_SYNCCTRL_UPDINTCTRL_OFFSET		25
#define PDP_SYNCCTRL_UPDCTRL_BITS		1	/* display update control */
#define PDP_SYNCCTRL_UPDCTRL_OFFSET		24
#define PDP_SYNCCTRL_UPDWAIT_BITS		5	/* fields to wait before updating */
#define PDP_SYNCCTRL_UPDWAIT_OFFSET		16
#define PDP_SYNCCTRL_CSYNCEN_BITS		1	/* composite output enable */
#define PDP_SYNCCTRL_CSYNCEN_OFFSET		12
#define PDP_SYNCCTRL_CLKPOL_BITS		1	/* pixel clock polarity */
#define PDP_SYNCCTRL_CLKPOL_OFFSET		11
#define PDP_SYNCCTRL_VSSLAVE_BITS		1	/* vsync master/slave */
#define PDP_SYNCCTRL_VSSLAVE_OFFSET		7
#define PDP_SYNCCTRL_HSSLAVE_BITS		1	/* hsync master/slave */
#define PDP_SYNCCTRL_HSSLAVE_OFFSET		6
#define PDP_SYNCCTRL_BLNKPOL_BITS		1	/* blank signal polarity */
#define PDP_SYNCCTRL_BLNKPOL_OFFSET		5
#define PDP_SYNCCTRL_BLNKDIS_BITS		1	/* blank signal disable */
#define PDP_SYNCCTRL_BLNKDIS_OFFSET		4
#define PDP_SYNCCTRL_VSPOL_BITS			1	/* vertical sync polarity */
#define PDP_SYNCCTRL_VSPOL_OFFSET		3
#define PDP_SYNCCTRL_VSDIS_BITS			1	/* vertical sync disable */
#define PDP_SYNCCTRL_VSDIS_OFFSET		2
#define PDP_SYNCCTRL_HSPOL_BITS			1	/* horizontal sync polarity */
#define PDP_SYNCCTRL_HSPOL_OFFSET		1
#define PDP_SYNCCTRL_HSDIS_BITS			1	/* horizontal sync disable */
#define PDP_SYNCCTRL_HSDIS_OFFSET		0

#define PDP_HSYNC1_HBPS_BITS		12	/* horizontal back porch start */
#define PDP_HSYNC1_HBPS_OFFSET		16
#define PDP_HSYNC1_HT_BITS		12	/* horizontal total */
#define PDP_HSYNC1_HT_OFFSET		0

#define PDP_HSYNC2_HAS_BITS		12	/* horizontal active start */
#define PDP_HSYNC2_HAS_OFFSET		16
#define PDP_HSYNC2_HLBS_BITS		12	/* horizontal left border start */
#define PDP_HSYNC2_HLBS_OFFSET		0

#define PDP_HSYNC3_HFPS_BITS		12	/* horizontal front porch start */
#define PDP_HSYNC3_HFPS_OFFSET		16
#define PDP_HSYNC3_HRBS_BITS		12	/* horizontal right border start */
#define PDP_HSYNC3_HRBS_OFFSET		0

#define PDP_VSYNC1_VBPS_BITS		12	/* vertical back porch start */
#define PDP_VSYNC1_VBPS_OFFSET		16
#define PDP_VSYNC1_VT_BITS		12	/* vertical total */
#define PDP_VSYNC1_VT_OFFSET		0

#define PDP_VSYNC2_VAS_BITS		12	/* vertical active start */
#define PDP_VSYNC2_VAS_OFFSET		16
#define PDP_VSYNC2_VTBS_BITS		12	/* vertical top border start */
#define PDP_VSYNC2_VTBS_OFFSET		0

#define PDP_VSYNC3_VFPS_BITS		12	/* vertical front porch start */
#define PDP_VSYNC3_VFPS_OFFSET		16
#define PDP_VSYNC3_VBBS_BITS		12	/* vertical bottom border start */
#define PDP_VSYNC3_VBBS_OFFSET		0

#define PDP_VEVENT_VEVENT_BITS		12	/* vertical event start */
#define PDP_VEVENT_VEVENT_OFFSET	16
#define PDP_VEVENT_VFETCH_BITS		12	/* vertical fetch start */
#define PDP_VEVENT_VFETCH_OFFSET	0

#define PDP_HDECTRL_HDES_BITS		12	/* horizontal data enable start */
#define PDP_HDECTRL_HDES_OFFSET		16
#define PDP_HDECTRL_HDEF_BITS		12	/* horizontal data enable finish */
#define PDP_HDECTRL_HDEF_OFFSET		0

#define PDP_VDECTRL_VDES_BITS		12	/* vertical data enable start */
#define PDP_VDECTRL_VDES_OFFSET		16
#define PDP_VDECTRL_VDEF_BITS		12	/* vertical data enable finish */
#define PDP_VDECTRL_VDEF_OFFSET		0

#define PDP_OPMASK_MASKLEVEL_BITS	1	/* masked output bit level */
#define PDP_OPMASK_MASKLEVEL_OFFSET	31
#define PDP_OPMASK_BLANKLEVEL_BITS	1	/* data disable output bit level */
#define PDP_OPMASK_BLANKLEVEL_OFFSET	30
#define PDP_OPMASK_MASKB_BITS		8	/* output data mask for blue channel */
#define PDP_OPMASK_MASKB_OFFSET		16
#define PDP_OPMASK_MASKG_BITS		8	/* output data mask for green channel */
#define PDP_OPMASK_MASKG_OFFSET		8
#define PDP_OPMASK_MASKR_BITS		8	/* output data mask for red channel */
#define PDP_OPMASK_MASKR_OFFSET		0

#define PDP_STRXPOSN_SRCSTRIDE_BITS	10	/* stride of surface in 16byte words - 1 */
#define PDP_STRXPOSN_SRCSTRIDE_OFFSET	22
#define PDP_STRXPOSN_XSTART_BITS	11	/* x coordinate of top left corner */
#define PDP_STRXPOSN_XSTART_OFFSET	11
#define PDP_STRXPOSN_YSTART_BITS	11	/* y coordinate of top left corner */
#define PDP_STRXPOSN_YSTART_OFFSET	0

#define PDP_PALETTE1_LUTADDR_BITS	8	/* set LUT address to read and write */
#define PDP_PALETTE1_LUTADDR_OFFSET	24
#define PDP_PALETTE2_LUTDATA_BITS	18	/* data to read or write to LUT in RGB666 */
#define PDP_PALETTE2_LUTDATA_OFFSET	0

#define PDP_INT_VEVENT0_BITS		1	/* start of safe update region */
#define PDP_INT_VEVENT0_OFFSET		2
#define PDP_INT_HBLNK0_BITS		1	/* start of horizontal blanking */
#define PDP_INT_HBLNK0_OFFSET		0

#define PDP_INTCTRL_HBLNKLINE_BITS	1	/* horizontal blanking interrupt line */
#define PDP_INTCTRL_HBLNKLINE_OFFSET	16
#define PDP_INTCTRL_HBLNKLINE_ALL	0
#define PDP_INTCTRL_HBLNKLINE_SPECIFIC	1
#define PDP_INTCTRL_HBLNKLINENO_BITS	12	/* horizontal line number to interrupt */
#define PDP_INTCTRL_HBLNKLINENO_OFFSET	0

#define PDP_MEMCTRL_MEMREFRESH_BITS	2	/* memory refresh control */
#define PDP_MEMCTRL_MEMREFRESH_OFFSET	30
#define PDP_MEMCTRL_MEMREFRESH_ALWAYS	0x0
#define PDP_MEMCTRL_MEMREFRESH_HBLNK	0x1
#define PDP_MEMCTRL_MEMREFRESH_VBLNK	0x2
#define PDP_MEMCTRL_MEMREFRESH_BOTH	0x3

#define PDP_SKIPCTRL_XCLIP_BITS		4	/* video pixels to remove after scaling */
#define PDP_SKIPCTRL_XCLIP_OFFSET	28
#define PDP_SKIPCTRL_HSKIP_BITS		11	/* video pixels to remove before scaling */
#define PDP_SKIPCTRL_HSKIP_OFFSET	16
#if PDP_REV >= 0x010001
#define PDP_SKIPCTRL_YCLIP_BITS		4	/* video pixels to remove after scaling */
#define PDP_SKIPCTRL_YCLIP_OFFSET	12
#define PDP_SKIPCTRL_VSKIP_BITS		11	/* video pixels to remove before scaling */
#define PDP_SKIPCTRL_VSKIP_OFFSET	0
#endif

#define PDP_SCALECTRL_HSCALEBP_BITS	1	/* video hscale bypass */
#define PDP_SCALECTRL_HSCALEBP_OFFSET	31
#define PDP_SCALECTRL_VSCALEBP_BITS	1	/* video hscale bypass */
#define PDP_SCALECTRL_VSCALEBP_OFFSET	30
#define PDP_SCALECTRL_HSBEFOREVS_BITS	1	/* hscale/vscale order */
#define PDP_SCALECTRL_HSBEFOREVS_OFFSET	29
#define PDP_SCALECTRL_VSURUNCTRL_BITS	1	/* vscale under-run control */
#define PDP_SCALECTRL_VSURUNCTRL_OFFSET	27
#define PDP_SCALECTRL_VORDER_BITS	2	/* vertical filter order */
#define PDP_SCALECTRL_VORDER_OFFSET	16
#define PDP_SCALECTRL_VORDER_1TAP	0x0	/* 1 tap (decim/replic) */
#define PDP_SCALECTRL_VORDER_2TAP	0x1	/* 2 tap (bilinear) */
#define PDP_SCALECTRL_VORDER_4TAP	0x2	/* 4 tap */
#define PDP_SCALECTRL_VPITCH_BITS	16	/* Vertical pitch, 5.11 fixpt */
#define PDP_SCALECTRL_VPITCH_OFFSET	0

#define PDP_HSINIT_HINITIAL_BITS	16	/* HQ vid filter initial hpos, 5.11 fixed pt */
#define PDP_HSINIT_HINITIAL_OFFSET	16
#define PDP_HSINIT_HINITIAL_FIX		11
#define PDP_HSINIT_HDECIM_BITS		1	/* pixel halving prior to scaling */
#define PDP_HSINIT_HDECIM_OFFSET	15
#define PDP_HSINIT_HPITCH_BITS		15	/* horizontal scale pitch, 4.11 fixed pt */
#define PDP_HSINIT_HPITCH_OFFSET	0
#define PDP_HSINIT_HPITCH_FIX		11

#define PDP_VSINIT_INITIAL1_BITS	16	/* Initial pos of field 1 */
#define PDP_VSINIT_INITIAL1_OFFSET	16
#define PDP_VSINIT_INITIAL0_BITS	16	/* Initial pos of field 0 */
#define PDP_VSINIT_INITIAL0_OFFSET	0

#define PDP_SCALESIZE_SCALEDWIDTH_BITS		11	/* width after scaling - 1 in px */
#define PDP_SCALESIZE_SCALEDWIDTH_OFFSET	16
#define PDP_SCALESIZE_SCALEDHEIGHT_BITS		11	/* height after scaling - 1 in px */
#define PDP_SCALESIZE_SCALEDHEIGHT_OFFSET	0

#define PDP_CSCCOEFF0_RU_BITS		11	/* U CSC coefficient for R channel */
#define PDP_CSCCOEFF0_RU_OFFSET		11
#define PDP_CSCCOEFF0_RY_BITS		11	/* Y CSC coefficient for R channel */
#define PDP_CSCCOEFF0_RY_OFFSET		0

#define PDP_CSCCOEFF1_GY_BITS		11	/* Y CSC coefficient for G channel */
#define PDP_CSCCOEFF1_GY_OFFSET		11
#define PDP_CSCCOEFF1_RV_BITS		11	/* V CSC coefficient for R channel */
#define PDP_CSCCOEFF1_RV_OFFSET		0

#define PDP_CSCCOEFF2_GV_BITS		11	/* V CSC coefficient for G channel */
#define PDP_CSCCOEFF2_GV_OFFSET		11
#define PDP_CSCCOEFF2_GU_BITS		11	/* U CSC coefficient for G channel */
#define PDP_CSCCOEFF2_GU_OFFSET		0

#define PDP_CSCCOEFF3_BU_BITS		11	/* U CSC coefficient for B channel */
#define PDP_CSCCOEFF3_BU_OFFSET		11
#define PDP_CSCCOEFF3_BY_BITS		11	/* Y CSC coefficient for B channel */
#define PDP_CSCCOEFF3_BY_OFFSET		0

#define PDP_CSCCOEFF4_BV_BITS		11	/* V CSC coefficient for B channel */
#define PDP_CSCCOEFF4_BV_OFFSET		0

#define PDP_LINESTAT_LINENO_STAT_BITS	12	/* Current line number. */
#define PDP_LINESTAT_LINENO_STAT_OFFSET	0

/* info about video plane addresses */
#ifndef PDP_SHARED_BASE
#define PDP_YADDR_BITS		28
#define PDP_YADDR_ALIGN		4
#define PDP_UADDR_BITS		28
#define PDP_UADDR_ALIGN		4
#define PDP_VADDR_BITS		28
#define PDP_VADDR_ALIGN		4
#else
#define PDP_YADDR_BITS		PDP_STRXCTRL_BASEADDR_BITS
#define PDP_YADDR_ALIGN		4
#define PDP_UADDR_BITS		PDP_STR2UADDR_UBASEADDR_BITS
#define PDP_UADDR_ALIGN		3
#define PDP_VADDR_BITS		PDP_STR2VADDR_VBASEADDR_BITS
#define PDP_VADDR_ALIGN		3
#endif
#define PDP_YSTRIDE_BITS	PDP_STRXPOSN_SRCSTRIDE_BITS
#define PDP_YSTRIDE_ALIGN	4

#define PDP_YADDR_MAX		(((1 << PDP_YADDR_BITS) - 1) << PDP_YADDR_ALIGN)
#define PDP_UADDR_MAX		(((1 << PDP_UADDR_BITS) - 1) << PDP_UADDR_ALIGN)
#define PDP_VADDR_MAX		(((1 << PDP_VADDR_BITS) - 1) << PDP_VADDR_ALIGN)
#define PDP_YSTRIDE_MAX		((1 << PDP_YSTRIDE_BITS) << PDP_YSTRIDE_ALIGN)
#define PDP_YADDR_ALIGNMASK	((1 << PDP_YADDR_ALIGN) - 1)
#define PDP_UADDR_ALIGNMASK	((1 << PDP_UADDR_ALIGN) - 1)
#define PDP_VADDR_ALIGNMASK	((1 << PDP_VADDR_ALIGN) - 1)
#define PDP_YSTRIDE_ALIGNMASK	((1 << PDP_YSTRIDE_ALIGN) - 1)

#endif
