/*
 *
 * ALSA SoC I2S  Audio Layer for TZ1090
 *
 * Author:      Neil Jones
 * Copyright:   (C) 2009-2012 Imagination Technologies
 *
 * based on pxa2xx i2S ASoC driver
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <linux/of.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "tz1090-pcm.h"
#include "tz1090-i2s.h"

#define DRV_NAME "tz1090-i2s"

static int comet_i2s_set_dai_fmt_out(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {

	case SND_SOC_DAIFMT_RIGHT_J:
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
	case SND_SOC_DAIFMT_AC97:
		ret = -EINVAL;
		break;
	default:
		printk(KERN_ERR "%s: Unknown DAI format type\n", __func__);
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto out;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:	/*Interface clk & frame master */
	case SND_SOC_DAIFMT_CBS_CFM:	/*Interface clk master, frame slave*/
	case SND_SOC_DAIFMT_CBM_CFS:	/*Interface clk slave, frame master*/
		ret = -EINVAL;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:	/*Interface clk & frame slave*/
		break;
	default:
		printk(KERN_ERR "%s: Unknown DAI master type\n", __func__);
		ret = -EINVAL;
		break;
	}
	if (ret)
		goto out;


	/*clock inversion options*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:	/* normal bit clock + frame */
		break;
	case SND_SOC_DAIFMT_NB_IF:	/* normal BCLK + inv FRM */
	case SND_SOC_DAIFMT_IB_NF:	/* invert BCLK + nor FRM */
	case SND_SOC_DAIFMT_IB_IF:	/* invert BCLK + FRM */
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}

static int comet_i2s_set_dai_fmt_in(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	int ret = 0;

	/*Set Data Format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {

	case SND_SOC_DAIFMT_RIGHT_J:	/*Right Justified Mode */
		break;

	case SND_SOC_DAIFMT_I2S:/*I2S Mode(Phillips Mode)*/
	case SND_SOC_DAIFMT_LEFT_J:	/*left Justified Mode */
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

	/*clocking scheme*/
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:	/*Codec clk & frame slave*/
		/*the only supported mode */
		break;
	case SND_SOC_DAIFMT_CBS_CFM:	/*Codec clk master, frame slave*/
	case SND_SOC_DAIFMT_CBM_CFS:	/*Codec clk slave, frame master*/
	case SND_SOC_DAIFMT_CBS_CFS:	/*Codec clk & frame master */
		ret = -EINVAL;
		break;

	default:
		printk(KERN_ERR "%s: Unknown DAI master type\n", __func__);
		ret = -EINVAL;
		break;
	}
	if (ret)
		goto out;

	/*clock inversion options*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:	/* normal bit clock + frame */
		break;
	case SND_SOC_DAIFMT_NB_IF:	/* normal BCLK + inv FRM */
	case SND_SOC_DAIFMT_IB_NF:	/* invert BCLK + nor FRM */
	case SND_SOC_DAIFMT_IB_IF:	/* invert BCLK + FRM */
		ret = -EINVAL;
	}
out:
	return ret;
}

static int comet_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	int ret = comet_i2s_set_dai_fmt_out(cpu_dai, fmt);
	if (ret)
		return ret;
	else
		return comet_i2s_set_dai_fmt_in(cpu_dai, fmt);
}

static int comet_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	struct tz1090_i2s *tzi2s = snd_soc_dai_get_drvdata(dai);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dai_set_dma_data(cpu_dai, substream,
					 &tzi2s->stereo_out);
	else
		snd_soc_dai_set_dma_data(cpu_dai, substream,
					 &tzi2s->stereo_in);

	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ENABLE, 1);

	return 0;
}

