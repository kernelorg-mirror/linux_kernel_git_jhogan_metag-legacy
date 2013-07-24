/*
 *  01xx.c
 *
 *  01SP/01TT TZ1090 Metamorph/Minimorph ASoC Audio Support using tansen codec.
 *
 *  Copyright:	(C) 2009-2013 Imagination Technologies
 *
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tansen.h>
#include <asm/soc-tz1090/hdmi-audio.h>
#include <asm/soc-tz1090/audiocodec.h>
#include <asm/soc-tz1090/defs.h>

#include "../codecs/tansen_gti.h"
#include "tz1090-pcm.h"
#include "tz1090-i2s.h"

struct zero1xx_priv {
	void __iomem *adc_ctrl;
	void __iomem *hp_ctrl;
	void __iomem *gti_ctrl;
};

static int zero1xx_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	/*
	 * Note: as the Comet I2S out block expects 24 bit data left aligned
	 * from the DMA and ALSA provides 24 bit data right aligned we
	 * configure the I2S out block in 32 bit mode we then get 24 bit data
	 * out of the I2S right aligned, which conforms to the Sony
	 * right aligned I2S timings.
	 */

	/*
	 * set codec DAI configuration:
	 *	Sony Right Justify Timing
	 *	normal bit clock normal frame clock
	 *	bit clock and frame clock slave
	 *
	 */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_RIGHT_J |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/*
	 * set cpu DAI configuration:
	 *	Sony Right Justify Timing
	 *	normal bit clock normal frame clock
	 *	bit clock and frame clock master
	 */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_RIGHT_J |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);

	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops zero1xx_ops = {
	.hw_params = zero1xx_hw_params,
};

