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
*** File Name  : umac_lmac_if.h
***
*** File Description:
*** This file describes UMAC/LMAC logical interface
***
******************************************************************************
*END**************************************************************************/

#ifndef _UCCP310WLAN_UMAC_LMAC_IF_H_
#define _UCCP310WLAN_UMAC_LMAC_IF_H_

#include <linux/compiler.h>

#define MICHAEL_LEN                   8
#define MAX_KEY_LEN                   16

#define WEP40_KEYLEN                  5
#define WEP104_KEYLEN                 13
#define MAX_WEP_KEY_LEN               13

enum UMAC_QUEUE_NUM {
	WLAN_AC_BK = 0,
	WLAN_AC_BE,
	WLAN_AC_VI,
	WLAN_AC_VO,
	WLAN_AC_BCN,
	WLAN_AC_MAX_CNT
};

struct umac_lmac_msg_hdr {
	unsigned int  id;
	unsigned int  flags;
} __packed;

/* Commands */
enum UMAC_CMD {
	CMD_FIRST = 0x000,
	CMD_RESET,
	CMD_VIF_CTRL,
	CMD_VIF_CFG,
	CMD_GLOBAL_CFG,
	CMD_TX_POWER,
	CMD_MCST_FILTER_CFG,
	CMD_MCST_FILTER_CTRL,
	CMD_RCV_BCN_MODE,
	CMD_TXQ_PARAMS,
	CMD_PS_CFG,
	CMD_CHANNEL,
	CMD_PEER_KEY_CFG,
	CMD_IF_KEY_CFG,
	CMD_TX,
	CMD_MIB_STATS,
	CMD_PHY_STATS,
	CMD_TEST, /* Can be used by hardware abstraction layer(s) */
	CMD_SCAN_START,
	CMD_SCAN_STOP,
	CMD_LAST
};
/* UMAC commands */

struct umac_cmd_reset {
	struct umac_lmac_msg_hdr hdr;
	/*
	 * 0 - LMAC ENABLE
	 * 1 - LMAC DISABLE
	 */
#define LMAC_ENABLE     0
#define LMAC_DISABLE    1
	unsigned int reset_type;
} __packed;

struct umac_cmd_scan_ind {
	struct umac_lmac_msg_hdr hdr;
} __packed;

struct umac_cmd_vif_ctrl {
	struct umac_lmac_msg_hdr hdr;
	/*
	 * if_ctrl -
	 * 1 - add interface address
	 * 2 - remove interface address
	 */
#define IF_ADD   1
#define IF_REM   2
	unsigned int        if_ctrl;

	/*
	 * Interface mode -
	 * 0 - STA in infrastucture mode
	 * 1 - STA in AD-HOC mode
	 * 2 - AP
	 */
#define IF_MODE_STA_BSS  0
#define IF_MODE_STA_IBSS 1
#define IF_MODE_AP       2
#define IF_MODE_INVALID  3
	unsigned int        if_mode;
	/*
	 * if_index -
	 * index assigned to this interface
	 */
	unsigned int        if_index;
	/*
	 * mac_addr -
	 * interface address to add or delete
	 */
	unsigned char       mac_addr[ETH_ALEN];
} __packed;

struct umac_cmd_vif_cfg {
	struct umac_lmac_msg_hdr hdr;
   /* Bitmap indicating whether value is changed or not */
  #define BASICRATES_CHANGED (1<<0)
  #define SHORTSLOT_CHANGED (1<<1)
  #define POWERSAVE_CHANGED (1<<2)
  #define UAPSDTYPE_CHANGED (1<<3)
  #define ATIMWINDOW_CHANGED (1<<4)
  #define AID_CHANGED (1<<5)
  #define CAPABILITY_CHANGED (1<<6)
  #define SHORTRETRY_CHANGED (1<<7)
  #define LONGRETRY_CHANGED (1<<8)
  #define BSSID_CHANGED (1<<9)

	unsigned int         changed_bitmap;
	/*
	 * bitmap of supported basic rates
	 */
	unsigned int        basic_rate_set;

	/*
	 * slot type -
	 * 0 - long slot
	 * 1 - short slot
	 */
	unsigned int        use_short_slot;

