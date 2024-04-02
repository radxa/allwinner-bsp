/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2023 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _SUNXI_DSI_H_
#define _SUNXI_DSI_H_

#include <drm/drm_mipi_dsi.h>
#include <linux/interrupt.h>

#include "sunxi_tcon.h"
#include "include.h"
#include "disp_al_tcon.h"

//drm_lcd use
s32 sunxi_dsi_open(struct device *dsi_dev, const struct disp_panel_para *panel);
s32 sunxi_dsi_close(struct device *dsi_dev);
int sunxi_dsi_prepare(struct device *dsi_dev,
		      struct disp_panel_para *panel_para, irq_handler_t handler,
		      void *dat);
int sunxi_dsi_unprepare(struct device *dsi_dev);

//panel use
s32 sunxi_dsi_clk_enable(struct device *dsi_dev);
s32 sunxi_dsi_clk_disable(struct device *dsi_dev);

s32 sunxi_dsi_dcs_wr(struct device *dsi_dev, u8 command, u8 *para,
		     u32 para_num);
s32 sunxi_dsi_dcs_write_0para(struct device *dsi_dev, u8 command);
s32 sunxi_dsi_dcs_write_1para(struct device *dsi_dev, u8 command, u8 para1);
s32 sunxi_dsi_dcs_write_2para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2);
s32 sunxi_dsi_dcs_write_3para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3);
s32 sunxi_dsi_dcs_write_4para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3, u8 para4);
s32 sunxi_dsi_dcs_write_5para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3, u8 para4, u8 para5);
s32 sunxi_dsi_dcs_write_6para(struct device *dsi_dev, u8 command, u8 para1,
			      u8 para2, u8 para3, u8 para4, u8 para5, u8 para6);

s32 sunxi_dsi_gen_wr(struct device *dsi_dev, u8 command, u8 *para,
		     u32 para_num);
#endif
