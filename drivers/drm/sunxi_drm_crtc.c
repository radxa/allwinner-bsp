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
#include <linux/kthread.h>
#include <linux/version.h>
#include <drm/drm_blend.h>
#include <drm/drm_edid.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_writeback.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <linux/version.h>
#include <linux/sort.h>
#include <linux/completion.h>

#include "sunxi_drm_crtc.h"
#include "sunxi_drm_drv.h"
#include "sunxi_drm_trace.h"
#include "sunxi_device/hardware/lowlevel_de/de_base.h"

#define WB_SIGNAL_MAX		2

/* wb finish after two vsync, use to signal work finish after vysnc, 2 struct wb_signal_wait is enough */
struct wb_signal_wait {
	bool active;
	unsigned int vsync_cnt;
};

struct sunxi_drm_wb {
	struct sunxi_de_wb *hw_wb;
	struct drm_writeback_connector wb_connector;
	spinlock_t signal_lock;
	struct wb_signal_wait signal[WB_SIGNAL_MAX];
};

struct sunxi_drm_crtc {
	struct drm_crtc crtc;
	struct sunxi_de_out *sunxi_de;
	unsigned int plane_cnt;
	unsigned int hw_id;
	/* protect by modeset_lock*/
	bool fbdev_output;
	/* protect by modeset_lock*/
	bool fbdev_flush_pending;
	/* protect by modeset_lock*/
	bool async_update_flush_pending;
	struct completion flush_done;
	unsigned int fbdev_chn_id;
	bool enabled;
	bool allow_sw_enable;
	unsigned long clk_freq;
	struct sunxi_drm_plane *plane;
	struct drm_pending_vblank_event *event;
	struct sunxi_drm_wb *wb;
	/* protect wb */
	spinlock_t wb_lock;
	/* output device info */
	vblank_enable_callback_t enable_vblank;
	fifo_status_check_callback_t check_status;
	is_sync_time_enough_callback_t is_sync_time_enough;
	void *output_dev_data;

	/* irq counter for systrace record */
	unsigned int irqcnt;
	unsigned int fifo_err;
};

struct sunxi_drm_plane {
	struct drm_plane plane;
	struct de_channel_handle *hdl;
	unsigned int index;
	unsigned int layer_cnt;
	struct sunxi_drm_crtc *crtc;
};

enum sunxi_plane_alpha_mode {
	PIXEL_ALPHA = 0,
	GLOBAL_ALPHA = 1,
	MIXED_ALPHA = 2,
};

#define to_sunxi_plane(x)			container_of(x, struct sunxi_drm_plane, plane)
#define to_sunxi_crtc(x)			container_of(x, struct sunxi_drm_crtc, crtc)
#define wb_connector_to_sunxi_wb(x)		container_of(x, struct sunxi_drm_wb, wb_connector.base)

struct task_struct *commit_task;

static int
sunxi_plane_replace_property_blob_from_id(struct drm_device *dev,
					 struct drm_property_blob **blob,
					 uint64_t blob_id,
					 ssize_t expected_size,
					 bool *replaced)
{
	struct drm_property_blob *new_blob = NULL;

	if (blob_id != 0) {
		new_blob = drm_property_lookup_blob(dev, blob_id);
		if (new_blob == NULL)
			return -EINVAL;

		if (expected_size > 0 &&
		    new_blob->length != expected_size) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
	}

	*replaced |= drm_property_replace_blob(blob, new_blob);
	drm_property_blob_put(new_blob);

	return *replaced ? 0 : -EINVAL;
}

