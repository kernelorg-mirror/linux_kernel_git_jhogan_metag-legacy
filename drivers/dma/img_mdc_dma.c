/*
 * IMG Meta DMA Controller (MDC) specific DMA code.
 *
 * Copyright (C) 2009,2012,2013 Imagination Technologies Ltd.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/img_mdc_dma.h>

#include "dmaengine.h"

#define MAX_MDC_DMA_CHANNELS 32
#define MAX_MDC_DMA_BUSY_RETRY 5
#define MDC_DMA_INT_ACTIVE (1<<8) /* INT_ACTIVE bit of Cmds Processed reg */

DEFINE_SPINLOCK(mdc_dma_lock);
static struct device_driver *wrapper_driver;

struct mdc_config_data {
	int dma_threads;
	int dma_channels;
	int bus_width;
};

struct mdc_chan {
	struct mdc_dmadev		*mdma;
	struct dma_chan			dchan;
	spinlock_t			lock;
	char				name[30];
	enum img_dma_channel_state	alloc_status;
	int				a_chan_nr; /* Channel NR */
	int				irq; /* MDC IRQ */
	int				periph; /* Peripheral NR */
	int				thread; /* Thread for this channel */
	/* virt/dma buffers for channel */
	void				*virt_addr;
	dma_addr_t			dma_addr;
	/* List of current DMA descriptors */
	struct list_head		active_desc; /* Active descriptors */
	struct list_head		free_desc; /* Used descriptors */
	bool				sg; /* true for sg xfer */
	bool				cyclic; /* true for cyclic xfer */
	bool				is_list; /* list-based xfer */
	bool				finished; /* xfer finished */
	int				irq_en; /* MDC IRQ status */
	/* Slave specific configuration */
	struct dma_slave_config		dma_config; /* config for channel */
	int				access_delay;
	int				priority;
	bool				skip_callback;
	/* tasklet for channel */
	struct tasklet_struct		tasklet; /* deferred work */
};

struct mdc_dmadev {
	struct dma_device		dma_slave;
	void __iomem			*base_addr;
	spinlock_t			lock;
	struct mdc_chan			slave_channel[MAX_MDC_DMA_CHANNELS];
	struct mdc_config_data		config;
	const struct img_mdc_soc_callbacks *callbacks;
	int				last_fthread; /* Current fast thread */
	int				last_sthread; /* Current slow thread */
#ifdef CONFIG_PM_SLEEP
	void				*pm_data;
#endif
};

struct mdc_dma_desc {
	struct dma_async_tx_descriptor	txd;
	struct list_head		node;
	enum dma_status			status;
	dma_addr_t			start_list;
	int				total_samples;
	int				buffer_size;
	int				sample_size;
	int				sample_count;
};

/* Forward declaration for dma driver */
static struct platform_driver img_mdc_dma_driver;
static int mdc_terminate_all(struct dma_chan *chan);

static inline struct mdc_chan *to_mdc_chan(struct dma_chan *c)
{
	return container_of(c, struct mdc_chan, dchan);
}

static inline struct mdc_dma_desc *txd_to_mdc_desc(
				struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct mdc_dma_desc, txd);
}

static inline struct device *mchan2dev(struct mdc_chan *c)
{
	return &c->dchan.dev->device;
}

/*
 * Burst Size (expressed in bytes) must be equal to or greater than the
 * system bus width for memory to memory accesses.
 * So use a simple lookup to find the size in bytes based on the system bus
 * width which is reported as log2 of the width in bits:
 *              width (2^n)  0, 1, 2, 3, 4 , 5, 6, 7
 *              width (bits) 1, 2, 4, 8, 16, 32,64,128
 */
static const unsigned burst_size_lookup[] = { 0, 0, 0, 1, 2, 4, 8, 16 };


/*rate limit for warning message*/
static DEFINE_RATELIMIT_STATE(rl_align_warn, 300 * HZ, 1); /* 1 per 5mins*/

/*
 * mdc_dma_filter_fn: Check if the DMA channel is free for allocation.
 * @chan: DMA channel for allocation requested by the dmaengine.
 * @param: Struct containing the requested DMA channel (if any) and the
 * peripheral device number requesting this channel. On return,
 * the req_channel member contains the channel that will be allocated
 * by the MDC DMA device. This is useful when the caller passed -1 (as in,
 * the first available channel) and then he wishes to know what channel will
 * be picked by the DMA device.
 *
 * This callback should be passed to dma_request_channel whenever it is used
 * by a slave device.
 */
