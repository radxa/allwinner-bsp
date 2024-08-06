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
#include "dw_dev.h"
#include "dw_mc.h"

typedef struct irq_vector {
	irq_sources_t source;
	unsigned int stat_reg;
	unsigned int mute_reg;
} irq_vector_t;

static irq_vector_t irq_vec[] = {
	{DW_IRQ_AUDIO_PACKET,	   IH_FC_STAT0,        IH_MUTE_FC_STAT0},
	{DW_IRQ_OTHER_PACKET,	   IH_FC_STAT1,        IH_MUTE_FC_STAT1},
	{DW_IRQ_PACKETS_OVERFLOW,  IH_FC_STAT2,	       IH_MUTE_FC_STAT2},
	{DW_IRQ_AUDIO_SAMPLER,	   IH_AS_STAT0,        IH_MUTE_AS_STAT0},
	{DW_IRQ_PHY,               IH_PHY_STAT0,       IH_MUTE_PHY_STAT0},
	{DW_IRQ_I2CM,              IH_I2CM_STAT0,      IH_MUTE_I2CM_STAT0},
	{DW_IRQ_CEC,               IH_CEC_STAT0,       IH_MUTE_CEC_STAT0},
	{DW_IRQ_VIDEO_PACKETIZER,  IH_VP_STAT0,	       IH_MUTE_VP_STAT0},
	{DW_IRQ_I2C_PHY,           IH_I2CMPHY_STAT0,   IH_MUTE_I2CMPHY_STAT0},
	{DW_IRQ_AUDIO_DMA,         IH_AHBDMAAUD_STAT0, IH_MUTE_AHBDMAAUD_STAT0},
	{0, 0, 0},
};

void _dw_mc_disable_csc_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_CSCCLK_DISABLE_MASK, bit);
}

void _dw_mc_disable_tmds_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_TMDSCLK_DISABLE_MASK, bit);
}

void _dw_mc_disable_pixel_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_PIXELCLK_DISABLE_MASK, bit);
}

void _dw_mc_disable_pixel_repetition_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_PREPCLK_DISABLE_MASK, bit);
}

void dw_mc_disable_hdcp_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_HDCPCLK_DISABLE_MASK, bit);
}

u8 dw_mc_get_hdcp_clk(void)
{
	return dw_read_mask(MC_CLKDIS, MC_CLKDIS_HDCPCLK_DISABLE_MASK);
}

void dw_mc_disable_cec_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_CECCLK_DISABLE_MASK, bit);
}

void dw_mc_disable_audio_sampler_clock(u8 bit)
{
	dw_write_mask(MC_CLKDIS, MC_CLKDIS_AUDCLK_DISABLE_MASK, bit);
}

u8 dw_mc_get_audio_sample_clock(void)
{
	return (u8)dw_read_mask(MC_CLKDIS, MC_CLKDIS_AUDCLK_DISABLE_MASK);
}

void _dw_mc_video_feed_through_off(u8 bit)
{
	dw_write_mask(MC_FLOWCTRL, MC_FLOWCTRL_FEED_THROUGH_OFF_MASK, bit);
}

void dw_mc_reset_audio_i2s(void)
{
	dw_write_mask(MC_SWRSTZREQ, MC_SWRSTZREQ_II2SSWRST_REQ_MASK, 0x0);
}

void dw_mc_reset_tmds_clock(void)
{
	dw_write_mask(MC_SWRSTZREQ, MC_SWRSTZREQ_TMDSSWRST_REQ_MASK, 0x1);
}

void dw_mc_reset_phy(u8 bit)
{
	dw_write_mask(MC_PHYRSTZ, MC_PHYRSTZ_PHYRSTZ_MASK, bit);
}

void dw_mc_all_clock_enable(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	u8 val = 0;

	hdmi_trace("dw hdmi mc enable all clock\n");

	_dw_mc_video_feed_through_off(0);
	_dw_mc_disable_pixel_clock(0);
	_dw_mc_disable_tmds_clock(0);
	val = (hdmi->pixel_repeat > 0) ? 0 : 1;
	_dw_mc_disable_pixel_repetition_clock(val);
	_dw_mc_disable_csc_clock(0);
	val = (hdmi->audio_on) ? 0 : 1;
	dw_mc_disable_audio_sampler_clock(val);
	dw_mc_disable_hdcp_clock(1);/* disable it */
}

void dw_mc_all_clock_disable(void)
{
	hdmi_trace("dw hdmi mc disable all clock\n");
	_dw_mc_disable_pixel_clock(1);
	_dw_mc_disable_tmds_clock(1);
	_dw_mc_disable_pixel_repetition_clock(1);
	_dw_mc_disable_csc_clock(1);
	dw_mc_disable_audio_sampler_clock(1);
	dw_mc_disable_cec_clock(1);
	dw_mc_disable_hdcp_clock(1);
}

u8 dw_mc_irq_get_state(irq_sources_t irq)
{
	int i = 0;
	u8 state = 0;

	for (i = 0; irq_vec[i].source != 0; i++) {
		if (irq_vec[i].source == irq) {
			state = (u8)dw_read(irq_vec[i].stat_reg);
			break;
		}
	}
	return state;
}

int dw_mc_irq_clear_state(irq_sources_t irq, u8 state)
{
	int i = 0;

	for (i = 0; irq_vec[i].source != 0; i++) {
		if (irq_vec[i].source == irq) {
			dw_write(irq_vec[i].stat_reg, state);
			return 0;
		}
	}
	return -1;
}

int dw_mc_irq_mute_source(irq_sources_t irq_source)
{
	int i = 0;

	for (i = 0; irq_vec[i].source != 0; i++) {
		if (irq_vec[i].source == irq_source) {
			dw_write(irq_vec[i].mute_reg,  0xff);
			return true;
		}
	}
	hdmi_err("irq source [%d] is not supported\n", irq_source);
	return false;
}

int dw_mc_irq_unmute_source(irq_sources_t irq_source)
{
	int i = 0;

	for (i = 0; irq_vec[i].source != 0; i++) {
		if (irq_vec[i].source == irq_source) {
			hdmi_trace("irq write unmute: irq[%d] mask[%d]\n",
							irq_source, 0x0);
			dw_write(irq_vec[i].mute_reg,  0x00);
			return true;
		}
	}
	hdmi_err("irq source [%d] is supported\n", irq_source);
	return false;
}

void dw_mc_set_main_irq(u8 state)
{
	hdmi_trace("dw hdmi mc %s main irq\n", state ? "enable" : "disable");
	dw_write(IH_MUTE, state ? 0x0 : 0x3);
}

void dw_mc_irq_mask_all(void)
{
	hdmi_trace("dw hdmi mc mask all irq\n");
	dw_mc_irq_mute_source(DW_IRQ_AUDIO_PACKET);
	dw_mc_irq_mute_source(DW_IRQ_OTHER_PACKET);
	dw_mc_irq_mute_source(DW_IRQ_PACKETS_OVERFLOW);
	dw_mc_irq_mute_source(DW_IRQ_AUDIO_SAMPLER);
	dw_mc_irq_mute_source(DW_IRQ_PHY);
	dw_mc_irq_mute_source(DW_IRQ_I2CM);
	dw_mc_irq_mute_source(DW_IRQ_VIDEO_PACKETIZER);
	dw_mc_irq_mute_source(DW_IRQ_I2C_PHY);
	dw_mc_irq_mute_source(DW_IRQ_AUDIO_DMA);
	dw_mc_set_main_irq(0x1);
}
