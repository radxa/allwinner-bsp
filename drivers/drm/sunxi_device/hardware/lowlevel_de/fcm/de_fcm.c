/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*******************************************************************************
 *  All Winner Tech, All Right Reserved. 2014-2021 Copyright (c)
 *
 *  File name   :  display engine 35x fcm basic function definition
 *
 *  History     :  2021/11/30 v0.1  Initial version
 *
 ******************************************************************************/

#include <linux/mutex.h>
#include "de_fcm_type.h"
#include "de_fcm.h"

#define HUE_BIN_LEN	28
#define SAT_BIN_LEN	13
#define LUM_BIN_LEN	13
#define HBH_HUE_MASK	0x7fff
#define SBH_HUE_MASK	0x1fff
#define VBH_HUE_MASK	0x7ff
#define SAT_MASK	0xff
#define LUM_MASK	0xff

#define FCM_PARA_NUM (12)
#define FCM_SUM_NUM (28)
enum { FCM_PARA_REG_BLK = 0,
       FCM_ANGLE_REG_BLK,
       FCM_HUE_REG_BLK,
       FCM_SAT_REG_BLK,
       FCM_LUM_REG_BLK,
       FCM_CSC_REG_BLK,
       FCM_REG_BLK_NUM,
};

struct de_fcm_private {
	struct de_reg_mem_info reg_mem_info;
	struct de_reg_mem_info reg_shadow;
	u32 reg_blk_num;
	struct de_reg_block reg_blks[FCM_REG_BLK_NUM];
	struct de_reg_block shadow_blks[FCM_REG_BLK_NUM];
	struct mutex lock;

	int (*de_fcm_set_csc)(struct de_fcm_handle *hdl,
					   struct de_csc_info *in_info, struct de_csc_info *out_info);
	s32 (*de_fcm_enable)(struct de_fcm_handle *hdl, u32 en);
	s32 (*de_fcm_set_window)(struct de_fcm_handle *hdl, u32 demo_enable,
				u32 x, u32 y, u32 w, u32 h);
	s32 (*de_fcm_set_size)(struct de_fcm_handle *hdl, u32 width, u32 height);
	s32 (*de_fcm_dump_state)(struct drm_printer *p, struct de_fcm_handle *hdl);
	int (*de_fcm_apply_lut) (struct de_fcm_handle *hdl, fcm_hardware_data_t *data, unsigned int update);
};

struct de_fcm_handle *de35x_fcm_create(struct module_create_info *info);
static enum enhance_init_state g_init_state;
//static struct de_fcm_private fcm_priv[DE_NUM][VI_CHN_NUM];

struct de_fcm_handle *de_fcm_create(struct module_create_info *info)
{
	if (info->de_version == 0x350)
		return de35x_fcm_create(info);
	else
		return NULL;
}

s32 de_fcm_set_size(struct de_fcm_handle *hdl, u32 width, u32 height)
{
	if (hdl->private->de_fcm_set_size)
		return hdl->private->de_fcm_set_size(hdl, width, height);
	else
		return 0;
}

s32 de_fcm_set_window(struct de_fcm_handle *hdl, u32 demo_enable,
			u32 x, u32 y, u32 w, u32 h)
{
	if (hdl->private->de_fcm_set_window)
		return hdl->private->de_fcm_set_window(hdl, demo_enable, x, y, w, h);
	else
		return 0;
}

s32 de_fcm_enable(struct de_fcm_handle *hdl, u32 en)
{
	DRM_DEBUG_DRIVER("[SUNXI-DE] %s %d \n", __FUNCTION__, __LINE__);
	if (hdl->private->de_fcm_enable)
		return hdl->private->de_fcm_enable(hdl, en);
	else
		return 0;
}

s32 de_fcm_dump_state(struct drm_printer *p, struct de_fcm_handle *hdl)
{
	if (hdl->private->de_fcm_dump_state)
		return hdl->private->de_fcm_dump_state(p, hdl);
	else
		return 0;
}

s32 de_fcm_apply_lut(struct de_fcm_handle *hdl, fcm_hardware_data_t *data, unsigned int update)
{
	if (hdl->private->de_fcm_apply_lut)
		return hdl->private->de_fcm_apply_lut(hdl, data, update);
	else
		return 0;
}

