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
#include <drm/drm_blend.h>
#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include "de_channel.h"
#include "de_base.h"
#include "de_frontend.h"
#include "../scaler/de_scaler.h"

/*
 *             +-  ovl0 -+
 *  channel0: -|- afbc0 -|-- SNR0 -- scaler0 -- frontend0
 *             +- tfbc0 -+
 *
 *
 *             +-  ovl1 -+
 *  channel1: -|- afbc1 -|-- SNR1 -- scaler1 -- frontend1
 *             +- tfbc1 -+
 *
 *             +-  ovlN -+
 *  channelN: -|- afbcN -|-- SNRN -- scalerN -- frontendN
 *             +- tfbcN -+
 *
 * +---------------- frontend0 ------------------+
 * | sharp0 -- CDC0 -- DCI0 -- FCM0 -- CSC0 |
 * +----------------------------------------+
 *
 * +------------------------- frontend1 -------------------------+
 * | sharp1(option) -- CDC1 -- DCI1(option) -- FCM1 -- CSC1 |
 * +--------------------------------------------------------+
 *
 * +------------------------- frontendN ------------------------+
 * | sharpN(option) -- CDCN(option) -- DCIN-- FCMN -- CSCN |
 * +-------------------------------------------------------+
 *
 * note:
 * SNR is also considered as part of frontend.
 * asu in scaler is also with pq proc, and also be handled in frontend.
 *
 *
 * how do de_frontend work ?
 * 1. pqd will update struct display_channel_state -> frontend_blob through ioctl without exactly update to hw
 * 2. if channel is update, de_frontend_apply() will be called at every frame update (pqd data may be update to hw here by using dirty flag)
 * 3. de_frontend_process_late() will be called when new frame update finish to do some routine for pq module.
 * 4. pq module itself should guarantee pqd api and module apply api simultaneously work well.
 * 5. to avoid race between pq module registers, most pq module use sw shawdow register protect by mutex, and will be update to rcq buffer at de_xxx_update_regs before flush
 * 6. de_frontend_inner_info.module_dirty should not be set directly, but compare to prvious config to decide if if is actally dirty.
 *
 *
 */

#define MIN_OVL_WIDTH  32
#define MIN_OVL_HEIGHT 32
#define MIN_BLD_WIDTH 32
#define MIN_BLD_HEIGHT 4

#define clear_mask(mask, module)		((mask) &= ~(module))
#define set_mask(mask, module)			((mask) |= (module))

struct de_frontend_inner_info {
	u32 layer_en_cnt;
	u32 ovl_width; /* scaler in size, after ovl down fetch size */
	u32 ovl_height;
	u32 bld_width; /* scaler out size, bld input size */
	u32 bld_height;
	u32 fb_width;
	u32 fb_height;
	u32 crop_x;
	u32 crop_y;
	unsigned int size_width;/* de disp out size */
	unsigned int size_height;

	u32 format;
	bool demo_window_en;

	u32 enable;
	u32 module_dirty;
	enum de_format_space chn_fmt;
};

struct de_frontend_private {
	//struct de_chn_info info;
	struct de_snr_handle *snr;
	struct de_sharp_handle *sharp;
	struct de_cdc_handle *cdc;
	struct de_dci_handle *dci;
	struct de_fcm_handle *fcm;
	struct de_csc_handle *csc1;
	struct de_csc_handle *csc2;
	struct de_scaler_handle *asu;
	u32 default_enable;

	/* front process info from kms procedure */
	struct de_frontend_inner_info *inner_info;
};

static int de_frontend_check_and_reconfig(struct de_frontend_handle *hdl, struct display_channel_state *cstate,
		  struct de_frontend_apply_cfg *frontend_cfg)
{
	struct de_frontend_inner_info *new_inner_info = kzalloc(sizeof(*new_inner_info), GFP_KERNEL);
	struct de_frontend_inner_info *inner_info = hdl->private->inner_info;
	struct drm_framebuffer *fb = cstate->base.fb;