	/*
	 * power save mode -
	 *
	 * bit0 - BK
	 * bit1 - BE
	 * bit2 - VI
	 * bit3 - VO
	 * all other bits are reserved
	 *
	 * 0 - indicates legacy mode powersave, 1 - indicates UAPSD for the
	 * corresponding AC.
	 *
	 * all bits are 0 - legacy mode
	 * some bits are 1's and some are 0's - mixed mode
	 *  all bits are 1 - APSD
	 */
	unsigned int        powersave_mode;

	/*
	 * UAPSD type
	 * if value is 0, AC is delivery enabled access category. otherwise it
	 * is trigger enabled
	 *
	 * bit0 - BK
	 * bit1 - BE
	 * bit2 - VI
	 * bit3 - VO
	 * all other bits are reserved
	 */
	unsigned int        uapsd_type;

	/*
	 * ATIM window
	 */
	unsigned int         atim_window;

	unsigned int         aid;

	unsigned int         capability;

	unsigned int         short_retry;

	unsigned int         long_retry ;

	/* index of the intended interface */
	unsigned int         if_index;
	unsigned char        vif_addr[ETH_ALEN];

	/* bssid of interface */
	unsigned char        bssid[ETH_ALEN];

} __packed;

struct umac_cmd_global_cfg {
	struct umac_lmac_msg_hdr hdr;
	/*
	 * Both the values in mSecs
	 */
	unsigned int         rx_msdu_lifetime;
	unsigned int         tx_msdu_lifetime;

	int                  ed_sensitivity;
	int		     dynamic_ed_ceiling;

	/*
	 * dynamic_ed_enable -
	 * 0 - disable dynamic ed
	 * 1 - enable dynamic ed
	 */
#define DYN_ED_DISABLE        0
#define DYN_ED_ENABLE         1
	unsigned int         dynamic_ed_enable;

	unsigned char        rf_params[8];
} __packed;

struct umac_cmd_txpower {
	struct umac_lmac_msg_hdr  hdr;
	unsigned int        txpower; /* In dbm */
} __packed;

struct umac_cmd_mcst_filter_cfg {
	struct umac_lmac_msg_hdr hdr;
	/*
	 * mcst_ctrl -
	 * 0 -- ADD multicast address
	 * 1 -- Remove multicast address
	 */
#define MCAST_ADDR_ADD        0
#define MCAST_ADDR_REM        1
	unsigned int        mcst_ctrl;

	/*
	 * addr to add or delete..
	 */
	unsigned char       addr[ETH_ALEN];

} __packed;

struct umac_cmd_mcst_filter_ctrl {
	struct umac_lmac_msg_hdr hdr;

	/*
	 * ctrl -
	 * 0 - disable multicast filtering in LMAC
	 * 1 - enable multicast filtering in LMAC
	 */
#define MCAST_FILTER_DISABLE  0
#define MCAST_FILTER_ENABLE   1
	unsigned int        ctrl;
} __packed;

struct umac_cmd_rcv_bcn_mode {
	struct umac_lmac_msg_hdr hdr;
    /*
     * 0 - all beacons
     * 1 - network only
     * 2 - no beacons
     */
#define RCV_ALL_BCNS          0
#define RCV_NETWORK_BCNS      1
#define RCV_NO_BCNS           2
	unsigned int        mode;
} __packed;

struct umac_cmd_txq_params {
	struct umac_lmac_msg_hdr  hdr;
	/*
	 * @UMAC_QUEUE_NUM_T will be used to fill this variable
	 */
	unsigned int         queue_num;

	/*
	 * AIFSN value
	 * user has to compute the AIFS from this value using
	 * AIFS = aifs*slot_time+SIFS
	 */
	unsigned int         aifsn;
	/*
	 * in units of uSec
	 */
	unsigned int         txop;
	/*
	 * CWmin value in number of slots
	 */
	unsigned int         cwmin;
	/*
	 * CWmax value in number of slots
	 */
	unsigned int         cwmax;

	/*
	 * interface index..
	 */
	unsigned int         if_index;

	/*
	 * interface address
	 */
	unsigned char        vif_addr[ETH_ALEN];

} __packed;

struct umac_cmd_ps_cfg {
	struct umac_lmac_msg_hdr hdr;

	/*
	 * state -
	 * 0 - power save off
	 * 1 - power save on
	 */
#define PWRSAVE_STATE_AWAKE   0
#define PWRSAVE_STATE_DOZE    1
	unsigned int        powersave_state;

