/* SPDX-License-Identifier: GPL-2.0-or-later */
/*******************************************************************************
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 ******************************************************************************/
#include <linux/delay.h>

#include "dw_dev.h"
#include "dw_phy.h"
#include "dw_avp.h"
#include "dw_mc.h"
#include "phy_top.h"
#include "phy_snps.h"

#define PHY_DRIVE_PARAMS_NUM		(5)

struct snps_phy_drive_s {
	u32 min_clock;  /* min pixel clock */
	u32 max_clock;  /* max pixel clock */
	u32 data1;    /* txterm data */
	u32 data2;    /* vlevctrl data */
	u32 data3;    /* cksymtxctrl data */
};

struct snps_phy_mpll_data_s {
	u16 data1;
	u16 data2;
	u16 data3;
};

/*************************************************
 * { pixel_clk, {
 *		{8-bits} , {10-bits},
 *		{12-bits}, {16-bits}}
 * }
 ************************************************/
struct snps_phy_mpll_s {
	u32 pixel_clk;
	struct snps_phy_mpll_data_s mpll[4];
};

struct snps_phy_plat_s {
	u8 mpll_reg1;
	u8 mpll_reg2;
	u8 mpll_reg3;

	u8 drive_reg1;
	u8 drive_reg2;
	u8 drive_reg3;

	struct snps_phy_mpll_s       *mpll_rep0_table;
	u32 mpll_rep0_size;

	struct snps_phy_mpll_s       *mpll_rep1_table;
	u32 mpll_rep1_size;

	struct snps_phy_drive_s      *drive_table;
	u32 drive_size;
};

static struct snps_phy_drive_s sun50i_drive[] = {
	{ 25175, 165000, 0x0004, 0x0232, 0x8009},
	{165000, 340000, 0x0005, 0x0210, 0x803d},
	{340000, 600000, 0x0000, 0x008A, 0x8029}
};

static struct snps_phy_drive_s sun60i_drive[] = {
	{ 25175, 165000, 0x0004, 0x0232, 0x8009},
	{165000, 340000, 0x0007, 0x8160, 0x8188},
	{340000, 600000, 0x0000, 0x008A, 0x8029}
};

