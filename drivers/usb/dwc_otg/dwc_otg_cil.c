/*==========================================================================
 *
 *Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 *"Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 *otherwise expressly agreed to in writing between Synopsys and you.
 *
 *The Software IS NOT an item of Licensed Software or Licensed Product under
 *any End User Software License Agreement or Agreement for Licensed Product
 *with Synopsys or any supplement thereto. You are permitted to use and
 *redistribute this Software in source and binary forms, with or without
 *modification, provided that redistributions of source code must retain this
 *notice. You may not view, use, disclose, copy or distribute this file or
 *any information contained herein except pursuant to this license grant from
 *Synopsys. If you do not agree with this notice, including the disclaimer
 *below, then you are not authorized to use the Software.
 *
 *THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 *AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 *INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *DAMAGE.
 *========================================================================== */


/**@file
 *
 *The Core Interface Layer provides basic services for accessing and
 *managing the DWC_otg hardware. These services are used by both the
 *Host Controller Driver and the Peripheral Controller Driver.
 *
 *The CIL manages the memory map for the core so that the HCD and PCD
 *don't have to do this separately. It also handles basic tasks like
 *reading/writing the registers and data FIFOs in the controller.
 *Some of the data access functions provide encapsulation of several
 *operations required to perform a task, such as writing multiple
 *registers to start a transfer. Finally, the CIL performs basic
 *services that are not specific to either the host or device modes
 *of operation. These services include management of the OTG Host
 *Negotiation Protocol (HNP) and Session Request Protocol (SRP). A
 *Diagnostic API is also provided to allow testing of the controller
 *hardware.
 *
 *The Core Interface Layer has the following requirements:
 *- Provides basic controller operations.
 *- Minimal use of OS services.
 *- The OS services used will be abstracted by using inline functions
 *	 or macros.
 *
 */
#include <asm/unaligned.h>
#include <linux/workqueue.h>
#include <linux/usb.h>

#ifdef DEBUG
#include <linux/jiffies.h>
#endif


#include "dwc_otg_driver.h"
#include "dwc_otg_regs.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_core_if.h"

#ifdef OTG_PPC_PLB_DMA_TASKLET
atomic_t release_later = ATOMIC_INIT(0);
#endif


/**
 *This function is called to initialize the DWC_otg CSR data
 *structures.	The register addresses in the device and host
 *structures are initialized from the base address supplied by the
 *caller.	The calling function must make the OS calls to get the
 *base address of the DWC_otg controller registers.  The core_params
 *argument holds the parameters that specify how the core should be
 *configured.
 *
 *@param[in] reg_base_addr Base address of DWC_otg core registers
 *@param[in] core_params Pointer to the core configuration parameters
 *
 */
struct dwc_otg_core_if *dwc_otg_cil_init(u32 __iomem *reg_base_addr,
	struct dwc_otg_core_params *core_params,
	struct dwc_otg_device *dwc_otg_device)
{
	struct dwc_otg_core_if *core_if = NULL;
	struct dwc_otg_dev_if *dev_if = NULL;
	struct dwc_otg_host_if *host_if = NULL;
	u8 __iomem *reg_base = (u8 __iomem *) reg_base_addr;
	int i = 0;
	DWC_DEBUGPL(DBG_CILV, "%s(%p,%p)\n", __func__, reg_base_addr,
		      core_params);
	core_if = kmalloc(sizeof(struct dwc_otg_core_if), GFP_KERNEL);
	if (!core_if) {
		DWC_DEBUGPL(DBG_CIL,
			"Allocation of struct dwc_otg_core_if failed\n");
		return NULL;
	}
	memset(core_if, 0, sizeof(struct dwc_otg_core_if));
	core_if->core_params = core_params;
	core_if->core_global_regs =
		(struct dwc_otg_core_global_regs __iomem *) reg_base;

	/*
	*Allocate the Device Mode structures.
	*/
	dev_if = kmalloc(sizeof(struct dwc_otg_dev_if), GFP_KERNEL);
	if (!dev_if) {
		DWC_DEBUGPL(DBG_CIL, "Allocation of struct d"
				"wc_otg_dev_if failed\n");
		kfree(core_if);
		return NULL;
	}
	dev_if->dev_global_regs =
		(struct dwc_otg_dev_global_regs __iomem *)(reg_base +
				DWC_DEV_GLOBAL_REG_OFFSET);
	/*
	 * NJ Bug fix: loop was looping for MAX EPS_CHANNELS which is
	 * the sum of both in and out EPs only need to loop for half this
	 * as setting up both in and out on each pass
	 */

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		dev_if->in_ep_regs[i] =
			(struct dwc_otg_dev_in_ep_regs __iomem *) (reg_base +
			DWC_DEV_IN_EP_REG_OFFSET + (i * DWC_EP_REG_OFFSET));
		dev_if->out_ep_regs[i] =
			(struct dwc_otg_dev_out_ep_regs __iomem *) (reg_base +
			DWC_DEV_OUT_EP_REG_OFFSET + (i * DWC_EP_REG_OFFSET));
		DWC_DEBUGPL(DBG_CILV, "in_ep_regs[%d]->diepctl=%p\n", i,
			     &dev_if->in_ep_regs[i]->diepctl);
		DWC_DEBUGPL(DBG_CILV, "out_ep_regs[%d]->doepctl=%p\n", i,
			     &dev_if->out_ep_regs[i]->doepctl);

	}
	dev_if->speed = 0;	/*unknown */
	core_if->dev_if = dev_if;
	core_if->otg_dev = dwc_otg_device;

	/*
	 * Allocate the Host Mode structures.
	 */
	host_if = kmalloc(sizeof(struct dwc_otg_host_if), GFP_KERNEL);
	if (!host_if) {
		DWC_DEBUGPL(DBG_CIL, "Allocation of struct "
				"dwc_otg_host_if failed\n");
		kfree(dev_if);
		kfree(core_if);
		return NULL;
	}
	host_if->host_global_regs = (struct dwc_otg_host_global_regs __iomem *)
	    (reg_base + DWC_OTG_HOST_GLOBAL_REG_OFFSET);
	host_if->hprt0 = (u32 __iomem *)
				(reg_base + DWC_OTG_HOST_PORT_REGS_OFFSET);

	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		host_if->hc_regs[i] = (struct dwc_otg_hc_regs __iomem *)
		    (reg_base + DWC_OTG_HOST_CHAN_REGS_OFFSET +
				    (i * DWC_OTG_CHAN_REGS_OFFSET));
		DWC_DEBUGPL(DBG_CILV, "hc_reg[%d]->hcchar=%p\n",
				i, &host_if->hc_regs[i]->hcchar);
	}

	host_if->num_host_channels = MAX_EPS_CHANNELS;
	core_if->host_if = host_if;
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		core_if->data_fifo[i] =
		    (u32 __iomem *) (reg_base + DWC_OTG_DATA_FIFO_OFFSET +
				  (i * DWC_OTG_DATA_FIFO_SIZE));
		DWC_DEBUGPL(DBG_CILV, "data_fifo[%d]=0x%p\n", i,
			    core_if->data_fifo[i]);
	}
	core_if->pcgcctl = (u32 __iomem *) (reg_base + DWC_OTG_PCGCCTL_OFFSET);

	/*Initiate lx_state to L3 disconnected state */
	core_if->lx_state = DWC_OTG_L3;
	/*
	*Store the contents of the hardware configuration registers here for
	*easy access later.
	*/
	core_if->hwcfg1.d32 =
		dwc_read_reg32(&core_if->core_global_regs->ghwcfg1);
	core_if->hwcfg2.d32 =
		dwc_read_reg32(&core_if->core_global_regs->ghwcfg2);
#ifdef DWC_SLAVE
	core_if->hwcfg2.b.architecture =  DWC_SLAVE_ONLY_ARCH;
#endif
	core_if->hwcfg3.d32 =
		dwc_read_reg32(&core_if->core_global_regs->ghwcfg3);
	core_if->hwcfg4.d32 =
		dwc_read_reg32(&core_if->core_global_regs->ghwcfg4);

	DWC_DEBUGPL(DBG_CILV, "hwcfg1=%08x\n", core_if->hwcfg1.d32);
	DWC_DEBUGPL(DBG_CILV, "hwcfg2=%08x\n", core_if->hwcfg2.d32);
	DWC_DEBUGPL(DBG_CILV, "hwcfg3=%08x\n", core_if->hwcfg3.d32);
	DWC_DEBUGPL(DBG_CILV, "hwcfg4=%08x\n", core_if->hwcfg4.d32);
	core_if->hcfg.d32 =
	    dwc_read_reg32(&core_if->host_if->host_global_regs->hcfg);
	core_if->dcfg.d32 =
	    dwc_read_reg32(&core_if->dev_if->dev_global_regs->dcfg);

	DWC_DEBUGPL(DBG_CILV, "hcfg=%08x\n", core_if->hcfg.d32);
	DWC_DEBUGPL(DBG_CILV, "dcfg=%08x\n", core_if->dcfg.d32);
	DWC_DEBUGPL(DBG_CILV, "op_mode=%0x\n", core_if->hwcfg2.b.op_mode);
	DWC_DEBUGPL(DBG_CILV, "arch=%0x\n", core_if->hwcfg2.b.architecture);
	DWC_DEBUGPL(DBG_CILV, "num_dev_ep=%d\n",
			core_if->hwcfg2.b.num_dev_ep + 1);
	DWC_DEBUGPL(DBG_CILV, "num_host_chan=%d\n",
			core_if->hwcfg2.b.num_host_chan);
	DWC_DEBUGPL(DBG_CILV, "nonperio_tx_q_depth=0x%0x\n",
		     core_if->hwcfg2.b.nonperio_tx_q_depth);
	DWC_DEBUGPL(DBG_CILV, "host_perio_tx_q_depth=0x%0x\n",
		     core_if->hwcfg2.b.host_perio_tx_q_depth);
	DWC_DEBUGPL(DBG_CILV, "dev_token_q_depth=0x%0x\n",
		     core_if->hwcfg2.b.dev_token_q_depth);
	DWC_DEBUGPL(DBG_CILV, "Total FIFO SZ=%d\n",
		      core_if->hwcfg3.b.dfifo_depth);
	DWC_DEBUGPL(DBG_CILV, "xfer_size_cntr_width=%0x\n",
		     core_if->hwcfg3.b.xfer_size_cntr_width);

	/*
	 *Set the SRP sucess bit for FS-I2c
	 */
	core_if->srp_success = 0;
	core_if->srp_timer_started = 0;

	/*
	 *Create new workqueue and init works
	 */
	core_if->wq_otg = create_singlethread_workqueue("dwc_otg");
	if (!core_if->wq_otg) {
		DWC_WARN("DWC_WORKQ_ALLOC failed\n");
		kfree(host_if);
		kfree(dev_if);
		kfree(core_if);
		return NULL;
	}
	core_if->snpsid = dwc_read_reg32(&core_if->core_global_regs->gsnpsid);

	DWC_PRINT("Core Release: %x.%x%x%x\n",
		   (core_if->snpsid >> 12 & 0xF),
		   (core_if->snpsid >> 8 & 0xF),
		   (core_if->snpsid >> 4 & 0xF), (core_if->snpsid & 0xF));

	core_if->wkp_timer = kzalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (!core_if->wkp_timer) {
		DWC_WARN(" Timer alloc failed\n");
		kfree(host_if);
		kfree(dev_if);
		destroy_workqueue(core_if->wq_otg);
		kfree(core_if);
		return NULL;
	}
	core_if->wkp_timer->function = w_wakeup_detected;
	core_if->wkp_timer->data = (unsigned long)core_if;
	init_timer(core_if->wkp_timer);

	return core_if;
}


/**
 *This function frees the structures allocated by dwc_otg_cil_init().
 *
 *@param[in] core_if The core interface pointer returned from
 *dwc_otg_cil_init().
 *
 */
void dwc_otg_cil_remove(struct dwc_otg_core_if *core_if)
{
	/*Disable all interrupts */
	dwc_modify_reg32(&core_if->core_global_regs->gahbcfg, 1, 0);
	dwc_write_reg32(&core_if->core_global_regs->gintmsk, 0);
	if (core_if->wq_otg) {
		flush_workqueue(core_if->wq_otg);
		destroy_workqueue(core_if->wq_otg);
	}

	/*kfree(NULL) is safe so checks for null removed (NJ)*/
	kfree(core_if->dev_if);
	kfree(core_if->host_if);
	kfree(core_if->wkp_timer);
	kfree(core_if);
}


/**
 *This function enables the controller's Global Interrupt in the AHB Config
 *register.
 *
 *@param[in] core_if Programming view of DWC_otg controller.
 */
void dwc_otg_enable_global_interrupts(struct dwc_otg_core_if *core_if)
{
	union gahbcfg_data ahbcfg = {.d32 = 0};
	ahbcfg.b.glblintrmsk = 1;	/*Enable interrupts */
	dwc_modify_reg32(&core_if->core_global_regs->gahbcfg, 0, ahbcfg.d32);
}

/**
 *This function disables the controller's Global Interrupt in the AHB Config
 *register.
 *
 *@param[in] core_if Programming view of DWC_otg controller.
 */
void dwc_otg_disable_global_interrupts(struct dwc_otg_core_if *core_if)
{
	union gahbcfg_data ahbcfg = {.d32 = 0};
	ahbcfg.b.glblintrmsk = 1;	/*Enable interrupts */
	dwc_modify_reg32(&core_if->core_global_regs->gahbcfg, ahbcfg.d32, 0);
}

/**
 *This function initializes the commmon interrupts, used in both
 *device and host modes.
 *
 *@param[in] core_if Programming view of the DWC_otg controller
 *
 */
static void dwc_otg_enable_common_interrupts(struct dwc_otg_core_if *core_if)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	union gintmsk_data intr_mask = {.d32 = 0};

	/*Clear any pending OTG Interrupts */
	dwc_write_reg32(&global_regs->gotgint, 0xFFFFFFFF);

	/*Clear any pending interrupts */
	dwc_write_reg32(&global_regs->gintsts, 0xFFFFFFFF);

	/*
	 *Enable the interrupts in the GINTMSK.
	 */
	intr_mask.b.modemismatch = 1;
	intr_mask.b.otgintr = 1;
	if (!core_if->dma_enable)
		intr_mask.b.rxstsqlvl = 1;

	intr_mask.b.conidstschng = 1;
	intr_mask.b.wkupintr = 1;
	intr_mask.b.disconnect = 1;
	intr_mask.b.usbsuspend = 1;
	intr_mask.b.sessreqintr = 1;
#ifdef CONFIG_USB_DWC_OTG_LPM
	if (core_if->core_params->lpm_enable)
		intr_mask.b.lpmtranrcvd = 1;
#endif
	dwc_write_reg32(&global_regs->gintmsk, intr_mask.d32);
}


/**
 *Initializes the FSLSPClkSel field of the HCFG register depending on the PHY
 *type.
 */
static void init_fslspclksel(struct dwc_otg_core_if *core_if)
{
	u32 val;
	union hcfg_data hcfg;
	if (((core_if->hwcfg2.b.hs_phy_type == 2) &&
		(core_if->hwcfg2.b.fs_phy_type == 1) &&
		(core_if->core_params->ulpi_fs_ls)) ||
		(core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS))
			/*Full speed PHY */
			val = DWC_HCFG_48_MHZ;
	else
		/*High speed PHY running at full speed or high speed */
		val = DWC_HCFG_30_60_MHZ;

	DWC_DEBUGPL(DBG_CIL, "Initializing HCFG.FSLSPClkSel to 0x%1x\n", val);
	hcfg.d32 = dwc_read_reg32(&core_if->host_if->host_global_regs->hcfg);
	hcfg.b.fslspclksel = val;
	dwc_write_reg32(&core_if->host_if->host_global_regs->hcfg, hcfg.d32);
}


/**
 *Initializes the DevSpd field of the DCFG register depending on the PHY type
 *and the enumeration speed of the device.
 */
static void init_devspd(struct dwc_otg_core_if *core_if)
{
	u32 val;
	union dcfg_data dcfg;
	if (((core_if->hwcfg2.b.hs_phy_type == 2) &&
		(core_if->hwcfg2.b.fs_phy_type == 1) &&
		(core_if->core_params->ulpi_fs_ls)) ||
		(core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS))
			/*Full speed PHY */
			val = 0x3;
	else if (core_if->core_params->speed == DWC_SPEED_PARAM_FULL)
		/*High speed PHY running at full speed */
		val = 0x1;
	else
		/*High speed PHY running at high speed */
		val = 0x0;

	DWC_DEBUGPL(DBG_CIL, "Initializing DCFG.DevSpd to 0x%1x\n", val);
	dcfg.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dcfg);
	dcfg.b.devspd = val;
	dwc_write_reg32(&core_if->dev_if->dev_global_regs->dcfg, dcfg.d32);
}


/**
 *This function calculates the number of IN EPS
 *using GHWCFG1 and GHWCFG2 registers values
 *
 *@param _pcd the pcd structure.
 */
static u32 calc_num_in_eps(struct dwc_otg_core_if *core_if)
{
	u32 num_in_eps = 0;
	u32 num_eps = core_if->hwcfg2.b.num_dev_ep;
	u32 hwcfg1 = core_if->hwcfg1.d32 >> 3;
	u32 num_tx_fifos = core_if->hwcfg4.b.num_in_eps;
	int i;
	for (i = 0; i < num_eps; ++i) {
		if (!(hwcfg1 & 0x1))
			num_in_eps++;
		hwcfg1 >>= 2;
	}
	if (core_if->hwcfg4.b.ded_fifo_en) {
		num_in_eps = (num_in_eps > num_tx_fifos) ?
				num_tx_fifos : num_in_eps;
	}
	return num_in_eps;
}


/**
 *This function calculates the number of OUT EPS
 *using GHWCFG1 and GHWCFG2 registers values
 *
 *@param _pcd the pcd structure.
 */
static u32 calc_num_out_eps(struct dwc_otg_core_if *core_if)
{
	u32 num_out_eps = 0;
	u32 num_eps = core_if->hwcfg2.b.num_dev_ep;
	u32 hwcfg1 = core_if->hwcfg1.d32 >> 2;
	int i;
	for (i = 0; i < num_eps; ++i) {
		if (!(hwcfg1 & 0x1))
			num_out_eps++;
		hwcfg1 >>= 2;
	}
	return num_out_eps;
}


/**
 *This function initializes the DWC_otg controller registers and
 *prepares the core for device mode or host mode operation.
 *
 *@param core_if Programming view of the DWC_otg controller
 *
 */
