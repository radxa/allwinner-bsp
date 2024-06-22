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
 */

#define DEMO_SPEED 200
#define MIN_OVL_WIDTH  32
#define MIN_OVL_HEIGHT 32
#define MIN_BLD_WIDTH 32
#define MIN_BLD_HEIGHT 4

struct de_frontend_inner_info {
	u32 layer_en_cnt;
	u32 ovl_width;
	u32 ovl_height;
	u32 bld_width;
	u32 bld_height;
	u32 fb_width;
	u32 fb_height;
	u32 crop_x;
	u32 crop_y;
	unsigned int size_width;
	unsigned int size_height;

	u32 format;
	bool demo_window_en;

	u32 bypass;
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

	/* front process info from kms procedure */
	struct de_frontend_inner_info *inner_info;

	struct work_struct frontend_work;
	bool has_frontend_work;
};


ssize_t de_frontend_data_size(void)
{
	return sizeof(struct de_frontend_data);
}

/* scaler need snr_en para, we have to deliver it by this way, ensure
 * frontend_data is only used in this file */
int de_frontend_parse_snr_en(struct display_channel_state *cstate)
{
	struct de_frontend_data *frontend_data;

	if (cstate->frontend_blob) {
		frontend_data = (struct de_frontend_data *)cstate->frontend_blob->data;
		return frontend_data->snr_para.en;
	} else
		return 0;
}

void de_frontend_process_late(struct de_frontend_handle *hdl)
{
	/* frontend work may spend much time on lut/pdfs's calculation,
	 * so use system_long_wq to deal with it
	 */
	if (hdl->private->has_frontend_work)
		queue_work(system_long_wq, &hdl->private->frontend_work);
}

s32 de_frontend_apply_csc(struct de_frontend_handle *hdl,
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

	if (hdl->private->fcm)
		de_fcm_set_csc(hdl->private->fcm, &icsc_info, &ocsc_info);

	//hyx
	//add deband csc
	//de_deband_set_outinfo may put after channel_apply or in rtmx_start

	ocsc_info.px_fmt_space = frontend_cfg->de_out_cfg.px_fmt_space;
	if (frontend_cfg->rgb_out)
		ocsc_info.px_fmt_space = DE_FORMAT_SPACE_RGB;
	ocsc_info.color_range = frontend_cfg->de_out_cfg.color_range;
	ocsc_info.color_space = frontend_cfg->de_out_cfg.color_space;
	ocsc_info.eotf = frontend_cfg->de_out_cfg.eotf;
	if (hdl->private->csc1)
		de_csc_apply(hdl->private->csc1, &icsc_info, &ocsc_info, 1);

//TODO
//	if (hdl->private->csc2)
//		de_csc_apply(hdl->private->csc2, &icsc_info, &ocsc_info, 1);

	return 0;
}

s32 de_frontend_apply_dci(struct de_frontend_handle *hdl, enum de_color_range cr)
{
	if (hdl->private->dci)
		de_dci_set_color_range(hdl->private->dci, cr);

	return 0;
}

s32 de_frontend_apply_snr(struct de_frontend_handle *hdl, struct display_channel_state *state)
{
	struct de_frontend_data *frontend_data;

	if (hdl->private->snr) {
		if (state->frontend_blob) {
			frontend_data = (struct de_frontend_data *)state->frontend_blob->data;
			de_snr_set_para(hdl->private->snr, state, &frontend_data->snr_para);
		} else {
			de_snr_enable(hdl->private->snr, 0);
		}
	}

	return 0;
}

s32 de_frontend_enable(struct de_frontend_handle *hdl, u32 en)
{

	if (hdl->private->cdc)
		de_cdc_enable(hdl->private->cdc, en);

	if (hdl->private->csc1)
		de_csc_enable(hdl->private->csc1, en);
//TODO
//	if (hdl->private->csc2)
//		de_csc_enable(hdl->private->csc2, en);

	if (hdl->private->fcm)
		de_fcm_enable(hdl->private->fcm, en);

	if (hdl->private->dci)
		de_dci_enable(hdl->private->dci, en);

	if (hdl->private->snr)
		de_snr_enable(hdl->private->snr, en);

	if (hdl->private->sharp)
		de_sharp_enable(hdl->private->sharp, en);

	return 0;
}

