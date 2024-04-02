/* SPDX-License-Identifier: GPL-2.0-or-later */
/*******************************************************************************
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 ******************************************************************************/

#ifndef _DW_EDID_H
#define _DW_EDID_H

#include "dw_dev.h"

#define EDID_I2C_ADDR		    0x50
#define EDID_I2C_SEGMENT_ADDR	0x30
#define EDID_BLOCK_SIZE		    (128)

typedef enum {
	EDID_ERROR = 0,
	EDID_IDLE,
	EDID_READING,
	EDID_DONE
} edid_status_t;

typedef struct speaker_alloc_code {
	unsigned char byte;
	unsigned char code;
} speaker_alloc_code_t;

#define EDID_DETAIL_EST_TIMINGS 0xf7
#define EDID_DETAIL_CVT_3BYTE 0xf8
#define EDID_DETAIL_COLOR_MGMT_DATA 0xf9
#define EDID_DETAIL_STD_MODES 0xfa
#define EDID_DETAIL_MONITOR_CPDATA 0xfb
#define EDID_DETAIL_MONITOR_NAME 0xfc
#define EDID_DETAIL_MONITOR_RANGE 0xfd
#define EDID_DETAIL_MONITOR_STRING 0xfe
#define EDID_DETAIL_MONITOR_SERIAL 0xff

void dw_edid_sink_reset(sink_edid_t *edidExt);

int dw_edid_read_extenal_block(int block, u8 *edid_buf);

int dw_edid_parse_info(u8 *data);

bool dw_edid_check_hdmi_vic(u32 code);

bool dw_edid_check_cea_vic(u32 code);

bool dw_edid_check_scdc_support(void);

int dw_edid_exit(void);

int dw_edid_init(void);

#endif	/* _DW_EDID_H */