bool mdc_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct device_driver *driver;
	spin_lock(&mdc_dma_lock);
	driver	= (wrapper_driver) ? wrapper_driver :
		&img_mdc_dma_driver.driver;
	spin_unlock(&mdc_dma_lock);

	if (chan->device->dev->driver == driver) {
		struct mdc_chan *mchan = to_mdc_chan(chan);
		struct mdc_dma_cookie *c = (struct mdc_dma_cookie *)param;
		if (mchan->alloc_status == IMG_DMA_CHANNEL_AVAILABLE) {
			/* Did the device request a specific channel? */
			if ((c->req_channel > -1) &&
			    (c->req_channel != mchan->a_chan_nr))
				/* Wrong channel */
				return false;
			mchan->periph = c->periph;
			c->req_channel = mchan->a_chan_nr;
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(mdc_dma_filter_fn);

static struct dma_chan *of_dma_mdc_xlate(struct of_phandle_args *dma_spec,
					struct of_dma *ofdma)
{
	struct mdc_dma_cookie cookie;
	dma_cap_mask_t cap;
	int count = dma_spec->args_count;

	/*
	 * 1st argument = peripheral
	 * 2nd argument = dma channel
	 */
	if (count != 2)
		return NULL;

	cookie.periph = dma_spec->args[0];
	cookie.req_channel = dma_spec->args[1];

	dma_cap_zero(cap);
	dma_cap_set(DMA_SLAVE, cap);
	dma_cap_set(DMA_CYCLIC, cap);

	return dma_request_channel(cap, mdc_dma_filter_fn, &cookie);
}

static int check_widths(struct mdc_dmadev *mdma, u32 address)
{
	/*
	 *  check alignment, we can do accesses to/from unaligned address but
	 *  we must set width_w and width_r appropriately and it will impact
	 *  on performance
	 */
	int width = -1;
	if (address & 0x1) { /*byte addresses*/
		if (mdma->config.bus_width > 3) /*2^3 = 8bits = 1byte*/
			width = 0; /*2^0 = 1 byte.*/
	} else if (address & 0x2) { /*word address*/
		if (mdma->config.bus_width > 4) /*2^4 = 16bits = 2 bytes*/
			width = 1;
	} else if (address & 0x4) {
		if (mdma->config.bus_width > 5)
			width = 2;
	}

	if (width < 0) { /*We are aligned*/

		/*
		 * system bus width is in log2(bits)
		 * we need log2(bytes) so subtract 3
		 */
		width = mdma->config.bus_width - 3;
	} else {
		if (__ratelimit(&rl_align_warn))
			dev_warn(mdma->dma_slave.dev,
				 "Using address not aligned to system bus width, this will impact performance\n");
	}

	return width;
}

/**
 * img_reset() - resets a channel
 * @mchan:	The channel to reset
 *
 * Resets a channel by clearing all of its context to zero
 * Then sets up the default settings.
 */
static void img_dma_reset(struct mdc_chan *mchan)
{
	u32 genconf = 0;
	u32 rpconf = 0;
	int dma_channel = mchan->a_chan_nr;
	int systembus_width = mchan->mdma->config.bus_width;

	unsigned long mdc_base_address = (unsigned long)mchan->mdma->base_addr;

	MDC_REG_RESET_CONTEXT(mdc_base_address, dma_channel);

	/*ensure probe has setup base address before proceeding*/
	BUG_ON(!mdc_base_address);

	/*Setup General Config */

	/*enable list interrupts*/
	MDC_SET_FIELD(genconf, MDC_LIST_IEN, mchan->irq_en);
	/*endian swap TODO make user configurable*/
	MDC_SET_FIELD(genconf, MDC_BSWAP, 0);
	/*enable interrupts*/
	MDC_SET_FIELD(genconf, MDC_IEN, mchan->irq_en);
	/*don't latch interrupts*/
	MDC_SET_FIELD(genconf, MDC_LEVEL_INT, 1);
	/* Physical channel.*/
	MDC_SET_FIELD(genconf, MDC_CHANNEL, dma_channel);
	/*256 cycle delay on burst accesses */
	MDC_SET_FIELD(genconf, MDC_ACC_DEL, mchan->access_delay);
	/* ?See manual? delays recognition of DREQ
	 * until burst has reached the unpacker */
	MDC_SET_FIELD(genconf, MDC_WAIT_UNPACK, 0);
	/* Inc write address TODO make user configurable*/
	MDC_SET_FIELD(genconf, MDC_INC_W, 1);
	/* ?See manual? delays recognition of DREQ until burst
	 * has reached the packer.*/
	MDC_SET_FIELD(genconf, MDC_WAIT_PACK, 0);
	/* Incr read address TODO make user configurable*/
	MDC_SET_FIELD(genconf, MDC_INC_R, 1);
	/* Should generally be set unless using a ram
	 * narrower than the system bus*/
	MDC_SET_FIELD(genconf, MDC_PHYSICAL_R, 1);
	MDC_SET_FIELD(genconf, MDC_PHYSICAL_W, 1);
	/* Note Read and Write widths get set when specifying direction. */

	MDC_RSET_GENERAL_CONFIG(mdc_base_address, dma_channel, genconf);

	/*Setup read port: */

	/*
	 * NJ:
	 * We are going to split the channels equally across the number of
	 * available threads in the DMA controller. Ideally we should assign
	 * a different threads to peripherals with high latency than to those
	 * without but we don't know what peripherals are attached, we could
	 * give the user an interface to set this in a more advanced driver.
	 *
	 * Email from Paul Welton (did the hardware design) to NJ on 8/7/09:
	 * " A different thread id should be used for peripherals with different
	 *  latency characteristics. In the case of reads, the fabric guarantees
	 *  that return data within a thread is returned in the same order as
	 *  the read requests. Therefore, if one peripheral or memory is slow to
	 *  return data, then return data from another peripheral or memory on
	 *  the same thread will be blocked. If they are allocated different
	 *  threads then the second one could continue independently.
	 *
	 *  There are also restrictions or bursts. Once a burst begins on one
	 *  thread it must complete before any other burst can begin on the same
	 *  thread. A burst can be blocked by the "READY" signal for that thread
	 *  going low. Note that the READY, unlike the ENABLE, is provided on a
	 *  per-thread basis. Therefore, even for writes, it is advantageous to
	 *  place peripherals or memories which are likely to block on a
	 *  different thread from other critical peripherals which should not
	 *  be blocked."
	 */

	/*
	 *  we split the available threads equally between channels
	 *  so a 16 channel system with 2 threads, channels 0-7 will use
	 *  thread 0 and channels 8-15 will use thread 1
	 */

	/*thread id used in tag for reads issued from list*/
	MDC_SET_FIELD(rpconf, MDC_STHREAD, mchan->thread);
	/*thread id used in tag for reads*/
	MDC_SET_FIELD(rpconf, MDC_RTHREAD, mchan->thread);
	/*thread id used in tag for writes*/
	MDC_SET_FIELD(rpconf, MDC_STHREAD, mchan->thread);

	/*priority of transfers*/
	MDC_SET_FIELD(rpconf, MDC_PRIORITY, mchan->priority);
	/* no of clock cycles before recognising DREQ following end-of-burst(
	 * at unpacker when WAIT_UNPACK=1)*/
	MDC_SET_FIELD(rpconf, MDC_HOLD_OFF, 0);
	/*burst size.*/
	MDC_SET_FIELD(rpconf, MDC_BURST_SIZE,
			(burst_size_lookup[systembus_width & 0x7] - 1));
	/*enable the use of the DREQ signal*/
	MDC_SET_FIELD(rpconf, MDC_DREQ_ENABLE, 0);
	/*perform read back on last write of transaction.*/
	MDC_SET_FIELD(rpconf, MDC_READBACK, 0);

	MDC_RSET_READ_PORT_CONFIG(mdc_base_address, dma_channel, rpconf);

	MDC_RSET_CMDS_PROCESSED(mdc_base_address, dma_channel, 0);

	wmb();
}

/*
 * mdc_handler_isr: Clear the IRQ for the DMA channel and
 * schedule the tasklet
 */
static irqreturn_t mdc_handler_isr(int irq, void *chan_id)
{

	u32 irq_status;
	struct mdc_chan *mchan = chan_id;

	spin_lock(&mchan->lock);

	irq_status = MDC_RGET_CMDS_PROCESSED((unsigned long)
					     mchan->mdma->base_addr,
					     mchan->a_chan_nr);

	if (irq_status & MDC_DMA_INT_ACTIVE) {
		/* reset irq */
		MDC_RSET_CMDS_PROCESSED((unsigned long)
					mchan->mdma->base_addr,
					mchan->a_chan_nr, 0);
		/* Skip tasklet? */
		if (mchan->skip_callback)
			mchan->skip_callback = false;
		else
			/* Schedule the tasklet */
			tasklet_schedule(&mchan->tasklet);
	}

	spin_unlock(&mchan->lock);

	return IRQ_HANDLED;
}

/*
 * mdc_dma_tasklet: Post IRQ work for a DMA channel
 * @unsigned long data: MDC channel pointer
 *
 * Updates descriptor xfer parameters, moves finished descriptors
 * to the free list, calls callback function.
 */
static void mdc_dma_tasklet(unsigned long data)
{
	struct mdc_chan *mchan = (struct mdc_chan *)data;
	struct mdc_dma_desc *desc;
	unsigned long flags;
	dma_async_tx_callback callback = NULL;
	void *param = NULL;

	spin_lock_irqsave(&mchan->lock, flags);
	if (list_empty(&mchan->active_desc)) {
		spin_unlock_irqrestore(&mchan->lock, flags);
		return;
	}

	desc = list_first_entry(&mchan->active_desc, typeof(*desc), node);
	if (++desc->sample_count == desc->total_samples) {
		desc->sample_count = 0;
		mchan->finished = true;
		/* For cyclic, this descriptor will remain active */
		if (!mchan->cyclic)
			/* Move it back to the free list */
			list_move_tail(&desc->node, &mchan->free_desc);
	}
	if (desc->txd.callback) {
		callback = desc->txd.callback;
		param = desc->txd.callback_param;
	}
	spin_unlock_irqrestore(&mchan->lock, flags);

	/* We are safe to call the callback now */
	if (callback)
		callback(param);
}

/*
 * mdc_dma_tx_submit: Start a DMA transfer for txd descriptor
 * @txd: Descriptor for the DMA transfer
 */
static dma_cookie_t mdc_dma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct mdc_dma_desc *dma_desc = txd_to_mdc_desc(txd);
	struct mdc_chan *mchan = to_mdc_chan(txd->chan);
	unsigned long flags;
	dma_cookie_t cookie;

	spin_lock_irqsave(&mchan->lock, flags);

	cookie = dma_cookie_assign(&dma_desc->txd);
	dma_desc->status = DMA_IN_PROGRESS;
	/* Add descriptor to active list */
	list_add(&dma_desc->node, &mchan->active_desc);

	spin_unlock_irqrestore(&mchan->lock, flags);

	dev_vdbg(txd->chan->device->dev,
		"New DMA descriptor\n"
		"Address           : 0x%p\n"
		"Cookie            : 0x%08x\n"
		"Channel           : %d\n"
		"Callback function : 0x%p\n"
		"Callback parameter: 0x%p\n",
		dma_desc, cookie, mchan->a_chan_nr,
		dma_desc->txd.callback,
		dma_desc->txd.callback_param);

	return cookie;
}
/*
 * map_to_mdc_width: Convert a dma engine width to the MDC one
 * @width: The dma_slave_buswidth value
 */
static enum img_dma_width map_to_mdc_width(enum dma_slave_buswidth width)
{
	/*
	 * mchan->dma_config.dst_addr uses enum dma_slave_buswidth
	 * Convert from dma_slave_buswidth to img_dma_width:
	 * DMA_SLAVE_BUSWIDTH_1_BYTE,
	 * DMA_SLAVE_BUSWIDTH_2_BYTES,
	 * DMA_SLAVE_BUSWIDTH_4_BYTES,
	 * DMA_SLAVE_BUSWIDTH_8_BYTES,
	 * to
	 * IMG_DMA_WIDTH_8,
	 * IMG_DMA_WIDTH_16,
	 * IMG_DMA_WIDTH_32,
	 * IMG_DMA_WIDTH_64,
	 * IMG_DMA_WIDTH_128,
	 */
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return IMG_DMA_WIDTH_8;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return IMG_DMA_WIDTH_16;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return IMG_DMA_WIDTH_32;
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return IMG_DMA_WIDTH_64;
	default:
		return IMG_DMA_WIDTH_128;
	}
}

