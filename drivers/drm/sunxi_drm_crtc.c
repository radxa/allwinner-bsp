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
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fourcc.h>
#include <linux/version.h>
#include <linux/sort.h>

#include "include.h"
#include "sunxi_device/sunxi_de.h"
#include "sunxi_drm_crtc.h"
#include "sunxi_drm_drv.h"

struct sunxi_drm_crtc {
	struct drm_crtc crtc;
	struct sunxi_de_out *sunxi_de;
	unsigned int channel_cnt;
	unsigned int layer_cnt;
	unsigned int hw_id;
	bool enabled;
	bool allow_sw_enable;
	struct sunxi_drm_plane *plane;
	struct drm_pending_vblank_event *event;
};

struct sunxi_drm_plane {
	struct drm_plane plane;
	unsigned int channel;
	unsigned int layer;
	struct sunxi_drm_crtc *crtc;
};

enum sunxi_plane_alpha_mode {
	PIXEL_ALPHA = 0,
	GLOBAL_ALPHA = 1,
	MIXED_ALPHA = 2,
};

struct display_channel_state {
	struct drm_plane_state base;

	uint32_t src_x[OVL_REMAIN], src_y[OVL_REMAIN];
	uint32_t src_w[OVL_REMAIN], src_h[OVL_REMAIN];
	uint32_t crtc_x[OVL_REMAIN], crtc_y[OVL_REMAIN];
	uint32_t crtc_w[OVL_REMAIN], crtc_h[OVL_REMAIN];
	struct drm_framebuffer *fb[OVL_REMAIN];
	uint32_t color[OVL_MAX];
	uint32_t eotf;
	uint32_t color_space;
	uint32_t zorder;
};

#define to_sunxi_plane(x)			container_of(x, struct sunxi_drm_plane, plane)
#define to_sunxi_crtc(x)			container_of(x, struct sunxi_drm_crtc, crtc)
#define to_display_channel_state(x)		container_of(x, struct display_channel_state, base)

// RGB/RGBA 32x8 layout3 split AFBC compress stream
#define SUNXI_RGB_AFBC_MOD                                                             \
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_SPARSE | \
							AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPLIT)

// YUV4:2:0 16x16 layout1 split AFBC compress stream
#define SUNXI_YUV_AFBC_MOD                                                              \
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE | \
							AFBC_FORMAT_MOD_SPLIT)

static bool is_full_range(int color_space)
{
	switch (color_space) {
	case DISP_UNDEF:
	case DISP_GBR:
	case DISP_BT709:
	case DISP_FCC:
	case DISP_BT470BG:
	case DISP_BT601:
	case DISP_SMPTE240M:
	case DISP_YCGCO:
	case DISP_BT2020NC:
	case DISP_BT2020C:
	case DISP_RESERVED:
		return false;
	case DISP_UNDEF_F:
	case DISP_GBR_F:
	case DISP_BT709_F:
	case DISP_FCC_F:
	case DISP_BT470BG_F:
	case DISP_BT601_F:
	case DISP_SMPTE240M_F:
	case DISP_YCGCO_F:
	case DISP_BT2020NC_F:
	case DISP_BT2020C_F:
	case DISP_RESERVED_F:
		return true;
	default:
		DRM_ERROR("get an unsupport color space %d\n", color_space);
		return true;
	}
}

static int drm_to_disp_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return DISP_FORMAT_ARGB_8888;
	case DRM_FORMAT_ABGR8888:
		return DISP_FORMAT_ABGR_8888;
	case DRM_FORMAT_RGBA8888:
		return DISP_FORMAT_RGBA_8888;
	case DRM_FORMAT_BGRA8888:
		return DISP_FORMAT_BGRA_8888;
	case DRM_FORMAT_XRGB8888:
		return DISP_FORMAT_XRGB_8888;
	case DRM_FORMAT_XBGR8888:
		return DISP_FORMAT_XBGR_8888;
	case DRM_FORMAT_RGBX8888:
		return DISP_FORMAT_RGBX_8888;
	case DRM_FORMAT_BGRX8888:
		return DISP_FORMAT_BGRX_8888;
	case DRM_FORMAT_RGB888:
		return DISP_FORMAT_RGB_888;
	case DISP_FORMAT_BGR_888:
		return DRM_FORMAT_BGR888;
	case DRM_FORMAT_RGB565:
		return DISP_FORMAT_RGB_565;
	case DRM_FORMAT_BGR565:
		return DISP_FORMAT_BGR_565;
	case DRM_FORMAT_ARGB4444:
		return DISP_FORMAT_ARGB_4444;
	case DRM_FORMAT_ABGR4444:
		return DISP_FORMAT_ABGR_4444;
	case DRM_FORMAT_RGBA4444:
		return DISP_FORMAT_RGBA_4444;
	case DRM_FORMAT_BGRA4444:
		return DISP_FORMAT_BGRA_4444;
	case DRM_FORMAT_ARGB1555:
		return DISP_FORMAT_ARGB_1555;
	case DRM_FORMAT_ABGR1555:
		return DISP_FORMAT_ABGR_1555;
	case DRM_FORMAT_RGBA5551:
		return DISP_FORMAT_RGBA_5551;
	case DRM_FORMAT_BGRA5551:
		return DISP_FORMAT_BGRA_5551;

	case DRM_FORMAT_AYUV:
		return DISP_FORMAT_YUV444_I_AYUV;
	case DRM_FORMAT_YUV444:
		return DISP_FORMAT_YUV444_P;
	case DRM_FORMAT_YUV422:
		return DISP_FORMAT_YUV422_P;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420: /* display2 workaround FIXME*/
		return DISP_FORMAT_YUV420_P;
	case DRM_FORMAT_YUV411:
		return DISP_FORMAT_YUV411_P;
	case DRM_FORMAT_NV61:
		return DISP_FORMAT_YUV422_SP_UVUV;
	case DRM_FORMAT_NV16:
		return DISP_FORMAT_YUV422_SP_VUVU;
	case DRM_FORMAT_NV12:
		return DISP_FORMAT_YUV420_SP_UVUV;
	case DRM_FORMAT_NV21:
		return DISP_FORMAT_YUV420_SP_VUVU;
	}

	DRM_ERROR("get an unsupport drm format %d\n", format);
	return DISP_FORMAT_ARGB_8888;
}

