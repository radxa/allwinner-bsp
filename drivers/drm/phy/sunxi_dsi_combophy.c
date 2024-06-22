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
#include <sunxi-log.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include "sunxi_dsi_combophy_reg.h"
#define CLK_PLL_DISPLL		0
#define CLK_DSI_LS		1
#define CLK_DSI_HS		2
#define CLK_LVDS_OR_RGB		3
struct dsi_combophy_data {
	unsigned int id;
};
struct sunxi_dsi_combophy {
	uintptr_t reg_base;
	int usage_count;
	unsigned int id;
	struct sunxi_dphy_lcd dphy_lcd;
	struct phy *phy;
	struct clk *phy_gating;
	struct reset_control *phy_rst;
	struct ccu_common               **ccu_clks;
	struct clk_hw_onecell_data      *hw_clks;

	struct mutex lock;
};
struct ccu_common {
	struct sunxi_dphy_lcd dphy_lcd;
	struct clk_hw   hw;
};

struct ccu_nm {
	u32			vco_enable;
	u32			m01;
	u32			m23;
	unsigned int            min_rate;
	unsigned int            max_rate;
	struct ccu_common       common;
};

static inline struct ccu_common *hw_to_ccu_common(struct clk_hw *hw)
{
	return container_of(hw, struct ccu_common, hw);
}

static inline struct ccu_nm *hw_to_ccu_nm(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_nm, common);
}

static long sunxi_displl_clk_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *prate)
{
/*
	struct ccu_nm *nm = hw_to_ccu_nm(hw);
	unsigned long rate_tmp;
	unsigned int m;

	rate_tmp = rate;

	for (m = 0; rate_tmp < nm->min_rate; ++m)
		rate_tmp = rate * m;

	prate = &rate_tmp;
*/
	return rate;
}

static int sunxi_displl_clk_set_rate(struct clk_hw *hw, unsigned long drate,
				  unsigned long prate)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);
	unsigned int n = 0, m = 0;
	unsigned long prate_tmp;
	struct displl_div div;

	if (nm->vco_enable) {
		prate_tmp = drate;
		for (m = 0; prate_tmp < nm->min_rate; ++m)
			prate_tmp = drate * m;

		n = DIV_ROUND_CLOSEST(prate_tmp, 24000000);
		div.n = n;
		div.m0 = 0;
		div.m1 = m - 1;
		div.m2 = 0;
		div.m3 = m - 1;
	} else {
		prate_tmp = get_displl_vco(&nm->common.dphy_lcd);
		m = DIV_ROUND_CLOSEST(prate_tmp, drate);
		div.n = 0;
		div.m0 = 0;
		div.m1 = 0;
		div.m2 = 3;
		div.m3 = m / div.m2;
	}
	displl_clk_set(&nm->common.dphy_lcd, &div);

	return 0;
}
static unsigned long sunxi_displl_clk_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);
	struct displl_div div;
	unsigned long rate = 0;

	displl_clk_get(&nm->common.dphy_lcd, &div);
	if (nm->m01)
		rate = 24000000 * div.n / (div.p + 1) / ((div.m0 + 1) * (div.m1 + 1));
	if (nm->m23)
		rate = 24000000 * div.n / (div.p + 1) / ((div.m2 + 1) * (div.m3 + 1));

	return rate;
}
static int sunxi_displl_clk_enable(struct clk_hw *hw)
{
	struct ccu_nm *nm = hw_to_ccu_nm(hw);

	displl_clk_enable(&nm->common.dphy_lcd);

	return 0;
}
const struct clk_ops sunxi_displl_clk_ops = {
	.enable		= sunxi_displl_clk_enable,
	.recalc_rate	= sunxi_displl_clk_recalc_rate,
	.round_rate	= sunxi_displl_clk_round_rate,
	.set_rate	= sunxi_displl_clk_set_rate,
};

/* ------------ dsi0 displl -------------*/
static struct ccu_nm pll_displl_clk = {
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init	= CLK_HW_INIT("displl", "dcxo24M",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_UNGATE | CLK_IGNORE_UNUSED),
	},
};

