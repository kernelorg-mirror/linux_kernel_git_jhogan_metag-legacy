/*
 *
 * ALSA SoC I2S  Audio Layer for Frontier Silicon Chorus2 processor
 *
 * Author:      Neil Jones
 * Copyright:   (C) 2009 Imagination Technologies
 *
 * based on pxa2xx i2S ASoC driver
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "chorus2-pcm.h"
#include "chorus2-i2s.h"

#define I2S_OUT_PERIP_NUM		16
#define SYS_CLK_CONTROL			0x02024024
#define I2S_CLOCK_CONTROL		0x02000098


static struct chorus2_pcm_dma_params chorus2_pcm_stereo_out[] =
{
	[0] = {
		.name = "I2S PCM Stereo Out Chan 0",
		.peripheral_num = I2S_OUT_PERIP_NUM,
		.peripheral_address = I2S_OUT_BASE_ADDR + _I2S_OUT_CHANS_OFFSET
					+ 0 * _I2S_OUT_CHANS_STRIDE,
	},
	[1] = {
		.name = "I2S PCM Stereo Out Chan 1",
		.peripheral_num = I2S_OUT_PERIP_NUM,
		.peripheral_address = I2S_OUT_BASE_ADDR + _I2S_OUT_CHANS_OFFSET
				      + 1 * _I2S_OUT_CHANS_STRIDE,
	},
	[2] = {
		.name = "I2S PCM Stereo Out Chan 2",
		.peripheral_num = I2S_OUT_PERIP_NUM,
		.peripheral_address = I2S_OUT_BASE_ADDR + _I2S_OUT_CHANS_OFFSET
				      + 2 * _I2S_OUT_CHANS_STRIDE,
	},
	[4] =  {
		.name = "I2S PCM Stereo Out Chan 3",
		.peripheral_num = I2S_OUT_PERIP_NUM,
		.peripheral_address = I2S_OUT_BASE_ADDR + _I2S_OUT_CHANS_OFFSET
				      + 3 * _I2S_OUT_CHANS_STRIDE,
	},
};


static int chorus2_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{

	int id = cpu_dai->id;
	int ret = 0;
	u32 temp;

	temp = I2S_OUT_RGET_CHAN_CONTROL(id);

	/* Disable module before changing registers */
	I2S_OUT_SET_REG_FIELD(I2S_OUT_ENABLE, 0);
	I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 0);
	I2S_OUT_RSET_CHAN_CONTROL(id, temp);
	wmb();


	/* Setup Hardware Formats: */
	temp = I2S_OUT_RGET_CHAN_CONTROL(id);

	/*left just. bit must be set to 1*/
	I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_MUSTB1, 1);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	/*I2S Mode (Phillips Mode)
		1 bit clock delay from left edge of word sel*/
	case SND_SOC_DAIFMT_I2S:
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_PH_NSY, 1);
		break;
	/*Right Justified Mode, data aligned with right edge of word sel.*/
	case SND_SOC_DAIFMT_RIGHT_J:
		ret = -EINVAL;
		break;
	/*
	 *  Left justified (sony) mode data aligned to left edge of word select
	 *  ie no delay
	 */
	case SND_SOC_DAIFMT_LEFT_J:
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_PH_NSY, 0);
		break;

	case SND_SOC_DAIFMT_DSP_A:	/* L data msb after FRM LRC */
	case SND_SOC_DAIFMT_DSP_B:	/* L data msb during FRM LRC */
	case SND_SOC_DAIFMT_AC97:	/* AC97 */
		ret = -EINVAL;
		break;

	default:
		printk(KERN_ERR "%s: Unknown DAI format type\n", __func__);
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto out;

	I2S_OUT_RSET_CHAN_CONTROL(id, temp);

	/*
	 * Setup Clocking scheme
	 */

	temp = I2S_OUT_RGET_MAIN_CONTROL();
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:	/*Interface clk & frame slave*/
		I2S_OUT_SET_FIELD(temp, I2S_OUT_MASTER, 0);
		break;
	case SND_SOC_DAIFMT_CBS_CFM:	/*Interface clk master, frame slave*/
	case SND_SOC_DAIFMT_CBM_CFS:	/*Interface clk slave, frame master*/
		ret = -EINVAL;
		break;

	case SND_SOC_DAIFMT_CBS_CFS:	/*Interface clk & frame master */
		I2S_OUT_SET_FIELD(temp, I2S_OUT_MASTER, 1);
		break;
	default:
		printk(KERN_ERR "%s: Unknown DAI master type\n", __func__);
		ret = -EINVAL;
		break;
	}
	if (ret)
		goto out;

	I2S_OUT_RSET_MAIN_CONTROL(temp);
	wmb();

	/*clock inversion options*/

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:	/* normal bit clock + frame */
		I2S_OUT_SET_REG_FIELD(I2S_OUT_BCLK_POL, 1);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_LEFT_POL, 0);
		break;
	case SND_SOC_DAIFMT_NB_IF:	/* normal BCLK + inv FRM */
		I2S_OUT_SET_REG_FIELD(I2S_OUT_BCLK_POL, 1);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_LEFT_POL, 1);
		break;
	case SND_SOC_DAIFMT_IB_NF:	/* invert BCLK + nor FRM */
		I2S_OUT_SET_REG_FIELD(I2S_OUT_BCLK_POL, 0);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_LEFT_POL, 0);
		break;
	case SND_SOC_DAIFMT_IB_IF:	/* invert BCLK + FRM */
		I2S_OUT_SET_REG_FIELD(I2S_OUT_BCLK_POL, 0);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_LEFT_POL, 1);
		break;
	}
	/*Re-enable module - but dont set channel run bit yet*/
	I2S_OUT_SET_REG_FIELD(I2S_OUT_ENABLE, 1);

