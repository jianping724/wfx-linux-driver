// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of chip-to-host event (aka indications) of WFxxx Split Mac
 * (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/skbuff.h>
#include <linux/etherdevice.h>

#include "wsm_rx.h"
#include "wsm_mib.h"
#include "wfx.h"
#include "bh.h"
#include "data_rx.h"
#include "secure_link.h"
#include "sta.h"

static int wsm_generic_confirm(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	// All confirm messages start with status
	int status = le32_to_cpu(*((__le32 *) buf));
	int cmd = hdr->id;
	int len = hdr->len - 4; // drop header

	WARN(!mutex_is_locked(&wdev->wsm_cmd.lock), "data locking error");

	if (!wdev->wsm_cmd.buf_send) {
		dev_warn(wdev->dev, "Unexpected confirmation: 0x%.2x\n", cmd);
		return -EINVAL;
	}

	if (cmd != wdev->wsm_cmd.buf_send->id) {
		dev_warn(wdev->dev, "Chip response mismatch request: 0x%.2x vs 0x%.2x\n",
			 cmd, wdev->wsm_cmd.buf_send->id);
		return -EINVAL;
	}

	if (wdev->wsm_cmd.buf_recv) {
		if (wdev->wsm_cmd.len_recv >= len)
			memcpy(wdev->wsm_cmd.buf_recv, buf, len);
		else
			status = -ENOMEM;
	}
	wdev->wsm_cmd.ret = status;

	if (!wdev->wsm_cmd.async) {
		complete(&wdev->wsm_cmd.done);
	} else {
		wdev->wsm_cmd.buf_send = NULL;
		mutex_unlock(&wdev->wsm_cmd.lock);
		if (cmd != HI_SL_EXCHANGE_PUB_KEYS_REQ_ID)
			mutex_unlock(&wdev->wsm_cmd.key_renew_lock);
	}
	return status;
}

static int wsm_tx_confirm(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct hif_cnf_tx *body = buf;
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);

	WARN_ON(!wvif);
	if (!wvif)
		return -EFAULT;

	wfx_tx_confirm_cb(wvif, body);
	return 0;
}

static int wsm_multi_tx_confirm(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct hif_cnf_multi_transmit *body = buf;
	struct hif_cnf_tx *buf_loc = (struct hif_cnf_tx *) &body->tx_conf_payload;
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);
	int count = body->num_tx_confs;
	int i;

	WARN(count <= 0, "Corrupted message");
	WARN_ON(!wvif);
	if (!wvif)
		return -EFAULT;

	for (i = 0; i < count; ++i) {
		wfx_tx_confirm_cb(wvif, buf_loc);
		buf_loc++;
	}
	return 0;
}

static int wsm_startup_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct hif_ind_startup *body = buf;

	if (body->status || body->firmware_type > 4) {
		dev_err(wdev->dev, "Received invalid startup indication");
		return -EINVAL;
	}
	memcpy(&wdev->wsm_caps, body, sizeof(struct hif_ind_startup));
	le32_to_cpus(&wdev->wsm_caps.status);
	le16_to_cpus(&wdev->wsm_caps.hardware_id);
	le16_to_cpus(&wdev->wsm_caps.num_inp_ch_bufs);
	le16_to_cpus(&wdev->wsm_caps.size_inp_ch_buf);

	complete(&wdev->firmware_ready);
	return 0;
}

static int wsm_wakeup_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	if (!wdev->pdata.gpio_wakeup
	    || !gpiod_get_value(wdev->pdata.gpio_wakeup)) {
		dev_warn(wdev->dev, "unexpected wake-up indication\n");
		return -EIO;
	}
	return 0;
}

