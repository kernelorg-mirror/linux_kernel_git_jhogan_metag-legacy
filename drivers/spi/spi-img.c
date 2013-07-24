/*
 * IMG SPI controller driver (master mode only)
 *
 * Driver for IMGLIB SPI Controller
 *
 * Author: Imagination Technologies Ltd.
 * Copyright: 2007, 2008, 2013 Imagination Technologies Ltd.
 *
 * Based on spi_bfin5xx.c from Analog Devices Inc.
 *
 * This program is free software ;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ;  either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <linux/spi/spi_img.h>
#include <linux/img_mdc_dma.h>

#define SPI_FREQ_MAX            ((24.576 * 0xff) / 512)
#define SPI_FREQ_MIN            ((24.576 * 0x01) / 512)

/*
 * The FIFO is 16 bytes deep. If we can fit the transfer in the FIFO
 * it's more efficient to do that and avoid the overhead of setting
 * up DMA.
 *
 *
 * NOTE: This is set to zero to disable PIO for now. It works with mmc but not
 * the Marvell 88W8686 wifi chip for reasons that are unknown at present.
 */
#define DMA_MIN_SIZE		0 /*16 if if worked for all devices*/

#if defined(CONFIG_META_DMA_CONTROLLER)

/* BURST SIZE is in bytes for MDC */
#define BURST_SIZE		8 /*Fifo is 16 bytes deep so burst this many*/
#define BURST_BYTES		(BURST_SIZE*8)

#define BURST_MASK              (~(BURST_BYTES - 1))

#else /* DMAC */

/* We have to work around a hardware bug. When DMAing from external memory to
 * the SPI controller the DMAC creates a byte mask for the first and last
 * transfers. Unfortunately it uses the end byte mask for every burst, not just
 * the final burst. The workaround is to make sure we only DMA whole bursts
 * (so the end byte mask is all 1s) or single bursts if we're transferring
 * less than a whole burst.
 *
 * The burst size is defined in bus-width terms so 1 is a 64-bit burst.
 */
#define BURST_SIZE              1
#define BURST_BYTES             (BURST_SIZE*8)
#define BURST_MASK              (~(BURST_BYTES - 1))

/* The DMAC also cannot do DMA from unaligned buffers thus we copy the data to
 * a dma-able bounce buffer first.
 */
#define NEED_BOUNCE_BUFFERS	1

#endif

/* SPI - Device Registers */
#define SPI_DEV0_REG            0x000   /* Device 0*/
#define SPI_DEV1_REG            0x004   /* Device 1*/
#define SPI_DEV2_REG            0x008   /* Device 2*/
#define SPI_MODE_REG            0x010
#define SPI_TRANS_REG           0x00C   /* transaction parameters */
#define DMA_SPIO_SENDDAT        0x014
#define DMA_SPII_GETDAT         0x018
#define DMA_SPIO_INT_STAT       0x01C
#define DMA_SPIO_INT_EN         0x020
#define DMA_SPIO_INT_CL         0x024
#define DMA_SPII_INT_STAT       0x028
#define DMA_SPII_INT_EN         0x02C
#define DMA_SPII_INT_CL         0x030
#define SPI_DI_STATUS           0x034

#define SPI_TRANS_REG_CONT_BIT  0x08000000
#define SPI_TRANS_RESET_BIT     0x04000000
#define SPI_TRANS_REG_GDMA_BIT  0x02000000
#define SPI_TRANS_REG_SDMA_BIT  0x01000000

#define SPI_SDTRIG_EN           0x1

#define SPI_GDTRIG		0x1
#define SPI_ALLDONE_TRIG	0x10

#define SPI_WRITE_INT_MASK      0x1f
#define SPI_READ_INT_MASK       0x1f

#define START_STATE   ((void *)0)
#define RUNNING_STATE ((void *)1)
#define DONE_STATE    ((void *)2)
#define ERROR_STATE   ((void *)-1)

#define QUEUE_RUNNING 0
#define QUEUE_STOPPED 1

#define spi_readb(dd, reg)		readb(dd->regs_base + reg)
#define spi_writeb(val, dd, reg)	writeb(val, dd->regs_base + reg)
#define spi_readl(dd, reg)		readl(dd->regs_base + reg)
#define spi_writel(val, dd, reg)	writel(val, dd->regs_base + reg)

struct driver_data {
	/* Driver model hookup */
	struct platform_device *pdev;

	/* SPI framework hookup */
	struct spi_master *master;

	/* Regs base of SPI controller */
	void __iomem *regs_base;
	unsigned int periph_base;

	/* Clocks */
	struct clk *clk;

	struct img_spi_master *master_info;

	/* Driver message queue */
	struct workqueue_struct *workqueue;
	struct work_struct pump_messages;
	spinlock_t lock;
	struct list_head queue;
	int busy;
	int run;

	/* Message Transfer pump */
	struct tasklet_struct pump_transfers;

	/* Current message transfer state info */
	struct spi_message *cur_msg;
	struct spi_transfer *cur_transfer;
	struct chip_data *cur_chip;

	/* Length of the current DMA */
	size_t len;

	/* Length of any subsequent DMA needed to clean up after the bug
	 * mentioned above.
	 */
	size_t tail_len;

	/* Total length of the transfer */
	size_t map_len;

	/* Virtual addresses of the current transfer buffers. */
	void *tx;
	void *rx;

