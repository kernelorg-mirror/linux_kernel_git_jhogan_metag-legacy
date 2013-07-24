/*
 * IMG Event Timer Driver interface
 *
 * Copyright (C) 2011  Imagination Technologies
 */
#ifndef EVENT_TIMER_H_
#define EVENT_TIMER_H_

#include <linux/types.h>


#define EVT_CLOCK_32K	0
#define EVT_CLOCK_SCP0	1
#define EVT_CLOCK_SCP1	2

struct evt_clock {
	__u32 src;
};

#define EVT_MAX_COUNTERS	6

#define EVT_SRC_UCC0		0
#define EVT_SRC_UCC1		1
#define EVT_SRC_TFT_HSYNC	2
#define EVT_SRC_TFT_VSYNC	3
#define EVT_SRC_TFT_2D		4
#define EVT_SRC_HEP		5
#define EVT_SRC_SCB0		6
#define EVT_SRC_SCB1		7
#define EVT_SRC_SCB2		8
#define EVT_SRC_SDIO		9
#define EVT_SRC_UART0		10
#define EVT_SRC_UART1		11
#define EVT_SRC_SPIM0		12
#define EVT_SRC_SPIS		13
#define EVT_SRC_SPIM1		14
#define EVT_SRC_I2SOUT0		15
#define EVT_SRC_I2SOUT1		16
#define EVT_SRC_I2SOUT2		17
#define EVT_SRC_I2SIN		18
#define EVT_SRC_GPIO0		19
#define EVT_SRC_GPIO1		20
#define EVT_SRC_GPIO2		21
#define EVT_SRC_SOCIF		22
#define EVT_SRC_LCD		23
#define EVT_SRC_PDC		24
#define EVT_SRC_USB		25
#define EVT_SRC_SDHOST		26
#define EVT_SRC_MDC0		27
#define EVT_SRC_MDC1		28
#define EVT_SRC_MDC2		29
#define EVT_SRC_MDC3		30
#define EVT_SRC_MDC4		31
#define EVT_SRC_MDC5		32
#define EVT_SRC_MDC6		33
#define EVT_SRC_MDC7		34
#define EVT_SRC_PDC_IR		35
#define EVT_SRC_PDC_RTC		36
#define EVT_SRC_PDC_WD		37

struct evt_event {
	__u32 counter;
	__u32 source;
	__u32 timestamp;
	__u32 txtimer;
	__u32 timeofday_sec;
	__u32 timeofday_ns;
};

#define EVTIO 0xF2

/*set clock source*/
#define EVTIO_SETCLOCK		_IOW(EVTIO, 0x40, struct evt_clock)
/*set the event to count*/
#define EVTIO_SETEVENTSRC	_IOW(EVTIO, 0x41, struct evt_event)
/*get event timestamp*/
#define EVTIO_GETEVTS		_IOWR(EVTIO, 0x42, struct evt_event)



#endif /* EVENT_TIMER_H_ */
