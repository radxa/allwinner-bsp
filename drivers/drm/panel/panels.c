// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 *
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_graph.h>

#include "panels.h"

static s32 sunxi_panel_get_dsi(struct sunxi_panel *sunxi_panel)
{
	int ret = -EINVAL;
	struct platform_device *pdev;
	struct device_node *dsi_node;

	if (sunxi_panel->panel_para.lcd_if != LCD_IF_DSI) {
		dev_err(sunxi_panel->dev,
			"try to get dsi for not dsi panel if = %d\n",
			sunxi_panel->panel_para.lcd_if);
		return ret;
	}
	dsi_node = of_get_parent(sunxi_panel->dev->of_node);
	if (!dsi_node) {
		dev_err(sunxi_panel->dev,
			"panel is not port of dsi device! get dsi failed\n");
		return ret;
	}

	pdev = of_find_device_by_node(dsi_node);
	if (IS_ERR_OR_NULL(pdev)) {
		dev_err(sunxi_panel->dev, "find dsi device failed \n");
		return ret;
	}

	sunxi_panel->dsi = &pdev->dev;
	return 0;
}

void sunxi_panel_para_from_timing(struct display_timing *timings,
				  struct disp_panel_para *panel_para)
{
	panel_para->lcd_x = timings->hactive.typ;
	panel_para->lcd_hbp = timings->hback_porch.typ;
	panel_para->lcd_hspw = timings->hsync_len.typ;
	panel_para->lcd_ht =
		timings->hactive.typ + timings->hback_porch.typ +
		/*timings->hsync_len.typ*/ +timings->hfront_porch.typ;

	panel_para->lcd_y = timings->vactive.typ;
	panel_para->lcd_vbp = timings->vback_porch.typ;
	panel_para->lcd_vspw = timings->vsync_len.typ;
	panel_para->lcd_vt =
		timings->vactive.typ + timings->vback_porch.typ +
		/*timings->vsync_len.typ*/ +timings->vfront_porch.typ; //fixme
	panel_para->lcd_dclk_freq = timings->pixelclock.typ / 1000000;
}
EXPORT_SYMBOL(sunxi_panel_para_from_timing);

s32 sunxi_panel_parse_timings(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	int ret;

	if (!np) {
		dev_err(sunxi_panel->dev,
			"sunxi panel's device node missing!\n");
		return -ENXIO;
	}

	ret = of_get_display_timing(np, "panel-timing", &sunxi_panel->timings);
	if (ret < 0) {
		dev_err(sunxi_panel->dev,
			"%pOF: problems parsing panel-timing (%d)\n", np, ret);
		return ret;
	}

	videomode_from_timing(&sunxi_panel->timings, &sunxi_panel->video_mode);
	sunxi_panel_para_from_timing(&sunxi_panel->timings,
				     &sunxi_panel->panel_para);

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_parse_timings);

s32 sunxi_panel_parse_delay_time(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	struct sunxi_panel_delay *delay = &sunxi_panel->delay;
	u32 val;

	if (!np) {
		dev_err(sunxi_panel->dev,
			"sunxi panel's device node missing!\n");
		return -ENXIO;
	}

	if (!of_property_read_u32(np, "prepare_time", &val))
		delay->prepare_time = val;

	if (!of_property_read_u32(np, "enable_time", &val))
		delay->enable_time = val;

	if (!of_property_read_u32(np, "disable_time", &val))
		delay->disable_time = val;

	if (!of_property_read_u32(np, "unprepare_time", &val))
		delay->unprepare_time = val;

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_parse_delay_time);

