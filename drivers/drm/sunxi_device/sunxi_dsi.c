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
#include <drm/drm_print.h>
#include "sunxi_dsi.h"

struct dsi_data {
	unsigned int id;
};

struct sunxi_dsi {
	uintptr_t reg_base;

	/* TODO implete sunxi_dsi as a mipi_dsi_host,
	 * for now, panel dts node is a sub node of sunxi_dsi to get dsi deice node easily.
	 *  this make panel dts node will not become a struct device and perform probe, drm_panel will not create,
	 *  which will lead to sunxi_lcd_create_output fail.
	 *  however, mipi_dsi_host register will register panel as it's sub device, and drm_panel will create, this is what we want
	 *  even thought sunxi_dsi_host_ops is useless.
	 */
	struct mipi_dsi_host fake_host;
	const struct dsi_data *dsi_data;

	/* TODO use combophy as struct phy */
	struct clk *clk;
	struct clk *clk_bus;
	struct clk *clk_combphy;
	struct reset_control *rst_bus;
	unsigned int irq_no;
	void *irq_data;
	irq_handler_t irq_handler;

	u32 tcon_mode;
	u32 slave_tcon_num;
	u32 port_num;
};

static const struct dsi_data dsi0_data = {
	.id = 0,
};

static const struct dsi_data dsi1_data = {
	.id = 1,
};

static const struct of_device_id sunxi_dsi_match[] = {
	{ .compatible = "allwinner,dsi0", .data = &dsi0_data },
	{ .compatible = "allwinner,dsi1", .data = &dsi1_data },
	{},
};

static struct sunxi_dsi *to_sunxi_dsi(struct device *dev)
{
	if (PTR_ERR_OR_ZERO(dev) || !of_match_node(sunxi_dsi_match, dev->of_node)) {
		DRM_ERROR("use not dsi device for a dsi api!\n");
		return ERR_PTR(-EINVAL);
	}
	return dev_get_drvdata(dev);
}

/**
 * dsi module mode switch
 * @id         :dsi id
 * @param[IN]  :cmd_en: command mode enable
 * @param[IN]  :lp_en: lower power mode enable
 * @return     :0 if success
s32 sunxi_dsi_mode_switch(struct sunxi_dsi *dsi, u32 cmd_en, u32 lp_en)
{
	s32 ret = -1;

	if (dsi->tcon_mode == DISP_TCON_SLAVE_MODE)
		goto OUT;

	ret = dsi_mode_switch(dsi->dsi_data->id, cmd_en, lp_en);
	if (dsi->tcon_mode == DISP_TCON_DUAL_DSI
		&& (dsi->dsi_data->id + 1) < DEVICE_DSI_NUM)
		ret = dsi_mode_switch(dsi->dsi_data->id + 1, cmd_en, lp_en);
	else if (dsi->tcon_mode != DISP_TCON_NORMAL_MODE
			&& dsi->tcon_mode != DISP_TCON_DUAL_DSI)
		ret = dsi_mode_switch(dsi->slave_tcon_num, cmd_en, lp_en);

OUT:
	return ret;
}

 */

static s32 sunxi_dsi_clk_en(struct sunxi_dsi *dsi, u32 en)
{
	s32 ret = -1;

	if (dsi->tcon_mode == DISP_TCON_SLAVE_MODE)
		goto OUT;

	ret = dsi_clk_enable(dsi->dsi_data->id, en);
	if (dsi->tcon_mode == DISP_TCON_DUAL_DSI &&
	    dsi->dsi_data->id + 1 < DEVICE_DSI_NUM)
		ret = dsi_clk_enable(dsi->dsi_data->id + 1, en);
	else if (dsi->tcon_mode != DISP_TCON_NORMAL_MODE &&
		 dsi->tcon_mode != DISP_TCON_DUAL_DSI)
		ret = dsi_clk_enable(dsi->slave_tcon_num, en);
OUT:
	return ret;
}

