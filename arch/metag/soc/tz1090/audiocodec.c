/*
 *  audiocodec.c - setup comet audio codec, DAC & ADC.
 *
 *  Copyright (C) 2010 Imagination Technologies
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <sound/tansen.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/soc-tz1090/defs.h>
#include <asm/soc-tz1090/audiocodec.h>
#include <asm/soc-tz1090/hdmi-audio.h>

static struct kobject *ac_kobj;

/*
 * TM_PWM*_S1 registers
 * Apparently not where indicated in the register list, but in 'reserved' regions?!?
 */
static unsigned long pwms1_regs[AUDIOCODEC_NUM_STEREOPAIRS] = {
	0x826, /* TW_PWM1_S1 */
	0x8AA, /* TW_PWM2_S1 */
	0x926, /* TW_PWM3_S1 */
};

struct str_int_map_t {
	char *name;
	int val;
};

static struct str_int_map_t map_bool[] = {
	{ "false", 0 },
	{ "true", 1 },
	{ NULL, -1 },
};

static struct str_int_map_t map_samplewidth[] = {
	{ "16b", AUDIOCODEC_SAMPLEWIDTH_16 },
	{ "20b", AUDIOCODEC_SAMPLEWIDTH_20 },
	{ "24b", AUDIOCODEC_SAMPLEWIDTH_24 },
	{ "32b", AUDIOCODEC_SAMPLEWIDTH_32 },
	{ NULL, -1 },
};

static struct str_int_map_t map_input[] = {
	{ "mic", AUDIOCODEC_INPUT_MIC },
	{ "line", AUDIOCODEC_INPUT_LINE },
	{ "ipod", AUDIOCODEC_INPUT_IPOD },
	{ "mic_differential", AUDIOCODEC_INPUT_MIC_DIFFERENTIAL },
	{ NULL, -1 },
};

static struct str_int_map_t map_mute[] = {
	{ "none", AUDIOCODEC_MUTE_NONE },
	{ "hard", AUDIOCODEC_MUTE_HARD },
	{ "90db", AUDIOCODEC_MUTE_90DB },
	{ "squarewave", AUDIOCODEC_MUTE_SQUAREWAVE },
	{ NULL, -1 },
};

static struct str_int_map_t map_frame[] = {
	{ "1616", AUDIOCODEC_FRAME_1616 },
	{ "3232", AUDIOCODEC_FRAME_3232 },
};

static struct str_int_map_t map_clock[] = {
	{ "256fs", AUDIOCODEC_CLOCK_256FS },
	{ "384fs", AUDIOCODEC_CLOCK_384FS },
};

static struct str_int_map_t map_i2sclock[] = {
	{ "disabled", AUDIOCODEC_I2SCLOCK_DISABLED },
	{ "xtal1", AUDIOCODEC_I2SCLOCK_XTAL1 },
	{ "xtal2", AUDIOCODEC_I2SCLOCK_XTAL2 },
	{ "sys_undeleted", AUDIOCODEC_I2SCLOCK_SYS_UNDELETED },
	{ "adc_pll", AUDIOCODEC_I2SCLOCK_ADC_PLL },
	{ NULL, -1 },
};

static struct str_int_map_t map_preset[] = {
	{ "none", AUDIOCODEC_PRESET_NONE },
	{ "phono", AUDIOCODEC_PRESET_PHONO },
#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	{ "hdmi", AUDIOCODEC_PRESET_HDMI },
	{ "default", AUDIOCODEC_PRESET_PHONO | AUDIOCODEC_PRESET_HDMI },
#else
	{ "default", AUDIOCODEC_PRESET_PHONO },
#endif
	{ NULL, -1 },
};

static int map_lookup(struct str_int_map_t *map, const char *str)
{
	size_t len = strlen(str);
	size_t cmplen = len;
	int i;

	if (len == 0)
		return -1;

	if (str[len-1] == '\n')
		cmplen--;

	for (i = 0; map[i].name; i++) {
		if (!strncmp(map[i].name, str, cmplen))
			return map[i].val;
	}

	return -1;
}

static int map_lookup_flags(struct str_int_map_t *map, const char *str)
{
	size_t len;
	int i, val = 0;
	bool change;

	do {
		change = false;

		for (i = 0; map[i].name; i++) {
			len = strlen(map[i].name);

			if (strncmp(map[i].name, str, len))
				continue;

			if (str[len] == ',' || str[len] == '\n' ||
			    str[len] == 0) {
				val |= map[i].val;
				change = true;
				str += len + (str[len] == ',' ? 1 : 0);
			}
		}
	} while (change);

	return val;
}

