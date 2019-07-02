// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, Silicon Laboratories, Inc.
 */

#include <linux/random.h>
#include <crypto/sha.h>
#include <mbedtls/md.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ccm.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

#include "wfx.h"
#include "wsm_rx.h"
#include "secure_link.h"

static int mbedtls_get_random_bytes(void *data, unsigned char *output, size_t len)
{
	get_random_bytes(output, len);

	return 0;
}

static void reverse_bytes(uint8_t *src, uint8_t length)
{
	uint8_t *lo = src;
	uint8_t *hi = src + length - 1;
	uint8_t swap;

	while (lo < hi) {
		swap = *lo;
		*lo++ = *hi;
		*hi-- = swap;
	}
}

int wfx_is_secure_command(struct wfx_dev *wdev, int cmd_id)
{
	return test_bit(cmd_id, wdev->sl_commands);
}

static void wfx_sl_init_cfg(struct wfx_dev *wdev)
{
	DECLARE_BITMAP(sl_commands, 256);

	bitmap_fill(sl_commands, 256);
	clear_bit(HI_SET_SL_MAC_KEY_REQ_ID, sl_commands);
	clear_bit(HI_SL_EXCHANGE_PUB_KEYS_REQ_ID, sl_commands);
	clear_bit(HI_SL_EXCHANGE_PUB_KEYS_IND_ID, sl_commands);
	clear_bit(HI_EXCEPTION_IND_ID, sl_commands);
	clear_bit(HI_ERROR_IND_ID, sl_commands);
	wsm_sl_config(wdev, sl_commands);
	bitmap_copy(wdev->sl_commands, sl_commands, 256);
}

static int wfx_sl_get_pubkey_mac(struct wfx_dev *wdev, uint8_t *pubkey, uint8_t *mac)
{
	return mbedtls_md_hmac(
			mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),
			wdev->pdata.sl_key, sizeof(wdev->pdata.sl_key),
			pubkey, API_HOST_PUB_KEY_SIZE,
			mac);
}

static int wfx_sl_key_exchange(struct wfx_dev *wdev)
{
	int ret;
	size_t olen;
	uint8_t host_pubmac[SHA512_DIGEST_SIZE];
	uint8_t host_pubkey[API_HOST_PUB_KEY_SIZE + 2];

	wdev->sl_rx_seqnum = 0;
	wdev->sl_tx_seqnum = 0;
	mbedtls_ecdh_init(&wdev->edch_ctxt);
	ret = mbedtls_ecdh_setup(&wdev->edch_ctxt, MBEDTLS_ECP_DP_CURVE25519);
	if (ret)
		goto err;
	wdev->edch_ctxt.point_format = MBEDTLS_ECP_PF_COMPRESSED;
	ret = mbedtls_ecdh_make_public(&wdev->edch_ctxt, &olen, host_pubkey,
			sizeof(host_pubkey), mbedtls_get_random_bytes, NULL);
	if (ret || olen != sizeof(host_pubkey))
		goto err;
	reverse_bytes(host_pubkey + 2, sizeof(host_pubkey) - 2);
	ret = wfx_sl_get_pubkey_mac(wdev, host_pubkey + 2, host_pubmac);
	if (ret)
		goto err;
	ret = wsm_send_pub_keys(wdev, host_pubkey + 2, host_pubmac);
	if (ret)
		goto err;
	if (!wait_for_completion_timeout(&wdev->sl_key_renew_done, msecs_to_jiffies(500)))
		goto err;
	if (!wdev->sl_enabled)
		goto err;

	mbedtls_ecdh_free(&wdev->edch_ctxt);
	return 0;
err:
	mbedtls_ecdh_free(&wdev->edch_ctxt);
	dev_err(wdev->dev, "key negociation error\n");
	return -EIO;
}

static void renew_key(struct work_struct *work)
{
	struct wfx_dev *wdev = container_of(work, struct wfx_dev, sl_key_renew_work);

	wsm_tx_lock_flush(wdev);
	mutex_lock(&wdev->wsm_cmd.key_renew_lock);
	wfx_sl_key_exchange(wdev);
	mutex_unlock(&wdev->wsm_cmd.key_renew_lock);
	wsm_tx_unlock(wdev);
}

int wfx_sl_init(struct wfx_dev *wdev)
{
	int link_mode = wdev->wsm_caps.Capabilities.LinkMode;

	INIT_WORK(&wdev->sl_key_renew_work, renew_key);
	init_completion(&wdev->sl_key_renew_done);
	if (!memzcmp(wdev->pdata.sl_key, sizeof(wdev->pdata.sl_key)))
		goto err;
	if (link_mode == SECURE_LINK_TRUSTED_ACTIVE_ENFORCED) {
		bitmap_set(wdev->sl_commands, HI_SL_CONFIGURE_REQ_ID, 1);
		if (wfx_sl_key_exchange(wdev))
			goto err;
		wfx_sl_init_cfg(wdev);
	} else if (link_mode == SECURE_LINK_TRUSTED_MODE) {
		if (wsm_set_mac_key(wdev, wdev->pdata.sl_key, SL_MAC_KEY_DEST_RAM))
			goto err;
		if (wfx_sl_key_exchange(wdev))
			goto err;
		wfx_sl_init_cfg(wdev);
	} else {
		dev_info(wdev->dev, "ignoring provided secure link key since chip does not support it\n");
	}
	return 0;

err:
	if (link_mode == SECURE_LINK_TRUSTED_ACTIVE_ENFORCED) {
		dev_err(wdev->dev, "chip require secure_link, but can't negociate it\n");
		return -EIO;
	}
	return 0;
}

