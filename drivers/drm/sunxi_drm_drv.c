/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/* sunxi_drm_drv.c
 *
 * Copyright (C) 2023 Allwinnertech Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/version.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_modes.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
#include <drm/drm_gem_cma_helper.h>
#else
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#endif
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include "sunxi_drm_drv.h"
#include "sunxi_drm_crtc.h"
#include "sunxi_drm_debug.h"

#define DRIVER_NAME "sunxi-drm"
#define DRIVER_DESC "allwinnertech SoC DRM"
#define DRIVER_DATE "20230901"
#define DRIVER_MAJOR 3
#define DRIVER_MINOR 0

struct sunxi_init_connecting {
	struct list_head list;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	bool done;
	//TODO add eotf colorspace etc
};

struct display_boot_info {
	unsigned int de_id;
	unsigned int tcon_id;
	unsigned int connector_type;//DRM_MODE_CONNECTOR_
	unsigned int hw_id;//DRM_MODE_CONNECTOR_
	struct drm_display_mode mode;
/*	unsigned int mode;
	unsigned int format;
	unsigned int bits;
	unsigned int colorspace;
	unsigned int eotf;*/

	enum de_format_space px_fmt_space;
	enum de_yuv_sampling yuv_sampling;
	enum de_eotf eotf;
	enum de_color_space color_space;
	enum de_color_range color_range;
	enum de_data_bits data_bits;

	struct sunxi_logo_info logo;
	struct list_head list;
};

struct sunxi_drm_pri {
	struct list_head connecting_head;
	struct list_head boot_info_head;
};

extern struct platform_driver sunxi_de_platform_driver;
extern struct platform_driver sunxi_hdmi_platform_driver;
extern struct platform_driver sunxi_drm_edp_platform_driver;
extern struct platform_driver sunxi_tcon_platform_driver;
extern struct platform_driver sunxi_tcon_top_platform_driver;
extern struct platform_driver sunxi_dsi_platform_driver;
extern struct platform_driver sunxi_lvds_platform_driver;
extern struct platform_driver sunxi_rgb_platform_driver;
extern struct platform_driver sunxi_dsi_combo_phy_platform_driver;

int sunxi_fbdev_init(struct drm_device *drm, struct display_channel_state *out_state);
int sunxi_drm_plane_property_create(struct sunxi_drm_private *private);

static void sunxi_drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	sunxidrm_debug_trace_framebuffer_unmap(fb);
	drm_gem_fb_destroy(fb);
}

static const struct drm_framebuffer_funcs sunxi_drm_gem_fb_funcs = {
	.destroy = sunxi_drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
};

struct drm_framebuffer *
sunxi_drm_gem_fb_create(struct drm_device *dev, struct drm_file *file,
			const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_funcs(dev, file, mode_cmd,
					    &sunxi_drm_gem_fb_funcs);
}

static const struct drm_mode_config_funcs sunxi_drm_mode_config_funcs = {
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.fb_create = sunxi_drm_gem_fb_create,
};

static void sunxi_drm_atomic_helper_commit_tail_rpm(struct drm_atomic_state *old_state)

{
	struct drm_device *dev = old_state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);

	drm_atomic_helper_fake_vblank(old_state);

	drm_atomic_helper_commit_hw_done(old_state);

/*	drm_atomic_helper_wait_for_vblanks(dev, old_state);*/

	drm_atomic_helper_cleanup_planes(dev, old_state);
}


static const struct drm_mode_config_helper_funcs sunxi_mode_config_helpers = {
	.atomic_commit_tail = sunxi_drm_atomic_helper_commit_tail_rpm,
};

static void sunxi_drm_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.normalize_zpos = true;

	/* max_width be decided by the de bufferline */
	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;
	dev->mode_config.funcs = &sunxi_drm_mode_config_funcs;
	dev->mode_config.helper_private = &sunxi_mode_config_helpers;
}

void sunxi_drm_unload(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);
}

