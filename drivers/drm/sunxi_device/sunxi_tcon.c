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
#include "sunxi_drm_hdmi.h"
#include "sunxi_drm_edp.h"

enum tcon_type {
	TCON_LCD = 0,
	TCON_TV = 1,
};

struct sunxi_tcon {
	int id;
	bool is_enabled;
	bool pending_enable_vblank;
	struct device *dev;
	struct device *tcon_top;
	struct phy *lvds_combo_phy0;
	struct phy *lvds_combo_phy1;
	struct tcon_device tcon_ctrl;
	struct sunxi_tcon_tv tcon_tv;
	struct sunxi_tcon_lcd tcon_lcd;

	uintptr_t reg_base;
	enum tcon_type type;

	/* clock resource */
	struct clk *ahb_clk; /* module clk */
	struct clk *mclk; /* module clk */
	struct clk *mclk_bus; /* module clk bus */
	struct reset_control *rst_bus_tcon;

	/* interrupt resource */
	unsigned int irq_no;
	unsigned int judge_line;
	void *output_data;

	void *irq_data;
	irq_handler_t irq_handler;
};

static int sunxi_tcon_request_irq(struct sunxi_tcon *hwtcon);
static int sunxi_tcon_free_irq(struct sunxi_tcon *hwtcon);

static const struct of_device_id sunxi_tcon_match[] = {
	{ .compatible = "allwinner,tcon-lcd", },
	{ .compatible = "allwinner,tcon-tv", },
	{},
};

static enum tcon_type get_dev_tcon_type(struct device_node *node)
{
	if (of_device_is_compatible(node, "allwinner,tcon-lcd"))
		return TCON_LCD;
	if (of_device_is_compatible(node, "allwinner,tcon-tv"))
		return TCON_TV;

	DRM_ERROR("invalid tcon match compatible\n");
	return TCON_LCD;
}


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

	return 0;
}

static int sunxi_tcon_output_destroy(struct device *dev, struct drm_device *drm)
{
	return 0;
}