	/*
	 * interface index..
	 */
	unsigned int        if_index;
	/*
	 * interface address
	 */
	unsigned char        vif_addr[ETH_ALEN];

} __packed;

struct umac_cmd_channel {
	struct umac_lmac_msg_hdr hdr;
	unsigned int        channel;
} __packed;

struct umac_cmd_peer_key_cfg {
	struct umac_lmac_msg_hdr hdr;

	/*
	 * 0 - add key
	 * 1 - del key
	 */
#define KEY_CTRL_ADD    0
#define KEY_CTRL_DEL    1
	unsigned int        op;

	/*
	 * key_type -
	 * 0 - unicast
	 * 1 - broadcast
	 */
#define KEY_TYPE_UCAST    0
#define KEY_TYPE_BCAST    1
	unsigned int        key_type;
	/*
	 * cipher_type -
	 * 0 - wep40
	 * 1 - wep104
	 * 2 - tkip
	 * 3 - ccmp
	 */
#define CIPHER_TYPE_WEP40    0
#define CIPHER_TYPE_WEP104   1
#define CIPHER_TYPE_TKIP     2
#define CIPHER_TYPE_CCMP     3
	unsigned int        cipher_type;
	unsigned int        key_id;

	/*
	 * if_index -
	 * interface index..
	 */
	unsigned int        if_index;

	unsigned char       vif_addr[ETH_ALEN];

	unsigned char       peer_mac[ETH_ALEN];

	unsigned char       key[MAX_KEY_LEN];

	unsigned char       tx_mic[MICHAEL_LEN];

	unsigned char       rx_mic[MICHAEL_LEN];

} __packed;

struct umac_cmd_if_key_cfg {
	struct umac_lmac_msg_hdr hdr;

	/*
	 * 0 - add key
	 * 1 - del key
	 */
#define KEY_CTRL_ADD    0
#define KEY_CTRL_DEL    1
	unsigned int        op;

	unsigned int        cipher_type;

	/*
	 * 0..3
	 */
	unsigned int        key_id;

	/*
	 * if_index -
	 * interface index..
	 */
	unsigned int        if_index;

	unsigned char       vif_addr[ETH_ALEN];

	union {
		struct {
			unsigned int     key_len;
			unsigned char    wep_key[MAX_WEP_KEY_LEN];
		} wep_key;
		struct {
			unsigned char    key[MAX_KEY_LEN];
			unsigned char    mic_key[MICHAEL_LEN];
		} rsn_grp_key;
	} key;

} __packed;

struct umac_cmd_tx {
	struct umac_lmac_msg_hdr hdr;
	/*
	 * @UMAC_QUEUE_NUM_T will be used to fill this variable
	 */
	unsigned int     queue_num;

	/*
	 * id of this TX buffer in global TX buffer pool
	 */
	unsigned int     buff_pool_id;

	unsigned int     num_of_frags;

	/*
	 * Length of the intermediate fragment if it is a fragmented frame or
	 * length of the frame in case of single MSDU
	 */
	unsigned int     frag_len;

	/*
	 * Length of the last fragment..
	 */
	unsigned int     last_frag_len;

	/*
	 * tx power..
	 */
	unsigned int     tx_power;

	/*
	 * if_index -
	 * interface index..
	 */
	unsigned int     if_index;

	unsigned char    vif_addr[ETH_ALEN];
	/*
	 * more_frms -
	 * it indicates that one or more high priority frames are buffered at
	 * UMAC
	 */
	unsigned int     more_frms;

	unsigned int     num_rates;
	unsigned int     rate[4];  /* Units of 500 Kbps */
	/*
	 * Control frame to transmit..
	 * 0 - none
	 * 1 - RTS-CTS protection
	 * 2 - CTS-to-Self protection
	 */
#define USE_PROTECTION_NONE        0
#define USE_PROTECTION_RTS         1
#define USE_PROTECTION_CTS2SELF    2
	unsigned int     rate_protection_type[4];
	/*
	 * use_short_preamble -
	 * 0 - don't use short preamble
	 * 1 - use short preamble for this frame transmission
	 */
#define DONT_USE_SHORT_PREAMBLE    0
#define USE_SHORT_PREAMBLE         1
	unsigned int     rate_preamble_type[4];
	unsigned int     rate_retries[4];

