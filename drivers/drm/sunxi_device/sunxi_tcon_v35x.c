/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/* sunxi_tcon_v35x.c
 *
 * Copyright (C) 2023 Allwinnertech Co., Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <drm/drm_print.h>
#include <linux/of_graph.h>
#include <linux/interrupt.h>
#include <linux/component.h>
#include <linux/phy/phy.h>

#include "sunxi_tcon.h"
#include "sunxi_tcon_top.h"
#include "tcon_feat.h"
#include "sunxi_drm_lcd.h"
#include "sunxi_drm_hdmi.h"
#include "sunxi_drm_edp.h"

struct tcon_data {
	enum tcon_type type;
	enum tcon_interface_type intf_type;
	bool use_panel;
};

struct sunxi_tcon {
	int id;
	bool is_enabled;
	struct device *dev;
	struct device *tcon_top;
	struct phy *lvds_combo_phy0;
	struct phy *lvds_combo_phy1;
	struct tcon_device tcon_ctrl;

	uintptr_t reg_base;
	const struct tcon_data *tcon_data;

	/* clock resource */
	struct clk *mclk; /* module clk */
	struct clk *mclk_bus; /* module clk bus */
	struct reset_control *rst_bus_tcon;
	struct reset_control *rst_bus_lvds;
	unsigned long long tcon_div;

	/* interrupt resource */
	unsigned int irq_no;
	/* judge_line for start delay, used to judge if there is enough time
	 *to update and sync DE register
	 */
	unsigned int judge_line;
	void *output_data;

	void *irq_data;
	irq_handler_t irq_handler;
};

#ifdef CONFIG_AW_FPGA_S4
static struct lcd_clk_info clk_tbl[] = {
	{ LCD_IF_HV, 0x12, 1, 1, 0 },
	{ LCD_IF_CPU, 12, 1, 1, 0 },
	{ LCD_IF_LVDS, 7, 1, 1, 0 },
	{ LCD_IF_DSI, 4, 1, 4, 0 },
};
#else
static struct lcd_clk_info clk_tbl[] = {
	{ LCD_IF_HV, 6, 1, 1, 0 },	    { LCD_IF_CPU, 12, 1, 1, 0 },
	{ LCD_IF_LVDS, 7, 1, 1, 0 },
#if defined(DSI_VERSION_40)
	{ LCD_IF_DSI, 4, 1, 4, 150000000 },
#else
	{ LCD_IF_DSI, 4, 1, 4, 0 },
#endif /* endif DSI_VERSION_40 */
	{ LCD_IF_VDPO, 4, 1, 1, 0 },
};
#endif

static const struct tcon_data lcd_data = {
	.type = TCON_LCD,
	.intf_type = INTERFACE_LCD,
	.use_panel = true,
};

static const struct tcon_data hdmi_data = {
	.type = TCON_TV,
	.intf_type = INTERFACE_HDMI,
	.use_panel = true,
};

static const struct tcon_data edp_data = {
	.type = TCON_TV,
	.intf_type = INTERFACE_EDP,
	.use_panel = true,
};

static const struct tcon_data tve_data = {
	.type = TCON_TV,
	.intf_type = INTERFACE_TVE,
	.use_panel = false,
};

static const struct of_device_id sunxi_tcon_match[] = {
	{ .compatible = "allwinner,tcon-lcd", .data = &lcd_data },
	{ .compatible = "allwinner,tcon-tv-hdmi", .data = &hdmi_data },
	{ .compatible = "allwinner,tcon-tv-edp", .data = &edp_data },
	{ .compatible = "allwinner,tcon-tv-tve", .data = &tve_data },
	{},
};

//remember top put tcon_top
static int sunxi_tcon_get_tcon_top(struct sunxi_tcon *tcon)
{
	struct device_node *top_node =
		of_parse_phandle(tcon->dev->of_node, "top", 0);
	struct platform_device *pdev = of_find_device_by_node(top_node);
	if (!pdev) {
		DRM_INFO("tcon %d use no tcon top\n", tcon->id);
		return 0;
	}
	tcon->tcon_top = &pdev->dev;
	return 0;
}

static int sunxi_tcon_output_create(struct device *dev, struct drm_device *drm)
{
	int ret;
	struct sunxi_tcon *tcon = dev_get_drvdata(dev);
	struct tcon_device *ctrl = &tcon->tcon_ctrl;

	DRM_INFO("sunxi_tcon_output_create start\n");
	ctrl->dev = dev;
	ctrl->drm = drm;
	ctrl->hw_id = tcon->id;

	if (tcon->tcon_data->intf_type == INTERFACE_LCD) {
		ret = sunxi_lcd_create(ctrl);
		DRM_INFO("sunxi_lcd_create start\n");
	} else if (tcon->tcon_data->intf_type == INTERFACE_HDMI) {
		ret = sunxi_hdmi_drm_create(ctrl);
		DRM_INFO("sunxi_hdmi_drm_create start\n");
	} else if (tcon->tcon_data->intf_type == INTERFACE_EDP) {
		ret = sunxi_drm_edp_create(ctrl);
		DRM_INFO("sunxi_drm_edp_create start\n");
	} else if (tcon->tcon_data->intf_type == INTERFACE_TVE) {
		/* TODO */
		ret = 0;
	} else {
		ret = 0;
	}

	return ret;
}