static int sunxi_de_pq_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	unsigned long *ubuffer = data;
	u32 gamma_size = 0;
	struct gamma_para *gamma_tmp = NULL;
	u32 *k_lut = NULL;
	u32 *u_lut;
	void *para = NULL;
	int ret = 0, cmd;
	int pq_type = ubuffer[0];
	int disp = ubuffer[1];
	void __user *user_para = (void __user *)ubuffer[2];
	int para_size = ubuffer[3];

	/* note: if new pq_type add, should not modify ubuffer define,
	 *   but rearrange to ubuffer defined before ioctl
	 */
	switch (pq_type) {
	case PQ_SET_REG:
		DRM_ERROR("not support PQ_SET_REG yet\n");
		break;
	case PQ_GET_REG:
		DRM_ERROR("not support PQ_GET_REG yet\n");
		break;
	case PQ_ENABLE:
		//noting to do
		break;
	case PQ_COLOR_MATRIX:
		DRM_ERROR("not support PQ_COLOR_MATRIX yet\n");
		//TODO
		break;
	case PQ_GAMMA:
	case PQ_FCM:
	case PQ_DCI:
	case PQ_DEBAND:
	case PQ_SHARP35X:
	case PQ_SNR:
	case PQ_GTM:
	case PQ_ASU:
		para = (void *)kzalloc(para_size,  GFP_KERNEL);
		if (!para) {
			DRM_ERROR("pq para alloc fail\n");
			ret = -ENOMEM;
			goto OUT;
		}

		if (copy_from_user(para, (void __user *)user_para,
						   para_size)) {
			DRM_ERROR("regs copy from user failed\n");
			ret = -EINVAL;
			goto OUT;
		}
		/* no mater what kinds of pq para, must follows with an int cmd first */
		cmd = (*(int *)para);

		if (pq_type == PQ_GAMMA) {
			gamma_tmp = para;
			gamma_size = gamma_tmp->size;
			u_lut = gamma_tmp->lut;
			k_lut = kzalloc(sizeof(u32) * gamma_size, GFP_KERNEL);
			if (k_lut == NULL) {
				DRM_ERROR("kzalloc struct gamma_lut failed!\n");
				ret = -ENOMEM;
				goto OUT;
			}
			if (cmd != PQ_READ) {
				if (copy_from_user(k_lut, (void __user *)u_lut,
						   sizeof(u32) * gamma_size)) {
					DRM_ERROR("gamma lut copy from user failed\n");
					ret = -EINVAL;
					goto OUT;
				}
			}
			/* set para userspace lut to kernel lut */
			gamma_tmp->lut = k_lut;
		}

		ret = sunxi_drm_crtc_pq_proc(dev, disp, pq_type, para);
		if (ret)
			goto OUT;

		if (cmd == PQ_READ) {
			if (pq_type == PQ_GAMMA) {
				if (copy_to_user((void __user *)u_lut, k_lut,
						  sizeof(u32) * gamma_size)) {
					DRM_ERROR("gamma lut copy to user failed\n");
					ret = -EINVAL;
					goto OUT;
				}
				/* reset para kernel lut to userspace lut */
				gamma_tmp->lut = u_lut;
			}
			if (copy_to_user((void __user *)user_para, para,
							 para_size)) {
				DRM_ERROR("copy to user REG failed\n");
				ret = -EFAULT;
				goto OUT;
			}
		}
		break;
	default:
		DRM_ERROR("pq cmd not found\n");
		ret = -EINVAL;
		break;
	}

OUT:
	if (para)
		kfree(para);
	if (k_lut)
		kfree(k_lut);

	return ret;
}

static const struct drm_ioctl_desc sunxi_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SUNXI_PQ_PROC, sunxi_de_pq_ioctl, 0),
};

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
DEFINE_DRM_GEM_CMA_FOPS(sunxi_drm_driver_fops);
#else
DEFINE_DRM_GEM_DMA_FOPS(sunxi_drm_driver_fops);
#endif
static struct drm_driver sunxi_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &sunxi_drm_driver_fops,
	.ioctls             = sunxi_drm_ioctls,
	.num_ioctls         = ARRAY_SIZE(sunxi_drm_ioctls),
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
#else
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
#endif
};

