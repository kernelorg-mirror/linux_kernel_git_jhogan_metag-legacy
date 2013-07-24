/*
 * tansen.c - CosmicCirucits internal DAC
 *
 * Copyright (C) 2010-2013 Imagination Technologies Ltd.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tansen.h>
#include <sound/tlv.h>

#include "tansen.h"
#include "tansen_gti.h"

struct tansen_priv {
	void __iomem *ctrl;
};

static const DECLARE_TLV_DB_SCALE(mic_in, -3300, 300, 0);
static const DECLARE_TLV_DB_SCALE(line_in, -2100, 300, 0);

static const struct snd_kcontrol_new tansen_snd_controls[] = {
SOC_DOUBLE_R("Master Playback Volume",
	     TANSEN_TM_OP_TOP6, TANSEN_TM_OP_TOP7,
	     0, 0xFF, 1
	     ),

SOC_DOUBLE_TLV("Line/IPOD-In Gain", TANSEN_TM_IP_TOP5, 5, 2, 0x7, 1, line_in),
SOC_DOUBLE_TLV("Mic-In Gain", TANSEN_TM_IP_TOP4, 0, 4, 0xF, 1, mic_in),
};

static const char *tansen_input_select[] = {
	"Mic In",
	"Line In",
	"Ipod In",
};

static const struct soc_enum tansen_ip_enum =
SOC_ENUM_SINGLE(TANSEN_TM_IP_TOP2, 4, 3, tansen_input_select);

/* Input mux */
static const struct snd_kcontrol_new tansen_input_mux_controls =
SOC_DAPM_ENUM("Input Select", tansen_ip_enum);


static const struct snd_soc_dapm_widget tansen_dapm_widgets[] = {
		/*All Power Control now removed*/
SND_SOC_DAPM_DAC("DAC_L", "Playback", SND_SOC_NOPM /*TANSEN_TM_OP_TOP3*/, 5, 0),
SND_SOC_DAPM_DAC("DAC_R", "Playback", SND_SOC_NOPM /*TANSEN_TM_OP_TOP3*/, 4, 0),
SND_SOC_DAPM_OUTPUT("LHPOUT"),
SND_SOC_DAPM_OUTPUT("RHPOUT"),
SND_SOC_DAPM_ADC("ADC_L", "Capture", SND_SOC_NOPM /*TANSEN_TM_IP_TOP2*/, 2, 0),
SND_SOC_DAPM_ADC("ADC_R", "Capture", SND_SOC_NOPM /*TANSEN_TM_IP_TOP2*/, 0, 0),
SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0, &tansen_input_mux_controls),
SND_SOC_DAPM_PGA("PGA_L", SND_SOC_NOPM /*TANSEN_TM_L_PGA3*/, 1, 1, NULL, 0),
SND_SOC_DAPM_PGA("PGA_R", SND_SOC_NOPM /*TANSEN_TM_R_PGA3*/, 1, 1, NULL, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias", TANSEN_TM_MICBIAS_1, 4, 1),
SND_SOC_DAPM_MIXER("Line Input", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Ipod Input", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_INPUT("MICIN"),
SND_SOC_DAPM_INPUT("RLINEIN"),
SND_SOC_DAPM_INPUT("LLINEIN"),
SND_SOC_DAPM_INPUT("RIPODIN"),
SND_SOC_DAPM_INPUT("LIPODIN")
};


static const struct snd_soc_dapm_route intercon[] = {

	/* outputs */
	{"RHPOUT", NULL, "DAC_L"},
	{"LHPOUT", NULL, "DAC_R"},

	/* input mux */
	{"Input Mux", "Line In",  "Line Input"},
	{"Input Mux", "Mic In",   "Mic Bias"},
	{"Input Mux", "Ipod In",  "Ipod Input"},
	{"ADC_L", NULL, "PGA_L"},
	{"ADC_R", NULL, "PGA_R"},
	{"PGA_L", NULL, "Input Mux"},
	{"PGA_R", NULL, "Input Mux"},

	/* inputs */
	{"Line Input", NULL, "LLINEIN"},
	{"Line Input", NULL, "RLINEIN"},
	{"Ipod Input", NULL, "LIPODIN"},
	{"Ipod Input", NULL, "RIPODIN"},
	{"Mic Bias", NULL, "MICIN"},
};

/* these regs don't readback correctly */
static int cache_tm_ip_top2;
static int cache_tm_ip_top4;
static int cache_tm_ip_top5;

static int tansen_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	u32 bitwidth = 0, reg_val;
	u32 rx_s1_regs[] = { TANSEN_TM_PWM1_S1, TANSEN_TM_PWM2_S1,
			     TANSEN_TM_PWM3_S1 };
	int i;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/*
		 * Note this should be 3 but the Comet I2S-IN block
		 * doesn't seem to work properly with a 16 bit input
		 * use 24 bits and let the I2S block trim the lower
		 * order bits.
		 */
		bitwidth = 1;
		break;

	case SNDRV_PCM_FORMAT_S20_3LE:
		bitwidth = 0;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		bitwidth = 1;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		bitwidth = 2;
		break;
	}

	/* Update TX bitwidth (inputs) */
	reg_val = snd_soc_read(codec, TANSEN_TM_IP_TOP6);
	reg_val = (reg_val & ~0x3) | bitwidth ;
	snd_soc_write(codec, TANSEN_TM_IP_TOP6, reg_val);

	/* Update RX bitwidths (outputs) */
	for (i = 0; i < ARRAY_SIZE(rx_s1_regs);  i++) {
		reg_val = snd_soc_read(codec, rx_s1_regs[i]);
		reg_val = (reg_val & ~0xc0) | (bitwidth << 6);
		snd_soc_write(codec, rx_s1_regs[i], reg_val);
	}
	reg_val = snd_soc_read(codec, TANSEN_TM_IP_TOP7);
	reg_val = (reg_val & ~0x3) | bitwidth;
	snd_soc_write(codec, TANSEN_TM_IP_TOP7, reg_val);

	return 0;
}

