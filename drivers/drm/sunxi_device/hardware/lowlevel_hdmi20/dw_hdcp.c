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
#include <linux/workqueue.h>

#include "dw_mc.h"
#include "dw_fc.h"
#include "dw_i2cm.h"
#include "dw_hdcp22.h"
#include "dw_hdcp.h"

bool hdcp14_enable;
bool hdcp22_enable;

static u32 hdcp14_auth_enable;
static u32 hdcp14_auth_complete;
static u32 hdcp14_encryption;

static u8 hdcp_status;
static u32 hdcp_engaged_count;

/** Number of supported devices
 * (depending on instantiated KSV MEM RAM - Revocation Memory to support
 * HDCP repeaters)
 */
#define DW_HDCP14_MAX_DEVICES		(128)
#define DW_HDCP14_KSV_HEADER		(10)
#define DW_HDCP14_KSV_SHAMAX		(20)
#define DW_HDCP14_KSV_LEN			(5)
#define DW_HDCP14_ADDR_JUMP			(4)
#define DW_HDCP14_OESS_SIZE			(0x40)

/* Bstatus Register Bit Field Definitions */
#define BSTATUS_HDMI_MODE_MASK				0x1000
#define BSTATUS_MAX_CASCADE_EXCEEDED_MASK	0x0800
#define BSTATUS_DEPTH_MASK					0x0700
#define BSTATUS_MAX_DEVS_EXCEEDED_MASK		0x0080
#define BSTATUS_DEVICE_COUNT_MASK			0x007F

enum _dw_hdcp_state_e {
	HDCP_IDLE = 0,
	HDCP_KSV_LIST_READY,
	HDCP_ERR_KSV_LIST_NOT_VALID,
	HDCP_KSV_LIST_ERR_DEPTH_EXCEEDED,
	HDCP_KSV_LIST_ERR_MEM_ACCESS,
	HDCP_ENGAGED,
	HDCP_FAILED
};

struct _dw_hdcp14_sha_s{
	u8 mLength[8];
	u8 mBlock[64];
	int mIndex;
	bool mComputed;
	bool mCorrupted;
	unsigned mDigest[5];
} ;

/**
 * @desc: config hdcp14 work in hdmi mode or dvi mode
 * @bit: 1 - hdmi mode
 *       0 - dvi mode
*/
static void _dw_hdcp_set_tmds_mode(u8 bit)
{
	hdmi_trace("dw hdcp set tmds mode: %s\n", bit ? "hdmi" : "dvi");
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_HDMIDVI_MASK, bit);
}

static u8 _dw_hdcp_get_tmds_mode(void)
{
	return dw_read_mask(A_HDCPCFG0, A_HDCPCFG0_HDMIDVI_MASK);
}

static void _dw_hdcp_set_hsync_polarity(u8 bit)
{
	log_trace1(bit);
	dw_write_mask(A_VIDPOLCFG, A_VIDPOLCFG_HSYNCPOL_MASK, bit);
}

static void _dw_hdcp_set_vsync_polarity(u8 bit)
{
	log_trace1(bit);
	dw_write_mask(A_VIDPOLCFG, A_VIDPOLCFG_VSYNCPOL_MASK, bit);
}

static void _dw_hdcp_enable_data_polarity(u8 bit)
{
	log_trace1(bit);
	dw_write_mask(A_VIDPOLCFG, A_VIDPOLCFG_DATAENPOL_MASK, bit);
}

void dw_hdcp_set_avmute_state(int enable)
{
	log_trace1(enable);
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_AVMUTE_MASK, enable);
}

u8 dw_hdcp_get_avmute_state(void)
{
	log_trace();
	return dw_read_mask(A_HDCPCFG0, A_HDCPCFG0_AVMUTE_MASK);
}

/**
 * @path: 0 - hdcp14 path
 *        1 - hdcp22 path
 */
void dw_hdcp_path_select(u8 path)
{
	dw_write_mask(HDCP22REG_CTRL, HDCP22REG_CTRL_HDCP22_OVR_VAL_MASK, path);
	dw_write_mask(HDCP22REG_CTRL, HDCP22REG_CTRL_HDCP22_OVR_EN_MASK, 0x1);
}

