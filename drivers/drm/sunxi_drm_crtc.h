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

// bitmask of plane(DE channel) feature
enum {
	SUNXI_PLANE_FEATURE_AFBC = 1,
	SUNXI_PLANE_FEATURE_VEP  = 2,
};

typedef void (*vblank_enable_callback_t)(bool, void *);
struct sunxi_de_out;

struct sunxi_crtc_state {
	struct drm_crtc_state base;
	int color_fmt;
	int color_depth;
	int eotf;
	int color_space;
	unsigned int tcon_id;
	vblank_enable_callback_t enable_vblank;
	void *vblank_enable_data;
	irq_handler_t crtc_irq_handler;
};

struct sunxi_de_info {
	struct drm_device *drm;
	struct sunxi_de_out *de_out;
	struct device_node *port;
	unsigned int hw_id;
	unsigned int v_chn_cnt;
	unsigned int u_chn_cnt;
};

#define to_sunxi_crtc_state(x) container_of(x, struct sunxi_crtc_state, base)

struct sunxi_drm_crtc *sunxi_drm_crtc_init_one(struct sunxi_de_info *info);
void sunxi_drm_crtc_destory(struct sunxi_drm_crtc *scrtc);
void sunxi_drm_crtc_handle_vblank(void *data);
int sunxi_drm_crtc_get_hw_id(struct drm_crtc *crtc);

#endif