static int sunxi_tcon_output_destroy(struct device *dev, struct drm_device *drm)
{
	int ret;
	struct sunxi_tcon *tcon = dev_get_drvdata(dev);

	if (tcon->tcon_data->intf_type == INTERFACE_LCD) {
		ret = sunxi_lcd_destroy(&tcon->tcon_ctrl);
	} else if (tcon->tcon_data->intf_type == INTERFACE_HDMI) {
		/* TODO */
		ret = 0;
	} else if (tcon->tcon_data->intf_type == INTERFACE_EDP) {
		ret = sunxi_drm_edp_destroy(&tcon->tcon_ctrl);
	} else if (tcon->tcon_data->intf_type == INTERFACE_TVE) {
		/* TODO */
		ret = 0;
	} else {
		ret = 0;
	}
	return ret;
}

int sunxi_tcon_tcon0_open(struct tcon_device *tcon)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);
	struct disp_panel_para *panel = &tcon->cfg.panel;
	return tcon0_open(hwtcon->id, panel);
}

int sunxi_tcon_tcon0_close(struct tcon_device *tcon)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);
	return tcon0_close(hwtcon->id);
}

int sunxi_tcon_lvds_open(struct tcon_device *tcon)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);
	struct disp_panel_para *panel = &tcon->cfg.panel;

	//	if (panel->lcd_lvds_if == LCD_LVDS_IF_DUAL_LINK) {
	phy_init(hwtcon->lvds_combo_phy0);
	phy_init(hwtcon->lvds_combo_phy1);
	//	}
	return lvds_open(hwtcon->id, panel);
}

int sunxi_tcon_lvds_close(struct tcon_device *tcon)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);

	lvds_close(hwtcon->id);
	//	if (panel->lcd_lvds_if == LCD_LVDS_IF_DUAL_LINK) {
	phy_exit(hwtcon->lvds_combo_phy0);
	phy_exit(hwtcon->lvds_combo_phy1);
	//	}
	return 0;
}

int sunxi_tcon_get_lcd_clk_info(struct lcd_clk_info *info,
				const struct disp_panel_para *panel)
{
	int tcon_div = 6;
	int lcd_div = 1;
	int dsi_div = 4;
	int dsi_rate = 0;
	int i;
	int find = 0;

	if (panel == NULL) {
		DRM_ERROR("panel is NULL\n");
		return 1;
	}

	for (i = 0; i < sizeof(clk_tbl) / sizeof(clk_tbl[0]); i++) {
		if (clk_tbl[i].lcd_if == panel->lcd_if) {
			tcon_div = clk_tbl[i].tcon_div;
			lcd_div = clk_tbl[i].lcd_div;
			dsi_div = clk_tbl[i].dsi_div;
			dsi_rate = clk_tbl[i].dsi_rate;
			find = 1;
			break;
		}
	}
	if (find == 0) {
		DRM_ERROR("cant find clk info for lcd_if %d\n", panel->lcd_if);
		return 1;
	}

#if defined(DSI_VERSION_40)
	if (panel->lcd_if == LCD_IF_DSI) {
		u32 lane = panel->lcd_dsi_lane;
		u32 bitwidth = 0;

		switch (panel->lcd_dsi_format) {
		case LCD_DSI_FORMAT_RGB888:
			bitwidth = 24;
			break;
		case LCD_DSI_FORMAT_RGB666:
			bitwidth = 24;
			break;
		case LCD_DSI_FORMAT_RGB565:
			bitwidth = 16;
			break;
		case LCD_DSI_FORMAT_RGB666P:
			bitwidth = 18;
			break;
		}

		dsi_div = bitwidth / lane;
		if (panel->lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE) {
			tcon_div = dsi_div;
		}
	}
#endif

	if (panel->lcd_if == LCD_IF_HV &&
	    panel->lcd_hv_if == LCD_HV_IF_CCIR656_2CYC &&
	    panel->ccir_clk_div > 0)
		tcon_div = panel->ccir_clk_div;
	else if (panel->lcd_tcon_mode == DISP_TCON_DUAL_DSI &&
		 panel->lcd_if == LCD_IF_DSI) {
		tcon_div = tcon_div / 2;
		dsi_div /= 2;
	}

#if defined(DSI_VERSION_28)
	if (panel->lcd_if == LCD_IF_DSI &&
	    panel->lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE) {
		tcon_div = 6;
		dsi_div = 6;
	}
#endif

	info->tcon_div = tcon_div;
	info->lcd_div = lcd_div;
	info->dsi_div = dsi_div;
	info->dsi_rate = dsi_rate;

	return 0;
}

