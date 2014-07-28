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
*** File Name  : 80211_if.c
***
*** File Description:
*** This file is the glue layer between net/mac80211 and UMAC
***
******************************************************************************
*END**************************************************************************/
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/device.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <net/ieee80211_radiotap.h>
#include <../net/mac80211/ieee80211_i.h>

#include "version.h"
#include "umac.h"
#include "utils.h"

#ifdef CONFIG_80211IF_DEBUG
#define _80211IF_DEBUG(fmt, args...) printk(KERN_DEBUG fmt, ##args)
#else
#define _80211IF_DEBUG(...) do { } while (0)
#endif

extern int reset_hal_params(void);

static char   *mac_addr = DEFAULT_MAC_ADDRESS;

/* Its value will be the default mac address and it can only be updated with the
 * command line arguments
 */
module_param(mac_addr, charp, 0000);

#define CHAN2G(_freq, _idx)  {		\
	.band = IEEE80211_BAND_2GHZ,	\
	.center_freq = (_freq),		\
	.hw_value = (_idx),		\
	.max_power = 20,		\
}

#define CHAN5G(_freq, _idx) {		\
	.band = IEEE80211_BAND_5GHZ,	\
	.center_freq = (_freq),		\
	.hw_value = (_idx),		\
	.max_power = 20,		\
}

struct wifi_dev {
	struct proc_dir_entry *umac_proc_dir_entry;
	struct wifi_params params;
	struct wifi_stats  stats;
	struct ieee80211_hw *hw;
};

static struct wifi_dev *wifi;

static struct ieee80211_channel dsss_chantable[] = {
	CHAN2G(2412, 0), /* Channel 1 */
	CHAN2G(2417, 1), /* Channel 2 */
	CHAN2G(2422, 2), /* Channel 3 */
	CHAN2G(2427, 3), /* Channel 4 */
	CHAN2G(2432, 4), /* Channel 5 */
	CHAN2G(2437, 5), /* Channel 6 */
	CHAN2G(2442, 6), /* Channel 7 */
	CHAN2G(2447, 7), /* Channel 8 */
	CHAN2G(2452, 8), /* Channel 9 */
	CHAN2G(2457, 9), /* Channel 10 */
	CHAN2G(2462, 10), /* Channel 11 */
	CHAN2G(2467, 11), /* Channel 12 */
	CHAN2G(2472, 12), /* Channel 13 */
	CHAN2G(2484, 13), /* Channel 14 */
};

static struct ieee80211_channel ofdm_chantable[] = {
	CHAN5G(5180, 14), /* Channel 36 */
	CHAN5G(5200, 15), /* Channel 40 */
	CHAN5G(5220, 16), /* Channel 44 */
	CHAN5G(5240, 17), /* Channel 48 */
	CHAN5G(5260, 18), /* Channel 52 */
	CHAN5G(5280, 19), /* Channel 56 */
	CHAN5G(5300, 20), /* Channel 60 */
	CHAN5G(5320, 21), /* Channel 64 */
	CHAN5G(5745, 33), /* Channel 149 */
	CHAN5G(5765, 34), /* Channel 153 */
	CHAN5G(5785, 35), /* Channel 157 */
	CHAN5G(5805, 36), /* Channel 161 */
};

static struct ieee80211_rate dsss_rates[] = {
	{ .bitrate = 10, .hw_value = 2 },
	{ .bitrate = 20, .hw_value = 4, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .hw_value = 11, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .hw_value = 22, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 , .hw_value = 12},
	{ .bitrate = 90 , .hw_value = 18},
	{ .bitrate = 120 , .hw_value = 24},
	{ .bitrate = 180 , .hw_value = 36},
	{ .bitrate = 240 , .hw_value = 48},
	{ .bitrate = 360 , .hw_value = 72},
	{ .bitrate = 480 , .hw_value = 96},
	{ .bitrate = 540 , .hw_value = 108}
};

static struct ieee80211_rate ofdm_rates[] = {
	{ .bitrate = 60 , .hw_value = 12},
	{ .bitrate = 90 , .hw_value = 18},
	{ .bitrate = 120 , .hw_value = 24},
	{ .bitrate = 180 , .hw_value = 36},
	{ .bitrate = 240 , .hw_value = 48},
	{ .bitrate = 360 , .hw_value = 72},
	{ .bitrate = 480 , .hw_value = 96},
	{ .bitrate = 540 , .hw_value = 108}
};

static struct ieee80211_supported_band band_2ghz = {
	.channels = dsss_chantable,
	.n_channels = ARRAY_SIZE(dsss_chantable),
	.band = IEEE80211_BAND_2GHZ,
	.bitrates = dsss_rates,
	.n_bitrates = ARRAY_SIZE(dsss_rates),
};

static struct ieee80211_supported_band band_5ghz = {
	.channels = ofdm_chantable,
	.n_channels = ARRAY_SIZE(ofdm_chantable),
	.band = IEEE80211_BAND_5GHZ,
	.bitrates = ofdm_rates,
	.n_bitrates = ARRAY_SIZE(ofdm_rates),
};

static const struct ieee80211_iface_limit if_limit1[] = {{ .max = 4, .types = BIT(NL80211_IFTYPE_STATION)} };
static const struct ieee80211_iface_limit if_limit2[] = {{ .max = 2, .types = BIT(NL80211_IFTYPE_STATION)} };
static const struct ieee80211_iface_limit if_limit3[] = {{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION),},
							 { .max = 1, .types = BIT(NL80211_IFTYPE_AP)|
									      BIT(NL80211_IFTYPE_P2P_CLIENT)|
								      BIT(NL80211_IFTYPE_ADHOC)|
									      BIT(NL80211_IFTYPE_P2P_GO)} };
