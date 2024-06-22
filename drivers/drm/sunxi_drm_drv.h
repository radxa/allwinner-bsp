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

#ifndef _SUNXI_DRM_DRV_H_
#define _SUNXI_DRM_DRV_H_

#include <drm/drm_drv.h>

#define BOOT_OUTPUT_MAX			2
#define OVL_MAX				4
#define OVL_REMAIN			(OVL_MAX - 1)

struct sunxi_logo_info {
	unsigned int phy_addr;
	unsigned int width;
	unsigned int height;
	unsigned int bpp;
	unsigned int stride;
	unsigned int crop_l;
	unsigned int crop_t;
	unsigned int crop_r;
	unsigned int crop_b;
};

struct display_boot_info {
	unsigned int de_id;
	unsigned int tcon_id;
	bool sw_enable;
	unsigned int device_type;
	unsigned int mode;
	unsigned int format;
	unsigned int bits;
	unsigned int colorspace;
	unsigned int eotf;
	struct sunxi_logo_info logo;
};

struct sunxi_drm_private {
	struct drm_device base;
	bool sw_enable;
	struct display_boot_info boot/*[BOOT_OUTPUT_MAX]*/;
	struct drm_property *prop_blend_mode[OVL_REMAIN];
	struct drm_property *prop_alpha[OVL_REMAIN];
	struct drm_property *prop_src_x[OVL_REMAIN], *prop_src_y[OVL_REMAIN];
	struct drm_property *prop_src_w[OVL_REMAIN], *prop_src_h[OVL_REMAIN];
	struct drm_property *prop_crtc_x[OVL_REMAIN], *prop_crtc_y[OVL_REMAIN];
	struct drm_property *prop_crtc_w[OVL_REMAIN], *prop_crtc_h[OVL_REMAIN];
	struct drm_property *prop_fb_id[OVL_REMAIN];
	struct drm_property *prop_color[OVL_MAX];
	struct drm_property *prop_layer_id;
	struct drm_property *prop_frontend_data;
	struct drm_property *prop_eotf;
	struct drm_property *prop_color_space;
	struct drm_property *prop_color_format;
	struct drm_property *prop_color_depth;
	struct drm_property *prop_color_range;
};

#define to_sunxi_drm_private(drm) container_of(drm, struct sunxi_drm_private, base)

#endif
