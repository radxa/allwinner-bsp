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
#ifndef _DE_BACKEND_H_
#define _DE_BACKEND_H_
#include "de_base.h"
#include "csc/de_csc.h"

struct de_backend_feature {
	bool support_gamma;
	unsigned int gamma_lut_len;
	bool support_crc;
};

struct de_backend_handle {
	struct module_create_info cinfo;
	unsigned int block_num;
	struct de_reg_block **block;
	struct de_backend_feature feat;
	struct de_backend_private *private;
};

struct de_backend_apply_cfg {
	unsigned int w, h;
	struct drm_color_lut *gamma_lut;
	bool gamma_dirty;
	unsigned int brightness, contrast, saturation, hue;
	struct de_csc_info in_csc;
	struct de_csc_info out_csc;
	bool csc_dirty;
};

int de_backend_apply(struct de_backend_handle *hdl, struct de_backend_apply_cfg *cfg);
struct de_backend_handle *de_backend_create(struct module_create_info *cinfo);
u32 de_backend_check_crc_status_with_clear(struct de_backend_handle *hdl, u32 mask);
int de_backend_dump_state(struct drm_printer *p, struct de_backend_handle *hdl);

#endif
