/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2023 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/version.h>
#include <drm/drm_blend.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#else
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem_dma_helper.h>
#endif
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>

#include "de_fbd_atw_type.h"
#include "de_fbd_atw.h"

#define CHN_FBD_ATW_OFFSET			(0x05000)

// RGB/RGBA 32x8 layout3 split AFBC compress stream
#define SUNXI_RGB_AFBC_MOD                                                             \
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_SPARSE | \
							AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPLIT)

// YUV4:2:0 16x16 layout1 split AFBC compress stream
#define SUNXI_YUV_AFBC_MOD                                                              \
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE | \
							AFBC_FORMAT_MOD_SPLIT)

struct afbc_stream_info {
	uint32_t format;
	uint32_t header_layout;
	uint32_t inputbits[4];
	/*
	 * superblock_layout[0]: layout when block size = 16x16
	 * superblock_layout[1]: layout when block size = 32x8
	 */
	uint32_t superblock_layout[2];
};

enum fbd_format {
	FBD_RGBA4444    =  0X0e,
	FBD_RGB565      =  0X0a,
	FBD_RGBA5551    =  0X12,
	FBD_RGB888      =  0X08,
	FBD_RGBA8888    =  0X02,
	FBD_RGBA1010102 =  0X16,

	FBD_YUV420      =  0X2a,
	FBD_YUV422      =  0X26,
	FBD_YUV420B10   =  0X30,
	FBD_YUV422B10   =  0X32,
};

struct de_fbd_info {
	enum fbd_format  fmt;
	u32 compbits[4];
	u32 sbs[2];
	u32 yuv_tran;
	u16 block_layout;
};

/*
 * disp_afbc_info - ARM FrameBuffer Compress buffer info
 *
 * @header_layout: sub sampled info
 *                 0 - 444
 *                 1 - 420 (half horizontal and vertical resolution for chroma)
 *                 2 - 422 (half horizontal resolution for chroma)
 * @block_layout: superbloc layout
 *                 0 - 16x16
 *                 1 - 16x16 420 subsampling
 *                 2 - 16x16 422 subsampling
 *                 3 - 32x8  444 subsampling
 * @inputbits[4]: indicates the number of bits for every component
 * @yuv_transform: yuv transform is used on the decoded data
 * @ block_size[2]: the horizontal/vertical size in units of block
 * @ image_crop[2]: how many pixels to crop to the left/top of the output image
 */

/*
 * superblock_layout_map - mapping from block size and subsampling to superblock layout.
 *
 *  Reference to chapter 9 'Premitted superblock layouts' of
 *         <ARM fram buffer compression 1-2 format specification>
 *
 *    |-------------------+-------+--------+---------------------+---------------|
 *    | SuperBlock Layout | width | height | Support ColorFormat | Support Split |
 *    |-------------------+-------+--------+---------------------+---------------|
 *    |                 0 |    16 |     16 | ALL                 | Yes           |
 *    |                 1 |    16 |     16 | YUV 4:2:0           | Yes           |
 *    |                 2 |    16 |     16 | YUV 4:2:2           | Yes           |
 *    |                 3 |    32 |      8 | RGB 17bits and more | Required      |
 *    |                 4 |    32 |      8 | RGB 16bits and less | No            |
 *    |                 5 |    32 |      8 | YUV 4:2:0           | No            |
 *    |                 6 |    32 |      8 | YUV 4:2:2           | No            |
 *    |-------------------+-------+--------+---------------------+---------------|
 *
 */

struct disp_afbc_info {
	u32 header_layout;
	u32 block_layout;
	u32 inputbits[4];
	u32 yuv_transform;
	u32 block_size[2];
	u32 image_crop[2];
};

enum fbd_atw_type {
	FBD_ATW_TYPE_INVALID = 0,
	FBD_ATW_TYPE_VI,
	FBD_TYPE_UI,
};

enum { FBD_ATW_V_REG_BLK_FBD = 0,
       FBD_ATW_V_REG_BLK_ATW,
       FBD_ATW_V_REG_BLK_NUM,

       FBD_ATW_MAX_REG_BLK_NUM = FBD_ATW_V_REG_BLK_NUM,
};

enum { FBD_U_REG_BLK_FBD = 0,
       FBD_U_REG_BLK_NUM,
};