static struct ccu_nm displl_dsi_ls = {
	.m23		= BIT(1),
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init        = CLK_HW_INIT("displl_dsi_ls", "displl",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_NO_REPARENT |
					CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};

static struct ccu_nm displl_dsi_hs = {
	.vco_enable	= BIT(1),
	.m01		= BIT(1),
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init        = CLK_HW_INIT("displl_dsi_hs", "displl",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_NO_REPARENT |
					CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};

static struct ccu_nm lvds_or_rgb = {
	.vco_enable	= BIT(1),
	.m23		= BIT(1),
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init        = CLK_HW_INIT("lvds_or_rgb", "displl",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_NO_REPARENT |
					CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};
static struct clk_hw_onecell_data sunxi_displl_hw_clks = {
	.hws    = {
		[CLK_PLL_DISPLL]	= &pll_displl_clk.common.hw,
		[CLK_DSI_LS]		= &displl_dsi_ls.common.hw,
		[CLK_DSI_HS]		= &displl_dsi_hs.common.hw,
		[CLK_LVDS_OR_RGB]	= &lvds_or_rgb.common.hw,
	},
	.num = 4,
};

static struct ccu_common *displl_ccu_clks[] = {
	&pll_displl_clk.common,
	&displl_dsi_ls.common,
	&displl_dsi_hs.common,
	&lvds_or_rgb.common,
};
/* ------------- end ------------------- */

/* ------------ dsi1 displl ------------- */
static struct ccu_nm pll_displl_clk1 = {
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init	= CLK_HW_INIT("displl1", "dcxo24M",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_UNGATE | CLK_IGNORE_UNUSED),
	},
};

static struct ccu_nm displl_dsi_ls1 = {
	.m23		= BIT(1),
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init        = CLK_HW_INIT("displl_dsi_ls1", "displl1",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_NO_REPARENT |
					CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};

static struct ccu_nm displl_dsi_hs1 = {
	.vco_enable	= BIT(1),
	.m01		= BIT(1),
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init        = CLK_HW_INIT("displl_dsi_hs1", "displl1",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_NO_REPARENT |
					CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};

static struct ccu_nm lvds_or_rgb1 = {
	.vco_enable	= BIT(1),
	.m23		= BIT(1),
	.min_rate       = 1260000000,
	.max_rate       = 2520000000,
	.common         = {
		.hw.init        = CLK_HW_INIT("lvds_or_rgb1", "displl1",
					&sunxi_displl_clk_ops,
					CLK_SET_RATE_NO_REPARENT |
					CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};
static struct clk_hw_onecell_data sunxi_displl_hw_clks1 = {
	.hws    = {
		[CLK_PLL_DISPLL]	= &pll_displl_clk1.common.hw,
		[CLK_DSI_LS]		= &displl_dsi_ls1.common.hw,
		[CLK_DSI_HS]		= &displl_dsi_hs1.common.hw,
		[CLK_LVDS_OR_RGB]	= &lvds_or_rgb1.common.hw,
	},
	.num = 4,
};

static struct ccu_common *displl_ccu_clks1[] = {
	&pll_displl_clk1.common,
	&displl_dsi_ls1.common,
	&displl_dsi_hs1.common,
	&lvds_or_rgb1.common,
};
/* ------------- end ------------------- */

static int sunxi_dsi_combophy_clk_enable(struct sunxi_dsi_combophy *cphy)
{
	int ret = 0;

	if (cphy->phy_rst) {
		ret = reset_control_deassert(cphy->phy_rst);
		if (ret) {
			DRM_ERROR("reset_control_deassert for phy_rst_clk failed\n");
			ret = -1;
		}
	}

	if (cphy->phy_gating) {
		ret = clk_prepare_enable(cphy->phy_gating);
		if (ret) {
			DRM_ERROR("clk_prepare_enable gating for phy_gating_clk failed\n");
			ret = -1;
		}
	}

	return ret;
}

static int sunxi_dsi_combophy_clk_disable(struct sunxi_dsi_combophy *cphy)
{
	int ret = 0;

	if (cphy->phy_gating)
		clk_disable_unprepare(cphy->phy_gating);

	if (cphy->phy_rst)
		reset_control_assert(cphy->phy_rst);

	return ret;
}

static int sunxi_dsi_combophy_power_on(struct phy *phy)
{
	struct sunxi_dsi_combophy *cphy = phy_get_drvdata(phy);

	mutex_lock(&cphy->lock);

	if (cphy->usage_count == 0)
		sunxi_dsi_combophy_clk_enable(cphy);

	cphy->usage_count++;

	mutex_unlock(&cphy->lock);

	return 0;
}

static int sunxi_dsi_combophy_power_off(struct phy *phy)
{
	struct sunxi_dsi_combophy *cphy = phy_get_drvdata(phy);

	mutex_lock(&cphy->lock);

	if (cphy->usage_count == 1)
		sunxi_dsi_combophy_clk_disable(cphy);

	cphy->usage_count--;

	mutex_unlock(&cphy->lock);

	return 0;
}

static int sunxi_dsi_combophy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct sunxi_dsi_combophy *cphy = phy_get_drvdata(phy);

	switch (mode) {
	case PHY_MODE_LVDS:
		sunxi_dsi_combophy_set_lvds_mode(&cphy->dphy_lcd, submode ? true : false);
		break;
	case PHY_MODE_MIPI_DPHY:
		sunxi_dsi_combophy_set_dsi_mode(&cphy->dphy_lcd, submode);
		break;
	default:
		DRM_ERROR("dsi combophy unsupport mode:%d\n", mode);
		break;
	}
	return 0;
}

static int sunxi_dsi_combophy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct phy_configure_opts_mipi_dphy *config = &opts->mipi_dphy;
	struct sunxi_dsi_combophy *cphy = phy_get_drvdata(phy);