static ssize_t map_string(struct str_int_map_t *map, int val, char *buf,
	size_t sz)
{
	int i;

	for (i = 0; map[i].name; i++) {
		if (map[i].val == val)
			return snprintf(buf, sz, "%s\n", map[i].name);
	}

	return snprintf(buf, sz, "err\n");
}

static ssize_t map_string_flags(struct str_int_map_t *map, int val, char *buf,
	size_t sz)
{
	int i;
	char *str = buf;
	size_t written, rem = sz - 1;

	for (i = 0; map[i].name; i++) {
		if (!map[i].val)
			continue;

		if ((map[i].val & val) == map[i].val) {
			if (buf < str) {
				*str++ = ',';
				*str = 0;
			}

			written = snprintf(str, rem, "%s", map[i].name);
			str += written;
			rem -= written;

			val &= ~map[i].val;
		}
	}

	if (str == buf) {
		/* outputted nothing */
		/* look for a zero mapping */

		for (i = 0; map[i].name; i++) {
			if (!map[i].val)
				return snprintf(buf, sz, "%s\n", map[i].name);
		}
	}

	*str++ = '\n';
	*str = 0;
	return (ssize_t)(str - buf);
}

static void ac_reset(void)
{
	writel(1, AUDIO_OUT_SOFT_RESET);
	udelay(1000);
	writel(0, AUDIO_OUT_SOFT_RESET);
	udelay(1000);
}

static int ac_dac_power_get(bool *pwr)
{
	uint32_t hpctrl = readl(CR_AUDIO_HP_CTRL);
	*pwr = !(hpctrl & AUDIO_PWDN_BG_OP);
	return 0;
}