void wfx_sl_deinit(struct wfx_dev *wdev)
{
	mbedtls_ccm_free(&wdev->ccm_ctxt);
}

int wfx_sl_check_ncp_keys(struct wfx_dev *wdev, uint8_t *ncp_pubkey, uint8_t *ncp_pubmac)
{
	int ret;
	size_t olen;
	uint8_t shared_secret[API_HOST_PUB_KEY_SIZE];
	uint8_t shared_secret_digest[SHA256_DIGEST_SIZE];
	uint8_t ncp_pubmac_computed[SHA512_DIGEST_SIZE];

	ret = wfx_sl_get_pubkey_mac(wdev, ncp_pubkey, ncp_pubmac_computed);
	if (ret)
		goto end;
	ret = memcmp(ncp_pubmac_computed, ncp_pubmac, sizeof(ncp_pubmac_computed));
	if (ret)
		goto end;

	// FIXME: save Y or (reset it), concat it with ncp_public_key and use mbedtls_ecdh_read_public.
	reverse_bytes(ncp_pubkey, API_NCP_PUB_KEY_SIZE);
	ret = mbedtls_mpi_read_binary(&wdev->edch_ctxt.Qp.X, ncp_pubkey, API_NCP_PUB_KEY_SIZE);
	if (ret)
		goto end;
	ret = mbedtls_mpi_lset(&wdev->edch_ctxt.Qp.Z, 1);
	if (ret)
		goto end;

	ret = mbedtls_ecdh_calc_secret(&wdev->edch_ctxt, &olen,
			shared_secret, sizeof(shared_secret),
			mbedtls_get_random_bytes, NULL);
	if (ret)
		goto end;

	reverse_bytes(shared_secret, sizeof(shared_secret));
	ret = mbedtls_sha256_ret(shared_secret, sizeof(shared_secret), shared_secret_digest, 0);
	if (ret)
		goto end;

	// Use the lower 16 bytes of the sha256 for session key
	ret = mbedtls_ccm_setkey(&wdev->ccm_ctxt, MBEDTLS_CIPHER_ID_AES,
			shared_secret_digest, 16 * BITS_PER_BYTE);

end:
	if (!ret)
		wdev->sl_enabled = true;
	return 0;
}

int wfx_sl_decode(struct wfx_dev *wdev, struct sl_wmsg *m)
{
	int ret;
	size_t clear_len = le16_to_cpu(m->len);
	size_t payload_len = round_up(clear_len - sizeof(m->len), 16);
	uint8_t *tag = m->payload + payload_len;
	uint8_t *output = (uint8_t *) m;
	uint32_t nonce[3] = { };

	WARN(m->encrypted != 0x02, "packet is not encrypted");

	// Other bytes of nonce are 0
	nonce[1] = m->seqnum;
	if (wdev->sl_rx_seqnum != m->seqnum)
		dev_warn(wdev->dev, "wrong encrypted message sequence: %d != %d\n",
				m->seqnum, wdev->sl_rx_seqnum);
	wdev->sl_rx_seqnum = m->seqnum + 1;
	if (wdev->sl_rx_seqnum == SECURE_LINK_NONCE_COUNTER_MAX)
		  schedule_work(&wdev->sl_key_renew_work);

	memcpy(output, &m->len, sizeof(m->len));
	ret = mbedtls_ccm_auth_decrypt(&wdev->ccm_ctxt, payload_len,
			(uint8_t *) nonce, sizeof(nonce), NULL, 0,
			m->payload, output + sizeof(m->len),
			tag, SECURE_LINK_CCM_TAG_LENGTH);
	if (ret) {
		dev_err(wdev->dev, "mbedtls error: %08x\n", ret);
		return -EIO;
	}
	if (memzcmp(output + clear_len, payload_len + sizeof(m->len) - clear_len))
		dev_warn(wdev->dev, "padding is not 0\n");
	return 0;
}

int wfx_sl_encode(struct wfx_dev *wdev, struct wmsg *input, struct sl_wmsg *output)
{
	int payload_len = round_up(input->len - sizeof(input->len), 16);
	uint8_t *tag = output->payload + payload_len;
	uint32_t nonce[3] = { };
	int ret;

	output->encrypted = 0x1;
	output->len = input->len;
	output->seqnum = wdev->sl_tx_seqnum;
	// Other bytes of nonce are 0
	nonce[2] = wdev->sl_tx_seqnum;
	wdev->sl_tx_seqnum++;
	if (wdev->sl_tx_seqnum == SECURE_LINK_NONCE_COUNTER_MAX)
		  schedule_work(&wdev->sl_key_renew_work);

	ret = mbedtls_ccm_encrypt_and_tag(&wdev->ccm_ctxt, payload_len,
			(uint8_t *) nonce, sizeof(nonce), NULL, 0,
			(uint8_t *) input + sizeof(input->len), output->payload,
			tag, SECURE_LINK_CCM_TAG_LENGTH);
	if (ret) {
		dev_err(wdev->dev, "mbedtls error: %08x\n", ret);
		return -EIO;
	}
	return 0;
}