void dwc_otg_core_init(struct dwc_otg_core_if *core_if)
{
	int i = 0;
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	union gahbcfg_data ahbcfg = {.d32 = 0};
	union gusbcfg_data usbcfg = {.d32 = 0};
	union gi2cctl_data i2cctl = {.d32 = 0};
	DWC_DEBUGPL(DBG_CILV, "dwc_otg_core_init(%p)\n", core_if);

	/*Common Initialization */
	usbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
	DWC_DEBUGPL(DBG_CIL, "USB config register: 0x%08x\n", usbcfg.d32);

	/*Program the ULPI External VBUS bit if needed */
#if defined(OTG_EXT_CHG_PUMP) || defined(CONFIG_460EX)
	usbcfg.b.ulpi_ext_vbus_drv = 1;
#else
	/*usbcfg.b.ulpi_ext_vbus_drv = 0;*/
	usbcfg.b.ulpi_ext_vbus_drv =
	(core_if->core_params->phy_ulpi_ext_vbus ==
		DWC_PHY_ULPI_EXTERNAL_VBUS) ? 1 : 0;
#endif

	/*Set external TS Dline pulsing */
	usbcfg.b.term_sel_dl_pulse = (core_if->core_params->ts_dline == 1) ?
			1 : 0;
	dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);

	/*Reset the Controller */
	dwc_otg_core_reset(core_if);

	/*Initialize parameters from Hardware configuration registers. */
	dev_if->num_in_eps = calc_num_in_eps(core_if);
	dev_if->num_out_eps = calc_num_out_eps(core_if);

	DWC_DEBUGPL(DBG_CIL, "num_dev_perio_in_ep=%d\n",
		       core_if->hwcfg4.b.num_dev_perio_in_ep);
	DWC_DEBUGPL(DBG_CIL, "Is power optimization enabled?  %s\n",
		     core_if->hwcfg4.b.power_optimiz ? "Yes" : "No");
	DWC_DEBUGPL(DBG_CIL, "vbus_valid filter enabled?  %s\n",
		     core_if->hwcfg4.b.vbus_valid_filt_en ? "Yes" : "No");
	DWC_DEBUGPL(DBG_CIL, "iddig filter enabled?  %s\n",
		     core_if->hwcfg4.b.iddig_filt_en ? "Yes" : "No");

	for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep; i++) {
		dev_if->perio_tx_fifo_size[i] =
		    dwc_read_reg32(&global_regs->dptxfsiz_dieptxf[i]) >> 16;
		DWC_DEBUGPL(DBG_CIL, "Periodic Tx FIFO SZ #%d=0x%0x\n", i,
			     dev_if->perio_tx_fifo_size[i]);
	}
	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
		dev_if->tx_fifo_size[i] =
		    dwc_read_reg32(&global_regs->dptxfsiz_dieptxf[i]) >> 16;
		DWC_DEBUGPL(DBG_CIL, "Tx FIFO SZ #%d=0x%0x\n", i,
			     dev_if->perio_tx_fifo_size[i]);
	}
	core_if->total_fifo_size = core_if->hwcfg3.b.dfifo_depth;
	core_if->rx_fifo_size = dwc_read_reg32(&global_regs->grxfsiz);
	core_if->nperio_tx_fifo_size =
		dwc_read_reg32(&global_regs->gnptxfsiz) >> 16;
	DWC_DEBUGPL(DBG_CIL, "Total FIFO SZ=%d\n", core_if->total_fifo_size);
	DWC_DEBUGPL(DBG_CIL, "Rx FIFO SZ=%d\n", core_if->rx_fifo_size);
	DWC_DEBUGPL(DBG_CIL, "NP Tx FIFO SZ=%d\n",
			core_if->nperio_tx_fifo_size);


	/*
	 * This programming sequence needs to happen in FS mode before any other
	 * programming occurs
	 */
	if ((core_if->core_params->speed == DWC_SPEED_PARAM_FULL) &&
		(core_if->core_params->phy_type == DWC_PHY_TYPE_PARAM_FS)) {

		/*If FS mode with FS PHY */

		/*
		 * core_init() is now called on every switch so only call the
		 * following for the first time through.
		 */
		if (!core_if->phy_init_done) {
			core_if->phy_init_done = 1;
			DWC_DEBUGPL(DBG_CIL, "FS_PHY detected\n");
			usbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
			usbcfg.b.physel = 1;
			dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);

			/*Reset after a PHY select */
			dwc_otg_core_reset(core_if);
		}

		/* Program DCFG.DevSpd or HCFG.FSLSPclkSel to 48Mhz in FS.
		 * Do this on HNP Dev/Host mode switches (done in dev_init and
		 * host_init).
		 */
		if (dwc_otg_is_host_mode(core_if)) {
			DWC_DEBUGPL(DBG_CIL, "host mode\n");
			init_fslspclksel(core_if);
		} else {
			DWC_DEBUGPL(DBG_CIL, "device mode\n");
			init_devspd(core_if);
		}

		if (core_if->core_params->i2c_enable) {
			DWC_DEBUGPL(DBG_CIL, "FS_PHY Enabling I2c\n");

			/*Program GUSBCFG.OtgUtmifsSel to I2C */
			usbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
			usbcfg.b.otgutmifssel = 1;
			dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);

			/*Program GI2CCTL.I2CEn */
			i2cctl.d32 = dwc_read_reg32(&global_regs->gi2cctl);
			i2cctl.b.i2cdevaddr = 1;
			i2cctl.b.i2cen = 0;
			dwc_write_reg32(&global_regs->gi2cctl, i2cctl.d32);
			i2cctl.b.i2cen = 1;
			dwc_write_reg32(&global_regs->gi2cctl, i2cctl.d32);
		}
	}	/*endif speed == DWC_SPEED_PARAM_FULL */
	else {
		/*High speed PHY. */
		if (!core_if->phy_init_done) {
			core_if->phy_init_done = 1;
			DWC_DEBUGPL(DBG_CIL, "High speed PHY\n");
			/*HS PHY parameters.  These parameters are preserved
			 *during soft reset so only program the first time.  Do
			 *a soft reset immediately after setting phyif.
			 */
			usbcfg.b.ulpi_utmi_sel = (core_if->core_params->phy_type != 1);
			if (usbcfg.b.ulpi_utmi_sel) {
				DWC_DEBUGPL(DBG_CIL, "ULPI\n");
				/*ULPI interface */
				usbcfg.b.phyif = 0;
				usbcfg.b.ddrsel =
					core_if->core_params->phy_ulpi_ddr;
			} else {
				/*UTMI+ interface */
				if (core_if->core_params->phy_utmi_width
						== 16) {
					usbcfg.b.phyif = 1;
					DWC_DEBUGPL(DBG_CIL, "UTMI+ 16\n");
				} else {
					DWC_DEBUGPL(DBG_CIL, "UTMI+ 8\n");
					usbcfg.b.phyif = 0;
				}
			}
			dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);
			/*Reset after setting the PHY parameters */
			dwc_otg_core_reset(core_if);
		}
	}
	if ((core_if->hwcfg2.b.hs_phy_type == 2) &&
		(core_if->hwcfg2.b.fs_phy_type == 1) &&
		(core_if->core_params->ulpi_fs_ls)) {
		DWC_DEBUGPL(DBG_CIL, "Setting ULPI FSLS\n");
		usbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
		usbcfg.b.ulpi_fsls = 1;
		usbcfg.b.ulpi_clk_sus_m = 1;
		dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);
	} else {
		DWC_DEBUGPL(DBG_CIL, "Setting ULPI FSLS=0\n");
		usbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
		usbcfg.b.ulpi_fsls = 0;
		usbcfg.b.ulpi_clk_sus_m = 0;
		dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);
	}

	/*Program the GAHBCFG Register. */
	switch (core_if->hwcfg2.b.architecture) {

	case DWC_SLAVE_ONLY_ARCH:
		DWC_DEBUGPL(DBG_CIL, "Slave Only Mode\n");
		ahbcfg.b.nptxfemplvl_txfemplvl =
			DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY;
		ahbcfg.b.ptxfemplvl = DWC_GAHBCFG_TXFEMPTYLVL_HALFEMPTY;
		core_if->dma_enable = 0;
		core_if->dma_desc_enable = 0;
		break;
	case DWC_EXT_DMA_ARCH:
		DWC_DEBUGPL(DBG_CIL, "External DMA Mode\n");
		ahbcfg.b.hburstlen = core_if->core_params->dma_burst_size;
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		core_if->dma_desc_enable =
			(core_if->core_params->dma_desc_enable != 0);
		break;
	case DWC_INT_DMA_ARCH:
		DWC_DEBUGPL(DBG_CIL, "Internal DMA Mode\n");
		ahbcfg.b.hburstlen = DWC_GAHBCFG_INT_DMA_BURST_INCR;
		core_if->dma_enable = (core_if->core_params->dma_enable != 0);
		core_if->dma_desc_enable =
			(core_if->core_params->dma_desc_enable != 0);
		break;
	}
	if (core_if->dma_enable) {
		if (core_if->dma_desc_enable)
			DWC_PRINT("Using Descriptor DMA mode\n");
		else
			DWC_PRINT("Using Buffer DMA mode\n");

	} else {
		DWC_PRINT("Using Slave mode\n");
		core_if->dma_desc_enable = 0;
	}
	ahbcfg.b.dmaenable = core_if->dma_enable;
	dwc_write_reg32(&global_regs->gahbcfg, ahbcfg.d32);
	core_if->en_multiple_tx_fifo = core_if->hwcfg4.b.ded_fifo_en;

	core_if->pti_enh_enable = core_if->core_params->pti_enable != 0;
	core_if->multiproc_int_enable = core_if->core_params->mpi_enable;
	DWC_PRINT("Periodic Transfer Interrupt Enhancement - %s\n",
		   ((core_if->pti_enh_enable) ? "enabled" : "disabled"));
	DWC_PRINT("Multiprocessor Interrupt Enhancement - %s\n",
		   ((core_if->multiproc_int_enable) ? "enabled" : "disabled"));
	/*
	 * Program the GUSBCFG register.
	 */
	usbcfg.d32 = dwc_read_reg32(&global_regs->gusbcfg);
	switch (core_if->hwcfg2.b.op_mode) {
	case DWC_MODE_HNP_SRP_CAPABLE:
		usbcfg.b.hnpcap = (core_if->core_params->otg_cap ==
			DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE);
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
			DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;
	case DWC_MODE_SRP_ONLY_CAPABLE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
			DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;
	case DWC_MODE_NO_HNP_SRP_CAPABLE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;
	case DWC_MODE_SRP_CAPABLE_DEVICE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
			DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;
	case DWC_MODE_NO_SRP_CAPABLE_DEVICE:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;
	case DWC_MODE_SRP_CAPABLE_HOST:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = (core_if->core_params->otg_cap !=
			DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		break;
	case DWC_MODE_NO_SRP_CAPABLE_HOST:
		usbcfg.b.hnpcap = 0;
		usbcfg.b.srpcap = 0;
		break;
	}
	dwc_write_reg32(&global_regs->gusbcfg, usbcfg.d32);

#ifdef CONFIG_USB_DWC_OTG_LPM
	if (core_if->core_params->lpm_enable) {
		union glpmcfg_data lpmcfg = {.d32 = 0 };

		/*To enable LPM support set lpm_cap_en bit */
		lpmcfg.b.lpm_cap_en = 1;

		/*Make AppL1Res ACK */
		lpmcfg.b.appl_resp = 1;

		/*Retry 3 times */
		lpmcfg.b.retry_count = 3;

		dwc_modify_reg32(&core_if->core_global_regs->glpmcfg,
				 0, lpmcfg.d32);

	}
#endif
	if (core_if->core_params->ic_usb_cap) {
		union gusbcfg_data gusbcfg = {.d32 = 0 };
		gusbcfg.b.ic_usb_cap = 1;
		dwc_modify_reg32(&core_if->core_global_regs->gusbcfg,
				 0, gusbcfg.d32);
	}
	/*Enable common interrupts */
	dwc_otg_enable_common_interrupts(core_if);

	/*Do device or host intialization based on mode during PCD
	*and HCD initialization
	 */
	if (dwc_otg_is_host_mode(core_if)) {
		DWC_DEBUGPL(DBG_ANY, "Host Mode\n");
		core_if->op_state = A_HOST;
	} else {
		DWC_DEBUGPL(DBG_ANY, "Device Mode\n");
		core_if->op_state = B_PERIPHERAL;
#ifdef DWC_DEVICE_ONLY
		dwc_otg_core_dev_init(core_if);
#endif	/* */
	}
}


/**
 *This function enables the Device mode interrupts.
 *
 *@param core_if Programming view of DWC_otg controller
 */
void dwc_otg_enable_device_interrupts(struct dwc_otg_core_if *core_if)
{
	union gintmsk_data intr_mask = {.d32 = 0};
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	DWC_DEBUGPL(DBG_CIL, "%s()\n", __func__);

#if 0
	/*
	 * Removed as this clears down any host mode
	 * ints that have been setup
	 */

	/*Disable all interrupts. */
	dwc_write_reg32(&global_regs->gintmsk, 0);
#endif

	/*Clear any pending interrupts */
	dwc_write_reg32(&global_regs->gintsts, 0xFFFFFFFF);

#if 0
	/*
	 * Removed as this clears down any host mode
	 * ints that have been setup and has already been called.
	 */

	/*Enable the common interrupts */
	dwc_otg_enable_common_interrupts(core_if);
#endif

	/*Enable interrupts */
	intr_mask.b.usbreset = 1;
	intr_mask.b.enumdone = 1;
	if (!core_if->multiproc_int_enable) {
		intr_mask.b.inepintr = 1;
		intr_mask.b.outepintr = 1;
	}
	intr_mask.b.erlysuspend = 1;
	if (core_if->en_multiple_tx_fifo == 0)
		intr_mask.b.epmismatch = 1;

	dwc_modify_reg32(&global_regs->gintmsk, intr_mask.d32,
			      intr_mask.d32);

	DWC_DEBUGPL(DBG_CIL, "%s() gintmsk=%0x\n", __func__,
		      dwc_read_reg32(&global_regs->gintmsk));
}


/**
 *This function initializes the DWC_otg controller registers for
 *device mode.
 *
 *@param core_if Programming view of DWC_otg controller
 *
 */
void dwc_otg_core_dev_init(struct dwc_otg_core_if *core_if)
{
	int i, ptxfifosize_each;
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	struct dwc_otg_core_params *params = core_if->core_params;
	union dcfg_data dcfg = {.d32 = 0};
	union grstctl_data resetctl = {.d32 = 0};
	u32 rx_fifo_size;
	union fifosize_data nptxfifosize;
	union fifosize_data txfifosize;
	union dthrctl_data dthrctl;
	union fifosize_data ptxfifosize;

	/*Restart the Phy Clock */
	dwc_write_reg32(core_if->pcgcctl, 0);

	/*Device configuration register */
	init_devspd(core_if);
	dcfg.d32 = dwc_read_reg32(&dev_if->dev_global_regs->dcfg);
	dcfg.b.descdma = (core_if->dma_desc_enable) ? 1 : 0;
	dcfg.b.perfrint = DWC_DCFG_FRAME_INTERVAL_80;
	dwc_write_reg32(&dev_if->dev_global_regs->dcfg, dcfg.d32);

	/*Configure data FIFO sizes */
	if (core_if->hwcfg2.b.dynamic_fifo && params->enable_dynamic_fifo) {
		/*Split the Fifo equally*/
		DWC_DEBUGPL(DBG_CIL, "Total FIFO Size=%d\n",
			    core_if->total_fifo_size);
		DWC_DEBUGPL(DBG_CIL, "Rx FIFO Size=%d\n",
			    core_if->total_fifo_size/3);
		DWC_DEBUGPL(DBG_CIL, "NP Tx FIFO Size=%d\n",
			    core_if->total_fifo_size/3);

		/*Rx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial grxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->grxfsiz));

		rx_fifo_size = core_if->total_fifo_size/3;
		dwc_write_reg32(&global_regs->grxfsiz, rx_fifo_size);
		DWC_DEBUGPL(DBG_CIL, "new grxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->grxfsiz));

		/**Set Periodic Tx FIFO Mask all bits 0 */
		core_if->p_tx_msk = 0;

		/**Set Tx FIFO Mask all bits 0 */
		core_if->tx_msk = 0;
		if (core_if->en_multiple_tx_fifo == 0) {
			/*Non-periodic Tx FIFO */
			DWC_DEBUGPL(DBG_CIL, "initial gnptxfsiz=%08x\n",
				dwc_read_reg32(&global_regs->gnptxfsiz));
			nptxfifosize.b.depth = core_if->total_fifo_size/3;
			nptxfifosize.b.startaddr = core_if->total_fifo_size/3;
			dwc_write_reg32(&global_regs->gnptxfsiz,
					nptxfifosize.d32);
			DWC_DEBUGPL(DBG_CIL, "new gnptxfsiz=%08x\n",
				      dwc_read_reg32(&global_regs->gnptxfsiz));

			/**@todo NGS: Fix Periodic FIFO Sizing! */

			/*
			 * Periodic Tx FIFOs These FIFOs are numbered from 1 to
			 * 15.Indexes of the FIFO size module parameters in the
			 * dev_perio_tx_fifo_size array and the FIFO size
			 * registers in the dptxfsiz array run from 0 to 14.
			 */

			ptxfifosize_each = (core_if->total_fifo_size/3) /
					   core_if->hwcfg4.b.num_dev_perio_in_ep;

			ptxfifosize.b.startaddr =
				nptxfifosize.b.startaddr + nptxfifosize.b.depth;
			for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep;
			    i++) {
				ptxfifosize.b.depth = ptxfifosize_each;

				DWC_DEBUGPL(DBG_CIL,
					    "initial dptxfsiz_dieptxf[%d]"
					    "=%08x\n",
					    i,
					    dwc_read_reg32(&global_regs->
							   dptxfsiz_dieptxf
							   [i]));
				dwc_write_reg32(&global_regs->
						dptxfsiz_dieptxf[i],
						ptxfifosize.d32);
				DWC_DEBUGPL(DBG_CIL,
					    "new dptxfsiz_dieptxf[%d]=%08x\n",
					    i,
					    dwc_read_reg32(&global_regs->
							   dptxfsiz_dieptxf
							   [i]));
				ptxfifosize.b.startaddr += ptxfifosize.b.depth;
			}
		} else {

			/*
			 * Tx FIFOs These FIFOs are numbered from 1 to 15.
			 * Indexes of the FIFO size module parameters in the
			 * dev_tx_fifo_size array and the FIFO size registers in
			 * the dptxfsiz_dieptxf array run from 0 to 14.
			 */

			/*Non-periodic Tx FIFO */
			DWC_DEBUGPL(DBG_CIL, "initial gnptxfsiz=%08x\n",
				dwc_read_reg32(&global_regs->gnptxfsiz));

			nptxfifosize.b.depth = core_if->total_fifo_size/3;
			nptxfifosize.b.startaddr = core_if->total_fifo_size/3;
			dwc_write_reg32(&global_regs->gnptxfsiz,
					nptxfifosize.d32);
			DWC_DEBUGPL(DBG_CIL, "new gnptxfsiz=%08x\n",
				      dwc_read_reg32(&global_regs->gnptxfsiz));
			txfifosize.b.startaddr =
			    nptxfifosize.b.startaddr + nptxfifosize.b.depth;

			ptxfifosize_each = (core_if->total_fifo_size/3) /
					   core_if->hwcfg4.b.num_in_eps;


			for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {

				txfifosize.b.depth = ptxfifosize_each;

				DWC_DEBUGPL(DBG_CIL,
					    "initial dptxfsiz_dieptxf[%d]"
					    "=%08x\n",
					    i,
					    dwc_read_reg32(&global_regs->
							   dptxfsiz_dieptxf
							   [i]));

				dwc_write_reg32(&global_regs->
						dptxfsiz_dieptxf[i],
						txfifosize.d32);

				DWC_DEBUGPL(DBG_CIL,
					    "new dptxfsiz_dieptxf[%d]=%08x\n",
					    i,
					    dwc_read_reg32(&global_regs->
							   dptxfsiz_dieptxf
							   [i]));
				txfifosize.b.startaddr += txfifosize.b.depth;
			}
		}
	}

	/*Flush the FIFOs */
	dwc_otg_flush_tx_fifo(core_if, 0x10);	/*all Tx FIFOs */
	dwc_otg_flush_rx_fifo(core_if);

	/*Flush the Learning Queue. */
	resetctl.b.intknqflsh = 1;
	dwc_write_reg32(&core_if->core_global_regs->grstctl, resetctl.d32);

	/*Clear all pending Device Interrupts */
	if (core_if->multiproc_int_enable) {
		for (i = 0; i < core_if->dev_if->num_in_eps; ++i) {
			dwc_write_reg32(&dev_if->dev_global_regs->
					diepeachintmsk[i], 0);
		}

		for (i = 0; i < core_if->dev_if->num_out_eps; ++i) {
			dwc_write_reg32(&dev_if->dev_global_regs->
					doepeachintmsk[i], 0);
		}

		dwc_write_reg32(&dev_if->dev_global_regs->deachint, 0xFFFFFFFF);
		dwc_write_reg32(&dev_if->dev_global_regs->deachintmsk, 0);
	} else {
		dwc_write_reg32(&dev_if->dev_global_regs->diepmsk, 0);
		dwc_write_reg32(&dev_if->dev_global_regs->doepmsk, 0);
		dwc_write_reg32(&dev_if->dev_global_regs->daint, 0xFFFFFFFF);
		dwc_write_reg32(&dev_if->dev_global_regs->daintmsk, 0);
	}
	for (i = 0; i <= dev_if->num_in_eps; i++) {
		union depctl_data depctl;
		depctl.d32 = dwc_read_reg32(&dev_if->in_ep_regs[i]->diepctl);
		if (depctl.b.epena) {
			depctl.d32 = 0;
			depctl.b.epdis = 1;
			depctl.b.snak = 1;
		} else {
			depctl.d32 = 0;
		}
		dwc_write_reg32(&dev_if->in_ep_regs[i]->diepctl, depctl.d32);
		dwc_write_reg32(&dev_if->in_ep_regs[i]->dieptsiz, 0);
		dwc_write_reg32(&dev_if->in_ep_regs[i]->diepdma, 0);
		dwc_write_reg32(&dev_if->in_ep_regs[i]->diepint, 0xFF);
	}
	for (i = 0; i <= dev_if->num_out_eps; i++) {
		union depctl_data depctl;
		depctl.d32 = dwc_read_reg32(&dev_if->out_ep_regs[i]->doepctl);
		if (depctl.b.epena) {
			depctl.d32 = 0;
			depctl.b.epdis = 1;
			depctl.b.snak = 1;
		} else {
			depctl.d32 = 0;
		}
		dwc_write_reg32(&dev_if->out_ep_regs[i]->doepctl, depctl.d32);
		dwc_write_reg32(&dev_if->out_ep_regs[i]->doeptsiz, 0);
		dwc_write_reg32(&dev_if->out_ep_regs[i]->doepdma, 0);
		dwc_write_reg32(&dev_if->out_ep_regs[i]->doepint, 0xFF);
	}
	if (core_if->en_multiple_tx_fifo && core_if->dma_enable) {
		dev_if->non_iso_tx_thr_en = params->thr_ctl & 0x1;
		dev_if->iso_tx_thr_en = (params->thr_ctl >> 1) & 0x1;
		dev_if->rx_thr_en = (params->thr_ctl >> 2) & 0x1;

		dev_if->rx_thr_length = params->rx_thr_length;
		dev_if->tx_thr_length = params->tx_thr_length;
		dev_if->setup_desc_index = 0;
		dthrctl.d32 = 0;
		dthrctl.b.non_iso_thr_en = dev_if->non_iso_tx_thr_en;
		dthrctl.b.iso_thr_en = dev_if->iso_tx_thr_en;
		dthrctl.b.tx_thr_len = dev_if->tx_thr_length;
		dthrctl.b.rx_thr_en = dev_if->rx_thr_en;
		dthrctl.b.rx_thr_len = dev_if->rx_thr_length;
		dthrctl.b.ahb_thr_ratio = params->ahb_thr_ratio;
		dwc_write_reg32(&dev_if->dev_global_regs->dtknqr3_dthrctl,
				dthrctl.d32);

		DWC_DEBUGPL(DBG_CIL,
			    "Non ISO Tx Thr - %d\nISO Tx Thr - %d\n"
			    "Rx Thr - %d\nTx Thr Len - %d\nRx Thr Len - %d\n",
			    dthrctl.b.non_iso_thr_en, dthrctl.b.iso_thr_en,
			    dthrctl.b.rx_thr_en, dthrctl.b.tx_thr_len,
			    dthrctl.b.rx_thr_len);
	}
	dwc_otg_enable_device_interrupts(core_if);
	{
		union diepint_data msk = {.d32 = 0};
		msk.b.txfifoundrn = 1;
		if (core_if->multiproc_int_enable) {
			dwc_modify_reg32(&dev_if->dev_global_regs->
					 diepeachintmsk[0], msk.d32, msk.d32);
		} else {
			dwc_modify_reg32(&dev_if->dev_global_regs->diepmsk,
					 msk.d32, msk.d32);
		}
	}

	if (core_if->multiproc_int_enable) {
		/*Set NAK on Babble */
		union dctl_data dctl = {.d32 = 0 };
		dctl.b.nakonbble = 1;
		dwc_modify_reg32(&dev_if->dev_global_regs->dctl, 0, dctl.d32);
	}
}

