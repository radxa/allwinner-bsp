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
#include <video/sunxi_drm.h>
#include "de_backend.h"
#include "crc/de_crc.h"
#include "gamma/de_gamma.h"
#include "dither/de_dither.h"
#include "smbl/de_smbl.h"
#include "deband/de_deband.h"

#define clear_mask(mask, module)		((mask) &= ~(module))
#define set_mask(mask, module)			((mask) |= (module))

struct de_backend_inner_info {
	u32 width;
	u32 height;
	enum de_color_space cs;
	enum de_data_bits bits;
	enum de_format_space fmt;
	bool size_dirty;
	u32 enable;
	u32 module_dirty;
};

struct de_backend_private {
	struct de_crc_handle *crc;
	struct de_gamma_handle *gamma;
	struct de_dither_handle *dither;
	struct de_smbl_handle *smbl;
	struct de_deband_handle *deband;
	struct de_csc_handle *csc;
	u32 default_enable;
	struct de_backend_inner_info info;
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

static int de_backend_check_and_reconfig(struct de_backend_handle *hdl, struct de_backend_apply_cfg *backend_cfg)
{
	struct de_backend_inner_info info;
	struct de_backend_inner_info *new_info = &info;
	struct de_backend_inner_info *old_info = &hdl->private->info;

	memset(new_info, 0, sizeof(*new_info));
	new_info->width = backend_cfg->w;
	new_info->height = backend_cfg->h;
	new_info->cs = backend_cfg->in_csc.color_space;
	new_info->bits = backend_cfg->bits;
	new_info->fmt = backend_cfg->in_csc.px_fmt_space;

	/* use out last config to decide if enable  */
	set_mask(new_info->enable, hdl->private->default_enable);

	if (new_info->width != old_info->width || new_info->height != old_info->height) {
		set_mask(new_info->module_dirty, PQ_DEBAND);
		new_info->size_dirty = true;
	} else {
		new_info->size_dirty = false;
	}
	/* some case can not enable deband */
	if (new_info->fmt != DE_FORMAT_SPACE_YUV && new_info->fmt != DE_FORMAT_SPACE_RGB) {
		clear_mask(new_info->enable, PQ_DEBAND);
	}

	if (new_info->fmt == DE_FORMAT_SPACE_RGB) {
		if (!(new_info->cs == DE_COLOR_SPACE_BT601 ||
			  new_info->cs == DE_COLOR_SPACE_BT2020NC ||
			  new_info->cs == DE_COLOR_SPACE_BT2020C))
			clear_mask(new_info->enable, PQ_DEBAND);
	}

	if (new_info->bits > DE_DATA_10BITS) {
		clear_mask(new_info->enable, PQ_DEBAND);
	}

	if (new_info->cs != old_info->cs || new_info->bits != old_info->bits ||
		  new_info->fmt != old_info->fmt) {
		set_mask(new_info->module_dirty, PQ_DEBAND);
	}

	/* setup module dirty flag base on difference */
	if (new_info->enable != old_info->enable) {
		new_info->module_dirty |= new_info->enable ^ old_info->enable;
		DRM_DEBUG_DRIVER("old enable %x new enable%x now dirty %x %x\n", old_info->enable, new_info->enable, new_info->module_dirty,
				new_info->enable ^ old_info->enable);
	}
	memcpy(old_info, new_info, sizeof(*new_info));
	return 0;
}

static int de_backend_apply_gamma(struct de_backend_handle *hdl, struct de_backend_apply_cfg *cfg)
{
	struct de_gamma_cfg gamma;
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
	return 0;
}

static int de_backend_apply_deband(struct de_backend_handle *hdl, struct de_backend_data *data)
{
	struct de_backend_inner_info *info = &hdl->private->info;
	DRM_DEBUG_DRIVER("%s %lx\n", __func__, (unsigned long)data);
	if (hdl->private->deband && data) {
		if (data->dirty & DEBAND_DIRTY && data->deband_para.dirty & PQD_DIRTY_MASK) {
			DRM_DEBUG_DRIVER("%s dirty%x\n", __func__, data->deband_para.dirty);
			de_deband_pq_proc(hdl->private->deband, &data->deband_para.pqd);
			data->deband_para.dirty &= ~PQD_DIRTY_MASK;
			data->dirty &= ~DEBAND_DIRTY;

			if (data->deband_para.pqd.cmd != PQ_READ) {
				if (de_deband_is_enabled(hdl->private->deband)) {
					DRM_DEBUG_DRIVER("pqd %s set enable\n", __func__);
					set_mask(hdl->private->default_enable, DEBAND_DIRTY);
					set_mask(info->enable, DEBAND_DIRTY);
				} else {
					DRM_DEBUG_DRIVER("pqd %s set disable\n", __func__);
					clear_mask(hdl->private->default_enable, DEBAND_DIRTY);
					clear_mask(info->enable, DEBAND_DIRTY);
				}

			}
		}
		if (info->module_dirty & DEBAND_DIRTY) {
			de_deband_enable(hdl->private->deband, info->enable & DEBAND_DIRTY);
			de_deband_set_size(hdl->private->deband, info->width, info->height);
			de_deband_set_outinfo(hdl->private->deband, info->cs, info->bits, info->fmt);
		}
	}
	return 0;
}

static int de_backend_apply_csc(struct de_backend_handle *hdl, struct de_backend_apply_cfg *cfg)
{
	struct bcsh_info bcsh;
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

static int de_backend_apply_crc(struct de_backend_handle *hdl)
{
	struct de_crc_gbl_cfg gcrc;
	struct de_backend_inner_info *info = &hdl->private->info;
	if (hdl->private->crc && info->size_dirty) {
		gcrc.w = info->width;
		gcrc.h = info->height;
		de_crc_global_config(hdl->private->crc, &gcrc);
	}
	return 0;
}

int de_backend_disable(struct de_backend_handle *hdl)
{
	memset(&hdl->private->info, 0, sizeof(hdl->private->info));
	if (hdl->private->deband)
		de_deband_enable(hdl->private->deband, 0);
	return 0;
}

int de_backend_get_pqd_config(struct de_backend_handle *hdl, struct de_backend_data *data)
{
	if (!data) {
		DRM_ERROR("blob null %s\n", __func__);
		return -EINVAL;
	}

	if (hdl->private->deband && data->dirty & DEBAND_DIRTY) {
		if ((data->deband_para.dirty & ~PQD_DIRTY_MASK) ||
			  (!(data->deband_para.dirty & PQD_DIRTY_MASK)) ||
			  (data->deband_para.pqd.cmd != PQ_READ)) {
			DRM_ERROR("%s deband invalid dirty flag, only support pqd read\n", __func__);
			return -EINVAL;
		}
		de_deband_pq_proc(hdl->private->deband, &data->deband_para.pqd);
		data->deband_para.dirty &= ~PQD_DIRTY_MASK;
		data->dirty &= ~DEBAND_DIRTY;
	}
	return 0;
}

int de_backend_apply(struct de_backend_handle *hdl, struct de_backend_data *data, struct de_backend_apply_cfg *cfg)
{

	DRM_DEBUG_DRIVER("[SUNXI-DE] %s \n", __FUNCTION__);

	de_backend_check_and_reconfig(hdl, cfg);

	de_backend_apply_crc(hdl);

	de_backend_apply_gamma(hdl, cfg);

	de_backend_apply_csc(hdl, cfg);

	de_backend_apply_deband(hdl, data);

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
	if (hdl->private->deband)
		de_deband_dump_state(p, hdl->private->deband);
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
	set_mask(hdl->private->default_enable, PQ_ALL_DIRTY);

	hdl->private->crc = de_crc_create(&info);
	hdl->private->gamma = de_gamma_create(&info);
	hdl->private->dither = de_dither_create(&info);
	hdl->private->smbl = de_smbl_create(&info);
	hdl->private->deband = de_deband_create(&info);

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

	if (hdl->private->deband)
		block_num += hdl->private->deband->block_num;

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

	if (hdl->private->deband) {
		for (i = 0; i < hdl->private->deband->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->deband->block[i];
		}
	}

	if (hdl->private->csc) {
		for (i = 0; i < hdl->private->csc->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->csc->block[i];
		}
	}

	return hdl;
}
