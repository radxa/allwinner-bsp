/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drv_edp2.h
 *
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef __DRV_EDP_H__
#define __DRV_EDP_H__

#include "sunxi_device/sunxi_edp.h"
#include <linux/module.h>
#include <drm/drm_edid.h>
#include <linux/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include <asm-generic/int-ll64.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/compat.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_property.h>

#include "sunxi_device/sunxi_tcon.h"
#include "sunxi_drm_intf.h"
#include "panel/panels.h"
#include "sunxi_drm_crtc.h"

#if IS_ENABLED(CONFIG_EXTCON)
#include <linux/extcon.h>
#endif

#define CEA_BASIC_AUDIO_MASK     (1 << 6) /* Version3 */
#define CEA_YCC444_MASK	         (1 << 5) /* Version3 */
#define CEA_YCC422_MASK	         (1 << 4) /* Version3 */

struct edp_debug {
	unsigned long aux_i2c_addr;
	unsigned long aux_i2c_len;
	unsigned long aux_read_start;
	unsigned long aux_read_end;
	u32 aux_write_start;
	u32 aux_write_len;
	u32 aux_write_val[16];
	u32 aux_write_val_before[16];
	u32 lane_debug_en;
	u32 hpd_mask;
	u32 hpd_mask_pre;

	/* resource lock, power&clk won't release when edp enable fail if not 0 */
	u32 edp_res_lock;

	/* bypass training for some signal test case */
	u32 bypass_training;
};

struct sunxi_drm_edp {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_device *drm_dev;
	struct drm_display_mode mode;
	unsigned int tcon_id;
	struct drm_panel *panel;
	struct task_struct *edp_task;
	bool bound;

	u32 enable;
	u32 irq;
	void __iomem *base_addr;
	dev_t devid;
	struct cdev *edp_cdev;
	struct class *edp_class;
	struct device *edp_class_dev;
	struct device *dev;
	struct device *tcon_dev;
	struct phy *dp_phy;
	struct phy *aux_phy;
	struct phy *combo_phy;
	struct clk *clk_bus;
	struct clk *clk;
	struct clk *clk_24m;
	struct regulator *vdd_regulator;
	struct regulator *vcc_regulator;
	struct reset_control *rst_bus;
	struct sunxi_edp_output_desc *desc;

	/* drm property */
	struct drm_property *colorspace_property;

	bool hpd_state;
	bool hpd_state_now;
	bool dpcd_parsed;
	bool use_dpcd;
	/*FIXME:TODO: optimize relate code*/
	bool fps_limit_60;
	/*end FIXME*/
	bool use_debug_para;
	bool is_enabled;

	bool sw_enable;
	bool allow_sw_enable;

	struct edp_tx_core edp_core;
	struct sunxi_edp_hw_desc edp_hw;
	struct edp_tx_cap source_cap;
	struct edp_rx_cap sink_cap;
	struct edp_debug edp_debug;
	struct sunxi_dp_hdcp hdcp;
#if IS_ENABLED(CONFIG_EXTCON)
	struct extcon_dev *extcon_edp;
#endif
};

struct sunxi_edp_connector_state {
	struct drm_connector_state state;
	int color_format;
	int color_depth;
	int lane_cnt;
	int lane_rate;
};

#define to_drm_edp_connector_state(s) container_of(s, struct sunxi_edp_connector_state, state)

struct sunxi_edp_output_desc {
	int connector_type;
	s32 (*bind)(struct sunxi_drm_edp *drm_edp);
	s32 (*unbind)(struct sunxi_drm_edp *drm_edp);
	s32 (*enable_early)(struct sunxi_drm_edp *drm_edp);
	s32 (*enable)(struct sunxi_drm_edp *drm_edp);
	s32 (*disable)(struct sunxi_drm_edp *drm_edp);
	s32 (*plugin)(struct sunxi_drm_edp *drm_edp);
	s32 (*plugout)(struct sunxi_drm_edp *drm_edp);
	s32 (*runtime_suspend)(struct sunxi_drm_edp *drm_edp);
	s32 (*runtime_resume)(struct sunxi_drm_edp *drm_edp);
	s32 (*suspend)(struct sunxi_drm_edp *drm_edp);
	s32 (*resume)(struct sunxi_drm_edp *drm_edp);
	void (*soft_reset)(struct sunxi_drm_edp *drm_edp);
};

#endif /*End of file*/