static int __init zero1xx_tansen_init(struct zero1xx_priv *zero1xx)
{
	u32 audio_hp_ctrl;
	u32 audio_adc_ctrl;
	/*
	 * Brings DAC out of Reset
	 * All other config handled by DAC driver
	 */

	u32 hpctrl, adcctrl;

	hpctrl = ioread32(zero1xx->hp_ctrl);
	adcctrl = ioread32(zero1xx->adc_ctrl);

	/* GTI port must be in reset for power up to work */
	gti_reset(zero1xx->gti_ctrl, 1);

	/* Power up bandgap */
	hpctrl &= ~(AUDIO_PWDN_BG_OP | AUDIO_PWDN_BG_IP);
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(100);

	/* Reset bandgap filter */
	hpctrl |= (AUDIO_RST_BG_OP | AUDIO_RST_BG_IP);
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(250);

	hpctrl &= ~(AUDIO_RST_BG_OP | AUDIO_RST_BG_IP);
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(100);

	/* Power up PLL */
	hpctrl &= ~AUDIO_PWDN_PLL;
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(100);

	/* Take digital blocks out of reset */
	hpctrl &= ~(AUDIO_RSTB_DIG_OP | AUDIO_RSTB_DIG_IP);
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(100);

	/* Power up outputs only need PWM_C (Left) and PWM_D (right) */
	hpctrl |= (/*AUDIO_PSCNT_PWM_F | AUDIO_PSCNT_PWM_E |*/ AUDIO_PSCNT_PWM_D |
		   AUDIO_PSCNT_PWM_C /*| AUDIO_PSCNT_PWM_B | AUDIO_PSCNT_PWM_A*/);
	hpctrl |= (AUDIO_PSCNTHP_R | AUDIO_PSCNTHP_L);
	iowrite32(hpctrl, zero1xx->hp_ctrl);

	adcctrl |= (AUDIO_PSCNTADC_R | AUDIO_PSCNTADC_L);
	iowrite32(adcctrl, zero1xx->adc_ctrl);

	udelay(100);

	/* Reset analogue filters */
	hpctrl |= (AUDIO_RSTB_ANA_OP | AUDIO_RSTB_ANA_IP);
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(250);

	hpctrl &= ~(AUDIO_RSTB_ANA_OP | AUDIO_RSTB_ANA_IP);
	iowrite32(hpctrl, zero1xx->hp_ctrl);
	udelay(250);

	/* Take the GTI port out of reset */
	gti_reset(zero1xx->gti_ctrl, 0);

	/* power up */

	/* Get initial state of control registers */
	audio_hp_ctrl = ioread32(zero1xx->hp_ctrl);
	audio_adc_ctrl = ioread32(zero1xx->adc_ctrl);

	/* Need to put the GTI interface into reset for the power up to work. */
	gti_reset(zero1xx->gti_ctrl, true);

	/*
	 *	Step 1.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PWDN_BG_OP	=	0
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PWDN_BG_IP	=	0
	 *	DELAY x us
	 */
	audio_hp_ctrl &= ~(CR_AUDIO_PWDN_BG_OP_MASK <<
			   CR_AUDIO_PWDN_BG_OP_SHIFT);
	audio_adc_ctrl &= ~(CR_AUDIO_PWDN_BG_IP_MASK <<
			    CR_AUDIO_PWDN_BG_IP_SHIFT);
	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(100);

	/*
	 *	Step 2.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RST_BG_OP	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RST_BG_IP	=	1
	 *	DELAY > 250 us
	 */
	audio_hp_ctrl |= (CR_AUDIO_RST_BG_OP_MASK << CR_AUDIO_RST_BG_OP_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_RST_BG_IP_MASK << CR_AUDIO_RST_BG_IP_SHIFT);

	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(250);

	/*
	 *	Step 3.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RST_BG_OP	=	0
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RST_BG_IP	=	0
	 *	DELAY x us
	 */
	audio_hp_ctrl &= ~(CR_AUDIO_RST_BG_OP_MASK << CR_AUDIO_RST_BG_OP_SHIFT);
	audio_hp_ctrl &= ~(CR_AUDIO_RST_BG_IP_MASK << CR_AUDIO_RST_BG_IP_SHIFT);

	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(100);

	/*
	 *	Step 4.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PWDN_PLL	=	0
	 *	DELAY > 100 us
	 */
	audio_hp_ctrl &= ~(CR_AUDIO_PWDN_PLL_MASK << CR_AUDIO_PWDN_PLL_SHIFT);

	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(100);

	/*
	 *	Step 5.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RSTB_DIG_OP	=	0
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RSTB_DIG_IP	=	0
	 *	DELAY x us
	 */
	audio_hp_ctrl &= ~(CR_AUDIO_RSTB_DIG_OP_MASK <<
			   CR_AUDIO_RSTB_DIG_OP_SHIFT);
	audio_hp_ctrl &= ~(CR_AUDIO_RSTB_DIG_IP_MASK <<
			   CR_AUDIO_RSTB_DIG_IP_SHIFT);

	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(100);

	/*
	 *	Step 6.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNT_PWM_F	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNT_PWM_E	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNT_PWM_D	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNT_PWM_C	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNT_PWM_B	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNT_PWM_A	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNTHP_R	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_PSCNTHP_L	=	1
	 *
	 *	CR_AUDIO_ADC_CTRL	CR_AUDIO_PSCNTADC_R	=	1
	 *	CR_AUDIO_ADC_CTRL	CR_AUDIO_PSCNTADC_L	=	1
	 *	DELAY x us
	 */
	audio_hp_ctrl |= (CR_AUDIO_PSCNT_PWM_F_MASK <<
			  CR_AUDIO_PSCNT_PWM_F_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNT_PWM_E_MASK <<
			  CR_AUDIO_PSCNT_PWM_E_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNT_PWM_D_MASK <<
			  CR_AUDIO_PSCNT_PWM_D_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNT_PWM_C_MASK <<
			  CR_AUDIO_PSCNT_PWM_C_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNT_PWM_B_MASK <<
			  CR_AUDIO_PSCNT_PWM_B_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNT_PWM_A_MASK <<
			  CR_AUDIO_PSCNT_PWM_A_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNTHP_R_MASK <<
			  CR_AUDIO_PSCNTHP_R_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_PSCNTHP_L_MASK <<
			  CR_AUDIO_PSCNTHP_L_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_I2S_EXT_MASK <<
			  CR_AUDIO_I2S_EXT_SHIFT);
	audio_hp_ctrl &= ~(CR_AUDIO_PGA_MODE_MASK <<
			   CR_AUDIO_PGA_MODE_SHIFT);
	audio_hp_ctrl |= 0x01 << CR_AUDIO_PGA_MODE_SHIFT;
	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	audio_adc_ctrl |= (CR_AUDIO_PSCNTADC_R_MASK <<
			   CR_AUDIO_PSCNTADC_R_SHIFT);
	audio_adc_ctrl |= (CR_AUDIO_PSCNTADC_L_MASK <<
			   CR_AUDIO_PSCNTADC_L_SHIFT);
	iowrite32(audio_adc_ctrl, zero1xx->adc_ctrl);

	udelay(100);

	/*
	 *	Step 7.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RSTB_ANA_OP	=	1
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RSTB_ANA_IP	=	1
	 *	DELAY >250 us
	 */
	audio_hp_ctrl |= (CR_AUDIO_RSTB_ANA_OP_MASK <<
			  CR_AUDIO_RSTB_ANA_OP_SHIFT);
	audio_hp_ctrl |= (CR_AUDIO_RSTB_ANA_IP_MASK <<
			  CR_AUDIO_RSTB_ANA_IP_SHIFT);

	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(250);

	/*
	 *	Step 8.
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RSTB_ANA_OP	=	0
	 *	CR_AUDIO_HP_CTRL	CR_AUDIO_RSTB_ANA_IP	=	0
	 *	DELAY >250 us
	 */
	audio_hp_ctrl &= ~(CR_AUDIO_RSTB_ANA_OP_MASK <<
			   CR_AUDIO_RSTB_ANA_OP_SHIFT);
	audio_hp_ctrl &= ~(CR_AUDIO_RSTB_ANA_IP_MASK <<
			   CR_AUDIO_RSTB_ANA_IP_SHIFT);

	iowrite32(audio_hp_ctrl, zero1xx->hp_ctrl);

	udelay(250);


	/* Take the GTI interface out of reset. */
	gti_reset(zero1xx->gti_ctrl, false);

	return 0;
}

