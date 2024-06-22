/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2023 Allwinnertech Co.Ltd
 *
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/of_device.h>
#include <linux/component.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <drm/drm_panel.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_property.h>
#include "sunxi_drm_drv.h"
#include "sunxi_device/sunxi_tcon.h"
#include "sunxi_drm_intf.h"
#include "sunxi_drm_crtc.h"
#if IS_ENABLED(CONFIG_ARCH_SUN55IW6)
#define RGB_DISPLL_CLK
#endif
struct rgb_data {
	int id;
};
struct sunxi_drm_rgb {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct device *tcon_dev;
	struct drm_device *drm_dev;
	struct drm_display_mode mode;
	struct disp_rgb_para rgb_para;
	unsigned int tcon_id;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	bool bound;
	bool allow_sw_enable;
	bool sw_enable;
	struct device *dev;

	struct phy *phy;
	union phy_configure_opts phy_opts;
	const struct rgb_data *rgb_data;
	u32 rgb_id;

	struct clk *pclk;
	unsigned long mode_flags;

};
static const struct rgb_data rgb0_data = {
	.id = 0,
};

static const struct rgb_data rgb1_data = {
	.id = 1,
};

static const struct of_device_id sunxi_drm_rgb_match[] = {
	{ .compatible = "allwinner,rgb0", .data = &rgb0_data },
	{ .compatible = "allwinner,rgb1", .data = &rgb1_data },
	{},
};
static inline struct sunxi_drm_rgb *
	drm_encoder_to_sunxi_drm_rgb(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sunxi_drm_rgb, encoder);
}

static struct device *drm_rgb_of_get_tcon(struct device *rgb_dev)
{
	struct device_node *node = rgb_dev->of_node;
	struct device_node *tcon_lcd_node;
	struct device_node *rgb_in_tcon;
	struct platform_device *pdev = NULL;
	struct device *tcon_lcd_dev = NULL;;

	rgb_in_tcon = of_graph_get_endpoint_by_regs(node, 0, 0);
	if (!rgb_in_tcon) {
		DRM_ERROR("endpoint rgb_in_tcon not fount\n");
		return NULL;
	}

	tcon_lcd_node = of_graph_get_remote_port_parent(rgb_in_tcon);
	if (!tcon_lcd_node) {
		DRM_ERROR("node tcon_lcd not fount\n");
		tcon_lcd_dev = NULL;
		goto RGB_PUT;
	}

	pdev = of_find_device_by_node(tcon_lcd_node);
	if (!pdev) {
		DRM_ERROR("tcon_lcd platform device not fount\n");
		tcon_lcd_dev = NULL;
		goto TCON_RGB_PUT;
	}

	tcon_lcd_dev = &pdev->dev;
	platform_device_put(pdev);

TCON_RGB_PUT:
	of_node_put(tcon_lcd_node);
RGB_PUT:
	of_node_put(rgb_in_tcon);

	return tcon_lcd_dev;
}

static inline struct sunxi_drm_rgb *encoder_to_sunxi_drm_rgb(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sunxi_drm_rgb, encoder);
}

static inline struct sunxi_drm_rgb *connector_to_sunxi_drm_rgb(struct drm_connector *connector)
{
	return container_of(connector, struct sunxi_drm_rgb, connector);
}

static int sunxi_lcd_pin_set_state(struct device *dev, char *name)
{
	int ret;
	struct pinctrl *pctl;
	struct pinctrl_state *state;

	DRM_INFO("[RGB] %s start\n", __FUNCTION__);
	pctl = pinctrl_get(dev);
	if (IS_ERR(pctl)) {
		DRM_INFO("[WARN]can NOT get pinctrl for %lx \n",
			(unsigned long)dev);
		ret = 0;
		goto exit;
	}

	state = pinctrl_lookup_state(pctl, name);
	if (IS_ERR(state)) {
		DRM_ERROR("pinctrl_lookup_state for %lx fail\n",
			(unsigned long)dev);
		ret = PTR_ERR(state);
		goto exit;
	}

	ret = pinctrl_select_state(pctl, state);
	if (ret < 0) {
		DRM_ERROR("pinctrl_select_state(%s) for %lx fail\n", name,
			(unsigned long)dev);
		goto exit;
	}

exit:
	return ret;
}

