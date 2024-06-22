/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Allwinner SoCs display driver.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "de_gamma_type.h"
#include "de_gamma.h"
#include "de_gamma_platform.h"

#define DISP_GAMMA_OFFSET	(0x9000)

enum {
	GAMMA_CSC_CTL_BLK = 0,
	GAMMA_CSC_TAB_BLK,
	GAMMA_CTL_REG_BLK,
	GAMMA_CTC_REG_BLK,
	GAMMA_DEMO_SKIN_BLK,
	GAMMA_TAB_REG_BLK,
	GAMMA_REG_BLK_NUM,
};

struct de_gamma_private {
	const struct de_gamma_dsc *dsc;
	struct de_csc_handle *csc;
	bool gamma_tbl_set;
	struct de_reg_mem_info reg_mem_info;
	u32 reg_blk_num;
	struct de_reg_block reg_blks[GAMMA_REG_BLK_NUM];
};

static inline struct gamma_reg *get_gamma_reg(struct de_gamma_private *priv)
{
	return (struct gamma_reg *)(priv->reg_blks[GAMMA_CSC_CTL_BLK].vir_addr);
}

static void gamma_set_block_dirty(struct de_gamma_private *priv, u32 blk_id,
				     u32 dirty)
{
	priv->reg_blks[blk_id].dirty = dirty;
	if (priv->reg_blks[blk_id].rcq_hd)
		priv->reg_blks[blk_id].rcq_hd->dirty.dwval = dirty;
}

static int de_gamma_set_cm_size(struct de_gamma_handle *hdl, u32 width, u32 height)
{
	struct de_gamma_private *priv = hdl->private;
	struct gamma_reg *reg = get_gamma_reg(priv);

	reg->cm_size.bits.width = width - 1;
	reg->cm_size.bits.height = height - 1;
	gamma_set_block_dirty(priv, GAMMA_CSC_CTL_BLK, 1);
	return 0;
}

static int de_gamma_set_csc(struct de_gamma_handle *hdl, u32 en, u32 w, u32 h, int *csc_coeff)
{
	struct de_gamma_private *priv = hdl->private;
	struct gamma_reg *reg = get_gamma_reg(priv);

	if (!en || csc_coeff == NULL) {
		reg->cm_en.dwval = 0x0;
		gamma_set_block_dirty(priv, GAMMA_CSC_CTL_BLK, 1);
		return -1;
	}
	reg->cm_en.dwval = 0x1;
	reg->cm_c00.dwval = (*(csc_coeff)) << 7;
	reg->cm_c01.dwval = (*(csc_coeff + 1)) << 7;
	reg->cm_c02.dwval = (*(csc_coeff + 2)) << 7;
	reg->cm_c03.dwval = (*(csc_coeff + 3) + 0x200) << 7;
	reg->cm_c10.dwval = (*(csc_coeff + 4)) << 7;
	reg->cm_c11.dwval = (*(csc_coeff + 5)) << 7;
	reg->cm_c12.dwval = (*(csc_coeff + 6)) << 7;
	reg->cm_c13.dwval = (*(csc_coeff + 7) + 0x200) << 7;
	reg->cm_c20.dwval = (*(csc_coeff + 8)) << 7;
	reg->cm_c21.dwval = (*(csc_coeff + 9)) << 7;
	reg->cm_c22.dwval = (*(csc_coeff + 10)) << 7;
	reg->cm_c23.dwval = (*(csc_coeff + 11) + 0x200) << 7;

	gamma_set_block_dirty(priv, GAMMA_CSC_CTL_BLK, 1);
	gamma_set_block_dirty(priv, GAMMA_CSC_TAB_BLK, 1);
	de_gamma_set_cm_size(hdl, w, h);
	return 0;
}

int de_gamma_apply_csc(struct de_gamma_handle *hdl, u32 w, u32 h, const struct de_csc_info *in_info,
		    const struct de_csc_info *out_info, const struct bcsh_info *bcsh)
{
	int csc_coeff[12];
	if (!hdl->private->csc)
		return -1;
	de_dcsc_apply(hdl->private->csc, in_info, out_info, bcsh, csc_coeff, false);
	de_gamma_set_csc(hdl, 1, w, h, csc_coeff);
	return 0;
}

