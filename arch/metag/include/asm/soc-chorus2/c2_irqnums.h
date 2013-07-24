#ifndef _C2_IRQNUMS_H
#define _C2_IRQNUMS_H

#include <linux/irqchip/metag-ext.h>

#include <asm/irq.h>

/*
 * Hardware IRQ numbers.
 * These are DEPRECATED, use device tree instead.
 * These can be mapped to virtual IRQ numbers using external_irq_map().
 */
#define LOCAL_BUS_IRQ_NUM		 4
#define DMA0_IRQ_NUM			32
#define DMA1_IRQ_NUM			33
#define DMA2_IRQ_NUM			34
#define DMA3_IRQ_NUM			35
#define DMA4_IRQ_NUM			36
#define DMA5_IRQ_NUM			37
#define DMA6_IRQ_NUM			38
#define DMA7_IRQ_NUM			39
#define DMA8_IRQ_NUM			40
#define DMA9_IRQ_NUM			41
#define DMA10_IRQ_NUM			42
#define DMA11_IRQ_NUM			43
#define UART1_IRQ_NUM			48
#define UART2_IRQ_NUM			49
#define RDI_IRQ_NUM			50
#define NAND_IRQ_NUM			51
#define PDP_IRQ_NUM			52
#define RSD_IRQ_NUM			64
#define MEM_STICK_IRQ_NUM		65
#define SCP_DMA_IRQ_NUM			66
#define SCP_AGC_IRQ_NUM			67
#define ECP_IRQ_NUM			68
#define ATAPI_IRQ_NUM			69
#define SPI1_DMAW_IRQ_NUM		70
#define SPI1_DMAR_IRQ_NUM		71
#define SPI1_CMP_IRQ_NUM		72
#define SPI2_DMAW_IRQ_NUM		73
#define SPI2_DMAR_IRQ_NUM		74
#define USB_IRQ_NUM			76
#define SCB1_IRQ_NUM			77
#define SCB2_IRQ_NUM			78
#define LCD_IRQ_NUM			79
#define LCD_DMA_IRQ_NUM			80
#define I2S_O0_IRQ_NUM			81
#define I2S_O1_IRQ_NUM			82
#define I2S_O2_IRQ_NUM			83
#define I2S_O3_IRQ_NUM			84
#define I2S_IN_IRQ_NUM			85
#define SPDIF_OUT_IRQ_NUM		86
#define SPDIF_IN_IRQ_NUM		87
#define GPIO_A_IRQ_NUM			88
#define GPIO_B_IRQ_NUM			89
#define GPIO_C_IRQ_NUM			90
#define GPIO_D_IRQ_NUM			91
#define GPIO_E_IRQ_NUM			92
#define GPIO_F_IRQ_NUM			93
#define GPIO_G_IRQ_NUM			94
#define GPIO_H_IRQ_NUM			95

#endif /* _C2_IRQNUMS_H */
