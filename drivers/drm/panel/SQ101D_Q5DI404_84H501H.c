// SPDX-License-Identifier: GPL-2.0+
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * SQ101D_Q5DI404_84H501H panel driver
 *
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "panels.h"
//TODO remove private dsi api
#include "../sunxi_device/sunxi_dsi.h"

#define REGFLAG_DELAY 0XFE
#define REGFLAG_END_OF_TABLE 0xFD // END OF REGISTERS MARKER

struct LCM_setting_table {
	u8 cmd;
	u32 count;
	u8 para_list[64];
};

/*add panel initialization below*/

static struct LCM_setting_table lcm_initialization_setting[] = {

	{ 0x11, 1, { 0x00 } },
	{ REGFLAG_DELAY, REGFLAG_DELAY, { 120 } },

	{ 0x29, 1, { 0x00 } },
	{ REGFLAG_DELAY, REGFLAG_DELAY, { 20 } },
	{ REGFLAG_END_OF_TABLE, REGFLAG_END_OF_TABLE, {} }
};

static void LCD_panel_init(struct sunxi_panel *panel)
{
	__u32 i;
	struct device *dsi = panel->dsi;

	sunxi_dsi_clk_enable(dsi);
	msleep(20);
	sunxi_dsi_dcs_write_0para(dsi, DSI_DCS_SOFT_RESET);
	msleep(10);

	for (i = 0;; i++) {
		if (lcm_initialization_setting[i].count == REGFLAG_END_OF_TABLE)
			break;
		else if (lcm_initialization_setting[i].count == REGFLAG_DELAY)
			msleep(lcm_initialization_setting[i].para_list[0]);
		else
			sunxi_dsi_dcs_wr(
				dsi, lcm_initialization_setting[i].cmd,
				lcm_initialization_setting[i].para_list,
				lcm_initialization_setting[i].count);
		//break;
	}

	return;
}

static void LCD_panel_exit(struct sunxi_panel *panel)
{
	struct device *dsi = panel->dsi;
	sunxi_dsi_dcs_write_0para(dsi, DSI_DCS_SET_DISPLAY_OFF);
	msleep(20);
	sunxi_dsi_dcs_write_0para(dsi, DSI_DCS_ENTER_SLEEP_MODE);
	msleep(80);
}

static void sunxi_panel_extend_para_populate(struct sunxi_panel *sunxi_panel)
{
	struct panel_extend_para *info = &sunxi_panel->extend_para;
	u32 i = 0, j = 0;
	u32 items;
	u8 lcd_gamma_tbl[][2] = {
		//{input value, corrected value}
		{ 0, 0 },     { 15, 15 },   { 30, 30 },	  { 45, 45 },
		{ 60, 60 },   { 75, 75 },   { 90, 90 },	  { 105, 105 },
		{ 120, 120 }, { 135, 135 }, { 150, 150 }, { 165, 165 },
		{ 180, 180 }, { 195, 195 }, { 210, 210 }, { 225, 225 },
		{ 240, 240 }, { 255, 255 },
	};

	u32 lcd_cmap_tbl[2][3][4] = {
		{
			{ LCD_CMAP_G0, LCD_CMAP_B1, LCD_CMAP_G2, LCD_CMAP_B3 },
			{ LCD_CMAP_B0, LCD_CMAP_R1, LCD_CMAP_B2, LCD_CMAP_R3 },
			{ LCD_CMAP_R0, LCD_CMAP_G1, LCD_CMAP_R2, LCD_CMAP_G3 },
		},
		{
			{ LCD_CMAP_B3, LCD_CMAP_G2, LCD_CMAP_B1, LCD_CMAP_G0 },
			{ LCD_CMAP_R3, LCD_CMAP_B2, LCD_CMAP_R1, LCD_CMAP_B0 },
			{ LCD_CMAP_G3, LCD_CMAP_R2, LCD_CMAP_G1, LCD_CMAP_R0 },
		},
	};

	items = sizeof(lcd_gamma_tbl) / 2;
	for (i = 0; i < items - 1; i++) {
		u32 num = lcd_gamma_tbl[i + 1][0] - lcd_gamma_tbl[i][0];

		for (j = 0; j < num; j++) {
			u32 value = 0;

			value = lcd_gamma_tbl[i][1] +
				((lcd_gamma_tbl[i + 1][1] -
				  lcd_gamma_tbl[i][1]) *
				 j) / num;
			info->lcd_gamma_tbl[lcd_gamma_tbl[i][0] + j] =
				(value << 16) + (value << 8) + value;
		}
	}
	info->lcd_gamma_tbl[255] = (lcd_gamma_tbl[items - 1][1] << 16) +
				   (lcd_gamma_tbl[items - 1][1] << 8) +
				   lcd_gamma_tbl[items - 1][1];

	memcpy(info->lcd_cmap_tbl, lcd_cmap_tbl, sizeof(lcd_cmap_tbl));
}

static int sq101d_panel_unprepare(struct drm_panel *panel)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);

	sunxi_panel_reset_assert(sunxi_panel);
	msleep(5);
	regulator_disable(sunxi_panel->supply[0]);
	msleep(20);
	return 0;
}

static int sq101d_panel_prepare(struct drm_panel *panel)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);
	int ret;

	sunxi_panel_reset_assert(sunxi_panel);
	ret = regulator_enable(sunxi_panel->supply[0]);
	if (ret < 0) {
		dev_err(sunxi_panel->dev, "failed to enable supply: %d\n", ret);
		return ret;
	}
	msleep(20);

	sunxi_panel_reset_deassert(sunxi_panel);
	msleep(5);

	return 0;
}

