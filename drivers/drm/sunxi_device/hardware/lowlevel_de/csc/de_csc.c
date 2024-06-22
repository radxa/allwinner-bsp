/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2017 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/mutex.h>
#include "de_csc_platform.h"
#include "de_csc_type.h"
#include "de_csc_table.h"
#include "de_csc.h"

/* TODO: unify de_csc2_apply/de35x_csc_apply/de_dcsc_apply and their calculate func, simplify csc tbl
 */

struct de_csc_handle *de35x_csc_create(struct module_create_info *info);

/*
   it seems that rcq can not update reg < 16byte, be careful.
*/
enum {
	CSC_REG_BLK_CTL = 0,
	CSC_REG_BLK_NUM,
};

struct csc_debug_info {
	bool enable;
	struct de_csc_info in_info;
	struct de_csc_info out_info;
};

struct de_csc_private {
	struct de_reg_mem_info reg_mem_info;
	struct csc_debug_info debug;
	bool reg_update;
	struct bcsh_info bcsh;
	const struct de_csc_desc *desc;
	u32 reg_blk_num;
	struct de_reg_block reg_blks[CSC_REG_BLK_NUM];
	s32 (*de_csc_enable)(struct de_csc_handle *hdl, u32 en);
	s32 (*de_csc_apply)(struct de_csc_handle *hdl, const struct de_csc_info *in_info,
			     const struct de_csc_info *out_info, bool en);
	void (*de_csc_dump_state)(struct drm_printer *p, struct de_csc_handle *hdl);
};

static int de_csc2_apply(struct de_csc_handle *hdl,
	const struct de_csc_info *in_info, const struct de_csc_info *out_info, bool en);
static int de_ccsc_de2_apply(struct de_csc_handle *hdl,
	const struct de_csc_info *in_info, const struct de_csc_info *out_info, bool en);

struct de_csc_handle *de_csc_create(struct module_create_info *info)
{
	return de35x_csc_create(info);
}

s32 de_csc_enable(struct de_csc_handle *hdl, u32 en)
{
	WARN_ON(!hdl->private->de_csc_enable);
	return hdl->private->de_csc_enable(hdl, en);
}

void de_csc_dump_state(struct drm_printer *p, struct de_csc_handle *hdl)
{
	if (hdl->private->de_csc_dump_state)
		hdl->private->de_csc_dump_state(p, hdl);
}

s32 de_csc_apply(struct de_csc_handle *hdl,
	struct de_csc_info *in_info, struct de_csc_info *out_info, bool en)
{
	if (hdl->private->de_csc_apply)
		return hdl->private->de_csc_apply(hdl, in_info, out_info, en);
	else
		return 0;
}

static void csc_set_block_dirty(
	struct de_csc_private *priv, u32 blk_id, u32 dirty)
{
	WARN_ON(!priv->reg_update);
	priv->reg_blks[blk_id].dirty = dirty;
	if (priv->reg_blks[blk_id].rcq_hd)
		priv->reg_blks[blk_id].rcq_hd->dirty.dwval = dirty;
}

void de_csc_update_regs(struct de_csc_handle *hdl)
{
}

static inline void *de35x_get_csc_reg(struct de_csc_private *priv)
{
	WARN_ON(!priv->reg_update);
	return (void *)(priv->reg_blks[0].vir_addr);
}

s32 de35x_csc_enable(struct de_csc_handle *hdl, u32 en)
{
	struct de_csc_private *priv = hdl->private;
	struct csc_reg *reg;
	if (!priv->reg_update)
		return -1;

	reg  = de35x_get_csc_reg(priv);

	reg->ctl.dwval = en;
	csc_set_block_dirty(priv, CSC_REG_BLK_CTL, 1);
	return 0;
}

static const char *to_format_space_name(enum de_format_space format_space)
{
	switch (format_space) {
	case DE_FORMAT_SPACE_RGB:
		return "rgb";
	case DE_FORMAT_SPACE_YUV:
		return "yuv";
	case DE_FORMAT_SPACE_IPT:
		return "ipt";
	case DE_FORMAT_SPACE_GRAY:
		return "gray";
	}
	return "invalid";
}

static const char *to_eotf_name(enum de_eotf eotf)
{
	switch (eotf) {
	case DE_EOTF_RESERVED:
		return "reserve";
	case DE_EOTF_BT709:
		return "bt709";
	case DE_EOTF_UNDEF:
		return "undef";
	case DE_EOTF_GAMMA22:
		return "gamma22";
	case DE_EOTF_GAMMA28:
		return "gamma28";
	case DE_EOTF_BT601:
		return "bt601";
	case DE_EOTF_SMPTE240M:
		return "smpte240m";
	case DE_EOTF_LINEAR:
		return "linear";
	case DE_EOTF_LOG100:
		return "log100";
	case DE_EOTF_LOG100S10:
		return "log100s10";
	case DE_EOTF_IEC61966_2_4:
		return "iec61966_2_4";
	case DE_EOTF_BT1361:
		return "bt1361";
	case DE_EOTF_IEC61966_2_1:
		return "iec61966_2_1";
	case DE_EOTF_BT2020_0:
		return "bt2020_0";
	case DE_EOTF_BT2020_1:
		return "bt2020_1";
	case DE_EOTF_SMPTE2084:
		return "smpte2084";
	case DE_EOTF_SMPTE428_1:
		return "smpte428_1";
	case DE_EOTF_ARIB_STD_B67:
		return "arib_std_b67";
	}
	return "invalid";
}

static const char *to_color_space_name(enum de_color_space cs)
{
	switch (cs) {
	case DE_COLOR_SPACE_GBR:
		return "GBR";
	case DE_COLOR_SPACE_BT709:
		return "BT709";
	case DE_COLOR_SPACE_FCC:
		return "FCC";
	case DE_COLOR_SPACE_BT470BG:
		return "BT470BG";
	case DE_COLOR_SPACE_BT601:
		return "BT601";
	case DE_COLOR_SPACE_SMPTE240M:
		return "SMPTE240M";
	case DE_COLOR_SPACE_YCGCO:
		return "YCGCO";
	case DE_COLOR_SPACE_BT2020NC:
		return "BT2020NC";
	case DE_COLOR_SPACE_BT2020C:
		return "BT2020C";
	}
	return "invalid";
}

static const char *to_color_range_name(enum de_color_range range)
{
	switch (range) {
	case DE_COLOR_RANGE_DEFAULT:
		return "default";
	case DE_COLOR_RANGE_0_255:
		return "full";
	case DE_COLOR_RANGE_16_235:
		return "limited";
	}
	return "invalid";
}

void de35x_csc_dump_state(struct drm_printer *p, struct de_csc_handle *hdl)
{
	struct csc_debug_info *debug = &hdl->private->debug;
	unsigned long base = (unsigned long)hdl->private->reg_blks[0].reg_addr;
	unsigned long de_base = (unsigned long)hdl->cinfo.de_reg_base;

	drm_printf(p, "\t%s@%8x: %sable\n", hdl->private->desc->name,
			    hdl->private->reg_update ? (unsigned int)(base - de_base) : 0,
			    debug->enable ? "en" : "dis");
	if (debug->enable) {
		drm_printf(p, "\t\tin:  colorfmt: %s, eotf: %s, colorspace: %s, range: %s\n",
			    to_format_space_name(debug->in_info.px_fmt_space),
			    to_color_space_name(debug->in_info.color_space),
			    to_color_range_name(debug->in_info.color_range),
			    to_eotf_name(debug->in_info.eotf));

		drm_printf(p, "\t\tout: colorfmt: %s, eotf: %s, colorspace: %s, range: %s\n",
			    to_format_space_name(debug->out_info.px_fmt_space),
			    to_color_space_name(debug->out_info.color_space),
			    to_color_range_name(debug->out_info.color_range),
			    to_eotf_name(debug->out_info.eotf));
		if (hdl->private->bcsh.enable)
			drm_printf(p, "\t\tbcsh: %d %d %d %d\n",
				    hdl->private->bcsh.brightness, hdl->private->bcsh.contrast,
				    hdl->private->bcsh.saturation, hdl->private->bcsh.hue);

	}
}

