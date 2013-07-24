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
*** File Name  : tx.c
***
*** File Description:
*** This file contains the source functions UMAC TX logic
***
******************************************************************************
*END**************************************************************************/
#include "umac.h"
#ifdef CONFIG_TX_DEBUG
#define UMACTX_DEBUG(fmt, args...) printk(KERN_DEBUG fmt, ##args)
#else
#define UMACTX_DEBUG(...) do { } while (0)
#endif

#define UMACTX_TO_MACDEV(x) ((struct mac80211_dev *)(container_of(x, struct mac80211_dev, tx)))

static void wait_for_tx_complete(struct tx_config *tx)
{
	int count;

	count = 0;

	while (tx->tx_buff_pool_bmp) {
		count++;

		if (count < TX_COMPLETE_TIMEOUT_TICKS) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		} else {
			printk(KERN_DEBUG "%s-UMACTX: WARNING: TX complete didn't succeed after %ld timer "
					"ticks, 0x%08x\n", UMACTX_TO_MACDEV(tx)->name, TX_COMPLETE_TIMEOUT_TICKS, (unsigned int) tx->tx_buff_pool_bmp);

			break;
		}
	}

	if (count && (count < TX_COMPLETE_TIMEOUT_TICKS))
		UMACTX_DEBUG("TX complete after %d timer ticks\n", count);
}

static inline int tx_queue_map(int queue)
{
	unsigned int ac[4] = {WLAN_AC_VO, WLAN_AC_VI, WLAN_AC_BE, WLAN_AC_BK};

	if (queue < 4)
		return ac[queue];

	return WLAN_AC_VO;
}

static inline int tx_queue_unmap(int queue)
{
	unsigned int ac[4] = {3, 2, 1, 0};

	return ac[queue];
}