static const struct ieee80211_iface_limit if_limit4[] = {{ .max = 1, .types = BIT(NL80211_IFTYPE_AP)},
							 { .max = 1, .types = BIT(NL80211_IFTYPE_P2P_GO)} };

static const struct ieee80211_iface_limit if_limit5[] = {{ .max = 1, .types = BIT(NL80211_IFTYPE_ADHOC)},
							 { .max = 1, .types = BIT(NL80211_IFTYPE_P2P_CLIENT)} };

static const struct ieee80211_iface_combination if_comb[] = {
	{ .limits = if_limit1, .n_limits = ARRAY_SIZE(if_limit1), .max_interfaces = 4, .num_different_channels = 1},
	{ .limits = if_limit2, .n_limits = ARRAY_SIZE(if_limit2), .max_interfaces = 2, .num_different_channels = 1},
	{ .limits = if_limit3, .n_limits = ARRAY_SIZE(if_limit3), .max_interfaces = 2, .num_different_channels = 1},
	{ .limits = if_limit4, .n_limits = ARRAY_SIZE(if_limit4), .max_interfaces = 2, .num_different_channels = 1},
	{ .limits = if_limit5, .n_limits = ARRAY_SIZE(if_limit5), .max_interfaces = 2, .num_different_channels = 1}
};

static int conv_str_to_byte(unsigned char *byte,
		unsigned char *str,
		int len)
{
	int  i, j = 0;
	unsigned char  ch, val = 0;

	for (i = 0; i < (len * 2); i++) {
		/*convert to lower*/
		ch = ((str[i] >= 'A' && str[i] <= 'Z') ? str[i] + 32 : str[i]);
		if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f'))
			return -1;
		if (ch >= '0' && ch <= '9')  /*check is digit*/
			ch = ch - '0';
		else
			ch = ch - 'a' + 10;
		val += ch;
		if (!(i%2))
			val <<= 4;
		else {
			byte[j] = val;
			j++;
			val = 0;
		}
	}
	return 0;
}

static unsigned char get_ps_info(unsigned char *ie_data,
		int ie_len)
{
	unsigned char *pos, *end;
	unsigned char val = 0;
	unsigned char wmm_oui[4] = { 0x00, 0x50, 0xF2, 0x02 };

	pos = ie_data;
	if (pos == NULL)
		return val;

	end = pos + ie_len;

	while ((pos + 1) < end) {
		if ((pos + 2 + pos[1]) > end)
			break;
		if ((*pos == 221) && (memcmp(pos+2, wmm_oui, 4) == 0)) {
			pos += 8;
			val = 0x0F & *pos;
			break;
		}
		pos += 2 + pos[1];
	}
	return val;
}

static void tx(struct ieee80211_hw *hw,
		struct ieee80211_tx_control *tx_control,
		struct sk_buff *skb)
{
	struct mac80211_dev *dev = hw->priv;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	unsigned char ps_info;
	struct umac_vif *uvif;

	if (tx_info->control.vif == NULL) {
		printk(KERN_DEBUG "%s: Dropping injected TX frame\n", dev->name);
		dev_kfree_skb_any(skb);
		return;
	}

	uvif = (struct umac_vif *)(tx_info->control.vif->drv_priv);

