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

#if !defined(__DWC_CIL_H__)
#define __DWC_CIL_H__

#include "dwc_otg_driver.h"
#include "dwc_otg_regs.h"
#ifdef DEBUG
#include "linux/timer.h"
#endif

#ifdef OTG_PPC_PLB_DMA
#include "ppc4xx_dma.h"
#include <asm/cacheflush.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/unaligned.h>

#undef OTG_PPC_PLB_DMA_DBG
#define OTG_TX_DMA 0    /* TX DMA direction */
#define OTG_RX_DMA 1    /* RX DMA direction */
#define PLB_DMA_CH DMA_CH0	/* plb dma channel */
#define PLB_DMA_CH_INT 12
#define PLB_DMA_INT_ENA	1
#define PLB_DMA_INT_DIS	0
#define USB_BUFSIZ	512

#ifdef OTG_PPC_PLB_DMA_TASKLET
#ifndef OTG_PPC_PLB_DMA
#define OTG_PPC_PLB_DMA
#endif

extern atomic_t release_later;
#endif
#endif

/**
 * @file
 * This file contains the interface to the Core Interface Layer.
 */

#define OTG_CORE_REV_2_60a	0x4F54260A
#define OTG_CORE_REV_2_71a	0x4F54271A
#define OTG_CORE_REV_2_72a	0x4F54272A
#define OTG_CORE_REV_2_80a	0x4F54280A
#define OTG_CORE_REV_2_81a	0x4F54281A
#define OTG_CORE_REV_2_90a	0x4F54290A
#define OTG_CORE_REV_2_91a	0x4F54290A

#ifdef CONFIG_405EZ
/*
 * Added-sr: 2007-07-26
 *
 * Since the 405EZ (Ultra) only support 2047 bytes as
 * max transfer size, we have to split up bigger transfers
 * into multiple transfers of 1024 bytes sized messages.
 * I happens often, that transfers of 4096 bytes are
 * required (zero-gadget, file_storage-gadget).
 *
 * MAX_XFER_LEN is set to 1024 right now, but could be 2047,
 * since the xfer-size field in the 405EZ USB device controller
 * implementation has 11 bits. Using 1024 seems to work for now.
 */
#define MAX_XFER_LEN	1024
#endif

/**
 * Information for each ISOC packet.
 */
struct iso_pkt_info {
	u32 offset;
	u32 length;
	int status;
};
/**
 * The <code>dwc_ep</code> structure represents the state of a single
 * endpoint when acting in device mode. It contains the data items
 * needed for an endpoint to be activated and transfer packets.
 */
struct dwc_ep {
	/** EP number used for register address lookup */
	u8	 num;
	/** EP direction 0 = OUT */
	unsigned is_in:1;
	/** EP active. */
	unsigned active:1;

	/*
	 * Periodic Tx FIFO # for IN EPs For INTR EP set to 0 to use
	 * non-periodic Tx FIFO If dedicated Tx FIFOs are enabled for all IN
	 * Eps - Tx FIFO # FOR IN EPs
	 */
	unsigned tx_fifo_num:4;
	/** EP type: 0 - Control, 1 - ISOC,	 2 - BULK,	3 - INTR */
	unsigned type:2;
	/** DATA start PID for INTR and BULK EP */
	unsigned data_pid_start:1;
	/** Frame (even/odd) for ISOC EP */
	unsigned even_odd_frame:1;
	/** Max Packet bytes */
	unsigned maxpacket:11;

	/** Max Transfer size */
	u32 maxxfer;
	/** @name Transfer state */

	/**
	 * Pointer to the beginning of the transfer buffer -- do not modify
	 * during transfer.
	 */

	dma_addr_t dma_addr;
	dma_addr_t dma_desc_addr;
	struct dwc_otg_dev_dma_desc *desc_addr;

	u8 *start_xfer_buff;
	/** pointer to the transfer buffer */
	u8 *xfer_buff;
	/** Number of bytes to transfer */
	unsigned xfer_len:19;
	/** Number of bytes transferred. */
	unsigned xfer_count:19;
	/** Sent ZLP */
	unsigned sent_zlp:1;
	/** Total len for control transfer */
	unsigned total_len:19;

	/** stall clear flag */
	unsigned stall_clear_flag:1;

#ifdef CONFIG_405EZ
	/*
	 * Added-sr: 2007-07-26
	 *
	 * Since the 405EZ (Ultra) only support 2047 bytes as
	 * max transfer size, we have to split up bigger transfers
	 * into multiple transfers of 1024 bytes sized messages.
	 * I happens often, that transfers of 4096 bytes are
	 * required (zero-gadget, file_storage-gadget).
	 *
	 * "bytes_pending" will hold the amount of bytes that are
	 * still pending to be send in further messages to complete
	 * the bigger transfer.
	 */
	u32 bytes_pending;
#endif

	/** Allocated DMA Desc count */
	u32 desc_cnt;

};

/*
 * Reasons for halting a host channel.
 */
