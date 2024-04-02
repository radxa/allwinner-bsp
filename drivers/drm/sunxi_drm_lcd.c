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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <video/sunxi_display2.h>

#include "include.h"
#include "panel/panels.h"
#include "sunxi_device/sunxi_dsi.h"
#include "sunxi_device/sunxi_tcon.h"
#include "sunxi_drm_crtc.h"
#include "sunxi_drm_lcd.h"
#include "sunxi_drm_drv.h"

struct sunxi_drm_lcd {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct tcon_device *tcon_dev;
	unsigned int tcon_id;
	struct sunxi_panel *sunxi_panel;
	bool sw_enable;
	bool allow_sw_enable;
};

static inline struct sunxi_drm_lcd *
drm_connector_to_sunxi_drm_lcd(struct drm_connector *connector)
{
	return container_of(connector, struct sunxi_drm_lcd, connector);
}

static inline struct sunxi_drm_lcd *
drm_encoder_to_sunxi_drm_lcd(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sunxi_drm_lcd, encoder);
}

static int sunxi_lcd_connector_get_modes(struct drm_connector *connector)
{
	struct sunxi_drm_lcd *lcd = drm_connector_to_sunxi_drm_lcd(connector);
	return drm_panel_get_modes(&lcd->sunxi_panel->panel, connector);
}

static const struct drm_connector_funcs sunxi_lcd_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs
	sunxi_lcd_connector_helper_funcs = {
		.get_modes = sunxi_lcd_connector_get_modes,
	};

static int sunxi_lcd_pin_set_state(struct device *dev, char *name)
{
	int ret;
	struct pinctrl *pctl;
	struct pinctrl_state *state;

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

static int sunxi_lcd_enable_output(struct sunxi_drm_lcd *lcd)
{
	struct tcon_device *tcon_dev = lcd->tcon_dev;
	enum disp_lcd_if lcd_if = lcd->sunxi_panel->panel_para.lcd_if;
	struct disp_panel_para *panel = &lcd->sunxi_panel->panel_para;

	sunxi_tcon_tcon0_open(tcon_dev);
	if (lcd_if == LCD_IF_LVDS) {
		sunxi_tcon_lvds_open(tcon_dev);
	} else if (lcd_if == LCD_IF_DSI) {
		sunxi_dsi_open(lcd->sunxi_panel->dsi, panel);
	}
	return 0;
}

static int sunxi_lcd_disable_output(struct sunxi_drm_lcd *lcd)
{
	enum disp_lcd_if lcd_if = lcd->sunxi_panel->panel_para.lcd_if;

	if (lcd_if == LCD_IF_LVDS) {
		sunxi_tcon_lvds_close(lcd->tcon_dev);
	} else if (lcd_if == LCD_IF_DSI) {
		sunxi_dsi_close(lcd->sunxi_panel->dsi);
	}
	sunxi_tcon_tcon0_close(lcd->tcon_dev);
	return 0;
}

static int sunxi_drm_panel_enable(struct sunxi_drm_lcd *lcd)
{
	int ret;
	struct drm_panel *panel = &lcd->sunxi_panel->panel;

	if (!panel)
		return -EINVAL;

	if (lcd->sw_enable) {
		sunxi_panel_sw_enable(panel);
		return 0;
	}

	drm_panel_prepare(panel);

	if (panel->funcs && panel->funcs->enable) {
		ret = panel->funcs->enable(panel);
		if (ret < 0)
			return ret;
	}

	ret = sunxi_lcd_enable_output(lcd);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to enable lcd ouput: %d\n",
			     ret);

	ret = backlight_enable(panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to enable backlight: %d\n",
			     ret);

	return 0;
}

static int sunxi_drm_panel_disable(struct sunxi_drm_lcd *lcd)
{
	int ret;
	struct drm_panel *panel = &lcd->sunxi_panel->panel;
	if (!panel)
		return -EINVAL;

	ret = backlight_disable(panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to disable backlight: %d\n",
			     ret);

	ret = sunxi_lcd_disable_output(lcd);
	if (ret < 0)
		DRM_DEV_INFO(panel->dev, "failed to disable lcd ouput: %d\n",
			     ret);

	if (panel->funcs && panel->funcs->disable)
		return panel->funcs->disable(panel);

	return 0;
}