	if (wifi->params.production_test) {
		if (((hdr->frame_control & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA) || (tx_info->control.vif == NULL)) {
			tx_info->flags |= IEEE80211_TX_STAT_ACK;
			tx_info->status.rates[0].count = 1;
			ieee80211_tx_status(hw, skb);
			return;
		}
	}

	if ((dev->power_save == PWRSAVE_STATE_DOZE) &&
			((hdr->frame_control & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA))
		hdr->frame_control |= IEEE80211_FCTL_PM;

	if ((hdr->frame_control & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) {
		if ((hdr->frame_control & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ) {
			ps_info = get_ps_info((unsigned char *)(skb->data + 28), (skb->len - 28));
			/* program the power save information */
			uccp310wlan_prog_vif_powersave_mode(uvif->vif_index, uvif->vif->addr, ps_info);
		}
		if ((hdr->frame_control & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_REASSOC_REQ) {
			ps_info = get_ps_info((unsigned char *)(skb->data + 34), (skb->len - 34));
			/* program the power save information */
			uccp310wlan_prog_vif_powersave_mode(uvif->vif_index, uvif->vif->addr, ps_info);
		}
	}

	if (uvif->noa_active) {
		uccp310wlan_noa_event(CMD_TX, (void *)uvif->vif_index, dev, skb);
		return;
	}

	uccp310wlan_tx_frame(skb, dev, false);
}

static int start(struct ieee80211_hw *hw)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;
	_80211IF_DEBUG("%s-80211IF: In start\n", dev->name);

	mutex_lock(&dev->mutex);
	if ((uccp310wlan_core_init(dev)) < 0) {
		_80211IF_DEBUG("%s-80211IF: umac init failed\n", dev->name);
		mutex_unlock(&dev->mutex);
		return -ENODEV;
	}

	dev->state = STARTED;
	mutex_unlock(&dev->mutex);

	return 0;
}

static void stop(struct ieee80211_hw *hw)
{
	struct mac80211_dev    *dev = (struct mac80211_dev *)hw->priv;

	_80211IF_DEBUG("%s-80211IF:In stop\n", dev->name);

	mutex_lock(&dev->mutex);
	uccp310wlan_core_deinit(dev);
	dev->state = STOPPED;
	mutex_unlock(&dev->mutex);
}

static int add_interface(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif)
{
	struct mac80211_dev    *dev = hw->priv;
	struct ieee80211_vif *v;
	struct umac_vif   *uvif;
	int vif_index, iftype;

	iftype = vif->type;
	v = vif;

	if (!(iftype == NL80211_IFTYPE_STATION ||
				iftype == NL80211_IFTYPE_ADHOC ||
				iftype == NL80211_IFTYPE_AP)) {
		printk(KERN_ERR "Invalid Interfacetype\n");
		return -ENOTSUPP;
	}

	mutex_lock(&dev->mutex);

	if (wifi->params.production_test) {
		if (dev->active_vifs || iftype != NL80211_IFTYPE_ADHOC) {
			mutex_unlock(&dev->mutex);
			return -EBUSY;
		}
	}
	for (vif_index = 0; vif_index < wifi->params.num_vifs; vif_index++)
		if (dev->if_mac_addresses[vif_index].addr[5] == vif->addr[5])
			break;
	uvif = (struct umac_vif *)&v->drv_priv;
	uvif->vif_index = vif_index;
	uvif->vif = v;
	uvif->dev = dev;
	uccp310wlan_vif_add(uvif);
	dev->active_vifs |= (1 << vif_index);

	rcu_assign_pointer(dev->vifs[vif_index], v);
	synchronize_rcu();

	mutex_unlock(&dev->mutex);

	return 0;
}

static void remove_interface(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif)
{
	struct mac80211_dev    *dev = hw->priv;
	struct ieee80211_vif *v;
	int vif_index;

	v = vif;
	vif_index = ((struct umac_vif *)&v->drv_priv)->vif_index;

	mutex_lock(&dev->mutex);

	uccp310wlan_vif_remove((struct umac_vif *)&v->drv_priv);
	dev->active_vifs &= ~(1 << vif_index);
	rcu_assign_pointer(dev->vifs[vif_index], NULL);
	synchronize_rcu();

	mutex_unlock(&dev->mutex);

}


static int config(struct ieee80211_hw *hw,
		unsigned int changed)
{
	struct mac80211_dev    *dev = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	unsigned int chnl;

	_80211IF_DEBUG("%s-80211IF:In config\n", dev->name);

	mutex_lock(&dev->mutex);

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		dev->txpower = conf->power_level;
		uccp310wlan_prog_txpower(dev->txpower);
	}

	/*Check for change in Channel*/
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		chnl = ieee80211_frequency_to_channel(conf->chandef.chan->center_freq);
		_80211IF_DEBUG("%s-80211IF:Set Channel to %d\n", dev->name, chnl);
		uccp310wlan_prog_channel(chnl);
	}

	/*Check for change in Power save state*/
	if (changed & IEEE80211_CONF_CHANGE_PS) {
		int i;
		for (i = 0; i < MAX_VIFS; i++)
			if (dev->active_vifs & (1 << i))
				break;

		if (conf->flags & IEEE80211_CONF_PS)
			dev->power_save = PWRSAVE_STATE_DOZE;
		else
			dev->power_save = PWRSAVE_STATE_AWAKE;
		_80211IF_DEBUG("%s-80211IF:Power save state of VIF %d changed to %d\n", dev->name, i, dev->power_save);
		uccp310wlan_prog_powersave_state(i, dev->if_mac_addresses[i].addr, dev->power_save);
	}


	/*Check for change in Listen Interval*/
	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL) {
		/* TODO */
	;
	}

	/*Check for change in Retry Limits*/
	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		int i;
		_80211IF_DEBUG("%s-80211IF:Retry Limits changed to %d and %d\n", dev->name, conf->short_frame_max_tx_count, conf->long_frame_max_tx_count);
		for (i = 0; i < MAX_VIFS; i++) {
			if (dev->active_vifs & (1 << i)) {
				uccp310wlan_prog_vif_short_retry(i, dev->if_mac_addresses[i].addr, conf->short_frame_max_tx_count);
				uccp310wlan_prog_vif_long_retry(i, dev->if_mac_addresses[i].addr, conf->long_frame_max_tx_count);
			}
		}
	}

	/* Production test hack */
	if (wifi->params.production_test) {
		struct ieee80211_sub_if_data *sdata;
		if (dev->vifs[0]) {
			sdata = vif_to_sdata(dev->vifs[0]);
			sdata->u.ibss.fixed_channel = 1;
			sdata->u.ibss.last_scan_completed = jiffies + HZ;
			sdata->u.ibss.ibss_join_req = jiffies - (10*HZ);
		}
	}

	mutex_unlock(&dev->mutex);
	return 0;
}

static u64 prepare_multicast(struct ieee80211_hw *hw,
		struct netdev_hw_addr_list *mc_list)
{
	struct mac80211_dev      *dev = hw->priv;
	int                  i;
	struct               netdev_hw_addr *ha;
	int                  mc_count = 0;

	if (dev->state != STARTED)
		return 0;

	netdev_hw_addr_list_for_each(ha, mc_list) {
		if (++mc_count > MAX_MCAST_FILTERS) {
			mc_count = 0;
			_80211IF_DEBUG("%s-80211IF: Multicast filter count : %d\n", dev->name, mc_count);
			goto out;
		}
	}
	_80211IF_DEBUG("%s-80211IF: Multicast filter count : %d\n", dev->name, mc_count);

	if (dev->mc_filter_count > 0) {
		/* Remove all previous multicast addresses from the LMAC */
		for (i = 0; i < dev->mc_filter_count; i++)
			uccp310wlan_prog_mcast_addr_cfg(dev->mc_filters[i], 1);
	}

