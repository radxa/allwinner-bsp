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
#include <linux/version.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
#include <drm/drm_hdcp.h>
#include <drm/drm_scdc_helper.h>
#else
#include <drm/display/drm_hdcp.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_scdc_helper.h>
#endif

#include "sunxi_hdmi.h"

struct sunxi_hdmi_s      *sunxi_hdmi;

static const struct sunxi_hdmi_vic_mode sunxi_hdmi_3d_table[] = {
	  /* .name */          /* .vic_code */
	{ "720P@50 FP",  SUNXI_HDMI_VIC_720P_50_3D_FP },
	{ "720P@60 FP",  SUNXI_HDMI_VIC_720P_60_3D_FP },
	{ "1080P@24 FP", SUNXI_HDMI_VIC_1080P_24_3D_FP},
};


#if defined(SUNXI_HDMI20_PHY_AW)
/* allwinner phy ops */
struct dw_phy_ops_s  phy_ops = {
	.phy_init   = aw_phy_init,
	.phy_config = aw_phy_config,
	.phy_resume = aw_phy_resume,
	.phy_resume = aw_phy_reset,
	.phy_read   = aw_phy_read,
	.phy_write  = aw_phy_write,
};
#elif defined(SUNXI_HDMI20_PHY_INNO)
struct dw_phy_ops_s   phy_ops = {
	.phy_init   = inno_phy_init,
	.phy_config = inno_phy_config,
	.phy_read   = inno_phy_read,
	.phy_write  = inno_phy_write,
	.phy_dump   = inno_phy_dump,
};
#else
struct dw_phy_ops_s   phy_ops = {
	.phy_config = snps_phy_config,
	.phy_read   = snps_phy_read,
	.phy_write  = snps_phy_write,
};
#endif

static int _sunxi_hdmi_check_3d_mode(u32 vic)
{
	int size = ARRAY_SIZE(sunxi_hdmi_3d_table);
	int i = 0;

	for (i = 0; i < size; i++) {
		if (vic == sunxi_hdmi_3d_table[i].vic_code) {
			return 0x1;
		}
	}
	return 0x0;
}

void sunxi_hdmi_ctrl_write(uintptr_t addr, u32 data)
{
	writeb((u8)data, (volatile void __iomem *)(sunxi_hdmi->reg_base + addr));
}

u32 sunxi_hdmi_ctrl_read(uintptr_t addr)
{
	return (u32)readb((volatile void __iomem *)(sunxi_hdmi->reg_base + addr));
}

#ifndef SUXNI_HDMI_USE_HDMI14
u8 sunxi_hdmi_scdc_read(u8 addr)
{
	u8 data = 0;
	int ret = 0;

	ret = drm_scdc_readb(sunxi_hdmi->connect->ddc, addr, &data);
	if (ret != 0) {
		hdmi_wrn("hdmi drm scdc byte read 0x%x failed\n", addr);
		data = 0;
	}

	return data;
}

void sunxi_hdmi_scdc_write(u8 addr, u8 data)
{
	int ret = 0;

	ret = drm_scdc_writeb(sunxi_hdmi->connect->ddc, addr, data);
	if (ret != 0) {
		hdmi_wrn("hdmi drm scdc byte write 0x%x = 0x%x failed\n", addr, data);
	}
}
#endif

int sunxi_hdmi_phy_write(u8 addr, u32 data)
{
	int ret = 0;
	if (sunxi_hdmi->phy_func->phy_write)
		ret = sunxi_hdmi->phy_func->phy_write(addr, (void *)&data);
	return ret;
}

int sunxi_hdmi_phy_read(u8 addr, u32 *data)
{
	int ret = 0;
	if (sunxi_hdmi->phy_func->phy_read)
		ret = sunxi_hdmi->phy_func->phy_read(addr, (void *)data);
	return 0;
}

int sunxi_hdmi_phy_config(void)
{
	if (sunxi_hdmi->phy_func->phy_config)
		sunxi_hdmi->phy_func->phy_config();
	return 0;
}

void sunxi_hdmi_phy_reset(void)
{
	if (sunxi_hdmi->phy_func->phy_reset)
		sunxi_hdmi->phy_func->phy_reset();
}

