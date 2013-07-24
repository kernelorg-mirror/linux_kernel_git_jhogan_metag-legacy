 /* ==========================================================================
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
  * ==========================================================================*/

#ifndef DWC_HOST_ONLY

/** @file
 * This file implements the Peripheral Controller Driver.
 *
 * The Peripheral Controller Driver (PCD) is responsible for
 * translating requests from the Function Driver into the appropriate
 * actions on the DWC_otg controller. It isolates the Function Driver
 * from the specifics of the controller by providing an API to the
 * Function Driver.
 *
 * The Peripheral Controller Driver for Linux will implement the
 * Gadget API, so that the existing Gadget drivers can be used.
 * (Gadget Driver is the Linux terminology for a Function Driver.)
 *
 * The Linux Gadget API is defined in the header file
 * <linux/usb/gadget.h>.  The USB EP operations API is
 * defined in the structure usb_ep_ops and the USB
 * Controller API is defined in the structure
 * usb_gadget_ops.
 *
 * An important function of the PCD is managing interrupts generated
 * by the DWC_otg controller. The implementation of the DWC_otg device
 * mode interrupt service routines is in dwc_otg_pcd_intr.c.
 *
 * @todo Add Device Mode test modes (Test J mode, Test K mode, etc).
 * @todo Does it work when the request size is greater than DEPTSIZ
 * transfer size
 *
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "dwc_otg_driver.h"
#include "dwc_otg_pcd.h"


static int need_stop_srp_timer(struct dwc_otg_core_if *core_if)
{
	if (core_if->core_params->phy_type != DWC_PHY_TYPE_PARAM_FS ||
	    !core_if->core_params->i2c_enable)
		return core_if->srp_timer_started ? 1 : 0;
	else
		return 0;
}
/**
 * Tests if the module is set to FS or if the PHY_TYPE is FS. If so, then the
 * gadget should not report as high-speed capable.
 */
static enum usb_device_speed dwc_otg_pcd_max_speed(struct dwc_otg_pcd *pcd)
{
	struct dwc_otg_core_if *core_if = GET_CORE_IF(pcd);

	if ((core_if->core_params->speed == DWC_SPEED_PARAM_FULL) ||
	    ((core_if->hwcfg2.b.hs_phy_type == 2) &&
	     (core_if->hwcfg2.b.fs_phy_type == 1) &&
	     (core_if->core_params->ulpi_fs_ls))) {
		return USB_SPEED_FULL;
	}

	return USB_SPEED_HIGH;
}

/**
 * Tests if driver is OTG capable.
 */
static u32 dwc_otg_pcd_is_otg(struct dwc_otg_pcd *pcd)
{
	struct dwc_otg_core_if *core_if = GET_CORE_IF(pcd);
	union gusbcfg_data usbcfg = {.d32 = 0 };

	usbcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->gusbcfg);
	if (!usbcfg.b.srpcap || !usbcfg.b.hnpcap)
		return 0;

	return 1;
}

/**
 * This function completes a request.  It call's the request call back.
 */
void
dwc_otg_request_done(struct dwc_otg_pcd_ep *ep, struct dwc_otg_pcd_request *req,
		     int status, unsigned long *irq_flags)
__releases(ep->pcd->lock)
__acquires(ep->pcd->lock)
{
	unsigned stopped = ep->stopped;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, ep);

	/*
	 * The DMA engine cannot do unaligned write accesses to memory
	 * so we must use an aligned bounce buffer for these :-(
	 */
	if (req->use_bounce_buffer) {
		/* copy data out of bounce buffer */
		memcpy(req->req.buf, ep->bounce_buffer, req->req.length);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
		req->use_bounce_buffer = 0;
	} else {

		if (req->mapped) {
			dma_unmap_single(ep->pcd->gadget.dev.parent,
				req->req.dma, req->req.length,
				ep->dwc_ep.is_in
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
			req->req.dma = DMA_ADDR_INVALID;
			req->mapped = 0;
		} else
			dma_sync_single_for_cpu(ep->pcd->gadget.dev.parent,
				req->req.dma, req->req.length,
				ep->dwc_ep.is_in
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
	}

	list_del_init(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	if (in_interrupt()) {
		spin_unlock(&ep->pcd->lock);
		req->req.complete(&ep->ep, &req->req);
		spin_lock(&ep->pcd->lock);

	} else {
		spin_unlock_irqrestore(&ep->pcd->lock, *irq_flags);
		req->req.complete(&ep->ep, &req->req);
		spin_lock_irqsave(&ep->pcd->lock, *irq_flags);
	}
	if (ep->dwc_ep.num == 0) {
		if (ep->pcd->ep0_request_pending > 0)
			--ep->pcd->ep0_request_pending;
	} else {
		if (ep->request_pending > 0)
			--ep->request_pending;
	}
	ep->stopped = stopped;

#ifdef CONFIG_405EZ
	/*
	 * Added-sr: 2007-07-26
	 *
	 * Finally, when the current request is done, mark this endpoint
	 * as not active, so that new requests can be processed.
	 */
	ep->dwc_ep.active = 0;
#endif
}

/**
 * This function terminates all the requsts in the EP request queue.
 */
void dwc_otg_request_nuke(struct dwc_otg_pcd_ep *ep, unsigned long *irq_flags)
{
	struct dwc_otg_pcd_request *req;
	ep->stopped = 1;

	/* called with irqs blocked ? - NJ: yes we now pass the flags in*/
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct dwc_otg_pcd_request,
				queue);
		dwc_otg_request_done(ep, req, -ESHUTDOWN, irq_flags);
	}
}

/**
 * This function assigns periodic Tx FIFO to an periodic EP
 * in shared Tx FIFO mode
 */
static u32 assign_periodic_tx_fifo(struct dwc_otg_core_if *core_if)
{
	u32 mask = 1;
	u32 i;

	for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep; ++i) {
		if (!(mask & core_if->p_tx_msk)) {
			core_if->p_tx_msk |= mask;
			return i + 1;
		}
		mask <<= 1;
	}
	return 0;
}

/**
 * This function releases periodic Tx FIFO
 * in shared Tx FIFO mode
 */
static void release_periodic_tx_fifo(struct dwc_otg_core_if *core_if,
				  u32 fifo_num)
{
	core_if->p_tx_msk =
		(core_if->p_tx_msk & (1 << (fifo_num - 1))) ^ core_if->p_tx_msk;
}

/**
 * This function assigns periodic Tx FIFO to an periodic EP
 * in shared Tx FIFO mode
 */
static u32 assign_tx_fifo(struct dwc_otg_core_if *core_if)
{
	u32 mask = 1;
	u32 i;

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; ++i) {
		if (!(mask & core_if->tx_msk)) {
			core_if->tx_msk |= mask;
			return i + 1;
		}
		mask <<= 1;
	}
	return 0;
}

/**
 * This function releases periodic Tx FIFO
 * in shared Tx FIFO mode
 */
static void release_tx_fifo(struct dwc_otg_core_if *core_if, u32 fifo_num)
{
	core_if->tx_msk =
		(core_if->tx_msk & (1 << (fifo_num - 1))) ^ core_if->tx_msk;
}

/**
 *This function activates an EP.  The Device EP control register for
 *the EP is configured as defined in the ep structure.	 Note: This
 *function is not used for EP0.
 */
void dwc_otg_ep_activate(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	union depctl_data depctl;
	u32 __iomem *addr;
	union daint_data daintmsk = {.d32 = 0};
	DWC_DEBUGPL(DBG_PCDV, "%s() EP%d-%s\n", __func__, ep->num,
		      (ep->is_in ? "IN" : "OUT"));

	/*Read DEPCTLn register */
	if (ep->is_in == 1) {
		addr = &dev_if->in_ep_regs[ep->num]->diepctl;
		daintmsk.ep.in = 1 << ep->num;
	} else {
		addr = &dev_if->out_ep_regs[ep->num]->doepctl;
		daintmsk.ep.out = 1 << ep->num;
	}

	/*
	 * If the EP is already active don't change the EP Control
	 * register.
	 */
	depctl.d32 = dwc_read_reg32(addr);
	if (!depctl.b.usbactep) {
		depctl.b.mps = ep->maxpacket;
		depctl.b.eptype = ep->type;
		depctl.b.txfnum = ep->tx_fifo_num;
		depctl.b.setd0pid = 1;
		depctl.b.usbactep = 1;
		dwc_write_reg32(addr, depctl.d32);
		DWC_DEBUGPL(DBG_PCDV, "DEPCTL=%08x\n", dwc_read_reg32(addr));
	}

	/*Enable the Interrupt for this EP */
	if (core_if->multiproc_int_enable) {
		if (ep->is_in == 1) {
			union diepint_data diepmsk = {.d32 = 0 };
			diepmsk.b.xfercompl = 1;
			diepmsk.b.timeout = 1;
			diepmsk.b.epdisabled = 1;
			diepmsk.b.ahberr = 1;
			diepmsk.b.intknepmis = 1;
			diepmsk.b.txfifoundrn = 1;

			if (core_if->dma_desc_enable)
				diepmsk.b.bna = 1;
#if 0
			if (core_if->dma_enable)
				doepmsk.b.nak = 1;
#endif
			dwc_write_reg32(&dev_if->dev_global_regs->
					diepeachintmsk[ep->num], diepmsk.d32);

		} else {
			union doepint_data doepmsk = {.d32 = 0 };
			doepmsk.b.xfercompl = 1;
			doepmsk.b.ahberr = 1;
			doepmsk.b.epdisabled = 1;

			if (core_if->dma_desc_enable)
				doepmsk.b.bna = 1;
#if 0
			doepmsk.b.babble = 1;
			doepmsk.b.nyet = 1;
			doepmsk.b.nak = 1;
#endif
			dwc_write_reg32(&dev_if->dev_global_regs->
					doepeachintmsk[ep->num], doepmsk.d32);
		}
		dwc_modify_reg32(&dev_if->dev_global_regs->deachintmsk,
				 0, daintmsk.d32);
	} else {
		dwc_modify_reg32(&dev_if->dev_global_regs->daintmsk,
				 0, daintmsk.d32);
	}
	DWC_DEBUGPL(DBG_PCDV, "DAINTMSK=%0x\n",
		      dwc_read_reg32(&dev_if->dev_global_regs->daintmsk));
	ep->stall_clear_flag = 0;
	return;
}