	/* DMA channels */
	struct dma_chan *txchan;
	struct dma_chan *rxchan;

	/* DMA mapped buffers of the current transfer. */
	dma_addr_t rx_dma;
	dma_addr_t tx_dma;

	/* Bounce buffers */
	void *rx_buf;
	void *tx_buf;

	dma_addr_t rx_dma_start;
	dma_addr_t tx_dma_start;

	int read_irq;

	/* This flag is set if this is the last transfer in the current
	 * message.
	 */
	int last_transfer;
	int cs_change;

#ifdef CONFIG_PM_SLEEP
	/* Suspend data */
	u32 modereg;
#endif

	/*set if we had to dma map the buffers provide to us*/
	int tx_mapped_by_us:1;
	int rx_mapped_by_us:1;
};

struct chip_data {
	u8 clk_pol;
	u8 clk_pha;

	u8 clk_div;
	u8 cs_setup;
	u8 cs_hold;
	u8 cs_delay;

	u8 cs_high;

	u8 chip_select_num;

	u8 bits_per_word;
};

/* Perform a byte-swap operation on a buffer, grouped by 16-bit words */
static void byte_swap(u16 *buf, int len)
{
	u16 *pos;
	for (pos = buf; pos < buf + len; pos++)
		*pos = cpu_to_be16(*pos);
}

/*  Calculate the clock divider value based on input HZ.*/
static u8 hz_to_clk_div(struct driver_data *drv_data, u32 speed_hz)
{
	/*  Register value is:
	 *  Fout = (Fin * reg / 512) MHz */
	u8 val = min_t(unsigned int, speed_hz/(clk_get_rate(drv_data->clk)/512),
				     0xffU);

	/* Clamp value at 1 as 0 is invalid (we get no clock) */
	val = val ? val : 1;

	return val;
}

static void setup_spi_mode(struct driver_data *drv_data,
			   struct chip_data *chip)
{
	unsigned int mask = 0xf;
	unsigned int shift = chip->chip_select_num * 4;
	unsigned int val;

	val = spi_readl(drv_data, SPI_MODE_REG);

	val &= ~(mask << shift);

	/* Data and chip select idle high. */
	val |= (((chip->clk_pha << 3) | (chip->clk_pol << 1) | 0x5) << shift);

	spi_writel(val, drv_data, SPI_MODE_REG);
}

static void write_spi_param(struct driver_data *drv_data, u8 cs, u8 clk_div,
			    u8 cs_setup, u8 cs_hold, u8 cs_delay)
{
	unsigned int params = ((clk_div << 24) | (cs_setup << 16) |
			       (cs_hold << 8) | (cs_delay));

	if (cs == 0)
		spi_writel(params, drv_data, SPI_DEV0_REG);
	else if (cs == 1)
		spi_writel(params, drv_data, SPI_DEV1_REG);
	else if (cs == 2)
		spi_writel(params, drv_data, SPI_DEV2_REG);
}

static void img_spi_dma_prep_slave(struct driver_data *drv_data,
				    unsigned int periph_base,
				    enum dma_transfer_direction direction)
{
	struct dma_slave_config conf;
	struct dma_chan *chan;

	conf.direction = direction;

	if (direction == DMA_DEV_TO_MEM) {
		conf.src_addr = periph_base;
		conf.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		conf.src_maxburst = BURST_SIZE;
		chan = drv_data->rxchan;
	} else {
		conf.dst_addr = periph_base;
		conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		conf.dst_maxburst = BURST_SIZE;
		chan = drv_data->txchan;
	}

	dmaengine_slave_config(chan, &conf);
}

static void start_dma(struct driver_data *drv_data, struct chip_data *chip)
{
	unsigned int transaction = 0;
	unsigned int chip_select = drv_data->cur_chip->chip_select_num;
	struct dma_async_tx_descriptor *rxdesc, *txdesc;
	dma_cookie_t rxcookie, txcookie;
	unsigned int periph_base;
	struct mdc_dma_tx_control tx_control;

	if (chip->cs_high)
		chip_select |= 0x1;

	/* Maximum transfer of 4096 bytes requires count of 0 */
	if (drv_data->len < IMG_SPI_MAX_TRANSFER)
		transaction |= drv_data->len;

	transaction |= (chip_select << 16);

	if ((drv_data->tail_len) ||
	    (!drv_data->cs_change && !drv_data->last_transfer))
		transaction |= SPI_TRANS_REG_CONT_BIT;

	/* Ensure all writes to the tx buffer have completed. */
	wmb();

	/* access delay = 1 for R/W */
	tx_control.flags = MDC_ACCESS_DELAY;
	tx_control.access_delay = 1;

	/* Setup RX */
	periph_base = (unsigned int)(drv_data->periph_base + DMA_SPII_GETDAT);

	drv_data->rxchan->private = (void *)&tx_control;
	img_spi_dma_prep_slave(drv_data, periph_base, DMA_DEV_TO_MEM);

	rxdesc = dmaengine_prep_slave_single(drv_data->rxchan, drv_data->rx_dma,
					   drv_data->len, DMA_DEV_TO_MEM,
					   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);

	if (!rxdesc) {
		dev_err(&drv_data->pdev->dev,
			"Failed to allocate a RX dma descriptor\n");
		return;
	}

	rxcookie = dmaengine_submit(rxdesc);

	dma_async_issue_pending(drv_data->rxchan);

	spi_writel(SPI_SDTRIG_EN, drv_data, DMA_SPII_INT_EN);
	transaction |= SPI_TRANS_REG_GDMA_BIT;

	/* Setup TX */
	periph_base = (unsigned int)(drv_data->periph_base + DMA_SPIO_SENDDAT);
	drv_data->txchan->private = (void *)&tx_control;

	img_spi_dma_prep_slave(drv_data, periph_base, DMA_MEM_TO_DEV);

	txdesc = dmaengine_prep_slave_single(drv_data->txchan, drv_data->tx_dma,
					   drv_data->len, DMA_MEM_TO_DEV,
					   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);

	if (!txdesc) {
		dev_err(&drv_data->pdev->dev,
			"Failed to allocate a TX dma descriptor\n");
		return;
	}

	txcookie = dmaengine_submit(txdesc);

	dma_async_issue_pending(drv_data->txchan);

	transaction |= SPI_TRANS_REG_SDMA_BIT;

	dev_dbg(&drv_data->pdev->dev, "Starting SPI Transaction"
			" Tx Buff 0x%08x "
			" Rx Buff 0x%08x "
			" Size %d\n",
			drv_data->tx_dma, drv_data->rx_dma, drv_data->len);


	spi_writel(transaction, drv_data, SPI_TRANS_REG);
}