#define DRV_PTR(drv, cond) (IS_ENABLED(cond) ? &drv : NULL)
static struct platform_driver *sunxi_drm_sub_drivers[] = {
	DRV_PTR(sunxi_tcon_top_platform_driver,		CONFIG_AW_DRM_TCON_TOP),
	DRV_PTR(sunxi_de_platform_driver,		CONFIG_AW_DRM_DE),
	DRV_PTR(sunxi_dsi_combo_phy_platform_driver,	CONFIG_AW_DRM_DSI_COMBOPHY),
	DRV_PTR(sunxi_dsi_platform_driver,		CONFIG_AW_DRM_DSI),
	DRV_PTR(sunxi_lvds_platform_driver,		CONFIG_AW_DRM_LVDS),
	DRV_PTR(sunxi_rgb_platform_driver,		CONFIG_AW_DRM_RGB),
	DRV_PTR(sunxi_hdmi_platform_driver,		CONFIG_AW_DRM_HDMI_TX),
	DRV_PTR(sunxi_drm_edp_platform_driver,		CONFIG_AW_DRM_EDP),
	DRV_PTR(sunxi_tcon_platform_driver,		CONFIG_AW_DRM_TCON),
	/* TODO add tv*/
};

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static struct component_match *sunxi_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	DRM_INFO("%s start\n", __FUNCTION__);
	for (i = 0; i < ARRAY_SIZE(sunxi_drm_sub_drivers); ++i) {
		struct platform_driver *drv = sunxi_drm_sub_drivers[i];
		struct device *p = NULL, *d;

		if (!drv)
			continue;

		while ((d = platform_find_device_by_driver(p, &drv->driver))) {
			put_device(p);
			component_match_add(dev, &match, compare_dev, d);
			p = d;
		}
		put_device(p);
	}
	DRM_INFO("%s finish\n", __FUNCTION__);

	return match ?: ERR_PTR(-ENODEV);
}

static int sunxi_drm_property_create(struct sunxi_drm_private *private)
{
	struct drm_device *dev = &private->base;
	struct drm_property *prop;
	int ret;
	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
			"FRONTEND_DATA", 0);
	if (!prop)
		return -ENOMEM;
	private->prop_frontend_data = prop;

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB,
			"BACKEND_DATA", 0);
	if (!prop)
		return -ENOMEM;
	private->prop_backend_data = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"EOTF", 0, 20);
	if (!prop)
		return -ENOMEM;
	private->prop_eotf = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"COLOR_SPACE", 0, 20);
	if (!prop)
		return -ENOMEM;
	private->prop_color_space = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"COLOR_FORMAT", 0, 20);
	if (!prop)
		return -ENOMEM;
	private->prop_color_format = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"COLOR_DEPTH", 0, 20);
	if (!prop)
		return -ENOMEM;
	private->prop_color_depth = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"COLOR_RANGE", 0, 20);
	if (!prop)
		return -ENOMEM;
	private->prop_color_range = prop;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	ret = drm_mode_create_tv_properties(dev, 0, NULL);
#else
	ret = drm_mode_create_tv_properties_legacy(dev, 0, NULL);
#endif
	if (ret)
		return ret;
	return sunxi_drm_plane_property_create(private);
}

struct print_info {
	char *buf;
	int write_size;
	int max_size;
};

static void __drm_printfn_buf(struct drm_printer *p, struct va_format *vaf)
{
	struct print_info *info = p->arg;
	int tmp;
	if (!IS_ERR_OR_NULL(p->arg)) {
		tmp = snprintf(info->buf + info->write_size,
					    info->max_size - info->write_size, "%pV", vaf);
		if (tmp <= info->max_size - info->write_size)
			info->write_size += tmp;
		else
			info->write_size = info->max_size;
	}
}

