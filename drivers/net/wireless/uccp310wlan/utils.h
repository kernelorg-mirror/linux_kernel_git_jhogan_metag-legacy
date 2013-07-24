/*HEADER**********************************************************************
******************************************************************************
***
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
*** File Name  : utils.h
***
*** File Description:
*** This file contains helper macros and data structures used across the code
***
******************************************************************************
*END**************************************************************************/
#ifndef _UCCP310WLAN_UTILS_H
#define _UCCP310WLAN_UTILS_H

#define RFPARAM2STR(a)   (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], a[6], a[7]
#define RFPARAMSTR       "0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X"

#define MASK_BITS(msb, lsb)               (((1U << ((msb) - (lsb) + 1)) - 1) << (lsb))
#define EXTRACT_BITS(arg, msb, lsb)       ((arg & MASK_BITS(msb, lsb)) >> (lsb))
#define INSERT_BITS(arg, msb, lsb, value) ((arg) = ((arg) & ~MASK_BITS(msb, lsb)) | (((value) << (lsb)) & MASK_BITS(msb, lsb)))

#define FRAME_CTRL_TYPE(arg)               EXTRACT_BITS(arg, 3, 2)
#define FRAME_CTRL_STYPE(arg)              EXTRACT_BITS(arg, 7, 4)
#define FTYPE_DATA                         0x02
#define FSTYPE_QOS_DATA                    0x08

#endif /* _UCCP310WLAN_UTILS_H */

/* EOF */
