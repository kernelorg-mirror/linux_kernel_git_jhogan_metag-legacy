/*HEADER**********************************************************************
******************************************************************************
***
*** Copyright (c) 2011, 2012, Imagination Technologies Ltd.
***
*** This program is free software; you can redistribute it and/or
*** modify it under the terms of the GNU General Public License
*** as published by the Free Software Foundation; either version 2
*** of the License, or (at your option) any later version.
***
*** This program is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*** GNU General Public License for more details.
***
*** You should have received a copy of the GNU General Public License
*** along with this program; if not, write to the Free Software
*** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
*** USA.
***
*** File Name  : hal.h
***
*** File Description:
*** This file contains Intermodule communication APIs
***
******************************************************************************
*END**************************************************************************/


#ifndef _UCCP310WLAN_HAL_H_
#define _UCCP310WLAN_HAL_H_

#define HOST_MOD_ID 0
#define UMAC_MOD_ID 1
#define LMAC_MOD_ID 2
#define MODULE_MAX 3

#define MAX_MESSAGE_SIZE 2600

typedef int (*msg_handler)(void *, unsigned char);

struct hal_ops_tag {
	int (*init)(void);
	int (*deinit)(void);
	void (*register_callback)(msg_handler, unsigned char);
	void (*send)(void*, unsigned char, unsigned char);
	void *(*get_buff)(unsigned int);
	void (*put_buff)(void *);
	unsigned char * (*get_data)(void *);
	unsigned char * (*get_head)(void *);
	unsigned int (*get_length)(void *);
	unsigned int (*get_true_length)(void *);
	unsigned char * (*put_data)(void *, unsigned int);
	unsigned char * (*pull_data)(void *, unsigned int);
	unsigned char * (*push_data)(void *, unsigned int);
	void (*link_buff)(void *dst, void *src);
};

extern struct hal_ops_tag hal_ops;
#endif /* _UCCP310WLAN_HAL_H_ */

/* EOF */