static int comet_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	int ret = 0;
	u32 temp = 0;
	struct tz1090_i2s *tzi2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			temp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, 0);
			I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 1);
			I2S_OUT_RSET_CHAN_CONTROL(tzi2s, 0, temp);
			if (substream->runtime->channels > 2) {
				temp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, 1);
				I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 1);
				I2S_OUT_RSET_CHAN_CONTROL(tzi2s, 1, temp);
			}
			if (substream->runtime->channels > 4) {
				temp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, 2);
				I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 1);
				I2S_OUT_RSET_CHAN_CONTROL(tzi2s, 2, temp);
			}

		} else { /* Record */
			temp = I2S_IN_RGET_CONTROL(tzi2s);
			I2S_IN_SET_FIELD(temp, I2S_IN_ENABLE, 1);
			I2S_IN_RSET_CONTROL(tzi2s, temp);
		}

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			temp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, 0);
			I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 0);
			I2S_OUT_RSET_CHAN_CONTROL(tzi2s, 0, temp);
			if (substream->runtime->channels > 2) {
				temp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, 1);
				I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 0);
				I2S_OUT_RSET_CHAN_CONTROL(tzi2s, 1, temp);
			}
			if (substream->runtime->channels > 4) {
				temp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, 2);
				I2S_OUT_SET_FIELD(temp, I2S_OUT_CHAN_RUN, 0);
				I2S_OUT_RSET_CHAN_CONTROL(tzi2s, 2, temp);
			}
		} else { /* Record */
			temp = I2S_IN_RGET_CONTROL(tzi2s);
			I2S_IN_SET_FIELD(temp, I2S_IN_ENABLE, 0);
			I2S_IN_RSET_CONTROL(tzi2s, temp);
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

static int comet_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct tz1090_i2s *tzi2s = snd_soc_dai_get_drvdata(dai);
	int ch;
	struct clk *i2s_clk = NULL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Set the number of channels in use */
		I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ENABLE, 0);
		ch = params_channels(params) / 2 - 1;
		I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ACTIVE_CHAN, ch);
		I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ENABLE, 1);

		/*Set Format*/
		switch (params_format(params)) {

		case SNDRV_PCM_FORMAT_S24_LE:
			break;
		case SNDRV_PCM_FORMAT_S16_LE:
		case SNDRV_PCM_FORMAT_S32_LE:
			return -EINVAL;
			break;
		default:
			printk(KERN_ERR "%s: Unknown PCM format\n", __func__);
			return -EINVAL;
		}

	} else {
		/*Set Format*/
		switch (params_format(params)) {

		case SNDRV_PCM_FORMAT_S24_LE:
			break;

		case SNDRV_PCM_FORMAT_S16_LE:
		case SNDRV_PCM_FORMAT_S32_LE:
			return -EINVAL;
			break;

		default:
			printk(KERN_ERR "%s: Unknown PCM format\n", __func__);
			return -EINVAL;
		}

	}

	/* Set Sample Rate */
	i2s_clk = clk_get(substream->pcm->dev, "i2s");
	if (IS_ERR(i2s_clk))
		return PTR_ERR(i2s_clk);

	switch (params_rate(params)) {
	case 32000:
		clk_set_rate(i2s_clk, 8192000);
		break;
	case 48000:
		clk_set_rate(i2s_clk, 12288000);
		break;
	case 96000:
		clk_set_rate(i2s_clk, 24576000);
		break;
	default:
		return -EINVAL;
	}
	clk_put(i2s_clk);

	return 0;
}

static void comet_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct tz1090_i2s *tzi2s = snd_soc_dai_get_drvdata(dai);
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ENABLE, 0);
	I2S_OUT_WRITE_REG(tzi2s, I2S_OUT_SOFT_RESET, 1);
	udelay(1000);
	I2S_OUT_WRITE_REG(tzi2s, I2S_OUT_SOFT_RESET, 0);
}

#ifdef CONFIG_PM
static int comet_i2s_suspend(struct snd_soc_dai *dai)
{
	/* TODO:
	 *	It should be possible to disable the clocks to
	 *	the I2S OUT module
	 */
	return 0;
}

static int comet_i2s_resume(struct snd_soc_dai *dai)
{
	/* TODO*/
	return 0;
}
#endif

#define COMET_I2S_RATES (SNDRV_PCM_RATE_32000 \
			| SNDRV_PCM_RATE_48000 \
			| SNDRV_PCM_RATE_96000)

#define COMET_FORMATS_PLAYBACK SNDRV_PCM_FMTBIT_S24_LE

#define COMET_FORMATS_RECORD SNDRV_PCM_FMTBIT_S24_LE

static struct snd_soc_dai_ops comet_i2s_dai_ops = {
	.startup	= comet_i2s_startup,
	.shutdown	= comet_i2s_shutdown,
	.trigger	= comet_i2s_trigger,
	.hw_params	= comet_i2s_hw_params,
	.set_fmt	= comet_i2s_set_dai_fmt,
};