static int wsm_keys_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct hif_ind_sl_exchange_pub_keys *body = buf;

	// Compatibility with legacy secure link
	if (body->status == SL_PUB_KEY_EXCHANGE_STATUS_SUCCESS)
		body->status = 0;
	if (body->status)
		dev_warn(wdev->dev, "secure link negociation error\n");
	wfx_sl_check_pubkey(wdev, body->ncp_pub_key, body->ncp_pub_key_mac);
	return 0;
}

static int wsm_receive_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf, struct sk_buff *skb)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);
	struct hif_ind_rx *body = buf;

	if (!wvif) {
		dev_warn(wdev->dev, "ignore rx data for non existant vif %d\n", hdr->interface);
		return 0;
	}
	skb_pull(skb, sizeof(struct hif_msg) + sizeof(struct hif_ind_rx));
	wfx_rx_cb(wvif, body, skb);

	return 0;
}

static int wsm_event_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);
	struct hif_ind_event *body = buf;
	struct wfx_wsm_event *event;
	int first;

	WARN_ON(!wvif);
	if (!wvif)
		return 0;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	memcpy(&event->evt, body, sizeof(struct hif_ind_event));
	spin_lock(&wvif->event_queue_lock);
	first = list_empty(&wvif->event_queue);
	list_add_tail(&event->link, &wvif->event_queue);
	spin_unlock(&wvif->event_queue_lock);

	if (first)
		schedule_work(&wvif->event_handler_work);

	return 0;
}

static int wsm_pm_mode_complete_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);

	WARN_ON(!wvif);
	complete(&wvif->set_pm_mode_complete);

	return 0;
}

static int wsm_scan_complete_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);
	struct hif_ind_scan_cmpl *body = buf;

	WARN_ON(!wvif);
	wfx_scan_complete_cb(wvif, body);

	return 0;
}

static int wsm_join_complete_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);

	WARN_ON(!wvif);
	dev_warn(wdev->dev, "unattended JoinCompleteInd\n");

	return 0;
}

static int wsm_suspend_resume_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct wfx_vif *wvif = wdev_to_wvif(wdev, hdr->interface);
	struct hif_ind_suspend_resume_tx *body = buf;

	WARN_ON(!wvif);
	wfx_suspend_resume(wvif, body);

	return 0;
}

static int wsm_error_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct hif_ind_error *body = buf;
	u8 *pRollback = (u8 *) body->data;
	u32 *pStatus = (u32 *) body->data;

	switch (body->type) {
	case  WSM_HI_ERROR_FIRMWARE_ROLLBACK:
		dev_err(wdev->dev, "asynchronous error: firmware rollback error %d\n", *pRollback);
		break;
	case  WSM_HI_ERROR_FIRMWARE_DEBUG_ENABLED:
		dev_err(wdev->dev, "asynchronous error: firmware debug feature enabled\n");
		break;
	case  WSM_HI_ERROR_OUTDATED_SESSION_KEY:
		dev_err(wdev->dev, "asynchronous error: secure link outdated key: %#.8x\n", *pStatus);
		break;
	case WSM_HI_ERROR_INVALID_SESSION_KEY:
		dev_err(wdev->dev, "asynchronous error: invalid session key\n");
		break;
	case  WSM_HI_ERROR_OOR_VOLTAGE:
		dev_err(wdev->dev, "asynchronous error: out-of-range overvoltage: %#.8x\n", *pStatus);
		break;
	case  WSM_HI_ERROR_PDS_VERSION:
		dev_err(wdev->dev, "asynchronous error: wrong PDS payload or version: %#.8x\n", *pStatus);
		break;
	default:
		dev_err(wdev->dev, "asynchronous error: unknown (%d)\n", body->type);
		break;
	}
	return 0;
}

