/*
 * board/polaris/setup.h
 * Polaris related things
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 */

#ifndef _POLARIS_SETUP_H_
#define _POLARIS_SETUP_H_

#define POLARIS_BL_BOOST_VBUS	0
#define POLARIS_BL_BOOST_5V_BL	1
int polaris_bl_boost_set(unsigned int user, int en);

#endif /* _POLARIS_SETUP_H_ */