	if (!new_inner_info) {
		DRM_ERROR("%s no mem\n", __func__);
		return -ENOMEM;
	}

	/* generate our new base info */
	/* do not touch new_inner_info module dirty flag start */
	new_inner_info->layer_en_cnt = frontend_cfg->layer_en_cnt;
	new_inner_info->ovl_width = frontend_cfg->ovl_out_win.width;
	new_inner_info->ovl_height = frontend_cfg->ovl_out_win.height;
	new_inner_info->bld_width = frontend_cfg->scn_win.width;
	new_inner_info->bld_height = frontend_cfg->scn_win.height;
	new_inner_info->size_width = frontend_cfg->de_out_cfg.width;
	new_inner_info->size_height = frontend_cfg->de_out_cfg.height;
	new_inner_info->crop_x = cstate->base.crtc_x;
	new_inner_info->crop_y = cstate->base.crtc_y;
	new_inner_info->fb_width = fb->width;
	new_inner_info->fb_height = fb->height;
	new_inner_info->format = drm_to_de_format(fb->format->format);
	new_inner_info->chn_fmt = frontend_cfg->px_fmt_space;

	/* use out last config to decide if enable  */
	set_mask(new_inner_info->enable, hdl->private->default_enable);

	/* mark disable if hw can not support */
	if ((frontend_cfg->ovl_out_win.width < MIN_OVL_WIDTH) ||
	    (frontend_cfg->ovl_out_win.height < MIN_OVL_HEIGHT) ||
	    (frontend_cfg->scn_win.width < MIN_BLD_WIDTH) ||
	    (frontend_cfg->scn_win.height < MIN_BLD_HEIGHT)) {
		clear_mask(new_inner_info->enable, PQ_ALL_DIRTY);
	}

	if (new_inner_info->format < DE_FORMAT_YUV444_I_AYUV) {
		if (hdl->private->dci) {
			clear_mask(new_inner_info->enable, DCI_DIRTY);
		}

		if (hdl->private->sharp) {
			clear_mask(new_inner_info->enable, SHARP_DIRTY);
		}
	}
	/*do not touch new_inner_info module_dirty flag end*/

	/* setup module dirty flag base on difference */
	if (new_inner_info->enable != inner_info->enable) {
		new_inner_info->module_dirty |= new_inner_info->enable ^ inner_info->enable;
		DRM_DEBUG_DRIVER("old enable %x new enable%x now dirty %x %x\n", inner_info->enable, new_inner_info->enable, new_inner_info->module_dirty,
				new_inner_info->enable ^ inner_info->enable);
	}

	/* module which concerned about scaler output size */
	if (new_inner_info->bld_width != inner_info->bld_width ||
		  new_inner_info->bld_height != inner_info->bld_height) {
		set_mask(new_inner_info->module_dirty, DCI_DIRTY);
		set_mask(new_inner_info->module_dirty, FCM_DIRTY);
		set_mask(new_inner_info->module_dirty, SHARP_DIRTY);
	}

	/* module which concerned about ovl output size */
/*	if (new_inner_info->ovl_width != inner_info->ovl_width ||
		  new_inner_info->ovl_height != inner_info->ovl_height) {
	}*/

	DRM_DEBUG_DRIVER("pq dirty %x enable %x\n", new_inner_info->module_dirty, new_inner_info->enable);
	memcpy(inner_info, new_inner_info, sizeof(*new_inner_info));
	kfree(new_inner_info);
	return 0;
}

static s32 de_frontend_apply_csc(struct de_frontend_handle *hdl,
	struct de_frontend_apply_cfg *frontend_cfg)
{
	struct de_csc_info icsc_info;
	struct de_csc_info ocsc_info;

	icsc_info.px_fmt_space = frontend_cfg->px_fmt_space;
	icsc_info.color_space = frontend_cfg->color_space;
	icsc_info.color_range = frontend_cfg->color_range;
	icsc_info.eotf = frontend_cfg->eotf;
	icsc_info.width = frontend_cfg->ovl_out_win.width;
	icsc_info.height = frontend_cfg->ovl_out_win.height;

