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
#include <linux/component.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
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
#define PHY_SINGLE_ENABLE 1
#define PHY_DUAL_ENABLE 2

#define DSI_DISPLL_CLK
struct dsi_data {
	int id;
};
struct sunxi_drm_dsi {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct device *tcon_dev;
	struct drm_device *drm_dev;
	struct mipi_dsi_host host;
	struct drm_display_mode mode;
	struct disp_dsi_para dsi_para;
	unsigned int tcon_id;
	struct drm_panel *panel;
	bool bound;
	bool allow_sw_enable;
	bool sw_enable;
	bool pending_enable_vblank;
	struct device *dev;
	struct sunxi_drm_dsi *master;
	struct sunxi_drm_dsi *slave;
	struct phy *phy;
	union phy_configure_opts phy_opts;
	struct sunxi_dsi_lcd dsi_lcd;
	uintptr_t reg_base;
	const struct dsi_data *dsi_data;

	u32 dsi_id;
	u32 enable;
	irq_handler_t irq_handler;
	void *irq_data;
	u32 irq_no;
	dev_t devid;

	struct clk *displl_ls;
	struct clk *displl_hs;
	struct clk *clk_bus;
	struct clk *clk;
	struct clk *combphy;
	struct reset_control *rst_bus;
	unsigned long mode_flags;
};
static const struct dsi_data dsi0_data = {
	.id = 0,
};

static const struct dsi_data dsi1_data = {
	.id = 1,
};

static const struct of_device_id sunxi_drm_dsi_match[] = {
	{ .compatible = "allwinner,dsi0", .data = &dsi0_data },
	{ .compatible = "allwinner,dsi1", .data = &dsi1_data },
	{},
};

static void sunxi_dsi_enable_vblank(bool enable, void *data);

static inline struct sunxi_drm_dsi *
	drm_encoder_to_sunxi_drm_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sunxi_drm_dsi, encoder);
}
/*
static struct sunxi_drm_dsi *dev_to_sunxi_drm_dsi(struct device *dev)
{
	if (PTR_ERR_OR_ZERO(dev) || !of_match_node(sunxi_drm_dsi_match, dev->of_node)) {
		DRM_ERROR("use not dsi device for a dsi api!\n");
		return ERR_PTR(-EINVAL);
	}
	return dev_get_drvdata(dev);
}
*/
static struct device *drm_dsi_of_get_tcon(struct device *dsi_dev)
{
	struct device_node *node = dsi_dev->of_node;
	struct device_node *tcon_lcd_node;
	struct device_node *dsi_in_tcon;
	struct platform_device *pdev = NULL;
	struct device *tcon_lcd_dev = NULL;;

	dsi_in_tcon = of_graph_get_endpoint_by_regs(node, 0, 0);
	if (!dsi_in_tcon) {
		DRM_ERROR("endpoint dsi_in_tcon not fount\n");
		return NULL;
	}

	tcon_lcd_node = of_graph_get_remote_port_parent(dsi_in_tcon);
	if (!tcon_lcd_node) {
		DRM_ERROR("node tcon_lcd not fount\n");
		tcon_lcd_dev = NULL;
		goto DSI_PUT;
	}

	pdev = of_find_device_by_node(tcon_lcd_node);
	if (!pdev) {
		DRM_ERROR("tcon_lcd platform device not fount\n");
		tcon_lcd_dev = NULL;
		goto TCON_DSI_PUT;
	}

	tcon_lcd_dev = &pdev->dev;
	platform_device_put(pdev);

TCON_DSI_PUT:
	of_node_put(tcon_lcd_node);
DSI_PUT:
	of_node_put(dsi_in_tcon);

	return tcon_lcd_dev;
}

static inline struct sunxi_drm_dsi *host_to_sunxi_drm_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct sunxi_drm_dsi, host);
}
static inline struct sunxi_drm_dsi *encoder_to_sunxi_drm_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sunxi_drm_dsi, encoder);
}