s32 de35x_csc_apply(struct de_csc_handle *hdl,
	const struct de_csc_info *in_info, const struct de_csc_info *out_info, bool en)
{
	struct de_csc_private *priv = hdl->private;
	struct csc_debug_info *debug = &priv->debug;
	struct csc_reg *reg;
	u32 *csc_coeff = NULL;

	memset(debug, 0, sizeof(*debug));
	if (!en) {
		debug->enable = false;
		return de_csc_enable(hdl, 0);
	}

	de_csc_coeff_calc(in_info, out_info, &csc_coeff);
	if (priv->reg_update && csc_coeff != NULL) {
		s32 dwval;
		reg = de35x_get_csc_reg(priv);

		dwval = *(csc_coeff + 12);
		dwval = ((dwval & 0x80000000) ? (u32)(-(s32)dwval) : dwval) & 0x3FF;
		reg->d0.dwval = dwval;
		dwval = *(csc_coeff + 13);
		dwval = ((dwval & 0x80000000) ? (u32)(-(s32)dwval) : dwval) & 0x3FF;
		reg->d1.dwval = dwval;
		dwval = *(csc_coeff + 14);
		dwval = ((dwval & 0x80000000) ? (u32)(-(s32)dwval) : dwval) & 0x3FF;
		reg->d2.dwval = dwval;

		reg->c0[0].dwval = *(csc_coeff + 0);
		reg->c0[1].dwval = *(csc_coeff + 1);
		reg->c0[2].dwval = *(csc_coeff + 2);
		reg->c0[3].dwval = *(csc_coeff + 3);

		reg->c1[0].dwval = *(csc_coeff + 4);
		reg->c1[1].dwval = *(csc_coeff + 5);
		reg->c1[2].dwval = *(csc_coeff + 6);
		reg->c1[3].dwval = *(csc_coeff + 7);

		reg->c2[0].dwval = *(csc_coeff + 8);
		reg->c2[1].dwval = *(csc_coeff + 9);
		reg->c2[2].dwval = *(csc_coeff + 10);
		reg->c2[3].dwval = *(csc_coeff + 11);

		reg->ctl.dwval = 1;
		csc_set_block_dirty(priv, CSC_REG_BLK_CTL, 1);
	}
	debug->enable = true;
	memcpy(&debug->in_info, in_info, sizeof(*in_info));
	memcpy(&debug->out_info, out_info, sizeof(*out_info));

	return 0;
}

/*
s32 de_csc_set_alpha(u32 disp, u32 chn, u8 en, u8 alpha)
{
	struct de_csc_private *priv = &(csc_priv[disp][chn]);
	struct csc_reg *reg = de35x_get_csc_reg(priv);

	reg->alpha.dwval = alpha | (en << 8);
	priv->set_blk_dirty(priv, CSC_REG_BLK_ALPHA, 1);
	return 0;
}*/

struct de_csc_handle *de35x_csc_create(struct module_create_info *info)
{
	int i;
	struct de_csc_handle *hdl;
	struct de_reg_block *block;
	struct de_reg_mem_info *reg_mem_info;
	struct de_csc_private *priv;
	u8 __iomem *reg_base;
	const struct de_csc_desc *desc;

	desc = get_csc_desc(info);
	if (!desc)
		return NULL;

	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	memcpy(&hdl->cinfo, info, sizeof(*info));
	hdl->private->desc = desc;
	hdl->private->reg_update = desc->type == CHANNEL_CSC || desc->type == DEVICE_CSC;

	if (hdl->private->reg_update) {
		reg_base = info->de_reg_base + info->reg_offset + desc->reg_offset;
		priv = hdl->private;
		reg_mem_info = &(priv->reg_mem_info);

		reg_mem_info->size = sizeof(struct csc_reg);
		reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
			reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
			info->update_mode == RCQ_MODE);
		if (NULL == reg_mem_info->vir_addr) {
			DRM_ERROR("alloc bld[%d] mm fail!size=0x%x\n",
			     info->id, reg_mem_info->size);
			return ERR_PTR(-ENOMEM);
		}

		block = &(priv->reg_blks[CSC_REG_BLK_CTL]);
		block->phy_addr = reg_mem_info->phy_addr;
		block->vir_addr = reg_mem_info->vir_addr;
		block->size = 0x44;
		block->reg_addr = reg_base;
		priv->reg_blk_num = CSC_REG_BLK_NUM;

		hdl->block_num = priv->reg_blk_num;
		hdl->block = kmalloc(sizeof(block[0]) * hdl->block_num, GFP_KERNEL | __GFP_ZERO);
		for (i = 0; i < hdl->private->reg_blk_num; i++)
			hdl->block[i] = &priv->reg_blks[i];
	}

	hdl->private->de_csc_enable = de35x_csc_enable;
	if (desc->version == 2)
		hdl->private->de_csc_apply = de_csc2_apply;
	else if (desc->version == 1)
		hdl->private->de_csc_apply = de_ccsc_de2_apply;
	else
		hdl->private->de_csc_apply = de35x_csc_apply;
	hdl->private->de_csc_dump_state = de35x_csc_dump_state;

	return hdl;
}

//////////////////csc2 add

