/*
 * IMG Universal Communications Core Platform (UCCP) driver interface.
 *
 * Copyright (C) 2010  Imagination Technologies
 */

#ifndef _UAPI_IMG_UCCP_H_
#define _UAPI_IMG_UCCP_H_

#include <linux/types.h>

#define UCCP_REGION_ALL			0x01
#define UCCP_REGION_SYS_INTERNAL	0x02
#define UCCP_REGION_MTX			0x10
#define UCCP_REGION_MCP_16_BIT		0x20
#define UCCP_REGION_MCP_24_BIT		0x21

struct uccp_region {
	__u32 type;	/* UCCP_REGION_* */
	__u32 physical;	/* Physical address of region */
	__u32 offset;	/* Offset within device file */
	__u32 size;	/* Size of region */
};

#define UCCP_REG_DIRECT		0x00
#define UCCP_REG_INDIRECT	0x01
#define UCCP_REG_MCPPERIP	0x02	/* MCP peripheral memory */
#define UCCP_REG_MCPPERIP_PACK	0x03	/* MCP packed peripheral memory */

struct uccp_reg {
	__u32 op;	/* UCCP_REG_* */
	__u32 reg;	/* Register id to read/write */
	__u32 val;	/* Value of register */
};

struct uccp_mcreq {
	__u32 index;	/* Index into mcreq table */
	__u32 bulk;	/* Bulk (MTX) address */
	__u32 size;	/* Size in bytes */
	__u32 physical;	/* Physical address */
};

#define UCCPIO 0xF2

/* get info about a memory region by type */
#define UCCPIO_GETREGION	_IOWR(UCCPIO, 0x80, struct uccp_region)
/* read/write to a register on the host bus */
#define UCCPIO_WRREG		_IOW(UCCPIO, 0x81, struct uccp_reg)
#define UCCPIO_RDREG		_IOWR(UCCPIO, 0x81, struct uccp_reg)
/* clear/get/set an MC REQ entry */
#define UCCPIO_CLRMCREQ		_IOW(UCCPIO, 0x82, struct uccp_mcreq)
#define UCCPIO_SETMCREQ		_IOW(UCCPIO, 0x83, struct uccp_mcreq)
#define UCCPIO_GETMCREQ		_IOWR(UCCPIO, 0x83, struct uccp_mcreq)
/* soft reset the UCCP */
#define UCCPIO_SRST		_IO(UCCPIO, 0x84)


#endif /* _UAPI_IMG_UCCP_H_ */