int sunxi_fbdev_plane_update(struct drm_device *dev, unsigned int de_id, unsigned int channel_id, struct display_channel_state *fake_state)
{
	struct drm_plane *plane;
	struct sunxi_drm_plane *sunxi_plane;
	struct sunxi_drm_crtc *scrtc;
	struct sunxi_de_channel_update info;
	bool update;

	DRM_DEBUG_DRIVER("[SUNXI-DE] fbdev plane update\n");
	drm_modeset_lock_all(dev);
	drm_for_each_plane(plane, dev) {
		sunxi_plane = to_sunxi_plane(plane);
		scrtc = sunxi_plane->crtc;
		if (scrtc->hw_id == de_id && sunxi_plane->index == channel_id)
			break;
		sunxi_plane = NULL;
	}
	if (!sunxi_plane) {
		DRM_ERROR("plane fb %d %d for fb not found!\n", de_id, channel_id);
		drm_modeset_unlock_all(dev);
		return -ENXIO;
	}

	if (plane->state->fb || plane->state->crtc) {
		WARN_ON(scrtc->fbdev_output);
		DRM_INFO("skip fbdev plane update because plane used by userspace\n");
		drm_modeset_unlock_all(dev);
		return 0;
	}

	scrtc->fbdev_output = true;
	scrtc->fbdev_chn_id = channel_id;
	info.fbdev_output = scrtc->fbdev_output;
	info.hdl = sunxi_plane->hdl;
	info.hwde = sunxi_plane->crtc->sunxi_de;
	info.new_state = fake_state;
	info.old_state = fake_state;
	info.is_fbdev = true;
	sunxi_de_channel_update(&info);

	mutex_lock(&dev->master_mutex);
	update = dev->master ? false : true;
	mutex_unlock(&dev->master_mutex);
	if (update)
		sunxi_de_atomic_flush(scrtc->sunxi_de, NULL);
	else
		scrtc->fbdev_flush_pending = true;
	reinit_completion(&scrtc->flush_done);
	drm_modeset_unlock_all(dev);
	if (!update)
		wait_for_completion(&scrtc->flush_done);
	DRM_DEBUG_DRIVER("[SUNXI-DE] fbdev plane update finish self_update :%d\n", update);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{

	struct display_channel_state *old_cstate = to_display_channel_state(old_state);
#else
static void sunxi_plane_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)

{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct display_channel_state *old_cstate = to_display_channel_state(old_state);
#endif
	struct drm_plane_state *new_state = plane->state;
	struct display_channel_state *new_cstate = to_display_channel_state(new_state);
	struct sunxi_drm_plane *sunxi_plane = to_sunxi_plane(plane);
	struct sunxi_drm_crtc *scrtc = sunxi_plane->crtc;
	struct sunxi_de_channel_update info;

	info.hdl = sunxi_plane->hdl;
	info.hwde = scrtc->sunxi_de;
	info.new_state = new_cstate;
	info.old_state = old_cstate;
	info.is_fbdev = false;
	info.fbdev_output = scrtc->fbdev_output;
	sunxi_de_channel_update(&info);

	// drm blob test
	/*
	struct drm_property_blob *blob;
	blob = drm_property_create_blob(plane->dev, de_frontend_data_size(), NULL);
	drm_property_replace_blob(&new_cstate->frontend_blob, blob);
	*/
}

static int __maybe_unused sunxi_plane_atomic_precheck(struct drm_plane *plane,
				      struct drm_plane_state *new_state)
{
	struct display_channel_state *cstate = to_display_channel_state(new_state);
	bool remain_enable = false;
	struct sunxi_drm_plane *sunxi_plane = to_sunxi_plane(plane);
	struct sunxi_drm_crtc *scrtc = sunxi_plane->crtc;
	int i, ret;
	struct drm_framebuffer *fb = NULL;
	/* FIXME: add crtc enable when disable layer 123, when still remain layer is enabled */

	/* if layer_id is set, enable async update */
	if (cstate->layer_id != COMMIT_ALL_LAYER) {
		new_state->state->legacy_cursor_update = true;
		DRM_DEBUG_DRIVER("[SUNXI-DE] channel %s  %s %d\n", plane->name, __func__, __LINE__);
	}

	/* if async update, handle plane's fake fb and crtc */
	if (new_state->state->legacy_cursor_update) {
		if (cstate->layer_id == 0) {
			fb = new_state->fb;
			/* enable layer0: remove fake flag */
			if (fb) {
				cstate->fake_layer0 = false;
				DRM_DEBUG_DRIVER("[SUNXI-DE] channel %s 0 async update check enable\n", plane->name);
			/* disable layer0: only disable when all layer is disable */
			} else {
				for (i = 0; i < OVL_REMAIN; i++) {
					fb = cstate->fb[i];
					if (fb) {
						remain_enable = true;
						break;
					}
				}
				if (remain_enable) {
					drm_atomic_set_fb_for_plane(new_state, fb);
					ret = drm_atomic_set_crtc_for_plane(new_state, &scrtc->crtc);
					if (ret) {
						DRM_ERROR("[SUNXI-DE] async update check set crtc fail for fake layer0\n");
						return ret;
					}
					cstate->fake_layer0 = true;
					DRM_DEBUG_DRIVER("[SUNXI-DE] async update check plane disable plane %s by layer0 "
							  "with fake_layer0 add\n", plane->name);
				} else {
					DRM_DEBUG_DRIVER("[SUNXI-DE] async update check disable plane %s by layer %d\n",
							  plane->name, cstate->layer_id);
				}
			}
		} else {
			fb = cstate->fb[cstate->layer_id - 1];
			/* disable layer1/2/3: disable layer0 if all layer disable*/
			if (!fb && cstate->fake_layer0) {
				for (i = 0; i < OVL_REMAIN; i++) {
					if (cstate->fb[i]) {
						remain_enable = true;
						break;
					}
				}
				if (!remain_enable) {
					cstate->fake_layer0 = false;
					drm_atomic_set_fb_for_plane(new_state, NULL);
					ret = drm_atomic_set_crtc_for_plane(new_state, NULL);
					if (ret) {
						DRM_ERROR("[SUNXI-DE] async update check set crtc fail for remove fake layer0\n");
						return ret;
					}

					DRM_DEBUG_DRIVER("[SUNXI-DE] async update check plane disable plane %s by layer%d "
							  "with fake_layer0 add\n", plane->name, cstate->layer_id);
				}
			} else if (fb) {
				/* enable layer1/2/3: enable layer0 if layer0 is not enable */
				if (!cstate->fake_layer0 && !new_state->fb) {
						drm_atomic_set_fb_for_plane(new_state, fb);
						ret = drm_atomic_set_crtc_for_plane(new_state, &scrtc->crtc);
						if (ret) {
							DRM_ERROR("[SUNXI-DE] async update check set crtc fail for fake layer0\n");
							return ret;
						}
						DRM_DEBUG_DRIVER("[SUNXI-DE] channel %s %d async update check enable with fake layer0\n",
								  plane->name, cstate->layer_id);
						cstate->fake_layer0 = true;
				} else {
					DRM_DEBUG_DRIVER("[SUNXI-DE] channel %s %d async update check enable without fake layer0\n",
							  plane->name, cstate->layer_id);
				}
			} else {
				DRM_DEBUG_DRIVER("[SUNXI-DE] channel %s async update check layer%d  fb %lx\n",
							plane->name, cstate->layer_id, (unsigned long)(fb));
			}
		}
	}
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static int sunxi_plane_atomic_async_check(struct drm_plane *plane,
				      struct drm_plane_state *new_state)
#else
static int sunxi_plane_atomic_async_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
#endif
{
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_plane_atomic_async_update(struct drm_plane *plane,
				      struct drm_plane_state *new_state)
{
#else
static void sunxi_plane_atomic_async_update(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
#endif
	struct display_channel_state *cstate = to_display_channel_state(new_state);
	struct display_channel_state *old_cstate = to_display_channel_state(plane->state);
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(new_state->crtc);
	int i = cstate->layer_id - 1;

	old_cstate->fake_layer0 = cstate->fake_layer0;
	if (cstate->layer_id == 0) {
		plane->state->crtc_x = new_state->crtc_x;
		plane->state->crtc_y = new_state->crtc_y;
		plane->state->crtc_h = new_state->crtc_h;
		plane->state->crtc_w = new_state->crtc_w;
		plane->state->src_x = new_state->src_x;
		plane->state->src_y = new_state->src_y;
		plane->state->src_h = new_state->src_h;
		plane->state->src_w = new_state->src_w;
		swap(plane->state->fb, new_state->fb);
	} else {
		old_cstate->crtc_x[i] = cstate->crtc_x[i];
		old_cstate->crtc_y[i] = cstate->crtc_y[i];
		old_cstate->crtc_h[i] = cstate->crtc_h[i];
		old_cstate->crtc_w[i] = cstate->crtc_w[i];
		old_cstate->src_x[i] = cstate->src_x[i];
		old_cstate->src_y[i] = cstate->src_y[i];
		old_cstate->src_h[i] = cstate->src_h[i];
		old_cstate->src_w[i] = cstate->src_w[i];
		swap(old_cstate->fb[i], cstate->fb[i]);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	sunxi_plane_atomic_update(plane, &old_cstate->base);
#else
	sunxi_plane_atomic_update(plane, state);
#endif
	scrtc->async_update_flush_pending = true;
	DRM_DEBUG_DRIVER("[SUNXI-DE] channel %s %d async update\n", plane->name, cstate->layer_id);
}

static int sunxi_atomic_plane_set_property(struct drm_plane *plane,
					  struct drm_plane_state *state,
					  struct drm_property *property,
					  uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct sunxi_drm_private *private = to_sunxi_drm_private(plane->dev);
	struct display_channel_state *cstate = to_display_channel_state(state);
	int i, ret;
	bool replaced;

	for (i = 0; i < OVL_REMAIN; i++) {
		if (property == private->prop_blend_mode[i]) {
			cstate->pixel_blend_mode[i] = val;
			return 0;
		}

		if (property == private->prop_alpha[i]) {
			cstate->alpha[i] = val;
			return 0;
		}

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
				drm_framebuffer_assign(&cstate->fb[i], NULL);
				return 0;
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

	/* how to set blob from userspace:
	 * use drmModeCreatePropertyBlob to request and fill a blob by ioctl,
	 * get the blob id and use add_property/add_property_optional to deliver
	 * to KMS. Finally the follow case would be called
	 */
	if (property == private->prop_frontend_data) {
		ret = sunxi_plane_replace_property_blob_from_id(dev,
				&cstate->frontend_blob,
				val, de_frontend_data_size(),
				&replaced);
		return ret;
	}

	if (property == private->prop_eotf) {
		cstate->eotf = val;
		return 0;
	}

	if (property == private->prop_color_space) {
		cstate->color_space = val;
		return 0;
	}

	if (property == private->prop_color_range) {
		cstate->color_range = val;
		return 0;
	}

	/* the read val of this prop is meaningless for userspace */
	if (property == private->prop_layer_id) {
		cstate->layer_id = val;
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
		if (property == private->prop_blend_mode[i]) {
			*val = cstate->pixel_blend_mode[i];
			return 0;
		}

		if (property == private->prop_alpha[i]) {
			*val = cstate->alpha[i];
			return 0;
		}

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

	if (property == private->prop_frontend_data) {
		*val = (cstate->frontend_blob) ? cstate->frontend_blob->base.id : 0;
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

	if (property == private->prop_color_range) {
		*val = cstate->color_range;
		return 0;
	}

	/* the read val of this prop is meaningless for userspace */
	if (property == private->prop_layer_id) {
		*val = cstate->layer_id;
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
	int i;

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
	for (i = 0; i < MAX_LAYER_NUM_PER_CHN - 1; i++) {
		state->alpha[i] = DRM_BLEND_ALPHA_OPAQUE;
		state->pixel_blend_mode[i] = DRM_MODE_BLEND_PREMULTI;
	}
	state->eotf = DE_EOTF_BT709;
	state->color_space = DE_COLOR_SPACE_BT709;
	state->color_range = DE_COLOR_RANGE_DEFAULT;
	state->fake_layer0 = false;
	state->layer_id = COMMIT_ALL_LAYER;
}

static bool sunxi_plane_format_mod_supported(struct drm_plane *plane, u32 format, u64 modifier)
{
	struct sunxi_drm_plane *sunxi_plane = to_sunxi_plane(plane);
	struct sunxi_drm_crtc *scrtc = sunxi_plane->crtc;
	return sunxi_de_format_mod_supported(scrtc->sunxi_de, sunxi_plane->hdl, format, modifier);
}

void sunxi_plane_print_state(struct drm_printer *p,
				   const struct drm_plane_state *state, bool state_only)
{
	struct sunxi_drm_plane *sunxi_plane = to_sunxi_plane(state->plane);
	struct sunxi_drm_crtc *scrtc = sunxi_plane->crtc;
	struct display_channel_state *cstate = to_display_channel_state(state);
	sunxi_de_dump_channel_state(p, scrtc->sunxi_de, sunxi_plane->hdl, cstate, state_only);
}

static void sunxi_plane_atomic_print_state(struct drm_printer *p,
				   const struct drm_plane_state *state)
{
	sunxi_plane_print_state(p, state, true);
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
	.format_mod_supported = sunxi_plane_format_mod_supported,
	.atomic_print_state = sunxi_plane_atomic_print_state,
};

static const struct drm_plane_helper_funcs sunxi_plane_helper_funcs = {
	.atomic_update = sunxi_plane_atomic_update,
	.atomic_async_check = sunxi_plane_atomic_async_check,
	.atomic_async_update = sunxi_plane_atomic_async_update,
	/* atomic_precheck is a private function ptr add by sunxi,
	 * should be used with additonal patch for kernel drm framework
	 */
#ifdef SUNXI_DRM_PLANE_ASYNC
	.atomic_precheck = sunxi_plane_atomic_precheck,
#endif
};

static int create_extra_blend_mode_prop(struct sunxi_drm_private *pri, unsigned int index,
					 unsigned int supported_modes)
{
	char name[32];
	struct drm_device *dev = &pri->base;
	struct drm_property *prop;
	static const struct drm_prop_enum_list props[] = {
		{ DRM_MODE_BLEND_PIXEL_NONE, "None" },
		{ DRM_MODE_BLEND_PREMULTI, "Pre-multiplied" },
		{ DRM_MODE_BLEND_COVERAGE, "Coverage" },
	};
	unsigned int valid_mode_mask = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
				       BIT(DRM_MODE_BLEND_PREMULTI)   |
				       BIT(DRM_MODE_BLEND_COVERAGE);
	int i;

	if (WARN_ON((supported_modes & ~valid_mode_mask) ||
		    ((supported_modes & BIT(DRM_MODE_BLEND_PREMULTI)) == 0)))
		return -EINVAL;

	sprintf(name, "pixel blend mode%d", index + 1);
	prop = drm_property_create(dev, DRM_MODE_PROP_ENUM,
				   name,
				   hweight32(supported_modes));
	if (!prop)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		int ret;

		if (!(BIT(props[i].type) & supported_modes))
			continue;

		ret = drm_property_add_enum(prop, props[i].type,
					    props[i].name);

		if (ret) {
			drm_property_destroy(dev, prop);

			return ret;
		}
	}

	pri->prop_blend_mode[index] = prop;

	return 0;
}

int sunxi_drm_plane_property_create(struct sunxi_drm_private *private)
{
	struct drm_device *dev = &private->base;
	struct drm_property *prop;
	char name[32];
	int i;

	prop = drm_property_create_range(dev, 0, "layer_id",
					 0, MAX_LAYER_NUM_PER_CHN);
	if (!prop)
		return -ENOMEM;
	private->prop_layer_id = prop;

	for (i = 0; i < OVL_REMAIN; i++) {
		if (create_extra_blend_mode_prop(private, i,
						  DRM_MODE_BLEND_PIXEL_NONE |
						  DRM_MODE_BLEND_PREMULTI|
						  DRM_MODE_BLEND_COVERAGE))
			return -ENOMEM;

		sprintf(name, "alpha%d", i + 1);
		prop = drm_property_create_range(dev, 0, name,
						 0, DRM_BLEND_ALPHA_OPAQUE);
		if (!prop)
			return -ENOMEM;
		private->prop_alpha[i] = prop;

		sprintf(name, "SRC_X%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_src_x[i] = prop;

		sprintf(name, "SRC_Y%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_src_y[i] = prop;

		sprintf(name, "SRC_W%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_src_w[i] = prop;

		sprintf(name, "SRC_H%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_src_h[i] = prop;

		sprintf(name, "CRTC_X%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_crtc_x[i] = prop;

		sprintf(name, "CRTC_Y%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_crtc_y[i] = prop;

		sprintf(name, "CRTC_W%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_crtc_w[i] = prop;

		sprintf(name, "CRTC_H%d", i + 1);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, INT_MIN, INT_MAX);
		if (!prop)
			return -ENOMEM;
		private->prop_crtc_h[i] = prop;

		sprintf(name, "FB_ID%d", i + 1);
		prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
				name, DRM_MODE_OBJECT_FB);
		if (!prop)
			return -ENOMEM;
		private->prop_fb_id[i] = prop;

		sprintf(name, "COLOR");
		if (i != 0)
			sprintf(name, "COLOR%d", i);
		prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
				name, 0, 0xffffffff);
		if (!prop)
			return -ENOMEM;
		private->prop_color[i] = prop;
	}

	sprintf(name, "COLOR%d", i);
	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			name, 0, 0xffffffff);
	if (!prop)
		return -ENOMEM;
	private->prop_color[i] = prop;

	return 0;
}

static void sunxi_drm_plane_property_init(struct sunxi_drm_plane *plane, unsigned int channel_cnt, bool afbc_rot_support)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(plane->plane.dev);
#if DRM_OBJECT_MAX_PROPERTY >= 57
	int i;
#endif

	drm_plane_create_alpha_property(&plane->plane);
	drm_plane_create_blend_mode_property(&plane->plane,
					BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					BIT(DRM_MODE_BLEND_PREMULTI) |
					BIT(DRM_MODE_BLEND_COVERAGE));
	drm_plane_create_zpos_property(&plane->plane, plane->index, 0, channel_cnt - 1);
	if (afbc_rot_support)
		drm_plane_create_rotation_property(&plane->plane, DRM_MODE_ROTATE_0, DRM_MODE_ROTATE_0 |
						    DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 | DRM_MODE_ROTATE_270 |
						    DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y);

	/* { FB_ID CRTC_X(YWH) SRC_X(YWH) COLOR "pixel blend mode" alpha} * 4 = 48
	IN_FORMATS CRTC_ID type IN_FENCE_FD zpos EOTF COLOR_SPACE COLOR_RANGE rotation =  9 */

//FIXME remove maroc
#if DRM_OBJECT_MAX_PROPERTY >= 57
	for (i = 0; i < plane->layer_cnt - 1 && plane->layer_cnt > 1; i++) {
		drm_object_attach_property(&plane->plane.base, pri->prop_blend_mode[i], DRM_MODE_BLEND_PREMULTI);
		drm_object_attach_property(&plane->plane.base, pri->prop_alpha[i], DRM_BLEND_ALPHA_OPAQUE);
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
	drm_object_attach_property(&plane->plane.base, pri->prop_color[i], 0);
#else
	drm_object_attach_property(&plane->plane.base, pri->prop_color[0], 0);
#endif
	drm_object_attach_property(&plane->plane.base, pri->prop_layer_id, COMMIT_ALL_LAYER);
	drm_object_attach_property(&plane->plane.base, pri->prop_frontend_data, 0);
	drm_object_attach_property(&plane->plane.base, pri->prop_eotf, 0);
	drm_object_attach_property(&plane->plane.base, pri->prop_color_space, 0);
	drm_object_attach_property(&plane->plane.base, pri->prop_color_range, 0);
}

static int sunxi_drm_plane_init(struct drm_device *dev,
				struct sunxi_drm_crtc *scrtc,
				uint32_t possible_crtc,
				struct sunxi_drm_plane *plane, int type,
				unsigned int de_id, const struct sunxi_plane_info *info)
{
	plane->crtc = scrtc;
	plane->hdl = info->hdl;
	plane->index = info->index;
	plane->layer_cnt = info->layer_cnt;

	if (drm_universal_plane_init(dev, &plane->plane, possible_crtc,
				     &sunxi_plane_funcs, info->formats, info->format_count,
				     info->format_modifiers, type,
				     "plane-%d-%s(%d)", plane->index, info->name, de_id)) {
		DRM_ERROR("drm_universal_plane_init failed\n");
		return -1;
	}

	drm_plane_helper_add(&plane->plane, &sunxi_plane_helper_funcs);
	sunxi_drm_plane_property_init(plane, scrtc->plane_cnt, info->afbc_rot_support);
	return 0;
}
/* plane end*/

static void wb_finish_proc(struct sunxi_drm_crtc *scrtc)
{
	int i;
	struct sunxi_drm_wb *wb;
	unsigned long flags;
	struct wb_signal_wait *wait;
	bool signal = false;

	spin_lock_irqsave(&scrtc->wb_lock, flags);
	wb = scrtc->wb;
	spin_unlock_irqrestore(&scrtc->wb_lock, flags);
	if (!wb) {
		return;
	}

	spin_lock_irqsave(&wb->signal_lock, flags);
	for (i = 0; i < WB_SIGNAL_MAX; i++) {
		wait = &wb->signal[i];
		if (wait->active) {
			wait->vsync_cnt++;
			if (wait->vsync_cnt == 2) {
				wait->active = 0;
				wait->vsync_cnt = 0;
				signal = true;
			}
		}
	}
	spin_unlock_irqrestore(&wb->signal_lock, flags);
	if (signal)
		drm_writeback_signal_completion(&wb->wb_connector, 0);
}

static int sunxi_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_crtc *crtc;
	unsigned int cnt = 0;
	struct drm_display_mode *mode;

	list_for_each_entry(crtc, &config->crtc_list, head) {
		mode = NULL;
		if (crtc->state) {
			mode = drm_mode_duplicate(dev, &crtc->state->mode);
			if (mode) {
				drm_mode_probed_add(connector, mode);
				cnt++;
			}
		}
	}

	cnt += drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
	return cnt;
}

static void commit_new_wb_job(struct sunxi_drm_crtc *scrtc, struct sunxi_drm_wb *wb)
{
	int i;
	unsigned long flags;
	bool found = false;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(scrtc->crtc.state);


	DRM_INFO("[SUNXI-DE] %s start\n", __FUNCTION__);
	/* find a free signal slot */
	spin_lock_irqsave(&wb->signal_lock, flags);
	for (i = 0; i < WB_SIGNAL_MAX; i++) {
		if (wb->signal[i].active == false) {
			DRM_DEBUG_DRIVER("[SUNXI-DE] set wb for crtc\n");
			wb->signal[i].active = true;
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&wb->signal_lock, flags);

	/* add wb for isr to signal wb job */
	WARN(!found, "no free wb active signal slot\n");
	spin_lock_irqsave(&scrtc->wb_lock, flags);
	scrtc_state->wb = NULL;
	scrtc->wb = wb;
	spin_unlock_irqrestore(&scrtc->wb_lock, flags);
	sunxi_de_write_back(scrtc->sunxi_de, wb->hw_wb, wb->wb_connector.base.state->writeback_job->fb);
	drm_writeback_queue_job(&wb->wb_connector, wb->wb_connector.base.state);
}

static void disable_and_reset_wb(struct sunxi_drm_crtc *scrtc)
{
	bool all_finish = true;
	struct sunxi_drm_wb *wb = scrtc->wb;
	unsigned long flags;
	int i;

	if (wb) {
		/* check if all jobs finsh */
		spin_lock_irqsave(&wb->signal_lock, flags);
		for (i = 0; i < WB_SIGNAL_MAX; i++) {
			if (wb->signal[i].active == true) {
				all_finish = false;
				break;
			}
		}
		spin_unlock_irqrestore(&wb->signal_lock, flags);

		/* disable wb if all jobs finish  */
		if (all_finish) {
			DRM_DEBUG_DRIVER("[SUNXI-DE] rm wb for crtc\n");
			spin_lock_irqsave(&scrtc->wb_lock, flags);
			scrtc->wb = NULL;
			spin_unlock_irqrestore(&scrtc->wb_lock, flags);
			sunxi_de_write_back(scrtc->sunxi_de, wb->hw_wb, NULL);
		}
	}
}

static void sunxi_wb_commit(struct sunxi_drm_crtc *scrtc)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(scrtc->crtc.state);
	struct sunxi_drm_wb *wb = scrtc_state->wb;
	struct drm_writeback_connector *wb_conn = wb ? &wb->wb_connector : NULL;
	struct drm_connector_state *conn_state = wb_conn ? wb_conn->base.state : NULL;
	struct drm_writeback_job *job = conn_state ? conn_state->writeback_job : NULL;
	struct drm_framebuffer *fb = job ? job->fb : NULL;

	if (fb)
		commit_new_wb_job(scrtc, wb);
	else
		disable_and_reset_wb(scrtc);
}

static int sunxi_wb_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)

{
	int i, ret = 0;
	unsigned long flags;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_wb *wb = container_of(encoder, struct sunxi_drm_wb, wb_connector.encoder);
	if (!crtc_state->active) {
		DRM_ERROR("[SUNXI-DE] wb check fail, crtc is not enabled %s %d \n", __FUNCTION__, __LINE__);
		return -EINVAL;
	}
	spin_lock_irqsave(&wb->signal_lock, flags);
	for (i = 0; i < WB_SIGNAL_MAX; i++) {
		if (wb->signal[i].active) {
			//ret = -EBUSY;
			DRM_ERROR("[SUNXI-DE] wb check fail, pending wb not finish %s %d \n", __FUNCTION__, __LINE__);
		}
	}
	spin_unlock_irqrestore(&wb->signal_lock, flags);
	/* user should make sure not to switch connector and request wb on the same commit*/
	if (crtc_state->mode_changed || crtc_state->connectors_changed) {
		crtc_state->mode_changed = false;
		crtc_state->connectors_changed = false;
		DRM_INFO("[SUNXI-DE] skip mode change and connector change for wb enable %s %d \n", __FUNCTION__, __LINE__);
	}
	if (!ret) {
		scrtc_state->wb = wb;
		DRM_DEBUG_DRIVER("[SUNXI-DE] %s add new fb\n", __func__);
	}
	return ret;
}

static const struct drm_connector_helper_funcs sunxi_wb_connector_helper_funcs = {
	.get_modes = sunxi_wb_connector_get_modes,
};

static const struct drm_connector_funcs sunxi_wb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_helper_funcs sunxi_wb_encoder_helper_funcs = {
	.atomic_check = sunxi_wb_encoder_atomic_check,
};

struct sunxi_drm_wb *sunxi_drm_wb_init_one(struct sunxi_de_wb_info *wb_info)
{
	int ret;
	struct drm_device *drm = wb_info->drm;
	struct sunxi_drm_wb *wb = devm_kzalloc(drm->dev, sizeof(*wb), GFP_KERNEL);
	static const u32 formats_wb[] = {
		DRM_FORMAT_ARGB8888,//TODO add more format base on hw feat
	};

	if (!wb) {
		DRM_ERROR("allocate memory for drm_wb fail\n");
		return ERR_PTR(-ENOMEM);
	}
	wb->hw_wb = wb_info->wb;
	spin_lock_init(&wb->signal_lock);
	wb->wb_connector.encoder.possible_crtcs = wb_info->support_disp_mask;
	drm_connector_helper_add(&wb->wb_connector.base, &sunxi_wb_connector_helper_funcs);

	ret = drm_writeback_connector_init(drm, &wb->wb_connector,
					   &sunxi_wb_connector_funcs,
					   &sunxi_wb_encoder_helper_funcs,
					   formats_wb, 1
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
		);
#else
		, wb_info->support_disp_mask);
#endif
	if (ret) {
		DRM_ERROR("writeback connector init failed\n");
		return NULL;
	} else
		return wb;
}

void sunxi_drm_wb_destory(struct sunxi_drm_wb *wb)
{
//TODO
}

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

irqreturn_t sunxi_crtc_event_proc(int irq, void *crtc)
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);
	bool timeout;

	scrtc->irqcnt++;
	if (scrtc->check_status(scrtc->output_dev_data))
		scrtc->fifo_err++;
	SUNXIDRM_TRACE_INT2("crtc-irq-", scrtc->hw_id, scrtc->irqcnt & 1);

	timeout = !scrtc->is_sync_time_enough(scrtc->output_dev_data);
	sunxi_de_event_proc(scrtc->sunxi_de, timeout);

	wb_finish_proc(scrtc);
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
	struct drm_device *drm = crtc->dev;
	struct drm_mode_config *config = &drm->mode_config;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(state);

	if (property == config->tv_brightness_property) {
		scrtc_state->excfg.brightness = val;
		scrtc_state->bcsh_changed = true;
		return 0;
	} else if (property == config->tv_contrast_property) {
		scrtc_state->excfg.contrast = val;
		scrtc_state->bcsh_changed = true;
		return 0;
	} else if (property == config->tv_saturation_property) {
		scrtc_state->excfg.saturation = val;
		scrtc_state->bcsh_changed = true;
		return 0;
	} else if (property == config->tv_hue_property) {
		scrtc_state->excfg.hue = val;
		scrtc_state->bcsh_changed = true;
		return 0;
	}
	return -EINVAL;
}

static int sunxi_crtc_atomic_get_property(struct drm_crtc *crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	struct drm_device *drm = crtc->dev;
	struct drm_mode_config *config = &drm->mode_config;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(state);

	if (property == config->tv_brightness_property) {
		*val = scrtc_state->excfg.brightness;
		return 0;
	} else if (property == config->tv_contrast_property) {
		*val = scrtc_state->excfg.contrast;
		return 0;
	} else if (property == config->tv_saturation_property) {
		*val = scrtc_state->excfg.saturation;
		return 0;
	} else if (property == config->tv_hue_property) {
		*val = scrtc_state->excfg.hue;
		return 0;
	}
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
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	if (crtc->state)
		sunxi_crtc_destroy_state(crtc, crtc->state);
	state->px_fmt_space = DE_FORMAT_SPACE_RGB;
	state->yuv_sampling = DE_YUV444;
	state->eotf = DE_EOTF_BT709;
	state->color_space = DE_COLOR_SPACE_BT709;
	state->color_range = DE_COLOR_RANGE_0_255;
	state->data_bits = DE_DATA_8BITS;
	state->clk_freq = scrtc->clk_freq;
	state->excfg.brightness = 50;
	state->excfg.contrast = 50;
	state->excfg.saturation = 50;
	state->excfg.hue = 50;
	__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

void sunxi_crtc_atomic_print_state(struct drm_printer *p,
				   const struct drm_crtc_state *state)
{
	unsigned long flags;
	struct sunxi_drm_wb *wb;
	struct sunxi_crtc_state *cstate = to_sunxi_crtc_state(state);
	struct sunxi_drm_crtc *scrtc = (struct sunxi_drm_crtc *)state->crtc;
	int w = state->mode.hdisplay;
	int h = state->mode.vdisplay;
	int fps = drm_mode_vrefresh(&state->mode);

	drm_printf(p, "\n\t%s: ", scrtc->enabled ? "on" : "off\n");
	if (scrtc->enabled) {
		drm_printf(p, "%dx%d@%d&%dMhz->tcon%d irqcnt=%d err=%d\n", w, h, fps,
			    (int)(scrtc->clk_freq / 1000000), cstate->tcon_id, scrtc->irqcnt, scrtc->fifo_err);
		drm_printf(p, "\t    format_space: %d yuv_sampling: %d eotf:%d cs: %d"
			    " color_range: %d data_bits: %d\n", cstate->px_fmt_space,
			    cstate->yuv_sampling, cstate->eotf, cstate->color_space,
			    cstate->color_range, cstate->data_bits);

		spin_lock_irqsave(&scrtc->wb_lock, flags);
		wb = scrtc->wb;
		if (!wb) {
			drm_printf(p, "\twb off\n");
		} else {
			drm_printf(p, "\twb on:\n\t\t[0]: %s %d\n\t\t[1]: %s %d\n",
				    wb->signal[0].active ? "waiting" : "finish", wb->signal[0].vsync_cnt,
				    wb->signal[1].active ? "waiting" : "finish", wb->signal[1].vsync_cnt);
		}
		spin_unlock_irqrestore(&scrtc->wb_lock, flags);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_crtc_atomic_enable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)

#else
static void sunxi_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
#endif
{
	//struct sunxi_drm_private *pri = to_sunxi_drm_private(crtc->dev);
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);
	struct drm_crtc_state *new_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(new_state);
	struct drm_mode_modeinfo modeinfo;
	struct sunxi_de_out_cfg cfg;
	bool sw_enable = false;

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

	SUNXIDRM_TRACE_BEGIN(__func__);

	scrtc->enable_vblank = scrtc_state->enable_vblank;
	scrtc->check_status = scrtc_state->check_status;
	scrtc->is_sync_time_enough = scrtc_state->is_sync_time_enough;
	scrtc->output_dev_data = scrtc_state->output_dev_data;

	drm_property_blob_get(new_state->mode_blob);
	memcpy(&modeinfo, new_state->mode_blob->data,
	       new_state->mode_blob->length);
	drm_property_blob_put(new_state->mode_blob);
	memset(&cfg, 0, sizeof(cfg));
	cfg.sw_enable = sw_enable;
	cfg.hwdev_index = scrtc_state->tcon_id;
	cfg.width = modeinfo.hdisplay;
	cfg.height = modeinfo.vdisplay;
	cfg.device_fps = modeinfo.vrefresh;
	cfg.px_fmt_space = scrtc_state->px_fmt_space;
	cfg.yuv_sampling = scrtc_state->yuv_sampling;
	cfg.eotf = scrtc_state->eotf;
	cfg.color_space = scrtc_state->color_space;
	cfg.color_range = scrtc_state->color_range;
	cfg.data_bits = scrtc_state->data_bits;

	if (sunxi_de_enable(scrtc->sunxi_de, &cfg) < 0)
		DRM_ERROR("sunxi_de_enable failed\n");

	scrtc->enabled = true;
	drm_crtc_vblank_on(crtc);
	SUNXIDRM_TRACE_END(__func__);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static void sunxi_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)

#else
static void sunxi_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
#endif

{
	unsigned long flags;
	struct sunxi_drm_wb *wb;
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	DRM_INFO("[SUNXI-CRTC]%s\n", __func__);
	drm_crtc_vblank_off(crtc);
	if (!scrtc->enabled) {
		DRM_ERROR("%s: crtc has been disabled\n", __func__);
		return;
	}

	/* remove not finish wb  */
	spin_lock_irqsave(&scrtc->wb_lock, flags);
	wb = scrtc->wb ? scrtc->wb : NULL;
	scrtc->wb = NULL;
	spin_unlock_irqrestore(&scrtc->wb_lock, flags);

//clear wb signal ??? drm_writeback_signal_completion??
	if (wb) {
		sunxi_de_write_back(scrtc->sunxi_de, wb->hw_wb, NULL);
	}

	scrtc->enabled = false;
	sunxi_de_disable(scrtc->sunxi_de);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);
		crtc->state->event = NULL;
	}

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
	struct drm_plane *plane;
	struct sunxi_drm_plane *sunxi_plane;

	DRM_DEBUG_DRIVER("%s\n", __func__);
	SUNXIDRM_TRACE_BEGIN(__func__);
	if (crtc->state->event) {
		drm_crtc_vblank_get(crtc);
		spin_lock_irqsave(&dev->event_lock, flags);
		scrtc->event = crtc->state->event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
		crtc->state->event = NULL;
	}
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		sunxi_plane = to_sunxi_plane(plane);
		if (sunxi_plane->index == scrtc->fbdev_chn_id) {
			if (plane->state && plane->state->fb)
				scrtc->fbdev_output = false;
			break;
		}
	}

	sunxi_de_atomic_begin(scrtc->sunxi_de);
	SUNXIDRM_TRACE_END(__func__);
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
	struct drm_crtc_state *new_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(new_state);
	struct sunxi_de_flush_cfg cfg;
	bool all_dirty = crtc->state->mode_changed;
	SUNXIDRM_TRACE_BEGIN(__func__);
	sunxi_wb_commit(scrtc);
	memset(&cfg, 0, sizeof(cfg));
	if (all_dirty || crtc->state->color_mgmt_changed) {
		if (crtc->state->gamma_lut) {
			cfg.gamma_lut = crtc->state->gamma_lut->data;
			cfg.gamma_dirty = true;
		}
	}

	if (all_dirty || scrtc_state->bcsh_changed) {
		cfg.brightness = scrtc_state->excfg.brightness;
		cfg.contrast = scrtc_state->excfg.contrast;
		cfg.saturation = scrtc_state->excfg.saturation;
		cfg.hue = scrtc_state->excfg.hue;
		cfg.bcsh_dirty = true;
	}
	sunxi_de_atomic_flush(scrtc->sunxi_de, &cfg);
	sunxi_crtc_finish_page_flip(crtc->dev, scrtc);

	if (scrtc->fbdev_flush_pending) {
		scrtc->fbdev_flush_pending = false;
		complete_all(&scrtc->flush_done);
	}

	if (scrtc->async_update_flush_pending) {
		scrtc->async_update_flush_pending = false;
	}

	SUNXIDRM_TRACE_END(__func__);
	DRM_DEBUG_DRIVER("%s finish\n", __func__);
}

static int sunxi_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	DRM_DEBUG_DRIVER("%s\n", __func__);
	if (scrtc->enable_vblank == NULL) {
		DRM_ERROR("enable vblank is not registerd!\n");
		return -1;
	}
	scrtc->enable_vblank(true, scrtc->output_dev_data);
	return 0;
}

static void sunxi_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(crtc);

	DRM_DEBUG_DRIVER("%s\n", __func__);
	if (scrtc->enable_vblank == NULL) {
		DRM_ERROR("enable vblank is not registerd!\n");
		return;
	}
	scrtc->enable_vblank(false, scrtc->output_dev_data);
}

s32 commit_task_thread(void *parg)
{
	struct sunxi_drm_crtc *scrtc = to_sunxi_crtc(parg);
	struct drm_device *dev = scrtc->crtc.dev;
	if (!parg) {
		DRM_ERROR("NUll ndl\n");
		return -1;
	}

	while (1) {
		if (kthread_should_stop())
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(nsecs_to_jiffies(16666666));

		drm_modeset_lock_all(dev);
		if (scrtc->fbdev_flush_pending || scrtc->async_update_flush_pending) {
			DRM_DEBUG_DRIVER("[SUNXI-DE] thread flush fbdev:%d async%d\n",
					  scrtc->fbdev_flush_pending,
					  scrtc->async_update_flush_pending);

			sunxi_de_atomic_flush(scrtc->sunxi_de, NULL);
			if (scrtc->fbdev_flush_pending) {
				scrtc->fbdev_flush_pending = false;
				complete_all(&scrtc->flush_done);
			}
			if (scrtc->async_update_flush_pending) {
				scrtc->async_update_flush_pending = false;
			}
		}
		drm_modeset_unlock_all(dev);
	}
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
	if (crtc_state->enable && (!scrtc_state->output_dev_data ||
	    !scrtc_state->enable_vblank ||
	    !scrtc_state->check_status)) {
		DRM_ERROR("invalid output device info\n");
		return -EINVAL;
	}
	return 0;
}

static const struct drm_crtc_funcs sunxi_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = sunxi_crtc_reset,
	.atomic_print_state = sunxi_crtc_atomic_print_state,
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

static void sunxi_drm_crtc_property_init(struct sunxi_drm_crtc *crtc)
{
	struct drm_device *drm = crtc->crtc.dev;
	struct drm_mode_config *conf = &drm->mode_config;
	/* although for drm core bcsh is consider as connector's prop, we add
	 *  them for crtc, add handle them ourself.
	 */
	drm_object_attach_property(&crtc->crtc.base, conf->tv_brightness_property, 50);
	drm_object_attach_property(&crtc->crtc.base, conf->tv_contrast_property, 50);
	drm_object_attach_property(&crtc->crtc.base, conf->tv_saturation_property, 50);
	drm_object_attach_property(&crtc->crtc.base, conf->tv_hue_property, 50);
}

void sunxi_drm_crtc_wait_one_vblank(struct sunxi_drm_crtc *scrtc)
{
	drm_crtc_wait_one_vblank(&scrtc->crtc);
}

struct sunxi_drm_crtc *sunxi_drm_crtc_init_one(struct sunxi_de_info *info)
{
	struct sunxi_drm_crtc *scrtc;
	struct drm_device *drm = info->drm;
	const struct sunxi_plane_info *plane = NULL;
	int i, ret;
	int primary_cnt = 0;
	int primary_index = 0;

	scrtc = devm_kzalloc(drm->dev, sizeof(*scrtc), GFP_KERNEL);
	if (!scrtc) {
		DRM_ERROR("allocate memory for sunxi_crtc fail\n");
		return ERR_PTR(-ENOMEM);
	}
	spin_lock_init(&scrtc->wb_lock);
	init_completion(&scrtc->flush_done);

	scrtc->allow_sw_enable = true;
	scrtc->sunxi_de = info->de_out;
	scrtc->hw_id = info->hw_id;
	scrtc->plane_cnt = info->plane_cnt;
	scrtc->crtc.port = info->port;
	scrtc->clk_freq = info->clk_freq;
	scrtc->plane =
		devm_kzalloc(drm->dev,
			     sizeof(*scrtc->plane) * info->plane_cnt,
			     GFP_KERNEL);
	if (!scrtc->plane) {
		DRM_ERROR("allocate mem for planes fail\n");
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < info->plane_cnt; i++) {
		if (info->planes[i].is_primary) {
			plane = &info->planes[i];
			primary_index = i;
			primary_cnt++;
		}
	}

	if (!plane || primary_cnt > 1) {
		DRM_ERROR("primary plane for de %d cfg err cnt %d\n", info->hw_id, primary_cnt);
		goto err_out;
	}

	/* create primary plane for crtc */
	ret = sunxi_drm_plane_init(drm, scrtc, 0, &scrtc->plane[primary_index],
				   DRM_PLANE_TYPE_PRIMARY, info->hw_id,
				   plane);
	if (ret) {
		DRM_ERROR("plane init fail for de %d\n", info->hw_id);
		goto err_out;
	}

	/* create crtc with primary plane */
	drm_crtc_init_with_planes(drm, &scrtc->crtc,
				  &scrtc->plane[primary_index].plane, NULL,
				  &sunxi_crtc_funcs, "DE-%d", info->hw_id);
	/* create overlay planes with remain channels for the specified crtc */
	for (i = 0; i < info->plane_cnt; i++) {
		plane = &info->planes[i];
		if (plane->is_primary)
			continue;
		ret = sunxi_drm_plane_init(drm, scrtc, drm_crtc_mask(&scrtc->crtc),
				     &scrtc->plane[i], DRM_PLANE_TYPE_OVERLAY,
				     info->hw_id, plane);
		if (ret) {
			DRM_ERROR("sunxi plane init for %d fail\n", i);
			goto err_out;
		}
	}

	drm_crtc_enable_color_mgmt(&scrtc->crtc, 0, false, info->gamma_lut_len);
	sunxi_drm_crtc_property_init(scrtc);
	drm_crtc_helper_add(&scrtc->crtc, &sunxi_crtc_helper_funcs);

#ifdef SUNXI_DRM_PLANE_ASYNC
	if (!commit_task) {
		commit_task = kthread_create(commit_task_thread,
					      &scrtc->crtc, "commit_task");
		if (IS_ERR(commit_task)) {
			DRM_ERROR("create thread fail\n");
			return (void *)commit_task;
		}
		wake_up_process(commit_task);
	}
#endif
	return scrtc;

err_out:
	for (i = 0; i < scrtc->plane_cnt; i++) {
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
#ifdef SUNXI_DRM_PLANE_ASYNC
	if (commit_task) {
		kthread_stop(commit_task);
		commit_task = NULL;
	}
#endif
	for (i = 0; i < scrtc->plane_cnt; i++) {
		drm_plane_cleanup(&scrtc->plane[i].plane);
	}
	drm_crtc_cleanup(&scrtc->crtc);
}