	i = 0;
	netdev_hw_addr_list_for_each(ha, mc_list)
	{
		/* Prog the multicast address into the LMAC */
		uccp310wlan_prog_mcast_addr_cfg(ha->addr, 0);
		memcpy(dev->mc_filters[i], ha->addr, 6);
		i++;
	}
	dev->mc_filter_count = mc_count;
out:
	return mc_count;
}

static void configure_filter(struct           ieee80211_hw *hw,
		unsigned int     changed_flags,
		unsigned int     *new_flags,
		u64              mc_count)
{
	struct mac80211_dev *dev = hw->priv;
	mutex_lock(&dev->mutex);

	changed_flags &= SUPPORTED_FILTERS;
	*new_flags &= SUPPORTED_FILTERS;

	if (dev->state != STARTED) {
		mutex_unlock(&dev->mutex);
		return;
	}

	if ((*new_flags & FIF_ALLMULTI) || (mc_count == 0)) {
		/* Disable the multicast filter in LMAC */
		_80211IF_DEBUG("%s-80211IF: Multicast filters disabled\n", dev->name);
		uccp310wlan_prog_mcast_filter_control(0);
	} else if (mc_count) {
		/* Enable the multicast filter in LMAC */
		_80211IF_DEBUG("%s-80211IF: Multicast filters enabled\n", dev->name);
		uccp310wlan_prog_mcast_filter_control(1);
	}

	if (changed_flags == 0)
		/* no filters which we support changed */
		goto out;

	if (wifi->params.production_test == 0) {
		if (*new_flags & FIF_BCN_PRBRESP_PROMISC) {
			/* receive all beacons and probe responses */
			_80211IF_DEBUG("%s-80211IF: RCV ALL bcns\n", dev->name);
			uccp310wlan_prog_rcv_bcn_mode(RCV_ALL_BCNS);
		} else {
			/* receive only network beacons and probe responses */
			_80211IF_DEBUG("%s-80211IF: RCV NW bcns\n", dev->name);
			uccp310wlan_prog_rcv_bcn_mode(RCV_NETWORK_BCNS);
		}
	}
out:
	if (wifi->params.production_test == 1) {
		_80211IF_DEBUG("%s-80211IF: RCV ALL bcns\n", dev->name);
		uccp310wlan_prog_rcv_bcn_mode(RCV_ALL_BCNS);
	}
	mutex_unlock(&dev->mutex);
	return;
}

static int conf_vif_tx(struct ieee80211_hw  *hw,
		struct ieee80211_vif    *vif,
		unsigned short  queue, const struct  ieee80211_tx_queue_params *params)
{
	struct mac80211_dev *dev = hw->priv;
	int vif_index, vif_active;

	for (vif_index = 0; vif_index < wifi->params.num_vifs; vif_index++)
		if (dev->if_mac_addresses[vif_index].addr[5] == vif->addr[5])
			break;

	vif_active = 0;
	if ((dev->active_vifs & (1 << vif_index)))
		vif_active = 1;
	mutex_lock(&dev->mutex);
	uccp310wlan_vif_set_edca_params(queue, (struct umac_vif *)&vif->drv_priv, params, vif_active);
	mutex_unlock(&dev->mutex);
	return 0;
}

static int set_key(struct ieee80211_hw *hw,
		enum set_key_cmd cmd,
		struct ieee80211_vif *vif,
		struct ieee80211_sta *sta,
		struct ieee80211_key_conf *key_conf)
{

	struct umac_key sec_key;
	unsigned int result = 0;
	struct mac80211_dev  *dev = hw->priv;
	unsigned int cipher_type, key_type;
	int vif_index;
	struct umac_vif *uvif;
	uvif = ((struct umac_vif *)&vif->drv_priv);

	memset(&sec_key, 0, sizeof(struct umac_key));
	switch (key_conf->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		sec_key.key = key_conf->key;
		cipher_type = CIPHER_TYPE_WEP40 ;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		sec_key.key = key_conf->key;
		cipher_type = CIPHER_TYPE_WEP104 ;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key_conf->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		/*
		 * We get the key in the following form:
		 * KEY (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 */
		sec_key.key = key_conf->key;
		sec_key.tx_mic = key_conf->key + 16;
		sec_key.rx_mic = key_conf->key + 24;
		cipher_type = CIPHER_TYPE_TKIP ;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		sec_key.key = key_conf->key;
		cipher_type = CIPHER_TYPE_CCMP;
		break;
	default:
		result = -EOPNOTSUPP;
		mutex_unlock(&dev->mutex);
		goto out;
	}

