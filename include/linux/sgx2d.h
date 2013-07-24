/*
 * PowerVR SGX 2D block driver.
 *
 * Copyright (C) 2010  Imagination Technologies
 */
#ifndef _IMG_SGX2D_H_
#define _IMG_SGX2D_H_

#include <uapi/linux/sgx2d.h>


/* private registers */
#define SGX2D_REG_PRIVATE	0x80000000
#define SGX2D_REG_SRST		(SGX2D_REG_PRIVATE | 0x06)	/* soft reset */

struct sgx2d_pdata_reg {
	struct sgx2d_reg reg;	/* offset into specified region */
	unsigned int region;	/* memory io resource number */
};
struct sgx2d_pdata {
	struct sgx2d_vers vers;
	struct sgx2d_pdata_reg *regs;	/* register information */
	unsigned int reg_count;		/* ARRAY_SIZE(regs) */
	int unmappable;			/* set if the iomem isn't mappable */
};

#endif /* _IMG_SGX2D_H_ */
