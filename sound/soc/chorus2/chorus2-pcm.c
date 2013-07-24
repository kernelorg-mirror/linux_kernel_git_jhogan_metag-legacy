/*
 * chorus2-pcm.c
 *
 * ALSA PCM interface for the Frontier Silicon Chorus2 chip
 *
 * Copyright:	(C) 2009 Imagination Technologies
 *
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <asm/img_dma.h>
#include <asm/img_dmac.h>
#include <asm/soc-chorus2/dma.h>

#include "chorus2-pcm.h"

#define BURST_SIZE 2
#define DMA_LIST_COUNT_REG 0x02001044


static const struct snd_pcm_hardware chorus2_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE,

	.channels_min		= 2,
	.channels_max		= 2,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= PAGE_SIZE/sizeof(struct img_dmac_desc),
	.buffer_bytes_max	= 128 * 1024,
};

static int __chorus2_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct chorus2_runtime_data *crtd = runtime->private_data;
	struct img_dmac_desc *dma_desc;
	dma_addr_t dma_buff_phys, next_desc_phys;
	u32 dma_perip_addr = (crtd->params->peripheral_address & 0xFFFFFF) >> 2;

	/*total buffer size*/
	size_t totsize = params_buffer_bytes(params);
	/*amount to transfer between ints (ie size per each list node)*/
	size_t period = params_period_bytes(params);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = totsize;

	dma_desc = (struct img_dmac_desc *)crtd->dma_list; /*base address of dma list*/
	next_desc_phys = crtd->dma_list_phys;
	dma_buff_phys = runtime->dma_addr;



	/*fill out dma list*/
	do {
		next_desc_phys += sizeof(struct img_dmac_desc);

		dma_desc->perip_setup = DMAC_LIST_PW_32;
		dma_desc->len_ints = DMAC_LIST_INT_BIT | (period/4);
		dma_desc->perip_address = dma_perip_addr;
		dma_desc->burst =  (BURST_SIZE << DMAC_LIST_BURST_S) |
				   (0 << DMAC_LIST_ACC_DELAY_S);
		dma_desc->twod = 0;
		dma_desc->twod_addr = 0;
		BUG_ON(dma_buff_phys & 0x7);
		dma_desc->data_addr = dma_buff_phys;
		dma_desc->next = next_desc_phys;

		if (period > totsize)
			period = totsize;

		dma_desc++;
		dma_buff_phys += period;
	} while (totsize -= period);

	/*point the last list node back to the start to get an infinite loop*/
	dma_desc[-1].next = crtd->dma_list_phys;

	/*setup list base pointer*/
	img_dma_set_list_addr(crtd->dma_ch, crtd->dma_list_phys);

	return 0;
}


static int chorus2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct chorus2_runtime_data *crtd = substream->runtime->private_data;
	int ret = 0;

	unsigned long flags;

	spin_lock_irqsave(&crtd->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Reset DMA list */
		img_dma_set_list_addr(crtd->dma_ch, crtd->dma_list_phys);
		crtd->period_index = 0;
		crtd->start_delay = 1;
		/* Start DMA */
		img_dma_start_list(crtd->dma_ch);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/*Stop DMA*/
		img_dma_stop_list(crtd->dma_ch);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
		img_dma_start_list(crtd->dma_ch);
		/* Restart DMA*/
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/*Reset DMA list*/
		/*Restart DMA*/
		img_dma_start_list(crtd->dma_ch);
		break;

	default:
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&crtd->lock, flags);

	return ret;
}

/*
 *  This callback is called when the PCM middle layer inquires the current
 *  hardware position on the buffer. The position must be returned in frames,
 *  ranging from 0 to buffer_size - 1. This is called usually from the
 *  buffer-update routine in the pcm middle layer, which is invoked when
 *  snd_pcm_period_elapsed() is called in the interrupt routine. Then the pcm
 *  middle layer updates the position and calculates the available space, and
 *  wakes up the sleeping poll threads, etc.
 */
