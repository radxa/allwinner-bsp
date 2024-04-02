/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/* sunxi_tcon.h
 *
 * Copyright (C) 2023 Allwinnertech Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _SUNXI_TCON_H_
#define _SUNXI_TCON_H_

#include "include.h"
#include "disp_al_tcon.h"

enum tcon_type {
	TCON_LCD = 0,
	TCON_TV = 1,
};

enum tcon_interface_type {
	INTERFACE_LCD = 0,
	INTERFACE_HDMI = 1,
	INTERFACE_TVE = 2,
	INTERFACE_EDP = 3,
	INTERFACE_INVALID,
};

struct lcd_clk_info {
	enum disp_lcd_if lcd_if;
	int tcon_div;
	int lcd_div;
	int dsi_div;
	int dsi_rate;
};

struct disp_output_config {
	unsigned int de_id;
	bool sw_enable;
	struct disp_panel_para panel;
	struct panel_extend_para panel_ext;
	struct disp_video_timings timing;
	enum disp_csc_type format;
	irq_handler_t irq_handler;
	void *irq_data;
	void *private_data;
};

struct tcon_device {
	struct device *dev;
	struct drm_device *drm;
	unsigned int hw_id;
	struct disp_output_config cfg;
};

enum tcon_builin_pattern {
	PATTERN_DE = 0,
	PATTERN_COLORBAR,
	PATTERN_GRAYSCALE,
	PATTERN_BLACK_WHITE,
	PATTERN_ALL0,
	PATTERN_ALL1,
	PATTERN_RESERVED,
	PATTERN_GRIDDING,
};


int sunxi_tcon_tcon0_open(struct tcon_device *tcon);
int sunxi_tcon_tcon0_close(struct tcon_device *tcon);
int sunxi_tcon_lvds_open(struct tcon_device *tcon);
int sunxi_tcon_lvds_close(struct tcon_device *tcon);
int sunxi_tcon_lcd_mode_init(struct tcon_device *tcon);
int sunxi_tcon_lcd_mode_exit(struct tcon_device *tcon);
int sunxi_tcon_hdmi_mode_init(struct tcon_device *tcon);
int sunxi_tcon_hdmi_mode_exit(struct tcon_device *tcon);
int sunxi_tcon_get_lcd_clk_info(struct lcd_clk_info *info,
				const struct disp_panel_para *panel);

int sunxi_tcon_show_pattern(struct tcon_device *tcon_dev, int pattern);
int sunxi_tcon_pattern_get(struct tcon_device *tcon_dev);
int sunxi_tcon_edp_mode_init(struct tcon_device *tcon_dev);
int sunxi_tcon_edp_mode_exit(struct tcon_device *tcon_dev);

#endif
