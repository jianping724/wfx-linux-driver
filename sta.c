// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of mac80211 API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/version.h>
#include <net/mac80211.h>

#include "sta.h"
#include "wfx.h"
#include "fwio.h"
#include "bh.h"
#include "key.h"
#include "scan.h"
#include "debug.h"
#include "hif_tx.h"
#include "hif_tx_mib.h"

#define HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES 2

#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
static __always_inline void assign_bit(long nr, volatile unsigned long *addr,
				       bool value)
{
	if (value)
		set_bit(nr, addr);
	else
		clear_bit(nr, addr);
}
#endif

u32 wfx_rate_mask_to_hw(struct wfx_dev *wdev, u32 rates)
{
	int i;
	u32 ret = 0;
	// WFx only support 2GHz
	struct ieee80211_supported_band *sband = wdev->hw->wiphy->bands[NL80211_BAND_2GHZ];

	for (i = 0; i < sband->n_bitrates; i++) {
		if (rates & BIT(i)) {
			if (i >= sband->n_bitrates)
				dev_warn(wdev->dev, "unsupported basic rate\n");
			else
				ret |= BIT(sband->bitrates[i].hw_value);
		}
	}
	return ret;
}

static void __wfx_free_event_queue(struct list_head *list)
{
	struct wfx_hif_event *event, *tmp;

	list_for_each_entry_safe(event, tmp, list, link) {
		list_del(&event->link);
		kfree(event);
	}
}

static void wfx_free_event_queue(struct wfx_vif *wvif)
{
	LIST_HEAD(list);

	spin_lock(&wvif->event_queue_lock);
	list_splice_init(&wvif->event_queue, &list);
	spin_unlock(&wvif->event_queue_lock);

	__wfx_free_event_queue(&list);
}

void wfx_cqm_bssloss_sm(struct wfx_vif *wvif, int init, int good, int bad)
{
	int tx = 0;

	mutex_lock(&wvif->bss_loss_lock);
	cancel_work_sync(&wvif->bss_params_work);

	if (init) {
		schedule_delayed_work(&wvif->bss_loss_work, HZ);
		wvif->bss_loss_state = 0;

		if (!atomic_read(&wvif->wdev->tx_lock))
			tx = 1;
	} else if (good) {
		cancel_delayed_work_sync(&wvif->bss_loss_work);
		wvif->bss_loss_state = 0;
		schedule_work(&wvif->bss_params_work);
	} else if (bad) {
		/* FIXME Should we just keep going until we time out? */
		if (wvif->bss_loss_state < 3)
			tx = 1;
	} else {
		cancel_delayed_work_sync(&wvif->bss_loss_work);
		wvif->bss_loss_state = 0;
	}

	/* Spit out a NULL packet to our AP if necessary */
	// FIXME: call ieee80211_beacon_loss/ieee80211_connection_loss instead
	if (tx) {
		struct sk_buff *skb;

		wvif->bss_loss_state++;

#if (KERNEL_VERSION(4, 14, 16) > LINUX_VERSION_CODE)
		skb = ieee80211_nullfunc_get(wvif->wdev->hw, wvif->vif);
#else
		skb = ieee80211_nullfunc_get(wvif->wdev->hw, wvif->vif, false);
#endif
		if (!skb)
			goto end;
		memset(IEEE80211_SKB_CB(skb), 0,
		       sizeof(*IEEE80211_SKB_CB(skb)));
		IEEE80211_SKB_CB(skb)->control.vif = wvif->vif;
		IEEE80211_SKB_CB(skb)->driver_rates[0].idx = 0;
		IEEE80211_SKB_CB(skb)->driver_rates[0].count = 1;
		IEEE80211_SKB_CB(skb)->driver_rates[1].idx = -1;
		wfx_tx(wvif->wdev->hw, NULL, skb);
	}
end:
	mutex_unlock(&wvif->bss_loss_lock);
}

int wfx_fwd_probe_req(struct wfx_vif *wvif, bool enable)
{
	wvif->fwd_probe_req = enable;
	return hif_set_rx_filter(wvif, wvif->filter_bssid,
				 wvif->fwd_probe_req);
}

static int wfx_set_mcast_filter(struct wfx_vif *wvif,
				    struct wfx_grp_addr_table *fp)
{
	int i, ret;
	struct hif_mib_config_data_filter config = { };
	struct hif_mib_set_data_filtering filter_data = { };
	struct hif_mib_mac_addr_data_frame_condition filter_addr_val = { };
	struct hif_mib_uc_mc_bc_data_frame_condition filter_addr_type = { };

	// Temporary workaround for filters
	return hif_set_data_filtering(wvif, &filter_data);

	if (!fp->enable) {
		filter_data.enable = 0;
		return hif_set_data_filtering(wvif, &filter_data);
	}

	// A1 Address match on list
	for (i = 0; i < fp->num_addresses; i++) {
		filter_addr_val.condition_idx = i;
		filter_addr_val.address_type = HIF_MAC_ADDR_A1;
		ether_addr_copy(filter_addr_val.mac_address,
				fp->address_list[i]);
		ret = hif_set_mac_addr_condition(wvif,
						 &filter_addr_val);
		if (ret)
			return ret;
		config.mac_cond |= 1 << i;
	}

	// Accept unicast and broadcast
	filter_addr_type.condition_idx = 0;
	filter_addr_type.param.bits.type_unicast = 1;
	filter_addr_type.param.bits.type_broadcast = 1;
	ret = hif_set_uc_mc_bc_condition(wvif, &filter_addr_type);
	if (ret)
		return ret;

	config.uc_mc_bc_cond = 1;
	config.filter_idx = 0; // TODO #define MULTICAST_FILTERING 0
	config.enable = 1;
	ret = hif_set_config_data_filter(wvif, &config);
	if (ret)
		return ret;

