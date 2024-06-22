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
#include <drm/drm_scdc_helper.h>
#else
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_scdc_helper.h>
#endif

#include "dw_mc.h"
#include "dw_fc.h"
#include "dw_edid.h"
#include "dw_hdcp.h"
#include "dw_avp.h"

struct dw_audio_n_table_s {
	u32 pixel_clock;/* KHZ */
	u32 n;
};

static struct dw_audio_n_table_s n_table_32k[] = {
	{0,      4096},
	{25175,  4576},
	{25200,  4096},
	{27000,  4096},
	{27027,  4096},
	{31500,  4096},
	{33750,  4096},
	{54000,  4096},
	{54054,  4096},
	{67500,  4096},
	{74176,  11648},
	{74250,  4096},
	{928125, 8192},
	{148352, 11648},
	{148500, 4096},
	{185625, 4096},
	{296703, 5824},
	{297000, 3072},
	{371250, 6144},
	{5940000, 3072},
	{0, 0}
};

static struct dw_audio_n_table_s n_table_44_1k[] = {
	{0,      6272},
	{25175,  7007},
	{25200,  6272},
	{27000,  6272},
	{27027,  6272},
	{31500,  6272},
	{33750,  6272},
	{54000,  6272},
	{54054,  6272},
	{67500,  6272},
	{74176,  17836},
	{74250,  6272},
	{92812,  6272},
	{148352, 8918},
	{148500, 6272},
	{185625, 6272},
	{296703, 4459},
	{297000, 4704},
	{371250, 4704},
	{594000, 9048},
	{0, 0}
};

static struct dw_audio_n_table_s n_table_48k[] = {
	{0,      6144},
	{25175,  6864},
	{25200,  6144},
	{27000,  6144},
	{27027,  6144},
	{31500,  6144},
	{33750,  6144},
	{54000,  6144},
	{54054,  6144},
	{67500,  6144},
	{74176,  11648},
	{74250,  6144},
	{928125, 12288},
	{148352, 5824},
	{148500, 6144},
	{185625, 6144},
	{296703, 5824},
	{297000, 5120},
	{371250, 5120},
	{594000, 6144},
	{0, 0}
};

static struct dw_audio_s *dw_get_audio(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	return &hdmi->audio_dev;
}

static struct dw_video_s *dw_get_video(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	return &hdmi->video_dev;
}

static u32 _dw_audio_get_clock_n(void)
{
	u32 value = 0;

	value |= dw_read_mask(AUD_N1, AUD_N1_AUDN_MASK);
	value |= dw_read_mask(AUD_N2, AUD_N2_AUDN_MASK) << 8;
	value |= dw_read_mask(AUD_N3, AUD_N3_AUDN_MASK) << 16;

	audio_log("dw audio get acr n: %d\n", value);
	return value;
}

static u32 _dw_audio_get_clock_cts(void)
{
	u32 value = 0x0;

	value |= dw_read_mask(AUD_CTS1, AUD_CTS1_AUDCTS_MASK);
	value |= dw_read_mask(AUD_CTS2, AUD_CTS2_AUDCTS_MASK) << 8;
	value |= dw_read_mask(AUD_CTS3, AUD_CTS3_AUDCTS_MASK) << 16;

	audio_log("dw audio get acr cts: %d\n", value);
	return value;
}

static void _dw_audio_i2s_set_data_enable(u8 bits)
{
	dw_write_mask(AUD_CONF0, AUD_CONF0_I2S_IN_EN_MASK, bits);
}

static u16 _dw_audio_get_fs_factor(void)
{
	u8 value = dw_read_mask(AUD_INPUTCLKFS, AUD_INPUTCLKFS_IFSFACTOR_MASK);

	switch (value) {
	case 4:
		return 64;
	case 1:
		return 256;
	case 2:
		return 512;
	case 0:
	default:
		return 128;
	}
}

static int _dw_audio_i2s_config(struct dw_audio_s *audio)
{
	u8 fs_fac = 0;
	u8 sample_size = 24;
	u8 pcuv_mode    = 0x0;
	u8 hbr_select   = 0x0;
	u8 nplcm_select = 0x1;

	/* select i2s interface */
	dw_write_mask(AUD_CONF0, AUD_CONF0_I2S_SELECT_MASK, 0x1);

	/* i2s data off */
	_dw_audio_i2s_set_data_enable(0x0);

	if (audio->mCodingType == DW_AUD_CODING_PCM) {
		pcuv_mode    = 0x1;
		nplcm_select = 0x0;
	} else if ((audio->mCodingType == DW_AUD_CODING_DTS_HD) ||
			(audio->mCodingType == DW_AUD_CODING_MAT)) {
		hbr_select   = 0x1;
	}
	dw_write_mask(AUD_CONF2, AUD_CONF2_INSERT_PCUV_MASK, pcuv_mode);
	dw_write_mask(AUD_CONF2, AUD_CONF2_HBR_MASK, hbr_select);
	dw_write_mask(AUD_CONF2, AUD_CONF2_NLPCM_MASK, nplcm_select);

	/* config i2s sample size */
	if (audio->mCodingType == DW_AUD_CODING_PCM)
		sample_size = audio->mSampleSize;
	dw_write_mask(AUD_CONF1, AUD_CONF1_I2S_WIDTH_MASK, sample_size);

	dw_write_mask(AUD_CONF1, AUD_CONF1_I2S_MODE_MASK, 0x0);
	dw_write_mask(AUD_INT,
		AUD_INT_FIFO_FULL_MASK_MASK | AUD_INT_FIFO_EMPTY_MASK_MASK, 0x3);

	/* reset i2s fifo */
	dw_write_mask(AUD_CONF0, AUD_CONF0_SW_AUDIO_FIFO_RST_MASK, 0x1);

	dw_mc_reset_audio_i2s();

	switch (audio->mClockFsFactor) {
	case 64:
		fs_fac = 4;
		break;
	case 128:
		fs_fac = 0;
		break;
	case 256:
		fs_fac = 1;
		break;
	case 512:
		fs_fac = 2;
		break;
	default:
		hdmi_wrn("dw audio factor %d unsupport. use default 128 factor\n",
			audio->mClockFsFactor);
		fs_fac = 0;
		break;
	}
	dw_write_mask(AUD_INPUTCLKFS, AUD_INPUTCLKFS_IFSFACTOR_MASK, fs_fac);

	audio_log("dw audio i2s config done!\n");
	return 0;
}

