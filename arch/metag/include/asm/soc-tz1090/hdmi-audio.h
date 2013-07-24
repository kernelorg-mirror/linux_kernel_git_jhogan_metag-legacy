/*
 * HDMI Audio component interface for ASoC layer
 */

#ifndef _HDMI_AUDIO_H_
#define _HDMI_AUDIO_H_

#ifdef CONFIG_TZ1090_01XX_HDMI_AUDIO

extern bool zero1sp_hdmi_audio_get_enabled(void);
extern void zero1sp_hdmi_audio_set_enabled(bool en);
#endif

#endif
