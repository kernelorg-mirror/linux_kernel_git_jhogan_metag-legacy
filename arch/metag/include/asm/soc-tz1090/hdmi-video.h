/*
 * HDMI public interface (platform data et al.)
 */

#ifndef _HDMI_VIDEO_H_
#define _HDMI_VIDEO_H_

/* Sync generation */
#define HDMI_SYNC_MODE_EXTERN	0x00	/* All external signals */
#define HDMI_SYNC_MODE_GEN_DE	0x01	/* [HV]SYNC external, DE internal */
#define HDMI_SYNC_MODE_EMBEDDED	0x02	/* Embedded ITU.656 sync signals */

struct hdmi_fb_ops {
	int (*get_pixclk)(void);
};

/* Platform data container */
struct hdmi_platform_data {
	/* Name of pixel clock */
	const char		*pix_clk;

	/* max supported pixel clock */
	unsigned long		max_pixfreq;
};

#endif