	vif_index = ((struct umac_vif *)&vif->drv_priv)->vif_index;
	mutex_lock(&dev->mutex);
	if (cmd == SET_KEY) {
		key_conf->hw_key_idx = 0; /* Don't really use this */

		/* This flag indicate that it requires IV generation */
		key_conf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;


		if (cipher_type == CIPHER_TYPE_WEP40 || cipher_type == CIPHER_TYPE_WEP104) {
			_80211IF_DEBUG("%s-80211IF: ADD IF KEY. vif_index = %d, keyidx = %d, cipher_type = %d\n", dev->name, vif_index, key_conf->keyidx, cipher_type);
			uccp310wlan_prog_if_key(vif_index, vif->addr, KEY_CTRL_ADD, key_conf->keyidx, cipher_type, &sec_key);
		} else {
			if (sta) {
				sec_key.peer_mac = sta->addr;
				if (key_conf->flags & IEEE80211_KEY_FLAG_PAIRWISE)
					key_type = KEY_TYPE_UCAST;
				else
					key_type = KEY_TYPE_BCAST;
				_80211IF_DEBUG("%s-80211IF: ADD PEER KEY. vif_index = %d, keyidx = %d, keytype = %d, cipher_type = %d\n", dev->name, vif_index, key_conf->keyidx, key_type, cipher_type);
				uccp310wlan_prog_peer_key(vif_index, vif->addr, KEY_CTRL_ADD, key_conf->keyidx, key_type, cipher_type, &sec_key);
			} else {
				key_type = KEY_TYPE_BCAST;
				if (vif->type == NL80211_IFTYPE_STATION) {
					sec_key.peer_mac = (vif_to_sdata(vif))->u.mgd.bssid;
					memcpy(uvif->bssid, (vif_to_sdata(vif)->u.mgd.bssid), ETH_ALEN);
					_80211IF_DEBUG("%s-80211IF: ADD PEER KEY. vif_index = %d, keyidx = %d, keytype = %d, cipher_type = %d\n", dev->name, vif_index, key_conf->keyidx, key_type, cipher_type);
					uccp310wlan_prog_peer_key(vif_index, vif->addr, KEY_CTRL_ADD, key_conf->keyidx, key_type, cipher_type, &sec_key);
				} else if (vif->type == NL80211_IFTYPE_AP) {
					_80211IF_DEBUG("%s-80211IF: ADD IF KEY. vif_index = %d, keyidx = %d, cipher_type = %d\n", dev->name, vif_index, key_conf->keyidx, cipher_type);
					uccp310wlan_prog_if_key(vif_index, vif->addr, KEY_CTRL_ADD, key_conf->keyidx, cipher_type, &sec_key);
				} else {
					_80211IF_DEBUG("%s-80211IF: ADD IF KEY. vif_index = %d, keyidx = %d, cipher_type = %d\n", dev->name, vif_index, key_conf->keyidx, cipher_type);
					uccp310wlan_prog_if_key(vif_index, vif->addr, KEY_CTRL_ADD, key_conf->keyidx, cipher_type, &sec_key);
				}
			}
		}
	} else if (cmd == DISABLE_KEY) {
		if ((cipher_type == CIPHER_TYPE_WEP40) || (cipher_type == CIPHER_TYPE_WEP104)) {
			uccp310wlan_prog_if_key(vif_index, vif->addr, KEY_CTRL_DEL, key_conf->keyidx, cipher_type, &sec_key);
		} else if (sta) {
			sec_key.peer_mac = sta->addr;
			if (key_conf->flags & IEEE80211_KEY_FLAG_PAIRWISE)
				key_type = KEY_TYPE_UCAST;
			else
				key_type = KEY_TYPE_BCAST;
			uccp310wlan_prog_peer_key(vif_index, vif->addr, KEY_CTRL_DEL, key_conf->keyidx, key_type, cipher_type, &sec_key);
		} else {
			if (vif->type == NL80211_IFTYPE_STATION) {
				sec_key.peer_mac = uvif->bssid;
				uccp310wlan_prog_peer_key(vif_index, vif->addr, KEY_CTRL_DEL, key_conf->keyidx, KEY_TYPE_BCAST, cipher_type, &sec_key);
			} else if (vif->type == NL80211_IFTYPE_AP) {
				uccp310wlan_prog_if_key(vif_index, vif->addr, KEY_CTRL_DEL, key_conf->keyidx, cipher_type, &sec_key);
			} else {
				uccp310wlan_prog_if_key(vif_index, vif->addr, KEY_CTRL_DEL, key_conf->keyidx, cipher_type, &sec_key);
			}
		}
	}
	mutex_unlock(&dev->mutex);

out:
	return result;
}


static int get_stats(struct ieee80211_hw *hw,
		struct ieee80211_low_level_stats *stats)
{

	/*TODO */
	return 0;
}

static void bss_info_changed(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif,
		struct ieee80211_bss_conf *bss_conf,
		unsigned int changed)
{
	struct mac80211_dev   *dev = hw->priv;

	mutex_lock(&dev->mutex);
	if (wifi->params.production_test) {
		/* Prevent IBSS merge scan in production test mode */
		struct ieee80211_sub_if_data *sdata;
		sdata = vif_to_sdata(vif);
		sdata->u.ibss.fixed_channel = 1;
		mutex_unlock(&dev->mutex);
		return;
	}

	uccp310wlan_vif_bss_info_changed((struct umac_vif *)&vif->drv_priv, bss_conf, changed);
	mutex_unlock(&dev->mutex);
	return;
}

static void sw_scan_start(struct ieee80211_hw *hw)
{
	_80211IF_DEBUG("%s-80211IF: scan started\n", ((struct mac80211_dev *)(hw->priv))->name);

	uccp310wlan_prog_scan_ind(1);

}

static void sw_scan_complete(struct ieee80211_hw *hw)
{
	struct mac80211_dev   *dev = hw->priv;
	struct ieee80211_vif  *vif;
	int vif_index;

	_80211IF_DEBUG("%s-80211IF: scan stopped\n", ((struct mac80211_dev *)(hw->priv))->name);
	if (dev->active_vifs  == 1) {
		for (vif_index = 0; vif_index < wifi->params.num_vifs; vif_index++) {
			vif = (struct ieee80211_vif *)rcu_dereference(dev->vifs[vif_index]);
			if (vif && (vif->type == NL80211_IFTYPE_STATION) && (vif->bss_conf.assoc))
				uccp310wlan_prog_scan_ind(0);
		}
	}
}