static snd_pcm_uframes_t
chorus2_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct chorus2_runtime_data *crtd = runtime->private_data;
	snd_pcm_uframes_t offset;
	size_t base_offset;

	/*
	 * calculate the offset based on which period is active
	 */
	base_offset = frames_to_bytes(runtime,
			crtd->period_index  * runtime->period_size);

	offset = bytes_to_frames(runtime, base_offset);


	return offset;
}

/*
 * This is called when the PCM is "prepared", formats + sample rates can be set
 * here, it differs to hw_params as id called whilst playing/recording used to
 * recover from underruns etc.
 *
 * NB THIS MUST BE ATOMIC
 */
static int chorus2_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}


static irqreturn_t chorus2_dma_irq(int irq_num, void *dev_id)
{
	struct snd_pcm_substream *substream = dev_id;
	struct chorus2_runtime_data *crtd = substream->runtime->private_data;
	unsigned long flags;
	u32 int_status;

	img_dma_get_int_status(crtd->dma_ch, &int_status);

	/*Sometime we get an irq with no status when stopping a dma list*/
	if (!int_status)
		return IRQ_HANDLED;

	if (int_status & (1<<20)) {
		int_status = 0;
		img_dma_set_int_status(crtd->dma_ch, int_status);

		/*
		 * note the interrupt is generated when a list node is loaded
		 * not when its finished thus we get an interrupt at the start
		 * before we have transfered anything, we must ignore this or
		 * our buffer calculation will be out.
		 */

		if (crtd->start_delay)
			crtd->start_delay = 0;
		else {
			spin_lock_irqsave(&crtd->lock, flags);
			if (crtd->period_index >= 0) {
				if (++crtd->period_index == substream->runtime->periods)
					crtd->period_index = 0;
			}
			spin_unlock_irqrestore(&crtd->lock, flags);

			/* Callback to PCM middle layer */
			snd_pcm_period_elapsed(substream);
		}
	} else {
		printk(KERN_ERR "%s: DMA error on channel %d "
			"Bad IRQ status on IRQ %d\n",
			crtd->params->name, crtd->dma_ch, irq_num);
		snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
	}


	return IRQ_HANDLED;
}


static unsigned int rates[] = {32000, 48000, 64000, 96000};

static struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
 };


/*Called on stream open */
static int chorus2_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct chorus2_runtime_data *crtd;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &chorus2_pcm_hardware);

	/*
	 * Setup any constraints / rules here,
	 */

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
						SNDRV_PCM_HW_PARAM_RATE,
						&constraints_rates);

	ret = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);

	/* allocate room for private data */
	ret = -ENOMEM;
	crtd = kzalloc(sizeof(*crtd), GFP_KERNEL);
	if (!crtd)
		goto out;

	spin_lock_init(&crtd->lock);

	/* Create mappings to DMA descriptors */
	crtd->dma_list =
		dma_alloc_coherent(substream->pcm->card->dev, PAGE_SIZE,
					&crtd->dma_list_phys, GFP_KERNEL);
	if (!crtd->dma_list)
		goto err1;


	/* Set our private data field */
	runtime->private_data = crtd;

	return 0;

 err1:
	kfree(crtd);
 out:
	return ret;
}

/*Called on stream close */
static int chorus2_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct chorus2_runtime_data *crtd = runtime->private_data;

	/*Free mapping to DMA descriptor list */
	dma_free_coherent(substream->pcm->card->dev, PAGE_SIZE,
			crtd->dma_list, crtd->dma_list_phys);
	kfree(crtd);
	return 0;
}


static int chorus2_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static int chorus2_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = chorus2_pcm_hardware.buffer_bytes_max;
	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;
	buf->bytes = size;
	return 0;
}


static void chorus2_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;
		dma_free_coherent(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);
		buf->area = NULL;
	}
}



