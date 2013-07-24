#
# Makefile for TZ1090 platform setup.
#

obj-y	+= afe.o
obj-y	+= clock.o
obj-y	+= irq.o
obj-y	+= pdp.o
obj-y	+= pm.o
obj-y	+= sdhost.o
obj-y	+= setup.o
obj-y	+= usb.o

obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_METAG_COREMEM)	+= coremem.o
obj-$(CONFIG_METAG_SUSPEND_MEM)	+= bootprot.o
obj-$(CONFIG_METAG_USER_TCM)	+= tcm.o
obj-$(CONFIG_NUMA)		+= numa.o

ifneq ($(CONFIG_MMC_DW_IDMAC),y)
obj-y	+= sdhost-dma.o
endif

obj-$(CONFIG_SOC_COMET_AUDIOCODEC)	+= audiocodec.o