static int sunxi_dsi_clk_config(struct sunxi_dsi *dsi,
				const struct disp_panel_para *p_panel)
{
	int ret = -1;
	struct lcd_clk_info clk_info;
	unsigned long dclk_rate;
	unsigned long pll_rate, lcd_rate;
	unsigned long dsi_rate = 0;
	unsigned long dsi_rate_set = 0, pll_rate_set = 0;
	struct clk *parent_clk = NULL;

	memset(&clk_info, 0, sizeof(clk_info));
	ret = sunxi_tcon_get_lcd_clk_info(&clk_info, p_panel);
	if (ret) {
		DRM_WARN("Get clk_info fail!\n");
		return ret;
	}
	dclk_rate = ((unsigned long long)p_panel->lcd_dclk_freq) * 1000000;

	lcd_rate = dclk_rate * clk_info.dsi_div;
	pll_rate = lcd_rate * clk_info.lcd_div;
	dsi_rate = pll_rate / clk_info.dsi_div;
	pll_rate_set = pll_rate;
	parent_clk = clk_get_parent(dsi->clk);
	if (parent_clk)
		pll_rate_set = clk_get_rate(parent_clk);

	if (p_panel->lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE)
		dsi_rate_set = pll_rate_set;
	else
		dsi_rate_set = pll_rate_set / clk_info.dsi_div;

	dsi_rate_set =
		(clk_info.dsi_rate == 0) ? dsi_rate_set : clk_info.dsi_rate;
	clk_set_rate(dsi->clk, dsi_rate_set);
	dsi_rate_set = clk_get_rate(dsi->clk);
	if (dsi_rate_set != dsi_rate)
		DRM_WARN("Dsi rate to be set:%lu, real clk rate:%lu\n",
			 dsi_rate, dsi_rate_set);

	return ret;
}

static int sunxi_dsi_clk_config_enable(struct sunxi_dsi *dsi,
				       const struct disp_panel_para *p_panel)
{
	int ret = 0;

	ret = sunxi_dsi_clk_config(dsi, p_panel);
	if (ret) {
		DRM_ERROR(
			"clk_prepare_enable for clk_mipi_dsi_combphy failed\n");
		return ret;
	}

	ret = reset_control_deassert(dsi->rst_bus);
	if (ret) {
		DRM_ERROR(
			"reset_control_deassert for rst_bus_mipi_dsi failed\n");
		return ret;
	}

	ret = clk_prepare_enable(dsi->clk);
	if (ret) {
		DRM_ERROR("clk_prepare_enable for clk_mipi_dsi failed\n");
		return ret;
	}

	ret = clk_prepare_enable(dsi->clk_bus);
	if (ret) {
		DRM_ERROR("clk_prepare_enable for clk_bus_mipi_dsi failed\n");
		return ret;
	}

	ret = clk_prepare_enable(dsi->clk_combphy);
	if (ret) {
		DRM_ERROR(
			"clk_prepare_enable for clk_mipi_dsi_combphy failed\n");
		return ret;
	}

	return ret;
}

static int sunxi_dsi_clk_config_disable(struct sunxi_dsi *dsi)
{
	int ret = 0;

	clk_disable_unprepare(dsi->clk_combphy);
	clk_disable_unprepare(dsi->clk_bus);
	clk_disable_unprepare(dsi->clk);

	ret = reset_control_assert(dsi->rst_bus);
	if (ret) {
		DRM_ERROR("reset_control_assert for rst_bus_mipi_dsi failed\n");
		return ret;
	}
	return ret;
}

static irqreturn_t sunxi_dsi_irq_event_proc(int irq, void *parg)
{
	struct sunxi_dsi *dsi = parg;
	/* NOTE: only for dsi40 */
	dsi_irq_query(dsi->dsi_data->id, DSI_IRQ_VIDEO_VBLK);
	return dsi->irq_handler(irq, dsi->irq_data);
}

int sunxi_dsi_prepare(struct device *dsi_dev,
		      struct disp_panel_para *panel_para, irq_handler_t handler,
		      void *data)
{
	int ret;
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	dsi->irq_handler = handler;
	dsi->irq_data = data;

	ret = devm_request_irq(dsi_dev, dsi->irq_no, sunxi_dsi_irq_event_proc,
			       0, dev_name(dsi_dev), dsi);
	if (ret) {
		DRM_ERROR("Couldn't request the IRQ for dsi\n");
	}

	ret = sunxi_dsi_clk_config_enable(dsi, panel_para);
	if (ret) {
		DRM_ERROR("dsi clk enable failed\n");
		return ret;
	}

	ret = dsi_cfg(dsi->dsi_data->id, panel_para);
	if (ret) {
		DRM_ERROR("dsi_cfg failed\n");
		return ret;
	}

	ret = dsi_io_open(dsi->dsi_data->id, panel_para);
	if (ret) {
		DRM_ERROR("dsi_io_open failed\n");
		return ret;
	}

	return ret;
}

int sunxi_dsi_unprepare(struct device *dsi_dev)
{
	int ret;
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	ret = sunxi_dsi_clk_config_disable(dsi);
	return ret;
}

//dsi hs start
s32 sunxi_dsi_clk_enable(struct device *dsi_dev)
{
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	return sunxi_dsi_clk_en(dsi, 1);
}
EXPORT_SYMBOL(sunxi_dsi_clk_enable);

//dsi hs end
s32 sunxi_dsi_clk_disable(struct device *dsi_dev)
{
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	return sunxi_dsi_clk_en(dsi, 0);
}
EXPORT_SYMBOL(sunxi_dsi_clk_disable);