int sunxi_hdmi_phy_resume(void)
{
	if (sunxi_hdmi->phy_func->phy_resume)
		return sunxi_hdmi->phy_func->phy_resume();
	return 0;
}

int sunxi_hdmi_audio_set_info(hdmi_audio_t *snd_param)
{
	int ret = 0;
	struct dw_audio_s data;

	if (!snd_param) {
		hdmi_err("check point snd_param is null\n");
		return -1;
	}

	data.mInterfaceType     = snd_param->hw_intf;
	data.mCodingType        = snd_param->data_raw;
	data.mSamplingFrequency = snd_param->sample_rate;
	data.mChannelAllocation = snd_param->ca;
	data.mChannelNum        = snd_param->channel_num;
	data.mSampleSize        = snd_param->sample_bit;
	data.mClockFsFactor     = snd_param->fs_between;

	ret = dw_audio_set_info((void *)&data);
	if (ret != 0) {
		hdmi_err("sunxi hdmi update audio param failed\n");
		return -1;
	}

	return 0;
}

int sunxi_hdmi_audio_setup(void)
{
	return dw_audio_on();
}

struct disp_device_config *sunxi_hdmi_video_get_info(void)
{
	struct disp_device_config *info = &sunxi_hdmi->disp_info;

	return info;
}

int sunxi_hdmi_video_set_info(struct disp_device_config *disp_param)
{
	u8 data_bit = 0;

	/* set encoding mode */
	dw_video_update_color_format((dw_color_format_t)disp_param->format);

	/* set data bits */
	switch (disp_param->bits) {
	case DISP_DATA_8BITS:
	case DISP_DATA_10BITS:
	case DISP_DATA_12BITS:
		data_bit = 8 + (2 * disp_param->bits);
		break;
	case DISP_DATA_16BITS:
		data_bit = 16;
		break;
	default:
		data_bit = 8;
		break;
	}
	dw_video_update_color_depth(data_bit);

	switch (disp_param->eotf) {
	case DISP_EOTF_GAMMA22:
		dw_video_update_hdr_eotf(0x0, DW_EOTF_SDR);
		break;
	case DISP_EOTF_SMPTE2084:
		dw_video_update_hdr_eotf(0x1, DW_EOTF_SMPTE2084);
		break;
	case DISP_EOTF_ARIB_STD_B67:
		dw_video_update_hdr_eotf(0x1, DW_EOTF_HLG);
		break;
	default:
		break;
	}

	/* set color space */
	switch (disp_param->cs) {
	case DISP_BT601:
		dw_video_update_color_metry(DW_METRY_ITU601, DW_METRY_EXT_XV_YCC601);
		break;
	case DISP_BT709:
		dw_video_update_color_metry(DW_METRY_ITU709, DW_METRY_EXT_XV_YCC601);
		break;
	case DISP_BT2020NC:
		dw_video_update_color_metry(DW_METRY_EXTENDED, DW_METRY_EXT_BT2020_Y_CB_CR);
		break;
	default:
		dw_video_update_color_metry(DW_METRY_NULL, DW_METRY_EXT_XV_YCC601);
		break;
	}

	/* set output mode: hdmi or avi */
	switch (disp_param->dvi_hdmi) {
	case DISP_DVI:
		dw_video_update_tmds_mode(DW_TMDS_MODE_DVI);
		break;
	default:
		dw_video_update_tmds_mode(DW_TMDS_MODE_HDMI);
		break;
	}

	/* set clor range: defult/limited/full */
	switch (disp_param->range) {
	case DISP_COLOR_RANGE_0_255:
		dw_video_update_range(DW_RGB_RANGE_FULL);
		break;
	case DISP_COLOR_RANGE_16_235:
		dw_video_update_range(DW_RGB_RANGE_LIMIT);
		break;
	default:
		dw_video_update_range(DW_RGB_RANGE_DEFAULT);
		break;
	}

	/* set scan info */
	dw_video_update_scaninfo(disp_param->scan);

	/* set aspect ratio */
	dw_video_update_ratio(disp_param->aspect_ratio ? disp_param->aspect_ratio : 0x8);

	/* save current config info */
	memcpy(&sunxi_hdmi->disp_info, disp_param, sizeof(struct disp_device_config));
	return 0;
}

void sunxi_hdmi_video_set_pattern(u8 bit, u32 value)
{
	dw_fc_video_force_value(bit, value);
}

