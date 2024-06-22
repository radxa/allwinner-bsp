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

#include <linux/of_address.h>
#include "dw_i2cm.h"
#include "dw_edid.h"

#define TAG_BASE_BLOCK         0x00
#define TAG_CEA_EXT            0x02
#define TAG_VTB_EXT            0x10
#define TAG_DI_EXT             0x40
#define TAG_LS_EXT             0x50
#define TAG_MI_EXT             0x60

const unsigned DTD_SIZE = 0x12;

static int _dw_edid_update_sink_hdmi20(bool state)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s *sink = hdmi->sink_info;

	if (!sink) {
		hdmi_err("check point sink is null\n");
		return -1;
	}

	sink->edid_m20Sink = state;
	hdmi->video_dev.mHdmi20 = state;
	return 0;
}

int _edid_calculate_checksum(u8 *edid)
{
	int i, checksum = 0;

	for (i = 0; i < EDID_BLOCK_SIZE; i++)
		checksum += edid[i];

	return checksum % 256; /* CEA-861 Spec */
}

void _reset_data_block_monitor_range_limits(dw_edid_monitor_descriptior_data_t *mrl)
{
	mrl->mMinVerticalRate = 0;
	mrl->mMaxVerticalRate = 0;
	mrl->mMinHorizontalRate = 0;
	mrl->mMaxHorizontalRate = 0;
	mrl->mMaxPixelClock = 0;
	mrl->mValid = false;
}

void _reset_data_block_colorimetry(dw_edid_colorimetry_data_t *cdb)
{
	cdb->mByte3 = 0;
	cdb->mByte4 = 0;
	cdb->mValid = false;
}

void _reset_data_block_hdr_metadata(dw_edid_hdr_static_metadata_data_t *hdr_metadata)
{
	memset(hdr_metadata, 0, sizeof(dw_edid_hdr_static_metadata_data_t));
}

void _reset_data_block_hdmi_forum(dw_edid_hdmi_forum_vs_data_t *forumvsdb)
{
	forumvsdb->mValid = false;
	forumvsdb->mIeee_Oui = 0;
	forumvsdb->mVersion = 0;
	forumvsdb->mMaxTmdsCharRate = 0;
	forumvsdb->mSCDC_Present = false;
	forumvsdb->mRR_Capable = false;
	forumvsdb->mLTS_340Mcs_scramble = false;
	forumvsdb->mIndependentView = false;
	forumvsdb->mDualView = false;
	forumvsdb->m3D_OSD_Disparity = false;
	forumvsdb->mDC_30bit_420 = false;
	forumvsdb->mDC_36bit_420 = false;
	forumvsdb->mDC_48bit_420 = false;
}

void _reset_data_block_hdmi(dw_edid_hdmi_vs_data_t *vsdb)
{
	int i, j = 0;

	vsdb->mPhysicalAddress = 0;
	vsdb->mSupportsAi = false;
	vsdb->mDeepColor30 = false;
	vsdb->mDeepColor36 = false;
	vsdb->mDeepColor48 = false;
	vsdb->mDeepColorY444 = false;
	vsdb->mDviDual = false;
	vsdb->mMaxTmdsClk = 0;
	vsdb->mVideoLatency = 0;
	vsdb->mAudioLatency = 0;
	vsdb->mInterlacedVideoLatency = 0;
	vsdb->mInterlacedAudioLatency = 0;
	vsdb->mId = 0;
	vsdb->mContentTypeSupport = 0;
	vsdb->mHdmiVicCount = 0;
	for (i = 0; i < DW_EDID_MAC_HDMI_VIC; i++)
		vsdb->mHdmiVic[i] = 0;

	vsdb->m3dPresent = false;
	for (i = 0; i < DW_EDID_MAX_VIC_WITH_3D; i++) {
		for (j = 0; j < DW_EDID_MAX_HDMI_3DSTRUCT; j++)
			vsdb->mVideo3dStruct[i][j] = 0;
	}
	for (i = 0; i < DW_EDID_MAX_VIC_WITH_3D; i++) {
		for (j = 0; j < DW_EDID_MAX_HDMI_3DSTRUCT; j++)
			vsdb->mDetail3d[i][j] = ~0;
	}
	vsdb->mValid = false;
}

void _reset_data_block_video_capabilit(dw_edid_video_capabilit_data_t *vcdb)
{
	vcdb->mQuantizationRangeSelectable = false;
	vcdb->mPreferredTimingScanInfo = 0;
	vcdb->mItScanInfo = 0;
	vcdb->mCeScanInfo = 0;
	vcdb->mValid = false;
}

void _reset_data_block_short_audio(dw_edid_block_sad_t *sad)
{
	sad->mFormat = 0;
	sad->mMaxChannels = 0;
	sad->mSampleRates = 0;
	sad->mByte3 = 0;
}

void _reset_data_block_short_video(dw_edid_block_svd_t *svd)
{
	svd->mNative = false;
	svd->mCode = 0;
}

void _reset_data_block_speaker_alloction(dw_edid_speaker_allocation_data_t *sadb)
{
	sadb->mByte1 = 0;
	sadb->mValid = false;
}

/**
 * Parses the Detailed Timing Descriptor.
 * Encapsulating the parsing process
 * @param dtd pointer to dw_dtd_t strucutute for the information to be save in
 * @param data a pointer to the 18-byte structure to be parsed.
 * @return true if success
 */
