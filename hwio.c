/*
 * Low-level device IO routines for Silicon Labs WFX drivers
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

#include "hwio.h"
#include "wfx.h"
#include "hwbus.h"
#include "traces.h"

/*
 * Internal helpers
 */

static int read32(struct wfx_dev *wdev, int reg, u32 *val)
{
	__le32 tmp;
	int ret;

	ret = wdev->hwbus_ops->copy_from_io(wdev->hwbus_priv, reg, &tmp, sizeof(u32));
	if (ret >= 0)
		*val = le32_to_cpu(tmp);
	else
		*val = ~0; // Never return undefined value
	return ret;
}

static int write32(struct wfx_dev *wdev, int reg, u32 val)
{
	__le32 tmp = cpu_to_le32(val);

	return wdev->hwbus_ops->copy_to_io(wdev->hwbus_priv, reg, &tmp, sizeof(u32));
}

static int read32_locked(struct wfx_dev *wdev, int reg, u32 *val)
{
	int ret;

	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = read32(wdev, reg, val);
	_trace_io_read32(reg, *val);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

static int write32_locked(struct wfx_dev *wdev, int reg, u32 val)
{
	int ret;

	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = write32(wdev, reg, val);
	_trace_io_write32(reg, val);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

static int write32_bits_locked(struct wfx_dev *wdev, int reg, u32 mask, u32 val)
{
	int ret;
	u32 val_r, val_w;

	WARN_ON(~mask & val);
	val &= mask;
	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = read32(wdev, reg, &val_r);
	_trace_io_read32(reg, val_r);
	if (ret < 0)
		goto err;
	val_w = (val_r & ~mask) | val;
	if (val_w != val_r) {
		ret = write32(wdev, reg, val_w);
		_trace_io_write32(reg, val_w);
	}
err:
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

static int indirect_read(struct wfx_dev *wdev, int reg, u32 addr, void *buf, size_t len)
{
	int ret;
	int i;
	u32 cfg;
	u32 prefetch;

	WARN_ON(len >= 0x2000);
	WARN_ON(reg != WFX_REG_AHB_DPORT && reg != WFX_REG_SRAM_DPORT);

	if (reg == WFX_REG_AHB_DPORT)
		prefetch = CFG_PREFETCH_AHB;
	else if (reg == WFX_REG_SRAM_DPORT)
		prefetch = CFG_PREFETCH_SRAM;
	else
		return -ENODEV;

	ret = write32(wdev, WFX_REG_BASE_ADDR, addr);
	if (ret < 0)
		goto err;

	ret = read32(wdev, WFX_REG_CONFIG, &cfg);
	if (ret < 0)
		goto err;

	ret = write32(wdev, WFX_REG_CONFIG, cfg | prefetch);
	if (ret < 0)
		goto err;

	for (i = 0; i < 20; i++) {
		ret = read32(wdev, WFX_REG_CONFIG, &cfg);
		if (ret < 0)
			goto err;
		if (!(cfg & prefetch))
			break;
		udelay(200);
	}
	if (i == 20) {
		ret = -ETIMEDOUT;
		goto err;
	}

	ret = wdev->hwbus_ops->copy_from_io(wdev->hwbus_priv, reg, buf, len);

err:
	if (ret < 0)
		memset(buf, 0xFF, len); // Never return undefined value
	return ret;
}

static int indirect_write(struct wfx_dev *wdev, int reg, u32 addr, const void *buf, size_t len)
{
	int ret;

	WARN_ON(len >= 0x2000);
	WARN_ON(reg != WFX_REG_AHB_DPORT && reg != WFX_REG_SRAM_DPORT);
	ret = write32(wdev, WFX_REG_BASE_ADDR, addr);
	if (ret < 0)
		return ret;

	return wdev->hwbus_ops->copy_to_io(wdev->hwbus_priv, reg, buf, len);
}

static int indirect_read_locked(struct wfx_dev *wdev, int reg, u32 addr, void *buf, size_t len)
{
	int ret;

	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = indirect_read(wdev, reg, addr, buf, len);
	_trace_io_ind_read(reg, addr, buf, len);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

static int indirect_write_locked(struct wfx_dev *wdev, int reg, u32 addr, const void *buf, size_t len)
{
	int ret;

	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = indirect_write(wdev, reg, addr, buf, len);
	_trace_io_ind_write(reg, addr, buf, len);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

static int indirect_read32_locked(struct wfx_dev *wdev, int reg, u32 addr, u32 *val)
{
	__le32 tmp;
	int ret;

	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = indirect_read(wdev, reg, addr, &tmp, sizeof(u32));
	*val = cpu_to_le32(tmp);
	_trace_io_ind_read32(reg, addr, *val);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

static int indirect_write32_locked(struct wfx_dev *wdev, int reg, u32 addr, u32 val)
{
	__le32 tmp = cpu_to_le32(val);
	int ret;

	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	ret = indirect_write(wdev, reg, addr, &tmp, sizeof(u32));
	_trace_io_ind_write32(reg, addr, val);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

int wfx_data_read(struct wfx_dev *wdev, void *buf, size_t len)
{
	int ret, i;

	WARN((long) buf & 3, "%s: unaligned buffer", __func__);
	//WARN(len > MAX_SZ_RD_WR_BUFFERS, "%s: request exceed WFx capability", __func__);
	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	for (i = 0; i < 3; i++) {
		ret = wdev->hwbus_ops->copy_from_io(wdev->hwbus_priv, WFX_REG_IN_OUT_QUEUE, buf, len);
		if (!ret)
			break;
		mdelay(1);
	}
	if (i == 3) {
		ret = -ETIMEDOUT;
		memset(buf, 0xFF, len); // Never return undefined value
	} else if (i > 0) {
		dev_info(wdev->pdev, "success read after %d failures\n", i);
	}
	_trace_io_read(WFX_REG_IN_OUT_QUEUE, buf, len);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

int wfx_data_write(struct wfx_dev *wdev, const void *buf, size_t len)
{
	int ret, i;

	WARN((long) buf & 3, "%s: unaligned buffer", __func__);
	//WARN(len > MAX_SZ_RD_WR_BUFFERS, "%s: request exceed WFx capability", __func__);
	wdev->hwbus_ops->lock(wdev->hwbus_priv);
	for (i = 0; i < 3; i++) {
		ret = wdev->hwbus_ops->copy_to_io(wdev->hwbus_priv, WFX_REG_IN_OUT_QUEUE, buf, len);
		if (!ret)
			break;
		mdelay(1);
	}
	if (i == 3)
		ret = -ETIMEDOUT;
	else if (i > 0)
		dev_info(wdev->pdev, "success write after %d failures\n", i);
	_trace_io_write(WFX_REG_IN_OUT_QUEUE, buf, len);
	wdev->hwbus_ops->unlock(wdev->hwbus_priv);
	return ret;
}

int sram_buf_read(struct wfx_dev *wdev, u32 addr, void *buf, size_t len)
{
	return indirect_read_locked(wdev, WFX_REG_SRAM_DPORT, addr, buf, len);
}

int ahb_buf_read(struct wfx_dev *wdev, u32 addr, void *buf, size_t len)
{
	return indirect_read_locked(wdev, WFX_REG_AHB_DPORT, addr, buf, len);
}

int sram_buf_write(struct wfx_dev *wdev, u32 addr, const void *buf, size_t len)
{
	return indirect_write_locked(wdev, WFX_REG_SRAM_DPORT, addr, buf, len);
}

int ahb_buf_write(struct wfx_dev *wdev, u32 addr, const void *buf, size_t len)
{
	return indirect_write_locked(wdev, WFX_REG_AHB_DPORT, addr, buf, len);
}

int sram_reg_read(struct wfx_dev *wdev, u32 addr, u32 *val)
{
	return indirect_read32_locked(wdev, WFX_REG_SRAM_DPORT, addr, val);
}

int ahb_reg_read(struct wfx_dev *wdev, u32 addr, u32 *val)
{
	return indirect_read32_locked(wdev, WFX_REG_AHB_DPORT, addr, val);
}

int sram_reg_write(struct wfx_dev *wdev, u32 addr, u32 val)
{
	return indirect_write32_locked(wdev, WFX_REG_SRAM_DPORT, addr, val);
}

int ahb_reg_write(struct wfx_dev *wdev, u32 addr, u32 val)
{
	return indirect_write32_locked(wdev, WFX_REG_AHB_DPORT, addr, val);
}

int config_reg_read(struct wfx_dev *wdev, u32 *val)
{
	return read32_locked(wdev, WFX_REG_CONFIG, val);
}

int config_reg_write(struct wfx_dev *wdev, u32 val)
{
	return write32_locked(wdev, WFX_REG_CONFIG, val);
}

int config_reg_write_bits(struct wfx_dev *wdev, u32 mask, u32 val)
{
	return write32_bits_locked(wdev, WFX_REG_CONFIG, mask, val);
}

int control_reg_read(struct wfx_dev *wdev, u32 *val)
{
	return read32_locked(wdev, WFX_REG_CONTROL, val);
}

int control_reg_write(struct wfx_dev *wdev, u32 val)
{
	return write32_locked(wdev, WFX_REG_CONTROL, val);
}

int control_reg_write_bits(struct wfx_dev *wdev, u32 mask, u32 val)
{
	return write32_bits_locked(wdev, WFX_REG_CONTROL, mask, val);
}

int igpr_reg_read(struct wfx_dev *wdev, u32 *val)
{
	return read32_locked(wdev, WFX_REG_SET_GEN_R_W, val);
}

int igpr_reg_write(struct wfx_dev *wdev, u32 val)
{
	return write32_locked(wdev, WFX_REG_SET_GEN_R_W, val);
}

