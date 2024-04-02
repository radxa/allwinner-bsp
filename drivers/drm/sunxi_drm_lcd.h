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

#ifndef _SUNXI_DRM_LCD_H_
#define _SUNXI_DRM_LCD_H_

#include <drm/drm_device.h>

int sunxi_lcd_create(struct tcon_device *tcon);
int sunxi_lcd_destroy(struct tcon_device *tcon);

#endif
