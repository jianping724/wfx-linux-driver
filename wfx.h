/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common private data for Silicon Labs WFx chips.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#ifndef WFX_H
#define WFX_H

#include <linux/version.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <net/mac80211.h>

#include "bh.h"
#include "data_tx.h"
#include "main.h"
#include "queue.h"
#include "secure_link.h"
#include "sta.h"
#include "scan.h"
#include "hif_tx.h"
#include "hif_api_general.h"

#define USEC_PER_TXOP 32 // see struct ieee80211_tx_queue_params
#define USEC_PER_TU 1024

#if (KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE)
#define array_index_nospec(index, size) index
#else
#include <linux/nospec.h>
#endif

#if (KERNEL_VERSION(4, 2, 0) > LINUX_VERSION_CODE)
static inline void _ieee80211_hw_set(struct ieee80211_hw *hw,
				     enum ieee80211_hw_flags flg)
{
	hw->flags |= flg;
}
#define ieee80211_hw_set(hw, flg)	_ieee80211_hw_set(hw, IEEE80211_HW_##flg)
#endif

#if (KERNEL_VERSION(4, 7, 0) > LINUX_VERSION_CODE)
#define nl80211_band ieee80211_band
#define NL80211_BAND_2GHZ IEEE80211_BAND_2GHZ
#define NUM_NL80211_BANDS IEEE80211_NUM_BANDS
#endif

#if (KERNEL_VERSION(4, 1, 0) > LINUX_VERSION_CODE)
// In kernels < 4.1, if we transate these define with 0, it is sufficient to
// make cfg80211_get_bss() happy. However, their values in kernel > 4.1 are not
// 0. So, only use them as parameters to cfg80211_get_bss().
#define IEEE80211_BSS_TYPE_ANY 0
#define IEEE80211_PRIVACY_ANY  0
#endif

#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
static inline u8 ieee80211_get_tid(struct ieee80211_hdr *hdr)
{
	u8 *qc = ieee80211_get_qos_ctl(hdr);

	return qc[0] & IEEE80211_QOS_CTL_TID_MASK;
}
#endif

#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
#define struct_size(p, member, n) \
	(n * sizeof(*(p)->member) + __must_be_array((p)->member) + sizeof(*(p)))
#endif

struct hwbus_ops;

struct wfx_dev {
	struct wfx_platform_data pdata;
	struct device		*dev;
	struct ieee80211_hw	*hw;
	struct ieee80211_vif	*vif[2];
	struct mac_address	addresses[2];
	const struct hwbus_ops	*hwbus_ops;
	void			*hwbus_priv;

	u8			keyset;
	struct completion	firmware_ready;
	struct hif_ind_startup	hw_caps;
	struct wfx_hif		hif;
	struct sl_context	sl;
	struct delayed_work	cooling_timeout_work;
	bool			chip_frozen;
	struct mutex		conf_mutex;

	struct wfx_hif_cmd	hif_cmd;
	struct wfx_queue	tx_queue[4];
	struct sk_buff_head	tx_pending;
	wait_queue_head_t	tx_dequeue;
	atomic_t		tx_lock;

	atomic_t		packet_id;
	u32			key_map;

	struct hif_rx_stats	rx_stats;
	struct mutex		rx_stats_lock;
};

struct wfx_vif {
	struct wfx_dev		*wdev;
	struct ieee80211_vif	*vif;
	struct ieee80211_channel *channel;
	int			id;

	u32			link_id_map;


	bool			after_dtim_tx_allowed;

	struct delayed_work	beacon_loss_work;
	struct work_struct	update_tim_work;

	struct tx_policy_cache	tx_policy_cache;
	struct work_struct	tx_policy_upload_work;


	int			filter_mcast_count;
	u8			filter_mcast_addr[8][ETH_ALEN];

	unsigned long		uapsd_mask;

	struct ieee80211_scan_request *scan_req;
	struct work_struct	scan_work;
	struct mutex		scan_lock;
	struct completion	scan_complete;
	bool			scan_abort;

	bool			bss_not_support_ps_poll;
	struct work_struct	update_pm_work;
	struct completion	set_pm_mode_complete;
};

static inline struct wfx_vif *wdev_to_wvif(struct wfx_dev *wdev, int vif_id)
{
	if (vif_id >= ARRAY_SIZE(wdev->vif)) {
		dev_dbg(wdev->dev, "requesting non-existent vif: %d\n", vif_id);
		return NULL;
	}
	vif_id = array_index_nospec(vif_id, ARRAY_SIZE(wdev->vif));
	if (!wdev->vif[vif_id]) {
		dev_dbg(wdev->dev, "requesting non-allocated vif: %d\n",
			vif_id);
		return NULL;
	}
	return (struct wfx_vif *) wdev->vif[vif_id]->drv_priv;
}

static inline struct wfx_vif *wvif_iterate(struct wfx_dev *wdev,
					   struct wfx_vif *cur)
{
	int i;
	int mark = 0;
	struct wfx_vif *tmp;

	if (!cur)
		mark = 1;
	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		tmp = wdev_to_wvif(wdev, i);
		if (mark && tmp)
			return tmp;
		if (tmp == cur)
			mark = 1;
	}
	return NULL;
}

static inline int wvif_count(struct wfx_dev *wdev)
{
	int i;
	int ret = 0;
	struct wfx_vif *wvif;

	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		wvif = wdev_to_wvif(wdev, i);
		if (wvif)
			ret++;
	}
	return ret;
}

static inline void memreverse(u8 *src, u8 length)
{
	u8 *lo = src;
	u8 *hi = src + length - 1;
	u8 swap;

	while (lo < hi) {
		swap = *lo;
		*lo++ = *hi;
		*hi-- = swap;
	}
}

static inline int memzcmp(void *src, unsigned int size)
{
	u8 *buf = src;

	if (!size)
		return 0;
	if (*buf)
		return 1;
	return memcmp(buf, buf + 1, size - 1);
}

#endif /* WFX_H */
