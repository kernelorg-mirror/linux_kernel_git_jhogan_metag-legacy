/*
 * tz1090-pcm.h
 */

#ifndef TZ1090PCM_H_
#define TZ1090PCM_H_

#include <linux/spinlock.h>
#include <linux/dmaengine.h>
#include <linux/img_mdc_dma.h>

extern struct snd_soc_platform_driver comet_soc_platform;

struct comet_pcm_dma_params {
	const char *name;
	int peripheral_num;
	int dma_channel;
	dma_addr_t peripheral_address;
	bool alloced;
};

#endif /* TZ1090PCM_H_ */