static u32 _dw_audio_sw_compute_n(u32 freq, u32 pixel_clk)
{
	int i = 0;
	u32 n = 0;
	struct dw_audio_n_table_s *n_table = NULL;
	int multiplier_factor = 1;

	if ((freq == 64000) || (freq == 882000) || (freq == 96000))
		multiplier_factor = 2;
	else if ((freq == 128000) || (freq == 176400) || (freq == 192000))
		multiplier_factor = 4;
	else if ((freq == 256000) || (freq == 3528000) || (freq == 384000))
		multiplier_factor = 8;

	if (32000 == (freq / multiplier_factor)) {
		n_table = n_table_32k;
	} else if (44100 == (freq / multiplier_factor)) {
		n_table = n_table_44_1k;
	} else if (48000 == (freq / multiplier_factor)) {
		n_table = n_table_48k;
	} else {
		hdmi_err("get n param frequency %dhz, pixel clk %dkhz, factor %d is unvalid!\n",
			freq, pixel_clk, multiplier_factor);
		return false;
	}

	for (i = 0; n_table[i].n != 0; i++) {
		if (pixel_clk == n_table[i].pixel_clock) {
			n = n_table[i].n * multiplier_factor;
			audio_log("get acr n value = %d\n", n);
			return n;
		}
	}

	n = n_table[0].n * multiplier_factor;

	hdmi_wrn("use default acr n value = %d\n", n);

	return n;
}

static int _dw_audio_param_reset(struct dw_audio_s *audio)
{
	memset(audio, 0x0, sizeof(struct dw_audio_s));

	audio->mInterfaceType = DW_AUDIO_INTERFACE_I2S;
	audio->mCodingType = DW_AUD_CODING_PCM;
	audio->mSamplingFrequency = 44100;
	audio->mChannelAllocation = 0;
	audio->mChannelNum = 2;
	audio->mSampleSize = 16;
	audio->mClockFsFactor = 64;

	return 0;
}

static int _dw_audio_param_print(struct dw_audio_s *audio)
{
	if (!audio) {
		hdmi_err("%s param is null!!!\n", __func__);
		return -1;
	}
	audio_log("[audio info]\n");
	audio_log(" - interface type = %s\n",
		audio->mInterfaceType == DW_AUDIO_INTERFACE_I2S ? "I2S" :
		audio->mInterfaceType == DW_AUDIO_INTERFACE_SPDIF ? "SPDIF" :
		audio->mInterfaceType == DW_AUDIO_INTERFACE_HBR ? "HBR" :
		audio->mInterfaceType == DW_AUDIO_INTERFACE_GPA ? "GPA" :
		audio->mInterfaceType == DW_AUDIO_INTERFACE_DMA ? "DMA" : "---");

	audio_log(" - coding type = %s\n",
		audio->mCodingType == DW_AUD_CODING_PCM ? "PCM" :
		audio->mCodingType == DW_AUD_CODING_AC3 ? "AC3" :
		audio->mCodingType == DW_AUD_CODING_MPEG1 ? "MPEG1" :
		audio->mCodingType == DW_AUD_CODING_MP3 ? "MP3" :
		audio->mCodingType == DW_AUD_CODING_MPEG2 ? "MPEG2" :
		audio->mCodingType == DW_AUD_CODING_AAC ? "AAC" :
		audio->mCodingType == DW_AUD_CODING_DTS ? "DTS" :
		audio->mCodingType == DW_AUD_CODING_ATRAC ? "ATRAC" :
		audio->mCodingType == DW_AUD_CODING_ONE_BIT_AUDIO ? "ONE BIT AUDIO" :
		audio->mCodingType == DW_AUD_CODING_DOLBY_DIGITAL_PLUS ? "DOLBY DIGITAL +" :
		audio->mCodingType == DW_AUD_CODING_DTS_HD ? "DTS HD" :
		audio->mCodingType == DW_AUD_CODING_MAT ? "MAT" : "---");
	audio_log(" - frequency = %dHz\n", audio->mSamplingFrequency);
	audio_log(" - sample size = %d\n", audio->mSampleSize);
	audio_log(" - FS factor = %d\n", audio->mClockFsFactor);
	audio_log(" - ChannelAllocationr = %d\n", audio->mChannelAllocation);
	audio_log(" - mChannelNum = %d\n", audio->mChannelNum);

	return 0;
}

static u8 _dw_video_color_bits_to_hw(u8 bits)
{
	switch (bits) {
	case DW_COLOR_DEPTH_10:
		return DW_HW_COLOR_DEPTH_10;
	case DW_COLOR_DEPTH_12:
		return DW_HW_COLOR_DEPTH_12;
	case DW_COLOR_DEPTH_16:
		return DW_HW_COLOR_DEPTH_16;
	case DW_COLOR_DEPTH_8:
	default:
		return DW_HW_COLOR_DEPTH_8;
	}
}

static u8 _dw_video_get_color_bits(void)
{
	u32 value = dw_read_mask(VP_PR_CD, VP_PR_CD_COLOR_DEPTH_MASK);

	switch (value) {
	case DW_HW_COLOR_DEPTH_10:
		return DW_COLOR_DEPTH_10;
	case DW_HW_COLOR_DEPTH_12:
		return DW_COLOR_DEPTH_12;
	case DW_HW_COLOR_DEPTH_16:
		return DW_COLOR_DEPTH_16;
	case DW_HW_COLOR_DEPTH_8:
	default:
		return DW_COLOR_DEPTH_8;
		break;
	}
}

/**
 * @return true if if video has pixel repetition
 */
static int _dw_video_is_pixel_repet(void)
{
	struct dw_video_s *video = dw_get_video();

	return (video->mPixelRepetitionFactor > 0) ||
		(video->mDtd.mPixelRepetitionInput > 0);
}

/**
 * @return true if color space decimation is needed
 */