struct de_fbd_atw_private {
	struct de_reg_mem_info reg_mem_info;
	u32 reg_blk_num;
	union {
		struct de_reg_block fbd_atw_v_blks[FBD_ATW_V_REG_BLK_NUM];
		struct de_reg_block fbd_u_blks[FBD_U_REG_BLK_NUM];
		struct de_reg_block reg_blks[FBD_ATW_MAX_REG_BLK_NUM];
	};
	enum fbd_atw_type type;
};

const static struct afbc_stream_info afbc_stream_infos[] = {
	{ DRM_FORMAT_ARGB8888, 0, {8, 8, 8, 8}, {0, 3} },
	{ DRM_FORMAT_ABGR8888, 0, {8, 8, 8, 8}, {0, 3} },
	{ DRM_FORMAT_RGBA8888, 0, {8, 8, 8, 8}, {0, 3} },
	{ DRM_FORMAT_BGRA8888, 0, {8, 8, 8, 8}, {0, 3} },

	{ DRM_FORMAT_XRGB8888, 0, {8, 8, 8, 8}, {0, 3} },
	{ DRM_FORMAT_XBGR8888, 0, {8, 8, 8, 8}, {0, 3} },
	{ DRM_FORMAT_RGBX8888, 0, {8, 8, 8, 8}, {0, 3} },
	{ DRM_FORMAT_BGRX8888, 0, {8, 8, 8, 8}, {0, 3} },

	{ DRM_FORMAT_RGB888,   0, {8, 8, 8, 0}, {0, 3} },
	{ DRM_FORMAT_BGR888,   0, {8, 8, 8, 0}, {0, 3} },

	{ DRM_FORMAT_RGB565,   0, {5, 6, 5, 0}, {0, 4} },
	{ DRM_FORMAT_BGR565,   0, {5, 6, 5, 0}, {0, 4} },

	{ DRM_FORMAT_ARGB4444, 0, {4, 4, 4, 4}, {0, 4} },
	{ DRM_FORMAT_ABGR4444, 0, {4, 4, 4, 4}, {0, 4} },
	{ DRM_FORMAT_RGBA4444, 0, {4, 4, 4, 4}, {0, 4} },
	{ DRM_FORMAT_BGRA4444, 0, {4, 4, 4, 4}, {0, 4} },

	{ DRM_FORMAT_ARGB1555, 0, {5, 5, 5, 1}, {0, 4} },
	{ DRM_FORMAT_ABGR1555, 0, {5, 5, 5, 1}, {0, 4} },
	{ DRM_FORMAT_RGBA5551, 0, {5, 5, 5, 1}, {0, 4} },
	{ DRM_FORMAT_BGRA5551, 0, {5, 5, 5, 1}, {0, 4} },

	{ DRM_FORMAT_YUV422,   2, {8, 8, 8, 0}, {2, 6} },
	{ DRM_FORMAT_NV61,     2, {8, 8, 8, 0}, {2, 6} },
	{ DRM_FORMAT_NV16,     2, {8, 8, 8, 0}, {2, 6} },

	{ DRM_FORMAT_YUV420,   1, {8, 8, 8, 0}, {1, 5} },
	{ DRM_FORMAT_YVU420,   1, {8, 8, 8, 0}, {1, 5} },
	{ DRM_FORMAT_NV12,     1, {8, 8, 8, 0}, {1, 5} },

	{ DRM_FORMAT_ABGR2101010, 0, {10, 10, 10, 2}, {0, 3} },
};

static const uint64_t format_modifiers_afbc[] = {
	SUNXI_YUV_AFBC_MOD,
	SUNXI_RGB_AFBC_MOD,
	DRM_FORMAT_MOD_INVALID,
};

struct de_version_afbd {
	unsigned int version;
	bool rotate_support;
	unsigned int phy_chn_cnt;
	bool *afbd_exist;
};

static bool de350_afbd_exist[] = {
	true, false, false, true, true, true, false,
};

static struct de_version_afbd de350 = {
	.version = 0x350,
	.phy_chn_cnt = ARRAY_SIZE(de350_afbd_exist),
	.afbd_exist = de350_afbd_exist,
};

static bool de355_afbd_exist[] = {
	true, false, true, true, true,
};

static struct de_version_afbd de355 = {
	.version = 0x355,
	.rotate_support = true,
	.phy_chn_cnt = ARRAY_SIZE(de355_afbd_exist),
	.afbd_exist = de355_afbd_exist,
};

static bool de352_afbd_exist[] = {
	true, false, true, false, false, false, false,
};

