#
# Makefile for Synopsys DWC OTG Contrller
#
# don't enable high-power workaround
#KBUILD_CPPFLAGS    += -DOTG_EXT_CHG_PUMP


ifeq ($(CONFIG_USB_DWC_OTG_HOST_ONLY),y)
	ccflags-y        += -DDWC_HOST_ONLY
else
ifeq ($(CONFIG_USB_DWC_OTG_DEVICE_ONLY),y)
	ccflags-y        += -DDWC_DEVICE_ONLY
else
	ccflags-y        += -DDWC_OTG_MODE
endif
endif

# Note: By defining DWC_SLAVE, internal DMA mode is disabled.
#       When DWC_SLAVE is not defined, the internal DMA mode is used.

ifeq ($(CONFIG_USB_DWC_OTG_SLAVE),y)
	ccflags-y        += -DDWC_SLAVE
else
#buffer vs descriptor dma set in driver code
ifeq ($(CONFIG_USB_DWC_OTG_EXT_DMA),y)
ifeq ($(CONFIG_4XX_SOC),y)
	ccflags-y += -DDWC_SLAVE -DOTG_PLB_DMA -DOTG_PLB_DMA_TASKLET
	ccflags-y += -DCONFIG_PPC4xx_EDMA
endif
endif
endif

#speed selection set in driver code

ccflags-$(CONFIG_USB_DWC_OTG_ISOCHRONOUS) += -DDWC_EN_ISOC

ccflags-$(CONFIG_USB_DWC_OTG_DEBUG) += -DDEBUG


obj-y		+= dwc_otg.o

dwc_otg-objs	:= dwc_otg_driver.o dwc_otg_attr.o
dwc_otg-objs	+= dwc_otg_cil.o dwc_otg_cil_intr.o
dwc_otg-objs	+= dwc_otg_pcd.o dwc_otg_pcd_intr.o
dwc_otg-objs	+= dwc_otg_hcd.o dwc_otg_hcd_intr.o
dwc_otg-objs	+= dwc_otg_hcd_ddma.o dwc_otg_hcd_queue.o

ifeq ($(CONFIG_4XX_SOC),y)
dwc_otg-objs += ppc4xx_dma.o
endif