s32 sunxi_dsi_open(struct device *dsi_dev, const struct disp_panel_para *panel)
{
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	return dsi_open(dsi->dsi_data->id, (struct disp_panel_para *)panel);
}

s32 sunxi_dsi_close(struct device *dsi_dev)
{
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	return dsi_close(dsi->dsi_data->id);
}

u32 sunxi_dsi_io_open(struct device *dsi_dev,
		      const struct disp_panel_para *panel)
{
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	return dsi_io_open(dsi->dsi_data->id, (struct disp_panel_para *)panel);
}

u32 sunxi_dsi_io_close(struct device *dsi_dev)
{
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	return dsi_io_close(dsi->dsi_data->id);
}

s32 sunxi_dsi_dcs_wr(struct device *dsi_dev, u8 command, u8 *para, u32 para_num)
{
	s32 ret = -1;
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	if (dsi->tcon_mode == DISP_TCON_SLAVE_MODE)
		goto OUT;

	ret = dsi_dcs_wr(dsi->dsi_data->id, command, para, para_num);
	if (dsi->tcon_mode == DISP_TCON_DUAL_DSI &&
	    (dsi->dsi_data->id + 1) < DEVICE_DSI_NUM &&
	    dsi->port_num == DISP_LCD_DSI_SINGLE_PORT)
		ret = dsi_dcs_wr(dsi->dsi_data->id + 1, command, para,
				 para_num);
	else if (dsi->tcon_mode != DISP_TCON_NORMAL_MODE &&
		 dsi->tcon_mode != DISP_TCON_DUAL_DSI)
		ret = dsi_dcs_wr(dsi->slave_tcon_num, command, para, para_num);
OUT:
	return ret;
}
EXPORT_SYMBOL(sunxi_dsi_dcs_wr);

s32 sunxi_dsi_dcs_write_0para(struct device *dsi_dev, u8 command)
{
	u8 tmp[5];
	return sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 0);
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_0para);

/**
 * sunxi_dsi_dcs_write_1para - write command and para to mipi panel.
 * @dsi: The index of screen.
 * @command: Command to be transfer.
 * @paran: Para to be transfer.
 */
s32 sunxi_dsi_dcs_write_1para(struct device *dsi_dev, u8 command, u8 para1)
{
	u8 tmp[5];
	tmp[0] = para1;
	sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 1);

	return -1;
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_1para);

s32 sunxi_dsi_dcs_write_2para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2)
{
	u8 tmp[5];
	tmp[0] = para1;
	tmp[1] = para2;
	return sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 2);
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_2para);

s32 sunxi_dsi_dcs_write_3para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3)
{
	u8 tmp[5];
	tmp[0] = para1;
	tmp[1] = para2;
	tmp[2] = para3;
	return sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 3);
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_3para);

s32 sunxi_dsi_dcs_write_4para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3, u8 para4)
{
	u8 tmp[5];
	tmp[0] = para1;
	tmp[1] = para2;
	tmp[2] = para3;
	tmp[3] = para4;
	return sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 4);
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_4para);

s32 sunxi_dsi_dcs_write_5para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3, u8 para4, u8 para5)
{
	u8 tmp[5];
	tmp[0] = para1;
	tmp[1] = para2;
	tmp[2] = para3;
	tmp[3] = para4;
	tmp[4] = para5;
	return sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 5);
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_5para);

s32 sunxi_dsi_dcs_write_6para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3, u8 para4, u8 para5, u8 para6)
{
	u8 tmp[6];
	tmp[0] = para1;
	tmp[1] = para2;
	tmp[2] = para3;
	tmp[3] = para4;
	tmp[4] = para5;
	tmp[5] = para6;
	return sunxi_dsi_dcs_wr(dsi_dev, command, tmp, 6);
}
EXPORT_SYMBOL(sunxi_dsi_dcs_write_6para);

