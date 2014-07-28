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
*** File Name  : hal_hostport.h
***
*** File Description:
*** This file contains the definitions specific to HOSPORT comms
***
******************************************************************************
*END**************************************************************************/

#ifndef _UCCP310WLAN_HAL_HOSTPORT_H_
#define _UCCP310WLAN_HAL_HOSTPORT_H_

/* Include files */
#include <asm/soc-tz1090/defs.h>

#if defined(__cplusplus)
extern "C"
{
#endif /* __cplusplus */

struct hal_priv {
/* UCCP and BULKRAM mappings */
unsigned long         uccp_mem_addr;
unsigned long         bulk_ram_mem_addr;
unsigned long         shm_offset;
/*
 *  TX
 */
struct sk_buff_head             txq;
struct tasklet_struct           tx_tasklet;
unsigned short                  cmd_cnt;
/*
 * RX
 */
struct sk_buff_head             rxq;
struct tasklet_struct           rx_tasklet;
unsigned short                  event_cnt;
msg_handler                     rcv_handler;
};

int _uccp310wlan_80211if_init(void);
void _uccp310wlan_80211if_exit(void);


/* Comet values */
#define HAL_META_UCC_BASE              0x02010400
#define HAL_META_BULK_RAM              0xE0200000
#define HAL_META_BULK_RAM_LEN          0x00060000
#define HAL_META_UCC_LEN               0x0000007c
#define HAL_MTX_BULK_RAM	       0xB0000000
/* Register UCCP_CORE_HOST_TO_MTX_CMD */
#define UCCP_CORE_HOST_TO_MTX_CMD        0x0030
#define UCCP_CORE_HOST_TO_MTX_CMD_ADDR      ((hpriv->uccp_mem_addr) + UCCP_CORE_HOST_TO_MTX_CMD)
#define UCCP_CORE_HOST_INT_SHIFT            31

/* Register UCCP_CORE_MTX_TO_HOST_CMD */
#define UCCP_CORE_MTX_TO_HOST_CMD 0x0034
#define UCCP_CORE_MTX_TO_HOST_CMD_ADDR      ((hpriv->uccp_mem_addr) + UCCP_CORE_MTX_TO_HOST_CMD)

/* Register UCCP_CORE_HOST_TO_MTX_ACK */
#define UCCP_CORE_HOST_TO_MTX_ACK 0x0038
#define UCCP_CORE_HOST_TO_MTX_ACK_ADDR      ((hpriv->uccp_mem_addr) + UCCP_CORE_HOST_TO_MTX_ACK)
#define UCCP_CORE_MTX_INT_CLR_SHIFT         31

/* Register UCCP_CORE_MTX_TO_HOST_ACK */
#define UCCP_CORE_MTX_TO_HOST_ACK 0x003C
#define UCCP_CORE_MTX_TO_HOST_ACK_ADDR      ((hpriv->uccp_mem_addr) + UCCP_CORE_MTX_TO_HOST_ACK)

/* Register UCCP_CORE_MTX_INT_ENABLE */
#define UCCP_CORE_MTX_INT_ENABLE         0x0044
#define UCCP_CORE_MTX_INT_ENABLE_ADDR       ((hpriv->uccp_mem_addr) + UCCP_CORE_MTX_INT_ENABLE)
#define UCCP_CORE_MTX_INT_EN_SHIFT          31

#define UCCP_CORE_INT_ENAB               0x0000
#define UCCP_CORE_INT_ENAB_ADDR             ((hpriv->uccp_mem_addr) + UCCP_CORE_INT_ENAB)
#define UCCP_CORE_MTX_INT_IRQ_ENAB_SHIFT    15

/******************************************************************************************************/
#define HAL_SHARED_MEM_OFFSET           0x30000
#define HAL_WLAN_BULK_RAM_START		(HAL_META_BULK_RAM + (hpriv->shm_offset))
#define HAL_WLAN_BULK_RAM_LEN		0x00016000

/* Command, Event and Buff mappping offsets */
#define HAL_COMMAND_OFFSET                    (0)
#define HAL_EVENT_OFFSET                      (HAL_COMMAND_OFFSET + 512)

#define HAL_BULK_RAM_CMD_START                 ((hpriv->bulk_ram_mem_addr) + HAL_COMMAND_OFFSET)
#define HAL_BULK_RAM_EVENT_START               ((hpriv->bulk_ram_mem_addr) + HAL_EVENT_OFFSET)


#define HAL_IRQ_LINE                      external_irq_map(UCC0_IRQ_NUM)

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* _UCCP310WLAN_HAL_HOSTPORT_H_ */

/* EOF */