	if (hdl->private->cdc) {
		ocsc_info.px_fmt_space = frontend_cfg->px_fmt_space;
		ocsc_info.color_range = frontend_cfg->color_range;
		ocsc_info.color_space = frontend_cfg->de_out_cfg.color_space;
		ocsc_info.eotf = frontend_cfg->de_out_cfg.eotf;
		ocsc_info.width = frontend_cfg->ovl_out_win.width;
		ocsc_info.height = frontend_cfg->ovl_out_win.height;
		de_cdc_apply_csc(hdl->private->cdc, &icsc_info, &ocsc_info);
		//update icsc_info by cdc's result
		memcpy((void *)&icsc_info, (void *)&ocsc_info,
			sizeof(icsc_info));
	}

	ocsc_info.px_fmt_space = frontend_cfg->de_out_cfg.px_fmt_space;
	if (frontend_cfg->rgb_out)
		ocsc_info.px_fmt_space = DE_FORMAT_SPACE_RGB;
	ocsc_info.color_range = frontend_cfg->de_out_cfg.color_range;
	ocsc_info.color_space = frontend_cfg->de_out_cfg.color_space;
	ocsc_info.eotf = frontend_cfg->de_out_cfg.eotf;
	if (hdl->private->csc1)
		de_csc_apply(hdl->private->csc1, &icsc_info, &ocsc_info, NULL, 1, 1);

//TODO
//	if (hdl->private->csc2)
//		de_csc_apply(hdl->private->csc2, &icsc_info, &ocsc_info, 1);

	return 0;
}

//TODO not test yet
//A733 support dlc, dci 's new version
static s32 de_frontend_apply_dci(struct de_frontend_handle *hdl, enum de_color_range cr)
{
	if (hdl->private->dci)
		de_dci_set_color_range(hdl->private->dci, cr);
	return 0;
}

static s32 de_frontend_apply_asu(struct de_frontend_handle *hdl, struct display_channel_state *state)
{
	struct de_frontend_data *frontend_data;
	struct de_frontend_inner_info *info = hdl->private->inner_info;
	if (hdl->private->asu) {
		if (state->frontend_blob) {
			frontend_data = (struct de_frontend_data *)state->frontend_blob->data;
			if (frontend_data->dirty & ASU_DIRTY) {
				DRM_DEBUG_DRIVER("%s dirty%x\n", __func__, frontend_data->asu_para.dirty);
				de_scaler_apply_asu_pq_config(hdl->private->asu, &frontend_data->asu_para.pqd);
				frontend_data->dirty &= ~ASU_DIRTY;
				frontend_data->asu_para.dirty &= ~PQD_DIRTY_MASK;

				/* update default enable and enable to avoid reset by de_frontend_apply in next frame */
				if (frontend_data->asu_para.pqd.cmd != PQ_READ) {
					if (de_scaler_pq_is_enabled(hdl->private->asu)) {
						DRM_DEBUG_DRIVER("pqd %s set enable\n", __func__);
						set_mask(hdl->private->default_enable, ASU_DIRTY);
						set_mask(info->enable, ASU_DIRTY);
					} else {
						DRM_DEBUG_DRIVER("pqd %s set disable\n", __func__);
						clear_mask(hdl->private->default_enable, ASU_DIRTY);
						clear_mask(info->enable, ASU_DIRTY);
					}
				}
			}
		}
		if (info->module_dirty & ASU_DIRTY)
			de_scaler_asu_pq_enable(hdl->private->asu, info->enable & ASU_DIRTY);
	}

	return 0;
}