/*
 * mdc_desc_init: Initialize an MDC transfer descriptor for a given channel
 * @desc: DMA descriptor
 * @mchan: DMA channel
 * @flags: Transfer flags
 *
 * Returns 0 on success or a negative number otherwise.
 */
static void mdc_desc_init(struct mdc_dma_desc *desc, struct mdc_chan *mchan,
			  unsigned long flags)
{
	dma_async_tx_descriptor_init(&desc->txd, &mchan->dchan);
	desc->txd.tx_submit = mdc_dma_tx_submit;
	desc->txd.flags = flags; /* Ignore the MDC tx flags */

	INIT_LIST_HEAD(&desc->node);
};

/*
 * mdc_prep_irq_status: Enable/Disable an IRQ for an MDC channel
 * @chan: The MDC channel
 * @flags: Transfer flags
 */
static void mdc_prep_irq_status(struct mdc_chan *chan, unsigned long flags)
{
	/* Disable/Enable (if needed) allocated IRQ for this channel */
	if (!(flags & DMA_PREP_INTERRUPT))
		chan->irq_en = 0;
	else
		chan->irq_en = 1;
}

/*
 * mdc_get_desc: Prepare a used descriptor or allocate a new one
 * @chan: The MDC channel
 * @flags: Transfer flags
 */
static struct mdc_dma_desc *mdc_dma_get_desc(struct mdc_chan *chan,
					     unsigned long flags)
{
	unsigned long irq_flags;
	struct mdc_dma_desc *desc;

	/* Find a suitable descriptor */
	spin_lock_irqsave(&chan->lock, irq_flags);
	list_for_each_entry(desc, &chan->free_desc, node) {
		if (async_tx_test_ack(&desc->txd)) {
			/* Found one. Delete it from the list */
			list_del(&desc->node);
			spin_unlock_irqrestore(&chan->lock, irq_flags);
			desc->txd.flags = flags;
			return desc;
		}
	}
	/* We couldn't find a suitable descriptor */
	spin_unlock_irqrestore(&chan->lock, irq_flags);
	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		dev_err(mchan2dev(chan),
			"Failed to allocate DMA descriptor\n");
		return NULL;
	}

	mdc_desc_init(desc, chan, flags);

	return desc;
}
/*
 * alloc_thead_for_chan: Allocate a suitable thread for a given DMA channel
 * @mchan: The MDC DMA channel
 * @type: The type of thread to be allocated for this channel
 */
static void alloc_thread_for_chan(struct mdc_chan *mchan,
				  enum mdc_dma_thread_type type)
{
	struct mdc_dmadev *mdma = mchan->mdma;
	int total_threads = mdma->config.dma_threads;

	dev_vdbg(mdma->dma_slave.dev,
		 "Requested thread type %d for channel %d\n",
		 type, mchan->a_chan_nr);

