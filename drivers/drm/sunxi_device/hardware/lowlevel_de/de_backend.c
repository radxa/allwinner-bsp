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

#include <linux/workqueue.h>
#include "de_backend.h"
#include "crc/de_crc.h"
#include "gamma/de_gamma.h"
#include "dither/de_dither.h"
#include "smbl/de_smbl.h"

struct de_backend_private {
	struct de_crc_handle *crc;
	struct de_gamma_handle *gamma;
	struct de_dither_handle *dither;
	struct de_smbl_handle *smbl;
	struct de_csc_handle *csc;
};

static u32 *get_sunxi_gamma_lut(struct drm_color_lut *gamma_lut, bool is_10bit)
{
	u32 *out;
	int i, lut_size = is_10bit ? 1024 : 256;
	int component_shift = is_10bit ? 10 : 8;
	int shift = is_10bit ? 6 : 8;
	u32 r, g, b;
	out = kmalloc(sizeof(*out) * lut_size, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < lut_size; i++) {
		r = gamma_lut[i].red >> shift << (component_shift << 1);
		g = gamma_lut[i].green >> shift << component_shift;
		b = gamma_lut[i].blue >> shift;
		out[i] = r | g | b;
	}
	return out;
}

static void put_sunxi_gamma_lut(u32 *lut)
{
	kfree(lut);
}

int de_backend_apply(struct de_backend_handle *hdl, struct de_backend_apply_cfg *cfg)
{
	struct de_gamma_cfg gamma;
	struct de_crc_gbl_cfg gcrc;
	struct bcsh_info bcsh;

	DRM_DEBUG_DRIVER("[SUNXI-DE] %s \n", __FUNCTION__);
	//TODO add dirty control
	if (hdl->private->crc) {
		gcrc.w = cfg->w;
		gcrc.h = cfg->h;
		de_crc_global_config(hdl->private->crc, &gcrc);
	}

	if (cfg->gamma_dirty && hdl->private->gamma) {
		memset(&gamma, 0, sizeof(gamma));
		gamma.enable = false;
		if (cfg->gamma_lut) {
			gamma.enable = true;
			gamma.gamma_tbl = get_sunxi_gamma_lut(cfg->gamma_lut,
						hdl->private->gamma->gamma_lut_len == 1024);
		}
		de_gamma_config(hdl->private->gamma, &gamma);
		put_sunxi_gamma_lut(gamma.gamma_tbl);
		DRM_DEBUG_DRIVER("[SUNXI-DE] %s gamma dirty\n", __FUNCTION__);
	}

	if (cfg->csc_dirty) {
		bcsh.enable = true;
		bcsh.brightness = cfg->brightness;
		bcsh.contrast = cfg->contrast;
		bcsh.saturation = cfg->saturation;
		bcsh.hue = cfg->hue;

		if (hdl->private->gamma && hdl->private->gamma->support_cm) {
			de_gamma_apply_csc(hdl->private->gamma, cfg->w, cfg->h,
					    &cfg->in_csc, &cfg->out_csc, &bcsh);
			DRM_DEBUG_DRIVER("[SUNXI-DE] %s gamma csc dirty\n", __FUNCTION__);
		} else if (hdl->private->smbl) {
			de_smbl_apply_csc(hdl->private->smbl, cfg->w, cfg->h,
					    &cfg->in_csc, &cfg->out_csc, &bcsh);
			DRM_DEBUG_DRIVER("[SUNXI-DE] %s device csc dirty\n", __FUNCTION__);
		}
		//TODO add csc
	}
	return 0;
}

u32 de_backend_check_crc_status_with_clear(struct de_backend_handle *hdl, u32 mask)
{
	if (hdl->private->crc)
		return de_crc_check_status_with_clear(hdl->private->crc, mask);
	return 0;
}

int de_backend_dump_state(struct drm_printer *p, struct de_backend_handle *hdl)
{
	if (hdl->private->crc)
		de_crc_dump_state(p, hdl->private->crc);
	if (hdl->private->gamma)
		de_gamma_dump_state(p, hdl->private->gamma);
	if (hdl->private->dither)
		de_dither_dump_state(p, hdl->private->dither);

	return 0;
}

struct de_backend_handle *de_backend_create(struct module_create_info *cinfo)
{
	int i;
	struct de_backend_handle *hdl;
	struct module_create_info info;
	struct csc_extra_create_info csc_info;
	unsigned int block_num = 0;
	int cur_block = 0;

	memcpy(&info, cinfo, sizeof(*cinfo));
	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	memcpy(&hdl->cinfo, cinfo, sizeof(*cinfo));

	hdl->private->crc = de_crc_create(&info);
	hdl->private->gamma = de_gamma_create(&info);
	hdl->private->dither = de_dither_create(&info);
	hdl->private->smbl = de_smbl_create(&info);

	info.extra = &csc_info;
	csc_info.type = DEVICE_CSC;
	hdl->private->csc = de_csc_create(&info);

	if (hdl->private->gamma) {
		hdl->feat.support_gamma = true;
		hdl->feat.gamma_lut_len = hdl->private->gamma->gamma_lut_len;
	}

	if (hdl->private->crc)
		hdl->feat.support_crc = true;

	/* block info */
	if (hdl->private->crc)
		block_num += hdl->private->crc->block_num;

	if (hdl->private->gamma)
		block_num += hdl->private->gamma->block_num;

	if (hdl->private->dither)
		block_num += hdl->private->dither->block_num;

	if (hdl->private->smbl)
		block_num += hdl->private->smbl->block_num;

	if (hdl->private->csc)
		block_num += hdl->private->csc->block_num;

	hdl->block_num = block_num;

	hdl->block = kmalloc(sizeof(*hdl->block) * block_num, GFP_KERNEL | __GFP_ZERO);

	if (hdl->private->crc) {
		for (i = 0; i < hdl->private->crc->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->crc->block[i];
		}
	}

	if (hdl->private->gamma) {
		for (i = 0; i < hdl->private->gamma->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->gamma->block[i];
		}
	}

	if (hdl->private->dither) {
		for (i = 0; i < hdl->private->dither->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->dither->block[i];
		}
	}

	if (hdl->private->smbl) {
		for (i = 0; i < hdl->private->smbl->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->smbl->block[i];
		}
	}

	if (hdl->private->csc) {
		for (i = 0; i < hdl->private->csc->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->csc->block[i];
		}
	}

	return hdl;
}