static s32 de_frontend_bypass(struct de_frontend_handle *hdl, u32 bypass)
{
	if (hdl->private->cdc && (bypass & BYPASS_CDC_MASK))
		de_cdc_enable(hdl->private->cdc, 0);

	if (hdl->private->csc1 && (bypass & BYPASS_CCSC_MASK))
		de_csc_enable(hdl->private->csc1, 0);

//TODO
//	if (hdl->private->csc2 && (bypass & BYPASS_CCSC_MASK))
//		de_csc_enable(hdl->private->csc2, 0);

	if (hdl->private->fcm && (bypass & BYPASS_FCM_MASK))
		de_fcm_enable(hdl->private->fcm, 0);

	if (hdl->private->dci && (bypass & BYPASS_DCI_MASK))
		de_dci_enable(hdl->private->dci, 0);

	if (hdl->private->snr && (bypass & BYPASS_SNR_MASK))
		de_snr_enable(hdl->private->snr, 0);

	if (hdl->private->sharp && (bypass & BYPASS_SHARP_MASK))
		de_sharp_enable(hdl->private->sharp, 0);

	return 0;
}

static s32 de_frontend_set_size(struct de_frontend_handle *hdl, struct de_frontend_inner_info *info)
{
	static u32 percent;
	static u32 opposite;
	enum de_format_space chn_fmt;
	u32 tmp_x, tmp_y, tmp2_x, tmp2_y, tmp3_x, tmp3_y;
	u32 tmp_w, tmp_h, tmp2_w, tmp2_h, tmp3_w, tmp3_h;
	u32 size_width, size_height;
	u32 size2_width, size2_height;
	u32 size3_width, size3_height;
	u32 demo_enable;

	demo_enable = info->demo_window_en;
	size_width = info->bld_width;
	size_height = info->bld_height;
	size2_width = info->ovl_width;
	size2_height = info->ovl_height;
	size3_width = info->size_width;
	size3_height = info->size_height;
	chn_fmt = info->chn_fmt;

	tmp_x = 0;
	tmp_y = 0;
	tmp2_x = 0;
	tmp2_y = 0;
	tmp3_x = 0;
	tmp3_y = 0;

	DRM_DEBUG_DRIVER("demo_enable=%d, size=%d,%d, size2=%d,%d\n", demo_enable,
		   size_width, size_height, size2_width, size2_height);
	/* demo mode: rgb half, yuv scroll */
	if (demo_enable) {
		if (chn_fmt == DE_FORMAT_SPACE_YUV) {
			if (!opposite)
				percent >= DEMO_SPEED ? opposite = 1 : percent++;
			else
				percent <= 0 ? opposite = 0 : percent--;

			if (size_width > size_height) {
				tmp_w = percent ?  (size_width * percent / DEMO_SPEED) : 1;
				tmp_h = size_height;
				/* ovl window follow bld window */
				tmp2_w = percent ?  (size2_width * percent / DEMO_SPEED) : 1;
				tmp2_h = size2_height;
				/* screen window */
				tmp3_w = percent ?  (size3_width * percent / DEMO_SPEED) : 1;
				tmp3_h = size3_height;
			} else {
				tmp_w = size_width;
				tmp_h = percent ?  (size_height * percent / DEMO_SPEED) : 1;
				/* ovl window follow bld window */
				tmp2_w = size2_width;
				tmp2_h = percent ?  (size2_height * percent / DEMO_SPEED) : 1;
				/* screen window */
				tmp3_w = size3_width;
				tmp3_h = percent ?  (size3_height * percent / DEMO_SPEED) : 1;
			}
		} else {
			if (size_width > size_height) {
				tmp_w = size_width >> 1;
				tmp_h = size_height;
				/* ovl window follow bld window */
				tmp2_w = size2_width >> 1;
				tmp2_h = size2_height;
				/* screen window */
				tmp3_w = size3_width >> 1;
				tmp3_h = size3_height;
			} else {
				tmp_w = size_width;
				tmp_h = size_height >> 1;
				/* ovl window follow bld window */
				tmp2_w = size2_width;
				tmp2_h = size2_height >> 1;
				/* screen window */
				tmp3_w = size3_width;
				tmp3_h = size3_height >> 1;
			}
		}
	} else {
		tmp_w = size_width;
		tmp_h = size_height;
		tmp2_w = size2_width;
		tmp2_h = size2_height;
		tmp3_w = size3_width;
		tmp3_h = size3_height;
	}


	if (hdl->private->dci && !(info->bypass & (BYPASS_DCI_MASK | BYPASS_ALL_MASK))) {
		if (info->format >= DE_FORMAT_YUV444_I_AYUV)
			de_dci_enable(hdl->private->dci, 1);
		else
			de_dci_enable(hdl->private->dci, 0);
		de_dci_set_size(hdl->private->dci, size_width, size_height);
		de_dci_set_window(hdl->private->dci, demo_enable, tmp_x, tmp_y, tmp_w, tmp_h);
	}

	if (hdl->private->fcm && !(info->bypass & (BYPASS_FCM_MASK | BYPASS_ALL_MASK))) {
		// fcm is disable defult
		//de_fcm_enable(hdl->private->fcm, 1);
		de_fcm_set_size(hdl->private->fcm, size_width, size_height);
		de_fcm_set_window(hdl->private->fcm, demo_enable, tmp_x, tmp_y, tmp_w, tmp_h);
	}

	if (hdl->private->sharp && !(info->bypass & (BYPASS_SHARP_MASK| BYPASS_ALL_MASK))) {
		if (info->format >= DE_FORMAT_YUV444_I_AYUV)
			de_sharp_enable(hdl->private->sharp, 1);
		else
			de_sharp_enable(hdl->private->sharp, 0);
		de_sharp_set_size(hdl->private->sharp, size_width, size_height);
		de_sharp_set_window(hdl->private->sharp, demo_enable, tmp_x, tmp_y, tmp_w, tmp_h);
	}

	return 0;

	/* move to backend */
//	/* asu peaking */
//	if (g_cfg->peaking_exist[disp][chn]) {
//		de_vsu_set_peaking_window(disp, chn, demo_enable, tmp_win);
//	}
//
//
//	/* process dep */
//	if (chn == 0) {
//		/* gamma */
//		if (de_feat_is_support_gamma(disp)) {
//			de_gamma_set_size(disp, size3_width, size3_height);
//			de_gamma_set_window(disp, demo_enable, tmp_win3);
//		}
//		/* dither */
//		if (de_feat_is_support_dither(disp)) {
//			de_dither_set_size(disp, size3_width, size3_height);
//		}
//		/* deband */
//		if (de_feat_is_support_deband(disp)) {
//			de_deband_set_size(disp, size3_width, size3_height);
//			de_deband_set_window(disp, demo_enable, tmp_win3);
//		}
//	}
//
//	return 0;
}