struct snd_soc_dai_driver comet_i2s_dai = {
	.id = -1,
#ifdef CONFIG_PM
	.suspend = comet_i2s_suspend,
	.resume = comet_i2s_resume,
#endif
	.playback = {
		.channels_min = 2,
		.channels_max = 6,
		.rates = COMET_I2S_RATES,
		.formats = COMET_FORMATS_PLAYBACK,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = COMET_I2S_RATES,
		.formats = COMET_FORMATS_RECORD,
	},
	.ops = &comet_i2s_dai_ops,
	.symmetric_rates = 1,

};
EXPORT_SYMBOL_GPL(comet_i2s_dai);

static const struct snd_soc_component_driver comet_i2s_component = {
	.name = "comet-i2s",
};

static struct of_device_id img_i2s_of_match[] = {
	{ .compatible = "img,i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, img_i2s_of_match);

static struct platform_device comet_audio_pcm = {
	.name = "comet-pcm-audio",
	.id = -1,
};

static struct platform_device *audio_devices[] __initdata = {
	&comet_audio_pcm,
};

static int comet_i2s_platform_probe(struct platform_device *pdev)
{
	struct clk *i2s;
	int ret, i;
	struct tz1090_i2s *tzi2s;
	u32 of_dma[6], outtemp, intemp;
	struct resource *mem;
	struct device *dev = &pdev->dev;

	ret = platform_add_devices(audio_devices, ARRAY_SIZE(audio_devices));
	if (ret)
		return ret;

	/*
	 * although the hardware can support various modes they must match
	 * between i2s in and i2s out also when disabling the i2s block
	 * to change the in parameters this would stop the out block (playback)
	 * and vica versa.
	 *
	 * thus we only support 1 configuration for both in and out
	 * and set it at startup.
	 *
	 * 24bit
	 * normal bit clock normal frame clock
	 * bit clock and frame clock slave
	 *
	 * Note1: as the Comet I2S out block expects 24 bit data left aligned
	 * from the DMA and ALSA provides 24 bit data right aligned we
	 * configure the I2S out block in 32 bit mode we then get 24 bit data
	 * out of the I2S right aligned, which conforms to the Sony
	 * right aligned I2S timings.
	 *
	 * Note2: I2S Audio In is broken in Comet in 16 bit mode, it does not
	 * work in 16+16 frame or 32+32 frame with any timing configuration
	 * (all have been tried)
	 *
	 * Note3: I2S Audio In is broken in Comet in 24bit right justify mode.
	 * So even though in the machine driver we select right justify mode
	 * and use this for I2S out, we use Philips mode for I2S In.
	 */

	tzi2s = devm_kzalloc(dev, sizeof(struct tz1090_i2s), GFP_KERNEL);
	if (!tzi2s) {
		dev_err(dev, "Can't allocate memory for tz1090 i2s");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, tzi2s);

	tzi2s->dai = comet_i2s_dai;
	tzi2s->dai.name = dev_name(&pdev->dev);
	/* Get DMA addresses for audio out/in */

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tzi2s->audio_out = devm_ioremap_resource(dev, mem);

	if (IS_ERR(tzi2s->audio_out)) {
		ret = PTR_ERR(tzi2s->audio_out);
		goto err_resource;
	}
	tzi2s->stereo_out.peripheral_address = (dma_addr_t)mem->start +
		_REG_ADDRESS(I2S_OUT_INTERLEAVE_DATA);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	tzi2s->audio_in = devm_ioremap_resource(dev, mem);

	if (IS_ERR(tzi2s->audio_in)) {
		ret = PTR_ERR(tzi2s->audio_in);
		goto err_resource;
	}
	tzi2s->stereo_in.peripheral_address = (dma_addr_t)mem->start +
		_REG_ADDRESS(I2S_IN_DATA);

	if (of_property_read_u32_array(dev->of_node, "dmas",
				       of_dma, 6)) {
		dev_err(dev, "dmas property not found");
		ret = -ENODEV;
		goto err_resource;
	}

	/* We ignore the dma phandles for now */
	tzi2s->stereo_out.name = "I2S PCM Stereo Out Chan 0";
	tzi2s->stereo_out.peripheral_num = of_dma[1];
	tzi2s->stereo_out.dma_channel = of_dma[2];
	tzi2s->stereo_in.name = "I2S PCM Stereo In";
	tzi2s->stereo_in.peripheral_num = of_dma[4];
	tzi2s->stereo_in.dma_channel = of_dma[5];

	/* Disable I2S In and Out */
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ENABLE, 0);
	intemp = I2S_IN_RGET_CONTROL(tzi2s);
	I2S_IN_SET_FIELD(intemp, I2S_IN_ENABLE, 0);
	I2S_IN_RSET_CONTROL(tzi2s, intemp);