static int _dw_video_csc_check_decimation(void)
{
	struct dw_video_s *video = dw_get_video();

	if (video->mEncodingOut != DW_COLOR_FORMAT_YCC422)
		return 0;

	return (video->mEncodingIn == DW_COLOR_FORMAT_RGB ||
		video->mEncodingIn == DW_COLOR_FORMAT_YCC444);
}

/**
 * @return true if if video is interpolated
 */
static int _dw_video_csc_check_interpolation(void)
{
	struct dw_video_s *video = dw_get_video();

	if (video->mEncodingIn != DW_COLOR_FORMAT_YCC422)
		return 0;

	return (video->mEncodingOut == DW_COLOR_FORMAT_RGB ||
		video->mEncodingOut == DW_COLOR_FORMAT_YCC444);
}

/**
 * Set up color space converter to video requirements
 * (if there is any encoding type conversion or csc coefficients)
 * @param baseAddr Base Address of module
 * @param params VideoParams
 * @return true if successful
 */
static int _dw_video_csc_config(struct dw_video_s *video)
{
	u8 color_depth = 0;
	u16 interpolation = 0, decimation = 0;

	if (_dw_video_csc_check_interpolation()) {
		if (video->mCscFilter > 1) {
			hdmi_err("invalid chroma interpolation filter: %d\n", video->mCscFilter);
			goto failed_exit;
		}
		interpolation = 1 + video->mCscFilter;
	} else if (_dw_video_csc_check_decimation()) {
		if (video->mCscFilter > 2) {
			hdmi_err("invalid chroma decimation filter: %d\n", video->mCscFilter);
			goto failed_exit;
		}
		decimation = 1 + video->mCscFilter;
	}

	color_depth = _dw_video_color_bits_to_hw(video->mColorResolution);

	video_log("dw video csc check interpolation: %d, decimation: %d, %dbit\n",
		interpolation, decimation, video->mColorResolution);

	dw_write_mask(CSC_CFG, CSC_CFG_INTMODE_MASK, interpolation);
	dw_write_mask(CSC_CFG, CSC_CFG_DECMODE_MASK, decimation);

	dw_write(CSC_COEF_A1_LSB, (u8)(video->mCscA[0]));
	dw_write_mask(CSC_COEF_A1_MSB,
			CSC_COEF_A1_MSB_CSC_COEF_A1_MSB_MASK, (u8)(video->mCscA[0] >> 8));

	dw_write(CSC_COEF_A2_LSB, (u8)(video->mCscA[1]));
	dw_write_mask(CSC_COEF_A2_MSB,
			CSC_COEF_A2_MSB_CSC_COEF_A2_MSB_MASK, (u8)(video->mCscA[1] >> 8));

	dw_write(CSC_COEF_A3_LSB, (u8)(video->mCscA[2]));
	dw_write_mask(CSC_COEF_A3_MSB,
		CSC_COEF_A3_MSB_CSC_COEF_A3_MSB_MASK, (u8)(video->mCscA[2] >> 8));

	dw_write(CSC_COEF_A4_LSB, (u8)(video->mCscA[3]));
	dw_write_mask(CSC_COEF_A4_MSB,
		CSC_COEF_A4_MSB_CSC_COEF_A4_MSB_MASK, (u8)(video->mCscA[3] >> 8));

	dw_write(CSC_COEF_B1_LSB, (u8)(video->mCscB[0]));
	dw_write_mask(CSC_COEF_B1_MSB,
		CSC_COEF_B1_MSB_CSC_COEF_B1_MSB_MASK, (u8)(video->mCscB[0] >> 8));

	dw_write(CSC_COEF_B2_LSB, (u8)(video->mCscB[1]));
	dw_write_mask(CSC_COEF_B2_MSB,
			CSC_COEF_B2_MSB_CSC_COEF_B2_MSB_MASK, (u8)(video->mCscB[1] >> 8));

	dw_write(CSC_COEF_B3_LSB, (u8)(video->mCscB[2]));
	dw_write_mask(CSC_COEF_B3_MSB,
		CSC_COEF_B3_MSB_CSC_COEF_B3_MSB_MASK, (u8)(video->mCscB[2] >> 8));

	dw_write(CSC_COEF_B4_LSB, (u8)(video->mCscB[3]));
	dw_write_mask(CSC_COEF_B4_MSB,
		CSC_COEF_B4_MSB_CSC_COEF_B4_MSB_MASK, (u8)(video->mCscB[3] >> 8));

	dw_write(CSC_COEF_C1_LSB, (u8) (video->mCscC[0]));
	dw_write_mask(CSC_COEF_C1_MSB,
		CSC_COEF_C1_MSB_CSC_COEF_C1_MSB_MASK, (u8)(video->mCscC[0] >> 8));

	dw_write(CSC_COEF_C2_LSB, (u8) (video->mCscC[1]));
	dw_write_mask(CSC_COEF_C2_MSB,
			CSC_COEF_C2_MSB_CSC_COEF_C2_MSB_MASK, (u8)(video->mCscC[1] >> 8));

	dw_write(CSC_COEF_C3_LSB, (u8) (video->mCscC[2]));
	dw_write_mask(CSC_COEF_C3_MSB,
		CSC_COEF_C3_MSB_CSC_COEF_C3_MSB_MASK, (u8)(video->mCscC[2] >> 8));

	dw_write(CSC_COEF_C4_LSB, (u8) (video->mCscC[3]));
	dw_write_mask(CSC_COEF_C4_MSB,
			CSC_COEF_C4_MSB_CSC_COEF_C4_MSB_MASK, (u8)(video->mCscC[3] >> 8));

	dw_write_mask(CSC_SCALE, CSC_SCALE_CSCSCALE_MASK, video->mCscScale);
	dw_write_mask(CSC_SCALE, CSC_SCALE_CSC_COLOR_DEPTH_MASK, color_depth);

	return 0;

failed_exit:
	return -1;
}

/**
 * Set up video packetizer which "packetizes" pixel transmission
 * (in deep colour mode, YCC422 mapping and pixel repetition)
 * @param params VideoParams
 * @return true if successful
 */