static void _dw_hdcp14_rxdetect_enable(u8 bit)
{
	hdmi_trace("dw hdcp set rx detect: %s\n", bit ? "enable" : "disable");
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_RXDETECT_MASK, bit);
}

static void _dw_hdcp14_disable_encrypt(u8 bit)
{
	hdmi_trace("dw hdcp set encrypt: %s\n", bit ? "disable" : "enable");
	dw_write_mask(A_HDCPCFG1, A_HDCPCFG1_ENCRYPTIONDISABLE_MASK, bit);
}

static u8 _dw_hdcp14_get_encryption(void)
{
	return dw_read_mask(A_HDCPCFG1, A_HDCPCFG1_ENCRYPTIONDISABLE_MASK);
}

static void _dw_hdcp14_interrupt_clear(u8 value)
{
	dw_write((A_APIINTCLR), value);
}

static void _dw_hdcp14_set_oess_size(u8 value)
{
	hdmi_trace("dw hdcp set oess size: %d\n", value);
	dw_write(A_OESSWCFG, value);
}

static void _dw_hdcp14_set_access_ksv_memory(u8 bit)
{
	log_trace();
	dw_write_mask(A_KSVMEMCTRL, A_KSVMEMCTRL_KSVMEMREQUEST_MASK, bit);
}

static u8 _dw_hdcp14_get_access_ksv_memory_granted(void)
{
	log_trace();
	return (u8)((dw_read(A_KSVMEMCTRL)
		& A_KSVMEMCTRL_KSVMEMACCESS_MASK) >> 1);
}

static void _dw_hdcp14_update_ksv_list_state(u8 bit)
{
	log_trace1(bit);
	dw_write_mask(A_KSVMEMCTRL, A_KSVMEMCTRL_SHA1FAIL_MASK, bit);
	dw_write_mask(A_KSVMEMCTRL, A_KSVMEMCTRL_KSVCTRLUPD_MASK, 1);
	dw_write_mask(A_KSVMEMCTRL, A_KSVMEMCTRL_KSVCTRLUPD_MASK, 0);
}

static u16 _dw_hdcp14_get_bstatus(void)
{
	u16 bstatus = 0;

	bstatus	= dw_read(HDCP_BSTATUS);
	bstatus	|= dw_read(HDCP_BSTATUS + DW_HDCP14_ADDR_JUMP) << 8;
	return bstatus;
}

static u8 _dw_hdcp14_get_bskv(u8 index)
{
	return (u8)dw_read_mask((HDCPREG_BKSV0 + (index * DW_HDCP14_ADDR_JUMP)),
		HDCPREG_BKSV0_HDCPREG_BKSV0_MASK);
}

static void _dw_hdcp14_sha_reset(struct _dw_hdcp14_sha_s *sha)
{
	size_t i = 0;

	sha->mIndex = 0;
	sha->mComputed = false;
	sha->mCorrupted = false;
	for (i = 0; i < sizeof(sha->mLength); i++)
		sha->mLength[i] = 0;

	sha->mDigest[0] = 0x67452301;
	sha->mDigest[1] = 0xEFCDAB89;
	sha->mDigest[2] = 0x98BADCFE;
	sha->mDigest[3] = 0x10325476;
	sha->mDigest[4] = 0xC3D2E1F0;
}