	spin_lock(&mdma->lock);
	if (type == MDC_THREAD_FAST) {
		mdma->last_fthread = ++mdma->last_fthread &
			(total_threads / 2 - 1);
		mchan->thread = total_threads / 2 + mdma->last_fthread;
	} else if (type == MDC_THREAD_SLOW) {
		mdma->last_sthread = ++mdma->last_sthread &
			(total_threads / 2 - 1);
		mchan->thread = total_threads % 2 + mdma->last_sthread;
	} else {
		mdma->last_sthread = ++mdma->last_sthread &
			(total_threads / 2 - 1);
		mchan->thread = total_threads % 2 + mdma->last_sthread;
		dev_warn(mdma->dma_slave.dev,
			 "Caller did not use a valid thread_type\n"
			 "Defaulting to MDC_THREAD_SLOW\n");
	}
	spin_unlock(&mdma->lock);
}

/*
 * parse_dma_chan_flags: Configure channel based on the MDC specific xfer flags
 * @chan: The dmaengine channel
 */
static void parse_dma_chan_flags(struct dma_chan *chan)
{
	struct mdc_dma_tx_control *tx_control;
	struct mdc_chan *mchan = to_mdc_chan(chan);
	if (chan->private) {
		tx_control = (struct mdc_dma_tx_control *)chan->private;

		if (tx_control->flags & MDC_PRIORITY)
			mchan->priority = tx_control->prio;

		if (tx_control->flags & MDC_ACCESS_DELAY)
			mchan->access_delay = tx_control->access_delay;

		if (tx_control->flags & MDC_NO_CALLBACK)
			mchan->skip_callback = true;

		if (tx_control->flags & MDC_NEED_THREAD)
			alloc_thread_for_chan(mchan, tx_control->thread_type);
	}
}

/*
 * mdc_prep_memcpy: Prepare a descriptor for memory to memory transfers
 * @chan: DMA channel
 * @dst: Destination buffer
 * @src: Source buffer
 * @len: Total bytes to transfer
 * @flags: DMA xfer flags
 */
static struct dma_async_tx_descriptor *mdc_prep_memcpy(struct dma_chan *chan,
						       dma_addr_t dst,
						       dma_addr_t src,
						       size_t len,
						       unsigned long flags)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma_desc *mdesc;
	unsigned long irq_flags;
	int width;
	u32 genconf, rpconf;

	mdc_prep_irq_status(mchan, flags);

	mdesc = mdc_dma_get_desc(mchan, flags);

	if (!mdesc)
		return NULL;

	mchan->is_list = false;
	mchan->cyclic = false;
	/* tx defaults for tx_status. single transfer */
	mdesc->sample_count = 0;
	mdesc->sample_size = 1;
	mdesc->total_samples = mdesc->buffer_size = 1;

	parse_dma_chan_flags(chan);

	img_dma_reset(mchan);

	spin_lock_irqsave(&mchan->lock, irq_flags);

	genconf = MDC_RGET_GENERAL_CONFIG(
			(unsigned long)mchan->mdma->base_addr,
			mchan->a_chan_nr);

	rpconf = MDC_RGET_READ_PORT_CONFIG(
			(unsigned long)mchan->mdma->base_addr,
			mchan->a_chan_nr);

	/* Prepare src */
	width = check_widths(mchan->mdma, src);
	MDC_RSET_READ_ADDRESS((unsigned long)mchan->mdma->base_addr,
			      mchan->a_chan_nr, src);
	MDC_SET_FIELD(genconf, MDC_WIDTH_R, width);
	MDC_SET_FIELD(genconf, MDC_INC_R, 1);
	/* Prepare dst */
	width = check_widths(mchan->mdma, dst);
	MDC_RSET_WRITE_ADDRESS((unsigned long)mchan->mdma->base_addr,
			       mchan->a_chan_nr, dst);
	MDC_SET_FIELD(genconf, MDC_WIDTH_W, width);
	MDC_SET_FIELD(genconf, MDC_INC_W, 1);
	MDC_SET_FIELD(rpconf, MDC_DREQ_ENABLE, 0);

	/* Set priority */
	MDC_SET_FIELD(rpconf, MDC_PRIORITY, mchan->priority);

	MDC_RSET_GENERAL_CONFIG((unsigned long)mchan->mdma->base_addr,
				mchan->a_chan_nr, genconf);

	MDC_RSET_READ_PORT_CONFIG((unsigned long)mchan->mdma->base_addr,
				  mchan->a_chan_nr, rpconf);
	MDC_RSET_TRANSFER_SIZE((unsigned long)mchan->mdma->base_addr,
			       mchan->a_chan_nr, len - 1);
	wmb();

	spin_unlock_irqrestore(&mchan->lock, irq_flags);

	return &mdesc->txd;
}

/*
 * mdc_prep_dma_cyclic: Prepare a descriptor channel for cyclic transfer
 * @chan: DMA channel
 * @buf_addr: Source buffer
 * @buf_len: Total bytes to transfer
 * @direction: Transfer direction
 * @flags: Transfer flags
 * @context: context
 */
static struct dma_async_tx_descriptor *mdc_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags, void *context)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma_desc *mdesc;
	struct img_dma_mdc_list *dma_desc;
	dma_addr_t list_base, next_list;
	int width;

	if (!buf_len && !period_len) {
		dev_err(mchan2dev(mchan), "Invalid buffer/period len\n");
		return NULL;
	}


	mdc_prep_irq_status(mchan, flags);

	mdesc = mdc_dma_get_desc(mchan, flags);

	if (!mdesc)
		return NULL;

	mdesc->sample_count = 0;
	mdesc->total_samples = 0;
	mdesc->sample_size = period_len;
	mdesc->buffer_size = buf_len;
	mchan->is_list = true;
	mchan->cyclic = true;

	parse_dma_chan_flags(chan);

	dev_vdbg(mchan2dev(mchan), "DMA cyclic xfer setup:\n"
		 "Peripheral dev  : %d\n"
		 "DMA channel     : %d\n"
		 "Buffer size     : %zu\n"
		 "Period size     : %zu\n"
		 "Direction       : %d\n"
		 "Flags           : %lu\n"
		 "Thread          : %d\n"
		 "DMA Buffer(bus) : 0x%08llx\n",
		 mchan->periph, mchan->a_chan_nr, buf_len,
		 period_len, direction, flags, mchan->thread,
		 (u64)buf_addr);

	img_dma_reset(mchan);
	/* This is for the MDC linked-list */
	dma_desc = (struct img_dma_mdc_list *)mchan->virt_addr;
	mdesc->start_list = list_base = next_list = mchan->dma_addr;

	/* Hand back the DMA buffer to the CPU */
	dma_sync_single_for_cpu(mchan->mdma->dma_slave.dev,
				mchan->dma_addr,
				PAGE_SIZE, DMA_BIDIRECTIONAL);
	width = check_widths(mchan->mdma, buf_addr);

	do {
		next_list += sizeof(struct img_dma_mdc_list);

		dma_desc->gen_conf = 0xB00000AA /* 2 byte width */
			| ((mchan->a_chan_nr & 0x3f) << 20);

		dma_desc->readport_conf = 0x00000002;
		MDC_SET_FIELD(dma_desc->readport_conf, MDC_PRIORITY,
			      mchan->priority);

		if (direction == DMA_MEM_TO_DEV) {
			dma_desc->gen_conf |= _MDC_INC_R_MASK;
			dma_desc->read_addr = buf_addr;
			dma_desc->write_addr = mchan->dma_config.dst_addr;
			dma_desc->readport_conf |=
				(mchan->dma_config.dst_maxburst
				 & 0xFF) << 4;
		} else {
			dma_desc->gen_conf |= _MDC_INC_W_MASK;
			dma_desc->read_addr = mchan->dma_config.src_addr;
			dma_desc->write_addr = buf_addr;
			dma_desc->readport_conf |=
				(mchan->dma_config.src_maxburst
				 & 0xFF) << 4;
		}
		dma_desc->xfer_size = period_len - 1;
		dma_desc->node_addr = next_list;
		dma_desc->cmds_done = 0;
		dma_desc->ctrl_status = 0x11;

		if (period_len > buf_len)
			period_len = buf_len;

		dma_desc++;
		buf_addr += period_len;
		mdesc->total_samples++;
	} while (buf_len -= period_len);
	/* Point back to the first item so we can get an infinite loop */
	dma_desc[-1].node_addr = list_base;


	/* we are done with the DMA buffer, give it back to the device */
	dma_sync_single_for_device(mchan->mdma->dma_slave.dev,
				   mchan->dma_addr,
				   PAGE_SIZE,
				   DMA_BIDIRECTIONAL);

	return &mdesc->txd;
}