static inline struct sunxi_drm_dsi *connector_to_sunxi_drm_dsi(struct drm_connector *connector)
{
	return container_of(connector, struct sunxi_drm_dsi, connector);
}

static int sunxi_drm_dsi_find_slave(struct sunxi_drm_dsi *dsi)
{
	struct device_node *node = NULL;
	struct platform_device *pdev;

	node = of_parse_phandle(dsi->dev->of_node, "dual-channel", 0);
	if (node) {
		pdev = of_find_device_by_node(node);
		if (!pdev) {
			of_node_put(node);
			return -EPROBE_DEFER;
		}

		dsi->slave = platform_get_drvdata(pdev);
		if (!dsi->slave) {
			platform_device_put(pdev);
			return -EPROBE_DEFER;
		}

		of_node_put(node);
		platform_device_put(pdev);

	}

	return 0;
}

static int sunxi_dsi_clk_config_enable(struct sunxi_drm_dsi *dsi,
					const struct disp_dsi_para *para)
{
	int ret = 0;
	unsigned long dsi_rate = 0;
	unsigned long combphy_rate, combphy_rate_set;
	unsigned long dsi_rate_set = 150000000;

	clk_set_rate(dsi->clk, dsi_rate_set);
	dsi_rate = clk_get_rate(dsi->clk);
	if (dsi_rate_set != dsi_rate)
		DRM_WARN("Dsi rate to be set:%lu, real clk rate:%lu\n",
			dsi_rate, dsi_rate_set);

	if (dsi->combphy) {
		if (dsi->slave || dsi->master)
			combphy_rate_set = dsi->dsi_para.timings.pixel_clk * 3;
		else
			combphy_rate_set = dsi->dsi_para.timings.pixel_clk * 6;
		clk_set_rate(dsi->combphy, combphy_rate_set);
		combphy_rate = clk_get_rate(dsi->combphy);
		DRM_INFO("combphy rate to be set:%lu, real clk rate:%lu\n",
				combphy_rate, combphy_rate_set);
		ret = clk_prepare_enable(dsi->combphy);
		if (ret) {
			DRM_ERROR("clk_prepare_enable for combphy_clk failed\n");
			return ret;
		}
	}
	ret = reset_control_deassert(dsi->rst_bus);
	if (ret) {
		DRM_ERROR(
			"reset_control_deassert for dsi_rst_clk failed\n");
		return ret;
	}
	ret = clk_prepare_enable(dsi->clk_bus);
	if (ret) {
		DRM_ERROR("clk_prepare_enable for dsi_gating_clk failed\n");
		return ret;
	}
	ret = clk_prepare_enable(dsi->clk);
	if (ret) {
		DRM_ERROR("clk_prepare_enable for dsi_clk failed\n");
		return ret;
	}

	return ret;
}

static int sunxi_dsi_clk_config_disable(struct sunxi_drm_dsi *dsi)
{
	int ret = 0;

	clk_disable_unprepare(dsi->clk_bus);
	clk_disable_unprepare(dsi->clk);
	if (dsi->combphy)
		clk_disable_unprepare(dsi->combphy);
	if (dsi->displl_ls)
		clk_disable_unprepare(dsi->displl_ls);

	ret = reset_control_assert(dsi->rst_bus);
	if (ret) {
		DRM_ERROR("reset_control_assert for rst_bus_mipi_dsi failed\n");
		return ret;
	}
	return ret;
}

static irqreturn_t sunxi_dsi_irq_event_proc(int irq, void *parg)
{
	struct sunxi_drm_dsi *dsi = parg;

	dsi_irq_query(&dsi->dsi_lcd, DSI_IRQ_VIDEO_VBLK);

	return dsi->irq_handler(irq, dsi->irq_data);
}

