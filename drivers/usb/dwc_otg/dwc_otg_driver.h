/* ==========================================================================
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

#if !defined(__DWC_OTG_DRIVER_H__)
#define __DWC_OTG_DRIVER_H__

/** @file
 * This file contains the interface to the Linux driver.
 */

#if defined(CONFIG_SOC_TZ1090)
#include "dwc_otg_tz1090.h"
#endif

#include <linux/usb/dwc_otg_platform.h>
#include "dwc_otg_cil.h"


/* Type declarations */
struct dwc_otg_pcd;
struct dwc_otg_hcd;

/**
 * This structure is a wrapper that encapsulates the driver components used to
 * manage a single DWC_otg controller.
 */
struct dwc_otg_device {
	/** Device pointer for convenience */
	struct device *dev;

	/** Base address returned from ioremap() */
	void __iomem *base;

	/** Pointer to the core interface structure. */
	struct dwc_otg_core_if *core_if;

	/** Register offset for Diagnostic API.*/
	u32 reg_offset;

	/** Pointer to the PCD structure. */
	struct dwc_otg_pcd *pcd;

	/** Pointer to the HCD structure. */
	struct dwc_otg_hcd *hcd;

	/** Flag to indicate whether the common IRQ handler is installed. */
	u8 common_irq_installed;

	/** Interrupt request number. */
	unsigned int irq;

	/*
	 * Physical address of Control and Status registers, used by
	 * release_mem_region().
	 */
	resource_size_t phys_addr;

	/** Length of memory region, used by release_mem_region(). */
	unsigned long base_len;

	/* methods for enabling / disabling Vbus at the SoC Level*/
	void (*soc_enable_vbus)(void);
	void (*soc_disable_vbus)(void);
	void (*soc_vbus_valid)(int normal);

};



/**
 * The Debug Level bit-mask variable.
 */
extern u32 g_dbg_lvl;
/**
 * Set the Debug Level variable.
 */
static inline u32 SET_DEBUG_LEVEL(const u32 _new)
{
	u32 old = g_dbg_lvl;
	g_dbg_lvl = _new;
	return old;
}


/** When debug level has the DBG_CIL bit set, display CIL Debug messages. */
#define DBG_CIL		(0x2)
/** When debug level has the DBG_CILV bit set, display CIL Verbose debug
 * messages */
#define DBG_CILV	(0x20)
/**  When debug level has the DBG_PCD bit set, display PCD (Device) debug
 *  messages */
#define DBG_PCD		(0x4)
/** When debug level has the DBG_PCDV set, display PCD (Device) Verbose debug
 * messages */
#define DBG_PCDV	(0x40)
/** When debug level has the DBG_HCD bit set, display Host debug messages */
#define DBG_HCD		(0x8)
/** When debug level has the DBG_HCDV bit set, display Verbose Host debug
 * messages */
#define DBG_HCDV	(0x80)
/** When debug level has the DBG_HCD_URB bit set, display enqueued URBs in host
 *  mode. */
#define DBG_HCD_URB	(0x800)

#define DBG_SP (0x10) /*???*/

/** When debug level has any bit set, display debug messages */
#define DBG_ANY		(0xFF)

/** All debug messages off */
#define DBG_OFF		0

/** Prefix string for DWC_DEBUG print macros. */
#define USB_DWC "dwc_otg: "

/**
 * Print a debug message when the Global debug level variable contains
 * the bit defined in lvl.
 *
 * @param[in] lvl - Debug level, use one of the DBG_ constants above.
 * @param[in] x - like printf
 *
 *    Example:
 *      DWC_DEBUGPL( DBG_ANY, "%s(%p)\n", __func__, _reg_base_addr);
 * results in:
 * 	usb-DWC_otg: dwc_otg_cil_init(ca867000)
 */
#ifdef DEBUG
#define DWC_DEBUGPL(lvl, x...) \
	do { if ((lvl)&g_dbg_lvl)printk(KERN_ERR USB_DWC x); } while (0)
#define DWC_DEBUGP(x...) DWC_DEBUGPL(DBG_ANY, x)

#define CHK_DEBUG_LEVEL(level) ((level) & g_dbg_lvl)

#else
/*
 * Debugging support vanishes in non-debug builds.
 */
#define DWC_DEBUGPL(lvl, x...) do {} while (0)
#define DWC_DEBUGP(x...)

#define CHK_DEBUG_LEVEL(level) (0)

#endif /*DEBUG*/

/**
 * Print an Error message.
 */
#define DWC_ERROR(x...) printk(KERN_ERR USB_DWC x)
/**
 * Print a Warning message.
 */
#define DWC_WARN(x...) printk(KERN_WARNING USB_DWC x)
/**
 * Print a notice (normal but significant message).
 */
#define DWC_NOTICE(x...) printk(KERN_NOTICE USB_DWC x)
/**
 *  Basic message printing.
 */
#define DWC_PRINT(x...) printk(KERN_INFO USB_DWC x)

#endif