static void fill_fb(struct disp_fb_info_inner *disp_fb, struct drm_framebuffer *fb)
{
	int i;
	const int plane_max = 3;
	unsigned long tmp;
	struct drm_gem_cma_object *gem;
	for (i = 0; i < plane_max; i++) {
		/* FIXME*/
		/*fb_inner->align[i] = fb->pitches[i];*/
		//disp_fb->align[i] = 0;
		//disp_fb->size[i].height = fb->height;
		disp_fb->pitch[i] = fb->pitches[i];

		/*FIXME disp_fb->size[i].width/height in pixel, fb->pitches in byte, convertion is format related */
/*		switch (fb->format->num_planes) {
		case 1:
			disp_fb->size[i].width = fb->width;
			break;

		case 2:
			if (i == 0)
				disp_fb->size[i].width =
					fb->pitches[i];
			else if (i == 1)
				disp_fb->size[i].width =
					fb->pitches[i] / 2;
			break;

		case 3:
			disp_fb->size[i].width = fb->pitches[i];
			break;
		}*/
		gem = drm_fb_cma_get_gem_obj(fb, i);
		/* color mode no need gem obj */
		if (!gem) {
			continue;
		}
		disp_fb->addr[i] =
			(unsigned long long)gem->paddr + fb->offsets[i];
	}
	disp_fb->format = drm_to_disp_format(fb->format->format);

	/*FIXME display2 workaround, swap v u order*/
	if (DRM_FORMAT_YVU420 == fb->format->format) {
		tmp = disp_fb->addr[1];
		disp_fb->addr[1] = disp_fb->addr[2];
		disp_fb->addr[2] = tmp;
	}
}

struct afbc_stream_info {
	uint32_t format;
	uint32_t header_layout;
	uint32_t inputbits[4];
};

const static struct afbc_stream_info afbc_stream_infos[] = {
	{ DRM_FORMAT_ARGB8888, 0, {8, 8, 8, 8} },
	{ DRM_FORMAT_ABGR8888, 0, {8, 8, 8, 8} },
	{ DRM_FORMAT_RGBA8888, 0, {8, 8, 8, 8} },
	{ DRM_FORMAT_BGRA8888, 0, {8, 8, 8, 8} },

	{ DRM_FORMAT_XRGB8888, 0, {8, 8, 8, 8} },
	{ DRM_FORMAT_XBGR8888, 0, {8, 8, 8, 8} },
	{ DRM_FORMAT_RGBX8888, 0, {8, 8, 8, 8} },
	{ DRM_FORMAT_BGRX8888, 0, {8, 8, 8, 8} },

	{ DRM_FORMAT_RGB888,   0, {8, 8, 8, 0} },
	{ DRM_FORMAT_BGR888,   0, {8, 8, 8, 0} },

	{ DRM_FORMAT_RGB565,   0, {5, 6, 5, 0} },
	{ DRM_FORMAT_BGR565,   0, {5, 6, 5, 0} },

	{ DRM_FORMAT_ARGB4444, 0, {4, 4, 4, 4} },
	{ DRM_FORMAT_ABGR4444, 0, {4, 4, 4, 4} },
	{ DRM_FORMAT_RGBA4444, 0, {4, 4, 4, 4} },
	{ DRM_FORMAT_BGRA4444, 0, {4, 4, 4, 4} },

	{ DRM_FORMAT_ARGB1555, 0, {5, 5, 5, 1} },
	{ DRM_FORMAT_ABGR1555, 0, {5, 5, 5, 1} },
	{ DRM_FORMAT_RGBA5551, 0, {5, 5, 5, 1} },
	{ DRM_FORMAT_BGRA5551, 0, {5, 5, 5, 1} },

	{ DRM_FORMAT_YUV422,   2, {8, 8, 8, 0} },
	{ DRM_FORMAT_NV61,     2, {8, 8, 8, 0} },
	{ DRM_FORMAT_NV16,     2, {8, 8, 8, 0} },

	{ DRM_FORMAT_YUV420,   1, {8, 8, 8, 0} },
	{ DRM_FORMAT_YVU420,   1, {8, 8, 8, 0} },
	{ DRM_FORMAT_NV12,     1, {8, 8, 8, 0} },

	{ DRM_FORMAT_ABGR2101010, 0, {10, 10, 10, 2} },
};

