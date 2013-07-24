/*
 * IMG Universal Communications Core Platform (UCCP) driver interface.
 *
 * Copyright (C) 2010  Imagination Technologies
 */
#ifndef _IMG_UCCP_H_
#define _IMG_UCCP_H_

#include <uapi/linux/uccp.h>


/* platform resource numbering in flags */
#define UCCP_RES_HOSTSYSBUS	0x0
#define UCCP_RES_MCREQ		0x1
#define UCCP_RES_UCCP(res)	(((res) & 0xf0) >> 4)
#define UCCP_RES_TYPE(res)	((res) & 0x0f)
#define UCCP_RES(uccp, res)	(((uccp) << 4) | (res))

struct uccp_core {
	struct uccp_region *regions;
	unsigned int num_regions;
	unsigned int num_mc_req;

	struct device *device;
	struct resource *host_sys_bus;
	struct resource *mc_req;
};

struct uccp_pdata {
	struct uccp_core *cores;
	unsigned int num_cores;

	struct uccp_region *regions;
	unsigned int num_regions;
};
#endif /* _IMG_UCCP_H_ */
