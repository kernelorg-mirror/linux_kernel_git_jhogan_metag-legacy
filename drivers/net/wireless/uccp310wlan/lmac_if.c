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
*** File Name  : lmac_if.c
***
*** File Description:
*** This file contains the defintions of helper functions for LMAC comms
***
******************************************************************************
*END**************************************************************************/
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

#include "lmac_if.h"

#ifdef CONFIG_LMACIF_DEBUG
#define LMACIF_DEBUG(fmt, args...) printk(KERN_DEBUG fmt, ##args)
#else
#define LMACIF_DEBUG(...) do { } while (0)
#endif

struct lmac_if_data {
	char      *name;
	void      *context;
};

static struct lmac_if_data __rcu *lmac_if;

static int uccp310wlan_send_cmd(unsigned char  *buf,
		unsigned int   len)
{
	struct umac_lmac_msg_hdr  *hdr = (struct umac_lmac_msg_hdr *)buf;
	struct sk_buff       *nbuf;
	unsigned char        *data;
	struct lmac_if_data *p;

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));
	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}

	nbuf = alloc_skb(len, GFP_ATOMIC);
	if (!nbuf) {
		rcu_read_unlock();
		return -1;
	}

	/* LMACIF_DEBUG ("%s-LMACIF: Sending command:%d\n", p->name, hdr->id); */
	hdr->flags = (len  - sizeof(struct umac_lmac_msg_hdr));

	data = skb_put(nbuf, len);
	memcpy(data, buf, len);

	hal_ops.send((void *)nbuf, LMAC_MOD_ID, UMAC_MOD_ID);
	rcu_read_unlock();
	return 0;
}

int uccp310wlan_prog_reset(unsigned int reset_type)
{
	struct umac_cmd_reset  reset;

	reset.hdr.id = CMD_RESET;
	reset.hdr.flags = 0;
	reset.reset_type = reset_type;
	return uccp310wlan_send_cmd((unsigned char *) &reset,
			sizeof(struct umac_cmd_reset));
}

int uccp310wlan_prog_vif_ctrl(int index,
		unsigned char *mac_addr,
		unsigned int  vif_type,
		unsigned int  op)
{
	struct umac_cmd_vif_ctrl  vif_ctrl;

	vif_ctrl.hdr.id = CMD_VIF_CTRL;
	vif_ctrl.hdr.flags = 0;
	vif_ctrl.if_mode = vif_type;
	memcpy(vif_ctrl.mac_addr, mac_addr, 6);
	vif_ctrl.if_index = index;
	vif_ctrl.if_ctrl = op;

	return uccp310wlan_send_cmd((unsigned char *) &vif_ctrl,
			sizeof(struct umac_cmd_vif_ctrl));
}

int uccp310wlan_prog_vif_basic_rates(int index,
		unsigned char *vif_addr,
		unsigned int basic_rate_set)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = BASICRATES_CHANGED;
	vif_cfg.basic_rate_set = basic_rate_set;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}
int uccp310wlan_prog_vif_short_slot(int index,
		unsigned char *vif_addr,
		unsigned int use_short_slot)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = SHORTSLOT_CHANGED;
	vif_cfg.use_short_slot = use_short_slot;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}

int uccp310wlan_prog_vif_powersave_mode(int index,
		unsigned char *vif_addr,
		unsigned int powersave_mode)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;
	vif_cfg.changed_bitmap = POWERSAVE_CHANGED;
	vif_cfg.powersave_mode = powersave_mode;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);
	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}
int uccp310wlan_prog_vif_atim_window(int index,
		unsigned char *vif_addr,
		unsigned int atim_window)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = ATIMWINDOW_CHANGED;
	vif_cfg.atim_window = atim_window;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}
int uccp310wlan_prog_vif_aid(int index,
		unsigned char *vif_addr,
		unsigned int aid)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = AID_CHANGED;
	vif_cfg.aid = aid;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}

int uccp310wlan_prog_vif_assoc_cap(int index,
		unsigned char *vif_addr,
		unsigned int caps)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = CAPABILITY_CHANGED;
	vif_cfg.capability = caps;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}