int sunxi_hdmi_i2c_xfer(struct i2c_msg *msgs, int num)
{
	return dw_i2cm_xfer(msgs, num);
}

#ifdef SUNXI_HDMI20_USE_HDCP
int sunxi_hdcp_get_sink_cap(void)
{
	u8 start = HDCP_2_2_HDMI_REG_VER_OFFSET & 0xff;
	u8 data = 0x0;
	int ret = 0x0;
	struct i2c_msg msgs[] = {
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = 0,
			.len = 0x1,
			.buf = &start,
		},
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = I2C_M_RD,
			.len = 0x1,
			.buf = &data
		}
	};

	ret = i2c_transfer(sunxi_hdmi->i2c, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs)) {
		if (data & HDCP_2_2_HDMI_SUPPORT_MASK)
			return 1;
	}

	return 0;
}
u8 sunxi_hdcp_get_state(void)
{
	int ret = 0;

	ret = dw_hdcp_get_state();
	if (ret == 1)
		return SUNXI_HDCP_ING;
	else if (ret == 0)
		return SUNXI_HDCP_SUCCESS;
	else if (ret == -1)
		return SUNXI_HDCP_FAILED;
	else if (ret == -2)
		return SUNXI_HDCP_FAILED;
	else
		return SUNXI_HDCP_DISABLE;
}

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14)
int sunxi_hdcp14_set_config(u8 state)
{
	if (state) {
		return dw_hdcp14_config();
	} else
		return dw_hdcp14_disconfig();
}
#endif

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
int sunxi_hdcp22_set_config(u8 state)
{
	if (state) {
		return dw_hdcp22_config();
	} else
		return dw_hdcp22_disconfig();
}

unsigned long *sunxi_hdcp22_get_fw_addr(void)
{
	return dw_hdcp22_get_fw_addr();
}

u32 sunxi_hdcp22_get_fw_size(void)
{
	return dw_hdcp22_get_fw_size();
}
#endif
#endif /* SUNXI_HDMI20_USE_HDCP */

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
void sunxi_cec_msg_dump(u8 *msg, u8 len)
{
	char buf[256];
	int n = 0, i = 0;

	n += sprintf(buf + n, "[msg]: ");
	for (i = 0; (i < len) && (n < 200); i++)
		n += sprintf(buf + n, "0x%02x ", *(msg + i));

	cec_log("%s\n", buf);
}

void sunxi_cec_enable(u8 state)
{
	dw_cec_set_enable(state == SUNXI_HDMI_ENABLE ? 0x1 : 0x0);
}

int sunxi_cec_message_receive(u8 *buf)
{
	int size = sizeof(buf);

	return dw_cec_receive_msg(buf, size);
}

void suxni_cec_message_send(u8 *buf, u8 len, u8 times)
{
	u8 type = SUNXI_CEC_WAIT_NULL;
	switch (times) {
	case SUNXI_CEC_WAIT_3BIT:
		type = DW_CEC_WAIT_3BIT;
		break;
	case SUNXI_CEC_WAIT_5BIT:
		type = DW_CEC_WAIT_5BIT;
	case SUNXI_CEC_WAIT_7BIT:
		type = DW_CEC_WAIT_7BIT;
	default:
		type = DW_CEC_WAIT_NULL;
		break;
	}
	sunxi_cec_msg_dump(buf, len);
	dw_cec_send_msg(buf, len, type);
}

void sunxi_cec_set_logic_addr(u16 addr)
{
	log_trace1(addr);
	dw_cec_set_logical_addr(addr);
}

u8 sunxi_cec_get_irq_state(void)
{
	u8 state = dw_mc_irq_get_state(DW_IRQ_CEC);

	if (!state)
		return SUNXI_CEC_IRQ_NULL;

	dw_mc_irq_clear_state(DW_IRQ_CEC, state);

	switch (state) {
	case IH_CEC_STAT0_DONE_MASK:
	  return SUNXI_CEC_IRQ_DONE;
	case IH_CEC_STAT0_EOM_MASK:
	  return SUNXI_CEC_IRQ_EOM;
	case IH_CEC_STAT0_NACK_MASK:
	  return SUNXI_CEC_IRQ_NACK;
	case IH_CEC_STAT0_ARB_LOST_MASK:
	  return SUNXI_CEC_IRQ_ARB;
	case IH_CEC_STAT0_ERROR_INITIATOR_MASK:
	  return SUNXI_CEC_IRQ_ERR_INITIATOR;
	case IH_CEC_STAT0_ERROR_FOLLOW_MASK:
	  return SUNXI_CEC_IRQ_ERR_FOLLOW;
	case IH_CEC_STAT0_WAKEUP_MASK:
	  return SUNXI_CEC_IRQ_WAKEUP;
	default:
	  return SUNXI_CEC_IRQ_NULL;
	}
}
#endif /* CONFIG_AW_HDMI20_CEC */