void sunxi_drm_rgb_encoder_atomic_enable(struct drm_encoder *encoder,
					struct drm_atomic_state *state)
{
	int ret;
	struct drm_crtc *crtc = encoder->crtc;
	int de_hw_id = sunxi_drm_crtc_get_hw_id(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct sunxi_drm_rgb *rgb = encoder_to_sunxi_drm_rgb(encoder);
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct disp_output_config disp_cfg;
	unsigned long pclk_clk_rate;

	DRM_INFO("[RGB] %s start\n", __FUNCTION__);
	drm_mode_to_sunxi_video_timings(&rgb->mode, &rgb->rgb_para.timings);

	memset(&disp_cfg, 0, sizeof(struct disp_output_config));
	memcpy(&disp_cfg.rgb_para, &rgb->rgb_para,
		sizeof(rgb->rgb_para));
	disp_cfg.type = INTERFACE_RGB;
	disp_cfg.de_id = de_hw_id;
	disp_cfg.irq_handler = sunxi_crtc_event_proc;
	disp_cfg.irq_data = scrtc_state->base.crtc;
	disp_cfg.sw_enable = rgb->sw_enable;
	disp_cfg.tcon_lcd_div = 7;
#ifdef RGB_DISPLL_CLK
	disp_cfg.displl_clk = true;
#else
	disp_cfg.displl_clk = false;
#endif
	sunxi_tcon_mode_init(rgb->tcon_dev, &disp_cfg);

	pclk_clk_rate = rgb->rgb_para.timings.pixel_clk * disp_cfg.tcon_lcd_div;

	sunxi_lcd_pin_set_state(rgb->dev, "active");

	if (rgb->sw_enable) {
		if (rgb->phy)
			phy_power_on(rgb->phy);
		drm_panel_prepare(rgb->panel);
	} else {
		if (rgb->phy) {
			phy_power_on(rgb->phy);
			if (rgb->pclk) {
				clk_set_rate(rgb->pclk, pclk_clk_rate);
				clk_prepare_enable(rgb->pclk);
			}
		}
		drm_panel_prepare(rgb->panel);
		ret = sunxi_rgb_enable_output(rgb->tcon_dev);
		if (ret < 0)
			DRM_DEV_INFO(rgb->dev, "failed to enable rgb ouput\n");
		drm_panel_enable(rgb->panel);
	}
	rgb->allow_sw_enable = false;
	DRM_INFO("[RGB] %s finish\n", __FUNCTION__);

	return;
}

void sunxi_drm_rgb_encoder_atomic_disable(struct drm_encoder *encoder,
					struct drm_atomic_state *state)
{
	struct sunxi_drm_rgb *rgb = drm_encoder_to_sunxi_drm_rgb(encoder);

	drm_panel_disable(rgb->panel);
	drm_panel_unprepare(rgb->panel);

	if (rgb->phy) {
		phy_power_off(rgb->phy);
	}
	if (rgb->pclk)
		clk_disable_unprepare(rgb->pclk);

	sunxi_lcd_pin_set_state(rgb->dev, "sleep");
	sunxi_rgb_disable_output(rgb->tcon_dev);
	sunxi_tcon_mode_exit(rgb->tcon_dev);
	DRM_DEBUG_DRIVER("%s finish\n", __FUNCTION__);
}

static bool sunxi_rgb_fifo_check(void *data)
{
	struct sunxi_drm_rgb *rgb = (struct sunxi_drm_rgb *)data;
	return sunxi_tcon_check_fifo_status(rgb->tcon_dev);
}

static bool sunxi_rgb_is_sync_time_enough(void *data)
{
	struct sunxi_drm_rgb *rgb = (struct sunxi_drm_rgb *)data;
	return sunxi_tcon_is_sync_time_enough(rgb->tcon_dev);
}

static void sunxi_rgb_enable_vblank(bool enable, void *data)
{
	struct sunxi_drm_rgb *rgb = (struct sunxi_drm_rgb *)data;

	sunxi_tcon_enable_vblank(rgb->tcon_dev, enable);
}

int sunxi_drm_rgb_encoder_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_rgb *rgb = encoder_to_sunxi_drm_rgb(encoder);