	// discard all data frames except match filter
	filter_data.enable = 1;
	filter_data.default_filter = 1; // discard all
	ret = hif_set_data_filtering(wvif, &filter_data);

	return ret;
}

void wfx_update_filtering(struct wfx_vif *wvif)
{
	int ret;
	bool is_sta = wvif->vif && NL80211_IFTYPE_STATION == wvif->vif->type;
	bool filter_bssid = wvif->filter_bssid;
	bool fwd_probe_req = wvif->fwd_probe_req;
	struct hif_mib_bcn_filter_enable bf_ctrl;
	struct hif_ie_table_entry filter_ies[] = {
		{
			.ie_id        = WLAN_EID_VENDOR_SPECIFIC,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
			.oui          = { 0x50, 0x6F, 0x9A },
		}, {
			.ie_id        = WLAN_EID_HT_OPERATION,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
		}, {
			.ie_id        = WLAN_EID_ERP_INFO,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
		}
	};
	int n_filter_ies;

	if (wvif->state == WFX_STATE_PASSIVE)
		return;

	if (wvif->disable_beacon_filter) {
		bf_ctrl.enable = 0;
		bf_ctrl.bcn_count = 1;
		n_filter_ies = 0;
	} else if (!is_sta) {
		bf_ctrl.enable = HIF_BEACON_FILTER_ENABLE |
				 HIF_BEACON_FILTER_AUTO_ERP;
		bf_ctrl.bcn_count = 0;
		n_filter_ies = 2;
	} else {
		bf_ctrl.enable = HIF_BEACON_FILTER_ENABLE;
		bf_ctrl.bcn_count = 0;
		n_filter_ies = 3;
	}

	ret = hif_set_rx_filter(wvif, filter_bssid, fwd_probe_req);
	if (!ret)
		ret = hif_set_beacon_filter_table(wvif, n_filter_ies,
						  filter_ies);
	if (!ret)
		ret = hif_beacon_filter_control(wvif, bf_ctrl.enable,
						bf_ctrl.bcn_count);
	if (!ret)
		ret = wfx_set_mcast_filter(wvif, &wvif->mcast_filter);
	if (ret)
		dev_err(wvif->wdev->dev, "update filtering failed: %d\n", ret);
}

static void wfx_update_filtering_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    update_filtering_work);

	wfx_update_filtering(wvif);
}

u64 wfx_prepare_multicast(struct ieee80211_hw *hw,
			  struct netdev_hw_addr_list *mc_list)
{
	int i;
	struct netdev_hw_addr *ha;
	struct wfx_vif *wvif = NULL;
	struct wfx_dev *wdev = hw->priv;
	int count = netdev_hw_addr_list_count(mc_list);

	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		memset(&wvif->mcast_filter, 0x00, sizeof(wvif->mcast_filter));
		if (!count ||
		    count > ARRAY_SIZE(wvif->mcast_filter.address_list))
			continue;

		i = 0;
		netdev_hw_addr_list_for_each(ha, mc_list) {
			ether_addr_copy(wvif->mcast_filter.address_list[i],
					ha->addr);
			i++;
		}
		wvif->mcast_filter.enable = true;
		wvif->mcast_filter.num_addresses = count;
	}

	return 0;
}

void wfx_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 unused)
{
	struct wfx_vif *wvif = NULL;
	struct wfx_dev *wdev = hw->priv;

	*total_flags &= FIF_OTHER_BSS | FIF_FCSFAIL | FIF_PROBE_REQ;

	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		mutex_lock(&wvif->scan_lock);
		wvif->filter_bssid = (*total_flags &
				      (FIF_OTHER_BSS | FIF_PROBE_REQ)) ? 0 : 1;
		wvif->disable_beacon_filter = !(*total_flags & FIF_PROBE_REQ);
		wfx_fwd_probe_req(wvif, true);
		wfx_update_filtering(wvif);
		mutex_unlock(&wvif->scan_lock);
	}
}

static int wfx_update_pm(struct wfx_vif *wvif)
{
	struct ieee80211_conf *conf = &wvif->wdev->hw->conf;
	bool ps = conf->flags & IEEE80211_CONF_PS;
	int ps_timeout = conf->dynamic_ps_timeout;

	WARN_ON(conf->dynamic_ps_timeout < 0);
	if (wvif->state != WFX_STATE_STA || !wvif->bss_params.aid)
		return 0;
	if (!ps)
		ps_timeout = 0;
	if (wvif->uapsd_mask)
		ps_timeout = 0;

	// Kernel disable PowerSave when multiple vifs are in use. In contrary,
	// it is absolutly necessary to enable PowerSave for WF200
	// FIXME: only if channel vif0 != channel vif1
	if (wvif_count(wvif->wdev) > 1) {
		ps = true;
		ps_timeout = 0;
	}

	if (!wait_for_completion_timeout(&wvif->set_pm_mode_complete,
					 TU_TO_JIFFIES(512)))
		dev_warn(wvif->wdev->dev,
			 "timeout while waiting of set_pm_mode_complete\n");
	return hif_set_pm(wvif, ps, ps_timeout);
}

int wfx_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;

	WARN_ON(queue >= hw->queues);

	mutex_lock(&wdev->conf_mutex);
	assign_bit(queue, &wvif->uapsd_mask, params->uapsd);
	memcpy(&wvif->edca_params[queue], params, sizeof(*params));
	hif_set_edca_queue_params(wvif, queue, params);
	if (wvif->vif->type == NL80211_IFTYPE_STATION) {
		hif_set_uapsd_info(wvif, wvif->uapsd_mask);
		wfx_update_pm(wvif);
	}
	mutex_unlock(&wdev->conf_mutex);
	return 0;
}

int wfx_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = NULL;

	while ((wvif = wvif_iterate(wdev, wvif)) != NULL)
		hif_rts_threshold(wvif, value);
	return 0;
}