static void _dw_hdcp14_sha_process_block(struct _dw_hdcp14_sha_s *sha)
{
#define shaCircularShift(bits, word) \
		((((word) << (bits)) & 0xFFFFFFFF) | ((word) >> (32-(bits))))

	const unsigned K[] = {	/* constants defined in SHA-1 */
		0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
	};
	unsigned W[80];		/* word sequence */
	unsigned A, B, C, D, E;	/* word buffers */
	unsigned temp = 0;
	int t = 0;

	/* Initialize the first 16 words in the array W */
	for (t = 0; t < 80; t++) {
		if (t < 16) {
			W[t] = ((unsigned)sha->mBlock[t * 4 + 0]) << 24;
			W[t] |= ((unsigned)sha->mBlock[t * 4 + 1]) << 16;
			W[t] |= ((unsigned)sha->mBlock[t * 4 + 2]) << 8;
			W[t] |= ((unsigned)sha->mBlock[t * 4 + 3]) << 0;
		} else {
			W[t] =
			    shaCircularShift(1,
					     W[t - 3] ^ W[t - 8] ^ W[t -
							14] ^ W[t - 16]);
		}
	}

	A = sha->mDigest[0];
	B = sha->mDigest[1];
	C = sha->mDigest[2];
	D = sha->mDigest[3];
	E = sha->mDigest[4];

	for (t = 0; t < 80; t++) {
		temp = shaCircularShift(5, A);
		if (t < 20)
			temp += ((B & C) | ((~B) & D)) + E + W[t] + K[0];
		else if (t < 40)
			temp += (B ^ C ^ D) + E + W[t] + K[1];
		else if (t < 60)
			temp += ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
		else
			temp += (B ^ C ^ D) + E + W[t] + K[3];

		E = D;
		D = C;
		C = shaCircularShift(30, B);
		B = A;
		A = (temp & 0xFFFFFFFF);
	}

	sha->mDigest[0] = (sha->mDigest[0] + A) & 0xFFFFFFFF;
	sha->mDigest[1] = (sha->mDigest[1] + B) & 0xFFFFFFFF;
	sha->mDigest[2] = (sha->mDigest[2] + C) & 0xFFFFFFFF;
	sha->mDigest[3] = (sha->mDigest[3] + D) & 0xFFFFFFFF;
	sha->mDigest[4] = (sha->mDigest[4] + E) & 0xFFFFFFFF;

	sha->mIndex = 0;
}

static void _dw_hdcp14_sha_input(struct _dw_hdcp14_sha_s *sha, const u8 *data, size_t size)
{
	int i = 0;
	unsigned j = 0;
	bool rc = true;

	if (data == 0 || size == 0) {
		hdmi_err("invalid input data\n");
		return;
	}
	if (sha->mComputed == true || sha->mCorrupted == true) {
		sha->mCorrupted = true;
		return;
	}
	while (size-- && sha->mCorrupted == false) {
		sha->mBlock[sha->mIndex++] = *data;

		for (i = 0; i < 8; i++) {
			rc = true;
			for (j = 0; j < sizeof(sha->mLength); j++) {
				sha->mLength[j]++;
				if (sha->mLength[j] != 0) {
					rc = false;
					break;
				}
			}
			sha->mCorrupted = (sha->mCorrupted == true
					   || rc == true) ? true : false;
		}
		/* if corrupted then message is too long */
		if (sha->mIndex == 64)
			_dw_hdcp14_sha_process_block(sha);

		data++;
	}
}

static void _dw_hdcp14_sha_pad_message(struct _dw_hdcp14_sha_s *sha)
{
	/*
	 *  Check to see if the current message block is too small to hold
	 *  the initial padding bits and length.  If so, we will pad the
	 *  block, process it, and then continue padding into a second
	 *  block.
	 */
	if (sha->mIndex > 55) {
		sha->mBlock[sha->mIndex++] = 0x80;
		while (sha->mIndex < 64)
			sha->mBlock[sha->mIndex++] = 0;

		_dw_hdcp14_sha_process_block(sha);
		while (sha->mIndex < 56)
			sha->mBlock[sha->mIndex++] = 0;

	} else {
		sha->mBlock[sha->mIndex++] = 0x80;
		while (sha->mIndex < 56)
			sha->mBlock[sha->mIndex++] = 0;
	}

	/* Store the message length as the last 8 octets */
	sha->mBlock[56] = sha->mLength[7];
	sha->mBlock[57] = sha->mLength[6];
	sha->mBlock[58] = sha->mLength[5];
	sha->mBlock[59] = sha->mLength[4];
	sha->mBlock[60] = sha->mLength[3];
	sha->mBlock[61] = sha->mLength[2];
	sha->mBlock[62] = sha->mLength[1];
	sha->mBlock[63] = sha->mLength[0];

	_dw_hdcp14_sha_process_block(sha);
}

static int _dw_hdcp14_sha_result(struct _dw_hdcp14_sha_s *sha)
{
	if (sha->mCorrupted == true)
		return false;

	if (sha->mComputed == false) {
		_dw_hdcp14_sha_pad_message(sha);
		sha->mComputed = true;
	}
	return true;
}

