/*
 * usb.c - contains block specific initialisation routines
 *
 * Copyright (C) 2009-2012 Imagination Technologies Ltd.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <asm/global_lock.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/usb.h>

#define COMET_XTAL5	12000000

/*
 * Note: To use USB XTAL5 must be fitted with a 12 or 24MHz XTAL and the Jumpers
 * set appropriately to select this as the clock source for the USB PHY.
 */

static void vbus_valid(int normal)
{
	int lstat;
	u32 temp;

	/*
	 * When this bit is clear, host communication is prevented, allowing the
	 * device to complete power-up and initialisation without the host
	 * seeing errors.
	 */
	__global_lock2(lstat);
	temp = readl(CR_PERIP_USB_PHY_TUNE_CONTROL);
	if (normal)
		temp |= CR_PERIP_USB_VMT_VBUSVALID;
	else
		temp &= ~CR_PERIP_USB_VMT_VBUSVALID;
	writel(temp, CR_PERIP_USB_PHY_TUNE_CONTROL);
	__global_unlock2(lstat);
}

static void comet_usb_init(void)
{
	int lstat;
	u8 xtal_val = 1;
	u32 temp;

	/*enable module clock*/
	__global_lock2(lstat);
	temp = readl(CR_TOP_CLKENAB);
	temp |= (1<<CR_TOP_USB_CLK_1_EN_BIT);
	writel(temp, CR_TOP_CLKENAB);
	__global_unlock2(lstat);

	/* Set up REFCLKS, Need to assert power on reset while we change this */
	if (COMET_XTAL5 == 12000000)
		xtal_val = 0;
	else if (COMET_XTAL5 == 24000000)
		xtal_val = 1;
	else
		printk(KERN_WARNING "Comet USB Init: "
				"Unsupported value for XTAL 5\n");

	/* Delay host comms until we're set up */
	vbus_valid(0);

	__global_lock2(lstat);
	/* Reset USB Block*/
	temp = readl(CR_PERIP_SRST);
	temp |= CR_PERIP_USB_PHY_PON_RESET_BIT;
	temp |= CR_PERIP_USB_PHY_PORTRESET_BIT;
	writel(temp, CR_PERIP_SRST);

	/*Set Clock input to XTAL5 and set frequency */
	temp = readl(CR_PERIP_USB_PHY_STRAP_CONTROL);
	temp &= ~CR_PERIP_USB_REFCLKSEL_BITS;
	temp |= (0 << CR_PERIP_USB_REFCLKSEL_SHIFT);
	temp &= ~CR_PERIP_USB_REFCLKDIV_BITS;
	temp |= xtal_val << CR_PERIP_USB_REFCLKDIV_SHIFT;
	writel(temp, CR_PERIP_USB_PHY_STRAP_CONTROL);

	/* Release Reset of USB BLock*/
	temp = readl(CR_PERIP_SRST);
	temp &= ~CR_PERIP_USB_PHY_PON_RESET_BIT;
	temp &= ~CR_PERIP_USB_PHY_PORTRESET_BIT;
	writel(temp, CR_PERIP_SRST);

	__global_unlock2(lstat);

	/*Now configure the PHY*/

	/* Turn isolation off (done in sample code note sure why ??)*/
	__global_lock2(lstat);
	temp = readl(CR_PERIP_USB_PHY_STRAP_CONTROL);
	temp |= CR_PERIP_USB_ISO_PHY_BIT;
	writel(temp, CR_PERIP_USB_PHY_STRAP_CONTROL);
	__global_unlock2(lstat);

	/*Wait until PHY and UTMI are ready */
#if 0 /*THIS CODE WON'T WORK FOR ES1 DUE TO A HARDWARE PROBLEM*/
	while (1) {
		u32 temp2 = readl(CR_PERIP_USB_PHY_STATUS);
		if ((temp2 & CR_PERIP_USB_RX_PHY_CLK) &&
				(temp2 & CR_PERIP_USB_RX_UTMI_CLK))
			break;
	}
#endif
}

static struct dwc_otg_board comet_usb_board_data = {
	.vbus_valid = vbus_valid,
};

static struct resource comet_usb_resources[] = {
	[0] = {
		.start          = USB_IRQ_NUM,
		/* mapped in comet_usb_setup() */
		.flags          = IORESOURCE_IRQ,
	},
	[1] = {
		.start          = USB_BASE_ADDR,
		.end            = USB_BASE_ADDR + USB_SIZE,
		.flags          = IORESOURCE_MEM,
	},
};


static u64 usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device comet_usb_device = {
	.name = "dwc_otg",
	.id = 0,
	.num_resources = ARRAY_SIZE(comet_usb_resources),
	.resource = comet_usb_resources,
	.dev = {
		.dma_mask      = &usb_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &comet_usb_board_data,
	},
};

#ifdef CONFIG_PM_SLEEP

static int comet_usb_suspend(void)
{
	return 0;
}

static void comet_usb_resume(void)
{
	comet_usb_init();
}
#else
#define comet_usb_suspend NULL
#define comet_usb_resume NULL
#endif	/* CONFIG_PM_SLEEP */

static struct syscore_ops comet_usb_syscore_ops = {
	.suspend = comet_usb_suspend,
	.resume  = comet_usb_resume,
};

int __init comet_usb_setup(const struct dwc_otg_board *board)
{
	int ret;

	/* use board specific vbus callbacks */
	comet_usb_board_data.enable_vbus = board->enable_vbus;
	comet_usb_board_data.disable_vbus = board->disable_vbus;

	/* map irq */
	ret = external_irq_map(comet_usb_resources[0].start);
	if (ret < 0)
		goto out;
	comet_usb_resources[0].start = ret;
	comet_usb_resources[0].end = ret;


	comet_usb_init();

	ret = platform_device_register(&comet_usb_device);
	if (ret)
		goto out;

	register_syscore_ops(&comet_usb_syscore_ops);

out:
	return ret;
}