static void sunxi_drm_atomic_plane_print_state(struct drm_printer *p,
		const struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;

	drm_printf(p, "plane[%u]: %s %sable\n", plane->base.id, plane->name, state->fb ? "en" : "dis");
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
	drm_printf(p, "\trotation=%x\n", state->rotation);
	drm_printf(p, "\tnormalized-zpos=%x\n", state->normalized_zpos);
	sunxi_plane_print_state(p, state, false);
}

static ssize_t sunxi_crtc_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct drm_mode_config *config = &drm_dev->mode_config;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	ssize_t n = 0;
	bool take_locks = true;
	struct print_info info = {
		.buf = buf,
		.write_size = 0,
		.max_size = PAGE_SIZE,
	};
	struct drm_printer p = {
		.printfn = __drm_printfn_buf,
		.arg = &info,
	};

	list_for_each_entry(plane, &config->plane_list, head) {
		if (take_locks)
			drm_modeset_lock(&plane->mutex, NULL);
		sunxi_drm_atomic_plane_print_state(&p, plane->state);
		if (take_locks)
			drm_modeset_unlock(&plane->mutex);
	}

	list_for_each_entry(crtc, &config->crtc_list, head) {
		drm_printf(&p, "crtc[%u]: %s\n", crtc->base.id, crtc->name);
		if (take_locks)
			drm_modeset_lock(&crtc->mutex, NULL);
		if (crtc->funcs->atomic_print_state)
			crtc->funcs->atomic_print_state(&p, crtc->state);
		if (take_locks)
			drm_modeset_unlock(&crtc->mutex);
	}

	n = strlen(buf);
	return n;
}

static DEVICE_ATTR(status, 0444,
	sunxi_crtc_status_show, NULL);

static struct attribute *_sunxi_crtc_attrs[] = {
	&dev_attr_status.attr,
	NULL
};

static struct attribute_group sunxi_crtc_group = {
	.name = "crtc",
	.attrs = _sunxi_crtc_attrs,
};

static int __maybe_unused commit_init_connecting(struct drm_device *drm)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *new_conn_state;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct sunxi_init_connecting *c;
	struct display_channel_state channel;
	struct display_channel_state *c_ref = &channel;
	struct display_channel_state *c_commit = NULL;
	int ret;

	memset(c_ref, 0, sizeof(*c_ref));
	sunxi_fbdev_init(drm, &channel);
	drm_modeset_acquire_init(&ctx, 0);
	state = drm_atomic_state_alloc(drm);
	if (!state) {
		ret = -ENOMEM;
		goto out_ctx;
	}

	state->acquire_ctx = &ctx;
retry:
	list_for_each_entry(c, &pri->priv->connecting_head, list) {
		crtc = c->crtc;
		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			goto out_state;
		}

		ret = drm_atomic_set_mode_for_crtc(crtc_state, c->mode);
		if (ret != 0) {
			ret = PTR_ERR(new_conn_state);
			goto out_state;
		}

		crtc_state->active = true;
		new_conn_state = drm_atomic_get_connector_state(state, c->connector);
		if (IS_ERR(new_conn_state)) {
			ret = PTR_ERR(new_conn_state);
			goto out_state;
		}

		ret = drm_atomic_set_crtc_for_connector(new_conn_state, crtc);
		if (ret) {
			DRM_ERROR("connector set crtc fail\n");
			goto out_state;
		}
		if (c_ref->base.fb && crtc == c_ref->base.crtc) {
			plane_state = drm_atomic_get_plane_state(state, c_ref->base.plane);
			c_commit = to_display_channel_state(plane_state);
			if (IS_ERR(plane_state)) {
				ret = PTR_ERR(plane_state);
				goto out_state;
			}
			//do not use memcpy, ref_cnt
			plane_state->crtc_x = c_ref->base.crtc_x;
			plane_state->crtc_y = c_ref->base.crtc_y;
			plane_state->crtc_w = c_ref->base.crtc_w;
			plane_state->crtc_h = c_ref->base.crtc_h;
			plane_state->src_x = c_ref->base.src_x;
			plane_state->src_y = c_ref->base.src_y;
			plane_state->src_w = c_ref->base.src_w;
			plane_state->src_h = c_ref->base.src_h;
			plane_state->alpha = c_ref->base.alpha;
			plane_state->pixel_blend_mode = c_ref->base.pixel_blend_mode;
			plane_state->rotation = c_ref->base.rotation;
			c_commit->eotf = c_ref->eotf;
			c_commit->color_space = c_ref->color_space;
			c_commit->color_range = c_ref->color_range;
			drm_atomic_set_fb_for_plane(plane_state, c_ref->base.fb);
			ret = drm_atomic_set_crtc_for_plane(plane_state, c_ref->base.crtc);
			if (ret)
				goto out_state;
			c_ref->base.fb = NULL;
		}
	}
	ret = drm_atomic_commit(state);