static struct snps_phy_mpll_s sun50i_mpll_rep0[] = {
	{25175, {
		{0x00B3, 0x0000, 0x0000}, {0x2153, 0x0000, 0x0000},
		{0x40F3, 0x0000, 0x0000}, {0x60B2, 0x0008, 0x0001}}
	},
	{27000, {
		{0x00B3, 0x0012, 0x0000}, {0x2153, 0x0000, 0x0000},
		{0x40F3, 0x0000, 0x0000}, {0x60B2, 0x0008, 0x0001}}
	},
	{50350, {
		{0x0072, 0x0008, 0x0001}, {0x2142, 0x0008, 0x0001},
		{0x40A2, 0x0008, 0x0001}, {0x6071, 0x001A, 0x0002}}
	},
	{54000, {
		{0x0072, 0x0013, 0x0001}, {0x2145, 0x0013, 0x0001},
		{0x4061, 0x0013, 0x0001}, {0x6071, 0x001A, 0x0002}}
	},
	{59400, {
		{0x0072, 0x0008, 0x0001}, {0x2142, 0x0008, 0x0001},
		{0x40A2, 0x0008, 0x0001}, {0x6071, 0x001A, 0x0002}}
	},
	{72000, {
		{0x0072, 0x0008, 0x0001}, {0x2142, 0x0008, 0x0001},
		{0x4061, 0x001B, 0x0002}, {0x6071, 0x001A, 0x0002}}
	},
	{74250, {
		{0x0072, 0x0013, 0x0001}, {0x2145, 0x0013, 0x0001},
		{0x4061, 0x0013, 0x0001}, {0x6071, 0x001A, 0x0002}}
	},
	{82500, {
		{0x0072, 0x0008, 0x0001}, {0x2145, 0x001A, 0x0002},
		{0x4061, 0x001B, 0x0002}, {0x6071, 0x001A, 0x0002}}
	},
	{90000, {
		{0x0072, 0x0008, 0x0001}, {0x2145, 0x001A, 0x0002},
		{0x4061, 0x001B, 0x0002}, {0x6071, 0x001A, 0x0002}}
	},
	{108000, {
		{0x0051, 0x001B, 0x0002}, {0x2145, 0x001A, 0x0002},
		{0x4061, 0x001B, 0x0002}, {0x6050, 0x0035, 0x0003}}
	},
	{148500, {
		{0x0051, 0x0019, 0x0002}, {0x214C, 0x0019, 0x0002},
		{0x4064, 0x0019, 0x0002}, {0x6050, 0x0035, 0x0003}}
	},
	{165000, {
		{0x0051, 0x001B, 0x0002}, {0x214C, 0x0033, 0x0003},
		{0x4064, 0x0034, 0x0003}, {0x6050, 0x0035, 0x0003}}
	},
	{185625, {
		{0x0040, 0x0036, 0x0003}, {0x214C, 0x0033, 0x0003},
		{0x4064, 0x0034, 0x0003}, {0x7A50, 0x001B, 0x0003}}
	},
	{198000, {
		{0x0040, 0x0036, 0x0003}, {0x214C, 0x0033, 0x0003},
		{0x4064, 0x0034, 0x0003}, {0x7A50, 0x001B, 0x0003}}
	},
	{216000, {
		{0x0040, 0x0036, 0x0003}, {0x214C, 0x0033, 0x0003},
		{0x4064, 0x0034, 0x0003}, {0x7A50, 0x001B, 0x0003}}
	},
	{237600, {
		{0x0040, 0x0036, 0x0003}, {0x214C, 0x0033, 0x0003},
		{0x5A64, 0x001B, 0x0003}, {0x7A50, 0x001B, 0x0003}}
	},
	{288000, {
		{0x0040, 0x0036, 0x0003}, {0x3B4C, 0x001B, 0x0003},
		{0x5A64, 0x001B, 0x0003}, {0x7A50, 0x003D, 0x0003}}
	},
	{297000, {
		{0x0040, 0x0019, 0x0003}, {0x3B4C, 0x001B, 0x0003},
		{0x5A64, 0x0019, 0x0003}, {0x7A50, 0x003D, 0x0003}}
	},
	{330000, {
		{0x0040, 0x0036, 0x0003}, {0x3B4C, 0x001B, 0x0003},
		{0x5A64, 0x001B, 0x0003}, {0x0000, 0x0000, 0x0000}}
	},
	{371250, {
		{0x1A40, 0x003F, 0x0003}, {0x3B4C, 0x001B, 0x0003},
		{0x5A64, 0x001B, 0x0003}, {0x0000, 0x0000, 0x0000}}
	},
	{495000, {
		{0x1A40, 0x003F, 0x0003}, {0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000}}
	},
	{594000, {
		{0x1A7c, 0x0010, 0x0003}, {0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000}}
	},
};

static struct snps_phy_mpll_s sun50i_mpll_rep1[] = {
	{13500, {
		{0x0133, 0x0000, 0x0000}, {0x2173, 0x0000, 0x0000},
		{0x41B3, 0x0000, 0x0000}, {0x6132, 0x0008, 0x0001}}
	},
	{27000, {
		{0x00B2, 0x0008, 0x0001}, {0x2152, 0x0008, 0x0001},
		{0x40F2, 0x0008, 0x0001}, {0x60B1, 0x0019, 0x0002}}
	},
	{54000, {
		{0x72, 0x8, 0x1}, { 0x2142, 0x8, 0x1},
		{0x40A2, 0x8, 0x1}, {0x6071, 0x001A, 0x2}}
	},
};

