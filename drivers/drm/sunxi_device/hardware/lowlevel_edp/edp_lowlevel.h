/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * function define of edp lowlevel function
 *
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EDP_LOWLEVEL_H__
#define __EDP_LOWLEVEL_H__

#include <../../sunxi_edp.h>
#include <linux/types.h>
#include <linux/printk.h>

#define SETMASK(width, shift)   ((width?((-1U) >> (32-width)):0)  << (shift))
#define CLRMASK(width, shift)   (~(SETMASK(width, shift)))
#define GET_BITS(shift, width, reg)     \
	(((reg) & SETMASK(width, shift)) >> (shift))
#define SET_BITS(shift, width, reg, val) \
	(((reg) & CLRMASK(width, shift)) | (val << (shift)))

#define NATIVE_READ   0b1001
#define NATIVE_WRITE  0b1000
#define AUX_I2C_READ  0b0001
#define AUX_I2C_WRITE 0b0000

#define AUX_REPLY_DEFER 0b0010
#define AUX_REPLY_NACK  0b0001
#define AUX_REPLY_ACK   0b0000
#define AUX_REPLY_I2C_DEFER 0b1000
#define AUX_REPLY_I2C_NACK  0b0100
#define AUX_REPLY_I2C_ACK   0b0000

#define EDID_ADDR DDC_ADDR

#if IS_ENABLED(CONFIG_AW_DRM_EDP_PHY_USED)
u64 edp_hal_get_max_rate(void);
u32 edp_hal_get_max_lane(void);

bool edp_hal_support_tps3(void);
bool edp_hal_support_audio(void);
bool edp_hal_support_psr(void);
bool edp_hal_support_assr(void);
bool edp_hal_support_ssc(void);
bool edp_hal_support_mst(void);

bool edp_hal_get_hotplug_change(void);
bool edp_hal_ssc_is_enabled(void);
bool edp_hal_psr_is_enabled(void);
bool edp_hal_audio_is_enabled(void);
bool edp_hal_support_fast_train(void);

void edp_hal_set_reg_base(uintptr_t base);
void edp_hal_lane_config(struct edp_tx_core *edp_core);
void edp_hal_set_misc(s32 misc0_val, s32 misc1_val);
void edp_hal_clean_hpd_interrupt_status(void);
void edp_hal_show_builtin_patten(u32 pattern);
void edp_hal_irq_handler(struct edp_tx_core *edp_core);
void edp_hal_set_training_pattern(u32 pattern);
void edp_hal_set_lane_sw_pre(u32 lane_id, u32 sw, u32 pre, u32 param_type);
void edp_hal_set_lane_rate(u64 bit_rate);
void edp_hal_set_lane_cnt(u32 lane_cnt);

s32 edp_hal_query_transfer_unit(struct edp_tx_core *edp_core,
								struct disp_video_timings *tmgs);
s32 edp_hal_init_early(void);
s32 edp_hal_phy_init(struct edp_tx_core *edp_core);
s32 edp_hal_enable(struct edp_tx_core *edp_core);
s32 edp_hal_disable(struct edp_tx_core *edp_core);
s32 edp_hal_read_edid_block(u8 *raw_edid, u32 block, size_t len);
s32 edp_hal_sink_init(u64 bit_rate, u32 lane_cnt);
s32 edp_hal_irq_enable(u32 irq_id);
s32 edp_hal_irq_disable(u32 irq_id);
s32 edp_hal_irq_query(void);
s32 edp_hal_irq_clear(void);
s32 edp_hal_get_cur_line(void);
s32 edp_hal_get_start_dly(void);
s32 edp_hal_aux_read(s32 addr, s32 len, char *buf, bool retry);
s32 edp_hal_aux_write(s32 addr, s32 len, char *buf, bool retry);
s32 edp_hal_aux_i2c_read(s32 addr, s32 len, char *buf, bool retry);
s32 edp_hal_aux_i2c_write(s32 addr, s32 len, char *buf, bool retry);
s32 edp_hal_link_start(void);
s32 edp_hal_link_stop(void);
//s32 edp_hal_audio_set_para(edp_audio_t *para);
s32 edp_hal_audio_enable(void);
s32 edp_hal_audio_disable(void);
s32 edp_hal_ssc_enable(bool enable);
s32 edp_hal_ssc_set_mode(u32 mode);
s32 edp_hal_ssc_get_mode(void);
s32 edp_hal_psr_enable(bool enable);
s32 edp_hal_assr_enable(bool enable);
s32 edp_hal_get_color_fmt(void);
s32 edp_hal_set_pattern(u32 pattern);
s32 edp_hal_get_pixclk(void);
s32 edp_hal_get_train_pattern(void);
s32 edp_hal_get_lane_para(struct edp_lane_para *tmp_lane_para);
s32 edp_hal_get_tu_size(void);
s32 edp_hal_get_valid_symbol_per_tu(void);
s32 edp_hal_get_audio_if(void);
s32 edp_hal_audio_is_mute(void);
s32 edp_hal_get_audio_chn_cnt(void);
s32 edp_hal_get_audio_date_width(void);
s32 edp_hal_set_video_format(struct edp_tx_core *edp_core);
s32 edp_hal_set_video_timings(struct disp_video_timings *tmgs);
s32 edp_hal_set_transfer_config(struct edp_tx_core *edp_core);
s32 edp_hal_get_hotplug_state(void);
#else
static inline s32 edp_hal_init_early(void)
{
	EDP_ERR("aw edp phy is not selected yet!\n");
	return RET_FAIL;
}