int _parse_data_block_detailed_timing(dw_dtd_t *dtd, u8 data[18])
{
	dtd->mCode = -1;
	dtd->mPixelRepetitionInput = 0;
	dtd->mLimitedToYcc420 = 0;
	dtd->mYcc420 = 0;

	dtd->mPixelClock = 1000 * dw_byte_to_word(data[1], data[0]);/* [10000Hz] */
	if (dtd->mPixelClock < 0x01) {	/* 0x0000 is defined as reserved */
		return false;
	}

	dtd->mHActive         = dw_concat_bits(data[4], 4, 4, data[2], 0, 8);
	dtd->mHBlanking       = dw_concat_bits(data[4], 0, 4, data[3], 0, 8);
	dtd->mHSyncOffset     = dw_concat_bits(data[11], 6, 2, data[8], 0, 8);
	dtd->mHSyncPulseWidth = dw_concat_bits(data[11], 4, 2, data[9], 0, 8);
	dtd->mHImageSize      = dw_concat_bits(data[14], 4, 4, data[12], 0, 8);
	dtd->mHBorder         = data[15];

	dtd->mVActive         = dw_concat_bits(data[7], 4, 4, data[5], 0, 8);
	dtd->mVBlanking       = dw_concat_bits(data[7], 0, 4, data[6], 0, 8);
	dtd->mVSyncOffset     = dw_concat_bits(data[11], 2, 2, data[10], 4, 4);
	dtd->mVSyncPulseWidth = dw_concat_bits(data[11], 0, 2, data[10], 0, 4);
	dtd->mVImageSize      = dw_concat_bits(data[14], 0, 4, data[13], 0, 8);
	dtd->mVBorder         = data[16];

	if (dw_bit_field(data[17], 4, 1) != 1) {/* if not DIGITAL SYNC SIGNAL DEF */
		hdmi_err("invalid dtd byte[17] bit4 parameters\n");
		return false;
	}

	if (dw_bit_field(data[17], 3, 1) != 1) {/* if not DIGITAL SEPATATE SYNC */
		hdmi_err("invalid dtd byte[17] bit3 parameters\n");
		return false;
	}

	/* no stereo viewing support in HDMI */
	dtd->mInterlaced    = dw_bit_field(data[17], 7, 1) == 1;
	dtd->mVSyncPolarity = dw_bit_field(data[17], 2, 1) == 1;
	dtd->mHSyncPolarity = dw_bit_field(data[17], 1, 1) == 1;
	return true;
}

int _parse_data_block_short_audio(dw_edid_block_sad_t *sad, u8 *data)
{
	_reset_data_block_short_audio(sad);
	if (data != 0) {
		sad->mFormat = dw_bit_field(data[0], 3, 4);
		sad->mMaxChannels = dw_bit_field(data[0], 0, 3) + 1;
		sad->mSampleRates = dw_bit_field(data[1], 0, 7);
		sad->mByte3 = data[2];
		return true;
	}
	return false;
}

int _parse_data_block_short_video(dw_edid_block_svd_t *svd, u8 data)
{
	_reset_data_block_short_video(svd);
	svd->mNative = (dw_bit_field(data, 7, 1) == 1) ? true : false;
	svd->mCode = dw_bit_field(data, 0, 7);
	svd->mLimitedToYcc420 = 0;
	svd->mYcc420 = 0;
	return true;
}

/**
 * Parse an array of data to fill the dw_edid_hdmi_vs_data_t data strucutre
 * @param *vsdb pointer to the structure to be filled
 * @param *data pointer to the 8-bit data type array to be parsed
 * @return Success, or error code:
 * @return 1 - array pointer invalid
 * @return 2 - Invalid datablock tag
 * @return 3 - Invalid minimum length
 * @return 4 - HDMI IEEE registration identifier not valid
 * @return 5 - Invalid length - latencies are not valid
 * @return 6 - Invalid length - Interlaced latencies are not valid
 */