/*
 * mdc_prep_slave_sg: Prepare a descriptor for sg transfer
 * @chan: DMA channel
 * @sgl: Scattergather list to transfer
 * @sg_len: Size of the scattergather list
 * @direction: Transfer direction
 * @flags: Transfer flags
 * @context: context
 */
static struct dma_async_tx_descriptor *mdc_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction direction,
	unsigned long flags, void *context)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dmadev *mdma = mchan->mdma;
	struct mdc_dma_desc *mdesc = NULL;
	struct device *dev = chan->device->dev;
	struct scatterlist *sg;
	struct img_dma_mdc_list *desc_list;
	dma_addr_t list_base, next_list, addr;
	int i, width, temp, burst_size_min, burst_size, req_width;
	u32 len;

	if (unlikely(!sg_len || !sgl || !mchan))
		return NULL;


	mdc_prep_irq_status(mchan, flags);

	mdesc = mdc_dma_get_desc(mchan, flags);

	if (!mdesc)
		return NULL;

	mchan->is_list = true;
	mchan->sg = true;
	mdesc->sample_count = 0;
	mdesc->sample_size = 1; /* single list item */
	mdesc->total_samples = mdesc->buffer_size = sg_len;

	parse_dma_chan_flags(chan);

	dev_vdbg(dev, "DMA xfer setup:\n"
		 "Peripheral dev  : %d\n"
		 "DMA channel     : %d\n"
		 "sg list         : 0x%p\n"
		 "sg list length  : %d\n"
		 "Direction       : %d\n"
		 "Priority        : %d\n"
		 "Thread          : %d\n"
		 "Access Delay    : %d\n"
		 "DMA Buffer      : 0x%p\n"
		 "DMA Buffer(bus) : 0x%08llx\n",
		 mchan->periph, mchan->a_chan_nr, sgl,
		 sg_len, direction,
		 mchan->priority, mchan->thread,
		 mchan->access_delay,
		 (u64 *)mchan->virt_addr,
		 (u64)mchan->dma_addr);

	img_dma_reset(mchan);

	/* This is for the MDC linked-list */
	desc_list = (struct img_dma_mdc_list *)mchan->virt_addr;
	mdesc->start_list = list_base = next_list = mchan->dma_addr;

	/* Hand back the DMA buffer to the CPU */
	dma_sync_single_for_cpu(mchan->mdma->dma_slave.dev,
				mchan->dma_addr,
				PAGE_SIZE, DMA_BIDIRECTIONAL);

	burst_size_min = burst_size_lookup[mdma->config.bus_width & 0x7];

	for_each_sg(sgl, sg, sg_len, i) {
		/*
		 * Each list item is a 32-byte packet represented by the
		 * img_dma_mdc_list struct. Every member of that struct
		 * corresponds to the channel register
		 */
		next_list += sizeof(struct img_dma_mdc_list);
		len = sg_dma_len(sg);
		addr = sg_dma_address(sg);
		width = check_widths(mchan->mdma, addr);
		desc_list->gen_conf = 0x30000088
			| ((mchan->a_chan_nr & 0x3f) << 20)
			| ((mchan->access_delay & 0x7) << 16);

		temp = (mchan->thread & 0xf);
		desc_list->readport_conf = 0x00000002 | temp << 2
			| temp << 24 | temp << 16;

		MDC_SET_FIELD(desc_list->readport_conf, MDC_PRIORITY,
			      mchan->priority);

		if (direction == DMA_MEM_TO_DEV) {
			MDC_SET_FIELD(desc_list->gen_conf, MDC_INC_R, 1);
			MDC_SET_FIELD(desc_list->gen_conf, MDC_WIDTH_R, width);
			req_width = mchan->dma_config.dst_addr_width;
			MDC_SET_FIELD(desc_list->gen_conf, MDC_WIDTH_W,
				      map_to_mdc_width(req_width));
			desc_list->read_addr = addr;
			desc_list->write_addr = mchan->dma_config.dst_addr;
			burst_size = mchan->dma_config.dst_maxburst;
			desc_list->readport_conf |=
				(burst_size < burst_size_min)
				? (burst_size_min - 1) << 4
				: (burst_size - 1) << 4;
		} else {
			MDC_SET_FIELD(desc_list->gen_conf, MDC_INC_W, 1);
			MDC_SET_FIELD(desc_list->gen_conf, MDC_WIDTH_W, width);
			req_width = mchan->dma_config.src_addr_width;
			MDC_SET_FIELD(desc_list->gen_conf, MDC_WIDTH_R,
				      map_to_mdc_width(req_width));
			desc_list->read_addr = mchan->dma_config.src_addr;
			desc_list->write_addr = addr;
			burst_size = mchan->dma_config.src_maxburst;
			desc_list->readport_conf |=
				(burst_size < burst_size_min)
				? (burst_size_min - 1) << 4
				: (burst_size - 1) << 4;
		}

		desc_list->xfer_size = len - 1;
		desc_list->node_addr = next_list;
		desc_list->cmds_done = 0;
		desc_list->ctrl_status = 0x11;

		desc_list++;
	}

	desc_list[-1].node_addr = 0;

	/* we are done with the DMA buffer, give it back to the device */
	dma_sync_single_for_device(mchan->mdma->dma_slave.dev,
				   mchan->dma_addr,
				   PAGE_SIZE,
				   DMA_BIDIRECTIONAL);

	return &mdesc->txd;
}

