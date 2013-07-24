/*
 * usb.h
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 */

#ifndef _TZ1090_USB_H_
#define _TZ1090_USB_H_

#include <linux/init.h>
#include <linux/usb/dwc_otg_platform.h>

extern int __init comet_usb_setup(const struct dwc_otg_board *board);

#endif