/**
 *This function enables the Host mode interrupts.
 *
 *@param core_if Programming view of DWC_otg controller
 */
void dwc_otg_enable_host_interrupts(struct dwc_otg_core_if *core_if)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	union gintmsk_data intr_mask = {.d32 = 0};
	DWC_DEBUGPL(DBG_CIL, "%s()\n", __func__);

#if 0
	/*
	 * Removed as this clears down any device mode
	 * ints that have been setup
	 */

	/*Disable all interrupts. */
	dwc_write_reg32(&global_regs->gintmsk, 0);
#endif

	/*Clear any pending interrupts. */
	dwc_write_reg32(&global_regs->gintsts, 0xFFFFFFFF);

#if 0
	/*
	 * Removed as this clears down any device mode
	 * ints that have been setup and has already been called.
	 */

	/*Enable the common interrupts */
	dwc_otg_enable_common_interrupts(core_if);
#endif

	/*
	 * Enable host mode interrupts without disturbing common
	 * interrupts.
	 */
	if (!core_if->dma_desc_enable)
		intr_mask.b.sofintr = 1;

	intr_mask.b.portintr = 1;
	intr_mask.b.hcintr = 1;
	dwc_modify_reg32(&global_regs->gintmsk, intr_mask.d32, intr_mask.d32);
}

/**
 *This function disables the Host Mode interrupts.
 *
 *@param core_if Programming view of DWC_otg controller
 */
void dwc_otg_disable_host_interrupts(struct dwc_otg_core_if *core_if)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	union gintmsk_data intr_mask = {.d32 = 0};
	DWC_DEBUGPL(DBG_CILV, "%s()\n", __func__);

	/*
	 * Disable host mode interrupts without disturbing common
	 * interrupts.
	 */
	intr_mask.b.sofintr = 1;
	intr_mask.b.portintr = 1;
	intr_mask.b.hcintr = 1;
	intr_mask.b.ptxfempty = 1;
	intr_mask.b.nptxfempty = 1;
	dwc_modify_reg32(&global_regs->gintmsk, intr_mask.d32, 0);
}

#if 0
/*currently not used, keep it here as if needed later */
static int phy_read(struct dwc_otg_core_if *core_if, int addr)
{
	u32 val;
	int timeout = 10;

	dwc_write_reg32(&core_if->core_global_regs->gpvndctl,
			0x02000000 | (addr << 16));
	val = dwc_read_reg32(&core_if->core_global_regs->gpvndctl);
	while (((val & 0x08000000) == 0) && (timeout--)) {
		udelay(1000);
		val = dwc_read_reg32(&core_if->core_global_regs->gpvndctl);
	}
	val = dwc_read_reg32(&core_if->core_global_regs->gpvndctl);
	printk(KERN_DEBUG"%s: addr=%02x regval=%02x\n",
			__func__, addr, val & 0x000000ff);

	return 0;
}
#endif

#ifdef CONFIG_405EX
static int phy_write(struct dwc_otg_core_if *core_if, int addr, int val8)
{
	u32 val;
	int timeout = 10;

	dwc_write_reg32(&core_if->core_global_regs->gpvndctl,
			0x02000000 | 0x00400000 |
			(addr << 16) | (val8 & 0x000000ff));
	val = dwc_read_reg32(&core_if->core_global_regs->gpvndctl);
	while (((val & 0x08000000) == 0) && (timeout--)) {
		udelay(1000);
		val = dwc_read_reg32(&core_if->core_global_regs->gpvndctl);
	}
	val = dwc_read_reg32(&core_if->core_global_regs->gpvndctl);

	return 0;
}
#endif

/**
 *This function initializes the DWC_otg controller registers for
 *host mode.
 *
 *This function flushes the Tx and Rx FIFOs and it flushes any entries in the
 *request queues. Host channels are reset to ensure that they are ready for
 *performing transfers.
 *
 *@param core_if Programming view of DWC_otg controller
 *
 */
void dwc_otg_core_host_init(struct dwc_otg_core_if *core_if)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	struct dwc_otg_host_if *host_if = core_if->host_if;
	struct dwc_otg_core_params *params = core_if->core_params;
	union hprt0_data hprt0 = {.d32 = 0};
	union fifosize_data nptxfifosize;
	union fifosize_data ptxfifosize;
	int i;
	union hcchar_data hcchar;
	union hcfg_data hcfg;
	struct dwc_otg_hc_regs __iomem *hc_regs;
	int num_channels;
	union gotgctl_data gotgctl = {.d32 = 0};
	DWC_DEBUGPL(DBG_CILV, "%s(%p)\n", __func__, core_if);

	/*Restart the Phy Clock */
	dwc_write_reg32(core_if->pcgcctl, 0);

	/*Initialize Host Configuration Register */
	init_fslspclksel(core_if);
	if (core_if->core_params->speed == DWC_SPEED_PARAM_FULL) {
		hcfg.d32 = dwc_read_reg32(&host_if->host_global_regs->hcfg);
		hcfg.b.fslssupp = 1;
		dwc_write_reg32(&host_if->host_global_regs->hcfg, hcfg.d32);
	}

	if (core_if->core_params->dma_desc_enable) {
		u8 op_mode = core_if->hwcfg2.b.op_mode;
		if (!(core_if->hwcfg4.b.desc_dma
			&& (core_if->snpsid >= OTG_CORE_REV_2_90a) &&
			((op_mode == DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG) ||
			(op_mode == DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG) ||
			(op_mode == DWC_HWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE_OTG) ||
			(op_mode == DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST) ||
			(op_mode == DWC_HWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST)))) {

			DWC_ERROR("Host can't operate in Descriptor DMA mode.\n"
					"Either core version is below 2.90a or "
					"GHWCFG2, GHWCFG4 registers' values do "
					"not allow Descriptor DMA in host mode."
					"\n"
					"To run the driver in Buffer DMA host "
					"mode set dma_desc_enable "
					"module parameter to 0.\n");
			return;
		}
		hcfg.d32 = dwc_read_reg32(&host_if->host_global_regs->hcfg);
		hcfg.b.descdma = 1;
		dwc_write_reg32(&host_if->host_global_regs->hcfg, hcfg.d32);
	}
	/*Configure data FIFO sizes */
	if (core_if->hwcfg2.b.dynamic_fifo && params->enable_dynamic_fifo) {
		/* split fifos equally */
		int fifo_size = core_if->total_fifo_size/3;

		DWC_DEBUGPL(DBG_CIL, "Total FIFO Size=%d\n",
			    core_if->total_fifo_size);
		DWC_DEBUGPL(DBG_CIL, "Rx FIFO Size=%d\n", fifo_size);
		DWC_DEBUGPL(DBG_CIL, "NP Tx FIFO Size=%d\n", fifo_size);
		DWC_DEBUGPL(DBG_CIL, "P Tx FIFO Size=%d\n", fifo_size);

		/*Rx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial grxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->grxfsiz));
		dwc_write_reg32(&global_regs->grxfsiz, fifo_size);
		DWC_DEBUGPL(DBG_CIL, "new grxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->grxfsiz));

		/*Non-periodic Tx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial gnptxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->gnptxfsiz));
		nptxfifosize.b.depth = fifo_size;
		nptxfifosize.b.startaddr = fifo_size;
		dwc_write_reg32(&global_regs->gnptxfsiz, nptxfifosize.d32);
		DWC_DEBUGPL(DBG_CIL, "new gnptxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->gnptxfsiz));

		/*Periodic Tx FIFO */
		DWC_DEBUGPL(DBG_CIL, "initial hptxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->hptxfsiz));
		ptxfifosize.b.depth = fifo_size;
		ptxfifosize.b.startaddr =
		    nptxfifosize.b.startaddr + nptxfifosize.b.depth;
		dwc_write_reg32(&global_regs->hptxfsiz, ptxfifosize.d32);
		DWC_DEBUGPL(DBG_CIL, "new hptxfsiz=%08x\n",
			    dwc_read_reg32(&global_regs->hptxfsiz));
	}

	/*Clear Host Set HNP Enable in the OTG Control Register */
	gotgctl.b.hstsethnpen = 1;
	dwc_modify_reg32(&global_regs->gotgctl, gotgctl.d32, 0);

	/*Make sure the FIFOs are flushed. */
	dwc_otg_flush_tx_fifo(core_if, 0x10 /*all Tx FIFOs */);
	dwc_otg_flush_rx_fifo(core_if);

	if (!core_if->core_params->dma_desc_enable) {
		/*Flush out any leftover queued requests. */
		num_channels = core_if->core_params->host_channels;
		for (i = 0; i < num_channels; i++) {
			hc_regs = core_if->host_if->hc_regs[i];
			hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
			hcchar.b.chen = 0;
			hcchar.b.chdis = 1;
			hcchar.b.epdir = 0;

			wmb();

			dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);
		}

		/*Halt all channels to put them into a known state. */
		for (i = 0; i < num_channels; i++) {
			int count = 0;
			hc_regs = core_if->host_if->hc_regs[i];
			hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
			hcchar.b.chen = 1;
			hcchar.b.chdis = 1;
			hcchar.b.epdir = 0;

			wmb();

			dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);
			DWC_DEBUGPL(DBG_HCDV, "%s: Halt channel %d\n",
					__func__, i);

			do {
				hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
#ifdef DWC_SLAVE		/* We must pop anything in the RxQueue */
				(void)dwc_read_reg32(&global_regs->grxstsp);
#endif
				if (++count > 200) {
					DWC_ERROR
					    ("%s: Unable to clear halt "
							    "on channel %d\n",
					     __func__, i);
					break;
				}
				udelay(100);
			} while (hcchar.b.chen);
		}
	}

	/* Turn on the vbus power. */
	DWC_PRINT("Init: Port Power? op_state=%d\n", core_if->op_state);
	if (core_if->op_state == A_HOST) {
		hprt0.d32 = dwc_otg_read_hprt0(core_if);
		DWC_PRINT("Init: Power Port (%d)\n", hprt0.b.prtpwr);
		if (hprt0.b.prtpwr == 0) {
			hprt0.b.prtpwr = 1;
			dwc_write_reg32(host_if->hprt0, hprt0.d32);
			if (core_if->otg_dev->soc_enable_vbus)
				core_if->otg_dev->soc_enable_vbus();
		}
	}

#ifdef CONFIG_405EX
	/*Write 0x60 to USB PHY register 7:
	 *Modify "Indicator Complement" and "Indicator Pass Thru" of
	 *Interface control register to disable the internal Vbus
	 *comparator, as suggested by RichTek FAE.
	 *This produced better results recognizing and mounting USB
	 *memory sticks on the Makalu 405EX platform. I couldn't see
	 *any difference on Kilauea, but since it seems to be better
	 *on Makalu, let's keep it in here too.
	 */
	phy_write(core_if, 7, 0x60);
#endif

	dwc_otg_enable_host_interrupts(core_if);
}


/**
 *Prepares a host channel for transferring packets to/from a specific
 *endpoint. The HCCHARn register is set up with the characteristics specified
 *in hc. Host channel interrupts that may need to be serviced while this
 *transfer is in progress are enabled.
 *
 *@param core_if Programming view of DWC_otg controller
 *@param hc Information needed to initialize the host channel
 */