/*
 * 01SP/01TT digital audio interface glue - connects codec <--> CPU
 *
 * The codec used on the 01SP/01TT is the Cosmic circuits DAC internal
 * to the SoC.
 *
 * Note: As we now only support 1 fixed audio configuration, HDMI audio
 * just piggybacks on the back of the DAC config (when
 * CONFIG_TZ1090_01XX_HDMI_AUDIO is set) and no longer has its own codec driver
 */

static struct snd_soc_dai_link zero1xx_dai = {
	.name = "tansen",
	.stream_name = "Playback",
	.codec_dai_name = "tansen",
	.platform_name = "comet-pcm-audio",
	.ops = &zero1xx_ops,
};


/*01SP/01TT audio machine driver */
static struct snd_soc_card snd_soc_zero1xx = {
	.name = "01SP/01TT-Tansen-Audio",
	.owner = THIS_MODULE,
	.dai_link = &zero1xx_dai,
	.num_links = 1,
};

static int zero1xx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_zero1xx;
	struct zero1xx_priv zero1xx;
	struct resource *mem;
	int ret;

	zero1xx_dai.codec_name = NULL;
	zero1xx_dai.codec_of_node = of_parse_phandle(np,
				"img,audio-codec", 0);
	if (!zero1xx_dai.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'img,audio-codec' not found\n");
		return -EINVAL;
	}

	zero1xx_dai.cpu_dai_name = NULL;
	zero1xx_dai.cpu_of_node = of_parse_phandle(np,
				"img,i2s-controller", 0);
	if (!zero1xx_dai.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'img,i2s-controller' not found\n");
		return -EINVAL;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	zero1xx.adc_ctrl = devm_request_and_ioremap(&pdev->dev, mem);
	if (!zero1xx.adc_ctrl) {
		dev_err(&pdev->dev,
			"Unable to ioremap the adc ctrl registers\n");
		return -ENOMEM;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	zero1xx.hp_ctrl = devm_request_and_ioremap(&pdev->dev, mem);
	if (!zero1xx.hp_ctrl) {
		dev_err(&pdev->dev,
			"Unable to ioremap the hp ctrl registers\n");
		return -ENOMEM;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 2);

	if (!mem) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -ENODEV;
	}

	zero1xx.gti_ctrl = ioremap_nocache(mem->start,
					   mem->end - mem->start + 1);
	if (!zero1xx.gti_ctrl) {
		dev_err(&pdev->dev,
			"Unable to ioremap the gti ctrl registers\n");
		return -ENOMEM;
	}

	zero1xx_tansen_init(&zero1xx);

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);

	if (ret)
		dev_err(&pdev->dev,
			"snd_soc_register_card() failed: %d\n",
			ret);


	return ret;
}

static int zero1xx_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id tz1090_audio_of_match[] = {
	{ .compatible = "img,tz1090-audio", },
	{}
};
MODULE_DEVICE_TABLE(of, tz1090_audio_of_match);

static struct platform_driver zero1xx_driver = {
	.driver = {
		.name   = "zero1xx-audio",
		.owner  = THIS_MODULE,
		.of_match_table = tz1090_audio_of_match,
	},
	.probe          = zero1xx_probe,
	.remove         = zero1xx_remove,
};

module_platform_driver(zero1xx_driver);


/* Module information */
MODULE_AUTHOR("Imagination Technologies");
MODULE_DESCRIPTION("ALSA SoC 01SP/01TT");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:zero1xx-audio");