out:
	return ret;
}

static int chorus2_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{

	return 0;
}

static int chorus2_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int ret = 0;
	u32 temp = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			temp = I2S_OUT_RGET_CHAN_CONTROL(dai->id);
			I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 1);
			I2S_OUT_RSET_CHAN_CONTROL(dai->id, temp);
			wmb();
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			temp = I2S_OUT_RGET_CHAN_CONTROL(dai->id);
			I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 0);
			I2S_OUT_RSET_CHAN_CONTROL(dai->id, temp);
			wmb();
		}
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int chorus2_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int id = cpu_dai->id;
	u32 temp;

	snd_soc_dai_set_dma_data(cpu_dai, substream,
				 &chorus2_pcm_stereo_out[id]);

	temp = I2S_OUT_RGET_CHAN_CONTROL(id);

	/* Disable module before changing registers */
	I2S_OUT_SET_REG_FIELD(I2S_OUT_ENABLE, 0);
	I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 0);
	I2S_OUT_RSET_CHAN_CONTROL(id, temp);
	wmb();

	/*Set Format*/
	switch (params_format(params)) {

	case SNDRV_PCM_FORMAT_S16_LE:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_FRAME, 2);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_PACKED, 1);
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_PACKED, 1);
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_FORMAT, 0);

		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_FRAME, 2);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_PACKED, 0);
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_PACKED, 0);
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_FORMAT, 4);
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_FRAME, 2);
		I2S_OUT_SET_REG_FIELD(I2S_OUT_PACKED, 0);
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_PACKED, 0);
		I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_FORMAT, 8);
		break;

	default:
		printk(KERN_ERR "%s: Unknown PCM format\n", __func__);
		return -EINVAL;
	}


	I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_LRDATA_POL, 0);
	I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_LRFORCE_DIS, 1);
	I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_LOCK_DIS, 1);

	/*write back channel settings*/
	I2S_OUT_RSET_CHAN_CONTROL(id, temp);
	/*enable bit clock */
	I2S_OUT_SET_REG_FIELD(I2S_OUT_BCLK_EN, 1);
	/* we only support 256fs */
	I2S_OUT_SET_REG_FIELD(I2S_OUT_ACLK_SEL, 0);
	/* Set Sample Rate */
	switch (params_rate(params)) {

	case 32000:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_ACLK_SEL, 1);
		iowrite32(1, (void *)SYS_CLK_CONTROL); /*12.288MHz*/
		iowrite32(4, (void *)I2S_CLOCK_CONTROL); /*12.288MHz*/
		break;
	case 48000:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_ACLK_SEL, 0);
		iowrite32(1, (void *)SYS_CLK_CONTROL); /*12.288MHz*/
		iowrite32(4, (void *)I2S_CLOCK_CONTROL); /*12.288MHz*/
		break;
	case 64000:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_ACLK_SEL, 1);
		iowrite32(0, (void *)SYS_CLK_CONTROL);/*24.576MHz*/
		iowrite32(5, (void *)I2S_CLOCK_CONTROL); /*24.576MHz*/
		break;
	case 96000:
		I2S_OUT_SET_REG_FIELD(I2S_OUT_ACLK_SEL, 0);
		iowrite32(0, (void *)SYS_CLK_CONTROL);/*24.576MHz*/
		iowrite32(5, (void *)I2S_CLOCK_CONTROL); /*24.576MHz*/
		break;
	default:
		return -EINVAL;
	}


	wmb();

	/*Re-enable module - but dont set channel run bit yet*/
	I2S_OUT_SET_REG_FIELD(I2S_OUT_ENABLE, 1);
	wmb();


	return 0;
}

