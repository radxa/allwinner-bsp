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
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
#include <drm/drm_scdc_helper.h>
#else
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_scdc_helper.h>
#endif

#include "dw_edid.h"
#include "dw_hdcp.h"
#include "dw_hdcp22.h"
#include "dw_i2cm.h"
#include "dw_phy.h"
#include "dw_avp.h"
#include "dw_dev.h"

static struct dw_hdmi_dev_s *hdmi;

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

u8 dw_hdmi_get_loglevel(void)
{
	if (IS_ERR(hdmi))
		return 0;
	return hdmi->log_level;
}

void dw_hdmi_set_loglevel(u8 level)
{
	if (IS_ERR(hdmi))
		return;
	hdmi->log_level = level;
}

bool dw_hdmi_check_loglevel(u8 index)
{
	u8 level = dw_hdmi_get_loglevel();

	if (level > DW_LOG_INDEX_TRACE)
		return true;

	if (level == index)
		return true;

	return false;
}

int dw_hdmi_ctrl_reset(void)
{
	hdmi->hdmi_on = 1;
	hdmi->tmds_clk = 0;
	hdmi->pixel_clk = 0;
	hdmi->color_bits = 0;
	hdmi->pixel_repeat = 0;
	hdmi->audio_on = 1;

	return 0;
}

int dw_hdmi_ctrl_update(void)
{
	dw_tmds_mode_t hdmi_on = 0;

	struct dw_video_s *video  = &hdmi->video_dev;

	hdmi_on = video->mHdmi;
	hdmi->hdmi_on    = (hdmi_on == DW_TMDS_MODE_HDMI) ? 1 : 0;
	hdmi->audio_on   = (hdmi_on == DW_TMDS_MODE_HDMI) ? 1 : 0;
	hdmi->pixel_clk  = dw_video_get_pixel_clk();
	hdmi->color_bits = video->mColorResolution;
	hdmi->pixel_repeat = video->mDtd.mPixelRepetitionInput;

	if (video->mEncodingIn == DW_COLOR_FORMAT_YCC422)
		hdmi->color_bits = 8;

	switch (video->mColorResolution) {
	case DW_COLOR_DEPTH_8:
		hdmi->tmds_clk = hdmi->pixel_clk;
		break;
	case DW_COLOR_DEPTH_10:
		if (video->mEncodingOut != DW_COLOR_FORMAT_YCC422)
			hdmi->tmds_clk = hdmi->pixel_clk * 125 / 100;
		else
			hdmi->tmds_clk = hdmi->pixel_clk;
		break;
	case DW_COLOR_DEPTH_12:
		if (video->mEncodingOut != DW_COLOR_FORMAT_YCC422)
			hdmi->tmds_clk = hdmi->pixel_clk * 3 / 2;
		else
			hdmi->tmds_clk = hdmi->pixel_clk;
		break;
	default:
		hdmi_err("unvalid color depth. default use 8bit depth\n");
		hdmi->tmds_clk = hdmi->pixel_clk;
		break;
	}

	if (video->mEncodingIn == DW_COLOR_FORMAT_YCC420) {
		hdmi->pixel_clk = hdmi->pixel_clk / 2;
		hdmi->tmds_clk /= 2;
	}

	return 0;
}

int dw_hdmi_scdc_set_scramble(u8 setup)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	drm_scdc_set_scrambling(hdmi->i2c_adap, setup);
	drm_scdc_set_high_tmds_clock_ratio(hdmi->i2c_adap, setup);
#else
	drm_scdc_set_scrambling(hdmi->connect, setup);
	drm_scdc_set_high_tmds_clock_ratio(hdmi->connect, setup);
#endif
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

	dw_hdcp_initial();

	dw_hdcp2x_init();

	return 0;
}

int dw_hdmi_exit(void)
{
	dw_edid_exit();

	dw_hdcp_exit();
	return 0;
}

ssize_t dw_hdmi_dump(char *buf)
{
	ssize_t n = 0;

	n += sprintf(buf + n, "[dw ctrl]\n");
	n += sprintf(buf + n, " - tmds_clk: [%dKHz], pixel_clk: [%dKHz], repet: [%d], bits: [%d]\n",
		hdmi->tmds_clk, hdmi->pixel_clk,
		hdmi->pixel_repeat, hdmi->color_bits);

	n += dw_phy_dump(buf + n);

	n += dw_avp_dump(buf + n);

	n += dw_hdcp_dump(buf + n);

	if (hdmi->phy_ext->phy_dump)
		n += hdmi->phy_ext->phy_dump(buf + n);

	return n;
}

struct dw_hdmi_dev_s *dw_get_hdmi(void)
{
	return hdmi;
}
