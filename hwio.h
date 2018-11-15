/*
 * Low-level API for mac80211 Silicon Labs WFX drivers
 *
 * Copyright (c) 2017, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
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

#ifndef WFX_HWIO_H
#define WFX_HWIO_H

#include <linux/types.h>

struct wfx_dev;

int wfx_data_read(struct wfx_dev *wdev, void *buf, size_t buf_len);
int wfx_data_write(struct wfx_dev *wdev, const void *buf, size_t buf_len);

int sram_buf_read(struct wfx_dev *wdev, u32 addr, void *buf, size_t len);
int sram_buf_write(struct wfx_dev *wdev, u32 addr, const void *buf, size_t len);

int ahb_buf_read(struct wfx_dev *wdev, u32 addr, void *buf, size_t len);
int ahb_buf_write(struct wfx_dev *wdev, u32 addr, const void *buf, size_t len);

int sram_reg_read(struct wfx_dev *wdev, u32 addr, u32 *val);
int sram_reg_write(struct wfx_dev *wdev, u32 addr, u32 val);

int ahb_reg_read(struct wfx_dev *wdev, u32 addr, u32 *val);
int ahb_reg_write(struct wfx_dev *wdev, u32 addr, u32 val);

#define CFG_ERR_SPI_FRAME          0x00000001 // only with SPI
#define CFG_ERR_BUF_MISMATCH       0x00000001 // only with SDIO
#define CFG_ERR_BUF_UNDERRUN       0x00000002
#define CFG_ERR_DATA_IN_TOO_LARGE  0x00000004
#define CFG_ERR_HOST_NO_OUT_QUEUE  0x00000008
#define CFG_ERR_BUF_OVERRUN        0x00000010
#define CFG_ERR_DATA_OUT_TOO_LARGE 0x00000020
#define CFG_ERR_HOST_NO_IN_QUEUE   0x00000040
#define CFG_ERR_HOST_CRC_MISS      0x00000080 // only with SDIO
#define CFG_SPI_IGNORE_CS          0x00000080 // only with SPI
#define CFG_SPI_WORD_MODE_MASK     0x00000300 // only with SPI. Bytes ordering:
#define CFG_SPI_WORD_MODE0         0x00000000 //   B1,B0,B3,B2
#define CFG_SPI_WORD_MODE1         0x00000100 //   B3,B2,B1,B0
#define CFG_SPI_WORD_MODE2         0x00000200 //   B0,B1,B2,B3
#define CFG_DIRECT_ACCESS_MODE     0x00000400 // Direct or queue access mode
#define CFG_PREFETCH_AHB           0x00000800
#define CFG_DISABLE_CPU_CLK        0x00001000
#define CFG_PREFETCH_SRAM          0x00002000
#define CFG_CPU_RESET              0x00004000
#define CFG_SDIO_RESERVED1         0x00008000 // only with SDIO
#define CFG_IRQ_ENABLE_DATA        0x00010000
#define CFG_IRQ_ENABLE_WRDY        0x00020000
#define CFG_CLK_RISE_EDGE          0x00040000 // only with SDIO?
#define CFG_SDIO_RESERVED0         0x00080000 // only with SDIO
#define CFG_RESERVED               0x00F00000
#define CFG_DEVICE_ID_MAJOR        0x07000000
#define CFG_DEVICE_ID_RESERVED     0x78000000
#define CFG_DEVICE_ID_TYPE         0x80000000
int config_reg_read(struct wfx_dev *wdev, u32 *val);
int config_reg_write(struct wfx_dev *wdev, u32 val);
int config_reg_write_bits(struct wfx_dev *wdev, u32 mask, u32 val);

#define CTRL_NEXT_LEN_MASK   0x00000FFF
#define CTRL_WLAN_WAKEUP     0x00001000
#define CTRL_WLAN_READY      0x00002000
int control_reg_read(struct wfx_dev *wdev, u32 *val);
int control_reg_write(struct wfx_dev *wdev, u32 val);
int control_reg_write_bits(struct wfx_dev *wdev, u32 mask, u32 val);

int igpr_reg_read(struct wfx_dev *wdev, u32 *val);
int igpr_reg_write(struct wfx_dev *wdev, u32 val);

#endif /* WFX_HWIO_H */