	/*
	 * This is used only by FMAC when it wants to encrypt the AUTH
	 * frame in the case of shared key authentication
	 * SoftMAC should set this field to 0
	 */
	unsigned int     force_encrypt;

} __packed;

struct umac_cmd_mib_stats {
	struct umac_lmac_msg_hdr hdr;
} __packed;

struct umac_cmd_phy_stats {
	struct umac_lmac_msg_hdr hdr;
} __packed;

/* Events */

enum UMAC_EVENT {
	EVENT_FIRST = 0x000,
	EVENT_NOA,
	EVENT_IBSS_MERGE,
	EVENT_TX_DONE,
	EVENT_RX,
	EVENT_PF_DROPPED,
	EVENT_RESET_COMPLETE,
	EVENT_MIB_STAT,
	EVENT_PHY_STAT,
	EVENT_LMAC_ERROR,
	EVENT_LAST
} ;


/* Event structures */

struct umac_event_lmac_error {
	struct umac_lmac_msg_hdr hdr;
	/*
	 * LMAC will send the unexpected errors in this event..
	 */
	unsigned int        error;
} __packed;

struct umac_event_noa {
	struct umac_lmac_msg_hdr hdr;
	unsigned int vif_index;
	unsigned char vif_addr[ETH_ALEN];

	/*
	 * 1 indicates NoA feature is active
	 * 0 indicates NoA feature is not active
	 */
	unsigned int noa_active;
#define ABSENCE_START 0 /* Indicates AP is absent */
#define ABSENCE_STOP  1 /* Indicates AP is present */
	unsigned int ap_present;
} __packed;

struct umac_event_reset_complete {
	struct umac_lmac_msg_hdr hdr;
	char                version[6];
} __packed;

struct umac_event_rx {
	struct umac_lmac_msg_hdr hdr;
	unsigned int        buff_len;
	unsigned int        rate;
	unsigned int        rssi;
	unsigned char       timestamp[8];
#define RX_MIC_SUCCESS 0 /* No MIC error in frame */
#define RX_MIC_FAILURE 1 /* MIC error in frame */
	unsigned int	    status;
	unsigned char	    ts1[8];
	unsigned char	    ts2[4];


} __packed;


struct umac_event_tx_done {
	struct umac_lmac_msg_hdr hdr;

	/*
	 * ID of the buffer transmitted...
	 */
	unsigned int        buff_pool_id;
	unsigned int        pdout_voltage;

	/*
	 * frame_status -
	 * 0 - success
	 * 1 - discarded due to retry limit exceeded
	 * 2 - discarded due to msdu lifetime expiry
	 * 3 - discarded due to encryption key not available
	 */
#define UMAC_EVENT_TX_DONE_SUCCESS            (0)
#define UMAC_EVENT_TX_DONE_ERROR_RETRY_LIMIT  (1)
#define UMAC_EVENT_TX_DONE_MSDU_LIFETIME      (2)
#define UMAC_EVENT_TX_DONE_KEY_NOT_FOUND      (3)
	unsigned int        frm_status;

	unsigned int        retries_num;
	unsigned int        rate;
	unsigned int        queue;
} __packed;

struct umac_event_mib_stats {
	struct umac_lmac_msg_hdr hdr;
	unsigned int    wlan_phy_stats;
	unsigned int    ofdm_rx_crc_success_cnt;
	unsigned int    ofdm_rx_crc_fail_cnt;
	unsigned int    ofdm_rx_false_trig_cnt;
	unsigned int    ofdm_rx_header_fail_cnt;
	unsigned int    dsss_rx_crc_success_cnt;
	unsigned int    dsss_rx_crc_fail_cnt;
	unsigned int    dsss_rx_false_trig_cnt;
	unsigned int    dsss_rx_header_fail_cnt;
	unsigned int    ed_cnt;
	unsigned int    cca_fail_cnt;
	unsigned int    tx_total_pkt_cnt;
	unsigned int    tx_complete_pkt_cnt;
	unsigned int    tx_err_cnt;
	unsigned int    tx_complete_err_pkt_cnt;
	unsigned int    tx_ack_pkt_cnt;
	unsigned int    frag_success_cnt;
	int             sensitivity;
} __packed;

struct umac_event_phy_stats {
	struct umac_lmac_msg_hdr hdr;
	unsigned int   phy_stats[32];
};

#endif /* _UCCP310WLAN_UMAC_LMAC_IF_H_ */

/* EOF */