static void fill_afbc_info(struct disp_afbc_info *afbc, struct drm_framebuffer *fb)
{
	size_t i = 0;

	for (i = 0; i < ARRAY_SIZE(afbc_stream_infos); i++) {
		if (afbc_stream_infos[i].format == fb->format->format) {
			afbc->header_layout = afbc_stream_infos[i].header_layout;
			afbc->inputbits[0]  = afbc_stream_infos[i].inputbits[0];
			afbc->inputbits[1]  = afbc_stream_infos[i].inputbits[1];
			afbc->inputbits[2]  = afbc_stream_infos[i].inputbits[2];
			afbc->inputbits[3]  = afbc_stream_infos[i].inputbits[3];
			break;
		}
	}

	if (i == ARRAY_SIZE(afbc_stream_infos)) {
		DRM_ERROR("get an drm format %d, not support afbc compress\n", fb->format->format);
		afbc->header_layout = 0;
		afbc->inputbits[0]  = 8;
		afbc->inputbits[1]  = 8;
		afbc->inputbits[2]  = 8;
		afbc->inputbits[3]  = 8;
	}

	afbc->image_crop[0] = 0;
	afbc->image_crop[1] = 0; // TODO: yuv stream from aw decoder should crop 16 lines on top

	if ((fb->modifier & DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_YTR)) ==
		DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_YTR)) {
		afbc->yuv_transform = 1;
	} else {
		afbc->yuv_transform = 0;
	}

	if ((fb->modifier & DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)) ==
		DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)) {
		afbc->block_layout = 3;
		afbc->block_size[0] = (fb->width  + 31) / 32;
		afbc->block_size[1] = (fb->height +  7) / 8;
	} else {
		afbc->block_layout = 1;
		afbc->block_size[0] = (fb->width  + 15) / 16;
		afbc->block_size[1] = (fb->height + 15) / 16;
	}

	DRM_DEBUG_DRIVER(
		"[SUNXI-CRTC] afbc header_layout=%d block_layout=%d width=%d height=%d "
		"modifier=0x%016llx\n",
		afbc->header_layout, afbc->block_layout, fb->width, fb->height, fb->modifier);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)

#else
static void sunxi_plane_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)