enum dwc_otg_halt_status {
	DWC_OTG_HC_XFER_NO_HALT_STATUS,
	DWC_OTG_HC_XFER_COMPLETE,
	DWC_OTG_HC_XFER_URB_COMPLETE,
	DWC_OTG_HC_XFER_ACK,
	DWC_OTG_HC_XFER_NAK,
	DWC_OTG_HC_XFER_NYET,
	DWC_OTG_HC_XFER_STALL,
	DWC_OTG_HC_XFER_XACT_ERR,
	DWC_OTG_HC_XFER_FRAME_OVERRUN,
	DWC_OTG_HC_XFER_BABBLE_ERR,
	DWC_OTG_HC_XFER_DATA_TOGGLE_ERR,
	DWC_OTG_HC_XFER_AHB_ERR,
	DWC_OTG_HC_XFER_PERIODIC_INCOMPLETE,
	DWC_OTG_HC_XFER_URB_DEQUEUE
};

/**
 * Host channel descriptor. This structure represents the state of a single
 * host channel when acting in host mode. It contains the data items needed to
 * transfer packets to an endpoint via a host channel.
 */
struct dwc_hc {
	/** Host channel number used for register address lookup */
	u8	 hc_num;

	/** Device to access */
	unsigned dev_addr:7;

	/** EP to access */
	unsigned ep_num:4;

	/** EP direction. 0: OUT, 1: IN */
	unsigned ep_is_in:1;

	/**
	 * EP speed.
	 * One of the following values (from ch9.h):
		USB_SPEED_UNKNOWN = 0,			 enumerating
		USB_SPEED_LOW, USB_SPEED_FULL,		 usb 1.1
		USB_SPEED_HIGH,				 usb 2.0
		USB_SPEED_WIRELESS,			 wireless (usb 2.5)
		USB_SPEED_SUPER,			 usb 3.0
	 */
	unsigned speed:2;


	/**
	 * Endpoint type.
	 * One of the following (from ch9.h):
	 *	- USB_ENDPOINT_XFER_CONTROL: 0
	 *	- USB_ENDPOINT_XFER_ISOC: 1
	 *	- USB_ENDPOINT_XFER_BULK: 2
	 *	- USB_ENDPOINT_XFER_INT: 3
	 */
	unsigned ep_type:2;

	/** Max packet size in bytes */
	unsigned max_packet:11;

	/**
	 * PID for initial transaction.
	 * 0: DATA0,<br>
	 * 1: DATA2,<br>
	 * 2: DATA1,<br>
	 * 3: MDATA (non-Control EP),
	 *	  SETUP (Control EP)
	 */
	unsigned data_pid_start:2;
#define DWC_OTG_HC_PID_DATA0 0
#define DWC_OTG_HC_PID_DATA2 1
#define DWC_OTG_HC_PID_DATA1 2
#define DWC_OTG_HC_PID_MDATA 3
#define DWC_OTG_HC_PID_SETUP 3

	/** Number of periodic transactions per (micro)frame */
	unsigned multi_count:2;

	/** @name Transfer State */
	/** @{ */

	/** Pointer to the current transfer buffer position. */
	u8 *xfer_buff;

	dma_addr_t align_buff;
	/** Total number of bytes to transfer. */
	u32 xfer_len;
	/** Number of bytes transferred so far. */
	u32 xfer_count;
	/** Packet count at start of transfer.*/
	u16 start_pkt_count;

	/**
	 * Flag to indicate whether the transfer has been started. Set to 1 if
	 * it has been started, 0 otherwise.
	 */
	u8 xfer_started;

	/**
	 * Set to 1 to indicate that a PING request should be issued on this
	 * channel. If 0, process normally.
	 */
	u8 do_ping;

	/**
	 * Set to 1 to indicate that the error count for this transaction is
	 * non-zero. Set to 0 if the error count is 0.
	 */
	u8 error_state;

	/**
	 * Set to 1 to indicate that this channel should be halted the next
	 * time a request is queued for the channel. This is necessary in
	 * slave mode if no request queue space is available when an attempt
	 * is made to halt the channel.
	 */
	u8 halt_on_queue;

	/**
	 * Set to 1 if the host channel has been halted, but the core is not
	 * finished flushing queued requests. Otherwise 0.
	 */
	u8 halt_pending;

	/**
	 * Reason for halting the host channel.
	 */
	enum dwc_otg_halt_status halt_status;

	/*
	 * Split settings for the host channel
	 */
	u8 do_split;		   /**< Enable split for the channel */
	u8 complete_split;	   /**< Enable complete split */
	u8 hub_addr;		   /**< Address of high speed hub */

	u8 port_addr;		   /**< Port of the low/full speed device */
	/** Split transaction position
	 * One of the following values:
	 *	  - DWC_HCSPLIT_XACTPOS_MID
	 *	  - DWC_HCSPLIT_XACTPOS_BEGIN
	 *	  - DWC_HCSPLIT_XACTPOS_END
	 *	  - DWC_HCSPLIT_XACTPOS_ALL */
	u8 xact_pos;

	/** Set when the host channel does a short read. */
	u8 short_read;

	/**
	 * Number of requests issued for this channel since it was assigned to
	 * the current transfer (not counting PINGs).
	 */
	u8 requests;

	/**
	 * Queue Head for the transfer being processed by this channel.
	 */
	struct dwc_otg_qh *qh;