static int __wfx_flush(struct wfx_dev *wdev, bool drop)
{
	for (;;) {
		if (drop)
			wfx_tx_queues_clear(wdev);
		if (wait_event_timeout(wdev->tx_queue_stats.wait_link_id_empty,
				       wfx_tx_queues_is_empty(wdev),
				       2 * HZ) <= 0)
			return -ETIMEDOUT;
		wfx_tx_flush(wdev);
		if (wfx_tx_queues_is_empty(wdev))
			return 0;
		dev_warn(wdev->dev, "frames queued while flushing tx queues");
	}
}

void wfx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop)
{
	// FIXME: only flush requested vif and queues
	__wfx_flush(hw->priv, drop);
}

/* WSM callbacks */

static void wfx_event_report_rssi(struct wfx_vif *wvif, u8 raw_rcpi_rssi)
{
	/* RSSI: signed Q8.0, RCPI: unsigned Q7.1
	 * RSSI = RCPI / 2 - 110
	 */
	int rcpi_rssi;
	int cqm_evt;

	rcpi_rssi = raw_rcpi_rssi / 2 - 110;
	if (rcpi_rssi <= wvif->vif->bss_conf.cqm_rssi_thold)
		cqm_evt = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else
		cqm_evt = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
#if (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE)
	ieee80211_cqm_rssi_notify(wvif->vif, cqm_evt, GFP_KERNEL);
#else
	ieee80211_cqm_rssi_notify(wvif->vif, cqm_evt, rcpi_rssi, GFP_KERNEL);
#endif
}

static void wfx_event_handler_work(struct work_struct *work)
{
	struct wfx_vif *wvif =
		container_of(work, struct wfx_vif, event_handler_work);
	struct wfx_hif_event *event;

	LIST_HEAD(list);

	spin_lock(&wvif->event_queue_lock);
	list_splice_init(&wvif->event_queue, &list);
	spin_unlock(&wvif->event_queue_lock);

	list_for_each_entry(event, &list, link) {
		switch (event->evt.event_id) {
		case HIF_EVENT_IND_BSSLOST:
			cancel_work_sync(&wvif->unjoin_work);
			mutex_lock(&wvif->scan_lock);
			wfx_cqm_bssloss_sm(wvif, 1, 0, 0);
			mutex_unlock(&wvif->scan_lock);
			break;
		case HIF_EVENT_IND_BSSREGAINED:
			wfx_cqm_bssloss_sm(wvif, 0, 0, 0);
			cancel_work_sync(&wvif->unjoin_work);
			break;
		case HIF_EVENT_IND_RCPI_RSSI:
			wfx_event_report_rssi(wvif,
					      event->evt.event_data.rcpi_rssi);
			break;
		case HIF_EVENT_IND_PS_MODE_ERROR:
			dev_warn(wvif->wdev->dev,
				 "error while processing power save request\n");
			break;
		default:
			dev_warn(wvif->wdev->dev,
				 "unhandled event indication: %.2x\n",
				 event->evt.event_id);
			break;
		}
	}
	__wfx_free_event_queue(&list);
}

static void wfx_bss_loss_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    bss_loss_work.work);

	ieee80211_connection_loss(wvif->vif);
}

static void wfx_bss_params_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    bss_params_work);

	mutex_lock(&wvif->wdev->conf_mutex);
	wvif->bss_params.bss_flags.lost_count_only = 1;
	hif_set_bss_params(wvif, &wvif->bss_params);
	wvif->bss_params.bss_flags.lost_count_only = 0;
	mutex_unlock(&wvif->wdev->conf_mutex);
}

static void wfx_do_unjoin(struct wfx_vif *wvif)
{
	mutex_lock(&wvif->wdev->conf_mutex);

	if (!wvif->state)
		goto done;

	if (wvif->state == WFX_STATE_AP)
		goto done;

	cancel_work_sync(&wvif->update_filtering_work);
	wvif->state = WFX_STATE_PASSIVE;

	/* Unjoin is a reset. */
	wfx_tx_flush(wvif->wdev);
	hif_keep_alive_period(wvif, 0);
	hif_reset(wvif, false);
	wfx_tx_policy_init(wvif);
	hif_set_macaddr(wvif, wvif->vif->addr);
	wfx_free_event_queue(wvif);
	cancel_work_sync(&wvif->event_handler_work);
	wfx_cqm_bssloss_sm(wvif, 0, 0, 0);

	/* Disable Block ACKs */
	hif_set_block_ack_policy(wvif, 0, 0);

	wvif->disable_beacon_filter = false;
	wfx_update_filtering(wvif);
	memset(&wvif->bss_params, 0, sizeof(wvif->bss_params));

done:
	mutex_unlock(&wvif->wdev->conf_mutex);
}

static void wfx_set_mfp(struct wfx_vif *wvif,
			struct cfg80211_bss *bss)
{
	const int pairwise_cipher_suite_count_offset = 8 / sizeof(u16);
	const int pairwise_cipher_suite_size = 4 / sizeof(u16);
	const int akm_suite_size = 4 / sizeof(u16);
	const u16 *ptr = NULL;
	bool mfpc = false;
	bool mfpr = false;

	/* 802.11w protected mgmt frames */

	/* retrieve MFPC and MFPR flags from beacon or PBRSP */

	rcu_read_lock();
	if (bss)
		ptr = (const u16 *) ieee80211_bss_get_ie(bss,
							      WLAN_EID_RSN);

	if (ptr) {
		ptr += pairwise_cipher_suite_count_offset;
		ptr += 1 + pairwise_cipher_suite_size * *ptr;
		ptr += 1 + akm_suite_size * *ptr;
		mfpr = *ptr & BIT(6);
		mfpc = *ptr & BIT(7);
	}
	rcu_read_unlock();

