/*
 * dwc_otg_platform.h
 *
 * (c) 2010 Imagination Technologies
 */

#ifndef DWC_OTG_PLATFORM_H_
#define DWC_OTG_PLATFORM_H_

/**
 * This structure is used to pass board specific information via platform data
 */
struct dwc_otg_board {
	/* methods for enabling / disabling Vbus at the SoC Level*/
	void (*enable_vbus)(void);
	void (*disable_vbus)(void);
	/**
	 * Control whether host communication is permitted, to allow the device
	 * to complete it's power-up and initialisation without the host getting
	 * confused.
	 * @normal 1: normal.
	 *         0: prevent host communication.
	 */
	void (*vbus_valid)(int normal);
};

#endif /* DWC_OTG_PLATFORM_H_ */