static int dwc_otg_pcd_ep_enable(struct usb_ep *_ep,
				 const struct usb_endpoint_descriptor *_desc)
{
	struct dwc_otg_pcd_ep *ep = NULL;
	struct dwc_otg_pcd *pcd = NULL;
	unsigned long flags;
	int retval = 0;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p)\n", __func__, ep, _desc);

	ep = container_of(_ep, struct dwc_otg_pcd_ep, ep);

	if (!_ep || !_desc || ep->desc
	     || _desc->bDescriptorType != USB_DT_ENDPOINT) {
		DWC_WARN("%s, bad ep or descriptor\n", __func__);
		return -EINVAL;
	}
	if (ep == &ep->pcd->ep0) {
		DWC_WARN("%s, bad ep(0)\n", __func__);
		return -EINVAL;
	}

	/* Check FIFO size? */
	if (!_desc->wMaxPacketSize) {
		DWC_WARN("%s, bad %s maxpacket\n", __func__, _ep->name);
		return -ERANGE;
	}
	pcd = ep->pcd;
	if (!pcd->driver || pcd->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_WARN("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&pcd->lock, flags);

	/*
	 * Activate the EP
	 */
	ep->desc = _desc;
	ep->ep.maxpacket = le16_to_cpu(_desc->wMaxPacketSize);
	ep->stopped = 0;
	ep->dwc_ep.is_in = (USB_DIR_IN & _desc->bEndpointAddress) != 0;
	ep->dwc_ep.maxpacket = ep->ep.maxpacket;
	ep->dwc_ep.type = _desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	if (ep->dwc_ep.is_in) {
		if (!pcd->otg_dev->core_if->en_multiple_tx_fifo) {
			ep->dwc_ep.tx_fifo_num = 0;
			if ((_desc->bmAttributes &
					USB_ENDPOINT_XFERTYPE_MASK) ==
						USB_ENDPOINT_XFER_ISOC) {
				/*
				 * if ISOC EP then assign a Periodic Tx FIFO.
				 */
				ep->dwc_ep.tx_fifo_num =
					assign_periodic_tx_fifo(pcd->
								otg_dev->
								core_if);
			}
		} else {
			/*
			 * if Dedicated FIFOs mode is on then assign a Tx FIFO.
			 */
			ep->dwc_ep.tx_fifo_num =
				assign_tx_fifo(pcd->otg_dev->core_if);
		}
	}

	/* Set initial data PID. */
	if ((_desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_BULK)
		ep->dwc_ep.data_pid_start = 0;


	DWC_DEBUGPL(DBG_PCD, "Activate %s-%s: type=%d, mps=%d desc=%p\n",
			ep->ep.name, (ep->dwc_ep.is_in ? "IN" : "OUT"),
			ep->dwc_ep.type, ep->dwc_ep.maxpacket, ep->desc);
	dwc_otg_ep_activate(GET_CORE_IF(pcd), &ep->dwc_ep);
	spin_unlock_irqrestore(&pcd->lock, flags);

	return retval;
}

/**
 *This function deactivates an EP. This is done by clearing the USB Active
 *EP bit in the Device EP control register.  Note: This function is not used
 *for EP0. EP0 cannot be deactivated.
 *
 */
void dwc_otg_ep_deactivate(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	union depctl_data depctl = {.d32 = 0};
	u32 __iomem *addr;
	union daint_data daintmsk = {.d32 = 0};

	/*Read DEPCTLn register */
	if (ep->is_in) {
		addr = &core_if->dev_if->in_ep_regs[ep->num]->diepctl;
		/*don't mask out interrupt if not disabling channel*/
		if (dwc_otg_can_disable_channel(core_if, ep))
			daintmsk.ep.in = 1 << ep->num;
	} else {
		addr = &core_if->dev_if->out_ep_regs[ep->num]->doepctl;
		daintmsk.ep.out = 1 << ep->num;
	}


	/*Disable the Interrupt for this EP */
	if (core_if->multiproc_int_enable) {
		dwc_modify_reg32(&core_if->dev_if->dev_global_regs->deachintmsk,
				 daintmsk.d32, 0);

		if (ep->is_in == 1) {
			/*don't mask out interrupt if not disabling channel*/
			if (dwc_otg_can_disable_channel(core_if, ep)) {
				dwc_write_reg32(&core_if->dev_if->
						dev_global_regs->
						diepeachintmsk[ep->num], 0);
			}
		} else {
			dwc_write_reg32(&core_if->dev_if->dev_global_regs->
					doepeachintmsk[ep->num], 0);
		}
	} else
		dwc_modify_reg32(&core_if->dev_if->dev_global_regs->daintmsk,
				 daintmsk.d32, 0);

	depctl.d32 = dwc_read_reg32(addr);

	depctl.b.usbactep = 0;

	if (core_if->dma_desc_enable &&
	    dwc_otg_can_disable_channel(core_if, ep))
		depctl.b.epdis = 1;

	dwc_write_reg32(addr, depctl.d32);
}

/**
 * This function is called when an EP is disabled due to disconnect or
 * change in configuration. Any pending requests will terminate with a
 * status of -ESHUTDOWN.
 *
 * This function modifies the dwc_otg_ep_t data structure for this EP,
 * and then calls dwc_otg_ep_deactivate.
 */
static int dwc_otg_pcd_ep_disable(struct usb_ep *_ep)
{
	struct dwc_otg_pcd_ep *ep;
	unsigned long flags;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _ep);

	ep = container_of(_ep, struct dwc_otg_pcd_ep, ep);

	if (!_ep || !ep->desc) {
		DWC_DEBUGPL(DBG_PCD, "%s, %s not enabled\n", __func__,
			     _ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->pcd->lock, flags);

	dwc_otg_request_nuke(ep, &flags);
	dwc_otg_ep_deactivate(GET_CORE_IF(ep->pcd), &ep->dwc_ep);
	ep->desc = NULL;
	ep->stopped = 1;
	if (ep->dwc_ep.is_in) {
		dwc_otg_flush_tx_fifo(GET_CORE_IF(ep->pcd),
				ep->dwc_ep.tx_fifo_num);
		release_periodic_tx_fifo(GET_CORE_IF(ep->pcd),
				ep->dwc_ep.tx_fifo_num);
		release_tx_fifo(GET_CORE_IF(ep->pcd),
				ep->dwc_ep.tx_fifo_num);
	}

	spin_unlock_irqrestore(&ep->pcd->lock, flags);

	DWC_DEBUGPL(DBG_PCD, "%s disabled\n", _ep->name);

	return 0;
}

/**
 * This function allocates a request object to use with the specified
 * endpoint.
 */
static struct usb_request *dwc_otg_pcd_alloc_request(struct usb_ep *ep,
						     gfp_t gfp_flags)
{
	struct dwc_otg_pcd_request *req;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%d)\n", __func__, ep, gfp_flags);
	if (!ep) {
		DWC_WARN("%s() %s\n", __func__, "Invalid EP!\n");
		return NULL;
	}
	req = kzalloc(sizeof(struct dwc_otg_pcd_request), gfp_flags);
	if (!req) {
		DWC_WARN("%s() %s\n", __func__, "request allocation failed!\n");
		return NULL;
	}

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

/**
 * This function frees a request object.
 */
static void dwc_otg_pcd_free_request(struct usb_ep *_ep,
				     struct usb_request *_req)
{
	struct dwc_otg_pcd_request *req;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p)\n", __func__, _ep, _req);
	if (!_ep || !_req) {
		DWC_WARN("%s() %s\n", __func__,
				"Invalid ep or req argument!\n");
		return;
	}
	req = container_of(_req, struct dwc_otg_pcd_request, req);
	kfree(req);
}

/**
 *This function initializes dma descriptor chain.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP to start the transfer on.
 */
static void
init_dma_desc_chain(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	struct dwc_otg_dev_dma_desc *dma_desc;
	u32 offset;
	u32 xfer_est;
	int i;

	ep->desc_cnt = (ep->total_len / ep->maxxfer) +
		((ep->total_len % ep->maxxfer) ? 1 : 0);
	if (!ep->desc_cnt)
		ep->desc_cnt = 1;

	dma_desc = ep->desc_addr;
	xfer_est = ep->total_len;
	offset = 0;
	for (i = 0; i < ep->desc_cnt; ++i) {
		/**DMA Descriptor Setup */
		if (xfer_est > ep->maxxfer) {
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 0;
			dma_desc->status.b.ioc = 0;
			dma_desc->status.b.sp = 0;
			dma_desc->status.b.bytes = ep->maxxfer;
			dma_desc->buf = ep->dma_addr + offset;
			dma_desc->status.b.bs = BS_HOST_READY;

			xfer_est -= ep->maxxfer;
			offset += ep->maxxfer;
		} else {
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			if (ep->is_in) {
				dma_desc->status.b.sp =
				    (xfer_est %
				     ep->maxpacket) ? 1 : ((ep->
							    sent_zlp) ? 1 : 0);
				dma_desc->status.b.bytes = xfer_est;
			} else {
				dma_desc->status.b.bytes =
				    xfer_est + ((4 - (xfer_est & 0x3)) & 0x3);
			}

			dma_desc->buf = ep->dma_addr + offset;
			dma_desc->status.b.bs = BS_HOST_READY;
		}
		BUG_ON((dma_desc->buf & 0x3) && !ep->is_in);
		dma_desc++;
	}
	wmb();

}

/**
 *This function does the setup for a data transfer for an EP and
 *starts the transfer.	 For an IN transfer, the packets will be
 *loaded into the appropriate Tx FIFO in the ISR. For OUT transfers,
 *the packets are unloaded from the Rx FIFO in the ISR.  the ISR.
 *
 */
void
dwc_otg_ep_start_transfer(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	union depctl_data depctl;
	union deptsiz_data deptsiz;
	union gintmsk_data intr_mask = {.d32 = 0};

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s()\n", __func__);
	DWC_DEBUGPL(DBG_PCD, "ep%d-%s xfer_len=%d xfer_cnt=%d "
		      "xfer_buff=%p start_xfer_buff=%p\n", ep->num,
		      (ep->is_in ? "IN" : "OUT"), ep->xfer_len,
		      ep->xfer_count, ep->xfer_buff, ep->start_xfer_buff);

	if (core_if->dma_desc_enable) {
		union doepint_data doepmsk = {.d32 = 0};
		doepmsk.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->doepmsk);
		doepmsk.b.bna = 1;
		dwc_write_reg32(&core_if->dev_if->dev_global_regs->doepmsk, doepmsk.d32);
	}

	/*IN endpoint */
	if (ep->is_in) {
		struct dwc_otg_dev_in_ep_regs __iomem *in_regs =
			core_if->dev_if->in_ep_regs[ep->num];
		union gnptxsts_data gtxstatus;
		gtxstatus.d32 =
			dwc_read_reg32(&core_if->core_global_regs->gnptxsts);
		if (core_if->en_multiple_tx_fifo == 0 &&
			gtxstatus.b.nptxqspcavail == 0) {
#ifdef DEBUG
			DWC_PRINT("TX Queue Full (0x%0x)\n", gtxstatus.d32);
#endif	/* */
		    return;
		}
		depctl.d32 = dwc_read_reg32(&(in_regs->diepctl));
		deptsiz.d32 = dwc_read_reg32(&(in_regs->dieptsiz));

		ep->xfer_len += (ep->maxxfer < (ep->total_len - ep->xfer_len)) ?
				ep->maxxfer : (ep->total_len - ep->xfer_len);
		/*Zero Length Packet? */
		if ((ep->xfer_len - ep->xfer_count) == 0) {
			deptsiz.b.xfersize = 0;
			deptsiz.b.pktcnt = 1;
		} else {
			/*Program the transfer size and packet count
			 *     as follows: xfersize = N *maxpacket +
			 *     short_packet pktcnt = N + (short_packet
			 *     exist ? 1 : 0)
			 */
			deptsiz.b.xfersize = ep->xfer_len - ep->xfer_count;
			deptsiz.b.pktcnt =
				(ep->xfer_len - ep->xfer_count - 1 +
				ep->maxpacket) / ep->maxpacket;
		}
#ifdef CONFIG_405EZ
		/*
		 *Added-sr: 2007-07-26
		 *
		 *Since the 405EZ (Ultra) only support 2047 bytes as
		 *max transfer size, we have to split up bigger transfers
		 *into multiple transfers of 1024 bytes sized messages.
		 *I happens often, that transfers of 4096 bytes are
		 *required (zero-gadget, file_storage-gadget).
		 */
		if (ep->xfer_len > MAX_XFER_LEN) {
			ep->bytes_pending = ep->xfer_len - MAX_XFER_LEN;
			ep->xfer_len = MAX_XFER_LEN;
		}
#endif

		/*Write the DMA register */
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				dwc_write_reg32(&in_regs->dieptsiz,
						deptsiz.d32);
				dwc_write_reg32(&(in_regs->diepdma),
						(u32) ep->dma_addr);
			} else {
				init_dma_desc_chain(core_if, ep);
				/**DIEPDMAn Register write */
				dwc_write_reg32(&in_regs->diepdma,
						ep->dma_desc_addr);
			}
		} else {
			dwc_write_reg32(&in_regs->dieptsiz, deptsiz.d32);
			if (ep->type != USB_ENDPOINT_XFER_ISOC) {
				/**
				 * Enable the Non-Periodic Tx FIFO empty
				 * interrupt, or the Tx FIFO empty
				 * interrupt in dedicated Tx FIFO mode,
				 * the data will be written into the fifo
				 * by the ISR.
				 */
				if (core_if->en_multiple_tx_fifo == 0) {
					intr_mask.b.nptxfempty = 1;
					dwc_modify_reg32(&core_if->
							 core_global_regs->
							 gintmsk, intr_mask.d32,
							 intr_mask.d32);
				} else {
				    /*
				     * Enable the Tx FIFO Empty Interrupt
				     * for this EP
				     */
				    if (ep->xfer_len > 0) {
						u32 fifoemptymsk = 0;
						fifoemptymsk = 1 << ep->num;
						dwc_modify_reg32(&core_if->
								 dev_if->
								 dev_global_regs->
								 dtknqr4_fifoemptymsk,
								 0,
								 fifoemptymsk);
					}
				}
			}
		}

		/*EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		wmb();
		dwc_write_reg32(&in_regs->diepctl, depctl.d32);

		depctl.d32 =
			dwc_read_reg32(&core_if->dev_if->in_ep_regs[0]->diepctl);
		depctl.b.nextep = ep->num;

		wmb();
		dwc_write_reg32(&core_if->dev_if->in_ep_regs[0]->diepctl,
				depctl.d32);

	} else {
		/*OUT endpoint */
		struct dwc_otg_dev_out_ep_regs __iomem *out_regs =
			core_if->dev_if->out_ep_regs[ep->num];
		depctl.d32 = dwc_read_reg32(&(out_regs->doepctl));
		deptsiz.d32 = dwc_read_reg32(&(out_regs->doeptsiz));

		ep->xfer_len += (ep->maxxfer < (ep->total_len - ep->xfer_len)) ?
				ep->maxxfer : (ep->total_len - ep->xfer_len);
		/*
		 * Program the transfer size and packet count as follows:
		 *
		 *     pktcnt = N
		 *     xfersize = N *maxpacket
		 */
		if ((ep->xfer_len - ep->xfer_count) == 0) {
			/*Zero Length Packet */
			deptsiz.b.xfersize = ep->maxpacket;
			deptsiz.b.pktcnt = 1;
		} else {
			deptsiz.b.pktcnt =
				(ep->xfer_len - ep->xfer_count +
				(ep->maxpacket - 1)) / ep->maxpacket;
			ep->xfer_len =
				deptsiz.b.pktcnt * ep->maxpacket
				+ ep->xfer_count;

			deptsiz.b.xfersize = ep->xfer_len - ep->xfer_count;
		}

		DWC_DEBUGPL(DBG_PCDV, "ep%d xfersize=%d pktcnt=%d\n",
			      ep->num, deptsiz.b.xfersize, deptsiz.b.pktcnt);
		if (core_if->dma_enable) {
			if (!core_if->dma_desc_enable) {
				dwc_write_reg32(&out_regs->doeptsiz,
						deptsiz.d32);

				dwc_write_reg32(&(out_regs->doepdma),
						(u32) ep->dma_addr);
			} else {
				init_dma_desc_chain(core_if, ep);

				/**DOEPDMAn Register write */
				dwc_write_reg32(&out_regs->doepdma,
						ep->dma_desc_addr);

			}
		} else
			dwc_write_reg32(&out_regs->doeptsiz, deptsiz.d32);

		if (ep->type == USB_ENDPOINT_XFER_ISOC) {
			/**@todo NGS: dpid is read-only. Use setd0pid
			 *or setd1pid. */
			if (ep->even_odd_frame)
				depctl.b.setd1pid = 1;
			else
				depctl.b.setd0pid = 1;
		}

		/*EP enable */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;

		wmb();
		dwc_write_reg32(&out_regs->doepctl, depctl.d32);
		DWC_DEBUGPL(DBG_PCD, "DOEPCTL=%08x DOEPTSIZ=%08x\n",
			dwc_read_reg32(&out_regs->doepctl),
			dwc_read_reg32(&out_regs->doeptsiz));
		DWC_DEBUGPL(DBG_PCD, "DAINTMSK=%08x GINTMSK=%08x\n",
			dwc_read_reg32(&core_if->dev_if->dev_global_regs->daintmsk),
			dwc_read_reg32(&core_if->core_global_regs->gintmsk));
	}
}

