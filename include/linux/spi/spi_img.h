#ifndef _IMG_SPI_H_
#define _IMG_SPI_H_

/* Maximum possible transfer size */
#define IMG_SPI_MAX_TRANSFER 4096

/* Platform data for SPI controller devices */
struct img_spi_master {
	u16 num_chipselect;
	/* MDC needs channel number */
	int tx_dma_channel_num;
	/* DMAC needs peripheral number */
	int tx_dma_peripheral_num;
	int rx_dma_channel_num;
	int rx_dma_peripheral_num;
	/* Clock rate (0 if it shouldn't be changed) */
	unsigned long clk_rate;
};

/* Controller data for SPI slave devices, passed as part of platform data */
struct img_spi_chip {
	u8 cs_setup;
	u8 cs_hold;
	u8 cs_delay;
};

#endif