static struct de_version_afbd de352 = {
	.version = 0x352,
	.rotate_support = true,
	.phy_chn_cnt = ARRAY_SIZE(de352_afbd_exist),
	.afbd_exist = de352_afbd_exist,
};

static struct de_version_afbd *de_version[] = {
	&de350, &de355, &de352,
};

static bool is_support_rotate(struct module_create_info *info)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(de_version); i++) {
		if (de_version[i]->version == info->de_version) {
			return de_version[i]->rotate_support;
		}
	}
	return false;
}

static bool is_afbd_exist(struct module_create_info *info)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(de_version); i++) {
		if (de_version[i]->version == info->de_version) {
			return de_version[i]->afbd_exist[info->id];
		}
	}
	return false;
}

static inline struct fbd_atw_v_reg *
get_fbd_atw_v_reg(struct de_fbd_atw_private *priv)
{
	return (struct fbd_atw_v_reg *)(priv->fbd_atw_v_blks[0].vir_addr);
}

static inline struct fbd_u_reg *get_fbd_u_reg(struct de_fbd_atw_private *priv)
{
	return (struct fbd_u_reg *)(priv->fbd_u_blks[0].vir_addr);
}

static void fbd_atw_set_block_dirty(struct de_fbd_atw_private *priv,
				       u32 blk_id, u32 dirty)
{
	priv->reg_blks[blk_id].dirty = dirty;
	if (priv->reg_blks[blk_id].rcq_hd)
		priv->reg_blks[blk_id].rcq_hd->dirty.dwval = dirty;
}

bool de_afbc_format_mod_supported(struct de_afbd_handle *hdl, u32 format, u64 modifier)
{
	if (modifier == DRM_FORMAT_MOD_INVALID)
		return false;

	// TODO: filter out the formats which not support AFBC
	if (modifier == SUNXI_RGB_AFBC_MOD || modifier == SUNXI_YUV_AFBC_MOD)
		return true;

    return false;
}

static void fill_afbc_info(const struct drm_framebuffer *fb, struct disp_afbc_info *afbc)
{
	size_t i = 0;
	uint32_t wide_block = (fb->modifier & DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)) ==
		DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8);

	for (i = 0; i < ARRAY_SIZE(afbc_stream_infos); i++) {
		if (afbc_stream_infos[i].format == fb->format->format) {
			afbc->header_layout = afbc_stream_infos[i].header_layout;
			afbc->inputbits[0]  = afbc_stream_infos[i].inputbits[0];
			afbc->inputbits[1]  = afbc_stream_infos[i].inputbits[1];
			afbc->inputbits[2]  = afbc_stream_infos[i].inputbits[2];
			afbc->inputbits[3]  = afbc_stream_infos[i].inputbits[3];

			afbc->block_layout = wide_block ? afbc_stream_infos[i].superblock_layout[1]
											: afbc_stream_infos[i].superblock_layout[0];
			break;
		}
	}

	if (i == ARRAY_SIZE(afbc_stream_infos)) {
		DRM_ERROR("get an drm format %d, not support afbc compress\n", fb->format->format);
		afbc->header_layout = 0;
		afbc->inputbits[0]  = 8;
		afbc->inputbits[1]  = 8;
		afbc->inputbits[2]  = 8;
		afbc->inputbits[3]  = 8;
	}

	afbc->image_crop[0] = 0;
	afbc->image_crop[1] = 0; // TODO: yuv stream from aw decoder should crop 16 lines on top

	if ((fb->modifier & DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_YTR)) ==
		DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_YTR)) {
		afbc->yuv_transform = 1;
	} else {
		afbc->yuv_transform = 0;
	}

	if (wide_block) {
		afbc->block_size[0] = (fb->width  + 31) / 32;
		afbc->block_size[1] = (fb->height +  7) / 8;
	} else {
		afbc->block_size[0] = (fb->width  + 15) / 16;
		afbc->block_size[1] = (fb->height + 15) / 16;
	}

	DRM_DEBUG_DRIVER(
		"[SUNXI-CRTC] afbc header_layout=%d block_layout=%d width=%d height=%d "
		"modifier=0x%016llx\n",
		afbc->header_layout, afbc->block_layout, fb->width, fb->height, (unsigned long long)fb->modifier);
}

