/*
 * SiI9022a HMDI Transmitter I2C interface driver.
 * Copyright (C) 2010,2011 Imagination Technologies.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <video/edid.h>
#include <asm/soc-tz1090/hdmi-video.h>
#include <asm/soc-tz1090/hdmi-audio.h>

#include "sii9022a.h"
#include "edid.h"

#define EDID_ADDRESS	0x50

/* Private driver data */
struct sii9022a_data {
	struct i2c_client	*client;
	struct clk		*pix_clk;

	u32			revision;	/* The chip revision */
	u8			hdmi_sink;	/* Is the connected device
						 * HDMI or DVI ? */

	u8			interlace;	/* Interlaced output mode */
	u8			polarity;	/* VSYNC pol << 4 | HSYNC pol */

	struct mutex		lock;		/* Protects fbinfo and specs */
	struct fb_info		*fbinfo;	/* Framebuffer to attach to */
	struct fb_monspecs	specs;		/* Monitor specs gleaned from
						 * EDID data */
	struct list_head	modedb;		/* Display mode db */
	struct spinlock		mode_lock;	/* Protects current_mode */
	struct fb_videomode	current_mode;	/* Current videomode */
	bool			audio_enabled;	/* enable audio output? */

	unsigned int		state;		/* Current state of xmitter */
#define HDMI_STATE_RESET	0		/* Error state/Post reset */
#define HDMI_STATE_UNPLUGGED	1		/* Initialised */
#define HDMI_STATE_PLUGGED	2		/* Cable plugged, no rx sense */

	unsigned int		deferred_edid;	/* Just cos hardware sucks... */

	unsigned char		int_mask;	/* Interrupt mask */
	unsigned char		int_stat;

	struct notifier_block	fb_notify;	/* framebuffer notification */
};

/* EDID data for later processing */
static unsigned char hdmi_edid[128];
static unsigned char *hdmi_ext_edid;
static unsigned int hdmi_ext_edid_len;

/* Misc device node */
static int hdmi_misc_open(struct inode *inode, struct file *file)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return 0;
}

static ssize_t hdmi_misc_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos)
{
	if (*ppos > 128)
		*ppos = 0;

	if (count + *ppos > 128)
		count = 128 - *ppos;

	copy_to_user(buffer, &hdmi_edid[*ppos], count);

	*ppos += count;
	return count;
}

const static struct file_operations hdmi_misc_fops = {
	.open		= hdmi_misc_open,
	.read		= hdmi_misc_read,
};

static struct miscdevice hdmi_misc_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "hdmi-i2c",
	.fops		= &hdmi_misc_fops,
};

/* ----------------------- EDID ----------------------- */
static int sii9022a_read_edid(struct i2c_client *client, int addr, u8 *buf)
{
	int old_addr = client->addr;
	int ret = 0, scratch = 0, i;
	unsigned char tmp = '\0';
	client->addr = addr;

	for (i = 0; i < 128; i += ret) {
		/* i2c_smbus_read_block_data only returns the first 32 bytes */
		ret = i2c_master_send(client, &tmp, 1);
		if (ret != 1) {
			ret = -EIO;
			goto done;
		}

		ret = i2c_master_recv(client, &buf[i], 128);
		if (ret <= 0) {
			if (!ret)
				ret = -EIO;
			goto done;
		}
	}

	/* See how many extension blocks there are  - none signifies DVI */
	scratch = buf[126];
	if (!scratch) {
		ret = 0;
		goto done;
	}

	hdmi_ext_edid = kzalloc(scratch * 128, GFP_KERNEL);
	if (!hdmi_ext_edid) {
		dev_err(&client->dev,
				"Cannot allocate memory for Extended EDID "
				"block(s) - number requested %d\n", scratch);
		ret = -ENOMEM;
	} else {
		hdmi_ext_edid_len = scratch;
		for (i = 0; i < scratch; i++) {
			ret = i2c_master_recv(client,
					hdmi_ext_edid + (i * 128), 128);
		}
		ret = scratch;
	}

done:
	client->addr = old_addr;
	return ret;
}

