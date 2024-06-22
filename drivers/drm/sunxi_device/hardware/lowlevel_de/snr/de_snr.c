/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * de_snr.c
 *
 * Copyright (c) 2007-2018 Allwinnertech Co., Ltd.
 * Author: zhengxiaobin <zhengxiaobin@allwinnertech.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <drm/drm_framebuffer.h>
#include <linux/mutex.h>
#include <drm/drm_fourcc.h>
#include "de_snr_type.h"
#include "de_snr.h"

struct de_snr_handle *de35x_snr_create(struct module_create_info *info);

enum {
	SNR_REG_BLK_CTL = 0,
	SNR_REG_BLK_NUM,
};

struct de_snr_private {
	struct de_reg_mem_info reg_mem_info;
	struct de_reg_mem_info reg_shadow;
	u32 reg_blk_num;
	struct de_reg_block reg_blks[SNR_REG_BLK_NUM];
	struct de_reg_block shadow_blks[SNR_REG_BLK_NUM];
	struct mutex lock;

	s32 (*de_snr_enable)(struct de_snr_handle *hdl, u32 snr_enable);
	s32 (*de_snr_set_para)(struct de_snr_handle *hdl, struct display_channel_state *state, struct de_snr_para *snr_para);
	s32 (*de_snr_dump_state)(struct de_snr_handle *hdl);
};

//static enum enhance_init_state g_init_state;
//static win_percent_t win_per;

struct de_snr_handle *de_snr_create(struct module_create_info *info)
{
	if (info->de_version == 0x350)
		return de35x_snr_create(info);
	else
		return NULL;
}

s32 de_snr_enable(struct de_snr_handle *hdl, u32 snr_enable)
{
	DRM_DEBUG_DRIVER("[SUNXI-DE] %s %d \n", __FUNCTION__, __LINE__);
	if (hdl->private->de_snr_enable)
		return hdl->private->de_snr_enable(hdl, snr_enable);
	else
		return 0;
}

s32 de_snr_set_para(struct de_snr_handle *hdl, struct display_channel_state *state, struct de_snr_para *snr_para)
{
	if (hdl->private->de_snr_set_para)
		return hdl->private->de_snr_set_para(hdl, state, snr_para);
	else
		return 0;
}

s32 de_snr_dump_state(struct de_snr_handle *hdl)
{
	if (hdl->private->de_snr_dump_state)
		return hdl->private->de_snr_dump_state(hdl);
	else
		return 0;
}

static void snr_set_block_dirty(
	struct de_snr_private *priv, u32 blk_id, u32 dirty)
{
	priv->reg_blks[blk_id].dirty = dirty;
	if (priv->reg_blks[blk_id].rcq_hd)
		priv->reg_blks[blk_id].rcq_hd->dirty.dwval = dirty;
}

static void de_snr_request_update(struct de_snr_private *priv, u32 blk_id, u32 dirty)
{
	priv->shadow_blks[blk_id].dirty = dirty;
}

void de_snr_update_regs(struct de_snr_handle *hdl)
{
	u32 blk_id = 0;
	struct de_reg_block *shadow_block;
	struct de_reg_block *block;
	struct de_snr_private *priv = hdl->private;

	mutex_lock(&priv->lock);
	for (blk_id = 0; blk_id < SNR_REG_BLK_NUM; blk_id++) {
		shadow_block = &(priv->shadow_blks[blk_id]);
		block = &(priv->reg_blks[blk_id]);

		if (shadow_block->dirty) {
			memcpy(block->vir_addr, shadow_block->vir_addr, shadow_block->size);
			DRM_DEBUG_DRIVER("[SUNXI-DE] %s %d blk_id:%d\n", __FUNCTION__, __LINE__, blk_id);
			snr_set_block_dirty(priv, blk_id, 1);
			shadow_block->dirty = 0;
		}
	}
	mutex_unlock(&priv->lock);
}

static inline struct snr_reg *de35x_get_snr_reg(struct de_snr_private *priv)
{
	return (struct snr_reg *)(priv->reg_blks[0].vir_addr);
}

static inline struct snr_reg *de35x_get_snr_shadow_reg(struct de_snr_private *priv)
{
	return (struct snr_reg *)(priv->shadow_blks[0].vir_addr);
}


s32 de35x_snr_enable(struct de_snr_handle *hdl, u32 snr_enable)
{
	struct de_snr_private *priv = hdl->private;
	struct snr_reg *reg = de35x_get_snr_shadow_reg(priv);

	mutex_lock(&priv->lock);
	if (!snr_enable)
		reg->snr_ctrl.bits.en = 0;
	else
		reg->snr_ctrl.bits.en = 1;
	de_snr_request_update(priv, SNR_REG_BLK_CTL, 1);
	mutex_unlock(&priv->lock);
	return 0;

}

