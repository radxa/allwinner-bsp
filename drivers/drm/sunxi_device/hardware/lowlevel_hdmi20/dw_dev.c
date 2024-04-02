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
#include <linux/kernel.h>
#include <linux/slab.h>
#include "dw_hdcp.h"
#include "dw_i2cm.h"
#include "dw_phy.h"
#include "dw_avp.h"
#include "dw_dev.h"

static struct dw_hdmi_dev_s *hdmi;
static u8 log_level;

/**
 * @desc: double word get byte
 * @data: double word
 * @index: byte number.0,1,2,3
 */
u8 dw_to_byte(u32 data, u8 index)
{
	return (data >> (index * 8)) & 0xFF;
}

u8 dw_bit_field(const u16 data, u8 shift, u8 width)
{
	return (data >> shift) & ((((u16) 1) << width) - 1);
}

u16 dw_concat_bits(u8 bHi, u8 oHi, u8 nHi, u8 bLo, u8 oLo, u8 nLo)
{
	return (dw_bit_field(bHi, oHi, nHi) << nLo) | dw_bit_field(bLo, oLo, nLo);
}

u16 dw_byte_to_word(const u8 hi, const u8 lo)
{
	return dw_concat_bits(hi, 0, 8, lo, 0, 8);
}

u32 dw_byte_to_dword(u8 b3, u8 b2, u8 b1, u8 b0)
{
	u32 retval = 0;

	retval |= b0 << (0 * 8);
	retval |= b1 << (1 * 8);
	retval |= b2 << (2 * 8);
	retval |= b3 << (3 * 8);
	return retval;
}

u32 dw_read(u32 addr)
{
	return (u32)readb((volatile void __iomem *)(hdmi->addr + addr));
}

void dw_write(u32 addr, u32 data)
{
	writeb((u8)data, (volatile void __iomem *)(hdmi->addr + addr));
}

u32 dw_read_mask(u32 addr, u8 mask)
{
	return (dw_read(addr) & mask) >> ((u8)ffs(mask) - 1);
}

void dw_write_mask(u32 addr, u8 mask, u8 data)
{
	u8 temp = dw_read(addr);

	temp &= ~(mask);
	temp |= (mask & (data << ((u8)ffs(mask) - 1)));
	dw_write(addr, temp);
}

struct dw_hdmi_dev_s *dw_get_hdmi(void)
{
	return hdmi;
}

void dw_dev_set_loglevel(u8 level)
{
	log_level = level;
}

bool dw_dev_check_loglevel(u8 index)
{
	if (log_level > DW_LOG_INDEX_TRACE)
		return true;

	if (log_level == index)
		return true;

	return false;
}

u32 dw_hdmi_get_tmds_clk(void)
{
	return hdmi->ctrl_dev.tmds_clk;
}

int dw_hdmi_ctrl_reset(void)
{
	struct dw_ctrl_s *ctrl = &hdmi->ctrl_dev;

	ctrl->hdmi_on = 1;
	ctrl->tmds_clk = 0;
	ctrl->pixel_clk = 0;
	ctrl->color_resolution = 0;
	ctrl->pixel_repetition = 0;
	ctrl->audio_on = 1;

	return 0;
}

int dw_dev_ctrl_update(void)
{
	dw_tmds_mode_t hdmi_on = 0;

	struct dw_video_s *video   = &hdmi->video_dev;
	struct dw_ctrl_s   *tx_ctrl = &hdmi->ctrl_dev;

	hdmi_on = video->mHdmi;
	tx_ctrl->hdmi_on    = (hdmi_on == DW_TMDS_MODE_HDMI) ? 1 : 0;
	tx_ctrl->audio_on   = (hdmi_on == DW_TMDS_MODE_HDMI) ? 1 : 0;
	tx_ctrl->pixel_clk      = dw_video_get_pixel_clk(video);
	tx_ctrl->color_resolution = video->mColorResolution;
	tx_ctrl->pixel_repetition = video->mDtd.mPixelRepetitionInput;

	if (video->mEncodingIn == DW_COLOR_FORMAT_YCC422)
		tx_ctrl->color_resolution = 8;

	switch (video->mColorResolution) {
	case DW_COLOR_DEPTH_8:
		tx_ctrl->tmds_clk = tx_ctrl->pixel_clk;
		break;
	case DW_COLOR_DEPTH_10:
		if (video->mEncodingOut != DW_COLOR_FORMAT_YCC422)
			tx_ctrl->tmds_clk = tx_ctrl->pixel_clk * 125 / 100;
		else
			tx_ctrl->tmds_clk = tx_ctrl->pixel_clk;
		break;
	case DW_COLOR_DEPTH_12:
		if (video->mEncodingOut != DW_COLOR_FORMAT_YCC422)
			tx_ctrl->tmds_clk = tx_ctrl->pixel_clk * 3 / 2;
		else
			tx_ctrl->tmds_clk = tx_ctrl->pixel_clk;
		break;
	default:
		hdmi_err("unvalid color depth. default use 8bit depth\n");
		tx_ctrl->tmds_clk = tx_ctrl->pixel_clk;
		break;
	}

	if (video->mEncodingIn == DW_COLOR_FORMAT_YCC420) {
		tx_ctrl->pixel_clk = tx_ctrl->pixel_clk / 2;
		tx_ctrl->tmds_clk /= 2;
	}

	return 0;
}

int dw_hdmi_init(struct dw_hdmi_dev_s *data)
{
	if (!data) {
		hdmi_err("check point data is null\n");
		return -1;
	}

	hdmi = data;

	dw_edid_init();

	dw_audio_init();

	dw_phy_init();

	dw_i2cm_init();

#ifdef SUNXI_HDMI20_USE_HDCP
	dw_hdcp_initial();
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	dw_esm_init();
#endif /* CONFIG_AW_HDMI20_HDCP22 */
#endif /* CONFIG_AW_HDMI20_HDCP14 */

	return 0;
}

int dw_hdmi_exit(void)
{
	dw_edid_exit();

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14)
	dw_hdcp_exit();
#endif
	return 0;
}

ssize_t dw_hdmi_dump(char *buf)
{
	ssize_t n = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();

	n += sprintf(buf + n, "\n----------------- dw hdmi -------------------\n");
	n += sprintf(buf + n, "[dw ctrl]\n");
	n += sprintf(buf + n, " - tmds_clk[%dKHz], pixel_clk[%dKHz], repet[%d], bits[%d]\n",
		hdmi->ctrl_dev.tmds_clk, hdmi->ctrl_dev.pixel_clk,
		hdmi->ctrl_dev.pixel_repetition, hdmi->ctrl_dev.color_resolution);
	n += dw_phy_dump(buf + n);
	n += dw_avp_dump(buf + n);
	if (hdmi->phy_ext->phy_dump)
		n += hdmi->phy_ext->phy_dump(buf + n);

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	n += dw_hdcp_dump(buf + n);
#endif

	return n;
}