/* test if there are more transfers to be done */
static void *next_transfer(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	struct spi_transfer *trans = drv_data->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		struct spi_transfer *next_trans;
		next_trans = list_entry(trans->transfer_list.next,
					struct spi_transfer, transfer_list);
		drv_data->cur_transfer = next_trans;
		if (list_is_last(&next_trans->transfer_list,
				 &msg->transfers))
			drv_data->last_transfer = 1;
		else
			drv_data->last_transfer = 0;
		return RUNNING_STATE;
	} else
		return DONE_STATE;
}

static void finished_transfer(struct driver_data *drv_data)
{
	drv_data->cur_msg->actual_length += drv_data->map_len;

	/* Move to next transfer */
	drv_data->cur_msg->state = next_transfer(drv_data);

	/* Schedule transfer tasklet */
	tasklet_schedule(&drv_data->pump_transfers);
}

static void start_pio(struct driver_data *drv_data, struct chip_data *chip)
{
	unsigned int transaction = 0;
	unsigned int chip_select = drv_data->cur_chip->chip_select_num;
	unsigned int i;
	uint32_t irq_status;

	if (chip->cs_high)
		chip_select |= 0x1;

	transaction |= drv_data->map_len;
	transaction |= (chip_select << 16);
	transaction |= SPI_TRANS_REG_SDMA_BIT;
	transaction |= SPI_TRANS_REG_GDMA_BIT;

	if (!drv_data->cs_change && !drv_data->last_transfer)
		transaction |= SPI_TRANS_REG_CONT_BIT;

	/* Fill up the FIFO */
	for (i = 0; i < drv_data->map_len; i++) {
		const u8 *write_buf = drv_data->tx;
		u8 write_byte;
		if (write_buf)
			write_byte = write_buf[i];
		else
			write_byte = 0x00;
		spi_writeb(write_byte, drv_data, DMA_SPIO_SENDDAT);
	}

	/* Start the transaction */
	spi_writel(transaction, drv_data, SPI_TRANS_REG);

	/* Wait for the data we're going to read to fill the FIFO */
	irq_status = 0;
	while (!((irq_status & SPI_GDTRIG) || (irq_status & SPI_ALLDONE_TRIG)))
		irq_status = spi_readl(drv_data, DMA_SPII_INT_STAT);

	/* Read from FIFO */
	for (i = 0; i < drv_data->map_len; i++) {
		u8 *read_buf = drv_data->rx;
		u8 read_byte = spi_readb(drv_data, DMA_SPII_GETDAT);
		if (read_buf)
			read_buf[i] = read_byte;
	}

	/* Clear any interrupts we generated */
	spi_writel(SPI_READ_INT_MASK, drv_data, DMA_SPII_INT_CL);

	finished_transfer(drv_data);
}

/*
 * caller already set message->status;
 * dma and pio irqs are blocked give finished message back
 */
static void giveback(struct driver_data *drv_data)
{
	unsigned long flags;
	struct spi_message *msg;

	spin_lock_irqsave(&drv_data->lock, flags);
	msg = drv_data->cur_msg;
	drv_data->cur_msg = NULL;
	drv_data->cur_transfer = NULL;
	drv_data->cur_chip = NULL;
	queue_work(drv_data->workqueue, &drv_data->pump_messages);
	spin_unlock_irqrestore(&drv_data->lock, flags);

	msg->state = NULL;

	if (msg->complete)
		msg->complete(msg->context);
}