int de_fcm_set_csc(struct de_fcm_handle *hdl, struct de_csc_info *in_info, struct de_csc_info *out_info)
{
	if (hdl->private->de_fcm_set_csc)
		return hdl->private->de_fcm_set_csc(hdl, in_info, out_info);
	else
		return 0;
}

static void fcm_set_block_dirty(
	struct de_fcm_private *priv, u32 blk_id, u32 dirty)
{
	priv->reg_blks[blk_id].dirty = dirty;
	if (priv->reg_blks[blk_id].rcq_hd)
		priv->reg_blks[blk_id].rcq_hd->dirty.dwval = dirty;
}

static void de_fcm_request_update(struct de_fcm_private *priv, u32 blk_id, u32 dirty)
{
	priv->shadow_blks[blk_id].dirty = dirty;
}

void de_fcm_update_regs(struct de_fcm_handle *hdl)
{
	u32 blk_id = 0;
	struct de_reg_block *shadow_block;
	struct de_reg_block *block;
	struct de_fcm_private *priv = hdl->private;

	mutex_lock(&priv->lock);
	for (blk_id = 0; blk_id < FCM_REG_BLK_NUM; blk_id++) {
		shadow_block = &(priv->shadow_blks[blk_id]);
		block = &(priv->reg_blks[blk_id]);

		if (shadow_block->dirty) {
			memcpy(block->vir_addr, shadow_block->vir_addr, shadow_block->size);
			DRM_DEBUG_DRIVER("[SUNXI-DE] %s %d blk_id:%d\n", __FUNCTION__, __LINE__, blk_id);
			fcm_set_block_dirty(priv, blk_id, 1);
			shadow_block->dirty = 0;
		}
	}
	mutex_unlock(&priv->lock);
}


static inline struct fcm_reg *de35x_get_fcm_reg(struct de_fcm_private *priv)
{
	return (struct fcm_reg *)(priv->reg_blks[FCM_PARA_REG_BLK].vir_addr);
}

static inline struct fcm_reg *de35x_get_fcm_shadow_reg(struct de_fcm_private *priv)
{
	return (struct fcm_reg *)(priv->shadow_blks[FCM_PARA_REG_BLK].vir_addr);
}