static int sunxi_tcon_set_clk(struct sunxi_tcon *hwtcon,
			      struct disp_panel_para *p_panel)
{
	int ret = 0;
	unsigned long pll_rate = 297000000, lcd_rate = 33000000;
	unsigned long dclk_rate = 33000000;
	unsigned long pll_rate_set = 297000000, lcd_rate_set = 33000000;
	struct clk *parent_clk;
	struct lcd_clk_info clk_info;

	if (!hwtcon->tcon_ctrl.cfg.sw_enable) {
		memset(&clk_info, 0, sizeof(clk_info));
		ret = sunxi_tcon_get_lcd_clk_info(&clk_info, p_panel);
		if (ret) {
			DRM_ERROR("Get clk_info fail!\n");
			return ret;
		}
		dclk_rate = ((unsigned long long)(p_panel->lcd_dclk_freq)) * 1000000;

		if (p_panel->lcd_if == LCD_IF_DSI) {
			lcd_rate = dclk_rate * clk_info.dsi_div;
			pll_rate = lcd_rate * clk_info.lcd_div;
		} else {
			lcd_rate = dclk_rate * clk_info.tcon_div;
			pll_rate = lcd_rate * clk_info.lcd_div;
		}

		parent_clk = clk_get_parent(hwtcon->mclk);
		if (parent_clk) {
			clk_set_rate(parent_clk, pll_rate);
			pll_rate_set = clk_get_rate(parent_clk);
		} else {
			DRM_INFO("clk parent not found for lcd\n");
		}

		if (clk_info.lcd_div)
			lcd_rate_set = pll_rate_set / clk_info.lcd_div;
		else
			lcd_rate_set = pll_rate_set;

		clk_set_rate(hwtcon->mclk, lcd_rate_set);
		lcd_rate_set = clk_get_rate(hwtcon->mclk);

		if (lcd_rate_set != lcd_rate)
			DRM_INFO("lcd rate to be set:%lu, real clk rate:%lu\n", lcd_rate,
				 lcd_rate_set);
	}

	if (hwtcon->rst_bus_tcon) {
		ret = reset_control_deassert(hwtcon->rst_bus_tcon);
		if (ret) {
			DRM_ERROR("reset_control_deassert for rst_bus_tcon failed!\n");
			return -1;
		}
	}

	if (p_panel->lcd_if == LCD_IF_LVDS) {
		if (hwtcon->rst_bus_lvds) {
			ret = reset_control_deassert(hwtcon->rst_bus_lvds);
			if (ret) {
				DRM_ERROR("reset_control_deassert for rst_bus_lvds failed!\n");
				return -1;
			}
		}
	}

	ret = clk_prepare_enable(hwtcon->mclk);
	if (ret != 0) {
		DRM_ERROR("fail enable TCON%d's clock!\n", hwtcon->id);
		return -1;
	}

	ret = clk_prepare_enable(hwtcon->mclk_bus);
	if (ret != 0) {
		DRM_ERROR("fail enable TCON%d's bus clock!\n", hwtcon->id);
		return -1;
	}

	hwtcon->tcon_div = clk_info.tcon_div;

	return 0;
}

static void sunxi_tcon_lcd_calc_judge_line(struct sunxi_tcon *hwtcon,
					   struct disp_panel_para *p_panel)
{
	unsigned int usec_per_line, start_delay;
	unsigned int usec_start_delay, usec_judge_point;

	usec_per_line = p_panel->lcd_ht / p_panel->lcd_dclk_freq;
	start_delay = tcon_get_start_delay(hwtcon->id, hwtcon->tcon_data->type);
	usec_start_delay = start_delay * usec_per_line;

	if (usec_start_delay <= 200)
		usec_judge_point = usec_start_delay * 3 / 7;
	else if (usec_start_delay <= 400)
		usec_judge_point = usec_start_delay / 2;
	else
		usec_judge_point = 200;

	hwtcon->judge_line = usec_judge_point / usec_per_line;
}

static int sunxi_tcon_lcd_prepare(struct sunxi_tcon *hwtcon,
				  struct disp_panel_para *p_panel)
{
	int ret = 0;
	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_enable(hwtcon->tcon_top);
	ret = sunxi_tcon_set_clk(hwtcon, p_panel);
	if (ret < 0) {
		DRM_ERROR("sunxi_tcon_set_clk failed\n");
		return -1;
	}

	return 0;
}

