/*
 * Common private data for Silicon Labs WFX drivers
 *
 * Copyright (c) 2017, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef WFX_H
#define WFX_H

#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <net/mac80211.h>

#include "wfx_api.h"
#include "queue.h"
#include "wsm.h"
#include "scan.h"
#include "data_txrx.h"
#include "pm.h"

#if (KERNEL_VERSION(4, 7, 0) > LINUX_VERSION_CODE)
#define nl80211_band ieee80211_band
#define NL80211_BAND_2GHZ IEEE80211_BAND_2GHZ
#define NL80211_BAND_5GHZ IEEE80211_BAND_5GHZ
#define NUM_NL80211_BANDS IEEE80211_NUM_BANDS
#endif

/* WFx indication error */
#define INVALID_PDS_CONFIG_FILE    1
#define JOIN_CNF_AUTH_FAILED       2

/* Please keep order */
enum wfx_join_status {
	WFX_JOIN_STATUS_PASSIVE = 0,
	WFX_JOIN_STATUS_MONITOR,
	WFX_JOIN_STATUS_JOINING,
	WFX_JOIN_STATUS_PRE_STA,
	WFX_JOIN_STATUS_STA,
	WFX_JOIN_STATUS_IBSS,
	WFX_JOIN_STATUS_AP,
};

/* Please keep order */
enum wfx_link_status {
	WFX_LINK_OFF,
	WFX_LINK_RESERVE,
	WFX_LINK_SOFT,
	WFX_LINK_HARD,
	WFX_LINK_RESET,
	WFX_LINK_RESET_REMAP,
};

#define WFX_MAX_STA_IN_AP_MODE    (8)
#define WFX_LINK_ID_AFTER_DTIM    (WFX_MAX_STA_IN_AP_MODE + 1)
#define WFX_LINK_ID_UAPSD         (WFX_MAX_STA_IN_AP_MODE + 2)
#define WFX_LINK_ID_MAX           (WFX_MAX_STA_IN_AP_MODE + 3)
#define WFX_MAX_REQUEUE_ATTEMPTS  (5)
#define WFX_MAX_TID               (8)

struct hwbus_ops;
struct wfx_debug_priv;

struct wfx_ht_info {
	struct ieee80211_sta_ht_cap	ht_cap;
	enum nl80211_channel_type	channel_type;
	u16				operation_mode;
};

struct wfx_link_entry {
	unsigned long			timestamp;
	enum wfx_link_status		status;
	enum wfx_link_status		prev_status;
	u8			mac[ETH_ALEN];          /* peer MAC address in use */
	u8			old_mac[ETH_ALEN];      /* Previous peerMAC address. To use in unmap message */
	u8				buffered[WFX_MAX_TID];
	struct sk_buff_head		rx_queue;
};

struct wfx_platform_data {
	const char *file_fw;
	const char *file_pds;
	int power_mode;
	struct gpio_desc *gpio_wakeup;
	bool support_ldpc;
	bool hif_clkedge; /* if true HIF D_out is sampled on the rising edge of the clock (intended to be used in 50Mhz SDIO) */
	bool sdio;
};

struct wfx_dev {
	struct wfx_platform_data pdata;
	/* interfaces to the rest of the stack */
	struct ieee80211_hw		*hw;
	struct ieee80211_vif		*vif;
	struct device			*pdev;

	/* Statistics */
	struct ieee80211_low_level_stats stats;

	/* MAC address affected to hardware. It may vary depending of mode of
	 * operations */
	u8 mac_addr[ETH_ALEN];

	/* Hardware interface */
	const struct hwbus_ops		*hwbus_ops;
	struct hwbus_priv		*hwbus_priv;

	/* Hardware information */
	int				hw_type;
	int			hw_revision;

	struct wfx_debug_priv	*debug;

	struct workqueue_struct		*workqueue;
	/* Mutex for device configuration */
	struct mutex			conf_mutex;

	struct wfx_queue		tx_queue[4];
	struct wfx_queue_stats	tx_queue_stats;
	int				tx_burst_idx;