static int ac_dac_power_set(bool pwr)
{
	uint32_t hpctrl = readl(CR_AUDIO_HP_CTRL);
	uint32_t adcctrl = readl(CR_AUDIO_ADC_CTRL);

	/* GTI port must be in reset for power up to work */
	gti_reset((void __iomem *)CR_TOP_AUDGTI_CTRL, 1);

	if (pwr) {
		/* Power up bandgap */
		hpctrl &= ~(AUDIO_PWDN_BG_OP | AUDIO_PWDN_BG_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(100);

		/* Reset bandgap filter */
		hpctrl |= (AUDIO_RST_BG_OP | AUDIO_RST_BG_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(250);

		hpctrl &= ~(AUDIO_RST_BG_OP | AUDIO_RST_BG_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(100);

		/* Power up PLL */
		hpctrl &= ~AUDIO_PWDN_PLL;
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(100);

		/* Take digital blocks out of reset */
		hpctrl &= ~(AUDIO_RSTB_DIG_OP | AUDIO_RSTB_DIG_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(100);

		/* Power up outputs */
		hpctrl |= (AUDIO_PSCNT_PWM_F | AUDIO_PSCNT_PWM_E |
			AUDIO_PSCNT_PWM_D | AUDIO_PSCNT_PWM_C |
			AUDIO_PSCNT_PWM_B | AUDIO_PSCNT_PWM_A);
		hpctrl |= (AUDIO_PSCNTHP_R | AUDIO_PSCNTHP_L);
		writel(hpctrl, CR_AUDIO_HP_CTRL);

		adcctrl |= (AUDIO_PSCNTADC_R | AUDIO_PSCNTADC_L);
		writel(adcctrl, CR_AUDIO_ADC_CTRL);

		udelay(100);

		/* Reset analogue filters */
		hpctrl |= (AUDIO_RSTB_ANA_OP | AUDIO_RSTB_ANA_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(250);

		hpctrl &= ~(AUDIO_RSTB_ANA_OP | AUDIO_RSTB_ANA_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
		udelay(250);
	} else {
		hpctrl |= (AUDIO_PWDN_BG_OP | AUDIO_PWDN_BG_IP);
		hpctrl |= (AUDIO_RSTB_DIG_OP | AUDIO_RSTB_DIG_IP);
		writel(hpctrl, CR_AUDIO_HP_CTRL);
	}

	/* Take the GTI port out of reset */
	gti_reset((void __iomem *)CR_TOP_AUDGTI_CTRL, 0);

	return 0;
}

static int ac_i2s_out_clock_get(int *clksrc)
{
	uint32_t val = readl(CR_TOP_CLKENAB);
	bool sw0, sw1, sw2;

	if (!(val & (1 << CR_TOP_I2S_1_EN_BIT))) {
		*clksrc = AUDIOCODEC_I2SCLOCK_DISABLED;
	} else {
		val = readl(CR_TOP_CLKSWITCH);
		sw0 = !!(val & (1 << CR_TOP_I2S_0_SW_BIT));
		sw1 = !!(val & (1 << CR_TOP_I2S_1_SW_BIT));
		sw2 = !!(val & (1 << CR_TOP_I2S_2_SW_BIT));

		if (!sw1) {
			if (sw2)
				*clksrc = AUDIOCODEC_I2SCLOCK_SYS_UNDELETED;
			else
				*clksrc = AUDIOCODEC_I2SCLOCK_XTAL1;
		} else if (!sw0)
			*clksrc = AUDIOCODEC_I2SCLOCK_XTAL2;
		else
			*clksrc = AUDIOCODEC_I2SCLOCK_ADC_PLL;
	}

	return 0;
}

static int ac_i2s_out_clock_set(int clksrc)
{
	uint32_t val = readl(CR_TOP_CLKENAB);

	if (clksrc == AUDIOCODEC_I2SCLOCK_DISABLED) {
		val &= ~(1 << CR_TOP_I2S_1_EN_BIT);
		writel(val, CR_TOP_CLKENAB);
		return 0;
	}

	val |= (1 << CR_TOP_I2S_1_EN_BIT);
	writel(val, CR_TOP_CLKENAB);

	val = readl(CR_TOP_CLKSWITCH);
	val &= ~(1 << CR_TOP_I2S_0_SW_BIT);
	val &= ~(1 << CR_TOP_I2S_1_SW_BIT);
	val &= ~(1 << CR_TOP_I2S_2_SW_BIT);

	switch (clksrc) {
	case AUDIOCODEC_I2SCLOCK_XTAL1:
		break;
	case AUDIOCODEC_I2SCLOCK_XTAL2:
		val |= (1 << CR_TOP_I2S_1_SW_BIT);
		break;
	case AUDIOCODEC_I2SCLOCK_SYS_UNDELETED:
		val |= (1 << CR_TOP_I2S_2_SW_BIT);
		break;
	case AUDIOCODEC_I2SCLOCK_ADC_PLL:
		val |= (1 << CR_TOP_I2S_0_SW_BIT);
		val |= (1 << CR_TOP_I2S_1_SW_BIT);
		break;
	default:
		return -EINVAL;
	}

	writel(val, CR_TOP_CLKSWITCH);
	return 0;
}

static int ac_i2s_out_clockdiv_get(int *div)
{
	*div = readl(CR_TOP_I2SCLK_DIV) & 0xff;
	return 0;
}

static int ac_i2s_out_clockdiv_set(int div)
{
	writel(div & 0xff, CR_TOP_I2SCLK_DIV);
	return 0;
}

static int ac_i2s_out_clockdiv2_get(int *div)
{
	*div = readl(CR_TOP_I2S_DIV2) & 0x3;
	return 0;
}

static int ac_i2s_out_clockdiv2_set(int div)
{
	writel(div & 0x3, CR_TOP_I2S_DIV2);
	return 0;
}

static int ac_i2s_out_active_channels_get(int *chans)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	*chans = (val >> AUDIO_OUT_CM_ACTIVE_CHAN_SHIFT) + 1;
	return 0;
}

static int ac_i2s_out_active_channels_set(int chans)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	val &= ~0xffffc000;
	val |= (chans - 1) << AUDIO_OUT_CM_ACTIVE_CHAN_SHIFT;
	writel(val, AUDIO_OUT_CONTROL_MAIN);
	return 0;
}

static int ac_i2s_out_frame_get(int *frame)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	*frame = (val >> AUDIO_OUT_CM_FRAME_SHIFT) & AUDIO_OUT_CM_FRAME_MASK;
	return 0;
}

static int ac_i2s_out_frame_set(int frame)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	val &= ~AUDIO_OUT_CM_FRAME_MASK;
	val |= frame << AUDIO_OUT_CM_FRAME_SHIFT;
	writel(val, AUDIO_OUT_CONTROL_MAIN);
	return 0;
}

static int ac_i2s_out_master_get(bool *en)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	*en = !!(val & AUDIO_OUT_CM_MASTER);
	return 0;
}

static int ac_i2s_out_master_set(bool en)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	val &= ~AUDIO_OUT_CM_MASTER;
	if (en)
		val |= AUDIO_OUT_CM_MASTER;
	writel(val, AUDIO_OUT_CONTROL_MAIN);
	return 0;
}