static const int rgb2yuv_17bit_fp[12][16] = {
	/* input : Full RGB 601 */
	/* output : Full YCbCr 601 */
	{
		0x00009917, 0x00012c8b, 0x00003a5e, 0x00000000,
		0xffffa9a0, 0xffff5660, 0x00010000, 0x00000200,
		0x00010000, 0xffff29a0, 0xffffd660, 0x00000200,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 709 */
	/* output : Full YCbCr 709 */
	{
		0x00006cda, 0x00016e2f, 0x000024f7, 0x00000000,
		0xffffc557, 0xffff3aa9, 0x00010000, 0x00000200,
		0x00010000, 0xffff1779, 0xffffe887, 0x00000200,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 2020 */
	/* output : Full YCbCr 2020 */
	{
		0x00008681, 0x00015b23, 0x00001e5d, 0x00000000,
		0xffffb882, 0xffff477e, 0x00010000, 0x00000200,
		0x00010000, 0xffff1497, 0xffffeb69, 0x00000200,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 601 */
	/* output : Limit YCbCr 601 */
	{
		0x00008396, 0x0001020c, 0x0000322d, 0x00000040,
		0xffffb41f, 0xffff6b02, 0x0000e0df, 0x00000200,
		0x0000e0df, 0xffff4396, 0xffffdba6, 0x00000200,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 709 */
	/* output : Limit YCbCr 709 */
	{
		0x00005d7e, 0x00013a78, 0x00001fbe, 0x00000040,
		0xffffcc7e, 0xffff52a3, 0x0000e0df, 0x00000200,
		0x0000e0df, 0xffff33c3, 0xffffeb5e, 0x00000200,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 2020 */
	/* output : Limit YCbCr 2020 */
	{
		0x00007382, 0x00012a23, 0x00001a10, 0x00000040,
		0xffffc134, 0xffff5dec, 0x0000e0df, 0x00000200,
		0x0000e0df, 0xffff3135, 0xffffedea, 0x00000200,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Limit RGB 2020 */
	/* output : Full YCbCr 2020 */
	{
		0x0000b241, 0x00015df2, 0x000043f6, 0x00000000,
		0xffff9b6d, 0xffff3a7e, 0x00012a15, 0x00000200,
		0x00012a15, 0xffff0663, 0xffffcf88, 0x00000200,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
	/* input : Limit RGB 709 */
	/* output : Full YCbCr 709 */
	{
		0x00007ebf, 0x0001aa61, 0x00002b0b, 0x00000000,
		0xffffbbb2, 0xffff1a39, 0x00012a15, 0x00000200,
		0x00012a15, 0xfffef140, 0xffffe4ab, 0x00000200,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
	/* input : Limit RGB 2020 */
	/* output : Full YCbCr 2020 */
	{
		0x00009c9d, 0x00019433, 0x0000235b, 0x00000000,
		0xffffacc1, 0xffff292a, 0x00012a15, 0x00000200,
		0x00012a15, 0xfffeede4, 0xffffe807, 0x00000200,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
	/* input : Limit RGB 601 */
	/* output : Limit YCbCr 601 */
	{
		0x00009917, 0x00012c8b, 0x00003a5e, 0x00000040,
		0xffffa7a3, 0xffff5285, 0x000105d8, 0x00000200,
		0x000105d8, 0xffff24bd, 0xffffd56b, 0x00000200,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
	/* input : Limit RGB 709 */
	/* output : Limit YCbCr 709 */
	{
		0x00006cda, 0x00016e2f, 0x000024f7, 0x00000040,
		0xffffc400, 0xffff3628, 0x000105d8, 0x00000200,
		0x000105d8, 0xffff122a, 0xffffe7fe, 0x00000200,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
	/* input : Limit RGB 2020 */
	/* output : Limit YCbCr 2020 */
	{
		0x00008681, 0x00015b23, 0x00001e5d, 0x00000040,
		0xffffb6e1, 0xffff4347, 0x000105d8, 0x00000200,
		0x000105d8, 0xffff0f37, 0xffffeaf1, 0x00000200,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
};

static const int yuv2rgb_17bit_fp[12][16] = {
	/* input : Full YCbCr 601 */
	/* output : Full RGB 601 */
	{
		0x00020000, 0x00000000, 0x0002cdd3, 0x00000000,
		0x00020000, 0xffff4fcd, 0xfffe925c, 0x00000000,
		0x00020000, 0x00038b44, 0x00000000, 0x00000000,
		0x00000000, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Full YCbCr 709 */
	/* output : Full RGB 709*/
	{
		0x00000000, 0x00000000, 0x0003264c, 0x00000000,
		0x00020000, 0xffffa017, 0xffff1052, 0x00000000,
		0x00020000, 0x0003b611, 0x00000000, 0x00000000,
		0x00000000, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Full YCbCr 2020 */
	/* output : Full RGB  2020*/
	{
		0x00020000, 0x00000000, 0x0002f2ff, 0x00000000,
		0x00020000, 0xffffabc0, 0xfffedb78, 0x00000000,
		0x00020000, 0x0003c347, 0x00000000, 0x00000000,
		0x00000000, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Full YCbCr 601 */
	/* output : Limit RGB 601 */
	{
		0x00020000, 0x00000000, 0x0002cdd3, 0x00000000,
		0x00020000, 0xffff4fcd, 0xfffe925c, 0x00000000,
		0x00020000, 0x00038b44, 0x00000000, 0x00000000,
		0x00000000, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Full YCbCr 709 */
	/* output : Limit RGB 709*/
	{
		0x00000000, 0x00000000, 0x0003264c, 0x00000000,
		0x00020000, 0xffffa017, 0xffff1052, 0x00000000,
		0x00020000, 0x0003b611, 0x00000000, 0x00000000,
		0x00000000, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Full YCbCr 2020 */
	/* output : Limit RGB  2020*/
	{
		0x00020000, 0x00000000, 0x0002f2ff, 0x00000000,
		0x00020000, 0xffffabc0, 0xfffedb78, 0x00000000,
		0x00020000, 0x0003c347, 0x00000000, 0x00000000,
		0x00000000, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Limit YCbCr 601 */
	/* output : Full RGB 601 */
	{
		0x0002542c, 0x00000000, 0x00033127, 0x00000000,
		0x0002542c, 0xffff3766, 0xfffe5fbe, 0x00000000,
		0x0002542c, 0x000408ce, 0x00000000, 0x00000000,
		0x00000040, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Limit YCbCr 709 */
	/* output : Full RGB 709*/
	{
		0x0002542c, 0x00000000, 0x000395dd, 0x00000000,
		0x0002542c, 0xffff92d7, 0xfffeef35, 0x00000000,
		0x0002542c, 0x0004398c, 0x00000000, 0x00000000,
		0x00000040, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Limit YCbCr 2020 */
	/* output : Full RGB  2020*/
	{
		0x0002542c, 0x00000000, 0x00035b7f, 0x00000000,
		0x0002542c, 0xffffa00d, 0xfffeb2f2, 0x00000000,
		0x0002542c, 0x0004489a, 0x00000000, 0x00000000,
		0x00000040, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Limit YCbCr 601 */
	/* output : Limit RGB 601 */
	{
		0x00020000, 0x00000000, 0x0002bdcd, 0x00000040,
		0x00020000, 0xffff53bc, 0xfffe9a86, 0x00000040,
		0x00020000, 0x00037703, 0x00000000, 0x00000040,
		0x00000040, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Limit YCbCr 709 */
	/* output : Limit RGB 709*/
	{
		0x00020000, 0x00000000, 0x0003144d, 0x00000040,
		0x00020000, 0xffffa23b, 0xffff15ac, 0x00000040,
		0x00020000, 0x0003a0dc, 0x00000000, 0x00000040,
		0x00000040, 0x00000200, 0x00000200, 0x00000000,
	},
	/* input : Limit YCbCr 2020 */
	/* output : Limit RGB  2020*/
	{
		0x00020000, 0x00000000, 0x0002e225, 0x00000040,
		0x00020000, 0xffffada1, 0xfffee1ff, 0x00000040,
		0x00020000, 0x0003adc6, 0x00000000, 0x00000040,
		0x00000040, 0x00000200, 0x00000200, 0x00000000,
	},
};

/* full rgb cs convert or color_range convert */
static const int rgb2rgb_17bit_fp[8][16] = {
	/* input : Full RGB 601 */
	/* output : Full RGB 709 */
	{
		0x00021687, 0xffffe979, 0x00000000, 0x00000000,
		0x00000000, 0x00020000, 0x00000000, 0x00000000,
		0x00000000, 0x0000060b, 0x0001f9f5, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 601 */
	/* output : Full RGB 2020 */
	{
		0x00014f5c, 0x00009aba, 0x000015ea, 0x00000000,
		0x000024ea, 0x0001d54d, 0x000005bc, 0x00000000,
		0x000008c1, 0x00003220, 0x0001c51f, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 709 */
	/* output : Full RGB 601 */
	{
		0x0001ea65, 0x0000159b, 0x00000000, 0x00000000,
		0x00000000, 0x00020000, 0x00000000, 0x00000000,
		0x00000000, 0xfffff9e8, 0x00020618, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 709 */
	/* output : Full RGB 2020 */
	{
		0x0001413b, 0x0000a89a, 0x0000162b, 0x00000000,
		0x00002361, 0x0001d6c9, 0x000005d6, 0x00000000,
		0x00000866, 0x00002d0e, 0x0001ca8c, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 2020 */
	/* output : Full RGB 601 */
	{
		0x00032b9f, 0xfffef845, 0xffffdc1c, 0x00000000,
		0xffffc034, 0x0002440b, 0xfffffbc0, 0x00000000,
		0xfffff759, 0xffffc4f7, 0x000243b0, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB 2020 */
	/* output : Full RGB 709 */
	{
		0x0003522d, 0xfffed326, 0xffffdaba, 0x00000000,
		0xffffc034, 0x0002440b, 0xfffffbc0, 0x00000000,
		0xfffff6ae, 0xffffcc7e, 0x00023cc6, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Full RGB */
	/* output : Limit RGB */
	{
		0x0001b7b8, 0x00000000, 0x00000000, 0x00000040,
		0x00000000, 0x0001b7b8, 0x00000000, 0x00000040,
		0x00000000, 0x00000000, 0x0001b7b8, 0x00000040,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	},
	/* input : Limit RGB */
	/* output : Full RGB */
	{
		0x0002542a, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x0002542a, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x0002542a, 0x00000000,
		0x00000040, 0x00000040, 0x00000040, 0x00000000,
	},
};

static const int bypass[16] = {
	0x00020000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00020000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00020000, 0x00000000,
	0,          0,         0,        0
};

static const int table_sin[91] = {
	0, 2, 4, 7, 9, 11, 13, 16, 18, 20,
	22, 24, 27, 29, 31, 33, 35, 37, 40, 42,
	44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
	64, 66, 68, 70, 72, 73, 75, 77, 79, 81,
	82, 84, 86, 87, 89, 91, 92, 94, 95, 97,
	98, 99, 101, 102, 104, 105, 106, 107, 109, 110,
	111, 112, 113, 114, 115, 116, 117, 118, 119, 119,
	120, 121, 122, 122, 123, 124, 124, 125, 125, 126,
	126, 126, 127, 127, 127, 128, 128, 128, 128, 128,
	128
};

static int get_csc2_color_space_index(u32 in_color_space, u32 out_color_space)
{
	if (in_color_space == DE_COLOR_SPACE_BT601) {
		if (out_color_space == DE_COLOR_SPACE_BT709)
			return 0;

		if (out_color_space == DE_COLOR_SPACE_BT2020C ||
		    out_color_space == DE_COLOR_SPACE_BT2020NC)
			return 1;
	} else if (in_color_space == DE_COLOR_SPACE_BT709) {
		if (out_color_space == DE_COLOR_SPACE_BT601)
			return 2;

		if (out_color_space == DE_COLOR_SPACE_BT2020C ||
		    out_color_space == DE_COLOR_SPACE_BT2020NC)
			return 3;
	} else if (in_color_space == DE_COLOR_SPACE_BT2020C ||
		   in_color_space == DE_COLOR_SPACE_BT2020NC) {
		if (out_color_space == DE_COLOR_SPACE_BT601)
			return 4;

		if (out_color_space == DE_COLOR_SPACE_BT709)
			return 5;
	}

	/* not support or same color space*/
	return -1;
}

static u32 get_csc2_mod_idx(enum de_color_space color_space)
{
	u32 idx;

	switch (color_space) {
	case DE_COLOR_SPACE_BT601:
		idx = 0; break;
	case DE_COLOR_SPACE_BT709:
		idx = 1; break;
	case DE_COLOR_SPACE_BT2020NC:
	case DE_COLOR_SPACE_BT2020C:
		idx = 2; break;
	default:
		idx = 0; break;
	}
	return idx;
}


static int de_csc2_coeff_calc(const struct de_csc_info *in_info, const struct de_csc_info *out_info, u32 *mat_sum, const int *mat[])
{
	u32 cs_index; /* index for r2r color_space matrix */
	u32 in_m, in_r, out_m, out_r;
	u32 mat_num = 0;

	in_r = in_info->color_range == DE_COLOR_RANGE_16_235 ? 1 : 0;
	in_m = get_csc2_mod_idx(in_info->color_space);
	out_r = in_info->color_range == DE_COLOR_RANGE_16_235 ? 1 : 0;
	out_m = get_csc2_mod_idx(out_info->color_space);
	cs_index = get_csc2_color_space_index(in_info->color_space, out_info->color_space);

	if (in_info->px_fmt_space == DE_FORMAT_SPACE_RGB) {
		if (out_info->px_fmt_space == DE_FORMAT_SPACE_YUV) {/* (r2r)(color space) + r2y */
			if (cs_index != -1) {
				mat[mat_num] = rgb2rgb_17bit_fp[cs_index];
				mat_num++;
			}
			mat[mat_num] = rgb2yuv_17bit_fp[out_r * 3 + out_m];
			mat_num++;
		} else {/* (r2r)(color space) + (r2r)(color range) */
			if (cs_index != -1) {
				mat[mat_num] = rgb2rgb_17bit_fp[cs_index];
				mat_num++;
			}
			if (out_info->color_range == DE_COLOR_RANGE_16_235) {
				mat[mat_num] = rgb2rgb_17bit_fp[6];
				mat_num++;
			}
		}
	} else { /* infmt == DE_YUV */
		if (out_info->px_fmt_space == DE_FORMAT_SPACE_YUV) {
		/* y2r(full) + (r2r)(color space) + (r2y) */
			if (in_info->color_range != out_info->color_range ||
			    cs_index != -1) {
				mat[mat_num] = yuv2rgb_17bit_fp[in_r * 6 + in_m];
				mat_num++;

				if (cs_index != -1) {
					mat[mat_num] = rgb2rgb_17bit_fp[cs_index];
					mat_num++;
				}

				mat[mat_num] = rgb2yuv_17bit_fp[out_r * 3 + out_m];
				mat_num++;
			}
		} else { /* outfmt == DE_RGB y2r + (r2r) + (r2r) */
			if (cs_index != -1) {
				mat[mat_num] = yuv2rgb_17bit_fp[in_r * 6 + in_m];
				mat_num++;

				mat[mat_num] = rgb2rgb_17bit_fp[cs_index];
				mat_num++;

				if (out_info->color_range == DE_COLOR_RANGE_16_235) {
					mat[mat_num] = rgb2rgb_17bit_fp[6];
					mat_num++;
				}
			} else {
				mat[mat_num] = yuv2rgb_17bit_fp[in_r * 6 + out_r * 3 + out_m];
				mat_num++;
			}

		}
	}

	*mat_sum = mat_num;
	return 0;
}

static inline __s64 IntRightShift64(__s64 datain, unsigned int shiftbit)
{
	__s64 dataout;
	__s64 tmp;

	tmp = (shiftbit >= 1) ? (1 << (shiftbit - 1)) : 0;
	if (datain >= 0)
		dataout = (datain + tmp) >> shiftbit;
	else
		dataout = -((-datain + tmp) >> shiftbit);

	return dataout;
}

static s32 IDE_SCAL_MATRIC_MUL(const int *in1, const int *in2, int *result)
{

	result[0] = (int)IntRightShift64(((long long)in1[0] * in2[0] + (long long)in1[1]
			   * in2[4] + (long long)in1[2] * in2[8] + 0x10000), 17);
	result[1] = (int)IntRightShift64(((long long)in1[0] * in2[1] + (long long)in1[1]
			   * in2[5] + (long long)in1[2] * in2[9] + 0x10000), 17);
	result[2] = (int)IntRightShift64(((long long)in1[0] * in2[2] + (long long)in1[1]
			   * in2[6] + (long long)in1[2] * in2[10] + 0x10000), 17);

	result[4] = (int)IntRightShift64(((long long)in1[4] * in2[0] + (long long)in1[5]
			   * in2[4] + (long long)in1[6] * in2[8] + 0x10000), 17);
	result[5] = (int)IntRightShift64(((long long)in1[4] * in2[1] + (long long)in1[5]
			   * in2[5] + (long long)in1[6] * in2[9] + 0x10000), 17);
	result[6] = (int)IntRightShift64(((long long)in1[4] * in2[2] + (long long)in1[5]
			   * in2[6] + (long long)in1[6] * in2[10] + 0x10000), 17);

	result[8] = (int)IntRightShift64(((long long)in1[8] * in2[0] + (long long)in1[9]
			   * in2[4] + (long long)in1[10] * in2[8] + 0x10000), 17);
	result[9] = (int)IntRightShift64(((long long)in1[8] * in2[1] + (long long)in1[9]
			   * in2[5] + (long long)in1[10] * in2[9] + 0x10000), 17);
	result[10] = (int)IntRightShift64(((long long)in1[8] * in2[2] + (long long)in1[9]
			    * in2[6] + (long long)in1[10] * in2[10] + 0x10000), 17);

	//C03/C13/C23
	result[3] = in1[3] + IntRightShift64((in1[0] * (in2[3] - in1[12]) +
					      in1[1] * (in2[7] - in1[13]) +
					      in1[2] * (in2[11] - in1[14]) + 0x10000), 17);
	result[7] = in1[7] + IntRightShift64((in1[4] * (in2[3] - in1[12]) +
					      in1[5] * (in2[7] - in1[13]) +
					      in1[6] * (in2[11] - in1[14]) + 0x10000), 17);
	result[11] = in1[11] + IntRightShift64((in1[8] * (in2[3] - in1[12]) +
						in1[9] * (in2[7] - in1[13]) +
						in1[10] * (in2[11] - in1[14]) + 0x10000), 17);

	//D0/D1/D2
	result[12] = in2[12];
	result[13] = in2[13];
	result[14] = in2[14];
	result[15] = 0;

	return 0;
}

static int de_csc2_apply(struct de_csc_handle *hdl,
	const struct de_csc_info *in_info, const struct de_csc_info *out_info, bool en)
{
	int csc_coeff[16], in0coeff[16], in1coeff[16];
	const int *matrix[5], *csc_matrix[3];
	unsigned int oper = 0, cscm_num = 0;
	int i, j;
	struct de_csc_private *priv = hdl->private;
	struct csc_debug_info *debug = &priv->debug;
	struct csc2_reg *regs = de35x_get_csc_reg(priv);
	memset(debug, 0, sizeof(*debug));

	if (!en) {
		debug->enable = false;
		return de_csc_enable(hdl, 0);
	}

	debug->enable = true;
	memcpy(&debug->in_info, in_info, sizeof(*in_info));
	memcpy(&debug->out_info, out_info, sizeof(*out_info));

	if (in_info->px_fmt_space == out_info->px_fmt_space &&
		    in_info->color_space == out_info->color_space &&
		    in_info->color_range == out_info->color_range &&
		    in_info->eotf == out_info->eotf) {
		memcpy(csc_coeff, bypass, sizeof(bypass));
		goto set_regs_exit;
	}

	de_csc2_coeff_calc(in_info, out_info, &cscm_num, csc_matrix);

	for (i = 0; i < cscm_num; i++)
		matrix[oper++] = csc_matrix[i];

	/* mat mul */
	if (oper == 0) {
		memcpy(csc_coeff, bypass, sizeof(bypass));
	} else if (oper == 1) {
		memcpy(csc_coeff, matrix[0], sizeof(csc_coeff));
	} else {
		for (i = 0; i < oper - 1; i++) {
			if (i == 0)
				IDE_SCAL_MATRIC_MUL(matrix[i + 1], matrix[i], in1coeff);
			else {
				memcpy(in0coeff, in1coeff, sizeof(in1coeff));
				IDE_SCAL_MATRIC_MUL(matrix[i], in0coeff, in1coeff);
			}
		}
		memcpy(csc_coeff, in1coeff, sizeof(in1coeff));
	}

 set_regs_exit:
	/* The 3x3 conversion coefficient is 17bit fixed-point --->10bit fixed-point */
	for (i = 0; i < 3; ++i) {
		for (j = 0; j < 3; ++j) {
			csc_coeff[i * 4 + j] = IntRightShift64((csc_coeff[i * 4 + j] + 64), 7);
		}
	}

	regs->ctl.bits.en = 1;
	regs->d0.dwval = *(csc_coeff + 12);
	regs->d1.dwval = *(csc_coeff + 13);
	regs->d2.dwval = *(csc_coeff + 14);
	regs->c00.dwval = *(csc_coeff);
	regs->c01.dwval = *(csc_coeff + 1);
	regs->c02.dwval = *(csc_coeff + 2);
	regs->c03.dwval = *(csc_coeff + 3);
	regs->c10.dwval = *(csc_coeff + 4);
	regs->c11.dwval = *(csc_coeff + 5);
	regs->c12.dwval = *(csc_coeff + 6);
	regs->c13.dwval = *(csc_coeff + 7);
	regs->c20.dwval = *(csc_coeff + 8);
	regs->c21.dwval = *(csc_coeff + 9);
	regs->c22.dwval = *(csc_coeff + 10);
	regs->c23.dwval = *(csc_coeff + 11);

	csc_set_block_dirty(priv, CSC_REG_BLK_CTL, 1);
	return 0;
}

//DCSC
struct __scal_matrix4x4 {
	__s64 x00;
	__s64 x01;
	__s64 x02;
	__s64 x03;
	__s64 x10;
	__s64 x11;
	__s64 x12;
	__s64 x13;
	__s64 x20;
	__s64 x21;
	__s64 x22;
	__s64 x23;
	__s64 x30;
	__s64 x31;
	__s64 x32;
	__s64 x33;
};

static s32 IDE_SCAL_MATRIC_MUL_2(struct __scal_matrix4x4 *in1,
	struct __scal_matrix4x4 *in2, struct __scal_matrix4x4 *result)
{

	result->x00 =
	    IntRightShift64(in1->x00 * in2->x00 + in1->x01 * in2->x10 +
			    in1->x02 * in2->x20 + in1->x03 * in2->x30, 10);
	result->x01 =
	    IntRightShift64(in1->x00 * in2->x01 + in1->x01 * in2->x11 +
			    in1->x02 * in2->x21 + in1->x03 * in2->x31, 10);
	result->x02 =
	    IntRightShift64(in1->x00 * in2->x02 + in1->x01 * in2->x12 +
			    in1->x02 * in2->x22 + in1->x03 * in2->x32, 10);
	result->x03 =
	    IntRightShift64(in1->x00 * in2->x03 + in1->x01 * in2->x13 +
			    in1->x02 * in2->x23 + in1->x03 * in2->x33, 10);
	result->x10 =
	    IntRightShift64(in1->x10 * in2->x00 + in1->x11 * in2->x10 +
			    in1->x12 * in2->x20 + in1->x13 * in2->x30, 10);
	result->x11 =
	    IntRightShift64(in1->x10 * in2->x01 + in1->x11 * in2->x11 +
			    in1->x12 * in2->x21 + in1->x13 * in2->x31, 10);
	result->x12 =
	    IntRightShift64(in1->x10 * in2->x02 + in1->x11 * in2->x12 +
			    in1->x12 * in2->x22 + in1->x13 * in2->x32, 10);
	result->x13 =
	    IntRightShift64(in1->x10 * in2->x03 + in1->x11 * in2->x13 +
			    in1->x12 * in2->x23 + in1->x13 * in2->x33, 10);
	result->x20 =
	    IntRightShift64(in1->x20 * in2->x00 + in1->x21 * in2->x10 +
			    in1->x22 * in2->x20 + in1->x23 * in2->x30, 10);
	result->x21 =
	    IntRightShift64(in1->x20 * in2->x01 + in1->x21 * in2->x11 +
			    in1->x22 * in2->x21 + in1->x23 * in2->x31, 10);
	result->x22 =
	    IntRightShift64(in1->x20 * in2->x02 + in1->x21 * in2->x12 +
			    in1->x22 * in2->x22 + in1->x23 * in2->x32, 10);
	result->x23 =
	    IntRightShift64(in1->x20 * in2->x03 + in1->x21 * in2->x13 +
			    in1->x22 * in2->x23 + in1->x23 * in2->x33, 10);
	result->x30 =
	    IntRightShift64(in1->x30 * in2->x00 + in1->x31 * in2->x10 +
			    in1->x32 * in2->x20 + in1->x33 * in2->x30, 10);
	result->x31 =
	    IntRightShift64(in1->x30 * in2->x01 + in1->x31 * in2->x11 +
			    in1->x32 * in2->x21 + in1->x33 * in2->x31, 10);
	result->x32 =
	    IntRightShift64(in1->x30 * in2->x02 + in1->x31 * in2->x12 +
			    in1->x32 * in2->x22 + in1->x33 * in2->x32, 10);
	result->x33 =
	    IntRightShift64(in1->x30 * in2->x03 + in1->x31 * in2->x13 +
			    in1->x32 * in2->x23 + in1->x33 * in2->x33, 10);
	return 0;
}

//it seems that in v2x, out_color_range is set to fix DISP_COLOR_RANGE_0_255
// out_color_range is never modify with DISP_COLOR_RANGE_0_255 default
//eotf is not care
static int __maybe_unused de_csc_coeff_calc_inner8bit(unsigned int infmt, unsigned int incscmod,
		      unsigned int outfmt, unsigned int outcscmod,
		      unsigned int brightness, unsigned int contrast,
		      unsigned int saturation, unsigned int hue,
		      unsigned int out_color_range, int *csc_coeff)
{
	struct __scal_matrix4x4 *enhancecoeff, *tmpcoeff;
	struct __scal_matrix4x4 *coeff[5], *in0coeff, *in1coeff;
	int oper, i;
	int i_bright, i_contrast, i_saturation, i_hue, sinv, cosv;

	oper = 0;

	enhancecoeff = kmalloc(sizeof(struct __scal_matrix4x4),
			GFP_KERNEL | __GFP_ZERO);
	tmpcoeff = kmalloc(sizeof(struct __scal_matrix4x4),
		GFP_KERNEL | __GFP_ZERO);
	in0coeff = kmalloc(sizeof(struct __scal_matrix4x4),
		GFP_KERNEL | __GFP_ZERO);

	if (!enhancecoeff || !tmpcoeff || !in0coeff) {
		DRM_ERROR("kmalloc fail!\n");
		goto err;
	}

	/* BYPASS */
	if (infmt == outfmt && incscmod == outcscmod
	    && out_color_range == DE_COLOR_RANGE_0_255 && brightness == 50
	    && contrast == 50 && saturation == 50 && hue == 50) {
		memcpy(csc_coeff, bypass_csc8bit, 48);
		goto err;
	}

	/* NON-BYPASS */
	if (infmt == DE_FORMAT_SPACE_RGB) {
		/* convert to YCbCr */
		if (outfmt == DE_FORMAT_SPACE_RGB) {
			coeff[oper] = (struct __scal_matrix4x4 *) (ir2y8bit + 0x20);
			oper++;
		} else {
			if (outcscmod == DE_COLOR_SPACE_BT601) {
				coeff[oper] = (struct __scal_matrix4x4 *) (ir2y8bit);
				oper++;
			} else if (outcscmod == DE_COLOR_SPACE_BT709) {
				coeff[oper] = (struct __scal_matrix4x4 *)(ir2y8bit + 0x20);
				oper++;
			}
		}
	} else {
		if (incscmod != outcscmod && outfmt == DE_FORMAT_SPACE_YUV) {
			if (incscmod == DE_COLOR_SPACE_BT601
				&& outcscmod == DE_COLOR_SPACE_BT709) {
				coeff[oper] = (struct __scal_matrix4x4 *) (y2y8bit);
				oper++;
			} else if (incscmod == DE_COLOR_SPACE_BT709
				   && outcscmod == DE_COLOR_SPACE_BT601) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y8bit + 0x20);
				oper++;
			}
		}
	}

	if (brightness != 50 || contrast != 50
		|| saturation != 50 || hue != 50) {
		brightness = brightness > 100 ? 100 : brightness;
		contrast = contrast > 100 ? 100 : contrast;
		saturation = saturation > 100 ? 100 : saturation;
		hue = hue > 100 ? 100 : hue;

		i_bright = (int)(brightness * 64 / 100);
		i_saturation = (int)(saturation * 64 / 100);
		i_contrast = (int)(contrast * 64 / 100);
		i_hue = (int)(hue * 64 / 100);

		sinv = sin_cos8bit[i_hue & 0x3f];
		cosv = sin_cos8bit[64 + (i_hue & 0x3f)];

		/* calculate enhance matrix */
		enhancecoeff->x00 = i_contrast << 7;
		enhancecoeff->x01 = 0;
		enhancecoeff->x02 = 0;
		enhancecoeff->x03 =
		    (((i_bright - 32) * 5 + 16) << 12) - (i_contrast << 11);
		enhancecoeff->x10 = 0;
		enhancecoeff->x11 = (i_contrast * i_saturation * cosv) >> 5;
		enhancecoeff->x12 = (i_contrast * i_saturation * sinv) >> 5;
		enhancecoeff->x13 =
		    (1 << 19) - ((enhancecoeff->x11 + enhancecoeff->x12) << 7);
		enhancecoeff->x20 = 0;
		enhancecoeff->x21 = (-i_contrast * i_saturation * sinv) >> 5;
		enhancecoeff->x22 = (i_contrast * i_saturation * cosv) >> 5;
		enhancecoeff->x23 =
		    (1 << 19) - ((enhancecoeff->x22 + enhancecoeff->x21) << 7);
		enhancecoeff->x30 = 0;
		enhancecoeff->x31 = 0;
		enhancecoeff->x32 = 0;
		enhancecoeff->x33 = 4096;

		coeff[oper] = enhancecoeff;
		oper++;

	}

	if (outfmt == DE_FORMAT_SPACE_RGB) {
		if (infmt == DE_FORMAT_SPACE_RGB) {
			coeff[oper] = (struct __scal_matrix4x4 *) (y2r8bit + 0x20);
			oper++;

			if (out_color_range == DE_COLOR_RANGE_16_235) {
				coeff[oper] = (struct __scal_matrix4x4 *) (ir2r8bit);
				oper++;
			}
		} else {
			if (out_color_range == DE_COLOR_RANGE_16_235) {
				if (incscmod == DE_COLOR_SPACE_BT601) {
					coeff[oper] =
					    (struct __scal_matrix4x4 *)
						(y2r8bit + 0x80);
					oper++;
				} else if (incscmod == DE_COLOR_SPACE_BT709) {
					coeff[oper] =
					    (struct __scal_matrix4x4 *)
						(y2r8bit + 0xa0);
					oper++;
				}
			} else {
				if (incscmod == DE_COLOR_SPACE_BT601) {
					coeff[oper] =
					    (struct __scal_matrix4x4 *) (y2r8bit);
					oper++;
				} else if (incscmod == DE_COLOR_SPACE_BT709) {
					coeff[oper] =
					    (struct __scal_matrix4x4 *)
						(y2r8bit + 0x20);
					oper++;
				}
			}
		}
	}
	/* matrix multiply */
	if (oper == 0) {
		memcpy(csc_coeff, bypass_csc8bit, sizeof(bypass_csc8bit));
	} else if (oper == 1) {
		for (i = 0; i < 12; i++)
			*(csc_coeff + i) =
			    IntRightShift64((int)(*((__s64 *) coeff[0] + i)),
					    oper << 1);
	} else {
		memcpy((void *)in0coeff, (void *)coeff[0],
		    sizeof(struct __scal_matrix4x4));
		for (i = 1; i < oper; i++) {
			in1coeff = coeff[i];
			IDE_SCAL_MATRIC_MUL_2(in1coeff, in0coeff, tmpcoeff);
			memcpy((void *)in0coeff, (void *)tmpcoeff,
				sizeof(struct __scal_matrix4x4));
		}

		for (i = 0; i < 12; i++)
			*(csc_coeff + i) =
			    IntRightShift64((int)(*((__s64 *) tmpcoeff + i)),
					    oper << 1);
	}

err:
	kfree(in0coeff);
	kfree(tmpcoeff);
	kfree(enhancecoeff);

	return 0;

}

static __maybe_unused int de_csc_coeff_calc_inner10bit(unsigned int infmt, unsigned int incscmod,
			    unsigned int outfmt, unsigned int outcscmod,
			    unsigned int brightness, unsigned int contrast,
			    unsigned int saturation, unsigned int hue,
			    unsigned int out_color_range, int *csc_coeff)
{
	struct __scal_matrix4x4 *enhancecoeff, *tmpcoeff;
	struct __scal_matrix4x4 *coeff[5], *in0coeff, *in1coeff;
	int oper, i;
	int i_bright, i_contrast, i_saturation, i_hue, sinv, cosv;

	oper = 0;

	enhancecoeff = kmalloc(sizeof(struct __scal_matrix4x4), GFP_KERNEL | __GFP_ZERO);
	tmpcoeff = kmalloc(sizeof(struct __scal_matrix4x4), GFP_KERNEL | __GFP_ZERO);
	in0coeff = kmalloc(sizeof(struct __scal_matrix4x4), GFP_KERNEL | __GFP_ZERO);

	if (!enhancecoeff || !tmpcoeff || !in0coeff) {
		DRM_ERROR("kmalloc fail!\n");
		goto err;
	}

	/* BYPASS */
	if (infmt == outfmt && incscmod == outcscmod &&
	    out_color_range == DE_COLOR_RANGE_0_255 && brightness == 50 &&
	    contrast == 50 && saturation == 50 && hue == 50) {
		memcpy(csc_coeff, bypass_csc10bit, 48);
		goto err;
	}

	/* NON-BYPASS */
	if (infmt == DE_FORMAT_SPACE_RGB) { // r2y
		/* convert to YCbCr */
		if (outfmt == DE_FORMAT_SPACE_RGB) {
			coeff[oper] = (struct __scal_matrix4x4 *)(ir2y10bit + 0x20);
			oper++;
		} else {
			if (outcscmod == DE_COLOR_SPACE_BT601) {
				coeff[oper] = (struct __scal_matrix4x4 *)(
						ir2y10bit); // r2ybt601 tv range
				oper++;
			} else if (outcscmod == DE_COLOR_SPACE_BT709) {
				coeff[oper] = (struct __scal_matrix4x4 *)(ir2y10bit
						+ 0x20); // r2ybt709 tv range
				oper++;
			} else if (outcscmod == DE_COLOR_SPACE_BT2020C
					   || outcscmod == DE_COLOR_SPACE_BT2020NC) {
				coeff[oper] = (struct __scal_matrix4x4 *)(ir2y10bit
						+ 0x80); // r2ybt2020 tv range
				oper++;
			}
		}
	} else { // y2y
		if (incscmod != outcscmod && outfmt == DE_FORMAT_SPACE_YUV) {
			if (incscmod == DE_COLOR_SPACE_BT601 &&
			    outcscmod == DE_COLOR_SPACE_BT709) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y10bit);
				oper++;
			} else if (incscmod == DE_COLOR_SPACE_BT709 &&
				   outcscmod == DE_COLOR_SPACE_BT601) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y10bit + 0x20);
				oper++;
			} else if (incscmod == DE_COLOR_SPACE_BT601 &&
				   (outcscmod == DE_COLOR_SPACE_BT2020C
					|| outcscmod == DE_COLOR_SPACE_BT2020NC)) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y10bit + 0x40);
				oper++;
			} else if ((incscmod == DE_COLOR_SPACE_BT2020NC
						|| incscmod == DE_COLOR_SPACE_BT2020C) &&
				   outcscmod == DE_COLOR_SPACE_BT601) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y10bit + 0x60);
				oper++;
			} else if (incscmod == DE_COLOR_SPACE_BT709 &&
				   (outcscmod == DE_COLOR_SPACE_BT2020NC
					|| outcscmod == DE_COLOR_SPACE_BT2020C)) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y10bit + 0x80);
				oper++;
			} else if ((incscmod == DE_COLOR_SPACE_BT2020NC
						|| incscmod == DE_COLOR_SPACE_BT2020C) &&
				   outcscmod == DE_COLOR_SPACE_BT709) {
				coeff[oper] = (struct __scal_matrix4x4 *)(y2y10bit + 0xa0);
				oper++;
			}
		}
	}

	// hsbcmat
	if (brightness != 50 || contrast != 50 || saturation != 50 ||
	    hue != 50) {
		brightness = brightness > 100 ? 100 : brightness;
		contrast = contrast > 100 ? 100 : contrast;
		saturation = saturation > 100 ? 100 : saturation;
		hue = hue > 100 ? 100 : hue;

		i_bright = (int)(brightness * 64 / 100);
		i_saturation = (int)(saturation * 64 / 100);
		i_contrast = (int)(contrast * 64 / 100);
		i_hue = (int)(hue * 64 / 100);

		sinv = sin_cos10bit[i_hue & 0x3f];
		cosv = sin_cos10bit[64 + (i_hue & 0x3f)];

		/* calculate enhance matrix */
		enhancecoeff->x00 = i_contrast << 7;
		enhancecoeff->x01 = 0;
		enhancecoeff->x02 = 0;
		enhancecoeff->x03 =
		    (((i_bright - 32) * 20 + 64) << 12) - (i_contrast << 13);
		enhancecoeff->x10 = 0;
		enhancecoeff->x11 = (i_contrast * i_saturation * cosv) >> 5;
		enhancecoeff->x12 = (i_contrast * i_saturation * sinv) >> 5;
		enhancecoeff->x13 =
		    (1 << 21) - ((enhancecoeff->x11 + enhancecoeff->x12) << 9);
		enhancecoeff->x20 = 0;
		enhancecoeff->x21 = (-i_contrast * i_saturation * sinv) >> 5;
		enhancecoeff->x22 = (i_contrast * i_saturation * cosv) >> 5;
		enhancecoeff->x23 =
		    (1 << 21) - ((enhancecoeff->x22 + enhancecoeff->x21) << 9);
		enhancecoeff->x30 = 0;
		enhancecoeff->x31 = 0;
		enhancecoeff->x32 = 0;
		enhancecoeff->x33 = 4096;

		coeff[oper] = enhancecoeff;
		oper++;
	}

	if (outfmt == DE_FORMAT_SPACE_RGB) {
		if (infmt == DE_FORMAT_SPACE_RGB) {
			coeff[oper] = (struct __scal_matrix4x4 *)(y2r10bit + 0x20);
			oper++;

			if (out_color_range == DE_COLOR_RANGE_16_235) {
				coeff[oper] = (struct __scal_matrix4x4 *)(ir2r10bit); // r2r
				oper++;
			}
		} else {
			if (out_color_range == DE_COLOR_RANGE_16_235) {
				if (incscmod == DE_COLOR_SPACE_BT601) {
					coeff[oper] = (struct __scal_matrix4x4 *)(y2r10bit
							+ 0x80); // y2rbt601 studio
					oper++;
				} else if (incscmod == DE_COLOR_SPACE_BT709) {
					coeff[oper] = (struct __scal_matrix4x4 *)(y2r10bit
							+ 0xa0); // y2rbt709 studio
					oper++;
				} else if (incscmod == DE_COLOR_SPACE_BT2020NC
						   || incscmod == DE_COLOR_SPACE_BT2020C) {
					coeff[oper] = (struct __scal_matrix4x4 *)(y2r10bit
							+ 0x100); // y2rbt2020 studio
					oper++;
				}
			} else {
				if (incscmod == DE_COLOR_SPACE_BT601) {
					coeff[oper] = (struct __scal_matrix4x4 *)(
							y2r10bit); // y2rbt601 TV rangne
					oper++;
				} else if (incscmod == DE_COLOR_SPACE_BT709) {
					coeff[oper] = (struct __scal_matrix4x4 *)(y2r10bit
							+ 0x20); // y2rbt709 TV range
					oper++;
				} else if (incscmod == DE_COLOR_SPACE_BT2020NC
						 || incscmod == DE_COLOR_SPACE_BT2020C) {
					coeff[oper] = (struct __scal_matrix4x4 *)(y2r10bit
							+ 0xc0); // y2rbt2020 TV range
					oper++;
				}
			}
		}
	}
	/* matrix multiply */
	if (oper == 0) {
		memcpy(csc_coeff, bypass_csc10bit, sizeof(bypass_csc8bit));
	} else if (oper == 1) {
		for (i = 0; i < 12; i++)
			*(csc_coeff + i) = IntRightShift64(
			    (int)(*((__s64 *)coeff[0] + i)), oper << 1);
	} else {
		memcpy((void *)in0coeff, (void *)coeff[0],
		       sizeof(struct __scal_matrix4x4));
		for (i = 1; i < oper; i++) {
			in1coeff = coeff[i];
			IDE_SCAL_MATRIC_MUL_2(in1coeff, in0coeff, tmpcoeff);
			memcpy((void *)in0coeff, (void *)tmpcoeff,
			       sizeof(struct __scal_matrix4x4));
		}

		for (i = 0; i < 12; i++)
			*(csc_coeff + i) = IntRightShift64(
			    (int)(*((__s64 *)tmpcoeff + i)), oper << 1);
	}

err:
	kfree(in0coeff);
	kfree(tmpcoeff);
	kfree(enhancecoeff);

	return 0;
}

int de_dcsc_apply(struct de_csc_handle *hdl, const struct de_csc_info *in_info,
		  const struct de_csc_info *out_info, const struct bcsh_info *bcsh,
		  int *csc_coeff, bool apply)
{
	struct bcsh_info mbcsh;
	struct de_csc_private *priv = hdl->private;
	struct csc_debug_info *debug = &priv->debug;

	memset(debug, 0, sizeof(*debug));
	if (bcsh == NULL || !bcsh->enable) {
		mbcsh.brightness = 50;
		mbcsh.contrast = 50;
		mbcsh.saturation = 50;
		mbcsh.hue = 50;
		mbcsh.enable = false;
	} else {
		mbcsh.brightness = bcsh->brightness;
		mbcsh.contrast = bcsh->contrast;
		mbcsh.saturation = bcsh->saturation;
		mbcsh.hue = bcsh->hue;
		mbcsh.enable = true;
	}
	memcpy(&priv->bcsh, &mbcsh, sizeof(mbcsh));
	/* map from 0~100 to 20~100 to avoid invisible */
	if (mbcsh.brightness < 50)
		mbcsh.brightness = mbcsh.brightness * 3 / 5 + 20;
	if (mbcsh.contrast < 50)
		mbcsh.contrast = mbcsh.contrast * 3 / 5 + 20;

	debug->enable = true;
	memcpy(&debug->in_info, in_info, sizeof(*in_info));
	memcpy(&debug->out_info, out_info, sizeof(*out_info));

/* FIXME ? */
/*	if (de_rtmx_is_input_10bit(sel)) {
		de_csc_coeff_calc_inner10bit(
		    in_info->px_fmt_space, in_info->color_space, out_info->px_fmt_space,
		    out_info->color_space, bcsh->brightness, bcsh->contrast,
		    bcsh->saturation, bcsh->hue, out_info->color_range,
		    csc_coeff);
	} else */{
		de_csc_coeff_calc_inner8bit(
		    in_info->px_fmt_space, in_info->color_space, out_info->px_fmt_space,
		    out_info->color_space, bcsh->brightness, bcsh->contrast,
		    bcsh->saturation, bcsh->hue, out_info->color_range,
		    csc_coeff);
	}

	if (apply) {
		/* TODO: only for device csc */
	}

	return 0;
}

static int de_ccsc_de2_apply(struct de_csc_handle *hdl,
	const struct de_csc_info *in_info, const struct de_csc_info *out_info, bool en)
{
	struct de_csc_private *priv = hdl->private;
	struct csc_debug_info *debug = &priv->debug;
	struct csc_de2_reg *reg = de35x_get_csc_reg(priv);
	const u32 default_bcsh_val = 50;
	int csc_coeff[12];

	if (!en) {
		debug->enable = false;
		return de_csc_enable(hdl, 0);
	}

	debug->enable = true;
	memcpy(&debug->in_info, in_info, sizeof(*in_info));
	memcpy(&debug->out_info, out_info, sizeof(*out_info));

	de_csc_coeff_calc_inner8bit(in_info->px_fmt_space, in_info->color_space,
			  out_info->px_fmt_space, out_info->color_space,
			  default_bcsh_val, default_bcsh_val,
			  default_bcsh_val, default_bcsh_val,
			  out_info->color_range, csc_coeff);

	reg->c00.dwval = *(csc_coeff);
	reg->c01.dwval = *(csc_coeff + 1);
	reg->c02.dwval = *(csc_coeff + 2);
	reg->c03.dwval = *(csc_coeff + 3) + 0x200;
	reg->c10.dwval = *(csc_coeff + 4);
	reg->c11.dwval = *(csc_coeff + 5);
	reg->c12.dwval = *(csc_coeff + 6);
	reg->c13.dwval = *(csc_coeff + 7) + 0x200;
	reg->c20.dwval = *(csc_coeff + 8);
	reg->c21.dwval = *(csc_coeff + 9);
	reg->c22.dwval = *(csc_coeff + 10);
	reg->c23.dwval = *(csc_coeff + 11) + 0x200;
	reg->bypass.bits.enable = 1;
	reg->alpha.bits.alpha = 0xff;
	csc_set_block_dirty(priv, CSC_REG_BLK_CTL, 1);
	return 0;
}

