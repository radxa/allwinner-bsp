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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
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

extern struct platform_driver sunxi_de_platform_driver;
extern struct platform_driver sunxi_hdmi_platform_driver;
extern struct platform_driver sunxi_drm_edp_platform_driver;
extern struct platform_driver sunxi_tcon_platform_driver;
extern struct platform_driver sunxi_tcon_top_platform_driver;
extern struct platform_driver sunxi_dsi_platform_driver;
extern struct platform_driver sunxi_dsi_combo_phy_platform_driver;

int sunxi_drm_fbdev_init(struct drm_device *dev);
void sunxi_drm_fbdev_fini(struct drm_device *dev);

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

	/* max_width be decided by the de bufferline */
	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;
	dev->mode_config.funcs = &sunxi_drm_mode_config_funcs;
	dev->mode_config.helper_private = &sunxi_mode_config_helpers;
}

static int get_boot_display_info(struct drm_device *drm)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
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
	     info->de_id, info->device_type, info->mode, info->format, info->bits, info->colorspace, info->eotf);
	return 0;
}

static int get_boot_fb_info(struct drm_device *drm)
{
	struct sunxi_drm_private *pri = to_sunxi_drm_private(drm);
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

	pri->sw_enable = true;
	return 0;
}

void sunxi_drm_unload(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);
}

DEFINE_DRM_GEM_CMA_FOPS(sunxi_drm_driver_fops);

static struct drm_driver sunxi_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &sunxi_drm_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
};

#define DRV_PTR(drv, cond) (IS_ENABLED(cond) ? &drv : NULL)
static struct platform_driver *sunxi_drm_sub_drivers[] = {
	DRV_PTR(sunxi_de_platform_driver,		CONFIG_AW_DRM_DE),
	DRV_PTR(sunxi_tcon_top_platform_driver,		CONFIG_AW_DRM_TCON_TOP),
	DRV_PTR(sunxi_dsi_combo_phy_platform_driver,	CONFIG_AW_DRM_LCD_DSI_COMBO_PHY),
	DRV_PTR(sunxi_dsi_platform_driver,		CONFIG_AW_DRM_LCD_DSI),
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

	return match ?: ERR_PTR(-ENODEV);
}

static int sunxi_drm_property_create(struct sunxi_drm_private *private)
{
	struct drm_device *dev = &private->base;
	struct drm_property *prop;
	char name[32];
	int i;

	for (i = 0; i < OVL_REMAIN; i++) {
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
	ret = drm_dev_register(drm, 0);
	sunxi_drm_fbdev_init(drm);

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

	match = sunxi_drm_match_add(&pdev->dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

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

MODULE_DESCRIPTION("Allwinnertech SoC DRM Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("V1.0.0");
MODULE_AUTHOR("chenqingjia <chenqingjia@allwinnertech.com>");