static irqreturn_t spi_irq(int irq, void *dev_id)
{
	struct driver_data *drv_data = dev_id;
	unsigned int stat;

	stat = spi_readl(drv_data, DMA_SPII_INT_STAT);
	spi_writel(0, drv_data, DMA_SPII_INT_EN);
	spi_writel(SPI_READ_INT_MASK, drv_data, DMA_SPII_INT_CL);

	if (!(stat & SPI_GDTRIG) || !drv_data->cur_msg) {
		dev_err(&drv_data->pdev->dev, "spurious read irq\n");
		return IRQ_HANDLED;
	}

	if (drv_data->tail_len) {
		if (drv_data->rx_dma)
			drv_data->rx_dma += drv_data->len;

		if (drv_data->tx_dma)
			drv_data->tx_dma += drv_data->len;

		drv_data->len = drv_data->tail_len;

		drv_data->tail_len = 0;

		start_dma(drv_data, drv_data->cur_chip);

		return IRQ_HANDLED;
	}

#ifdef NEED_BOUNCE_BUFFERS
	if (drv_data->rx)
		memcpy(drv_data->rx, drv_data->rx_buf, drv_data->map_len);
#else

	if (drv_data->tx_mapped_by_us) {

		dma_unmap_single(&drv_data->pdev->dev, drv_data->tx_dma,
			drv_data->map_len, DMA_TO_DEVICE);

		drv_data->tx_mapped_by_us = 0;

		/*dev_dbg(&drv_data->pdev->dev, "UnMapped tx address %08x"
				" size %d\n",
				(u32)drv_data->tx_dma,
				drv_data->len);*/
	}
	if (drv_data->rx_mapped_by_us) {

		dma_unmap_single(&drv_data->pdev->dev, drv_data->rx_dma,
			drv_data->map_len, DMA_FROM_DEVICE);

		drv_data->rx_mapped_by_us = 0;

		/*dev_dbg(&drv_data->pdev->dev, "UnMapped rx address %08x"
				" size %d\n",
				(u32)drv_data->rx_dma,
				drv_data->len);*/
	}
#endif

	dev_dbg(&drv_data->pdev->dev, "interrupt di status: %#x\n",
		spi_readl(drv_data, SPI_DI_STATUS));

	/* For a 16bpw transfer, byte swap the rx buffer */
	if ((drv_data->cur_chip->bits_per_word == 16) && (drv_data->rx)) {
		wmb();
		byte_swap((u16 *)drv_data->rx, drv_data->map_len >> 1);
	}

	/*
	 * Some drivers (i.e the new libertas patches) rely on the data put
	 * intop the buffer NOT being changed. So if we've changed the data
	 * (i.e. buffer swapped) change it back.  Because we're half-duplex,
	 * we can use drv_data->map_len; if we ever became full duplex,
	 * map_len would need to be separated for tx and rx.
	 */
	if ((drv_data->cur_chip->bits_per_word == 16) && (drv_data->tx)) {
		wmb();
		byte_swap((u16 *)drv_data->tx, drv_data->map_len >> 1);
	}

	finished_transfer(drv_data);

	return IRQ_HANDLED;
}

#ifndef NEED_BOUNCE_BUFFERS
/* Helper:*/
static int map_buffers(struct driver_data *drv_data,
		struct spi_message *message,
		struct spi_transfer *transfer)
{
	if (message->is_dma_mapped) {
		/* Buffer already has a DMA mapping */

		dev_dbg(&drv_data->pdev->dev, "Buffers pre-mapped\n");

		drv_data->tx_mapped_by_us = 0;
		drv_data->rx_mapped_by_us = 0;

		if (drv_data->tx) {
			drv_data->tx_dma = transfer->tx_dma;
		} else {
			/* We have to send something - use the bounce buffer
			   set to zero */
			memset(drv_data->tx_buf, 0x00, transfer->len);
			drv_data->tx_dma = drv_data->tx_dma_start;
		}

		if (drv_data->rx) {
			drv_data->rx_dma = transfer->rx_dma;
		} else {  /*no rx buffer we still need to dma data out
			  of fifo though so use the bounce buffer*/
			drv_data->rx_dma = drv_data->rx_dma_start;
		}

	} else {
		/* We must create a dma mapping for the buffer */
		if (drv_data->tx) {
			drv_data->tx_dma = dma_map_single(&drv_data->pdev->dev,
					drv_data->tx, transfer->len,
					DMA_TO_DEVICE);

			/*dev_dbg(&drv_data->pdev->dev, "Mapped Tx address %08x"
					" to %08x size %d\n",
					(u32)tx_temp,
					(u32)drv_data->tx_dma,
					transfer->len);*/

			if (!drv_data->tx_dma) {
				dev_err(&drv_data->pdev->dev,
						"Failed to DMA Map Tx Buffer");
				return -ENOMEM;
			}
			drv_data->tx_mapped_by_us = 1;
		} else {
			/* we have to send something - use the bounce buffer
			   set to zero */
			memset(drv_data->tx_buf, 0x00, transfer->len);
			drv_data->tx_dma = drv_data->tx_dma_start;
		}

		if (drv_data->rx) {

			drv_data->rx_dma = dma_map_single(
					&drv_data->pdev->dev,
					drv_data->rx,
					transfer->len,
					DMA_FROM_DEVICE);

			/*dev_dbg(&drv_data->pdev->dev, "Mapped Rx address %08x"
					" to %08x size %d\n",
					(u32)drv_data->rx,
					(u32)drv_data->rx_dma,
					transfer->len);*/

			if (!drv_data->rx_dma) {
				dev_err(&drv_data->pdev->dev,
					"Failed to DMA Map Rx Buffer");

				return -ENOMEM;
			}
			drv_data->rx_mapped_by_us = 1;
		} else {
			/*no rx buffer we still need to dma data out
			  of fifo though so use the bounce buffer*/
			drv_data->rx_dma = drv_data->rx_dma_start;
		}
	}
	return 0;
}
#endif