static s32 de_fbd_get_info(const struct drm_framebuffer *fb, struct de_fbd_info *info, struct disp_afbc_info *afbc)
{
	u32 inputbits;

	fill_afbc_info(fb, afbc);

	inputbits = afbc->inputbits[0] | (afbc->inputbits[1] << 8) |
		    (afbc->inputbits[2] << 16) | (afbc->inputbits[3] << 24);

	info->block_layout = afbc->block_layout;
	info->yuv_tran = afbc->yuv_transform;

	switch (afbc->header_layout) {
	case 0:
		if (inputbits == 0x08080808) {
			info->fmt = FBD_RGBA8888;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 8;
			info->compbits[1] = 9;
			info->compbits[2] = 9;
			info->compbits[3] = 8;
		} else if ((inputbits & 0xFFFFFF) == 0x080808) {
			info->fmt = FBD_RGB888;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 8;
			info->compbits[1] = 9;
			info->compbits[2] = 9;
			info->compbits[3] = 0;
		} else if ((inputbits & 0xFFFFFF) == 0x050605) {
			info->fmt = FBD_RGB565;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 6;
			info->compbits[1] = 7;
			info->compbits[2] = 7;
			info->compbits[3] = 0;
		} else if (inputbits == 0x04040404) {
			info->fmt = FBD_RGBA4444;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 4;
			info->compbits[1] = 5;
			info->compbits[2] = 5;
			info->compbits[3] = 4;
		} else if (inputbits == 0x01050505) {
			info->fmt = FBD_RGBA5551;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 5;
			info->compbits[1] = 6;
			info->compbits[2] = 6;
			info->compbits[3] = 1;
		} else if (inputbits == 0x020A0A0A) {
			info->fmt = FBD_RGBA1010102;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 10;
			info->compbits[1] = 11;
			info->compbits[2] = 11;
			info->compbits[3] = 2;
		} else if ((inputbits & 0xFFFFFF) == 0x0A0A0A) {
			info->fmt = FBD_YUV420B10;
			info->sbs[0] = 1;
			info->sbs[1] = 2;
			info->compbits[0] = 10;
			info->compbits[1] = 10;
			info->compbits[2] = 10;
			info->compbits[3] = 0;
		}
		break;
	case 1:
		if ((inputbits & 0xFFFFFF) == 0x080808) {
			info->fmt = FBD_YUV420;
			info->sbs[0] = 1;
			info->sbs[1] = 1;
			info->compbits[0] = 8;
			info->compbits[1] = 8;
			info->compbits[2] = 8;
			info->compbits[3] = 0;
		} else if ((inputbits & 0xFFFFFF) == 0x0A0A0A) {
			info->fmt = FBD_YUV420B10;
			info->sbs[0] = 1;
			info->sbs[1] = 2;
			info->compbits[0] = 10;
			info->compbits[1] = 10;
			info->compbits[2] = 10;
			info->compbits[3] = 0;
		}
		break;
	case 2:
		if ((inputbits & 0xFFFFFF) == 0x080808) {
			info->fmt = FBD_YUV422;
			info->sbs[0] = 1;
			info->sbs[1] = 2;
			info->compbits[0] = 8;
			info->compbits[1] = 8;
			info->compbits[2] = 8;
			info->compbits[3] = 0;
		} else if ((inputbits & 0xFFFFFF) == 0x0A0A0A) {
			info->fmt = FBD_YUV422B10;
			info->sbs[0] = 2;
			info->sbs[1] = 3;
			info->compbits[0] = 10;
			info->compbits[1] = 10;
			info->compbits[2] = 10;
			info->compbits[3] = 0;
		}
		break;
	default:
		info->fmt = 0;
		info->sbs[0] = 1;
		info->sbs[1] = 1;
		info->compbits[0] = 8;
		info->compbits[1] = 8;
		info->compbits[2] = 8;
		info->compbits[3] = 8;
		DRM_ERROR("no support afbd header layout %d\n", afbc->header_layout);
		break;
	}
	return 0;
}

static s32 de_fbd_atw_disable(struct de_afbd_handle *handle)
{
	struct de_fbd_atw_private *priv = handle->private;
	if (priv->type == FBD_ATW_TYPE_VI) {
		struct fbd_atw_v_reg *reg = get_fbd_atw_v_reg(priv);

		reg->fbd_ctl.dwval = 0;
		reg->atw_attr.dwval = 0;
		fbd_atw_set_block_dirty(priv, FBD_ATW_V_REG_BLK_FBD, 1);
		fbd_atw_set_block_dirty(priv, FBD_ATW_V_REG_BLK_ATW, 1);
	} else if (priv->type == FBD_TYPE_UI) {
		struct fbd_u_reg *reg = get_fbd_u_reg(priv);

		reg->fbd_ctl.dwval = 0;
		fbd_atw_set_block_dirty(priv, FBD_U_REG_BLK_FBD, 1);
	}
	return 0;
}