static int ac_i2s_out_aclk_get(int *aclk)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	*aclk = (val >> AUDIO_OUT_CM_ACLK_SHIFT) & AUDIO_OUT_CM_ACLK_MASK;
	return 0;
}

static int ac_i2s_out_aclk_set(int aclk)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	val &= ~AUDIO_OUT_CM_ACLK_MASK;
	val |= aclk << AUDIO_OUT_CM_ACLK_SHIFT;
	writel(val, AUDIO_OUT_CONTROL_MAIN);
	return 0;
}

static int ac_i2s_out_blrclk_get(bool *en)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	*en = !!(val & AUDIO_OUT_CM_BLRCLK_EN);
	return 0;
}

static int ac_i2s_out_blrclk_set(bool en)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	val &= ~AUDIO_OUT_CM_BLRCLK_EN;
	if (en)
		val |= AUDIO_OUT_CM_BLRCLK_EN;
	writel(val, AUDIO_OUT_CONTROL_MAIN);
	return 0;
}

static int ac_i2s_out_enable_get(bool *en)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	*en = !!(val & AUDIO_OUT_CM_ME);
	return 0;
}

static int ac_i2s_out_enable_set(bool en)
{
	uint32_t val = readl(AUDIO_OUT_CONTROL_MAIN);
	val &= ~AUDIO_OUT_CM_ME;
	if (en)
		val |= AUDIO_OUT_CM_ME;
	writel(val, AUDIO_OUT_CONTROL_MAIN);
	return 0;
}

static int ac_i2swidth_dac_get(int *sw)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL, 0xe85);
	*sw = val & 0x3;
	return 0;
}

static int ac_i2swidth_dac_set(int sw)
{
	int i;
	uint32_t addr;
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL, 0xe85);
	val &= ~0x3; /* Clear TX width field */
	val |= sw;
	gti_write((void __iomem *)CR_TOP_AUDGTI_CTRL, 0xe85, val);

	for (i = 0; i < AUDIOCODEC_NUM_STEREOPAIRS; i++) {
		addr = AUDIO_OUT_BASE_ADDR + (0x20 * AUDIOCODEC_NUM_STEREOPAIRS)
			+ (0x20 * i) + 0x04;
		val = readl(addr);
		val &= ~AUDIO_OUT_CC_FORMAT_MASK;
		val |= 4 << AUDIO_OUT_CC_FORMAT_SHIFT;
		val |= AUDIO_OUT_CC_LEFT_JUST;
		writel(val, addr);
	}

	return 0;
}

static int ac_i2swidth_adc_get(int *sw)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL, 0xe86);
	*sw = val & 0x3;
	return 0;
}

static int ac_i2swidth_adc_set(int sw)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL, 0xe86);
	val &= ~0x3; /* Clear RX width field */
	val |= sw;
	gti_write((void __iomem *)CR_TOP_AUDGTI_CTRL, 0xe86, val);
	return 0;
}

static int ac_bypass_adc_get(bool *bypass)
{
	uint32_t val = readl(CR_AUDIO_HP_CTRL);
	*bypass = !(val & AUDIO_I2S_EXT);
	return 0;
}

static int ac_bypass_adc_set(bool bypass)
{
	uint32_t val = readl(CR_AUDIO_HP_CTRL);
	val &= ~AUDIO_I2S_EXT;
	if (!bypass)
		val |= AUDIO_I2S_EXT;
	writel(val, CR_AUDIO_HP_CTRL);
	return 0;
}

static int ac_input_route_get(int *route)
{
	uint32_t val = readl(CR_AUDIO_HP_CTRL);
	*route = (val & AUDIO_PGA_MODE_MASK) >> AUDIO_PGA_MODE_SHIFT;
	return 0;
}

static int ac_input_route_set(int route)
{
	uint32_t val = readl(CR_AUDIO_HP_CTRL);
	val &= ~AUDIO_PGA_MODE_MASK;
	val |= AUDIO_PGA_MODE(route);
	writel(val, CR_AUDIO_HP_CTRL);
	return 0;
}

static int ac_chan_i2sinput_get(int chan, int *in)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL,
				pwms1_regs[chan]);
	*in = (val & 0x3) - 1;
	return 0;
}

static int ac_chan_i2sinput_set(int chan, int in)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL,
				pwms1_regs[chan]);
	val &= ~0x3;  /* Clear routing */
	val |= (in + 1) & 0x3;
	gti_write((void __iomem *)CR_TOP_AUDGTI_CTRL, pwms1_regs[chan], val);
	return 0;
}