static void sunxi_tcon_lcd_unprepare(struct sunxi_tcon *hwtcon)
{
	//fixme
	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_disable(hwtcon->tcon_top);
	clk_disable_unprepare(hwtcon->mclk_bus);
	clk_disable_unprepare(hwtcon->mclk);
	reset_control_assert(hwtcon->rst_bus_lvds);
	reset_control_assert(hwtcon->rst_bus_tcon);
}

static int sunxi_tcon_device_query_irq(struct sunxi_tcon *hwtcon)
{
	int ret = 0;
	enum __lcd_irq_id_t irq_id;
	irq_id = (hwtcon->tcon_data->type == TCON_LCD) ? LCD_IRQ_TCON0_VBLK :
							 LCD_IRQ_TCON1_VBLK;
	ret = tcon_irq_query(hwtcon->id, irq_id);

	return ret;
}

static irqreturn_t sunxi_tcon_irq_event_proc(int irq, void *parg)
{
	struct sunxi_tcon *hwtcon = parg;
	sunxi_tcon_device_query_irq(hwtcon);
	return hwtcon->irq_handler(irq, hwtcon->irq_data);
}

static int sunxi_tcon_request_irq(struct sunxi_tcon *hwtcon)
{
	int ret;
	hwtcon->irq_handler = hwtcon->tcon_ctrl.cfg.irq_handler;
	hwtcon->irq_data = hwtcon->tcon_ctrl.cfg.irq_data;

	ret = devm_request_irq(hwtcon->dev, hwtcon->irq_no,
			       sunxi_tcon_irq_event_proc, 0, dev_name(hwtcon->dev),
			       hwtcon);
	if (ret) {
		DRM_ERROR("Couldn't request the IRQ for tcon\n");
	}
	return ret;
}

static int sunxi_tcon_free_irq(struct sunxi_tcon *hwtcon)
{
	if (hwtcon->irq_data != hwtcon->tcon_ctrl.cfg.irq_data) {
		DRM_ERROR("Couldn't free the IRQ for tcon\n");
		return -EINVAL;
	}
	devm_free_irq(hwtcon->dev, hwtcon->irq_no, hwtcon);
	return 0;
}

int sunxi_tcon_lcd_mode_init(struct tcon_device *tcon)
{
	int ret = 0;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);
	struct disp_panel_para *p_panel = &tcon->cfg.panel;
	unsigned int de_id = tcon->cfg.de_id;
	struct panel_extend_para *p_panel_ext = &tcon->cfg.panel_ext;

	ret = sunxi_tcon_lcd_prepare(hwtcon, p_panel);
	if (ret < 0) {
		DRM_ERROR("sunxi_tcon_lcd_prepare failed\n");
		return ret;
	}
	if (!tcon->cfg.sw_enable) {
		tcon_init(hwtcon->id);
		tcon0_set_dclk_div(hwtcon->id, hwtcon->tcon_div);
		if (tcon0_cfg(hwtcon->id, p_panel, 0) != 0) { /* FIXME   the final param is rcq related */
			DRM_ERROR("lcd cfg fail!\n");
			return -1;
		}
		tcon0_cfg_ext(hwtcon->id, p_panel_ext);
		tcon0_src_select(hwtcon->id, LCD_SRC_DE, de_id);
		sunxi_tcon_lcd_calc_judge_line(hwtcon, p_panel);
	}
	sunxi_tcon_request_irq(hwtcon);

	hwtcon->is_enabled = true;

	return 0;
}

int sunxi_tcon_lcd_mode_exit(struct tcon_device *tcon)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);

	sunxi_tcon_free_irq(hwtcon);

	hwtcon->is_enabled = false;
	hwtcon->judge_line = 0;

	tcon_exit(hwtcon->id);
	sunxi_tcon_lcd_unprepare(hwtcon);

	return 0;
}


/*******************************************************************************
 * @desc: suxni tcon tv for hdmi api, referred from sunxi display2
 ******************************************************************************/
static void _sunxi_tcon_hdmi_calc_judge_line(struct sunxi_tcon *hwtcon,
		struct disp_video_timings *p_timgs)
{
	unsigned int usec_per_line, start_delay;
	unsigned int usec_start_delay, usec_judge_point;

	usec_per_line = p_timgs->hor_total_time * 1000000ull;
	do_div(usec_per_line, p_timgs->pixel_clk);

	start_delay = tcon_get_start_delay(hwtcon->id, hwtcon->tcon_data->type);
	usec_start_delay = start_delay * usec_per_line;

	if (usec_start_delay <= 200)
		usec_judge_point = usec_start_delay * 3 / 7;
	else if (usec_start_delay <= 400)
		usec_judge_point = usec_start_delay / 2;
	else
		usec_judge_point = 200;

	hwtcon->judge_line = usec_judge_point / usec_per_line;

	DRM_INFO("[SUNXI-TCON-HDMI]tcon%d judge_line:%u\n",
			hwtcon->id, hwtcon->judge_line);
}