static int _dw_hdcp14_verify_ksv(const u8 *data, size_t size)
{
	size_t i = 0;
	struct _dw_hdcp14_sha_s sha;

	log_trace1((int)size);

	if (data == 0 || size < (DW_HDCP14_KSV_HEADER + DW_HDCP14_KSV_SHAMAX)) {
		hdmi_err("invalid input data\n");
		return false;
	}
	_dw_hdcp14_sha_reset(&sha);
	_dw_hdcp14_sha_input(&sha, data, size - DW_HDCP14_KSV_SHAMAX);

	if (_dw_hdcp14_sha_result(&sha) == false) {
		hdmi_err("cannot process SHA digest\n");
		return false;
	}

	for (i = 0; i < DW_HDCP14_KSV_SHAMAX; i++) {
		if (data[size - DW_HDCP14_KSV_SHAMAX + i] !=
				(u8) (sha.mDigest[i / 4] >> ((i % 4) * 8))) {
			hdmi_err("SHA digest does not match\n");
			return false;
		}
	}
	return true;
}

/* SHA-1 calculation by Software */
static u8 _dw_hdcp14_read_ksv_list(int *param)
{
	int timeout = 1000;
	u16 bstatus = 0;
	u16 deviceCount = 0;
	int valid = HDCP_IDLE;
	int size = 0;
	int i = 0;

	u8 *hdcp_ksv_list_buffer = NULL;

	/* 1 - Wait for an interrupt to be triggered
		(a_apiintstat.KSVSha1calcint) */
	/* This is called from the INT_KSV_SHA1 irq
		so nothing is required for this step */

	/* 2 - Request access to KSV memory through
		setting a_ksvmemctrl.KSVMEMrequest to 1'b1 and */
	/* pool a_ksvmemctrl.KSVMEMaccess until
		this value is 1'b1 (access granted). */
	_dw_hdcp14_set_access_ksv_memory(true);
	while (_dw_hdcp14_get_access_ksv_memory_granted() == 0 && timeout--)
		asm volatile ("nop");

	if (_dw_hdcp14_get_access_ksv_memory_granted() == 0) {
		_dw_hdcp14_set_access_ksv_memory(false);
		hdcp_log("KSV List memory access denied");
		*param = 0;
		return HDCP_KSV_LIST_ERR_MEM_ACCESS;
	}

	/* 3 - Read VH', M0, Bstatus, and the KSV FIFO.
	The data is stored in the revocation memory, as */
	/* provided in the "Address Mapping for Maximum Memory Allocation"
	table in the databook. */
	bstatus = _dw_hdcp14_get_bstatus();
	deviceCount = bstatus & BSTATUS_DEVICE_COUNT_MASK;
	if (deviceCount > DW_HDCP14_MAX_DEVICES) {
		*param = 0;
		hdcp_log("depth exceeds KSV List memory");
		return HDCP_KSV_LIST_ERR_DEPTH_EXCEEDED;
	}

	size = deviceCount * DW_HDCP14_KSV_LEN + DW_HDCP14_KSV_HEADER + DW_HDCP14_KSV_SHAMAX;

	for (i = 0; i < size; i++) {
		if (i < DW_HDCP14_KSV_HEADER) { /* BSTATUS & M0 */
			hdcp_ksv_list_buffer[(deviceCount * DW_HDCP14_KSV_LEN) + i] =
			(u8)dw_read(HDCP_BSTATUS + (i * DW_HDCP14_ADDR_JUMP));
		} else if (i < (DW_HDCP14_KSV_HEADER + (deviceCount * DW_HDCP14_KSV_LEN))) { /* KSV list */
			hdcp_ksv_list_buffer[i - DW_HDCP14_KSV_HEADER] =
			(u8)dw_read(HDCP_BSTATUS + (i * DW_HDCP14_ADDR_JUMP));
		} else { /* SHA */
			hdcp_ksv_list_buffer[i] = (u8)dw_read(HDCP_BSTATUS + (i * DW_HDCP14_ADDR_JUMP));
		}
	}

	/* 4 - Calculate the SHA-1 checksum (VH) over M0,
		Bstatus, and the KSV FIFO. */
	if (_dw_hdcp14_verify_ksv(hdcp_ksv_list_buffer, size) == true) {
		valid = HDCP_KSV_LIST_READY;
		hdcp_log("HDCP_KSV_LIST_READY");
	} else {
		valid = HDCP_ERR_KSV_LIST_NOT_VALID;
		hdcp_log("HDCP_ERR_KSV_LIST_NOT_VALID");
	}

	/* 5 - If the calculated VH equals the VH',
	set a_ksvmemctrl.SHA1fail to 0 and set */
	/* a_ksvmemctrl.KSVCTRLupd to 1.
	If the calculated VH is different from VH' then set */
	/* a_ksvmemctrl.SHA1fail to 1 and set a_ksvmemctrl.KSVCTRLupd to 1,
	forcing the controller */
	/* to re-authenticate from the beginning. */
	_dw_hdcp14_set_access_ksv_memory(false);
	_dw_hdcp14_update_ksv_list_state((valid == HDCP_KSV_LIST_READY) ? 0 : 1);

	return valid;
}