static void init_hw(struct ieee80211_hw    *hw)
{
	struct mac80211_dev  *dev = (struct mac80211_dev *)hw->priv;
	/* Supported Interface Types and other Default values*/
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_ADHOC) | BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_P2P_CLIENT) | BIT(NL80211_IFTYPE_P2P_GO);
	if (wifi->params.num_vifs > 1) {
		hw->wiphy->iface_combinations = if_comb;
		hw->wiphy->n_iface_combinations = sizeof(if_comb)/sizeof(struct ieee80211_iface_combination);
	}

	hw->flags = IEEE80211_HW_SIGNAL_DBM  |
		IEEE80211_HW_SUPPORTS_PS ; /* umac */
	hw->flags |= IEEE80211_HW_SUPPORTS_UAPSD;
	hw->flags |= IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;
	hw->flags |= IEEE80211_HW_SUPPORTS_PER_STA_GTK;
	hw->flags |= IEEE80211_HW_CONNECTION_MONITOR;

	hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->max_listen_interval = 10; /* umac */
	hw->max_rates = 4; /* umac */
	hw->max_rate_tries = 5; /* umac */
	hw->channel_change_time = 5000; /* umac */
	hw->queues = 4; /* umac */

	/*size */
	hw->extra_tx_headroom = 0; /* umac */
	hw->vif_data_size = sizeof(struct umac_vif);
	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &band_2ghz;
	if (wifi->params.dot11a_support)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &band_5ghz;

	hw->rate_control_algorithm = NULL;
	memset(hw->wiphy->addr_mask, 0, sizeof(hw->wiphy->addr_mask));
	if (wifi->params.num_vifs == 1) {
		hw->wiphy->addresses = NULL;
		SET_IEEE80211_PERM_ADDR(hw, dev->if_mac_addresses[0].addr);
	} else {
		hw->wiphy->n_addresses = wifi->params.num_vifs;
		hw->wiphy->addresses = dev->if_mac_addresses;
	}
	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;

}
static struct ieee80211_ops ops = {
	.tx                 = tx,
	.start              = start,
	.stop               = stop,
	.add_interface      = add_interface,
	.remove_interface   = remove_interface,
	.config             = config,
	.prepare_multicast  = prepare_multicast,
	.configure_filter   = configure_filter,
	.sw_scan_start      = sw_scan_start,
	.sw_scan_complete   = sw_scan_complete,
	.get_stats          = get_stats,
	.sta_notify         = NULL,
	.conf_tx            = conf_vif_tx,
	.bss_info_changed   = bss_info_changed,
	.set_tim            = NULL,
	.set_key            = set_key,
	.hw_scan            = NULL,
	.get_tkip_seq       = NULL,
	.set_rts_threshold  = NULL,
	.tx_last_beacon     = NULL,
	.ampdu_action       = NULL,
};

static void uccp310wlan_exit(void)
{
	ieee80211_unregister_hw(wifi->hw);
	ieee80211_free_hw(wifi->hw);
	wifi->hw = NULL;
}

static int uccp310wlan_init(void)
{
	struct ieee80211_hw *hw;
	int error;
	struct mac80211_dev  *dev;
	int i;
	unsigned char addr[ETH_ALEN];

	/*Allocate new hardware device*/
	hw = ieee80211_alloc_hw(sizeof(struct mac80211_dev), &ops);
	if (hw == NULL) {
		printk(KERN_ERR "Failed to allocate memory for ieee80211_hw\n");
		error = -ENOMEM;
		goto out;
	}
	dev = (struct mac80211_dev *)hw->priv;

	/*TODO : Set dev->dev */

	conv_str_to_byte(addr, mac_addr, ETH_ALEN);
	printk(KERN_INFO "MAC ADDR: %pM\n", addr);
	SET_IEEE80211_DEV(hw, dev->dev);

	mutex_init(&dev->mutex);
	spin_lock_init(&dev->bcast_lock);
	dev->state = STOPPED;
	dev->active_vifs = 0;
	dev->txpower = DEFAULT_TX_POWER;
	strcpy(dev->name, "UCCP310WIFI");
	/* TODO : dev->wlan.default_keyid = -1;*/

	for (i = 0; i < wifi->params.num_vifs; i++) {
		memcpy(dev->if_mac_addresses[i].addr, addr, ETH_ALEN);
		addr[5]++;
	}
	/* Initialize HW parameters */
	init_hw(hw);

	/*Register hardware*/
	error = ieee80211_register_hw(hw);

	/* Production test hack: Set all channel flags to 0 to allow IBSS creation
	   in all channels */
	if (wifi->params.production_test && !error) {
		enum ieee80211_band band;
		struct ieee80211_supported_band *sband;
		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			sband = hw->wiphy->bands[band];
			if (sband)
				for (i = 0; i < sband->n_channels; i++)
					sband->channels[i].flags = 0;
		}
	}
	if (!error) {
		wifi->hw = hw;
		dev->hw = hw;
		dev->params = &wifi->params;
		dev->stats = &wifi->stats;
	} else
		ieee80211_free_hw(hw);
out:
	return error;

}