	/* FIXME:TODO: color_fmt/clolor_depth update by actual configuration */
//	scrtc_state->color_fmt = DISP_CSC_TYPE_RGB;
//	scrtc_state->color_depth = DISP_DATA_8BITS;
	scrtc_state->tcon_id = rgb->tcon_id;
	scrtc_state->is_sync_time_enough = sunxi_rgb_is_sync_time_enough;
	scrtc_state->enable_vblank = sunxi_rgb_enable_vblank;
	scrtc_state->check_status = sunxi_rgb_fifo_check;
	scrtc_state->output_dev_data = rgb;
	DRM_DEBUG_DRIVER("%s finish\n", __FUNCTION__);
	return 0;
}

static void sunxi_drm_rgb_encoder_mode_set(struct drm_encoder *encoder,
					struct drm_display_mode *mode,
					struct drm_display_mode *adj_mode)
{
	struct sunxi_drm_rgb *rgb = encoder_to_sunxi_drm_rgb(encoder);
	DRM_INFO("[RGB]%s start\n", __FUNCTION__);

	drm_mode_copy(&rgb->mode, adj_mode);
	DRM_INFO("[RGB]%s finish\n", __FUNCTION__);

}

static const struct drm_encoder_helper_funcs sunxi_rgb_encoder_helper_funcs = {
	.atomic_enable = sunxi_drm_rgb_encoder_atomic_enable,
	.atomic_disable = sunxi_drm_rgb_encoder_atomic_disable,
	.atomic_check = sunxi_drm_rgb_encoder_atomic_check,
	.mode_set = sunxi_drm_rgb_encoder_mode_set,
//	.loader_protect = sunxi_drm_rgb_encoder_loader_protect,
};

static int drm_rgb_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state,
		struct drm_property *property,
		uint64_t val)
{
	return 0;

}
static int drm_rgb_connector_get_property(struct drm_connector *connector,
		const struct drm_connector_state *state,
		struct drm_property *property,
		uint64_t *val)
{
	return 0;

}

static void drm_rgb_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs sunxi_rgb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_rgb_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = drm_rgb_connector_set_property,
	.atomic_get_property = drm_rgb_connector_get_property,
};

static int sunxi_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct sunxi_drm_rgb *rgb = connector_to_sunxi_drm_rgb(connector);

	DRM_INFO("[RGB]%s start\n", __FUNCTION__);
	return drm_panel_get_modes(rgb->panel, connector);
}

static const struct drm_connector_helper_funcs sunxi_rgb_connector_helper_funcs = {
	.get_modes = sunxi_rgb_connector_get_modes,
};

