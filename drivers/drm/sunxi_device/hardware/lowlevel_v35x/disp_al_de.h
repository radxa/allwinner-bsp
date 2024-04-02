/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2017 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _DISP_AL_DE_H_
#define _DISP_AL_DE_H_

#include "../include.h"
#include "de35x/de_rtmx.h"
#include "de35x/de_wb.h"
#include "de35x/de_dcsc.h"
#include "de35x/de_dcsc.h"
#include "de35x/de_enhance.h"

enum {
	DISP_AL_IRQ_FLAG_FRAME_END  = DE_IRQ_FLAG_FRAME_END,
	DISP_AL_IRQ_FLAG_RCQ_FINISH = DE_IRQ_FLAG_RCQ_FINISH,
	DISP_AL_IRQ_FLAG_RCQ_ACCEPT = DE_IRQ_FLAG_RCQ_ACCEPT,
	DISP_AL_IRQ_FLAG_MASK       = DE_IRQ_FLAG_MASK,
};

enum  {
	DISP_AL_IRQ_STATE_FRAME_END  = DE_IRQ_STATE_FRAME_END,
	DISP_AL_IRQ_STATE_RCQ_FINISH = DE_IRQ_STATE_RCQ_FINISH,
	DISP_AL_IRQ_STATE_RCQ_ACCEPT = DE_IRQ_STATE_RCQ_ACCEPT,
	DISP_AL_IRQ_STATE_MASK = DE_IRQ_STATE_MASK,
};

enum {
	DISP_AL_CAPTURE_IRQ_FLAG_FRAME_END = WB_IRQ_FLAG_INTR,
	DISP_AL_CAPTURE_IRQ_FLAG_RCQ_ACCEPT = DE_WB_IRQ_FLAG_RCQ_ACCEPT,
	DISP_AL_CAPTURE_IRQ_FLAG_RCQ_FINISH = DE_WB_IRQ_FLAG_RCQ_FINISH,
	DISP_AL_CAPTURE_IRQ_FLAG_MASK =
		DISP_AL_CAPTURE_IRQ_FLAG_FRAME_END
		| DISP_AL_CAPTURE_IRQ_FLAG_RCQ_ACCEPT
		| DISP_AL_CAPTURE_IRQ_FLAG_RCQ_FINISH,
};

enum {
	DISP_AL_CAPTURE_IRQ_STATE_FRAME_END = WB_IRQ_STATE_PROC_END,
	DISP_AL_CAPTURE_IRQ_STATE_RCQ_ACCEPT = DE_WB_IRQ_STATE_RCQ_ACCEPT,
	DISP_AL_CAPTURE_IRQ_STATE_RCQ_FINISH = DE_WB_IRQ_STATE_RCQ_FINISH,
	DISP_AL_CAPTURE_IRQ_STATE_MASK =
		DISP_AL_CAPTURE_IRQ_STATE_FRAME_END
		| DISP_AL_CAPTURE_IRQ_STATE_RCQ_ACCEPT
		| DISP_AL_CAPTURE_IRQ_STATE_RCQ_FINISH,
};

#endif /* #ifndef _DISP_AL_DE_H_ */
