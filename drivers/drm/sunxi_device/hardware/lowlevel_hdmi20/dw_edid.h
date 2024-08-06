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

#define EDID_BLOCK_SIZE		    (128)

void dw_edid_sink_reset(void);

int dw_edid_read_extenal_block(int block, u8 *edid_buf);

int dw_edid_parse_info(u8 *data);

int dw_sink_is_hdmi20(void);

int dw_edid_check_only_yuv420(u32 vic);

int dw_edid_check_yuv420_base(u32 vic);

int dw_edid_check_yuv422_base(void);

int dw_edid_check_yuv444_base(void);

int dw_edid_check_rgb_dc(u8 bits);

int dw_edid_check_yuv444_dc(u8 bits);

int dw_edid_check_yuv422_dc(u8 bits);

int dw_edid_check_yuv420_dc(u8 bits);

int dw_edid_check_hdr10(void);

int dw_edid_check_hlg(void);

int dw_edid_check_max_tmds_clk(u32 clk);

bool dw_edid_check_hdmi_vic(u32 code);

bool dw_edid_check_cea_vic(u32 code);

bool dw_edid_check_scdc_support(void);

int dw_edid_exit(void);

int dw_edid_init(void);

ssize_t dw_edid_dump(char *buf);

#endif	/* _DW_EDID_H */
