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

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/phy/phy-mipi-dphy.h>
#include "sunxi_dsi_combophy_reg.h"
#define DEVICE_DSI_NUM 3
#define DUAL_DSI_PHY 2
static int dsi_phy_mode;

static s32 sunxi_dsi_lane_set(struct sunxi_dphy_lcd *dphy, u32 lanes)
{
	u32 lane_den = 0;
	u32 i = 0;

	for (i = 0; i < lanes; i++)
		lane_den |= (1 << i);

	dphy->reg->dphy_ana3.bits.envttd = lane_den;
	dphy->reg->dphy_ana2.bits.enp2s_cpu = lane_den;
	dphy->reg->dphy_gctl.bits.lane_num = lanes - 1;
	return 0;
}

static s32 sunxi_dsi_dphy_cfg(struct sunxi_dphy_lcd *dphy)
{
	struct __disp_dsi_dphy_timing_t *dphy_timing_p;
	struct __disp_dsi_dphy_timing_t dphy_timing_cfg1 = {
		/* lp_clk_div(100ns) hs_prepare(70ns) hs_trail(100ns) */
		14,
		6,
		4,
		/* clk_prepare(70ns) clk_zero(300ns) clk_pre */
		7,
		50,
		3,
		/*
		 * clk_post: 400*6.734 for nop inst  2.5us
		 * clk_trail: 200ns
		 * hs_dly_mode
		 */
		10,
		30,
		0,
		/* hs_dly lptx_ulps_exit hstx_ana1 hstx_ana0 */
		10,
		3,
		3,
		3,
	};

	dphy->reg->dphy_gctl.bits.module_en = 0;
//	dphy->reg->dphy_gctl.bits.lane_num = panel->lcd_dsi_lane - 1;

	dphy->reg->dphy_tx_ctl.bits.hstx_clk_cont = 1;

	dphy_timing_p = &dphy_timing_cfg1;

	dphy->reg->dphy_tx_time0.bits.lpx_tm_set =
	    dphy_timing_p->lp_clk_div;
	dphy->reg->dphy_tx_time0.bits.hs_pre_set =
	    dphy_timing_p->hs_prepare;
	dphy->reg->dphy_tx_time0.bits.hs_trail_set =
	    dphy_timing_p->hs_trail;
	dphy->reg->dphy_tx_time1.bits.ck_prep_set =
	    dphy_timing_p->clk_prepare;
	dphy->reg->dphy_tx_time1.bits.ck_zero_set = dphy_timing_p->clk_zero;
	dphy->reg->dphy_tx_time1.bits.ck_pre_set = dphy_timing_p->clk_pre;
	dphy->reg->dphy_tx_time1.bits.ck_post_set = dphy_timing_p->clk_post;
	dphy->reg->dphy_tx_time2.bits.ck_trail_set =
	    dphy_timing_p->clk_trail;
	dphy->reg->dphy_tx_time2.bits.hs_dly_mode = 0;
	dphy->reg->dphy_tx_time2.bits.hs_dly_set = 0;
	dphy->reg->dphy_tx_time3.bits.lptx_ulps_exit_set = 0;
	dphy->reg->dphy_tx_time4.bits.hstx_ana0_set = 3;
	dphy->reg->dphy_tx_time4.bits.hstx_ana1_set = 3;
	dphy->reg->dphy_gctl.bits.module_en = 1;

	return 0;
}

unsigned long get_displl_vco(struct sunxi_dphy_lcd *dphy)
{
	u32 n = 0;

	n = dphy->reg->dphy_pll_reg0.bits.n;

	return n * 24000000;
}

void displl_clk_set(struct sunxi_dphy_lcd *dphy, struct displl_div *div)
{
	dphy->reg->dphy_pll_reg0.bits.ldo_en = 1;
	if (div->n)
		dphy->reg->dphy_pll_reg0.bits.n = div->n;
	if (div->m0)
		dphy->reg->dphy_pll_reg0.bits.m0 = div->m0 - 1;
	else
		dphy->reg->dphy_pll_reg0.bits.m0 = 0;
	if (div->m1)
		dphy->reg->dphy_pll_reg0.bits.m1 = div->m1 - 1;
	if (div->m2)
		dphy->reg->dphy_pll_reg0.bits.post_div0_clk_ls = div->m2 - 1;
	if (div->m3)
		dphy->reg->dphy_pll_reg0.bits.post_div1_clk_ls = div->m3 - 1;

	dphy->reg->dphy_pll_reg0.bits.p = 0;
}