static void pump_transfers(unsigned long data)
{
	struct driver_data *drv_data = (struct driver_data *)data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct chip_data *chip = NULL;

	/* Get current state information */
	message = drv_data->cur_msg;
	transfer = drv_data->cur_transfer;
	chip = drv_data->cur_chip;

	/*
	 * if msg is error or done, report it back using complete() callback
	 */

	 /* Handle for abort */
	if (message->state == ERROR_STATE) {
		message->status = -EIO;
		giveback(drv_data);
		return;
	}

	/* Handle end of message */
	if (message->state == DONE_STATE) {
		message->status = 0;
		giveback(drv_data);
		return;
	}

	/* Delay if requested at end of transfer */
	if (message->state == RUNNING_STATE) {
		previous = list_entry(transfer->transfer_list.prev,
				      struct spi_transfer, transfer_list);
		if (previous->delay_usecs)
			udelay(previous->delay_usecs);
	}

	if (transfer->len > IMG_SPI_MAX_TRANSFER) {
		dev_dbg(&drv_data->pdev->dev, "pump_transfers: transfer "
			 "length (%d) greater than maximum (%d)\n",
			 transfer->len, IMG_SPI_MAX_TRANSFER);
		transfer->len = IMG_SPI_MAX_TRANSFER;
	}

	if (transfer->len == 0) {
		dev_warn(&drv_data->pdev->dev, "pump_transfers: transfer "
			 "length is zero\n");
		message->status = -EINVAL;
		giveback(drv_data);
		return;
	}

	/* Kernel headers qualify this as const, so we need to cast away to
	 * stop compiler warnings */
	drv_data->tx = (void *)transfer->tx_buf;
	drv_data->rx = transfer->rx_buf;

	/* Byte swap the tx buffer before it is used in a 16-bit transmission */
	if ((chip->bits_per_word == 16) && (drv_data->tx)) {
		byte_swap((u16 *)drv_data->tx, transfer->len >> 1);
		wmb();
	}

#ifdef NEED_BOUNCE_BUFFERS
	/* Copy data to bounce buffer due to DMAC bug */
	if (transfer->len > DMA_MIN_SIZE) {
		if (drv_data->tx)
			memcpy(drv_data->tx_buf, drv_data->tx, transfer->len);
		else
			/*send out zeros*/
			memset(drv_data->tx_buf, 0x00, transfer->len);

		drv_data->rx_dma = drv_data->rx_dma_start;
		drv_data->tx_dma = drv_data->tx_dma_start;

	}

	if (transfer->len > BURST_BYTES)
		drv_data->len = transfer->len & BURST_MASK;
	else
		drv_data->len = transfer->len;

	drv_data->tail_len = transfer->len - drv_data->len;

#else	/* MDC */
	/* setup dma mappings for buffers, no need for
	 * the bounce buffer!
	 */
	if (transfer->len > DMA_MIN_SIZE) {
		int ret = map_buffers(drv_data, message, transfer);
		if (ret) {
			message->status = ret;
			giveback(drv_data);
			return;
		}
		drv_data->len = transfer->len;
		drv_data->tail_len = 0;
	}
#endif

	drv_data->map_len = transfer->len;

	drv_data->cs_change = transfer->cs_change;

	/* Make sure soft reset bit is cleared */
	spi_writel(0, drv_data, SPI_TRANS_REG);

	/* Change speed per transfer */
	if (transfer->speed_hz) {
		write_spi_param(drv_data, chip->chip_select_num,
				hz_to_clk_div(drv_data, transfer->speed_hz),
				chip->cs_setup, chip->cs_hold, chip->cs_delay);
		dev_dbg(&drv_data->pdev->dev, "Setting Clock to %d HZ\n",
				transfer->speed_hz);

	} else
		write_spi_param(drv_data, chip->chip_select_num, chip->clk_div,
				chip->cs_setup, chip->cs_hold, chip->cs_delay);

	message->state = RUNNING_STATE;

	if (transfer->len > DMA_MIN_SIZE)
		start_dma(drv_data, chip);
	else
		start_pio(drv_data, chip);


}




/* pop a msg from queue and kick off real transfer */
static void pump_messages(struct work_struct *work)
{
	struct driver_data *drv_data = container_of(work, struct driver_data,
						    pump_messages);
	struct spi_transfer *next_trans;
	unsigned long flags;

	/* Lock queue and check for queue work */
	spin_lock_irqsave(&drv_data->lock, flags);
	if (list_empty(&drv_data->queue) || drv_data->run == QUEUE_STOPPED) {
		/* pumper kicked off but no work to do */
		drv_data->busy = 0;
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return;
	}

	/* Make sure we are not already running a message */
	if (drv_data->cur_msg) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return;
	}

	/* Extract head of queue */
	drv_data->cur_msg = list_entry(drv_data->queue.next,
				       struct spi_message, queue);
	list_del_init(&drv_data->cur_msg->queue);

	/* Initial message state */
	drv_data->cur_msg->state = START_STATE;
	next_trans = list_entry(drv_data->cur_msg->transfers.next,
				struct spi_transfer, transfer_list);
	drv_data->cur_transfer = next_trans;
	if (list_is_last(&next_trans->transfer_list,
			 &drv_data->cur_msg->transfers))
		drv_data->last_transfer = 1;
	else
		drv_data->last_transfer = 0;

	/* Setup the SPI using the per chip configuration */
	drv_data->cur_chip = spi_get_ctldata(drv_data->cur_msg->spi);

	dev_dbg(&drv_data->pdev->dev, "the first transfer len is %d\n",
		drv_data->cur_transfer->len);

	/* Mark as busy and launch transfers */
	tasklet_schedule(&drv_data->pump_transfers);

	drv_data->busy = 1;
	spin_unlock_irqrestore(&drv_data->lock, flags);
}