static int tansen_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	/*struct snd_soc_codec *codec = codec_dai->codec;*/

	switch (freq) {
	case  8192000:
	case 12288000:
	case 24576000:
		return 0;
	}
	return -EINVAL;
}

static int tansen_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u32 tx_value, rx_value, pwm_rx_value1, pwm_rx_value2, pwm_rx_value3;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* clock and frame slave only */
		break;
	default:
		return -EINVAL;
	}


	tx_value  = snd_soc_read(codec, TANSEN_TM_IP_TOP6);
	rx_value = snd_soc_read(codec, TANSEN_TM_IP_TOP7);
	pwm_rx_value1 = snd_soc_read(codec, TANSEN_TM_PWM1_S1);
	pwm_rx_value2 = snd_soc_read(codec, TANSEN_TM_PWM2_S1);
	pwm_rx_value3 = snd_soc_read(codec, TANSEN_TM_PWM3_S1);
	tx_value &= ~0x30;
	rx_value &= ~0x30;
	pwm_rx_value1 &= ~0xC;
	pwm_rx_value2 &= ~0xC;
	pwm_rx_value3 &= ~0xC;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		tx_value |= (0 << 4);
		rx_value |= (0 << 4);
		pwm_rx_value1 |= (0 << 2);
		pwm_rx_value2 |= (0 << 2);
		pwm_rx_value3 |= (0 << 2);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		/*
		 * Note these two values should be set to 2 for right justify
		 * but right justify mode for I2S In seems to be broken
		 * so even though we are using right justify mode for I2S
		 * out we set in to Phillips mode for in, Weirdly the top level
		 * rx register needs Phillips mode as well, even though we are
		 * sending right justified data!
		 */
		tx_value |= (0 << 4);
		rx_value |= (0 << 4);

		pwm_rx_value1 |= (2 << 2);
		pwm_rx_value2 |= (2 << 2);
		pwm_rx_value3 |= (2 << 2);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		tx_value |= (3 << 4);
		rx_value |= (3 << 4);
		pwm_rx_value1 |= (3 << 2);
		pwm_rx_value2 |= (3 << 2);
		pwm_rx_value3 |= (3 << 2);
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, TANSEN_TM_PWM1_S1, pwm_rx_value1);
	snd_soc_write(codec, TANSEN_TM_PWM2_S1, pwm_rx_value2);
	snd_soc_write(codec, TANSEN_TM_PWM3_S1, pwm_rx_value3);

	pwm_rx_value1 = snd_soc_read(codec, TANSEN_TM_PWM1_S2);
	pwm_rx_value2 = snd_soc_read(codec, TANSEN_TM_PWM2_S2);
	pwm_rx_value3 = snd_soc_read(codec, TANSEN_TM_PWM3_S2);

	rx_value &= ~0xC0;
	pwm_rx_value1 &= ~0x3;
	pwm_rx_value2 &= ~0x3;
	pwm_rx_value3 &= ~0x3;

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /*normal bit normal frame*/
		rx_value |= (0 << 6);
		pwm_rx_value1 |= 0;
		pwm_rx_value2 |= 0;
		pwm_rx_value3 |= 0;
		break;
	case SND_SOC_DAIFMT_IB_IF: /*inv. bit inv. frame*/
		rx_value |= (3 << 6);
		pwm_rx_value1 |= 3;
		pwm_rx_value2 |= 3;
		pwm_rx_value3 |= 3;
		break;
	case SND_SOC_DAIFMT_IB_NF: /*inv. bit normal frame*/
		rx_value |= (2 << 6);
		pwm_rx_value1 |= 2;
		pwm_rx_value2 |= 2;
		pwm_rx_value3 |= 2;
		break;
	case SND_SOC_DAIFMT_NB_IF: /*normal bit inv. frame*/
		rx_value |= (1 << 6);
		pwm_rx_value1 |= 1;
		pwm_rx_value2 |= 1;
		pwm_rx_value3 |= 1;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, TANSEN_TM_PWM1_S2, pwm_rx_value1);
	snd_soc_write(codec, TANSEN_TM_PWM2_S2, pwm_rx_value2);
	snd_soc_write(codec, TANSEN_TM_PWM3_S2, pwm_rx_value3);
	snd_soc_write(codec, TANSEN_TM_IP_TOP6, tx_value);
	snd_soc_write(codec, TANSEN_TM_IP_TOP7, rx_value);

	return 0;
}