/*
 * mdc_alloc_chan_resources: Allocate resources for an MDC channel
 * @chan: The MDC channel
 */
static int mdc_alloc_chan_resources(struct dma_chan *chan)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dmadev *mdma = mchan->mdma;
	struct device *dev = chan->device->dev;
	int total_threads = mdma->config.dma_threads;
	int ret;

	mchan->cyclic = false;
	mchan->sg = false;
	mchan->is_list = false;
	mchan->finished = false;
	mchan->irq_en = 1;
	mchan->priority = 0; /* Assume bulk priority */
	mchan->access_delay = 0; /* Assume fast peripheral */
	mchan->skip_callback = false;
	/* Clear private data from previous allocations */
	mchan->dchan.private = NULL;
	/* Defaults to slow threads */
	spin_lock(&mdma->lock);
	mdma->last_sthread = ++mdma->last_sthread % (total_threads / 2);
	mchan->thread = (total_threads % 2) + mdma->last_sthread;
	spin_unlock(&mdma->lock);

	BUG_ON(!mdma->callbacks->allocate);

	ret = mdma->callbacks->allocate(mchan->a_chan_nr, mchan->periph);

	if (ret < 0) {
		dev_err(dev,
			"Failed to allocate channel %d for device %u with err=%d\n",
			mchan->a_chan_nr, mchan->periph, ret);
		return ret;
	}

	mchan->alloc_status = IMG_DMA_CHANNEL_INUSE;

	dma_cookie_init(&mchan->dchan);

	dev_dbg(dev, "DMA channel %d allocated for peripheral device %u\n",
		 mchan->a_chan_nr, mchan->periph);

	/* Reset channel before we request an IRQ for it */

	img_dma_reset(mchan);

	/* Allocate an IRQ for this channel */

	mchan->irq = platform_get_irq(to_platform_device(dev),
				      mchan->a_chan_nr);

	ret = request_irq(mchan->irq, mdc_handler_isr, 0,
			  mchan->name, mchan);

	if (ret) {
		dev_err(dev,
			"Failed to allocate IRQ %d for channel %d\n",
			mchan->irq, mchan->a_chan_nr);
		return -ENXIO;
	}

	dev_dbg(dev,
		"IRQ %d (%s) allocated for channel %d\n",
		mchan->irq, mchan->name, mchan->a_chan_nr);

	/*
	 * We need to allocate a DMA buffer for the MDC linked-list
	 * operation
	 */
	mchan->virt_addr = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!mchan->virt_addr) {
		dev_err(mchan->mdma->dma_slave.dev,
			"Failed to allocate memory for channel %d\n",
			mchan->a_chan_nr);
		ret = -ENOMEM;
		goto free_irq;
	}
	/*
	 * Since we don't know the direction yet, map using
	 * DMA_BIDRECTIONAL to cover both cases
	 */
	mchan->dma_addr = dma_map_single(mchan->mdma->dma_slave.dev,
					     mchan->virt_addr,
					     PAGE_SIZE,
					     DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dev, mchan->dma_addr))
		goto free_buf;

	return 0;

free_irq:
	free_irq(mchan->irq, mchan);
free_buf:
	kfree(mchan->virt_addr);

	return ret;
}

/*
 * mdc_free_chan_resources: Free resources for an MDC channel
 * @chan: The MDC DMA channel
 */
static void mdc_free_chan_resources(struct dma_chan *chan)
{
	struct mdc_chan *dchan = to_mdc_chan(chan);
	struct mdc_dmadev *mdma = dchan->mdma;
	struct device *dev = chan->device->dev;
	int ret;

	/* Stop transfers and remove descriptors */
	mdc_terminate_all(chan);

	BUG_ON(!mdma->callbacks->free);

	ret = mdma->callbacks->free(dchan->a_chan_nr);
	if (ret < 0) {
		dev_err(dev,
			"Failed to unregister channel %d for device %u\n",
			dchan->a_chan_nr, dchan->periph);
	} else {
		dchan->alloc_status = IMG_DMA_CHANNEL_AVAILABLE;
		free_irq(dchan->irq, dchan);

		dma_unmap_single(dchan->mdma->dma_slave.dev,
				 dchan->dma_addr, PAGE_SIZE,
				 DMA_BIDIRECTIONAL);
		kfree(dchan->virt_addr);

		dchan->periph = 0;

		dev_vdbg(dev,
			 "DMA channel %d for device %u deallocated\n",
			 dchan->a_chan_nr, dchan->periph);
	}
}

/*
 * slave_check_width: Check the slave bus width or default to a good one
 * @chan: The MDC DMA channel
 * @req_width: Requested width for transfer
 */
static int slave_check_width(struct mdc_chan *chan, int req_width)
{
	if (chan->mdma->config.bus_width < req_width) {
		dev_err(chan->mdma->dma_slave.dev,
			"Invalid transfer width\n"
			"System    : %d\n"
			"Requested : %d\n",
			chan->mdma->config.bus_width,
			req_width);
		return chan->mdma->config.bus_width;
	} else {
		return req_width;
	}
}

/*
 * mdc_tx_status: Get DMA status for a given cookie
 * @chan: The MDC DMA channel
 * @cookie: Transfer cookie
 * @txstate: Struct containing the cookie status
 */
static enum dma_status mdc_tx_status(struct dma_chan *chan,
			 dma_cookie_t cookie,
			 struct dma_tx_state *txstate)
{

	struct mdc_chan *mchan = to_mdc_chan(chan);
	int dma_status, ret;
	int dma_retry = 0;
	int total_xfered, residue;
	unsigned long flags;
	struct mdc_dma_desc *desc, *safe;
	struct list_head *root = &mchan->active_desc;

	ret = dma_cookie_status(chan, cookie, txstate);

	if (ret == DMA_SUCCESS) {
		dma_set_residue(txstate, 0);
		return ret;
	}

	if (!mchan->irq_en)
		mchan->finished = true;

	if (!mchan->cyclic && mchan->finished) {
		do {
			dma_status = MDC_REG_IS_BUSY((unsigned long)
						     mchan->mdma->base_addr,
						     mchan->a_chan_nr);
			if (!dma_status)
				break;
			if (++dma_retry > MAX_MDC_DMA_BUSY_RETRY)
				return DMA_IN_PROGRESS;
		} while (1);
	}