	DRM_INFO("[PHY] %s start\n", __FUNCTION__);
	sunxi_dsi_combophy_configure_dsi(&cphy->dphy_lcd, phy->attrs.mode, config);

	return 0;
}


static const struct phy_ops sunxi_dsi_combophy_ops = {
	.power_on = sunxi_dsi_combophy_power_on,
	.power_off = sunxi_dsi_combophy_power_off,
	.set_mode = sunxi_dsi_combophy_set_mode,
	.configure = sunxi_dsi_combophy_configure,
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

static int sunxi_displl_probe(struct device_node *node, struct sunxi_dsi_combophy *cphy)
{
	int ret = 0, i;

	if (cphy->id == 0) {
		cphy->hw_clks = &sunxi_displl_hw_clks;
		cphy->ccu_clks = displl_ccu_clks;
	} else {
		cphy->hw_clks = &sunxi_displl_hw_clks1;
		cphy->ccu_clks = displl_ccu_clks1;
	}
	for (i = 0; i < cphy->hw_clks->num ; i++) {
		struct clk_hw *hw = cphy->hw_clks->hws[i];
		struct ccu_common *cclk = cphy->ccu_clks[i];
		const char *name;

		if (!hw)
			continue;
		name = hw->init->name;
		cclk->dphy_lcd = cphy->dphy_lcd;
		sunxi_debug(NULL, "%s: sunxi ccu register. \n", name);

		ret = of_clk_hw_register(node, hw);
		if (ret) {
			sunxi_err(NULL, "[DISPLL]Couldn't register clock\n");
			return ret;
		}
	}
	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, cphy->hw_clks);

	return ret;
}

static int sunxi_dsi_combophy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct sunxi_dsi_combophy *cphy;
	struct resource *res;
	struct device *dev = &pdev->dev;
	const struct dsi_combophy_data *cphy_data;

	DRM_INFO("[PHY] %s start\n", __FUNCTION__);
	cphy = devm_kzalloc(dev, sizeof(*cphy), GFP_KERNEL);
	if (!cphy)
		return -ENOMEM;
	cphy_data = of_device_get_match_data(dev);
	if (!cphy_data) {
		DRM_ERROR("sunxi dsi combo phy fail to get match data\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cphy->reg_base = (uintptr_t)devm_ioremap_resource(&pdev->dev, res);
	if (!cphy->reg_base) {
		DRM_ERROR("unable to map dsi combo phy registers\n");
		return -EINVAL;
	}

	cphy->phy_gating = devm_clk_get(dev, "phy_gating_clk");
	if (IS_ERR_OR_NULL(cphy->phy_gating)) {
		DRM_ERROR("Maybe dsi clk gating is not need for phy_gating_clk\n");
		return -EINVAL;
	}

	cphy->phy_rst = devm_reset_control_get_shared(dev, "phy_rst_clk");
	if (IS_ERR_OR_NULL(cphy->phy_rst)) {
		DRM_ERROR("fail to get reset phy_rst_clk\n");
		return -EINVAL;
	}

	cphy->phy = devm_phy_create(&pdev->dev, NULL, &sunxi_dsi_combophy_ops);
	if (IS_ERR(cphy->phy)) {
		dev_err(&pdev->dev, "failed to create PHY\n");
		return PTR_ERR(cphy->phy);
	}

	cphy->id = cphy_data->id;
	cphy->dphy_lcd.dphy_index = cphy->id;
	sunxi_dsi_combo_phy_set_reg_base(&cphy->dphy_lcd, cphy->reg_base);

	sunxi_displl_probe(pdev->dev.of_node, cphy);
	phy_set_drvdata(cphy->phy, cphy);

	phy_provider =
		devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);

	component_add(&pdev->dev, &sunxi_cphy_component_ops);
	cphy->usage_count = 0;
	mutex_init(&cphy->lock);

	DRM_INFO("[PHY]%s finish\n", __FUNCTION__);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct dsi_combophy_data phy0_data = {
	.id = 0,
};

static const struct dsi_combophy_data phy1_data = {
	.id = 1,
};

static const struct of_device_id sunxi_dsi_combophy_of_table[] = {
	{ .compatible = "allwinner,sunxi-dsi-combo-phy0", .data = &phy0_data },
	{ .compatible = "allwinner,sunxi-dsi-combo-phy1", .data = &phy1_data },
	{}
};
MODULE_DEVICE_TABLE(of, sunxi_dsi_combophy_of_table);

struct platform_driver sunxi_dsi_combo_phy_platform_driver = {
	.probe		= sunxi_dsi_combophy_probe,
	.driver		= {
		.name		= "sunxi-dsi-combo-phy",
		.of_match_table	= sunxi_dsi_combophy_of_table,
	},
};
EXPORT_SYMBOL_GPL(sunxi_dsi_combo_phy_platform_driver);

MODULE_LICENSE("GPL");