	hif_set_mfp(wvif, mfpc, mfpr);
}

static void wfx_do_join(struct wfx_vif *wvif)
{
	const u8 *bssid;
	struct ieee80211_bss_conf *conf = &wvif->vif->bss_conf;
	struct cfg80211_bss *bss = NULL;
	struct hif_req_join join = {
		.infrastructure_bss_mode = !conf->ibss_joined,
		.short_preamble = conf->use_short_preamble,
		.probe_for_join = 1,
		.atim_window = 0,
		.basic_rate_set = wfx_rate_mask_to_hw(wvif->wdev,
						      conf->basic_rates),
	};

	wfx_tx_lock_flush(wvif->wdev);

	if (wvif->channel->flags & IEEE80211_CHAN_NO_IR)
		join.probe_for_join = 0;

	if (wvif->state)
		wfx_do_unjoin(wvif);

	bssid = wvif->vif->bss_conf.bssid;

#if (KERNEL_VERSION(4, 1, 0) > LINUX_VERSION_CODE)
	bss = cfg80211_get_bss(wvif->wdev->hw->wiphy, wvif->channel,
			       bssid, NULL, 0,
			       0, 0);
#else
	bss = cfg80211_get_bss(wvif->wdev->hw->wiphy, wvif->channel,
			       bssid, NULL, 0,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
#endif

	if (!bss && !conf->ibss_joined) {
		wfx_tx_unlock(wvif->wdev);
		return;
	}

	mutex_lock(&wvif->wdev->conf_mutex);

	/* Sanity check basic rates */
	if (!join.basic_rate_set)
		join.basic_rate_set = 7;

	/* Sanity check beacon interval */
	if (!wvif->beacon_int)
		wvif->beacon_int = 1;

	join.beacon_interval = wvif->beacon_int;
	join.channel_number = wvif->channel->hw_value;
	memcpy(join.bssid, bssid, sizeof(join.bssid));

	if (!conf->ibss_joined) {
		const u8 *ssidie;

		rcu_read_lock();
		ssidie = ieee80211_bss_get_ie(bss, WLAN_EID_SSID);
		if (ssidie) {
			join.ssid_length = ssidie[1];
			memcpy(join.ssid, &ssidie[2], join.ssid_length);
		}
		rcu_read_unlock();
	}

	wfx_tx_flush(wvif->wdev);

	if (wvif_count(wvif->wdev) <= 1)
		hif_set_block_ack_policy(wvif, 0xFF, 0xFF);

	wfx_set_mfp(wvif, bss);

	/* Perform actual join */
	wvif->wdev->tx_burst_idx = -1;
	if (hif_join(wvif, &join)) {
		ieee80211_connection_loss(wvif->vif);
		wvif->join_complete_status = -1;
		/* Tx lock still held, unjoin will clear it. */
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wvif->wdev);
	} else {
		wvif->join_complete_status = 0;
		if (wvif->vif->type == NL80211_IFTYPE_ADHOC)
			wvif->state = WFX_STATE_IBSS;
		else
			wvif->state = WFX_STATE_PRE_STA;
		wfx_tx_unlock(wvif->wdev);

		/* Upload keys */
		wfx_upload_keys(wvif);

		/* Due to beacon filtering it is possible that the
		 * AP's beacon is not known for the mac80211 stack.
		 * Disable filtering temporary to make sure the stack
		 * receives at least one
		 */
		wvif->disable_beacon_filter = true;
	}
	wfx_update_filtering(wvif);

	mutex_unlock(&wvif->wdev->conf_mutex);
	if (bss)
		cfg80211_put_bss(wvif->wdev->hw->wiphy, bss);
}

static void wfx_unjoin_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, unjoin_work);

	wfx_do_unjoin(wvif);
	wfx_tx_unlock(wvif->wdev);
}

int wfx_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *) &sta->drv_priv;
	struct wfx_link_entry *entry;
	struct sk_buff *skb;

	if (wvif->vif->type != NL80211_IFTYPE_AP)
		return 0;

	sta_priv->vif_id = wvif->id;
	sta_priv->link_id = wfx_find_link_id(wvif, sta->addr);
	if (!sta_priv->link_id) {
		dev_warn(wdev->dev, "mo more link-id available\n");
		return -ENOENT;
	}

	entry = &wvif->link_id_db[sta_priv->link_id - 1];
	spin_lock_bh(&wvif->ps_state_lock);
	if ((sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK) ==
					IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK)
		wvif->sta_asleep_mask |= BIT(sta_priv->link_id);
	entry->status = WFX_LINK_HARD;
	while ((skb = skb_dequeue(&entry->rx_queue)))
		ieee80211_rx_irqsafe(wdev->hw, skb);
	spin_unlock_bh(&wvif->ps_state_lock);
	return 0;
}

int wfx_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *) &sta->drv_priv;
	struct wfx_link_entry *entry;

	if (wvif->vif->type != NL80211_IFTYPE_AP || !sta_priv->link_id)
		return 0;

	entry = &wvif->link_id_db[sta_priv->link_id - 1];
	spin_lock_bh(&wvif->ps_state_lock);
	entry->status = WFX_LINK_RESERVE;
	entry->timestamp = jiffies;
	wfx_tx_lock(wdev);
	if (!schedule_work(&wvif->link_id_work))
		wfx_tx_unlock(wdev);
	spin_unlock_bh(&wvif->ps_state_lock);
	flush_work(&wvif->link_id_work);
	return 0;
}

static void wfx_set_cts_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, set_cts_work);
	u8 erp_ie[3] = { WLAN_EID_ERP_INFO, 1, 0 };
	struct hif_ie_flags target_frame = {
		.beacon = 1,
	};

	mutex_lock(&wvif->wdev->conf_mutex);
	erp_ie[2] = wvif->erp_info;
	mutex_unlock(&wvif->wdev->conf_mutex);

	hif_erp_use_protection(wvif, erp_ie[2] & WLAN_ERP_USE_PROTECTION);

	if (wvif->vif->type != NL80211_IFTYPE_STATION)
		hif_update_ie(wvif, &target_frame, erp_ie, sizeof(erp_ie));
}

