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
*** File Name  : core.c
***
*** File Description:
*** This file contains the source functions for UMAC core
***
******************************************************************************
*END**************************************************************************/
#include "umac.h"

#ifdef CONFIG_CORE_DEBUG
#define UMAC_DEBUG(fmt, args...) printk(KERN_DEBUG fmt, ##args)
#else
#define UMAC_DEBUG(...) do { } while (0)
#endif

#define UMAC_PRINT(fmt, args...) printk(KERN_DEBUG fmt, ##args)

static int wait_for_reset_complete(struct mac80211_dev *dev)
{
	int count;
	count = 0;

check_reset_complete:
	if (!dev->reset_complete && (count < RESET_TIMEOUT_TICKS)) {
		count++;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		goto check_reset_complete;
	}

	if (!dev->reset_complete) {
		UMAC_PRINT("%s-UMAC: Warning: Didn't get reset complete after %ld timer ticks\n", dev->name, RESET_TIMEOUT_TICKS);
		return -1;
	}

	UMAC_PRINT("%s-UMAC: Reset complete after %d timer ticks\n", dev->name, count);
	return 0;

}

static void vif_bcn_timer_expiry(unsigned long data)
{
	struct umac_vif *uvif = (struct umac_vif *)data;
	struct ieee80211_sub_if_data *sdata;
	struct sk_buff *skb, *temp;
	struct sk_buff_head bcast_frames;
	unsigned long flags;

	sdata = vif_to_sdata(uvif->vif);

	if (!ieee80211_sdata_running(sdata))
		return;
	if (uvif->vif->bss_conf.enable_beacon == false)
		return;

	if (uvif->vif->type == NL80211_IFTYPE_AP) {
		skb_queue_head_init(&bcast_frames);

		temp = skb = ieee80211_beacon_get(uvif->dev->hw, uvif->vif);
		skb->priority = 1;
		skb_queue_tail(&bcast_frames, skb);

		skb = ieee80211_get_buffered_bc(uvif->dev->hw, uvif->vif);
		while (skb) {
			skb->priority = 1; /* Hack: skb->priority is used to indicate more frames */
			skb_queue_tail(&bcast_frames, skb);
			temp = skb;
			skb = ieee80211_get_buffered_bc(uvif->dev->hw, uvif->vif);
		}

		temp->priority = 0;

		spin_lock_irqsave(&uvif->dev->bcast_lock, flags);
		while ((skb = skb_dequeue(&bcast_frames)))
			uccp310wlan_tx_frame(skb, uvif->dev, true);
		spin_unlock_irqrestore(&uvif->dev->bcast_lock, flags);
	} else {
		skb = ieee80211_beacon_get(uvif->dev->hw, uvif->vif);
		uccp310wlan_tx_frame(skb, uvif->dev, true);

		/* TODO: IBSS PS handling */
	}

	mod_timer(&uvif->bcn_timer, jiffies + msecs_to_jiffies(uvif->vif->bss_conf.beacon_int));

}
int uccp310wlan_core_init(struct mac80211_dev *dev)
{

	UMAC_DEBUG("%s-UMAC: Init called\n", dev->name);
	uccp310wlan_lmac_if_init(dev, dev->name);

	/* Enable the LMAC, set defaults and initialize TX */
	dev->reset_complete = 0;
	UMAC_PRINT("%s-UMAC: Reset (ENABLE)\n", dev->name);
	uccp310wlan_prog_reset(LMAC_ENABLE);

	if (wait_for_reset_complete(dev) < 0) {
		uccp310wlan_lmac_if_deinit();
		return -1;
	}

	uccp310wlan_prog_global_cfg(512, /* Rx MSDU life time in msecs */
			512, /* Tx MSDU life time in msecs */
			dev->params->ed_sensitivity,
			dev->params->auto_sensitivity,
			dev->params->rf_params);

	uccp310wlan_prog_txpower(dev->txpower);
	uccp310wlan_tx_init(dev);

	return 0;
}