	/** @} */

	/** Entry in list of host channels. */
	struct list_head	hc_list_entry;
	/** @name Descriptor DMA support */
	/** @{ */

	/** Number of Transfer Descriptors */
	u16 ntd;

	/** Descriptor List DMA address */
	dma_addr_t desc_list_addr;

	/** Scheduling micro-frame bitmap. */
	u8 schinfo;

	/** @} */
};

/**
 * The following parameters may be specified when starting the module. These
 * parameters define how the DWC_otg controller should be configured.
 * Parameter values are passed to the CIL initialisation function
 * dwc_otg_cil_init.
 */
struct dwc_otg_core_params {
	int opt;
#define dwc_param_opt_default 1

	/**
	 * Specifies the OTG capabilities. The driver will automatically
	 * detect the value for this parameter if none is specified.
	 * 0 - HNP and SRP capable (default)
	 * 1 - SRP Only capable
	 * 2 - No HNP/SRP capable
	 */
	int otg_cap;
#define DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE 0
#define DWC_OTG_CAP_PARAM_SRP_ONLY_CAPABLE 1
#define DWC_OTG_CAP_PARAM_NO_HNP_SRP_CAPABLE 2
#define dwc_param_otg_cap_default DWC_OTG_CAP_PARAM_HNP_SRP_CAPABLE

	/**
	 * Specifies whether to use slave or DMA mode for accessing the data
	 * FIFOs. The driver will automatically detect the value for this
	 * parameter if none is specified.
	 * 0 - Slave
	 * 1 - DMA (default, if available)
	 */
	int dma_enable;
#define dwc_param_dma_enable_default 1
	/**
	 * When DMA mode is enabled specifies whether to use address DMA or
	 * DMA Descritor mode for accessing the data
	 * FIFOs in device mode. The driver will automatically
	 * detect the value for this
	 * parameter if none is specified.
	 * 0 - address DMA
	 * 1 - DMA Descriptor(default, if available)
	 */
	int dma_desc_enable;
	/** The DMA Burst size (applicable only for External DMA
	 * Mode). 1, 4, 8 16, 32, 64, 128, 256 (default 32)
	 */
	int dma_burst_size;	 /* Translate this to GAHBCFG values */
#define dwc_param_dma_burst_size_default 32

	/**
	 * Specifies the maximum speed of operation in host and device mode.
	 * The actual speed depends on the speed of the attached device and
	 * the value of phy_type. The actual speed depends on the speed of the
	 * attached device.
	 * 0 - High Speed (default)
	 * 1 - Full Speed
	 */
	int speed;
#define dwc_param_speed_default 0
#define DWC_SPEED_PARAM_HIGH 0
#define DWC_SPEED_PARAM_FULL 1

	/** Specifies whether low power mode is supported when attached
	 *	to a Full Speed or Low Speed device in host mode.
	 * 0 - Don't support low power mode (default)
	 * 1 - Support low power mode
	 */
	int host_support_fs_ls_low_power;
#define dwc_param_host_support_fs_ls_low_power_default 0

	/** Specifies the PHY clock rate in low power mode when connected to a
	 * Low Speed device in host mode. This parameter is applicable only if
	 * HOST_SUPPORT_FS_LS_LOW_POWER is enabled. If PHY_TYPE is set to FS
	 * then defaults to 6 MHZ otherwise 48 MHZ.
	 *
	 * 0 - 48 MHz
	 * 1 - 6 MHz
	 */
	int host_ls_low_power_phy_clk;
#define dwc_param_host_ls_low_power_phy_clk_default 0
#define DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ 0
#define DWC_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ 1

	/**
	 * 0 - Use cC FIFO size parameters
	 * 1 - Allow dynamic FIFO sizing (default)
	 */
	int enable_dynamic_fifo;
#define dwc_param_enable_dynamic_fifo_default 1

	/** Total number of 4-byte words in the data FIFO memory. This
	 * memory includes the Rx FIFO, non-periodic Tx FIFO, and periodic
	 * Tx FIFOs.
	 * 32 to 32768 (default 8192)
	 * Note: The total FIFO memory depth in the FPGA configuration is 8192.
	 */
	int data_fifo_size;
#define dwc_param_data_fifo_size_default 8192

	/** Number of 4-byte words in the Rx FIFO in device mode when dynamic
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1064)
	 */
	int dev_rx_fifo_size;
#define dwc_param_dev_rx_fifo_size_default 1064

	/** Number of 4-byte words in the non-periodic Tx FIFO in device mode
	 * when dynamic FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int dev_nperio_tx_fifo_size;
#define dwc_param_dev_nperio_tx_fifo_size_default 1024

	/** Number of 4-byte words in each of the periodic Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.
	 * 4 to 768 (default 256)
	 */
	u32 dev_perio_tx_fifo_size[MAX_PERIO_FIFOS];
#define dwc_param_dev_perio_tx_fifo_size_default 256