int de_gamma_demo_mode_config(struct de_gamma_handle *hdl, struct de_gamma_demo_cfg *cfg)
{
	struct de_gamma_private *priv = hdl->private;
	struct gamma_reg *reg = get_gamma_reg(priv);

	reg->demo_ctrl.bits.demo_en = cfg->enable;
	reg->hori_win.bits.start = cfg->x;
	reg->hori_win.bits.end = cfg->x + cfg->w - 1;
	reg->vert_win.bits.start = cfg->y;
	reg->vert_win.bits.end = cfg->y + cfg->h - 1;
	gamma_set_block_dirty(priv, GAMMA_DEMO_SKIN_BLK, 1);
	return 0;
}

static int de_gamma_set_table(struct de_gamma_handle *hdl, u32 en, u32 *gamma_tbl)
{
	struct de_gamma_private *priv = hdl->private;
	struct gamma_reg *reg = get_gamma_reg(priv);
	int i;
	unsigned int *addr = NULL;

	if (!en || gamma_tbl == NULL) {
		priv->gamma_tbl_set = false;
		reg->ctl.dwval = 0x0;
		gamma_set_block_dirty(priv, GAMMA_CTL_REG_BLK, 1);
		return -1;
	}

	priv->gamma_tbl_set = true;
	addr = (unsigned int *)(&(reg->tab[0]));
	reg->ctl.dwval = 0x1;

	for (i = 0; i < hdl->gamma_lut_len; i++) {
		*addr = gamma_tbl[i];
		addr++;
	}

	gamma_set_block_dirty(priv, GAMMA_CTL_REG_BLK, 1);
	gamma_set_block_dirty(priv, GAMMA_TAB_REG_BLK, 1);

	return 0;
}

static int de_gamma_skin_config(struct de_gamma_handle *hdl, bool enable,
			    unsigned int darken, unsigned int brighten)
{
	struct de_gamma_private *priv = hdl->private;
	struct gamma_reg *reg = get_gamma_reg(priv);
	reg->skin_prot.bits.skin_en = enable ? 1 : 0;
	reg->skin_prot.bits.skin_darken_w = darken;
	reg->skin_prot.bits.skin_brighten_w = brighten;
	gamma_set_block_dirty(priv, GAMMA_CTC_REG_BLK, 1);
	return 0;
}

int de_gamma_ctc_config(struct de_gamma_handle *hdl, struct de_ctc_cfg *cfg)
{
	struct de_gamma_private *priv = hdl->private;
	struct gamma_reg *reg = get_gamma_reg(priv);
	reg->crc_ctrl.bits.ctc_en = cfg->enable ? 1 : 0;
	reg->r_gain.bits.gain = cfg->red_gain;
	reg->g_gain.bits.gain = cfg->green_gain;
	reg->b_gain.bits.gain = cfg->blue_gain;
	reg->r_offset.bits.offset = cfg->red_offset;
	reg->g_offset.bits.offset = cfg->green_offset;
	reg->b_offset.bits.offset = cfg->blue_offset;
	gamma_set_block_dirty(priv, GAMMA_DEMO_SKIN_BLK, 1);
	return 0;
}

int de_gamma_config(struct de_gamma_handle *hdl, struct de_gamma_cfg *cfg)
{
	de_gamma_set_table(hdl, cfg->enable, cfg->gamma_tbl);
	de_gamma_skin_config(hdl, cfg->skin_protect_enable, cfg->skin_darken, cfg->skin_brighten);
	return 0;
}

void de_gamma_dump_state(struct drm_printer *p, struct de_gamma_handle *hdl)
{
	unsigned long base = (unsigned long)hdl->private->reg_blks[0].reg_addr;
	unsigned long de_base = (unsigned long)hdl->cinfo.de_reg_base;

	drm_printf(p, "\tgamma%d@%8x:\n", hdl->private->dsc->id, (unsigned int)(base - de_base));
	drm_printf(p, "\t\tgamma_tbl %s\n", hdl->private->gamma_tbl_set ? "on" : "off");
	if (hdl->private->csc)
		de_csc_dump_state(p, hdl->private->csc);

}

struct de_gamma_handle *de_gamma_create(struct module_create_info *info)
{
	struct de_gamma_handle *hdl;
	struct de_reg_block *reg_blk;
	struct de_reg_mem_info *reg_mem_info;
	u8 __iomem *base;
	int i;
	struct de_gamma_private *priv;
	const struct de_gamma_dsc *dsc;
	struct module_create_info csc;
	struct csc_extra_create_info excsc;

