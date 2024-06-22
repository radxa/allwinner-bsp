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

#define DRIVER_NAME "sunxi-drm"
#define DRIVER_DESC "allwinnertech SoC DRM"
#define DRIVER_DATE "20230901"
#define DRIVER_MAJOR 3
#define DRIVER_MINOR 0

struct sunxi_init_connecting {
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	//TODO add eotf colorspace etc
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

int sunxi_fbdev_init(struct drm_device *drm);
int sunxi_drm_plane_property_create(struct sunxi_drm_private *private);

static const struct drm_mode_config_funcs sunxi_drm_mode_config_funcs = {
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.fb_create = drm_gem_fb_create,
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

static int get_boot_display_info(struct drm_device *drm)
{
/*	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
	struct display_boot_info *info = &pri->boot;
	const char *str = NULL;
	char name[16] = {0};
	const int output_cnt = 2;
	int len, i, ret = -1;
	char *tmp, *p, *boot_str;
	unsigned int *buf[] = {
		&info->device_type,
		&info->mode,
		&info->format,
		&info->bits,
		&info->colorspace,
		&info->eotf,
	};

	for (i = 0; i < output_cnt; i++) {
		sprintf(name, "boot_disp%d", i);
		ret = of_property_read_string(drm->dev->of_node, name, &str);
		if (!ret)
			break;
	}

	if (ret) {
		DRM_INFO("no boot disp\n");
		return 0;
	}

	info->de_id = i;
	len = strlen(str);
	tmp = kzalloc(len + 1, GFP_KERNEL);
	if (!tmp) {
		DRM_INFO("kzalloc failed\n");
		return -ENOMEM;
	}

	memcpy((void *)tmp, (void *)str, len);
	boot_str = tmp;

	i = 0;
	do {
		p = strstr(boot_str, ",");
		if (p != NULL)
			*p = '\0';
		ret = kstrtouint(boot_str, 16, buf[i]);
		if (ret) {
			DRM_ERROR("parse %s fail! index = %d\n", name, i);
			break;
		}
		i++;
		boot_str = p + 1;
	} while (i < sizeof(buf) / sizeof(buf[0]) && p != NULL);

	kfree(tmp);
	DRM_INFO("boot_disp %u type %u mode %u format %u bits %u colorspace %u eotf %u\n",
	     info->de_id, info->device_type, info->mode, info->format, info->bits, info->colorspace, info->eotf);*/
	return 0;
}

static int get_boot_fb_info(struct drm_device *drm)
{
/*	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
	struct sunxi_logo_info *logo = &pri->logo;
	char *boot_fb_str = NULL;
	int ret, len, i = 0;
	char *tmp, *p;
	const char *str = NULL;
	unsigned int *buf[] = {
		&logo->phy_addr,
		&logo->width,
		&logo->height,
		&logo->bpp,
		&logo->stride,
		&logo->crop_l,
		&logo->crop_t,
		&logo->crop_r,
		&logo->crop_b,
	};

	ret = of_property_read_string(drm->dev->of_node, "boot_fb0", &str);
	if (ret < 0) {
		DRM_ERROR("boot_fb0 read err\n");
		return -1;
	}

	len = strlen(str);
	tmp = kzalloc(len + 1, GFP_KERNEL);
	if (!tmp) {
		DRM_INFO("malloc memory fail\n");
		return -1;
	}
	memcpy((void *)tmp, (void *)str, len);
	boot_fb_str = tmp;

	do {
		p = strstr(boot_fb_str, ",");
		if (p != NULL)
			*p = '\0';
		ret = kstrtouint(boot_fb_str, 16, buf[i]);
		if (ret)
			DRM_ERROR("parse boot_fb fail! index = %d\n", i);
		i++;
		boot_fb_str = p + 1;
	} while (i < sizeof(buf) / sizeof(buf[0]) && p != NULL);

	kfree(tmp);
	DRM_INFO("boot_fb para: src[phy_addr=0x%x,w=%u,h=%u,bpp=%u,stride=%u,ltrb=%u %u %u %u]\n",
	     logo->phy_addr, logo->width, logo->height, logo->bpp, logo->stride,
	     logo->crop_l, logo->crop_t, logo->crop_r, logo->crop_b);

	pri->sw_enable = true;*/
	return 0;
}

void sunxi_drm_unload(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
DEFINE_DRM_GEM_CMA_FOPS(sunxi_drm_driver_fops);
#else
DEFINE_DRM_GEM_DMA_FOPS(sunxi_drm_driver_fops);
#endif
static struct drm_driver sunxi_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &sunxi_drm_driver_fops,
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

static struct drm_connector *pick_connector(struct drm_device *drm, struct drm_connector **connectors, unsigned int connector_count)
{
	return connectors[0];
}

static struct drm_crtc *pick_crtc(struct drm_device *drm, struct drm_crtc **crtcs, unsigned int crtc_count)
{
	return crtcs[0];
}

static struct drm_display_mode *pick_mode(struct drm_connector *connector)
{
	unsigned int modes_count = 0;
	struct drm_display_mode *mode;

	modes_count = connector->funcs->fill_modes(connector, 8192, 8192);
	list_for_each_entry(mode, &connector->modes, head) {
		return mode;
	}
	return NULL;
}

static int __maybe_unused commit(struct drm_device *drm, struct sunxi_init_connecting *connecting)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc *crtc = connecting->crtc;
	int ret;


	drm_modeset_acquire_init(&ctx, 0);
	state = drm_atomic_state_alloc(drm);
	if (!state) {
		ret = -ENOMEM;
		goto out_ctx;
	}

	state->acquire_ctx = &ctx;

retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out_state;
	}


	ret = drm_atomic_set_mode_for_crtc(crtc_state, connecting->mode);
	if (ret != 0) {
		ret = PTR_ERR(new_conn_state);
		goto out_state;
	}

	crtc_state->active = true;


	new_conn_state = drm_atomic_get_connector_state(state, connecting->connector);
	if (IS_ERR(new_conn_state)) {
		ret = PTR_ERR(new_conn_state);
		goto out_state;
	}

	ret = drm_atomic_set_crtc_for_connector(new_conn_state, crtc);
	if (ret) {
		DRM_ERROR("connector set crtc fail\n");
		goto out_state;
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

static int __maybe_unused setup_bootloader_connecting_state(struct drm_device *drm)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector, **connectors = NULL;
	unsigned int connector_count = 0;
	unsigned int num_crtc = drm->mode_config.num_crtc;
	struct drm_crtc *crtc, **crtcs;
	struct drm_display_mode *mode;
	int i = 0, ret;
	struct sunxi_init_connecting connecting;

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

	connector = pick_connector(drm, connectors, connector_count);
	drm_connector_get(connector);

	mutex_lock(&drm->mode_config.mutex);
	mode = pick_mode(connector);
	mutex_unlock(&drm->mode_config.mutex);

//get all crtc
	crtcs = kcalloc(num_crtc + 1, sizeof(*crtcs), GFP_KERNEL);
	drm_for_each_crtc(crtc, drm)
		crtcs[i++] = crtc;
	crtc = pick_crtc(drm, crtcs, num_crtc);

//setup
	connecting.crtc = crtc;
	connecting.connector = connector;
	connecting.mode = mode;
	commit(drm, &connecting);

free_connectors:
	for (i = 0; i < connector_count; i++)
		drm_connector_put(connectors[i]);
	kfree(connectors);
	return 0;
}

static int sunxi_drm_bind(struct device *dev)
{
	int ret;
	struct drm_device *drm;
	struct sunxi_drm_private *private;

	DRM_INFO("%s start\n", __FUNCTION__);
	private = devm_drm_dev_alloc(dev, &sunxi_drm_driver,
	    struct sunxi_drm_private, base);
	drm = &private->base;
	if (IS_ERR(private))
		return PTR_ERR(private);

	get_boot_display_info(drm);
	get_boot_fb_info(drm);
	ret = drmm_mode_config_init(drm);
	if (ret) {
		DRM_ERROR("drmm_mode_config_init fail %d\n", ret);
		return ret;
	}

	sunxi_drm_mode_config_init(drm);
	sunxi_drm_property_create(private);

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
#if IS_ENABLED (CONFIG_DRM_FBDEV_EMULATION)
	setup_bootloader_connecting_state(drm);
	sunxi_fbdev_init(drm);
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