/**
 *This function setup a zero length transfer in Buffer DMA and
 *Slave modes for usb requests with zero field set
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP to start the transfer on.
 *
 */
void dwc_otg_ep_start_zl_transfer(struct dwc_otg_core_if *core_if,
		struct dwc_ep *ep)
{

	union depctl_data depctl;
	union deptsiz_data deptsiz;
	union gintmsk_data intr_mask = {.d32 = 0 };

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s()\n", __func__);
	DWC_PRINT("zero length transfer is called\n");

	/*IN endpoint */
	if (ep->is_in == 1) {
		struct dwc_otg_dev_in_ep_regs __iomem *in_regs =
		    core_if->dev_if->in_ep_regs[ep->num];

		depctl.d32 = dwc_read_reg32(&(in_regs->diepctl));
		deptsiz.d32 = dwc_read_reg32(&(in_regs->dieptsiz));

		deptsiz.b.xfersize = 0;
		deptsiz.b.pktcnt = 1;

		/*Write the DMA register */
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				dwc_write_reg32(&in_regs->dieptsiz,
						deptsiz.d32);
				dwc_write_reg32(&(in_regs->diepdma),
						(u32) ep->dma_addr);
			}
		} else {
			dwc_write_reg32(&in_regs->dieptsiz, deptsiz.d32);
			/**
			 * Enable the Non-Periodic Tx FIFO empty interrupt,
			 * or the Tx FIFO epmty interrupt in dedicated Tx FIFO
			 * mode, the data will be written into the fifo by the
			 * ISR.
			 */
			if (core_if->en_multiple_tx_fifo == 0) {
				intr_mask.b.nptxfempty = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gintmsk, intr_mask.d32,
						 intr_mask.d32);
			} else {
				/*
				 * Enable the Tx FIFO Empty Interrupt
				 * for this EP
				 */
				if (ep->xfer_len > 0) {
					u32 fifoemptymsk = 0;
					fifoemptymsk = 1 << ep->num;
					dwc_modify_reg32(&core_if->dev_if->
							 dev_global_regs->
							 dtknqr4_fifoemptymsk,
							 0, fifoemptymsk);
				}
			}
		}

		/*EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		wmb();
		dwc_write_reg32(&in_regs->diepctl, depctl.d32);

		depctl.d32 =
		    dwc_read_reg32(&core_if->dev_if->in_ep_regs[0]->diepctl);
		depctl.b.nextep = ep->num;
		wmb();
		dwc_write_reg32(&core_if->dev_if->in_ep_regs[0]->diepctl,
				depctl.d32);

	} else {
		/*OUT endpoint */
		struct dwc_otg_dev_out_ep_regs __iomem *out_regs =
		    core_if->dev_if->out_ep_regs[ep->num];

		depctl.d32 = dwc_read_reg32(&(out_regs->doepctl));
		deptsiz.d32 = dwc_read_reg32(&(out_regs->doeptsiz));

		/*Zero Length Packet */
		deptsiz.b.xfersize = ep->maxpacket;
		deptsiz.b.pktcnt = 1;

		if (core_if->dma_enable) {
			if (!core_if->dma_desc_enable) {
				dwc_write_reg32(&out_regs->doeptsiz,
						deptsiz.d32);

				dwc_write_reg32(&(out_regs->doepdma),
						(u32) ep->dma_addr);
			}
		} else {
			dwc_write_reg32(&out_regs->doeptsiz, deptsiz.d32);
		}

		/*EP enable */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;

		wmb();
		dwc_write_reg32(&out_regs->doepctl, depctl.d32);

	}
}

/**
 *This function does the setup for a data transfer for EP0 and starts
 *the transfer.  For an IN transfer, the packets will be loaded into
 *the appropriate Tx FIFO in the ISR. For OUT transfers, the packets are
 *unloaded from the Rx FIFO in the ISR.
 */
void
dwc_otg_ep0_start_transfer(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	union depctl_data depctl;
	union deptsiz0_data deptsiz;
	union gintmsk_data intr_mask = {.d32 = 0};
	struct dwc_otg_dev_dma_desc *dma_desc;
	DWC_DEBUGPL(DBG_PCD, "ep%d-%s xfer_len=%d xfer_cnt=%d "
		      "xfer_buff=%p start_xfer_buff=%p total_len=%d\n",
		      ep->num, (ep->is_in ? "IN" : "OUT"), ep->xfer_len,
		      ep->xfer_count, ep->xfer_buff, ep->start_xfer_buff,
		      ep->total_len);
	ep->total_len = ep->xfer_len;

	/*IN endpoint */
	if (ep->is_in) {
		struct dwc_otg_dev_in_ep_regs __iomem *in_regs =
			core_if->dev_if->in_ep_regs[0];
		union gnptxsts_data gtxstatus;
		gtxstatus.d32 =
			dwc_read_reg32(&core_if->core_global_regs->gnptxsts);
		if (core_if->en_multiple_tx_fifo == 0 &&
			gtxstatus.b.nptxqspcavail == 0) {
#ifdef DEBUG
			deptsiz.d32 = dwc_read_reg32(&in_regs->dieptsiz);
			DWC_DEBUGPL(DBG_PCD, "DIEPCTL0=%0x\n",
				     dwc_read_reg32(&in_regs->diepctl));
			DWC_DEBUGPL(DBG_PCD, "DIEPTSIZ0=%0x (sz=%d, pcnt=%d)\n",
				     deptsiz.d32, deptsiz.b.xfersize,
				     deptsiz.b.pktcnt);
			DWC_PRINT("TX Queue or FIFO Full (0x%0x)\n",
					gtxstatus.d32);
#endif	/* */
			printk(KERN_DEBUG"TX Queue or FIFO Full!!!!\n");
			return;
		}
		depctl.d32 = dwc_read_reg32(&in_regs->diepctl);
		deptsiz.d32 = dwc_read_reg32(&in_regs->dieptsiz);

		/*Zero Length Packet? */
		if (ep->xfer_len == 0) {
			deptsiz.b.xfersize = 0;
			deptsiz.b.pktcnt = 1;
		} else {
			/*Program the transfer size and packet count
			*     as follows: xfersize = N *maxpacket +
			*     short_packet pktcnt = N + (short_packet
			*     exist ? 1 : 0)
			*/
			if (ep->xfer_len > ep->maxpacket) {
				ep->xfer_len = ep->maxpacket;
				deptsiz.b.xfersize = ep->maxpacket;
			} else
				deptsiz.b.xfersize = ep->xfer_len;

			deptsiz.b.pktcnt = 1;
		}
		DWC_DEBUGPL(DBG_PCDV,
			    "IN len=%d  xfersize=%d pktcnt=%d [%08x]\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt,
			    deptsiz.d32);

		/*Write the DMA register */
		if (core_if->dma_enable) {
			if (core_if->dma_desc_enable == 0) {
				dwc_write_reg32(&in_regs->dieptsiz,
						deptsiz.d32);

				dwc_write_reg32(&(in_regs->diepdma),
						(u32) ep->dma_addr);
			} else {
				dma_desc = core_if->dev_if->in_desc_addr;

				/**DMA Descriptor Setup */
				dma_desc->status.b.bs = BS_HOST_BUSY;
				dma_desc->status.b.l = 1;
				dma_desc->status.b.ioc = 1;
				dma_desc->status.b.sp =
				    (ep->xfer_len == ep->maxpacket) ? 0 : 1;
				dma_desc->status.b.bytes = ep->xfer_len;
				dma_desc->buf = ep->dma_addr;
				dma_desc->status.b.bs = BS_HOST_READY;

				wmb();

				/**DIEPDMA0 Register write */
				dwc_write_reg32(&in_regs->diepdma,
						core_if->dev_if->
						dma_in_desc_addr);
			}
		} else {
			dwc_write_reg32(&in_regs->dieptsiz, deptsiz.d32);
		}

		/*EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		wmb();
		dwc_write_reg32(&in_regs->diepctl, depctl.d32);

		/**
		 *Enable the Non-Periodic Tx FIFO empty interrupt, the
		 *data will be written into the fifo by the ISR.
		 */
		if (!core_if->dma_enable) {
			if (core_if->en_multiple_tx_fifo == 0) {
				intr_mask.b.nptxfempty = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gintmsk, intr_mask.d32,
						 intr_mask.d32);
			} else {
				/*Enable the Tx FIFO Empty Int for this EP */
				if (ep->xfer_len > 0) {
					u32 fifoemptymsk = 0;
					fifoemptymsk |= 1 << ep->num;
					dwc_modify_reg32(&core_if->dev_if->
							 dev_global_regs->
							 dtknqr4_fifoemptymsk,
						 0, fifoemptymsk);
				}
			}
		}
	} else {
		/*OUT endpoint */
		struct dwc_otg_dev_out_ep_regs __iomem *out_regs =
			core_if->dev_if->out_ep_regs[0];
		depctl.d32 = dwc_read_reg32(&out_regs->doepctl);
		deptsiz.d32 = dwc_read_reg32(&out_regs->doeptsiz);

		/*
		 * Program the transfer size and packet count as follows:
		 *     xfersize = N *(maxpacket + 4 - (maxpacket % 4))
		 *     pktcnt = N
		 */

		/*Zero Length Packet */
		deptsiz.b.xfersize = ep->maxpacket;
		deptsiz.b.pktcnt = 1;
		DWC_DEBUGPL(DBG_PCDV, "len=%d  xfersize=%d pktcnt=%d\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt);

		if (core_if->dma_enable) {
			if (!core_if->dma_desc_enable) {
				dwc_write_reg32(&out_regs->doeptsiz,
						deptsiz.d32);

				dwc_write_reg32(&(out_regs->doepdma),
						(u32) ep->dma_addr);
			} else {
				dma_desc = core_if->dev_if->out_desc_addr;

				/**DMA Descriptor Setup */
				dma_desc->status.b.bs = BS_HOST_BUSY;
				dma_desc->status.b.l = 1;
				dma_desc->status.b.ioc = 1;
				dma_desc->status.b.bytes = ep->maxpacket;
				dma_desc->buf = ep->dma_addr;
				dma_desc->status.b.bs = BS_HOST_READY;

				wmb();

				dwc_write_reg32(&out_regs->doepdma,
						core_if->dev_if->
						dma_out_desc_addr);
			}
		} else
			dwc_write_reg32(&out_regs->doeptsiz, deptsiz.d32);

		/*EP enable */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		wmb();
		dwc_write_reg32(&(out_regs->doepctl), depctl.d32);
	}
}