	/* Radio data */
	int output_power;

	/* BBP/MAC state */
	struct ieee80211_rate		*rates;
	struct ieee80211_rate		*mcs_rates;
	struct ieee80211_channel	*channel;
	struct wfx_ht_info		ht_info;
	int				channel_switch_in_progress;
	wait_queue_head_t		channel_switch_done;
	u8				long_frame_max_tx_count;
	u8				short_frame_max_tx_count;

	struct wfx_pm_state		pm_state;


	/* BH */
	atomic_t				bh_rx; /* record that the IRQ triggered */
	atomic_t			bh_tx;
	atomic_t			bh_term;
	atomic_t			bh_suspend;
	atomic_t				device_can_sleep; /* =!WUP signal */

	struct workqueue_struct         *bh_workqueue;
	struct work_struct              bh_work;

	int				bh_error;
	wait_queue_head_t		bh_wq;
	wait_queue_head_t		bh_evt_wq;
	u8				wsm_rx_seq;
	u8				wsm_tx_seq;
	int				hw_bufs_used;
	bool					sleep_activated;

	/* Scan status */
	struct wfx_scan scan;
	/* Keep wfx200 awake (WUP = 1) 1 second after each scan to avoid
	 * FW issue with sleeping/waking up.
	 */
	atomic_t				wait_for_scan;

	/* WSM */
	HiStartupIndBody_t			wsm_caps;
	/* Mutex to protect wsm message sending */
	struct mutex			wsm_cmd_mux;
	struct wsm_buf			wsm_cmd_buf;
	struct wsm_cmd			wsm_cmd;
	wait_queue_head_t		wsm_cmd_wq;
	wait_queue_head_t		wsm_startup_done;
	int                             firmware_ready;
	atomic_t			tx_lock;

	/* WSM Join */

	u32			pending_frame_id;

	// FIXME: key_map and keys should be parts of wvif
	u32			key_map;
	WsmHiAddKeyReqBody_t			keys[WSM_KEY_MAX_INDEX + 1];

	/* TX rate policy cache */
	struct tx_policy_cache tx_policy_cache;
	struct work_struct tx_policy_upload_work;

	/* legacy PS mode switch in suspend */
	int			ps_mode_switch_in_progress;
	wait_queue_head_t	ps_mode_switch_done;

};

struct wfx_vif {
	struct wfx_dev		*wdev;
	struct ieee80211_vif	*vif;
	int			Id;
	int			mode;
	int			power_set_true;
	int			user_power_set_true;
	int			cqm_link_loss_count;
	int			cqm_beacon_loss_count;
	int			join_dtim_period;
	int			beacon_int;
	int			bss_loss_state;
	int			delayed_link_loss;
	int			cqm_rssi_thold;
	int			join_complete_status;

	unsigned		cqm_rssi_hyst;

	u8			user_pm_mode;
	u8			bssid[ETH_ALEN];
	u8			action_frame_sa[ETH_ALEN];
	u8			action_linkid;

	u32			link_id_map;
	u32			sta_asleep_mask;
	u32			pspoll_mask;
	u32			listen_interval;
	u32			erp_info;
	u32			bss_loss_confirm_id;
	u32			cipherType;
	u32			rts_threshold;

	bool			join_pending;
	bool			enable_beacon;
	bool			htcap;
	bool			setbssparams_done;
	bool			buffered_multicasts;
	bool			tx_multicast;
	bool			aid0_bit_set;
	bool			delayed_unjoin;
	bool			has_multicast_subscription;
	bool			disable_beacon_filter;
	bool			listening;
	bool			cqm_use_rssi;

	/* TX/RX and security */
	s8			wep_default_key_id;

	enum wfx_join_status	join_status;

	struct wsm_rx_filter	rx_filter;
	struct wsm_edca_params	edca;
	struct wsm_tx_queue_params	tx_queue_params;
	struct wfx_link_entry	link_id_db[WFX_MAX_STA_IN_AP_MODE];