	/** Number of 4-byte words in the Rx FIFO in host mode when dynamic
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int host_rx_fifo_size;
#define dwc_param_host_rx_fifo_size_default 1024

	/** Number of 4-byte words in the non-periodic Tx FIFO in host mode
	 * when Dynamic FIFO sizing is enabled in the core.
	 * 16 to 32768 (default 1024)
	 */
	int host_nperio_tx_fifo_size;
#define dwc_param_host_nperio_tx_fifo_size_default 1024

	/** Number of 4-byte words in the host periodic Tx FIFO when dynamic
	 * FIFO sizing is enabled.
	 * 16 to 32768 (default 1024)
	 */
	int host_perio_tx_fifo_size;
#define dwc_param_host_perio_tx_fifo_size_default 1024

	/** The maximum transfer size supported in bytes.
	 * 2047 to 65,535  (default 65,535)
	 */
	int max_transfer_size;
#define dwc_param_max_transfer_size_default 65535

	/** The maximum number of packets in a transfer.
	 * 15 to 511  (default 511)
	 */
	int max_packet_count;
#define dwc_param_max_packet_count_default 511

	/** The number of host channel registers to use.
	 * 1 to 16 (default 12)
	 * Note: The FPGA configuration supports a maximum of 12 host channels.
	 */
	int host_channels;
#define dwc_param_host_channels_default 12

	/** The number of endpoints in addition to EP0 available for device
	 * mode operations.
	 * 1 to 15 (default 6 IN and OUT)
	 * Note: The FPGA configuration supports a maximum of 6 IN and OUT
	 * endpoints in addition to EP0.
	 */
	int dev_endpoints;
#define dwc_param_dev_endpoints_default 6

		/**
		 * Specifies the type of PHY interface to use. By default,
		 * the driver will automatically detect the phy_type.
		 *
		 * 0 - Full Speed PHY
		 * 1 - UTMI+ (default)
		 * 2 - ULPI
		 */
	int phy_type;
#define DWC_PHY_TYPE_PARAM_FS 0
#define DWC_PHY_TYPE_PARAM_UTMI 1
#define DWC_PHY_TYPE_PARAM_ULPI 2
#define dwc_param_phy_type_default DWC_PHY_TYPE_PARAM_UTMI

	/**
	 * Specifies the UTMI+ Data Width.	This parameter is
	 * applicable for a PHY_TYPE of UTMI+ or ULPI. (For a ULPI
	 * PHY_TYPE, this parameter indicates the data width between
	 * the MAC and the ULPI Wrapper.) Also, this parameter is
	 * applicable only if the OTG_HSPHY_WIDTH cC parameter was set
	 * to "8 and 16 bits", meaning that the core has been
	 * configured to work at either data path width.
	 *
	 * 8 or 16 bits (default 16)
	 */
	int phy_utmi_width;
#define dwc_param_phy_utmi_width_default 16

	/**
	 * Specifies whether the ULPI operates at double or single
	 * data rate. This parameter is only applicable if PHY_TYPE is
	 * ULPI.
	 *
	 * 0 - single data rate ULPI interface with 8 bit wide data
	 * bus (default)
	 * 1 - double data rate ULPI interface with 4 bit wide data
	 * bus
	 */
	int phy_ulpi_ddr;
#define dwc_param_phy_ulpi_ddr_default 0

	/**
	 * Specifies whether to use the internal or external supply to
	 * drive the vbus with a ULPI phy.
	 */
	int phy_ulpi_ext_vbus;
#define DWC_PHY_ULPI_INTERNAL_VBUS 0
#define DWC_PHY_ULPI_EXTERNAL_VBUS 1
#define dwc_param_phy_ulpi_ext_vbus_default DWC_PHY_ULPI_INTERNAL_VBUS

	/**
	 * Specifies whether to use the I2Cinterface for full speed PHY. This
	 * parameter is only applicable if PHY_TYPE is FS.
	 * 0 - No (default)
	 * 1 - Yes
	 */
	int i2c_enable;
#define dwc_param_i2c_enable_default 0

	int ulpi_fs_ls;
#define dwc_param_ulpi_fs_ls_default 0

	int ts_dline;
#define dwc_param_ts_dline_default 0

	/**
	 * Specifies whether dedicated transmit FIFOs are
	 * enabled for non periodic IN endpoints in device mode
	 * 0 - No
	 * 1 - Yes
	 */
	 int en_multiple_tx_fifo;
#define dwc_param_en_multiple_tx_fifo_default 1

	/** Number of 4-byte words in each of the Tx FIFOs in device
	 * mode when dynamic FIFO sizing is enabled.
	 * 4 to 768 (default 256)
	 */
	u32 dev_tx_fifo_size[MAX_TX_FIFOS];
#define dwc_param_dev_tx_fifo_size_default 256

	/** Thresholding enable flag-
	 * bit 0 - enable non-ISO Tx thresholding
	 * bit 1 - enable ISO Tx thresholding
	 * bit 2 - enable Rx thresholding
	 */
	u32 thr_ctl;
#define dwc_param_thr_ctl_default 0

	/** Thresholding length for Tx
	 *	FIFOs in 32 bit DWORDs
	 */
	u32 tx_thr_length;
#define dwc_param_tx_thr_length_default 64