s32 de35x_fcm_set_size(struct de_fcm_handle *hdl, u32 width, u32 height)
{
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);

	mutex_lock(&priv->lock);
	reg->size.bits.width = width - 1;
	reg->size.bits.height = height - 1;
	de_fcm_request_update(priv, FCM_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

s32 de35x_fcm_set_window(struct de_fcm_handle *hdl, u32 demo_enable,
			u32 x, u32 y, u32 w, u32 h)
{
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);
	u32 fcm_en;

	/* if (g_init_state >= ENHANCE_TIGERLCD_ON) { */
		/* return 0; */
	/* } */
	mutex_lock(&priv->lock);
	fcm_en = reg->ctl.bits.fcm_en;
	reg->ctl.bits.window_en = fcm_en ? demo_enable : 0;
	if (demo_enable) {
		reg->win0.bits.win_left = x;
		reg->win1.bits.win_right = x + w - 1;
		reg->win0.bits.win_top = y;
		reg->win1.bits.win_bot = y + h - 1;
	}
	de_fcm_request_update(priv, FCM_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

s32 de35x_fcm_enable(struct de_fcm_handle *hdl, u32 en)
{
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);

	mutex_lock(&priv->lock);
	reg->ctl.bits.fcm_en = en;
	de_fcm_request_update(priv, FCM_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

s32 de35x_fcm_init(struct de_fcm_handle *hdl)
{
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);

	mutex_lock(&priv->lock);
	if (g_init_state == ENHANCE_TIGERLCD_ON) {
		/* sram need refresh, normal reg refreshed by system*/
		de_fcm_request_update(priv, FCM_ANGLE_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_HUE_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_SAT_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_LUM_REG_BLK, 1);
		mutex_unlock(&priv->lock);
		return 0;
	}
	//reg->ctl.dwval = 0x80000001;
	reg->ctl.dwval = 0x0;
	de_fcm_request_update(priv, FCM_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

static void fcm_set_csc_coeff(struct fcm_reg *reg,
	u32 *icsc_coeff, u32 *ocsc_coeff)
{
	u32 dwval0, dwval1, dwval2;
/*
	dwval0 = (icsc_coeff[12] >= 0) ? (icsc_coeff[12] & 0x3ff) :
		(0x400 - (u32)(icsc_coeff[12] & 0x3ff));
	dwval1 = (icsc_coeff[13] >= 0) ? (icsc_coeff[13] & 0x3ff) :
		(0x400 - (u32)(icsc_coeff[13] & 0x3ff));
	dwval2 = (icsc_coeff[14] >= 0) ? (icsc_coeff[14] & 0x3ff) :
		(0x400 - (u32)(icsc_coeff[14] & 0x3ff));
*/
	dwval0 = ((icsc_coeff[12] & 0x80000000) ? (u32)(-(s32)icsc_coeff[12]) : icsc_coeff[12]) & 0x3ff;
	dwval1 = ((icsc_coeff[13] & 0x80000000) ? (u32)(-(s32)icsc_coeff[13]) : icsc_coeff[13]) & 0x3ff;
	dwval2 = ((icsc_coeff[14] & 0x80000000) ? (u32)(-(s32)icsc_coeff[14]) : icsc_coeff[14]) & 0x3ff;

	reg->csc0_d0.dwval = dwval0;
	reg->csc0_d1.dwval = dwval1;
	reg->csc0_d2.dwval = dwval2;
	reg->csc0_c00.dwval = icsc_coeff[0];
	reg->csc0_c01.dwval = icsc_coeff[1];
	reg->csc0_c02.dwval = icsc_coeff[2];
	reg->csc0_c03.dwval = icsc_coeff[3];
	reg->csc0_c10.dwval = icsc_coeff[4];
	reg->csc0_c11.dwval = icsc_coeff[5];
	reg->csc0_c12.dwval = icsc_coeff[6];
	reg->csc0_c13.dwval = icsc_coeff[7];
	reg->csc0_c20.dwval = icsc_coeff[8];
	reg->csc0_c21.dwval = icsc_coeff[9];
	reg->csc0_c22.dwval = icsc_coeff[10];
	reg->csc0_c23.dwval = icsc_coeff[11];

/*
	dwval0 = (ocsc_coeff[12] >= 0) ? (ocsc_coeff[12] & 0x3ff) :
		(0x400 - (u32)(ocsc_coeff[12] & 0x3ff));
	dwval1 = (ocsc_coeff[13] >= 0) ? (ocsc_coeff[13] & 0x3ff) :
		(0x400 - (u32)(ocsc_coeff[13] & 0x3ff));
	dwval2 = (ocsc_coeff[14] >= 0) ? (ocsc_coeff[14] & 0x3ff) :
		(0x400 - (u32)(ocsc_coeff[14] & 0x3ff));
*/

	dwval0 = ((ocsc_coeff[12] & 0x80000000) ? (u32)(-(s32)ocsc_coeff[12]) : ocsc_coeff[12]) & 0x3ff;
	dwval1 = ((ocsc_coeff[13] & 0x80000000) ? (u32)(-(s32)ocsc_coeff[13]) : ocsc_coeff[13]) & 0x3ff;
	dwval2 = ((ocsc_coeff[14] & 0x80000000) ? (u32)(-(s32)ocsc_coeff[14]) : ocsc_coeff[14]) & 0x3ff;

	reg->csc1_d0.dwval = dwval0;
	reg->csc1_d1.dwval = dwval1;
	reg->csc1_d2.dwval = dwval2;
	reg->csc1_c00.dwval = ocsc_coeff[0];
	reg->csc1_c01.dwval = ocsc_coeff[1];
	reg->csc1_c02.dwval = ocsc_coeff[2];
	reg->csc1_c03.dwval = ocsc_coeff[3];
	reg->csc1_c10.dwval = ocsc_coeff[4];
	reg->csc1_c11.dwval = ocsc_coeff[5];
	reg->csc1_c12.dwval = ocsc_coeff[6];
	reg->csc1_c13.dwval = ocsc_coeff[7];
	reg->csc1_c20.dwval = ocsc_coeff[8];
	reg->csc1_c21.dwval = ocsc_coeff[9];
	reg->csc1_c22.dwval = ocsc_coeff[10];
	reg->csc1_c23.dwval = ocsc_coeff[11];
}

int de35x_fcm_set_csc(struct de_fcm_handle *hdl, struct de_csc_info *in_info, struct de_csc_info *out_info)
{
	struct de_csc_info icsc_out, ocsc_in;
	u32 *icsc_coeff, *ocsc_coeff;
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);

	mutex_lock(&priv->lock);
	icsc_out.eotf = in_info->eotf;
	memcpy((void *)&ocsc_in, out_info, sizeof(ocsc_in));

	if (in_info->px_fmt_space == DE_FORMAT_SPACE_RGB) {
		icsc_out.px_fmt_space = DE_FORMAT_SPACE_RGB;
		icsc_out.color_space = in_info->color_space;
		icsc_out.color_range = in_info->color_range;
		ocsc_in.color_range = DE_COLOR_RANGE_0_255;
		ocsc_in.px_fmt_space = DE_FORMAT_SPACE_RGB;
	} else if (in_info->px_fmt_space == DE_FORMAT_SPACE_YUV) {
		icsc_out.px_fmt_space = DE_FORMAT_SPACE_RGB;
		icsc_out.color_range = DE_COLOR_RANGE_0_255;
		if ((in_info->color_space != DE_COLOR_SPACE_BT2020NC)
				&& (in_info->color_space != DE_COLOR_SPACE_BT2020C))
					icsc_out.color_space = DE_COLOR_SPACE_BT709;
		else
				icsc_out.color_space = in_info->color_space;
		ocsc_in.color_range = DE_COLOR_RANGE_0_255;
		ocsc_in.px_fmt_space = DE_FORMAT_SPACE_RGB;
	} else {
			DRM_ERROR("px_fmt_space %d no support",
					in_info->px_fmt_space);
			mutex_unlock(&priv->lock);
			return -1;
	}

	de_csc_coeff_calc(in_info, &icsc_out, &icsc_coeff);
	de_csc_coeff_calc(&ocsc_in, out_info, &ocsc_coeff);
	fcm_set_csc_coeff(reg, icsc_coeff, ocsc_coeff);
	de_fcm_request_update(priv, FCM_CSC_REG_BLK, 1);
	mutex_unlock(&priv->lock);
	return 1;
}

static void de_fcm_set_hue_lut(struct fcm_reg *reg, unsigned int *h, unsigned int *s, unsigned int *v)
{
	int i;
	for (i = 0; i < HUE_BIN_LEN; i++) {
		reg->hbh_hue_lut[i].bits.hbh_hue_lut_low = h[i] & HBH_HUE_MASK;
		reg->sbh_hue_lut[i].bits.sbh_hue_lut_low = s[i] & SBH_HUE_MASK;
		reg->vbh_hue_lut[i].bits.vbh_hue_lut_low = v[i] & VBH_HUE_MASK;
		if (i == HUE_BIN_LEN - 1) {
			reg->hbh_hue_lut[i].bits.hbh_hue_lut_high = h[0] & HBH_HUE_MASK;
			reg->sbh_hue_lut[i].bits.sbh_hue_lut_high = s[0] & SBH_HUE_MASK;
			reg->vbh_hue_lut[i].bits.vbh_hue_lut_high = v[0] & VBH_HUE_MASK;
		} else {
			reg->hbh_hue_lut[i].bits.hbh_hue_lut_high = h[i+1] & HBH_HUE_MASK;
			reg->sbh_hue_lut[i].bits.sbh_hue_lut_high = s[i+1] & SBH_HUE_MASK;
			reg->vbh_hue_lut[i].bits.vbh_hue_lut_high = v[i+1] & VBH_HUE_MASK;
		}
	}
}

static void de_fcm_set_sat_lut(struct fcm_reg *reg, unsigned int *h, unsigned int *s, unsigned int *y)
{
	int i, j, m, n;
	for (i = 0; i < HUE_BIN_LEN; i++) {
		for (j = 0; j < SAT_BIN_LEN; j++) {
			n = j;
			m = i;

			reg->hbh_sat_lut[i * SAT_BIN_LEN + j].bits.hbh_sat_lut0 = h[i * SAT_BIN_LEN + j] & SAT_MASK;
			reg->sbh_sat_lut[i * SAT_BIN_LEN + j].bits.sbh_sat_lut0 = s[i * SAT_BIN_LEN + j] & SAT_MASK;
			reg->vbh_sat_lut[i * SAT_BIN_LEN + j].bits.vbh_sat_lut0 = y[i * SAT_BIN_LEN + j] & SAT_MASK;

			if (j == SAT_BIN_LEN - 1)
				n = j - 1;
			if (i == HUE_BIN_LEN - 1)
				m = -1;

			reg->hbh_sat_lut[i * SAT_BIN_LEN + j].bits.hbh_sat_lut1 = h[i * SAT_BIN_LEN + n + 1] & SAT_MASK;
			reg->sbh_sat_lut[i * SAT_BIN_LEN + j].bits.sbh_sat_lut1 = s[i * SAT_BIN_LEN + n + 1] & SAT_MASK;
			reg->vbh_sat_lut[i * SAT_BIN_LEN + j].bits.vbh_sat_lut1 = y[i * SAT_BIN_LEN + n + 1] & SAT_MASK;

			reg->hbh_sat_lut[i * SAT_BIN_LEN + j].bits.hbh_sat_lut2 = h[(m + 1) * SAT_BIN_LEN + j] & SAT_MASK;
			reg->sbh_sat_lut[i * SAT_BIN_LEN + j].bits.sbh_sat_lut2 = s[(m + 1) * SAT_BIN_LEN + j] & SAT_MASK;
			reg->vbh_sat_lut[i * SAT_BIN_LEN + j].bits.vbh_sat_lut2 = y[(m + 1) * SAT_BIN_LEN + j] & SAT_MASK;

			reg->hbh_sat_lut[i * SAT_BIN_LEN + j].bits.hbh_sat_lut3 = h[(m + 1) * SAT_BIN_LEN + n + 1] & SAT_MASK;
			reg->sbh_sat_lut[i * SAT_BIN_LEN + j].bits.sbh_sat_lut3 = s[(m + 1) * SAT_BIN_LEN + n + 1] & SAT_MASK;
			reg->vbh_sat_lut[i * SAT_BIN_LEN + j].bits.vbh_sat_lut3 = y[(m + 1) * SAT_BIN_LEN + n + 1] & SAT_MASK;
		}
	}
}

static void de_fcm_set_lum_lut(struct fcm_reg *reg, unsigned int *h, unsigned int *s, unsigned int *y)
{
	int i, j, m, n;
	for (i = 0; i < HUE_BIN_LEN; i++) {
		for (j = 0; j < LUM_BIN_LEN; j++) {
			n = j;
			m = i;

			reg->hbh_lum_lut[i * LUM_BIN_LEN + j].bits.hbh_lum_lut0 = h[i * LUM_BIN_LEN + j] & LUM_MASK;
			reg->sbh_lum_lut[i * LUM_BIN_LEN + j].bits.sbh_lum_lut0 = s[i * LUM_BIN_LEN + j] & LUM_MASK;
			reg->vbh_lum_lut[i * LUM_BIN_LEN + j].bits.vbh_lum_lut0 = y[i * LUM_BIN_LEN + j] & LUM_MASK;

			if (j == LUM_BIN_LEN - 1)
				n = j - 1;
			if (i == HUE_BIN_LEN - 1)
				m = -1;

			reg->hbh_lum_lut[i * LUM_BIN_LEN + j].bits.hbh_lum_lut1 = h[i * LUM_BIN_LEN + n + 1] & LUM_MASK;
			reg->sbh_lum_lut[i * LUM_BIN_LEN + j].bits.sbh_lum_lut1 = s[i * LUM_BIN_LEN + n + 1] & LUM_MASK;
			reg->vbh_lum_lut[i * LUM_BIN_LEN + j].bits.vbh_lum_lut1 = y[i * LUM_BIN_LEN + n + 1] & LUM_MASK;

			reg->hbh_lum_lut[i * LUM_BIN_LEN + j].bits.hbh_lum_lut2 = h[(m + 1) * LUM_BIN_LEN + j] & LUM_MASK;
			reg->sbh_lum_lut[i * LUM_BIN_LEN + j].bits.sbh_lum_lut2 = s[(m + 1) * LUM_BIN_LEN + j] & LUM_MASK;
			reg->vbh_lum_lut[i * LUM_BIN_LEN + j].bits.vbh_lum_lut2 = y[(m + 1) * LUM_BIN_LEN + j] & LUM_MASK;

			reg->hbh_lum_lut[i * LUM_BIN_LEN + j].bits.hbh_lum_lut3 = h[(m + 1) * LUM_BIN_LEN + n + 1] & LUM_MASK;
			reg->sbh_lum_lut[i * LUM_BIN_LEN + j].bits.sbh_lum_lut3 = s[(m + 1) * LUM_BIN_LEN + n + 1] & LUM_MASK;
			reg->vbh_lum_lut[i * LUM_BIN_LEN + j].bits.vbh_lum_lut3 = y[(m + 1) * LUM_BIN_LEN + n + 1] & LUM_MASK;
		}
	}
}

int de35x_fcm_apply_lut(struct de_fcm_handle *hdl, fcm_hardware_data_t *data, unsigned int update)
{
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);

	mutex_lock(&priv->lock);
	de_fcm_set_hue_lut(reg, data->hbh_hue, data->sbh_hue, data->ybh_hue);
	de_fcm_set_sat_lut(reg, data->hbh_sat, data->sbh_sat, data->ybh_sat);
	de_fcm_set_lum_lut(reg, data->hbh_lum, data->sbh_lum, data->ybh_lum);

	memcpy(reg->angle_hue_lut, data->angle_hue, sizeof(s32) * 28);
	memcpy(reg->angle_sat_lut, data->angle_sat, sizeof(s32) * 13);
	memcpy(reg->angle_lum_lut, data->angle_lum, sizeof(s32) * 13);

	if (update) {
		reg->ctl.bits.fcm_en = 1;
		reg->csc_ctl.dwval = 1;
		g_init_state = ENHANCE_TIGERLCD_ON;
		de_fcm_request_update(priv, FCM_PARA_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_ANGLE_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_HUE_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_SAT_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_LUM_REG_BLK, 1);
		de_fcm_request_update(priv, FCM_CSC_REG_BLK, 1);
	} else {
		de_fcm_request_update(priv, FCM_PARA_REG_BLK, 0);
		de_fcm_request_update(priv, FCM_ANGLE_REG_BLK, 0);
		de_fcm_request_update(priv, FCM_HUE_REG_BLK, 0);
		de_fcm_request_update(priv, FCM_SAT_REG_BLK, 0);
		de_fcm_request_update(priv, FCM_LUM_REG_BLK, 0);
		de_fcm_request_update(priv, FCM_CSC_REG_BLK, 0);
	}
	mutex_unlock(&priv->lock);
	return 0;
}

int de_fcm_get_lut(struct de_fcm_handle *hdl, fcm_hardware_data_t *data)
{
	struct de_fcm_private *priv = hdl->private;
	struct fcm_reg *reg = de35x_get_fcm_shadow_reg(priv);

	memcpy(data->hbh_hue, reg->hbh_hue_lut, sizeof(s32) * 28);
	memcpy(data->sbh_hue, reg->sbh_hue_lut, sizeof(s32) * 28);
	memcpy(data->ybh_hue, reg->vbh_hue_lut, sizeof(s32) * 28);

	memcpy(data->angle_hue, reg->angle_hue_lut, sizeof(s32) * 28);
	memcpy(data->angle_sat, reg->angle_sat_lut, sizeof(s32) * 13);
	memcpy(data->angle_lum, reg->angle_lum_lut, sizeof(s32) * 13);

	memcpy(data->hbh_sat, reg->hbh_sat_lut, sizeof(s32) * 364);
	memcpy(data->sbh_sat, reg->sbh_sat_lut, sizeof(s32) * 364);
	memcpy(data->ybh_sat, reg->vbh_sat_lut, sizeof(s32) * 364);

	memcpy(data->hbh_lum, reg->hbh_lum_lut, sizeof(s32) * 364);
	memcpy(data->sbh_lum, reg->sbh_lum_lut, sizeof(s32) * 364);
	memcpy(data->ybh_lum, reg->vbh_lum_lut, sizeof(s32) * 364);

	return 0;
}

struct de_fcm_handle *de35x_fcm_create(struct module_create_info *info)
{
	int i;
	struct de_fcm_handle *hdl;
	struct de_reg_block *block;
	struct de_reg_mem_info *reg_mem_info;
	struct de_reg_mem_info *reg_shadow;
	struct de_fcm_private *priv;
	u8 __iomem *reg_base;
	const struct de_fcm_desc *desc;

	desc = get_fcm_desc(info);
	if (!desc)
		return NULL;

	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	memcpy(&hdl->cinfo, info, sizeof(*info));

	reg_base = info->de_reg_base + info->reg_offset + desc->reg_offset;
	priv = hdl->private;
	reg_mem_info = &(priv->reg_mem_info);

	reg_mem_info->size = sizeof(struct fcm_reg);
	reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
		info->update_mode == RCQ_MODE);
	if (NULL == reg_mem_info->vir_addr) {
		DRM_ERROR("alloc bld[%d] mm fail!size=0x%x\n",
		     info->id, reg_mem_info->size);
		return ERR_PTR(-ENOMEM);
	}


	block = &(priv->reg_blks[FCM_PARA_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr;
	block->vir_addr = reg_mem_info->vir_addr;
	block->size = 0x20;
	block->reg_addr = reg_base;

	block = &(priv->reg_blks[FCM_ANGLE_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr + 0x20;
	block->vir_addr = reg_mem_info->vir_addr + 0x20;
	block->size = 0x100;
	block->reg_addr = reg_base + 0x20;

	block = &(priv->reg_blks[FCM_HUE_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr + 0x120;
	block->vir_addr = reg_mem_info->vir_addr + 0x120;
	block->size = 0x1e0;
	block->reg_addr = reg_base + 0x120;

	block = &(priv->reg_blks[FCM_SAT_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr + 0x300;
	block->vir_addr = reg_mem_info->vir_addr + 0x300;
	block->size = 0x1200;
	block->reg_addr = reg_base + 0x300;

	block = &(priv->reg_blks[FCM_LUM_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr + 0x1500;
	block->vir_addr = reg_mem_info->vir_addr + 0x1500;
	block->size = 0x1b00;
	block->reg_addr = reg_base + 0x1500;

	block = &(priv->reg_blks[FCM_CSC_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr + 0x3000;
	block->vir_addr = reg_mem_info->vir_addr + 0x3000;
	block->size = 0x80;
	block->reg_addr = reg_base + 0x3000;

	priv->reg_blk_num = FCM_REG_BLK_NUM;

	hdl->block_num = priv->reg_blk_num;
	hdl->block = kmalloc(sizeof(block[0]) * hdl->block_num, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < hdl->private->reg_blk_num; i++)
		hdl->block[i] = &priv->reg_blks[i];

	/* create shadow block */
	reg_shadow = &(priv->reg_shadow);

	reg_shadow->size = sizeof(struct fcm_reg);
	reg_shadow->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_shadow->size, (void *)&(reg_shadow->phy_addr),
		info->update_mode == RCQ_MODE);
	if (NULL == reg_shadow->vir_addr) {
		DRM_ERROR("alloc bld[%d] mm fail!size=0x%x\n",
		     info->id, reg_shadow->size);
		return ERR_PTR(-ENOMEM);
	}

	block = &(priv->shadow_blks[FCM_PARA_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr;
	block->vir_addr = reg_shadow->vir_addr;
	block->size = 0x20;

	block = &(priv->shadow_blks[FCM_ANGLE_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr + 0x20;
	block->vir_addr = reg_shadow->vir_addr + 0x20;
	block->size = 0x100;

	block = &(priv->shadow_blks[FCM_HUE_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr + 0x120;
	block->vir_addr = reg_shadow->vir_addr + 0x120;
	block->size = 0x1e0;

	block = &(priv->shadow_blks[FCM_SAT_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr + 0x300;
	block->vir_addr = reg_shadow->vir_addr + 0x300;
	block->size = 0x1200;

	block = &(priv->shadow_blks[FCM_LUM_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr + 0x1500;
	block->vir_addr = reg_shadow->vir_addr + 0x1500;
	block->size = 0x1b00;

	block = &(priv->shadow_blks[FCM_CSC_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr + 0x3000;
	block->vir_addr = reg_shadow->vir_addr + 0x3000;
	block->size = 0x80;

	g_init_state = ENHANCE_INVALID;

	mutex_init(&priv->lock);

	de35x_fcm_init(hdl);

	hdl->private->de_fcm_set_csc = de35x_fcm_set_csc;
	hdl->private->de_fcm_enable = de35x_fcm_enable;
	hdl->private->de_fcm_set_window = de35x_fcm_set_window;
	hdl->private->de_fcm_set_size = de35x_fcm_set_size;
	hdl->private->de_fcm_apply_lut = de35x_fcm_apply_lut;

	return hdl;
}