static void get_rate(struct sk_buff *skb, struct umac_cmd_tx *txcmd, struct mac80211_dev *dev)
{
	struct ieee80211_rate *rate;
	struct ieee80211_tx_info *c;
	unsigned int index;

	rate = ieee80211_get_tx_rate(dev->hw, IEEE80211_SKB_CB(skb));
	if (rate == NULL) {
		rate = &dev->hw->wiphy->bands[dev->hw->conf.chandef.chan->band]->bitrates[0];
		txcmd->num_rates = 1;
		txcmd->rate[0] = rate->hw_value;
		txcmd->rate_retries[0] = 5;
		txcmd->rate_protection_type[0] = USE_PROTECTION_NONE;
		txcmd->rate_preamble_type[0] = DONT_USE_SHORT_PREAMBLE;
	} else {
		c = IEEE80211_SKB_CB(skb);
		txcmd->num_rates = 0;
		for (index = 0; index < 4; index++) {
			if (c->control.rates[index].idx >= 0) {
				rate = &dev->hw->wiphy->bands[c->band]->bitrates[c->control.rates[index].idx];
				txcmd->rate[index] = rate->hw_value;
				txcmd->rate_retries[index] = c->control.rates[index].count;
				if (c->control.rates[index].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
					txcmd->rate_preamble_type[index] = USE_SHORT_PREAMBLE;
				else
					txcmd->rate_preamble_type[index] = DONT_USE_SHORT_PREAMBLE;

				if (c->control.rates[index].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
					txcmd->rate_protection_type[index] = USE_PROTECTION_CTS2SELF;
				else if (c->control.rates[index].flags & IEEE80211_TX_RC_USE_RTS_CTS)
					txcmd->rate_protection_type[index] = USE_PROTECTION_RTS;
				else
					txcmd->rate_protection_type[index] = USE_PROTECTION_NONE;
				txcmd->num_rates++;
			}
		}
	}
	return;
}

static void tx_status(struct sk_buff *skb, struct umac_event_tx_done *tx_done, struct mac80211_dev *dev)
{
	int index;
	struct ieee80211_tx_info *tx_info;

	tx_info = IEEE80211_SKB_CB(skb);

	if (tx_done->frm_status == UMAC_EVENT_TX_DONE_SUCCESS)
		tx_info->flags |= IEEE80211_TX_STAT_ACK;

	for (index = 0; index < 4; index++) {
		if ((dev->hw->wiphy->bands[tx_info->band]->bitrates[tx_info->status.rates[index].idx]).hw_value == (tx_done->rate)) {
			tx_info->status.rates[index].count = (tx_done->retries_num + 1);
			break;
		}
	}
	while (((index + 1) < 4) && (tx_info->status.rates[index + 1].idx >= 0)) {
		tx_info->status.rates[index + 1].idx = -1;
		tx_info->status.rates[index + 1].count = 0;
		index++;
	}
	ieee80211_tx_status(dev->hw, skb);

}

static int uccp310wlan_tx_alloc_buff_req(struct mac80211_dev *dev, int queue, unsigned int *id, struct sk_buff *skb)
{
	int cnt = 0;
	struct tx_config *tx = &dev->tx;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);

	UMACTX_DEBUG("%s: Alloc buf Req q = %d, bmap = 0x%08lx\n", dev->name, queue, tx->tx_buff_pool_bmp);

	*id = MAX_BUFF_POOL_ELEMENTS;
	/*
	 * first 4 tx tokens will be reserved for 4 AC's
	 * 5th and 6th tokens are reserved for beacon or broadcast or ATIMframes..
	 */
	if (queue != WLAN_AC_BCN) {
		if (!__test_and_set_bit(queue, &tx->tx_buff_pool_bmp))
			*id = queue;
		else {
			/*
			 * while searching the buff pool bmp, it should be noted that 2 buffers
			 * are reserved for beacon. so pool bmp should be searched from beacon queue+2
			 * instead of WLAN_AC_MAX_CNT
			 */
			for (cnt = WLAN_AC_BCN+2; cnt < MAX_BUFF_POOL_ELEMENTS; cnt++) {
				if (!__test_and_set_bit(cnt, &tx->tx_buff_pool_bmp)) {
					*id = cnt;
					break;
				}

			}

		}

	} else {/* (queue == WLAN_AC_BCN) */
		if (!__test_and_set_bit(WLAN_AC_BCN, &tx->tx_buff_pool_bmp))
			*id = WLAN_AC_BCN;

		else if (!__test_and_set_bit(WLAN_AC_BCN+1, &tx->tx_buff_pool_bmp))
			*id = WLAN_AC_BCN+1;

	}

	if (*id == MAX_BUFF_POOL_ELEMENTS) {
		skb_queue_tail(&tx->pending_pkt[queue], skb);
		if ((queue != WLAN_AC_BCN) && (tx->pending_pkt[queue].qlen > MAX_TX_QUEUE_LEN)) {
			ieee80211_stop_queue(dev->hw, skb->queue_mapping);
			tx->queue_stopped_bmp |= (1 << queue);
		}
	} else {
		tx->tx_pkt[*id] = skb;
	}

	UMACTX_DEBUG("%s: Alloc buf Result *id = %d, bmap = 0x%08lx\n", dev->name, *id, tx->tx_buff_pool_bmp);
	spin_unlock_irqrestore(&tx->lock, flags);

	return 0;
}

static struct sk_buff *uccp310wlan_tx_free_buff_req(struct mac80211_dev  *dev, unsigned int buff_pool_id, unsigned int *queue, struct sk_buff **skb)
{
	int i;
	unsigned long flags;
	struct tx_config *tx = &dev->tx;
	struct sk_buff *pending = NULL;

	spin_lock_irqsave(&tx->lock, flags);

	for (i = 0; i < WLAN_AC_MAX_CNT; i++)
		if (skb_peek(&tx->pending_pkt[i]))
			break;
	if (i == WLAN_AC_MAX_CNT) {
		/* No pending packets */
		__clear_bit(buff_pool_id, &tx->tx_buff_pool_bmp);
	} else if (buff_pool_id <= WLAN_AC_MAX_CNT) { /* Reserved token */
		if (buff_pool_id >= WLAN_AC_BCN)
			*queue =  WLAN_AC_BCN;
		else
			*queue = buff_pool_id;

		pending = skb_dequeue(&tx->pending_pkt[*queue]);
		if (!pending)
			__clear_bit(buff_pool_id, &tx->tx_buff_pool_bmp);
	} else if (buff_pool_id > WLAN_AC_MAX_CNT) { /* Spare token */
		unsigned int next_spare_token_ac = tx->next_spare_token_ac + 1;
		if (next_spare_token_ac == WLAN_AC_MAX_CNT)
			tx->next_spare_token_ac = 0;
		else
			tx->next_spare_token_ac = next_spare_token_ac;
		while (!skb_peek(&tx->pending_pkt[tx->next_spare_token_ac])) {
			next_spare_token_ac = tx->next_spare_token_ac + 1;
			if (next_spare_token_ac == WLAN_AC_MAX_CNT)
				tx->next_spare_token_ac = 0;
			else
				tx->next_spare_token_ac = next_spare_token_ac;
		}
		pending = skb_dequeue(&tx->pending_pkt[tx->next_spare_token_ac]);
		*queue = tx->next_spare_token_ac;
	}

	*skb = tx->tx_pkt[buff_pool_id];
	tx->tx_pkt[buff_pool_id] = pending;


	if (pending && (tx->queue_stopped_bmp & (1 << *queue)) &&
			tx->pending_pkt[*queue].qlen < (MAX_TX_QUEUE_LEN / 2)) {
		ieee80211_wake_queue(dev->hw, tx_queue_unmap(*queue));
		tx->queue_stopped_bmp &= ~(1 << (*queue));
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	return pending;
}


void uccp310wlan_tx_init(struct mac80211_dev *dev)
{
	int cnt = 0;
	struct tx_config *tx = &dev->tx;

	tx->tx_buff_pool_bmp = 0;
	tx->queue_stopped_bmp = 0;
	tx->next_spare_token_ac = WLAN_AC_BE;

	for (cnt = 0; cnt < WLAN_AC_MAX_CNT; cnt++)
		skb_queue_head_init(&tx->pending_pkt[cnt]);

	for (cnt = 0; cnt < MAX_BUFF_POOL_ELEMENTS; cnt++)
		tx->tx_pkt[cnt] = NULL;

	spin_lock_init(&tx->lock);
	ieee80211_wake_queues(dev->hw);

	UMACTX_DEBUG("%s-UMACTX: initialization successful\n", UMACTX_TO_MACDEV(tx)->name);
}

void uccp310wlan_tx_deinit(struct mac80211_dev *dev)
{
	int  cnt = 0;
	struct sk_buff    *skb;
	struct tx_config  *tx = &dev->tx;

	ieee80211_stop_queues(dev->hw);

	wait_for_tx_complete(tx);

	for (cnt = 0; cnt < MAX_BUFF_POOL_ELEMENTS; cnt++) {
		if (tx->tx_pkt[cnt]) {
			dev_kfree_skb_any(tx->tx_pkt[cnt]);
			tx->tx_pkt[cnt] = NULL;
		}
	}

	for (cnt = 0; cnt < WLAN_AC_MAX_CNT; cnt++) {
		skb = skb_dequeue(&tx->pending_pkt[cnt]);
		while (skb) {
			dev_kfree_skb_any(skb);
			skb = skb_dequeue(&tx->pending_pkt[cnt]);
		}

	}

	UMACTX_DEBUG("%s-UMACTX: deinitialization successful\n", UMACTX_TO_MACDEV(tx)->name);
}


static int __uccp310wlan_tx_frame(struct sk_buff  *skb,
		unsigned int       queue,
		unsigned int       buff_pool_id,
		unsigned int       more_frames,
		struct mac80211_dev     *dev)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct sk_buff *nbuff;
	struct umac_cmd_tx tx_cmd;
	unsigned char *data;
	int vif_index;

	vif_index = vif_addr_to_index(hdr->addr2, dev);
	if (vif_index > -1) {
		tx_cmd.if_index = vif_index;
		memcpy(tx_cmd.vif_addr, hdr->addr2, ETH_ALEN);
	} else {
		tx_cmd.if_index = 0;
		memcpy(tx_cmd.vif_addr, dev->if_mac_addresses[0].addr, ETH_ALEN);
	}
	tx_cmd.queue_num = queue;
	tx_cmd.buff_pool_id = buff_pool_id;
	tx_cmd.more_frms = more_frames;
	tx_cmd.tx_power = dev->txpower;

	/* Get the rate at which packet has to be transmitted */
	get_rate(skb, &tx_cmd, dev);

	tx_cmd.num_of_frags = 1;
	tx_cmd.last_frag_len = skb->len;
	tx_cmd.frag_len = skb->len;

	tx_cmd.force_encrypt = 0;

	nbuff = alloc_skb(128 + skb->len, GFP_ATOMIC); /* 128 bytes offset from start of cmd_tx to payload. sizeof(cmd_tx) < 128 */
	if (!nbuff) {
		printk(KERN_DEBUG "%s-UMACTX: Unable to allocate skb for TX packet\n", dev->name);
		return -1;
	}
	data = skb_put(nbuff, 128);
	memcpy(data, &tx_cmd, sizeof(struct umac_cmd_tx));
	data = skb_put(nbuff, skb->len);
	memcpy(data, skb->data, skb->len);

	UMACTX_DEBUG("%s-UMACTX: TX Frame, Queue = %d, pool_id = %d, len = %d\n", dev->name, tx_cmd.queue_num, tx_cmd.buff_pool_id, nbuff->len);
	UMACTX_DEBUG("%s-UMACTX: Num rates = %d, %d, %d, %d, %d\n", dev->name, tx_cmd.num_rates, tx_cmd.rate[0], tx_cmd.rate[1], tx_cmd.rate[2], tx_cmd.rate[3]);
#ifdef CONFIG_TX_DEBUG
	/* print_hex_dump(KERN_DEBUG, " ",DUMP_PREFIX_NONE,16,1,nbuff->data,nbuff->len,1); */
#endif
	return uccp310wlan_prog_tx(nbuff);
}

int uccp310wlan_tx_frame(struct sk_buff *skb,
		struct mac80211_dev *dev,
		bool bcast)
{
	unsigned int queue, buff_pool_id, more_frames;

	if (bcast == false) {
		queue = tx_queue_map(skb->queue_mapping);
		more_frames = 0;
	} else {
		queue = WLAN_AC_BCN;
		more_frames = skb->priority; /* Hack: skb->priority is used to indicate more frames */
	}

	uccp310wlan_tx_alloc_buff_req(dev, queue, &buff_pool_id, skb);

	if (buff_pool_id == MAX_BUFF_POOL_ELEMENTS)
		return NETDEV_TX_OK;


	if (__uccp310wlan_tx_frame(skb, queue, buff_pool_id, more_frames, dev) < 0) {
		struct umac_event_tx_done tx_done;
		printk(KERN_DEBUG "%s-UMACTX: Unable to send frame, dropping ..\n", dev->name);

		tx_done.buff_pool_id = buff_pool_id;
		tx_done.frm_status = UMAC_EVENT_TX_DONE_ERROR_RETRY_LIMIT;
		tx_done.rate = 0;
		uccp310wlan_tx_complete(&tx_done, dev);
	}

	return NETDEV_TX_OK;
}

void uccp310wlan_tx_complete(struct umac_event_tx_done *tx_done, void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	struct sk_buff *skb, *pending;
	unsigned int queue, more_frames;
	int vif_index, vif_index_bitmap = 0;

tx_complete:
	queue = 0;
	UMACTX_DEBUG("%s-UMACTX: TX Done, pool_id = %d, status = %d, rate = %d, retries = %d\n", dev->name, tx_done->buff_pool_id, tx_done->frm_status, tx_done->rate, tx_done->retries_num);


	pending = uccp310wlan_tx_free_buff_req(dev, tx_done->buff_pool_id, &queue, &skb);

	if (skb) {
		if (!ieee80211_is_beacon(((struct ieee80211_hdr *)(skb->data))->frame_control)) {
			vif_index = vif_addr_to_index(((struct ieee80211_hdr *)(skb->data))->addr2, dev);
			if (vif_index > -1)
				vif_index_bitmap |= (1 << vif_index);
			tx_status(skb, tx_done, dev);
		} else
			dev_kfree_skb_any(skb);
	}

	if (pending) {
		if ((queue == WLAN_AC_BCN) && (pending->priority == 1))
			more_frames = 1;
		else
			more_frames = 0;

		if (__uccp310wlan_tx_frame(pending, queue, tx_done->buff_pool_id, more_frames, dev) < 0) {
			printk(KERN_DEBUG "%s-UMACTX: Unable to send pending frame, dropping ..\n", dev->name);

			tx_done->frm_status = UMAC_EVENT_TX_DONE_ERROR_RETRY_LIMIT;
			tx_done->rate = 0;
			goto tx_complete;
		}
	}

	for (vif_index = 0; vif_index < MAX_VIFS; vif_index++)
		if (vif_index_bitmap & (1 << vif_index))
			uccp310wlan_noa_event(EVENT_TX_DONE, (void *)vif_index, (void *)dev, NULL);

}