void dwc_otg_hc_init(struct dwc_otg_core_if *core_if, struct dwc_hc *hc)
{
	u32 intr_enable;
	union hcintmsk_data hc_intr_mask;
	union gintmsk_data gintmsk = {.d32 = 0};
	union hcchar_data hcchar;
	union hcsplt_data hcsplt;
	u8 hc_num = hc->hc_num;
	struct dwc_otg_host_if *host_if = core_if->host_if;
	struct dwc_otg_hc_regs __iomem *hc_regs = host_if->hc_regs[hc_num];

	/*Clear old interrupt conditions for this host channel. */
	hc_intr_mask.d32 = 0xFFFFFFFF;
	hc_intr_mask.b.reserved14_31 = 0;
	dwc_write_reg32(&hc_regs->hcint, hc_intr_mask.d32);

	/*Enable channel interrupts required for this transfer. */
	hc_intr_mask.d32 = 0;
	hc_intr_mask.b.chhltd = 1;
	if (core_if->dma_enable) {
		if (!core_if->dma_desc_enable)
			hc_intr_mask.b.ahberr = 1;
		else
			if (hc->ep_type == USB_ENDPOINT_XFER_ISOC)
				hc_intr_mask.b.xfercompl = 1;

		if (hc->error_state && !hc->do_split &&
			 hc->ep_type != USB_ENDPOINT_XFER_ISOC) {
			hc_intr_mask.b.ack = 1;
			if (hc->ep_is_in) {
				hc_intr_mask.b.datatglerr = 1;
				if (hc->ep_type != USB_ENDPOINT_XFER_INT)
					hc_intr_mask.b.nak = 1;
			}
		}
	} else {
		switch (hc->ep_type) {
		case USB_ENDPOINT_XFER_CONTROL:
		case USB_ENDPOINT_XFER_BULK:
			hc_intr_mask.b.xfercompl = 1;
			hc_intr_mask.b.stall = 1;
			hc_intr_mask.b.xacterr = 1;
			hc_intr_mask.b.datatglerr = 1;
			if (hc->ep_is_in)
				hc_intr_mask.b.bblerr = 1;
			else {
				hc_intr_mask.b.nak = 1;
				hc_intr_mask.b.nyet = 1;
				if (hc->do_ping)
					hc_intr_mask.b.ack = 1;
			}
			if (hc->do_split) {
				hc_intr_mask.b.nak = 1;
				if (hc->complete_split)
					hc_intr_mask.b.nyet = 1;
				else
					hc_intr_mask.b.ack = 1;
			}
			if (hc->error_state)
				hc_intr_mask.b.ack = 1;
			break;
		case USB_ENDPOINT_XFER_INT:
			hc_intr_mask.b.xfercompl = 1;
			hc_intr_mask.b.nak = 1;
			hc_intr_mask.b.stall = 1;
			hc_intr_mask.b.xacterr = 1;
			hc_intr_mask.b.datatglerr = 1;
			hc_intr_mask.b.frmovrun = 1;
			if (hc->ep_is_in)
				hc_intr_mask.b.bblerr = 1;
			if (hc->error_state)
				hc_intr_mask.b.ack = 1;
			if (hc->do_split) {
				if (hc->complete_split)
					hc_intr_mask.b.nyet = 1;
				else
					hc_intr_mask.b.ack = 1;
			}
			break;
		case USB_ENDPOINT_XFER_ISOC:
			hc_intr_mask.b.xfercompl = 1;
			hc_intr_mask.b.frmovrun = 1;
			hc_intr_mask.b.ack = 1;
			if (hc->ep_is_in) {
				hc_intr_mask.b.xacterr = 1;
				hc_intr_mask.b.bblerr = 1;
			}
			break;
		}
	}
	dwc_write_reg32(&hc_regs->hcintmsk, hc_intr_mask.d32);

	/* Enable the top level host channel interrupt. */
	intr_enable = (1 << hc_num);
	dwc_modify_reg32(&host_if->host_global_regs->haintmsk, 0, intr_enable);

	/* Make sure host channel interrupts are enabled. */
	gintmsk.b.hcintr = 1;
	dwc_modify_reg32(&core_if->core_global_regs->gintmsk, 0, gintmsk.d32);

	/*
	 * Program the HCCHARn register with the endpoint characteristics for
	 * the current transfer.
	 */
	hcchar.d32 = 0;
	hcchar.b.devaddr = hc->dev_addr;
	hcchar.b.epnum = hc->ep_num;
	hcchar.b.epdir = hc->ep_is_in;
	hcchar.b.lspddev = (hc->speed == USB_SPEED_LOW);
	hcchar.b.eptype = hc->ep_type;
	hcchar.b.mps = hc->max_packet;
	dwc_write_reg32(&host_if->hc_regs[hc_num]->hcchar, hcchar.d32);
	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 Dev Addr: %d\n", hcchar.b.devaddr);
	DWC_DEBUGPL(DBG_HCDV, "	 Ep Num: %d\n", hcchar.b.epnum);
	DWC_DEBUGPL(DBG_HCDV, "	 Is In: %d\n", hcchar.b.epdir);
	DWC_DEBUGPL(DBG_HCDV, "	 Is Low Speed: %d\n", hcchar.b.lspddev);
	DWC_DEBUGPL(DBG_HCDV, "	 Ep Type: %d\n", hcchar.b.eptype);
	DWC_DEBUGPL(DBG_HCDV, "	 Max Pkt: %d\n", hcchar.b.mps);
	DWC_DEBUGPL(DBG_HCDV, "	 Multi Cnt: %d\n", hcchar.b.multicnt);

	/*
	 * Program the HCSPLIT register for SPLITs
	 */

	hcsplt.d32 = 0;
	if (hc->do_split) {
		DWC_DEBUGPL(DBG_HCDV, "Programming HC %d with split --> %s\n",
			hc->hc_num, hc->complete_split ? "CSPLIT" : "SSPLIT");
		hcsplt.b.compsplt = hc->complete_split;
		hcsplt.b.xactpos = hc->xact_pos;
		hcsplt.b.hubaddr = hc->hub_addr;
		hcsplt.b.prtaddr = hc->port_addr;
		DWC_DEBUGPL(DBG_HCDV, "	  comp split %d\n", hc->complete_split);
		DWC_DEBUGPL(DBG_HCDV, "	  xact pos %d\n", hc->xact_pos);
		DWC_DEBUGPL(DBG_HCDV, "	  hub addr %d\n", hc->hub_addr);
		DWC_DEBUGPL(DBG_HCDV, "	  port addr %d\n", hc->port_addr);
		DWC_DEBUGPL(DBG_HCDV, "	  is_in %d\n", hc->ep_is_in);
		DWC_DEBUGPL(DBG_HCDV, "	  Max Pkt: %d\n", hcchar.b.mps);
		DWC_DEBUGPL(DBG_HCDV, "	  xferlen: %d\n", hc->xfer_len);
	}
	dwc_write_reg32(&host_if->hc_regs[hc_num]->hcsplt, hcsplt.d32);
}


/**
 *Attempts to halt a host channel. This function should only be called in
 *Slave mode or to abort a transfer in either Slave mode or DMA mode. Under
 *normal circumstances in DMA mode, the controller halts the channel when the
 *transfer is complete or a condition occurs that requires application
 *intervention.
 *
 *In slave mode, checks for a free request queue entry, then sets the Channel
 *Enable and Channel Disable bits of the Host Channel Characteristics
 *register of the specified channel to intiate the halt. If there is no free
 *request queue entry, sets only the Channel Disable bit of the HCCHARn
 *register to flush requests for this channel. In the latter case, sets a
 *flag to indicate that the host channel needs to be halted when a request
 *queue slot is open.
 *
 *In DMA mode, always sets the Channel Enable and Channel Disable bits of the
 *HCCHARn register. The controller ensures there is space in the request
 *queue before submitting the halt request.
 *
 *Some time may elapse before the core flushes any posted requests for this
 *host channel and halts. The Channel Halted interrupt handler completes the
 *deactivation of the host channel.
 *
 *@param core_if Controller register interface.
 *@param hc Host channel to halt.
 *@param _halt_status Reason for halting the channel.
 */
void dwc_otg_hc_halt(struct dwc_otg_core_if *core_if,
		     struct dwc_hc *hc,  enum dwc_otg_halt_status _halt_status)
{
	union gnptxsts_data nptxsts;
	union hptxsts_data hptxsts;
	union hcchar_data hcchar;
	struct dwc_otg_hc_regs __iomem *hc_regs;
	struct dwc_otg_core_global_regs __iomem *global_regs;
	struct dwc_otg_host_global_regs __iomem *host_global_regs;
	hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	global_regs = core_if->core_global_regs;
	host_global_regs = core_if->host_if->host_global_regs;
	WARN_ON(_halt_status == DWC_OTG_HC_XFER_NO_HALT_STATUS);
	if (_halt_status == DWC_OTG_HC_XFER_URB_DEQUEUE ||
		 _halt_status == DWC_OTG_HC_XFER_AHB_ERR) {

		/*
		 * Disable all channel interrupts except Ch Halted. The QTD
		 * and QH state associated with this transfer has been cleared
		 * (in the case of URB_DEQUEUE), so the channel needs to be
		 * shut down carefully to prevent crashes.
		 */
		union hcintmsk_data hcintmsk;
		hcintmsk.d32 = 0;
		hcintmsk.b.chhltd = 1;
		dwc_write_reg32(&hc_regs->hcintmsk, hcintmsk.d32);

		/*
		 * Make sure no other interrupts besides halt are currently
		 * pending. Handling another interrupt could cause a crash due
		 * to the QTD and QH state.
		 */
		dwc_write_reg32(&hc_regs->hcint, ~hcintmsk.d32);

		/*
		 * Make sure the halt status is set to URB_DEQUEUE or AHB_ERR
		 * even if the channel was already halted for some other
		 * reason.
		 */
		hc->halt_status = _halt_status;
		hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);

		if (hcchar.b.chen == 0) {
			/*
			 * The channel is either already halted or it hasn't
			 * started yet. In DMA mode, the transfer may halt if
			 * it finishes normally or a condition occurs that
			 * requires driver intervention. Don't want to halt
			 * the channel again. In either Slave or DMA mode,
			 * it's possible that the transfer has been assigned
			 * to a channel, but not started yet when an URB is
			 * dequeued. Don't want to halt a channel that hasn't
			 * started yet.
			 */
			return;
		}
	}
	if (hc->halt_pending) {

		/*
		 * A halt has already been issued for this channel. This might
		 * happen when a transfer is aborted by a higher level in
		 * the stack.
		 */
#ifdef DEBUG
		DWC_PRINT("***%s: Channel %d, "
				"hc->halt_pending already set ***\n",
				__func__, hc->hc_num);
		/* dwc_otg_dump_global_registers(core_if); */
		/* dwc_otg_dump_host_registers(core_if); */
#endif
		return;
	}
	hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
	/*No need to set the bit in DDMA for disabling the channel */
	/* TODO check it everywhere channel is disabled */
	if (!core_if->core_params->dma_desc_enable)
		hcchar.b.chen = 1;
	hcchar.b.chdis = 1;
	if (!core_if->dma_enable) {
		/*Check for space in the request queue to issue the halt. */
		if (hc->ep_type == USB_ENDPOINT_XFER_CONTROL
			|| hc->ep_type == USB_ENDPOINT_XFER_BULK) {
			nptxsts.d32 = dwc_read_reg32(&global_regs->gnptxsts);
			if (nptxsts.b.nptxqspcavail == 0)
				hcchar.b.chen = 0;
		} else {
			hptxsts.d32 =
				dwc_read_reg32(&host_global_regs->hptxsts);
			if ((hptxsts.b.ptxqspcavail == 0) ||
				 (core_if->queuing_high_bandwidth))
				hcchar.b.chen = 0;
		}
	}

	wmb();

	dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);
	hc->halt_status = _halt_status;
	if (hcchar.b.chen) {
		hc->halt_pending = 1;
		hc->halt_on_queue = 0;
	} else
		hc->halt_on_queue = 1;

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 hcchar: 0x%08x\n", hcchar.d32);
	DWC_DEBUGPL(DBG_HCDV, "	 halt_pending: %d\n", hc->halt_pending);
	DWC_DEBUGPL(DBG_HCDV, "	 halt_on_queue: %d\n", hc->halt_on_queue);
	DWC_DEBUGPL(DBG_HCDV, "	 halt_status: %d\n", hc->halt_status);
	return;
}


/**
 *Clears the transfer state for a host channel. This function is normally
 *called after a transfer is done and the host channel is being released.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param hc Identifies the host channel to clean up.
 */
void dwc_otg_hc_cleanup(struct dwc_otg_core_if *core_if, struct dwc_hc *hc)
{
	struct dwc_otg_hc_regs __iomem *hc_regs;
	hc->xfer_started = 0;

	/*
	 * Clear channel interrupt enables and any unhandled channel interrupt
	 * conditions.
	 */
	hc_regs = core_if->host_if->hc_regs[hc->hc_num];
	dwc_write_reg32(&hc_regs->hcintmsk, 0);
	dwc_write_reg32(&hc_regs->hcint, 0xFFFFFFFF);

#ifdef DEBUG
	del_timer(&core_if->hc_xfer_timer[hc->hc_num]);
	{
		union hcchar_data hcchar;
		hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
		if (hcchar.b.chdis) {
			DWC_WARN("%s: chdis set, channel %d, hcchar 0x%08x\n",
				  __func__, hc->hc_num, hcchar.d32);
		}
	}
#endif
}


/**
 *Sets the channel property that indicates in which frame a periodic transfer
 *should occur. This is always set to the _next_ frame. This function has no
 *effect on non-periodic transfers.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param hc Identifies the host channel to set up and its properties.
 *@param _hcchar Current value of the HCCHAR register for the specified host
 *channel.
 */
static inline void hc_set_even_odd_frame(struct dwc_otg_core_if *core_if,
					 struct dwc_hc *hc,
					 union hcchar_data *_hcchar)
{
	if (hc->ep_type == USB_ENDPOINT_XFER_INT ||
			hc->ep_type == USB_ENDPOINT_XFER_ISOC) {
		union hfnum_data hfnum;
		hfnum.d32 =
		dwc_read_reg32(&core_if->host_if->host_global_regs->hfnum);

		/*1 if _next_ frame is odd, 0 if it's even */
		_hcchar->b.oddfrm = (hfnum.b.frnum & 0x1) ? 0 : 1;

#ifdef DEBUG
		if (hc->ep_type == USB_ENDPOINT_XFER_INT && hc->do_split
			&& !hc->complete_split) {
			switch (hfnum.b.frnum & 0x7) {
			case 7:
				core_if->hfnum_7_samples++;
				core_if->hfnum_7_frrem_accum += hfnum.b.frrem;
				break;
			case 0:
				core_if->hfnum_0_samples++;
				core_if->hfnum_0_frrem_accum += hfnum.b.frrem;
				break;
			default:
				core_if->hfnum_other_samples++;
				core_if->hfnum_other_frrem_accum +=
				    hfnum.b.frrem;
				break;
			}
		}
#endif	/* */
	}
}

#ifdef DEBUG
static void hc_xfer_timeout(unsigned long _ptr)
{
	struct hc_xfer_info *xfer_info = (struct hc_xfer_info *) _ptr;
	int hc_num = xfer_info->hc->hc_num;
	DWC_WARN("%s: timeout on channel %d\n", __func__, hc_num);
	DWC_WARN("	start_hcchar_val 0x%08x\n",
		  xfer_info->core_if->start_hcchar_val[hc_num]);
}
#endif	/* */

static void set_pid_isoc(struct dwc_hc *hc)
{
	/*Set up the initial PID for the transfer. */
	if (hc->speed == USB_SPEED_HIGH) {
		if (hc->ep_is_in) {
			if (hc->multi_count == 1) {
				hc->data_pid_start =
				    DWC_OTG_HC_PID_DATA0;
			} else if (hc->multi_count == 2) {
				hc->data_pid_start =
				    DWC_OTG_HC_PID_DATA1;
			} else {
				hc->data_pid_start =
				    DWC_OTG_HC_PID_DATA2;
			}
		} else {
			if (hc->multi_count == 1) {
				hc->data_pid_start =
				    DWC_OTG_HC_PID_DATA0;
			} else {
				hc->data_pid_start =
				    DWC_OTG_HC_PID_MDATA;
			}
		}
	} else {
		hc->data_pid_start = DWC_OTG_HC_PID_DATA0;
	}
}
/*
 *This function does the setup for a data transfer for a host channel and
 *starts the transfer. May be called in either Slave mode or DMA mode. In
 *Slave mode, the caller must ensure that there is sufficient space in the
 *request queue and Tx Data FIFO.
 *
 *For an OUT transfer in Slave mode, it loads a data packet into the
 *appropriate FIFO. If necessary, additional data packets will be loaded in
 *the Host ISR.
 *
 *For an IN transfer in Slave mode, a data packet is requested. The data
 *packets are unloaded from the Rx FIFO in the Host ISR. If necessary,
 *additional data packets are requested in the Host ISR.
 *
 *For a PING transfer in Slave mode, the Do Ping bit is set in the HCTSIZ
 *register along with a packet count of 1 and the channel is enabled. This
 *causes a single PING transaction to occur. Other fields in HCTSIZ are
 *simply set to 0 since no data transfer occurs in this case.
 *
 *For a PING transfer in DMA mode, the HCTSIZ register is initialized with
 *all the information required to perform the subsequent data transfer. In
 *addition, the Do Ping bit is set in the HCTSIZ register. In this case, the
 *controller performs the entire PING protocol, then starts the data
 *transfer.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param hc Information needed to initialize the host channel. The xfer_len
 *value may be reduced to accommodate the max widths of the XferSize and
 *PktCnt fields in the HCTSIZn register. The multi_count value may be changed
 *to reflect the final xfer_len value.
 */
void
dwc_otg_hc_start_transfer(struct dwc_otg_core_if *core_if, struct dwc_hc *hc)
{
	union hcchar_data hcchar;
	union hctsiz_data hctsiz;
	u16 num_packets;
	u32 max_hc_xfer_size = core_if->core_params->max_transfer_size;
	u16 max_hc_pkt_count = core_if->core_params->max_packet_count;
	struct dwc_otg_hc_regs __iomem *hc_regs =
					core_if->host_if->hc_regs[hc->hc_num];
	hctsiz.d32 = 0;
	if (hc->do_ping) {
		if (!core_if->dma_enable) {
			dwc_otg_hc_do_ping(core_if, hc);
			hc->xfer_started = 1;
			return;
		} else {
			hctsiz.b.dopng = 1;
		}
	}
	if (hc->do_split) {
		num_packets = 1;
		if (hc->complete_split && !hc->ep_is_in)
			/*
			 * For CSPLIT OUT Transfer, set the size to 0 so the
			 * core doesn't expect any data written to the FIFO
			 */
			hc->xfer_len = 0;
		else if (hc->ep_is_in || (hc->xfer_len > hc->max_packet))
			hc->xfer_len = hc->max_packet;
		else if (!hc->ep_is_in && (hc->xfer_len > 188))
			hc->xfer_len = 188;

		hctsiz.b.xfersize = hc->xfer_len;
	} else {
		/*
		 * Ensure that the transfer length and packet count will fit
		 * in the widths allocated for them in the HCTSIZn register.
		 */
		if (hc->ep_type == USB_ENDPOINT_XFER_INT
			|| hc->ep_type == USB_ENDPOINT_XFER_ISOC) {
			/*
			 * Make sure the transfer size is no larger than one
			 * (micro)frame's worth of data. (A check was done
			 * when the periodic transfer was accepted to ensure
			 * that a (micro)frame's worth of data can be
			 * programmed into a channel.)
			 */
			u32 max_periodic_len = hc->multi_count * hc->max_packet;
			if (hc->xfer_len > max_periodic_len)
				hc->xfer_len = max_periodic_len;
		} else if (hc->xfer_len > max_hc_xfer_size)
			/*
			 * Make sure that xfer_len is a multiple of
			 * max packet size.
			 */
			hc->xfer_len = max_hc_xfer_size - hc->max_packet + 1;

		if (hc->xfer_len > 0) {
			num_packets = (hc->xfer_len + hc->max_packet - 1) /
					hc->max_packet;

			if (num_packets > max_hc_pkt_count) {
				num_packets = max_hc_pkt_count;
				hc->xfer_len = num_packets * hc->max_packet;
			}
		} else
			/*Need 1 packet for transfer length of 0. */
			num_packets = 1;

		if (hc->ep_is_in)
			/*
			 * Always program an integral # of max packets
			 * for IN transfers.
			 */
			hc->xfer_len = num_packets * hc->max_packet;

		if (hc->ep_type == USB_ENDPOINT_XFER_INT
			|| hc->ep_type == USB_ENDPOINT_XFER_ISOC)
			/*
			 * Make sure that the multi_count field matches the
			 * actual transfer length.
			 */
			hc->multi_count = num_packets;


		if (hc->ep_type == USB_ENDPOINT_XFER_ISOC)
			set_pid_isoc(hc);

		hctsiz.b.xfersize = hc->xfer_len;
	}

	hc->start_pkt_count = num_packets;
	hctsiz.b.pktcnt = num_packets;
	hctsiz.b.pid = hc->data_pid_start;
	dwc_write_reg32(&hc_regs->hctsiz, hctsiz.d32);
	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 Xfer Size: %d\n", hctsiz.b.xfersize);
	DWC_DEBUGPL(DBG_HCDV, "	 Num Pkts: %d\n", hctsiz.b.pktcnt);
	DWC_DEBUGPL(DBG_HCDV, "	 Start PID: %d\n", hctsiz.b.pid);
	if (core_if->dma_enable) {
		dma_addr_t dma_addr;
		if (hc->align_buff)
			dma_addr = hc->align_buff;
		else
			dma_addr = (u32)hc->xfer_buff;

		dwc_write_reg32(&hc_regs->hcdma, dma_addr);
	}

	/*Start the split */
	if (hc->do_split) {
		union hcsplt_data hcsplt;
		hcsplt.d32 = dwc_read_reg32(&hc_regs->hcsplt);
		hcsplt.b.spltena = 1;
		dwc_write_reg32(&hc_regs->hcsplt, hcsplt.d32);
		wmb();
	}
	hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
	hcchar.b.multicnt = hc->multi_count;
	hc_set_even_odd_frame(core_if, hc, &hcchar);

#ifdef DEBUG
	core_if->start_hcchar_val[hc->hc_num] = hcchar.d32;
	if (hcchar.b.chdis) {
		DWC_WARN("%s: chdis set, channel %d, hcchar 0x%08x\n",
			  __func__, hc->hc_num, hcchar.d32);
	}

#endif

	/*Set host channel enable after all other setup is complete. */
	hcchar.b.chen = 1;
	hcchar.b.chdis = 0;

	wmb();
	dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);

	hc->xfer_started = 1;
	hc->requests++;
	if (!core_if->dma_enable && !hc->ep_is_in && hc->xfer_len > 0)
		/*Load OUT packet into the appropriate Tx FIFO. */
		dwc_otg_hc_write_packet(core_if, hc);