static ssize_t proc_read(struct seq_file *m, void *v)
{

	seq_printf(m, "************* PARAMS ***********\n");
	seq_printf(m, "dot11a_support = %d\n", wifi->params.dot11a_support);
	seq_printf(m, "sensitivity = %d\n", wifi->params.ed_sensitivity);
	seq_printf(m, "auto_sensitivity = %d\n", wifi->params.auto_sensitivity);
	seq_printf(m, "rf_params = "RFPARAMSTR"\n", RFPARAM2STR(wifi->params.rf_params));
	seq_printf(m, "production_test = %d\n", wifi->params.production_test);
	seq_printf(m, "show_phy_stats = %d\n", wifi->params.show_phy_stats);
	seq_printf(m, "num_vifs = %d\n", wifi->params.num_vifs);
	seq_printf(m, "max_bcn_loss = %d\n", wifi->params.max_bcn_loss);
	seq_printf(m, "************* STATS ***********\n");
	seq_printf(m, "rx_packet_count = %d\n", wifi->stats.rx_packet_count);
	if (wifi->params.show_phy_stats) {
		seq_printf(m, "ofdm_rx_crc_success_cnt = %d\n", wifi->stats.ofdm_rx_crc_success_cnt);
		seq_printf(m, "ofdm_rx_crc_fail_cnt = %d\n", wifi->stats.ofdm_rx_crc_fail_cnt);
		seq_printf(m, "ofdm_rx_false_trig_cnt = %d\n", wifi->stats.ofdm_rx_false_trig_cnt);
		seq_printf(m, "ofdm_rx_header_fail_cnt = %d\n", wifi->stats.ofdm_rx_header_fail_cnt);
		seq_printf(m, "dsss_rx_crc_success_cnt = %d\n", wifi->stats.dsss_rx_crc_success_cnt);
		seq_printf(m, "dsss_rx_crc_fail_cnt = %d\n", wifi->stats.dsss_rx_crc_fail_cnt);
		seq_printf(m, "dsss_rx_false_trig_cnt = %d\n", wifi->stats.dsss_rx_false_trig_cnt);
		seq_printf(m, "dsss_rx_header_fail_cnt = %d\n", wifi->stats.dsss_rx_header_fail_cnt);
		seq_printf(m, "ed_cnt = %d\n", wifi->stats.ed_cnt);
		seq_printf(m, "cca_fail_cnt = %d\n", wifi->stats.cca_fail_cnt);
		seq_printf(m, "pdout_val = %d\n", wifi->stats.pdout_val);
	}
	seq_printf(m, "current sensitivity = %d\n", wifi->stats.current_sensitivity);
	seq_printf(m, "************* VERSION ***********\n");
	seq_printf(m, "UMAC_VERSION = %s\n", UMAC_VERSION);

	if (wifi->hw && (((struct mac80211_dev *)(wifi->hw->priv))->state != STARTED)) {
		seq_printf(m, "LMAC_VERSION = %s\n", "UNKNOWN");
		seq_printf(m, "Firmware version = %s\n", "UNKNOWN");
	} else {
		seq_printf(m, "LMAC_VERSION = %s\n", wifi->stats.uccp310_lmac_version);
		seq_printf(m, "Firmware version= %d.%d\n", (wifi->stats.uccp310_lmac_version[0] - '0'), (wifi->stats.uccp310_lmac_version[2] - '0'));
	}
#ifdef CONFIG_TIMING_DEBUG
	{
		unsigned long temp[20];
		unsigned long i, j, prev, curr;
		seq_printf(m, "************* TIMING DEBUG ***********\n");
		spin_lock_irqsave(&timing_lock, j);
		i = irq_ts_index;
		memcpy(temp, irq_timestamp, 20 * sizeof(unsigned long));
		spin_unlock_irqrestore(&timing_lock, j);
		if (i == 0)
			i = 19;
		else
			i = i - 1;
		for (j = 0; j < 20; j++) {
			if (j != 0) {
				curr = temp[i];
				if (i == 0)
					prev = temp[19];
				else
					prev = temp[i-1];

				if (curr > prev)
					seq_printf(m, "%d ", (unsigned int)(curr - prev));
				else
					seq_printf(m, "%d ", (unsigned int)((0xFFFFFFFF - prev) + curr));
			}

			if (i == 0)
				i = 19;
			else
				i = i - 1;
		}
		seq_printf(m, "\n");
	}
#endif
	seq_printf(m, "TS1 = %llu\n", (unsigned long long)get_unaligned_le64(wifi->params.ts1));
	seq_printf(m, "TS2 = %lu\n", (unsigned long)get_unaligned_le32(wifi->params.ts2));
	seq_printf(m, "BSSID = %pM\n", (wifi->params.bssid));

	return 0;
}