/* do nor encry until stabilizing successful authentication */
static void _dw_hdcp14_check_engaged(void)
{
	if ((hdcp_status == HDCP_ENGAGED) && (hdcp_engaged_count >= 20)) {
		_dw_hdcp14_disable_encrypt(false);
		hdcp_engaged_count = 0;
		hdcp14_encryption  = 1;
		hdmi_inf("hdcp start encryption\n");
	}
}

u8 _dw_hdcp14_status_handler(int *param, u32 irq_stat)
{
	u8 interrupt_status = 0;
	int valid = HDCP_IDLE;

	log_trace();
	interrupt_status = irq_stat;

	if (interrupt_status != 0)
		hdcp_log("hdcp get interrupt state: 0x%x\n", interrupt_status);

	if (interrupt_status == 0) {
		if (hdcp_engaged_count && (hdcp_status == HDCP_ENGAGED)) {
			hdcp_engaged_count++;
			_dw_hdcp14_check_engaged();
		}

		return hdcp_status;
	}

	if ((interrupt_status & A_APIINTSTAT_KEEPOUTERRORINT_MASK) != 0) {
		hdmi_inf("hdcp status: keep out error interrupt\n");
		hdcp_status = HDCP_FAILED;

		hdcp_engaged_count = 0;
		return HDCP_FAILED;
	}

	if ((interrupt_status & A_APIINTSTAT_LOSTARBITRATION_MASK) != 0) {
		hdmi_inf("hdcp status: lost arbitration error interrupt\n");
		hdcp_status = HDCP_FAILED;

		hdcp_engaged_count = 0;
		return HDCP_FAILED;
	}

	if ((interrupt_status & A_APIINTSTAT_I2CNACK_MASK) != 0) {
		hdmi_inf("hdcp status: i2c nack error interrupt\n");
		hdcp_status = HDCP_FAILED;

		hdcp_engaged_count = 0;
		return HDCP_FAILED;
	}

	if (interrupt_status & A_APIINTSTAT_KSVSHA1CALCINT_MASK) {
		hdmi_inf("hdcp status: ksv sha1\n");
		return _dw_hdcp14_read_ksv_list(param);
	}

	if ((interrupt_status & A_APIINTSTAT_HDCP_FAILED_MASK) != 0) {
		*param = 0;
		hdmi_inf("hdcp status: failed\n");
		_dw_hdcp14_disable_encrypt(true);
		hdcp_status = HDCP_FAILED;

		hdcp_engaged_count = 0;
		return HDCP_FAILED;
	}

	if ((interrupt_status & A_APIINTSTAT_HDCP_ENGAGED_MASK) != 0) {
		*param = 1;
		hdmi_inf("hdcp status: engaged\n");

		hdcp_status = HDCP_ENGAGED;
		hdcp_engaged_count = 1;

		return HDCP_ENGAGED;
	}

	return valid;
}