int uccp310wlan_prog_vif_apsd_type(int index,
		unsigned char *vif_addr,
		unsigned int uapsd_type)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = UAPSDTYPE_CHANGED;
	vif_cfg.uapsd_type = uapsd_type;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}
int uccp310wlan_prog_vif_long_retry(int index,
		unsigned char *vif_addr,
		unsigned int long_retry)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = LONGRETRY_CHANGED;
	vif_cfg.long_retry = long_retry;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}
int uccp310wlan_prog_vif_short_retry(int index,
		unsigned char *vif_addr,
		unsigned int short_retry)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = SHORTRETRY_CHANGED;
	vif_cfg.short_retry = short_retry;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}

int uccp310wlan_prog_vif_bssid(int index,
		unsigned char *vif_addr,
		unsigned char *bssid)
{
	struct umac_cmd_vif_cfg   vif_cfg;

	vif_cfg.hdr.id = CMD_VIF_CFG;
	vif_cfg.hdr.flags = 0;

	vif_cfg.changed_bitmap = BSSID_CHANGED;
	memcpy(vif_cfg.bssid, bssid, 6);
	memcpy(vif_cfg.vif_addr, vif_addr, 6);
	vif_cfg.if_index = index;
	return uccp310wlan_send_cmd((unsigned char *)&vif_cfg,
			sizeof(struct umac_cmd_vif_cfg));
}

int uccp310wlan_prog_powersave_state(int index,
		unsigned char *vif_addr,
		unsigned int powersave_state)
{
	struct umac_cmd_ps_cfg   ps_cfg;

	ps_cfg.hdr.id = CMD_PS_CFG;
	ps_cfg.hdr.flags = 0;

	ps_cfg.powersave_state = powersave_state;
	ps_cfg.if_index = index;
	memcpy(ps_cfg.vif_addr, vif_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *)&ps_cfg,
			sizeof(struct umac_cmd_ps_cfg));
}

int uccp310wlan_prog_global_cfg(unsigned int rx_msdu_lifetime,
		unsigned int tx_msdu_lifetime,
		unsigned int sensitivity,
		unsigned int dyn_ed_enable,
		unsigned char *rf_params)
{
	struct umac_cmd_global_cfg  gbl_config;

	gbl_config.hdr.id = CMD_GLOBAL_CFG;
	gbl_config.hdr.flags = 0;

	gbl_config.rx_msdu_lifetime = rx_msdu_lifetime;
	gbl_config.tx_msdu_lifetime = tx_msdu_lifetime;
	gbl_config.ed_sensitivity = sensitivity;
	gbl_config.dynamic_ed_enable = dyn_ed_enable;
	memcpy(gbl_config.rf_params, rf_params, 8);

	return uccp310wlan_send_cmd((unsigned char *) &gbl_config,
			sizeof(struct umac_cmd_global_cfg));
}

int uccp310wlan_prog_txpower(unsigned int txpower)
{
	struct umac_cmd_txpower  power;

	power.hdr.id = CMD_TX_POWER;
	power.hdr.flags = 0;

	power.txpower = txpower;

	return uccp310wlan_send_cmd((unsigned char *) &power,
			sizeof(struct umac_cmd_txpower));
}

int uccp310wlan_prog_mcast_addr_cfg(unsigned char  *mcast_addr,
		unsigned int   op)
{
	struct umac_cmd_mcst_filter_cfg    mcast_config;

	mcast_config.hdr.id = CMD_MCST_FILTER_CFG;
	mcast_config.hdr.flags = 0;
	mcast_config.mcst_ctrl = op;
	memcpy(mcast_config.addr, mcast_addr, 6);

	return uccp310wlan_send_cmd((unsigned char *) &mcast_config,
			sizeof(struct umac_cmd_mcst_filter_cfg));
}

int uccp310wlan_prog_mcast_filter_control(unsigned int mcast_filter_enable)
{
	struct umac_cmd_mcst_filter_ctrl  mcast_ctrl;

	mcast_ctrl.hdr.id = CMD_MCST_FILTER_CTRL;
	mcast_ctrl.hdr.flags = 0;
	mcast_ctrl.ctrl = mcast_filter_enable;

	return uccp310wlan_send_cmd((unsigned char *) &mcast_ctrl,
			sizeof(struct umac_cmd_mcst_filter_ctrl));
}