static int sunxi_lcd_enable(struct sunxi_drm_lcd *lcd,
			    struct sunxi_crtc_state *scrtc_state)
{
	int ret = 0;
	struct disp_panel_para *panel_para = &lcd->sunxi_panel->panel_para;
	struct sunxi_panel *sunxi_panel = lcd->sunxi_panel;

	if (panel_para->lcd_if == LCD_IF_DSI) {
		//* TODO add sw_enable */
		ret = sunxi_dsi_prepare(sunxi_panel->dsi, panel_para,
					scrtc_state->crtc_irq_handler,
					scrtc_state->base.crtc);
		if (ret) {
			DRM_ERROR("sunxi_dsi_prepare failed\n");
			goto OUT;
		}
	}

	sunxi_lcd_pin_set_state(sunxi_panel->panel.dev, "active");
	ret = sunxi_drm_panel_enable(lcd);
	if (ret) {
		DRM_ERROR("sunxi_drm_panel_enable failed\n");
		goto OUT;
	}

OUT:
	return ret;
}

// TODO add more disable ops
static void sunxi_lcd_disable(struct sunxi_drm_lcd *lcd,
			      struct sunxi_crtc_state *scrtc_state)
{
	sunxi_drm_panel_disable(lcd);
}

static void sunxi_lcd_enable_vblank(bool enable, void *data)
{
	/* for now nothing to do */
}

void sunxi_lcd_encoder_atomic_disable(struct drm_encoder *encoder,
				      struct drm_atomic_state *state)
{
	struct sunxi_drm_lcd *lcd = drm_encoder_to_sunxi_drm_lcd(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);

	sunxi_lcd_disable(lcd, scrtc_state);
	sunxi_tcon_lcd_mode_exit(lcd->tcon_dev);
	DRM_DEBUG_DRIVER("%s finish\n", __FUNCTION__);
}

void sunxi_lcd_encoder_atomic_enable(struct drm_encoder *encoder,
				     struct drm_atomic_state *state)
{
	struct drm_crtc *crtc = encoder->crtc;
	const int lcd_type = 1;
	struct sunxi_drm_private *drv_private = to_sunxi_drm_private(encoder->dev);
	int de_hw_id = sunxi_drm_crtc_get_hw_id(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_lcd *lcd = drm_encoder_to_sunxi_drm_lcd(encoder);

	/* TODO detect if is this lcd need sw_enable */
	lcd->sw_enable = lcd->allow_sw_enable && drv_private->sw_enable &&
			    drv_private->boot.device_type == lcd_type;
	lcd->allow_sw_enable = false;
	memcpy(&lcd->tcon_dev->cfg.panel, &lcd->sunxi_panel->panel_para,
	    sizeof(lcd->sunxi_panel->panel_para));
	memcpy(&lcd->tcon_dev->cfg.panel_ext,  &lcd->sunxi_panel->extend_para,
	    sizeof(lcd->sunxi_panel->extend_para));
	lcd->tcon_dev->cfg.de_id = de_hw_id;
	lcd->tcon_dev->cfg.irq_handler = scrtc_state->crtc_irq_handler;
	lcd->tcon_dev->cfg.irq_data = scrtc_state->base.crtc;
	lcd->tcon_dev->cfg.sw_enable = lcd->sw_enable;

	sunxi_tcon_lcd_mode_init(lcd->tcon_dev);
	sunxi_lcd_enable(lcd, scrtc_state);
	DRM_DEBUG_DRIVER("%s finish sw = %d\n", __FUNCTION__, lcd->sw_enable);
}

int sunxi_lcd_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_lcd *lcd = drm_encoder_to_sunxi_drm_lcd(encoder);