static int wfx_start_ap(struct wfx_vif *wvif)
{
	int ret;

	wvif->beacon_int = wvif->vif->bss_conf.beacon_int;
	memset(&wvif->link_id_db, 0, sizeof(wvif->link_id_db));

	wvif->wdev->tx_burst_idx = -1;
	ret = hif_start(wvif, &wvif->vif->bss_conf, wvif->channel);
	if (ret)
		return ret;
	ret = wfx_upload_keys(wvif);
	if (ret)
		return ret;
	if (wvif_count(wvif->wdev) <= 1)
		hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
	wvif->state = WFX_STATE_AP;
	wfx_update_filtering(wvif);
	return 0;
}

static int wfx_update_beaconing(struct wfx_vif *wvif)
{
	if (wvif->vif->type != NL80211_IFTYPE_AP)
		return 0;
	if (wvif->state == WFX_STATE_AP &&
	    wvif->beacon_int == wvif->vif->bss_conf.beacon_int)
		return 0;
	wfx_tx_lock_flush(wvif->wdev);
	hif_reset(wvif, false);
	wfx_tx_policy_init(wvif);
	wvif->state = WFX_STATE_PASSIVE;
	wfx_start_ap(wvif);
	wfx_tx_unlock(wvif->wdev);
	return 0;
}

static int wfx_upload_ap_templates(struct wfx_vif *wvif)
{
	struct sk_buff *skb;

	if (wvif->vif->type == NL80211_IFTYPE_STATION ||
	    wvif->vif->type == NL80211_IFTYPE_MONITOR ||
	    wvif->vif->type == NL80211_IFTYPE_UNSPECIFIED)
		return 0;

	skb = ieee80211_beacon_get(wvif->wdev->hw, wvif->vif);
	if (!skb)
		return -ENOMEM;
	hif_set_template_frame(wvif, skb, HIF_TMPLT_BCN,
			       API_RATE_INDEX_B_1MBPS);
	dev_kfree_skb(skb);

	skb = ieee80211_proberesp_get(wvif->wdev->hw, wvif->vif);
	if (!skb)
		return -ENOMEM;
	hif_set_template_frame(wvif, skb, HIF_TMPLT_PRBRES,
			       API_RATE_INDEX_B_1MBPS);
	dev_kfree_skb(skb);
	return 0;
}

static void wfx_join_finalize(struct wfx_vif *wvif,
			      struct ieee80211_bss_conf *info)
{
	struct ieee80211_sta *sta = NULL;
	struct hif_mib_set_association_mode association_mode = { };

	wvif->beacon_int = info->beacon_int;
	rcu_read_lock();
	if (info->bssid && !info->ibss_joined)
		sta = ieee80211_find_sta(wvif->vif, info->bssid);
	rcu_read_unlock();
	if (sta)
		wvif->bss_params.operational_rate_set =
			wfx_rate_mask_to_hw(wvif->wdev, sta->supp_rates[wvif->channel->band]);
	else
		wvif->bss_params.operational_rate_set = -1;
	if (sta &&
	    info->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT)
		hif_dual_cts_protection(wvif, true);
	else
		hif_dual_cts_protection(wvif, false);

	association_mode.preambtype_use = 1;
	association_mode.mode = 1;
	association_mode.rateset = 1;
	association_mode.spacing = 1;
	association_mode.short_preamble = info->use_short_preamble;
	association_mode.basic_rate_set = cpu_to_le32(wfx_rate_mask_to_hw(wvif->wdev, info->basic_rates));
	if (sta && sta->ht_cap.ht_supported &&
	    !(info->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT))
		association_mode.greenfield =
			!!(sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);
	if (sta && sta->ht_cap.ht_supported)
		association_mode.mpdu_start_spacing = sta->ht_cap.ampdu_density;

	wfx_cqm_bssloss_sm(wvif, 0, 0, 0);
	cancel_work_sync(&wvif->unjoin_work);

	wvif->bss_params.beacon_lost_count = 20;
	wvif->bss_params.aid = info->aid;

	hif_set_association_mode(wvif, &association_mode);

	if (!info->ibss_joined) {
		hif_keep_alive_period(wvif, 30 /* sec */);
		hif_set_bss_params(wvif, &wvif->bss_params);
		hif_set_beacon_wakeup_period(wvif, info->dtim_period,
					     info->dtim_period);
		wfx_update_pm(wvif);
	}
}