	/** Thresholding length for Rx
	 *	FIFOs in 32 bit DWORDs
	 */
	u32 rx_thr_length;
#define dwc_param_rx_thr_length_default 64
	/**
	 * Specifies whether LPM (Link Power Management) support is enabled
	 */
	int lpm_enable;

	/** Per Transfer Interrupt
	 *	mode enable flag
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int pti_enable;

	/** Multi Processor Interrupt
	 *	mode enable flag
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int mpi_enable;

	/** IS_USB Capability
	 * 1 - Enabled
	 * 0 - Disabled
	 */
	int ic_usb_cap;

	/** AHB Threshold Ratio
	 * 2'b00 AHB Threshold = 	MAC Threshold
	 * 2'b01 AHB Threshold = 1/2 	MAC Threshold
	 * 2'b10 AHB Threshold = 1/4	MAC Threshold
	 * 2'b11 AHB Threshold = 1/8	MAC Threshold
	 */
	int ahb_thr_ratio;
};

#ifdef DEBUG
struct dwc_otg_core_if;
struct hc_xfer_info {
	struct dwc_otg_core_if	*core_if;
	struct dwc_hc		*hc;
};
#endif
/*
 * Device States
 */
enum dwc_otg_lx_state {
	/** On state */
	DWC_OTG_L0,
	/** LPM sleep state*/
	DWC_OTG_L1,
	/** USB suspend state*/
	DWC_OTG_L2,
	/** Off state*/
	DWC_OTG_L3
};

#ifdef OTG_PPC_PLB_DMA_TASKLET
struct dma_xfer_s {
	u32	*dma_data_buff;
	void	*dma_data_fifo;
	u32	dma_count;
	u32	dma_dir;
};
#endif

/**
 * The <code>dwc_otg_core_if</code> structure contains information needed to
 * manage the DWC_otg controller acting in either host or device mode. It
 * represents the programming view of the controller as a whole.
 */
struct dwc_otg_core_if {
	/** Parameters that define how the core should be configured.*/
	struct dwc_otg_core_params	   *core_params;

	/** Core Global registers starting at offset 000h. */
	struct dwc_otg_core_global_regs __iomem *core_global_regs;

	/** Device-specific information */
	struct dwc_otg_dev_if		   *dev_if;
	/** Host-specific information */
	struct dwc_otg_host_if		   *host_if;
	/** Value from SNPSID register */
	u32 snpsid;

	/*
	 * Set to 1 if the core PHY interface bits in USBCFG have been
	 * initialized.
	 */
	u8 phy_init_done;

	/*
	 * SRP Success flag, set by srp success interrupt in FS I2C mode
	 */
	u8 srp_success;
	u8 srp_timer_started;

	/* Common configuration information */
	/** Power and Clock Gating Control Register */
	u32 __iomem *pcgcctl;
#define DWC_OTG_PCGCCTL_OFFSET 0xE00

	/** Push/pop addresses for endpoints or host channels.*/
	u32 __iomem *data_fifo[MAX_EPS_CHANNELS];
#define DWC_OTG_DATA_FIFO_OFFSET 0x1000
#define DWC_OTG_DATA_FIFO_SIZE 0x1000

	/** Total RAM for FIFOs (Bytes) */
	u16 total_fifo_size;
	/** Size of Rx FIFO (Bytes) */
	u16 rx_fifo_size;
	/** Size of Non-periodic Tx FIFO (Bytes) */
	u16 nperio_tx_fifo_size;


	/** 1 if DMA is enabled, 0 otherwise. */
	u8 dma_enable;
	/** 1 if DMA descriptor is enabled, 0 otherwise. */
	u8 dma_desc_enable;

	/** 1 if PTI Enhancement mode is enabled, 0 otherwise. */
	u8 pti_enh_enable;

	/** 1 if MPI Enhancement mode is enabled, 0 otherwise. */
	u8 multiproc_int_enable;

	/** 1 if dedicated Tx FIFOs are enabled, 0 otherwise. */
	u8 en_multiple_tx_fifo;

	/** Set to 1 if multiple packets of a high-bandwidth transfer is in
	 * process of being queued */
	u8 queuing_high_bandwidth;

	/** Hardware Configuration -- stored here for convenience.*/
	union hwcfg1_data hwcfg1;
	union hwcfg2_data hwcfg2;
	union hwcfg3_data hwcfg3;
	union hwcfg4_data hwcfg4;

	/** Host and Device Configuration -- stored here for convenience.*/
	union hcfg_data hcfg;
	union dcfg_data dcfg;

	/** The operational State, during transations
	 * (a_host>>a_peripherial and b_device=>b_host) this may not
	 * match the core but allows the software to determine
	 * transitions.
	 */
	u8 op_state;

	/**
	 * Set to 1 if the HCD needs to be restarted on a session request
	 * interrupt. This is required if no connector ID status change has
	 * occurred since the HCD was last disconnected.
	 */
	u8 restart_hcd_on_session_req;

	/** HCD callbacks */
	/** A-Device is a_host */
#define A_HOST		(1)
	/** A-Device is a_suspend */
#define A_SUSPEND	(2)
	/** A-Device is a_peripherial */
#define A_PERIPHERAL	(3)
	/** B-Device is operating as a Peripheral. */
#define B_PERIPHERAL	(4)
	/** B-Device is operating as a Host. */
#define B_HOST		(5)