static ssize_t proc_write(struct file *file,
			  const char __user *buffer,
			  size_t count,
			  loff_t *ppos)
{
	char buf[100];
	int ret;
	unsigned long val;
	long sval;

	if (count > sizeof(buf))
		count = sizeof(buf)-1;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';
	if (!strncmp(buf, "dot11a_support=", 15)) {
		ret = kstrtoul(buf+15, 0, &val);
		if (((val == 0) || (val == 1)) && (wifi->params.dot11a_support != val)) {
			wifi->params.dot11a_support = val;
			if (wifi->hw) {
				uccp310wlan_exit();
				wifi->hw = NULL;
			}
			printk(KERN_ERR "Re-intializing UMAC ..\n");
			uccp310wlan_init();
		} else
			printk(KERN_ERR "Invalid parameter value.\n");
	} else if (!strncmp(buf, "sensitivity=", 12)) {
		ret = kstrtol(buf+12, 0, &sval);
		if (sval > -51 || sval < -96 || (sval % 3 != 0))
			printk(KERN_ERR "Invalid parameter value.\n");
		else
			wifi->params.ed_sensitivity = sval;
	} else if (!strncmp(buf, "dyn_ed_ceiling=", 15)) {
		ret = kstrtol(buf+15, 0, &sval);
		if (sval > -51 || sval < -96 || (sval % 3 != 0))
			printk(KERN_ERR "Invalid parameter value.\n");
		else
			wifi->params.dyn_ed_ceiling = sval;
	} else if (!strncmp(buf, "auto_sensitivity=", 17)) {
		ret = kstrtoul(buf+17, 0, &val);
		if ((val == 0) || (val == 1))
			wifi->params.auto_sensitivity = val;
		else
			printk(KERN_ERR "Invalid parameter value.\n");
	} else if (!strncmp(buf, "production_test=", 16)) {
		ret = kstrtoul(buf+16, 0, &val);
		if ((val == 0) || (val == 1)) {
			if (wifi->params.production_test != val) {
				if (wifi->params.production_test) {
					wifi->params.show_phy_stats = 1;
					wifi->params.num_vifs = 1;
				}
				wifi->params.production_test = val;
				if (wifi->hw) {
					uccp310wlan_exit();
					wifi->hw = NULL;
				}
				printk(KERN_ERR "Re-intializing UMAC ..\n");
				uccp310wlan_init();
			}
		} else
			printk(KERN_ERR "Invalid parameter value.\n");
	} else if (!strncmp(buf, "num_vifs=", 9)) {
		ret = kstrtoul(buf+9, 0, &val);
		if (val > 0 && val <= MAX_VIFS) {
			if (wifi->params.num_vifs != val) {
				if (wifi->hw) {
					uccp310wlan_exit();
					wifi->hw = NULL;
				}
				printk(KERN_ERR "Re-intializing UMAC ..\n");
				wifi->params.num_vifs = val;
				uccp310wlan_init();
			}
		}
	} else if (!strncmp(buf, "show_phy_stats=", 15)) {
		ret = kstrtoul(buf+15, 0, &val);
		if ((val == 0) || (val == 1))
			wifi->params.show_phy_stats = val;
		else
			printk(KERN_ERR "Invalid parameter value.\n");
	} else if (!strncmp(buf, "rf_params=", 10)) {
		conv_str_to_byte(wifi->params.rf_params, buf+10, 8);
	} else if (!strncmp(buf, "rx_packet_count=", 16)) {
		ret = kstrtoul(buf+16, 0, &val);
		if (val >= 0)
			wifi->stats.rx_packet_count = val;
		else
			printk(KERN_ERR "Invalid parameter value.\n");
	} else if (!strncmp(buf, "pdout_val=", 10)) {
		ret = kstrtoul(buf+10, 0, &val);
		if (val >= 0)
			wifi->stats.pdout_val = val;
		else
			printk(KERN_ERR "Invalid parameter value.\n");
	} else if (!strncmp(buf, "get_rx_stats=", 13)) {
		uccp310wlan_prog_mib_stats();
	} else if (!strncmp(buf, "reset_hal_params=", 17)) {
		ret = kstrtoul(buf+17, 0, &val);
		if (((struct mac80211_dev *)(wifi->hw->priv))->state != STARTED) {
			if (val != 1)
				printk(KERN_ERR "Invalid parameter value.\n");
			else
				reset_hal_params();
		} else {
			printk(KERN_ERR "HAL parameters reset can be done only when all interface are down\n");
		}
	} else if (!strncmp(buf, "max_bcn_loss=", 13)) {
		ret = kstrtoul(buf+13, 0, &val);
		if (val >= 5)
			wifi->params.max_bcn_loss = val;
		else
			printk(KERN_ERR "Invalid parameter value (should be >=5)");

	} else {
		printk(KERN_ERR "Invalid parameter name.\n");
	}
	return count;
}

static int proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read, NULL);
}

static const struct file_operations proc_fops = {
	.open = proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = proc_write
};

static int proc_init(void)
{
	struct proc_dir_entry *entry;
	int err = 0;

	wifi = kzalloc(sizeof(struct wifi_dev), GFP_KERNEL);
	if (!wifi) {
		err = -ENOMEM;
		goto out;
	}

	wifi->umac_proc_dir_entry = proc_mkdir("umac", NULL);
	if (!wifi->umac_proc_dir_entry) {
		printk(KERN_ERR "Failed to create proc dir\n");
		err = -ENOMEM;
		goto  proc_dir_fail;
	}

	entry = proc_create("params", 0644, wifi->umac_proc_dir_entry,
			    &proc_fops);
	if (!entry) {
		printk(KERN_ERR "Failed to create proc entry\n");
		err = -ENOMEM;
		goto  proc_entry_fail;
	}

	/* Initialize WLAN params */
	memset(&wifi->params, 0, sizeof(struct wifi_params));
	memset(wifi->params.rf_params, 0xff, sizeof(wifi->params.rf_params));
	wifi->params.ed_sensitivity = -84;
	wifi->params.dyn_ed_ceiling = -51;
	wifi->params.rf_params[0] = 0x3B;
	wifi->params.auto_sensitivity = 1;
	wifi->params.num_vifs = 1;
	wifi->params.max_bcn_loss = MAX_BEACON_LOSS_COUNT;

	return err;

proc_entry_fail:
	remove_proc_entry("umac", NULL);
proc_dir_fail:
	kfree(wifi);
out:
	return err;

}

static void proc_exit(void)
{
	remove_proc_entry("params", wifi->umac_proc_dir_entry);
	remove_proc_entry("umac", NULL);
	kfree(wifi);
}


int _uccp310wlan_80211if_init(void)
{
	int error;
	error = proc_init();
	if (error)
		return error;
	error = uccp310wlan_init();

	return error;
}

void _uccp310wlan_80211if_exit(void)
{
	if (wifi->hw)
		uccp310wlan_exit();
	proc_exit();
}