static s32 de_frontend_apply_fcm(struct de_frontend_handle *hdl, struct display_channel_state *cstate, struct de_frontend_apply_cfg *frontend_cfg)
{
	struct de_frontend_data *frontend_data;
	struct de_frontend_inner_info *info = hdl->private->inner_info;
	struct de_csc_info csc_info;
	csc_info.px_fmt_space = frontend_cfg->px_fmt_space;
	csc_info.color_space = frontend_cfg->color_space;
	csc_info.color_range = frontend_cfg->color_range;
	csc_info.eotf = frontend_cfg->eotf;
	csc_info.width = frontend_cfg->ovl_out_win.width;
	csc_info.height = frontend_cfg->ovl_out_win.height;

	if (hdl->private->fcm) {
		if (cstate->frontend_blob) {
			frontend_data = (struct de_frontend_data *)cstate->frontend_blob->data;
			if (frontend_data->dirty & FCM_DIRTY &&
				  frontend_data->fcm_para.dirty & PQD_DIRTY_MASK) {
				DRM_DEBUG_DRIVER("%s dirty%x\n", __func__, frontend_data->fcm_para.dirty);
				de_fcm_lut_proc(hdl->private->fcm, &frontend_data->fcm_para.pqd);
				de_fcm_set_csc(hdl->private->fcm, &csc_info, &csc_info);
				de_fcm_set_size(hdl->private->fcm, info->bld_width, info->bld_height);
				frontend_data->fcm_para.dirty &= ~PQD_DIRTY_MASK;
				frontend_data->dirty &= ~FCM_DIRTY;

				/* update default enable and enable to avoid reset by de_frontend_apply in next frame */
				if (frontend_data->fcm_para.pqd.cmd != PQ_READ) {
					if (de_fcm_is_enabled(hdl->private->fcm)) {
						DRM_DEBUG_DRIVER("pqd %s set enable\n", __func__);
						set_mask(hdl->private->default_enable, FCM_DIRTY);
						set_mask(info->enable, FCM_DIRTY);
					} else {
						DRM_DEBUG_DRIVER("pqd %s set disable\n", __func__);
						clear_mask(hdl->private->default_enable, FCM_DIRTY);
						clear_mask(info->enable, FCM_DIRTY);
					}
				}

			}
		}

		if (info->module_dirty & FCM_DIRTY) {
			/* fcm csc shoulde not modify anything  */
			de_fcm_set_csc(hdl->private->fcm, &csc_info, &csc_info);
			de_fcm_enable(hdl->private->fcm, info->enable & FCM_DIRTY);
			de_fcm_set_size(hdl->private->fcm, info->bld_width, info->bld_height);
		}
	}

	return 0;
}

static s32 de_frontend_apply_sharp(struct de_frontend_handle *hdl, struct display_channel_state *cstate)
{
	struct de_frontend_data *frontend_data;
	struct de_frontend_inner_info *info = hdl->private->inner_info;

	/*check bypass or not*/
	if (cstate->frontend_blob) {
		frontend_data = (struct de_frontend_data *)cstate->frontend_blob->data;
		if (hdl->private->sharp && frontend_data->dirty & SHARP_DIRTY &&
			  frontend_data->sharp_para.dirty & PQD_DIRTY_MASK) {
			DRM_DEBUG_DRIVER("%s dirty%x\n", __func__, frontend_data->sharp_para.dirty);
			de_sharp_pq_proc(hdl->private->sharp, &frontend_data->sharp_para.pqd);
			frontend_data->sharp_para.dirty &= ~PQD_DIRTY_MASK;
			frontend_data->dirty &= ~SHARP_DIRTY;
			/* update default enable and enable to avoid reset by de_frontend_apply in next frame */
			if (frontend_data->sharp_para.pqd.cmd != PQ_READ) {
				if (de_sharp_is_enabled(hdl->private->sharp)) {
					DRM_DEBUG_DRIVER("pqd %s set enable\n", __func__);
					set_mask(hdl->private->default_enable, SHARP_DIRTY);
					set_mask(info->enable, SHARP_DIRTY);
				} else {
					DRM_DEBUG_DRIVER("pqd %s set disable\n", __func__);
					clear_mask(hdl->private->default_enable, SHARP_DIRTY);
					clear_mask(info->enable, SHARP_DIRTY);
				}
			}

		}
	}

	if (hdl->private->sharp && info->module_dirty & SHARP_DIRTY) {
		de_sharp_set_size(hdl->private->sharp, info->bld_width, info->bld_height);
		de_sharp_enable(hdl->private->sharp, info->enable & SHARP_DIRTY);
	}
	return 0;
}