void wfx_bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u32 changed)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	bool do_join = false;
	int i;

	mutex_lock(&wdev->conf_mutex);

	/* TODO: BSS_CHANGED_QOS */
	if (changed & BSS_CHANGED_ARP_FILTER) {
		for (i = 0; i < HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES; i++) {
			__be32 *arp_addr = &info->arp_addr_list[i];

			if (info->arp_addr_cnt > HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES)
				arp_addr = NULL;
			if (i >= info->arp_addr_cnt)
				arp_addr = NULL;
			hif_set_arp_ipv4_filter(wvif, i, arp_addr);
		}
	}

	if (changed & BSS_CHANGED_BEACON ||
	    changed & BSS_CHANGED_AP_PROBE_RESP ||
	    changed & BSS_CHANGED_BSSID ||
	    changed & BSS_CHANGED_SSID ||
	    changed & BSS_CHANGED_IBSS) {
		wvif->beacon_int = info->beacon_int;
		wfx_update_beaconing(wvif);
		wfx_upload_ap_templates(wvif);
		wfx_fwd_probe_req(wvif, false);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED &&
	    wvif->state != WFX_STATE_IBSS)
		hif_beacon_transmit(wvif, info->enable_beacon);

	if (changed & BSS_CHANGED_BEACON_INFO)
		hif_set_beacon_wakeup_period(wvif, info->dtim_period,
					     info->dtim_period);

	/* assoc/disassoc, or maybe AID changed */
	if (changed & BSS_CHANGED_ASSOC) {
		wfx_tx_lock_flush(wdev);
		wvif->wep_default_key_id = -1;
		wfx_tx_unlock(wdev);
	}

	if (changed & BSS_CHANGED_ASSOC && !info->assoc &&
	    (wvif->state == WFX_STATE_STA || wvif->state == WFX_STATE_IBSS)) {
		/* Shedule unjoin work */
		wfx_tx_lock(wdev);
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wdev);
	} else {
		if (changed & BSS_CHANGED_BEACON_INT) {
			if (info->ibss_joined)
				do_join = true;
			else if (wvif->state == WFX_STATE_AP)
				wfx_update_beaconing(wvif);
		}

		if (changed & BSS_CHANGED_BSSID)
			do_join = true;

		if (changed & BSS_CHANGED_ASSOC ||
		    changed & BSS_CHANGED_BSSID ||
		    changed & BSS_CHANGED_IBSS ||
		    changed & BSS_CHANGED_BASIC_RATES ||
		    changed & BSS_CHANGED_HT) {
			if (info->assoc) {
				if (wvif->state < WFX_STATE_PRE_STA) {
					ieee80211_connection_loss(vif);
					mutex_unlock(&wdev->conf_mutex);
					return;
				} else if (wvif->state == WFX_STATE_PRE_STA) {
					wvif->state = WFX_STATE_STA;
				}
			} else {
				do_join = true;
			}

			if (info->assoc || info->ibss_joined)
				wfx_join_finalize(wvif, info);
			else
				memset(&wvif->bss_params, 0,
				       sizeof(wvif->bss_params));
		}
	}

	/* ERP Protection */
	if (changed & BSS_CHANGED_ASSOC ||
	    changed & BSS_CHANGED_ERP_CTS_PROT ||
	    changed & BSS_CHANGED_ERP_PREAMBLE) {
		u32 prev_erp_info = wvif->erp_info;

		if (info->use_cts_prot)
			wvif->erp_info |= WLAN_ERP_USE_PROTECTION;
		else if (!(prev_erp_info & WLAN_ERP_NON_ERP_PRESENT))
			wvif->erp_info &= ~WLAN_ERP_USE_PROTECTION;

		if (info->use_short_preamble)
			wvif->erp_info |= WLAN_ERP_BARKER_PREAMBLE;
		else
			wvif->erp_info &= ~WLAN_ERP_BARKER_PREAMBLE;

		if (prev_erp_info != wvif->erp_info)
			schedule_work(&wvif->set_cts_work);
	}

	if (changed & BSS_CHANGED_ASSOC || changed & BSS_CHANGED_ERP_SLOT)
		hif_slot_time(wvif, info->use_short_slot ? 9 : 20);

	if (changed & BSS_CHANGED_ASSOC || changed & BSS_CHANGED_CQM)
		hif_set_rcpi_rssi_threshold(wvif, info->cqm_rssi_thold,
					    info->cqm_rssi_hyst);

	if (changed & BSS_CHANGED_TXPOWER)
		hif_set_output_power(wvif, info->txpower);
	mutex_unlock(&wdev->conf_mutex);

	if (do_join)
		wfx_do_join(wvif);
}

static void wfx_ps_notify(struct wfx_vif *wvif, enum sta_notify_cmd notify_cmd,
			  int link_id)
{
	u32 bit, prev;

	spin_lock_bh(&wvif->ps_state_lock);
	/* Zero link id means "for all link IDs" */
	if (link_id) {
		bit = BIT(link_id);
	} else if (notify_cmd != STA_NOTIFY_AWAKE) {
		dev_warn(wvif->wdev->dev, "unsupported notify command\n");
		bit = 0;
	} else {
		bit = wvif->link_id_map;
	}
	prev = wvif->sta_asleep_mask & bit;

	switch (notify_cmd) {
	case STA_NOTIFY_SLEEP:
		if (!prev) {
			if (wvif->mcast_buffered && !wvif->sta_asleep_mask)
				schedule_work(&wvif->mcast_start_work);
			wvif->sta_asleep_mask |= bit;
		}
		break;
	case STA_NOTIFY_AWAKE:
		if (prev) {
			wvif->sta_asleep_mask &= ~bit;
			wvif->pspoll_mask &= ~bit;
			if (link_id && !wvif->sta_asleep_mask)
				schedule_work(&wvif->mcast_stop_work);
			wfx_bh_request_tx(wvif->wdev);
		}
		break;
	}
	spin_unlock_bh(&wvif->ps_state_lock);
}

void wfx_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    enum sta_notify_cmd notify_cmd, struct ieee80211_sta *sta)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *) &sta->drv_priv;

	wfx_ps_notify(wvif, notify_cmd, sta_priv->link_id);
}

static int wfx_set_tim_impl(struct wfx_vif *wvif, bool aid0_bit_set)
{
	struct sk_buff *skb;
	struct hif_ie_flags target_frame = {
		.beacon = 1,
	};
	u16 tim_offset, tim_length;
	u8 *tim_ptr;

	skb = ieee80211_beacon_get_tim(wvif->wdev->hw, wvif->vif,
				       &tim_offset, &tim_length);
	if (!skb) {
		__wfx_flush(wvif->wdev, true);
		return -ENOENT;
	}
	tim_ptr = skb->data + tim_offset;

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally.
		 */
		tim_ptr[2] = 0;

		/* Set/reset aid0 bit */
		if (aid0_bit_set)
			tim_ptr[4] |= 1;
		else
			tim_ptr[4] &= ~1;
	}

	hif_update_ie(wvif, &target_frame, tim_ptr, tim_length);
	dev_kfree_skb(skb);

	return 0;
}

