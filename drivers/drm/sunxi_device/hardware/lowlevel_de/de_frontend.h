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
#ifndef _DE_FRONTEND_H_
#define _DE_FRONTEND_H_

#include <drm/drm_plane.h>
#include "de_base.h"
#include "csc/de_csc.h"
#include "cdc/de_cdc.h"
#include "snr/de_snr.h"
#include "sharp/de_sharp.h"
#include "dci/de_dci.h"
#include "fcm/de_fcm.h"

#define BYPASS_CDC_MASK		(1 << 0)
#define BYPASS_CCSC_MASK	(1 << 1)
#define BYPASS_FCM_MASK		(1 << 2)
#define BYPASS_DCI_MASK		(1 << 3)
#define BYPASS_SNR_MASK		(1 << 4)
#define BYPASS_SHARP_MASK	(1 << 5)
#define BYPASS_ALL_MASK		(1 << 28)

struct de_frontend_handle {
	struct module_create_info cinfo;
	unsigned int block_num;
	struct de_reg_block **block;
	struct de_frontend_private *private;
};

struct de_frontend_output_cfg {
	enum de_color_space color_space;
	enum de_eotf eotf;
	enum de_color_range color_range;
	enum de_format_space px_fmt_space;
	unsigned int width;
	unsigned int height;
};

struct de_frontend_apply_cfg {
	//unsigned int disp;
	unsigned int layer_en_cnt;
	enum de_color_range color_range;
	enum de_format_space px_fmt_space;
	enum de_color_space color_space;
	enum de_eotf eotf;
	struct de_rect_s ovl_out_win;
	struct de_rect_s scn_win;
	bool rgb_out;

	struct de_frontend_output_cfg de_out_cfg;
};

struct de_frontend_data {
	u32 demo_en;
	struct de_snr_para snr_para;
	struct de_dci_para dci_para;
	struct de_fcm_para fcm_para;
	struct de_cdc_para cdc_para;
	struct de_csc_para csc1_para;
	struct de_csc_para csc2_para;
	struct de_sharp_para sharp_para;
};


void de_frontend_update_regs(struct de_frontend_handle *hdl);

void de_frontend_init(struct de_frontend_handle *hdl);
s32 de_frontend_enable(struct de_frontend_handle *hdl, u32 en);
s32 de_frontend_apply(struct de_frontend_handle *hdl, struct display_channel_state *cstate,
					 struct de_frontend_apply_cfg *frontend_cfg);
void de_frontend_process_late(struct de_frontend_handle *hdl);
s32 de_frontend_dump_state(struct drm_printer *p, struct de_frontend_handle *hdl);

struct de_frontend_handle *de_frontend_create(struct module_create_info *cinfo);
int de_frontend_parse_snr_en(struct display_channel_state *cstate);

#endif
