/*
 * asm/soc-tz1090/afe.h
 * A simple interface to the Comet AUX DAC.
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 */

#ifndef _ASM_METAG_SOC_TZ1090_AFE_H
#define _ASM_METAG_SOC_TZ1090_AFE_H

int comet_afe_auxdac_get(void);
void comet_afe_auxdac_put(void);

void comet_afe_auxdac_set_power(unsigned int power);
unsigned int comet_afe_auxdac_get_power(void);
void comet_afe_auxdac_set_standby(unsigned int standby);
unsigned int comet_afe_auxdac_get_standby(void);
int comet_afe_auxdac_set_source(unsigned int source);
void comet_afe_auxdac_set_value(unsigned int value);
int comet_afe_auxdac_get_value(void);

#endif /* _ASM_METAG_SOC_TZ1090_AFE_H */