static struct snps_phy_mpll_s sun60i_mpll_rep0[] = {
	{27000, {
		{0x0003, 0x0283, 0x0628}, {0x2153, 0x0019, 0x0000},
		{0x40F3, 0x0019, 0x0000}, {0x60B2, 0x003B, 0x0001}}
	},
	{54000, {
		{0x0072, 0x001B, 0x0001}, {0x2142, 0x0023, 0x0001},
		{0x40A2, 0x002A, 0x0001}, {0x6071, 0x003F, 0x0002}}
	},
	{74250, {
		{0x0002, 0x1142, 0x0414}, {0x1009, 0x2203, 0x0619},
		{0x4061, 0x002E, 0x0002}, {0x6071, 0x003F, 0x0002}}
	},
	{108000, {
		{0x0051, 0x001C, 0x0002}, {0x2145, 0x003F, 0x0002},
		{0x4061, 0x002E, 0x0002}, {0x6050, 0x003F, 0x0003}}
	},
	{148500, {
		{0x0001, 0x2080, 0x020A}, {0x1018, 0x3203, 0x0619},
		{0x4064, 0x0019, 0x0002}, {0x6050, 0x0035, 0x0003}}
	},
	{297000, {
		{0x0000, 0x3041, 0x0205}, {0x3B4C, 0x003D, 0x0003},
		{0x5A64, 0x002A, 0x0003}, {0x7A50, 0x0022, 0x0003}}
	},
	{594000, {
		{0x0640, 0x3080, 0x0005}, {0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000}}
	},
};

static struct snps_phy_mpll_s sun60i_mpll_rep1[] = {
	// TODO
};

static struct snps_phy_plat_s sun50i_phy = {
	.mpll_reg1       = 0x06,
	.mpll_reg2       = 0x10,
	.mpll_reg3       = 0x15,
	.drive_reg1      = 0x19,
	.drive_reg2      = 0x0E,
	.drive_reg3      = 0x09,

	.mpll_rep0_table = sun50i_mpll_rep0,
	.mpll_rep0_size  = ARRAY_SIZE(sun50i_mpll_rep0),

	.mpll_rep1_table = sun50i_mpll_rep1,
	.mpll_rep1_size  = ARRAY_SIZE(sun50i_mpll_rep1),

	.drive_table     = sun50i_drive,
	.drive_size      = ARRAY_SIZE(sun50i_drive),
};

static struct snps_phy_plat_s sun60i_phy = {
	.mpll_reg1       = 0x06,
	.mpll_reg2       = 0x10,
	.mpll_reg3       = 0x11,
	.drive_reg1      = 0x19,
	.drive_reg2      = 0x0E,
	.drive_reg3      = 0x09,

	.mpll_rep0_table = sun60i_mpll_rep0,
	.mpll_rep0_size  = ARRAY_SIZE(sun60i_mpll_rep0),

	.mpll_rep1_table = sun60i_mpll_rep1,
	.mpll_rep1_size  = ARRAY_SIZE(sun60i_mpll_rep1),

	.drive_table     = sun60i_drive,
	.drive_size      = ARRAY_SIZE(sun60i_drive),
};

static struct snps_phy_plat_s    *snps_phy;

static u8 _color_bit_to_mpll_index(u8 bits)
{
	switch (bits) {
	case DW_COLOR_DEPTH_10:
		return 1;
	case DW_COLOR_DEPTH_12:
		return 2;
	case DW_COLOR_DEPTH_16:
		return 3;
	case DW_COLOR_DEPTH_8:
	default:
		return 0;
	}
}

static int _snps_phy_config_init(void)
{
	dw_mc_reset_phy(1);

	dw_phy_power_enable(0);

	dw_phy_svsret();

	dw_mc_reset_phy(0);

	dw_phy_reconfigure_interface();

	return 0;
}

