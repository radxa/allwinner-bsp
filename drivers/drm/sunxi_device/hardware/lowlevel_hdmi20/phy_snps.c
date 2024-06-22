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

#include "dw_phy.h"
#include "dw_avp.h"
#include "dw_mc.h"

#include "phy_snps.h"

#if IS_ENABLED(CONFIG_ARCH_SUN60IW2)
	/* Refer to the MPLL configuration chapter by Databook */
	#define MPLL_REG1         (0x06)
	#define MPLL_REG2         (0x10)
	#define MPLL_REG3         (0x11)
	/* Refer PHY Register Databook */
	#define DRIVE_REG1        (0x19) /* Transmission Termination Register */
	#define DRIVE_REG2        (0x0E) /* Voltage Level Control and PLL Measure Control Register */
	#define DRIVE_REG3        (0x09) /* Clock Symbol and Transmitter Control Register */
#elif (IS_ENABLED(CONFIG_ARCH_SUN50IW9))
	/* Refer to the MPLL configuration chapter by databook */
	#define MPLL_REG1         (0x06)
	#define MPLL_REG2         (0x10)
	#define MPLL_REG3         (0x15)
	/* Refer PHY Register Databook */
	#define DRIVE_REG1        (0x19) /* Transmission Termination Register */
	#define DRIVE_REG2        (0x0E) /* Voltage Level Control Register */
	#define DRIVE_REG3        (0x09) /* Clock Symbol and Transmitter Control Register */
#else
	/* Refer to the MPLL configuration chapter by databook */
	#define MPLL_REG1         (0x06)
	#define MPLL_REG2         (0x10)
	#define MPLL_REG3         (0x15)
	/* Refer PHY Register Databook */
	#define DRIVE_REG1        (0x19) /* Transmission Termination Register */
	#define DRIVE_REG2        (0x0E) /* Voltage Level Control Register */
	#define DRIVE_REG3        (0x09) /* Clock Symbol and Transmitter Control Register */
#endif

struct snps_phy_drive_s {
	unsigned int min_clock;  /* min pixel clock */
	unsigned int max_clock;  /* max pixel clock */
	unsigned int data1;    /* txterm data */
	unsigned int data2;    /* vlevctrl data */
	unsigned int data3;    /* cksymtxctrl data */
};

struct snps_phy_mpll_data_s {
	u16 data1;
	u16 data2;
	u16 data3;
};

struct snps_phy_mpll_s {
	u32 pixel_clk;
	struct snps_phy_mpll_data_s mpll[4];
};

struct snps_phy_dev {
	struct snps_phy_mpll_data_s  *mpll_table;

	struct snps_phy_drive_s *drive_table;
	unsigned int drive_data_size;
};

static struct snps_phy_dev    phy_plat;

#define PHY_DRIVE_PARAMS_NUM		(5)
static struct snps_phy_drive_s phy_drive_def[] = {
	{ 25175, 165000, 0x0004, 0x0232, 0x8009},
	{165000, 340000, 0x0005, 0x0210, 0x803d},
	{340000, 600000, 0x0000, 0x008A, 0x8029}
};

static struct snps_phy_mpll_s phy_rep_0[] = {
	{25175, {
		{0x00B3, 0x0000, 0x0000}, {0x2153, 0x0000, 0x0000},
		{0x2153, 0x0000, 0x0000}, {0x2153, 0x0000, 0x0000}}
	},
	{27000, {
		{0x00B3, 0x0000, 0x0000}, {0x2153, 0x0000, 0x0000},
		{0x2153, 0x0000, 0x0000}, {0x2153, 0x0000, 0x0000}}
	},
};

static struct snps_phy_mpll_s phy_rep_1[] = {
	{13500, {
		{0x0133, 0x0000, 0x0000}, {0x2173, 0x0000, 0x0000},
		{0x41B3, 0x0000, 0x0000}, {0x6132, 0x0008, 0x0001}}
	},
	{27000, {
		{0x00B2, 0x0008, 0x0001}, {0x2152, 0x0008, 0x0001},
		{0x40F2, 0x0008, 0x0001}, {0x60B1, 0x0019, 0x0002}}
	},
	{54000, {
		{0x0072, 0x0008, 0x0001}, {0x2142, 0x0008, 0x0001},
		{0x40A2, 0x0008, 0x0001}, {0x6071, 0x001A, 0x0002}}
	},
};

#if IS_ENABLED(CONFIG_ARCH_SUN50IW9)
/* phy301 table */
static struct snps_phy_mpll_s phy_rep_0[] = {
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
}