int uccp310wlan_prog_rcv_bcn_mode(unsigned int  bcn_rcv_mode)
{
	struct umac_cmd_rcv_bcn_mode  rcv_bcn_mode;

	rcv_bcn_mode.hdr.id = CMD_RCV_BCN_MODE;
	rcv_bcn_mode.hdr.flags = 0;
	rcv_bcn_mode.mode = bcn_rcv_mode;

	return uccp310wlan_send_cmd((unsigned char *) &rcv_bcn_mode,
			sizeof(struct umac_cmd_rcv_bcn_mode));
}

int uccp310wlan_prog_txq_params(int index,
		unsigned char *addr,
		unsigned int queue,
		unsigned int aifs,
		unsigned int txop,
		unsigned int cwmin,
		unsigned int cwmax)
{
	struct umac_cmd_txq_params    params;

	params.hdr.id = CMD_TXQ_PARAMS;
	params.hdr.flags = 0;

	params.if_index = index;
	memcpy(params.vif_addr, addr, ETH_ALEN);
	params.queue_num = queue;
	params.aifsn = aifs;
	params.txop = txop;
	params.cwmin = cwmin;
	params.cwmax = cwmax;

	return uccp310wlan_send_cmd((unsigned char *) &params,
			sizeof(struct umac_cmd_txq_params));
}

int uccp310wlan_prog_channel(unsigned int ch)
{
	struct umac_cmd_channel    channel;

	channel.hdr.id = CMD_CHANNEL;
	channel.hdr.flags = 0;
	channel.channel = ch;

	return uccp310wlan_send_cmd((unsigned char *) &channel,
			sizeof(struct umac_cmd_channel));
}

int uccp310wlan_prog_peer_key(int              vif_index,
		unsigned char    *vif_addr,
		unsigned int     op,
		unsigned int     key_id,
		unsigned int     key_type,
		unsigned int     cipher_type,
		struct umac_key  *key)
{
	struct umac_cmd_peer_key_cfg  peer_key;

	peer_key.hdr.id = CMD_PEER_KEY_CFG;
	peer_key.hdr.flags = 0;

	peer_key.if_index = vif_index;
	memcpy(peer_key.vif_addr, vif_addr, ETH_ALEN);
	peer_key.op = op;
	peer_key.key_id = key_id;
	memcpy(peer_key.peer_mac, key->peer_mac, ETH_ALEN);

	peer_key.key_type = key_type;
	peer_key.cipher_type = cipher_type;
	memcpy(peer_key.key, key->key, MAX_KEY_LEN);
	if (key->tx_mic)
		memcpy(peer_key.tx_mic, key->tx_mic, MICHAEL_LEN);
	if (key->rx_mic)
		memcpy(peer_key.rx_mic, key->rx_mic, MICHAEL_LEN);
	return uccp310wlan_send_cmd((unsigned char *) &peer_key,
			sizeof(struct umac_cmd_peer_key_cfg));
}

int uccp310wlan_prog_if_key(int             vif_index,
		unsigned char   *vif_addr,
		unsigned int    op,
		unsigned int    key_id,
		unsigned int    cipher_type,
		struct umac_key     *key)
{
	struct umac_cmd_if_key_cfg  if_key;

	if_key.hdr.id = CMD_IF_KEY_CFG;
	if_key.hdr.flags = 0;

	if_key.if_index = vif_index;
	memcpy(if_key.vif_addr, vif_addr, 6);
	if_key.key_id = key_id;
	if_key.op = op;

	if (op == KEY_CTRL_ADD) {
		if_key.cipher_type = cipher_type;
		if (cipher_type == CIPHER_TYPE_TKIP ||	cipher_type == CIPHER_TYPE_CCMP) {
			memcpy(if_key.key.rsn_grp_key.key, key->key, MAX_KEY_LEN);
			if (key->tx_mic)
				memcpy(if_key.key.rsn_grp_key.mic_key, key->tx_mic, MICHAEL_LEN);
		} else {
			if_key.key.wep_key.key_len =
				(cipher_type == CIPHER_TYPE_WEP40) ? 5 : 13;
			memcpy(if_key.key.wep_key.wep_key, key->key,
					if_key.key.wep_key.key_len);
		}
	}
	return uccp310wlan_send_cmd((unsigned char *) &if_key,
			sizeof(struct umac_cmd_if_key_cfg));
}