static void sii9022a_process_cea_timings(struct sii9022a_data *data,
		unsigned char *ext_block)
{
	struct i2c_client *client = data->client;
	struct fb_var_screeninfo var;
	struct fb_videomode fbvm;
	int offset, num_dtds;
	unsigned char *block;

	if ((*ext_block != 0x02) && (*(ext_block + 0x01) != 0x03))
		return;

	/* This is a CEA EDID Timing Extension block */
	offset = *(ext_block + 0x02);
	if (offset == 0x00)
		return;	/* There are no DTD blocks and no non-DTD data */

	num_dtds = *(ext_block + 0x03);
	dev_dbg(&client->dev, "CEA EDID Timing block.\n"
		"Display support:\nUnderscan is %s\nBasic audio is %s\n"
		"YCbCr 4:4:4 is %s\nYCbCr 4:2:2 is %s\n"
		"Number of native DTDs: %d\n",
		(num_dtds & 0x80) ? "supported" : "not supported",
		(num_dtds & 0x40) ? "supported" : "not supported",
		(num_dtds & 0x20) ? "supported" : "not supported",
		(num_dtds & 0x10) ? "supported" : "not supported",
		(num_dtds & 0x0f));

	do {
		/* Parse DTD - taken from fb_parse_edid */
		block = ext_block + offset;

		memset(&var, 0, sizeof(struct fb_var_screeninfo));

		var.xres = var.xres_virtual = H_ACTIVE;
		var.yres = var.yres_virtual = V_ACTIVE;
		var.height = var.width = 0;
		var.right_margin = H_SYNC_OFFSET;
		var.left_margin = (H_ACTIVE + H_BLANKING) -
			(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
		var.upper_margin = V_BLANKING - V_SYNC_OFFSET -
			V_SYNC_WIDTH;
		var.lower_margin = V_SYNC_OFFSET;
		var.hsync_len = H_SYNC_WIDTH;
		var.vsync_len = V_SYNC_WIDTH;
		var.pixclock = PIXEL_CLOCK;
		var.pixclock /= 1000;
		var.pixclock = KHZ2PICOS(var.pixclock);

		if (HSYNC_POSITIVE)
			var.sync |= FB_SYNC_HOR_HIGH_ACT;
		if (VSYNC_POSITIVE)
			var.sync |= FB_SYNC_VERT_HIGH_ACT;

		memset(&fbvm, 0, sizeof(fbvm));
		fb_var_to_videomode(&fbvm, &var);
		fb_add_videomode(&fbvm, &data->modedb);

		offset += DETAILED_TIMING_DESCRIPTION_SIZE;
	} while (offset < 110);
}

/* remove modes that we cannot support even if the monitor can */
static void sii902a_filter_modelist(struct sii9022a_data *data)
{
	struct list_head *pos, *n;
	struct fb_modelist *modelist;
	struct fb_videomode *m;
	struct hdmi_platform_data *pdata;
	u32 min_pixclock;

	pdata = data->client->dev.platform_data;
	if (!pdata->max_pixfreq)
		return;

	min_pixclock = KHZ2PICOS(pdata->max_pixfreq/1000);

	list_for_each_safe(pos, n, &data->modedb) {
		modelist = list_entry(pos, struct fb_modelist, list);
		m = &modelist->mode;
		if (m->pixclock < min_pixclock) {
			list_del(pos);
			kfree(pos);
		}
	}
}

/* ------------------ Hotplug Event ------------------- */

/*
 * Replace the specified framebuffer's mode list with a copy of our modedb
 * based on the monitor's EDID.
 * Console semaphore and data->lock must both be held (in order).
 */
static void sii9022a_store_modes(struct fb_info *fbinfo,
				 struct sii9022a_data *data)
{
	struct fb_modelist *modelist;
	struct list_head *pos;
	LIST_HEAD(old_list);

	list_splice(&fbinfo->modelist, &old_list);
	INIT_LIST_HEAD(&fbinfo->modelist);
	list_for_each(pos, &data->modedb) {
		modelist = list_entry(pos, struct fb_modelist, list);
		if (fb_add_videomode(&modelist->mode, &fbinfo->modelist))
			break;
	}
	if (fb_new_modelist(fbinfo)) {
		fb_destroy_modelist(&fbinfo->modelist);
		list_splice(&old_list, &fbinfo->modelist);
	} else {
		fb_destroy_modelist(&old_list);
	}

	return;
}

static int sii9022a_handle_unplug(struct sii9022a_data *data)
{
	int ret = 0, status = data->int_stat, retries = 0;

	if (data->state == HDMI_STATE_UNPLUGGED)
		goto done;

	/* Drop the power state to low power */
	i2c_smbus_write_byte_data(data->client, SII9022A_POWER_STATE,
			PWRSTATE_PWR_LOW);

	while (status & (INT_STAT_HOTPLUG | INT_STAT_RX_SENSE)) {
		if (++retries > 5) {
			dev_err(&data->client->dev, "Unplug event timed out\n");
			goto done;
		}
		status = i2c_smbus_read_byte_data(data->client,
				SII9022A_INT_STATUS);
	}

	dev_dbg(&data->client->dev, "Cable detect dropped.\n");

	/* Clear the edid data */
	memset(hdmi_edid, 0, 128);
	kfree(hdmi_ext_edid);
	hdmi_ext_edid = NULL;

	fb_destroy_modelist(&data->modedb);

	data->state = HDMI_STATE_UNPLUGGED;

	/* Setup Interrupt Enable - listen for just Hotplug interrupts. */
	data->int_mask = INT_HOTPLUG;
	i2c_smbus_write_byte_data(data->client, SII9022A_INT_ENABLE,
		data->int_mask);
done:
	return ret;
}

static int sii9022a_handle_hotplug(struct sii9022a_data *data)
{
	int ret = 0, scratch = 0, i = 0;

	/*
	 * Read EDID data into the EDID buffer
	 * First, request DDC bus access
	 */
	scratch = i2c_smbus_read_byte_data(data->client, SII9022A_TPI_SYS_CTL);
	scratch |= SYS_CTL_DDC_REQ;
	ret = i2c_smbus_write_byte_data(data->client,
			SII9022A_TPI_SYS_CTL, scratch);

	do {
		scratch = i2c_smbus_read_byte_data(data->client,
				SII9022A_TPI_SYS_CTL);
	} while (!(scratch & SYS_CTL_DDC_GRANT));

	/* We have pass-through access to the DDC bus after we ack it */
	ret = i2c_smbus_write_byte_data(data->client, SII9022A_TPI_SYS_CTL,
			SYS_CTL_DDC_ACK);

	/*
	 * Do EDID stuffs!! EDID is at address 0x50. However, we need to use
	 * raw i2c for the transfer...
	 */
	ret = sii9022a_read_edid(data->client, EDID_ADDRESS, hdmi_edid);

	/* Release the passthrough so we can continue issuing commands */
	do {
		i2c_smbus_write_byte_data(data->client,
				SII9022A_TPI_SYS_CTL, 0x00);

		scratch = i2c_smbus_read_byte_data(data->client,
				SII9022A_TPI_SYS_CTL);
	} while (scratch & SYS_CTL_DDC_ACK);

	if (ret < 0) {
		/* We don't have a sink connected... */
		goto done;
	}

	/* ret holds whether this is an HDMI or DVI sink */
	if (ret)
		data->hdmi_sink = SYS_CTL_OUTPUT_HDMI;
	else
		data->hdmi_sink = SYS_CTL_OUTPUT_DVI;

	i2c_smbus_write_byte_data(data->client, SII9022A_TPI_SYS_CTL,
			SYS_CTL_TDMS_OUTN | data->hdmi_sink);

	/* Build fb mode db */
	console_lock();
	mutex_lock(&data->lock);
	fb_edid_to_monspecs(hdmi_edid, &data->specs);
	fb_videomode_to_modelist(data->specs.modedb, data->specs.modedb_len,
			&data->modedb);
	if (hdmi_ext_edid) {
		/* process the extended edid stuff */
		for (i = 0; i < hdmi_ext_edid_len; i++) {
			sii9022a_process_cea_timings(data,
					hdmi_ext_edid + (i * 128));
		}
	}
	sii902a_filter_modelist(data);
	if (data->fbinfo)
		sii9022a_store_modes(data->fbinfo, data);
	mutex_unlock(&data->lock);
	console_unlock();

	data->state = HDMI_STATE_PLUGGED;

	/* Start looking for Receiver Sense interrupts */
	data->int_mask = INT_HOTPLUG;
	i2c_smbus_write_byte_data(data->client, SII9022A_INT_ENABLE,
		data->int_mask);

	ret = 0;
done:
	return ret;
}

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
/*
 * Values taken from IEC60958-3 specification (Table 1 channel status)
 * and the sil9022a spec.
 *
 *  byte 0: 0x02 - Consumer, LPCM, Non copyright, no pre-emp, mode 0
 *  byte 1: 0x00 - Category Code
 *  byte 2: 0x00 - source and channel number 0 (do not take into account)
 *  byte 3: 0x02 - 48kHz, clock accuracy level II (normal accuracy)
 *  byte 4: 0x0b - 24bit, original sampling frequency not indicated
 */


static unsigned char audio_header[5] = {0x02, 0x00, 0x00, 0x02, 0x0B};

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO_51
/*
 *  Audio Info Frame See Section 6.6 of IEC CEA-861-E (for bytes D0-D9)
 *  and Table 16 of SIL-PR-1032 (The HDMI chips Programmer Reference)
 *  byte 0: 0xC2 - Frame enabled , repeat enabled, Audio Info Frame
 *  byte 1: 0x84 - InfoFrame, type audio
 *  byte 2: 0x01 - version 1
 *  byte 3: 0x0a - length 10 bytes
 *  byte 4: 0x42 - data checksum = 0x100 - (Sum of bytes 1-3, 5-14 mod 256)
 *  byte 5 (D0): 0x15 - 6 channel PCM (Data byte 0)
 *  byte 6 (D1): 0x0f - 48kHz, 24bit
 *  byte 7 (D2): 0x00 - extended data formats
 *  byte 8 (D3): 0x0B - standard5.1 speaker layout FL,FR,LFE,C,SR,SL
 *  byte 9 (D4): 0x00 - 0db level shift
 *  byte 10 (D5): 0x00 - downmix allowed
 *  byte 11 (D6): 0x00 - reserved
 *  byte 12 (D7): 0x00 - reserved
 *  byte 13 (D8): 0x00 - reserved
 *  byte 14 (D9): 0x00 - reserved
 */
static unsigned char audio_frame[15] = {0xc2, 0x84, 0x01, 0x0a, 0x42, 0x15,
		0x0F, 0x00, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#else
/*
 *  Audio Info Frame See Section 6.6 of IEC CEA-861-E
 *  and Table 16 of SIL-PR-1032 (The HDMI chips Programmer Reference)
 *  byte 0: 0xc2 - Frame enabled , repeat enabled, Audio Info Frame
 *  byte 1: 0x84 - InfoFrame, type audio
 *  byte 2: 0x01 - version 1
 *  byte 3: 0x0a - length 10 bytes
 *  byte 4: 0x51 - data checksum 0x100 - (Sum of bytes 1-3, 5-14 mod 256)
 *  byte 5: 0x11 - 2 channel PCM
 *  byte 6: 0x0f - 48kHz, 24bit
 *  byte 7 (D2): 0x00 - extended data formats
 *  byte 8 (D3): 0x00 - reserved
 *  byte 9 (D4): 0x00 - reserved
 *  byte 10 (D5): 0x00 - reserved
 *  byte 11 (D6): 0x00 - reserved
 *  byte 12 (D7): 0x00 - reserved
 *  byte 13 (D8): 0x00 - reserved
 *  byte 14 (D9): 0x00 - reserved
 */
static unsigned char audio_frame[15] = {0xc2, 0x84, 0x01, 0x0a, 0x51, 0x11,
		0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#endif

#endif

static unsigned char avi_info[14] = {0x00, 0x12, 0x28, 0x00, 0x04, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

#define ASPECT_RATIO_NONE	0x08
#define ASPECT_RATIO_4_3	0x18
#define ASPECT_RATIO_16_9	0x28

struct video_mode_map {
	u16 xres;
	u16 yres;
	u8	video_code;
	u8	refresh;
	u8	aspect_ratio;
	u8	overscan;
};

#define MAX_XRES	2048
#define MAX_YRES	2048
#define DIFF(a, b) (a > b ? a - b : b - a)

static struct video_mode_map vmode_map[] = {
	{ 1, 1, 0, 60, ASPECT_RATIO_NONE, 0 },
	{ 640, 480, 1, 60, ASPECT_RATIO_4_3, 1 },
	{ 720, 480, 3, 60, ASPECT_RATIO_16_9, 0 },
	{ 720, 576, 17, 50, ASPECT_RATIO_4_3, 0 },
	{ 800, 600, 0, 60, ASPECT_RATIO_4_3, 0 },
	{ 1024, 768, 0, 60, ASPECT_RATIO_4_3, 0 },
	{ 1280, 720, 4, 60, ASPECT_RATIO_16_9, 0 },
	{ 1280, 720, 19, 50, ASPECT_RATIO_16_9, 0 },
	{ 1280, 768, 0, 60, ASPECT_RATIO_16_9, 0 },
	{ 1360, 768, 0, 60, ASPECT_RATIO_16_9, 0 },
	{ 1680, 1050, 0, 60, ASPECT_RATIO_16_9, 0 },
	{ 1920, 1080, 33, 25, ASPECT_RATIO_16_9, 0 },
	{ 1920, 1080, 34, 30, ASPECT_RATIO_16_9, 0 },
	{ MAX_XRES+1, MAX_YRES+1, 0, 0, ASPECT_RATIO_NONE, 0 },
};

static inline u8 cksum(u8 *buf, int len, int init)
{
	int i, cksum = init;

	for (i = 0; i < len; i++)
		cksum += buf[i];

	return 0x100 - cksum;
}

static const struct video_mode_map *fb_to_vmm(const struct fb_videomode * fbvm)
{
	int i = 0;

	for (i = 0; (vmode_map[i].xres < MAX_YRES); i++) {
		if (fbvm->xres > vmode_map[i].xres)
			continue;
		if (i && (fbvm->xres <= vmode_map[i].xres) &&
				(fbvm->yres <= vmode_map[i].yres)) {
			if (DIFF(fbvm->refresh, vmode_map[i].refresh) < 3)
				/* refresh rate diff < 3Hz */
				return &vmode_map[i];
			else
				continue;
		}

		if (fbvm->yres > vmode_map[i].yres) /* no similar found */
			return &vmode_map[0];
	}
	return &vmode_map[0];
}

static void sii9022a_update_avi_infoframe(const struct video_mode_map *vmm)
{
	avi_info[1] = 0x10 | vmm->overscan;
	avi_info[2] = vmm->aspect_ratio;
	avi_info[4] = vmm->video_code;

	avi_info[0] = cksum(avi_info, 14, 0x91);

	return;
}

static void sii9022a_audio_set_enabled(struct sii9022a_data *data, bool en)
{
	int i;
	uint8_t scratch;

	data->audio_enabled = en;

	scratch = i2c_smbus_read_byte_data(data->client, 0x40);
	scratch |= 0x1;
	i2c_smbus_write_byte_data(data->client, 0x40, scratch);
	msleep(100);
	scratch &= ~0x1;
	i2c_smbus_write_byte_data(data->client, 0x40, scratch);

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	if (en) {
#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO_51
		/* mute, select i2s and PCM */
		i2c_smbus_write_byte_data(data->client, 0x26, 0xB1);
#else
		/* mute, select i2s, multi-channel layout and PCM */
		i2c_smbus_write_byte_data(data->client, 0x26, 0x91);
#endif
		/*
		 * rising edge, 256fps, left low, right justified,
		 * MSB first.
		 * Sony mode (no delay(shift) to first bit)
		 *
		 * Note 1: as the Comet I2S out block expects 24 bit data left
		 * aligned from the DMA and ALSA provides 24 bit data
		 * right aligned we configure the I2S out block in 32 bit mode
		 * we then get 24 bit data out of the I2S right aligned, which
		 * conforms to the Sony right aligned I2S timings.
		 *
		 * Note 2: the HDMI chip expects 24bit audio in a 24 + 24 frame
		 * the Comet I2S out block cannot supply this, we have to use a
		 * 32 + 32 frame (data right aligned due to reasons above), this
		 * generally works but we get slight crackling on full range
		 * audio, this can be reduced by limiting the gain of the PCM
		 * audio being played to be less than the full 24 bits.
		 *
		 * 16 bit audio in a 16 + 16 frame is better but I2S in
		 * is broken in 16bit mode on Comet.
		 */
		i2c_smbus_write_byte_data(data->client, 0x20, 0x95);

		/*
		 * ALSAs channel order doesn't match the HDMI standard channel order
		 * so swap channel 2-3 and 4-5 here at the fifo input selection.
		 *
		 * Also swap the L/R ordering of channels 1,2 and 5,6.
		 */

		i2c_smbus_write_byte_data(data->client, 0x1f, 0x84);
		i2c_smbus_write_byte_data(data->client, 0x1f, 0xA1);
		i2c_smbus_write_byte_data(data->client, 0x1f, 0x96);

		/* sample size and frequency defined by stream header*/
		i2c_smbus_write_byte_data(data->client, 0x27, 0xD8);

		/* Input word length */
		i2c_smbus_write_byte_data(data->client, SII9022A_INTERNAL_PAGE, 0x02);
		i2c_smbus_write_byte_data(data->client, SII9022A_INDEXED_REG, 0x24);
		scratch = i2c_smbus_read_byte_data(data->client, SII9022A_IND_REG_VAL);
		scratch &= 0xF0;
		scratch |= 0x0B;
		i2c_smbus_write_byte_data(data->client, SII9022A_IND_REG_VAL, scratch);

		/* Header Layout Settings */
		i2c_smbus_write_byte_data(data->client, SII9022A_INTERNAL_PAGE, 0x02);

		i2c_smbus_write_byte_data(data->client, SII9022A_INDEXED_REG, 0x2F);
		scratch = i2c_smbus_read_byte_data(data->client, SII9022A_IND_REG_VAL);
		scratch &= ~0x02;
		scratch |= 0x02;
		i2c_smbus_write_byte_data(data->client, SII9022A_IND_REG_VAL, scratch);
		i2c_smbus_write_block_data(data->client, 0x21, 5, audio_header);

		/*
		 * write audio info frame.
		 *
		 * Note the Programmers reference says to program the info frame data
		 * with a single block write, but when we do this the data reads back
		 * incorrectly and we cannot get 5.1 to work. Programming a byte at a
		 * time in a loop appears to work fine.
		 *
		 */
		for(i=0; i<15; i++)
			i2c_smbus_write_byte_data(data->client, 0xBF+i, audio_frame[i]);

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO_51
		i2c_smbus_write_byte_data(data->client, 0x26, 0xA1);
#else
		i2c_smbus_write_byte_data(data->client, 0x26, 0x81);
#endif
	} else {
#endif /* CONFIG_TZ1090_01XX_HDMI_AUDIO */

		/* disable audio interface */
		i2c_smbus_write_byte_data(data->client, 0x26, 0x11);

		/* enable 1-1 I2Sn to FIFOn mapping, disabled */
		for (i = 0; i < 4; i++)
			i2c_smbus_write_byte_data(data->client, 0x1f,
				0x00 | (i << 4) | i);

		/* disable audio interface */
		i2c_smbus_write_byte_data(data->client, 0x26, 0x01);

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	}
#endif
}

static int sii9022a_change_res(struct sii9022a_data *data)
{
	const struct video_mode_map *vmm;
	int ret = 0, scratch = 0, htotal, vtotal, refresh, pixclk;
	unsigned char vid_res[8];

	scratch = i2c_smbus_read_byte_data(data->client, SII9022A_TPI_SYS_CTL);
	scratch |= SYS_CTL_AV_MUTE;
	ret = i2c_smbus_write_byte_data(data->client, SII9022A_TPI_SYS_CTL,
			scratch);

	msleep(128);	/* wait 128ms to allow info frames to be sent */

	/* Turn off TDMS */
	ret = i2c_smbus_write_byte_data(data->client, SII9022A_POWER_STATE,
			PWRSTATE_PWR_LOW);
	scratch = i2c_smbus_read_byte_data(data->client, SII9022A_TPI_SYS_CTL);
	scratch |= SYS_CTL_TDMS_OUTN;
	ret = i2c_smbus_write_byte_data(data->client, SII9022A_TPI_SYS_CTL,
			scratch);

	/* Setup Input mode - 8bit RGB*/
	i2c_smbus_write_byte_data(data->client,
			SII9022A_INPUT_FORMAT, IN_FMT_RGB);

	/* Set the output mode - HDMI RGB with no c/s conversion */
	i2c_smbus_write_byte_data(data->client, 0x0a, 0x00);

	pixclk = clk_get_rate(data->pix_clk) / 10000;

	spin_lock_bh(&data->mode_lock);
	htotal = data->current_mode.xres +
		 data->current_mode.left_margin +
		 data->current_mode.right_margin +
		 data->current_mode.hsync_len;
	vtotal = data->current_mode.yres +
		 data->current_mode.upper_margin +
		 data->current_mode.lower_margin +
		 data->current_mode.vsync_len;
	vmm = fb_to_vmm(&data->current_mode);
	spin_unlock_bh(&data->mode_lock);
	refresh = 1000000 / (htotal * vtotal / pixclk);

	vid_res[0] = pixclk & 0xff;
	vid_res[1] = (pixclk & 0xff00) >> 8;
	vid_res[2] = refresh & 0xff;
	vid_res[3] = (refresh & 0xff00) >> 8;
	vid_res[4] = htotal & 0xff;
	vid_res[5] = (htotal & 0xff00) >> 8;
	vid_res[6] = vtotal & 0xff;
	vid_res[7] = (vtotal & 0xff00) >> 8;

	i2c_smbus_write_i2c_block_data(data->client, SII9022A_PXLCLK_LSB,
		8, vid_res);

	/* Set the AVI Info frame - HDMI only */
	if (data->hdmi_sink) {
		/* AVI Info Frame stuff */
		sii9022a_update_avi_infoframe(vmm);
		i2c_smbus_write_i2c_block_data(data->client, SII9022A_AVIINFO,
				0x0e, avi_info);
	}

	/* Audio mode */
	sii9022a_audio_set_enabled(data, data->audio_enabled);

	ret = i2c_smbus_write_byte_data(data->client, SII9022A_POWER_STATE,
			PWRSTATE_PWR_ON);

	/* Enable video output */
	scratch = i2c_smbus_read_byte_data(data->client, SII9022A_TPI_SYS_CTL);
	scratch &= ~(SYS_CTL_TDMS_OUTN);
	ret = i2c_smbus_write_byte_data(data->client, SII9022A_TPI_SYS_CTL,
			scratch);

	scratch = i2c_smbus_read_byte_data(data->client, SII9022A_TPI_SYS_CTL);
	scratch &= ~(SYS_CTL_AV_MUTE);
	ret = i2c_smbus_write_byte_data(data->client, SII9022A_TPI_SYS_CTL,
			scratch);
	/* Pixel repetition - same as default */
	return ret;
}

static int sii9022a_set_res(struct fb_info *info, struct sii9022a_data *data)
{
	const struct fb_videomode *new_mode;

	new_mode = fb_match_mode(&info->var, &info->modelist);
	dev_dbg(info->dev, "%s - %dx%d@%d\n", __FUNCTION__, new_mode->xres,
			new_mode->yres, new_mode->refresh);
	/* only if mode has actually changed */
	if (new_mode && !fb_mode_is_equal(&data->current_mode, new_mode)) {
		/* this is the only place to write to current_mode */
		spin_lock_bh(&data->mode_lock);
		data->current_mode = *new_mode;
		spin_unlock_bh(&data->mode_lock);
		sii9022a_change_res(data);
	}

	return 0;
}

/* -------------------- Work Queue -------------------- */
static irqreturn_t sii9022a_irq_thread(int irq, void *priv)
{
	struct sii9022a_data *data = priv;
	int ret =0;
	u8 status = 0;

	if (data->state == HDMI_STATE_RESET)
		return IRQ_HANDLED;

	/* Do we need locking here ? */
	status = i2c_smbus_read_byte_data(data->client, SII9022A_INT_STATUS);

	if (data->deferred_edid) {
		data->deferred_edid = 0;
		msleep(500);
		ret = sii9022a_handle_hotplug(data);
		if (ret) {
			dev_err(&data->client->dev,
				"EDID read error or sink not connected.\n");
			return IRQ_HANDLED;
		}
		if (data->current_mode.xres)
			sii9022a_change_res(data);
	}


	/* Check the state of the interrupt against our mask */
	if (!(status & data->int_mask))
		return IRQ_HANDLED;

	/* Is this a hotplug based event? */
	if (!(status & INT_HOTPLUG))
		return IRQ_HANDLED;

	if (data->state == HDMI_STATE_PLUGGED) {
		if (!(status & INT_STAT_HOTPLUG))
			sii9022a_handle_unplug(data);
	}
	else if (data->state == HDMI_STATE_UNPLUGGED) {
		/* Wait for the 500ms that is needed for the
		 * connection debounce */
		if (status & INT_STAT_HOTPLUG) {
			msleep(500);
			data->deferred_edid = 1;
		}
	}

	/* Clear down the interrupt pending states */
	i2c_smbus_write_byte_data(data->client, SII9022A_INT_STATUS, status);
	return IRQ_HANDLED;
}

static irqreturn_t sii9022a_thread_wake(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

/* ------------- Framebuffer Notification ------------- */
static int sii9022a_fb_notification(struct notifier_block *self,
		unsigned long event, void *evdata)
{
	struct fb_event *event_data = evdata;
	struct fb_info *info = event_data->info;
	struct sii9022a_data *data;

	data = container_of(self, struct sii9022a_data, fb_notify);

	if (event == FB_EVENT_FB_REGISTERED) {
		mutex_lock(&data->lock);
		/* Only attach to the first registered fb */
		if (data->fbinfo) {
			mutex_unlock(&data->lock);
			return 0;
		}
		data->fbinfo = info;
		sii9022a_store_modes(info, data);
		mutex_unlock(&data->lock);

		/* also set the video mode */
		event = FB_EVENT_MODE_CHANGE;
	}
	if (data->fbinfo != info)
		return 0;

	switch (event) {
	case FB_EVENT_FB_UNREGISTERED:
		mutex_lock(&data->lock);
		data->fbinfo = NULL;
		mutex_unlock(&data->lock);
		break;

	case FB_EVENT_MODE_CHANGE:
		if (sii9022a_set_res(info, data))
			dev_err(info->dev, "Unable to change resolution.\n");
		break;

	case FB_EVENT_SUSPEND:
		dev_dbg(info->dev, "FB Suspend\n");
		break;

	case FB_EVENT_RESUME:
		dev_dbg(info->dev, "FB Resume\n");
		break;

	case FB_EVENT_BLANK:
		dev_dbg(info->dev, "FB Blank\n");
		break;
	}

	return 0;
}

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO

static struct sii9022a_data *zero1sp_hdmi;

bool zero1sp_hdmi_audio_get_enabled(void)
{
	if (!zero1sp_hdmi)
		return false;
	return zero1sp_hdmi->audio_enabled;
}
EXPORT_SYMBOL_GPL(zero1sp_hdmi_audio_get_enabled);

void zero1sp_hdmi_audio_set_enabled(bool en)
{
	if (!zero1sp_hdmi)
		return;
	sii9022a_audio_set_enabled(zero1sp_hdmi, en);
}
EXPORT_SYMBOL_GPL(zero1sp_hdmi_audio_set_enabled);
#endif

/* --------------- Chip initialisation ---------------- */
static int sii9022a_chip_init(struct i2c_client *client,
		struct sii9022a_data *data)
{
	int ret = 0, rev = 0, scratch = 0;
	int retries = 0;

	/* Enable TPI mode */
	do {
		ret = i2c_smbus_write_byte_data(client,
				SII9022A_RESET_AND_INIT, 0x00);
		if (++retries > 5) {
			dev_err(&client->dev,
				"I2C Bus timed out on initialisation.\n");
			goto out;
		}
	} while (ret);

	/*
	 * When we can read a value from the revision register, the device has
	 * initialised.
	 */
	do {
		ret = i2c_smbus_read_byte_data(client, SII9022A_REV_DEV_ID);
		if (ret < 0) {
			dev_err(&client->dev, "HDMI transmitter not connected."
				" Error %d\n", ret);
			goto out;
		}
	} while (ret != 0xb0);	/* Maybe include 0xb4 for SiI9136/9334 */

	/* Get revision data */
	rev = ret << 16;	/* Device ID, normally 0xb0 */
	/* Production revision */
	rev |= i2c_smbus_read_byte_data(client, SII9022A_REV_PROD_ID) << 8;
	/* TPI revision */
	rev |= i2c_smbus_read_byte_data(client, SII9022A_REV_TPI_ID);

	if (rev == 0xb00000)
		dev_info(&client->dev, "SiI9022/9024 HDMI Transmitter.\n");
	else if (rev == 0xb00203)
		dev_info(&client->dev, "SiI9022a/9024a HDMI Transmitter.\n");

	data->revision = rev;

	/* Power up */
	i2c_smbus_write_byte_data(client, SII9022A_POWER_STATE,
			PWRSTATE_PWR_ON);

	/* Enable source termination */
	if (rev == 0xb00203) {
		i2c_smbus_write_byte_data(client, SII9022A_INTERNAL_PAGE, 0x01);
		i2c_smbus_write_byte_data(client, SII9022A_INDEXED_REG, 0x82);
		scratch = i2c_smbus_read_byte_data(client,
				SII9022A_IND_REG_VAL);
		scratch |= 0x01;
		i2c_smbus_write_byte_data(client,
				SII9022A_IND_REG_VAL, scratch);
	}

	/* Input bus defaults to: 1x clock, full pixel wide bus, falling edge
	 * latching and no pixel repetition */

	/* YC input mode defaults to: don't swap LSB/MSB, lower 12 bits select
	 * DDR, non gap mode disabled, normal YC input mode */

	/* Sync register recommends DE_ADJ to be cleared */
	i2c_smbus_write_byte_data(client, SII9022A_SYNC_METHOD, 0x00);

	/* Get interlace and polarity */
	ret = i2c_smbus_read_byte_data(client, SII9022A_SYNC_POLARITY);
	data->interlace = (ret & 0x04) >> 2;
	data->polarity = ((ret & 0x02) << 3) | (ret & 0x01);

	ret = i2c_smbus_read_byte_data(client, SII9022A_INT_STATUS);
	dev_dbg(&client->dev, "Initial Status: 0x%02x\n", ret);

	/*
	 * Detect if a monitor is already connected (e.g. it may have been set
	 * up by the bootloader).
	 */
	if (ret & INT_STAT_HOTPLUG) {
		/* assume it's been plugged in for a while, so no delay */
		ret = sii9022a_handle_hotplug(data);
		if (ret)
			dev_err(&client->dev,
				"EDID read error or sink not connected.\n");
		else if (data->current_mode.xres)
			sii9022a_change_res(data);
	}

	/* Setup Interrupt Enable - listen for just Hotplug interrupts. */
	data->int_mask = INT_HOTPLUG;
	if (data->state == HDMI_STATE_RESET)
		data->state = HDMI_STATE_UNPLUGGED;
	i2c_smbus_write_byte_data(client, SII9022A_INT_ENABLE,
		data->int_mask);

	ret = 0;
out:
	return ret;
}

/* --------- Probe, Remove, Suspend & Resume ---------- */
static int sii9022a_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct sii9022a_data *sii9022a;
	struct hdmi_platform_data *pdata = NULL;

	pdata = client->dev.platform_data;
	if (!pdata) {
		ret = -ENODEV;
		dev_err(&client->dev, "No platform data defined!\n");
		goto out;
	}

	/* Get some private driver data space */
	sii9022a = kzalloc(sizeof(struct sii9022a_data), GFP_KERNEL);
	if (!sii9022a) {
		ret = -ENOMEM;
		dev_err(&client->dev,
			"No memory available for HDMI I2C interface.\n");
		goto out;
	}
	mutex_init(&sii9022a->lock);
	spin_lock_init(&sii9022a->mode_lock);
	INIT_LIST_HEAD(&sii9022a->modedb);

	/* Keep a reference to the I2C client we are in */
	sii9022a->client = client;
	sii9022a->state = HDMI_STATE_RESET;
	sii9022a->audio_enabled = true;

	ret = sii9022a_chip_init(client, sii9022a);
	if (ret) {
		dev_err(&client->dev,
			"HDMI Transmitter initialisation failed; "
			"Error: %d\n", ret);
		goto free_hdmi_dev;
	}

	sii9022a->pix_clk = clk_get(&client->dev, pdata->pix_clk);
	if (IS_ERR(sii9022a->pix_clk)) {
		dev_err(&client->dev,
			"Could not get pixel clock named \"%s\"\n",
			pdata->pix_clk);
		goto free_hdmi_dev;
	}

	i2c_set_clientdata(client, sii9022a);

	/* Setup the IRQ - use default thread wakey routine */
	ret = request_threaded_irq(client->irq, sii9022a_thread_wake,
			sii9022a_irq_thread, IRQF_ONESHOT,
			"hdmi-event-irq", sii9022a);
	if (ret) {
		dev_err(&client->dev, "Unable to setup HDMI IRQ.\n");
		goto free_pix_clk;
	}

	sii9022a->fb_notify.notifier_call = sii9022a_fb_notification;
	ret = fb_register_client(&sii9022a->fb_notify);
	if (ret) {
		dev_err(&client->dev,
			"Unable to register framebuffer notifier (%d)\n", ret);
		/* carry on anyway */
	}

	/*
	 * If a framebuffer is already registered, we'll have missed it, so
	 * fake a framebuffer register event.
	 */
	console_lock();
	if (num_registered_fb) {
		struct fb_event ev;
		ev.info = registered_fb[0];
		sii9022a_fb_notification(&sii9022a->fb_notify,
			FB_EVENT_FB_REGISTERED, &ev);
	}
	console_unlock();

	/* Register a misc device node */
	ret = misc_register(&hdmi_misc_device);
	if (ret) {
		dev_err(&client->dev,
			"Unable to register device node - Error %d\n", ret);
		goto free_hdmi_irq;
	}

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	zero1sp_hdmi = sii9022a;
#endif

	dev_info(&client->dev,
			"SiI922x HDMI Transmitter probed successfully.\n");
	goto out;

free_hdmi_irq:
	free_irq(client->irq, sii9022a);
free_pix_clk:
	clk_put(sii9022a->pix_clk);
free_hdmi_dev:
	kfree(sii9022a);
out:
	return ret;
}

static int sii9022a_remove(struct i2c_client *client)
{
	struct sii9022a_data *sii9022a;

	sii9022a = i2c_get_clientdata(client);

	fb_unregister_client(&sii9022a->fb_notify);
	misc_deregister(&hdmi_misc_device);
	free_irq(client->irq, sii9022a);
	clk_put(sii9022a->pix_clk);
	kfree(sii9022a);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sii9022a_suspend(struct device *dev)
{
	/* Set transmitter power mode to D2 */
	return 0;
}

static int sii9022a_resume(struct device *dev)
{
	/* Set transmitter power mode to D0 */
	return 0;
}
#else
#define sii9022a_suspend	NULL
#define sii9022a_resume		NULL
#endif

static SIMPLE_DEV_PM_OPS(sii9022a_pmops, sii9022a_suspend, sii9022a_resume);


/* ----------- Compulsory I2C declarations ------------ */
static struct i2c_device_id sii9022a_ids[] = {
	{ "sii9022a-tpi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sii9022a_ids)

/* Driver structure */
static struct i2c_driver sii9022a_driver = {
	.driver	= {
		.name	= "sii9022a-tpi",
		.owner	= THIS_MODULE,
		.pm	= &sii9022a_pmops,
	},

	.id_table	= sii9022a_ids,
	.probe		= sii9022a_probe,
	.remove		= sii9022a_remove,
};

/* ------------ Compulsory module routines ------------ */
static int __init sii9022a_init(void)
{
#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	zero1sp_hdmi = NULL;
#endif
	return i2c_add_driver(&sii9022a_driver);
}
module_init(sii9022a_init);

static void __exit sii9022a_exit(void)
{
#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO
	zero1sp_hdmi = NULL;
#endif
	i2c_del_driver(&sii9022a_driver);
}
module_exit(sii9022a_exit);

MODULE_DESCRIPTION("SiI9022a HDMI Transmitter I2C Command interface driver");
MODULE_AUTHOR("Imagination Technologies");
MODULE_LICENSE("GPL");