/*
 * got a msg to transfer, queue it in drv_data->queue.
 * And kick off message pumper
 */
static int transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct driver_data *drv_data = spi_master_get_devdata(spi->master);
	unsigned long flags;

	spin_lock_irqsave(&drv_data->lock, flags);

	if (drv_data->run == QUEUE_STOPPED) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return -ESHUTDOWN;
	}

	msg->actual_length = 0;
	msg->status = -EINPROGRESS;
	msg->state = START_STATE;

	dev_dbg(&spi->dev, "adding a msg in transfer()\n");
	list_add_tail(&msg->queue, &drv_data->queue);

	if (drv_data->run == QUEUE_RUNNING && !drv_data->busy)
		queue_work(drv_data->workqueue, &drv_data->pump_messages);

	spin_unlock_irqrestore(&drv_data->lock, flags);

	return 0;
}

/* first setup for new devices */
static int setup(struct spi_device *spi)
{
	struct driver_data *drv_data = spi_master_get_devdata(spi->master);
	struct img_spi_chip *chip_info = NULL;
	struct chip_data *chip;

	/* Zero (the default) here means 8 bits */
	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	/* Allow for 8- or 16-bit word */
	if ((spi->bits_per_word != 8) && (spi->bits_per_word != 16)) {
		dev_dbg(&spi->dev, "setup: unsupported bits per word %x\n",
			spi->bits_per_word);
		return -EINVAL;
	}

	if ((spi->mode & SPI_CS_HIGH) && (spi->chip_select != 2)) {
		dev_dbg(&spi->dev,
			"setup: SPI_CS_HIGH only supported on CS 2\n");
		return -EINVAL;
	}

	if (spi->mode & SPI_LSB_FIRST) {
		dev_dbg(&spi->dev,
			"setup: LSB first devices are unsupported\n");
		return -EINVAL;
	}

	/* Only alloc (or use chip_info) on first setup */
	chip = spi_get_ctldata(spi);
	if (chip == NULL) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;

		chip_info = spi->controller_data;
	}

	chip->cs_setup = 0xa;  /* 400 ns */
	chip->cs_hold = 0xa;   /* 400 ns */
	chip->cs_delay = 0x14; /* 800 ns */

	/* chip_info isn't always needed */
	if (chip_info) {
		chip->cs_setup = chip_info->cs_setup;
		chip->cs_hold = chip_info->cs_hold;
		chip->cs_delay = chip_info->cs_delay;
	}

	if (spi->mode & SPI_CPOL)
		chip->clk_pol = 1;
	else
		chip->clk_pol = 0;

	if (spi->mode & SPI_CPHA)
		chip->clk_pha = 1;
	else
		chip->clk_pha = 0;

	if (spi->mode & SPI_CS_HIGH)
		chip->cs_high = 1;
	else
		chip->cs_high = 0;


	dev_dbg(&spi->dev, "Setting Clock to %d HZ (Max)\n", spi->max_speed_hz);

	chip->clk_div = hz_to_clk_div(drv_data, spi->max_speed_hz);

	chip->chip_select_num = spi->chip_select;

	chip->bits_per_word = spi->bits_per_word;

	setup_spi_mode(drv_data, chip);

	spi_set_ctldata(spi, chip);

	return 0;
}

/*
 * callback for spi framework.
 * clean driver specific data
 */
static void cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);

	kfree(chip);
}

static int init_queue(struct driver_data *drv_data)
{
	INIT_LIST_HEAD(&drv_data->queue);
	spin_lock_init(&drv_data->lock);

	drv_data->run = QUEUE_STOPPED;
	drv_data->busy = 0;

	/* init transfer tasklet */
	tasklet_init(&drv_data->pump_transfers,
		     pump_transfers, (unsigned long)drv_data);

	/* init messages workqueue */
	INIT_WORK(&drv_data->pump_messages, pump_messages);
	drv_data->workqueue =
		create_singlethread_workqueue(dev_name(drv_data->master->dev.parent));
	if (drv_data->workqueue == NULL)
		return -EBUSY;

	return 0;
}

static int start_queue(struct driver_data *drv_data)
{
	unsigned long flags;

	spin_lock_irqsave(&drv_data->lock, flags);

	if (drv_data->run == QUEUE_RUNNING || drv_data->busy) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return -EBUSY;
	}

	drv_data->run = QUEUE_RUNNING;
	drv_data->cur_msg = NULL;
	drv_data->cur_transfer = NULL;
	drv_data->cur_chip = NULL;
	spin_unlock_irqrestore(&drv_data->lock, flags);

	queue_work(drv_data->workqueue, &drv_data->pump_messages);

	return 0;
}