void displl_clk_get(struct sunxi_dphy_lcd *dphy, struct displl_div *div)
{
	div->n = dphy->reg->dphy_pll_reg0.bits.n;
	div->m0 = dphy->reg->dphy_pll_reg0.bits.m0;
	div->m1 = dphy->reg->dphy_pll_reg0.bits.m1;
	div->m2 = dphy->reg->dphy_pll_reg0.bits.post_div0_clk_ls;
	div->m3 = dphy->reg->dphy_pll_reg0.bits.post_div1_clk_ls;
	div->p = dphy->reg->dphy_pll_reg0.bits.p;
}

void displl_clk_enable(struct sunxi_dphy_lcd *dphy)
{
//	u32 count = 0;

	dphy->reg->dphy_pll_reg0.bits.pll_en = 1;
	dphy->reg->dphy_pll_reg1.bits.lockdet_en = 1;
	dphy->reg->dphy_pll_reg0.bits.reg_update = 1;
/*
	while (count < 3) {
		if (dphy->reg->dphy_dbg0.bits.lock == 1)
			count++;
		udelay(5);
	}
*/
	udelay(20);
}

u32 sunxi_dsi_comb_dphy_pll_set(struct sunxi_dphy_lcd *dphy, u32 hs_clk_rate, enum phy_mode mode)
{
	u64 frq = hs_clk_rate;
	u32 n;
	u32 div_p, div_m0 = 0, div_m1 = 0, div_m2 = 0, div_m3 = 0;
//	frq = dclk * bpp / lane * 1000000; /* unit:Hz */
	if (frq <= 264000000) {
		frq = frq  * 8;
		n = div_u64(frq, 24000000);
		div_p = 0;
		if (dsi_phy_mode == DUAL_DSI_PHY) {
			div_m0 = 1;
			div_m1 = 7;
			div_m2 = 3;
			div_m3 = 11;
		} else {
			if (mode == PHY_MODE_MIPI_DPHY) {
				div_m0 = 0;
				div_m1 = 7;
				div_m2 = 3;
				div_m3 = 7;
			} else
				div_m3 = 7;
		}
	} else if (frq <= 536000000) {
		frq = frq  * 4;
		n = div_u64(frq, 24000000);
		div_p = 0;
		if (dsi_phy_mode == DUAL_DSI_PHY) {
			div_m0 = 1;
			div_m1 = 3;
			div_m2 = 3;
			div_m3 = 5;
		} else {
			if (mode == PHY_MODE_MIPI_DPHY) {
				div_m0 = 0;
				div_m1 = 3;
				div_m2 = 3;
				div_m3 = 3;
			} else
				div_m3 = 3;
		}
	} else if (frq <= 1072000000) {
		frq = frq  * 2;
		n = div_u64(frq, 24000000);
		div_p = 0;
		if (dsi_phy_mode == DUAL_DSI_PHY) {
			div_m0 = 0;
			div_m1 = 3;
			div_m2 = 1;
			div_m3 = 5;
		} else {
			if (mode == PHY_MODE_MIPI_DPHY) {
				div_m0 = 0;
				div_m1 = 1;
				div_m2 = 3;
				div_m3 = 1;
			} else
				div_m3 = 1;

		}
	} else if (frq <= 2144000000) {
		frq = frq  * 1;
		n = div_u64(frq, 24000000);
		div_p = 0;
		if (dsi_phy_mode == DUAL_DSI_PHY) {
			div_m0 = 0;
			div_m1 = 1;
			div_m2 = 0;
			div_m3 = 5;
		} else {
			if (mode == PHY_MODE_MIPI_DPHY) {
				div_m0 = 0;
				div_m1 = 0;
				div_m2 = 3;
				div_m3 = 0;
			} else
				div_m3 = 0;
		}
	} else {
		n = div_u64(frq, 24000000);
		div_p = 0;
		div_m0 = 0;
	}

	/* clk_hs:24MHz*n/(p+1)/(m0+1)/(m1+1); */
	/* clk_ls:24MHz*n/(p+1)/(m2+1)/(m3+1) */
	dphy->reg->dphy_pll_reg0.bits.n = n;
	dphy->reg->dphy_pll_reg0.bits.p = div_p;
	dphy->reg->dphy_pll_reg0.bits.m0 = div_m0;
	dphy->reg->dphy_pll_reg0.bits.m1 = div_m1;
	dphy->reg->dphy_pll_reg0.bits.post_div0_clk_ls = div_m2;
	dphy->reg->dphy_pll_reg0.bits.post_div1_clk_ls = div_m3;
	dphy->reg->dphy_pll_reg2.dwval = 0; /* Disable sdm */

	dphy->reg->dphy_pll_reg0.bits.pll_en = 1;
	dphy->reg->dphy_pll_reg0.bits.ldo_en = 1;

	dphy->reg->dphy_pll_reg1.bits.lockdet_en = 1;
	dphy->reg->dphy_pll_reg0.bits.reg_update = 1;

	return 24000000 * n / (div_p + 1) / (div_m0 + 1) / (div_m1 +1);
}