static inline bool de_frontend_is_need_update_work(struct de_frontend_handle *hdl)
{
	return (hdl->private->dci || hdl->private->cdc);
}

void de_frontend_process_late(struct de_frontend_handle *hdl)
{
	/* don't forget de_frontend_is_need_update_work when new work add */
	if (hdl->private->dci)
		de_dci_update_local_param(hdl->private->dci);

	if (hdl->private->cdc)
		de_cdc_update_local_param(hdl->private->cdc);
}

s32 de_frontend_dump_state(struct drm_printer *p, struct de_frontend_handle *hdl)
{
	if (hdl->private->dci)
		de_dci_dump_state(p, hdl->private->dci);

	if (hdl->private->cdc)
		de_cdc_dump_state(p, hdl->private->cdc);

	if (hdl->private->fcm)
		de_fcm_dump_state(p, hdl->private->fcm);

	if (hdl->private->sharp)
		de_sharp_dump_state(p, hdl->private->sharp);

	if (hdl->private->snr)
		de_snr_dump_state(p, hdl->private->snr);

	if (hdl->private->csc1)
		de_csc_dump_state(p, hdl->private->csc1);

//TODO
//	if (hdl->private->csc2)
//		de_csc_dump_state(hdl->private->csc2);

	return 0;
}

bool de_frontend_apply_snr(struct de_frontend_handle *hdl, struct display_channel_state *state, unsigned int ovl_w, unsigned int ovl_h)
{
	bool enable;
	struct de_frontend_data *frontend_data;

	if (!hdl->private->snr)
		return false;

	if (state->frontend_blob) {
		frontend_data = (struct de_frontend_data *)state->frontend_blob->data;
		if (frontend_data->dirty & SNR_DIRTY) {
			de_snr_set_para(hdl->private->snr, state, &frontend_data->snr_para);
			frontend_data->dirty &= ~SNR_DIRTY;
			frontend_data->snr_para.dirty = 0;
			/* update default enable and enable to avoid reset by de_frontend_apply in next frame */
			if (frontend_data->snr_para.pqd.cmd != PQ_READ) {
				if (de_snr_is_enabled(hdl->private->snr)) {
					set_mask(hdl->private->default_enable, PQ_SNR);
				} else {
					clear_mask(hdl->private->default_enable, PQ_SNR);
				}
			}
		}
	}

	enable = hdl->private->default_enable & PQ_SNR;
	if (enable) {
		de_snr_set_size(hdl->private->snr, ovl_w, ovl_h);
		de_snr_enable(hdl->private->snr, 1);
	}
	return enable;
}

s32 de_frontend_disable(struct de_frontend_handle *hdl)
{
	struct de_frontend_inner_info *inner_info = hdl->private->inner_info;

	/* reset for next enable dirty check */
	memset(inner_info, 0, sizeof(*inner_info));

	if (hdl->private->cdc)
		de_cdc_enable(hdl->private->cdc, 0);

	if (hdl->private->csc1)
		de_csc_enable(hdl->private->csc1, 0);
//TODO
//	if (hdl->private->csc2)
//		de_csc_enable(hdl->private->csc2, en);

	if (hdl->private->fcm)
		de_fcm_enable(hdl->private->fcm, 0);

	if (hdl->private->dci)
		de_dci_enable(hdl->private->dci, 0);

	if (hdl->private->snr)
		de_snr_enable(hdl->private->snr, 0);

	if (hdl->private->sharp)
		de_sharp_enable(hdl->private->sharp, 0);

	return 0;
}

