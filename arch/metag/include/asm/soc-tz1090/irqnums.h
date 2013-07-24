#ifndef _TZ1090_IRQNUMS_H
#define _TZ1090_IRQNUMS_H

#include <linux/irqchip/metag-ext.h>

#include <asm/irq.h>

/*
 * Hardware IRQ numbers.
 * These are DEPRECATED, use device tree instead.
 * These can be mapped to virtual IRQ numbers using external_irq_map().
 */
#define SCB0_IRQ_NUM			 0
#define SCB1_IRQ_NUM			 1
#define SCB2_IRQ_NUM			 2
#define SDIO_DEV_IRQ_NUM		 3
#define UART0_IRQ_NUM			 4
#define UART1_IRQ_NUM			 5
#define SPI_MASTER0_IRQ_NUM		 6
#define SPI_SLAVE_IRQ_NUM		 7
#define SPI_MASTER1_IRQ_NUM		 8
#define I2S_OUT0_IRQ_NUM		 9
#define I2S_OUT1_IRQ_NUM		10
#define I2S_OUT2_IRQ_NUM		11
#define I2S_IN_IRQ_NUM			12
#define GPIO0_IRQ_NUM			13
#define GPIO1_IRQ_NUM			14
#define GPIO2_IRQ_NUM			15
#define BUSERR_IRQ_NUM			16
#define LCD_IRQ_NUM			17
#define PDC_IRQ_NUM			18
#define USB_IRQ_NUM			19
#define SDIO_HOST_IRQ_NUM		20
#define MDC0_IRQ_NUM			21
#define MDC1_IRQ_NUM			22
#define MDC2_IRQ_NUM			23
#define MDC3_IRQ_NUM			24
#define MDC4_IRQ_NUM			25
#define MDC5_IRQ_NUM			26
#define MDC6_IRQ_NUM			27
#define MDC7_IRQ_NUM			28
#define PDC_IR_IRQ_NUM			29
#define PDC_RTC_IRQ_NUM			30
#define PDC_WDOG_IRQ_NUM		31
#define UCC0_IRQ_NUM			32
#define UCC1_IRQ_NUM			33
#define TWOD_IRQ_NUM			34
#define PDP_IRQ_NUM			35
#define SOFT0_IRQ_NUM			36
#define SOFT1_IRQ_NUM			37
#define SOFT2_IRQ_NUM			38
#define SOFT3_IRQ_NUM			39
#define SOFT4_IRQ_NUM			40
#define SOFT5_IRQ_NUM			41
#define SOFT6_IRQ_NUM			42
#define SOFT7_IRQ_NUM			43

#endif /* _TZ1090_IRQNUMS_H */