static int stop_queue(struct driver_data *drv_data)
{
	unsigned long flags;
	unsigned limit = 500;
	int status = 0;

	spin_lock_irqsave(&drv_data->lock, flags);

	/*
	 * This is a bit lame, but is optimized for the common execution path.
	 * A wait_queue on the drv_data->busy could be used, but then the common
	 * execution path (pump_messages) would be required to call wake_up or
	 * friends on every SPI message. Do this instead
	 */
	drv_data->run = QUEUE_STOPPED;
	while (!list_empty(&drv_data->queue) && drv_data->busy && limit--) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		msleep(10);
		spin_lock_irqsave(&drv_data->lock, flags);
	}

	if (!list_empty(&drv_data->queue) || drv_data->busy)
		status = -EBUSY;

	spin_unlock_irqrestore(&drv_data->lock, flags);

	return status;
}

static int destroy_queue(struct driver_data *drv_data)
{
	int status;

	status = stop_queue(drv_data);
	if (status != 0)
		return status;

	destroy_workqueue(drv_data->workqueue);

	return 0;
}

static void img_spi_init_hw(struct driver_data *drv_data)
{
	/* Reset the SPI controller. */
	spi_writel(SPI_TRANS_RESET_BIT, drv_data, SPI_TRANS_REG);
	spi_writel(0, drv_data, SPI_TRANS_REG);

	/* Disable any interrupts that may be enabled. */
	spi_writel(0, drv_data, DMA_SPIO_INT_EN);
	spi_writel(0, drv_data, DMA_SPII_INT_EN);
	spi_writel(SPI_WRITE_INT_MASK, drv_data, DMA_SPIO_INT_CL);
	spi_writel(SPI_READ_INT_MASK, drv_data, DMA_SPII_INT_CL);
}

static int img_spi_init_dma(struct platform_device *pdev,
			    struct driver_data *drv_data,
			    struct device *dev) {
	int status;

	drv_data->rxchan = dma_request_slave_channel(&pdev->dev,
						     "rx");

	if (!drv_data->rxchan) {
		dev_err(dev, "Failed to get SPI DMA rx channel\n");
		status = -EBUSY;
		goto out;
	}

	drv_data->txchan = dma_request_slave_channel(&pdev->dev,
						     "tx");

	if (!drv_data->txchan) {
		dev_err(dev, "Failed to get SPI DMA tx channel\n");
		goto free_rx;
	}

	/* Allocate necessary coherent buffers */

	drv_data->rx_buf = dma_alloc_coherent(dev, IMG_SPI_MAX_TRANSFER,
					      &drv_data->rx_dma_start,
					      GFP_KERNEL);

	if (!drv_data->rx_buf) {
		dev_err(dev, "failed to alloc read dma buffer\n");
		status = -ENOMEM;
		goto free_tx;
	}

	drv_data->tx_buf = dma_alloc_coherent(dev, IMG_SPI_MAX_TRANSFER,
					      &drv_data->tx_dma_start,
					      GFP_KERNEL);

	if (!drv_data->tx_buf) {
		dev_err(dev, "failed to alloc write dma buffer\n");
		status = -ENOMEM;
		goto free_buf;
	}


	return 0;

free_buf:
	dma_free_coherent(dev, IMG_SPI_MAX_TRANSFER, drv_data->rx_buf,
			  drv_data->rx_dma_start);
free_tx:
	dma_release_channel(drv_data->txchan);
free_rx:
	dma_release_channel(drv_data->rxchan);
out:
	return status;
}

static struct of_device_id img_spi_of_match[] = {
		{ .compatible = "img,spi", },
		{},
};
MODULE_DEVICE_TABLE(of, img_spi_of_match);

static struct img_spi_master *img_spi_parse_dt(
		struct platform_device *pdev)
{
	struct img_spi_master *pdata;
	u32 prop;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Memory alloc for pdata failed\n");
		return NULL;
	}

	if (of_property_read_u32(pdev->dev.of_node, "num-cs",
				 &prop) < 0) {
		dev_err(&pdev->dev,
			"num-cs not defined\n");
		goto free_data;
	}

	pdata->num_chipselect = prop;

	if (of_property_read_u32(pdev->dev.of_node, "clock-frequency",
				 &prop) < 0)
		/* Default to 40Mhz */
		prop = 40000000;

	pdata->clk_rate = prop;

	return pdata;

free_data:
	kfree(pdata);
	return NULL;
}

