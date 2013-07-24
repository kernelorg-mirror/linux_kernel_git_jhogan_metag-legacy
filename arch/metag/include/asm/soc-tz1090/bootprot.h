/*
 * bootprot.h
 *
 * Boot protocol abstraction API (DFU, resume from suspend-to-RAM etc).
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 */

#ifndef _TZ1090_BOOTPROT_H_
#define _TZ1090_BOOTPROT_H_

void bootprot_normal_boot(unsigned long swprot0);
void bootprot_suspend_ram(unsigned long swprot0,
			  int (*resume)(void *),
			  void *data);
void bootprot_resume_ram(unsigned long swprot0);

#endif /* _TZ1090_BOOTPROT_H_ */