static u32 sunxi_dsi_io_open(struct sunxi_dphy_lcd *dphy)
{
	dphy->reg->dphy_ana4.bits.reg_soft_rcal = 0x18;
	dphy->reg->dphy_ana4.bits.en_soft_rcal = 1;
	dphy->reg->dphy_ana4.bits.reg_vlv_set = 5;
	dphy->reg->dphy_ana4.bits.reg_vlptx_set = 3;
	dphy->reg->dphy_ana4.bits.reg_vtt_set = 6;
	dphy->reg->dphy_ana4.bits.reg_vres_set = 3;
	dphy->reg->dphy_ana4.bits.reg_ib = 4;
	dphy->reg->dphy_ana4.bits.reg_vref_source = 0;

	dphy->reg->dphy_ana2.bits.enck_cpu = 1;

	dphy->reg->dphy_ana2.bits.enib = 1;
	dphy->reg->dphy_ana3.bits.enldor = 1;
	dphy->reg->dphy_ana3.bits.enldoc = 1;
	dphy->reg->dphy_ana3.bits.enldod = 1;
	dphy->reg->dphy_ana0.bits.reg_lptx_setr = 7;
	dphy->reg->dphy_ana0.bits.reg_lptx_setc = 7;
	dphy->reg->combo_phy_reg0.bits.en_cp = 1;

	dphy->reg->dphy_ana4.bits.en_mipi = 1;
	dphy->reg->combo_phy_reg0.bits.en_mipi = 1;
	dphy->reg->combo_phy_reg0.bits.en_comboldo = 1;
	dphy->reg->combo_phy_reg2.bits.hs_stop_dly = 20;
	udelay(1);

	dphy->reg->dphy_ana3.bits.envttc = 1;
//	dphy->reg->dphy_ana3.bits.envttd = lane_den;
	dphy->reg->dphy_ana3.bits.endiv = 1;
	dphy->reg->dphy_ana2.bits.enck_cpu = 1;
	dphy->reg->dphy_ana1.bits.reg_vttmode = 1;
//	dphy->reg->dphy_ana2.bits.enp2s_cpu = lane_den;

	dphy->reg->dphy_gctl.bits.module_en = 1;
	return 0;
}

static s32 sunxi_dsi_dphy_close(struct sunxi_dphy_lcd *dphy)
{
	dphy->reg->dphy_ana2.bits.enck_cpu = 0;
	dphy->reg->dphy_ana3.bits.endiv = 0;

	dphy->reg->dphy_ana2.bits.enib = 0;
	dphy->reg->dphy_ana3.bits.enldor = 0;
	dphy->reg->dphy_ana3.bits.enldoc = 0;
	dphy->reg->dphy_ana3.bits.enldod = 0;
	dphy->reg->dphy_ana3.bits.envttc = 0;
	dphy->reg->dphy_ana3.bits.envttd = 0;
	return 0;
}

