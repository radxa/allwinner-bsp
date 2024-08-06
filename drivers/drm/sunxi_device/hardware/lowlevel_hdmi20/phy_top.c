/* SPDX-License-Identifier: GPL-2.0-or-later */
/*******************************************************************************
 *
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 ******************************************************************************/
#include <linux/delay.h>
#include <linux/clk.h>

#include "dw_dev.h"
#include "phy_top.h"

static DECLARE_WAIT_QUEUE_HEAD(top_phy_wq);

#define PHY_DCXO_24M	(24000000)
#define PHY_DCXO_19_2M	(19200000)
#define PHY_DCXO_26M	(26000000)

struct top_phy_pll_s {
	u32 pixel_clk; /* KHZ */
	u32 pll_value;
	u32 ldo_value;
	u32 pll_patern0;
	u32 pll_patern1;
};

struct top_phy_pll_s sun60i_phypll_26m[] = {
	{ 27000, 0xE8595C00, 0x00035000, 0x80000000, 0xCB10EFED},
	{ 54000, 0xE80C1A00, 0x00035000, 0x80000000, 0xCB10C4EC},
	{ 74250, 0xE81F5A00, 0x00035000, 0x80000000, 0xCB10C4EC},
	{108000, 0xE80C3500, 0x00035000, 0x80000000, 0xCB10C4EC},
	{148500, 0xE80F5A00, 0x00035000, 0x80000000, 0xCB10C8ED},
	{297000, 0xE807B602, 0x00035000, 0x00000000, 0x30000000},
	{594000, 0xE803B602, 0x00035000, 0x00000000, 0x30000000},
};

struct top_phy_pll_s sun60i_phypll_24m[] = {
	{ 27000, 0xE81F1100, 0x00035000, 0x80000000, 0xCB10EFED},
	{ 54000, 0xE8071100, 0x00035000, 0x80000000, 0xCB10C4EC},
	{ 74250, 0xE81F6200, 0x00035000, 0x80000000, 0xCB10C4EC},
	{108000, 0xE8031100, 0x00035000, 0x80000000, 0xCB10C4EC},
	{148500, 0xE80F6200, 0x00035000, 0x80000000, 0xCB10C8ED},
	{297000, 0xE8076200, 0x00035000, 0x00000000, 0x30000000},
	{594000, 0xE8036200, 0x00035000, 0x00000000, 0x30000000},
};

struct top_phy_s {
	top_phy_power_t					power_type;
	unsigned int                    dcxo_rate;
	unsigned int					pll_size;
	struct top_phy_pll_s			*pll_data;

	uintptr_t	     offset;
	volatile struct top_phy_regs	*phy_addr;
};

static struct top_phy_s			top_phy;

void top_phy_write(u32 offset, u32 data)
{
	*((volatile u32 *)top_phy.phy_addr + offset) = data;
}

u32 top_phy_read(u32 offset)
{
	return *((volatile u32 *)top_phy.phy_addr + offset);
}

u8 top_phy_get_clock_select(void)
{
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;
	u8 sel = 0;

	sel = phy_reg->reg_0024.sun60i.hdmi_outclk_sel;
	return sel;
}

void top_phy_set_clock_select(u8 sel)
{
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;

	phy_reg->reg_0024.sun60i.hdmi_outclk_sel = sel;
	hdmi_trace("top phy output clock from: %s\n", sel ? "ccmu" : "phy pll");
}

u8 top_phy_get_pad_select(void)
{
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;
	u8 sel = 0;

	sel = phy_reg->reg_0004.sun60i.hdmi_pad_sel;
	hdmi_trace("top phy get pad select: %s\n", sel ? "gpio pad" : "hdmi pad");
	return sel;
}

void top_phy_set_pad_select(u8 sel)
{
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;

	phy_reg->reg_0004.sun60i.hdmi_pad_sel = sel;
	hdmi_trace("top phy set pad select: %s\n", sel ? "gpio pad" : "hdmi pad");
}

u8 top_phy_pll_get_lock(void)
{
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;
	u8 state = (u8)phy_reg->reg_0040.sun60i.lock_status;

	return state;
}