#ifdef DEBUG
	/*Start a timer for this transfer. */
	core_if->hc_xfer_timer[hc->hc_num].function = hc_xfer_timeout;
	core_if->hc_xfer_info[hc->hc_num].core_if = core_if;
	core_if->hc_xfer_info[hc->hc_num].hc = hc;
	core_if->hc_xfer_timer[hc->hc_num].data =
		(unsigned long)(&core_if->hc_xfer_info[hc->hc_num]);
	core_if->hc_xfer_timer[hc->hc_num].expires = jiffies + (HZ * 10);
	add_timer(&core_if->hc_xfer_timer[hc->hc_num]);
#endif
}

/**
 *This function does the setup for a data transfer for a host channel
 *and starts the transfer in Descriptor DMA mode.
 *
 *Initializes HCTSIZ register. For a PING transfer the Do Ping bit is set.
 *Sets PID and NTD values. For periodic transfers
 *initializes SCHED_INFO field with micro-frame bitmap.
 *
 *Initializes HCDMA register with descriptor list address and CTD value
 *then starts the transfer via enabling the channel.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param hc Information needed to initialize the host channel.
 */
void
dwc_otg_hc_start_transfer_ddma(struct dwc_otg_core_if *core_if,
				struct dwc_hc *hc)
{
	struct dwc_otg_hc_regs __iomem *hc_regs =
					core_if->host_if->hc_regs[hc->hc_num];
	union hcchar_data hcchar;
	union hctsiz_data hctsiz;
	union hcdma_data  hcdma;

	hctsiz.d32 = 0;

	if (hc->do_ping && !hc->ep_is_in)
		hctsiz.b_ddma.dopng = 1;

	if (hc->ep_type == USB_ENDPOINT_XFER_ISOC)
		set_pid_isoc(hc);

	/*Packet Count and Xfer Size are not used in Descriptor DMA mode */
	hctsiz.b_ddma.pid = hc->data_pid_start;
	/*0 - 1 descriptor, 1 - 2 descriptors, etc. */
	hctsiz.b_ddma.ntd = hc->ntd - 1;
	/*Non-zero only for high-speed interrupt endpoints */
	hctsiz.b_ddma.schinfo = hc->schinfo;

	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	DWC_DEBUGPL(DBG_HCDV, "	 Start PID: %d\n", hctsiz.b.pid);
	DWC_DEBUGPL(DBG_HCDV, "	 NTD: %d\n", hctsiz.b_ddma.ntd);

	dwc_write_reg32(&hc_regs->hctsiz, hctsiz.d32);

	hcdma.d32 = 0;
	hcdma.b.dma_addr = ((u32)hc->desc_list_addr) >> 11;

	/*Always start from first descriptor. */
	hcdma.b.ctd = 0;
	dwc_write_reg32(&hc_regs->hcdma, hcdma.d32);

	hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
	hcchar.b.multicnt = hc->multi_count;

#ifdef DEBUG
	core_if->start_hcchar_val[hc->hc_num] = hcchar.d32;
	if (hcchar.b.chdis) {
		DWC_WARN("%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, hc->hc_num, hcchar.d32);
	}
#endif

	/*Set host channel enable after all other setup is complete. */
	hcchar.b.chen = 1;
	hcchar.b.chdis = 0;

	wmb();

	dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);

	hc->xfer_started = 1;
	hc->requests++;

#if 0
	if ((hc->ep_type != USB_ENDPOINT_XFER_INT) &&
			(hc->ep_type != USB_ENDPOINT_XFER_ISOC)) {
		core_if->hc_xfer_info[hc->hc_num].core_if = core_if;
		core_if->hc_xfer_info[hc->hc_num].hc = hc;
		/*Start a timer for this transfer. */
		DWC_TIMER_SCHEDULE(core_if->hc_xfer_timer[hc->hc_num], 10000);
	}

#endif

}
/**
 *This function continues a data transfer that was started by previous call
 *to dwc_otg_hc_start_transfer. The caller must ensure there is
 *sufficient space in the request queue and Tx Data FIFO. This function
 *should only be called in Slave mode. In DMA mode, the controller acts
 *autonomously to complete transfers programmed to a host channel.
 *
 *For an OUT transfer, a new data packet is loaded into the appropriate FIFO
 *if there is any data remaining to be queued. For an IN transfer, another
 *data packet is always requested. For the SETUP phase of a control transfer,
 *this function does nothing.
 *
 *@return 1 if a new request is queued, 0 if no more requests are required
 *for this transfer.
 */
int dwc_otg_hc_continue_transfer(struct dwc_otg_core_if *core_if,
				struct dwc_hc *hc)
{
	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	if (hc->do_split)
		/*SPLITs always queue just once per channel */
		return 0;
	else if (hc->data_pid_start == DWC_OTG_HC_PID_SETUP)
		/*SETUPs are queued only once since they can't be NAKed. */
		return 0;
	else if (hc->ep_is_in) {
		/*
		 * Always queue another request for other IN transfers. If
		 * back-to-back INs are issued and NAKs are received for both,
		 * the driver may still be processing the first NAK when the
		 * second NAK is received. When the interrupt handler clears
		 * the NAK interrupt for the first NAK, the second NAK will
		 * not be seen. So we can't depend on the NAK interrupt
		 * handler to requeue a NAKed request. Instead, IN requests
		 * are issued each time this function is called. When the
		 * transfer completes, the extra requests for the channel will
		 * be flushed.
		 */
		union hcchar_data hcchar;
		struct dwc_otg_hc_regs __iomem *hc_regs =
			core_if->host_if->hc_regs[hc->hc_num];

		hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);

		hc_set_even_odd_frame(core_if, hc, &hcchar);
		hcchar.b.chen = 1;
		hcchar.b.chdis = 0;
		DWC_DEBUGPL(DBG_HCDV, "	 IN xfer: hcchar = 0x%08x\n",
				hcchar.d32);

		wmb();

		dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);

		hc->requests++;
		return 1;
	} else {
	    /*OUT transfers. */
	    if (hc->xfer_count < hc->xfer_len) {
			if (hc->ep_type == USB_ENDPOINT_XFER_INT ||
				hc->ep_type == USB_ENDPOINT_XFER_ISOC) {
				union hcchar_data hcchar;
				struct dwc_otg_hc_regs __iomem *hc_regs;
				hc_regs = core_if->host_if->hc_regs[hc->hc_num];
				hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
				hc_set_even_odd_frame(core_if, hc, &hcchar);
			}

			/*Load OUT packet into the appropriate Tx FIFO. */
			dwc_otg_hc_write_packet(core_if, hc);
			hc->requests++;
			return 1;
		} else
			return 0;
	}
}

/**
 *Starts a PING transfer. This function should only be called in Slave mode.
 *The Do Ping bit is set in the HCTSIZ register, then the channel is enabled.
 */
void dwc_otg_hc_do_ping(struct dwc_otg_core_if *core_if, struct dwc_hc *hc)
{
	union hcchar_data hcchar;
	union hctsiz_data hctsiz;
	struct dwc_otg_hc_regs __iomem *hc_regs =
					core_if->host_if->hc_regs[hc->hc_num];
	DWC_DEBUGPL(DBG_HCDV, "%s: Channel %d\n", __func__, hc->hc_num);
	hctsiz.d32 = 0;
	hctsiz.b.dopng = 1;
	hctsiz.b.pktcnt = 1;
	dwc_write_reg32(&hc_regs->hctsiz, hctsiz.d32);
	hcchar.d32 = dwc_read_reg32(&hc_regs->hcchar);
	hcchar.b.chen = 1;
	hcchar.b.chdis = 0;

	wmb();

	dwc_write_reg32(&hc_regs->hcchar, hcchar.d32);
}


#ifdef OTG_PPC_PLB_DMA      /*PPC_PLB_DMA mode */
/*
 * This will dump the status of the dma registers -
 * Only used in debug mode
 */
void ppc4xx_dump_dma(unsigned int dmanr)
{
	int index;

	printk(KERN_DEBUG"%32s:\n", __func__);
	for (index = 0; index <= 7; index++) {
		printk(KERN_DEBUG"%32s dmanr=%d , 0x%x=0x%x\n", __func__, dmanr,
			DCRN_DMACR0 + dmanr*8+index,
			mfdcr(DCRN_DMACR0 + dmanr*8 + index)
			);
	}
	printk(KERN_DEBUG"%32s DCRN_DMASR=0x%x\n", __func__, mfdcr(DCRN_DMASR));
}

/*
 * This function programs the PLB-DMA engine to perform MEM-MEM transfer
 * This is used to RD & WR from the DWC_FIFO by the PLB_DMA engine
 */
void ppc4xx_start_plb_dma(struct dwc_otg_core_if *core_if, void *src, void *dst,
		unsigned int length, unsigned int use_interrupt,
		unsigned int dma_ch, unsigned int dma_dir)
{
	int res = 0;
	unsigned int control;
	ppc_dma_ch_t p_init;

	memset((char *)&p_init, sizeof(p_init), 0);
	p_init.polarity = 0;
	p_init.pwidth = PW_32;
	p_init.in_use = 0;
	if (dma_dir == OTG_TX_DMA) {
		p_init.sai = 1;
		p_init.dai = 0;
	} else if (dma_dir == OTG_RX_DMA) {
		p_init.sai = 0;
		p_init.dai = 1;
	}
	res = ppc4xx_init_dma_channel(dma_ch, &p_init);
	if (res) {
		printk(KERN_DEBUG"%32s: nit_dma_channel return %d %d "
				"bytes dest %p\n",
				__func__, res, length, dst);
	}
	res = ppc4xx_clr_dma_status(dma_ch);
	if (res) {
		printk(KERN_DEBUG"%32s: ppc4xx_clr_dma_status %d\n",
				__func__, res);
	}

	if (dma_dir == OTG_TX_DMA) {
		ppc4xx_set_src_addr(dma_ch, virt_to_bus(src));
		ppc4xx_set_dst_addr(dma_ch, (core_if->phys_addr +
				(dst - (void *)(core_if->core_global_regs))));
	} else if (dma_dir == OTG_RX_DMA) {
		ppc4xx_set_src_addr(dma_ch, (core_if->phys_addr +
			(src - (void *)(core_if->core_global_regs))));
		ppc4xx_set_dst_addr(dma_ch, virt_to_bus(dst));
	}

	ppc4xx_set_dma_mode(dma_ch, DMA_MODE_MM);
	ppc4xx_set_dma_count(dma_ch, length);

	/*flush cache before enabling DMA transfer */
	if (dma_dir == OTG_TX_DMA) {
		flush_dcache_range((unsigned long)src,
				(unsigned long)(src + length));
	} else if (dma_dir == OTG_RX_DMA) {
		flush_dcache_range((unsigned long)dst,
				(unsigned long)(dst + length));
	}

	if (use_interrupt)
		res = ppc4xx_enable_dma_interrupt(dma_ch);
	else
		res = ppc4xx_disable_dma_interrupt(dma_ch);

	if (res) {
		printk(KERN_DEBUG"%32s: en/disable_dma_interrupt "
				"%d return %d per %d\n",
				__func__, use_interrupt, res,
		ppc4xx_get_peripheral_width(dma_ch));
	}

	control = mfdcr(DCRN_DMACR0 + (dma_ch * 8));

	control &= ~(SET_DMA_BEN(1));
	control &= ~(SET_DMA_PSC(3));
	control &= ~(SET_DMA_PWC(0x3f));
	control &= ~(SET_DMA_PHC(0x7));
	control &= ~(SET_DMA_PL(1));

	mtdcr(DCRN_DMACR0 + (dma_ch * 8), control);

#ifdef OTG_PPC_PLB_DMA_DBG
	ppc4xx_dump_dma(dma_ch);
#endif
	ppc4xx_enable_dma(dma_ch);
}
#endif

/*
 * This function writes a packet into the Tx FIFO associated with the Host
 * Channel. For a channel associated with a non-periodic EP, the non-periodic
 * Tx FIFO is written. For a channel associated with a periodic EP, the
 * periodic Tx FIFO is written. This function should only be called in Slave
 * mode.
 *
 * Upon return the xfer_buff and xfer_count fields in hc are incremented by
 * then number of bytes written to the Tx FIFO.
 */
void dwc_otg_hc_write_packet(struct dwc_otg_core_if *core_if, struct dwc_hc *hc)
{
#ifndef OTG_PPC_PLB_DMA
	u32 i;
#endif
	u32 remaining_count;
	u32 byte_count;
	u32 dword_count;
	u32 *data_buff = (u32 *) (hc->xfer_buff);
	u32 __iomem *data_fifo = core_if->data_fifo[hc->hc_num];
#if !defined(OTG_PPC_PLB_DMA_TASKLET) &&  defined(OTG_PPC_PLB_DMA)
	u32 dma_sts = 0;
#endif
	remaining_count = hc->xfer_len - hc->xfer_count;
	if (remaining_count > hc->max_packet)
		byte_count = hc->max_packet;
	else
		byte_count = remaining_count;

	dword_count = (byte_count + 3) / 4;

#ifdef OTG_PPC_PLB_DMA
#ifdef OTG_PPC_PLB_DMA_TASKLET

	if (hc->xfer_len < USB_BUFSIZ) {
		int i;
		if ((((unsigned long)data_buff) & 0x3) == 0) {
			/*xfer_buff is DWORD aligned. */
			for (i = 0; i < dword_count; i++, data_buff++)
				dwc_write_datafifo32(data_fifo, *data_buff);
		} else {
			/*xfer_buff is not DWORD aligned. */
			for (i = 0; i < dword_count; i++, data_buff++)
				dwc_write_datafifo32(data_fifo,
						get_unaligned(data_buff));
		}
	} else {
		DWC_DEBUGPL(DBG_SP, "%s set release_later %d\n",
				__func__, dword_count);
		atomic_set(&release_later, 1);

		dwc_otg_disable_global_interrupts(core_if);

		core_if->dma_xfer.dma_data_buff = data_buff;
		core_if->dma_xfer.dma_data_fifo = (void *)data_fifo;
		core_if->dma_xfer.dma_count = dword_count;
		core_if->dma_xfer.dma_dir = OTG_TX_DMA;
		tasklet_schedule(core_if->plbdma_tasklet);
	}
#else	/*!OTG_PPC_PLB_DMA_TASKLET */
	if ((((unsigned long)data_buff) & 0x3) == 0) {
		/*call tx_dma - src,dest,len,intr */
		ppc4xx_start_plb_dma(core_if,
					(void *)data_buff,
					data_fifo,
					(dword_count * 4),
					PLB_DMA_INT_DIS,
					PLB_DMA_CH, O
					TG_TX_DMA
					);
	} else {
		ppc4xx_start_plb_dma(core_if,
					(void *)get_unaligned(data_buff),
					data_fifo,
					(dword_count * 4),
					PLB_DMA_INT_DIS,
					PLB_DMA_CH,
					OTG_TX_DMA
					);
	}

	while (mfdcr(DCRN_DMACR0 + (PLB_DMA_CH*8)) & DMA_CE_ENABLE)
		; /*tight spin*/

	dma_sts = (u32)ppc4xx_get_dma_status();
#ifdef OTG_PPC_PLB_DMA_DBG
	if (!(dma_sts & DMA_CS0))
		printk(KERN_DEBUG"Status (Terminal Count not occured) 0x%08x\n",
				mfdcr(DCRN_DMASR));
#endif
	if (dma_sts & DMA_CH0_ERR)
		printk(KERN_DEBUG"Status (Channel Error) 0x%08x\n",
				mfdcr(DCRN_DMASR));

	ppc4xx_clr_dma_status(PLB_DMA_CH);
#ifdef OTG_PPC_PLB_DMA_DBG
	printk(KERN_DEBUG"%32s DMA Status =0x%08x\n", __func__,
			mfdcr(DCRN_DMASR);
#endif

#endif	/*OTG_PPC_PLB_DMA_TASKLET */


#else
	if ((((unsigned long)data_buff) & 0x3) == 0) {
		/*xfer_buff is DWORD aligned. */
		for (i = 0; i < dword_count; i++, data_buff++)
			dwc_write_datafifo32(data_fifo, *data_buff);
	} else {
		/*xfer_buff is not DWORD aligned. */
		for (i = 0; i < dword_count; i++, data_buff++)
			dwc_write_datafifo32(data_fifo,
					get_unaligned(data_buff));
	}
#endif
	hc->xfer_count += byte_count;
	hc->xfer_buff += byte_count;
}

/**
 *This function reads a setup packet from the Rx FIFO into the destination
 *buffer. This function is called from the Rx Status Queue Level (RxStsQLvl)
 *Interrupt routine when a SETUP packet has been received in Slave mode.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param dest Destination buffer for packet data.
 */
void dwc_otg_read_setup_packet(struct dwc_otg_core_if *core_if, u32 *dest)
{
	/*Get the 8 bytes of a setup transaction data */

	/*Pop 2 DWORDS off the receive data FIFO into memory */
	dest[0] = dwc_read_datafifo32(core_if->data_fifo[0]);
	dest[1] = dwc_read_datafifo32(core_if->data_fifo[0]);
}

/**
 *This function enables EP0 OUT to receive SETUP packets and configures EP0
 *IN for transmitting packets.	 It is normally called when the
 *"Enumeration Done" interrupt occurs.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP0 data.
 */
void dwc_otg_ep0_activate(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	union dsts_data dsts;
	union depctl_data diepctl;
	union depctl_data doepctl;
	union dctl_data dctl = {.d32 = 0};

	/*Read the Device Status and Endpoint 0 Control registers */
	dsts.d32 = dwc_read_reg32(&dev_if->dev_global_regs->dsts);
	diepctl.d32 = dwc_read_reg32(&dev_if->in_ep_regs[0]->diepctl);
	doepctl.d32 = dwc_read_reg32(&dev_if->out_ep_regs[0]->doepctl);

	/*Set the MPS of the IN EP based on the enumeration speed */
	switch (dsts.b.enumspd) {

	case DWC_DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ:
	case DWC_DSTS_ENUMSPD_FS_PHY_48MHZ:
		diepctl.b.mps = DWC_DEP0CTL_MPS_64;
		break;
	case DWC_DSTS_ENUMSPD_LS_PHY_6MHZ:
		diepctl.b.mps = DWC_DEP0CTL_MPS_8;
		break;
	}
	dwc_write_reg32(&dev_if->in_ep_regs[0]->diepctl, diepctl.d32);

	/*Enable OUT EP for receive */
	doepctl.b.epena = 1;
	dwc_write_reg32(&dev_if->out_ep_regs[0]->doepctl, doepctl.d32);

#ifdef VERBOSE
	DWC_DEBUGPL(DBG_PCDV, "doepctl0=%0x\n",
			dwc_read_reg32(&dev_if->out_ep_regs[0]->doepctl));
	DWC_DEBUGPL(DBG_PCDV, "diepctl0=%0x\n",
			dwc_read_reg32(&dev_if->in_ep_regs[0]->diepctl));

#endif	/* */
	dctl.b.cgnpinnak = 1;
	dwc_modify_reg32(&dev_if->dev_global_regs->dctl, dctl.d32, dctl.d32);
	DWC_DEBUGPL(DBG_PCDV, "dctl=%0x\n",
			dwc_read_reg32(&dev_if->dev_global_regs->dctl));
}


#if defined(DEBUG) && defined(VERBOSE)
void dwc_otg_dump_msg(const u8 *buf, unsigned int length)
{
	unsigned int start, num, i;
	char line[52], *p;
	if (length >= 512)
		return;
	start = 0;
	while (length > 0) {
		num = min(length, 16u);
		p = line;
		for (i = 0; i < num; ++i) {
			if (i == 8)
				*p++ = ' ';
			sprintf(p, " %02x", buf[i]);
			p += 3;
		}
		*p = 0;
		DWC_PRINT("%6x: %s\n", start, line);
		buf += num;
		start += num;
		length -= num;
	}
}
#endif	/* */

/**
 *This function writes a packet into the Tx FIFO associated with the
 *EP.	For non-periodic EPs the non-periodic Tx FIFO is written.  For
 *periodic EPs the periodic Tx FIFO associated with the EP is written
 *with all packets for the next micro-frame.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param ep The EP to write packet for.
 *@param dma Indicates if DMA is being used.
 */
void dwc_otg_ep_write_packet(struct dwc_otg_core_if *core_if, struct dwc_ep *ep,
			     int dma)
{
	/**
	 * The buffer is padded to DWORD on a per packet basis in
	 * slave/dma mode if the MPS is not DWORD aligned. The last
	 * packet, if short, is also padded to a multiple of DWORD.
	 *
	 * ep->xfer_buff always starts DWORD aligned in memory and is a
	 * multiple of DWORD in length
	 *
	 * ep->xfer_len can be any number of bytes
	 *
	 * ep->xfer_count is a multiple of ep->maxpacket until the last
	 *	packet
	 *
	 * FIFO access is DWORD
	 */
#ifndef OTG_PPC_PLB_DMA
	u32 i;
#endif
	u32 byte_count;
	u32 dword_count;
	u32 __iomem *fifo;
	u32 *data_buff = (u32 *) ep->xfer_buff;
#if !defined(OTG_PPC_PLB_DMA_TASKLET) && defined(OTG_PPC_PLB_DMA)
	u32 dma_sts = 0;
#endif

	if (ep->xfer_count >= ep->xfer_len) {
		DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "%s() No data for EP%d!!!\n",
				__func__, ep->num);
		return;
	}

	/*Find the byte length of the packet either short packet or MPS */
	if ((ep->xfer_len - ep->xfer_count) < ep->maxpacket)
		byte_count = ep->xfer_len - ep->xfer_count;
	else
		byte_count = ep->maxpacket;


	/*Find the DWORD length, padded by extra bytes as neccessary if MPS
	 *is not a multiple of DWORD */
	dword_count = (byte_count + 3) / 4;

	dwc_otg_dump_msg(ep->xfer_buff, byte_count);

	/**@todo NGS Where are the Periodic Tx FIFO addresses
	 *intialized?	What should this be? */
	fifo = core_if->data_fifo[ep->num];
	DWC_DEBUGPL((DBG_PCDV | DBG_CILV), "fifo=%p buff=%p *p=%08x bc=%d\n",
		       fifo, data_buff, *data_buff, byte_count);
	if (!dma) {
#ifdef OTG_PPC_PLB_DMA
#ifdef OTG_PPC_PLB_DMA_TASKLET
		if (byte_count < USB_BUFSIZ) {
			int i;
			for (i = 0; i < dword_count; i++, data_buff++)
				dwc_write_datafifo32(fifo, *data_buff);
		} else {
			DWC_DEBUGPL(DBG_SP, "%s set release_later %d\n",
					__func__, dword_count);
			atomic_set(&release_later, 1);

			dwc_otg_disable_global_interrupts(core_if);

			core_if->dma_xfer.dma_data_buff = data_buff;
			core_if->dma_xfer.dma_data_fifo = fifo;
			core_if->dma_xfer.dma_count = dword_count;
			core_if->dma_xfer.dma_dir = OTG_TX_DMA;
			tasklet_schedule(core_if->plbdma_tasklet);
		}
#else  /*!OTG_PPC_PLB_DMA_TASKLET */
		ppc4xx_start_plb_dma(core_if, data_buff, fifo, dword_count * 4,
			PLB_DMA_INT_DIS, PLB_DMA_CH, OTG_TX_DMA);

		while (mfdcr(DCRN_DMACR0 + (DMA_CH0*8)) & DMA_CE_ENABLE)
			; /*tight poll loop*/

		dma_sts = (u32)ppc4xx_get_dma_status();

#ifdef OTG_PPC_PLB_DMA_DBG
		if (!(dma_sts & DMA_CS0))
			printk(KERN_DEBUG"DMA Status (Terminal Count not "
				"Occurred) 0x%08x\n", mfdcr(DCRN_DMASR));
#endif
		if (dma_sts & DMA_CH0_ERR)
			printk(KERN_DEBUG"DMA Status (Channel 0 Error) "
					"0x%08x\n",
					mfdcr(DCRN_DMASR));

		ppc4xx_clr_dma_status(PLB_DMA_CH);

#ifdef OTG_PPC_PLB_DMA_DBG
		printk(KERN_DEBUG"%32s DMA Status =0x%08x\n",
				__func__,
				mfdcr(DCRN_DMASR));
#endif

#endif	/*OTG_PPC_PLB_DMA_TASKLET */

#else	/*DWC_SLAVE mode */
		if ((((unsigned long)data_buff) & 0x3) == 0) {
			/*xfer_buff is DWORD aligned. */
			for (i = 0; i < dword_count; i++, data_buff++)
				dwc_write_datafifo32(fifo, *data_buff);
		} else {
			/*xfer_buff is not DWORD aligned. */
			for (i = 0; i < dword_count; i++, data_buff++)
				dwc_write_datafifo32(fifo,
						get_unaligned(data_buff));
		}
#endif
	}

	ep->xfer_count += byte_count;
	ep->xfer_buff += byte_count;
	ep->dma_addr += byte_count;
}