static inline s32 edp_hal_query_transfer_unit(struct edp_tx_core *edp_core,
											  struct disp_video_timings *tmgs) { return RET_FAIL; }
static inline u64 edp_hal_get_max_rate(void) { return 0; }
static inline u32 edp_hal_get_max_lane(void) { return 0; }

static inline bool edp_hal_support_tps3(void) { return false; }
static inline bool edp_hal_support_audio(void) { return false; }
static inline bool edp_hal_support_psr(void) { return false; }
static inline bool edp_hal_support_assr(void) { return false; }
static inline bool edp_hal_support_ssc(void) { return false; }
static inline bool edp_hal_support_mst(void) { return false; }


static inline bool edp_hal_get_hotplug_change(void) { return false; }
static inline bool edp_hal_ssc_is_enabled(void) { return false; }
static inline bool edp_hal_psr_is_enabled(void) { return false; }
static inline bool edp_hal_audio_is_enabled(void) { return false; }
static inline bool edp_hal_support_fast_train(void) { return false; }

static inline void edp_hal_set_reg_base(uintptr_t base) {}
static inline void edp_hal_lane_config(struct edp_tx_core *edp_core) {}
static inline void edp_hal_set_misc(s32 misc0_val, s32 misc1_val) {}
static inline void edp_hal_clean_hpd_interrupt_status(void) {}
static inline void edp_hal_show_builtin_patten(u32 pattern) {}
static inline void edp_hal_irq_handler(struct edp_tx_core *edp_core) {}
static inline void edp_hal_set_training_pattern(u32 pattern) {}
static inline void edp_hal_set_lane_sw_pre(u32 lane_id, u32 sw, u32 pre, u32 param_type) {}
static inline void edp_hal_set_lane_rate(u64 bit_rate) {}
static inline void edp_hal_set_lane_cnt(u32 lane_cnt) {}

static inline s32 edp_hal_phy_init(struct edp_tx_core *edp_core) { return RET_FAIL; }
static inline s32 edp_hal_enable(struct edp_tx_core *edp_core) { return RET_FAIL; }
static inline s32 edp_hal_disable(struct edp_tx_core *edp_core) { return RET_FAIL; }
static inline s32 edp_hal_sink_init(u64 bit_rate, u32 lane_cnt) { return RET_FAIL; }
static inline s32 edp_hal_irq_enable(u32 irq_id) { return RET_FAIL; }
static inline s32 edp_hal_irq_disable(u32 irq_id) { return RET_FAIL; }
static inline s32 edp_hal_irq_query(void) { return RET_FAIL; }
static inline s32 edp_hal_irq_clear(void) { return RET_FAIL; }
static inline s32 edp_hal_get_cur_line(void) { return RET_FAIL; }
static inline s32 edp_hal_get_start_dly(void) { return RET_FAIL; }
static inline s32 edp_hal_aux_read(s32 addr, s32 len, char *buf, bool retry) { return RET_FAIL; }
static inline s32 edp_hal_aux_write(s32 addr, s32 len, char *buf, bool retry) { return RET_FAIL; }
static inline s32 edp_hal_aux_i2c_read(s32 addr, s32 len, char *buf, bool retry) { return RET_FAIL; }
static inline s32 edp_hal_aux_i2c_write(s32 addr, s32 len, char *buf, bool retry) { return RET_FAIL; }
static inline s32 edp_hal_link_start(void) { return RET_FAIL; }
static inline s32 edp_hal_link_stop(void) { return RET_FAIL; }
//static inline s32 edp_hal_audio_set_para(edp_audio_t *para) { return RET_FAIL; }
static inline s32 edp_hal_audio_enable(void) { return RET_FAIL; }
static inline s32 edp_hal_audio_disable(void) { return RET_FAIL; }
static inline s32 edp_hal_ssc_enable(bool enable) { return RET_FAIL; }
static inline s32 edp_hal_ssc_set_mode(u32 mode) { return RET_FAIL; }
static inline s32 edp_hal_ssc_get_mode(void) { return RET_FAIL; }
static inline s32 edp_hal_psr_enable(bool enable) { return RET_FAIL; }
static inline s32 edp_hal_assr_enable(bool enable) { return RET_FAIL; }
static inline s32 edp_hal_get_color_fmt(void) { return RET_FAIL; }
static inline s32 edp_hal_set_pattern(u32 pattern) { return RET_FAIL; }
static inline s32 edp_hal_get_pixclk(void) { return RET_FAIL; }
static inline s32 edp_hal_get_train_pattern(void) { return RET_FAIL; }
static inline s32 edp_hal_get_lane_para(struct edp_lane_para *tmp_lane_para) { return RET_FAIL; }
static inline s32 edp_hal_get_tu_size(void) { return RET_FAIL; }
static inline s32 edp_hal_get_valid_symbol_per_tu(void) { return RET_FAIL; }
static inline s32 edp_hal_get_audio_if(void) { return RET_FAIL; }
static inline s32 edp_hal_audio_is_mute(void) { return RET_FAIL; }
static inline s32 edp_hal_get_audio_chn_cnt(void) { return RET_FAIL; }
static inline s32 edp_hal_get_audio_date_width(void) { return RET_FAIL; }
static inline s32 edp_hal_set_video_format(struct edp_tx_core *edp_core) { return RET_FAIL; }
static inline s32 edp_hal_set_video_timings(struct disp_video_timings *tmgs) { return RET_FAIL; }
static inline s32 edp_hal_set_transfer_config(struct edp_tx_core *edp_core) { return RET_FAIL; }
static inline s32 edp_hal_get_hotplug_state(void) { return RET_FAIL; }
#endif

#endif