/**
 *This function continues control IN transfers started by
 *dwc_otg_ep0_start_transfer, when the transfer does not fit in a
 *single packet.  NOTE: The DIEPCTL0/DOEPCTL0 registers only have one
 *bit for the packet count.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP0 data.
 */
void dwc_otg_ep0_continue_transfer(struct dwc_otg_core_if *core_if,
				   struct dwc_ep *ep)
{
	union depctl_data depctl;
	union deptsiz0_data deptsiz;
	union gintmsk_data intr_mask = {.d32 = 0};
	struct dwc_otg_dev_dma_desc *dma_desc;
	if (ep->is_in == 1) {
		struct dwc_otg_dev_in_ep_regs __iomem *in_regs =
			core_if->dev_if->in_ep_regs[0];
		union gnptxsts_data tx_status = {.d32 = 0};
		tx_status.d32 =
			dwc_read_reg32(&core_if->core_global_regs->gnptxsts);

		/*
		 * @todo Should there be check for room in the Tx
		 * Status Queue.  If not remove the code above this comment.
		 * */
		depctl.d32 = dwc_read_reg32(&in_regs->diepctl);
		deptsiz.d32 = dwc_read_reg32(&in_regs->dieptsiz);

		/*
		 * Program the transfer size and packet count
		 * as follows: xfersize = N *maxpacket +
		 * short_packet pktcnt = N + (short_packet
		 * exist ? 1 : 0)
		 */
		if (core_if->dma_desc_enable == 0) {
			deptsiz.b.xfersize =
				(ep->total_len - ep->xfer_count) > ep->maxpacket
					?
					ep->maxpacket
					:
					(ep->total_len - ep->xfer_count);

			deptsiz.b.pktcnt = 1;
			if (core_if->dma_enable == 0)
				ep->xfer_len += deptsiz.b.xfersize;
			else
				ep->xfer_len = deptsiz.b.xfersize;

			dwc_write_reg32(&in_regs->dieptsiz, deptsiz.d32);
		} else {
			ep->xfer_len =
				(ep->total_len - ep->xfer_count) > ep->maxpacket
					?
					ep->maxpacket
					:
					(ep->total_len - ep->xfer_count);

			dma_desc = core_if->dev_if->in_desc_addr;

			/**DMA Descriptor Setup */
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			dma_desc->status.b.sp =
			    (ep->xfer_len == ep->maxpacket) ? 0 : 1;
			dma_desc->status.b.bytes = ep->xfer_len;
			dma_desc->buf = ep->dma_addr;
			dma_desc->status.b.bs = BS_HOST_READY;

			wmb();

			/**DIEPDMA0 Register write */
			dwc_write_reg32(&in_regs->diepdma,
					core_if->dev_if->dma_in_desc_addr);
		}
		DWC_DEBUGPL(DBG_PCDV, "IN len=%d  xfersize=%d "
				"pktcnt=%d [%08x]\n",
				ep->xfer_len, deptsiz.b.xfersize,
				deptsiz.b.pktcnt, deptsiz.d32);

		/*Write the DMA register */
		if (core_if->hwcfg2.b.architecture == DWC_INT_DMA_ARCH) {
			if (core_if->dma_desc_enable == 0)
				dwc_write_reg32(&(in_regs->diepdma),
						(u32) ep->dma_addr);
		}

		/*EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;
		wmb();
		dwc_write_reg32(&in_regs->diepctl, depctl.d32);

		/**
		 *Enable the Non-Periodic Tx FIFO empty interrupt, the
		 *data will be written into the fifo by the ISR.
		 */
		if (!core_if->dma_enable) {
			if (core_if->en_multiple_tx_fifo == 0) {
				/*First clear it from GINTSTS */
				intr_mask.b.nptxfempty = 1;
				dwc_modify_reg32(&core_if->core_global_regs->
						 gintmsk, intr_mask.d32,
						 intr_mask.d32);

			} else {
				/*Enable the Tx FIFO Empty Int for this EP */
				if (ep->xfer_len > 0) {
					u32 fifoemptymsk = 0;
					fifoemptymsk |= 1 << ep->num;
					dwc_modify_reg32(&core_if->dev_if->
							 dev_global_regs->
							 dtknqr4_fifoemptymsk,
							 0, fifoemptymsk);
				}
			}
		}
	} else {
		struct dwc_otg_dev_out_ep_regs __iomem *out_regs =
			core_if->dev_if->out_ep_regs[0];

		depctl.d32 = dwc_read_reg32(&out_regs->doepctl);
		deptsiz.d32 = dwc_read_reg32(&out_regs->doeptsiz);

		/*Program the transfer size and packet count
		 *     as follows: xfersize = N *maxpacket +
		 *     short_packet pktcnt = N + (short_packet
		 *     exist ? 1 : 0)
		 */
		deptsiz.b.xfersize = ep->maxpacket;
		deptsiz.b.pktcnt = 1;

		if (core_if->dma_desc_enable == 0)
			dwc_write_reg32(&out_regs->doeptsiz, deptsiz.d32);
		else {
			dma_desc = core_if->dev_if->out_desc_addr;

			/**DMA Descriptor Setup */
			dma_desc->status.b.bs = BS_HOST_BUSY;
			dma_desc->status.b.l = 1;
			dma_desc->status.b.ioc = 1;
			dma_desc->status.b.bytes = ep->maxpacket;
			dma_desc->buf = ep->dma_addr;
			dma_desc->status.b.bs = BS_HOST_READY;

			wmb();

			/**DOEPDMA0 Register write */
			dwc_write_reg32(&out_regs->doepdma,
					core_if->dev_if->dma_out_desc_addr);
		}

		DWC_DEBUGPL(DBG_PCDV,
			    "IN len=%d  xfersize=%d pktcnt=%d [%08x]\n",
			    ep->xfer_len, deptsiz.b.xfersize, deptsiz.b.pktcnt,
			    deptsiz.d32);

		/*Write the DMA register */
		if (core_if->hwcfg2.b.architecture == DWC_INT_DMA_ARCH) {
			if (core_if->dma_desc_enable == 0)
				dwc_write_reg32(&(out_regs->doepdma),
						(u32) ep->dma_addr);
		}

		/*EP enable, IN data in FIFO */
		depctl.b.cnak = 1;
		depctl.b.epena = 1;

		wmb();
		dwc_write_reg32(&out_regs->doepctl, depctl.d32);

	}
}

/**
 * This function allocates a DMA Descriptor chain for the Endpoint
 * buffer to be used for a transfer to/from the specified endpoint.
 */
static struct dwc_otg_dev_dma_desc *
dwc_otg_ep_alloc_desc_chain(struct device *dev, dma_addr_t *dma_desc_addr,
			    u32 count)
{

	return dma_alloc_coherent(dev,
				  count * sizeof(struct dwc_otg_dev_dma_desc),
				  dma_desc_addr,
				  GFP_KERNEL);
}

/**
 * This function frees a DMA Descriptor chain that was allocated by
 * ep_alloc_desc.
 */
static void
dwc_otg_ep_free_desc_chain(struct device *dev,
			   struct dwc_otg_dev_dma_desc *desc_addr,
			   dma_addr_t dma_desc_addr, u32 count)
{
	dma_free_coherent(dev,
			  count * sizeof(struct dwc_otg_dev_dma_desc),
			  (void *)desc_addr,
			  dma_desc_addr);
}

/**
 * This function is used to submit an I/O Request to an EP.
 *
 *	- When the request completes the request's completion callback
 *	  is called to return the request to the driver.
 *	- An EP, except control EPs, may have multiple requests
 *	  pending.
 *	- Once submitted the request cannot be examined or modified.
 *	- Each request is turned into one or more packets.
 *	- A BULK EP can queue any amount of data; the transfer is
 *	  packetized.
 *	- Zero length Packets are specified with the request 'zero'
 *	  flag.
 */