static int sunxi_lcd_pin_set_state(struct device *dev, char *name)
{
	int ret;
	struct pinctrl *pctl;
	struct pinctrl_state *state;

	DRM_INFO("[DSI] %s start\n", __FUNCTION__);
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

static int sunxi_dsi_enable_output(struct sunxi_drm_dsi *dsi)
{
	struct disp_dsi_para *dsi_para = &dsi->dsi_para;

	sunxi_tcon_dsi_enable_output(dsi->tcon_dev);
	dsi_open(&dsi->dsi_lcd, dsi_para);
	if (dsi->slave)
		dsi_open(&dsi->slave->dsi_lcd, dsi_para);

	return 0;
}

static int sunxi_dsi_disable_output(struct sunxi_drm_dsi *dsi)
{
	dsi_close(&dsi->dsi_lcd);
	if (dsi->slave)
		dsi_close(&dsi->slave->dsi_lcd);
	sunxi_tcon_dsi_disable_output(dsi->tcon_dev);

	return 0;
}

void sunxi_drm_dsi_encoder_atomic_enable(struct drm_encoder *encoder,
					struct drm_atomic_state *state)
{
	int ret, bpp;
	struct drm_crtc *crtc = encoder->crtc;
//	struct sunxi_drm_private *drv_private = to_sunxi_drm_private(encoder->dev);
	int de_hw_id = sunxi_drm_crtc_get_hw_id(crtc);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct sunxi_drm_dsi *dsi = encoder_to_sunxi_drm_dsi(encoder);
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct disp_output_config disp_cfg;
	unsigned long hs_clk_rate, ls_clk_rate;

	DRM_INFO("[DSI] %s start\n", __FUNCTION__);
	dsi->enable = true;
	bpp = mipi_dsi_pixel_format_to_bpp(dsi->dsi_para.format);
	drm_mode_to_sunxi_video_timings(&dsi->mode, &dsi->dsi_para.timings);

	if (dsi->phy) {
		phy_mipi_dphy_get_default_config(dsi->dsi_para.timings.pixel_clk,
					bpp, dsi->dsi_para.lanes, &dsi->phy_opts.mipi_dphy);
	}

	dsi->allow_sw_enable = false;
	memset(&disp_cfg, 0, sizeof(struct disp_output_config));
	memcpy(&disp_cfg.dsi_para, &dsi->dsi_para,
		sizeof(dsi->dsi_para));
	disp_cfg.type = INTERFACE_DSI;
	disp_cfg.de_id = de_hw_id;
	disp_cfg.irq_handler = sunxi_crtc_event_proc;
	disp_cfg.irq_data = scrtc_state->base.crtc;
	disp_cfg.sw_enable = dsi->sw_enable;
#ifdef DSI_DISPLL_CLK
	disp_cfg.displl_clk = true;
	disp_cfg.tcon_lcd_div = 1;
#else
	disp_cfg.displl_clk = false;
	if (dsi->slave)
		disp_cfg.tcon_lcd_div = 3;
	else
		disp_cfg.tcon_lcd_div = 6;
#endif
	if (dsi->slave || (dsi->dsi_para.mode_flags & MIPI_DSI_SLAVE_MODE))
		disp_cfg.slave_dsi = true;

	sunxi_tcon_mode_init(dsi->tcon_dev, &disp_cfg);

	/* dual dsi use tcon's irq, single dsi use its own irq */
	if (!disp_cfg.slave_dsi) {
		dsi->irq_handler = sunxi_crtc_event_proc;
		dsi->irq_data = scrtc_state->base.crtc;
		ret = devm_request_irq(dsi->dev, dsi->irq_no, sunxi_dsi_irq_event_proc,
					0, dev_name(dsi->dev), dsi);
		if (ret) {
			DRM_ERROR("Couldn't request the IRQ for dsi\n");
		}
	}

	ret = sunxi_dsi_clk_config_enable(dsi, &dsi->dsi_para);
	if (ret) {
		DRM_ERROR("dsi clk enable failed\n");
		return;
	}
	ls_clk_rate = dsi->dsi_para.timings.pixel_clk;
	hs_clk_rate = dsi->dsi_para.timings.pixel_clk * bpp / dsi->dsi_para.lanes;
	if (dsi->slave) {
		hs_clk_rate = dsi->dsi_para.timings.pixel_clk * bpp / (dsi->dsi_para.lanes * 2);
		ret = sunxi_dsi_clk_config_enable(dsi->slave, &dsi->dsi_para);
		if (ret) {
			DRM_ERROR("slave clk enable failed\n");
			return;
		}
	}

	ret = dsi_cfg(&dsi->dsi_lcd, &dsi->dsi_para);
	if (ret) {
		DRM_ERROR("dsi_cfg failed\n");
		return;
	}
	if (dsi->slave) {
		ret = dsi_cfg(&dsi->slave->dsi_lcd, &dsi->dsi_para);
		if (ret) {
			DRM_ERROR("dsi_cfg slave failed\n");
			return;
		}
	}
	sunxi_lcd_pin_set_state(dsi->dev, "active");
	if (dsi->slave) {
		sunxi_lcd_pin_set_state(dsi->slave->dev, "active");
	}

	if (dsi->sw_enable) {
		if (dsi->phy)
			phy_power_on(dsi->phy);
		if (dsi->slave) {
			if (dsi->slave->phy)
				phy_power_on(dsi->slave->phy);
		}

		/*
		 * make permission: each panel's prepare just do power&
		 * gpio operation, to ensure sw enable! We need not to
		 * configure backlight becase it is control by
		 * drm_panel_enable/disable automatically!
		 */
		drm_panel_prepare(dsi->panel);
	} else {
		if (dsi->phy) {
			phy_power_on(dsi->phy);
			phy_set_mode_ext(dsi->phy, PHY_MODE_MIPI_DPHY, PHY_SINGLE_ENABLE);
			phy_configure(dsi->phy, &dsi->phy_opts);
			if (dsi->displl_hs)
				clk_set_rate(dsi->displl_hs, hs_clk_rate);
			if (dsi->displl_ls)
				clk_set_rate(dsi->displl_ls, ls_clk_rate);
			if (dsi->displl_ls)
				clk_prepare_enable(dsi->displl_ls);
		}
		if (dsi->slave) {
			if (dsi->slave->phy) {
				phy_power_on(dsi->slave->phy);
				phy_set_mode_ext(dsi->phy, PHY_MODE_MIPI_DPHY, PHY_DUAL_ENABLE);
				phy_set_mode_ext(dsi->slave->phy, PHY_MODE_MIPI_DPHY, PHY_DUAL_ENABLE);
				phy_configure(dsi->slave->phy, &dsi->phy_opts);
			}
		}

		drm_panel_prepare(dsi->panel);
		dsi_clk_enable(&dsi->dsi_lcd, &dsi->dsi_para, 1);
		if (dsi->slave)
			dsi_clk_enable(&dsi->slave->dsi_lcd, &dsi->dsi_para, 1);
		drm_panel_enable(dsi->panel);

		ret = sunxi_dsi_enable_output(dsi);
		if (ret < 0)
			DRM_DEV_INFO(dsi->dev, "failed to enable dsi ouput\n");
	}
	if (dsi->pending_enable_vblank) {
		sunxi_dsi_enable_vblank(1, dsi);
		dsi->pending_enable_vblank = false;
	}
	DRM_INFO("[DSI] %s finish\n", __FUNCTION__);
}

void sunxi_drm_dsi_encoder_atomic_disable(struct drm_encoder *encoder,
					struct drm_atomic_state *state)
{
	struct sunxi_drm_dsi *dsi = drm_encoder_to_sunxi_drm_dsi(encoder);

	drm_panel_disable(dsi->panel);
	drm_panel_unprepare(dsi->panel);

	if (dsi->phy) {
		phy_power_off(dsi->phy);
	}
	if (dsi->slave) {
		if (dsi->slave->phy)
			phy_power_off(dsi->phy);
	}

	sunxi_dsi_clk_config_disable(dsi);
	if (dsi->slave)
		sunxi_dsi_clk_config_disable(dsi->slave);

	sunxi_lcd_pin_set_state(dsi->dev, "sleep");
	if (dsi->slave)
		sunxi_lcd_pin_set_state(dsi->slave->dev, "sleep");
	sunxi_dsi_disable_output(dsi);
	sunxi_tcon_mode_exit(dsi->tcon_dev);

	if (!(dsi->slave || (dsi->dsi_para.mode_flags & MIPI_DSI_SLAVE_MODE)))
		devm_free_irq(dsi->dev, dsi->irq_no, dsi);

	dsi->enable = false;
	DRM_DEBUG_DRIVER("%s finish\n", __FUNCTION__);
}

static bool sunxi_dsi_fifo_check(void *data)
{
	struct sunxi_drm_dsi *dsi = (struct sunxi_drm_dsi *)data;
	int status;
	status = dsi_get_status(&dsi->dsi_lcd);

	return status ? true : false;
}

static bool sunxi_dsi_is_sync_time_enough(void *data)
{
	struct sunxi_drm_dsi *dsi = (struct sunxi_drm_dsi *)data;
	return sunxi_tcon_is_sync_time_enough(dsi->tcon_dev);
}

static void sunxi_dsi_enable_vblank(bool enable, void *data)
{
	struct sunxi_drm_dsi *dsi = (struct sunxi_drm_dsi *)data;
	if (!dsi->enable) {
		dsi->pending_enable_vblank = enable;
		return;
	}

	if (dsi->slave || (dsi->dsi_para.mode_flags & MIPI_DSI_SLAVE_MODE))
		sunxi_tcon_enable_vblank(dsi->tcon_dev, enable);
	else
		dsi_enable_vblank(&dsi->dsi_lcd, enable);
}

int sunxi_drm_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_dsi *dsi = encoder_to_sunxi_drm_dsi(encoder);

	/* FIXME:TODO: color_fmt/clolor_depth update by actual configuration */
//	scrtc_state->color_fmt = DISP_CSC_TYPE_RGB;
//	scrtc_state->color_depth = DISP_DATA_8BITS;
	scrtc_state->tcon_id = dsi->tcon_id;
	scrtc_state->enable_vblank = sunxi_dsi_enable_vblank;
	scrtc_state->check_status = sunxi_dsi_fifo_check;
	scrtc_state->is_sync_time_enough = sunxi_dsi_is_sync_time_enough;
	scrtc_state->output_dev_data = dsi;
	DRM_DEBUG_DRIVER("%s finish\n", __FUNCTION__);
	return 0;
}