static void tansen_gti_set_overides(unsigned int reg, unsigned int *value)
{
	/* some of the overrides get dropped when components get powered down */
	switch (reg) {
	case  TANSEN_TM_IP_TOP2:
		*value |= 0x80;
		break;
	case TANSEN_TM_MICBIAS_1:
		*value |= 0x20;
		break;
	case TANSEN_TM_OP_TOP2:
		*value |= 0x80;
		break;
	case TANSEN_TM_OP_TOP3:
		*value |= 0x40;
		break;
	case TANSEN_TM_R_PGA3:
		*value |= 0x01;
		break;
	case TANSEN_TM_L_PGA3:
		*value |= 0x01;
		break;
	case TANSEN_TM_IP_TOP3:
		*value |= 0xF0;
		break;
	};
}

static unsigned int
tansen_gti_read(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case TANSEN_TM_IP_TOP2:
		return cache_tm_ip_top2;
	case TANSEN_TM_IP_TOP4:
		return cache_tm_ip_top4;
	case TANSEN_TM_IP_TOP5:
		return cache_tm_ip_top5;
	default:
		return gti_read(((struct tansen_priv *)
				 codec->control_data)->ctrl,
				reg);
	}
}

static int
tansen_gti_write(struct snd_soc_codec *codec, unsigned int reg,
		 unsigned int value)
{
	tansen_gti_set_overides(reg, &value);

	switch (reg) {
	case TANSEN_TM_IP_TOP2:
		cache_tm_ip_top2 = value;
		break;
	case TANSEN_TM_IP_TOP4:
		cache_tm_ip_top4 = value;
		break;
	case TANSEN_TM_IP_TOP5:
		cache_tm_ip_top5 = value;
		break;
	}

	gti_write(((struct tansen_priv *)codec->control_data)->ctrl,
		  reg, value);

	return 0;
}


#define TANSEN_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)
/*
 * The DAC supports 128,256 & 512 sample rates but our I2S h/w only support 256
 * or 384 fs, so we can only support (@ 256fs):
 *   48KHz @ 12.288MHz MCLK
 *   96KHz @ 24.576MHz MCLK
 *   32KHz @ 8.192MHz MCLK
 */
#define TANSEN_RATES (SNDRV_PCM_RATE_32000 \
		     | SNDRV_PCM_RATE_48000 \
		     | SNDRV_PCM_RATE_96000)

static struct snd_soc_dai_ops tansen_dai_ops = {
	.hw_params	= tansen_hw_params,
	.set_sysclk	= tansen_set_dai_sysclk,
	.set_fmt	= tansen_set_dai_fmt,
};