static int sq101d_panel_enable(struct drm_panel *panel)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);

	LCD_panel_init(sunxi_panel);

	msleep(10);
	sunxi_panel_enable_gpio(sunxi_panel);
	return 0;
}

static int sq101d_panel_disable(struct drm_panel *panel)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);

	LCD_panel_exit(sunxi_panel);
	msleep(20);

	return 0;
}

static int sq101d_panel_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;

	drm_display_mode_from_videomode(&sunxi_panel->video_mode, mode);
	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	//	connector->display_info.width_mm = lvds->width;
	//	connector->display_info.height_mm = lvds->height;
	/*	drm_display_info_set_bus_formats(&connector->display_info,
					 &lvds->bus_format, 1);*/
	/*	connector->display_info.bus_flags = lvds->data_mirror
					  ? DRM_BUS_FLAG_DATA_LSB_TO_MSB
					  : DRM_BUS_FLAG_DATA_MSB_TO_LSB;*/
	//	drm_connector_set_panel_orientation(connector, lvds->orientation);

	return 1;
}
/*
static int sq101d_panel_get_timings(struct drm_panel *panel,
				    unsigned int num_timings,
				    struct display_timing *timings)
{
	struct sunxi_panel *sunxi_panel = to_sunxi_panel(panel);

	if (timings)
		*timings = kmemdup(&sunxi_panel->timings,
				  sizeof(struct display_timing), GFP_KERNEL);
	;

	return sunxi_panel->num_timings;
}*/

static const struct drm_panel_funcs sq101d_panel_funcs = {
	.prepare = sq101d_panel_prepare,
	.unprepare = sq101d_panel_unprepare,
	.get_modes = sq101d_panel_get_modes,
	.enable = sq101d_panel_enable,
	.disable = sq101d_panel_disable,
/*	.get_timings = sq101d_panel_get_timings,*/
};

static int sq101d_panel_parse_dt(struct sunxi_panel *sunxi_panel)
{
	int ret;

	ret = sunxi_panel_parse_timings(sunxi_panel);
	if (ret) {
		dev_err(sunxi_panel->dev, "sunxi panel parse timings failed\n");
		return ret;
	}

	ret = sunxi_panel_parse_misc(sunxi_panel);
	if (ret) {
		dev_err(sunxi_panel->dev, "sunxi panel parse misc failed\n");
		return ret;
	}

	ret = sunxi_panel_parse_dsi(sunxi_panel);
	if (ret) {
		dev_err(sunxi_panel->dev, "sunxi panel parse dsi failed\n");
		return ret;
	}

	return 0;
}

static int sq101d_panel_probe(struct mipi_dsi_device *dsi)
{
	struct sunxi_panel *sunxi_panel;
	int ret;

	sunxi_panel =
		devm_kzalloc(&dsi->dev, sizeof(struct sunxi_panel), GFP_KERNEL);
	if (!sunxi_panel)
		return -ENOMEM;

	sunxi_panel->dev = &dsi->dev;
	sunxi_panel->num_timings = 1;

	dev_err(sunxi_panel->dev, "panel initi\n");
	ret = sq101d_panel_parse_dt(sunxi_panel);
	if (ret)
		return ret;

	sunxi_panel_extend_para_populate(sunxi_panel);

	ret = sunxi_panel_get_power(sunxi_panel);
	if (ret)
		return ret;

	/* Get GPIOs*/
	ret = sunxi_panel_get_enable_gpio(sunxi_panel);
	if (ret)
		return ret;

	ret = sunxi_panel_get_reset_gpio(sunxi_panel);
	if (ret)
		return ret;

	/* Register the panel. */
	drm_panel_init(&sunxi_panel->panel, sunxi_panel->dev,
		       &sq101d_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	/* Get backlight controller. */
	ret = drm_panel_of_backlight(&sunxi_panel->panel);
	if (ret)
		return ret;

	drm_panel_add(&sunxi_panel->panel);

	dev_set_drvdata(sunxi_panel->dev, sunxi_panel);
	return 0;
}

static int sq101d_panel_remove(struct mipi_dsi_device *dsi)
{
	struct sunxi_panel *sunxi_panel = dev_get_drvdata(&dsi->dev);

	drm_panel_remove(&sunxi_panel->panel);

	drm_panel_disable(&sunxi_panel->panel);

	return 0;
}

static const struct of_device_id sq101d_of_table[] = {
	{
		.compatible = "SQ101D_Q5DI404_84H501",
	},
	{ /* Sentinel */ },
};

static struct mipi_dsi_driver sq101d_dsi_panel_driver = {
	.driver = {
		.name = "panel-SQ101D_Q5DI404_84H501",
		.of_match_table = sq101d_of_table,
	},
	.probe = sq101d_panel_probe,
	.remove = sq101d_panel_remove,
};

static int __init panel_init(void)
{
	int err = -EINVAL;

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI)) {
		err = mipi_dsi_driver_register(&sq101d_dsi_panel_driver);
	}

	return err;
}

module_init(panel_init);
/*
MODULE_DEVICE_TABLE(of, sq101d_of_table);

static struct platform_driver sq101d_panel_driver = {
	.probe		= sq101d_panel_probe,
	.remove		= sq101d_panel_remove,
	.driver		= {
		.name	= "SQ101D",
		.of_match_table = sq101d_of_table,
	},
};

module_platform_driver(sq101d_panel_driver);
*/
MODULE_AUTHOR("Huangyongxing <huangyongxing@allwinnertech.com>");
MODULE_DESCRIPTION("SQ101D_Q5DI404_84H501 Panel Driver");
MODULE_LICENSE("GPL");