static int _sunxi_tcon_hdmi_cfg_clk(struct sunxi_tcon *hwtcon,
		unsigned long rate)
{
	int ret = 0;
	struct clk *parent_clk = NULL;
	long rate_diff = 0, parent_rate_diff = 0;
	unsigned long round_rate = 0;
	unsigned long parent_rate = 0, parent_round_rate = 0;
	unsigned int div = 1;

	if (!hwtcon->mclk) {
		DRM_ERROR("%s tcon module clock is null\n", __func__);
		return -1;
	}

	parent_clk = clk_get_parent(hwtcon->mclk);
	if (!parent_clk) {
		DRM_ERROR("can not get tcon hdmi parent clock!\n");
		return -1;
	}

	round_rate = clk_round_rate(hwtcon->mclk, rate);
	rate_diff = (long)(round_rate - rate);
	if ((rate_diff > 5000000) || (rate_diff < -5000000)) {
		for (div = 1; (rate * div) <= 600000000; div++) {
			parent_rate = rate * div;
			parent_round_rate = clk_round_rate(parent_clk, parent_rate);
			parent_rate_diff = (long)(parent_round_rate - parent_rate);
			if ((parent_rate_diff < 5000000) && (parent_rate_diff > -5000000)) {
				clk_set_rate(parent_clk, parent_rate);
				clk_set_rate(hwtcon->mclk, rate);
				break;
			}
		}
		if ((rate * div) > 600000000)
			clk_set_rate(hwtcon->mclk, rate);
	} else {
		clk_set_rate(hwtcon->mclk, rate);
	}

	ret = clk_prepare_enable(hwtcon->mclk);
	if (ret != 0) {
		DRM_ERROR("can not enable tcon hdmi clock!\n");
		return -1;
	}

	return 0;
}

static int _sunxi_tcon_hdmi_set_clk(struct sunxi_tcon *hwtcon,
		unsigned long pclk)
{
	int ret = 0;

	/* deassert tcon bus clock */
	if (hwtcon->rst_bus_tcon) {
		ret = reset_control_deassert(hwtcon->rst_bus_tcon);
		if (ret != 0) {
			DRM_ERROR("%s reset tcon bus failed\n", __func__);
			return -1;
		}
	}

	/* enable tcon bus clock */
	if (hwtcon->mclk_bus) {
		ret = clk_prepare_enable(hwtcon->mclk_bus);
		if (ret != 0) {
			DRM_ERROR("%s enable tcon bus clock failed\n", __func__);
			return -1;
		}
	}

	/* config tcon hdmi clock */
	ret = _sunxi_tcon_hdmi_cfg_clk(hwtcon, pclk);
	if (ret != 0) {
		DRM_ERROR("%s config tcon clock failed\n", __func__);
		return -1;
	}

	return 0;
}

static int _sunxi_tcon_hdmi_clk_unset(struct sunxi_tcon *hwtcon)
{
	if (!hwtcon) {
		DRM_ERROR("%s param is null!!!\n", __func__);
		return -1;
	}

	clk_disable_unprepare(hwtcon->mclk);

	clk_disable_unprepare(hwtcon->mclk_bus);

	reset_control_assert(hwtcon->rst_bus_tcon);

	return 0;
}

static int _sunxi_tcon_hdmi_prepare(struct sunxi_tcon *hwtcon,
		unsigned long pclk)
{
	int ret = 0;

	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_enable(hwtcon->tcon_top);
	ret = _sunxi_tcon_hdmi_set_clk(hwtcon, pclk);
	if (ret < 0) {
		DRM_ERROR("tcon hdmi set clock failed\n");
		return -1;
	}

	return 0;
}

static int _sunxi_tcon_hdmi_unprepare(struct sunxi_tcon *hwtcon)
{
	int ret = 0;

	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_disable(hwtcon->tcon_top);

	ret = _sunxi_tcon_hdmi_clk_unset(hwtcon);
	if (ret < 0) {
		DRM_ERROR("_sunxi_tcon_hdmi_clk_unset failed\n");
		return -1;
	}
	return 0;
}

static int _sunxi_tcon_hdmi_config(struct sunxi_tcon *hwtcon, unsigned int de_id,
		struct disp_video_timings *p_timing, enum disp_csc_type format)
{
	struct disp_video_timings *timings = NULL;

	if (!p_timing) {
		DRM_ERROR("point p_timing is null\n");
		return -1;
	}

	timings = kmalloc(sizeof(struct disp_video_timings), GFP_KERNEL | __GFP_ZERO);
	if (timings) {
		memcpy(timings, p_timing, sizeof(struct disp_video_timings));
		if (format == DISP_CSC_TYPE_YUV420) {
			timings->x_res /= 2;
			timings->hor_total_time /= 2;
			timings->hor_back_porch /= 2;
			timings->hor_front_porch /= 2;
			timings->hor_sync_time /= 2;
		}
	}

