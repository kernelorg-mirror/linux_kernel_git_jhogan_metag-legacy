/*
 *  atp_dp.c
 *
 *  atp_dp (Metamorph) ASOC Audio Support
 *
 *  Copyright:	(C) 2009 Imagination Technologies
 *
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "chorus2-pcm.h"
#include "chorus2-i2s.h"



static int atp_dp_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;


	/* set codec DAI configuration */

		/*Simple codec (non configurable) */

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */

		/*Simple codec (non configurable) */

	/* set the I2S system clock  */

	/*  if we support clock setup methods ...
	ret = snd_soc_dai_set_sysclk(cpu_dai, ...);
	if (ret < 0)
		return ret;
	*/

	return 0;
}

static struct snd_soc_ops atp_dp_ops = {
	.hw_params = atp_dp_hw_params,
};



/*
 * ATP-DP digital audio interface glue - connects codec <--> CPU
 *
 * Note the codec used on the ATP-DP base board (WM8727) is a simple I2S
 * Based DAC, without a configuration interface.
 */
static struct snd_soc_dai_link atp_dp_dai = {
	.name = "wm8727",
	.stream_name = "Playback",
	.cpu_dai_name = "chorus2-i2s.0",
	.codec_name = "wm8727-codec.0",
	.codec_dai_name = "wm8727-hifi",
	.platform_name = "chorus2-pcm-audio",
	.ops = &atp_dp_ops,
};

/*ATP-DP audio machine driver */
static struct snd_soc_card snd_soc_atp_dp = {
	.name = "ATP-DP-Audio",
	.dai_link = &atp_dp_dai,
	.num_links = 1,
};


/* Chorus2 i2s platform device */
static struct platform_device chorus2_i2s_platform = {
	.name           = "chorus2-i2s",
};

/* wm8727 platform device */
static struct platform_device wm8727_platform = {
	.name           = "wm8727-codec",
};

static struct platform_device *audio_devices[] __initdata = {
	&chorus2_i2s_platform,
	&wm8727_platform,

};

static struct platform_device *atp_dp_snd_device;

static int __init atp_dp_init(void)
{
	int ret;

	ret = platform_add_devices(audio_devices,
					    ARRAY_SIZE(audio_devices));
	if (ret)
		return ret;

	atp_dp_snd_device = platform_device_alloc("soc-audio", -1);
	if (!atp_dp_snd_device)
		return -ENOMEM;

	platform_set_drvdata(atp_dp_snd_device, &snd_soc_atp_dp);
	ret = platform_device_add(atp_dp_snd_device);

	if (ret)
		platform_device_put(atp_dp_snd_device);

	return ret;
}

static void __exit atp_dp_exit(void)
{
	platform_device_unregister(atp_dp_snd_device);
}

module_init(atp_dp_init);
module_exit(atp_dp_exit);

/* Module information */
MODULE_AUTHOR("Neil Jones");
MODULE_DESCRIPTION("ALSA SoC ATP-DP");
MODULE_LICENSE("GPL");