s32 de_frontend_apply_size_and_bypass(struct de_frontend_handle *hdl, struct display_channel_state *cstate,
		 struct de_frontend_apply_cfg *frontend_cfg)
{
	struct de_frontend_data *frontednd_data;
	struct de_frontend_inner_info *new_inner_info = kzalloc(sizeof(*new_inner_info), GFP_KERNEL);
	struct drm_framebuffer *fb = cstate->base.fb;
	struct de_frontend_private *priv = hdl->private;
	u32 dirty = 0;

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

	/*check bypass or not*/
	if (cstate->frontend_blob) {
		frontednd_data = (struct de_frontend_data *)cstate->frontend_blob->data;

		new_inner_info->demo_window_en = frontednd_data->demo_en;

		if (frontednd_data->snr_para.bypass)
			new_inner_info->bypass |= BYPASS_SNR_MASK;
		if (frontednd_data->dci_para.bypass)
			new_inner_info->bypass |= BYPASS_DCI_MASK;
		if (frontednd_data->cdc_para.bypass)
			new_inner_info->bypass |= BYPASS_CDC_MASK;
		if (frontednd_data->csc1_para.bypass)
			new_inner_info->bypass |= BYPASS_CCSC_MASK;
//TODO
//		if (frontednd_data->csc2_para.bypass)
//			new_inner_info->bypass |= BYPASS_CCSC_MASK;
		if (frontednd_data->fcm_para.bypass)
			new_inner_info->bypass |= BYPASS_FCM_MASK;
		if (frontednd_data->sharp_para.bypass)
			new_inner_info->bypass |= BYPASS_SHARP_MASK;
	}