	/* work: initiate in wfx_vif_setup */
	struct work_struct	join_complete_work;
	struct work_struct	unjoin_work;
	struct work_struct	wep_key_work;
	struct work_struct	set_tim_work;
	struct work_struct	set_cts_work;
	struct work_struct	multicast_start_work;
	struct work_struct	multicast_stop_work;
	struct work_struct	link_id_work;
	struct work_struct	update_filtering_work;
	struct work_struct	set_beacon_wakeup_period_work;
	struct work_struct	bss_params_work;
	/* Workaround for WFD testcase 6.1.10*/
	struct work_struct	linkid_reset_work;
	struct work_struct	event_handler;

	/* delayed work */
	struct delayed_work	join_timeout;
	struct delayed_work	bss_loss_work;
	struct delayed_work	link_id_gc_work;

	/* API */
	WsmHiSetPmModeReqBody_t		powersave_mode;
	WsmHiSetBssParamsReqBody_t	bss_params;
	WsmHiSetPmModeReqBody_t		firmware_ps_mode;
	WsmHiMibSetUapsdInformation_t	uapsd_info;
	WsmHiMibSetAssociationMode_t	association_mode;
	WsmHiMibGrpAddrTable_t		multicast_filter;
	/*Add support in mac80211 for psmode info per VIF */
	WsmHiMibP2PPsModeInfo_t		p2p_ps_modeinfo;

	/* spinlock/mutex */
	struct mutex		bss_loss_lock;
	spinlock_t		ps_state_lock;
	spinlock_t		vif_lock;
	spinlock_t		event_queue_lock;

	/* WSM events and CQM implementation */
	struct list_head	event_queue;

	struct timer_list	mcast_timeout;
};

static inline struct wfx_vif *wdev_to_wvif(struct wfx_dev *wdev, int vif_id)
{
	WARN(vif_id, "Not yet supported");
	if (!wdev->vif)
		return NULL;
	return (struct wfx_vif *) wdev->vif->drv_priv;
}

struct wfx_sta_priv {
	int link_id;
	int vif_id;
};

extern const char *const wfx_fw_types[];

/* interfaces for the drivers */
int wfx_core_probe(const struct wfx_platform_data *pdata,
		   const struct hwbus_ops *hwbus_ops,
		      struct hwbus_priv *hwbus,
		      struct device *pdev,
		   struct wfx_dev **pself);

void wfx_core_release(struct wfx_dev *self);

struct gpio_desc *wfx_get_gpio(struct device *dev, int override, const char *label);

static inline int wfx_is_ht(const struct wfx_ht_info *ht_info)
{
	return ht_info->channel_type != NL80211_CHAN_NO_HT;
}

/* 802.11n HT capability: IEEE80211_HT_CAP_GRN_FLD.
 * Device supports Greenfield preamble.
 */
static inline int wfx_ht_greenfield(const struct wfx_ht_info *ht_info)
{
	return wfx_is_ht(ht_info) &&
		(ht_info->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD) &&
		!(ht_info->operation_mode &
		  IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
}

/* 802.11n HT capability: IEEE80211_HT_CAP_LDPC_CODING.
 * Device supports LDPC coding.
 */
static inline int wfx_ht_fecCoding(const struct wfx_ht_info *ht_info)
{
	return wfx_is_ht(ht_info) &&
	       (ht_info->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING);
}

/* 802.11n HT capability: IEEE80211_HT_CAP_SGI_20.
 * Device supports Short Guard    Interval on 20MHz channels.
 */
static inline int wfx_ht_shortGi(const struct wfx_ht_info *ht_info)
{
	return wfx_is_ht(ht_info) &&
	       (ht_info->ht_cap.cap & IEEE80211_HT_CAP_SGI_20);
}

static inline int wfx_ht_ampdu_density(const struct wfx_ht_info *ht_info)
{
	if (!wfx_is_ht(ht_info))
		return 0;
	return ht_info->ht_cap.ampdu_density;
}

#endif /* WFX_H */
