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
 *  File name   :  display engine 35x sharp basic function definition
 *
 *  History     :  2021/11/30 v0.1  Initial version
 *
 ******************************************************************************/

#include <linux/mutex.h>
#include "de_sharp_type.h"
#include "de_sharp.h"

struct de_sharp_handle *de35x_sharp_create(struct module_create_info *info);
enum {
	SHARP_PARA_REG_BLK = 0,
	SHARP_REG_BLK_NUM,
};

struct de_sharp_private {
	struct de_reg_mem_info reg_mem_info;
	struct de_reg_mem_info reg_shadow;
	u32 reg_blk_num;
	struct de_reg_block reg_blks[SHARP_REG_BLK_NUM];
	struct de_reg_block shadow_blks[SHARP_REG_BLK_NUM];
	struct mutex lock;

	s32 (*de_sharp_enable)(struct de_sharp_handle *hdl, u32 enable);
	s32 (*de_sharp_set_size)(struct de_sharp_handle *hdl, u32 width, u32 height);
	s32 (*de_sharp_set_window)(struct de_sharp_handle *hdl,
		      u32 win_enable, u32 x, u32 y, u32 w, u32 h);
	s32 (*de_sharp_dump_state)(struct drm_printer *p, struct de_sharp_handle *hdl);
};

static enum enhance_init_state g_init_state;
static win_percent_t win_per;


struct de_sharp_handle *de_sharp_create(struct module_create_info *info)
{
	if (info->de_version == 0x350)
		return de35x_sharp_create(info);
	else
		return NULL;
}

s32 de_sharp_enable(struct de_sharp_handle *hdl, u32 en)
{
	DRM_DEBUG_DRIVER("[SUNXI-DE] %s %d \n", __FUNCTION__, __LINE__);
	if (hdl->private->de_sharp_enable)
		return hdl->private->de_sharp_enable(hdl, en);
	else
		return 0;
}

s32 de_sharp_dump_state(struct drm_printer *p, struct de_sharp_handle *hdl)
{
	if (hdl->private->de_sharp_dump_state)
		return hdl->private->de_sharp_dump_state(p, hdl);
	else
		return 0;
}

s32 de_sharp_set_size(struct de_sharp_handle *hdl, u32 width,
		    u32 height)
{
	if (hdl->private->de_sharp_set_size)
		return hdl->private->de_sharp_set_size(hdl, width, height);
	else
		return 0;
}

s32 de_sharp_set_window(struct de_sharp_handle *hdl,
		      u32 win_enable, u32 x, u32 y, u32 w, u32 h)
{
	if (hdl->private->de_sharp_set_window)
		return hdl->private->de_sharp_set_window(hdl, win_enable, x, y, w, h);
	else
		return 0;
}

static void sharp_set_block_dirty(
	struct de_sharp_private *priv, u32 blk_id, u32 dirty)
{
	priv->reg_blks[blk_id].dirty = dirty;
	if (priv->reg_blks[blk_id].rcq_hd)
		priv->reg_blks[blk_id].rcq_hd->dirty.dwval = dirty;
}

static void de_sharp_request_update(struct de_sharp_private *priv, u32 blk_id, u32 dirty)
{
	priv->shadow_blks[blk_id].dirty = dirty;
}

void de_sharp_update_regs(struct de_sharp_handle *hdl)
{
	u32 blk_id = 0;
	struct de_reg_block *shadow_block;
	struct de_reg_block *block;
	struct de_sharp_private *priv = hdl->private;

	mutex_lock(&priv->lock);
	for (blk_id = 0; blk_id < SHARP_REG_BLK_NUM; blk_id++) {
		shadow_block = &(priv->shadow_blks[blk_id]);
		block = &(priv->reg_blks[blk_id]);

		if (shadow_block->dirty) {
			memcpy(block->vir_addr, shadow_block->vir_addr, shadow_block->size);
			DRM_DEBUG_DRIVER("[SUNXI-DE] %s %d blk_id:%d\n", __FUNCTION__, __LINE__, blk_id);
			sharp_set_block_dirty(priv, blk_id, 1);
			shadow_block->dirty = 0;
		}
	}
	mutex_unlock(&priv->lock);
}