#endif
{
	struct drm_plane_state *new_state = plane->state;
	struct display_channel_state *cstate = to_display_channel_state(new_state);
	struct sunxi_drm_plane *sunxi_plane = to_sunxi_plane(plane);
	struct sunxi_drm_crtc *scrtc = sunxi_plane->crtc;
	int i = sunxi_plane->channel * OVL_MAX;
	struct drm_framebuffer *fb;
	struct disp_layer_config_inner config[OVL_MAX];

	memset(config, 0, sizeof(config[0]) * OVL_MAX);
	for (i = 0; i < OVL_MAX; i++) {
		config[i].channel = sunxi_plane->channel;
		config[i].layer_id = i;
		config[i].info.mode = cstate->color[i] ? LAYER_MODE_COLOR : LAYER_MODE_BUFFER;
		if (config[i].info.mode == LAYER_MODE_COLOR)
			config[i].info.color = cstate->color[i];

		if (i == 0) {
			fb = new_state->fb;
			/* do not set as new_state->visable,
			     drm_atomic_helper_check_plane_state is not used for our platform now */
			config[i].enable = true;
			config[i].info.screen_win.x = new_state->crtc_x;
			config[i].info.screen_win.y = new_state->crtc_y;
			config[i].info.screen_win.width = new_state->crtc_w;
			config[i].info.screen_win.height = new_state->crtc_h;

			config[i].info.fb.crop.x = (((unsigned long long)new_state->src_x) >> 16) << 32;
			config[i].info.fb.crop.y = (((unsigned long long)new_state->src_y) >> 16) << 32;
			config[i].info.fb.crop.width = (((unsigned long long)new_state->src_w) >> 16) << 32;
			config[i].info.fb.crop.height = (((unsigned long long)new_state->src_h) >> 16) << 32;
		} else {
			fb = cstate->fb[i - 1];
			config[i].enable = fb ? true : false;
			if (config[i].enable) {
				config[i].info.screen_win.x = cstate->crtc_x[i - 1];
				config[i].info.screen_win.y = cstate->crtc_y[i - 1];
				config[i].info.screen_win.width = cstate->crtc_w[i - 1];
				config[i].info.screen_win.height = cstate->crtc_h[i - 1];

				config[i].info.fb.crop.x = (((unsigned long long)cstate->src_x[i - 1]) >> 16) << 32;
				config[i].info.fb.crop.y = (((unsigned long long)cstate->src_y[i - 1]) >> 16) << 32;
				config[i].info.fb.crop.width = (((unsigned long long)cstate->src_w[i - 1]) >> 16) << 32;
				config[i].info.fb.crop.height = (((unsigned long long)cstate->src_h[i - 1]) >> 16) << 32;
			}
		}
		if (fb) {
			fill_fb(&config[i].info.fb, fb);
			config[i].info.zorder = cstate->zorder + i;
			config[i].info.alpha_mode = new_state->pixel_blend_mode != DRM_MODE_BLEND_PIXEL_NONE ? MIXED_ALPHA : GLOBAL_ALPHA;
			config[i].info.alpha_value = new_state->alpha >> 8;
			config[i].info.fb.color_space = cstate->color_space;
			config[i].info.fb.pre_multiply = new_state->pixel_blend_mode == DRM_MODE_BLEND_PREMULTI;
			config[i].info.fb.eotf = cstate->eotf;
			DRM_DEBUG_DRIVER("[SUNXI-CRTC]%s %s %lx\n", __func__, plane->name, (unsigned long)config[i].info.fb.addr[0]);

			if (fb->modifier & DRM_FORMAT_MOD_ARM_AFBC(0)) {
				config[i].info.fb.fbd_en = 1;
				fill_afbc_info(&config[i].info.fb.afbc_info, fb);
			}
		}
	}

	for (i = 0; i < OVL_MAX; i++) {
		sunxi_de_layer_update(scrtc->sunxi_de, &config[i]);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
#else
static void sunxi_plane_atomic_disable(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
#endif
	struct sunxi_drm_plane *sunxi_plane = to_sunxi_plane(plane);
	struct sunxi_drm_crtc *scrtc = sunxi_plane->crtc;
	struct disp_layer_config_inner config[OVL_MAX];
	int i;

	memset(config, 0, sizeof(config[0]) * OVL_MAX);
	for (i = 0; i < OVL_MAX; i++) {
		config[i].enable = 0;
		config[i].channel = sunxi_plane->channel;
		config[i].layer_id = i;
		DRM_DEBUG_DRIVER("[SUNXI-CRTC]%s %d %d\n", __func__, config[i].channel, config[i].layer_id);
		sunxi_de_layer_update(scrtc->sunxi_de, &config[i]);
	}
}

static int sunxi_atomic_plane_set_property(struct drm_plane *plane,
					  struct drm_plane_state *state,
					  struct drm_property *property,
					  uint64_t val)
{
	struct sunxi_drm_private *private = to_sunxi_drm_private(plane->dev);
	struct display_channel_state *cstate = to_display_channel_state(state);
	int i;
	for (i = 0; i < OVL_REMAIN; i++) {
		if (property == private->prop_src_x[i]) {
			cstate->src_x[i] = val;
			return 0;
		}

		if (property == private->prop_src_y[i]) {
			cstate->src_y[i] = val;
			return 0;
		}

		if (property == private->prop_src_w[i]) {
			cstate->src_w[i] = val;
			return 0;
		}

		if (property == private->prop_src_h[i]) {
			cstate->src_h[i] = val;
			return 0;
		}

		if (property == private->prop_crtc_x[i]) {
			cstate->crtc_x[i] = val;
			return 0;
		}

		if (property == private->prop_crtc_y[i]) {
			cstate->crtc_y[i] = val;
			return 0;
		}

		if (property == private->prop_crtc_w[i]) {
			cstate->crtc_w[i] = val;
			return 0;
		}

		if (property == private->prop_crtc_h[i]) {
			cstate->crtc_h[i] = val;
			return 0;
		}

		if (property == private->prop_fb_id[i]) {
			struct drm_framebuffer *fb = NULL;
			struct drm_device *dev = plane->dev;
			bool found = false;
			mutex_lock(&dev->mode_config.fb_lock);
			list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
				if (fb->base.id == val) {
					found = true;
					drm_framebuffer_get(fb);
					break;
				}
			}
			mutex_unlock(&dev->mode_config.fb_lock);
			if (found) {
				drm_framebuffer_assign(&cstate->fb[i], fb);
				drm_framebuffer_put(fb);
				return 0;
			} else {
				DRM_ERROR("plane fb %d not found!\n", property->base.id);
				return -EINVAL;
			}
		}

		if (property == private->prop_color[i]) {
			cstate->color[i] = val;
			return 0;
		}
	}

	if (property == private->prop_color[OVL_MAX - 1]) {
		cstate->color[OVL_MAX - 1] = val;
		return 0;
	}

	if (property == private->prop_eotf) {
		cstate->eotf = val;
		return 0;
	}

	if (property == private->prop_color_space) {
		cstate->color_space = val;
		return 0;
	}

	DRM_ERROR("plane property %d name%s not found!\n",
		  property->base.id, property->name);
	return -EINVAL;
}

static int sunxi_atomic_plane_get_property(struct drm_plane *plane,
					  const struct drm_plane_state *state,
					  struct drm_property *property,
					  uint64_t *val)
{
	struct sunxi_drm_private *private = to_sunxi_drm_private(plane->dev);
	struct display_channel_state *cstate = to_display_channel_state(state);
	int i;

	for (i = 0; i < OVL_REMAIN; i++) {
		if (property == private->prop_src_x[i]) {
			*val = cstate->src_x[i];
			return 0;
		}

		if (property == private->prop_src_y[i]) {
			*val = cstate->src_y[i];
			return 0;
		}

		if (property == private->prop_src_w[i]) {
			*val = cstate->src_w[i];
			return 0;
		}

		if (property == private->prop_src_h[i]) {
			*val = cstate->src_h[i];
			return 0;
		}

		if (property == private->prop_crtc_x[i]) {
			*val = cstate->crtc_x[i];
			return 0;
		}

		if (property == private->prop_crtc_y[i]) {
			*val = cstate->crtc_y[i];
			return 0;
		}

		if (property == private->prop_crtc_w[i]) {
			*val = cstate->crtc_w[i];
			return 0;
		}

		if (property == private->prop_crtc_h[i]) {
			*val = cstate->crtc_h[i];
			return 0;
		}

		if (property == private->prop_fb_id[i]) {
			*val = cstate->fb[i] ? cstate->fb[i]->base.id : 0;
			return 0;
		}

		if (property == private->prop_color[i]) {
			*val = cstate->color[i];
			return 0;
		}
	}

	if (property == private->prop_color[OVL_MAX - 1]) {
		*val = cstate->color[OVL_MAX - 1];
		return 0;
	}

	if (property == private->prop_eotf) {
		*val = cstate->eotf;
		return 0;
	}

	if (property == private->prop_color_space) {
		*val = cstate->color_space;
		return 0;
	}

	DRM_ERROR("plane property %d name%s not found!\n",
		  property->base.id, property->name);
	return -EINVAL;
}

static struct drm_plane_state *sunxi_atomic_plane_duplicate_state(struct drm_plane *plane)
{

	struct display_channel_state *old;
	struct display_channel_state *new;
	int i;

	if (WARN_ON(!plane->state))
		return NULL;

	old = to_display_channel_state(plane->state);
	new = kmemdup(old, sizeof(*new), GFP_KERNEL);

	if (!new)
		return NULL;
	for (i = 0; i < OVL_REMAIN; i++) {
		if (new->fb[i])
			drm_framebuffer_get(new->fb[i]);
	}

	__drm_atomic_helper_plane_duplicate_state(plane, &new->base);
	return &new->base;
}

static void sunxi_atomic_plane_destroy_state(struct drm_plane *plane,
					    struct drm_plane_state *state)
{
	struct display_channel_state *cstate = to_display_channel_state(state);
	int i;

	for (i = 0; i < OVL_REMAIN; i++) {
		if (cstate->fb[i])
			drm_framebuffer_put(cstate->fb[i]);
	}

	__drm_atomic_helper_plane_destroy_state(state);
	kfree(cstate);
}

static void sunxi_atomic_plane_reset(struct drm_plane *plane)
{
	struct display_channel_state *state;

	if (plane->state) {
		state = to_display_channel_state(plane->state);
		sunxi_atomic_plane_destroy_state(plane, plane->state);
	} else {
		kfree(state);
	}
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		DRM_ERROR("%s fail, no mem\n", __FUNCTION__);
		return;
	}

	__drm_atomic_helper_plane_reset(plane, &state->base);
	state->eotf = DISP_EOTF_UNDEF;
	state->color_space = DISP_UNDEF;
}