	/** HCD callbacks */
	struct dwc_otg_cil_callbacks *hcd_cb;
	/** PCD callbacks */
	struct dwc_otg_cil_callbacks *pcd_cb;

	/** Device mode Periodic Tx FIFO Mask */
	u32 p_tx_msk;
	/** Device mode Periodic Tx FIFO Mask */
	u32 tx_msk;

	/** Workqueue object used for handling several interrupts */
	struct workqueue_struct *wq_otg;

	/** Timer object used for handling "Wakeup Detected" Interrupt */
	struct timer_list *wkp_timer;
#ifdef DEBUG
	u32			start_hcchar_val[MAX_EPS_CHANNELS];

	struct hc_xfer_info	hc_xfer_info[MAX_EPS_CHANNELS];
	struct timer_list	hc_xfer_timer[MAX_EPS_CHANNELS];

	u32			hfnum_7_samples;
	u64			hfnum_7_frrem_accum;
	u32			hfnum_0_samples;
	u64			hfnum_0_frrem_accum;
	u32			hfnum_other_samples;
	u64			hfnum_other_frrem_accum;
#endif
	resource_size_t phys_addr;
	/* Added to support PLB DMA : phys-virt mapping */

#ifdef OTG_PPC_PLB_DMA_TASKLET
	/* Tasklet to do plbdma */
	struct tasklet_struct   *plbdma_tasklet;
#if 1
	dma_xfer_t		dma_xfer;
#else
	u32 			*dma_data_buff;
	void 			*dma_data_fifo;
	u32 			dma_count;
	u32 			dma_dir;
#endif
#endif
	/** Lx state of device */
	enum dwc_otg_lx_state lx_state;

	struct dwc_otg_device *otg_dev;
};


/*
 * The following functions are functions for works
 * using during handling some interrupts
 */
extern void w_wakeup_detected(unsigned long);

/* The following functions support initialization of the CIL driver component
 * and the DWC_otg controller.
 */
extern struct dwc_otg_core_if *dwc_otg_cil_init(
		u32 __iomem *reg_base_addr,
		struct dwc_otg_core_params *_core_params,
		struct dwc_otg_device *dwc_otg_device);
extern void dwc_otg_cil_remove(struct dwc_otg_core_if *core_if);
extern void dwc_otg_core_init(struct dwc_otg_core_if *core_if);
extern void dwc_otg_core_host_init(struct dwc_otg_core_if *core_if);
extern void dwc_otg_core_dev_init(struct dwc_otg_core_if *core_if);
extern void dwc_otg_enable_global_interrupts(struct dwc_otg_core_if *core_if);
extern void dwc_otg_disable_global_interrupts(struct dwc_otg_core_if *core_if);

/* Debug functions */

#if defined(DEBUG) && defined(VERBOSE)
extern void dwc_otg_dump_msg(const u8 *buf, unsigned int length);
#else
static inline void dwc_otg_dump_msg(const u8 *buf, unsigned int length)
{
}
#endif

/** @name Device CIL Functions
 * The following functions support managing the DWC_otg controller in device
 * mode.
 */