static inline struct sharp_reg *de35x_get_sharp_reg(struct de_sharp_private *priv)
{
	return (struct sharp_reg *)(priv->reg_blks[SHARP_PARA_REG_BLK].vir_addr);
}

static inline struct sharp_reg *de35x_get_sharp_shadow_reg(struct de_sharp_private *priv)
{
	return (struct sharp_reg *)(priv->shadow_blks[SHARP_PARA_REG_BLK].vir_addr);
}

s32 de35x_sharp_set_size(struct de_sharp_handle *hdl, u32 width,
		    u32 height)
{
	struct de_sharp_private *priv = hdl->private;
	struct sharp_reg *reg = de35x_get_sharp_shadow_reg(priv);

	mutex_lock(&priv->lock);
	reg->size.bits.width = width - 1;
	reg->size.bits.height = height - 1;
	de_sharp_request_update(priv, SHARP_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);
	return 0;
}

s32 de35x_sharp_set_window(struct de_sharp_handle *hdl,
		      u32 win_enable, u32 x, u32 y, u32 w, u32 h)
{
	struct de_sharp_private *priv = hdl->private;
	struct sharp_reg *reg = de35x_get_sharp_shadow_reg(priv);

	mutex_lock(&priv->lock);
	if (g_init_state >= ENHANCE_TIGERLCD_ON) {
		reg->demo_horz.bits.demo_horz_start =
		    x + reg->size.bits.width * win_per.hor_start / 100;
		reg->demo_horz.bits.demo_horz_end =
		    x + reg->size.bits.width * win_per.hor_end / 100;
		reg->demo_vert.bits.demo_vert_start =
		    y + reg->size.bits.height * win_per.ver_start / 100;
		reg->demo_vert.bits.demo_vert_end =
		    y + reg->size.bits.height * win_per.ver_end / 100;
	}
	if (win_enable) {
		reg->demo_horz.bits.demo_horz_start = x;
		reg->demo_horz.bits.demo_horz_end = x + w - 1;
		reg->demo_vert.bits.demo_vert_start = y;
		reg->demo_vert.bits.demo_vert_end = y + h - 1;
	}
	reg->ctrl.bits.demo_en = win_enable | win_per.demo_en;
	de_sharp_request_update(priv, SHARP_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

s32 de35x_sharp_enable(struct de_sharp_handle *hdl, u32 en)
{
	struct de_sharp_private *priv = hdl->private;
	struct sharp_reg *reg = de35x_get_sharp_shadow_reg(priv);

	mutex_lock(&priv->lock);
	if (g_init_state == ENHANCE_TIGERLCD_ON && en == 1)
		en = 1;
	else
		en = 0;

	reg->ctrl.bits.en = en;
	de_sharp_request_update(priv, SHARP_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

s32 de35x_sharp_init(struct de_sharp_handle *hdl)
{
	struct de_sharp_private *priv = hdl->private;
	struct sharp_reg *reg = de35x_get_sharp_shadow_reg(priv);

	if (g_init_state >= ENHANCE_INITED) {
		return 0;
	}

	mutex_lock(&priv->lock);
	g_init_state = ENHANCE_INITED;
	/*reg->ctrl.dwval = 0;*/
	reg->strengths.dwval = 0x101010;

	reg->extrema.dwval = 0x5;
	reg->edge_adaptive.dwval = 0x60810;
	reg->coring.dwval = 0x20002;
	reg->detail0_weight.dwval = 0x28;
	reg->horz_smooth.dwval = 0x6;
	reg->gaussian_coefs0.dwval = 0x31d40;
	reg->gaussian_coefs1.dwval = 0x20a1e2c;
	reg->gaussian_coefs2.dwval = 0x40e1c24;
	reg->gaussian_coefs3.dwval = 0x911181c;

	de_sharp_request_update(priv, SHARP_PARA_REG_BLK, 1);
	mutex_unlock(&priv->lock);

	return 0;
}

struct de_sharp_handle *de35x_sharp_create(struct module_create_info *info)
{
	struct de_sharp_handle *hdl;
	struct de_reg_block *block;
	struct de_reg_mem_info *reg_mem_info;
	struct de_reg_mem_info *reg_shadow;
	struct de_sharp_private *priv;
	u8 __iomem *reg_base;
	const struct de_sharp_desc *desc;
	u32 i;

	desc = get_sharp_desc(info);
	if (!desc)
		return NULL;

	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	memcpy(&hdl->cinfo, info, sizeof(*info));

	reg_base = info->de_reg_base + info->reg_offset + desc->reg_offset;
	priv = hdl->private;
	reg_mem_info = &(priv->reg_mem_info);

	reg_mem_info->size = sizeof(struct sharp_reg);
	reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
		info->update_mode == RCQ_MODE);
	if (NULL == reg_mem_info->vir_addr) {
		DRM_ERROR("alloc bld[%d] mm fail!size=0x%x\n",
		     info->id, reg_mem_info->size);
		return ERR_PTR(-ENOMEM);
	}

	block = &(priv->reg_blks[SHARP_PARA_REG_BLK]);
	block->phy_addr = reg_mem_info->phy_addr;
	block->vir_addr = reg_mem_info->vir_addr;
	block->size = 0x50;
	block->reg_addr = reg_base;

	priv->reg_blk_num = SHARP_REG_BLK_NUM;

	hdl->block_num = priv->reg_blk_num;
	hdl->block = kmalloc(sizeof(block[0]) * hdl->block_num, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < hdl->private->reg_blk_num; i++)
		hdl->block[i] = &priv->reg_blks[i];

	/* create shadow block */
	reg_shadow = &(priv->reg_shadow);

	reg_shadow->size = sizeof(struct sharp_reg);
	reg_shadow->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_shadow->size, (void *)&(reg_shadow->phy_addr),
		info->update_mode == RCQ_MODE);
	if (NULL == reg_shadow->vir_addr) {
		DRM_ERROR("alloc bld[%d] mm fail!size=0x%x\n",
		     info->id, reg_shadow->size);
		return ERR_PTR(-ENOMEM);
	}

	block = &(priv->shadow_blks[SHARP_PARA_REG_BLK]);
	block->phy_addr = reg_shadow->phy_addr;
	block->vir_addr = reg_shadow->vir_addr;
	block->size = 0x50;

	g_init_state = ENHANCE_INVALID;
	mutex_init(&priv->lock);

	de35x_sharp_init(hdl);

	priv->de_sharp_enable = de35x_sharp_enable;
	priv->de_sharp_set_size = de35x_sharp_set_size;
	priv->de_sharp_set_window = de35x_sharp_set_window;

	return hdl;
}

//int de_sharp_pq_proc(u32 sel, u32 cmd, u32 subcmd, void *data)
//{
//	struct de_sharp_private *priv = NULL;
//	struct sharp_reg *reg = NULL;
//	sharp_de35x_t *para = NULL;
//	int i = 0;
//
//	DE_INFO("sel=%d, cmd=%d, subcmd=%d, data=%px\n", sel, cmd, subcmd, data);
//	para = (sharp_de35x_t *)data;
//	if (para == NULL) {
//		DE_WARN("para NULL\n");
//		return -1;
//	}
//	for (i = 0; i < VI_CHN_NUM; i++) {
//		if (!de_feat_is_support_sharp_by_chn(sel, i))
//			continue;
//		priv = &(sharp_priv[sel][i]);
//		reg = de35x_get_sharp_reg(priv);
//
//		if (subcmd == 16) { /* read */
//			para->value[0] = reg->ctrl.bits.en;
//			/*para->value[1] = reg->ctrl.bits.demo_en;*/
//			para->value[2] = reg->horz_smooth.bits.hsmooth_en;
//			para->value[3] = reg->strengths.bits.strength_bp2;
//			para->value[4] = reg->strengths.bits.strength_bp1;
//			para->value[5] = reg->strengths.bits.strength_bp0;
//			para->value[6] = reg->d0_boosting.bits.d0_gain;
//			para->value[7] = reg->edge_adaptive.bits.edge_gain;
//			para->value[8] = reg->edge_adaptive.bits.weak_edge_th;
//			para->value[9] = reg->edge_adaptive.bits.edge_trans_width;
//			para->value[10] = reg->edge_adaptive.bits.min_sharp_strength;
//			para->value[11] = reg->overshoot_ctrl.bits.pst_up;
//			para->value[12] = reg->overshoot_ctrl.bits.pst_shift;
//			para->value[13] = reg->overshoot_ctrl.bits.neg_up;
//			para->value[14] = reg->overshoot_ctrl.bits.neg_shift;
//			para->value[15] = reg->d0_boosting.bits.d0_pst_level;
//			para->value[16] = reg->d0_boosting.bits.d0_neg_level;
//			para->value[17] = reg->coring.bits.zero;
//			para->value[18] = reg->coring.bits.width;
//			para->value[19] = reg->detail0_weight.bits.th_flat;
//			para->value[20] = reg->detail0_weight.bits.fw_type;
//			para->value[21] = reg->horz_smooth.bits.hsmooth_trans_width;
//			/*para->value[22] = reg->demo_horz.bits.demo_horz_start;
//			para->value[23] = reg->demo_horz.bits.demo_horz_end;
//			para->value[24] = reg->demo_vert.bits.demo_vert_start;
//			para->value[25] = reg->demo_vert.bits.demo_vert_end;*/
//			para->value[22] = win_per.hor_start;
//			para->value[23] = win_per.hor_end;
//			para->value[24] = win_per.ver_start;
//			para->value[25] = win_per.ver_end;
//			para->value[1] = win_per.demo_en;
//
//		} else {
//			reg->ctrl.bits.en = para->value[0];
//			g_init_state = para->value[0] ? ENHANCE_TIGERLCD_ON : ENHANCE_TIGERLCD_OFF;
//			/*reg->ctrl.bits.demo_en = para->value[1];*/
//			reg->horz_smooth.bits.hsmooth_en = para->value[2];
//			reg->strengths.bits.strength_bp2 = para->value[3];
//			reg->strengths.bits.strength_bp1 = para->value[4];
//			reg->strengths.bits.strength_bp0 = para->value[5];
//			reg->d0_boosting.bits.d0_gain = para->value[6];
//			reg->edge_adaptive.bits.edge_gain = para->value[7];
//			reg->edge_adaptive.bits.weak_edge_th = para->value[8];
//			reg->edge_adaptive.bits.edge_trans_width = para->value[9];
//			reg->edge_adaptive.bits.min_sharp_strength = para->value[10];
//			reg->overshoot_ctrl.bits.pst_up = para->value[11];
//			reg->overshoot_ctrl.bits.pst_shift = para->value[12];
//			reg->overshoot_ctrl.bits.neg_up = para->value[13];
//			reg->overshoot_ctrl.bits.neg_shift = para->value[14];
//			reg->d0_boosting.bits.d0_pst_level = para->value[15];
//			reg->d0_boosting.bits.d0_neg_level = para->value[16];
//			reg->coring.bits.zero = para->value[17];
//			reg->coring.bits.width = para->value[18];
//			reg->detail0_weight.bits.th_flat = para->value[19];
//			reg->detail0_weight.bits.fw_type = para->value[20];
//			reg->horz_smooth.bits.hsmooth_trans_width = para->value[21];
//			/*reg->demo_horz.bits.demo_horz_start = para->value[22];
//			reg->demo_horz.bits.demo_horz_end = para->value[23];
//			reg->demo_vert.bits.demo_vert_start = para->value[24];
//			reg->demo_vert.bits.demo_vert_end = para->value[25];*/
//			win_per.hor_start = para->value[22];
//			win_per.hor_end = para->value[23];
//			win_per.ver_start = para->value[24];
//			win_per.ver_end = para->value[25];
//			win_per.demo_en = para->value[1];
//			reg->ctrl.bits.demo_en = win_per.demo_en;
//			reg->demo_horz.bits.demo_horz_start =
//			    reg->size.bits.width * win_per.hor_start / 100;
//			reg->demo_horz.bits.demo_horz_end =
//			    reg->size.bits.width * win_per.hor_end / 100;
//			reg->demo_vert.bits.demo_vert_start =
//			    reg->size.bits.height * win_per.ver_start / 100;
//			reg->demo_vert.bits.demo_vert_end =
//			    reg->size.bits.height * win_per.ver_end / 100;
//			sharp_set_block_dirty(priv, SHARP_PARA_REG_BLK, 1);
//		}
//	}
//	return 0;
//}