static int dwc_otg_pcd_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
				gfp_t _gfp_flags)
{
	int start_needed = 0;
	struct dwc_otg_pcd_request *req;
	struct dwc_otg_pcd_ep *ep;
	struct dwc_otg_pcd *pcd;
	unsigned long flags = 0;
	uint32_t max_transfer;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p,%d)\n", __func__, _ep, _req,
		      _gfp_flags);
	req = container_of(_req, struct dwc_otg_pcd_request, req);
	if (!_req || !_req->complete || !_req->buf
	     || !list_empty(&req->queue)) {
		DWC_WARN("%s, bad params\n", __func__);
		return -EINVAL;
	}
	ep = container_of(_ep, struct dwc_otg_pcd_ep, ep);
	if (!_ep || (!ep->desc && ep->dwc_ep.num != 0)) {
		DWC_WARN("%s, bad ep\n", __func__);
		return -EINVAL;
	}
	pcd = ep->pcd;
	if (!pcd->driver || pcd->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_DEBUGPL(DBG_PCDV, "gadget.speed=%d\n", pcd->gadget.speed);
		DWC_WARN("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}
	DWC_DEBUGPL(DBG_PCD, "%s queue req %p, len %d buf %p\n", _ep->name,
		       _req, _req->length, _req->buf);
	if (!GET_CORE_IF(pcd)->core_params->opt) {
		if (ep->dwc_ep.num != 0) {
			DWC_ERROR("%s queue req %p, len %d buf %p\n",
				   _ep->name, _req, _req->length, _req->buf);
		}
	}
	spin_lock_irqsave(&ep->pcd->lock, flags);

	dwc_otg_dump_msg(_req->buf, _req->length);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/*
	 * For EP0 IN without premature status, zlp is required?
	 */
	if (ep->dwc_ep.num == 0 && ep->dwc_ep.is_in)
		DWC_DEBUGPL(DBG_PCDV, "%s-OUT ZLP\n", _ep->name);

	/*
	 * The DMA engine cannot do unaligned write accesses to memory
	 * so we must use an aligned bounce buffer for these :-(
	 */
	if (GET_CORE_IF(pcd)->dma_desc_enable &&
		(((!ep->dwc_ep.is_in && ((u32)_req->buf & 3)) ||
			(!ep->dwc_ep.is_in && (_req->length & 3))))) {

		/*
		 * our bounce buffer is only PAGE_SIZE
		 * TODO split request if bigger than PAGE_SIZE (v.unlikely)
		 */
		BUG_ON(_req->length > PAGE_SIZE);

		_req->dma = ep->bounce_buffer_dma;
		req->use_bounce_buffer = 1;
		req->mapped = 1;

	} else {

		/* map virtual address to hardware */
		if (_req->dma == DMA_ADDR_INVALID && _req->length) {
			_req->dma = dma_map_single(ep->pcd->gadget.dev.parent,
						  _req->buf,
						  _req->length,
						  ep->dwc_ep.is_in
						  ? DMA_TO_DEVICE :
						  DMA_FROM_DEVICE);
			req->mapped = 1;
		} else {
			dma_sync_single_for_device(ep->pcd->gadget.dev.parent,
						   _req->dma, _req->length,
						   ep->dwc_ep.is_in
						   ? DMA_TO_DEVICE :
						   DMA_FROM_DEVICE);
			req->mapped = 0;
		}
	}

	/* Start the transfer */
	if (list_empty(&ep->queue) && !ep->stopped) {
		/* EP0 Transfer? */
		if (ep->dwc_ep.num == 0) {
			switch (pcd->ep0state) {
			case EP0_IN_DATA_PHASE:
				DWC_DEBUGPL(DBG_PCD, "%s ep0: "
						"EP0_IN_DATA_PHASE\n",
						__func__);
				break;
			case EP0_OUT_DATA_PHASE:
				DWC_DEBUGPL(DBG_PCD, "%s ep0: "
						"EP0_OUT_DATA_PHASE\n",
						__func__);
				if (pcd->request_config) {
					/* Complete STATUS PHASE */
					ep->dwc_ep.is_in = 1;
					pcd->ep0state = EP0_IN_STATUS_PHASE;
				}
				break;
			case EP0_IN_STATUS_PHASE:
				DWC_DEBUGPL(DBG_PCD,
					    "%s ep0: EP0_IN_STATUS_PHASE\n",
					    __func__);
				break;
			default:
				DWC_DEBUGPL(DBG_ANY, "ep0: odd state %d\n",
						pcd->ep0state);
				spin_unlock_irqrestore(&pcd->lock, flags);
				return -EL2HLT;
			}
			ep->dwc_ep.dma_addr = _req->dma;
			ep->dwc_ep.start_xfer_buff = _req->buf;
			ep->dwc_ep.xfer_buff = _req->buf;
			ep->dwc_ep.xfer_len = _req->length;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;
			/*
			 * delay start till after putting request on queue
			 * to avoid a race.
			 */
			start_needed = 1;
		} else {
				max_transfer =
				    GET_CORE_IF(ep->pcd)->core_params->
				    max_transfer_size;
			/* Setup and start the Transfer */
			ep->dwc_ep.dma_addr = _req->dma;
			ep->dwc_ep.start_xfer_buff = _req->buf;
			ep->dwc_ep.xfer_buff = _req->buf;
			ep->dwc_ep.xfer_len = _req->length;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;
			ep->dwc_ep.maxxfer = max_transfer;
			if (GET_CORE_IF(pcd)->dma_desc_enable) {
					uint32_t out_max_xfer =
					    DDMA_MAX_TRANSFER_SIZE -
					    (DDMA_MAX_TRANSFER_SIZE % 4);
				if (ep->dwc_ep.is_in) {
					if (ep->dwc_ep.maxxfer >
					    DDMA_MAX_TRANSFER_SIZE) {
						ep->dwc_ep.maxxfer =
						    DDMA_MAX_TRANSFER_SIZE;
					}
				} else {
						if (ep->dwc_ep.maxxfer >
						    out_max_xfer) {
						ep->dwc_ep.maxxfer =
						    out_max_xfer;
					}
				}
			}
			if (ep->dwc_ep.maxxfer < ep->dwc_ep.total_len) {
				ep->dwc_ep.maxxfer -= (ep->dwc_ep.maxxfer %
							ep->dwc_ep.maxpacket);
			}

			/*
			 * delay start till after putting request on queue
			 * to avoid a race.
			 */
			start_needed = 1;
		}
	}

	if (req) {
		if (ep->dwc_ep.num == 0)
			++pcd->ep0_request_pending;
		else
			++ep->request_pending;

		list_add_tail(&req->queue, &ep->queue);
		if (ep->dwc_ep.is_in && ep->stopped
				&& !(GET_CORE_IF(pcd)->dma_enable)) {
			/** @todo NGS Create a function for this. */
			union diepint_data diepmsk = {.d32 = 0};
			diepmsk.b.intktxfemp = 1;
			if (GET_CORE_IF(pcd)->multiproc_int_enable) {
				dwc_modify_reg32(&GET_CORE_IF(pcd)->
						dev_if->
						dev_global_regs->
						 diepeachintmsk[ep->dwc_ep.num],
						0,
						diepmsk.d32);
			} else {
				dwc_modify_reg32(&GET_CORE_IF(pcd)->dev_if->
						 dev_global_regs->diepmsk, 0,
						 diepmsk.d32);
			}
		}
		if (start_needed) {
			if (ep->dwc_ep.num == 0) {
				dwc_otg_ep0_start_transfer(GET_CORE_IF(pcd),
							   &ep->dwc_ep);
			} else {
				dwc_otg_ep_start_transfer(GET_CORE_IF(pcd),
							  &ep->dwc_ep);
			}
		}
	}
	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

/**
 * This function cancels an I/O request from an EP.
 */
static int dwc_otg_pcd_ep_dequeue(struct usb_ep *_ep,
				  struct usb_request *_req)
{
	struct dwc_otg_pcd_request *req;
	struct dwc_otg_pcd_ep *ep;
	struct dwc_otg_pcd *pcd;
	unsigned long flags;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p)\n", __func__, _ep, _req);
	ep = container_of(_ep, struct dwc_otg_pcd_ep, ep);
	if (!_ep || !_req || (!ep->desc && ep->dwc_ep.num != 0)) {
		DWC_WARN("%s, bad argument\n", __func__);
		return -EINVAL;
	}
	pcd = ep->pcd;
	if (!pcd->driver || pcd->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_WARN("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}
	spin_lock_irqsave(&pcd->lock, flags);
	DWC_DEBUGPL(DBG_PCDV, "%s %s %s %p\n", __func__, _ep->name,
		     ep->dwc_ep.is_in ? "IN" : "OUT", _req);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore(&pcd->lock, flags);
		return -EINVAL;
	}
	if (!list_empty(&req->queue))
		dwc_otg_request_done(ep, req, -ECONNRESET, &flags);
	else
		req = NULL;

	spin_unlock_irqrestore(&pcd->lock, flags);
	return req ? 0 : -EOPNOTSUPP;
}

/**
 *Set the EP STALL.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP to set the stall on.
 */
void dwc_otg_ep_set_stall(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	union depctl_data depctl;
	u32 __iomem *depctl_addr;
	DWC_DEBUGPL(DBG_PCD, "%s ep%d-%s\n", __func__, ep->num,
		      (ep->is_in ? "IN" : "OUT"));
	if (ep->is_in == 1) {
		depctl_addr = &(core_if->dev_if->in_ep_regs[ep->num]->diepctl);
		depctl.d32 = dwc_read_reg32(depctl_addr);

		/*set the disable and stall bits */
		if (depctl.b.epena) {
			if (dwc_otg_can_disable_channel(core_if, ep))
				depctl.b.epdis = 1;
		}
		depctl.b.stall = 1;
		dwc_write_reg32(depctl_addr, depctl.d32);
	} else {
		depctl_addr = &(core_if->dev_if->out_ep_regs[ep->num]->doepctl);
		depctl.d32 = dwc_read_reg32(depctl_addr);

		/*set the stall bit */
		depctl.b.stall = 1;
		dwc_write_reg32(depctl_addr, depctl.d32);
	}
	DWC_DEBUGPL(DBG_PCD, "DEPCTL=%0x\n", dwc_read_reg32(depctl_addr));
	return;
}


/**
 *Clear the EP STALL.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP to clear stall from.
 */
void dwc_otg_ep_clear_stall(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	union depctl_data depctl;
	u32 __iomem *depctl_addr;
	DWC_DEBUGPL(DBG_PCD, "%s ep%d-%s\n", __func__, ep->num,
		      (ep->is_in ? "IN" : "OUT"));

	if (ep->is_in == 1)
		depctl_addr = &(core_if->dev_if->in_ep_regs[ep->num]->diepctl);
	else
		depctl_addr = &(core_if->dev_if->out_ep_regs[ep->num]->doepctl);

	depctl.d32 = dwc_read_reg32(depctl_addr);

	/*clear the stall bits */
	depctl.b.stall = 0;

	/*
	 * USB Spec 9.4.5: For endpoints using data toggle, regardless
	 * of whether an endpoint has the Halt feature set, a
	 * ClearFeature(ENDPOINT_HALT) request always results in the
	 * data toggle being reinitialised to DATA0.
	 */
	if (ep->type == USB_ENDPOINT_XFER_INT ||
		ep->type == USB_ENDPOINT_XFER_BULK) {
		depctl.b.setd0pid = 1;	/*DATA0 */
	}
	dwc_write_reg32(depctl_addr, depctl.d32);
	DWC_DEBUGPL(DBG_PCD, "DEPCTL=%0x\n", dwc_read_reg32(depctl_addr));
	return;
}

/**
 * usb_ep_set_halt stalls an endpoint.
 *
 * usb_ep_clear_halt clears an endpoint halt and resets its data
 * toggle.
 *
 * Both of these functions are implemented with the same underlying
 * function. The behavior depends on the value argument.
 *	- 0 means clear_halt.
 *	- 1 means set_halt,
 *	- 2 means clear stall lock flag.
 *	- 3 means set  stall lock flag.
 */
static int dwc_otg_pcd_ep_set_halt(struct usb_ep *_ep, int value)
{
	int retval = 0;
	unsigned long flags;
	struct dwc_otg_pcd_ep *ep = NULL;
	DWC_DEBUGPL(DBG_PCD, "HALT %s %d\n", _ep->name, value);
	ep = container_of(_ep, struct dwc_otg_pcd_ep, ep);
	if (!ep || (!ep->desc && ep != &ep->pcd->ep0)
	      || ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		DWC_WARN("%s, bad ep\n", __func__);
		return -EINVAL;
	}
	spin_lock_irqsave(&ep->pcd->lock, flags);
	if (!list_empty(&ep->queue)) {
		DWC_DEBUGPL(DBG_PCD, "%s() %s XFer In process\n",
			    __func__, _ep->name);
		retval = -EAGAIN;
	} else if (value == 0)
		dwc_otg_ep_clear_stall(ep->pcd->otg_dev->core_if, &ep->dwc_ep);
	else if (value == 1) {
		if (ep->dwc_ep.is_in == 1 &&
				GET_CORE_IF(ep->pcd)->dma_desc_enable) {
			union dtxfsts_data txstatus;
			union fifosize_data txfifosize;

			txfifosize.d32 =
			    dwc_read_reg32(&GET_CORE_IF(ep->pcd)->
					   core_global_regs->
					   dptxfsiz_dieptxf[ep->dwc_ep.
							    tx_fifo_num]);
			txstatus.d32 =
			    dwc_read_reg32(&GET_CORE_IF(ep->pcd)->dev_if->
					   in_ep_regs[ep->dwc_ep.num]->dtxfsts);

			if (txstatus.b.txfspcavail < txfifosize.b.depth) {
				DWC_DEBUGPL(DBG_PCD, "%s() Data In Tx Fifo\n",
					    __func__);
				retval = -EAGAIN;
			} else {
				if (ep->dwc_ep.num == 0)
					ep->pcd->ep0state = EP0_STALL;
				ep->stopped = 1;
				dwc_otg_ep_set_stall(ep->pcd->otg_dev->core_if,
						&ep->dwc_ep);
			}
		} else {
			if (ep->dwc_ep.num == 0)
				ep->pcd->ep0state = EP0_STALL;

			ep->stopped = 1;
			dwc_otg_ep_set_stall(GET_CORE_IF(ep->pcd), &ep->dwc_ep);
		}
	} else if (value == 2)
		ep->dwc_ep.stall_clear_flag = 0;
	else if (value == 3)
		ep->dwc_ep.stall_clear_flag = 1;

	spin_unlock_irqrestore(&ep->pcd->lock, flags);
	return retval;
}

static struct usb_ep_ops dwc_otg_pcd_ep_ops = {
	.enable = dwc_otg_pcd_ep_enable,
	.disable = dwc_otg_pcd_ep_disable,
	.alloc_request = dwc_otg_pcd_alloc_request,
	.free_request = dwc_otg_pcd_free_request,
	.queue = dwc_otg_pcd_ep_queue,
	.dequeue = dwc_otg_pcd_ep_dequeue,
	.set_halt = dwc_otg_pcd_ep_set_halt,
};

/**
 *Gets the current USB frame number. This is the frame number from the last
 *SOF packet.
 */
u32 dwc_otg_get_frame_number(struct dwc_otg_core_if *core_if)
{
	union dsts_data dsts;
	dsts.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dsts);
	/*read current frame/microframe number from DSTS register */
	return dsts.b.soffn;
}

/**
 *Gets the USB Frame number of the last SOF.
 */
static int dwc_otg_pcd_get_frame(struct usb_gadget *gadget)
{
	struct dwc_otg_pcd *pcd;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, gadget);
	if (gadget)
		return -ENODEV;
	else {
		pcd = container_of(gadget, struct dwc_otg_pcd, gadget);
		dwc_otg_get_frame_number(GET_CORE_IF(pcd));
	}
	return 0;
}