static void sunxi_drm_dsi_encoder_mode_set(struct drm_encoder *encoder,
					struct drm_display_mode *mode,
					struct drm_display_mode *adj_mode)
{
	struct sunxi_drm_dsi *dsi = encoder_to_sunxi_drm_dsi(encoder);

	drm_mode_copy(&dsi->mode, adj_mode);

}
/*
static int sunxi_drm_dsi_encoder_loader_protect(struct drm_encoder *encoder,
		bool on)
{
	struct sunxi_drm_dsi *dsi = encoder_to_dsi(encoder);

	if (dsi->panel)
		drm_panel_loader_protect(dsi->panel, on);

	return sunxi_drm_dsi_loader_protect(dsi, on);
}
*/
static const struct drm_encoder_helper_funcs sunxi_dsi_encoder_helper_funcs = {
	.atomic_enable = sunxi_drm_dsi_encoder_atomic_enable,
	.atomic_disable = sunxi_drm_dsi_encoder_atomic_disable,
	.atomic_check = sunxi_drm_dsi_encoder_atomic_check,
	.mode_set = sunxi_drm_dsi_encoder_mode_set,
//	.loader_protect = sunxi_drm_dsi_encoder_loader_protect,
};

static int drm_dsi_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state,
				struct drm_property *property,
				uint64_t val)
{
	return 0;

}
static int drm_dsi_connector_get_property(struct drm_connector *connector,
		const struct drm_connector_state *state,
				struct drm_property *property,
				uint64_t *val)
{
	return 0;

}