static int _dw_hdcp14_status_check_and_handle(void)
{
	u8 hdcp14_status = 0;
	int param;
	u8 ret = 0;

	log_trace();

	if (!hdcp14_auth_enable) {
		hdmi_wrn("hdcp14 auth not enable!\n");
		return 0;
	}

	if (!hdcp14_auth_complete) {
		hdmi_wrn("hdcp14 auth not complete!\n");
		return 0;
	}

	hdcp14_status = dw_read(A_APIINTSTAT);
	_dw_hdcp14_interrupt_clear(hdcp14_status);

	ret = _dw_hdcp14_status_handler(&param, (u32)hdcp14_status);
	if ((ret != HDCP_ERR_KSV_LIST_NOT_VALID) && (ret != HDCP_FAILED))
		return 0;
	else
		return -1;
}

void dw_hdcp_config_init(void)
{
	/* init hdcp14 oess windows size is DW_HDCP14_OESS_SIZE */
	_dw_hdcp14_set_oess_size(DW_HDCP14_OESS_SIZE);

	/* frame composer keepout hdcp */
	dw_fc_video_set_hdcp_keepout(true);

	/* main control enable hdcp clock */
	dw_mc_disable_hdcp_clock(false);

	/* disable hdcp14 rx detect */
	_dw_hdcp14_rxdetect_enable(false);

	/* disable hdcp14 encrypt */
	_dw_hdcp14_disable_encrypt(true);

	/* disable hdcp data polarity */
	_dw_hdcp_enable_data_polarity(false);
}

static int _dw_hdcp14_start_auth(void)
{
	dw_tmds_mode_t mode = dw_fc_video_get_tmds_mode();
	u8 hsPol = dw_fc_video_get_hsync_polarity();
	u8 vsPol = dw_fc_video_get_vsync_polarity();
	u8 hdcp_mask = 0, data = 0;

	if (hdcp14_auth_complete) {
		hdmi_inf("hdcp14 has been auth done!\n");
		return -1;
	}

	msleep(20);

	/* disable encrypt */
	_dw_hdcp14_disable_encrypt(true);

	/* enable hdcp keepout */
	dw_fc_video_set_hdcp_keepout(true);

	/* config hdcp work mode */
	_dw_hdcp_set_tmds_mode((mode == DW_TMDS_MODE_HDMI) ? true : false);

	/* config Hsync and Vsync polarity and enable */
	_dw_hdcp_set_hsync_polarity((hsPol > 0) ? true : false);
	_dw_hdcp_set_vsync_polarity((vsPol > 0) ? true : false);
	_dw_hdcp_enable_data_polarity(true);

	/* bypass hdcp_block = 0 */
	dw_write_mask(0x4003, 1 << 5, 0x0);

	/* config use hdcp14 path */
	dw_hdcp_path_select(0x0);

	/* config hdcp14 feature11 */
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_EN11FEATURE_MASK, 0x0);

	/* config hdcp14 ri check mode */
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_SYNCRICHECK_MASK, 0x0);

	/* config hdcp14 i2c mode */
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_I2CFASTMODE_MASK, 0x0);

	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_ELVENA_MASK, 0x0);

	/* fixed */
	dw_hdcp_set_avmute_state(false);

	/* config send color when sending unencrtypted video data */
	dw_write_mask(A_VIDPOLCFG, A_VIDPOLCFG_UNENCRYPTCONF_MASK, 0x2);

	/* set enable encoding packet header */
	dw_write_mask(A_HDCPCFG1, A_HDCPCFG1_PH2UPSHFTENC_MASK, 0x1);

	/* config encryption oess size */
	_dw_hdcp14_set_oess_size(DW_HDCP14_OESS_SIZE);

	/* software reset hdcp14 engine */
	dw_write_mask(A_HDCPCFG1, A_HDCPCFG1_SWRESET_MASK, 0x0);

	/* enable hdcp14 rx detect for start auth */
	_dw_hdcp14_rxdetect_enable(true);

	hdcp_mask  = A_APIINTCLR_KSVACCESSINT_MASK;
	hdcp_mask |= A_APIINTCLR_KSVSHA1CALCINT_MASK;
	hdcp_mask |= A_APIINTCLR_KEEPOUTERRORINT_MASK;
	hdcp_mask |= A_APIINTCLR_LOSTARBITRATION_MASK;
	hdcp_mask |= A_APIINTCLR_I2CNACK_MASK;
	hdcp_mask |= A_APIINTCLR_HDCP_FAILED_MASK;
	hdcp_mask |= A_APIINTCLR_HDCP_ENGAGED_MASK;
	_dw_hdcp14_interrupt_clear(hdcp_mask);

	hdcp_mask  = A_APIINTMSK_KSVACCESSINT_MASK;
	hdcp_mask |= A_APIINTMSK_KSVSHA1CALCINT_MASK;
	hdcp_mask |= A_APIINTMSK_KEEPOUTERRORINT_MASK;
	hdcp_mask |= A_APIINTMSK_LOSTARBITRATION_MASK;
	hdcp_mask |= A_APIINTMSK_I2CNACK_MASK;
	hdcp_mask |= A_APIINTMSK_SPARE_MASK;
	hdcp_mask |= A_APIINTMSK_HDCP_FAILED_MASK;
	hdcp_mask |= A_APIINTMSK_HDCP_ENGAGED_MASK;
	data = (~hdcp_mask) & dw_read(A_APIINTMSK);
	dw_write(A_APIINTMSK, data);

	hdcp14_auth_enable = 1;
	hdcp14_auth_complete = 1;

	return 0;
}