s32 sunxi_rgb_parse_dt(struct device *dev)
{
	struct sunxi_drm_rgb *rgb = dev_get_drvdata(dev);

	rgb->phy = devm_phy_get(dev, "combophy0");
	if (IS_ERR_OR_NULL(rgb->phy)) {
		DRM_INFO("rgb%d's combophy0 not setting, maybe not used!\n", rgb->rgb_id);
		rgb->phy = NULL;
	}

	rgb->pclk = devm_clk_get_optional(dev, "rgb_pclk");
	if (IS_ERR(rgb->pclk)) {
		DRM_ERROR("fail to get rgb_pclk\n");
	}

	return 0;
}
static int sunxi_drm_rgb_bind(struct device *dev, struct device *master, void *data)
{
	const int lcd_type = 1;
	struct drm_device *drm = (struct drm_device *)data;
	struct device *tcon_lcd_dev = NULL;
	struct sunxi_drm_private *drv_private = to_sunxi_drm_private(drm);
	struct sunxi_drm_rgb *rgb = dev_get_drvdata(dev);
	int ret, tcon_id;

	DRM_INFO("[RGB]%s start\n", __FUNCTION__);
	ret = sunxi_rgb_parse_dt(dev);
	if (ret) {
		DRM_ERROR("sunxi_tcon_parse_dts failed\n");
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
			&rgb->panel, &rgb->bridge);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to find panel or bridge: %d\n", ret);
		return ret;
	}

	tcon_lcd_dev = drm_rgb_of_get_tcon(rgb->dev);
	if (tcon_lcd_dev == NULL) {
		DRM_ERROR("tcon_lcd for rgb not found!\n");
		return -ENODEV;
	}
	tcon_id = sunxi_tcon_of_get_id(tcon_lcd_dev);

	rgb->tcon_dev = tcon_lcd_dev;
	rgb->tcon_id = tcon_id;
	rgb->drm_dev = drm;
	drm_encoder_helper_add(&rgb->encoder, &sunxi_rgb_encoder_helper_funcs);
	ret = drm_simple_encoder_init(drm, &rgb->encoder, DRM_MODE_ENCODER_DPI);
	if (ret) {
		DRM_ERROR("Couldn't initialise the encoder for tcon %d\n", tcon_id);
		return ret;
	}

	rgb->encoder.possible_crtcs =
			drm_of_find_possible_crtcs(drm, tcon_lcd_dev->of_node);
	if (rgb->panel) {
		drm_connector_helper_add(&rgb->connector,
				&sunxi_rgb_connector_helper_funcs);

		ret = drm_connector_init(drm, &rgb->connector,
				&sunxi_rgb_connector_funcs,
				DRM_MODE_CONNECTOR_DPI);
		if (ret) {
			drm_encoder_cleanup(&rgb->encoder);
			DRM_ERROR("[RGB]Couldn't initialise the connector for tcon %d\n", tcon_id);
			return ret;
		}

		drm_connector_attach_encoder(&rgb->connector, &rgb->encoder);
	//	tcon_dev->cfg.private_data = rgb;
	} else {
		ret = drm_bridge_attach(&rgb->encoder, rgb->bridge, NULL, 0);
		if (ret) {
			drm_encoder_cleanup(&rgb->encoder);
			DRM_ERROR("[RGB]failed to attach bridge %d\n", ret);
			return ret;
		}
	}

	rgb->allow_sw_enable = true;
	rgb->bound = true;
	rgb->sw_enable = rgb->allow_sw_enable && drv_private->sw_enable &&
			    (drv_private->boot.device_type == lcd_type);
	DRM_INFO("[RGB]%s ok\n", __FUNCTION__);

	return 0;
}

static void sunxi_drm_rgb_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct sunxi_drm_rgb *rgb = dev_get_drvdata(dev);

	drm_connector_cleanup(&rgb->connector);
	drm_encoder_cleanup(&rgb->encoder);
	rgb->bound = false;
}

static const struct component_ops sunxi_drm_rgb_component_ops = {
	.bind = sunxi_drm_rgb_bind,
	.unbind = sunxi_drm_rgb_unbind,
};


static int sunxi_drm_rgb_probe(struct platform_device *pdev)
{
	struct sunxi_drm_rgb *rgb;
	struct device *dev = &pdev->dev;

	DRM_INFO("[RGB]%s start\n", __FUNCTION__);
	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->rgb_data = of_device_get_match_data(dev);
	if (!rgb->rgb_data) {
		DRM_ERROR("sunxi_drm_rgb fail to get match data\n");
		return -ENODEV;
	}
	rgb->rgb_id = rgb->rgb_data->id;
	rgb->dev = dev;

	dev_set_drvdata(dev, rgb);
	platform_set_drvdata(pdev, rgb);

	DRM_INFO("[RGB]%s ok\n", __FUNCTION__);

	return component_add(&pdev->dev, &sunxi_drm_rgb_component_ops);
}

static int sunxi_drm_rgb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sunxi_drm_rgb_component_ops);
	return 0;
}

struct platform_driver sunxi_rgb_platform_driver = {
	.probe = sunxi_drm_rgb_probe,
	.remove = sunxi_drm_rgb_remove,
	.driver = {
		.name = "drm-rgb",
		.owner = THIS_MODULE,
		.of_match_table = sunxi_drm_rgb_match,
	},
};