s32 sunxi_dsi_gen_wr(struct device *dsi_dev, u8 command, u8 *para, u32 para_num)
{
	s32 ret = -1;
	struct sunxi_dsi *dsi = to_sunxi_dsi(dsi_dev);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	if (dsi->tcon_mode == DISP_TCON_SLAVE_MODE)
		goto OUT;

	ret = dsi_gen_wr(dsi->dsi_data->id, command, para, para_num);
	if (dsi->tcon_mode == DISP_TCON_DUAL_DSI &&
	    (dsi->dsi_data->id + 1) < DEVICE_DSI_NUM &&
	    dsi->port_num == DISP_LCD_DSI_SINGLE_PORT)
		ret = dsi_gen_wr(dsi->dsi_data->id + 1, command, para,
				 para_num);
	else if (dsi->tcon_mode != DISP_TCON_NORMAL_MODE &&
		 dsi->tcon_mode != DISP_TCON_DUAL_DSI)
		ret = dsi_gen_wr(dsi->slave_tcon_num, command, para, para_num);
OUT:
	return ret;
}
EXPORT_SYMBOL(sunxi_dsi_gen_wr);
/*
s32 sunxi_dsi_gen_short_read(u32 id, u8 *para_p, u8 para_num,
				    u8 *result)
{
	s32 ret = -1;

	if (!result || !para_p || para_num > 2) {
		pr_err("[sunxi-dsi]error: wrong para\n");
		goto OUT;
	}

	ret = dsi_gen_short_rd(id, para_p, para_num, result);
OUT:
	return ret;
}

s32 sunxi_dsi_dcs_read(u32 id, u8 cmd, u8 *result, u32 *num_p)
{
	s32 ret = -1;

	if (!result || !num_p) {
		pr_err("[sunxi-dsi]error: wrong para\n");
		goto OUT;
	}
	ret = dsi_dcs_rd(id, cmd, result, num_p);
OUT:
	return ret;
}

s32 sunxi_set_max_ret_size(u32 id, u32 size)
{
	return dsi_set_max_ret_size(id, size);
}
*/
static int sunxi_dsi_bind(struct device *dev, struct device *master, void *data)
{
	return 0;
}

static void sunxi_dsi_unbind(struct device *dev, struct device *master,
			     void *data)
{
}

static const struct component_ops sunxi_dsi_component_ops = {
	.bind = sunxi_dsi_bind,
	.unbind = sunxi_dsi_unbind,
};

static int sunxi_dsi_attach(struct mipi_dsi_host *host,
			    struct mipi_dsi_device *device)
{
	return 0;
}

static int sunxi_dsi_detach(struct mipi_dsi_host *host,
			    struct mipi_dsi_device *device)
{
	return 0;
}

static ssize_t sunxi_dsi_transfer(struct mipi_dsi_host *host,
				  const struct mipi_dsi_msg *msg)
{
	return 0;
}

static const struct mipi_dsi_host_ops sunxi_dsi_host_ops = {
	.attach = sunxi_dsi_attach,
	.detach = sunxi_dsi_detach,
	.transfer = sunxi_dsi_transfer,
};

static int sunxi_dsi_probe(struct platform_device *pdev)
{
	struct sunxi_dsi *dsi;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	/* tcon_mode, slave_tcon_num, port_num is init to 0 for now not support other mode*/

	if (!dsi)
		return -ENOMEM;
	dsi->dsi_data = of_device_get_match_data(dev);
	if (!dsi->dsi_data) {
		DRM_ERROR("sunxi_dsi fail to get match data\n");
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

	dsi->clk = devm_clk_get(dev, "clk_mipi_dsi");
	if (IS_ERR(dsi->clk)) {
		DRM_ERROR("fail to get clk clk_mipi_dsi\n");
		return -EINVAL;
	}

	dsi->clk_bus = devm_clk_get(dev, "clk_bus_mipi_dsi");
	if (IS_ERR(dsi->clk_bus)) {
		DRM_ERROR("fail to get clk clk_bus_mipi_dsi\n");
		return -EINVAL;
	}

	dsi->clk_combphy = devm_clk_get(dev, "clk_mipi_dsi_combphy");
	if (IS_ERR(dsi->clk_bus)) {
		DRM_ERROR("fail to get clk_mipi_dsi_combphy\n");
		return -EINVAL;
	}

	dsi->rst_bus = devm_reset_control_get_shared(dev, "rst_bus_mipi_dsi");
	if (IS_ERR(dsi->rst_bus)) {
		DRM_ERROR("fail to get reset rst_bus_mipi_dsi\n");
		return -EINVAL;
	}

	dsi_set_reg_base(dsi->dsi_data->id, dsi->reg_base);
	dev_set_drvdata(dev, dsi);

	dsi->fake_host.ops = &sunxi_dsi_host_ops;
	dsi->fake_host.dev = dev;

	ret = mipi_dsi_host_register(&dsi->fake_host);
	if (ret) {
		DRM_ERROR("Couldn't register MIPI-DSI host\n");
		return ret;
	}

	DRM_INFO("%s ok\n", __FUNCTION__);

	return component_add(&pdev->dev, &sunxi_dsi_component_ops);
}

static int sunxi_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sunxi_dsi_component_ops);
	return 0;
}

struct platform_driver sunxi_dsi_platform_driver = {
	.probe = sunxi_dsi_probe,
	.remove = sunxi_dsi_remove,
	.driver = {
		   .name = "sunxi-dsi",
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_dsi_match,
	},
};