void uccp310wlan_core_deinit(struct mac80211_dev *dev)
{
	UMAC_DEBUG("%s-UMAC: De-init called\n", dev->name);

	/* De initialize tx  and disable LMAC*/
	uccp310wlan_tx_deinit(dev);

	/* Disable the LMAC */
	dev->reset_complete = 0;
	UMAC_PRINT("%s-UMAC: Reset (DISABLE)\n", dev->name);
	uccp310wlan_prog_reset(LMAC_DISABLE);

	wait_for_reset_complete(dev);

	uccp310wlan_lmac_if_deinit();

	return;
}

void uccp310wlan_vif_add(struct umac_vif *uvif)
{
	unsigned int type;
	struct ieee80211_conf *conf = &uvif->dev->hw->conf;

	UMAC_DEBUG("%s-UMAC: Add VIF %d Type = %d\n", uvif->dev->name, uvif->vif_index, uvif->vif->type);

	uvif->config.atim_window = uvif->config.bcn_lost_cnt = uvif->config.aid = 0;

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		type = IF_MODE_STA_BSS;
		uvif->noa_active = 0;
		skb_queue_head_init(&uvif->noa_que);
		break;
	case NL80211_IFTYPE_ADHOC:
		type = IF_MODE_STA_IBSS;
		init_timer(&uvif->bcn_timer);
		uvif->bcn_timer.data = (unsigned long)uvif;
		uvif->bcn_timer.function = vif_bcn_timer_expiry;
		break;
	case NL80211_IFTYPE_AP:
		type = IF_MODE_AP;
		init_timer(&uvif->bcn_timer);
		uvif->bcn_timer.data = (unsigned long)uvif;
		uvif->bcn_timer.function = vif_bcn_timer_expiry;
		break;
	default:
		WARN_ON(1);
		return;
	}
	uccp310wlan_prog_vif_ctrl(uvif->vif_index,
			uvif->vif->addr,
			type,
			IF_ADD);

	/* Reprogram retry counts */
	uccp310wlan_prog_vif_short_retry(uvif->vif_index, uvif->vif->addr, conf->short_frame_max_tx_count);
	uccp310wlan_prog_vif_long_retry(uvif->vif_index, uvif->vif->addr, conf->long_frame_max_tx_count);

	if (uvif->vif->type == NL80211_IFTYPE_AP) {
		/* Program the EDCA params */
		int queue;
		for (queue = 0; queue < 4; queue++)
			uccp310wlan_prog_txq_params(uvif->vif_index,
					uvif->vif->addr,
					queue,
					uvif->config.edca_params[queue].aifs,
					uvif->config.edca_params[queue].txop,
					uvif->config.edca_params[queue].cwmin,
					uvif->config.edca_params[queue].cwmax);


	}
}

void uccp310wlan_vif_remove(struct umac_vif *uvif)
{
	struct sk_buff *skb;
	unsigned int type;
	unsigned long flags;
	UMAC_DEBUG("%s-UMAC: Remove VIF %d called\n", uvif->dev->name, uvif->vif_index);

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		type = IF_MODE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = IF_MODE_STA_IBSS;
		del_timer(&uvif->bcn_timer);
		break;
	case NL80211_IFTYPE_AP:
		type = IF_MODE_AP;
		del_timer(&uvif->bcn_timer);
		break;
	default:
		WARN_ON(1);
		return;
	}

	spin_lock_irqsave(&uvif->noa_que.lock, flags);
	while ((skb = __skb_dequeue(&uvif->noa_que)))
		dev_kfree_skb(skb);
	spin_unlock_irqrestore(&uvif->noa_que.lock, flags);

	uccp310wlan_prog_vif_ctrl(uvif->vif_index,
			uvif->vif->addr,
			type,
			IF_REM);

}