int de_frontend_get_pqd_config(struct de_frontend_handle *hdl, struct display_channel_state *cstate)
{
	struct de_frontend_data *frontend_data = NULL;
	if (cstate->frontend_blob) {
		frontend_data = (struct de_frontend_data *)cstate->frontend_blob->data;
	} else {
		DRM_ERROR("blob null %s\n", __func__);
		return -EINVAL;
	}

	/* pqd actually only needs single config as read result, but we have several frontend, which has no diference between them.
	 * it's ok to read multiple times.
	 */
	if (hdl->private->fcm && frontend_data->dirty & FCM_DIRTY) {
		if ((frontend_data->fcm_para.dirty & ~PQD_DIRTY_MASK) ||
			  (!(frontend_data->fcm_para.dirty & PQD_DIRTY_MASK)) ||
			  (frontend_data->fcm_para.pqd.cmd != PQ_READ)) {
			DRM_ERROR("%s fcm invalid dirty flag, only support pqd read\n", __func__);
			return -EINVAL;
		}
		de_fcm_lut_proc(hdl->private->fcm, &frontend_data->fcm_para.pqd);
		frontend_data->fcm_para.dirty &= ~PQD_DIRTY_MASK;
		frontend_data->dirty &= ~FCM_DIRTY;
	}

	if (hdl->private->sharp && frontend_data->dirty & SHARP_DIRTY) {
		if ((frontend_data->sharp_para.dirty & ~PQD_DIRTY_MASK) ||
			  (!(frontend_data->sharp_para.dirty & PQD_DIRTY_MASK)) ||
			  (frontend_data->sharp_para.pqd.cmd != PQ_READ)) {
			DRM_ERROR("%s sharp invalid dirty flag, only support pqd read\n", __func__);
			return -EINVAL;
		}
		de_sharp_pq_proc(hdl->private->sharp, &frontend_data->sharp_para.pqd);
		frontend_data->sharp_para.dirty &= ~PQD_DIRTY_MASK;
		frontend_data->dirty &= ~SHARP_DIRTY;
	}

	if (hdl->private->asu && frontend_data->dirty & ASU_DIRTY) {
		if ((frontend_data->asu_para.dirty & ~PQD_DIRTY_MASK) ||
			  (!(frontend_data->asu_para.dirty & PQD_DIRTY_MASK)) ||
			  (frontend_data->asu_para.pqd.cmd != PQ_READ)) {
			DRM_ERROR("%s asu invalid dirty flag, only support pqd read\n", __func__);
			return -EINVAL;
		}
		de_scaler_apply_asu_pq_config(hdl->private->asu, &frontend_data->asu_para.pqd);
		frontend_data->asu_para.dirty &= ~PQD_DIRTY_MASK;
		frontend_data->dirty &= ~ASU_DIRTY;
	}

	if (hdl->private->snr && frontend_data->dirty & SNR_DIRTY) {
		if ((frontend_data->snr_para.dirty & ~PQD_DIRTY_MASK) ||
			  (!(frontend_data->snr_para.dirty & PQD_DIRTY_MASK)) ||
			  (frontend_data->snr_para.pqd.cmd != PQ_READ)) {
			DRM_ERROR("%s snr invalid dirty flag, only support pqd read\n", __func__);
			return -EINVAL;
		}
		de_snr_set_para(hdl->private->snr, cstate, &frontend_data->snr_para);
		frontend_data->snr_para.dirty &= ~PQD_DIRTY_MASK;
		frontend_data->dirty &= ~SNR_DIRTY;
	}

	return 0;
}

s32 de_frontend_apply(struct de_frontend_handle *hdl, struct display_channel_state *cstate,
		 struct de_frontend_apply_cfg *frontend_cfg)
{
	de_frontend_check_and_reconfig(hdl, cstate, frontend_cfg);

	de_frontend_apply_fcm(hdl, cstate, frontend_cfg);

	de_frontend_apply_sharp(hdl, cstate);

	de_frontend_apply_asu(hdl, cstate);

//	scaler need snr info, de_frontend_apply is called after scaler, so separate snr apply
//	de_frontend_apply_snr(hdl, cstate);

	de_frontend_apply_dci(hdl, frontend_cfg->color_range);