static int sunxi_tcon_lcd_set_clk(struct sunxi_tcon *hwtcon, unsigned long pixel_clk)
{
	int ret = 0;
	unsigned long tcon_rate, tcon_rate_set;

	if (!hwtcon->tcon_ctrl.cfg.sw_enable && pixel_clk) {
		tcon_rate = pixel_clk;
		clk_set_rate(hwtcon->mclk, tcon_rate);
		tcon_rate_set = clk_get_rate(hwtcon->mclk);
		if (tcon_rate_set != tcon_rate)
			DRM_INFO("tcon rate to be set:%luHz, real clk rate:%luHz\n", tcon_rate,
				 tcon_rate_set);
	}
	if (hwtcon->rst_bus_tcon) {
		ret = reset_control_deassert(hwtcon->rst_bus_tcon);
		if (ret) {
			DRM_ERROR("reset_control_deassert for rst_bus_tcon failed!\n");
			return -1;
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

	return 0;
}

static void sunxi_tcon_calc_judge_line(struct sunxi_tcon *hwtcon,
		const struct disp_video_timings *p_timgs)
{
	u64 usec_per_line, start_delay = 0;
	unsigned int usec_start_delay, usec_judge_point;
	unsigned int tcon_type = hwtcon->type;

	usec_per_line = p_timgs->hor_total_time * 1000000ull;
	if (p_timgs->pixel_clk)
		do_div(usec_per_line, p_timgs->pixel_clk);

	if (tcon_type == TCON_LCD) {
		start_delay = tcon_lcd_get_start_delay(&hwtcon->tcon_lcd);
	} else {
		start_delay = tcon_tv_get_start_delay(&hwtcon->tcon_tv);
	}
	usec_start_delay = start_delay * usec_per_line;

	if (usec_start_delay <= 200)
		usec_judge_point = usec_start_delay * 3 / 7;
	else if (usec_start_delay <= 400)
		usec_judge_point = usec_start_delay / 2;
	else
		usec_judge_point = 200;

	if (usec_per_line)
		hwtcon->judge_line = usec_judge_point / usec_per_line;
	if (hwtcon->judge_line == 0)
		hwtcon->judge_line = 1;
}

static int sunxi_tcon_lcd_prepare(struct sunxi_tcon *hwtcon, unsigned long pixel_clk)
{
	int ret = 0;
	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_enable(hwtcon->tcon_top);
	ret = sunxi_tcon_lcd_set_clk(hwtcon, pixel_clk);
	if (ret < 0) {
		DRM_ERROR("sunxi_tcon_lcd_set_clk failed\n");
		return -1;
	}

	return 0;
}

static void sunxi_tcon_lcd_unprepare(struct sunxi_tcon *hwtcon)
{
	clk_disable_unprepare(hwtcon->mclk_bus);
	clk_disable_unprepare(hwtcon->mclk);
	reset_control_assert(hwtcon->rst_bus_tcon);
	if (hwtcon->tcon_top)
		sunxi_tcon_top_clk_disable(hwtcon->tcon_top);
}

int sunxi_tcon_dsi_enable_output(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	struct disp_dsi_para *dsi_para = &hwtcon->tcon_ctrl.cfg.dsi_para;

	tcon_dsi_open(&hwtcon->tcon_lcd, dsi_para);

	return 0;
}

int sunxi_tcon_dsi_disable_output(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	tcon_dsi_close(&hwtcon->tcon_lcd);

	return 0;
}
static int sunxi_tcon_dsi_mode_init(struct device *tcon_dev)
{
	int ret = 0;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	struct disp_dsi_para *dsi_para = &hwtcon->tcon_ctrl.cfg.dsi_para;
	bool sw_enable = hwtcon->tcon_ctrl.cfg.sw_enable;
	bool displl_clk = hwtcon->tcon_ctrl.cfg.displl_clk;
	unsigned int tcon_div = hwtcon->tcon_ctrl.cfg.tcon_lcd_div;

	DRM_INFO("[DSI] %s start\n", __FUNCTION__);
	hwtcon->is_enabled = true;
	if (displl_clk)
		ret = sunxi_tcon_lcd_prepare(hwtcon, 0);
	else
		ret = sunxi_tcon_lcd_prepare(hwtcon, dsi_para->timings.pixel_clk * tcon_div);

	if (ret < 0) {
		DRM_ERROR("sunxi_tcon_lcd_prepare failed\n");
		return ret;
	}
	if (!sw_enable) {
		tcon_lcd_init(&hwtcon->tcon_lcd);
		tcon_lcd_set_dclk_div(&hwtcon->tcon_lcd, tcon_div);
		if (displl_clk)
			tcon_lcd_dsi_clk_source(hwtcon->id, dsi_para->dual_dsi);
		if (tcon_dsi_cfg(&hwtcon->tcon_lcd, dsi_para) != 0) { /* the final param is rcq related */
			DRM_ERROR("lcd cfg fail!\n");
			return -1;
		}
		tcon_lcd_src_select(&hwtcon->tcon_lcd, LCD_SRC_DE);
//		tcon_dsi_open(&hwtcon->tcon_lcd, dsi_para);
	}

	sunxi_tcon_calc_judge_line(hwtcon, &dsi_para->timings);
	if (hwtcon->pending_enable_vblank) {
		sunxi_tcon_enable_vblank(tcon_dev, 1);
		hwtcon->pending_enable_vblank = false;
	}
	if (hwtcon->tcon_ctrl.cfg.slave_dsi)
		sunxi_tcon_request_irq(hwtcon);

	return 0;
}

static int sunxi_tcon_dsi_mode_exit(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	if (hwtcon->tcon_ctrl.cfg.slave_dsi)
		sunxi_tcon_free_irq(hwtcon);

	hwtcon->is_enabled = false;
	hwtcon->judge_line = 0;

//	tcon_dsi_close(&hwtcon->tcon_lcd);
	tcon_lcd_exit(&hwtcon->tcon_lcd);
	sunxi_tcon_lcd_unprepare(hwtcon);

	return 0;
}

int sunxi_lvds_enable_output(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	struct disp_lvds_para *lvds_para = &hwtcon->tcon_ctrl.cfg.lvds_para;

	tcon_lvds_open(&hwtcon->tcon_lcd);
	lvds_open(&hwtcon->tcon_lcd, lvds_para);
	return 0;
}

int sunxi_lvds_disable_output(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	tcon_lvds_close(&hwtcon->tcon_lcd);
	lvds_close(&hwtcon->tcon_lcd);

	return 0;
}

static int sunxi_tcon_lvds_mode_init(struct device *tcon_dev)
{
	int ret = 0;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	struct disp_lvds_para *lvds_para = &hwtcon->tcon_ctrl.cfg.lvds_para;
	bool sw_enable = hwtcon->tcon_ctrl.cfg.sw_enable;
	bool displl_clk = hwtcon->tcon_ctrl.cfg.displl_clk;
	unsigned int tcon_div = hwtcon->tcon_ctrl.cfg.tcon_lcd_div;

	DRM_INFO("[LVDS] %s start\n", __FUNCTION__);
	hwtcon->is_enabled = true;
	ret = sunxi_tcon_lcd_prepare(hwtcon, lvds_para->timings.pixel_clk * tcon_div);
	if (ret < 0) {
		DRM_ERROR("sunxi_tcon_lcd_prepare failed\n");
		return ret;
	}
	if (!sw_enable) {
		tcon_lcd_init(&hwtcon->tcon_lcd);
		tcon_lcd_set_dclk_div(&hwtcon->tcon_lcd, tcon_div);
		if (displl_clk)
			tcon_lcd_dsi_clk_source(hwtcon->id, 0);
		if (tcon_lvds_cfg(&hwtcon->tcon_lcd, lvds_para) != 0) { /* the final param is rcq related */
			DRM_ERROR("lcd cfg fail!\n");
			return -1;
		}
		tcon_lcd_src_select(&hwtcon->tcon_lcd, LCD_SRC_DE);
	}
	sunxi_tcon_calc_judge_line(hwtcon, &lvds_para->timings);
	if (hwtcon->pending_enable_vblank) {
		sunxi_tcon_enable_vblank(tcon_dev, 1);
		hwtcon->pending_enable_vblank = false;
	}
	sunxi_tcon_request_irq(hwtcon);

	return 0;
}

static int sunxi_tcon_lvds_mode_exit(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	hwtcon->is_enabled = false;
	hwtcon->judge_line = 0;

	sunxi_tcon_free_irq(hwtcon);
	tcon_lcd_exit(&hwtcon->tcon_lcd);
	sunxi_tcon_lcd_unprepare(hwtcon);

	return 0;
}

int sunxi_rgb_enable_output(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	tcon_rgb_open(&hwtcon->tcon_lcd);

	return 0;
}

int sunxi_rgb_disable_output(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	tcon_rgb_close(&hwtcon->tcon_lcd);

	return 0;
}

static int sunxi_tcon_rgb_mode_init(struct device *tcon_dev)
{
	int ret = 0;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	struct disp_rgb_para *rgb_para = &hwtcon->tcon_ctrl.cfg.rgb_para;
	bool sw_enable = hwtcon->tcon_ctrl.cfg.sw_enable;
	bool displl_clk = hwtcon->tcon_ctrl.cfg.displl_clk;
	unsigned int tcon_div = hwtcon->tcon_ctrl.cfg.tcon_lcd_div;

	DRM_INFO("[RGB] %s start\n", __FUNCTION__);
	hwtcon->is_enabled = true;
	ret = sunxi_tcon_lcd_prepare(hwtcon, rgb_para->timings.pixel_clk * tcon_div);
	if (ret < 0) {
		DRM_ERROR("sunxi_tcon_lcd_prepare failed\n");
		return ret;
	}
	if (!sw_enable) {
		tcon_lcd_init(&hwtcon->tcon_lcd);
		tcon_lcd_set_dclk_div(&hwtcon->tcon_lcd, tcon_div);
		if (displl_clk)
			tcon_lcd_dsi_clk_source(hwtcon->id, 0);
		if (tcon_rgb_cfg(&hwtcon->tcon_lcd, rgb_para) != 0) {
			DRM_ERROR("lcd-rgb cfg fail!\n");
			return -1;
		}
		tcon_lcd_src_select(&hwtcon->tcon_lcd, LCD_SRC_DE);
	}
	sunxi_tcon_calc_judge_line(hwtcon, &rgb_para->timings);
	if (hwtcon->pending_enable_vblank) {
		sunxi_tcon_enable_vblank(tcon_dev, 1);
		hwtcon->pending_enable_vblank = false;
	}
	sunxi_tcon_request_irq(hwtcon);

	return 0;
}

static int sunxi_tcon_rgb_mode_exit(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	hwtcon->is_enabled = false;
	hwtcon->judge_line = 0;

	sunxi_tcon_free_irq(hwtcon);

	tcon_lcd_exit(&hwtcon->tcon_lcd);
	sunxi_tcon_lcd_unprepare(hwtcon);

	return 0;
}

static int sunxi_tcon_device_query_irq(struct sunxi_tcon *hwtcon)
{
	int ret = 0;

	if (hwtcon->type == TCON_LCD)
		ret = tcon_lcd_irq_query(&hwtcon->tcon_lcd, LCD_IRQ_TCON0_VBLK);
	else
		ret = tcon_tv_irq_query(&hwtcon->tcon_tv, LCD_IRQ_TCON1_VBLK);
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

	tcon_tv_init(&hwtcon->tcon_tv);
	tcon_tv_set_timming(&hwtcon->tcon_tv, timings ? timings : p_timing);
	tcon_tv_src_select(&hwtcon->tcon_tv, LCD_SRC_DE, de_id);
	tcon_tv_black_src(&hwtcon->tcon_tv, 0x0, format);

#if IS_ENABLED(CONFIG_ARCH_SUN55IW3)
	tcon_tv_volume_force(&hwtcon->tcon_tv, 0x00040014);
#endif /* CONFIG_ARCH_SUN55IW3 */

	tcon1_hdmi_clk_enable(hwtcon->id, 1);

	tcon_tv_open(&hwtcon->tcon_tv);

	kfree(timings);

	return 0;
}

static int _sunxi_tcon_hdmi_disconfig(struct sunxi_tcon *hwtcon)
{

	tcon_tv_close(&hwtcon->tcon_tv);
	tcon_tv_exit(&hwtcon->tcon_tv);

	tcon1_hdmi_clk_enable(hwtcon->id, 0);

	return 0;
}

int sunxi_tcon_hdmi_mode_init(struct device *dev)
{
	int ret = 0;
	unsigned long pclk;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(dev);
	unsigned int de_id = hwtcon->tcon_ctrl.cfg.de_id;
	struct disp_video_timings *p_timing = &hwtcon->tcon_ctrl.cfg.timing;
	enum disp_csc_type format = hwtcon->tcon_ctrl.cfg.format;

	if (hwtcon->is_enabled) {
		DRM_WARN("tcon hdmi has been enable");
		return 0;
	}

	/* calculate actual pixel clock */
	hwtcon->is_enabled = true;
	pclk = p_timing->pixel_clk * (p_timing->pixel_repeat + 1);
	if (format == DISP_CSC_TYPE_YUV420)
		pclk /= 2;

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
	sunxi_tcon_calc_judge_line(hwtcon, p_timing);

	if (hwtcon->pending_enable_vblank) {
		sunxi_tcon_enable_vblank(dev, 1);
		hwtcon->pending_enable_vblank = false;
	}
	sunxi_tcon_request_irq(hwtcon);
	return 0;
}

int sunxi_tcon_hdmi_mode_exit(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
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
static int edp_tcon_clk_enable(struct sunxi_tcon *hwtcon)
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

static int edp_tcon_clk_disable(struct sunxi_tcon *hwtcon)
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

int sunxi_tcon_show_pattern(struct device *tcon_dev, int pattern)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	if (hwtcon->type == TCON_LCD)
		tcon_lcd_show_builtin_patten(&hwtcon->tcon_lcd, pattern);
	else
		tcon_tv_show_builtin_patten(&hwtcon->tcon_tv, pattern);

	return 0;
}

int sunxi_tcon_pattern_get(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	if (hwtcon->type == TCON_LCD)
		return tcon_lcd_src_get(&hwtcon->tcon_lcd);
	else
		return tcon_tv_src_get(&hwtcon->tcon_tv);
}

static int sunxi_tcon_edp_mode_init(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);
	struct disp_video_timings *timings = &hwtcon->tcon_ctrl.cfg.timing;
	unsigned int de_id = hwtcon->tcon_ctrl.cfg.de_id;
	bool sw_enable = hwtcon->tcon_ctrl.cfg.sw_enable;

	if (hwtcon->is_enabled) {
		DRM_WARN("tcon edp has been enable!\n");
		return 0;
	}

	hwtcon->is_enabled = true;
	edp_tcon_clk_enable(hwtcon);
	clk_set_rate(hwtcon->mclk, timings->pixel_clk);

	if (!sw_enable) {
		tcon_tv_init(&hwtcon->tcon_tv);
		tcon_tv_set_timming(&hwtcon->tcon_tv, timings);

		tcon_tv_src_select(&hwtcon->tcon_tv, LCD_SRC_DE, de_id);

		tcon1_edp_clk_enable(hwtcon->id, 1);
		tcon_tv_open(&hwtcon->tcon_tv);

	}
	sunxi_tcon_calc_judge_line(hwtcon, timings);
	if (hwtcon->pending_enable_vblank) {
		sunxi_tcon_enable_vblank(tcon_dev, 1);
		hwtcon->pending_enable_vblank = false;
	}
	sunxi_tcon_request_irq(hwtcon);

	return 0;
}

static int sunxi_tcon_edp_mode_exit(struct device *tcon_dev)
{
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	if (!hwtcon->is_enabled) {
		DRM_WARN("tcon edp has been disable!\n");
		return 0;
	}

	sunxi_tcon_free_irq(hwtcon);
	tcon_tv_close(&hwtcon->tcon_tv);
	tcon_tv_exit(&hwtcon->tcon_tv);
	tcon1_edp_clk_enable(hwtcon->id, 0);

	edp_tcon_clk_disable(hwtcon);

	hwtcon->is_enabled = false;
	hwtcon->judge_line = 0;

	return 0;
}

bool sunxi_tcon_is_sync_time_enough(struct device *tcon_dev)
{
	int cur_line = 0, judge_line = 0, start_delay = 0;
	unsigned int tcon_type;
	struct sunxi_tcon *hwtcon = dev_get_drvdata(tcon_dev);

	tcon_type = hwtcon->type;
	judge_line = hwtcon->judge_line;

	if (tcon_type == TCON_LCD) {
		cur_line = tcon_lcd_get_cur_line(&hwtcon->tcon_lcd);
		start_delay = tcon_lcd_get_start_delay(&hwtcon->tcon_lcd);
	} else {
		cur_line = tcon_tv_get_cur_line(&hwtcon->tcon_tv);
		start_delay = tcon_tv_get_start_delay(&hwtcon->tcon_tv);
	}

	//WARN_ON(!judge_line || !start_delay);
	return cur_line <= (start_delay - judge_line);
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

	hwtcon->mclk = devm_clk_get_optional(dev, "clk_tcon");
	if (IS_ERR(hwtcon->mclk)) {
		DRM_ERROR("fail to get clk for tcon \n");
		return -EINVAL;
	}

	hwtcon->ahb_clk = devm_clk_get_optional(dev, "clk_ahb_tcon");
	if (IS_ERR(hwtcon->ahb_clk)) {
		DRM_ERROR("fail to get ahb clk for tcon \n");
		return -EINVAL;
	}

	hwtcon->mclk_bus = devm_clk_get_optional(dev, "clk_bus_tcon");
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

	return 0;
}

bool sunxi_tcon_check_fifo_status(struct device *tcon_dev)
{
	int status = 0;
	struct sunxi_tcon *tcon = dev_get_drvdata(tcon_dev);

	if (tcon->type == TCON_TV) {
		status = tcon_tv_get_status(&tcon->tcon_tv);
	}
	if (tcon->type == TCON_LCD) {
		status = tcon_lcd_get_status(&tcon->tcon_lcd);
	}
	return status ? true : false;
}

void sunxi_tcon_enable_vblank(struct device *tcon_dev, bool enable)
{
	struct sunxi_tcon *tcon = dev_get_drvdata(tcon_dev);

	if (!tcon->is_enabled) {
		tcon->pending_enable_vblank = enable;
		return;
	}

	if (tcon->type == TCON_TV)
		tcon_tv_enable_vblank(&tcon->tcon_tv, enable);
	if (tcon->type == TCON_LCD)
		tcon_lcd_enable_vblank(&tcon->tcon_lcd, enable);
}

int sunxi_tcon_of_get_id(struct device *tcon_dev)
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

int sunxi_tcon_mode_init(struct device *tcon_dev, struct disp_output_config *disp_cfg)
{
	struct sunxi_tcon  *tcon = dev_get_drvdata(tcon_dev);
	struct tcon_device *ctrl = &tcon->tcon_ctrl;

	memcpy(&ctrl->cfg, disp_cfg, sizeof(struct disp_output_config));

	switch (ctrl->cfg.type) {
	case INTERFACE_EDP:
		return sunxi_tcon_edp_mode_init(tcon_dev);
	case INTERFACE_HDMI:
		return sunxi_tcon_hdmi_mode_init(tcon_dev);
	case INTERFACE_DSI:
		return sunxi_tcon_dsi_mode_init(tcon_dev);
	case INTERFACE_LVDS:
		return sunxi_tcon_lvds_mode_init(tcon_dev);
	case INTERFACE_RGB:
		return sunxi_tcon_rgb_mode_init(tcon_dev);
	default:
		break;
	}

	return 0;
}

int sunxi_tcon_mode_exit(struct device *tcon_dev)
{
	struct sunxi_tcon  *tcon = dev_get_drvdata(tcon_dev);
	struct tcon_device *ctrl = &tcon->tcon_ctrl;

	switch (ctrl->cfg.type) {
	case INTERFACE_EDP:
		return sunxi_tcon_edp_mode_exit(tcon_dev);
	case INTERFACE_HDMI:
		return sunxi_tcon_hdmi_mode_exit(tcon_dev);
	case INTERFACE_DSI:
		return sunxi_tcon_dsi_mode_exit(tcon_dev);
	case INTERFACE_LVDS:
		return sunxi_tcon_lvds_mode_exit(tcon_dev);
	case INTERFACE_RGB:
		return sunxi_tcon_rgb_mode_exit(tcon_dev);
	default:
		break;
	}

	return 0;
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

	DRM_INFO("[TCON] sunxi_tcon_probe start\n");

	tcon = devm_kzalloc(&pdev->dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon) {
		DRM_ERROR("can NOT allocate memory for tcon_drv\n");
		ret = -ENOMEM;
		goto out;
	}
	tcon->dev = dev;
	dev_set_drvdata(dev, tcon);
	tcon->id = sunxi_tcon_of_get_id(dev);
	tcon->type = get_dev_tcon_type(dev->of_node);
	ret = sunxi_tcon_parse_dts(dev);
	if (ret) {
		DRM_ERROR("sunxi_tcon_parse_dts failed\n");
		goto out;
	}

	if (tcon->type == TCON_TV) {
		tcon->tcon_tv.tcon_index = tcon->id;
		tcon_tv_set_reg_base(&tcon->tcon_tv, tcon->reg_base);
	}

	if (tcon->type == TCON_LCD) {
		tcon->tcon_lcd.tcon_index = tcon->id;
		tcon_lcd_set_reg_base(&tcon->tcon_lcd, tcon->reg_base);
	}

	ret = sunxi_tcon_get_tcon_top(tcon);
	if (ret)
		goto out;

	ret = component_add(&pdev->dev, &sunxi_tcon_component_ops);
	if (ret < 0) {
		DRM_ERROR("failed to add component tcon\n");
	}
out:
	DRM_INFO("[TCON] sunxi_tcon_probe ret = %d\n", ret);
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