int de_afbd_apply_lay(struct de_afbd_handle *handle, struct display_channel_state *state, struct de_afbd_cfg *cfg, bool *is_enable)
{
	u32 dwval;
	u32 width, height, left, top;
	struct de_fbd_info fbd_info;
	struct disp_afbc_info afbc_info;
	struct drm_framebuffer *fb = state->base.fb;
	struct de_fbd_atw_private *priv = handle->private;
	struct fbd_atw_v_reg *reg = get_fbd_atw_v_reg(priv);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
	struct drm_gem_cma_object *gem;
#else
	struct drm_gem_dma_object *gem;
#endif
	u64 addr;

	if (!state->base.fb || !de_afbc_format_mod_supported(handle, fb ? fb->format->format : 0, fb->modifier)) {
		*is_enable = false;
		return de_fbd_atw_disable(handle);
	}

	de_fbd_get_info(state->base.fb, &fbd_info, &afbc_info);

	dwval = 1 | (1 << 4);
	dwval |= ((state->base.pixel_blend_mode != DRM_MODE_BLEND_PIXEL_NONE ? 2 : 1) << 2);
	dwval |= (state->base.alpha >> 8 << 24);
	reg->fbd_ctl.dwval = dwval;

//	reg->atw_rotate.dwval = lay_info->transform & 0x7;
	dwval = 0;
	switch (fb->format->format) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ARGB1555:
		dwval = 0x1230;
		break;
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_ABGR1555:
		dwval = 0x3210;
		break;
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_RGBA5551:
		dwval = 0x0123;
		break;
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_BGRA5551:
		dwval = 0x2103;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_RGB565:
		dwval = 0x1230;
		break;
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_BGR565:
		dwval = 0x3210;
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
//	case DISP_FORMAT_YUV422_P_10BIT:
//	case DISP_FORMAT_YUV420_P_10BIT: not exist in drm
		dwval = 0x3210;
		break;
	default:
		dwval = 0x3210;
		DRM_ERROR("unsupported format=%x\n", fb->format->format);
	}
	reg->fbd_fmt_seq.dwval = dwval;

	width = ((unsigned long long)state->base.src_w) >> 16;
	height = ((unsigned long long)state->base.src_h) >> 16;
	dwval = ((width ? (width - 1) : 0) & 0xFFF) |
		(height ? (((height - 1) & 0xFFF) << 16) : 0);
	reg->fbd_img_size.dwval = dwval;

	width  = afbc_info.block_size[0];
	height = afbc_info.block_size[1];
	dwval = (width & 0x3FF) | ((height & 0x3FF) << 16);
	reg->fbd_blk_size.dwval = dwval;

	left = afbc_info.image_crop[0];
	top  = afbc_info.image_crop[1];
	dwval = (left & 0xF) | ((top & 0xF) << 16);
	reg->fbd_src_crop.dwval = dwval;

	left = (u32)((state->base.src_x) >> 16);
	top  = (u32)((state->base.src_y) >> 16);
	dwval = (left & 0xFFF) | ((top & 0xFFF) << 16);
	reg->fbd_lay_crop.dwval = dwval;

	dwval = (fbd_info.fmt & 0x7F) | ((fbd_info.yuv_tran & 0x1) << 7) |
		((fbd_info.block_layout & 0x3) << 8) |
		((fbd_info.sbs[0] & 0x3) << 16) |
		((fbd_info.sbs[1] & 0x3) << 18);
	reg->fbd_fmt.dwval = dwval;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
	gem = drm_fb_cma_get_gem_obj(fb, 0);
	if (gem) {
		addr = (u64)(gem->paddr) + fb->offsets[0];
#else
	gem = drm_fb_dma_get_gem_obj(fb, 0);
	if (gem) {
		addr = (u64)(gem->dma_addr) + fb->offsets[0];
#endif
	} else {
		*is_enable = false;
		DRM_ERROR("not found paddr for afbc buffer\n");
		return -EINVAL;
	}

	reg->fbd_hd_laddr.dwval = (u32)(addr);
	reg->fbd_hd_haddr.dwval = (u32)(addr >> 32) & 0xFF;

	width = cfg->ovl_win.width;
	height = cfg->ovl_win.height;
	dwval = (width ? ((width - 1) & 0x1FFF) : 0) |
		(height ? (((height - 1) & 0x1FFF) << 16) : 0);
	reg->fbd_ovl_size.dwval = dwval;

	left = cfg->ovl_win.left;
	top = cfg->ovl_win.top;
	dwval = (left & 0x1FFF) | ((top & 0x1FFF) << 16);
	reg->fbd_ovl_coor.dwval = dwval;

	reg->fbd_bg_color.dwval = 0xFF000000;

/* this will not be used by user
	reg->fbd_fcolor.dwval =
	    ((lay_info->mode == LAYER_MODE_BUFFER) ? 0 : lay_info->color);*/

	dwval = (((1 << fbd_info.compbits[0]) - 1) & 0xFFFF) |
		((((1 << fbd_info.compbits[3]) - 1) & 0xFFFF) << 16);
	reg->fbd_color0.dwval = dwval;
	dwval = (((1 << fbd_info.compbits[2]) - 1) & 0xFFFF) |
		((((1 << fbd_info.compbits[1]) - 1) & 0xFFFF) << 16);
	reg->fbd_color1.dwval = dwval;

	fbd_atw_set_block_dirty(priv, FBD_ATW_V_REG_BLK_FBD, 1);
	*is_enable = true;

	return 0;
}