static void drm_dsi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs sunxi_dsi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_dsi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = drm_dsi_connector_set_property,
	.atomic_get_property = drm_dsi_connector_get_property,
};

static int sunxi_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct sunxi_drm_dsi *dsi = connector_to_sunxi_drm_dsi(connector);

	return drm_panel_get_modes(dsi->panel, connector);
}

static const struct drm_connector_helper_funcs
	sunxi_dsi_connector_helper_funcs = {
	.get_modes = sunxi_dsi_connector_get_modes,
};

static int sunxi_drm_dsi_bind(struct device *dev, struct device *master, void *data)
{
	const int lcd_type = 1;
	struct sunxi_drm_dsi *dsi = dev_get_drvdata(dev);
	struct drm_device *drm = (struct drm_device *)data;
	struct device *tcon_lcd_dev = NULL;
	struct sunxi_drm_private *drv_private = to_sunxi_drm_private(drm);
	int ret, tcon_id;

	DRM_INFO("[DSI]%s start\n", __FUNCTION__);
	dsi->dev = dev;
	ret = sunxi_drm_dsi_find_slave(dsi);
	if (ret)
		return ret;
	if (dsi->slave) {
		dsi->slave->master = dsi;
		dsi->dsi_para.dual_dsi = 1;
		DRM_INFO("[DSI]dsi%d slave is ok\n", dsi->dsi_id);
	}
	if (dsi->master)
		return 0;

	tcon_lcd_dev = drm_dsi_of_get_tcon(dsi->dev);
	if (tcon_lcd_dev == NULL) {
		DRM_ERROR("tcon_lcd for dsi not found!\n");
		ret = -1;
		goto ERR_GROUP;
	}
	tcon_id = sunxi_tcon_of_get_id(tcon_lcd_dev);

	dsi->tcon_dev = tcon_lcd_dev;
	dsi->tcon_id = tcon_id;
	dsi->drm_dev = drm;

	if (!dsi->panel) {
		DRM_ERROR("[DSI]Failed to find panel\n");
		return -EPROBE_DEFER;
	}

	drm_encoder_helper_add(&dsi->encoder, &sunxi_dsi_encoder_helper_funcs);
	ret = drm_simple_encoder_init(drm, &dsi->encoder, DRM_MODE_ENCODER_DSI);
	if (ret) {
		DRM_ERROR("Couldn't initialise the encoder for tcon %d\n", tcon_id);
		goto ERR_GROUP;
	}

	dsi->encoder.possible_crtcs =
			drm_of_find_possible_crtcs(drm, tcon_lcd_dev->of_node);

	drm_connector_helper_add(&dsi->connector,
			&sunxi_dsi_connector_helper_funcs);

//	dsi->connector.polled = DRM_CONNECTOR_POLL_HPD;
	ret = drm_connector_init(drm, &dsi->connector,
			&sunxi_dsi_connector_funcs,
			DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		drm_encoder_cleanup(&dsi->encoder);
		DRM_ERROR("Couldn't initialise the connector for tcon %d\n", tcon_id);
		goto ERR_GROUP;
	}
//	drm_dsi_connector_init_property(drm, &dsi->connector);

	drm_connector_attach_encoder(&dsi->connector, &dsi->encoder);
//	tcon_dev->cfg.private_data = dsi;

	dsi->allow_sw_enable = true;
	dsi->sw_enable = dsi->allow_sw_enable && drv_private->sw_enable &&
			    (drv_private->boot.device_type == lcd_type);
	dsi->bound = true;

	return 0;
ERR_GROUP:
	return ret;
}