int _parse_data_block_hdmi(dw_edid_hdmi_vs_data_t *vsdb, u8 *data)
{
	u8 blockLength = 0;
	unsigned videoInfoStart = 0;
	unsigned hdmi3dStart = 0;
	unsigned hdmiVicLen = 0;
	unsigned hdmi3dLen = 0;
	unsigned spanned3d = 0;
	unsigned i = 0;
	unsigned j = 0;

	_reset_data_block_hdmi(vsdb);
	if (data == 0)
		return false;

	if (dw_bit_field(data[0], 5, 3) != 0x3) {
		hdmi_err("%s invalid datablock tag!\n", __func__);
		return false;
	}
	blockLength = dw_bit_field(data[0], 0, 5);

	if (blockLength < 5) {
		hdmi_err("%s invalid datablock length!\n", __func__);
		return false;
	}

	if (dw_byte_to_dword(0x00, data[3], data[2], data[1]) != 0x000C03) {
		hdmi_err("hdmi ieee registration identifier not valid\n");
		return false;
	}

	_reset_data_block_hdmi(vsdb);
	vsdb->mId = 0x000C03;
	vsdb->mPhysicalAddress = dw_byte_to_word(data[4], data[5]);
	/* parse extension fields if they exist */
	if (blockLength > 5) {
		vsdb->mSupportsAi = dw_bit_field(data[6], 7, 1) == 1;
		vsdb->mDeepColor48 = dw_bit_field(data[6], 6, 1) == 1;
		vsdb->mDeepColor36 = dw_bit_field(data[6], 5, 1) == 1;
		vsdb->mDeepColor30 = dw_bit_field(data[6], 4, 1) == 1;
		vsdb->mDeepColorY444 = dw_bit_field(data[6], 3, 1) == 1;
		vsdb->mDviDual = dw_bit_field(data[6], 0, 1) == 1;
	} else {
		vsdb->mSupportsAi = false;
		vsdb->mDeepColor48 = false;
		vsdb->mDeepColor36 = false;
		vsdb->mDeepColor30 = false;
		vsdb->mDeepColorY444 = false;
		vsdb->mDviDual = false;
	}
	vsdb->mMaxTmdsClk = (blockLength > 6) ? data[7] : 0;
	vsdb->mVideoLatency = 0;
	vsdb->mAudioLatency = 0;
	vsdb->mInterlacedVideoLatency = 0;
	vsdb->mInterlacedAudioLatency = 0;
	if (blockLength > 7) {
		if (dw_bit_field(data[8], 7, 1) == 1) {
			if (blockLength < 10) {
				hdmi_err("Invalid length - latencies are not valid\n");
				return false;
			}
			if (dw_bit_field(data[8], 6, 1) == 1) {
				if (blockLength < 12) {
					hdmi_err("Invalid length - Interlaced latencies are not valid\n");
					return false;
				} else {
					vsdb->mVideoLatency = data[9];
					vsdb->mAudioLatency = data[10];
					vsdb->mInterlacedVideoLatency
								= data[11];
					vsdb->mInterlacedAudioLatency
								= data[12];
					videoInfoStart = 13;
				}
			} else {
				vsdb->mVideoLatency = data[9];
				vsdb->mAudioLatency = data[10];
				vsdb->mInterlacedVideoLatency = 0;
				vsdb->mInterlacedAudioLatency = 0;
				videoInfoStart = 11;
			}
		} else {	/* no latency data */
			vsdb->mVideoLatency = 0;
			vsdb->mAudioLatency = 0;
			vsdb->mInterlacedVideoLatency = 0;
			vsdb->mInterlacedAudioLatency = 0;
			videoInfoStart = 9;
		}
		vsdb->mContentTypeSupport = dw_bit_field(data[8], 0, 4);
	}
	/* additional video format capabilities are described */
	if (dw_bit_field(data[8], 5, 1) == 1) {
		vsdb->mImageSize = dw_bit_field(data[videoInfoStart], 3, 2);
		hdmiVicLen = dw_bit_field(data[videoInfoStart + 1], 5, 3);
		hdmi3dLen = dw_bit_field(data[videoInfoStart + 1], 0, 5);
		for (i = 0; i < hdmiVicLen; i++)
			vsdb->mHdmiVic[i] = data[videoInfoStart + 2 + i];

		vsdb->mHdmiVicCount = hdmiVicLen;
		if (dw_bit_field(data[videoInfoStart], 7, 1) == 1) {/* 3d present */
			vsdb->m3dPresent = true;
			hdmi3dStart = videoInfoStart + hdmiVicLen + 2;
			/* 3d multi 00 -> both 3d_structure_all
			and 3d_mask_15 are NOT present */
			/* 3d mutli 11 -> reserved */
			if (dw_bit_field(data[videoInfoStart], 5, 2) == 1) {
				/* 3d multi 01 */
				/* 3d_structure_all is present but 3d_mask_15 not present */
				for (j = 0; j < 16; j++) {
					/* j spans 3d structures */
					if (dw_bit_field(data[hdmi3dStart
						+ (j / 8)], (j % 8), 1) == 1) {
						for (i = 0; i < 16; i++)
							vsdb->mVideo3dStruct[i][(j < 8)	? j+8 : j - 8] = 1;
					}
				}
				spanned3d = 2;
				/* hdmi3dStart += 2;
				   hdmi3dLen -= 2; */
			} else if (dw_bit_field(data[videoInfoStart], 5, 2) == 2) {
				/* 3d multi 10 */
				/* 3d_structure_all and 3d_mask_15 are present */
				for (j = 0; j < 16; j++) {
					for (i = 0; i < 16; i++) {
						if (dw_bit_field(data[hdmi3dStart + 2 + (i / 8)], (i % 8), 1) == 1)
							vsdb->mVideo3dStruct[(i < 8) ? i + 8 : i - 8][(j < 8) ? j + 8 : j - 8] = dw_bit_field(data[hdmi3dStart + (j / 8)], (j % 8), 1);
					}
				}
				spanned3d = 4;
			}
			if (hdmi3dLen > spanned3d) {
				hdmi3dStart += spanned3d;
				for (i = 0, j = 0; i < (hdmi3dLen - spanned3d); i++) {
					vsdb->mVideo3dStruct[dw_bit_field(data[hdmi3dStart + i + j], 4, 4)][dw_bit_field(data[hdmi3dStart + i + j], 0, 4)] = 1;
					if (dw_bit_field(data[hdmi3dStart + i + j], 4, 4) > 7) {
						j++;
						vsdb->mDetail3d[dw_bit_field(data[hdmi3dStart + i + j], 4, 4)][dw_bit_field(data[hdmi3dStart + i + j], 4, 4)] = dw_bit_field(data[hdmi3dStart + i + j], 4, 4);
					}
				}
			}
		} else {	/* 3d NOT present */
			vsdb->m3dPresent = false;
		}
	}
	vsdb->mValid = true;

	return true;
}