	/* if exceed hardware limit, bypass all frontend module */
	if ((new_inner_info->ovl_width < MIN_OVL_WIDTH) ||
	    (new_inner_info->ovl_height < MIN_OVL_HEIGHT) ||
	    (new_inner_info->bld_width < MIN_BLD_WIDTH) ||
	    (new_inner_info->bld_height < MIN_BLD_HEIGHT)) {
		new_inner_info->bypass |= BYPASS_ALL_MASK;
	}

	//check dirty or not, judge if need update
	if (new_inner_info->layer_en_cnt != priv->inner_info->layer_en_cnt)
		dirty++;

	if ((new_inner_info->ovl_width != priv->inner_info->ovl_width) ||
	    (new_inner_info->ovl_height != priv->inner_info->ovl_height) ||
	    (new_inner_info->bld_width != priv->inner_info->bld_width) ||
	    (new_inner_info->bld_height != priv->inner_info->bld_height) ||
	    (new_inner_info->crop_x != priv->inner_info->crop_x) ||
	    (new_inner_info->crop_y != priv->inner_info->crop_y) ||
	    (new_inner_info->fb_width != priv->inner_info->fb_width) ||
	    (new_inner_info->fb_height != priv->inner_info->fb_height))
		dirty++;

	if (new_inner_info->demo_window_en != priv->inner_info->demo_window_en)
		dirty++;

	if (new_inner_info->bypass != priv->inner_info->bypass)
		dirty++;

	if (dirty) {
		de_frontend_set_size(hdl, new_inner_info);
	}

	/*bypass mask: size layer user module-by-module */
	/* layer and size are the hardware limit, frontend should disable
	 when hardware exceed occre.
	 */
	if (new_inner_info->bypass != priv->inner_info->bypass) {
		if (new_inner_info->bypass & BYPASS_ALL_MASK)
			de_frontend_enable(hdl, 0);
		else if (new_inner_info->bypass)
			de_frontend_bypass(hdl, new_inner_info->bypass);
	}

	memcpy(priv->inner_info, new_inner_info, sizeof(*new_inner_info));

	kfree(new_inner_info);
	return 0;
}

s32 de_frontend_apply_lut(struct de_frontend_handle *hdl, struct display_channel_state *cstate)
{
	struct de_frontend_data *frontednd_data;

	/*check bypass or not*/
	if (cstate->frontend_blob) {
		frontednd_data = (struct de_frontend_data *)cstate->frontend_blob->data;

		if (hdl->private->fcm && frontednd_data->fcm_para.lut_need_update)
			de_fcm_apply_lut(hdl->private->fcm, &frontednd_data->fcm_para.fcm_lut_data, 1);
	}

	return 0;
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

	if (hdl->private->csc1)
		de_csc_dump_state(p, hdl->private->csc1);

//TODO
//	if (hdl->private->csc2)
//		de_csc_dump_state(hdl->private->csc2);

	return 0;
}

s32 de_frontend_apply(struct de_frontend_handle *hdl, struct display_channel_state *cstate,
		 struct de_frontend_apply_cfg *frontend_cfg)
{

	/* fake code for blob test */
	/*
	struct de_frontend_data *frontend_data;

	if (cstate->frontend_blob) {
		frontend_data = (struct de_frontend_data *)cstate->frontend_blob->data;
		frontend_data->fcm_para = xxx;
		frontend_data->cdc_para = xxx;
		...
	*/

	if (hdl->private->snr)
		de_frontend_apply_snr(hdl, cstate);

	if (hdl->private->dci)
		de_frontend_apply_dci(hdl, frontend_cfg->color_range);

	de_frontend_apply_csc(hdl, frontend_cfg);

	de_frontend_apply_size_and_bypass(hdl, cstate, frontend_cfg);

	de_frontend_apply_lut(hdl, cstate);

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

}

static void de_frontend_calculate_update_local_param_work(struct work_struct *work)
{
	struct de_frontend_private *priv = container_of(work, struct de_frontend_private, frontend_work);

	if (priv->dci)
		de_dci_update_local_param(priv->dci);

	if (priv->cdc)
		de_cdc_update_local_param(priv->cdc);
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
	memcpy(&hdl->cinfo, cinfo, sizeof(*cinfo));

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

	INIT_WORK(&hdl->private->frontend_work, de_frontend_calculate_update_local_param_work);
	hdl->private->has_frontend_work = true;

	return hdl;
}