/**
 * This function is called when the SRP timer expires.	The SRP should
 * complete within 6 seconds.
 */
static void srp_timeout(unsigned long _ptr)
{
	union gotgctl_data gotgctl;
	struct dwc_otg_core_if *core_if = (struct dwc_otg_core_if *) _ptr;
	u32 __iomem *addr = &core_if->core_global_regs->gotgctl;
	gotgctl.d32 = dwc_read_reg32(addr);
	core_if->srp_timer_started = 0;
	if ((core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS) &&
		(core_if->core_params->i2c_enable)) {
		DWC_PRINT("SRP Timeout\n");
		if ((core_if->srp_success) && (gotgctl.b.bsesvld)) {
			if (core_if->pcd_cb && core_if->pcd_cb->resume_wakeup)
				core_if->pcd_cb->resume_wakeup(core_if->
								pcd_cb->p);

			/* Clear Session Request */
			gotgctl.d32 = 0;
			gotgctl.b.sesreq = 1;
			dwc_modify_reg32(&core_if->
					core_global_regs->
					gotgctl,
					gotgctl.d32,
					0);

			core_if->srp_success = 0;
		} else {
			DWC_ERROR("Device not connected/responding\n");
			gotgctl.b.sesreq = 0;
			dwc_write_reg32(addr, gotgctl.d32);
		}
	} else if (gotgctl.b.sesreq) {
		DWC_PRINT("SRP Timeout\n");
		DWC_ERROR("Device not connected/responding\n");
		gotgctl.b.sesreq = 0;
		dwc_write_reg32(addr, gotgctl.d32);
	} else
		DWC_PRINT(" SRP GOTGCTL=%0x\n", gotgctl.d32);
}

/**
 * Start the SRP timer to detect when the SRP does not complete within
 * 6 seconds.
 *
 */
void dwc_otg_pcd_start_srp_timer(struct dwc_otg_pcd *pcd)
{
	struct timer_list *srp_timer = &pcd->srp_timer;
	GET_CORE_IF(pcd)->srp_timer_started = 1;
	init_timer(srp_timer);
	srp_timer->function = srp_timeout;
	srp_timer->data = (unsigned long)GET_CORE_IF(pcd);
	srp_timer->expires = jiffies + (HZ * 6);
	add_timer(srp_timer);
}


void dwc_otg_pcd_initiate_srp(struct dwc_otg_pcd *pcd)
{
	u32 __iomem *addr = &(GET_CORE_IF(pcd)->core_global_regs->gotgctl);
	union gotgctl_data mem;
	union gotgctl_data val;
	val.d32 = dwc_read_reg32(addr);
	if (val.b.sesreq) {
		DWC_ERROR("Session Request Already active!\n");
		return;
	}
	DWC_NOTICE("Session Request Initated\n");
	mem.d32 = dwc_read_reg32(addr);
	mem.b.sesreq = 1;
	dwc_write_reg32(addr, mem.d32);

	/* Start the SRP timer */
	dwc_otg_pcd_start_srp_timer(pcd);
	return;
}

/**
 * This function initiates remote wakeup of the host from suspend state.
 */
static void dwc_otg_pcd_rem_wkup_from_suspend(struct dwc_otg_pcd *pcd, int set)
{
	union dctl_data dctl = { 0 };
	struct dwc_otg_core_if *core_if = GET_CORE_IF(pcd);
	union dsts_data dsts;

	dsts.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dsts);
	if (!dsts.b.suspsts)
		DWC_WARN("Remote wakeup while is not in suspend state\n");

	/* Check if DEVICE_REMOTE_WAKEUP feature enabled */
	if (pcd->remote_wakeup_enable) {
		if (set) {
			dctl.b.rmtwkupsig = 1;
			dwc_modify_reg32(&core_if->dev_if->dev_global_regs->
					 dctl, 0, dctl.d32);
			DWC_DEBUGPL(DBG_PCD, "Set Remote Wakeup\n");
			mdelay(2);
			dwc_modify_reg32(&core_if->dev_if->dev_global_regs->
					 dctl, dctl.d32, 0);
			DWC_DEBUGPL(DBG_PCD, "Clear Remote Wakeup\n");
		}
	} else {
		DWC_DEBUGPL(DBG_PCD, "Remote Wakeup is disabled\n");
	}
}

#ifdef CONFIG_USB_DWC_OTG_LPM
/**
 * This function initiates remote wakeup of the host from L1 sleep state.
 */
void dwc_otg_pcd_rem_wkup_from_sleep(struct dwc_otg_pcd *pcd, int set)
{
	union glpmcfg_data lpmcfg;
	struct dwc_otg_core_if *core_if = GET_CORE_IF(pcd);

	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);

	/* Check if we are in L1 state */
	if (!lpmcfg.b.prt_sleep_sts) {
		DWC_DEBUGPL(DBG_PCD, "Device is not in sleep state\n");
		return;
	}

	/* Check if host allows remote wakeup */
	if (!lpmcfg.b.rem_wkup_en) {
		DWC_DEBUGPL(DBG_PCD, "Host does not allow remote wakeup\n");
		return;
	}

	/* Check if Resume OK */
	if (!lpmcfg.b.sleep_state_resumeok) {
		DWC_DEBUGPL(DBG_PCD, "Sleep state resume is not OK\n");
		return;
	}

	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.en_utmi_sleep = 0;
	lpmcfg.b.hird_thres &= (~(1 << 4));
	dwc_write_reg32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);

	if (set) {
		union dctl_data_ dctl = {.d32 = 0 };
		dctl.b.rmtwkupsig = 1;
		/* Set RmtWkUpSig bit to start remote wakup signaling.
		 * Hardware will automatically clear this bit.
		 */
		dwc_modify_reg32(&core_if->dev_if->dev_global_regs->dctl,
				 0, dctl.d32);
		DWC_DEBUGPL(DBG_PCD, "Set Remote Wakeup\n");
	}

}
#endif
void dwc_otg_pcd_remote_wakeup(struct dwc_otg_pcd *pcd, int set)
{
	if (dwc_otg_is_device_mode(GET_CORE_IF(pcd))) {
#ifdef CONFIG_USB_DWC_OTG_LPM
		if (core_if->lx_state == DWC_OTG_L1) {
			dwc_otg_pcd_rem_wkup_from_sleep(pcd, set);
		} else {
#endif
			dwc_otg_pcd_rem_wkup_from_suspend(pcd, set);
#ifdef CONFIG_USB_DWC_OTG_LPM
		}
#endif
	}
	return;
}

/**
 * Initiates Session Request Protocol (SRP) to wakeup the host if no
 * session is in progress. If a session is already in progress, but
 * the device is suspended, remote wakeup signaling is started.
 *
 */
static int dwc_otg_pcd_wakeup(struct usb_gadget *gadget)
{
	unsigned long flags;
	struct dwc_otg_pcd *pcd;
	union dsts_data dsts;
	union gotgctl_data gotgctl;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, gadget);

	if (!gadget)
		return -ENODEV;
	else
		pcd = container_of(gadget, struct dwc_otg_pcd, gadget);

	spin_lock_irqsave(&pcd->lock, flags);

	/*
	 * This function starts the Protocol if no session is in progress. If
	 * a session is already in progress, but the device is suspended,
	 * remote wakeup signaling is started.
	 */

	/* Check if valid session */
	gotgctl.d32 =
		dwc_read_reg32(&(GET_CORE_IF(pcd)->core_global_regs->gotgctl));
	if (gotgctl.b.bsesvld) {

		/* Check if suspend state */
		dsts.d32 =
			dwc_read_reg32(&(GET_CORE_IF(pcd)->
					dev_if->
					dev_global_regs->
					dsts));
		if (dsts.b.suspsts)
			dwc_otg_pcd_remote_wakeup(pcd, 1);
	} else
		dwc_otg_pcd_initiate_srp(pcd);

	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

static int dwc_otg_pullup(struct usb_gadget *gadget, int is_on)
{
	union dctl_data dctl = {.d32 = 0 };
	struct dwc_otg_pcd *pcd;
	unsigned long flags;
	dctl.b.sftdiscon = 1;

	if (!gadget)
		return -ENODEV;
	else
		pcd = container_of(gadget, struct dwc_otg_pcd, gadget);

	spin_lock_irqsave(&pcd->lock, flags);

	if (is_on)
		dwc_modify_reg32(&(GET_CORE_IF(pcd)->dev_if->dev_global_regs->dctl),
				 dctl.d32,
				 0);
	else
		dwc_modify_reg32(&(GET_CORE_IF(pcd)->dev_if->dev_global_regs->dctl),
				 0,
				 dctl.d32);

	spin_unlock_irqrestore(&pcd->lock, flags);

	return 0;
}

static int dwc_otg_pcd_gadget_start(struct usb_gadget *g,
				    struct usb_gadget_driver *driver);
static int dwc_otg_pcd_gadget_stop(struct usb_gadget *g,
				   struct usb_gadget_driver *driver);