	dsc = get_gamma_dsc(info);
	if (!dsc)
		return NULL;

	base = (u8 __iomem *)(info->de_reg_base + info->reg_offset + DISP_GAMMA_OFFSET);
	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	memcpy(&hdl->cinfo, info, sizeof(*info));
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	hdl->gamma_lut_len = dsc->gamma_lut_len;
	hdl->support_ctc = dsc->support_ctc;
	hdl->support_cm = dsc->support_cm;
	hdl->support_demo_skin = dsc->support_demo_skin;

	if (dsc->support_cm) {
		excsc.type = GAMMA_CSC;
		excsc.extra_id = 0;
		memcpy(&csc, info, sizeof(*info));
		csc.extra = &excsc;
		hdl->private->csc = de_csc_create(&csc);
		WARN_ON(!hdl->private->csc);
	}
	priv = hdl->private;
	hdl->private->dsc = dsc;
	reg_mem_info = &(hdl->private->reg_mem_info);
	reg_mem_info->size = sizeof(struct gamma_reg);
	reg_mem_info->vir_addr = (u8 *)sunxi_de_reg_buffer_alloc(hdl->cinfo.de,
		reg_mem_info->size, (void *)&(reg_mem_info->phy_addr),
		info->update_mode == RCQ_MODE);
	if (reg_mem_info->vir_addr == NULL) {
		DRM_ERROR("alloc gamma[%d] mm fail!size=0x%x\n",
			 dsc->id, reg_mem_info->size);
		return ERR_PTR(-ENOMEM);
	}

	reg_blk = &(priv->reg_blks[GAMMA_CSC_CTL_BLK]);
	reg_blk->phy_addr = reg_mem_info->phy_addr;
	reg_blk->vir_addr = reg_mem_info->vir_addr;
	reg_blk->size = 0x8;
	reg_blk->reg_addr = (u8 __iomem *)base;

	reg_blk = &(priv->reg_blks[GAMMA_CSC_TAB_BLK]);
	reg_blk->phy_addr = reg_mem_info->phy_addr + 0x10;
	reg_blk->vir_addr = reg_mem_info->vir_addr + 0x10;
	reg_blk->size = 0x30;
	reg_blk->reg_addr = (u8 __iomem *)base + 0x10;

	reg_blk = &(priv->reg_blks[GAMMA_CTL_REG_BLK]);
	reg_blk->phy_addr = reg_mem_info->phy_addr + 0x40;
	reg_blk->vir_addr = reg_mem_info->vir_addr + 0x40;
	reg_blk->size = 0x8;
	reg_blk->reg_addr = (u8 __iomem *)base + 0x40;

	reg_blk = &(priv->reg_blks[GAMMA_CTC_REG_BLK]);
	reg_blk->phy_addr = reg_mem_info->phy_addr + 0x50;
	reg_blk->vir_addr = reg_mem_info->vir_addr + 0x50;
	reg_blk->size = 0x1c;
	reg_blk->reg_addr = (u8 __iomem *)base + 0x50;

	reg_blk = &(priv->reg_blks[GAMMA_DEMO_SKIN_BLK]);
	reg_blk->phy_addr = reg_mem_info->phy_addr + 0x70;
	reg_blk->vir_addr = reg_mem_info->vir_addr + 0x70;
	reg_blk->size = 0x10;
	reg_blk->reg_addr = (u8 __iomem *)base + 0x70;

	reg_blk = &(priv->reg_blks[GAMMA_TAB_REG_BLK]);
	reg_blk->phy_addr = reg_mem_info->phy_addr + 0x100;
	reg_blk->vir_addr = reg_mem_info->vir_addr + 0x100;
	reg_blk->size = hdl->gamma_lut_len * 4;
	reg_blk->reg_addr = (u8 __iomem *)base + 0x100;

	priv->reg_blk_num = GAMMA_REG_BLK_NUM;
	hdl->block_num = priv->reg_blk_num;
	hdl->block = kmalloc(sizeof(reg_blk[0]) * hdl->block_num, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < hdl->private->reg_blk_num; i++)
		hdl->block[i] = &priv->reg_blks[i];

	return hdl;
}