static int initialise_dma_hw(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct chorus2_runtime_data *crtd = runtime->private_data;
	u32 dma_perip_addr = (crtd->params->peripheral_address & 0xFFFFFF);

	int chan, ret;

	chan = img_request_dma(-1, crtd->params->peripheral_num);
	if (chan < 0) {
		ret = chan;
		goto out;
	}
	crtd->dma_ch = chan;

	crtd->irq_num = img_dma_get_irq(chan) + META_IRQS;

	ret = request_irq(crtd->irq_num, chorus2_dma_irq, 0,
				"pcm-dma", substream);
	if (ret < 0)
		goto out;

	ret = chan;

	img_dma_set_io_address(crtd->dma_ch, dma_perip_addr, BURST_SIZE);


out:
	return ret;
}

/*
 * This is called when hw_params is setup by an app.
 * hence buffer sizes, period and formats should be setup, so
 * we can setup our buffers and DMA
 */
static int chorus2_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct chorus2_runtime_data *crtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct chorus2_pcm_dma_params *dma;
	int ret = 0;

	dma = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	/* return if this is a bufferless transfer */
	if (!dma)
		goto out;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	/* this may get called several times by oss emulation
	 * with different params */
	if (!crtd->params) {
		crtd->params = dma;
		ret = initialise_dma_hw(substream);
		if (ret < 0)
			goto out;
		crtd->dma_ch = ret;

	} else if (crtd->params != dma) {
		img_free_dma(crtd->dma_ch);
		crtd->params = dma;
		ret = initialise_dma_hw(substream);
		if (ret < 0)
			goto out;
		crtd->dma_ch = ret;
	}

	ret = 0;
	return __chorus2_pcm_hw_params(substream, params);
out:
	return ret;
}

/*release resources allocated in hw_params*/
static int chorus2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct chorus2_runtime_data *crtd = substream->runtime->private_data;

	img_dma_reset(crtd->dma_ch);

	snd_pcm_set_runtime_buffer(substream, NULL);

	if (crtd->dma_ch >= 0) {
		img_free_dma(crtd->dma_ch);
		crtd->dma_ch = -1;
		crtd->params = 0;
	}

	if (crtd->irq_num > 0) {
		free_irq(crtd->irq_num, substream);
		crtd->irq_num = -1;
	}

	return 0;
}

static struct snd_pcm_ops chorus2_pcm_ops = {
	.open		= chorus2_pcm_open,
	.close		= chorus2_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= chorus2_pcm_hw_params,
	.hw_free	= chorus2_pcm_hw_free,
	.prepare	= chorus2_pcm_prepare,
	.trigger	= chorus2_pcm_trigger,
	.pointer	= chorus2_pcm_pointer,
	.mmap		= chorus2_pcm_mmap,
};

static u64 chorus2_pcm_dmamask = DMA_BIT_MASK(64);

static int chorus2_soc_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &chorus2_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(64);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = chorus2_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		dev_err(card->dev, "Audio Capture (Recording) not supported");
		ret = -EINVAL;
		goto out;
	}
 out:
	return ret;
}

struct snd_soc_platform_driver chorus2_soc_platform = {
	.ops		= &chorus2_pcm_ops,
	.pcm_new	= chorus2_soc_pcm_new,
	.pcm_free	= chorus2_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(chorus2_soc_platform);

static int chorus2_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &chorus2_soc_platform);
}

static int chorus2_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver chorus2_pcm_driver = {
	.driver = {
			.name = "chorus2-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = chorus2_soc_platform_probe,
	.remove = chorus2_soc_platform_remove,
};
static int __init chorus2_soc_platform_init(void)
{
	return platform_driver_register(&chorus2_pcm_driver);
}
module_init(chorus2_soc_platform_init);

static void __exit chorus2_soc_platform_exit(void)
{
	platform_driver_unregister(&chorus2_pcm_driver);
}
module_exit(chorus2_soc_platform_exit);

MODULE_AUTHOR("Neil Jones");
MODULE_DESCRIPTION("Frontier Silicon Chorus2 PCM Module");
MODULE_LICENSE("GPL");