	tcon_init(hwtcon->id);

	tcon1_set_timming(hwtcon->id, timings ? timings : p_timing);

	tcon1_src_select(hwtcon->id, LCD_SRC_DE, de_id);

	tcon1_black_src(hwtcon->id, 0, format);

#if IS_ENABLED(CONFIG_ARCH_SUN55IW3)
	tcon1_volume_force(hwtcon->id, 0x00040014);
#endif /* CONFIG_ARCH_SUN55IW3 */

	tcon1_hdmi_clk_enable(hwtcon->id, 1);

	tcon1_open(hwtcon->id);

	kfree(timings);

	return 0;
}

static int _sunxi_tcon_hdmi_disconfig(struct sunxi_tcon *hwtcon)
{
	tcon1_close(hwtcon->id);

	tcon_exit(hwtcon->id);

	tcon1_hdmi_clk_enable(hwtcon->id, 0);

	return 0;
}

int sunxi_tcon_hdmi_mode_init(struct tcon_device *tcon)
{
	int ret = 0;
	unsigned long pclk;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);
	unsigned int de_id = tcon->cfg.de_id;
	struct disp_video_timings *p_timing = &tcon->cfg.timing;
	enum disp_csc_type format = tcon->cfg.format;

	if (hwtcon->is_enabled) {
		DRM_WARN("tcon hdmi has been enable");
		return 0;
	}

	/* calculate actual pixel clock */
	pclk = p_timing->pixel_clk * (p_timing->pixel_repeat + 1);
	if (format == DISP_CSC_TYPE_YUV420)
		pclk /= 2;

	_sunxi_tcon_hdmi_calc_judge_line(hwtcon, p_timing);

	ret = _sunxi_tcon_hdmi_prepare(hwtcon, pclk);
	if (ret != 0) {
		DRM_ERROR("tcon hdmi prepare failed\n");
		return ret;
	}

	ret = _sunxi_tcon_hdmi_config(hwtcon, de_id, p_timing, format);
	if (ret != 0) {
		DRM_ERROR("tcon hdmi config failed\n");
		return ret;
	}

	sunxi_tcon_request_irq(hwtcon);
	hwtcon->is_enabled = true;

	return 0;
}

int sunxi_tcon_hdmi_mode_exit(struct tcon_device *tcon)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon->dev);
	int ret = 0;

	if (!hwtcon->is_enabled) {
		DRM_WARN("tcon hdmi has been disable");
		return 0;
	}

	sunxi_tcon_free_irq(hwtcon);
	ret = _sunxi_tcon_hdmi_disconfig(hwtcon);
	if (ret != 0) {
		DRM_ERROR("_sunxi_tcon_hdmi_disconfig failed\n");
		return -1;
	}

	ret = _sunxi_tcon_hdmi_unprepare(hwtcon);
	if (ret != 0) {
		DRM_ERROR("_sunxi_tcon_hdmi_unprepare failed\n");
		return -1;
	}

	hwtcon->judge_line = 0;
	hwtcon->is_enabled = false;

	return 0;
}
static int edp_clk_enable(struct sunxi_tcon *hwtcon)
{
	int ret = 0;

	if (!hwtcon->rst_bus_tcon) {
		DRM_WARN("[%s] edp reset is NULL\n", __func__);
		return -1;
	}

	if (!hwtcon->mclk_bus) {
		DRM_WARN("[%s] edp clk_bus is NULL\n", __func__);
		return -1;
	}

	if (!hwtcon->mclk) {
		DRM_WARN("edp clk is NULL\n");
		return -1;
	}

	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_enable(hwtcon->tcon_top);

	ret = reset_control_deassert(hwtcon->rst_bus_tcon);
	if (ret) {
		DRM_ERROR("[%s] deassert reset tcon edp failed!\n", __func__);
		return -1;
	}

	ret = clk_prepare_enable(hwtcon->mclk_bus);
	if (ret != 0) {
		DRM_WARN("fail enable edp's bus clock!\n");
		return -1;
	}

	if (clk_prepare_enable(hwtcon->mclk)) {
		DRM_WARN("fail to enable edp clk\n");
		return -1;
	}

	return 0;
}

static int edp_clk_disable(struct sunxi_tcon *hwtcon)
{
	int ret = 0;

	if (!hwtcon->mclk) {
		DRM_WARN("edp clk is NULL\n");
		return -1;
	}

	if (!hwtcon->mclk_bus) {
		DRM_WARN("[%s] edp clk_bus is NULL\n", __func__);
		return -1;
	}

	if (!hwtcon->rst_bus_tcon) {
		DRM_WARN("[%s] edp reset is NULL\n", __func__);
		return -1;
	}

	clk_disable_unprepare(hwtcon->mclk);

	clk_disable_unprepare(hwtcon->mclk_bus);

	ret = reset_control_assert(hwtcon->rst_bus_tcon);
	if (ret) {
		DRM_ERROR("[%s] assert reset tcon edp failed!\n", __func__);
		return -1;
	}

	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_disable(hwtcon->tcon_top);

	return 0;
}