out_state:
	if (ret == -EDEADLK)
		goto backoff;

	drm_atomic_state_put(state);
out_ctx:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

backoff:
	drm_atomic_state_clear(state);
	drm_modeset_backoff(&ctx);

	goto retry;
}

static int get_boot_display_info(struct drm_device *drm)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
	struct display_boot_info *info;
	struct drm_display_mode *mode;
	struct device_node *routing;
	struct device_node *mode_node;
	char name[16];
	int i, ret;
	u32 read_val = 0;

	INIT_LIST_HEAD(&pri->priv->boot_info_head);
	for (i = 0; ; i++) {
		snprintf(name, 16, "booting-%d", i);

		routing = of_get_child_by_name(drm->dev->of_node, name);
		if (!routing)
			return 0;

		info = kmalloc(sizeof(*info), GFP_KERNEL | __GFP_ZERO);
		ret = of_property_read_u32_array(routing, "logo", &info->logo.phy_addr, 4);
		if (ret) {
			of_node_put(routing);
			kfree(info);
			return 0;
		}

		ret = of_property_read_u32_array(routing, "route", &info->de_id, 4);
		if (ret) {
			of_node_put(routing);
			kfree(info);
			return 0;
		}

		/* parse add mode info */
		mode = &info->mode;
		mode_node = of_get_child_by_name(routing, "mode");
		if (!mode_node) {
			DRM_ERROR("find mode node failed\n");
			return 0;
		}

#define of_mode_read(name, value)                              \
		do {                                                   \
			of_property_read_u32(mode_node, name, &read_val);  \
			value = read_val;                                  \
		} while (0)

		of_mode_read("clock", mode->clock);
		of_mode_read("hdisplay", mode->hdisplay);
		of_mode_read("vdisplay", mode->vdisplay);
		of_mode_read("hsync_start", mode->hsync_start);
		of_mode_read("vsync_start", mode->vsync_start);
		of_mode_read("hsync_end", mode->hsync_end);
		of_mode_read("vsync_end", mode->vsync_end);
		of_mode_read("htotal", mode->htotal);
		of_mode_read("vtotal", mode->vtotal);
		of_mode_read("vscan", mode->vscan);
		of_mode_read("flags", mode->flags);
		of_mode_read("hskew", mode->hskew);
		of_mode_read("type",  mode->type);
#undef of_mode_read

		drm_mode_set_name(mode);

		DRM_DEBUG_DRIVER("boot mode: " DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));

		list_add_tail(&info->list, &pri->priv->boot_info_head);

		of_node_put(mode_node);
		of_node_put(routing);
	}
	return 0;
}