static bool sunxi_format_mod_supported(struct drm_plane *plane, u32 format, u64 modifier)
{
	if (modifier == DRM_FORMAT_MOD_INVALID)
		return false;

	// TODO: filter out the formats which not support AFBC
	if (modifier == DRM_FORMAT_MOD_LINEAR || modifier == SUNXI_RGB_AFBC_MOD ||
		modifier == SUNXI_YUV_AFBC_MOD)
		return true;

    return false;
}

static const struct drm_plane_funcs sunxi_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = sunxi_atomic_plane_reset,
	.atomic_duplicate_state = sunxi_atomic_plane_duplicate_state,
	.atomic_destroy_state = sunxi_atomic_plane_destroy_state,
	.atomic_set_property = sunxi_atomic_plane_set_property,
	.atomic_get_property = sunxi_atomic_plane_get_property,
	.format_mod_supported = sunxi_format_mod_supported,
};

static const struct drm_plane_helper_funcs sunxi_plane_helper_funcs = {
	.atomic_update = sunxi_plane_atomic_update,
	.atomic_disable = sunxi_plane_atomic_disable,
};

static void sunxi_drm_plane_property_init(struct sunxi_drm_plane *plane, unsigned int channel_cnt)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(plane->plane.dev);
#if DRM_OBJECT_MAX_PROPERTY >= 48
	int i;
#endif

	drm_plane_create_alpha_property(&plane->plane);
	drm_plane_create_blend_mode_property(&plane->plane,
					BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					BIT(DRM_MODE_BLEND_PREMULTI) |
					BIT(DRM_MODE_BLEND_COVERAGE));
	drm_plane_create_zpos_property(&plane->plane, plane->channel, 0, channel_cnt - 1);

	/* { FB_ID CRTC_X(YWH) SRC_X(YWH) COLOR } * 4 = 40
	CRTC_ID type IN_FENCE_FD alpha "pixel blend mode" zpos EOTF COLOR_SPACE  =  8 */