static int ac_chan_i2swidth_get(int chan, int *sw)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL,
				pwms1_regs[chan]);
	*sw = (val >> 6) & 0x3;
	return 0;
}

static int ac_chan_i2swidth_set(int chan, int sw)
{
	uint32_t val = gti_read((void __iomem *)CR_TOP_AUDGTI_CTRL,
				pwms1_regs[chan]);
	val &= ~0xc;  /* Clear width */
	val |= sw << 6;
	gti_write((void __iomem *)CR_TOP_AUDGTI_CTRL, pwms1_regs[chan], val);
	return 0;
}

static int ac_chan_vol_get(int pair, int lr, uint8_t *db)
{
	uint32_t reg_addr, value, shift = 32;

	if (lr & AUDIOCODEC_LEFT)
		shift = ((pair % 2) * 16);
	else if (lr & AUDIOCODEC_RIGHT)
		shift = (((pair % 2) * 16) + 8);

	/* Each 32 bit GAIN register represents 2 channel pairs */
	reg_addr = CR_AUDIO_GAIN0 + ((pair / 2) * sizeof(uint32_t));

	value = readl(reg_addr);
	*db = (value >> shift) & 0xFF;
	return 0;
}

static int ac_chan_vol_set(int pair, int lr, uint8_t db)
{
	uint32_t reg_addr, value, clear_mask = 0, set_mask = 0;

	if (lr & AUDIOCODEC_LEFT) {
		clear_mask += 0xFF << ((pair % 2) * 16);
		set_mask += db << ((pair % 2) * 16);
	}

	if (lr & AUDIOCODEC_RIGHT) {
		clear_mask += 0xFF << (((pair % 2) * 16) + 8);
		set_mask += db << (((pair % 2) * 16) + 8);
	}

	/* Each 32 bit GAIN register represents 2 channel pairs */
	reg_addr = CR_AUDIO_GAIN0 + ((pair / 2) * sizeof(uint32_t));
	value = readl(reg_addr);
	value &= ~clear_mask;
	value |= set_mask;
	writel(value, reg_addr);
	return 0;
}

static int ac_chan_mute_get(int pair, int lr, int *mute)
{
	uint32_t val = readl(CR_AUDIO_MUTE);
	*mute = AUDIOCODEC_MUTE_NONE;

	if (val & (lr << (16 + (pair * 2))))
		*mute |= AUDIOCODEC_MUTE_HARD;

	if (val & (lr << (8 + (pair * 2))))
		*mute |= AUDIOCODEC_MUTE_90DB;

	if (val & (lr << (0 + (pair * 2))))
		*mute |= AUDIOCODEC_MUTE_SQUAREWAVE;

	return 0;
}

static int ac_chan_mute_set(int pair, int lr, int mute)
{
	uint32_t val = readl(CR_AUDIO_MUTE);

	val &= ~(lr << (16 + (pair * 2)));
	val &= ~(lr << (8 + (pair * 2)));
	val &= ~(lr << (0 + (pair * 2)));

	if (mute & AUDIOCODEC_MUTE_HARD)
		val |= (lr << (16 + (pair * 2)));

	if (mute & AUDIOCODEC_MUTE_90DB)
		val |= (lr << (8 + (pair * 2)));

	if (mute & AUDIOCODEC_MUTE_SQUAREWAVE)
		val |= (lr << (0 + (pair * 2)));

	writel(val, CR_AUDIO_MUTE);
	return 0;
}

static int curr_preset = AUDIOCODEC_PRESET_NONE;

static ssize_t ac_preset_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return map_string_flags(map_preset, curr_preset, buf, PAGE_SIZE);
}