static int _snps_phy_cfg_mpll(void)
{
	struct snps_phy_mpll_s *table = NULL;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	u8 index = _color_bit_to_mpll_index(hdmi->color_bits);
	u32 ref_clk = 0, clk = hdmi->pixel_clk, size = 0, i = 0;

	switch (hdmi->pixel_repeat) {
	case 0x1:
		table = snps_phy->mpll_rep1_table;
		size  = snps_phy->mpll_rep1_size;
		break;
	case 0x0:
	default:
		table = snps_phy->mpll_rep0_table;
		size  = snps_phy->mpll_rep0_size;
		break;
	}

	/* check min clock */
	if (clk < table[0].pixel_clk) {
		ref_clk = table[0].pixel_clk;
		hdmi_wrn("snps phy check clk: %dHz is too low and change use: %dHz\n",
			clk, ref_clk);
		goto clk_cfg;
	}

	/* check max clock */
	if (clk > table[size - 1].pixel_clk) {
		ref_clk = table[size - 1].pixel_clk;
		hdmi_wrn("snps phy check clk: %dHz is too max and change use: %dHz\n",
			clk, ref_clk);
		goto clk_cfg;
	}

	/* check clock is match */
	for (i = 0; i < size; i++) {
		if (clk == table[i].pixel_clk) {
			ref_clk = clk;
			goto clk_cfg;
		}
	}

	/* check and use near clock */
	for (i = 0; i < (size - 1); i++) {
		if (clk < table[i].pixel_clk)
			continue;
		if (clk > table[i + 1].pixel_clk)
			continue;
		if ((clk - table[i].pixel_clk) > (table[i + 1].pixel_clk - clk))
			ref_clk = table[i + 1].pixel_clk;
		else
			ref_clk = table[i].pixel_clk;
		hdmi_wrn("snps phy check clk: %dHz is match in [%dHz-%dHz] and uchange use: %dHz\n",
			clk, table[i].pixel_clk, table[i + 1].pixel_clk, ref_clk);
		goto clk_cfg;
	}

clk_cfg:
	for (i = 0; i < size; i++) {
		if (ref_clk != table[i].pixel_clk)
			continue;

		dw_phy_write(snps_phy->mpll_reg1, table[i].mpll[index].data1);
		dw_phy_write(snps_phy->mpll_reg2, table[i].mpll[index].data2);
		dw_phy_write(snps_phy->mpll_reg3, table[i].mpll[index].data3);

		hdmi_inf("snps phy %dKHz get mpll: 0x%04x, 0x%04x, 0x%04x\n", ref_clk,
			table[i].mpll[index].data1,
			table[i].mpll[index].data2,
			table[i].mpll[index].data3);

		return 0;
	}
	return -1;
}

static int _snps_phy_cfg_drive(void)
{
	struct dw_hdmi_dev_s    *hdmi  = dw_get_hdmi();
	struct snps_phy_drive_s *table = snps_phy->drive_table;
	u32 pixel_clk = hdmi->pixel_clk, i = 0;

	for (i = 0; i < snps_phy->drive_size; i++) {
		if (table[i].min_clock == table[i].max_clock) {
			if (pixel_clk != table[i].min_clock)
				continue;
		} else {
			if (pixel_clk < table[i].min_clock)
				continue;
			if (pixel_clk >= table[i].max_clock)
				continue;
		}

		hdmi_trace("snps phy drive: %dHz use [%dHz~%dHz]: 0x%04x, 0x%04x, 0x%04x\n",
			pixel_clk, table[i].min_clock, table[i].max_clock,
			table[i].data1, table[i].data2, table[i].data3);

		dw_phy_write(snps_phy->drive_reg1, table[i].data1);
		dw_phy_write(snps_phy->drive_reg2, table[i].data2);
		dw_phy_write(snps_phy->drive_reg3, table[i].data3);

		return 0;
	}

	hdmi_err("snps phy check pixel clock %dHz unmatch in table!\n", pixel_clk);
	return -1;
}

int snps_phy_disconfig(void)
{
	int ret = 0;

	ret = dw_phy_standby();
	if (ret != 0) {
		hdmi_err("dw phy standby failed\n");
		return -1;
	}

	return ret;
}

int snps_phy_config(void)
{
	int ret = 0;

	_snps_phy_config_init();

	ret = _snps_phy_cfg_mpll();
	if (ret != 0) {
		hdmi_err("snps phy config mpll failed\n");
		goto failed_exit;
	}
	ret = _snps_phy_cfg_drive();
	if (ret != 0) {
		hdmi_err("snps phy config drive failed\n");
		goto failed_exit;
	}

	dw_phy_power_enable(1);

	ret = dw_phy_wait_lock();
	hdmi_inf("snps phy state: %s\n", ret == 1 ? "lock" : "unlock");
	if (ret == 1)
		return 0;

failed_exit:
	return -1;
}

