/*
 * EHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 2007 Imagination Technologies Ltd.
 *
 * Bus Glue for Chorus2 On-Chip EHCI driver
 *
 * Based on "ehci-au1xxx.c" by K.Boge <karsten.boge@amd.com>
 *
 * This file is licenced under the GPL.
 */

#include <linux/platform_device.h>

#define USBMODE_REG             0x200c1a8

/* called during probe() after chip reset completes */
static int ehci_chorus2_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	int		retval;

	/* ehci_halt seems to always fail for this HCD. */
	writel(0, &ehci->regs->command);

	retval = ehci_init(hcd);
	if (retval)
		return retval;

	ehci->sbrn = 0x20;
	retval = ehci_reset(ehci);
	if (retval)
		return retval;

	writel(3, USBMODE_REG);

	return 0;
}

/**
 * usb_ehci_chorus2_probe() - initialize Chorus2 SoC-based HCDs
 * @driver:	Host controller driver structure
 * @dev:	Platform device
 *
 * Allocates basic resources for this USB host controller, and then invokes the
 * start() method for the HCD associated with it through the hotplug entry's
 * driver_data.
 */
static int usb_ehci_chorus2_probe(const struct hc_driver *driver,
				  struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;

	if (dev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		retval = -ENOMEM;
	}
	hcd = usb_create_hcd(driver, &dev->dev, "Chorus2 EHCI");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = dev->resource[0].start;
	hcd->rsrc_len = dev->resource[0].end - dev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_debug("request_mem_region failed");
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err2;
	}

	hcd->has_tt = 1;

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs +
		HC_LENGTH(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase));

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	retval = usb_add_hcd(hcd, dev->resource[1].start, 0);
	if (retval == 0)
		return retval;

	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_ehci_hcd_chorus2_remove() - shutdown processing for Chorus2 based HCDs
 * @hcd:	Host controller driver
 * @dev:	Platform device being being removed
 *
 * Reverses the effect of @usb_ehci_chorus2_probe, first invoking the HCD's
 * stop() method. It is always called from a thread context, normally "rmmod",
 * "apmd", or something similar.
 */
static void usb_ehci_chorus2_remove(struct usb_hcd *hcd,
				    struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

static const struct hc_driver ehci_chorus2_hc_driver = {
	.description = hcd_name,
	.product_desc = "Chorus2 EHCI",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_chorus2_setup,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,

	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static int ehci_hcd_chorus2_drv_probe(struct platform_device *pdev)
{
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = usb_ehci_chorus2_probe(&ehci_chorus2_hc_driver, pdev);
	return ret;
}

static int ehci_hcd_chorus2_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_ehci_chorus2_remove(hcd, pdev);
	return 0;
}

MODULE_ALIAS("chorus2-ehci");
static struct platform_driver ehci_chorus2_driver = {
	.probe = ehci_hcd_chorus2_drv_probe,
	.remove = ehci_hcd_chorus2_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		.name = "chorus2-ehci",
		.bus = &platform_bus_type
	}
};