static int _dw_video_path_config(struct dw_video_s *video)
{
	u8 color_depth = 0, remap_size = 0, output_select = 0;
	char *data_path[] = {"pixel packing", "ycc422 remap", "bypass", "bypass"};

	if (video->mEncodingOut != DW_COLOR_FORMAT_YCC422) {
		color_depth = _dw_video_color_bits_to_hw(video->mColorResolution);
		if (color_depth == DW_HW_COLOR_DEPTH_8)
			output_select = 3;
	} else {
		if ((video->mColorResolution == DW_COLOR_DEPTH_8) ||
				(video->mColorResolution == 0))
			remap_size = 0;
		else if (video->mColorResolution == DW_COLOR_DEPTH_10)
			remap_size = 1;
		else if (video->mColorResolution == DW_COLOR_DEPTH_12)
			remap_size = 2;
		else {
			hdmi_err("invalid color remap size: %d\n", video->mColorResolution);
			goto failed_exit;
		}
		output_select = 1;
	}

	/* desired factor */
	dw_write_mask(VP_PR_CD, VP_PR_CD_DESIRED_PR_FACTOR_MASK,
			video->mPixelRepetitionFactor);
	/* enable stuffing */
	dw_write_mask(VP_STUFF, VP_STUFF_PR_STUFFING_MASK, 1);
	/* enable block */
	dw_write_mask(VP_CONF, VP_CONF_PR_EN_MASK,
			(video->mPixelRepetitionFactor > 1) ? 1 : 0);
	/* bypass block */
	dw_write_mask(VP_CONF, VP_CONF_BYPASS_SELECT_MASK,
			(video->mPixelRepetitionFactor > 1) ? 0 : 1);

	dw_write_mask(VP_PR_CD, VP_PR_CD_COLOR_DEPTH_MASK, color_depth);

	dw_write_mask(VP_STUFF, VP_STUFF_IDEFAULT_PHASE_MASK,
		video->mPixelPackingDefaultPhase);

	dw_write_mask(VP_REMAP, VP_REMAP_YCC422_SIZE_MASK, remap_size);

	video_log("dw video path output select: %s\n", data_path[output_select]);
	if (output_select == 0) {
		/* pixel packing */
		dw_write_mask(VP_CONF, VP_CONF_BYPASS_EN_MASK, 0);
		dw_write_mask(VP_CONF, VP_CONF_PP_EN_MASK, 1);
		dw_write_mask(VP_CONF, VP_CONF_YCC422_EN_MASK, 0);
	} else if (output_select == 1) {
		/* YCC422 */
		dw_write_mask(VP_CONF, VP_CONF_BYPASS_EN_MASK, 0);
		dw_write_mask(VP_CONF, VP_CONF_PP_EN_MASK, 0);
		dw_write_mask(VP_CONF, VP_CONF_YCC422_EN_MASK, 1);
	} else if (output_select == 2 || output_select == 3) {
		/* bypass */
		dw_write_mask(VP_CONF, VP_CONF_BYPASS_EN_MASK, 1);
		dw_write_mask(VP_CONF, VP_CONF_PP_EN_MASK, 0);
		dw_write_mask(VP_CONF, VP_CONF_YCC422_EN_MASK, 0);
	} else {
		hdmi_err("video path output select %d invalid.\n", output_select);
		goto failed_exit;
	}

	/* YCC422 stuffing */
	dw_write_mask(VP_STUFF, VP_STUFF_YCC422_STUFFING_MASK, 1);
	/* pixel packing stuffing */
	dw_write_mask(VP_STUFF, VP_STUFF_PP_STUFFING_MASK, 1);
	/* output selector */
	dw_write_mask(VP_CONF, VP_CONF_OUTPUT_SELECTOR_MASK, output_select);

	return 0;

failed_exit:
	return -1;
}

/**
 * Set up video mapping and stuffing
 * @video: VideoParams
 * @return true if successful
 */
static int _dw_video_sampler(struct dw_video_s *video)
{
	u8 map_code = 0;

	if (video->mEncodingIn == DW_COLOR_FORMAT_RGB) {
		if ((video->mColorResolution == DW_COLOR_DEPTH_8) ||
				(video->mColorResolution == 0))
			map_code = 1;
		else if (video->mColorResolution == DW_COLOR_DEPTH_10)
			map_code = 3;
		else if (video->mColorResolution == DW_COLOR_DEPTH_12)
			map_code = 5;
		else if (video->mColorResolution == DW_COLOR_DEPTH_16)
			map_code = 7;
		else
			goto failed_exit;
	} else if (video->mEncodingIn == DW_COLOR_FORMAT_YCC422) {
		/* YCC422 mapping is discontinued - only map 1 is supported */
		if (video->mColorResolution == DW_COLOR_DEPTH_8)
			map_code = 22;
		else if (video->mColorResolution == DW_COLOR_DEPTH_10)
			map_code = 20;
		else if ((video->mColorResolution == DW_COLOR_DEPTH_12) ||
			(video->mColorResolution == 0))
			map_code = 18;
		else
			goto failed_exit;
	} else if (video->mEncodingIn == DW_COLOR_FORMAT_YCC444) {
		if (video->mColorResolution == DW_COLOR_DEPTH_8)
			map_code = 23;
		else if (video->mColorResolution == DW_COLOR_DEPTH_10)
			map_code = 24;
		else if ((video->mColorResolution == DW_COLOR_DEPTH_12) ||
			(video->mColorResolution == 0))
			map_code = 27;
		else
			goto failed_exit;
	} else if (video->mEncodingIn == DW_COLOR_FORMAT_YCC420) {
		if (video->mColorResolution == DW_COLOR_DEPTH_8)
			map_code = 9;
		else if (video->mColorResolution == DW_COLOR_DEPTH_10)
			map_code = 11;
		else if ((video->mColorResolution == DW_COLOR_DEPTH_12) ||
			(video->mColorResolution == 0))
			map_code = 13;
		else
			goto failed_exit;
	} else
		goto failed_exit;

	dw_write_mask(TX_INVID0, TX_INVID0_INTERNAL_DE_GENERATOR_MASK, 0x0);

	dw_write_mask(TX_INVID0, TX_INVID0_VIDEO_MAPPING_MASK, map_code);

	dw_write(TX_GYDATA0, 0x0);
	dw_write(TX_GYDATA1, 0x0);
	dw_write_mask(TX_INSTUFFING, TX_INSTUFFING_GYDATA_STUFFING_MASK, 1);


	dw_write(TX_RCRDATA0, 0x0);
	dw_write(TX_RCRDATA1, 0x0);
	dw_write_mask(TX_INSTUFFING, TX_INSTUFFING_RCRDATA_STUFFING_MASK, 1);

	dw_write(TX_BCBDATA0, 0x0);
	dw_write(TX_BCBDATA1, 0x0);
	dw_write_mask(TX_INSTUFFING, TX_INSTUFFING_BCBDATA_STUFFING_MASK, 1);

	return 0;

failed_exit:
	hdmi_err("video sampler failed when format: %d - %dbits\n",
			video->mEncodingIn, video->mColorResolution);
	return -1;
}