static void sunxi_tcon_edp_calc_judge_line(struct sunxi_tcon *hwtcon,
					   struct disp_video_timings *timings)
{
	unsigned int usec_per_line, start_delay;
	unsigned int usec_start_delay, usec_judge_point;

	usec_per_line =
		    timings->hor_total_time * 1000000 / timings->pixel_clk;
	start_delay = tcon_get_start_delay(hwtcon->id, hwtcon->tcon_data->type);
	usec_start_delay = start_delay * usec_per_line;

	if (usec_start_delay <= 200)
		usec_judge_point = usec_start_delay * 3 / 7;
	else if (usec_start_delay <= 400)
		usec_judge_point = usec_start_delay / 2;
	else
		usec_judge_point = 200;

	hwtcon->judge_line = usec_judge_point / usec_per_line;
}

int sunxi_tcon_show_pattern(struct tcon_device *tcon_dev, int pattern)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev->dev);

	tcon_show_builtin_patten(hwtcon->id, pattern);

	return 0;
}

int sunxi_tcon_pattern_get(struct tcon_device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev->dev);

	return tcon0_src_get(hwtcon->id);
}

int sunxi_tcon_edp_mode_init(struct tcon_device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev->dev);
	struct disp_video_timings *timings = &tcon_dev->cfg.timing;
	unsigned int de_id = tcon_dev->cfg.de_id;

	if (hwtcon->is_enabled) {
		DRM_WARN("tcon edp has been enable!\n");
		return 0;
	}

	edp_clk_enable(hwtcon);
	clk_set_rate(hwtcon->mclk, timings->pixel_clk);

	if (!tcon_dev->cfg.sw_enable) {
		tcon_init(hwtcon->id);
		tcon1_set_timming(hwtcon->id, timings);

		tcon1_src_select(hwtcon->id, LCD_SRC_DE, de_id);

		tcon1_edp_clk_enable(hwtcon->id, 1);
		tcon1_open(hwtcon->id);

		sunxi_tcon_edp_calc_judge_line(hwtcon, timings);
	}

	sunxi_tcon_request_irq(hwtcon);

	hwtcon->is_enabled = true;

	return 0;
}

int sunxi_tcon_edp_mode_exit(struct tcon_device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev->dev);

	if (!hwtcon->is_enabled) {
		DRM_WARN("tcon edp has been disable!\n");
		return 0;
	}

	sunxi_tcon_free_irq(hwtcon);
	tcon1_close(hwtcon->id);
	tcon_exit(hwtcon->id);
	tcon1_edp_clk_enable(hwtcon->id, 0);

	edp_clk_disable(hwtcon);

	hwtcon->is_enabled = false;
	hwtcon->judge_line = 0;

	return 0;
}

/*
 * referred from sunxi display
 */
//call by de, not use now
#define TODO_TEMP_MASK 0
#if TODO_TEMP_MASK
bool sunxi_tcon_sync_time_is_enough(unsigned int nr)
{
	int cur_line, judge_line, start_delay;
	unsigned int tcon_type;
	struct sunxi_tcon *hwtcon = sunxi_tcon_get_tcon(nr);

	tcon_type = hwtcon->type;
	judge_line = hwtcon->judge_line;

	cur_line = tcon_get_cur_line(nr, tcon_type);
	start_delay = tcon_get_start_delay(nr, tcon_type);

	/*
	DRM_INFO("cur_line:%d start_delay:%d judge_line:%d\n",
			cur_line, start_delay, judge_line);
	 */

	if (cur_line <= (start_delay - judge_line))
		return true;

	return false;
}

#endif

static int sunxi_tcon_init_al(struct device *dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(dev);
	tcon_set_reg_base(hwtcon->id, hwtcon->reg_base);
	return 0;
}

static int sunxi_tcon_parse_dts(struct device *dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hwtcon->reg_base = (uintptr_t)devm_ioremap_resource(dev, res);

	if (!hwtcon->reg_base) {
		DRM_ERROR("unable to map io for tcon\n");
		return -EINVAL;
	}

	hwtcon->irq_no = platform_get_irq(pdev, 0);
	if (!hwtcon->irq_no) {
		DRM_ERROR("get irq no of tcon failed\n");
		return -EINVAL;
	}

	hwtcon->mclk = devm_clk_get(dev, "clk_tcon");
	if (IS_ERR(hwtcon->mclk)) {
		DRM_ERROR("fail to get clk for tcon \n");
		return -EINVAL;
	}

	hwtcon->mclk_bus = devm_clk_get(dev, "clk_bus_tcon");
	if (IS_ERR(hwtcon->mclk_bus)) {
		DRM_ERROR("fail to get clk bus for tcon\n");
		return -EINVAL;
	}

	hwtcon->rst_bus_tcon =
		devm_reset_control_get_shared(dev, "rst_bus_tcon");
	if (IS_ERR(hwtcon->rst_bus_tcon)) {
		DRM_ERROR("fail to get reset clk for tcon\n");
		return -EINVAL;
	}

	hwtcon->rst_bus_lvds =
		devm_reset_control_get_optional_shared(dev, "rst_bus_lvds");
	if (IS_ERR(hwtcon->rst_bus_lvds)) {
		DRM_ERROR("fail to get reset clk for tcon\n");
		return -EINVAL;
	}

	return 0;
}