int dw_hdcp14_config(void)
{
	int ret = 0;
	log_trace();
	hdcp_status = HDCP_FAILED;

	dw_hdcp_config_init();
	ret = _dw_hdcp14_start_auth();
	if (ret != 0) {
		hdmi_err("dw hdcp14 start auth failed\n");
		return -1;
	}

	hdcp14_enable = true;
	hdcp22_enable = false;
	return 0;
}

int dw_hdcp14_disconfig(void)
{
	/* 1. disable encryption */
	_dw_hdcp14_disable_encrypt(true);

	hdcp_status   = HDCP_FAILED;
	hdcp14_enable = false;
	hdcp14_auth_enable   = false;
	hdcp14_auth_complete = false;
	hdcp14_encryption    = false;
	hdcp_log("hdcp14 disconfig done!\n");
	return 0;
}

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
/* chose which way to enable hdcp22
* @enable: 0-chose to enable hdcp22 by dwc_hdmi inner signal ist_hdcp_capable
*              1-chose to enable hdcp22 by hdcp22_ovr_val bit
*/
void _dw_hdcp22_ovr_enable_avmute(u8 val, u8 enable)
{
	dw_write_mask(HDCP22REG_CTRL1,
		HDCP22REG_CTRL1_HDCP22_AVMUTE_OVR_VAL_MASK, val);
	dw_write_mask(HDCP22REG_CTRL1,
		HDCP22REG_CTRL1_HDCP22_AVMUTE_OVR_EN_MASK, enable);
}

/* chose what place hdcp22 hpd come from and config hdcp22 hpd enable or disable
* @val: 0 - hdcp22 hpd come from phy: phy_stat0.HPD
*       1 - hdcp22 hpd come from hpd_ovr_val
* @enable:hpd_ovr_val
*/
static void _dw_hdcp22_ovr_enable_hpd(u8 val, u8 enable)
{
	dw_write_mask(HDCP22REG_CTRL, HDCP22REG_CTRL_HPD_OVR_VAL_MASK, val);
	dw_write_mask(HDCP22REG_CTRL, HDCP22REG_CTRL_HPD_OVR_EN_MASK, enable);
}

static int _dw_hdcp22_status_check_and_handle(void)
{
	return dw_esm_status_check_and_handle();
}

u32 dw_hdcp22_get_fw_size(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_hdcp_s *hdcp = &hdmi->hdcp_dev;
	return hdcp->esm_firm_size;
}

unsigned long *dw_hdcp22_get_fw_addr(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_hdcp_s *hdcp = &hdmi->hdcp_dev;
	return &hdcp->esm_firm_vir_addr;
}