/**
 * @desc: Configures the video blocks to do any video processing and to
 * transmit the video set up required by the user, allowing to
 * force video pixels (from the DEBUG pixels) to be transmitted
 * rather than the video stream being received.
 * @return 0 - success
 *        -1 - failed
 */
static int _dw_video_on(void)
{
	struct dw_video_s *video = dw_get_video();
	int ret = 0;

	/* dvi mode does not support pixel repetition */
	if ((video->mHdmi == DW_TMDS_MODE_DVI) && _dw_video_is_pixel_repet()) {
		hdmi_err("dvi mode but check pixel is repet!\n");
		return -1;
	}

	ret = dw_fc_video_config(video);
	if (ret != 0)
		goto failed_exit;

	ret = _dw_video_path_config(video);
	if (ret != 0)
		goto failed_exit;

	ret = _dw_video_csc_config(video);
	if (ret != 0)
		goto failed_exit;

	ret = _dw_video_sampler(video);
	if (ret != 0)
		goto failed_exit;

	video_log("dw video config done!\n");
	return 0;

failed_exit:
	hdmi_err("dw video config failed\n");
	return -1;
}

void dw_audio_set_clock_cts(u32 value)
{
	if (value > 0) {
		/* 19-bit width */
		dw_write_mask(AUD_CTS1, AUD_CTS1_AUDCTS_MASK, dw_to_byte(value, 0));
		dw_write_mask(AUD_CTS2, AUD_CTS2_AUDCTS_MASK, dw_to_byte(value, 1));
		dw_write_mask(AUD_CTS3, AUD_CTS3_AUDCTS_MASK, dw_to_byte(value, 2));
		dw_write_mask(AUD_CTS3, AUD_CTS3_CTS_MANUAL_MASK, 1);
		return;
	}

	/* Set to automatic generation of CTS values */
	dw_write_mask(AUD_CTS3, AUD_CTS3_CTS_MANUAL_MASK, 0);
}

int dw_audio_set_info(void *data)
{
	struct dw_audio_s *info = (struct dw_audio_s *)data;
	struct dw_audio_s *audio = dw_get_audio();
	int ret = 0;

	ret = _dw_audio_param_reset(audio);
	if (ret != 0) {
		hdmi_err("dw audio param reset failed\n");
		return -1;
	}

	audio->mInterfaceType = (info->mInterfaceType < DW_AUDIO_INTERFACE_HBR) ?
			DW_AUDIO_INTERFACE_I2S : info->mInterfaceType;
	audio->mCodingType = (info->mCodingType < DW_AUD_CODING_PCM) ?
			DW_AUD_CODING_PCM : info->mCodingType;

	audio->mSamplingFrequency = info->mSamplingFrequency;
	audio->mChannelAllocation = info->mChannelAllocation;
	audio->mChannelNum        = info->mChannelNum;
	audio->mSampleSize        = info->mSampleSize;
	audio->mClockFsFactor     = info->mClockFsFactor;

	_dw_audio_param_print(audio);
	return 0;
}

int dw_audio_init(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_audio_s *audio  = &hdmi->audio_dev;
	int ret = 0;

	if (!audio) {
		hdmi_err("check point audio is null\n");
		return -1;
	}

	ret = _dw_audio_param_reset(audio);
	if (ret != 0) {
		hdmi_err("dw audio params reset failed\n");
		return -1;
	}

	return 0;
}

int dw_audio_on(void)
{
	int ret = 0;
	u32 n = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_audio_s *audio = &hdmi->audio_dev;

	_dw_audio_param_print(audio);

	ret = dw_hdmi_ctrl_update();
	if (ret != 0) {
		hdmi_err("dw hdmi update control param failed\n");
		return -1;
	}

	if (!hdmi->hdmi_on) {
		hdmi_inf("dw audio not config when dvi mode\n");
		return 0;
	}

	if (!hdmi->audio_on) {
		hdmi_inf("dw audio control is off\n");
		return 0;
	}

	if ((audio->mCodingType == DW_AUD_CODING_ONE_BIT_AUDIO) ||
			(audio->mCodingType == DW_AUD_CODING_DST)) {
		hdmi_wrn("dw audio not support this coding type: %d\n",
			audio->mCodingType);
		return -1;
	}

	if ((hdmi->pixel_clk < 74250) &&
			((audio->mChannelNum * audio->mSamplingFrequency) > 384000)) {
		hdmi_wrn("dw audio not support this audio freq on this video format\n");
		return -1;
	}

	if (audio->mInterfaceType != DW_AUDIO_INTERFACE_I2S) {
		hdmi_err("dw audio unsupport this interface type: %d\n",
			audio->mInterfaceType);
		return -1;
	}

	/* clear audio fifo interrupt state */
	dw_write_mask(AUD_INT, AUD_INT_FIFO_FULL_MASK_MASK, 0x1);
	dw_write_mask(AUD_INT, AUD_INT_FIFO_EMPTY_MASK_MASK, 0x1);
	dw_write_mask(AUD_SPDIFINT, AUD_SPDIFINT_SPDIF_FIFO_FULL_MASK_MASK, 0x1);
	dw_write_mask(AUD_SPDIFINT, AUD_SPDIFINT_SPDIF_FIFO_EMPTY_MASK_MASK, 0x1);

	/* set audio mute */
	dw_fc_audio_set_mute(1);

	/* Configure Frame Composer audio parameters */
	dw_fc_audio_sample_config(audio);

	audio->mClockFsFactor = 64;
	ret = _dw_audio_i2s_config(audio);
	if (ret != 0) {
		hdmi_err("audio i2s config is failed!!!\n");
		return -1;
	}

	n = _dw_audio_sw_compute_n(audio->mSamplingFrequency, hdmi->tmds_clk);

	dw_write_mask(AUD_N1, AUD_N1_AUDN_MASK, (u8)(n >> 0));
	dw_write_mask(AUD_N2, AUD_N2_AUDN_MASK, (u8)(n >> 8));
	dw_write_mask(AUD_N3, AUD_N3_AUDN_MASK, (u8)(n >> 16));
	dw_write_mask(AUD_CTS3, AUD_CTS3_N_SHIFT_MASK, 0x0);

	dw_write_mask(AUD_CTS3, AUD_CTS3_CTS_MANUAL_MASK, 0x0);

	if (!(audio->mChannelNum % 2))
		_dw_audio_i2s_set_data_enable((1 << (audio->mChannelNum / 2)) - 1);
	else
		_dw_audio_i2s_set_data_enable((1 << ((audio->mChannelNum + 1) / 2)) - 1);

	/* Configure audio info frame packets */
	dw_fc_audio_packet_config(audio);

	dw_mc_disable_audio_sampler_clock(0x0);

	dw_fc_audio_set_mute(0);

	audio_log("dw audio config done!\n");
	return 0;
}