	if (mchan->finished) {
		/*
		 * For cyclic or disabled irqs, we will
		 * look in the active list
		 */
		root = (mchan->irq_en && !mchan->cyclic) ?
			&mchan->free_desc : root;
		dma_status = DMA_SUCCESS;
	} else {
		dma_status = DMA_IN_PROGRESS;
	}

	spin_lock_irqsave(&mchan->lock, flags);
	list_for_each_entry_safe(desc, safe, root, node) {
		if (desc->txd.cookie == cookie) {
			if (mchan->finished) {
				dma_set_residue(txstate, 0);
				if (!mchan->cyclic)
					dma_cookie_complete(&desc->txd);
				/*
				 * If IRQ is disabled, we need to move the
				 * descriptor to the free list and ack it now
				 */
				if (!mchan->irq_en) {
					async_tx_ack(&desc->txd);
					list_move_tail(&desc->node,
						       &mchan->free_desc);
				}
			} else {
				total_xfered = desc->sample_count *
					desc->sample_size;
				residue = desc->buffer_size - total_xfered;
				dma_set_residue(txstate, residue);
			}
		}
	}

	/* Reset status */
	mchan->finished = false;

	spin_unlock_irqrestore(&mchan->lock, flags);

	return dma_status;
}

/*
 * mdc_slave_config: Configure slave config for a DMA channel
 * @mchan: The MDC DMA channel
 * @config: The slave configuration passed by the caller
 */
static int mdc_slave_config(struct mdc_chan *mchan,
			    struct dma_slave_config *config)
{

	if (config->direction == DMA_MEM_TO_DEV) {
		config->dst_addr_width = slave_check_width(mchan,
						   config->dst_addr_width);
	} else if (config->direction == DMA_DEV_TO_MEM) {
		config->src_addr_width = slave_check_width(mchan,
						   config->src_addr_width);
	} else {
		dev_err(mchan->mdma->dma_slave.dev,
			"Unsupported slave direction\n");
		/*
		 * The caller needs to be fixed
		 */
		BUG();
		return -1;
	}

	/* Copy the rest of the slave config */
	memcpy(&mchan->dma_config, config, sizeof(*config));

	return 0;
}

/*
 * mdc_terminate_all: Stop transfers and free lists
 * @chan: The MDC DMA channel
 */
static int mdc_terminate_all(struct dma_chan *chan)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma_desc *desc, *safe;
	unsigned long flags;

	/* Remove all descriptors */
	spin_lock_irqsave(&mchan->lock, flags);

	MDC_CANCEL((unsigned long)mchan->mdma->base_addr,
			   mchan->a_chan_nr);
	wmb();

	/* Safe removal of list items */
	list_for_each_entry_safe(desc, safe, &mchan->free_desc, node) {
		list_del(&desc->node);
		kfree(desc);
	}
	/* Safe removal of list items */
	list_for_each_entry_safe(desc, safe, &mchan->active_desc, node) {
		list_del(&desc->node);
		kfree(desc);
	}

	/* Reset cookie for this channel */
	dma_cookie_init(chan);

	spin_unlock_irqrestore(&mchan->lock, flags);

	return 0;
}

/*
 * mdc_control: Control cmds for the DMA channel
 * @chan: DMA channel
 * @cmd: Command (as passed by the dmaengine infrastracture)
 * @arg: Opaque data. Can be anything depending on the cmd argument
 */
static int mdc_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
		       unsigned long arg)
{
	int ret = 0;
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct dma_slave_config *config = NULL;

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		return mdc_terminate_all(chan);
	case DMA_SLAVE_CONFIG:
		config = (struct dma_slave_config *)arg;
		ret = mdc_slave_config(mchan, config);
		break;
	default:
	case DMA_PAUSE:
	case DMA_RESUME:
		ret = -ENOSYS;
	}

	return ret;
}

/*
 * mdc_issue_pending: Make the actual transfer
 * @chan: The MDC DMA channel
 */
static void mdc_issue_pending(struct dma_chan *chan)
{
	struct mdc_chan *mchan = to_mdc_chan(chan);
	struct mdc_dma_desc *desc = NULL;
	unsigned long flags;

	spin_lock_irqsave(&mchan->lock, flags);

	/* Make sure the xfer list is not empty */
	if (list_empty(&mchan->active_desc)) {
		spin_unlock_irqrestore(&mchan->lock, flags);
		return;
	}

	/* Fetch first descriptor */
	desc = list_first_entry(&mchan->active_desc,
				typeof(*desc), node);

	spin_unlock_irqrestore(&mchan->lock, flags);

	/* Make the transfer */
	if (mchan->is_list) {
		MDC_RSET_LIST_NODE_ADDR((unsigned long)mchan->mdma->base_addr,
					mchan->a_chan_nr, desc->start_list);
		wmb();
		MDC_LIST_ENABLE((unsigned long)mchan->mdma->base_addr,
				mchan->a_chan_nr);
		dev_dbg(mchan->mdma->dma_slave.dev,
			"Starting list transfer for channel %d\n",
			mchan->a_chan_nr);
	} else { /* Single shot */
		MDC_REG_ENABLE((unsigned long)mchan->mdma->base_addr,
		       mchan->a_chan_nr);
		dev_dbg(mchan->mdma->dma_slave.dev,
			"Starting single transfer for channel %d\n",
			mchan->a_chan_nr);

	}
}

/*
 * mdc_dma_init: Initialize the dma_device structure.
 * @dma: The dma_device structure to initialize.
 * @dev: Device where the 'dma' structure belongs to.
 */
static void mdc_dma_init(struct mdc_dmadev *mdma, struct device *dev)
{
	mdma->dma_slave.chancnt = MAX_MDC_DMA_CHANNELS;
	mdma->dma_slave.device_prep_slave_sg = mdc_prep_slave_sg;
	mdma->dma_slave.device_prep_dma_cyclic = mdc_prep_dma_cyclic;
	mdma->dma_slave.device_prep_dma_memcpy = mdc_prep_memcpy;
	mdma->dma_slave.device_alloc_chan_resources = mdc_alloc_chan_resources;
	mdma->dma_slave.device_free_chan_resources = mdc_free_chan_resources;
	mdma->dma_slave.device_tx_status = mdc_tx_status;
	mdma->dma_slave.device_issue_pending = mdc_issue_pending;
	mdma->dma_slave.device_control = mdc_control;
	mdma->dma_slave.dev = dev;

	INIT_LIST_HEAD(&mdma->dma_slave.channels);
}

/*
 * mdc_get_current_config: Get current DMA configuration.
 * @mdma: MDC DMA device
 */