static const struct usb_gadget_ops dwc_otg_pcd_ops = {
	.get_frame = dwc_otg_pcd_get_frame,
	.wakeup = dwc_otg_pcd_wakeup,
	.pullup = dwc_otg_pullup,
	.udc_start = dwc_otg_pcd_gadget_start,
	.udc_stop = dwc_otg_pcd_gadget_stop,
};

/**
 * This function updates the otg values in the gadget structure.
 */
void dwc_otg_pcd_update_otg(struct dwc_otg_pcd *pcd, const unsigned reset)
{
	if (!pcd->gadget.is_otg)
		return;
	if (reset) {
		pcd->b_hnp_enable = 0;
		pcd->a_hnp_support = 0;
		pcd->a_alt_hnp_support = 0;
	}
	pcd->gadget.b_hnp_enable = pcd->b_hnp_enable;
	pcd->gadget.a_hnp_support = pcd->a_hnp_support;
	pcd->gadget.a_alt_hnp_support = pcd->a_alt_hnp_support;
}

/**
 * This function is the top level PCD interrupt handler.
 */
static irqreturn_t  dwc_otg_pcd_irq(int irq, void *dev)
{
	struct dwc_otg_pcd *pcd = dev;
	int retval;
	retval = dwc_otg_pcd_handle_intr(pcd);
	return IRQ_RETVAL(retval);
}

/**
 * PCD Callback function for initializing the PCD when switching to
 * device mode.
 */
static int dwc_otg_pcd_start_cb(void *_p)
{
	struct dwc_otg_pcd *pcd = (struct dwc_otg_pcd *) _p;

	/*
	 * Initialized the Core for Device mode.
	 */
	if (dwc_otg_is_device_mode(GET_CORE_IF(pcd)))
		dwc_otg_core_dev_init(GET_CORE_IF(pcd));

	return 1;
}

/**
 * PCD Callback function for stopping the PCD when switching to Host
 * mode.
 */
static int dwc_otg_pcd_stop_cb(void *_p)
{

	dwc_otg_pcd_stop((struct dwc_otg_pcd *) _p);
	return 1;
}

/**
 * PCD Callback function for notifying the PCD when resuming from
 * suspend.
 *
 * Do not call with lock held, currently this cb is only called from
 * the common interrupt handler which takes no locks.
 *
 */
static int dwc_otg_pcd_suspend_cb(void *_p)
{
	struct dwc_otg_pcd *pcd = (struct dwc_otg_pcd *) _p;
	if (pcd->driver && pcd->driver->suspend)
		pcd->driver->suspend(&pcd->gadget);

	return 1;
}

/**
 * PCD Callback function for notifying the PCD when resuming from
 * suspend.
 */
static int dwc_otg_pcd_resume_cb(void *_p)
{
	struct dwc_otg_pcd *pcd = (struct dwc_otg_pcd *) _p;
	if (pcd->driver && pcd->driver->resume) {
		WARN_ON(!in_interrupt());
		pcd->driver->resume(&pcd->gadget);
	}

	/* Maybe stop the SRP timeout timer. */
	if (need_stop_srp_timer(GET_CORE_IF(pcd)))  {
		GET_CORE_IF(pcd)->srp_timer_started = 0;
		del_timer_sync(&pcd->srp_timer);
	}
	return 1;
}

/**
 * PCD Callback structure for handling mode switching.
 */
static struct dwc_otg_cil_callbacks pcd_callbacks = {
	.start = dwc_otg_pcd_start_cb,
	.stop = dwc_otg_pcd_stop_cb,
	.suspend = dwc_otg_pcd_suspend_cb,
	.resume_wakeup = dwc_otg_pcd_resume_cb,
	/* p set at registration */
};

/**
 * Tasklet
 *
 */
static void start_xfer_tasklet_func(unsigned long data)
{
	struct dwc_otg_pcd *pcd = (struct dwc_otg_pcd *) data;
	struct dwc_otg_core_if *core_if = pcd->otg_dev->core_if;
	int i;
	union depctl_data diepctl;
	DWC_DEBUGPL(DBG_PCDV, "Start xfer tasklet\n");
	diepctl.d32 =
		dwc_read_reg32(&core_if->dev_if->in_ep_regs[0]->diepctl);
	if (pcd->ep0.queue_sof) {
		pcd->ep0.queue_sof = 0;
		start_next_request(&pcd->ep0);
	}
	for (i = 0; i < core_if->dev_if->num_in_eps; i++) {
		union depctl_data diepctl;
		diepctl.d32 =
		    dwc_read_reg32(&core_if->dev_if->in_ep_regs[i]->diepctl);
		if (pcd->in_ep[i].queue_sof) {
			pcd->in_ep[i].queue_sof = 0;
			start_next_request(&pcd->in_ep[i]);
		}
	}
}

static DECLARE_TASKLET(start_xfer_tasklet, start_xfer_tasklet_func, 0);

static int dwc_otg_pcd_init_ep(struct dwc_otg_pcd *pcd,
				struct dwc_otg_pcd_ep *pcd_ep,
				u32 is_in, u32 ep_num)
{
	int retval = 0;

	/* Init EP structure */
	pcd_ep->desc = NULL;
	pcd_ep->pcd = pcd;
	pcd_ep->stopped = 1;
	pcd_ep->queue_sof = 0;

	/* Init DWC ep structure */
	pcd_ep->dwc_ep.is_in = is_in;
	pcd_ep->dwc_ep.num = ep_num;
	pcd_ep->dwc_ep.active = 0;
	pcd_ep->dwc_ep.tx_fifo_num = 0;
	/* Control until ep is actvated */
	pcd_ep->dwc_ep.type = USB_ENDPOINT_XFER_CONTROL;
	pcd_ep->dwc_ep.maxpacket = MAX_PACKET_SIZE;
	pcd_ep->dwc_ep.dma_addr = 0;
	pcd_ep->dwc_ep.start_xfer_buff = NULL;
	pcd_ep->dwc_ep.xfer_buff = NULL;
	pcd_ep->dwc_ep.xfer_len = 0;
	pcd_ep->dwc_ep.xfer_count = 0;
	pcd_ep->dwc_ep.sent_zlp = 0;
	pcd_ep->dwc_ep.total_len = 0;
	pcd_ep->dwc_ep.desc_addr = NULL;
	pcd_ep->dwc_ep.dma_desc_addr = 0;


	/*
	 * pre-allocate all DMA buffers instead of allocing and freeing them
	 * all the time
	 */
	if (GET_CORE_IF(pcd)->dma_desc_enable) {
		pcd_ep->dwc_ep.desc_addr =
		    dwc_otg_ep_alloc_desc_chain(pcd->dev,
						&pcd_ep->dwc_ep.dma_desc_addr,
						MAX_DMA_DESC_CNT);
		if (!pcd_ep->dwc_ep.desc_addr) {
			DWC_WARN("%s, can't allocate DMA descriptor\n",
				 __func__);
			retval = -ESHUTDOWN;
			goto out;
		}
		pcd_ep->bounce_buffer = dma_alloc_coherent(pcd->dev,
						       PAGE_SIZE,
						       &pcd_ep->bounce_buffer_dma,
						       GFP_KERNEL);
		if (!pcd_ep->bounce_buffer) {
			DWC_WARN("%s, can't allocate DMA bounce buffer\n",
				 __func__);
			retval = -ESHUTDOWN;
			goto out;
		}
	}
out:
	return retval;
}

static void dwc_otg_free_channel_dma(struct dwc_otg_pcd *pcd)
{
	int i;
	u32 num_in_eps = (GET_CORE_IF(pcd))->dev_if->num_in_eps;
	u32 num_out_eps = (GET_CORE_IF(pcd))->dev_if->num_out_eps;
	for (i = 1; i < num_in_eps; i++) {
		struct dwc_otg_pcd_ep *ep = &pcd->in_ep[i-i];
		if (ep->dwc_ep.desc_addr)
			dwc_otg_ep_free_desc_chain(ep->pcd->dev,
						   ep->dwc_ep.desc_addr,
						   ep->dwc_ep.dma_desc_addr,
						   MAX_DMA_DESC_CNT);

		if (ep->bounce_buffer)
			dma_free_coherent(ep->pcd->dev, PAGE_SIZE,
					  ep->bounce_buffer,
					  ep->bounce_buffer_dma);
	}

	for (i = 1; i < num_out_eps; i++) {
		struct dwc_otg_pcd_ep *ep = &pcd->out_ep[i-1];
		if (ep->dwc_ep.desc_addr)
			dwc_otg_ep_free_desc_chain(ep->pcd->dev,
						   ep->dwc_ep.desc_addr,
						   ep->dwc_ep.dma_desc_addr,
						   MAX_DMA_DESC_CNT);

		if (ep->bounce_buffer)
			dma_free_coherent(ep->pcd->dev, PAGE_SIZE,
					  ep->bounce_buffer,
					 ep->bounce_buffer_dma);

	}
}

/**
 * This function initialized the pcd ep structures to there default
 * state.
 *
 * @param pcd the pcd structure.
 */
static int dwc_otg_pcd_reinit(struct dwc_otg_pcd *pcd)
{
	int retval = 0;
	int i;
	int in_ep_cntr, out_ep_cntr;
	u32 hwcfg1;
	u32 num_in_eps = (GET_CORE_IF(pcd))->dev_if->num_in_eps;
	u32 num_out_eps = (GET_CORE_IF(pcd))->dev_if->num_out_eps;
	struct dwc_otg_pcd_ep *ep;
	static const char *names[] = {
		"ep0", "ep1in", "ep2in", "ep3in", "ep4in", "ep5in",
		"ep6in", "ep7in", "ep8in", "ep9in", "ep10in", "ep11in",
		"ep12in", "ep13in", "ep14in", "ep15in", "ep1out", "ep2out",
		"ep3out", "ep4out", "ep5out", "ep6out", "ep7out", "ep8out",
		"ep9out", "ep10out", "ep11out", "ep12out",
		"ep13out", "ep14out", "ep15out"
	};

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, pcd);
	INIT_LIST_HEAD(&pcd->gadget.ep_list);
	pcd->gadget.ep0 = &pcd->ep0.ep;
	pcd->gadget.speed = USB_SPEED_UNKNOWN;
	INIT_LIST_HEAD(&pcd->gadget.ep0->ep_list);

	/**
	 * Initialize the EP0 structure.
	 */
	ep = &pcd->ep0;
	retval = dwc_otg_pcd_init_ep(pcd, ep, 0, 0);
	if (retval)
		goto out;

	ep->ep.name = names[0];
	ep->ep.ops = &dwc_otg_pcd_ep_ops;

	ep->ep.maxpacket = MAX_PACKET_SIZE;
	list_add_tail(&ep->ep.ep_list, &pcd->gadget.ep_list);

	INIT_LIST_HEAD(&ep->queue);

	in_ep_cntr = 0;
	hwcfg1 = (GET_CORE_IF(pcd))->hwcfg1.d32 >> 3;
	for (i = 1; in_ep_cntr < num_in_eps; i++) {
		if (!(hwcfg1 & 0x1)) {
			struct dwc_otg_pcd_ep *ep = &pcd->in_ep[in_ep_cntr];
			in_ep_cntr++;
			/**
			 * @todo NGS: Add direction to EP, based on contents
			 * of HWCFG1.  Need a copy of HWCFG1 in pcd structure?
			 */
			retval = dwc_otg_pcd_init_ep(pcd, ep, 1 /* IN */ , i);
			if (retval) {
				dwc_otg_free_channel_dma(pcd);
				goto out;
			}

			ep->ep.name = names[i];
			ep->ep.ops = &dwc_otg_pcd_ep_ops;

			ep->ep.maxpacket = MAX_PACKET_SIZE;
			list_add_tail(&ep->ep.ep_list, &pcd->gadget.ep_list);

			INIT_LIST_HEAD(&ep->queue);
		}
		hwcfg1 >>= 2;
	}
	out_ep_cntr = 0;
	hwcfg1 = (GET_CORE_IF(pcd))->hwcfg1.d32 >> 2;
	for (i = 1; out_ep_cntr < num_out_eps; i++) {
		if (!(hwcfg1 & 0x1)) {
			struct dwc_otg_pcd_ep *ep = &pcd->out_ep[out_ep_cntr];
			out_ep_cntr++;
			/**
			 * @todo NGS: Add direction to EP, based on contents
			 * of HWCFG1.  Need a copy of HWCFG1 in pcd structure?
			 */
			retval = dwc_otg_pcd_init_ep(pcd, ep, 0 /* OUT */ , i);
			if (retval) {
				dwc_otg_free_channel_dma(pcd);
				goto out;
			}


			ep->ep.name = names[15 + i];
			ep->ep.ops = &dwc_otg_pcd_ep_ops;

			ep->ep.maxpacket = MAX_PACKET_SIZE;
			list_add_tail(&ep->ep.ep_list, &pcd->gadget.ep_list);

			INIT_LIST_HEAD(&ep->queue);
		}
		hwcfg1 >>= 2;
	}

	list_del_init(&pcd->ep0.ep.ep_list);
	pcd->ep0state = EP0_DISCONNECT;
	pcd->ep0.ep.maxpacket = MAX_EP0_SIZE;
	pcd->ep0.dwc_ep.maxpacket = MAX_EP0_SIZE;
	pcd->ep0.dwc_ep.type = USB_ENDPOINT_XFER_CONTROL;