static struct snd_soc_dai_driver tansen_dai = {
	.name = "tansen",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 6,
		.rates = TANSEN_RATES,
		.formats = TANSEN_FORMATS,
		},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TANSEN_RATES,
		.formats = TANSEN_FORMATS,
		},

	.ops = &tansen_dai_ops,
};


static int tansen_soc_probe(struct snd_soc_codec *codec)
{
	u32 val;

	codec->control_data = dev_get_drvdata(codec->dev);

	/*
	 * Allow overriding of SoC Registers by GTI i/f for control of
	 * Input mux, Mic bias, Input + Output gain.
	 */

	val = snd_soc_read(codec, TANSEN_TM_IP_TOP2);
	val |= 0x90;
	snd_soc_write(codec, TANSEN_TM_IP_TOP2, val);


	val = snd_soc_read(codec, TANSEN_TM_OP_TOP2);
	val |= 0x80;
	snd_soc_write(codec, TANSEN_TM_OP_TOP2, val);

	val = snd_soc_read(codec, TANSEN_TM_IP_TOP3);
	val |= 0xF0;
	snd_soc_write(codec, TANSEN_TM_IP_TOP3, val);

	/* Program I2S Routing:
	 *	 Data0 -> Chan2
	 *	 Data1 -> Chan1
	 *	 Data2 -> Chan3
	 * We do this presumably because the headphone Amp is on PDM C+D
	 */

	val = snd_soc_read(codec, TANSEN_TM_PWM1_S1);
	val &= ~0x3;
	val |= 2;
	snd_soc_write(codec, TANSEN_TM_PWM1_S1, val);
	val = snd_soc_read(codec, TANSEN_TM_PWM2_S1);
	val &= ~0x3;
	val |= 1;
	snd_soc_write(codec, TANSEN_TM_PWM2_S1, val);
	val = snd_soc_read(codec, TANSEN_TM_PWM3_S1);
	val &= ~0x3;
	val |= 3;
	snd_soc_write(codec, TANSEN_TM_PWM3_S1, val);

	val = snd_soc_read(codec, TANSEN_TM_IP_TOP6);
	val &= ~0x80;
	val |= 0;
	snd_soc_write(codec, TANSEN_TM_IP_TOP6, val);

	val = snd_soc_read(codec, TANSEN_TM_IP_TOP7);
	val &= ~0x80;
	val |= 0;
	snd_soc_write(codec, TANSEN_TM_IP_TOP7, val);


	return 0;
}

static int tansen_soc_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_tansen = {
	.probe		= tansen_soc_probe,
	.remove		= tansen_soc_remove,
	.write		= tansen_gti_write,
	.read		= tansen_gti_read,

	.controls	= tansen_snd_controls,
	.num_controls	= ARRAY_SIZE(tansen_snd_controls),
	.dapm_widgets	= tansen_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tansen_dapm_widgets),
	.dapm_routes = intercon,
	.num_dapm_routes = ARRAY_SIZE(intercon),

};

static const struct of_device_id tansen_of_match[] = {
	{ .compatible = "cosmic,tansen", },
	{},
};
MODULE_DEVICE_TABLE(of, tansen_of_match);

static int tansen_platform_probe(struct platform_device *pdev)
{
	struct resource *mem;
	struct tansen_priv *tansen;
	int ret;

	tansen = kzalloc(sizeof(*tansen), GFP_KERNEL);
	if (!tansen)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tansen->ctrl = devm_request_and_ioremap(&pdev->dev, mem);

	if (!tansen->ctrl) {
		dev_err(&pdev->dev, "unable to ioremap registers\n");
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, (void *)tansen);

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_tansen,
				     &tansen_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to register CODEC: %d\n", ret);
		goto out;
	}

	return 0;

out:
	kfree(tansen);
	return ret;
}

static int tansen_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver tansen_codec_driver = {
	.driver = {
			.name = "tansen-codec",
			.owner = THIS_MODULE,
			.of_match_table = tansen_of_match,
	},

	.probe = tansen_platform_probe,
	.remove = tansen_platform_remove,
};

module_platform_driver(tansen_codec_driver);

MODULE_DESCRIPTION("ASoC CosmicCircuits Tansen audio driver");
MODULE_AUTHOR("Imagination Technologies");
MODULE_LICENSE("GPL");
