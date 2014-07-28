/*
 * tz1090-pcm.c
 *
 * ALSA PCM interface for the TZ1090 SoC
 *
 * Copyright:	(C) 2010-2012 Imagination Technologies
 *
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/img_mdc_dma.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "tz1090-pcm.h"

#define BURST_SIZE 2

static const struct snd_pcm_hardware comet_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE |
				  SNDRV_PCM_FMTBIT_S24_LE,

	.channels_min		= 2,
	.channels_max		= 6,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= PAGE_SIZE/sizeof(struct img_dma_mdc_list),
	.buffer_bytes_max	= 512 * 1024,
};

/*
 * This is called when the PCM is "prepared", formats + sample rates can be set
 * here, it differs to hw_params as id called whilst playing/recording used to
 * recover from underruns etc.
 */
static int comet_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static unsigned int rates[] = {32000, 48000, 96000};

static struct snd_pcm_hw_constraint_list c_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

/*Called on stream open */
static int comet_pcm_open(struct snd_pcm_substream *substream)
{
	int ret;
	struct comet_pcm_dma_params *dma;
	struct mdc_dma_cookie *cookie;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dev = rtd->platform->dev;

	/*
	 * Setup any constraints / rules here,
	 */
	runtime->hw.rate_min = c_rates.list[0];
	runtime->hw.rate_max = c_rates.list[c_rates.count - 1];
	runtime->hw.rates = SNDRV_PCM_RATE_KNOT;

	snd_soc_set_runtime_hwparams(substream, &comet_pcm_hardware);

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &c_rates);

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	dma = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer */
	if (!dma)
		goto out;

	cookie = kzalloc(sizeof(*cookie), GFP_KERNEL);
	if (!cookie)
		return -ENOMEM;
	cookie->req_channel = dma->dma_channel;
	cookie->periph = dma->peripheral_num;

	ret = snd_dmaengine_pcm_open_request_chan(substream, &mdc_dma_filter_fn,
						  cookie);

	if (ret) {
		dev_err(dev,
			"dmaengine pcm open failed with err %d\n", ret);
		kfree(cookie);
		return ret;
	}

	kfree(cookie);

out:
	return 0;
}

/*Called on stream close */
static int comet_pcm_close(struct snd_pcm_substream *substream)
{
	snd_dmaengine_pcm_close_release_chan(substream);
	return 0;
}

static int comet_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static int comet_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = comet_pcm_hardware.buffer_bytes_max;

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

static void comet_pcm_free_dma_buffers(struct snd_pcm *pcm)
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

/*
 * An interrupt fires up in MDC when the list is loaded so we must not
 * execute the call back. Therefore we pass the MDC_NO_CALLBACK flag to
 * the channel
 */
static int comet_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct mdc_dma_tx_control tx_control = {
		.flags = MDC_NO_CALLBACK,
	};

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
	{
		struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
		chan->private = (void *)&tx_control;
		return snd_dmaengine_pcm_trigger(substream,
					SNDRV_PCM_TRIGGER_START);
	}
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		return snd_dmaengine_pcm_trigger(substream,
						 SNDRV_PCM_TRIGGER_STOP);
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * This is called when hw_params is setup by an app.
 * hence buffer sizes, period and formats should be setup, so
 * we can setup our buffers and DMA
 */
static int comet_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct dma_slave_config conf;
	struct comet_pcm_dma_params *dma;
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	int burst_size, ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct device *dev = rtd->platform->dev;

	dma = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer */
	if (!dma)
		goto out;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	burst_size = params_channels(params) * 4 * 2 - 1;

	/* Set I/O address */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		conf.direction = DMA_MEM_TO_DEV;
		conf.dst_addr = dma->peripheral_address;
		conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		conf.dst_maxburst = burst_size;
	} else {
		conf.direction = DMA_DEV_TO_MEM;
		conf.src_addr = dma->peripheral_address;
		conf.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		conf.src_maxburst = burst_size;
	}

	ret = dmaengine_slave_config(chan, &conf);

	if (ret < 0) {
		dev_err(dev, "dma slave config failed wit err %d\n", ret);
		return ret;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

out:
	return ret;
}

/*release resources allocated in hw_params*/
static int comet_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static struct snd_pcm_ops comet_pcm_ops = {
	.open		= comet_pcm_open,
	.close		= comet_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= comet_pcm_hw_params,
	.hw_free	= comet_pcm_hw_free,
	.prepare	= comet_pcm_prepare,
	.trigger	= comet_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer,
	.mmap		= comet_pcm_mmap,
};

static u64 comet_pcm_dmamask = DMA_BIT_MASK(64);

static int comet_soc_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &comet_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(64);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = comet_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = comet_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

struct snd_soc_platform_driver comet_soc_platform = {
	.ops		= &comet_pcm_ops,
	.pcm_new	= comet_soc_pcm_new,
	.pcm_free	= comet_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(comet_soc_platform);

static int comet_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &comet_soc_platform);
}

static int comet_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver comet_pcm_driver = {
	.driver = {
			.name = "comet-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = comet_soc_platform_probe,
	.remove = comet_soc_platform_remove,
};

static int __init snd_comet_pcm_init(void)
{
	return platform_driver_register(&comet_pcm_driver);
}
module_init(snd_comet_pcm_init);

static void __exit snd_comet_pcm_exit(void)
{
	platform_driver_unregister(&comet_pcm_driver);
}
module_exit(snd_comet_pcm_exit);

MODULE_AUTHOR("Imagination Technologies");
MODULE_DESCRIPTION("Comet PCM Module");
MODULE_LICENSE("GPL");
