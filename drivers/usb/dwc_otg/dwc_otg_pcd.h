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
#ifndef DWC_HOST_ONLY
#if !defined(__DWC_PCD_H__)
#define __DWC_PCD_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>

struct lm_device;
struct dwc_otg_device;

#include "dwc_otg_cil.h"

/**
 *
 * This file contains the structures, constants, and interfaces for
 * the Peripheral Controller Driver (PCD).
 *
 * The Peripheral Controller Driver (PCD) for Linux will implement the
 * Gadget API, so that the existing Gadget drivers can be used.	 For
 * the Mass Storage Function driver the File-backed USB Storage Gadget
 * (FBS) driver will be used.  The FBS driver supports the
 * Control-Bulk (CB), Control-Bulk-Interrupt (CBI), and Bulk-Only
 * transports.
 *
 */

/** Invalid DMA Address */
#define DMA_ADDR_INVALID	(~(dma_addr_t)0)
/** Maxpacket size for EP0 */
#define MAX_EP0_SIZE	64
/** Maxpacket size for any EP */
#define MAX_PACKET_SIZE 1024
/** Max Transfer size for any EP */
#define DDMA_MAX_TRANSFER_SIZE 65535
/** Max DMA Descriptor count for any EP */
#define MAX_DMA_DESC_CNT 64


/**
 * Get the pointer to the core_if from the pcd pointer.
 */
#define GET_CORE_IF(_pcd) (_pcd->otg_dev->core_if)

/**
 * States of EP0.
 */
enum ep0_state {
	EP0_DISCONNECT,		/* no host */
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_IN_STATUS_PHASE,
	EP0_OUT_STATUS_PHASE,
	EP0_STALL,
};

/** Fordward declaration.*/
struct dwc_otg_pcd;

/**
 * PCD EP structure.
 * This structure describes an EP, there is an array of EPs in the PCD
 * structure.
 */
struct dwc_otg_pcd_ep {
	/** USB EP data */
	struct usb_ep		ep;
	/** USB EP Descriptor */
	const struct usb_endpoint_descriptor	*desc;

	/** queue of dwc_otg_pcd_requests. */
	struct list_head	queue;
	unsigned stopped:1;
	unsigned disabling:1;
	unsigned dma:1;
	unsigned queue_sof:1;

	/** Count of pending Requests */
	unsigned request_pending;

	/** bounce buffer for unaligned accesses*/
	void *bounce_buffer;
	dma_addr_t bounce_buffer_dma;

	/** DWC_otg ep data. */
	struct dwc_ep dwc_ep;

	/** Pointer to PCD */
	struct dwc_otg_pcd *pcd;

	void *priv;
};



/** DWC_otg PCD Structure.
 * This structure encapsulates the data for the dwc_otg PCD.
 */
struct dwc_otg_pcd {
	/** USB gadget */
	struct usb_gadget gadget;
	/** USB gadget driver pointer*/
	struct usb_gadget_driver *driver;
	/** The DWC otg device pointer. */
	struct dwc_otg_device *otg_dev;

	/** State of EP0 */
	enum ep0_state	ep0state;
	/** EP0 Request is pending */
	unsigned	ep0_pending:1;
	/** Indicates when SET CONFIGURATION Request is in process */
	unsigned	request_config:1;
	/** The state of the Remote Wakeup Enable. */
	unsigned	remote_wakeup_enable:1;
	/** The state of the B-Device HNP Enable. */
	unsigned	b_hnp_enable:1;
	/** The state of A-Device HNP Support. */
	unsigned	a_hnp_support:1;
	/** The state of the A-Device Alt HNP support. */
	unsigned	a_alt_hnp_support:1;
	/** Count of pending Requests */
	unsigned	ep0_request_pending;

	/** SETUP packet for EP0
	 * This structure is allocated as a DMA buffer on PCD initialization
	 * with enough space for up to 3 setup packets.
	 */
	union {
			struct usb_ctrlrequest	req;
			u32	d32[2];
	} *setup_pkt;

	dma_addr_t setup_pkt_dma_handle;

	/** 2-byte dma buffer used to return status from GET_STATUS */
	u16 *status_buf;
	dma_addr_t status_buf_dma_handle;

	/** Array of EPs. */
	struct dwc_otg_pcd_ep ep0;
	/** Array of IN EPs. */
	struct dwc_otg_pcd_ep in_ep[MAX_EPS_CHANNELS - 1];
	/** Array of OUT EPs. */
	struct dwc_otg_pcd_ep out_ep[MAX_EPS_CHANNELS - 1];
	/** number of valid EPs in the above array. */

