/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PANELS_H__
#define __PANELS_H__

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>
#include <drm/drm_crtc.h>
#include <drm/drm_panel.h>

//#include "de_dsi.h"

#define PANEL_POWER_MAX 10
#define PANEL_GPIO_MAX 10

#define LCD_PIN_STATE_ACTIVE "active"
#define LCD_PIN_STATE_SLEEP "sleep"

struct sunxi_panel_delay {
	unsigned int prepare_time;
	unsigned int enable_time;
	unsigned int disable_time;
	unsigned int unprepare_time;
};

struct sunxi_panel {
	struct drm_panel panel;
	struct device *dev;
	struct videomode video_mode;
	unsigned int num_modes;
	struct display_timing timings;
	unsigned int num_timings;
//	struct disp_panel_para panel_para;
//	struct panel_extend_para extend_para;
	struct sunxi_panel_delay delay;

	struct regulator *supply[PANEL_POWER_MAX];

	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *general_gpio[PANEL_GPIO_MAX];
	//	TODO use struct mipi_dsi_device
	struct device *dsi;
};

static inline struct sunxi_panel *to_sunxi_panel(struct drm_panel *panel)
{
	return container_of(panel, struct sunxi_panel, panel);
}

int sunxi_panel_sw_enable(struct drm_panel *panel);
s32 sunxi_panel_parse_timings(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_parse_delay_time(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_parse_dsi(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_parse_lvds(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_parse_hv(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_parse_misc(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_get_power(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_get_enable_gpio(struct sunxi_panel *sunxi_panel);
s32 sunxi_panel_get_reset_gpio(struct sunxi_panel *sunxi_panel);
void sunxi_panel_set_gpio_output_value(struct gpio_desc *gpio,
				       unsigned int value);
void sunxi_panel_enable_gpio(struct sunxi_panel *sunxi_panel);
void sunxi_panel_disable_gpio(struct sunxi_panel *sunxi_panel);
void sunxi_panel_reset_assert(struct sunxi_panel *sunxi_panel);
void sunxi_panel_reset_deassert(struct sunxi_panel *sunxi_panel);

#endif