/**
 *This function reads a packet from the Rx FIFO into the destination
 *buffer.	To read SETUP data use dwc_otg_read_setup_packet.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param dest	  Destination buffer for the packet.
 *@param bytes  Number of bytes to copy to the destination.
 */
void dwc_otg_read_packet(struct dwc_otg_core_if *core_if,
			 u8 *dest,  u16 bytes)
{
#ifndef OTG_PPC_PLB_DMA
	int i;
#endif
	int word_count = (bytes + 3) / 4;
	u32 __iomem *fifo = core_if->data_fifo[0];
	u32 *data_buff = (u32 *) dest;
#if !defined(OTG_PPC_PLB_DMA_TASKLET) && defined(OTG_PPC_PLB_DMA)
	u32 dma_sts = 0;
#endif

	/**
	 *@todo Account for the case where dest is not dword aligned. This
	 *requires reading data from the FIFO into a u32 temp buffer,
	 *then moving it into the data buffer.
	 */

	DWC_DEBUGPL((DBG_PCDV | DBG_CILV | DBG_SP), "%s(%p,%p,%d)\n", __func__,
			 core_if, dest, bytes);
#ifdef OTG_PPC_PLB_DMA
#ifdef OTG_PPC_PLB_DMA_TASKLET
	if (bytes < USB_BUFSIZ) {
		int i;
		for (i = 0; i < word_count; i++, data_buff++)
			*data_buff = dwc_read_datafifo32(fifo);
	} else {
		DWC_DEBUGPL(DBG_SP, "%s set release_later %d\n",
				__func__, bytes);
		atomic_set(&release_later, 1);

		dwc_otg_disable_global_interrupts(core_if);

		/*plbdma tasklet */
		core_if->dma_xfer.dma_data_buff = data_buff;
		core_if->dma_xfer.dma_data_fifo = (void *)fifo;
		core_if->dma_xfer.dma_count = word_count;
		core_if->dma_xfer.dma_dir = OTG_RX_DMA;
		tasklet_schedule(core_if->plbdma_tasklet);
	}
#else /*!OTG_PPC_PLB_DMA_TASKLET */
	ppc4xx_start_plb_dma(core_if, (void *)fifo, data_buff, (word_count * 4),
				PLB_DMA_INT_DIS, PLB_DMA_CH, OTG_RX_DMA);

	while (mfdcr(DCRN_DMACR0 + (DMA_CH0*8)) & DMA_CE_ENABLE)
		; /*tight poll loop*/

	dma_sts = (u32)ppc4xx_get_dma_status();
#ifdef OTG_PPC_PLB_DMA_DBG
	if (!(dma_sts & DMA_CS0)) {
		printk(KERN_DEBUG"DMA Status (Terminal Count not occurred) "
				"0x%08x\n",
				mfdcr(DCRN_DMASR));
	}
#endif
	if (dma_sts & DMA_CH0_ERR) {
		printk(KERN_DEBUG"DMA Status (Channel 0 Error) 0x%08x\n",
				mfdcr(DCRN_DMASR));
	}
	ppc4xx_clr_dma_status(PLB_DMA_CH);
#ifdef OTG_PPC_PLB_DMA_DBG
	printk(KERN_DEBUG"%32s DMA Status =0x%08x\n", __func__,
			mfdcr(DCRN_DMASR));
	printk(KERN_DEBUG" Rxed buffer \n");
	for (i = 0; i < bytes; i++)
		printk(KERN_DEBUG" 0x%02x", *(dest + i));

	printk(KERN_DEBUG" \n End of Rxed buffer \n");
#endif
#endif /*OTG_PPC_PLB_DMA_TASKLET */

#else   /*DWC_SLAVE mode */

	BUG_ON(data_buff == NULL);
	if ((((unsigned long)data_buff) & 0x3) == 0) {
		/*xfer_buff is DWORD aligned. */
		for (i = 0; i < word_count; i++, data_buff++)
			*data_buff = dwc_read_datafifo32(fifo);
	} else {
		/*xfer_buff is not DWORD aligned. */
		for (i = 0; i < word_count; i++, data_buff++) {
			u32 temp = dwc_read_datafifo32(fifo);
			put_unaligned(temp, data_buff);
		}
	}

#endif
	return;
}


/**
 *This functions reads the device registers and prints them
 *
 *@param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_dev_registers(struct dwc_otg_core_if *core_if)
{
	int i;
	u32 __iomem *addr;
	DWC_PRINT("Device Global Registers\n");
	addr = &core_if->dev_if->dev_global_regs->dcfg;
	DWC_PRINT("DCFG		 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->dctl;
	DWC_PRINT("DCTL		 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->dsts;
	DWC_PRINT("DSTS		 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->diepmsk;
	DWC_PRINT("DIEPMSK	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->doepmsk;
	DWC_PRINT("DOEPMSK	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->daint;
	DWC_PRINT("DAINT	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->daintmsk;
	DWC_PRINT("DAINTMSK	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->dtknqr1;
	DWC_PRINT("DTKNQR1	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	if (core_if->hwcfg2.b.dev_token_q_depth > 6) {
		addr = &core_if->dev_if->dev_global_regs->dtknqr2;
		DWC_PRINT("DTKNQR2	 @0x%p : 0x%08X\n",
			   addr, dwc_read_reg32(addr));
	}
	addr = &core_if->dev_if->dev_global_regs->dvbusdis;
	DWC_PRINT("DVBUSID	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->dev_if->dev_global_regs->dvbuspulse;
	DWC_PRINT("DVBUSPULSE	@0x%p : 0x%08X\n",
		   addr, dwc_read_reg32(addr));
	if (core_if->hwcfg2.b.dev_token_q_depth > 14) {
		addr = &core_if->dev_if->dev_global_regs->dtknqr3_dthrctl;
		DWC_PRINT("DTKNQR3	 @0x%p : 0x%08X\n",
			   addr, dwc_read_reg32(addr));
	}
	if (core_if->hwcfg2.b.dev_token_q_depth > 22) {
		addr = &core_if->dev_if->dev_global_regs->dtknqr4_fifoemptymsk;
		DWC_PRINT("DTKNQR4	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
	}
	for (i = 0; i <= core_if->dev_if->num_in_eps; i++) {
		DWC_PRINT("Device IN EP %d Registers\n", i);
		addr = &core_if->dev_if->in_ep_regs[i]->diepctl;
		DWC_PRINT("DIEPCTL	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->in_ep_regs[i]->diepint;
		DWC_PRINT("DIEPINT	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->in_ep_regs[i]->dieptsiz;
		DWC_PRINT("DIETSIZ	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->in_ep_regs[i]->diepdma;
		DWC_PRINT("DIEPDMA	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->in_ep_regs[i]->dtxfsts;
		DWC_PRINT("DTXFSTS	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->in_ep_regs[i]->diepdmab;
		DWC_PRINT("DIEPDMAB	 @0x%p : 0x%08X\n", addr,
			   0 /*dwc_read_reg32(addr) */);
	}
	for (i = 0; i <= core_if->dev_if->num_out_eps; i++) {
		DWC_PRINT("Device OUT EP %d Registers\n", i);
		addr = &core_if->dev_if->out_ep_regs[i]->doepctl;
		DWC_PRINT("DOEPCTL	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->out_ep_regs[i]->doepfn;
		DWC_PRINT("DOEPFN	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->out_ep_regs[i]->doepint;
		DWC_PRINT("DOEPINT	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->out_ep_regs[i]->doeptsiz;
		DWC_PRINT("DOETSIZ	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->dev_if->out_ep_regs[i]->doepdma;
		DWC_PRINT("DOEPDMA	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		if (core_if->dma_enable) {
			/*Don't access this register in SLAVE mode */
			addr = &core_if->dev_if->out_ep_regs[i]->doepdmab;
			DWC_PRINT("DOEPDMAB	 @0x%p : 0x%08X\n",
				   addr, dwc_read_reg32(addr));
		}
	}
	return;
}


/**
 *This function reads the host registers and prints them
 *
 *@param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_host_registers(struct dwc_otg_core_if *core_if)
{
	int i;
	u32 __iomem *addr;
	DWC_PRINT("Host Global Registers\n");
	addr = &core_if->host_if->host_global_regs->hcfg;
	DWC_PRINT("HCFG		 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->host_if->host_global_regs->hfir;
	DWC_PRINT("HFIR		 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->host_if->host_global_regs->hfnum;
	DWC_PRINT("HFNUM	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->host_if->host_global_regs->hptxsts;
	DWC_PRINT("HPTXSTS	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->host_if->host_global_regs->haint;
	DWC_PRINT("HAINT	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	addr = &core_if->host_if->host_global_regs->haintmsk;
	DWC_PRINT("HAINTMSK	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	if (core_if->dma_desc_enable) {
		addr = &core_if->host_if->host_global_regs->hflbaddr;
		DWC_PRINT("HFLBADDR	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
	}
	addr = core_if->host_if->hprt0;
	DWC_PRINT("HPRT0	 @0x%p : 0x%08X\n", addr,
		   dwc_read_reg32(addr));
	for (i = 0; i < core_if->core_params->host_channels; i++) {
		DWC_PRINT("Host Channel %d Specific Registers\n", i);
		addr = &core_if->host_if->hc_regs[i]->hcchar;
		DWC_PRINT("HCCHAR	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcsplt;
		DWC_PRINT("HCSPLT	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcint;
		DWC_PRINT("HCINT	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcintmsk;
		DWC_PRINT("HCINTMSK	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->host_if->hc_regs[i]->hctsiz;
		DWC_PRINT("HCTSIZ	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		addr = &core_if->host_if->hc_regs[i]->hcdma;
		DWC_PRINT("HCDMA	 @0x%p : 0x%08X\n", addr,
			   dwc_read_reg32(addr));
		if (core_if->dma_desc_enable) {
			addr = &core_if->host_if->hc_regs[i]->hcdmab;
			DWC_PRINT("HCDMAB	 @0x%p : 0x%08X\n",
					addr, dwc_read_reg32(addr));
		}
	}
	return;
}


/**
 *This function reads the core global registers and prints them
 *
 *@param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_dump_global_registers(struct dwc_otg_core_if *core_if)
{
	int i;
	u32 __iomem *addr;
	DWC_PRINT("Core Global Registers");
	addr = &core_if->core_global_regs->gotgctl;
	DWC_PRINT("GOTGCTL	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gotgint;
	DWC_PRINT("GOTGINT	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gahbcfg;
	DWC_PRINT("GAHBCFG	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gusbcfg;
	DWC_PRINT("GUSBCFG	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->grstctl;
	DWC_PRINT("GRSTCTL	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gintsts;
	DWC_PRINT("GINTSTS	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gintmsk;
	DWC_PRINT("GINTMSK	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->grxstsr;
	DWC_PRINT("GRXSTSR	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));

	/*
	 * addr=&core_if->core_global_regs->grxstsp;
	 * DWC_PRINT("GRXSTSP   @0x%p : 0x%08X\n", addr,
	 * 	dwc_read_reg32(addr));
	 */
	addr = &core_if->core_global_regs->grxfsiz;
	DWC_PRINT("GRXFSIZ	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gnptxfsiz;
	DWC_PRINT("GNPTXFSIZ @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gnptxsts;
	DWC_PRINT("GNPTXSTS	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gi2cctl;
	DWC_PRINT("GI2CCTL	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gpvndctl;
	DWC_PRINT("GPVNDCTL	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->ggpio;
	DWC_PRINT("GGPIO	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->guid;
	DWC_PRINT("GUID		 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->gsnpsid;
	DWC_PRINT("GSNPSID	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->ghwcfg1;
	DWC_PRINT("GHWCFG1	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->ghwcfg2;
	DWC_PRINT("GHWCFG2	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->ghwcfg3;
	DWC_PRINT("GHWCFG3	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->ghwcfg4;
	DWC_PRINT("GHWCFG4	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->glpmcfg;
	DWC_PRINT("GLPMCFG	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	addr = &core_if->core_global_regs->hptxfsiz;
	DWC_PRINT("HPTXFSIZ	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
	for (i = 0; i < core_if->hwcfg4.b.num_dev_perio_in_ep; i++) {
		addr = &core_if->core_global_regs->dptxfsiz_dieptxf[i];
		DWC_PRINT("DPTXFSIZ[%d] @0x%p : 0x%08X\n", i,
				addr, dwc_read_reg32(addr));
	}
	addr = core_if->pcgcctl;
	DWC_PRINT("PCGCCTL	 @0x%p : 0x%08X\n", addr,
			dwc_read_reg32(addr));
}


/**
 *Flush a Tx FIFO.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param _num Tx FIFO to flush.
 */
void dwc_otg_flush_tx_fifo(struct dwc_otg_core_if *core_if,
				  const int num)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	union grstctl_data greset = {.d32 = 0 };
	int count = 0;
	DWC_DEBUGPL((DBG_CIL | DBG_PCDV), "Flush Tx FIFO %d\n", num);
	greset.b.txfflsh = 1;
	greset.b.txfnum = num;
	dwc_write_reg32(&global_regs->grstctl, greset.d32);

	do {
		greset.d32 = dwc_read_reg32(&global_regs->grstctl);
		if (++count > 10000) {
			DWC_WARN("%s() HANG! GRSTCTL=%0x GNPTXSTS=0x%08x\n",
				  __func__, greset.d32,
				  dwc_read_reg32(&global_regs->gnptxsts));
			break;
		}
		udelay(1);
	} while (greset.b.txfflsh == 1);
	/*Wait for 3 PHY Clocks */
	udelay(1);
}


/**
 *Flush Rx FIFO.
 *
 *@param core_if Programming view of DWC_otg controller.
 */
void dwc_otg_flush_rx_fifo(struct dwc_otg_core_if *core_if)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	union grstctl_data greset = {.d32 = 0 };
	int count = 0;
	DWC_DEBUGPL((DBG_CIL | DBG_PCDV), "%s\n", __func__);

	greset.b.rxfflsh = 1;
	dwc_write_reg32(&global_regs->grstctl, greset.d32);

	do {
		greset.d32 = dwc_read_reg32(&global_regs->grstctl);
		if (++count > 10000) {
			DWC_WARN("%s() HANG! GRSTCTL=%0x\n",
					__func__, greset.d32);
			break;
		}
		udelay(1);
	} while (greset.b.rxfflsh == 1);

	/*Wait for 3 PHY Clocks */
	udelay(1);
}


/**
 *Do core a soft reset of the core.  Be careful with this because it
 *resets all the internal state machines of the core.
 */
void dwc_otg_core_reset(struct dwc_otg_core_if *core_if)
{
	struct dwc_otg_core_global_regs __iomem *global_regs =
		core_if->core_global_regs;
	u32 greset = 0;
	int count = 0;
	DWC_DEBUGPL(DBG_CILV, "%s\n", __func__);

	/*Wait for AHB master IDLE state. */
	do {
		udelay(10);
		greset = dwc_read_reg32(&global_regs->grstctl);
		if (++count > 100000) {
			DWC_WARN("%s() HANG! AHB Idle GRSTCTL=%0x\n",
					__func__, greset);
			return;
		}
	} while ((greset & 0x80000000) == 0);

	/*Core Soft Reset */
	count = 0;
	greset |= 1;
	dwc_write_reg32(&global_regs->grstctl, greset);
	wmb();
	mdelay(1);
	do {
		udelay(10);
		greset = dwc_read_reg32(&global_regs->grstctl);
		if (++count > 10000) {
			DWC_WARN("%s() HANG! Soft Reset GRSTCTL=%0x\n",
					__func__, greset);
			break;
		}
	} while (greset & 0x1);

	/*Wait for at least 3 PHY Clocks */
	mdelay(500);
}


/**
 *Register HCD callbacks.	The callbacks are used to start and stop
 *the HCD for interrupt processing.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param _cb the HCD callback structure.
 *@param _p pointer to be passed to callback function (usb_hcd*).
 */
void dwc_otg_cil_register_hcd_callbacks(struct dwc_otg_core_if *core_if,
		       struct dwc_otg_cil_callbacks *_cb, void *_p)
{
	core_if->hcd_cb = _cb;
	_cb->p = _p;
}

/**
 *Register PCD callbacks.	The callbacks are used to start and stop
 *the PCD for interrupt processing.
 *
 *@param core_if Programming view of DWC_otg controller.
 *@param _cb the PCD callback structure.
 *@param _p pointer to be passed to callback function (pcd*).
 */
void dwc_otg_cil_register_pcd_callbacks(struct dwc_otg_core_if *core_if,
		       struct dwc_otg_cil_callbacks *_cb, void *_p)
{
	core_if->pcd_cb = _cb;
	_cb->p = _p;
}

static int dwc_otg_param_initialized(int val)
{
	return val != -1;
}
u8 dwc_otg_is_dma_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->dma_enable;
}

/*Checks if the parameter is outside of its valid range of values */
#define DWC_OTG_PARAM_TEST(_param_, _low_, _high_) \
		(((_param_) < (_low_)) || \
		((_param_) > (_high_)))
/*Parameter access functions */
int dwc_otg_set_param_otg_cap(struct dwc_otg_core_if *core_if, int val)
{
	int valid;
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 2)) {
		DWC_WARN("Wrong value for otg_cap parameter\n");
		DWC_WARN("otg_cap parameter must be 0,1 or 2\n");
		retval = -EINVAL;
		goto out;
	}

	valid = 1;
	switch (val) {
	case DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE:
		if (core_if->hwcfg2.b.op_mode !=
		    DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
			valid = 0;
		break;
	case DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE:
		if ((core_if->hwcfg2.b.op_mode !=
		     DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
		    && (core_if->hwcfg2.b.op_mode !=
			DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG)
		    && (core_if->hwcfg2.b.op_mode !=
			DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE)
		    && (core_if->hwcfg2.b.op_mode !=
		      DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)) {
			valid = 0;
		}
		break;
	case DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE:
		/*always valid */
		break;
	}
	if (!valid) {
		if (dwc_otg_param_initialized(core_if->core_params->otg_cap)) {
			DWC_ERROR("%d invalid for otg_cap paremter. "
					"Check HW configuration.\n",
					val);
		}
		val = (((core_if->hwcfg2.b.op_mode ==
			DWC_HWCFG2_OP_MODE_HNP_SRP_CAPABLE_OTG)
			|| (core_if->hwcfg2.b.op_mode ==
			DWC_HWCFG2_OP_MODE_SRP_ONLY_CAPABLE_OTG)
			|| (core_if->hwcfg2.b.op_mode ==
			DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE)
			|| (core_if->hwcfg2.b.op_mode ==
			DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)) ?
					DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE :
					DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE);
		retval = -EINVAL;
	}

	core_if->core_params->otg_cap = val;
out:
	return retval;
}

int dwc_otg_get_param_otg_cap(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->otg_cap;
}

int dwc_otg_set_param_opt(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for opt parameter\n");
		return -EINVAL;
	}
	core_if->core_params->opt = val;
	return 0;
}

int dwc_otg_get_param_opt(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->opt;
}

int dwc_otg_set_param_dma_enable(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for dma enable\n");
		return -EINVAL;
	}

	if ((val == 1) && (core_if->hwcfg2.b.architecture == 0)) {
		if (dwc_otg_param_initialized(core_if->core_params->dma_enable))
			DWC_ERROR("%d invalid for dma_enable parameter. "
					"Check HW configuration.\n", val);
		val = 0;
		retval = -EINVAL;
	}

	core_if->core_params->dma_enable = val;
	if (val == 0)
		dwc_otg_set_param_dma_desc_enable(core_if, 0);

	return retval;
}

int dwc_otg_get_param_dma_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->dma_enable;
}

int dwc_otg_set_param_dma_desc_enable(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for dma_enable\n");
		DWC_WARN("dma_desc_enable must be 0 or 1\n");
		return -EINVAL;
	}

	if ((val == 1)
	    && ((dwc_otg_get_param_dma_enable(core_if) == 0)
		|| (core_if->hwcfg4.b.desc_dma == 0))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dma_desc_enable)) {
			DWC_ERROR("%d invalid for dma_desc_enable parameter. "
					"Check HW configuration.\n", val);
		}
		val = 0;
		retval = -EINVAL;
	}
	core_if->core_params->dma_desc_enable = val;
	return retval;
}

int dwc_otg_get_param_dma_desc_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->dma_desc_enable;
}

int
dwc_otg_set_param_host_support_fs_ls_low_power(struct dwc_otg_core_if *core_if,
						   int val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for host_support_fs_low_power\n");
		DWC_WARN("host_support_fs_low_power must be 0 or 1\n");
		return -EINVAL;
	}
	core_if->core_params->host_support_fs_ls_low_power = val;
	return 0;
}

int dwc_otg_get_param_host_support_fs_ls_low_power(struct dwc_otg_core_if *
						       core_if)
{
	return core_if->core_params->host_support_fs_ls_low_power;
}

int dwc_otg_set_param_enable_dynamic_fifo(struct dwc_otg_core_if *core_if,
					  int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for enable_dynamic_fifo\n");
		DWC_WARN("enable_dynamic_fifo must be 0 or 1\n");
		return -EINVAL;
	}

	if ((val == 1) && (core_if->hwcfg2.b.dynamic_fifo == 0)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->enable_dynamic_fifo)) {
			DWC_ERROR("%d invalid for enable_dynamic_fifo parameter"
					" Check HW configuration.\n", val);
		}
		val = 0;
		retval = -EINVAL;
	}
	core_if->core_params->enable_dynamic_fifo = val;
	return retval;
}

int dwc_otg_get_param_enable_dynamic_fifo(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->enable_dynamic_fifo;
}

int dwc_otg_set_param_data_fifo_size(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 32, 32768)) {
		DWC_WARN("Wrong value for data_fifo_size\n");
		DWC_WARN("data_fifo_size must be 32-32768\n");
		return -EINVAL;
	}

	if (val > core_if->hwcfg3.b.dfifo_depth) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->data_fifo_size)) {
			DWC_ERROR("%d invalid for data_fifo_size parameter. "
					"Check HW configuration.\n", val);
		}
		val = core_if->hwcfg3.b.dfifo_depth;
		retval = -EINVAL;
	}

	core_if->core_params->data_fifo_size = val;
	return retval;
}

