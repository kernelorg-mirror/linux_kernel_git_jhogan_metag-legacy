/*
 * Comet DMA functions for Synopsys SDHost driver
 *
 * Copyright (C) 2010 Imagination Technologies
 */
#include <linux/mmc/host.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/img_mdc_dma.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/sdhost.h>

#define dw_mci_set_pending(host, event)				\
	set_bit(event, &host->pending_events)

struct dw_mci_dma_data {
	struct dma_chan	*txchan;
	struct dma_chan *rxchan;
	struct dma_async_tx_descriptor *desc;
};

struct dma_pdata {
	unsigned int tx_dma;
	unsigned int rx_dma;
};

static void dw_mmc_dma_cleanup(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	if (data)
		dma_unmap_sg(host->dev, data->sg, data->sg_len,
		     ((data->flags & MMC_DATA_WRITE)
		      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

static void dw_mmc_dma_complete(void *arg)
{
	struct dw_mci	*host = arg;
	struct mmc_data	*data = host->data;

	host->dma_ops->cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point trying
	 * to send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
		dw_mci_set_pending(host, EVENT_XFER_COMPLETE);
		tasklet_schedule(&host->tasklet);
	}
}

/* Returns 0 on success, error code otherwise */
static int dw_mmc_dma_init(struct dw_mci *host)
{
	struct dw_mci_dma_data *dma_data;
	struct mdc_dma_cookie *cookie;
	dma_cap_mask_t mask;
	int ret = 0;

	/* If we are resuming, don't allocate new resources */
	if (host->dma_data)
		return 0;

	dma_data = kzalloc(sizeof(struct dw_mci_dma_data), GFP_KERNEL);
	if (!dma_data) {
		ret = -ENOMEM;
		goto out;
	}

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie) {
		ret = -ENOMEM;
		goto free_data;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Set peripheral number to dma cookie and reset channel */
	cookie->periph = host->pdata->data->tx_dma;
	cookie->req_channel = -1;
	dma_data->txchan = dma_request_channel(mask, &mdc_dma_filter_fn,
					       cookie);
	if (!dma_data->txchan) {
		dev_err(host->dev,
			"%s: could not find suitable tx DMA channel.\n",
			__func__);
		ret = -ENXIO;
		goto free_cookie;
	}

	/* Set peripheral number to dma cookie and reset channel */
	cookie->periph = host->pdata->data->rx_dma;
	cookie->req_channel = -1;

	dma_data->rxchan = dma_request_channel(mask, &mdc_dma_filter_fn,
					       cookie);
	if (!dma_data->rxchan) {
		dev_err(host->dev,
			"%s: could not find suitable rx DMA channel.\n",
			__func__);
		ret = -ENXIO;
		dma_release_channel(dma_data->txchan);
		goto free_cookie;
	}

	host->dma_data = dma_data;

	kfree(cookie);

	return 0;

free_cookie:
	kfree(cookie);
free_data:
	kfree(dma_data);
out:
	return ret;
}

static void dw_mmc_dma_exit(struct dw_mci *host)
{
	dma_release_channel(host->dma_data->txchan);
	dma_release_channel(host->dma_data->rxchan);
	kfree(host->dma_data);
}

static void dw_mmc_dma_start(struct dw_mci *host, unsigned int sg_len)
{
	struct mmc_data *data = host->data;
	int direction;
	struct dma_chan *chan;
	struct mdc_dma_tx_control tx_control;

	direction = (data->flags & MMC_DATA_READ) ?
			DMA_FROM_DEVICE : DMA_TO_DEVICE;

	if (direction == DMA_TO_DEVICE) {
		struct dma_slave_config dma_tx_conf = {
			.direction = DMA_MEM_TO_DEV,
			.dst_addr = CR_PERIP_SDHOST_DMA_RDATA,
			.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
			.dst_maxburst = 0, /* minimum burst */
		};
		tx_control.flags = MDC_ACCESS_DELAY;
		tx_control.access_delay = 1;
		host->dma_data->txchan->private = (void *)&tx_control;

		dmaengine_slave_config(host->dma_data->txchan,
				       &dma_tx_conf);

		/* Prepare the DMA channel for transfer */
		host->dma_data->desc = dmaengine_prep_slave_sg(
						host->dma_data->txchan,
						data->sg,
						sg_len,
						DMA_MEM_TO_DEV,
						DMA_PREP_INTERRUPT|
						DMA_CTRL_ACK);
		chan = host->dma_data->txchan;
	} else {
		struct dma_slave_config dma_rx_conf = {
			.direction = DMA_DEV_TO_MEM,
			.src_addr = CR_PERIP_SDHOST_DMA_WDATA,
			.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
			.src_maxburst = 0, /* minimum burst */
		};
		tx_control.flags = MDC_ACCESS_DELAY;
		tx_control.access_delay = 0;
		host->dma_data->rxchan->private = (void *)&tx_control;

		dmaengine_slave_config(host->dma_data->rxchan,
				       &dma_rx_conf);

		/* Prepare the DMA channel for transfer */
		host->dma_data->desc = dmaengine_prep_slave_sg(
						host->dma_data->rxchan,
						data->sg,
						sg_len,
						DMA_DEV_TO_MEM,
						DMA_PREP_INTERRUPT|
						DMA_CTRL_ACK);
		chan = host->dma_data->rxchan;
	}

	if (!host->dma_data->desc) {
		dev_err(host->dev,
			"Failed to allocate transfer descriptor\n");
		return;
	}

	/* set the callbacks */
	host->dma_data->desc->callback = dw_mmc_dma_complete;
	host->dma_data->desc->callback_param = host;
	/* Submit the descriptor */
	dmaengine_submit(host->dma_data->desc);
	/* Make the transfer */
	dma_async_issue_pending(chan);
}

static void dw_mmc_dma_stop(struct dw_mci *host)
{
	dmaengine_terminate_all(host->dma_data->txchan);
	dmaengine_terminate_all(host->dma_data->rxchan);
}

struct dw_mci_dma_ops comet_dma_ops = {
	.init = dw_mmc_dma_init,
	.exit = dw_mmc_dma_exit,
	.start = dw_mmc_dma_start,
	.stop = dw_mmc_dma_stop,
	.cleanup = dw_mmc_dma_cleanup,
};