static ssize_t ac_preset_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int preset = map_lookup_flags(map_preset, buf);

	if (preset == 0)
		return -EINVAL;

	curr_preset = preset;
	ac_reset();

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	zero1sp_hdmi_audio_set_enabled(!!(preset & AUDIOCODEC_PRESET_HDMI));
#endif

	ac_dac_power_set(!!(preset & AUDIOCODEC_PRESET_PHONO));

	ac_chan_vol_set(0, AUDIOCODEC_LEFT | AUDIOCODEC_RIGHT, 128);
	ac_chan_mute_set(0, AUDIOCODEC_LEFT | AUDIOCODEC_RIGHT,
		AUDIOCODEC_MUTE_NONE);
	ac_i2s_out_clock_set(AUDIOCODEC_I2SCLOCK_XTAL1);
	ac_i2s_out_clockdiv_set(0);
	ac_i2s_out_clockdiv2_set(1);
	ac_i2s_out_active_channels_set(1);
	ac_i2s_out_master_set(true);
	ac_i2s_out_frame_set(AUDIOCODEC_FRAME_3232);
	ac_i2s_out_aclk_set(AUDIOCODEC_CLOCK_256FS);
	ac_i2s_out_blrclk_set(true);
	ac_i2s_out_enable_set(true);

	ac_i2swidth_dac_set(AUDIOCODEC_SAMPLEWIDTH_24);
	ac_i2swidth_adc_set(AUDIOCODEC_SAMPLEWIDTH_24);
	ac_chan_i2sinput_set(0, 1);
	ac_chan_i2swidth_set(0, AUDIOCODEC_SAMPLEWIDTH_24);
	ac_chan_i2sinput_set(1, 0);
	ac_chan_i2swidth_set(1, AUDIOCODEC_SAMPLEWIDTH_24);
	ac_chan_i2sinput_set(2, 2);
	ac_chan_i2swidth_set(2, AUDIOCODEC_SAMPLEWIDTH_24);

	ac_bypass_adc_set(false);
	ac_input_route_set(AUDIOCODEC_INPUT_LINE);

	return count;
}

static ssize_t ac_dac_power_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	bool pwr;
	if (ac_dac_power_get(&pwr))
		return -EINVAL;
	return map_string(map_bool, pwr, buf, PAGE_SIZE);
}

static ssize_t ac_dac_power_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int pwr = map_lookup(map_bool, buf);
	if (pwr == -1)
		return -EINVAL;
	if (ac_dac_power_set(!!pwr))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_active_channels_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int chans;
	if (ac_i2s_out_active_channels_get(&chans))
		return -EINVAL;
	return snprintf(buf, PAGE_SIZE, "%d\n", chans);
}

static ssize_t ac_i2s_out_active_channels_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int chans;
	if (sscanf(buf, "%d", &chans) != 1)
		return -EINVAL;
	if (ac_i2s_out_active_channels_set(chans))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_clock_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int clksrc;
	if (ac_i2s_out_clock_get(&clksrc))
		return -EINVAL;
	return map_string(map_i2sclock, clksrc, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_out_clock_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int clksrc = map_lookup(map_i2sclock, buf);
	if (clksrc == -1)
		return -EINVAL;
	if (ac_i2s_out_clock_set(clksrc))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_clockdiv_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int chans;
	if (ac_i2s_out_clockdiv_get(&chans))
		return -EINVAL;
	return snprintf(buf, PAGE_SIZE, "%d\n", chans);
}

static ssize_t ac_i2s_out_clockdiv_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int chans;
	if (sscanf(buf, "%d", &chans) != 1)
		return -EINVAL;
	if (ac_i2s_out_clockdiv_set(chans))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_clockdiv2_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int chans;
	if (ac_i2s_out_clockdiv2_get(&chans))
		return -EINVAL;
	return snprintf(buf, PAGE_SIZE, "%d\n", chans);
}