static void sunxi_drm_dsi_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct sunxi_drm_dsi *dsi = dev_get_drvdata(dev);

	drm_connector_cleanup(&dsi->connector);
	drm_encoder_cleanup(&dsi->encoder);

	dsi->bound = false;
	if (dsi->slave)
		dsi->slave = NULL;
}

static const struct component_ops sunxi_drm_dsi_component_ops = {
	.bind = sunxi_drm_dsi_bind,
	.unbind = sunxi_drm_dsi_unbind,
};

/* panel mipi_dsi_attach(dsi) */
static int sunxi_drm_dsi_host_attach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *device)
{
	struct sunxi_drm_dsi *dsi = host_to_sunxi_drm_dsi(host);
	struct drm_panel *panel = of_drm_find_panel(device->dev.of_node);

	DRM_INFO("[DSI]%s start\n", __FUNCTION__);

	dsi->panel = panel;
	dsi->dsi_para.dsi_div = 6;
	dsi->dsi_para.lanes = device->lanes;
	dsi->dsi_para.channel = device->channel;
	dsi->dsi_para.format = device->format;
	dsi->dsi_para.mode_flags = device->mode_flags;
	dsi->dsi_para.hs_rate = device->hs_rate;
	dsi->dsi_para.lp_rate = device->lp_rate;

	DRM_INFO("[DSI]%s finish\n", __FUNCTION__);
	return 0;
}