int sunxi_edid_parse(u8 *buffer)
{
	u8 temp_edid[EDID_BLOCK_SIZE] = {0x0};
	int edid_ext_cnt = 0, i = 0, ret = 0;

	if (!buffer) {
		hdmi_err("point buffer is null\n");
		return -1;
	}

	memcpy(temp_edid, buffer, EDID_BLOCK_SIZE);
	ret = dw_edid_parse_info((u8 *)temp_edid);
	if (ret != 0) {
		hdmi_err("hdmi edid parse block0 failed\n");
		return -1;
	}
	hdmi_inf("hdmi edid parse block0 finish\n");
	edid_ext_cnt = temp_edid[126];

	if (edid_ext_cnt == 0x0) {
		hdmi_inf("hdmi edid only has block0 and parse finish\n");
		return 0;
	}

	for (i = 0; i < edid_ext_cnt; i++) {
		memcpy(temp_edid, buffer + (EDID_BLOCK_SIZE * (i + 1)), EDID_BLOCK_SIZE);
		ret = dw_edid_parse_info((u8 *)temp_edid);
		if (ret != 0) {
			hdmi_err("hdmi edid parse block%d failed\n", i + 1);
			continue;
		}
		hdmi_inf("hdmi edid parse block%d finish\n", i + 1);
	}

	return 0;
}

int sunxi_hdmi_check_use_hdmi20_4k(u32 code)
{
	switch (code) {
	case HDMI_VIC_3840x2160P50:
	case HDMI_VIC_3840x2160P60:
	case HDMI_VIC_4096x2160P50:
	case HDMI_VIC_4096x2160P60:
		return 0x1;
	default:
		return 0x0;
	}
}

void sunxi_hdmi_update_prefered_video(void)
{
	int ret = 0;
	u32 dtd_code = dw_video_get_dtd_code();

	ret = _sunxi_hdmi_check_3d_mode(dtd_code);
	/* check is 3D video code */
	if (ret != 0) {
		dw_video_update_vic_format(DW_VIDEO_FORMAT_3D,
			(dtd_code - HDMI_VIC_3D_OFFSET));
		hdmi_inf("sunxi hdmi check is 3d video mode\n");
		return;
	}

	ret = sunxi_hdmi_check_use_hdmi20_4k(dtd_code);

	dw_avp_update_perfer_info(ret);
}

u8 sunxi_hdmi_get_hpd(void)
{
	return dw_phy_hot_plug_state();
}

int suxni_hdmi_set_loglevel(u8 level)
{
	dw_dev_set_loglevel(level);
	return 0;
}

int sunxi_hdmi_close(void)
{
	dw_phy_standby();

	dw_mc_all_clock_disable();

	dw_hdmi_ctrl_reset();

	return 0;
}

/* first disable output, send mute */
int sunxi_hdmi_disconfig(void)
{
	/* 1. send avmute */
	dw_avp_set_mute(0x1);

	/* 2. clear sink scdc info */
	if (dw_phy_hot_plug_state() && dw_fc_video_get_scramble()) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
		drm_scdc_set_scrambling(sunxi_hdmi->i2c, 0x0);
		drm_scdc_set_high_tmds_clock_ratio(sunxi_hdmi->i2c, 0x0);
#else
		drm_scdc_set_scrambling(sunxi_hdmi->connect, 0x0);
		drm_scdc_set_high_tmds_clock_ratio(sunxi_hdmi->connect, 0x0);
#endif
	}

	return 0;
}

int sunxi_hdmi_config(void)
{
	int ret = 0;

	log_trace();

	ret = dw_avp_config();
	if (ret != 0) {
		hdmi_err("sunxi hdmi avp config failed\n");
		return -1;
	}

	return 0;
}

