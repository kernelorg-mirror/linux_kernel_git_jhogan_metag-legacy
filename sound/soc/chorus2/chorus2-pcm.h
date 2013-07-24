/*
 * chorus2-pcm.h
 */

#ifndef CHORUS2PCM_H_
#define CHORUS2PCM_H_

#include <asm/soc-chorus2/dma.h>
#include <linux/spinlock.h>

extern struct snd_soc_platform_driver chorus2_soc_platform;

struct chorus2_pcm_dma_params {
	const char *name;
	int peripheral_num;
	int peripheral_address;
};

struct chorus2_runtime_data {
	int dma_ch;
	int irq_num;
	struct chorus2_pcm_dma_params *params;
	struct chorus2_dma_desc *dma_list;
	dma_addr_t dma_list_phys;
	int period_index;
	int start_delay;
	spinlock_t lock;

};

#endif /* CHORUS2PCM_H_ */