int dw_video_filling_timing(dw_dtd_t *dtd, u32 rate)
{
	struct dw_video_s *video = dw_get_video();

	if (!video) {
		hdmi_err("check point video is null\n");
		goto ret_failed;
	}

	if (!dtd) {
		hdmi_err("check point dtd is null\n");
		goto ret_failed;
	}

	memcpy(&video->mDtd, dtd, sizeof(dw_dtd_t));
	video->rate = rate;

	hdmi_trace("[filling timing]\n");
	hdmi_trace(" - pixel clock: %dKHz, repeat: %d, %s, ratio: %dx%d\n",
		video->mDtd.mPixelClock, video->mDtd.mPixelRepetitionInput,
		video->mDtd.mInterlaced ? "interlaced" : "progressive",
		video->mDtd.mHImageSize, video->mDtd.mVImageSize);
	hdmi_trace(" - vic code: %d, framerate: %dHz\n",
		video->mDtd.mCode, video->rate);
	hdmi_trace(" - hactive: %d, hblank: %d, hfront: %d, hsync: %d, hpol: %d\n",
		video->mDtd.mHActive, video->mDtd.mHBlanking, video->mDtd.mHSyncOffset,
		video->mDtd.mHSyncPulseWidth, video->mDtd.mHSyncPolarity);
	hdmi_trace(" - vactive: %d, vblank: %d, vfront: %d, vsync: %d, vpol: %d\n",
		video->mDtd.mVActive, video->mDtd.mVBlanking, video->mDtd.mVSyncOffset,
		video->mDtd.mVSyncPulseWidth, video->mDtd.mVSyncPolarity);

	return 0;

ret_failed:
	return -1;
}

u32 dw_video_get_pixel_clk(void)
{
	struct dw_video_s *video = dw_get_video();

	if (video->mHdmiVideoFormat == DW_VIDEO_FORMAT_3D) {
		if (video->m3dStructure == HDMI_3D_STRUCTURE_FRAME_PACKING)
			return 2 * video->mDtd.mPixelClock;
	}

	return video->mDtd.mPixelClock;
}

u32 dw_video_get_cea_vic(void)
{
	struct dw_video_s *video = dw_get_video();

	return video->mDtd.mCode;
}

int dw_video_cea_to_hdmi_vic(int cea_vic)
{
	switch (cea_vic) {
	case 95:
		return 1;
	case 94:
		return 2;
	case 93:
		return 3;
	case 98:
		return 4;
	default:
		return -1;
	}
}

int dw_video_use_hdmi14_vsif(u8 format, u8 data_pb5)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_product_s	 *prod = &hdmi->prod_dev;

	prod->mOUI = HDMI_IEEE_OUI;
	prod->mVendorPayload[0] = format << 5;
	prod->mVendorPayload[1] = data_pb5;
	prod->mVendorPayload[2] = 0;
	prod->mVendorPayload[3] = 0;
	prod->mVendorPayloadLength = 4;

	return 0;
}

int dw_video_use_hdmi20_vsif(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_product_s	 *prod = &hdmi->prod_dev;

	if (!hdmi->video_dev.mHdmi20) {
		return -1;
	}

	prod->mOUI = HDMI_FORUM_IEEE_OUI;
	prod->mVendorPayload[0] = 0x1;
	prod->mVendorPayload[1] = 0;
	prod->mVendorPayload[2] = 0;
	prod->mVendorPayload[3] = 0;
	prod->mVendorPayloadLength = 4;

	return 0;
}

int dw_video_set_vic_format(enum dw_video_format_e type, u32 code)
{
	struct dw_video_s *video = dw_get_video();

	video->mHdmiVideoFormat = type;
	video->m3dStructure = 0x0;

	switch (type) {
	case DW_VIDEO_FORMAT_3D:
		video->mCea_code  = code;
		video->mHdmi_code = 0x0;
		break;
	case DW_VIDEO_FORMAT_HDMI14_4K:
		video->mCea_code  = 0x0;
		video->mHdmi_code = code;
		break;
	case DW_VIDEO_FORMAT_NONE:
	default:
		video->mCea_code  = code;
		video->mHdmi_code = 0x0;
		break;
	}

	return 0;
}