int dwc_otg_get_param_data_fifo_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->data_fifo_size;
}

int dwc_otg_set_param_dev_rx_fifo_size(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for dev_rx_fifo_size\n");
		DWC_WARN("dev_rx_fifo_size must be 16-32768\n");
		return -EINVAL;
	}

	if (val > dwc_read_reg32(&core_if->core_global_regs->grxfsiz)) {
		if (dwc_otg_param_initialized(core_if->core_params->dev_rx_fifo_size)) {
			DWC_WARN("%d invalid for dev_rx_fifo_size parameter\n",
					val);
		}
		val = dwc_read_reg32(&core_if->core_global_regs->grxfsiz);
		retval = -EINVAL;
	}

	core_if->core_params->dev_rx_fifo_size = val;
	return retval;
}

int dwc_otg_get_param_dev_rx_fifo_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->dev_rx_fifo_size;
}

int dwc_otg_set_param_dev_nperio_tx_fifo_size(struct dwc_otg_core_if *core_if,
					      int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for dev_nperio_tx_fifo\n");
		DWC_WARN("dev_nperio_tx_fifo must be 16-32768\n");
		return -EINVAL;
	}

	if (val >
		(dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >> 16)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dev_nperio_tx_fifo_size)) {
			DWC_ERROR("%d invalid for dev_nperio_tx_fifo_size."
					"Check HW configuration.\n", val);
		}
		val = (dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >>
				16);
		retval = -EINVAL;
	}

	core_if->core_params->dev_nperio_tx_fifo_size = val;
	return retval;
}

int dwc_otg_get_param_dev_nperio_tx_fifo_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->dev_nperio_tx_fifo_size;
}

int dwc_otg_set_param_host_rx_fifo_size(struct dwc_otg_core_if *core_if,
					int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for host_rx_fifo_size\n");
		DWC_WARN("host_rx_fifo_size must be 16-32768\n");
		return -EINVAL;
	}

	if (val > dwc_read_reg32(&core_if->core_global_regs->grxfsiz)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_rx_fifo_size)) {
			DWC_ERROR("%d invalid for host_rx_fifo_size. "
					"Check HW configuration.\n", val);
		}
		val = dwc_read_reg32(&core_if->core_global_regs->grxfsiz);
		retval = -EINVAL;
	}

	core_if->core_params->host_rx_fifo_size = val;
	return retval;

}

int dwc_otg_get_param_host_rx_fifo_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->host_rx_fifo_size;
}

int dwc_otg_set_param_host_nperio_tx_fifo_size(struct dwc_otg_core_if *core_if,
					       int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for host_nperio_tx_fifo_size\n");
		DWC_WARN("host_nperio_tx_fifo_size must be 16-32768\n");
		return -EINVAL;
	}

	if (val > (dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >> 16)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_nperio_tx_fifo_size)) {
			DWC_ERROR("%d invalid for host_nperio_tx_fifo_size. "
					"Check HW configuration.\n", val);
		}
		val =
		    (dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz) >>
		     16);
		retval = -EINVAL;
	}

	core_if->core_params->host_nperio_tx_fifo_size = val;
	return retval;
}

int dwc_otg_get_param_host_nperio_tx_fifo_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->host_nperio_tx_fifo_size;
}

int dwc_otg_set_param_host_perio_tx_fifo_size(struct dwc_otg_core_if *core_if,
					      int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 16, 32768)) {
		DWC_WARN("Wrong value for host_perio_tx_fifo_size\n");
		DWC_WARN("host_perio_tx_fifo_size must be 16-32768\n");
		return -EINVAL;
	}

	if (val >
	    ((dwc_read_reg32(&core_if->core_global_regs->hptxfsiz) >> 16))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_perio_tx_fifo_size)) {
			DWC_ERROR("%d invalid for host_perio_tx_fifo_size. "
					"Check HW configuration.\n", val);
		}
		val =
		    (dwc_read_reg32(&core_if->core_global_regs->hptxfsiz) >>
		     16);
		retval = -EINVAL;
	}

	core_if->core_params->host_perio_tx_fifo_size = val;
	return retval;
}

int dwc_otg_get_param_host_perio_tx_fifo_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->host_perio_tx_fifo_size;
}

int dwc_otg_set_param_max_transfer_size(struct dwc_otg_core_if *core_if,
					int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 2047, 524288)) {
		DWC_WARN("Wrong value for max_transfer_size\n");
		DWC_WARN("max_transfer_size must be 2047-524288\n");
		return -EINVAL;
	}

	if (val >= (1 << (core_if->hwcfg3.b.xfer_size_cntr_width + 11))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->max_transfer_size)) {
			DWC_ERROR("%d invalid for max_transfer_size. "
					"Check HW configuration.\n", val);
		}
		val =
		    ((1 << (core_if->hwcfg3.b.packet_size_cntr_width + 11)) -
		     1);
		retval = -EINVAL;
	}

	core_if->core_params->max_transfer_size = val;
	return retval;
}

int dwc_otg_get_param_max_transfer_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->max_transfer_size;
}

int dwc_otg_set_param_max_packet_count(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 15, 511)) {
		DWC_WARN("Wrong value for max_packet_count\n");
		DWC_WARN("max_packet_count must be 15-511\n");
		return -EINVAL;
	}

	if (val > (1 << (core_if->hwcfg3.b.packet_size_cntr_width + 4))) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->max_packet_count)) {
			DWC_ERROR("%d invalid for max_packet_count. "
					"Check HW configuration.\n", val);
		}
		val =
		    ((1 << (core_if->hwcfg3.b.packet_size_cntr_width + 4)) - 1);
		retval = -EINVAL;
	}

	core_if->core_params->max_packet_count = val;
	return retval;
}

int dwc_otg_get_param_max_packet_count(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->max_packet_count;
}

int dwc_otg_set_param_host_channels(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 1, 16)) {
		DWC_WARN("Wrong value for host_channels\n");
		DWC_WARN("host_channels must be 1-16\n");
		return -EINVAL;
	}

	if (val > (core_if->hwcfg2.b.num_host_chan + 1)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->host_channels)) {
			DWC_ERROR("%d invalid for host_channels."
					"Check HW configurations.\n", val);
		}
		val = (core_if->hwcfg2.b.num_host_chan + 1);
		retval = -EINVAL;
	}

	core_if->core_params->host_channels = val;
	return retval;
}

int dwc_otg_get_param_host_channels(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->host_channels;
}

int dwc_otg_set_param_dev_endpoints(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 1, 15)) {
		DWC_WARN("Wrong value for dev_endpoints\n");
		DWC_WARN("dev_endpoints must be 1-15\n");
		return -EINVAL;
	}

	if (val > (core_if->hwcfg2.b.num_dev_ep)) {
		if (dwc_otg_param_initialized
		    (core_if->core_params->dev_endpoints)) {
			DWC_ERROR("%d invalid for dev_endpoints. "
					"Check HW configurations.\n", val);
		}
		val = core_if->hwcfg2.b.num_dev_ep;
		retval = -EINVAL;
	}

	core_if->core_params->dev_endpoints = val;
	return retval;
}

int dwc_otg_get_param_dev_endpoints(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->dev_endpoints;
}

int dwc_otg_set_param_phy_type(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	int valid = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 2)) {
		DWC_WARN("Wrong value for phy_type\n");
		DWC_WARN("phy_type must be 0,1 or 2\n");
		return -EINVAL;
	}
#ifndef NO_FS_PHY_HW_CHECKS
	if ((val == DWC_PHY_TYPE_PARAM_UTMI) &&
	    ((core_if->hwcfg2.b.hs_phy_type == 1) ||
	     (core_if->hwcfg2.b.hs_phy_type == 3))) {
		valid = 1;
	} else if ((val == DWC_PHY_TYPE_PARAM_ULPI) &&
		   ((core_if->hwcfg2.b.hs_phy_type == 2) ||
		    (core_if->hwcfg2.b.hs_phy_type == 3))) {
		valid = 1;
	} else if ((val == DWC_PHY_TYPE_PARAM_FS) &&
		   (core_if->hwcfg2.b.fs_phy_type == 1)) {
		valid = 1;
	}
	if (!valid) {
		if (dwc_otg_param_initialized(core_if->core_params->phy_type)) {
			DWC_ERROR("%d invalid for phy_type. "
					"Check HW configurations.\n", val);
		}
		if (core_if->hwcfg2.b.hs_phy_type) {
			if ((core_if->hwcfg2.b.hs_phy_type == 3) ||
			    (core_if->hwcfg2.b.hs_phy_type == 1)) {
				val = DWC_PHY_TYPE_PARAM_UTMI;
			} else {
				val = DWC_PHY_TYPE_PARAM_ULPI;
			}
		}
		retval = -EINVAL;
	}
#endif
	core_if->core_params->phy_type = val;
	return retval;
}

int dwc_otg_get_param_phy_type(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->phy_type;
}

int dwc_otg_set_param_speed(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for speed parameter\n");
		DWC_WARN("max_speed parameter must be 0 or 1\n");
		return -EINVAL;
	}
	if ((val == 0)
	    && dwc_otg_get_param_phy_type(core_if) == DWC_PHY_TYPE_PARAM_FS) {
		if (dwc_otg_param_initialized(core_if->core_params->speed)) {
			DWC_ERROR("%d invalid for speed paremter. "
					"Check HW configuration.\n", val);
		}
		val =
		    (dwc_otg_get_param_phy_type(core_if) ==
		     DWC_PHY_TYPE_PARAM_FS ? 1 : 0);
		retval = -EINVAL;
	}
	core_if->core_params->speed = val;
	return retval;
}

int dwc_otg_get_param_speed(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->speed;
}

int dwc_otg_set_param_host_ls_low_power_phy_clk(struct dwc_otg_core_if *core_if,
						int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN
		    ("Wrong value for host_ls_low_power_phy_clk parameter\n");
		DWC_WARN("host_ls_low_power_phy_clk must be 0 or 1\n");
		return -EINVAL;
	}

	if ((val == DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ)
	    && (dwc_otg_get_param_phy_type(core_if) == DWC_PHY_TYPE_PARAM_FS)) {
		if (dwc_otg_param_initialized(core_if->core_params->host_ls_low_power_phy_clk)) {
			DWC_ERROR("%d invalid for host_ls_low_power_phy_clk. "
					"Check HW configuration.\n", val);
		}
		val =
		    (dwc_otg_get_param_phy_type(core_if) ==
		     DWC_PHY_TYPE_PARAM_FS) ?
		    DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ :
		    DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ;

		retval = -EINVAL;
	}

	core_if->core_params->host_ls_low_power_phy_clk = val;
	return retval;
}

int dwc_otg_get_param_host_ls_low_power_phy_clk(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->host_ls_low_power_phy_clk;
}

