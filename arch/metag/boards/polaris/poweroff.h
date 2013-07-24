#ifndef _POLARIS_POWEROFF_H_
#define _POLARIS_POWEROFF_H_

/**
 * polaris_raw_power_off() - Power down the board and SoC.
 *
 * This powers down +1v8 (DDR etc), 3v3_main (SoC pads etc), and 1v2 (SoC
 * peripherals).
 */
extern void polaris_raw_power_off(void);

/* Size in bytes of the polaris_raw_power_off() code. */
extern unsigned int polaris_raw_power_off_sz;

#endif /* _POLARIS_POWEROFF_H_ */
