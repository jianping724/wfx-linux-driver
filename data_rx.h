/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_DATA_RX_H
#define WFX_DATA_RX_H

#include "api_wsm_cmd.h"

struct wfx_vif;
struct sk_buff;

void wfx_rx_cb(struct wfx_vif *wvif, WsmHiRxIndBody_t *arg, struct sk_buff *skb);

#endif /* WFX_DATA_RX_H */