static int wsm_generic_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	struct hif_ind_generic *body = buf;

	switch (body->indication_type) {
	case  HI_GENERIC_INDICATION_TYPE_RAW:
		return 0;
	case HI_GENERIC_INDICATION_TYPE_STRING:
		dev_info(wdev->dev, "firmware says: %s", (char *) body->indication_data.raw_data);
		return 0;
	case HI_GENERIC_INDICATION_TYPE_RX_STATS:
		mutex_lock(&wdev->rx_stats_lock);
		// Older firmware send a generic indication beside RxStats
		if (!wfx_api_older_than(wdev, 1, 4))
			dev_info(wdev->dev, "RX test ongoing. Temperature: %d°C\n", body->indication_data.rx_stats.current_temp);
		memcpy(&wdev->rx_stats, &body->indication_data.rx_stats, sizeof(wdev->rx_stats));
		mutex_unlock(&wdev->rx_stats_lock);
		return 0;
	default:
		dev_err(wdev->dev, "generic_indication: unknown indication type: %#.8x\n", body->indication_type);
		return -EIO;
	}
}

static int wsm_exception_indication(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf)
{
	size_t len = hdr->len - 4; // drop header
	dev_err(wdev->dev, "Firmware exception.\n");
	print_hex_dump_bytes("Dump: ", DUMP_PREFIX_NONE, buf, len);
	wdev->chip_frozen = 1;

	return -1;
}

static const struct {
	int msg_id;
	int (*handler)(struct wfx_dev *wdev, struct hif_msg *hdr, void *buf);
} wsm_handlers[] = {
	/* Confirmations */
	{ WSM_HI_TX_CNF_ID,              wsm_tx_confirm },
	{ WSM_HI_MULTI_TRANSMIT_CNF_ID,  wsm_multi_tx_confirm },
	/* Indications */
	{ WSM_HI_EVENT_IND_ID,           wsm_event_indication },
	{ WSM_HI_SET_PM_MODE_CMPL_IND_ID, wsm_pm_mode_complete_indication },
	{ WSM_HI_JOIN_COMPLETE_IND_ID,   wsm_join_complete_indication },
	{ WSM_HI_SCAN_CMPL_IND_ID,       wsm_scan_complete_indication },
	{ WSM_HI_SUSPEND_RESUME_TX_IND_ID, wsm_suspend_resume_indication },
	{ HI_ERROR_IND_ID,               wsm_error_indication },
	{ HI_STARTUP_IND_ID,             wsm_startup_indication },
	{ HI_WAKEUP_IND_ID,              wsm_wakeup_indication },
	{ HI_GENERIC_IND_ID,             wsm_generic_indication },
	{ HI_EXCEPTION_IND_ID,           wsm_exception_indication },
	{ HI_SL_EXCHANGE_PUB_KEYS_IND_ID, wsm_keys_indication },
	// FIXME: allocate skb_p from wsm_receive_indication and make it generic
	//{ WSM_HI_RX_IND_ID,            wsm_receive_indication },
};

void wsm_handle_rx(struct wfx_dev *wdev, struct sk_buff *skb)
{
	int i;
	struct hif_msg *wsm = (struct hif_msg *) skb->data;
	int wsm_id = wsm->id;

	if (wsm_id == WSM_HI_RX_IND_ID) {
		// wsm_receive_indication take care of skb lifetime
		wsm_receive_indication(wdev, wsm, wsm->body, skb);
		return;
	}
	// Note: mutex_is_lock cause an implicit memory barrier that protect
	// buf_send
	if (mutex_is_locked(&wdev->wsm_cmd.lock)
	    && wdev->wsm_cmd.buf_send && wdev->wsm_cmd.buf_send->id == wsm_id) {
		wsm_generic_confirm(wdev, wsm, wsm->body);
		goto free;
	}
	for (i = 0; i < ARRAY_SIZE(wsm_handlers); i++) {
		if (wsm_handlers[i].msg_id == wsm_id) {
			if (wsm_handlers[i].handler)
				wsm_handlers[i].handler(wdev, wsm, wsm->body);
			goto free;
		}
	}
	dev_err(wdev->dev, "Unsupported WSM ID %02x\n", wsm_id);
free:
	dev_kfree_skb(skb);
}

