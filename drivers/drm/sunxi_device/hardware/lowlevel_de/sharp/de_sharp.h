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

#ifndef _DE_SHARP_H_
#define _DE_SHARP_H_

#include <linux/types.h>
#include "de_base.h"
#include "de_channel.h"
#include "de_sharp_platform.h"

struct de_sharp_handle {
	struct module_create_info cinfo;
	unsigned int block_num;
	struct de_reg_block **block;
	struct de_sharp_private *private;
};

struct de_sharp_para {
	bool bypass;
};

struct de_sharp_handle *de_sharp_create(struct module_create_info *info);
s32 de_sharp_set_size(struct de_sharp_handle *hdl, u32 width, u32 height);
s32 de_sharp_set_window(struct de_sharp_handle *hdl,
		      u32 win_enable, u32 x, u32 y, u32 w, u32 h);
s32 de_sharp_enable(struct de_sharp_handle *hdl, u32 en);
void de_sharp_update_regs(struct de_sharp_handle *hdl);
s32 de_sharp_dump_state(struct drm_printer *p, struct de_sharp_handle *hdl);

#endif /* #ifndef _DE_CDC_H_ */