	spinlock_t	lock;
	/** Timer for SRP.	If it expires before SRP is successful
	 * clear the SRP. */
	struct timer_list srp_timer;

	/** Tasklet to defer starting of TEST mode transmissions until
	 *	Status Phase has been completed.
	 */
	struct tasklet_struct test_mode_tasklet;

	/** Tasklet to delay starting of xfer in DMA mode */
	struct tasklet_struct *start_xfer_tasklet;

	/** The test mode to enter when the tasklet is executed. */
	unsigned test_mode;

	struct device *dev;

};
#define to_dwc_otg_pcd(g)	(container_of((g), struct dwc_otg_pcd, gadget))


/** DWC_otg request structure.
 * This structure is a list of requests.
 */
struct dwc_otg_pcd_request {
	struct usb_request	req; /**< USB Request. */
	struct list_head	queue;	/**< queue of these requests. */
	unsigned mapped:1;
	unsigned use_bounce_buffer:1;
};


extern int __init dwc_otg_pcd_init(struct device *_dev);

extern void dwc_otg_pcd_stop(struct dwc_otg_pcd *_pcd);

/* Display the contents of the buffer */
extern void start_next_request(struct dwc_otg_pcd_ep *ep);
extern void dwc_otg_pcd_remove(struct device *_dev);
extern int dwc_otg_pcd_handle_intr(struct dwc_otg_pcd *_pcd);
extern void dwc_otg_pcd_start_srp_timer(struct dwc_otg_pcd *_pcd);
extern void dwc_otg_pcd_initiate_srp(struct dwc_otg_pcd *_pcd);
extern void dwc_otg_pcd_remote_wakeup(struct dwc_otg_pcd *_pcd, int set);
/* request functions defined in "dwc_otg_pcd.c" */
extern void dwc_otg_request_done(struct dwc_otg_pcd_ep *_ep,
				 struct dwc_otg_pcd_request *_req,
				 int _status,
				 unsigned long *irq_flags);
extern void dwc_otg_request_nuke(struct dwc_otg_pcd_ep *_ep,
				 unsigned long *irq_flags);
extern void dwc_otg_pcd_update_otg(struct dwc_otg_pcd *_pcd,
				   const unsigned _reset);


/**
 * Helper used for workaround of STAR 9000364833:
 * Title: Core Does not Respond When IN EndPoints are Randomly Disabled in
 *        Scatter/Gather DMA Device Mode
 * Impacted Configuration: Configurations with Scatter/Gather Device DMA
 *                         functionality
 * Nature of Defect: The core stops responding for IN EPs after a random EP
 *                   disable is programmed by the application for IN EPs.
 * Consequence of Defect: The core NAKs for all the IN tokens received on USB.
 *
 * Description:
 * This defect manifests as follows:
 * 1. The core is operating in Device Scatter/Gather DMA mode.
 * 2. USB reset is driven by the USB host at an arbitrary time.
 * 3. The core interrupts its application with GINTSTS.USBRst bit set.
 * 4. In response to the USB reset interrupt, the core's application programs EP
 *    disable (DIOEPCTLn[31:30] = 2'b11) for all the enabled endpoints to stop
 *    transfers on these endpoints (because USB reset is an indication that the
 *    current session is no longer valid).
 * 5. The core starts disabling the endpoints sequentially and generates
 *    corresponding EP disabled interrupt (DIOEPINTn.EPDisbld) after every EP
 *    is disabled.
 * 6. Because of this defect, the FSM of the DWC_otg_aiu_dsch.v module in the
 *    core is unable to service any IN EP after the enumeration is done
 *    (unable to proceed with IN transfers of the control transfer).
 *    OUT endpoints are serviced because they are not affected by this defect.
 *
 * Versions Affected 2.90a, 2.81a, 2.80a, 2.72a, 2.71a and 2.70a only
 *
 * Workaround:  Don't disable endpoints once enabled on effected cores, this
 * is a little nasty but testing has proved this to work. Without this change
 * the Ch9 test in the USBCV tool fail.
 */
static inline int
dwc_otg_can_disable_channel(struct dwc_otg_core_if *core_if,
			    struct dwc_ep *ep) {
	if (core_if->snpsid < OTG_CORE_REV_2_91a && ep->is_in)
		return true;
	else
		return false;
}

#endif
#endif /* DWC_HOST_ONLY */