void dw_video_csc_update_coefficients(struct dw_video_s *video)
{
	u16 i = 0;
	u8 use_csc = 0x0;

	if (video->mEncodingIn != video->mEncodingOut)
		use_csc = 0x1;

	if (!use_csc) {
		for (i = 0; i < 4; i++) {
			video->mCscA[i] = 0;
			video->mCscB[i] = 0;
			video->mCscC[i] = 0;
		}
		video->mCscA[0] = 0x2000;
		video->mCscB[1] = 0x2000;
		video->mCscC[2] = 0x2000;
		video->mCscScale = 1;
	} else if (use_csc &&
			video->mEncodingOut == DW_COLOR_FORMAT_RGB) {
		if (video->mColorimetry == DW_METRY_ITU601) {
			video->mCscA[0] = 0x2000;
			video->mCscA[1] = 0x6926;
			video->mCscA[2] = 0x74fd;
			video->mCscA[3] = 0x010e;

			video->mCscB[0] = 0x2000;
			video->mCscB[1] = 0x2cdd;
			video->mCscB[2] = 0x0000;
			video->mCscB[3] = 0x7e9a;

			video->mCscC[0] = 0x2000;
			video->mCscC[1] = 0x0000;
			video->mCscC[2] = 0x38b4;
			video->mCscC[3] = 0x7e3b;

			video->mCscScale = 1;
		} else if (video->mColorimetry == DW_METRY_ITU709) {
			video->mCscA[0] = 0x2000;
			video->mCscA[1] = 0x7106;
			video->mCscA[2] = 0x7a02;
			video->mCscA[3] = 0x00a7;

			video->mCscB[0] = 0x2000;
			video->mCscB[1] = 0x3264;
			video->mCscB[2] = 0x0000;
			video->mCscB[3] = 0x7e6d;

			video->mCscC[0] = 0x2000;
			video->mCscC[1] = 0x0000;
			video->mCscC[2] = 0x3b61;
			video->mCscC[3] = 0x7e25;

			video->mCscScale = 1;
		}
	} else if (use_csc &&
		video->mEncodingIn == DW_COLOR_FORMAT_RGB) {
		if (video->mColorimetry == DW_METRY_ITU601) {
			video->mCscA[0] = 0x2591;
			video->mCscA[1] = 0x1322;
			video->mCscA[2] = 0x074b;
			video->mCscA[3] = 0x0000;

			video->mCscB[0] = 0x6535;
			video->mCscB[1] = 0x2000;
			video->mCscB[2] = 0x7acc;
			video->mCscB[3] = 0x0200;

			video->mCscC[0] = 0x6acd;
			video->mCscC[1] = 0x7534;
			video->mCscC[2] = 0x2000;
			video->mCscC[3] = 0x0200;

			video->mCscScale = 0;
		} else if (video->mColorimetry == DW_METRY_ITU709) {
			video->mCscA[0] = 0x2dc5;
			video->mCscA[1] = 0x0d9b;
			video->mCscA[2] = 0x049e;
			video->mCscA[3] = 0x0000;

			video->mCscB[0] = 0x62f0;
			video->mCscB[1] = 0x2000;
			video->mCscB[2] = 0x7d11;
			video->mCscB[3] = 0x0200;

			video->mCscC[0] = 0x6756;
			video->mCscC[1] = 0x78ab;
			video->mCscC[2] = 0x2000;
			video->mCscC[3] = 0x0200;

			video->mCscScale = 0;
		}
	}
}

int dw_video_update_color_format(dw_color_format_t format)
{
	struct dw_video_s *video_dev = dw_get_video();

#ifdef SUNXI_HDMI20_USE_CSC
	if (format == DW_COLOR_FORMAT_YCC422) {
		video_dev->mEncodingIn  = DW_COLOR_FORMAT_YCC444;
		video_dev->mEncodingOut = DW_COLOR_FORMAT_YCC422;
		hdmi_inf("dw video use csc: yuv444 to yuv422\n");
		return 0;
	}
#endif

	video_dev->mEncodingIn  = format;
	video_dev->mEncodingOut = format;
	hdmi_trace("dw video update format: %d\n", format);
	return 0;
}

int dw_video_update_color_depth(u8 bits)
{
	struct dw_video_s *video_dev = dw_get_video();

	video_dev->mColorResolution = bits;
	hdmi_trace("dw video update depth: %d\n", bits);
	return 0;
}

int dw_video_update_color_metry(u8 metry, u8 ext_metry)
{
	struct dw_video_s *video_dev = dw_get_video();

	video_dev->mColorimetry = metry;
	video_dev->mExtColorimetry = ext_metry;
	return 0;
}

int dw_video_update_hdr_eotf(u8 hdr, u8 eotf)
{
	struct dw_video_s *video_dev = dw_get_video();

	if (video_dev->pb) {
		kfree(video_dev->pb);
		video_dev->pb = NULL;
	}

	video_dev->pb = kmalloc(sizeof(dw_fc_drm_pb_t), GFP_KERNEL);
	if (!video_dev->pb) {
		hdmi_err("malloc drm pb failed\n");
		return -1;
	}

	memset(video_dev->pb, 0x0, sizeof(dw_fc_drm_pb_t));

	video_dev->mHdr = hdr;
	video_dev->pb->eotf = eotf;

	if (hdr) {
		dw_drm_packet_filling_data(video_dev->pb);
	}

	return 0;
}

int dw_video_update_tmds_mode(u8 mode)
{
	struct dw_video_s *video_dev = dw_get_video();

	video_dev->mHdmi = mode;
	return 0;
}

int dw_video_update_range(u8 range)
{
	struct dw_video_s *video_dev = dw_get_video();

	video_dev->mRgbQuantizationRange = range;
	return 0;
}

int dw_video_update_scaninfo(u8 scan)
{
	struct dw_video_s *video_dev = dw_get_video();

	video_dev->mScanInfo = scan;
	return 0;
}

int dw_video_update_ratio(u8 ratio)
{
	struct dw_video_s *video_dev = dw_get_video();

	video_dev->mActiveFormatAspectRatio = ratio;
	return 0;
}