void uccp310wlan_vif_set_edca_params(unsigned short queue, struct umac_vif *uvif, const struct ieee80211_tx_queue_params *params, unsigned int vif_active)
{
	switch (queue) {
	case 0:
		queue = 3; /* Voice */
		break;
	case 1:
		queue = 2; /* Video */
		break;
	case 2:
		queue = 1; /* Best effort */
		break;
	case 3:
		queue = 0; /* Back groud */
		break;
	}

	UMAC_DEBUG("%s-UMAC: Set EDCA params for VIF %d, Values: %d, %d, %d, %d, %d\n", uvif->dev ? uvif->dev->name : 0, uvif->vif_index, queue,
			params->aifs, params->txop,
			params->cw_min, params->cw_max);
	if (uvif->dev->params->production_test == 0) {
		/* arbitration interframe space [0..255] */
		uvif->config.edca_params[queue].aifs = params->aifs;

		/* maximum burst time in units of 32 usecs, 0 meaning disabled */
		uvif->config.edca_params[queue].txop = params->txop;

		/* minimum contention window in units of  2^n-1 */
		uvif->config.edca_params[queue].cwmin = params->cw_min;

		/*  maximum contention window in units of 2^n-1 */
		uvif->config.edca_params[queue].cwmax = params->cw_max;
	} else {
		uvif->config.edca_params[queue].aifs = 3;
		uvif->config.edca_params[queue].txop = 0;
		uvif->config.edca_params[queue].cwmin = 0;
		uvif->config.edca_params[queue].cwmax = 0;
	}
	/* For the AP case, EDCA params are set before ADD interface is called.
	   Since this is not supported, we simply store the params and program them
	   to the LMAC after the interface is added */
	if (!vif_active)
		return;

	/* Program the txq parameters into the LMAC */
	uccp310wlan_prog_txq_params(uvif->vif_index,
			uvif->vif->addr,
			queue,
			params->aifs,
			params->txop,
			params->cw_min,
			params->cw_max);

}

void uccp310wlan_vif_bss_info_changed(struct umac_vif *uvif, struct ieee80211_bss_conf *bss_conf, unsigned int changed)
{
	struct ieee80211_bss *bss;
	UMAC_DEBUG("%s-UMAC: BSS INFO changed %d, %d, %d\n", uvif->dev->name, uvif->vif_index, uvif->vif->type, changed);

	bss = (void *)((cfg80211_get_bss(uvif->dev->hw->wiphy, NULL, bss_conf->bssid, NULL, 0, 0, 0))->priv);

	if (changed & BSS_CHANGED_BSSID)
		uccp310wlan_prog_vif_bssid(uvif->vif_index, uvif->vif->addr, (unsigned char *)bss_conf->bssid);

	if (changed & BSS_CHANGED_BASIC_RATES) {
		if (bss_conf->basic_rates)
			uccp310wlan_prog_vif_basic_rates(uvif->vif_index, uvif->vif->addr, bss_conf->basic_rates);
		else
			uccp310wlan_prog_vif_basic_rates(uvif->vif_index, uvif->vif->addr, 0x153);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		unsigned short queue;
		uccp310wlan_prog_vif_short_slot(uvif->vif_index,
				uvif->vif->addr,
				bss_conf->use_short_slot);

		for (queue = 0; queue < WLAN_AC_MAX_CNT; queue++)
			if (uvif->config.edca_params[queue].cwmin != 0)
				uccp310wlan_prog_txq_params(uvif->vif_index,
						uvif->vif->addr,
						queue,
						uvif->config.edca_params[queue].aifs,
						uvif->config.edca_params[queue].txop,
						uvif->config.edca_params[queue].cwmin,
						uvif->config.edca_params[queue].cwmax);
	}

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		if (changed & BSS_CHANGED_ASSOC) {
			if (bss_conf->assoc) {
				UMAC_DEBUG("%s-UMAC: AID %d, CAPS 0x%04x\n", uvif->dev->name, bss_conf->aid, bss_conf->assoc_capability |
						(bss->wmm_used << 9));
				uccp310wlan_prog_vif_aid(uvif->vif_index,
							uvif->vif->addr,
							bss_conf->aid);
				uccp310wlan_prog_vif_assoc_cap(uvif->vif_index,
							uvif->vif->addr,
							bss_conf->assoc_capability | (bss->wmm_used << 9));
				uvif->noa_active = 0;
			}
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		if (changed & BSS_CHANGED_BEACON_ENABLED) {
			if (uvif->vif->bss_conf.enable_beacon == true)
				mod_timer(&uvif->bcn_timer, jiffies + msecs_to_jiffies(uvif->vif->bss_conf.beacon_int));
			else
				del_timer(&uvif->bcn_timer);
		}
		if (changed & BSS_CHANGED_BEACON_INT) {
			if (uvif->vif->bss_conf.enable_beacon == true)
				mod_timer(&uvif->bcn_timer, jiffies + msecs_to_jiffies(uvif->vif->bss_conf.beacon_int));
		}
		break;
	case NL80211_IFTYPE_AP:
		if (changed & BSS_CHANGED_BEACON_ENABLED) {
			if (uvif->vif->bss_conf.enable_beacon == true)
				mod_timer(&uvif->bcn_timer, jiffies + msecs_to_jiffies(uvif->vif->bss_conf.beacon_int));
			else
				del_timer(&uvif->bcn_timer);
		}
		if (changed & BSS_CHANGED_BEACON_INT) {
			if (uvif->vif->bss_conf.enable_beacon == true)
				mod_timer(&uvif->bcn_timer, jiffies + msecs_to_jiffies(uvif->vif->bss_conf.beacon_int));
		}
		break;
	default:
		WARN_ON(1);
		return;
	}

}