int _parse_data_block_hdmi_forum(dw_edid_hdmi_forum_vs_data_t *forumvsdb, u8 *data)
{
	u16 blockLength;

	_reset_data_block_hdmi_forum(forumvsdb);
	if (data == 0)
		return false;

	if (dw_bit_field(data[0], 5, 3) != 0x3) {
		hdmi_err("Invalid datablock tag\n");
		return false;
	}
	blockLength = dw_bit_field(data[0], 0, 5);
	if (blockLength < 7) {
		hdmi_err("Invalid minimum length\n");
		return false;
	}
	if (dw_byte_to_dword(0x00, data[3], data[2], data[1]) !=
	    0xC45DD8) {
		hdmi_err("HDMI IEEE registration identifier not valid\n");
		return false;
	}
	forumvsdb->mVersion = dw_bit_field(data[4], 0, 7);
	forumvsdb->mMaxTmdsCharRate = dw_bit_field(data[5], 0, 7);
	forumvsdb->mSCDC_Present = dw_bit_field(data[6], 7, 1);
	forumvsdb->mRR_Capable = dw_bit_field(data[6], 6, 1);
	forumvsdb->mLTS_340Mcs_scramble = dw_bit_field(data[6], 3, 1);
	forumvsdb->mIndependentView = dw_bit_field(data[6], 2, 1);
	forumvsdb->mDualView = dw_bit_field(data[6], 1, 1);
	forumvsdb->m3D_OSD_Disparity = dw_bit_field(data[6], 0, 1);
	forumvsdb->mDC_30bit_420 = dw_bit_field(data[7], 0, 1);
	forumvsdb->mDC_36bit_420 = dw_bit_field(data[7], 1, 1);
	forumvsdb->mDC_48bit_420 = dw_bit_field(data[7], 2, 1);
	forumvsdb->mValid = true;

	edid_log("version %d\n", dw_bit_field(data[4], 0, 7));
	edid_log("Max_TMDS_Charater_rate %d\n",
		    dw_bit_field(data[5], 0, 7));
	edid_log("SCDC_Present %d\n", dw_bit_field(data[6], 7, 1));
	edid_log("RR_Capable %d\n", dw_bit_field(data[6], 6, 1));
	edid_log("LTE_340Mcsc_scramble %d\n",
		    dw_bit_field(data[6], 3, 1));
	edid_log("Independent_View %d\n", dw_bit_field(data[6], 2, 1));
	edid_log("Dual_View %d\n", dw_bit_field(data[6], 1, 1));
	edid_log("3D_OSD_Disparity %d\n", dw_bit_field(data[6], 0, 1));
	edid_log("DC_48bit_420 %d\n", dw_bit_field(data[7], 2, 1));
	edid_log("DC_36bit_420 %d\n", dw_bit_field(data[7], 1, 1));
	edid_log("DC_30bit_420 %d\n", dw_bit_field(data[7], 0, 1));

	return true;
}

int _parse_data_block_speaker_allocation(dw_edid_speaker_allocation_data_t *sadb, u8 *data)
{
	_reset_data_block_speaker_alloction(sadb);
	if ((data != 0) && (dw_bit_field(data[0], 0, 5) == 0x03) &&
				(dw_bit_field(data[0], 5, 3) == 0x04)) {
		sadb->mByte1 = data[1];
		sadb->mValid = true;
		return true;
	}
	return false;
}

int _parse_data_block_video_capability(dw_edid_video_capabilit_data_t *vcdb, u8 *data)
{
	_reset_data_block_video_capabilit(vcdb);
	/* check tag code and extended tag */
	if ((data != 0) && (dw_bit_field(data[0], 5, 3) == 0x7) &&
		(dw_bit_field(data[1], 0, 8) == 0x0) &&
			(dw_bit_field(data[0], 0, 5) == 0x2)) {
		/* so far VCDB is 2 bytes long */
		vcdb->mCeScanInfo = dw_bit_field(data[2], 0, 2);
		vcdb->mItScanInfo = dw_bit_field(data[2], 2, 2);
		vcdb->mPreferredTimingScanInfo = dw_bit_field(data[2], 4, 2);
		vcdb->mQuantizationRangeSelectable =
				(dw_bit_field(data[2], 6, 1) == 1) ? true : false;
		vcdb->mValid = true;
		return true;
	}
	return false;
}

int _parse_data_block_colorimetry(dw_edid_colorimetry_data_t *cdb, u8 *data)
{
	_reset_data_block_colorimetry(cdb);
	if ((data != 0) && (dw_bit_field(data[0], 0, 5) == 0x03) &&
		(dw_bit_field(data[0], 5, 3) == 0x07)
			&& (dw_bit_field(data[1], 0, 7) == 0x05)) {
		cdb->mByte3 = data[2];
		cdb->mByte4 = data[3];
		cdb->mValid = true;
		return true;
	}
	return false;
}