static s32 lvds_combphy_close(struct sunxi_dphy_lcd *dphy)
{
	dphy->reg->combo_phy_reg1.dwval = 0x0;
	dphy->reg->combo_phy_reg0.dwval = 0x0;
	dphy->reg->dphy_ana4.dwval = 0x0;
	dphy->reg->dphy_ana3.dwval = 0x0;
	dphy->reg->dphy_ana1.dwval = 0x0;

	return 0;
}

static s32 lvds_combphy_open(struct sunxi_dphy_lcd *dphy)
{

	dphy->reg->combo_phy_reg1.dwval = 0x43;
	dphy->reg->combo_phy_reg0.dwval = 0x1;
	udelay(5);
	dphy->reg->combo_phy_reg0.dwval = 0x5;
	udelay(5);
	dphy->reg->combo_phy_reg0.dwval = 0x7;
	udelay(5);
	dphy->reg->combo_phy_reg0.dwval = 0xf;

	dphy->reg->dphy_ana4.dwval = 0x84000000;
	dphy->reg->dphy_ana3.dwval = 0x01040000;
	dphy->reg->dphy_ana2.dwval =
	    dphy->reg->dphy_ana2.dwval & (0x0 << 1);
	dphy->reg->dphy_ana1.dwval = 0x0;

	return 0;
}

static u32 sunxi_dsi_io_close(struct sunxi_dphy_lcd *dphy)
{
	dphy->reg->dphy_ana2.bits.enp2s_cpu = 0;
	dphy->reg->dphy_ana1.bits.reg_vttmode = 0;
	udelay(1);
	dphy->reg->dphy_ana2.bits.enck_cpu = 0;
	udelay(1);
	dphy->reg->dphy_ana3.bits.endiv = 0;
	udelay(1);
	dphy->reg->dphy_ana3.bits.envttd = 0;
	dphy->reg->dphy_ana3.bits.envttc = 0;
	udelay(1);
	dphy->reg->dphy_ana3.bits.enldod = 0;
	dphy->reg->dphy_ana3.bits.enldoc = 0;
	dphy->reg->dphy_ana3.bits.enldor = 0;
	udelay(5);
	dphy->reg->dphy_ana2.bits.enib = 0;
	dphy->reg->dphy_ana4.dwval = 0;
	dphy->reg->dphy_ana1.bits.reg_svtt = 0;
	dphy->reg->dphy_ana0.bits.reg_lptx_setr = 0;
	dphy->reg->dphy_ana0.bits.reg_lptx_setc = 0;
	dphy->reg->dphy_ana1.bits.reg_csmps = 0;
	dphy->reg->dphy_ana1.bits.reg_vttmode = 0;
	sunxi_dsi_dphy_close(dphy);

	return 0;
}


int sunxi_dsi_combo_phy_set_reg_base(struct sunxi_dphy_lcd *dphy, uintptr_t base)
{
	dphy->reg = (struct dphy_lcd_reg *) base;
	return 0;
}

int sunxi_dsi_combophy_set_dsi_mode(struct sunxi_dphy_lcd *dphy, int mode)
{
	dsi_phy_mode = mode;
	if (mode)
		sunxi_dsi_io_open(dphy);
	else
		sunxi_dsi_io_close(dphy);
	return 0;
}

int sunxi_dsi_combophy_configure_dsi(struct sunxi_dphy_lcd *dphy, enum phy_mode mode, struct phy_configure_opts_mipi_dphy *config)
{
	if (mode == PHY_MODE_MIPI_DPHY) {
		sunxi_dsi_dphy_cfg(dphy);
		sunxi_dsi_lane_set(dphy, config->lanes);
	}
//	sunxi_dsi_comb_dphy_pll_set(dphy, config->hs_clk_rate, mode);

	return 0;
}

int sunxi_dsi_combophy_set_lvds_mode(struct sunxi_dphy_lcd *dphy, bool enable)
{
	if (enable)
		lvds_combphy_open(dphy);
	else
		lvds_combphy_close(dphy);

	return 0;
}