static int sunxi_drm_dsi_host_detach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *device)
{
	struct sunxi_drm_dsi *dsi = host_to_sunxi_drm_dsi(host);
	dsi->panel = NULL;
	dsi->dsi_para.lanes = 0;
	dsi->dsi_para.channel = 0;
	dsi->dsi_para.format = 0;
	dsi->dsi_para.mode_flags = 0;
	dsi->dsi_para.hs_rate = 0;
	dsi->dsi_para.lp_rate = 0;
	memset(&dsi->dsi_para.timings, 0, sizeof(struct disp_video_timings));

	return 0;
}

static s32 sunxi_dsi_read_para(struct sunxi_drm_dsi *dsi, const struct mipi_dsi_msg *msg)
{
	s32 ret;

	ret = dsi_dcs_rd(&dsi->dsi_lcd, msg->rx_buf, msg->rx_len);

	return ret;
}

static s32 sunxi_dsi_write_para(struct sunxi_drm_dsi *dsi, struct mipi_dsi_packet *packet)
{
	u32 ecc, crc, para_num;
	u8 *para = NULL;

	para = kmalloc(packet->size + 2, GFP_ATOMIC);
	if (!para) {
	//	printk("%s %s %s :kmalloc fail\n", __FILE__, __func__, __LINE__);
		return -1;
	}
	ecc = packet->header[0] | (packet->header[1] << 8) | (packet->header[2] << 16);
	para[0] = packet->header[0];
	para[1] = packet->header[1];
	para[2] = packet->header[2];
	para[3] = dsi_ecc_pro(ecc);
	para_num = 4;

	if (packet->payload_length) {
		memcpy(para + 4, packet->payload, packet->payload_length);
		crc = dsi_crc_pro((u8 *)packet->payload, packet->payload_length + 1);
		para[packet->size] = (crc >> 0) & 0xff;
		para[packet->size + 1] = (crc >> 8) & 0xff;
		para_num = packet->size + 2;
	}
	dsi_dcs_wr(&dsi->dsi_lcd, para, para_num);

	kfree(para);
	para = NULL;

	return 0;
}

static ssize_t sunxi_drm_dsi_transfer(struct sunxi_drm_dsi *dsi,
				const struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int ret;

	/* create a packet to the DSI protocol */
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		DRM_ERROR("failed to create packet\n");
		return ret;
	}
	sunxi_dsi_write_para(dsi, &packet);
	if (msg->rx_len) {
		ret = sunxi_dsi_read_para(dsi, msg);
		if (ret < 0)
			return ret;
	}

	if (dsi->slave)
		sunxi_drm_dsi_transfer(dsi->slave, msg);

	return msg->tx_len;
}

/* panel mipi_dsi_generic_write() mipi_dsi_dcs_write_buffer() */
static ssize_t sunxi_drm_dsi_host_transfer(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct sunxi_drm_dsi *dsi = host_to_sunxi_drm_dsi(host);

	return sunxi_drm_dsi_transfer(dsi, msg);
}