int _parse_data_block_hdr_static_metadata(dw_edid_hdr_static_metadata_data_t *hdr_metadata, u8 *data)
{
	_reset_data_block_hdr_metadata(hdr_metadata);
	if ((data != 0) && (dw_bit_field(data[0], 0, 5) > 1)
		&& (dw_bit_field(data[0], 5, 3) == 0x07)
		&& (data[1] == 0x06)) {
		hdr_metadata->et_n = dw_bit_field(data[2], 0, 5);
		hdr_metadata->sm_n = data[3];

		if (dw_bit_field(data[0], 0, 5) > 3)
			hdr_metadata->dc_max_lum_data = data[4];
		if (dw_bit_field(data[0], 0, 5) > 4)
			hdr_metadata->dc_max_fa_lum_data = data[5];
		if (dw_bit_field(data[0], 0, 5) > 5)
			hdr_metadata->dc_min_lum_data = data[6];

		return true;
	}
	return false;
}

static void _parse_data_block_ycc420_video(struct sink_info_s *sink,
		u8 Ycc420All, u8 LimitedToYcc420All)
{
	u32 edid_cnt = 0;

	for (edid_cnt = 0; edid_cnt < sink->edid_mSvdIndex; edid_cnt++) {
		switch (sink->edid_mSvd[edid_cnt].mCode) {
		case 96:
		case 97:
		case 101:
		case 102:
		case 106:
		case 107:
			Ycc420All == 1 ?
				sink->edid_mSvd[edid_cnt].mYcc420 = Ycc420All : 0;
			LimitedToYcc420All == 1 ?
				sink->edid_mSvd[edid_cnt].mLimitedToYcc420 =
							LimitedToYcc420All : 0;
			break;
		}
	}
}