/**@{*/
extern void
dwc_otg_wakeup(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_read_setup_packet(struct dwc_otg_core_if *core_if, u32 *dest);

extern u32
dwc_otg_get_frame_number(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_ep0_activate(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_ep_activate(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_ep_deactivate(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_ep_start_transfer(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_ep_start_zl_transfer(struct dwc_otg_core_if *core_if,
					struct dwc_ep *ep);

extern void
dwc_otg_ep0_start_transfer(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_ep0_continue_transfer(struct dwc_otg_core_if *core_if,
					struct dwc_ep *ep);

extern void
dwc_otg_ep_write_packet(struct dwc_otg_core_if *core_if, struct dwc_ep *ep,
					int dma);

extern void
dwc_otg_ep_set_stall(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_ep_clear_stall(struct dwc_otg_core_if *core_if, struct dwc_ep *ep);

extern void
dwc_otg_enable_device_interrupts(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_dump_dev_registers(struct dwc_otg_core_if *core_if);

/**@}*/

/** @name Host CIL Functions
 * The following functions support managing the DWC_otg controller in host
 * mode.
 */
/**@{*/
extern void
dwc_otg_hc_init(struct dwc_otg_core_if *core_if, struct dwc_hc *hc);

extern void
dwc_otg_hc_halt(struct dwc_otg_core_if *core_if, struct dwc_hc *hc,
			enum dwc_otg_halt_status _halt_status);

extern void
dwc_otg_hc_cleanup(struct dwc_otg_core_if *core_if, struct dwc_hc *hc);

extern void
dwc_otg_hc_start_transfer(struct dwc_otg_core_if *core_if, struct dwc_hc *hc);

extern int
dwc_otg_hc_continue_transfer(struct dwc_otg_core_if *core_if,
				struct dwc_hc *hc);

extern void
dwc_otg_hc_do_ping(struct dwc_otg_core_if *core_if, struct dwc_hc *hc);

extern void
dwc_otg_hc_write_packet(struct dwc_otg_core_if *core_if, struct dwc_hc *hc);

extern void
dwc_otg_enable_host_interrupts(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_disable_host_interrupts(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_hc_start_transfer_ddma(struct dwc_otg_core_if *core_if,
					struct dwc_hc *hc);

/* Macro used to clear one channel interrupt */
#define clear_hc_int(_hc_regs_, _intr_) \
do { \
	union hcint_data hcint_clear = {.d32 = 0}; \
	hcint_clear.b._intr_ = 1; \
	dwc_write_reg32(&(_hc_regs_)->hcint, hcint_clear.d32); \
} while (0)

/*
 * Macro used to disable one channel interrupt. Channel interrupts are
 * disabled when the channel is halted or released by the interrupt handler.
 * There is no need to handle further interrupts of that type until the
 * channel is re-assigned. In fact, subsequent handling may cause crashes
 * because the channel structures are cleaned up when the channel is released.
 */
#define disable_hc_int(_hc_regs_, _intr_) \
do { \
	union hcintmsk_data hcintmsk = {.d32 = 0}; \
	hcintmsk.b._intr_ = 1; \
	dwc_modify_reg32(&(_hc_regs_)->hcintmsk, hcintmsk.d32, 0); \
} while (0)
/**
 * This function Reads HPRT0 in preparation to modify.	It keeps the
 * WC bits 0 so that if they are read as 1, they won't clear when you
 * write it back
 */
static inline u32 dwc_otg_read_hprt0(struct dwc_otg_core_if *core_if)
{
	union hprt0_data hprt0;
	hprt0.d32 = dwc_read_reg32(core_if->host_if->hprt0);
	hprt0.b.prtena = 0;
	hprt0.b.prtconndet = 0;
	hprt0.b.prtenchng = 0;
	hprt0.b.prtovrcurrchng = 0;
	return hprt0.d32;
}

extern void dwc_otg_dump_host_registers(struct dwc_otg_core_if *core_if);
/**@}*/

/** @name Common CIL Functions
 * The following functions support managing the DWC_otg controller in either
 * device or host mode.
 */
/**@{*/

extern void
dwc_otg_read_packet(struct dwc_otg_core_if *core_if,
				u8 *dest,
				u16 bytes);

extern void
dwc_otg_dump_global_registers(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_flush_tx_fifo(struct dwc_otg_core_if *core_if, const int _num);

extern void
dwc_otg_flush_rx_fifo(struct dwc_otg_core_if *core_if);

extern void
dwc_otg_core_reset(struct dwc_otg_core_if *core_if);

#ifdef OTG_PPC_PLB_DMA
extern void
ppc4xx_start_plb_dma(struct dwc_otg_core_if *core_if, void *src, void *dst,
			unsigned int length, unsigned int use_interrupt,
			unsigned int dma_ch, unsigned int dma_dir);
#endif
#define NP_TXFIFO_EMPTY -1
#define MAX_NP_TXREQUEST_Q_SLOTS 8
/**
 * This function returns the endpoint number of the request at
 * the top of non-periodic TX FIFO, or -1 if the request FIFO is
 * empty.
 */
static inline int dwc_otg_top_nptxfifo_epnum(struct dwc_otg_core_if *core_if)
{
	union gnptxsts_data txstatus = {.d32 = 0};

	txstatus.d32 = dwc_read_reg32(&core_if->core_global_regs->gnptxsts);
	return (txstatus.b.nptxqspcavail == MAX_NP_TXREQUEST_Q_SLOTS ?
		-1 : txstatus.b.nptxqtop_chnep);
}

/**
 * This function returns the Core Interrupt register.
 */
static inline u32 dwc_otg_read_core_intr(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gintsts) &
		dwc_read_reg32(&core_if->core_global_regs->gintmsk);
}

/**
 * This function returns the OTG Interrupt register.
 */
static inline u32 dwc_otg_read_otg_intr(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gotgint);
}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the IN endpoint interrupt bits.
 */
static inline u32
dwc_otg_read_dev_all_in_ep_intr(struct dwc_otg_core_if *core_if)
{
	u32 v;
	if (core_if->multiproc_int_enable) {
		v = dwc_read_reg32(&core_if->dev_if->dev_global_regs->
				   deachint) & dwc_read_reg32(&core_if->dev_if->
							      dev_global_regs->
							      deachintmsk);
	} else {
		v = dwc_read_reg32(&core_if->dev_if->dev_global_regs->daint) &
		    dwc_read_reg32(&core_if->dev_if->dev_global_regs->daintmsk);
	}
	return v & 0xffff;

}

/**
 * This function reads the Device All Endpoints Interrupt register and
 * returns the OUT endpoint interrupt bits.
 */
static inline u32
dwc_otg_read_dev_all_out_ep_intr(struct dwc_otg_core_if *core_if)
{
	u32 v;
	if (core_if->multiproc_int_enable) {
		v = dwc_read_reg32(&core_if->dev_if->dev_global_regs->
				   deachint) & dwc_read_reg32(&core_if->dev_if->
							      dev_global_regs->
							      deachintmsk);
	} else {
		v = dwc_read_reg32(&core_if->dev_if->dev_global_regs->daint) &
		    dwc_read_reg32(&core_if->dev_if->dev_global_regs->daintmsk);
	}
	return (v & 0xffff0000) >> 16;
}

/**
 * This function returns the Device IN EP Interrupt register
 */
static inline
u32 dwc_otg_read_dev_in_ep_intr(struct dwc_otg_core_if *core_if,
					struct dwc_ep *ep)
{
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	u32 v, msk, emp;

	if (core_if->multiproc_int_enable) {
		msk =
		    dwc_read_reg32(&dev_if->dev_global_regs->
				   diepeachintmsk[ep->num]);
		emp =
		    dwc_read_reg32(&dev_if->dev_global_regs->
				   dtknqr4_fifoemptymsk);
		msk |= ((emp >> ep->num) & 0x1) << 7;
		v = dwc_read_reg32(&dev_if->in_ep_regs[ep->num]->diepint) & msk;
	} else {
		msk = dwc_read_reg32(&dev_if->dev_global_regs->diepmsk);
		emp =
		    dwc_read_reg32(&dev_if->dev_global_regs->
				   dtknqr4_fifoemptymsk);
		msk |= ((emp >> ep->num) & 0x1) << 7;
		v = dwc_read_reg32(&dev_if->in_ep_regs[ep->num]->diepint) & msk;
	}
/*
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	u32 v;
	v = dwc_read_reg32(&dev_if->in_ep_regs[ep->num]->diepint) &
			dwc_read_reg32(&dev_if->dev_global_regs->diepmsk);
*/
	return v;
}
/**
 * This function returns the Device OUT EP Interrupt register
 */
static inline u32
dwc_otg_read_dev_out_ep_intr(struct dwc_otg_core_if *core_if, struct dwc_ep *ep)
{
	struct dwc_otg_dev_if *dev_if = core_if->dev_if;
	u32 v;
	union doepint_data msk = {.d32 = 0 };

	if (core_if->multiproc_int_enable) {
		msk.d32 =
		    dwc_read_reg32(&dev_if->dev_global_regs->
				   doepeachintmsk[ep->num]);
		if (core_if->pti_enh_enable)
			msk.b.pktdrpsts = 1;
		v = dwc_read_reg32(&dev_if->out_ep_regs[ep->num]->
				   doepint) & msk.d32;
	} else {
		msk.d32 = dwc_read_reg32(&dev_if->dev_global_regs->doepmsk);
		if (core_if->pti_enh_enable)
			msk.b.pktdrpsts = 1;
		v = dwc_read_reg32(&dev_if->out_ep_regs[ep->num]->
				   doepint) & msk.d32;
	}
	return v;
}

/**
 * This function returns the Host All Channel Interrupt register
 */
static inline u32
dwc_otg_read_host_all_channels_intr(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->host_if->host_global_regs->haint);
}

static inline u32
dwc_otg_read_host_channel_intr(struct dwc_otg_core_if *core_if,
		struct dwc_hc *hc)
{
	return dwc_read_reg32(&core_if->host_if->hc_regs[hc->hc_num]->hcint);
}


/**
 * This function returns the mode of the operation, host or device.
 *
 * @return 0 - Device Mode, 1 - Host Mode
 */
static inline u32 dwc_otg_mode(struct dwc_otg_core_if *core_if)
{
	return dwc_read_reg32(&core_if->core_global_regs->gintsts) & 0x1;
}

static inline u8 dwc_otg_is_device_mode(struct dwc_otg_core_if *core_if)
{
	return dwc_otg_mode(core_if) != DWC_HOST_MODE;
}
static inline u8 dwc_otg_is_host_mode(struct dwc_otg_core_if *core_if)
{
	return dwc_otg_mode(core_if) == DWC_HOST_MODE;
}

extern int dwc_otg_handle_common_intr(struct dwc_otg_core_if *core_if);


/**@}*/

/**
 * DWC_otg CIL callback structure.	This structure allows the HCD and
 * PCD to register functions used for starting and stopping the PCD
 * and HCD for role change on for a DRD.
 */
struct dwc_otg_cil_callbacks {
	/** Start function for role change */
	int (*start) (void *_p);
	/** Stop Function for role change */
	int (*stop) (void *_p);
	/** Disconnect Function for role change */
	int (*disconnect) (void *_p);
	/** Resume/Remote wakeup Function */
	int (*resume_wakeup) (void *_p);
	/** Suspend function */
	int (*suspend) (void *_p);
	/** Session Start (SRP) */
	int (*session_start) (void *_p);
#ifdef CONFIG_USB_DWC_OTG_LPM
	/** Sleep (switch to L0 state) */
	int (*sleep) (void *_p);
#endif
	/** Pointer passed to start() and stop() */
	void *p;
};

extern void
dwc_otg_cil_register_pcd_callbacks(struct dwc_otg_core_if *core_if,
					struct dwc_otg_cil_callbacks *_cb,
					void *_p);
extern void
dwc_otg_cil_register_hcd_callbacks(struct dwc_otg_core_if *core_if,
					struct dwc_otg_cil_callbacks *_cb,
					void *_p);
#endif