void uccp310wlan_reset_complete(char *lmac_version, void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	memcpy(dev->stats->uccp310_lmac_version, lmac_version, 5);
	dev->stats->uccp310_lmac_version[5] = '\0';
	dev->reset_complete = 1;
}

void uccp310wlan_mib_stats(struct umac_event_mib_stats *mib_stats, void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;

	dev->stats->ofdm_rx_crc_success_cnt = mib_stats->ofdm_rx_crc_success_cnt;
	dev->stats->ofdm_rx_crc_fail_cnt = mib_stats->ofdm_rx_crc_fail_cnt;
	dev->stats->ofdm_rx_false_trig_cnt = mib_stats->ofdm_rx_false_trig_cnt;
	dev->stats->ofdm_rx_header_fail_cnt = mib_stats->ofdm_rx_header_fail_cnt;
	dev->stats->dsss_rx_crc_success_cnt = mib_stats->dsss_rx_crc_success_cnt;
	dev->stats->dsss_rx_crc_fail_cnt = mib_stats->dsss_rx_crc_fail_cnt;
	dev->stats->dsss_rx_false_trig_cnt = mib_stats->dsss_rx_false_trig_cnt;
	dev->stats->ed_cnt = mib_stats->ed_cnt;
	dev->stats->cca_fail_cnt = mib_stats->cca_fail_cnt;
	dev->stats->current_sensitivity = mib_stats->sensitivity;
}