	scrtc_state->color_fmt = DISP_CSC_TYPE_RGB;
	scrtc_state->color_depth = DISP_DATA_8BITS;
	scrtc_state->tcon_id = lcd->tcon_id;
	scrtc_state->enable_vblank = sunxi_lcd_enable_vblank;
	scrtc_state->vblank_enable_data = lcd;
	DRM_DEBUG_DRIVER("%s finish\n", __FUNCTION__);
	return 0;
}

static const struct drm_encoder_helper_funcs sunxi_lcd_encoder_helper_funcs = {
	.atomic_disable = sunxi_lcd_encoder_atomic_disable,
	.atomic_enable = sunxi_lcd_encoder_atomic_enable,
	.atomic_check = sunxi_lcd_encoder_atomic_check,
};

int sunxi_lcd_create(struct tcon_device *tcon)
{
	struct device *dev = tcon->dev;
	struct drm_device *drm = tcon->drm;
	unsigned int id = tcon->hw_id;
	struct sunxi_drm_lcd *lcd;
	int ret;
	struct drm_panel *drm_panel;
	struct device_node *panel;
	int lcd_if = 100;
	int lcd_type[][3] = {
		{ LCD_IF_HV, DRM_MODE_CONNECTOR_Unknown,
		  DRM_MODE_ENCODER_NONE },
		{ LCD_IF_CPU, DRM_MODE_CONNECTOR_Unknown,
		  DRM_MODE_ENCODER_NONE },
		{ 2, DRM_MODE_CONNECTOR_Unknown, DRM_MODE_ENCODER_NONE },
		{ LCD_IF_LVDS, DRM_MODE_CONNECTOR_LVDS, DRM_MODE_ENCODER_LVDS },
		{ LCD_IF_DSI, DRM_MODE_CONNECTOR_DSI, DRM_MODE_ENCODER_DSI },
	};

	lcd = devm_kzalloc(drm->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	lcd->tcon_dev = tcon;
	lcd->tcon_id = id;

	panel = of_parse_phandle(dev->of_node, "panel", 0);
	if (panel) {
		drm_panel = of_drm_find_panel(panel);
		of_node_put(panel);
		if (IS_ERR(drm_panel))
			return PTR_ERR(drm_panel);
		lcd->sunxi_panel = to_sunxi_panel(drm_panel);
		lcd_if = lcd->sunxi_panel->panel_para.lcd_if;
	} else {
		DRM_ERROR("use panel set, but panel not found for tcon %d\n",
			  id);
		return -EINVAL;
	}

	if (lcd_if > LCD_IF_DSI) {
		DRM_ERROR("not support panel if %d\n", lcd_if);
		return -EINVAL;
	}
	drm_encoder_helper_add(&lcd->encoder, &sunxi_lcd_encoder_helper_funcs);
	ret = drm_simple_encoder_init(drm, &lcd->encoder, lcd_type[lcd_if][2]);
	if (ret) {
		DRM_ERROR("Couldn't initialise the encoder for tcon %d\n", id);
		return ret;
	}

	lcd->allow_sw_enable = true;
	lcd->encoder.possible_crtcs =
		drm_of_find_possible_crtcs(drm, dev->of_node);

	drm_connector_helper_add(&lcd->connector,
				 &sunxi_lcd_connector_helper_funcs);
	ret = drm_connector_init(drm, &lcd->connector,
				 &sunxi_lcd_connector_funcs,
				 lcd_type[lcd_if][1]);
	if (ret) {
		drm_encoder_cleanup(&lcd->encoder);
		DRM_ERROR("Couldn't initialise the connector for tcon %d\n",
			  id);
		return ret;
	}

	drm_connector_attach_encoder(&lcd->connector, &lcd->encoder);
	tcon->cfg.private_data = lcd;
	return 0;
}

int sunxi_lcd_destroy(struct tcon_device *tcon)
{
	struct sunxi_drm_lcd *lcd = tcon->cfg.private_data;
	drm_connector_cleanup(&lcd->connector);
	drm_encoder_cleanup(&lcd->encoder);
	return 0;
}