static int __init img_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct img_spi_master *platform_info;
	struct spi_master *master;
	struct driver_data *drv_data = NULL;
	struct resource *irq_resource, *mem_resource;
	int status = 0;
	unsigned long clk_rate;

	/* Allocate master with space for drv_data */
	master = spi_alloc_master(dev, sizeof(struct driver_data));
	if (!master) {
		dev_err(&pdev->dev, "cannot alloc spi_master\n");
		return -ENOMEM;
	}

	platform_info = img_spi_parse_dt(pdev);
	if (IS_ERR(platform_info)) {
		status = PTR_ERR(platform_info);
		goto out_error_resource;
	}

	drv_data = spi_master_get_devdata(master);
	drv_data->master = master;
	drv_data->master_info = platform_info;
	drv_data->pdev = pdev;

	/* the spi->mode bits supported by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	master->num_chipselect = platform_info->num_chipselect;
	master->cleanup = cleanup;
	master->setup = setup;
	master->transfer = transfer;
	master->dev.of_node = pdev->dev.of_node;
	master->dma_alignment = 8; /*64bit alignement*/

	dev_set_drvdata(&pdev->dev, master);

	mem_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drv_data->regs_base = devm_request_and_ioremap(&pdev->dev,
						       mem_resource);
	if (!drv_data->regs_base) {
		dev_err(&pdev->dev, "reg not defined\n");
		status = -ENODEV;
		goto out_error_resource;
	}

	drv_data->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(drv_data->clk)) {
		dev_err(dev, "spi clock not found\n");
		status = PTR_ERR(drv_data->clk);
		goto out_error_resource;
	}

	/* try setting the clock to the requested rate */
	if (platform_info->clk_rate) {
		status = clk_set_rate(drv_data->clk, platform_info->clk_rate);
		clk_rate = clk_get_rate(drv_data->clk);
		if (clk_rate != platform_info->clk_rate) {
			dev_warn(dev,
				 "SPI clock requested: %lu HZ. Actual SPI clock: %lu (status=%d)\n",
				 platform_info->clk_rate, clk_rate, status);
		}
		status = 0;
	}

	/* try enabling the clock */
	status = clk_prepare_enable(drv_data->clk);
	if (status) {
		dev_err(dev, "SPI clock could not be enabled\n");
		goto out_error_resource;
	}

	irq_resource = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_resource) {
		dev_err(&pdev->dev, "irq resource not defined\n");
		goto out_error_resource;
	}
	drv_data->read_irq = irq_resource->start;

	/* Initialize and start queue */
	status = init_queue(drv_data);
	if (status != 0) {
		dev_err(&pdev->dev, "problem initializing queue\n");
		goto out_error_queue_alloc;
	}
	status = start_queue(drv_data);
	if (status != 0) {
		dev_err(&pdev->dev, "problem starting queue\n");
		goto out_error_queue_alloc;
	}

	if (drv_data->regs_base == NULL) {
		dev_err(dev, "cannot map IO\n");
		status = -ENXIO;
		goto out_error_queue_alloc;
	}

	drv_data->periph_base = (unsigned int)drv_data->regs_base;

	if (img_spi_init_dma(pdev, drv_data, dev) < 0) {
		dev_err(dev,
			"Failed to allocated tx/rx DMA channels\n");
			status = -ENOMEM;
		goto out_error_queue_alloc;
	}

	if (request_irq(drv_data->read_irq, spi_irq, 0, "img-spi",
			drv_data)) {
		dev_err(&pdev->dev, "failed to get SPI read irq\n");
		status = -EBUSY;
		goto out_error_queue_alloc;
	}

	img_spi_init_hw(drv_data);

	/* Register with the SPI framework */
	platform_set_drvdata(pdev, drv_data);
	status = spi_register_master(master);
	if (status != 0) {
		dev_err(&pdev->dev, "problem registering spi master\n");
		goto out_error_read_irq;
	}
	dev_dbg(&pdev->dev, "controller probed successfully\n");
	return status;

out_error_read_irq:
	free_irq(drv_data->read_irq, drv_data);
out_error_queue_alloc:
	destroy_queue(drv_data);
	clk_disable_unprepare(drv_data->clk);
out_error_resource:
	spi_master_put(master);
	return status;
}

/* stop hardware and remove the driver */
static int img_spi_remove(struct platform_device *pdev)
{
	struct driver_data *drv_data = platform_get_drvdata(pdev);
	int status = 0;

	if (!drv_data)
		return 0;

	/* Remove the queue */
	status = destroy_queue(drv_data);
	if (status != 0)
		return status;

	/* Release DMA */
	dma_release_channel(drv_data->rxchan);
	dma_release_channel(drv_data->txchan);

	/* Free irq */
	free_irq(drv_data->read_irq, drv_data);

	/* Stop the SPI clock */
	clk_disable_unprepare(drv_data->clk);

	/* Disconnect from the SPI framework */
	spi_unregister_master(drv_data->master);

	/* Prevent double remove */
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int img_spi_suspend(struct device *dev)
{
	struct driver_data *drv_data = dev_get_drvdata(dev);
	int status = 0;

	status = stop_queue(drv_data);
	if (status != 0)
		return status;

	drv_data->modereg = spi_readl(drv_data, SPI_MODE_REG);

	/* FIXME Can we do anything here to power down the SPI? */

	return 0;
}

static int img_spi_resume(struct device *dev)
{
	struct driver_data *drv_data = dev_get_drvdata(dev);
	int status = 0;

	/* Reinitialise the hardware */
	img_spi_init_hw(drv_data);
	spi_writel(drv_data->modereg, drv_data, SPI_MODE_REG);

	/* Start the queue running */
	status = start_queue(drv_data);
	if (status != 0) {
		dev_err(dev, "problem starting queue (%d)\n", status);
		return status;
	}

	return 0;
}
#else
#define img_spi_suspend NULL
#define img_spi_resume NULL
#endif	/* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(img_spi_pmops, img_spi_suspend, img_spi_resume);

MODULE_ALIAS("img-spi");	/* for platform bus hotplug */
static struct platform_driver img_spi_driver = {
	.driver	= {
		.name	= "img-spi",
		.owner	= THIS_MODULE,
		.pm	= &img_spi_pmops,
		.of_match_table = img_spi_of_match,
	},
	.remove		= img_spi_remove,
};

static int __init img_spi_init(void)
{
	return platform_driver_probe(&img_spi_driver, img_spi_probe);
}
module_init(img_spi_init);

static void __exit img_spi_exit(void)
{
	platform_driver_unregister(&img_spi_driver);
}
module_exit(img_spi_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("IMG SPI Controller");
MODULE_LICENSE("GPL");