/* configure hdcp2.2 and enable hdcp2.2 encrypt */
int dw_hdcp22_config(void)
{
	dw_tmds_mode_t mode = dw_fc_video_get_tmds_mode();
	u8 hsPol = dw_fc_video_get_hsync_polarity();
	u8 vsPol = dw_fc_video_get_vsync_polarity();

	log_trace();

	dw_hdcp_config_init();

	/* 1 - set main controller hdcp clock disable */
	dw_hdcp22_data_enable(0);

	/* 2 - set hdcp keepout */
	dw_fc_video_set_hdcp_keepout(true);

	/* 3 - Select DVI or HDMI mode */
	_dw_hdcp_set_tmds_mode((mode == DW_TMDS_MODE_HDMI) ? 1 : 0);

	/* 4 - Set the Data enable, Hsync, and VSync polarity */
	_dw_hdcp_set_hsync_polarity((hsPol > 0) ? true : false);
	_dw_hdcp_set_vsync_polarity((vsPol > 0) ? true : false);
	_dw_hdcp_enable_data_polarity(true);

	dw_write_mask(0x4003, 1 << 5, 0x1);
	dw_hdcp_path_select(0x1);
	_dw_hdcp22_ovr_enable_hpd(1, 1);
	_dw_hdcp22_ovr_enable_avmute(0, 0);
	dw_write_mask(0x4003, 1 << 4, 0x1);

	/* mask the interrupt of hdcp22 event */
	dw_write_mask(HDCP22REG_MASK, 0xff, 0);

	if (dw_esm_open() < 0)
		return -1;

	hdcp14_enable = false;
	hdcp22_enable = true;

	return 0;
}

int dw_hdcp22_disconfig(void)
{
	dw_mc_disable_hdcp_clock(1);

	dw_write_mask(0x4003, 1 << 5, 0x0);
	dw_hdcp_path_select(0x0);
	_dw_hdcp22_ovr_enable_hpd(0, 1);
	dw_write_mask(0x4003, 1 << 4, 0x0);
	dw_esm_disable();

	hdcp22_enable = false;
	hdcp_log("hdcp22 disconfig done!\n");
	return 0;
}

void dw_hdcp22_data_enable(u8 enable)
{
	dw_mc_disable_hdcp_clock(enable ? 0x0 : 0x1);
	_dw_hdcp22_ovr_enable_avmute(enable ? 0x0 : 0x1, 0x1);
}

#endif

int dw_hdcp_get_state(void)
{
	if (hdcp22_enable)
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
		return _dw_hdcp22_status_check_and_handle();
#else
		return 0;
#endif
	else if (hdcp14_enable)
		return _dw_hdcp14_status_check_and_handle();
	else
		return 0;
}

void dw_hdcp_initial(void)
{
	dw_i2cm_re_init();

	/* enable hdcp14 bypass encryption */
	dw_write_mask(A_HDCPCFG0, A_HDCPCFG0_BYPENCRYPTION_MASK, 0x0);

	/* disable hdcp14 encryption */
	_dw_hdcp14_disable_encrypt(true);

	/* reset hdcp14 status flag */
	hdcp14_auth_enable   = 0;
	hdcp14_auth_complete = 0;
	hdcp14_encryption    = 0;
}

void dw_hdcp_exit(void)
{
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	dw_esm_exit();
#endif
}

ssize_t dw_hdcp_dump(char *buf)
{
	ssize_t n = 0;

	n += sprintf(buf + n, "[dw hdcp]:\n");

	if (!hdcp14_enable && !hdcp22_enable) {
		n += sprintf(buf + n, "hdcp not enable\n");
		return n;
	}

	n += sprintf(buf + n, " - tmds mode: %s\n",
		_dw_hdcp_get_tmds_mode() ? "hdmi" : "dvi");

	if (hdcp14_enable) {
		n += sprintf(buf + n, " - [hdcp14]\n");
		n += sprintf(buf + n, "    - auth state: %s\n",\
			hdcp14_auth_complete ? "complete" : "uncomplete");
		n += sprintf(buf + n, "    - sw encrty: %s\n",
			hdcp14_encryption ? "on" : "off");
		n += sprintf(buf + n, "    - hw encrty: %s\n",
			_dw_hdcp14_get_encryption() == 1 ? "off" : "on");
		n += sprintf(buf + n, "    - bksv: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
			_dw_hdcp14_get_bskv(0), _dw_hdcp14_get_bskv(1),
			_dw_hdcp14_get_bskv(2), _dw_hdcp14_get_bskv(3),
			_dw_hdcp14_get_bskv(4));
	}
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	if (hdcp22_enable) {
		n += sprintf(buf + n, " - [hdcp22]\n");
		n += dw_esm_dump(buf);
	}
#endif
	return n;
}
