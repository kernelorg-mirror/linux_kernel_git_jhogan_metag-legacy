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
*** File Name  : umac.h
***
*** File Description:
*** This file contains the declarations of structures that will
*** be used by core, tx and rx code
***
******************************************************************************
*END**************************************************************************/

#ifndef _UCCP310WLAN_UMAC_H_
#define _UCCP310WLAN_UMAC_H_

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/wireless.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <../net/mac80211/ieee80211_i.h>
#include <net/mac80211.h>

#include "lmac_if.h"

#ifdef CONFIG_TIMING_DEBUG
extern unsigned long irq_timestamp[20];
extern unsigned int irq_ts_index;
extern spinlock_t timing_lock;
#endif

#define RESET_TIMEOUT  1000   /* Specify delay in milli-seconds*/
#define RESET_TIMEOUT_TICKS   msecs_to_jiffies(RESET_TIMEOUT)

#define TX_COMPLETE_TIMEOUT  1000   /* Specify delay in milli-seconds*/
#define TX_COMPLETE_TIMEOUT_TICKS   msecs_to_jiffies(TX_COMPLETE_TIMEOUT)

#define   MAX_MCAST_FILTERS             10
#define   MAX_VIFS			5
#define   DEFAULT_TX_POWER              15
#define   DEFAULT_MAC_ADDRESS           "001122334455"
#define   SUPPORTED_FILTERS             (FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC)
#define   MAX_BUFF_POOL_ELEMENTS        9 /* Must be alteast 6, one for each AC + two for BCN queue + spares*/
#define	  MAX_TX_QUEUE_LEN		20

struct wifi_params {
	int            ed_sensitivity;
	int            num_vifs;
	unsigned char  auto_sensitivity;
	unsigned char  rf_params[8];
	unsigned char  show_phy_stats;
	unsigned char  production_test;
	unsigned int   dot11a_support;
};

struct wifi_stats {
	unsigned int   rx_packet_count;
	 int            current_sensitivity;
	unsigned int   ofdm_rx_crc_success_cnt;
	unsigned int   ofdm_rx_crc_fail_cnt;
	unsigned int   ofdm_rx_false_trig_cnt;
	unsigned int   ofdm_rx_header_fail_cnt;
	unsigned int   dsss_rx_crc_success_cnt;
	unsigned int   dsss_rx_crc_fail_cnt;
	unsigned int   dsss_rx_false_trig_cnt;
	unsigned int   dsss_rx_header_fail_cnt;
	unsigned int   ed_cnt;
	unsigned int   cca_fail_cnt;
	unsigned int   pdout_val;
	unsigned char  uccp310_lmac_version[8];
};

struct tx_config {
	/*
	 * used to protect the TX pool
	 */
	spinlock_t      lock;
	/*
	*used to store tx tokens(buff pool ids)
	*/
	unsigned long   tx_buff_pool_bmp;

	unsigned int    next_spare_token_ac;

	/*
	 *  used to store the address of pending skbs per ac
	 */
	struct sk_buff_head  pending_pkt[WLAN_AC_MAX_CNT];

	/*
	 * used to store the address of tx'ed skb.. it will be used in tx
	 * complete
	 */
	struct sk_buff  *tx_pkt[MAX_BUFF_POOL_ELEMENTS];

	unsigned int queue_stopped_bmp;
};

enum DEVICE_STATE {
	STOPPED = 0,
	STARTED
};

struct mac80211_dev {
	struct device       *dev;
	struct mac_address  if_mac_addresses[MAX_VIFS];
	unsigned int        active_vifs;
	struct mutex        mutex;
	int                 state;
	int                 txpower;
	unsigned char       mc_filters[MAX_MCAST_FILTERS][6];
	int                 mc_filter_count;

	struct tx_config     tx;

	struct wifi_params  *params;
	struct wifi_stats   *stats;
	char                name[20];
	char                reset_complete;
	int                 power_save; /* Will be set only when a single VIF in STA mode is active */
	struct ieee80211_vif __rcu *vifs[MAX_VIFS];
	struct ieee80211_hw *hw;
	spinlock_t           bcast_lock; /* Used to ensure more_frames bit is set properly when transmitting bcast frames in AP in IBSS modes */
};


struct edca_params {
	unsigned short    txop; /* units of 32us */
	unsigned short    cwmin;/* units of 2^n-1 */
	unsigned short    cwmax;/* units of 2^n-1 */
	unsigned char     aifs;
};

struct umac_vif {
	struct timer_list           bcn_timer;
	struct uvif_config {
		unsigned int             atim_window;
		unsigned int             aid;
		unsigned int             bcn_lost_cnt;
		struct edca_params       edca_params[WLAN_AC_MAX_CNT];
	} config;

	unsigned int                noa_active;
	struct sk_buff_head         noa_que;
	unsigned int                noa_tx_allowed;

	int			vif_index;
	struct ieee80211_vif        *vif;
	struct mac80211_dev         *dev;
	unsigned char			bssid[ETH_ALEN];
};

extern int  uccp310wlan_core_init(struct mac80211_dev *dev);
extern void uccp310wlan_core_deinit(struct mac80211_dev *dev);
extern void uccp310wlan_vif_add(struct umac_vif  *uvif);
extern void uccp310wlan_vif_remove(struct umac_vif *uvif);
extern void uccp310wlan_vif_set_edca_params(unsigned short queue, struct umac_vif *uvif, const struct  ieee80211_tx_queue_params *params, unsigned int vif_active);
extern void uccp310wlan_vif_bss_info_changed(struct umac_vif *uvif, struct ieee80211_bss_conf *bss_conf, unsigned int changed);
extern int  uccp310wlan_tx_frame(struct sk_buff *skb, struct mac80211_dev *dev, bool bcast);
extern void uccp310wlan_tx_init(struct mac80211_dev *dev);
extern void uccp310wlan_tx_deinit(struct mac80211_dev *dev);

static inline int vif_addr_to_index(unsigned char *addr, struct mac80211_dev *dev)
{
	int i;
	for (i = 0; i < MAX_VIFS; i++)
		if (!compare_ether_addr(addr, dev->if_mac_addresses[i].addr))
			break;
	if ((i < MAX_VIFS) && (dev->active_vifs & (1 << i)))
		return i;
	else
		return -1;
}

#endif /* _UCCP310WLAN_UMAC_H_ */

/* EOF */