static int _parse_cta_data_block(u8 *data, struct sink_info_s *sink)
{
	u8 c = 0;
	dw_edid_block_sad_t tmpSad;
	dw_edid_block_svd_t tmpSvd;
	u8 tmpYcc420All = 0;
	u8 tmpLimitedYcc420All = 0;
	u32 ieeeId = 0;
	u8 extendedTag = 0;
	int i = 0;
	int edid_cnt = 0;
	int svdNr = 0;
	int icnt = 0;
	u8 tag = dw_bit_field(data[0], 5, 3);
	u8 length = dw_bit_field(data[0], 0, 5);

	tmpSvd.mLimitedToYcc420 = 0;
	tmpSvd.mYcc420 = 0;

	switch (tag) {
	case 0x1:		/* Audio Data Block */
		edid_log("edid parse audio data block\n");
		for (c = 1; c < (length + 1); c += 3) {
			_parse_data_block_short_audio(&tmpSad, data + c);
			if (sink->edid_mSadIndex < (sizeof(sink->edid_mSad) / sizeof(dw_edid_block_sad_t)))
				sink->edid_mSad[sink->edid_mSadIndex++] = tmpSad;
			else
				edid_log("buffer full - sad ignored\n");
		}
		break;
	case 0x2:		/* Video Data Block */
		edid_log("edid parse video data block\n");
		for (c = 1; c < (length + 1); c++) {
			_parse_data_block_short_video(&tmpSvd, data[c]);
			if (sink->edid_mSvdIndex < (sizeof(sink->edid_mSvd) / sizeof(dw_edid_block_svd_t)))
				sink->edid_mSvd[sink->edid_mSvdIndex++] = tmpSvd;
			else
				edid_log("buffer full - SVD ignored\n");
		}
		break;
	case 0x3:		/* Vendor Specific Data Block HDMI or HF */
		ieeeId = dw_byte_to_dword(0x00, data[3], data[2], data[1]);
		if (ieeeId == 0x000C03) {	/* HDMI */
			edid_log("edid parse hdmi vendor specific data block");
			if (_parse_data_block_hdmi(&sink->edid_mHdmivsdb, data) != true) {
				hdmi_err("hdmi vendor specific data dlock parse failed!!!\n");
				break;
			}
			edid_log("hdmi vsdb parsed success.\n");
		} else {
			if (ieeeId == 0xC45DD8) {	/* HDMI-F */
				edid_log("edid parse hdmi forum vendor specific data block");
				_dw_edid_update_sink_hdmi20(true);
				if (_parse_data_block_hdmi_forum(&sink->edid_mHdmiForumvsdb, data) != true) {
					hdmi_err("hdmi forum vendor specific data block parse failed!!!\n");
					break;
				}
				edid_log("hdmi forum vsdb parsed success.\n");
			} else {
				edid_log("vendor specific data block not parsed ieeeId: 0x%x\n",
					ieeeId);
			}
		}
		break;
	case 0x4:		/* Speaker Allocation Data Block */
		edid_log("edid parse speaker allocation data block\n");
		if (_parse_data_block_speaker_allocation(&sink->edid_mSpeakerAllocationDataBlock, data) != true)
			hdmi_err("Speaker Allocation Data Block corrupt\n");
		break;
	case 0x7:{
		extendedTag = data[1];
		switch (extendedTag) {
		case 0x00:	/* Video Capability Data Block */
			edid_log("edid parse video capability Data Block\n");
			if (_parse_data_block_video_capability(&sink->edid_mVideoCapabilityDataBlock, data) != true)
				hdmi_err("Video Capability Data Block corrupt\n");
			break;
		case 0x04:	/* HDMI Video Data Block */
			edid_log("edid unsupport parse hdmi video data block\n");
			break;
		case 0x05:	/* Colorimetry Data Block */
			edid_log("edid parse colorimetry Data Block\n");
			if (_parse_data_block_colorimetry(&sink->edid_mColorimetryDataBlock, data) != true)
				hdmi_err("Colorimetry Data Block corrupt\n");
			break;
		case 0x06:	/* HDR Static Metadata Data Block */
			edid_log("edid parse hdr static metadata data block\n");
			if (_parse_data_block_hdr_static_metadata(&sink->edid_hdr_static_metadata_data_block, data) != true)
				hdmi_err("HDR Static Metadata Data Block corrupt\n");
			break;
		case 0x12:	/* HDMI Audio Data Block */
			edid_log("edid unsupport parse hdmi audio data block\n");
			break;
		case 0xe:
			/* * If it is a YCC420 VDB then VICs can ONLY be displayed in YCC 4:2:0 */
			edid_log("edid parse ycc420 video data block\n");
			/* * If Sink has YCC Datablocks it is HDMI 2.0 */
			_dw_edid_update_sink_hdmi20(true);
			tmpLimitedYcc420All = (dw_bit_field(data[0], 0, 5) == 1 ? 1 : 0);
			_parse_data_block_ycc420_video(sink, tmpYcc420All, tmpLimitedYcc420All);
			for (i = 0; i < (dw_bit_field(data[0], 0, 5) - 1); i++) {
				/* * Length includes the tag byte */
				tmpSvd.mCode = data[2 + i];
				tmpSvd.mNative = 0;
				tmpSvd.mLimitedToYcc420 = 1;
				for (edid_cnt = 0; edid_cnt < sink->edid_mSvdIndex; edid_cnt++) {
					if (sink->edid_mSvd[edid_cnt].mCode == tmpSvd.mCode) {
						sink->edid_mSvd[edid_cnt] =	tmpSvd;
						goto concluded;
					}
				}
				if (sink->edid_mSvdIndex <
					(sizeof(sink->edid_mSvd) /  sizeof(dw_edid_block_svd_t))) {
					sink->edid_mSvd[sink->edid_mSvdIndex] = tmpSvd;
					sink->edid_mSvdIndex++;
				} else {
					edid_log("buffer full - YCC 420 DTD ignored\n");
				}
concluded:;
			}
			break;
		case 0x0f:
			/* * If it is a YCC420 CDB then VIC can ALSO be displayed in YCC 4:2:0 */
			_dw_edid_update_sink_hdmi20(true);
			edid_log("edid parse ycc420 capability map data block\n");
			/* If YCC420 CMDB is bigger than 1, then there is SVD info to parse */
			if (dw_bit_field(data[0], 0, 5) > 1) {
				for (i = 0; i < dw_bit_field(data[0], 0, 5) - 1; i++) {
					for (icnt = 0; icnt < 8; icnt++) {
						/* * Lenght includes the tag byte */
						if ((dw_bit_field(data[i + 2], icnt, 1) & 0x01)) {
							svdNr = icnt + (i * 8);
							tmpSvd.mCode = sink->edid_mSvd[svdNr].mCode;
							tmpSvd.mYcc420 = 1;
							sink->edid_mSvd[svdNr] = tmpSvd;
							edid_log("svd[%d] update ycc420 = 1\n", svdNr);
						}
					}
				}
				/* Otherwise, all SVDs present at the Video Data Block support YCC420 */
			} else {
				tmpYcc420All = (dw_bit_field(data[0], 0, 5) == 1 ? 1 : 0);
				_parse_data_block_ycc420_video(sink, tmpYcc420All, tmpLimitedYcc420All);
				sink->edid_mYcc420Support = true;
			}
			break;
		default:
			edid_log("Extended Data Block not parsed %d\n",
					extendedTag);
			break;
		}
		break;
	}
	default:
		edid_log("Data Block not parsed %d\n", tag);
		break;
	}
	return length + 1;
}

static int _edid_parser_block_base(struct edid *edid, struct sink_info_s *sink)
{
	int i;
	char *monitorName;

	if (edid->header[0] != 0) {
		hdmi_err("edid base block is unvalid!!!\n");
		return -1;
	}

	edid_log("parse block0 detailed discriptor:\n");
	for (i = 0; i < 4; i++) {
		struct detailed_timing *detailed_timing = &(edid->detailed_timings[i]);

		if (detailed_timing->pixel_clock == 0) {
			struct detailed_non_pixel *npixel = &(detailed_timing->data.other_data);

			switch (npixel->type) {
			case EDID_DETAIL_MONITOR_NAME:
				monitorName = (char *) &(npixel->data.str.str);
				edid_log("Monitor name: %s\n", monitorName);
				break;
			case EDID_DETAIL_MONITOR_RANGE:
				break;

			}
		} else { /* Detailed Timing Definition */
			struct detailed_pixel_timing *ptiming = &(detailed_timing->data.pixel_data);
			edid_log(" - pixel_clock:%d\n", detailed_timing->pixel_clock * 10000);
			edid_log(" - hactive * vactive: %d * %d\n\n",
			(((ptiming->hactive_hblank_hi >> 4) & 0x0f) << 8) | ptiming->hactive_lo,
			(((ptiming->vactive_vblank_hi >> 4) & 0x0f) << 8) | ptiming->vactive_lo);
		}
	}
	return 0;
}

