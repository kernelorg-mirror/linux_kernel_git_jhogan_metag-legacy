/*
 * SiI9022a HDMI Transmitter Audio codec component.
 * Copyright (C) 2010 Imagination Technologies.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 *
 *  This program is distributed in the hope that, in addition to its
 *  original purpose to support Neuros hardware, it will be useful
 *  otherwise, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include <asm/soc-tz1090/hdmi-audio.h>
#include "sii9022a.h"

#define SII9022A_AUDIO_FORMATS	SNDRV_PCM_FMTBIT_S24_LE
#define SII9022A_AUDIO_RATES	(/*SNDRV_PCM_RATE_32000 |*/\
				 SNDRV_PCM_RATE_48000)


/*
 * Note the DAI ops bellow must be implemented here, even if they only return 0
 * otherwise soc-core returns -EINVAL if the call isn't implemented and
 * assumes the requested format is unsupported
 */

static int sii9022a_audio_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	return 0;
}

static int sii9022a_audio_set_dai_fmt(struct snd_soc_dai *codec_dai,
				      unsigned int fmt)
{
	return 0;
}

static struct snd_soc_dai_ops sii9022a_audio_ops = {
	.hw_params	= sii9022a_audio_hw_params,
	.set_fmt	= sii9022a_audio_set_dai_fmt,
};

static struct snd_soc_dai_driver sii9022a_audio_dai = {
	.name		= "sii9022a-audio",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 6,
		.rates		= SII9022A_AUDIO_RATES,
		.formats	= SII9022A_AUDIO_FORMATS,
	},
	.ops = &sii9022a_audio_ops,
};

static struct snd_soc_codec_driver soc_codec_dev_sii9022a;

static int sii9022a_audio_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_sii9022a,
				      &sii9022a_audio_dai, 1);
}

static int sii9022a_audio_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver sii9022a_codec_driver = {
	.driver = {
		.name	= "sii9022a-codec",
		.owner	= THIS_MODULE,
	},

	.probe	= sii9022a_audio_probe,
	.remove	= sii9022a_audio_remove,
};

static int __init sii9022a_audio_init(void)
{
	return platform_driver_register(&sii9022a_codec_driver);
}
module_init(sii9022a_audio_init);

static void __exit sii9022a_audio_exit(void)
{
	platform_driver_unregister(&sii9022a_codec_driver);
}
module_exit(sii9022a_audio_exit);

MODULE_DESCRIPTION("ALSA SoC codec layer for SiI9022a HDMI transmitter");
MODULE_AUTHOR("Imagination Technologies");
MODULE_LICENSE("GPL");