void uccp310wlan_noa_event(int event, void *msg, void *context, struct sk_buff *skb)
{
	struct mac80211_dev  *dev = (struct mac80211_dev *)context;
	struct umac_event_noa *noa = (struct umac_event_noa *)msg;
	struct ieee80211_vif *vif;
	struct umac_vif *uvif;
	unsigned long flags;
	bool transmit = false;

	rcu_read_lock();

	if (event == EVENT_TX_DONE || event == CMD_TX)
		vif = (struct ieee80211_vif *)rcu_dereference(dev->vifs[(int)msg]);
	else
		vif = (struct ieee80211_vif *)rcu_dereference(dev->vifs[noa->vif_index]);

	if (vif == NULL) {
		rcu_read_unlock();
		return;
	}

	uvif = (struct umac_vif *)vif->drv_priv;

	spin_lock_irqsave(&uvif->noa_que.lock, flags);

	if (event == CMD_TX) {
		if (uvif->noa_active) {
			if (!uvif->noa_tx_allowed || skb_peek(&uvif->noa_que))
				skb_queue_tail(&uvif->noa_que, skb);
			else
				transmit = true;
		} else
			transmit = true;
	} else if (event == EVENT_TX_DONE) {
		if (uvif->noa_active && uvif->noa_tx_allowed) {
			skb = skb_dequeue(&uvif->noa_que);
			if (skb)
				transmit = true;
		}
	} else { /* event = EVENT_NOA */

		uvif->noa_active = noa->noa_active;
		if (uvif->noa_active) {
			printk(KERN_DEBUG "%s: noa active = %d, ap_present = %d\n", dev->name, noa->noa_active, noa->ap_present);
			uvif->noa_tx_allowed = noa->ap_present;
			if (uvif->noa_tx_allowed) {
				skb = skb_dequeue(&uvif->noa_que);
				if (skb)
					transmit = true;
			}
		} else {
			printk(KERN_DEBUG "%s: noa active = %d\n", dev->name, noa->noa_active);
			uvif->noa_tx_allowed = 1;
			/* Can be done in a better way. For now, just flush the NoA Queue */
			while ((skb = skb_dequeue(&uvif->noa_que)))
				dev_kfree_skb_any(skb);
		}
	}

	spin_unlock_irqrestore(&uvif->noa_que.lock, flags);

	rcu_read_unlock();

	if (transmit)
		uccp310wlan_tx_frame(skb, dev, false);

	return;
}

void uccp310wlan_rx_frame(struct sk_buff *skb, void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	struct umac_event_rx *rx = (struct umac_event_rx *)(skb->data);
	struct ieee80211_hdr *hdr;
	struct ieee80211_rx_status rx_status;
	int i;

	dev->stats->rx_packet_count++;

	skb_pull(skb, 32); /* Remove RX control information */

#ifdef CONFIG_RX_DEBUG
	printk(KERN_DEBUG "%s-RX: RX frame, length = %d, RSSI = %d, rate = %d\n", dev->name, rx->buff_len, rx->rssi, rx->rate);
	/* print_hex_dump(KERN_DEBUG, " ",DUMP_PREFIX_NONE,16,1,skb->data,skb->len,1); */
#endif


	hdr = (struct ieee80211_hdr *)skb->data;

	if ((ieee80211_is_data_qos(hdr->frame_control)) &&
			(!ieee80211_is_qos_nullfunc(hdr->frame_control)))
		skb_pull(skb, 2);


	memset(&rx_status, 0, sizeof(struct ieee80211_rx_status));

	rx_status.band = dev->hw->conf.chandef.chan->band;
	rx_status.freq = dev->hw->conf.chandef.chan->center_freq;
	rx_status.signal = rx->rssi;
	rx_status.antenna = 0;

	for (i = 0; i < dev->hw->wiphy->bands[dev->hw->conf.chandef.chan->band]->n_bitrates; i++) {
		if (rx->rate == dev->hw->wiphy->bands[dev->hw->conf.chandef.chan->band]->bitrates[i].hw_value) {
			rx_status.rate_idx = i;
			break;
		}
	}

	rx_status.flag |= RX_FLAG_DECRYPTED;
	rx_status.flag |= RX_FLAG_MMIC_STRIPPED;
	if (rx->status == RX_MIC_FAILURE)
		rx_status.flag |= RX_FLAG_MMIC_ERROR;

	if (((hdr->frame_control & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
			((hdr->frame_control & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BEACON)) {
		rx_status.mactime = get_unaligned_le64(rx->timestamp);
		rx_status.flag |= RX_FLAG_MACTIME_START;
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
	ieee80211_rx(dev->hw, skb);
}