static int _edid_parser_block_cta_861(u8 *buffer, struct sink_info_s *sink)
{
	int i = 0;
	int c = 0;
	dw_dtd_t tmpDtd;
	u8 offset = buffer[2];

	if (buffer[1] < 0x03) {
		hdmi_err("edid cta 861 block version unsupport!\n");
		return -1;
	}

	/* CTA 861 header block parse */
	sink->edid_mYcc422Support     = dw_bit_field(buffer[3],	4, 1) == 1;
	sink->edid_mYcc444Support     = dw_bit_field(buffer[3],	5, 1) == 1;
	sink->edid_mBasicAudioSupport = dw_bit_field(buffer[3], 6, 1) == 1;
	sink->edid_mUnderscanSupport  = dw_bit_field(buffer[3], 7, 1) == 1;

	/* CTA 861 data block parse */
	if (offset != 4) {
		for (i = 4; i < offset; i += _parse_cta_data_block(buffer + i, sink))
			;
	}

	memcpy(sink->detailed_timings, buffer + 84, sizeof(sink->detailed_timings));

	/* last is checksum */
	for (i = offset, c = 0; ((i + DTD_SIZE) < (EDID_BLOCK_SIZE - 1)) && c < 6; i += DTD_SIZE, c++) {
		if (_parse_data_block_detailed_timing(&tmpDtd, buffer + i) == true) {
			if (sink->edid_mDtdIndex < ((sizeof(sink->edid_mDtd) / sizeof(dw_dtd_t)))) {
				sink->edid_mDtd[sink->edid_mDtdIndex] = tmpDtd;
				edid_log("edid_mDtd code %d\n", sink->edid_mDtd[sink->edid_mDtdIndex].mCode);
				edid_log("edid_mDtd limited to Ycc420? %d\n", sink->edid_mDtd[sink->edid_mDtdIndex].mLimitedToYcc420);
				edid_log("edid_mDtd supports Ycc420? %d\n", sink->edid_mDtd[sink->edid_mDtdIndex].mYcc420);
				sink->edid_mDtdIndex++;
			} else {
				edid_log("buffer full - DTD ignored\n");
			}
		}
	}
	return 0;
}

void dw_edid_sink_reset(struct sink_info_s *sink)
{
	unsigned i = 0;

	_dw_edid_update_sink_hdmi20(false);

	for (i = 0; i < sizeof(sink->edid_mMonitorName); i++)
		sink->edid_mMonitorName[i] = 0;

	sink->edid_mBasicAudioSupport = false;
	sink->edid_mUnderscanSupport  = false;
	sink->edid_mYcc422Support = false;
	sink->edid_mYcc444Support = false;
	sink->edid_mYcc420Support = false;
	sink->edid_mDtdIndex = 0;
	sink->edid_mSadIndex = 0;
	sink->edid_mSvdIndex = 0;

	_reset_data_block_hdmi(&sink->edid_mHdmivsdb);
	_reset_data_block_hdmi_forum(&sink->edid_mHdmiForumvsdb);
	_reset_data_block_monitor_range_limits(&sink->edid_mMonitorRangeLimits);
	_reset_data_block_video_capabilit(&sink->edid_mVideoCapabilityDataBlock);
	_reset_data_block_colorimetry(&sink->edid_mColorimetryDataBlock);
	_reset_data_block_hdr_metadata(&sink->edid_hdr_static_metadata_data_block);
	_reset_data_block_speaker_alloction(&sink->edid_mSpeakerAllocationDataBlock);
}

int dw_edid_parse_info(u8 *data)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s *sink = hdmi->sink_info;
	int ret = -1;

	if (!sink) {
		hdmi_err("check point sink is null\n");
		goto parse_exit;
	}

	dw_edid_sink_reset(sink);

	switch (data[0]) {
	case TAG_BASE_BLOCK:
		ret = _edid_parser_block_base((struct edid *)data, sink);
		goto parse_exit;
	case TAG_CEA_EXT:
		ret = _edid_parser_block_cta_861(data, sink);
		goto parse_exit;
	case TAG_VTB_EXT:
	case TAG_DI_EXT:
	case TAG_LS_EXT:
	case TAG_MI_EXT:
	default:
		hdmi_inf("edid block header 0x%02x not supported\n", data[0]);
	}

parse_exit:
	return ret;
}

bool dw_edid_check_hdmi_vic(u32 code)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	u32 i;

	if (!code) {
		hdmi_wrn("hdmi code %d is invalid\n", code);
		return false;
	}

	if (!hdmi->sink_info) {
		hdmi_err("dw edid check point sink_info is null\n");
		return false;
	}

	for (i = 0; (i < 4) && (hdmi->sink_info->edid_mHdmivsdb.mHdmiVic[i] != 0); i++) {
		if (hdmi->sink_info->edid_mHdmivsdb.mHdmiVic[i] == code)
			return true;
	}

	return false;
}

bool dw_edid_check_cea_vic(u32 code)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	u32 i;

	if (!code) {
		hdmi_wrn("cea code %d is invalid\n", code);
		return false;
	}

	if (!hdmi->sink_info) {
		hdmi_err("dw edid check point sink_info is null\n");
		return false;
	}

	for (i = 0; (i < 128) && (hdmi->sink_info->edid_mSvd[i].mCode != 0); i++) {
		if (hdmi->sink_info->edid_mSvd[i].mCode == code)
			return true;
	}

	return false;
}