static void wfx_set_tim_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, set_tim_work);

	wfx_set_tim_impl(wvif, wvif->aid0_bit_set);
}

int wfx_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_sta_priv *sta_dev = (struct wfx_sta_priv *) &sta->drv_priv;
	struct wfx_vif *wvif = wdev_to_wvif(wdev, sta_dev->vif_id);

	schedule_work(&wvif->set_tim_work);
	return 0;
}

static void wfx_mcast_start_work(struct work_struct *work)
{
	struct wfx_vif *wvif =
		container_of(work, struct wfx_vif, mcast_start_work);
	struct ieee80211_bss_conf *conf = &wvif->vif->bss_conf;
	long tmo = conf->dtim_period * TU_TO_JIFFIES(wvif->beacon_int + 20);

	cancel_work_sync(&wvif->mcast_stop_work);
	if (!wvif->aid0_bit_set) {
		wfx_tx_lock_flush(wvif->wdev);
		wfx_set_tim_impl(wvif, true);
		wvif->aid0_bit_set = true;
		mod_timer(&wvif->mcast_timeout, jiffies + tmo);
		wfx_tx_unlock(wvif->wdev);
	}
}

static void wfx_mcast_stop_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    mcast_stop_work);

	if (wvif->aid0_bit_set) {
		del_timer_sync(&wvif->mcast_timeout);
		wfx_tx_lock_flush(wvif->wdev);
		wvif->aid0_bit_set = false;
		wfx_set_tim_impl(wvif, false);
		wfx_tx_unlock(wvif->wdev);
	}
}

#if (KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE)
static void wfx_mcast_timeout(unsigned long arg)
{
	struct wfx_vif *wvif = (struct wfx_vif *)arg;
#else
static void wfx_mcast_timeout(struct timer_list *t)
{
	struct wfx_vif *wvif = from_timer(wvif, t, mcast_timeout);
#endif
	dev_warn(wvif->wdev->dev, "multicast delivery timeout\n");
	spin_lock_bh(&wvif->ps_state_lock);
	wvif->mcast_tx = wvif->aid0_bit_set && wvif->mcast_buffered;
	if (wvif->mcast_tx)
		wfx_bh_request_tx(wvif->wdev);
	spin_unlock_bh(&wvif->ps_state_lock);
}

#if (KERNEL_VERSION(4, 4, 0) > LINUX_VERSION_CODE)
int wfx_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     enum ieee80211_ampdu_mlme_action action,
		     struct ieee80211_sta *sta, u16 tid,
		     u16 *ssn, u8 buf_size)
#else
#if (KERNEL_VERSION(4, 4, 69) > LINUX_VERSION_CODE)
int wfx_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     enum ieee80211_ampdu_mlme_action action,
		     struct ieee80211_sta *sta, u16 tid, u16 *ssn,
		     u8 buf_size, bool amsdu)
#else
int wfx_ampdu_action(struct ieee80211_hw *hw,
		     struct ieee80211_vif *vif,
		     struct ieee80211_ampdu_params *params)
#endif
#endif
{
	/* Aggregation is implemented fully in firmware,
	 * including block ack negotiation. Do not allow
	 * mac80211 stack to do anything: it interferes with
	 * the firmware.
	 */

	/* Note that we still need this function stubbed. */

	return -ENOTSUPP;
}

void wfx_suspend_resume(struct wfx_vif *wvif,
			const struct hif_ind_suspend_resume_tx *arg)
{
	if (arg->suspend_resume_flags.bc_mc_only) {
		bool cancel_tmo = false;

		spin_lock_bh(&wvif->ps_state_lock);
		if (!arg->suspend_resume_flags.resume)
			wvif->mcast_tx = false;
		else
			wvif->mcast_tx = wvif->aid0_bit_set &&
					 wvif->mcast_buffered;
		if (wvif->mcast_tx) {
			cancel_tmo = true;
			wfx_bh_request_tx(wvif->wdev);
		}
		spin_unlock_bh(&wvif->ps_state_lock);
		if (cancel_tmo)
			del_timer_sync(&wvif->mcast_timeout);
	} else if (arg->suspend_resume_flags.resume) {
		// FIXME: should change each station status independently
		wfx_ps_notify(wvif, STA_NOTIFY_AWAKE, 0);
		wfx_bh_request_tx(wvif->wdev);
	} else {
		// FIXME: should change each station status independently
		wfx_ps_notify(wvif, STA_NOTIFY_SLEEP, 0);
	}
}

int wfx_add_chanctx(struct ieee80211_hw *hw,
		    struct ieee80211_chanctx_conf *conf)
{
	return 0;
}

void wfx_remove_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *conf)
{
}

void wfx_change_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *conf,
			u32 changed)
{
}

int wfx_assign_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_chanctx_conf *conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct ieee80211_channel *ch = conf->def.chan;

	WARN(wvif->channel, "channel overwrite");
	wvif->channel = ch;

	return 0;
}

void wfx_unassign_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_chanctx_conf *conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct ieee80211_channel *ch = conf->def.chan;

	WARN(wvif->channel != ch, "channel mismatch");
	wvif->channel = NULL;
}

int wfx_config(struct ieee80211_hw *hw, u32 changed)
{
	int ret = 0;
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif;

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		mutex_lock(&wdev->conf_mutex);
		wvif = NULL;
		while ((wvif = wvif_iterate(wdev, wvif)) != NULL)
			wfx_update_pm(wvif);
		mutex_unlock(&wdev->conf_mutex);
	}
	return ret;
}

int wfx_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	int i;
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
#if (KERNEL_VERSION(3, 19, 0) <= LINUX_VERSION_CODE)
			     IEEE80211_VIF_SUPPORTS_UAPSD |