#if DRM_OBJECT_MAX_PROPERTY >= 48
	for (i = 0; i < OVL_REMAIN; i++) {
		drm_object_attach_property(&plane->plane.base, pri->prop_src_x[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_src_y[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_src_w[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_src_h[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_crtc_x[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_crtc_y[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_crtc_w[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_crtc_h[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_fb_id[i], 0);
		drm_object_attach_property(&plane->plane.base, pri->prop_color[i], 0);
	}
	drm_object_attach_property(&plane->plane.base, pri->prop_color[OVL_REMAIN], 0);
#else
	drm_object_attach_property(&plane->plane.base, pri->prop_color[0], 0);
#endif
	drm_object_attach_property(&plane->plane.base, pri->prop_eotf, 0);
	drm_object_attach_property(&plane->plane.base, pri->prop_color_space, 0);
}

static const uint64_t format_modifiers_afbc[] = {
	SUNXI_YUV_AFBC_MOD,
	SUNXI_RGB_AFBC_MOD,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_common[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static int sunxi_drm_plane_init(struct drm_device *dev,
				struct sunxi_drm_crtc *scrtc,
				uint32_t possible_crtc,
				struct sunxi_drm_plane *plane, int type,
				unsigned int de_id, unsigned int channel,
				unsigned int layer)
{
	const uint32_t *formats;
	unsigned int format_count;
	int afbc_supported = 0;

	plane->crtc = scrtc;
	plane->channel = channel;
	plane->layer = layer;

	sunxi_de_get_layer_formats(scrtc->sunxi_de, channel, &formats,
				   &format_count);

	afbc_supported = sunxi_de_get_layer_features(scrtc->sunxi_de, channel) & SUNXI_PLANE_FEATURE_AFBC;

	if (drm_universal_plane_init(dev, &plane->plane, possible_crtc,
				     &sunxi_plane_funcs, formats, format_count,
				     afbc_supported ? format_modifiers_afbc : format_modifiers_common,
				     type, "plane%d-%d-%d", de_id, channel, layer)) {
		DRM_ERROR("drm_universal_plane_init failed\n");
		return -1;
	}

	drm_plane_helper_add(&plane->plane, &sunxi_plane_helper_funcs);
	sunxi_drm_plane_property_init(plane, scrtc->channel_cnt);
	return 0;
}
/* plane end*/

static void sunxi_crtc_finish_page_flip(struct drm_device *dev,
					struct sunxi_drm_crtc *scrtc)
{
	unsigned long flags;

	/* send the vblank of drm_crtc_state->event */
	spin_lock_irqsave(&dev->event_lock, flags);
	if (scrtc->event) {
		drm_crtc_send_vblank_event(&scrtc->crtc, scrtc->event);
		scrtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static irqreturn_t sunxi_crtc_event_proc(int irq, void *parg)
{
	int ret = 0;
	struct drm_crtc *crtc = (struct drm_crtc *)parg;
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	ret = sunxi_de_event_proc(scrtc->sunxi_de);
	if (ret < 0) {
		DRM_ERROR("sunxi_de_event_proc FAILED!\n");
		goto out;
	}

out:
	/* vblank common process */
	drm_crtc_handle_vblank(&scrtc->crtc);
	//sunxi_crtc_finish_page_flip(crtc->dev, scrtc);
	return IRQ_HANDLED;
}

static int sunxi_crtc_atomic_set_property(struct drm_crtc *crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(state);
	struct sunxi_drm_private *private = to_sunxi_drm_private(crtc->dev);

	if (property == private->prop_eotf) {
		scrtc_state->eotf = val;
		return 0;
	}

	if (property == private->prop_color_format) {
		scrtc_state->color_fmt = val;
		return 0;
	}

	if (property == private->prop_color_depth) {
		scrtc_state->color_depth = val;
		return 0;
	}

	if (property == private->prop_color_space) {
		scrtc_state->color_space = val;
		return 0;
	}

	DRM_ERROR("plane property %d name%s not found!\n",
		  property->base.id, property->name);
	return -EINVAL;
}

static int sunxi_crtc_atomic_get_property(struct drm_crtc *crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(state);
	struct sunxi_drm_private *private = to_sunxi_drm_private(crtc->dev);

	if (property == private->prop_eotf) {
		*val = scrtc_state->eotf;
		return 0;
	}

	if (property == private->prop_color_format) {
		*val = scrtc_state->color_fmt;
		return 0;
	}

	if (property == private->prop_color_depth) {
		*val = scrtc_state->color_depth;
		return 0;
	}

	if (property == private->prop_color_space) {
		*val = scrtc_state->color_space;
		return 0;
	}

	DRM_ERROR("plane property %d name%s not found!\n",
		  property->base.id, property->name);
	return -EINVAL;
}

static struct drm_crtc_state *sunxi_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct sunxi_crtc_state *state, *cur;

	if (WARN_ON(!crtc->state))
		return NULL;

	cur = to_sunxi_crtc_state(crtc->state);
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	memcpy(state, cur, sizeof(*state));

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);
	return &state->base;
}

static void sunxi_crtc_destroy_state(struct drm_crtc *crtc,
				     struct drm_crtc_state *s)
{
	struct sunxi_crtc_state *state;

	state = to_sunxi_crtc_state(s);
	__drm_atomic_helper_crtc_destroy_state(s);
	kfree(state);
}

static void sunxi_crtc_reset(struct drm_crtc *crtc)
{
	struct sunxi_crtc_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (crtc->state)
		sunxi_crtc_destroy_state(crtc, crtc->state);
	state->color_fmt = DISP_CSC_TYPE_RGB;
	state->color_depth = DISP_DATA_8BITS;
	state->eotf = DISP_EOTF_BT709;
	state->color_space = DISP_BT709;
	__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_crtc_atomic_enable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)

#else
static void sunxi_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
#endif
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(crtc->dev);
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);
	struct drm_crtc_state *new_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(new_state);
	struct drm_mode_modeinfo modeinfo;
	struct disp_manager_info info;
	bool sw_enable = pri->sw_enable && pri->boot.de_id == scrtc->hw_id && scrtc->allow_sw_enable;

	scrtc->allow_sw_enable = false;
	DRM_INFO("[SUNXI-CRTC]%s\n", __func__);
	if (scrtc->enabled) {
		DRM_INFO("crtc has been enable, no need to enable again\n");
		return;
	}

	if ((!new_state->enable) || (!new_state->active)) {
		DRM_INFO("Warn: DRM do NOT want to enable or active crtc%d,"
			 " so can NOT be enabled\n",
			 scrtc->crtc.index);
		return;
	}

	drm_property_blob_get(new_state->mode_blob);
	memcpy(&modeinfo, new_state->mode_blob->data,
	       new_state->mode_blob->length);
	drm_property_blob_put(new_state->mode_blob);

	memset(&info, 0, sizeof(info));
	info.size.width = modeinfo.hdisplay;
	info.size.height = modeinfo.vdisplay;

	info.color_space = scrtc_state->color_space;
	info.cs = scrtc_state->color_fmt;
	info.hwdev_index = scrtc_state->tcon_id;
	info.device_fps = modeinfo.vrefresh;
	info.eotf = scrtc_state->eotf;
	info.data_bits = scrtc_state->color_depth;
	info.color_range = is_full_range(scrtc_state->color_space) ?
				DISP_COLOR_RANGE_0_255 : DISP_COLOR_RANGE_16_235;

	info.enable = true;
	info.blank = false;

	if (sunxi_de_enable(scrtc->sunxi_de, &info, sw_enable) < 0)
		DRM_ERROR("sunxi_de_enable failed\n");

	scrtc->enabled = true;
	drm_crtc_vblank_on(crtc);
	DRM_INFO("%s finish\n", __func__);
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)

#else
static void sunxi_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
#endif

{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	DRM_INFO("[SUNXI-CRTC]%s\n", __func__);
	drm_crtc_vblank_off(crtc);
	if (!scrtc->enabled) {
		DRM_ERROR("%s: crtc has been disabled\n", __func__);
		return;
	}

	scrtc->enabled = false;
	sunxi_de_disable(scrtc->sunxi_de);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);
		crtc->state->event = NULL;
	}

	return;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_crtc_atomic_begin(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)

#else
static void sunxi_crtc_atomic_begin(struct drm_crtc *crtc,
			     struct drm_atomic_state *state)
#endif
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	DRM_DEBUG_DRIVER("%s\n", __func__);
	if (crtc->state->event) {
		drm_crtc_vblank_get(crtc);
		spin_lock_irqsave(&dev->event_lock, flags);
		scrtc->event = crtc->state->event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
		crtc->state->event = NULL;
	}
	sunxi_de_atomic_begin(scrtc->sunxi_de);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
#else
static void sunxi_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
#endif
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	sunxi_de_atomic_flush(scrtc->sunxi_de);
	sunxi_crtc_finish_page_flip(crtc->dev, scrtc);
	DRM_DEBUG_DRIVER("%s finish\n", __func__);
}

static int sunxi_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	// for now vblank is enable afer output device is ready,this func do noting
	// in fact
	struct drm_crtc_state *new_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(new_state);

	DRM_DEBUG_DRIVER("%s\n", __func__);
	if (scrtc_state->enable_vblank == NULL) {
		DRM_ERROR("enable vblank is not registerd!\n");
		return -1;
	}
	scrtc_state->enable_vblank(true, scrtc_state->vblank_enable_data);
	return 0;
}

static void sunxi_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	// for now vblank is enable afer output device is ready and never disable,
	// this func do noting in fact
	struct drm_crtc_state *new_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(new_state);

	DRM_DEBUG_DRIVER("%s\n", __func__);
	if (scrtc_state->enable_vblank == NULL) {
		DRM_ERROR("enable vblank is not registerd!\n");
		return;
	}
	scrtc_state->enable_vblank(false, scrtc_state->vblank_enable_data);
}

static int sunxi_plane_state_zpos_cmp(const void *a, const void *b)
{
	const struct drm_plane_state *sa = *(struct drm_plane_state **)a;
	const struct drm_plane_state *sb = *(struct drm_plane_state **)b;
	return sa->zpos - sb->zpos;
}

static int sunxi_layer_zorder_config(struct drm_crtc_state *state, struct drm_crtc *crtc)
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);
	struct drm_plane *plane;
	struct drm_plane_state **states;
	struct drm_device *dev = crtc->dev;
	unsigned int channenl_cnt = scrtc->channel_cnt;
	struct drm_plane_state *plane_state;
	struct display_channel_state *cstate;
	struct drm_framebuffer *fb;
	int i = 0, n = 0, z = 0, j = 0, en_cnt = 0;

	states = kmalloc_array(channenl_cnt, sizeof(*states), GFP_KERNEL);
	if (!states)
		return -ENOMEM;

	drm_for_each_plane_mask(plane, dev, state->plane_mask) {
		plane_state = drm_atomic_get_new_plane_state(state->state, plane);
		states[n++] = plane_state;
	}
	sort(states, n, sizeof(*states), sunxi_plane_state_zpos_cmp, NULL);

	/* insert our layer zorder, cstate->zorder = layer0 zorder */
	for (i = 0; i < n; i++) {
		if (!states[i])
			continue;
		plane = states[i]->plane;
		cstate = to_display_channel_state(states[i]);
		en_cnt = 0;
		for (j = 0; j < OVL_MAX; j++) {
			fb = j == 0 ? states[i]->fb : cstate->fb[j - 1];
			if (fb) {
				if (j == 0)
					cstate->zorder = z;
				z++;
				en_cnt++;
			} else
				break;
		}
		DRM_DEBUG_DRIVER("[PLANE:%d:%s] normalized zpos value %d new %d en_cnt %d\n",
				 plane->base.id, plane->name, i, cstate->zorder, en_cnt);
	}
	kfree(states);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static int sunxi_drm_crtc_atomic_check(struct drm_crtc *crtc,
				      struct drm_crtc_state *state)
{
	struct drm_crtc_state *crtc_state = state;
#else

static int sunxi_drm_crtc_atomic_check(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
#endif
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	scrtc_state->crtc_irq_handler = sunxi_crtc_event_proc;
	sunxi_layer_zorder_config(crtc_state, crtc);
	return 0;
}

static const struct drm_crtc_funcs sunxi_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = sunxi_crtc_reset,
	.atomic_duplicate_state = sunxi_crtc_duplicate_state,
	.atomic_destroy_state = sunxi_crtc_destroy_state,
	.atomic_get_property = sunxi_crtc_atomic_get_property,
	.atomic_set_property = sunxi_crtc_atomic_set_property,
	.enable_vblank = sunxi_drm_crtc_enable_vblank,
	.disable_vblank = sunxi_drm_crtc_disable_vblank,
};

static const struct drm_crtc_helper_funcs sunxi_crtc_helper_funcs = {
	.atomic_enable = sunxi_crtc_atomic_enable,
	.atomic_disable = sunxi_crtc_atomic_disable,
	.atomic_check = sunxi_drm_crtc_atomic_check,
	.atomic_begin = sunxi_crtc_atomic_begin,
	.atomic_flush = sunxi_crtc_atomic_flush,
};

int sunxi_drm_crtc_get_hw_id(struct drm_crtc *crtc)
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);
	if (!crtc || !scrtc) {
		DRM_ERROR("crtc is NULL\n");
		return -EINVAL;
	}
	return scrtc->hw_id;
}