struct de_afbd_handle *de_afbd_create(struct module_create_info *info)
{
	struct de_afbd_handle *hdl;
	struct de_reg_block *block;
	struct de_reg_mem_info *reg_mem_info;
	u8 __iomem *reg_base;
	int i;
	struct de_fbd_atw_private *priv;

	if (!is_afbd_exist(info))
		return NULL;

	reg_base = (u8 __iomem *)(info->de_reg_base + info->reg_offset + CHN_FBD_ATW_OFFSET);

	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);

	memcpy(&hdl->cinfo, info, sizeof(*info));
	hdl->format_modifiers = format_modifiers_afbc;
	hdl->format_modifiers_num = ARRAY_SIZE(format_modifiers_afbc) - 1;
	hdl->rotate_support = is_support_rotate(info);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	priv = hdl->private;
	reg_mem_info = &(hdl->private->reg_mem_info);

/*	if (IS_PHY_CHN_VIDEO(phy_chn)) {
		priv->type = FBD_ATW_TYPE_VI;
		reg_mem_info->size = sizeof(struct fbd_atw_v_reg);
		reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
			reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
				info->update_mode == RCQ_MODE);
		if (NULL == reg_mem_info->vir_addr) {
			DRM_ERROR("alloc afbd %d mm fail!size=0x%x\n",
			       phy_chn, reg_mem_info->size);
			return ERR_PTR(-ENOMEM);
		}
		block = &(priv->fbd_atw_v_blks[FBD_ATW_V_REG_BLK_FBD]);
		block->phy_addr = reg_mem_info->phy_addr;
		block->vir_addr = reg_mem_info->vir_addr;
		block->size = 0x5C;
		block->reg_addr = reg_base;

		block = &(priv->fbd_atw_v_blks[FBD_ATW_V_REG_BLK_ATW]);
		block->phy_addr = reg_mem_info->phy_addr + 0x100;
		block->vir_addr = reg_mem_info->vir_addr + 0x100;
		block->size = 0x40;
		block->reg_addr = reg_base + 0x100;

		priv->reg_blk_num = FBD_ATW_V_REG_BLK_NUM;

	} else*/ {
		priv->type = FBD_TYPE_UI;
		reg_mem_info->size = sizeof(struct fbd_u_reg);
		reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
			reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
				info->update_mode == RCQ_MODE);
		if (NULL == reg_mem_info->vir_addr) {
			DRM_ERROR("alloc afbd %d mm fail!size=0x%x\n",
			       info->id, reg_mem_info->size);
			return ERR_PTR(-ENOMEM);
		}
		block = &(priv->fbd_u_blks[FBD_U_REG_BLK_FBD]);
		block->phy_addr = reg_mem_info->phy_addr;
		block->vir_addr = reg_mem_info->vir_addr;
		block->size = 0x58;
		block->reg_addr = reg_base;

		priv->reg_blk_num = FBD_U_REG_BLK_NUM;
	}

	hdl->block_num = priv->reg_blk_num;
	hdl->block = kmalloc(sizeof(block[0]) * hdl->block_num, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < hdl->private->reg_blk_num; i++)
		hdl->block[i] = &priv->reg_blks[i];
	return hdl;
}
