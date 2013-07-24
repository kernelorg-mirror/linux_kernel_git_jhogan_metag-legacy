/*
 * SiI9022a HDMI Transmitter driver
 * Register Defs
 */

/* Reset Register */
#define SII9022A_RESET_AND_INIT	0xc7

/* Revision registers */
#define SII9022A_REV_DEV_ID	0x1b
#define SII9022A_REV_PROD_ID	0x1c
#define SII9022A_REV_TPI_ID	0x1d
#define SII9022A_REV_HDCP	0x30

/* Power state register */
#define SII9022A_POWER_STATE	0x1e
#define PWRSTATE_PWR_ON		0x00
#define PWRSTATE_PWR_LOW	0x02
#define PWRSTATE_PWR_V_LOW	0x03
#define PWRSTATE_PWR_VV_LOW	0x04

/* Internal registers */
#define SII9022A_INTERNAL_PAGE	0xbc
#define SII9022A_INDEXED_REG	0xbd
#define SII9022A_IND_REG_VAL	0xbe

/* Sync register */
#define SII9022A_SYNC_METHOD	0x60
#define SYNC_EMBEDDED		0x80
#define SYNC_YC_MUX_ENABLE	0x20
#define SYNC_INVERT_POLARITY	0x10
#define SYNC_ADJUST_VSYNC	0x02
#define SYNC_VSYNC_PLUS		0x01
#define SYNC_VSYNC_MINUS	0x00

/* Sync Polarity (RO) */
#define SII9022A_SYNC_POLARITY	0x61

/* Interrupts */
#define SII9022A_INT_ENABLE	0x3c
#define SII9022A_INT_STATUS	0x3d
#define INT_AUDIO_ERR		0x10
#define INT_STAT_RX_SENSE	0x08
#define INT_STAT_HOTPLUG	0x04
#define INT_RX_SENSE		0x02
#define INT_HOTPLUG		0x01

/* TPI System Control */
#define SII9022A_TPI_SYS_CTL	0x1a
#define SYS_CTL_DYNAMIC_LI	0x40
#define SYS_CTL_TDMS_OUTN	0x10
#define SYS_CTL_AV_MUTE		0x08
#define SYS_CTL_DDC_REQ		0x04
#define SYS_CTL_DDC_GRANT	0x02
#define SYS_CTL_OUTPUT_HDMI	0x01
#define SYS_CTL_OUTPUT_DVI	0x00
#define SYS_CTL_DDC_ACK		(SYS_CTL_DDC_REQ | SYS_CTL_DDC_GRANT)

/* Input mode */
#define SII9022A_INPUT_FORMAT	0x09
#define IN_FMT_12BIT		0xC0
#define IN_FMT_10BIT		0x80
#define IN_FMT_16BIT		0x40
#define IN_FMT_VREXP_OFF	0x08
#define IN_FMT_VREXP_ON		0x04
#define IN_FMT_BLACK		0x03
#define IN_FMT_YC422		0x02
#define IN_FMT_YC444		0x01
#define IN_FMT_RGB		0x00

/* Output resolution options */
#define SII9022A_PXLCLK_LSB	0x00
#define SII9022A_PXLCLK_MSB	0x01
#define SII9022A_VFREQ_LSB	0x02
#define SII9022A_VFREQ_MSB	0x03
#define SII9022A_PIXELS_LSB	0x04
#define SII9022A_PIXELS_MSB	0x05
#define SII9022A_LINES_LSB	0x06
#define SII9022A_LINES_MSB	0x07

/* AVI Info Frame */
#define SII9022A_AVIINFO	0x0C