static int sunxi_tcon_of_get_id(struct device *tcon_dev)
{
	struct device_node *node = tcon_dev->of_node;
	struct device_node *disp0_output_ep;
	struct device_node *tcon_in_disp0_ep;
	struct of_endpoint endpoint;
	int ret;

	tcon_in_disp0_ep = of_graph_get_endpoint_by_regs(node, 0, 0);
	if (!tcon_in_disp0_ep) {
		DRM_ERROR("endpoint tcon_in_disp0_ep not fount\n");
		return -EINVAL;
	}
	disp0_output_ep = of_graph_get_remote_endpoint(tcon_in_disp0_ep);
	if (!disp0_output_ep) {
		DRM_ERROR("endpoint disp0_output_ep not fount\n");
		return -EINVAL;
	}
	ret = of_graph_parse_endpoint(disp0_output_ep, &endpoint);
	if (ret) {
		DRM_ERROR("endpoint parse fail\n");
		return -EINVAL;
	}
	DRM_INFO("[SUNXI-TCON] %s %d\n", __FUNCTION__, endpoint.id);
	of_node_put(tcon_in_disp0_ep);
	of_node_put(disp0_output_ep);

	return endpoint.id;
}

static int sunxi_tcon_bind(struct device *dev, struct device *master,
			   void *data)
{
	return sunxi_tcon_output_create(dev, (struct drm_device *)data);
}

static void sunxi_tcon_unbind(struct device *dev, struct device *master,
			      void *data)
{
	sunxi_tcon_output_destroy(dev, (struct drm_device *)data);
}

static const struct component_ops sunxi_tcon_component_ops = {
	.bind = sunxi_tcon_bind,
	.unbind = sunxi_tcon_unbind,
};

static int sunxi_tcon_probe(struct platform_device *pdev)
{
	int ret;
	struct sunxi_tcon *tcon;
	struct device *dev = &pdev->dev;

	DRM_INFO("[SUNXI-TCON] sunxi_tcon_probe start\n");

	tcon = devm_kzalloc(&pdev->dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon) {
		DRM_ERROR("can NOT allocate memory for tcon_drv\n");
		ret = -ENOMEM;
		goto out;
	}
	tcon->dev = dev;
	dev_set_drvdata(dev, tcon);
	tcon->id = sunxi_tcon_of_get_id(dev);
	tcon->tcon_data = of_device_get_match_data(dev);
	if (!tcon->tcon_data) {
		DRM_ERROR("sunxi_tcon fail to get match data\n");
		ret = -ENODEV;
		goto out;
	}
	ret = sunxi_tcon_parse_dts(dev);
	if (ret) {
		DRM_ERROR("sunxi_tcon_parse_dts failed\n");
		goto out;
	}
	ret = sunxi_tcon_init_al(dev);
	if (ret) {
		DRM_ERROR("sunxi_tcon_init_al failed\n");
		goto out;
	}
	ret = sunxi_tcon_get_tcon_top(tcon);
	if (ret)
		goto out;

	tcon->lvds_combo_phy0 = devm_phy_optional_get(dev, "lvds_combo_phy0");
	if (IS_ERR(tcon->lvds_combo_phy0)) {
		DRM_ERROR("lvds_combo_phy0 get fail failed\n");
		goto out;
	}

	tcon->lvds_combo_phy1 = devm_phy_optional_get(dev, "lvds_combo_phy1");
	if (IS_ERR(tcon->lvds_combo_phy1)) {
		DRM_ERROR("lvds_combo_phy1 get fail failed\n");
		goto out;
	}

	ret = component_add(&pdev->dev, &sunxi_tcon_component_ops);
	if (ret < 0) {
		DRM_ERROR("failed to add component tcon\n");
	}
out:
	DRM_INFO("[SUNXI-DE] sunxi_tcon_probe ret = %d\n", ret);
	return ret;
}

//TODO
static int sunxi_tcon_remove(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver sunxi_tcon_platform_driver = {
	.probe = sunxi_tcon_probe,
	.remove = sunxi_tcon_remove,
	.driver = {
		   .name = "tcon",
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_tcon_match,
	},
};