static int init_connecting(struct drm_device *drm, struct drm_crtc **crtcs, unsigned int crtc_cnt,
				struct drm_connector **connectors, unsigned int connector_cnt)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
	struct sunxi_init_connecting *c;
	struct sunxi_drm_device *sdrm;
	struct drm_crtc *crtc_ = NULL;
	struct drm_connector *connector_ = NULL;
	struct display_boot_info *info;
	struct drm_display_mode *mode = NULL;
	int i, id, modes_count, init_cnt = 0, mode_found = 0;

	if (!crtc_cnt || !connector_cnt) {
		DRM_ERROR("connector or crtc null\n");
		return -ENODEV;
	}

	INIT_LIST_HEAD(&pri->priv->connecting_head);
	list_for_each_entry(info, &pri->priv->boot_info_head, list) {
		connector_ = NULL;
		crtc_ = NULL;
		mode = NULL;
		for (i = 0; i < crtc_cnt; i++) {
			id = sunxi_drm_crtc_get_hw_id(crtcs[i]);
			if (id == info->de_id) {
				crtc_ = crtcs[i];
				break;
			}
		}
		if (!crtc_)
			return -ENODEV;

		DRM_DEBUG_DRIVER("%s %d connector cnt %d want hw_id %d type %d\n",
				 __func__, __LINE__, connector_cnt, info->hw_id, info->connector_type);
		for (i = 0; i < connector_cnt; i++) {
			sdrm = container_of(connectors[i], struct sunxi_drm_device, connector);
			DRM_DEBUG_DRIVER("%s %d name:%s type:%d hw_id:%d\n",
					 __func__, __LINE__, connectors[i]->name,
					 connectors[i]->connector_type, sdrm->hw_id);
			if (info->connector_type == connectors[i]->connector_type &&
				  info->hw_id == sdrm->hw_id) {
				mutex_lock(&drm->mode_config.mutex);
				modes_count = connectors[i]->funcs->fill_modes(connectors[i], 8192, 8192);
				if (!modes_count)
					break;

				list_for_each_entry(mode, &connectors[i]->modes, head) {
					if (mode && drm_mode_equal(&info->mode, mode)) {
						mode_found = 1;
						break;
					}
				};
				if (mode_found)
					mode = &info->mode;
				else
					mode = list_first_entry_or_null(&connectors[i]->modes, struct drm_display_mode, head);
				DRM_DEBUG_DRIVER("%s use mode %dx%d\n", __FUNCTION__, mode->hdisplay, mode->vdisplay);
				mutex_unlock(&drm->mode_config.mutex);

				if (mode) {
					drm_connector_get(connectors[i]);
					connector_ = connectors[i];
					break;
				}
			}
		}
		if (!connector_)
			return -ENODEV;

		c = kmalloc(sizeof(*c), GFP_KERNEL | __GFP_ZERO);
		c->crtc = crtc_;
		c->connector = connector_;
		c->mode = mode;
		list_add_tail(&c->list, &pri->priv->connecting_head);
		init_cnt++;
	}

	/* no bootloader connecting info, use the first one we found */
	if (init_cnt == 0) {
		drm_connector_get(connectors[0]);
		mutex_lock(&drm->mode_config.mutex);
		modes_count = connectors[0]->funcs->fill_modes(connectors[0], 8192, 8192);
		//DRM_INFO("%s found mode %d\n", __FUNCTION__, modes_count);
		mode = list_first_entry_or_null(&connectors[0]->modes, struct drm_display_mode, head);
		if (mode) {
			drm_connector_get(connectors[0]);
			c = kmalloc(sizeof(*c), GFP_KERNEL | __GFP_ZERO);
			c->crtc = crtcs[0];
			c->connector = connectors[0];
			c->mode = mode;
			list_add_tail(&c->list, &pri->priv->connecting_head);
		} else
			DRM_ERROR("none mode found %s\n", __func__);
		mutex_unlock(&drm->mode_config.mutex);
	}
	return 0;
}