bool dw_edid_check_scdc_support(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s *sink = hdmi->sink_info;

	return sink->edid_mHdmiForumvsdb.mSCDC_Present;
}

int dw_edid_check_only_yuv420(u32 vic)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;
	int i = 0;

	if (vic == 0x0)
		return 0x0;

	for (i = 0; i < sink->edid_mSvdIndex; i++) {
		if (sink->edid_mSvd[i].mCode == vic) {
			if (sink->edid_mSvd[i].mLimitedToYcc420) {
				return 0x1;
			}
		}
	}

	return 0x0;
}

int dw_edid_check_yuv420_base(u32 vic)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;
	int i = 0;

	if (vic == 0)
		return 0x0;

	for (i = 0; i < sink->edid_mSvdIndex; i++) {
		if (sink->edid_mSvd[i].mCode == vic) {
			if (sink->edid_mSvd[i].mLimitedToYcc420 || sink->edid_mSvd[i].mYcc420) {
				edid_log("this vic %d is support 420. limit - %d, all - %d\n",
					vic, sink->edid_mSvd[i].mLimitedToYcc420, sink->edid_mSvd[i].mYcc420);
				return 0x1;
			}
		}
	}

	return 0x0;
}

int dw_edid_check_yuv422_base(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	if (sink->edid_mYcc422Support)
		return 0x1;

	return 0x0;
}

int dw_edid_check_yuv444_base(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	if (sink->edid_mYcc444Support)
		return 0x1;

	return 0x0;
}

int dw_edid_check_rgb_dc(u8 bits)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	switch (bits) {
	case 1: /* 10-bits */
		return sink->edid_mHdmivsdb.mDeepColor30;
	case 2: /* 12-bits */
		return sink->edid_mHdmivsdb.mDeepColor36;
	case 3: /* 16-bits */
		return sink->edid_mHdmivsdb.mDeepColor48;
	default:
		return 0x0;
	}

	return 0x0;
}

int dw_edid_check_yuv444_dc(u8 bits)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	if (sink->edid_mHdmivsdb.mDeepColorY444 == 0)
		return 0x0;

	switch (bits) {
	case 1: /* 10-bits */
		return sink->edid_mHdmivsdb.mDeepColor30;
	case 2: /* 12-bits */
		return sink->edid_mHdmivsdb.mDeepColor36;
	case 3: /* 16-bits */
		return sink->edid_mHdmivsdb.mDeepColor48;
	default:
		return 0x0;
	}

	return 0x0;
}

int dw_edid_check_yuv422_dc(u8 bits)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	switch (bits) {
	case 1: /* 10-bits */
		return sink->edid_mHdmivsdb.mDeepColor30;
	case 2: /* 12-bits */
		return sink->edid_mHdmivsdb.mDeepColor36;
	case 3: /* 16-bits */
		return sink->edid_mHdmivsdb.mDeepColor48;
	default:
		return 0x0;
	}

	return 0x0;
}

int dw_edid_check_yuv420_dc(u8 bits)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	switch (bits) {
	case 1: /* 10-bits */
		return sink->edid_mHdmiForumvsdb.mDC_30bit_420;
	case 2: /* 12-bits */
		return sink->edid_mHdmiForumvsdb.mDC_36bit_420;
	case 3: /* 16-bits */
		return sink->edid_mHdmiForumvsdb.mDC_48bit_420;
	default:
		return 0;
	}

	return 0;
}

int dw_edid_check_hdr10(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	if (sink->edid_hdr_static_metadata_data_block.et_n & BIT(2))
		return 0x1;

	return 0x0;
}

int dw_edid_check_hlg(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;

	if (sink->edid_hdr_static_metadata_data_block.et_n & BIT(3))
		return 0x1;

	return 0x0;
}

int dw_edid_check_max_tmds_clk(u32 clk)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;
	u32 max_rate = 300;

	if (sink->edid_m20Sink) {
		if (sink->edid_mHdmiForumvsdb.mMaxTmdsCharRate)
			max_rate = sink->edid_mHdmiForumvsdb.mMaxTmdsCharRate * 5;
		else
			max_rate = 600;
	}

	if (max_rate > (clk / 1000))
		return 0x1;

	return 0x0;
}

int dw_edid_exit(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();

	if (hdmi->sink_info) {
		kfree(hdmi->sink_info);
		hdmi->sink_info = NULL;
	}

	return 0;
}

int dw_edid_init(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();

	if (hdmi->sink_info) {
		kfree(hdmi->sink_info);
		hdmi->sink_info = NULL;
	}

	hdmi->sink_info = kzalloc(sizeof(struct sink_info_s), GFP_KERNEL);
	if (!hdmi->sink_info) {
		hdmi_err("dw edid init memory failed!\n");
		return -1;
	}
	memset(hdmi->sink_info, 0, sizeof(struct sink_info_s));

	return 0;
}

ssize_t dw_edid_dump(char *buf)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct sink_info_s   *sink = hdmi->sink_info;
	ssize_t n = 0;

	n += sprintf(buf + n, "[edid]\n");
	n += sprintf(buf + n, " - dtd num: %d, svd num: %d\n",
		sink->edid_mDtdIndex, sink->edid_mSvdIndex);
	n += sprintf(buf + n, "\n");

	return n;
}