s32 de35x_snr_set_para(struct de_snr_handle *hdl, struct display_channel_state *state, struct de_snr_para *snr_para)
{
	struct de_snr_private *priv = hdl->private;
	struct snr_reg *reg = de35x_get_snr_shadow_reg(priv);
	u32 i = 0;
	u32 width, height;
	struct drm_framebuffer *fb;
	struct de_snr_para para;
	int fmt;

	/* use the param delivered from userspace */
	memset(&para, 0, sizeof(struct de_snr_para));
	memcpy(&para, snr_para, sizeof(*snr_para));

	fb = state->base.fb;
	width = state->base.src_w >> 16;
	height = state->base.src_h >> 16;

	if (!fb) {
		reg->snr_ctrl.bits.en = 0;
		DRM_DEBUG_DRIVER("[SUNXI-SNR] channel:%d %s %d \n", i, __FUNCTION__, __LINE__);
		//reg->snr_ctrl.bits.demo_en = 0;
		goto DIRTY;
	}

	fmt = drm_to_de_format(fb->format->format);

	if (fmt != DE_FORMAT_YUV422_SP_UVUV &&
	    fmt != DE_FORMAT_YUV422_SP_VUVU &&
	    fmt != DE_FORMAT_YUV420_SP_UVUV &&
	    fmt != DE_FORMAT_YUV420_SP_VUVU &&
	    fmt != DE_FORMAT_YUV422_P &&
	    fmt != DE_FORMAT_YUV420_P &&
	    fmt != DE_FORMAT_YUV422_SP_UVUV_10BIT &&
	    fmt != DE_FORMAT_YUV422_SP_VUVU_10BIT &&
	    fmt != DE_FORMAT_YUV420_SP_UVUV_10BIT &&
	    fmt != DE_FORMAT_YUV420_SP_VUVU_10BIT) {
		reg->snr_ctrl.bits.en = 0;
		//reg->snr_ctrl.bits.demo_en = 0;
		goto DIRTY;
	}

	if (para.b_trd_out) {
		uint32_t buffer_flags = para.flags;
		long long w = width;
		long long h = height;

		switch (buffer_flags) {
		// When 3D output enable, the video channel's output height
		// is twice of the input layer crop height.
		case DISP_BF_STEREO_TB:
		case DISP_BF_STEREO_SSH:
			h = h * 2;
			break;
		}
		reg->snr_size.bits.width  = w - 1;
		reg->snr_size.bits.height = h - 1;
	} else {
		reg->snr_size.bits.width =  width - 1;
		reg->snr_size.bits.height = height - 1;
	}

	reg->snr_ctrl.bits.en = para.en;
	reg->snr_ctrl.bits.demo_en = para.demo_en;
	reg->demo_win_hor.bits.demo_horz_start = para.demo_x;
	reg->demo_win_hor.bits.demo_horz_end = para.demo_x + para.demo_width;
	reg->demo_win_ver.bits.demo_vert_start = para.demo_y;
	reg->demo_win_ver.bits.demo_vert_end = para.demo_y + para.demo_height;
	reg->snr_strength.bits.strength_y = para.y_strength;
	reg->snr_strength.bits.strength_u = para.u_strength;
	reg->snr_strength.bits.strength_v = para.v_strength;
	reg->snr_line_det.bits.th_hor_line = (para.th_hor_line) ? para.th_hor_line : 0xa;
	reg->snr_line_det.bits.th_ver_line = (para.th_ver_line) ? para.th_hor_line : 0xa;
	//support TIGER LCD later

DIRTY:
	de_snr_request_update(priv, SNR_REG_BLK_CTL, 1);
	mutex_unlock(&priv->lock);

	return 0;

}

struct de_snr_handle *de35x_snr_create(struct module_create_info *info)
{
	int i;
	struct de_snr_handle *hdl;
	struct de_reg_block *block;
	struct de_reg_mem_info *reg_mem_info;
	struct de_reg_mem_info *reg_shadow;
	struct de_snr_private *priv;
	u8 __iomem *reg_base;
	const struct de_snr_desc *desc;

	desc = get_snr_desc(info);
	if (!desc)
		return NULL;

	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	memcpy(&hdl->cinfo, info, sizeof(*info));

	reg_base = info->de_reg_base + info->reg_offset + desc->reg_offset;
	priv = hdl->private;
	reg_mem_info = &(priv->reg_mem_info);

	reg_mem_info->size = sizeof(struct snr_reg);
	reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
		info->update_mode == RCQ_MODE);
	if (NULL == reg_mem_info->vir_addr) {
		DRM_ERROR("alloc snr[%d] mm fail!size=0x%x\n",
		     info->id, reg_mem_info->size);
		return ERR_PTR(-ENOMEM);
	}


	block = &(priv->reg_blks[SNR_REG_BLK_CTL]);
	block->phy_addr = reg_mem_info->phy_addr;
	block->vir_addr = reg_mem_info->vir_addr;
	block->size = (info->id == 0) ? 0x140 : 0x04;
	block->reg_addr = reg_base;

	priv->reg_blk_num = SNR_REG_BLK_NUM;

	hdl->block_num = priv->reg_blk_num;
	hdl->block = kmalloc(sizeof(block[0]) * hdl->block_num, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < hdl->private->reg_blk_num; i++)
		hdl->block[i] = &priv->reg_blks[i];


	/* create shadow block */
	reg_shadow = &(priv->reg_shadow);

	reg_shadow->size = sizeof(struct snr_reg);
	reg_shadow->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_shadow->size, (void *)&(reg_shadow->phy_addr),
		info->update_mode == RCQ_MODE);
	if (NULL == reg_shadow->vir_addr) {
		DRM_ERROR("alloc snr[%d] mm fail!size=0x%x\n",
		     info->id, reg_shadow->size);
		return ERR_PTR(-ENOMEM);
	}


	block = &(priv->shadow_blks[SNR_REG_BLK_CTL]);
	block->phy_addr = reg_shadow->phy_addr;
	block->vir_addr = reg_shadow->vir_addr;
	block->size = (info->id == 0) ? 0x140 : 0x04;

	mutex_init(&priv->lock);

	hdl->private->de_snr_enable = de35x_snr_enable;
	hdl->private->de_snr_set_para = de35x_snr_set_para;

	return hdl;
}