void top_phy_pll_set_output(u8 state)
{
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;

	phy_reg->reg_0020.sun60i.pll_output_gate = state;
	hdmi_trace("top phy pll output gate: %s\n", state ? "enable" : "disable");
}

int top_phy_config(void)
{
	int ret = 0, i = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct top_phy_pll_s *pll_cfg = top_phy.pll_data;
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;
	u32 raw_clk = hdmi->pixel_clk;
	u32 size = top_phy.pll_size;
	u8 index = 0;

	top_phy_pll_set_output(0x0);

	if (hdmi->clock_src) {
		hdmi_inf("not-config phy pll when use ccmu mode\n");
		return 0;
	}

	/* check min clock */
	if (raw_clk < pll_cfg[0].pixel_clk) {
		index = 0;
		goto cfg_pll;
	}

	if (raw_clk > pll_cfg[size - 1].pixel_clk) {
		index = size - 1;
		goto cfg_pll;
	}

	for (i = 0; i < size; i++) {
		if (raw_clk == pll_cfg[i].pixel_clk) {
			index = i;
			goto cfg_pll;
		}

		if (raw_clk > pll_cfg[i].pixel_clk &&
				raw_clk < pll_cfg[i + 1].pixel_clk) {
			if ((pll_cfg[i + 1].pixel_clk - raw_clk) >
					(raw_clk - pll_cfg[i].pixel_clk))
				index = i;
			else
				index = i + 1;
			goto cfg_pll;
		}
	}

cfg_pll:
	phy_reg->reg_0020.sun60i.pll_input_div2 =
			((pll_cfg[index].pll_value & 0x00000002) >> 1);

	phy_reg->reg_0020.sun60i.pll_lock_model =
			((pll_cfg[index].pll_value & 0x00000020) >> 5);

	phy_reg->reg_0020.sun60i.pll_unlock_model =
			((pll_cfg[index].pll_value & 0x000000C0) >> 6);

	phy_reg->reg_0020.sun60i.pll_n =
			((pll_cfg[index].pll_value & 0x0000FF00) >> 8);

	phy_reg->reg_0020.sun60i.pll_p0 =
			((pll_cfg[index].pll_value & 0x007F0000) >> 16);

	phy_reg->reg_0028.dwval = pll_cfg[index].ldo_value;

	phy_reg->reg_002C.dwval = pll_cfg[index].pll_patern0;

	phy_reg->reg_0030.dwval = pll_cfg[index].pll_patern1;

	/* check pll open */
	if (!phy_reg->reg_0020.sun60i.pll_en)
		phy_reg->reg_0020.sun60i.pll_en = 0x1;

	if (!phy_reg->reg_0020.sun60i.pll_ldo_en)
		phy_reg->reg_0020.sun60i.pll_ldo_en = 0x1;

	phy_reg->reg_0024.sun60i.pll_level_shifter_gate = 0x1;

	/* enable lock check */
	phy_reg->reg_0020.sun60i.lock_enable = 0x1;

	/* wait top phy pll lock */
	ret = wait_event_timeout(top_phy_wq, top_phy_pll_get_lock(),
			msecs_to_jiffies(20));
	if (ret == 0) {
		hdmi_err("top phy wait pll lock timeout\n");
		return -1;
	}

	udelay(20);
	top_phy_pll_set_output(0x1);

	hdmi_inf("top phy config done\n");
	return 0;
}