s32 sunxi_panel_parse_dsi(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	struct disp_panel_para *panel_para = &sunxi_panel->panel_para;
	u32 val;

	if (!np) {
		dev_err(sunxi_panel->dev,
			"sunxi panel's device node missing!\n");
		return -ENXIO;
	}

	if (!of_property_read_u32(np, "lcd_dsi_if", &val))
		panel_para->lcd_dsi_if = val;

	if (!of_property_read_u32(np, "lcd_dsi_lane", &val))
		panel_para->lcd_dsi_lane = val;

	//	if (!of_property_read_u32(np, "lcd_dclk_freq", &val))
	//		panel_para->lcd_dclk_freq = val;

	if (!of_property_read_u32(np, "lcd_dsi_format", &val))
		panel_para->lcd_dsi_format = val;

	if (!of_property_read_u32(np, "lcd_dsi_te", &val))
		panel_para->lcd_dsi_te = val;

	if (!of_property_read_u32(np, "lcd_dsi_eotp", &val))
		panel_para->lcd_dsi_eotp = val;

	if (!of_property_read_u32(np, "lcd_dsi_port_num", &val))
		panel_para->lcd_dsi_port_num = val;

	return sunxi_panel_get_dsi(sunxi_panel);
}
EXPORT_SYMBOL(sunxi_panel_parse_dsi);

s32 sunxi_panel_parse_lvds(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	struct disp_panel_para *panel_para = &sunxi_panel->panel_para;
	u32 val;

	if (!np) {
		dev_err(sunxi_panel->dev,
			"sunxi panel's device node missing!\n");
		return -ENXIO;
	}

	if (!of_property_read_u32(np, "lcd_lvds_if", &val))
		panel_para->lcd_lvds_if = val;

	if (!of_property_read_u32(np, "lcd_lvds_mode", &val))
		panel_para->lcd_lvds_mode = val;

	if (!of_property_read_u32(np, "lcd_lvds_colordepth", &val))
		panel_para->lcd_lvds_colordepth = val;

	if (!of_property_read_u32(np, "lcd_lvds_io_polarity", &val))
		panel_para->lcd_lvds_io_polarity = val;

	if (!of_property_read_u32(np, "lcd_lvds_clk_polarity", &val))
		panel_para->lcd_lvds_clk_polarity = val;

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_parse_lvds);

s32 sunxi_panel_parse_hv(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	struct disp_panel_para *panel_para = &sunxi_panel->panel_para;
	u32 val;

	if (!np) {
		dev_err(sunxi_panel->dev,
			"sunxi panel's device node missing!\n");
		return -ENXIO;
	}

	if (!of_property_read_u32(np, "lcd_hv_if", &val))
		panel_para->lcd_hv_if = val;

	if (!of_property_read_u32(np, "lcd_hv_clk_phase", &val))
		panel_para->lcd_hv_clk_phase = val;

	if (!of_property_read_u32(np, "lcd_hv_sync_polarity", &val))
		panel_para->lcd_hv_sync_polarity = val;

	if (!of_property_read_u32(np, "lcd_hv_data_polarity", &val))
		panel_para->lcd_hv_data_polarity = val;

	if (!of_property_read_u32(np, "lcd_hv_srgb_seq", &val))
		panel_para->lcd_hv_srgb_seq = val;

	if (!of_property_read_u32(np, "lcd_hv_syuv_seq", &val))
		panel_para->lcd_hv_syuv_seq = val;

	if (!of_property_read_u32(np, "lcd_hv_syuv_fdly", &val))
		panel_para->lcd_hv_syuv_fdly = val;

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_parse_hv);

s32 sunxi_panel_parse_misc(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	struct disp_panel_para *panel_para = &sunxi_panel->panel_para;
	u32 val;

	if (!np) {
		dev_err(sunxi_panel->dev,
			"sunxi panel's device node missing!\n");
		return -ENXIO;
	}

	if (!of_property_read_u32(np, "lcd_if", &val))
		panel_para->lcd_if = val;

	if (!of_property_read_u32(np, "lcd_frm", &val))
		panel_para->lcd_frm = val;

	if (!of_property_read_u32(np, "lcd_gamma_en", &val))
		panel_para->lcd_gamma_en = val;

	if (!of_property_read_u32(np, "lcd_xtal_freq", &val))
		panel_para->lcd_xtal_freq = val;

	if (!of_property_read_u32(np, "lcd_width", &val))
		panel_para->lcd_width = val;

	if (!of_property_read_u32(np, "lcd_height", &val))
		panel_para->lcd_height = val;

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_parse_misc);