static ssize_t ac_i2s_out_clockdiv2_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int chans;
	if (sscanf(buf, "%d", &chans) != 1)
		return -EINVAL;
	if (ac_i2s_out_clockdiv2_set(chans))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_frame_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int frame;
	if (ac_i2s_out_frame_get(&frame))
		return -EINVAL;
	return map_string(map_frame, frame, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_out_frame_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int frame = map_lookup(map_frame, buf);
	if (frame == -1)
		return -EINVAL;
	if (ac_i2s_out_frame_set(frame))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_master_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	bool master;
	if (ac_i2s_out_master_get(&master))
		return -EINVAL;
	return map_string(map_bool, master, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_out_master_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int master = map_lookup(map_bool, buf);
	if (master == -1)
		return -EINVAL;
	if (ac_i2s_out_master_set(!!master))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_aclk_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int aclk;
	if (ac_i2s_out_aclk_get(&aclk))
		return -EINVAL;
	return map_string(map_clock, aclk, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_out_aclk_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int aclk = map_lookup(map_clock, buf);
	if (aclk == -1)
		return -EINVAL;
	if (ac_i2s_out_aclk_set(aclk))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_blrclk_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	bool blrclk;
	if (ac_i2s_out_blrclk_get(&blrclk))
		return -EINVAL;
	return map_string(map_bool, blrclk, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_out_blrclk_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int blrclk = map_lookup(map_bool, buf);
	if (blrclk == -1)
		return -EINVAL;
	if (ac_i2s_out_blrclk_set(!!blrclk))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_out_enable_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	bool enable;
	if (ac_i2s_out_enable_get(&enable))
		return -EINVAL;
	return map_string(map_bool, enable, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_out_enable_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int enable = map_lookup(map_bool, buf);
	if (enable == -1)
		return -EINVAL;
	if (ac_i2s_out_enable_set(!!enable))
		return -EINVAL;
	return count;
}

static ssize_t ac_bypass_adc_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	bool bypass;
	if (ac_bypass_adc_get(&bypass))
		return -EINVAL;
	return map_string(map_bool, bypass, buf, PAGE_SIZE);
}

static ssize_t ac_bypass_adc_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int bypass = map_lookup(map_bool, buf);
	if (bypass == -1)
		return -EINVAL;
	if (ac_bypass_adc_set(!!bypass))
		return -EINVAL;
	return count;
}

static ssize_t ac_input_route_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int in;
	if (ac_input_route_get(&in))
		return -EINVAL;
	return map_string(map_input, in, buf, PAGE_SIZE);
}

static ssize_t ac_input_route_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int in = map_lookup(map_input, buf);
	if (in == -1)
		return -EINVAL;
	if (ac_input_route_set(in))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_samplewidth_dac_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int sw;
	if (ac_i2swidth_dac_get(&sw))
		return -EINVAL;
	return map_string(map_samplewidth, sw, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_samplewidth_dac_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int sw = map_lookup(map_samplewidth, buf);
	if (sw == -1)
		return -EINVAL;
	if (ac_i2swidth_dac_set(sw))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_samplewidth_adc_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int sw;
	if (ac_i2swidth_adc_get(&sw))
		return -EINVAL;
	return map_string(map_samplewidth, sw, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_samplewidth_adc_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int sw = map_lookup(map_samplewidth, buf);
	if (sw == -1)
		return -EINVAL;
	if (ac_i2swidth_adc_set(sw))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_input_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int pair, in;
	if (sscanf(attr->attr.name, "i2s_input_%d", &pair) != 1)
		return -EINVAL;
	if (ac_chan_i2sinput_get(pair, &in))
		return -EINVAL;
	return snprintf(buf, PAGE_SIZE, "%d\n", in);
}

static ssize_t ac_i2s_input_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int pair, in;
	if (sscanf(attr->attr.name, "i2s_input_%d", &pair) != 1)
		return -EINVAL;
	if (sscanf(buf, "%d", &in) != 1)
		return -EINVAL;
	if (ac_chan_i2sinput_set(pair, in))
		return -EINVAL;
	return count;
}

static ssize_t ac_i2s_samplewidth_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int pair, sw;
	if (sscanf(attr->attr.name, "i2s_samplewidth_%d", &pair) != 1)
		return -EINVAL;
	if (ac_chan_i2swidth_get(pair, &sw))
		return -EINVAL;
	return map_string(map_samplewidth, sw, buf, PAGE_SIZE);
}

static ssize_t ac_i2s_samplewidth_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int pair, sw;
	if (sscanf(attr->attr.name, "i2s_samplewidth_%d", &pair) != 1)
		return -EINVAL;
	sw = map_lookup(map_samplewidth, buf);
	if (sw == -1)
		return -EINVAL;
	if (ac_chan_i2swidth_set(pair, sw))
		return -EINVAL;
	return count;
}

static int ac_lr_char(char lrchar)
{
	switch (lrchar) {
	case 'l': return AUDIOCODEC_LEFT;
	case 'r': return AUDIOCODEC_RIGHT;
	case 'b': return AUDIOCODEC_LEFT | AUDIOCODEC_RIGHT;
	default: return -EINVAL;
	}
}

static ssize_t ac_vol_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int pair, lr;
	uint8_t vol;
	char lrchar;
	if (sscanf(attr->attr.name, "vol_%d%c", &pair, &lrchar) != 2)
		return -EINVAL;
	lr = ac_lr_char(lrchar);
	if (ac_chan_vol_get(pair, lr, &vol))
		return -EINVAL;
	return snprintf(buf, PAGE_SIZE, "%d\n", (int)vol);
}

static ssize_t ac_vol_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int pair, lr, vol;
	char lrchar;
	if (sscanf(attr->attr.name, "vol_%d%c", &pair, &lrchar) != 2)
		return -EINVAL;
	if (sscanf(buf, "%d", &vol) != 1)
		return -EINVAL;
	lr = ac_lr_char(lrchar);
	if (ac_chan_vol_set(pair, lr, (uint8_t)vol))
		return -EINVAL;
	return count;
}