	de_frontend_apply_csc(hdl, frontend_cfg);

	return 0;
}

void de_frontend_update_regs(struct de_frontend_handle *hdl)
{
	if (hdl->private->dci)
		de_dci_update_regs(hdl->private->dci);

	if (hdl->private->cdc)
		de_cdc_update_regs(hdl->private->cdc);

	if (hdl->private->fcm)
		de_fcm_update_regs(hdl->private->fcm);

	if (hdl->private->sharp)
		de_sharp_update_regs(hdl->private->sharp);

	if (hdl->private->snr)
		de_snr_update_regs(hdl->private->snr);
}

struct de_frontend_handle *de_frontend_create(struct module_create_info *cinfo)
{
	int i;
	struct de_frontend_handle *hdl;
	struct module_create_info info;
	struct csc_extra_create_info csc_info;
	unsigned int block_num = 0;
	int cur_block = 0;

	memcpy(&info, cinfo, sizeof(*cinfo));
	hdl = kmalloc(sizeof(*hdl), GFP_KERNEL | __GFP_ZERO);
	hdl->private = kmalloc(sizeof(*hdl->private), GFP_KERNEL | __GFP_ZERO);
	hdl->private->inner_info = kmalloc(sizeof(*hdl->private->inner_info), GFP_KERNEL | __GFP_ZERO);
	set_mask(hdl->private->default_enable, PQ_ALL_DIRTY);
	memcpy(&hdl->cinfo, cinfo, sizeof(*cinfo));

	hdl->private->asu = cinfo->extra;
	info.extra = NULL;
	hdl->private->snr = de_snr_create(&info);
	hdl->private->sharp = de_sharp_create(&info);
	hdl->private->cdc = de_cdc_create(&info);
	hdl->private->dci = de_dci_create(&info);
	hdl->private->fcm = de_fcm_create(&info);

	info.extra = &csc_info;
	csc_info.type = CHANNEL_CSC;
	csc_info.extra_id = 1;
	hdl->private->csc1 = de_csc_create(&info);

	csc_info.extra_id = 2;
	hdl->private->csc2 = de_csc_create(&info);
	info.extra = NULL;

	/* block info */
	if (hdl->private->snr)
		block_num += hdl->private->snr->block_num;

	if (hdl->private->sharp)
		block_num += hdl->private->sharp->block_num;

	if (hdl->private->cdc)
		block_num += hdl->private->cdc->block_num;

	if (hdl->private->dci)
		block_num += hdl->private->dci->block_num;

	if (hdl->private->fcm)
		block_num += hdl->private->fcm->block_num;

	if (hdl->private->csc1)
		block_num += hdl->private->csc1->block_num;

	if (hdl->private->csc2)
		block_num += hdl->private->csc2->block_num;

	hdl->block_num = block_num;

	hdl->block = kmalloc(sizeof(*hdl->block) * block_num, GFP_KERNEL | __GFP_ZERO);

	if (hdl->private->snr) {
		for (i = 0; i < hdl->private->snr->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->snr->block[i];
		}
	}

	if (hdl->private->sharp) {
		for (i = 0; i < hdl->private->sharp->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->sharp->block[i];
		}
	}

	if (hdl->private->cdc) {
		for (i = 0; i < hdl->private->cdc->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->cdc->block[i];
		}
	}

	if (hdl->private->dci) {
		for (i = 0; i < hdl->private->dci->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->dci->block[i];
		}
	}

	if (hdl->private->fcm) {
		for (i = 0; i < hdl->private->fcm->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->fcm->block[i];
		}
	}

	if (hdl->private->csc1) {
		for (i = 0; i < hdl->private->csc1->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->csc1->block[i];
		}
	}

	if (hdl->private->csc2) {
		for (i = 0; i < hdl->private->csc2->block_num; i++, cur_block++) {
			hdl->block[cur_block] = hdl->private->csc2->block[i];
		}
	}

	hdl->routine_job = de_frontend_is_need_update_work(hdl);
	return hdl;
}