int sunxi_drm_get_logo_info(struct drm_device *dev, struct sunxi_logo_info *logo,
				unsigned int *scn_w, unsigned int *scn_h)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(dev);
	struct display_boot_info *info;
	struct sunxi_init_connecting *c;

	c = list_first_entry_or_null(&pri->priv->connecting_head, struct sunxi_init_connecting, list);
	if (!c) {
		DRM_ERROR("init connecting not found %s\n", __func__);
		return -1;
	}
	*scn_w = c->mode->hdisplay;
	*scn_h = c->mode->vdisplay;

	info = list_first_entry_or_null(&pri->priv->boot_info_head, struct display_boot_info, list);
	if (!info) {
		logo->phy_addr = 0;
		logo->width = c->mode->hdisplay;
		logo->height = c->mode->vdisplay;
	} else {
		memcpy(logo, &info->logo, sizeof(*logo));
	}

	return 0;
}

bool sunxi_drm_check_if_need_sw_enable(struct drm_connector *connector)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(connector->dev);
	struct sunxi_init_connecting *c;
	struct display_boot_info *info;

	info = list_first_entry_or_null(&pri->priv->boot_info_head, struct display_boot_info, list);
	if (!info)
		return false;

	list_for_each_entry(c, &pri->priv->connecting_head, list) {
		if (!c->done && connector == c->connector)
			return true;
	}
	return false;
}

bool sunxi_drm_check_device_boot_enabled(struct drm_device *drm,
			unsigned int connector_type, unsigned int hw_id)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
	struct display_boot_info *info;

	list_for_each_entry(info, &pri->priv->boot_info_head, list) {
		if ((info->connector_type == connector_type) &&
			 (info->hw_id == hw_id))
			return true;
	}

	return false;
}

void sunxi_drm_signal_sw_enable_done(struct drm_crtc *crtc)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(crtc->dev);
	struct sunxi_init_connecting *c;
	list_for_each_entry(c, &pri->priv->connecting_head, list) {
		if (crtc == c->crtc) {
			WARN_ON(c->done);
			c->done = true;
		}
	}
}

static int __maybe_unused setup_bootloader_connecting_state(struct drm_device *drm)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector, **connectors = NULL;
	unsigned int connector_count = 0;
	unsigned int num_crtc = drm->mode_config.num_crtc;
	struct drm_crtc *crtc, **crtcs;
	int i = 0, ret;

//	count = get_boot_display_info(drm);

//get all connector
	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_client_for_each_connector_iter(connector, &conn_iter) {
		struct drm_connector **tmp;

		tmp = krealloc(connectors, (connector_count + 1) * sizeof(*connectors), GFP_KERNEL);
		if (!tmp) {
			ret = -ENOMEM;
			goto free_connectors;
		}

		connectors = tmp;
		drm_connector_get(connector);
		connectors[connector_count++] = connector;
	}
	drm_connector_list_iter_end(&conn_iter);
	if (!connector_count) {
		DRM_ERROR("connector conut zero\n");
		return -1;
	}

//get all crtc
	crtcs = kcalloc(num_crtc, sizeof(*crtcs), GFP_KERNEL);
	drm_for_each_crtc(crtc, drm)
		crtcs[i++] = crtc;

//setup connecting
	ret = init_connecting(drm, crtcs, num_crtc, connectors, connector_count);
	if (ret)
		goto free_connectors;

free_connectors:
	for (i = 0; i < connector_count; i++)
		drm_connector_put(connectors[i]);
	kfree(connectors);
	return ret;
}

static int sunxi_drm_bind(struct device *dev)
{
	int ret;
	struct drm_device *drm;
	struct sunxi_drm_private *private;

	DRM_INFO("%s start\n", __FUNCTION__);
//	private = devm_drm_dev_alloc(dev, &sunxi_drm_driver,
//	    struct sunxi_drm_private, base);
	private = __devm_drm_dev_alloc(dev, &sunxi_drm_driver,
		  sizeof(*private) + sizeof(struct sunxi_drm_pri),
		    offsetof(struct sunxi_drm_private, base));
	drm = &private->base;
	if (IS_ERR(private))
		return PTR_ERR(private);

	private->priv = ((void *)private) + sizeof(*private);
	ret = drmm_mode_config_init(drm);
	if (ret) {
		DRM_ERROR("drmm_mode_config_init fail %d\n", ret);
		return ret;
	}

	sunxi_drm_mode_config_init(drm);
	sunxi_drm_property_create(private);

	get_boot_display_info(drm);

	ret = component_bind_all(dev, drm);
	if (ret)
		goto mode_config_clean;

	dev_set_drvdata(dev, drm);
	drm_mode_config_reset(drm);
	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret) {
		DRM_ERROR("failed to init vblank.\n");
		return ret;
	}

	/* Enable connectors polling * */
	drm_kms_helper_poll_init(drm);
	setup_bootloader_connecting_state(drm);