#endif
			     IEEE80211_VIF_SUPPORTS_CQM_RSSI;

	mutex_lock(&wdev->conf_mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		break;
	default:
		mutex_unlock(&wdev->conf_mutex);
		return -EOPNOTSUPP;
	}

	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		if (!wdev->vif[i]) {
			wdev->vif[i] = vif;
			wvif->id = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(wdev->vif)) {
		mutex_unlock(&wdev->conf_mutex);
		return -EOPNOTSUPP;
	}
	// FIXME: prefer use of container_of() to get vif
	wvif->vif = vif;
	wvif->wdev = wdev;

	INIT_WORK(&wvif->link_id_work, wfx_link_id_work);
	INIT_DELAYED_WORK(&wvif->link_id_gc_work, wfx_link_id_gc_work);

	spin_lock_init(&wvif->ps_state_lock);
	INIT_WORK(&wvif->set_tim_work, wfx_set_tim_work);

	INIT_WORK(&wvif->mcast_start_work, wfx_mcast_start_work);
	INIT_WORK(&wvif->mcast_stop_work, wfx_mcast_stop_work);
#if (KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE)
	setup_timer(&wvif->mcast_timeout, wfx_mcast_timeout, (unsigned long) wvif);
#else
	timer_setup(&wvif->mcast_timeout, wfx_mcast_timeout, 0);
#endif

	memset(&wvif->bss_params, 0, sizeof(wvif->bss_params));

	mutex_init(&wvif->bss_loss_lock);
	INIT_DELAYED_WORK(&wvif->bss_loss_work, wfx_bss_loss_work);

	wvif->wep_default_key_id = -1;
	INIT_WORK(&wvif->wep_key_work, wfx_wep_key_work);

	spin_lock_init(&wvif->event_queue_lock);
	INIT_LIST_HEAD(&wvif->event_queue);
	INIT_WORK(&wvif->event_handler_work, wfx_event_handler_work);

	init_completion(&wvif->set_pm_mode_complete);
	complete(&wvif->set_pm_mode_complete);
	INIT_WORK(&wvif->update_filtering_work, wfx_update_filtering_work);
	INIT_WORK(&wvif->bss_params_work, wfx_bss_params_work);
	INIT_WORK(&wvif->set_cts_work, wfx_set_cts_work);
	INIT_WORK(&wvif->unjoin_work, wfx_unjoin_work);
	INIT_WORK(&wvif->tx_policy_upload_work, wfx_tx_policy_upload_work);

	mutex_init(&wvif->scan_lock);
	init_completion(&wvif->scan_complete);
	INIT_WORK(&wvif->scan_work, wfx_hw_scan_work);

	mutex_unlock(&wdev->conf_mutex);

	hif_set_macaddr(wvif, vif->addr);

	wfx_tx_policy_init(wvif);
	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		// Combo mode does not support Block Acks. We can re-enable them
		if (wvif_count(wdev) == 1)
			hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
		else
			hif_set_block_ack_policy(wvif, 0x00, 0x00);
		// Combo force powersave mode. We can re-enable it now
		wfx_update_pm(wvif);
	}
	return 0;
}

void wfx_remove_interface(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	int i;

	wait_for_completion_timeout(&wvif->set_pm_mode_complete, msecs_to_jiffies(300));

	mutex_lock(&wdev->conf_mutex);
	switch (wvif->state) {
	case WFX_STATE_PRE_STA:
	case WFX_STATE_STA:
	case WFX_STATE_IBSS:
		wfx_tx_lock_flush(wdev);
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wdev);
		break;
	case WFX_STATE_AP:
		for (i = 0; wvif->link_id_map; ++i) {
			if (wvif->link_id_map & BIT(i)) {
				wfx_unmap_link(wvif, i);
				wvif->link_id_map &= ~BIT(i);
			}
		}
		memset(wvif->link_id_db, 0, sizeof(wvif->link_id_db));
		wvif->sta_asleep_mask = 0;
		wvif->mcast_tx = false;
		wvif->aid0_bit_set = false;
		wvif->mcast_buffered = false;
		wvif->pspoll_mask = 0;
		/* reset.link_id = 0; */
		hif_reset(wvif, false);
		break;
	default:
		break;
	}

	wvif->state = WFX_STATE_PASSIVE;
	wfx_tx_queues_wait_empty_vif(wvif);
	wfx_tx_unlock(wdev);

	/* FIXME: In add to reset MAC address, try to reset interface */
	hif_set_macaddr(wvif, NULL);

	wfx_cqm_bssloss_sm(wvif, 0, 0, 0);
	cancel_work_sync(&wvif->unjoin_work);
	cancel_delayed_work_sync(&wvif->link_id_gc_work);
	del_timer_sync(&wvif->mcast_timeout);
	wfx_free_event_queue(wvif);

	wdev->vif[wvif->id] = NULL;
	wvif->vif = NULL;

	mutex_unlock(&wdev->conf_mutex);
	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		// Combo mode does not support Block Acks. We can re-enable them
		if (wvif_count(wdev) == 1)
			hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
		else
			hif_set_block_ack_policy(wvif, 0x00, 0x00);
		// Combo force powersave mode. We can re-enable it now
		wfx_update_pm(wvif);
	}
}

int wfx_start(struct ieee80211_hw *hw)
{
	return 0;
}

void wfx_stop(struct ieee80211_hw *hw)
{
	struct wfx_dev *wdev = hw->priv;

	wfx_tx_lock_flush(wdev);
	mutex_lock(&wdev->conf_mutex);
	wfx_tx_queues_clear(wdev);
	mutex_unlock(&wdev->conf_mutex);
	wfx_tx_unlock(wdev);
	WARN(atomic_read(&wdev->tx_lock), "tx_lock is locked");
}