void top_phy_power(top_phy_power_t type)
{
	char *power_type[] = {"on", "off", "low"};
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;

	switch (type) {
	case SUNXI_TOP_PHY_POWER_ON:
		phy_reg->reg_0000.sun60i.phy_reset         = 0x1;
		phy_reg->reg_0000.sun60i.phy_pddq          = 0x1;
		phy_reg->reg_0000.sun60i.phy_txpwron       = 0x1;
		phy_reg->reg_0000.sun60i.phy_enhpdrxsense  = 0x1;
	break;
	case SUNXI_TOP_PHY_POWER_LOW:
		phy_reg->reg_0000.sun60i.phy_reset         = 0x0;
		phy_reg->reg_0000.sun60i.phy_pddq          = 0x0;
		phy_reg->reg_0000.sun60i.phy_txpwron       = 0x0;
		phy_reg->reg_0000.sun60i.phy_svsret_mode   = 0x1;
		phy_reg->reg_0000.sun60i.phy_enhpdrxsense  = 0x0;
	break;
	case SUNXI_TOP_PHY_POWER_OFF:
	default:
		phy_reg->reg_0000.sun60i.phy_reset         = 0x0;
		phy_reg->reg_0000.sun60i.phy_pddq          = 0x0;
		phy_reg->reg_0000.sun60i.phy_txpwron       = 0x0;
		phy_reg->reg_0000.sun60i.phy_svsret_mode   = 0x0;
		phy_reg->reg_0000.sun60i.phy_enhpdrxsense  = 0x0;
	break;
	}

	top_phy.power_type = type;
	hdmi_trace("top phy power: %s\n", power_type[type]);
}

ssize_t top_phy_dump(char *buf)
{
	ssize_t n = 0;
	char *power_type[] = {"on", "off", "low"};
	volatile struct top_phy_regs *phy_reg = top_phy.phy_addr;

	n += sprintf(buf + n, "[top phy]\n");
	n += sprintf(buf + n, " - power mode  : %s\n",
			power_type[top_phy.power_type]);
	n += sprintf(buf + n, " - dcxo rate   : %dHz\n",
			top_phy.dcxo_rate);
	n += sprintf(buf + n, " - clock source: %s\n",
			top_phy_get_clock_select() ? "ccmu" : "phypll");
	n += sprintf(buf + n, " - pll state   : %s\n",
			top_phy_pll_get_lock() ? "lock" : "unlock");
	n += sprintf(buf + n, " - output gate : %s\n",
			phy_reg->reg_0020.sun60i.pll_output_gate ? "enable" : "disable");

	return n;
}

int top_phy_init(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct device *dev = hdmi->dev;
	struct clk *clk_dcxo = NULL;
	int dcxo_rate = 0;

	if (IS_ERR_OR_NULL(dev)) {
		shdmi_err(dev);
		return -1;
	}

	clk_dcxo = devm_clk_get(dev, "clk_dcxo");
	if (IS_ERR_OR_NULL(clk_dcxo))
		hdmi_inf("top phy can not get clk dcxo rate\n");

	dcxo_rate = clk_get_rate(clk_dcxo);

	if (hdmi->plat_id == HDMI_SUN60I_W2_P1) {
		top_phy.offset    = 0xE0000;
		top_phy.dcxo_rate = dcxo_rate;
		if (dcxo_rate == PHY_DCXO_24M) {
			top_phy.pll_data = sun60i_phypll_24m;
			top_phy.pll_size = ARRAY_SIZE(sun60i_phypll_24m);
			hdmi_trace("top phy get sun60i use 24M dcxo\n");
		} else if (dcxo_rate == PHY_DCXO_26M) {
			top_phy.pll_data = sun60i_phypll_26m;
			top_phy.pll_size = ARRAY_SIZE(sun60i_phypll_26m);
			hdmi_trace("top phy get sun60i use 26M dcxo\n");
		} else {
			top_phy.pll_data = sun60i_phypll_24m;
			top_phy.pll_size = ARRAY_SIZE(sun60i_phypll_24m);
			hdmi_inf("top phy get sun60i dcxo: %d is unsupport, use 24M\n",
				dcxo_rate);
		}
	} else {
		hdmi_inf("top phy unsupport plat id %d\n", hdmi->plat_id);
		return 0;
	}

	top_phy.phy_addr = (struct top_phy_regs *)(hdmi->addr + top_phy.offset);

	if (hdmi->sw_init)
		return 0;

	top_phy_power(SUNXI_TOP_PHY_POWER_ON);

	top_phy_pll_set_output(0x0);

	top_phy_set_clock_select(hdmi->clock_src);

	return 0;
}
