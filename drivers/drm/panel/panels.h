/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PANELS_H__
#define __PANELS_H__

#if IS_ENABLED(CONFIG_PANEL_DSI_GENERAL)
int panel_dsi_regulator_enable(struct drm_panel *panel);
#endif

#if IS_ENABLED(CONFIG_PANEL_LVDS_GENERAL)
int panel_lvds_regulator_enable(struct drm_panel *panel);
#endif

#if IS_ENABLED(CONFIG_PANEL_RGB_GENERAL)
int panel_rgb_regulator_enable(struct drm_panel *panel);
#endif

#endif