struct sunxi_drm_crtc *sunxi_drm_crtc_init_one(struct sunxi_de_info *info)
{
	struct sunxi_drm_crtc *scrtc;
	struct drm_device *drm = info->drm;
	unsigned int v_chn_cnt = info->v_chn_cnt;
	unsigned int u_chn_cnt = info->u_chn_cnt;
	int i, ret;

	scrtc = devm_kzalloc(drm->dev, sizeof(*scrtc), GFP_KERNEL);
	if (!scrtc) {
		DRM_ERROR("allocate memory for sunxi_crtc fail\n");
		return ERR_PTR(-ENOMEM);
	}
	scrtc->allow_sw_enable = true;
	scrtc->sunxi_de = info->de_out;
	scrtc->hw_id = info->hw_id;
	scrtc->channel_cnt = v_chn_cnt + u_chn_cnt;
	scrtc->layer_cnt = scrtc->channel_cnt * OVL_MAX;
	scrtc->plane =
		devm_kzalloc(drm->dev,
			     sizeof(*scrtc->plane) * (v_chn_cnt + u_chn_cnt),
			     GFP_KERNEL);
	if (!scrtc->plane) {
		DRM_ERROR("allocate mem for planes fail\n");
		return ERR_PTR(-ENOMEM);
	}