static int __init mdc_get_current_config(struct mdc_dmadev *mdma)
{
	unsigned long mdc_base_address = (unsigned long)mdma->base_addr;
	mdma->config.dma_channels = _MDC_READ_GLOBAL_REG_FIELD(mdc_base_address,
							MDC_NUM_CONTEXTS);
	mdma->config.dma_threads = 1 << _MDC_READ_GLOBAL_REG_FIELD(
							mdc_base_address,
							MDC_THREADID_WIDTH);
	mdma->config.bus_width = _MDC_READ_GLOBAL_REG_FIELD(mdc_base_address,
							MDC_SYS_DATA_WIDTH);

	if (!(mdma->config.bus_width || mdma->config.dma_channels))
		return -1;

	mdma->last_fthread = mdma->config.dma_threads / 2;
	mdma->last_sthread = mdma->config.dma_threads % 2;

	return 0;
}

/*
 * mdc_chan_init: Initialize all DMA channels
 * @mdma: MDC DMA device
 * @mchan: Array of DMA channels for this device
 */
static void __init mdc_chan_init(struct mdc_dmadev *mdma,
				 struct mdc_chan *mchan)
{
	int i;
	for (i = 0; i < MAX_MDC_DMA_CHANNELS; i++) {
		struct mdc_chan *mdc_chan = &mchan[i];
		mdc_chan->mdma = mdma;
		mdc_chan->dchan.device = &mdma->dma_slave;
		mdc_chan->a_chan_nr = i;
		mdc_chan->periph = 0;
		if (i < mdma->config.dma_channels)
			mdma->slave_channel[i].alloc_status =
				IMG_DMA_CHANNEL_AVAILABLE;
		else
			mdma->slave_channel[i].alloc_status =
				IMG_DMA_CHANNEL_RESERVED;
		snprintf(mdc_chan->name, sizeof(mdc_chan->name), "mdc-chan-%d",
			mdc_chan->a_chan_nr);
		/* init tasklet for this channel */
		tasklet_init(&mdc_chan->tasklet, mdc_dma_tasklet,
			     (unsigned long)mdc_chan);

		/* init the list of descriptors for this channel */
		INIT_LIST_HEAD(&mdc_chan->active_desc);
		INIT_LIST_HEAD(&mdc_chan->free_desc);

		/* Add channel to the DMA channel linked-list */
		list_add_tail(&mdc_chan->dchan.device_node,
			      &mdma->dma_slave.channels);
	}
}

int mdc_dma_probe(struct platform_device *pdev,
		  const struct img_mdc_soc_callbacks *callbacks)
{
	struct device *dev = &pdev->dev;
	struct mdc_dmadev *mdma;
	struct resource *mem_resource;
	int status = 0;

	/* Are we using a wrapper to initialize this driver? */
	spin_lock(&mdc_dma_lock);
	if (pdev->dev.driver != &img_mdc_dma_driver.driver)
		wrapper_driver = pdev->dev.driver;
	spin_unlock(&mdc_dma_lock);

	if (!pdev->dev.of_node)
		return -ENOENT;

	mdma = devm_kzalloc(dev, sizeof(*mdma), GFP_KERNEL);
	if (!mdma) {
		dev_err(dev, "Can't allocate controller\n");
		return -ENOMEM;
	}

	mem_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdma->base_addr = devm_request_and_ioremap(dev, mem_resource);
	if (!mdma->base_addr) {
		dev_err(dev, "unable to ioremap registers\n");
		status = -ENOMEM;
		goto out;
	}

	/*
	 * Set DMA controller capabilities.
	 * The controller can do DEV <-> MEM and MEM <-> MEM transfers.
	 */
	dma_cap_zero(mdma->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, mdma->dma_slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, mdma->dma_slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, mdma->dma_slave.cap_mask);

	/* Set callbacks */
	mdc_dma_init(mdma, dev);

	/*
	 * Set SoC callbacks. It's very unlikely
	 * for the driver to work without SoC specific
	 * alloc/free callbacks
	 */
	if (wrapper_driver)
		BUG_ON(!callbacks);

	mdma->callbacks = callbacks;

	/* Get configuration */
	if (mdc_get_current_config(mdma)) {
		status = -EINVAL;
		goto out;
	}

	/* Initialize channels */
	mdc_chan_init(mdma, mdma->slave_channel);

	/* Register the device */
	status = dma_async_device_register(&mdma->dma_slave);

	if (status)
		goto out;

	platform_set_drvdata(pdev, mdma);

	dev_dbg(dev, "MDC DMA hardware supports %d channels and %d threads\n",
		mdma->config.dma_channels,
		mdma->config.dma_threads);

	status = of_dma_controller_register(pdev->dev.of_node,
					    of_dma_mdc_xlate, mdma);

	return 0;

out:
	kfree(mdma);
	return status;
}
EXPORT_SYMBOL_GPL(mdc_dma_probe);

static int mdc_probe(struct platform_device *pdev)
{
	return mdc_dma_probe(pdev, NULL);
}

/* stop hardware and remove the driver */
static int mdc_remove(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
	return 0;
}

static const struct of_device_id mdc_dma_id[] = {
		{ .compatible = "img,mdc-dma" },
		{},
};
MODULE_DEVICE_TABLE(of, mdc_dma_id);

#ifdef CONFIG_PM_SLEEP

static int img_mdc_dma_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdc_dmadev *mdma = platform_get_drvdata(pdev);

	if (mdma->callbacks->suspend)
		mdma->pm_data = mdma->callbacks->suspend();

	return 0;
}

static int img_mdc_dma_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdc_dmadev *mdma = platform_get_drvdata(pdev);

	if (mdma->callbacks->resume)
		mdma->callbacks->resume(mdma->pm_data);
	return 0;
}
#else
#define img_mdc_dma_suspend NULL
#define img_mdc_dma_resume NULL
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops img_mdc_dma_pm_ops = {
	.suspend_noirq = img_mdc_dma_suspend,
	.resume_noirq = img_mdc_dma_resume,
};

static struct platform_driver img_mdc_dma_driver = {
	.driver	= {
		.name	= "img-mdc-dma",
		.owner	= THIS_MODULE,
		.pm	= &img_mdc_dma_pm_ops,
		.of_match_table = mdc_dma_id,
	},
	.remove		= mdc_remove,
};

static int __init mdc_init(void)
{
	return platform_driver_probe(&img_mdc_dma_driver, mdc_probe);
}
subsys_initcall(mdc_init);


static void mdc_exit(void)
{
	platform_driver_unregister(&img_mdc_dma_driver);
}
module_exit(mdc_exit);

MODULE_ALIAS("img-mdc-dma");	/* for platform bus hotplug */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Imagination Technologies LTD.");
MODULE_DESCRIPTION("IMG - MDC DMA Controller");
