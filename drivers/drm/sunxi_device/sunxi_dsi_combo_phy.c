/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2023 Allwinner.
 *
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <drm/drm_print.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>

#include "sunxi_dsi.h"
struct dsi_cphy_data {
	unsigned int id;
};

/* TODO:
     1. implement sunxi_dsi_dphy as a phy without disp2 lowlevel, for now, only clk enable work.
     2. remove dsi_cphy_data.id
 */

struct sunxi_cphy {
	uintptr_t reg_base;
	const struct dsi_cphy_data *cphy_data;
	unsigned int id;

	struct phy *phy;
	struct clk *clk;
	struct clk *clk_bus;
	struct clk *clk_combphy;
	struct reset_control *rst_bus;
};

static int sunxi_dsi_cphy_clk_enable(struct sunxi_cphy *cphy)
{
	int ret = 0;

	ret = reset_control_deassert(cphy->rst_bus);
	if (ret) {
		DRM_ERROR(
			"reset_control_deassert for rst_bus_mipi_dsi failed\n");
		return ret;
	}

	ret = clk_prepare_enable(cphy->clk);
	if (ret) {
		DRM_ERROR("clk_prepare_enable for clk_mipi_dsi failed\n");
		return ret;
	}

	ret = clk_prepare_enable(cphy->clk_bus);
	if (ret) {
		DRM_ERROR("clk_prepare_enable for clk_bus_mipi_dsi failed\n");
		return ret;
	}

	ret = clk_prepare_enable(cphy->clk_combphy);
	if (ret) {
		DRM_ERROR(
			"clk_prepare_enable for clk_mipi_dsi_combphy failed\n");
		return ret;
	}

	return ret;
}

static int sunxi_dsi_cphy_clk_disable(struct sunxi_cphy *cphy)
{
	//FIXME
	int ret = 0;

	clk_disable_unprepare(cphy->clk_combphy);

	ret = reset_control_assert(cphy->rst_bus);
	if (ret) {
		DRM_ERROR("reset_control_assert for rst_bus_mipi_dsi failed\n");
		return ret;
	}
	return ret;
}

static int sunxi_dsi_cphy_power_on(struct phy *phy)
{
	return 0;
}

static int sunxi_dsi_cphy_power_off(struct phy *phy)
{
	return 0;
}

//still NOT use now
int sunxi_dsi_cphy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	//	struct sunxi_cphy *cphy = phy_get_drvdata(phy);
	//	if (mode == PHY_MODE_LVDS)
	//		lvds_combphy_open(cphy->cphy_data->id, NULL);
	return 0;
}

static int sunxi_dsi_cphy_init(struct phy *phy)
{
	struct sunxi_cphy *cphy = phy_get_drvdata(phy);
	return sunxi_dsi_cphy_clk_enable(cphy);
}

static int sunxi_dsi_cphy_exit(struct phy *phy)
{
	struct sunxi_cphy *cphy = phy_get_drvdata(phy);
	return sunxi_dsi_cphy_clk_disable(cphy);
}

static const struct phy_ops sunxi_cphy_ops = {
	.power_on = sunxi_dsi_cphy_power_on,
	.power_off = sunxi_dsi_cphy_power_off,
	.set_mode = sunxi_dsi_cphy_set_mode,
	.init = sunxi_dsi_cphy_init,
	.exit = sunxi_dsi_cphy_exit,
};

static int sunxi_cphy_bind(struct device *dev, struct device *master, void *data)
{
	return 0;
}

static void sunxi_cphy_unbind(struct device *dev, struct device *master,
			     void *data)
{
}

static const struct component_ops sunxi_cphy_component_ops = {
	.bind = sunxi_cphy_bind,
	.unbind = sunxi_cphy_unbind,
};

static int sunxi_dsi_combo_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct sunxi_cphy *cphy;
	struct resource *res;
	struct device *dev = &pdev->dev;

	cphy = devm_kzalloc(dev, sizeof(*cphy), GFP_KERNEL);
	if (!cphy)
		return -ENOMEM;
	cphy->cphy_data = of_device_get_match_data(dev);
	if (!cphy->cphy_data) {
		DRM_ERROR("sunxi dsi combo phy fail to get match data\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cphy->reg_base = (uintptr_t)devm_ioremap_resource(&pdev->dev, res);
	if (!cphy->reg_base) {
		DRM_ERROR("unable to map dsi combo phy registers\n");
		return -EINVAL;
	}

	cphy->clk = devm_clk_get(dev, "clk_mipi_dsi");
	if (IS_ERR(cphy->clk)) {
		DRM_ERROR("fail to get clk clk_mipi_dsi\n");
		return -EINVAL;
	}

	cphy->clk_bus = devm_clk_get(dev, "clk_bus_mipi_dsi");
	if (IS_ERR(cphy->clk_bus)) {
		DRM_ERROR("fail to get clk clk_bus_mipi_dsi\n");
		return -EINVAL;
	}

	cphy->clk_combphy = devm_clk_get(dev, "clk_mipi_dsi_combphy");
	if (IS_ERR(cphy->clk_bus)) {
		DRM_ERROR("fail to get clk_mipi_dsi_combphy\n");
		return -EINVAL;
	}

	cphy->rst_bus = devm_reset_control_get_shared(dev, "rst_bus_mipi_dsi");
	if (IS_ERR(cphy->rst_bus)) {
		DRM_ERROR("fail to get reset rst_bus_mipi_dsi\n");
		return -EINVAL;
	}

	cphy->phy = devm_phy_create(&pdev->dev, NULL, &sunxi_cphy_ops);
	if (IS_ERR(cphy->phy)) {
		dev_err(&pdev->dev, "failed to create PHY\n");
		return PTR_ERR(cphy->phy);
	}

	dsi_combo_phy_set_reg_base(cphy->cphy_data->id, cphy->reg_base);
	phy_set_drvdata(cphy->phy, cphy);

	phy_provider =
		devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);

	component_add(&pdev->dev, &sunxi_cphy_component_ops);
	DRM_INFO("%s finsh\n", __FUNCTION__);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct dsi_cphy_data phy0_data = {
	.id = 0,
};

static const struct dsi_cphy_data phy1_data = {
	.id = 1,
};

static const struct of_device_id sunxi_cphy_of_table[] = {
	{ .compatible = "allwinner,sunxi-dsi-combo-phy0", .data = &phy0_data },
	{ .compatible = "allwinner,sunxi-dsi-combo-phy1", .data = &phy1_data },
	{}
};
MODULE_DEVICE_TABLE(of, sunxi_cphy_of_table);

struct platform_driver sunxi_dsi_combo_phy_platform_driver = {
	.probe		= sunxi_dsi_combo_phy_probe,
	.driver		= {
		.name		= "sunxi-dsi-combo-phy",
		.of_match_table	= sunxi_cphy_of_table,
	},
};
