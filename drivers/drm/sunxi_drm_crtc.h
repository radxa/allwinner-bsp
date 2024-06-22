/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (C) 2023 Allwinnertech Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _SUNXI_DRM_CRTC_H_
#define _SUNXI_DRM_CRTC_H_

#include <drm/drm_crtc.h>
#include "sunxi_device/hardware/lowlevel_de/sunxi_de.h"

typedef void (*vblank_enable_callback_t)(bool, void *);
typedef bool (*fifo_status_check_callback_t)(void *);
typedef bool (*is_sync_time_enough_callback_t)(void *);

struct sunxi_drm_crtc;

struct de_out_exconfig {
	struct drm_color_lut *gamma_lut;
	unsigned int brightness, contrast, saturation, hue;
};

struct sunxi_crtc_state {
	struct drm_crtc_state base;
	enum de_format_space px_fmt_space;
	enum de_yuv_sampling yuv_sampling;
	enum de_eotf eotf;
	enum de_color_space color_space;
	enum de_color_range color_range;
	enum de_data_bits data_bits;
	unsigned int tcon_id;
	unsigned long clk_freq;
	struct de_out_exconfig excfg;
	bool bcsh_changed;
	vblank_enable_callback_t enable_vblank;
	fifo_status_check_callback_t check_status;
	is_sync_time_enough_callback_t is_sync_time_enough;
	void *output_dev_data;
	struct sunxi_drm_wb *wb;
};

#define to_sunxi_crtc_state(x) container_of(x, struct sunxi_crtc_state, base)

irqreturn_t sunxi_crtc_event_proc(int irq, void *crtc);
int sunxi_drm_crtc_get_hw_id(struct drm_crtc *crtc);
void sunxi_plane_print_state(struct drm_printer *p,
				   const struct drm_plane_state *state, bool state_only);

int sunxi_fbdev_plane_update(struct drm_device *dev, unsigned int de_id,
				    unsigned int channel_id, struct display_channel_state *fake_state);

void sunxi_drm_crtc_wait_one_vblank(struct sunxi_drm_crtc *scrtc);

#endif