out:
	return retval;
}

/**
 * This function releases the Gadget device.
 * required by device_unregister().
 *
 * @todo Should this do something?	Should it free the PCD?
 */
static void dwc_otg_pcd_gadget_release(struct device *dev)
{
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, dev);
}

/**
 * This function initialized the PCD portion of the driver.
 *
 */
int __init dwc_otg_pcd_init(struct device *dev)
{
	static char pcd_name[] = "dwc_otg_pcd";
	struct dwc_otg_pcd *pcd;
	struct dwc_otg_device *otg_dev = dev_get_drvdata(dev);
	struct dwc_otg_dev_if *dev_if;
	int retval = 0;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, dev);

	/*
	 * Allocate PCD structure
	 */
	pcd = kzalloc(sizeof(*pcd), GFP_KERNEL);
	if (!pcd)
		return -ENOMEM;


	spin_lock_init(&pcd->lock);
	otg_dev->pcd = pcd;
	pcd->dev = dev;
	pcd->gadget.name = pcd_name;
	pcd->otg_dev = dev_get_drvdata(dev);
	pcd->gadget.dev.parent = dev;
	pcd->gadget.dev.release = dwc_otg_pcd_gadget_release;
	pcd->gadget.ops = &dwc_otg_pcd_ops;
	if (GET_CORE_IF(pcd)->hwcfg4.b.ded_fifo_en)
		DWC_PRINT("Dedicated Tx FIFOs mode\n");
	else
		DWC_PRINT("Shared Tx FIFO mode\n");

	pcd->gadget.max_speed = dwc_otg_pcd_max_speed(pcd);
	pcd->gadget.is_otg = dwc_otg_pcd_is_otg(pcd);

	pcd->driver = NULL;

	retval = usb_add_gadget_udc(dev, &pcd->gadget);
	if (retval) {
		DWC_ERROR("failed to add gadget udc\n");
		goto err_free;
	}

	/*
	 * Initialized the Core for Device mode.
	 */
	if (dwc_otg_is_device_mode(GET_CORE_IF(pcd)))
		dwc_otg_core_dev_init(GET_CORE_IF(pcd));

	/*
	 * Initialize EP structures
	 */
	retval = dwc_otg_pcd_reinit(pcd);
	if (retval != 0) {
		DWC_ERROR("failed to setup EPs\n");
		goto err_gadget;
	}

	/*
	 * Register the PCD Callbacks.
	 */
	dwc_otg_cil_register_pcd_callbacks(otg_dev->core_if,
			&pcd_callbacks, pcd);

	/*
	 * Setup interrupt handler
	 */
	DWC_DEBUGPL(DBG_ANY, "registering handler for irq%d\n", otg_dev->irq);
	retval = request_irq(otg_dev->irq, dwc_otg_pcd_irq, IRQF_SHARED,
			     pcd->gadget.name, pcd);
	if (retval != 0) {
		DWC_ERROR("request of irq%d failed\n", otg_dev->irq);
		retval = -EBUSY;
		goto err_eps;
	}

	/*
	 * Initialize the DMA buffer for SETUP packets
	 */
	retval = -ENOMEM;
	if (GET_CORE_IF(pcd)->dma_enable) {
		pcd->setup_pkt = dma_alloc_coherent(dev,
					sizeof(*pcd->setup_pkt) * 5,
					&pcd->setup_pkt_dma_handle, 0);
		if (!pcd->setup_pkt)
			goto err_irq;
		pcd->status_buf = dma_alloc_coherent(dev,
					sizeof(uint16_t),
					&pcd->status_buf_dma_handle, 0);
		if (!pcd->status_buf)
			goto err_free_setup_pkt;

		if (GET_CORE_IF(pcd)->dma_desc_enable) {
			dev_if = otg_dev->core_if->dev_if;

			dev_if->setup_desc_addr[0] =
			    dwc_otg_ep_alloc_desc_chain(dev, &dev_if->
							dma_setup_desc_addr[0],
							1);
			if (!dev_if->setup_desc_addr[0])
				goto err_free_status_buf;

			dev_if->setup_desc_addr[1] =
			    dwc_otg_ep_alloc_desc_chain(dev, &dev_if->
							dma_setup_desc_addr[1],
							1);
			if (!dev_if->setup_desc_addr[1])
				goto err_free_setup_desc_0;

			dev_if->in_desc_addr =
			    dwc_otg_ep_alloc_desc_chain(dev, &dev_if->
							dma_in_desc_addr, 1);
			if (!dev_if->in_desc_addr)
				goto err_free_setup_desc_1;

			dev_if->out_desc_addr =
			    dwc_otg_ep_alloc_desc_chain(dev, &dev_if->
							dma_out_desc_addr, 1);
			if (!dev_if->out_desc_addr)
				goto err_free_in_desc;

			pcd->ep0.bounce_buffer = dma_alloc_coherent(dev,
							PAGE_SIZE,
							&pcd->ep0.bounce_buffer_dma,
							GFP_KERNEL);
			if (!pcd->ep0.bounce_buffer)
				goto err_free_out_desc;
		}
	} else {
		pcd->setup_pkt =  kmalloc(sizeof(*pcd->setup_pkt) * 5,
					GFP_KERNEL);
		if (!pcd->setup_pkt)
			goto err_irq;
		pcd->status_buf = kmalloc(sizeof(uint16_t),
					GFP_KERNEL);
		if (!pcd->status_buf)
			goto err_free_setup_pkt_nodma;
	}

	/* Initialize tasklet */
	start_xfer_tasklet.data = (unsigned long)pcd;
	pcd->start_xfer_tasklet = &start_xfer_tasklet;

	return 0;

	/* DMA enable */
err_free_out_desc:
	dwc_otg_ep_free_desc_chain(dev, dev_if->out_desc_addr,
				   dev_if->dma_out_desc_addr, 1);
err_free_in_desc:
	dwc_otg_ep_free_desc_chain(dev, dev_if->in_desc_addr,
				   dev_if->dma_in_desc_addr, 1);
err_free_setup_desc_1:
	dwc_otg_ep_free_desc_chain(dev, dev_if->setup_desc_addr[1],
				   dev_if->dma_setup_desc_addr[1], 1);
err_free_setup_desc_0:
	dwc_otg_ep_free_desc_chain(dev, dev_if->setup_desc_addr[0],
				   dev_if->dma_setup_desc_addr[0], 1);
err_free_status_buf:
	dma_free_coherent(dev, sizeof(*pcd->status_buf), pcd->status_buf,
			  pcd->status_buf_dma_handle);
err_free_setup_pkt:
	dma_free_coherent(dev, sizeof(*pcd->setup_pkt) * 5, pcd->setup_pkt,
			  pcd->setup_pkt_dma_handle);
	goto err_irq;

	/* DMA disable */
err_free_setup_pkt_nodma:
	kfree(pcd->setup_pkt);

	/* common error handling */
err_irq:
	free_irq(otg_dev->irq, pcd);
err_eps:
	dwc_otg_free_channel_dma(pcd);
err_gadget:
	usb_del_gadget_udc(&pcd->gadget);
err_free:
	kfree(pcd);
	return retval;
}

/**
 * Cleanup the PCD.
 */
void dwc_otg_pcd_remove(struct device *dev)
{
	struct dwc_otg_device *otg_dev = dev_get_drvdata(dev);
	struct dwc_otg_pcd *pcd = otg_dev->pcd;
	struct dwc_otg_dev_if *dev_if = GET_CORE_IF(pcd)->dev_if;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, dev);

	/*
	 * Free the IRQ
	 */
	free_irq(otg_dev->irq, pcd);

	usb_del_gadget_udc(&pcd->gadget);

	/* start with the driver above us */
	if (pcd->driver) {

		/* should have been done already by driver model core */
		DWC_WARN("driver '%s' is still registered\n",
				pcd->driver->driver.name);
		usb_gadget_unregister_driver(pcd->driver);
	}
	if (GET_CORE_IF(pcd)->dma_enable) {

		dwc_otg_free_channel_dma(pcd);

		dma_free_coherent(dev, sizeof(*pcd->setup_pkt) * 5,
				   pcd->setup_pkt, pcd->setup_pkt_dma_handle);
		dma_free_coherent(dev, sizeof(uint16_t), pcd->status_buf,
				   pcd->status_buf_dma_handle);
		if (GET_CORE_IF(pcd)->dma_desc_enable) {
			dwc_otg_ep_free_desc_chain(dev,
						   dev_if->setup_desc_addr[0],
						   dev_if->
						   dma_setup_desc_addr[0], 1);
			dwc_otg_ep_free_desc_chain(dev,
						   dev_if->setup_desc_addr[1],
						   dev_if->
						   dma_setup_desc_addr[1], 1);
			dwc_otg_ep_free_desc_chain(dev,
						   dev_if->in_desc_addr,
						   dev_if->dma_in_desc_addr, 1);
			dwc_otg_ep_free_desc_chain(dev,
						   dev_if->out_desc_addr,
						   dev_if->dma_out_desc_addr,
						   1);

			dma_free_coherent(dev, PAGE_SIZE,
					  pcd->ep0.bounce_buffer,
					  pcd->ep0.bounce_buffer_dma);
		}
	} else {
		kfree(pcd->setup_pkt);
		kfree(pcd->status_buf);
	}

	kfree(pcd);
	otg_dev->pcd = NULL;
}

/**
 * This function registers a gadget driver with the PCD.
 *
 * When a driver is successfully registered, it will receive control
 * requests including set_configuration(), which enables non-control
 * requests.  then usb traffic follows until a disconnect is reported.
 * then a host may connect again, or the driver might get unbound.
 */
static int dwc_otg_pcd_gadget_start(struct usb_gadget *g,
				    struct usb_gadget_driver *driver)
{
	struct dwc_otg_pcd *pcd = to_dwc_otg_pcd(g);
	DWC_DEBUGPL(DBG_PCD, "registering gadget driver '%s'\n",
		      driver->driver.name);

	/* hook up the driver */
	pcd->driver = driver;
	pcd->gadget.dev.driver = &driver->driver;

	return 0;
}

/**
 * This function unregisters a gadget driver
 */
static int dwc_otg_pcd_gadget_stop(struct usb_gadget *g,
				   struct usb_gadget_driver *driver)
{
	struct dwc_otg_pcd *pcd = to_dwc_otg_pcd(g);

	dwc_otg_pcd_stop(pcd);

	pcd->gadget.dev.driver = NULL;
	pcd->driver = NULL;

	return 0;
}

#endif	/* DWC_HOST_ONLY */