#if IS_ENABLED (CONFIG_DRM_FBDEV_EMULATION)
	commit_init_connecting(drm);
#endif
	ret = drm_dev_register(drm, 0);
	ret = sysfs_create_group(&dev->kobj, &sunxi_crtc_group);

	DRM_INFO("%s ok\n", __FUNCTION__);
	return 0;

mode_config_clean:
	drm_mode_config_cleanup(drm);
	DRM_INFO("%s fail ret = %d\n", __FUNCTION__, ret);
	return ret;
}

static void sunxi_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);
	drm_dev_unregister(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);
	drm_atomic_helper_shutdown(drm_dev);
	component_unbind_all(dev, drm_dev);
}

static const struct component_master_ops sunxi_drm_ops = {
	.bind = sunxi_drm_bind,
	.unbind = sunxi_drm_unbind,
};

static int sunxi_drm_platform_probe(struct platform_device *pdev)
{
	struct component_match *match;

	DRM_INFO("%s start\n", __FUNCTION__);
	match = sunxi_drm_match_add(&pdev->dev);
	if (IS_ERR(match)) {
		DRM_INFO("sunxi_drm_match_add fail\n");
		return PTR_ERR(match);
	}

	return component_master_add_with_match(&pdev->dev, &sunxi_drm_ops,
					       match);
}

static int sunxi_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &sunxi_drm_ops);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sunxi_drm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int sunxi_drm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops sunxi_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sunxi_drm_suspend,
				sunxi_drm_resume)
};

static const struct of_device_id sunxi_of_match[] = {
	{
		.compatible = "allwinner,sunxi-drm",
	},
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_of_match);

static struct platform_driver sunxi_drm_platform_driver = {
	.probe = sunxi_drm_platform_probe,
	.remove = sunxi_drm_platform_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "sunxi-drm",
		.of_match_table = sunxi_of_match,
		.pm = &sunxi_drm_pm_ops,
	},
};

static void sunxi_drm_unregister_drivers(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(sunxi_drm_sub_drivers); ++i) {
		struct platform_driver *drv = sunxi_drm_sub_drivers[i];
		if (!drv)
			continue;

		platform_driver_unregister(drv);
	}
}

static int sunxi_drm_register_drivers(void)
{
	int i, ret;
	for (i = 0; i < ARRAY_SIZE(sunxi_drm_sub_drivers); ++i) {
		struct platform_driver *drv = sunxi_drm_sub_drivers[i];
		if (!drv)
			continue;

		ret = platform_driver_register(drv);
		if (ret) {
			DRM_ERROR("driver register fail %d %ld\n", i,
				  ARRAY_SIZE(sunxi_drm_sub_drivers));
			break;
		}
	}

	/* add drm master device */
	ret = platform_driver_register(&sunxi_drm_platform_driver);

	return ret;
}

static int __init sunxi_drm_drv_init(void)
{
	int ret;
	ret = sunxi_drm_register_drivers();
	if (ret)
		sunxi_drm_unregister_drivers();
	return ret;
}

static void __exit sunxi_drm_drv_exit(void)
{
	sunxi_drm_unregister_drivers();
}

module_init(sunxi_drm_drv_init);
module_exit(sunxi_drm_drv_exit);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_DESCRIPTION("Allwinnertech SoC DRM Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("V1.0.0");
MODULE_AUTHOR("chenqingjia <chenqingjia@allwinnertech.com>");