static ssize_t ac_mute_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int pair, lr, mute;
	char lrchar;
	if (sscanf(attr->attr.name, "mute_%d%c", &pair, &lrchar) != 2)
		return -EINVAL;
	lr = ac_lr_char(lrchar);
	if (ac_chan_mute_get(pair, lr, &mute))
		return -EINVAL;
	return map_string_flags(map_mute, mute, buf, PAGE_SIZE);
}

static ssize_t ac_mute_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int pair, lr, mute;
	char lrchar;
	if (sscanf(attr->attr.name, "mute_%d%c", &pair, &lrchar) != 2)
		return -EINVAL;
	lr = ac_lr_char(lrchar);
	mute = map_lookup_flags(map_mute, buf);
	if (ac_chan_mute_set(pair, lr, mute))
		return -EINVAL;
	return count;
}

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
static ssize_t ac_hdmi_audio_enable_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	bool en = zero1sp_hdmi_audio_get_enabled();
	return map_string(map_bool, en, buf, PAGE_SIZE);
}

static ssize_t ac_hdmi_audio_enable_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int en = map_lookup(map_bool, buf);
	if (en == -1)
		return -EINVAL;
	zero1sp_hdmi_audio_set_enabled(!!en);
	return count;
}
#endif

#define AC_ATTR(name, fnname) \
	__ATTR(name, 0666, ac_##fnname##_show, ac_##fnname##_store)

#define AC_ATTR_PERPAIR(name, fnname) \
	AC_ATTR(name##_0, fnname), \
	AC_ATTR(name##_1, fnname), \
	AC_ATTR(name##_2, fnname)

#define AC_ATTR_PERCHAN(name, fnname) \
	AC_ATTR(name##_0l, fnname), \
	AC_ATTR(name##_0r, fnname), \
	AC_ATTR(name##_1l, fnname), \
	AC_ATTR(name##_1r, fnname), \
	AC_ATTR(name##_2l, fnname), \
	AC_ATTR(name##_2r, fnname)

static struct kobj_attribute ac_kobj_attributes[] = {
	AC_ATTR(preset, preset),
	AC_ATTR(dac_power, dac_power),
	AC_ATTR(bypass_adc, bypass_adc),
	AC_ATTR(input_route, input_route),
	AC_ATTR(i2s_out_clock, i2s_out_clock),
	AC_ATTR(i2s_out_clockdiv, i2s_out_clockdiv),
	AC_ATTR(i2s_out_clockdiv2, i2s_out_clockdiv2),
	AC_ATTR(i2s_out_active_channels, i2s_out_active_channels),
	AC_ATTR(i2s_out_frame, i2s_out_frame),
	AC_ATTR(i2s_out_master, i2s_out_master),
	AC_ATTR(i2s_out_aclk, i2s_out_aclk),
	AC_ATTR(i2s_out_blrclk, i2s_out_blrclk),
	AC_ATTR(i2s_out_enable, i2s_out_enable),
	AC_ATTR(i2s_samplewidth_dac, i2s_samplewidth_dac),
	AC_ATTR(i2s_samplewidth_adc, i2s_samplewidth_adc),
	AC_ATTR_PERPAIR(i2s_input, i2s_input),
	AC_ATTR_PERPAIR(i2s_samplewidth, i2s_samplewidth),
	AC_ATTR_PERCHAN(vol, vol),
	AC_ATTR_PERCHAN(mute, mute),
#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	AC_ATTR(hdmi_audio_enable, hdmi_audio_enable),
#endif
};

static struct attribute *ac_attributes[ARRAY_SIZE(ac_kobj_attributes)+1];

static struct attribute_group ac_attr_group = {
	.attrs = ac_attributes,
};

static int __init audiocodec_init(void)
{
	int i, ret;

	ac_kobj = kobject_create_and_add("audiocodec", kernel_kobj);
	if (!ac_kobj) {
		ret = -ENOMEM;
		goto err0;
	}

	for (i = 0; i < ARRAY_SIZE(ac_kobj_attributes); i++)
		ac_attributes[i] = &ac_kobj_attributes[i].attr;
	ac_attributes[i] = NULL;

	ret = sysfs_create_group(ac_kobj, &ac_attr_group);
	if (ret)
		goto err1;

	return 0;

err1:
	kobject_put(ac_kobj);
err0:
	return ret;
}

static void audiocodec_exit(void)
{
	kobject_put(ac_kobj);
}

module_init(audiocodec_init);
module_exit(audiocodec_exit);

