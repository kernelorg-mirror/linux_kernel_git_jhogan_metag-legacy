/*
 * Copyright (C) 2009,2010 Imagination Technologies Limited.
 *
 * Quantum TouchScreen Controller driver.
 */

#ifndef _INPUT_TS_QT5480_H
#define _INPUT_TS_QT5480_H

#include <linux/ioctl.h>

/* Our IOCTL family group */
#define QT5480_IOCTL	'G'

/* Send a Calibrate Command */
#define QT5480_CALIBRATE		_IOW(QT5480_IOCTL, 1, int)

/* Switch ON / OFF the device */
#define QT5480_POWER			_IOW(QT5480_IOCTL, 2, int)

/* Enable / Disable Debug tracking in the PDP Memory (TFT) */
#define QT5480_DEBUG			_IOW(QT5480_IOCTL, 3, int)

/* Raw register access - not normally for general use! */

/* Read a single register */
#define QT5480_GETREG			_IOWR(QT5480_IOCTL, 4, struct ts_qt5480_frame)
/* Write a single register */
#define QT5480_SETREG			_IOWR(QT5480_IOCTL, 5, struct ts_qt5480_frame)

/* touch-screen mapping data */
typedef struct {
	unsigned short x_sensor_res;
	unsigned short x_screen_res;
	unsigned short x_flip;
	unsigned short x_sensor_size;
	unsigned short x_screen_size;
	short x_sensor_offset;

	unsigned short y_sensor_res;
	unsigned short y_screen_res;
	unsigned short y_flip;
	unsigned short y_sensor_size;
	unsigned short y_screen_size;
	short y_sensor_offset;

} ts_qt5480_mapping_t;

/* register entry in the QT5480 configuration table */
typedef struct {
	unsigned char set;
	unsigned char value;
} ts_qt5480_conf_reg_t;

/* QT5xx0 registers */
enum {
	QT_CHIP_ID = 0,
	QT_CODE_VERSION,
	QT_CALIBRATE,
	QT_RESET,
	QT_BACKUP_REQUEST,
	QT_ADDRESS_PTR,
	QT_EEPROM_CHKSUM,
	QT_KEY_STATUS_0 = 8,
	QT_KEY_STATUS_4 = 12,
	QT_GENERAL_STATUS_1 = 14,
	QT_GENERAL_STATUS_2,
	QT_TOUCHSCR_0_X,
	QT_TOUCHSCR_0_Y = 18,
	QT_TOUCHSCR_1_X = 20,
	QT_SLIDER_0 = 20,
	QT_TOUCHSCR_1_Y = 22,
	QT_SLIDER_4 = 24,
	QT_FORCE_SNS = 26,
	QT_KEY_GATE_STATUS,
	QT_TOUCH_0_GESTURE = 28,
	QT_TOUCH_1_GESTURE = 32,
	QT_RESERVED_1,
	QT_CHAN_1_DELTA = 256,
	QT_CHAN_1_REF = 352,
	QT_RESERVED_2 = 448,
	QT_KEY_CONTROL = 512,
	QT_THRESHOLD = 560,
	QT_BL = 608,
	QT_LP_MODE = 656,
	QT_MIN_CYC_TIME,
	QT_AWAKE_TIMEOUT,
	QT_TRIGGER_CONTROL,
	QT_GUARD_KEY_ENABLE,
	QT_TOUCHSCR_SETUP,
	QT_TOUCHSCR_LEN,
	QT_SLIDER_1_LEN = 662,
	QT_TOUCHSCR_HYST = 668,
	QT_SLIDER_1_HYST = 668,
	QT_GPO_CONTROL = 674,
	QT_NDRIFT,
	QT_PDRIFT,
	QT_NDIL,
	QT_SDIL,
	QT_NRD,
	QT_DHT,
	QT_FORCE_THRESH,
	QT_CLIP_LIMIT_X,
	QT_CLIP_LIMIT_Y,
	QT_LIN_OFFSET_X,
	QT_LIN_TABLE_X = 686,
	QT_LIN_OFFSET_Y = 702,
	QT_LIN_TABLE_Y = 704,
	QT_BURST_CONTROL = 720,
	QT_STATUS_MASK,
	QT_POSITION_FILTER,
	QT_TOUCH_SIZE_RES,
	QT_TOUCHSCR_PLATEAU,
	QT_SLEW_RATE,
	QT_MED_FILT_LEN,
	QT_SIG_IIR_CONTROL,
	QT_TOUCHDOWN_POS_HYST,
	QT_GEST_CONFIG = 734,
	QT_TAP_TIMEOUT,
	QT_DRAG_TIMEOUT,
	QT_FLICK_TIMEOUT,
	QT_PRESS_SHORT_TIMEOUT,
	QT_PRESS_LONG_TIMEOUT,
	QT_PRESS_RPT_TIMEOUT,
	QT_FLICK_THR_LSB = 742,
	QT_FLICK_THR_MSB,
	QT_DRAG_THR_LSB,
	QT_DRAG_THR_MSB,

	QT_MAX_REG = 748,
};

/* touch-screen frame */
struct ts_qt5480_frame {
	__le16 addr;
	unsigned char data[5];
	int stat;
};

struct qt5480_platform_data {
	/* Function to poll change status. */
	int (*poll_status)(void);
	/* Physical mapping of the touch sensor. */
	ts_qt5480_mapping_t *phy_map;
	/* Touch screen configuration. */
	ts_qt5480_conf_reg_t *config;
};

#endif