int snps_phy_write(u8 addr, void *data)
{
	u16 *value = (u16 *)data;

	if (IS_ERR_OR_NULL(value)) {
		shdmi_err(value);
		return -1;
	}

	return dw_phy_write(addr, *value);
}

int snps_phy_read(u8 addr, void *data)
{
	u16 *value = (u16 *)data;

	if (IS_ERR_OR_NULL(value)) {
		shdmi_err(value);
		return -1;
	}

	return dw_phy_read(addr, value);
}

ssize_t snps_phy_dump(char *buf)
{
	ssize_t n = 0;
	u16 mpll_data[3] = {0}, drive_data[3] = {0};

	snps_phy_read(snps_phy->mpll_reg1, &mpll_data[0]);
	snps_phy_read(snps_phy->mpll_reg2, &mpll_data[1]);
	snps_phy_read(snps_phy->mpll_reg3, &mpll_data[2]);

	snps_phy_read(snps_phy->drive_reg1, &drive_data[0]);
	snps_phy_read(snps_phy->drive_reg2, &drive_data[1]);
	snps_phy_read(snps_phy->drive_reg3, &drive_data[2]);

	n += sprintf(buf + n, "[snps phy]\n");
	n += sprintf(buf + n, " - mpll : 0x%04x, 0x%04x, 0x%04x\n",
		mpll_data[0], mpll_data[1], mpll_data[2]);
	n += sprintf(buf + n, " - drive: 0x%04x, 0x%04x, 0x%04x\n",
		drive_data[0], drive_data[1], drive_data[2]);

	return n;
}

int snps_phy_init(void)
{
	int ret = 0, i = 0;
	u32 *tmp_buf = NULL, tmp_size = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct device_node *hdmi_node = hdmi->dev->of_node;

	hdmi_trace("snps phy init\n");

	/* probe platform data */
	if (hdmi->plat_id == HDMI_SUN50I_W9_P1) {
		snps_phy = &sun50i_phy;
	} else if (hdmi->plat_id == HDMI_SUN60I_W2_P1) {
		snps_phy = &sun60i_phy;
	} else {
		hdmi_wrn("this platform %d not get register!\n", hdmi->plat_id);
		return -1;
	}

	/* get phy drive when dts is config */
	tmp_size = of_property_count_elems_of_size(hdmi_node, "snps_phy", sizeof(u32));
	if (tmp_size <= 0) {
		hdmi_inf("snps phy not get table from dts, use default\n");
		goto exit;
	}
	snps_phy->drive_size = tmp_size / PHY_DRIVE_PARAMS_NUM;

	tmp_buf = kmalloc(snps_phy->drive_size * sizeof(struct snps_phy_drive_s),
			GFP_KERNEL | __GFP_ZERO);
	if (!tmp_buf) {
		hdmi_err("snps phy alloc buffer failed\n");
		goto exit;
	}

	ret = of_property_read_u32_array(hdmi_node,
			"snps_phy", tmp_buf, tmp_size);
	if (ret < 0) {
		hdmi_err("snps phy get dts table value failed\n");
		goto exit;
	}

	snps_phy->drive_table = (struct snps_phy_drive_s *)tmp_buf;
	snps_phy->drive_size  = tmp_size / PHY_DRIVE_PARAMS_NUM;

	hdmi_trace("snps phy dts:\n");
	for (i = 0; i < snps_phy->drive_size; i++) {
		hdmi_trace(" - line[%d]: [%d ~ %d], 0x%04x, 0x%04x, 0x%04x\n", i,
			snps_phy->drive_table[i].min_clock,
			snps_phy->drive_table[i].max_clock,
			snps_phy->drive_table[i].data1,
			snps_phy->drive_table[i].data2,
			snps_phy->drive_table[i].data3);
	}

exit:
	return 0;
}