static void chorus2_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{

}


#ifdef CONFIG_PM
static int chorus2_i2s_suspend(struct snd_soc_dai *dai)
{
	/* TODO:
	 *	It should be possible to disable the clocks to
	 *	the I2S OUT module
	 */
	return 0;
}

static int chorus2_i2s_resume(struct snd_soc_dai *dai)
{
	/* TODO*/
	return 0;
}
#endif

#define CHORUS2_I2S_RATES (SNDRV_PCM_RATE_32000 |   \
				SNDRV_PCM_RATE_48000 |  \
				SNDRV_PCM_RATE_64000 |  \
				SNDRV_PCM_RATE_96000)

#define CHORUS2_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops chorus2_i2s_dai_ops = {
	.startup	= chorus2_i2s_startup,
	.shutdown	= chorus2_i2s_shutdown,
	.trigger	= chorus2_i2s_trigger,
	.hw_params	= chorus2_i2s_hw_params,
	.set_fmt	= chorus2_i2s_set_dai_fmt,
};

struct snd_soc_dai_driver chorus2_i2s_dai = {
	.name = "chorus2-i2s",
	.id = 0,
#ifdef CONFIG_PM
	.suspend = chorus2_i2s_suspend,
	.resume = chorus2_i2s_resume,
#endif
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = CHORUS2_I2S_RATES,
		.formats = CHORUS2_FORMATS,},
	.ops = &chorus2_i2s_dai_ops,
};
EXPORT_SYMBOL_GPL(chorus2_i2s_dai);


static int chorus2_i2s_platform_probe(struct platform_device *pdev)
{
	int ret;

	ret = snd_soc_register_dai(&pdev->dev, &chorus2_i2s_dai);

	return ret;
}

static int chorus2_i2s_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&pdev->dev);
	return 0;
}

static struct platform_driver chorus2_i2s_driver = {
	.probe = chorus2_i2s_platform_probe,
	.remove = chorus2_i2s_platform_remove,

	.driver = {
		.name = "chorus2-i2s",
		.owner = THIS_MODULE,
	},
};


static int __init chorus_i2s_init(void)
{
	return platform_driver_register(&chorus2_i2s_driver);
}
module_init(chorus_i2s_init);

static void __exit chorus2_i2s_exit(void)
{
	platform_driver_unregister(&chorus2_i2s_driver);
}
module_exit(chorus2_i2s_exit);

/* Module information */
MODULE_AUTHOR("Neil Jones");
MODULE_DESCRIPTION("I2S driver for Frontier Silicon Chorus2 Soc");
MODULE_LICENSE("GPL");