static const struct mipi_dsi_host_ops sunxi_drm_dsi_host_ops = {
	.attach = sunxi_drm_dsi_host_attach,
	.detach = sunxi_drm_dsi_host_detach,
	.transfer = sunxi_drm_dsi_host_transfer,
};

static int sunxi_drm_dsi_probe(struct platform_device *pdev)
{
	struct sunxi_drm_dsi *dsi;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret;

	DRM_INFO("[DSI] sunxi_drm_dsi_probe start\n");
	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	/* tcon_mode, slave_tcon_num, port_num is init to 0 for now not support other mode*/

	if (!dsi)
		return -ENOMEM;
	dsi->dsi_data = of_device_get_match_data(dev);
	if (!dsi->dsi_data) {
		DRM_ERROR("sunxi_drm_dsi fail to get match data\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->reg_base = (uintptr_t)devm_ioremap_resource(dev, res);
	if (!dsi->reg_base) {
		DRM_ERROR("unable to map dsi registers\n");
		return -EINVAL;
	}

	dsi->irq_no = platform_get_irq(pdev, 0);
	if (!dsi->irq_no) {
		DRM_ERROR("get irq no of dsi failed\n");
		return -EINVAL;
	}

	dsi->clk = devm_clk_get(dev, "dsi_clk");
	if (IS_ERR(dsi->clk)) {
		DRM_ERROR("fail to get dsi_clk\n");
		return -EINVAL;
	}

	dsi->combphy = devm_clk_get_optional(dev, "combphy_clk");
	if (IS_ERR(dsi->combphy)) {
		DRM_ERROR("fail to get combphy_clk\n");
	}

	dsi->clk_bus = devm_clk_get(dev, "dsi_gating_clk");
	if (IS_ERR(dsi->clk_bus)) {
		DRM_ERROR("fail to get dsi_gating_clk\n");
		return -EINVAL;
	}

	dsi->displl_ls = devm_clk_get_optional(dev, "displl_ls");
	if (IS_ERR(dsi->displl_ls)) {
		DRM_ERROR("fail to get displl_ls\n");
	}
	dsi->displl_hs = devm_clk_get_optional(dev, "displl_hs");
	if (IS_ERR(dsi->displl_hs)) {
		DRM_ERROR("fail to get displl_hs\n");
	}

	dsi->rst_bus = devm_reset_control_get_shared(dev, "dsi_rst_clk");
	if (IS_ERR(dsi->rst_bus)) {
		DRM_ERROR("fail to get reset rst_bus_mipi_dsi\n");
		return -EINVAL;
	}

	dsi->dsi_id = dsi->dsi_data->id;
	dsi->phy = devm_phy_get(dev, "combophy");
	if (IS_ERR_OR_NULL(dsi->phy))
		DRM_INFO("dsi%d's combophy not setting, maybe not used!\n", dsi->dsi_id);

	dsi->host.ops = &sunxi_drm_dsi_host_ops;
	dsi->host.dev = dev;

	ret = mipi_dsi_host_register(&dsi->host);
	if (ret) {
		DRM_ERROR("Couldn't register MIPI-DSI host\n");
		return ret;
	}

	dsi->dev = dev;

	dsi->dsi_lcd.dsi_index = dsi->dsi_id;
	dsi_set_reg_base(&dsi->dsi_lcd, dsi->reg_base);
	dev_set_drvdata(dev, dsi);
	platform_set_drvdata(pdev, dsi);

	DRM_INFO("[DSI]%s ok\n", __FUNCTION__);

	return component_add(&pdev->dev, &sunxi_drm_dsi_component_ops);
}

static int sunxi_drm_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sunxi_drm_dsi_component_ops);
	return 0;
}

struct platform_driver sunxi_dsi_platform_driver = {
	.probe = sunxi_drm_dsi_probe,
	.remove = sunxi_drm_dsi_remove,
	.driver = {
		.name = "sunxi-dsi",
		.owner = THIS_MODULE,
		.of_match_table = sunxi_drm_dsi_match,
	},
};