int sunxi_hdmi_mode_convert(struct drm_display_mode *mode)
{
	dw_dtd_t video;
	u32 rate = 0;

	memset(&video, 0x0, sizeof(dw_dtd_t));

	if (!mode) {
		hdmi_err("check point mode is null\n");
		goto ret_failed;
	}

	video.mCode = (u32)drm_match_cea_mode(mode);

	video.mPixelClock = mode->clock;
	video.mInterlaced = (mode->flags & DRM_MODE_FLAG_INTERLACE) ? 0x1 : 0x0;
	video.mPixelRepetitionInput = (mode->flags & DRM_MODE_FLAG_DBLCLK) ? 0x1 : 0x0;

	video.mHActive     = mode->hdisplay;
	video.mHBlanking   = mode->htotal - mode->hdisplay;
	video.mHSyncOffset = mode->hsync_start - mode->hdisplay;
	video.mHSyncPulseWidth = mode->hsync_end - mode->hdisplay - video.mHSyncOffset;
	video.mHSyncPolarity   = (mode->flags & DRM_MODE_FLAG_PHSYNC) ? 0x1 : 0x0;

	video.mVActive     = mode->vdisplay;
	video.mVBlanking   = mode->vtotal - mode->vdisplay;
	video.mVSyncOffset = mode->vsync_start - mode->vdisplay;
	video.mVSyncPulseWidth = mode->vsync_end - mode->vdisplay - video.mVSyncOffset;
	video.mVSyncPolarity   = (mode->flags & DRM_MODE_FLAG_PVSYNC) ? 0x1 : 0x0;

	switch (mode->picture_aspect_ratio) {
	case HDMI_PICTURE_ASPECT_4_3:
		video.mHImageSize = 4;
		video.mVImageSize = 3;
		break;
	case HDMI_PICTURE_ASPECT_16_9:
		video.mHImageSize = 16;
		video.mVImageSize = 9;
		break;
	case HDMI_PICTURE_ASPECT_64_27:
		video.mHImageSize = 64;
		video.mVImageSize = 27;
		break;
	case HDMI_PICTURE_ASPECT_256_135:
		video.mHImageSize = 256;
		video.mVImageSize = 135;
		break;
	default:
		video.mHImageSize = 16;
		video.mVImageSize = 9;
		break;
	}

	rate = drm_mode_vrefresh(mode);

	dw_video_dtd_filling(&video, rate);
	sunxi_hdmi_update_prefered_video();
	return 0;
ret_failed:
	return -1;
}

void sunxi_hdmi_correct_config(void)
{
	dw_avp_correct_config();
}

int sunxi_hdmi_adap_bind(struct i2c_adapter *i2c_adap)
{
	if (!i2c_adap) {
		hdmi_err("check point i2c_adap is null\n");
		return -1;
	}

	sunxi_hdmi->i2c = i2c_adap;
	dw_i2cm_adap_bind(i2c_adap);
	return 0;
}

int sunxi_hdmi_connect_creat(struct drm_connector *data)
{
	/* sunxi hdmi connect */
	sunxi_hdmi->connect = data;
	/* dw hdmi connect */
	sunxi_hdmi->dw_hdmi.connect = data;
	return 0;
}

int sunxi_hdmi_init(struct sunxi_hdmi_s *hdmi)
{
	int ret = 0;
	sunxi_hdmi = hdmi;

	/* bind phy ops */
	hdmi->phy_func = &phy_ops;
	hdmi->blacklist_index = -1;

	hdmi->dw_hdmi.dev  = &hdmi->pdev->dev;
	hdmi->dw_hdmi.addr = hdmi->reg_base;
	hdmi->dw_hdmi.phy_ext = &phy_ops;
	ret = dw_hdmi_init(&hdmi->dw_hdmi);
	if (ret != 0) {
		hdmi_err("dw dev init failed\n");
		return -1;
	}

	return 0;
}

void sunxi_hdmi_exit(struct sunxi_hdmi_s *hdmi)
{
	dw_hdmi_exit();

	SUNXI_KFREE_POINT(hdmi);
}

ssize_t sunxi_hdmi_dump(char *buf)
{
	int n = 0;

	n += dw_hdmi_dump(buf + n);

	return n;
}