int dwc_otg_set_param_phy_ulpi_ddr(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for phy_ulpi_ddr\n");
		DWC_WARN("phy_upli_ddr must be 0 or 1\n");
		return -EINVAL;
	}

	core_if->core_params->phy_ulpi_ddr = val;
	return 0;
}

int dwc_otg_get_param_phy_ulpi_ddr(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->phy_ulpi_ddr;
}

int dwc_otg_set_param_phy_ulpi_ext_vbus(struct dwc_otg_core_if *core_if,
					int val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for phy_ulpi_ext_vbus\n");
		DWC_WARN("phy_ulpi_ext_vbus must be 0 or 1\n");
		return -EINVAL;
	}

	core_if->core_params->phy_ulpi_ext_vbus = val;
	return 0;
}

int dwc_otg_get_param_phy_ulpi_ext_vbus(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->phy_ulpi_ext_vbus;
}

int dwc_otg_set_param_phy_utmi_width(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 8, 8) && DWC_OTG_PARAM_TEST(val, 16, 16)) {
		DWC_WARN("Wrong valaue for phy_utmi_width\n");
		DWC_WARN("phy_utmi_width must be 8 or 16\n");
		return -EINVAL;
	}

	core_if->core_params->phy_utmi_width = val;
	return 0;
}

int dwc_otg_get_param_phy_utmi_width(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->phy_utmi_width;
}

int dwc_otg_set_param_ulpi_fs_ls(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for ulpi_fs_ls\n");
		DWC_WARN("ulpi_fs_ls must be 0 or 1\n");
		return -EINVAL;
	}

	core_if->core_params->ulpi_fs_ls = val;
	return 0;
}

int dwc_otg_get_param_ulpi_fs_ls(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->ulpi_fs_ls;
}

int dwc_otg_set_param_ts_dline(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for ts_dline\n");
		DWC_WARN("ts_dline must be 0 or 1\n");
		return -EINVAL;
	}

	core_if->core_params->ts_dline = val;
	return 0;
}

int dwc_otg_get_param_ts_dline(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->ts_dline;
}

int dwc_otg_set_param_i2c_enable(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for i2c_enable\n");
		DWC_WARN("i2c_enable must be 0 or 1\n");
		return -EINVAL;
	}
#ifndef NO_FS_PHY_HW_CHECK
	if (val == 1 && core_if->hwcfg3.b.i2c == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->i2c_enable))
			DWC_ERROR("%d invalid for i2c_enable. "
					"Check HW configuration.\n", val);
		val = 0;
		retval = -EINVAL;
	}
#endif

	core_if->core_params->i2c_enable = val;
	return retval;
}

int dwc_otg_get_param_i2c_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->i2c_enable;
}

int dwc_otg_set_param_dev_perio_tx_fifo_size(struct dwc_otg_core_if *core_if,
					     int val, int fifo_num)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 4, 768)) {
		DWC_WARN("Wrong value for dev_perio_tx_fifo_size\n");
		DWC_WARN("dev_perio_tx_fifo_size must be 4-768\n");
		return -EINVAL;
	}

	if (val > (dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[fifo_num]))) {
		if (dwc_otg_param_initialized(core_if->core_params->dev_perio_tx_fifo_size[fifo_num])) {
			DWC_ERROR("`%d' invalid for parameter "
					"`dev_perio_fifo_size_%d'. "
					"Check HW configuration.\n",
					val, fifo_num);
		}
		val = (dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[fifo_num]));
		retval = -EINVAL;
	}

	core_if->core_params->dev_perio_tx_fifo_size[fifo_num] = val;
	return retval;
}

int dwc_otg_get_param_dev_perio_tx_fifo_size(struct dwc_otg_core_if *core_if,
						 int fifo_num)
{
	return core_if->core_params->dev_perio_tx_fifo_size[fifo_num];
}

int dwc_otg_set_param_en_multiple_tx_fifo(struct dwc_otg_core_if *core_if,
					  int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong valaue for en_multiple_tx_fifo,\n");
		DWC_WARN("en_multiple_tx_fifo must be 0 or 1\n");
		return -EINVAL;
	}

	if (val == 1 && core_if->hwcfg4.b.ded_fifo_en == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->en_multiple_tx_fifo)) {
			DWC_ERROR("%d invalid for parameter en_multiple_tx_fifo"
					" Check HW configuration.\n",
					val);
		}
		val = 0;
		retval = -EINVAL;
	}

	core_if->core_params->en_multiple_tx_fifo = val;
	return retval;
}

int dwc_otg_get_param_en_multiple_tx_fifo(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->en_multiple_tx_fifo;
}

int dwc_otg_set_param_dev_tx_fifo_size(struct dwc_otg_core_if *core_if, int val,
				       int fifo_num)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 4, 768)) {
		DWC_WARN("Wrong value for dev_tx_fifo_size\n");
		DWC_WARN("dev_tx_fifo_size must be 4-768\n");
		return -EINVAL;
	}

	if (val > (dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[fifo_num]))) {
		if (dwc_otg_param_initialized(core_if->core_params->dev_tx_fifo_size[fifo_num])) {
			DWC_ERROR("`%d' invalid for parameter "
					"`dev_tx_fifo_size_%d'. "
					"Check HW configuration.\n",
					val,
					fifo_num);
		}
		val =
			(dwc_read_reg32(&core_if->core_global_regs->dptxfsiz_dieptxf[fifo_num]));
		retval = -EINVAL;
	}

	core_if->core_params->dev_tx_fifo_size[fifo_num] = val;
	return retval;
}

int dwc_otg_get_param_dev_tx_fifo_size(struct dwc_otg_core_if *core_if,
					   int fifo_num)
{
	return core_if->core_params->dev_tx_fifo_size[fifo_num];
}

int dwc_otg_set_param_thr_ctl(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 7)) {
		DWC_WARN("Wrong value for thr_ctl\n");
		DWC_WARN("thr_ctl must be 0-7\n");
		return -EINVAL;
	}

	if ((val != 0) && (!dwc_otg_get_param_dma_enable(core_if) ||
				!core_if->hwcfg4.b.ded_fifo_en)) {
		if (dwc_otg_param_initialized(core_if->core_params->thr_ctl)) {
			DWC_ERROR("%d invalid for parameter thr_ctl. "
					"Check HW configuration.\n", val);
		}
		val = 0;
		retval = -EINVAL;
	}

	core_if->core_params->thr_ctl = val;
	return retval;
}

static int dwc_otg_get_param_thr_ctl(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->thr_ctl;
}

int dwc_otg_set_param_lpm_enable(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;

	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("Wrong value for lpm_enable\n");
		DWC_WARN("lpm_enable must be 0 or 1\n");
		return -EINVAL;
	}

	if (val && !core_if->hwcfg3.b.otg_lpm_en) {
		if (dwc_otg_param_initialized(core_if->core_params->lpm_enable))
			DWC_ERROR("%d invalid for parameter lpm_enable."
					"Check HW configuration.\n", val);
		val = 0;
		retval = -EINVAL;
	}

	core_if->core_params->lpm_enable = val;
	return retval;
}

int dwc_otg_get_param_lpm_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->lpm_enable;
}

int dwc_otg_set_param_tx_thr_length(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 8, 128)) {
		DWC_WARN("Wrong valaue for tx_thr_length\n");
		DWC_WARN("tx_thr_length must be 8 - 128\n");
		return -EINVAL;
	}

	core_if->core_params->tx_thr_length = val;
	return 0;
}

int dwc_otg_get_param_tx_thr_length(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->tx_thr_length;
}

int dwc_otg_set_param_rx_thr_length(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 8, 128)) {
		DWC_WARN("Wrong valaue for rx_thr_length\n");
		DWC_WARN("rx_thr_length must be 8 - 128\n");
		return -EINVAL;
	}

	core_if->core_params->rx_thr_length = val;
	return 0;
}

int dwc_otg_get_param_rx_thr_length(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->rx_thr_length;
}

int dwc_otg_set_param_dma_burst_size(struct dwc_otg_core_if *core_if, int val)
{
	if (DWC_OTG_PARAM_TEST(val, 1, 1) &&
	    DWC_OTG_PARAM_TEST(val, 4, 4) &&
	    DWC_OTG_PARAM_TEST(val, 8, 8) &&
	    DWC_OTG_PARAM_TEST(val, 16, 16) &&
	    DWC_OTG_PARAM_TEST(val, 32, 32) &&
	    DWC_OTG_PARAM_TEST(val, 64, 64) &&
	    DWC_OTG_PARAM_TEST(val, 128, 128) &&
	    DWC_OTG_PARAM_TEST(val, 256, 256)) {
		DWC_WARN("`%d' invalid for parameter `dma_burst_size'\n", val);
		return -EINVAL;
	}
	core_if->core_params->dma_burst_size = val;
	return 0;
}

int dwc_otg_get_param_dma_burst_size(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->dma_burst_size;
}

int dwc_otg_set_param_pti_enable(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `pti_enable'\n", val);
		return -EINVAL;
	}
	if (val && (core_if->snpsid < OTG_CORE_REV_2_72a)) {
		if (dwc_otg_param_initialized(core_if->core_params->pti_enable))
			DWC_ERROR("%d invalid for parameter pti_enable. "
					"Check HW configuration.\n", val);
		retval = -EINVAL;
		val = 0;
	}
	core_if->core_params->pti_enable = val;
	return retval;
}

int dwc_otg_get_param_pti_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->pti_enable;
}

int dwc_otg_set_param_mpi_enable(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `mpi_enable'\n", val);
		return -EINVAL;
	}
	if (val && (core_if->hwcfg2.b.multi_proc_int == 0)) {
		if (dwc_otg_param_initialized(core_if->core_params->mpi_enable))
			DWC_ERROR("%d invalid for parameter mpi_enable. "
					"Check HW configuration.\n", val);
		retval = -EINVAL;
		val = 0;
	}
	core_if->core_params->mpi_enable = val;
	return retval;
}

int dwc_otg_get_param_mpi_enable(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->mpi_enable;
}

int dwc_otg_set_param_ic_usb_cap(struct dwc_otg_core_if *core_if,
					int val)
{
	int retval = 0;
	if (DWC_OTG_PARAM_TEST(val, 0, 1)) {
		DWC_WARN("`%d' invalid for parameter `ic_usb_cap'\n", val);
		DWC_WARN("ic_usb_cap must be 0 or 1\n");
		return -EINVAL;
	}

	if (val && (core_if->hwcfg3.b.otg_enable_ic_usb == 0)) {
		if (dwc_otg_param_initialized(core_if->core_params->ic_usb_cap))
			DWC_ERROR("%d invalid for parameter ic_usb_cap. "
					"Check HW configuration.\n", val);
		retval = -EINVAL;
		val = 0;
	}
	core_if->core_params->ic_usb_cap = val;
	return retval;
}
int dwc_otg_get_param_ic_usb_cap(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->ic_usb_cap;
}

int dwc_otg_set_param_ahb_thr_ratio(struct dwc_otg_core_if *core_if, int val)
{
	int retval = 0;
	int valid = 1;

	if (DWC_OTG_PARAM_TEST(val, 0, 3)) {
		DWC_WARN("`%d' invalid for parameter `ahb_thr_ratio'\n", val);
		DWC_WARN("ahb_thr_ratio must be 0 - 3\n");
		return -EINVAL;
	}

	if (val && (core_if->snpsid < OTG_CORE_REV_2_81a ||
			!dwc_otg_get_param_thr_ctl(core_if)))
		valid = 0;
	else
		if (val && ((dwc_otg_get_param_tx_thr_length(core_if) /
				(1 << val)) < 4))
			valid = 0;

	if (valid == 0) {
		if (dwc_otg_param_initialized(core_if->core_params->ahb_thr_ratio))
			DWC_ERROR("%d invalid for parameter ahb_thr_ratio. "
					"Check HW configuration.\n", val);

		retval = -EINVAL;
		val = 0;
	}

	core_if->core_params->ahb_thr_ratio = val;
	return retval;
}
int dwc_otg_get_param_ahb_thr_ratio(struct dwc_otg_core_if *core_if)
{
	return core_if->core_params->ahb_thr_ratio;
}
u32 dwc_otg_get_hnpstatus(struct dwc_otg_core_if *core_if)
{
	union gotgctl_data otgctl;
	otgctl.d32 = dwc_read_reg32(&core_if->core_global_regs->gotgctl);
	return otgctl.b.hstnegscs;
}

u32 dwc_otg_get_srpstatus(struct dwc_otg_core_if *core_if)
{
	union gotgctl_data otgctl;
	otgctl.d32 = dwc_read_reg32(&core_if->core_global_regs->gotgctl);
	return otgctl.b.sesreqscs;
}

void dwc_otg_set_hnpreq(struct dwc_otg_core_if *core_if, u32 val)
{
	union gotgctl_data otgctl;
	otgctl.d32 = dwc_read_reg32(&core_if->core_global_regs->gotgctl);
	otgctl.b.hnpreq = val;
	dwc_write_reg32(&core_if->core_global_regs->gotgctl, otgctl.d32);
}

u32 dwc_otg_get_gsnpsid(struct dwc_otg_core_if *core_if)
{
	return core_if->snpsid;
}

u32 dwc_otg_get_mode(struct dwc_otg_core_if *core_if)
{
	union gotgctl_data otgctl;
	otgctl.d32 = dwc_read_reg32(&core_if->core_global_regs->gotgctl);
	return otgctl.b.currmod;
}

u32 dwc_otg_get_hnpcapable(struct dwc_otg_core_if *core_if)
{
	union gusbcfg_data usbcfg;
	usbcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->gusbcfg);
	return usbcfg.b.hnpcap;
}

void dwc_otg_set_hnpcapable(struct dwc_otg_core_if *core_if, u32 val)
{
	union gusbcfg_data usbcfg;
	usbcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->gusbcfg);
	usbcfg.b.hnpcap = val;
	dwc_write_reg32(&core_if->core_global_regs->gusbcfg, usbcfg.d32);
}

u32 dwc_otg_get_srpcapable(struct dwc_otg_core_if *core_if)
{
	union gusbcfg_data usbcfg;
	usbcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->gusbcfg);
	return usbcfg.b.srpcap;
}

void dwc_otg_set_srpcapable(struct dwc_otg_core_if *core_if, u32 val)
{
	union gusbcfg_data usbcfg;
	usbcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->gusbcfg);
	usbcfg.b.srpcap = val;
	dwc_write_reg32(&core_if->core_global_regs->gusbcfg, usbcfg.d32);
}

u32 dwc_otg_get_devspeed(struct dwc_otg_core_if *core_if)
{
	union dcfg_data dcfg;
	dcfg.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dcfg);
	return dcfg.b.devspd;
}

void dwc_otg_set_devspeed(struct dwc_otg_core_if *core_if, u32 val)
{
	union dcfg_data dcfg;
	dcfg.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dcfg);
	dcfg.b.devspd = val;
	dwc_write_reg32(&core_if->dev_if->dev_global_regs->dcfg, dcfg.d32);
}

u32 dwc_otg_get_busconnected(struct dwc_otg_core_if *core_if)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	return hprt0.b.prtconnsts;
}

u32 dwc_otg_get_enumspeed(struct dwc_otg_core_if *core_if)
{
	union dsts_data dsts;
	dsts.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dsts);
	return dsts.b.enumspd;
}

u32 dwc_otg_get_prtpower(struct dwc_otg_core_if *core_if)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	return hprt0.b.prtpwr;

}

void dwc_otg_set_prtpower(struct dwc_otg_core_if *core_if, u32 val)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	hprt0.b.prtpwr = val;
	dwc_write_reg32(core_if->host_if->hprt0, val);
}

u32 dwc_otg_get_prtsuspend(struct dwc_otg_core_if *core_if)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	return hprt0.b.prtsusp;

}

void dwc_otg_set_prtsuspend(struct dwc_otg_core_if *core_if, u32 val)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	hprt0.b.prtsusp = val;
	dwc_write_reg32(core_if->host_if->hprt0, val);
}

void dwc_otg_set_prtresume(struct dwc_otg_core_if *core_if, u32 val)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	hprt0.b.prtres = val;
	dwc_write_reg32(core_if->host_if->hprt0, val);
}

u32 dwc_otg_get_remotewakesig(struct dwc_otg_core_if *core_if)
{
	union dctl_data dctl;
	dctl.d32 = dwc_read_reg32(&core_if->dev_if->dev_global_regs->dctl);
	return dctl.b.rmtwkupsig;
}

u32 dwc_otg_get_lpm_portsleepstatus(struct dwc_otg_core_if *core_if)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);

	if ((core_if->lx_state == DWC_OTG_L1) ^ lpmcfg.b.prt_sleep_sts) {
		printk(KERN_ERR"lx_state = %d, lmpcfg.prt_sleep_sts = %d\n",
				core_if->lx_state, lpmcfg.b.prt_sleep_sts);
		BUG_ON(1);
	}
	return lpmcfg.b.prt_sleep_sts;
}

u32 dwc_otg_get_lpm_remotewakeenabled(struct dwc_otg_core_if *core_if)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.rem_wkup_en;
}

u32 dwc_otg_get_lpmresponse(struct dwc_otg_core_if *core_if)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.appl_resp;
}

void dwc_otg_set_lpmresponse(struct dwc_otg_core_if *core_if, u32 val)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.appl_resp = val;
	dwc_write_reg32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

u32 dwc_otg_get_hsic_connect(struct dwc_otg_core_if *core_if)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.hsic_connect;
}

void dwc_otg_set_hsic_connect(struct dwc_otg_core_if *core_if, u32 val)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.hsic_connect = val;
	dwc_write_reg32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

u32 dwc_otg_get_inv_sel_hsic(struct dwc_otg_core_if *core_if)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	return lpmcfg.b.inv_sel_hsic;

}

void dwc_otg_set_inv_sel_hsic(struct dwc_otg_core_if *core_if, u32 val)
{
	union glpmcfg_data lpmcfg;
	lpmcfg.d32 = dwc_read_reg32(&core_if->core_global_regs->glpmcfg);
	lpmcfg.b.inv_sel_hsic = val;
	dwc_write_reg32(&core_if->core_global_regs->glpmcfg, lpmcfg.d32);
}

u32 dwc_otg_get_gotgctl(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gotgctl);
}

void dwc_otg_set_gotgctl(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->gotgctl, val);
}

u32 dwc_otg_get_gusbcfg(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gusbcfg);
}

void dwc_otg_set_gusbcfg(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->gusbcfg, val);
}

u32 dwc_otg_get_grxfsiz(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->grxfsiz);
}

void dwc_otg_set_grxfsiz(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->grxfsiz, val);
}

u32 dwc_otg_get_gnptxfsiz(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gnptxfsiz);
}

void dwc_otg_set_gnptxfsiz(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->gnptxfsiz, val);
}

u32 dwc_otg_get_gpvndctl(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gpvndctl);
}

void dwc_otg_set_gpvndctl(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->gpvndctl, val);
}

u32 dwc_otg_get_ggpio(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->ggpio);
}

void dwc_otg_set_ggpio(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->ggpio, val);
}

u32 dwc_otg_get_hprt0(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(core_if->host_if->hprt0);

}

void dwc_otg_set_hprt0(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(core_if->host_if->hprt0, val);
}

u32 dwc_otg_get_guid(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->guid);
}

void dwc_otg_set_guid(struct dwc_otg_core_if *core_if, u32 val)
{
	dwc_write_reg32(&core_if->core_global_regs->guid, val);
}

u32 dwc_otg_get_hptxfsiz(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->hptxfsiz);
}
