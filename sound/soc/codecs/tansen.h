/*
 * tansen.h - CosmicCircuits internal DAC
 *
 * Copyright (C) 2010 Imagination Technologies Ltd.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef TANSEN_H_
#define TANSEN_H_

#define TANSEN_TM_PWM1_L1	0x820
#define TANSEN_TM_PWM1_L2	0x821
#define TANSEN_TM_PWM1_L3	0x822
#define TANSEN_TM_PWM1_R1	0x823
#define TANSEN_TM_PWM1_R2	0x824
#define TANSEN_TM_PWM1_R4	0x825
#ifdef CONFIG_SOC_COMET_ES1
#define TANSEN_TM_PWM1_S1	0x860 /*Wrong in data sheet not 0x826*/
#else
#define TANSEN_TM_PWM1_S1	0x826 /*now fixed*/
#endif
#define TANSEN_TM_PWM1_S2	0x827
#define TANSEN_TM_PWM2_L1	0x8a0
#define TANSEN_TM_PWM2_L2	0x8a1
#define TANSEN_TM_PWM2_L3	0x8a2
#define TANSEN_TM_PWM2_L4	0x8a3
#define TANSEN_TM_PWM2_L5	0x8a4
#define TANSEN_TM_PWM2_R1	0x8a5
#define TANSEN_TM_PWM2_R2	0x8a6
#define TANSEN_TM_PWM2_R3	0x8a7
#define TANSEN_TM_PWM2_R4	0x8a8
#define TANSEN_TM_PWM2_R5	0x8a9
#ifdef CONFIG_SOC_COMET_ES1
#define TANSEN_TM_PWM2_S1	0x8e0 /*Wrong in datasheet not 0x8aa */
#else
#define TANSEN_TM_PWM2_S1	0x8aa
#endif
#define TANSEN_TM_PWM2_S2	0x8ab
#define TANSEN_TM_PWM3_L1	0x920
#define TANSEN_TM_PWM3_L2	0x921
#define TANSEN_TM_PWM3_L3	0x922
#define TANSEN_TM_PWM3_R1	0x923
#define TANSEN_TM_PWM3_R2	0x924
#define TANSEN_TM_PWM3_R4	0x925
#ifdef CONFIG_SOC_COMET_ES1
#define TANSEN_TM_PWM3_S1	0x960 /*Wrong in datasheet not 0x926 */
#else
#define TANSEN_TM_PWM3_S1	0x926
#endif
#define TANSEN_TM_PWM3_S2	0x927
#define TANSEN_TM_BG_OP1	0x980
#define TANSEN_TM_BG_OP2	0x981
#define TANSEN_TM_BG_OP3	0x982
#define TANSEN_TM_BG_OP4	0x983
#define TANSEN_TM_BG_OP5	0x984
#define TANSEN_TM_BG_OP6	0x985
#define TANSEN_TM_BG_OP7	0x986
#define TANSEN_TM_HP_L_1	0xA20
#define TANSEN_TM_HP_L_2	0xA21
#define TANSEN_TM_HP_L_3	0xA22
#define TANSEN_TM_HP_L_4	0xA23
#define TANSEN_TM_HP_L_5	0xA24
#define TANSEN_TM_HP_L_6	0xA25
#define TANSEN_TM_HP_L_7	0xA26
#define TANSEN_TM_HP_L_8	0xA27
#define TANSEN_TM_HP_L_9	0xA28
#define TANSEN_TM_HP_L_10	0xA29
#define TANSEN_TM_HP_L_11	0xA2A
#define TANSEN_TM_HP_L_12	0xA2B
#define TANSEN_TM_HP_L_13	0xA2C
#define TANSEN_TM_HP_L_14	0xA2D
#define TANSEN_TM_HP_L_15	0xA2E
#define TANSEN_TM_HP_L_16	0xA2F
#define TANSEN_TM_HP_L_17	0xA30
#define TANSEN_TM_HP_L_18	0xA31
#define TANSEN_TM_HP_L_19	0xA32
#define TANSEN_TM_HP_L_20	0xA33
#define TANSEN_TM_HP_L_21	0xA34
#define TANSEN_TM_HP_L_22	0xA35
#define TANSEN_TM_HP_L_23	0xA36
#define TANSEN_TM_HP_L_24	0xA37
#define TANSEN_TM_HP_L_25	0xA38
#define TANSEN_TM_HP_L_26	0xA39
#define TANSEN_TM_HP_R_1	0xA40
#define TANSEN_TM_HP_R_2	0xA41
#define TANSEN_TM_HP_R_3	0xA42
#define TANSEN_TM_HP_R_4	0xA43
#define TANSEN_TM_HP_R_5	0xA44
#define TANSEN_TM_HP_R_6	0xA45
#define TANSEN_TM_HP_R_7	0xA46
#define TANSEN_TM_HP_R_8	0xA47
#define TANSEN_TM_HP_R_9	0xA48
#define TANSEN_TM_HP_R_10	0xA49
#define TANSEN_TM_HP_R_11	0xA4A
#define TANSEN_TM_HP_R_12	0xA4B
#define TANSEN_TM_HP_R_13	0xA4C
#define TANSEN_TM_HP_R_14	0xA4D
#define TANSEN_TM_HP_R_15	0xA4E
#define TANSEN_TM_HP_R_16	0xA4f
#define TANSEN_TM_HP_R_17	0xA50
#define TANSEN_TM_HP_R_18	0xA51
#define TANSEN_TM_HP_R_19	0xA52
#define TANSEN_TM_HP_R_20	0xA53
#define TANSEN_TM_HP_R_21	0xA54
#define TANSEN_TM_HP_R_22	0xA55
#define TANSEN_TM_HP_R_23	0xA56
#define TANSEN_TM_HP_R_24	0xA57
#define TANSEN_TM_HP_R_25	0xA58
#define TANSEN_TM_HP_R_26	0xA59
#define TANSEN_TM_PLL_1		0xA80
#define TANSEN_TM_PLL_2		0xA81
#define TANSEN_TM_PLL_3		0xA82
#define TANSEN_TM_PLL_4		0xA83
#define TANSEN_TM_PLL_5		0xA84
#define TANSEN_TM_PLL_6		0xA85
#define TANSEN_TM_PLL_7		0xA86
#define TANSEN_TM_PLL_8		0xA87
#define TANSEN_TM_PLL_9		0xA88
#define TANSEN_TM_PLL_10	0xA89
#define TANSEN_TM_OP_TOP1	0xB00
#define TANSEN_TM_OP_TOP2	0xB01
#define TANSEN_TM_OP_TOP3	0xB02
#define TANSEN_TM_OP_TOP4	0xB03
#define TANSEN_TM_OP_TOP5	0xB04
#define TANSEN_TM_OP_TOP6	0xB05
#define TANSEN_TM_OP_TOP7	0xB06
#define TANSEN_TM_OP_TOP8	0xB07
#define TANSEN_TM_OP_TOP9	0xB08
#define TANSEN_TM_OP_TOP10	0xB09
#define TANSEN_TM_OP_TOP11	0xBOA
#define TANSEN_TM_OP_TOP12	0xB0B
#define TANSEN_TM_OP_TOP13	0xB0C
#define TANSEN_TM_L_PGA1	0xC20
#define TANSEN_TM_L_PGA2	0xC21
#define TANSEN_TM_L_PGA3	0xC22
#define TANSEN_TM_L_PGA4	0xC23
#define TANSEN_TM_L_PGA5	0xC24
#define TANSEN_TM_R_PGA1	0xC40
#define TANSEN_TM_R_PGA2	0xC41
#define TANSEN_TM_R_PGA3	0xC42
#define TANSEN_TM_R_PGA4	0xC43
#define TANSEN_TM_R_PGA5	0xC44
#define TANSEN_TM_L_ADC1	0xCA0
#define TANSEN_TM_L_ADC2	0xCA1
#define TANSEN_TM_L_ADC3	0xCA2
#define TANSEN_TM_L_ADC4	0xCA3
#define TANSEN_TM_L_ADC5	0xCA4
#define TANSEN_TM_L_ADC6	0xCA5
#define TANSEN_TM_L_ADC7	0xCA6
#define TANSEN_TM_L_ADC8	0xCA7
#define TANSEN_TM_L_ADC9	0xCA8
#define TANSEN_TM_L_ADC10	0xCA9
#define TANSEN_TM_R_ADC1	0xCC0
#define TANSEN_TM_R_ADC2	0xCC1
#define TANSEN_TM_R_ADC3	0xCC2
#define TANSEN_TM_R_ADC4	0xCC3
#define TANSEN_TM_R_ADC5	0xCC4
#define TANSEN_TM_R_ADC6	0xCC5
#define TANSEN_TM_R_ADC7	0xCC6
#define TANSEN_TM_R_ADC8	0xCC7
#define TANSEN_TM_R_ADC9	0xCC8
#define TANSEN_TM_R_ADC10	0xCCA /*typo in datasheet ? should be 0xCC9 ? */
#define TANSEN_TM_DECFILT_L1	0xD20
#define TANSEN_TM_DECFILT_L2	0xD21
#define TANSEN_TM_DECFILT_R1	0xD40
#define TANSEN_TM_DECFILT_R2	0xD41
#define TANSEN_TM_BG_IP_1	0xD80
#define TANSEN_TM_BG_IP_2	0xD81
#define TANSEN_TM_BG_IP_3	0xD82
#define TANSEN_TM_BG_IP_4	0xD83
#define TANSEN_TM_BG_IP_5	0xD84
#define TANSEN_TM_BG_IP_6	0xD85
#define TANSEN_TM_BG_IP_7	0xD86
#define TANSEN_TM_MICBIAS_1	0xE00
#define TANSEN_TM_IP_TOP1	0xE80
#define TANSEN_TM_IP_TOP2	0xE81
#define TANSEN_TM_IP_TOP3	0xE82
#define TANSEN_TM_IP_TOP4	0xE83
#define TANSEN_TM_IP_TOP5	0xE84
#define TANSEN_TM_IP_TOP6	0xE85
#define TANSEN_TM_IP_TOP7	0xE86

#endif /* TANSEN_H_ */
