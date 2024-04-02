/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (C) 2023 Allwinnertech Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
*/
#ifndef _SUNXI_DE_H_
#define _SUNXI_DE_H_

#include "include.h"
#include "sunxi_drm_crtc.h"

//not visable for user
struct sunxi_de_out;

int sunxi_de_event_proc(struct sunxi_de_out *hwde);

void sunxi_de_atomic_begin(struct sunxi_de_out *hwde);
void sunxi_de_atomic_flush(struct sunxi_de_out *hwde);
int sunxi_de_enable(struct sunxi_de_out *hwde,
		    const struct disp_manager_info *info, bool sw);
void sunxi_de_disable(struct sunxi_de_out *hwde);
void sunxi_de_get_layer_formats(struct sunxi_de_out *hwde,
				unsigned int channel_id,
				const unsigned int **formats,
				unsigned int *count);
int sunxi_de_get_layer_features(struct sunxi_de_out *hwde, unsigned int channel_id);
int sunxi_de_layer_update(struct sunxi_de_out *hwde,  const struct disp_layer_config_inner *data);

#endif