	/* I2S Out Setup */
	for (i = 0; i < 3; i++) {
		outtemp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, i);
		/*
		 * Disable module before changing registers
		 */
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_RUN, 0);
		I2S_OUT_RSET_CHAN_CONTROL(tzi2s, i, outtemp);
		/*
		 * Setup Hardware Formats:
		 */
		outtemp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, i);
		/*left just. bit must be set to 1*/
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_MUSTB1, 1);
		/* phillips mode*/
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_PH_NSY, 0);
		I2S_OUT_RSET_CHAN_CONTROL(tzi2s, i, outtemp);

		/*Set Format*/
		outtemp = I2S_OUT_RGET_CHAN_CONTROL(tzi2s, i);
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_PACKED, 0);
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_FORMAT, 8);
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_LRDATA_POL, 0);
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_LRFORCE_DIS, 1);
		I2S_OUT_SET_FIELD(outtemp, I2S_OUT_CHAN_LOCK_DIS, 1);

		/*write back channel settings*/
		I2S_OUT_RSET_CHAN_CONTROL(tzi2s, i, outtemp);
	}

	/* Set common frame format */
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_FRAME, 2);
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_PACKED, 0);
	/*
	 * Setup Clocking scheme
	 */
	outtemp = I2S_OUT_RGET_MAIN_CONTROL(tzi2s);
	/*Interface clk & frame slave*/
	I2S_OUT_SET_FIELD(outtemp, I2S_OUT_MASTER, 1);
	I2S_OUT_RSET_MAIN_CONTROL(tzi2s, outtemp);
	/* normal bit clock + frame */
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_BCLK_POL, 1);
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_LEFT_POL, 1);
	/*enable bit clock */
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_BCLK_EN, 1);
	/* we only support 256fs */
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ACLK_SEL, 0);

	/*I2S In Setup*/

	/*Philips I2S mode*/
	I2S_IN_SET_FIELD(intemp, I2S_IN_RJUST, 0);
	I2S_IN_SET_FIELD(intemp, I2S_IN_LRDLY, 1);
	I2S_IN_SET_FIELD(intemp, I2S_IN_ALIGN, 1);
	/* Normal bit clock + frame */
	I2S_IN_SET_FIELD(intemp, I2S_IN_BCLKPOL, 0);
	I2S_IN_SET_FIELD(intemp, I2S_IN_LR_DATA_POL, 0);
	/* 24 bit LE data */
	I2S_IN_SET_FIELD(intemp, I2S_IN_FRAME_PACK, 0);
	I2S_IN_SET_FIELD(intemp, I2S_IN_SAMPLE_WIDTH, 1);
	I2S_IN_SET_FIELD(intemp, I2S_IN_FRAME_WIDTH, 1);
	/*output data word to DMA is left aligned*/
	I2S_IN_SET_FIELD(intemp, I2S_IN_PACKH, 0);

	I2S_IN_RSET_CONTROL(tzi2s, intemp);

	/*Re-enable module - but dont set channel run bit yet*/
	I2S_OUT_SET_REG_FIELD(tzi2s, I2S_OUT_ENABLE, 1);


	ret = snd_soc_register_component(dev, &comet_i2s_component, &tzi2s->dai, 1);
	if (ret) {
		dev_err(dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_dai;
	}

	i2s = devm_clk_get(dev, NULL);
	if (IS_ERR(i2s)) {
		ret = PTR_ERR(i2s);
		goto err_dai;
	}

	clk_prepare_enable(i2s);
	clk_put(i2s);

	dev_info(dev, "TZ1090 I2S probed successfully");


	return 0;

err_dai:
	snd_soc_unregister_component(dev);
err_resource:
	return ret;
}

static int comet_i2s_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static struct platform_driver comet_i2s_driver = {
	.probe = comet_i2s_platform_probe,
	.remove = comet_i2s_platform_remove,

	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = img_i2s_of_match,
	},
};

static int __init comet_i2s_init(void)
{
	return platform_driver_register(&comet_i2s_driver);
}
module_init(comet_i2s_init);

static void __exit comet_i2s_exit(void)
{
	platform_driver_unregister(&comet_i2s_driver);
}
module_exit(comet_i2s_exit);

/* Module information */
MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("I2S driver for TZ1090 SoC");
MODULE_LICENSE("GPL");