	if (u_chn_cnt == 0) {
		/* TODO */
		DRM_ERROR("NOT support yet\n");
		return ERR_PTR(-EINVAL);
	}

	/* param possible crtc is not used for primariy plane */
	ret = sunxi_drm_plane_init(drm, scrtc, 0, &scrtc->plane[v_chn_cnt],
				   DRM_PLANE_TYPE_PRIMARY, info->hw_id,
				   v_chn_cnt, 0);
	if (ret) {
		DRM_ERROR("plane init fail for de %d\n", info->hw_id);
		goto err_out;
	}

	drm_crtc_init_with_planes(drm, &scrtc->crtc,
				  &scrtc->plane[v_chn_cnt].plane, NULL,
				  &sunxi_crtc_funcs, "DE-%d", info->hw_id);

	/* Set crtc.port to use drm_of_find_possible_crtcs for encoder */
	scrtc->crtc.port = info->port;

	for (i = 0; i < v_chn_cnt + u_chn_cnt; i++) {
		ret = sunxi_drm_plane_init(drm, scrtc, drm_crtc_mask(&scrtc->crtc),
				     &scrtc->plane[i], DRM_PLANE_TYPE_OVERLAY,
				     info->hw_id, i, 0);
		if (i == v_chn_cnt - 1)
			i++;
		if (ret) {
			DRM_ERROR("sunxi plane init for %d fail\n", i);
			goto err_out;
		}
	}
	drm_crtc_helper_add(&scrtc->crtc, &sunxi_crtc_helper_funcs);
	return scrtc;

err_out:
	for (i = 0; i < scrtc->channel_cnt; i++) {
		if (scrtc->plane[i].plane.dev)
			drm_plane_cleanup(&scrtc->plane[i].plane);
	}
	if (scrtc->crtc.dev)
		drm_crtc_cleanup(&scrtc->crtc);
	return ERR_PTR(-EINVAL);
}

void sunxi_drm_crtc_destory(struct sunxi_drm_crtc *scrtc)
{
	int i;
	for (i = 0; i < scrtc->channel_cnt; i++) {
		drm_plane_cleanup(&scrtc->plane[i].plane);
	}
	drm_crtc_cleanup(&scrtc->crtc);
}
