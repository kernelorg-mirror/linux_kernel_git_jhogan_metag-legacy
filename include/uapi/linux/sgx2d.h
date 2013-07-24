/*
 * PowerVR SGX 2D block driver.
 *
 * Copyright (C) 2010  Imagination Technologies
 */

#ifndef _UAPI_IMG_SGX2D_H_
#define _UAPI_IMG_SGX2D_H_

#include <linux/types.h>

/* architecture families */
#define SGX2D_FAM_UNSPECIFIED	0x00
#define SGX2D_FAM_COMET		0x01

/* architecture capabilities */
#define SGX2D_CAP_FIFOFREE	0x00000001	/* Fifo freespace available */

/* registers */
#define SGX2D_REG_INVALID	0x00
#define SGX2D_REG_SLAVEPORT	0x01
#define SGX2D_REG_FIFOFREE	0x02	/* space left in slave port */
#define SGX2D_REG_BLTCOUNT	0x03	/* number of completed blits */
#define SGX2D_REG_BUSY		0x04	/* busy status */
#define SGX2D_REG_IDLE		0x05	/* idle status */
#define SGX2D_REG_BASEADDR	0x06	/* memory base address */

struct sgx2d_vers {
	__u32 sgx_vers;
	__u32 arch_fam;		/* SGX2D_FAM_* */
	__u32 arch_vers;
	__u32 caps;		/* SGX2D_CAP_* */
};

struct sgx2d_reg {
	__u32 id;		/* in, SGX2D_REG_* */
	__u32 offset;		/* offset into mmio memory */
	__u32 mask;		/* register mask */
	__u8 shift;
};

/* what to pass to mmap */
struct sgx2d_meminfo {
	void *addr;		/* address to use/pass to mmap */
	unsigned int len;	/* length of memory area */
	int flags;		/* mmap flags (0 indicates unmappable) */
};

#define SGX2DIO 0xF2

#define SGX2DIO_WAITIDLE	_IO(SGX2DIO,  0xC0)
#define SGX2DIO_SOFTRST		_IO(SGX2DIO,  0xC1)
#define SGX2DIO_GETVERS		_IOR(SGX2DIO,  0xC2, struct sgx2d_vers)
#define SGX2DIO_GETREG		_IOWR(SGX2DIO, 0xC3, struct sgx2d_reg)
#define SGX2DIO_GETMEM		_IOR(SGX2DIO,  0xC4, struct sgx2d_meminfo)


#endif /* _UAPI_IMG_SGX2D_H_ */