s32 sunxi_panel_get_power(struct sunxi_panel *sunxi_panel)
{
	struct device_node *np = sunxi_panel->dev->of_node;
	struct regulator *regulator;
	char power_name[25];
	char power_supply_name[25];
	u32 i;
	int ret;

	for (i = 0; i < PANEL_POWER_MAX; i++) {
		sprintf(power_name, "power%d", i);
		sprintf(power_supply_name, "%s-supply", power_name);
		if (of_property_read_bool(np, power_supply_name)) {
			regulator = devm_regulator_get_optional(
				sunxi_panel->dev, power_name);
			if (IS_ERR(regulator)) {
				ret = PTR_ERR(regulator);

				if (ret != -ENODEV) {
					if (ret != -EPROBE_DEFER)
						dev_err(sunxi_panel->dev,
							"failed to request %s's regulator: %d\n",
							power_supply_name, ret);
					return ret;
				}

				sunxi_panel->supply[i] = NULL;
			} else
				sunxi_panel->supply[i] = regulator;
		}
	}

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_get_power);

s32 sunxi_panel_get_enable_gpio(struct sunxi_panel *sunxi_panel)
{
	int ret;

	sunxi_panel->enable_gpio = devm_gpiod_get_optional(
		sunxi_panel->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(sunxi_panel->enable_gpio)) {
		ret = PTR_ERR(sunxi_panel->enable_gpio);
		dev_err(sunxi_panel->dev, "failed to request %s GPIO: %d\n",
			"enable", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_get_enable_gpio);

s32 sunxi_panel_get_reset_gpio(struct sunxi_panel *sunxi_panel)
{
	int ret;

	sunxi_panel->reset_gpio = devm_gpiod_get_optional(
		sunxi_panel->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sunxi_panel->reset_gpio)) {
		ret = PTR_ERR(sunxi_panel->reset_gpio);
		dev_err(sunxi_panel->dev, "failed to request %s GPIO: %d\n",
			"reset", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_get_reset_gpio);

void sunxi_panel_set_gpio_output_value(struct gpio_desc *gpio,
				       unsigned int value)
{
	gpiod_set_value_cansleep(gpio, value);
}
EXPORT_SYMBOL(sunxi_panel_set_gpio_output_value);

void sunxi_panel_enable_gpio(struct sunxi_panel *sunxi_panel)
{
	if (sunxi_panel->enable_gpio)
		gpiod_set_value_cansleep(sunxi_panel->enable_gpio, 1);
}
EXPORT_SYMBOL(sunxi_panel_enable_gpio);

void sunxi_panel_disable_gpio(struct sunxi_panel *sunxi_panel)
{
	if (sunxi_panel->enable_gpio)
		gpiod_set_value_cansleep(sunxi_panel->enable_gpio, 0);
}
EXPORT_SYMBOL(sunxi_panel_disable_gpio);

void sunxi_panel_reset_assert(struct sunxi_panel *sunxi_panel)
{
	if (sunxi_panel->reset_gpio)
		gpiod_set_value_cansleep(sunxi_panel->reset_gpio, 0);
}
EXPORT_SYMBOL(sunxi_panel_reset_assert);

void sunxi_panel_reset_deassert(struct sunxi_panel *sunxi_panel)
{
	if (sunxi_panel->reset_gpio)
		sunxi_panel_set_gpio_output_value(sunxi_panel->reset_gpio, 1);
}
EXPORT_SYMBOL(sunxi_panel_reset_deassert);

int sunxi_panel_sw_enable(struct drm_panel *panel)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);
	int ret;
	int i;

	for (i = 0; i < PANEL_POWER_MAX; i++) {
		if (sunxi_panel->supply[i]) {
			ret = regulator_enable(sunxi_panel->supply[i]);
			if (ret) {
				dev_err(panel->dev, "failed to enable supply: %d ret =%d \n", i, ret);
				return ret;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(sunxi_panel_sw_enable);