//int de_snr_pq_proc(u32 sel, u32 cmd, u32 subcmd, void *data)
//{
//	struct de_snr_private *priv = NULL;
//	struct snr_reg *reg = NULL;
//	snr_module_param_t *para = NULL;
//	int chn_num = 0, chn = 0;
//
//	DE_INFO("sel=%d, cmd=%d, subcmd=%d, data=%px\n", sel, cmd, subcmd, data);
//	para = (snr_module_param_t *)data;
//	if (para == NULL) {
//		DE_WARN("para NULL\n");
//		return -1;
//	}
//
//	chn_num = de_feat_get_num_chns(sel);
//	for (chn = 0; chn < chn_num; ++chn) {
//		if (!de_feat_is_support_snr_by_chn(sel, chn))
//			continue;
//		priv = &(snr_priv[sel][chn]);
//		reg = de35x_get_snr_reg(priv);
//
//		if (subcmd == 16) { /* read */
//			//para->value[0] = reg->snr_ctrl.bits.en;
//			para->value[0] = (g_init_state == ENHANCE_TIGERLCD_ON ? 1 : 0);
//			/*para->value[1] = reg->snr_ctrl.bits.demo_en;*/
//			para->value[2] = reg->snr_strength.bits.strength_y;
//			para->value[3] = reg->snr_strength.bits.strength_u;
//			para->value[4] = reg->snr_strength.bits.strength_v;
//			para->value[5] = reg->snr_line_det.bits.th_ver_line;
//			para->value[6] = reg->snr_line_det.bits.th_hor_line;
//			/*para->value[7] = reg->demo_win_hor.bits.demo_horz_start;
//			para->value[8] = reg->demo_win_hor.bits.demo_horz_end;
//			para->value[9] = reg->demo_win_ver.bits.demo_vert_start;
//			para->value[10] = reg->demo_win_ver.bits.demo_vert_end;*/
//			para->value[7] = win_per.hor_start;
//			para->value[8] = win_per.hor_end;
//			para->value[9] = win_per.ver_start;
//			para->value[10] = win_per.ver_end;
//			para->value[1] = win_per.demo_en;
//		} else {
//			g_init_state = para->value[0] ? ENHANCE_TIGERLCD_ON : ENHANCE_TIGERLCD_OFF;
//			/* cannot open snr when rgb fmt, should opened by layer para*/
//			//reg->snr_ctrl.bits.en = para->value[0];
//			/*reg->snr_ctrl.bits.demo_en = para->value[1];*/
//			reg->snr_strength.bits.strength_y = para->value[2];
//			reg->snr_strength.bits.strength_u = para->value[3];
//			reg->snr_strength.bits.strength_v = para->value[4];
//			reg->snr_line_det.bits.th_ver_line = para->value[5];
//			reg->snr_line_det.bits.th_hor_line = para->value[6];
//			/*reg->demo_win_hor.bits.demo_horz_start = para->value[7];
//			reg->demo_win_hor.bits.demo_horz_end = para->value[8];
//			reg->demo_win_ver.bits.demo_vert_start = para->value[9];
//			reg->demo_win_ver.bits.demo_vert_end = para->value[10];*/
//			win_per.hor_start = para->value[7];
//			win_per.hor_end = para->value[8];
//			win_per.ver_start = para->value[9];
//			win_per.ver_end = para->value[10];
//			win_per.demo_en = para->value[1];
//			reg->demo_win_hor.bits.demo_horz_start =
//			    reg->snr_size.bits.width * win_per.hor_start / 100;
//			reg->demo_win_hor.bits.demo_horz_end =
//			    reg->snr_size.bits.width * win_per.hor_end / 100;
//			reg->demo_win_ver.bits.demo_vert_start =
//			    reg->snr_size.bits.height * win_per.ver_start / 100;
//			reg->demo_win_ver.bits.demo_vert_end =
//			    reg->snr_size.bits.height * win_per.ver_end / 100;
//			reg->snr_ctrl.bits.demo_en = win_per.demo_en;
//			priv->set_blk_dirty(priv, SNR_REG_BLK_CTL, 1);
//		}
//	}
//	return 0;
//}