int dw_video_dump_disp_info(void)
{
	struct dw_video_s *video = dw_get_video();
	char *color_format[] = {"RGB", "YUV422", "YUV444", "YUV420"};
	char *color_range[] = {"default", "limit", "full"};
	char *color_eotf[] = {"sdr", "hdr", "smpte2084", "hlg"};
	char *scan_info[] = {"no-data", "overscan", "underscan"};
	char *color_merty[] = {"none", "bt601", "bt709", "extern"};

	hdmi_trace("[disp info]\n");
	hdmi_trace(" - %s mode, encoding-in: %s, encding-out: %s, %dbits\n",
		video->mHdmi == DW_TMDS_MODE_HDMI ? "hdmi" : "dvi",
		color_format[video->mEncodingIn], color_format[video->mEncodingOut],
		video->mColorResolution);
	hdmi_trace(" - %s mode, eotf: %s, %s-range, %s, color metry: %s\n",
		video->mHdr ? "hdr" : "sdr", color_eotf[video->pb->eotf],
		color_range[video->mRgbQuantizationRange], scan_info[video->mScanInfo],
		color_merty[video->mColorimetry]);
	if (video->mColorimetry == DW_METRY_EXTENDED)
		hdmi_trace("   - extern color metry: %s\n",
			video->mExtColorimetry == DW_METRY_EXT_BT2020_Y_CB_CR ?
				"BT2020_YCBCR" : "unsupport");
	return 0;
}

ssize_t dw_avp_dump(char *buf)
{
	int n = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_video_s *video = &hdmi->video_dev;
	char *color_format[] = {"RGB", "YUV422", "YUV444", "YUV420"};
	u32 state = 0;
	u32 hactive = dw_fc_video_get_hactive();

	if (dw_avi_get_rgb_ycc() == DW_COLOR_FORMAT_YCC420)
		hactive = hactive * 2;

	n += sprintf(buf + n, "[dw video]\n");
	n += sprintf(buf + n, " - %d*%d@%dHz %s_%dbits\n",
		hactive, dw_fc_video_get_vactive(),
		video->rate, color_format[dw_avi_get_rgb_ycc()],
		_dw_video_get_color_bits());
	n += sprintf(buf + n, " - tmds clock: [%dKhz], pixel clock: [%dkhz]\n",
		hdmi->tmds_clk, hdmi->pixel_clk);
	n += sprintf(buf + n, " - avmute: [%s], scramble: [%s], tmds_mode: [%s], vic: [%d]\n",
		dw_gcp_get_avmute() ? "mute" : "unmute",
		dw_fc_video_get_scramble() ? "enable" : "disable",
		dw_fc_video_get_tmds_mode() ? "hdmi" : "dvi", dw_avi_get_video_code());

	n += sprintf(buf + n, "[dw audio]\n");
	n += sprintf(buf + n, " - acr_n: [%d], acr_cts: [%d], cts_mode: [%s]\n",
		_dw_audio_get_clock_n(), _dw_audio_get_clock_cts(),  \
		dw_read_mask(AUD_CTS3, AUD_CTS3_CTS_MANUAL_MASK) ? "user" : "auto");
	n += sprintf(buf + n, " - fs: [%dHz], factor: [%dxFs], mute: [%s], ",
		dw_fc_audio_get_sample_freq(), _dw_audio_get_fs_factor(), \
		dw_fc_audio_get_mute() ? "on" : "off");

	state = dw_mc_irq_get_state(DW_IRQ_AUDIO_SAMPLER);
	if (state == 0) {
		n += sprintf(buf + n, "audio_fifo: [ok]\n");
	} else {
		n += sprintf(buf + n, "audio_fifo: ");
		if (state & IH_AS_STAT0_AUD_FIFO_OVERFLOW_MASK)
			n += sprintf(buf + n, "[over flow]");
		if (state & IH_AS_STAT0_AUD_FIFO_UNDERFLOW_MASK)
			n += sprintf(buf + n, "[under flow]");
		if (state & IH_AS_STAT0_FIFO_OVERRUN_MASK)
			n += sprintf(buf + n, "[over run]");
		if (state & IH_AS_STAT0_FIFO_UNDERRUN_MASK)
			n += sprintf(buf + n, "[under run]");
		n += sprintf(buf + n, "\n");
	}

	return n;
}

int dw_avp_set_mute(u8 enable)
{
	dw_gcp_set_avmute(enable);
	dw_hdcp_set_avmute_state(enable);
	return 0;
}

int dw_avp_config_scramble(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	bool setup = false;

	if (hdmi->tmds_clk < 340000)
		goto config_flow; /* not need scramble */

	if (!dw_edid_check_scdc_support()) {
		hdmi_err("sink unsupport config scramble and 1/40 ratio\n");
		return 0;
	}

	/* need scramble */
	setup = true;

config_flow:
	/* set sink scdc info */
	dw_hdmi_scdc_set_scramble(setup);
	/* tmds software reset */
	dw_mc_reset_tmds_clock(setup);
	/* enable scramble */
	dw_fc_video_set_scramble(setup);
	dw_fc_video_set_hdcp_keepout(setup);

	video_log("dw video %s scramble.\n", setup ? "enable" : "disable");
	return 0;
}

int dw_avp_config(void)
{
	int ret = 0;
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();

	dw_avp_set_mute(0x1);
	dw_mc_set_main_irq(0x0);
	dw_fc_video_force_value(0x1, 0x0);

	if (hdmi->phy_ext->phy_disconfig)
		hdmi->phy_ext->phy_disconfig();

	ret = dw_hdmi_ctrl_update();
	if (ret != 0) {
		hdmi_err("dw hdmi update control param failed\n");
		return -1;
	}

	ret = _dw_video_on();
	if (ret != 0) {
		hdmi_err("dw video path config failed\n");
		return -1;
	}

	ret = dw_audio_on();
	if (ret != 0) {
		hdmi_err("dw audio path config failed\n");
	}

	/* config hdmi infoframe packet */
	ret = dw_infoframe_packet();
	if (ret != 0)
		hdmi_err("dw infoframe packet failed\n");

	dw_mc_all_clock_enable();

	/* config scramble */
	dw_avp_config_scramble();

	if (hdmi->phy_ext->phy_config)
		hdmi->phy_ext->phy_config();

	dw_fc_video_force_value(0x0, 0x0);
	dw_mc_irq_mask_all();
	dw_mc_set_main_irq(0x1);

	mdelay(50);

	dw_avp_set_mute(0x0);

	hdmi_trace("dw avp config done\n");
	return 0;
}