static struct snps_phy_mpll_s phy_rep_1[] = {
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

}
#endif

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
	u32 ref_clk = 0, clk = 0;
	u32 size = 0, i = 0;

	clk = hdmi->pixel_clk;

	switch (hdmi->pixel_repeat) {
	case 0x0:
		table = phy_rep_0;
		size = ARRAY_SIZE(phy_rep_0);
		break;
	case 0x1:
		table = phy_rep_1;
		size = ARRAY_SIZE(phy_rep_1);
		break;
	default:
		table = phy_rep_0;
		size = ARRAY_SIZE(phy_rep_0);
		break;
	}

	hdmi_inf("snps phy use rep %d table size: %d\n",
		hdmi->pixel_repeat, size);
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
		phy_plat.mpll_table = &table[i].mpll[hdmi->color_bits];
		if (!phy_plat.mpll_table) {
			hdmi_err("snps phy can not get mpll data\n");
			return -1;
		}

		dw_phy_write(MPLL_REG1, phy_plat.mpll_table->data1);
		dw_phy_write(MPLL_REG2, phy_plat.mpll_table->data2);
		dw_phy_write(MPLL_REG3, phy_plat.mpll_table->data3);

		hdmi_inf("snps phy %dHz get mpll: 0x%4x, 0x%4x, 0x%4x\n", ref_clk,
			phy_plat.mpll_table->data1,
			phy_plat.mpll_table->data2, phy_plat.mpll_table->data3);

		return 0;
	}
	return -1;
}

static int _snps_phy_cfg_drive(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct snps_phy_drive_s *table = NULL;
	u32 pixel_clk = 0, size = 0, i = 0;

	pixel_clk = hdmi->pixel_clk;

	if (phy_plat.drive_data_size) {
		/* use dts table */
		table = phy_plat.drive_table;
		size  = phy_plat.drive_data_size / PHY_DRIVE_PARAMS_NUM;
	} else {
		/* use default table */
		table = phy_drive_def;
		size  = ARRAY_SIZE(phy_drive_def);
	}

	for (i = 0; i < size; i++) {
		if (table[i].min_clock == table[i].max_clock) {
			if (pixel_clk != table[i].min_clock)
				continue;
		} else {
			if (pixel_clk < table[i].min_clock)
				continue;
			if (pixel_clk >= table[i].max_clock)
				continue;
		}

		hdmi_inf("snps phy drive: %dHz match in [%dHz~%dHz]: 0x%x, 0x%x, 0x%x\n",
			pixel_clk, table[i].min_clock, table[i].max_clock,
			table[i].data1, table[i].data2, table[i].data3);

		/* config drive data */
		dw_phy_write(DRIVE_REG1, table[i].data1);
		dw_phy_write(DRIVE_REG2, table[i].data2);
		dw_phy_write(DRIVE_REG3, table[i].data3);
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
		hdmi_err("sunxi hdmi phy standby failed\n");
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

int snps_phy_init(void)
{
	int ret = 0, i = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	u32 *tmp_buf = NULL;

	ret = of_property_count_elems_of_size(hdmi->dev->of_node, "snps_phy", sizeof(u32));
	if (ret <= 0) {
		hdmi_inf("snps phy not get table from dts, use default\n");
		goto reset;
	}

	phy_plat.drive_data_size = ret;
	hdmi_inf("snps phy get dts config drive table size: %d\n", ret);

	tmp_buf = kmalloc(phy_plat.drive_data_size, GFP_KERNEL | __GFP_ZERO);
	if (!tmp_buf) {
		hdmi_err("snps phy alloc buffer failed\n");
		goto reset;
	}

	ret = of_property_read_u32_array(hdmi->dev->of_node,
			"snps_phy", tmp_buf, phy_plat.drive_data_size);
	if (ret < 0) {
		hdmi_err("snps phy get dts table value failed\n");
		goto reset;
	}
	phy_plat.drive_table = (struct snps_phy_drive_s *)tmp_buf;

	for (i = 0; i < (phy_plat.drive_data_size / PHY_DRIVE_PARAMS_NUM); i++) {
		phy_log("line[%d], min_clk: %d, max_clk: %d, 0x%x, 0x%x, 0x%x\n", i,
			phy_plat.drive_table[i].min_clock,
			phy_plat.drive_table[i].max_clock,
			phy_plat.drive_table[i].data1,
			phy_plat.drive_table[i].data2,
			phy_plat.drive_table[i].data3);
	}
	goto exit;

reset:
	phy_plat.drive_table = NULL;
	phy_plat.drive_data_size = 0;
exit:
	return 0;
}

int snps_phy_write(u8 addr, void *data)
{
	u16 *value = (u16 *)data;
	if (!value) {
		hdmi_err("snps phy write check value is null\n");
		return -1;
	}
	return dw_phy_write(addr, *value);
}

int snps_phy_read(u8 addr, void *data)
{
	u16 *value = (u16 *)data;
	if (!value) {
		hdmi_err("snps phy read check value is null\n");
		return -1;
	}

	return dw_phy_read(addr, value);
}