int uccp310wlan_prog_tx(struct sk_buff  *skb)
{
	struct umac_cmd_tx    *tx_cmd;
	struct lmac_if_data *p;
	tx_cmd = (struct umac_cmd_tx *)skb->data;
	tx_cmd->hdr.id = CMD_TX;
	tx_cmd->hdr.flags = ((sizeof(struct umac_cmd_tx) - sizeof(struct umac_lmac_msg_hdr)) |	((skb->len - sizeof(struct umac_cmd_tx)) << 16));

	rcu_read_lock();

	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}

	hal_ops.send((void *)skb, LMAC_MOD_ID, UMAC_MOD_ID);

	rcu_read_unlock();
	return 0;
}

int uccp310wlan_prog_mib_stats(void)
{
	struct umac_cmd_mib_stats    mib_stats;

	mib_stats.hdr.id = CMD_MIB_STATS;
	mib_stats.hdr.flags = 0;

	return uccp310wlan_send_cmd((unsigned char *) &mib_stats,
			sizeof(struct umac_cmd_mib_stats));
}

int uccp310wlan_prog_phy_stats(void)
{
	struct umac_cmd_phy_stats  phy_stats;

	phy_stats.hdr.id = CMD_PHY_STATS;
	phy_stats.hdr.flags = 0;

	return uccp310wlan_send_cmd((unsigned char *) &phy_stats,
			sizeof(struct umac_cmd_phy_stats));
}

static int uccp310wlan_msg_handler (void *nbuff,
		unsigned char sender_id)
{
	unsigned int              event;
	unsigned char             *buff;
	struct umac_lmac_msg_hdr  *hdr;
	struct lmac_if_data       *p;
	struct sk_buff            *skb = (struct sk_buff *)nbuff;

	rcu_read_lock();

	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		dev_kfree_skb_any(skb);
		rcu_read_unlock();
		return 0;
	}

	buff = skb->data;
	hdr = (struct umac_lmac_msg_hdr *)buff;

	hdr->id &= 0x0000ffff;
	hdr->flags &= 0x0000ffff;

	event = hdr->id;

	/* LMACIF_DEBUG("%s-LMACIF: event %d received\n", p->name, event); */
	if (event == EVENT_RESET_COMPLETE) {
		struct umac_event_reset_complete *r = (struct umac_event_reset_complete *)buff;
		uccp310wlan_reset_complete(r->version, p->context);

	} else if (event == EVENT_RX /* || event == EVENT_RX_MIC_FAILURE*/) {
		uccp310wlan_rx_frame(nbuff, p->context);

	} else if (event == EVENT_TX_DONE) {
		uccp310wlan_tx_complete((struct umac_event_tx_done *)buff,	p->context);

	} else if (event == EVENT_MIB_STAT) {
		struct umac_event_mib_stats  *mib_stats = (struct umac_event_mib_stats *) buff;
		uccp310wlan_mib_stats(mib_stats, p->context);

	} else if (event == EVENT_NOA) {
		uccp310wlan_noa_event(event, (void *)buff, p->context, NULL);

	} else {
		/*
		 * TODO:: handle remaining events
		 */
	}

	if (event != EVENT_RX)
		dev_kfree_skb_any(skb);

	rcu_read_unlock();
	return 0;
}

int uccp310wlan_lmac_if_init(void *context, const char *name)
{
	struct lmac_if_data *p;
	LMACIF_DEBUG("%s-LMACIF: lmac_if init called\n", name);
	p = kzalloc(sizeof(struct lmac_if_data), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	p->name = (char *)name;
	p->context = context;
	hal_ops.register_callback(uccp310wlan_msg_handler, UMAC_MOD_ID);
	rcu_assign_pointer(lmac_if, p);
	return 0;
}

void uccp310wlan_lmac_if_deinit(void)
{
	struct lmac_if_data *p;
	LMACIF_DEBUG("%s-LMACIF: Deinit called\n", lmac_if->name);
	p = rcu_dereference(lmac_if);
	rcu_assign_pointer(lmac_if, NULL);
	synchronize_rcu();
	kfree(p);
}
