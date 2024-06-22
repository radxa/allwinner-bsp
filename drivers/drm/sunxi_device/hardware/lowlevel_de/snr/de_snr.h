/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * de_snr.h
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
#ifndef _DE_SNR_H
#define _DE_SNR_H

#include <linux/types.h>
#include "de_base.h"
#include "de_channel.h"
#include "de_snr_platform.h"

struct de_snr_handle {
	struct module_create_info cinfo;
	unsigned int block_num;
	struct de_reg_block **block;
	struct de_snr_private *private;
};

enum snr_buffer_flags {
	DISP_BF_NORMAL     = 0, /* non-stereo */
	DISP_BF_STEREO_TB  = 1 << 0, /* stereo top-bottom */
	DISP_BF_STEREO_FP  = 1 << 1, /* stereo frame packing */
	DISP_BF_STEREO_SSH = 1 << 2, /* stereo side by side half */
	DISP_BF_STEREO_SSF = 1 << 3, /* stereo side by side full */
	DISP_BF_STEREO_LI  = 1 << 4, /* stereo line interlace */
	/*
	 * 2d plus depth to convert into 3d,
	 * left and right image using the same frame buffer
	 */
	DISP_BF_STEREO_2D_DEPTH  = 1 << 5,
};

/* date info deliver from userspace by set_blob_property */
struct de_snr_para {
	bool b_trd_out;
	bool bypass;
	unsigned char en;
	unsigned char demo_en;
	int demo_x;
	int demo_y;
	unsigned int demo_width;
	unsigned int demo_height;
	unsigned char y_strength;
	unsigned char u_strength;
	unsigned char v_strength;
	unsigned char th_ver_line;
	unsigned char th_hor_line;
	enum snr_buffer_flags   flags;
};

s32 de_snr_set_para(struct de_snr_handle *hdl, struct display_channel_state *state, struct de_snr_para *snr_para);
struct de_snr_handle *de_snr_create(struct module_create_info *info);
void de_snr_update_regs(struct de_snr_handle *hdl);
s32 de_snr_enable(struct de_snr_handle *hdl, u32 snr_enable);
s32 de_snr_dump_state(struct de_snr_handle *hdl);

#endif /*End of file*/
