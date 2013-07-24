/*
 * img_dma.h
 *
 * API for IMG's DMAC Controller.
 *
 * Copyright (C) 2010,2012 Imagination Technologies Ltd.
*/

#ifndef IMG_DMA_H_
#define IMG_DMA_H_

enum img_dma_channel_state {
	IMG_DMA_CHANNEL_RESERVED,
	IMG_DMA_CHANNEL_AVAILABLE,
	IMG_DMA_CHANNEL_INUSE,
};

enum img_dma_priority {
	IMG_DMA_PRIO_BULK = 0,
	IMG_DMA_PRIO_REALTIME,
};

enum img_dma_direction {
	IMG_DMA_INVALID_DIR,
	IMG_DMA_TO_PERIPHERAL,
	IMG_DMA_FROM_PERIPHERAL,
	IMG_DMA_MEM2MEM,
};

enum img_dma_width {
	IMG_DMA_WIDTH_8 = 0,
	IMG_DMA_WIDTH_16,
	IMG_DMA_WIDTH_32,
	IMG_DMA_WIDTH_64,
	IMG_DMA_WIDTH_128,
};

int img_request_dma(int dma_channel, unsigned int periph);
int img_free_dma(int dma_channel);
int img_dma_reset(int dma_channel);
int img_dma_get_irq(int dma_channel);
int img_dma_set_direction(int dma_channel, enum img_dma_direction direction);
int img_dma_set_data_address(int dma_channel, u32 address);
int img_dma_set_length(int dma_channel, unsigned long bytes,
		enum img_dma_width width);
int img_dma_set_io_address(int dma_channel, u32 address,
		unsigned int burst_size);
int img_dma_start(int dma_channel);
int img_dma_is_finished(int dma_channel);
int img_dma_set_priority(int dma_channel, enum img_dma_priority prio);
int img_dma_get_int_status(int dma_channel, u32 *status);
int img_dma_set_int_status(int dma_channel, u32 status);
int img_dma_has_request(int dma_channel);
int img_dma_set_level_int(int dma_channel, int enable);
int img_dma_set_access_delay(int dma_channel, u8 delay);

/*List API*/
int img_dma_set_list_addr(int dma_channel, u32 address);
u32 img_dma_get_list_addr(int dma_channel);
int img_dma_start_list(int dma_channel);
int img_dma_stop_list(int dma_channel);
int img_dma_translate_sglist(int dma_channel,
			     struct scatterlist	*sg,
			     void *list,
			     dma_addr_t list_phys,
			     unsigned long len,
			     enum dma_data_direction dir,
			     u32 perip_address,
			     enum img_dma_width perip_width,
			     int access_delay);


#endif /* IMG_DMA_H_ */


