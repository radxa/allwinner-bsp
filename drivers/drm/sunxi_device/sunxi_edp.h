/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * edp_core.h
 *
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef __DRM_EDP_CORE_H__
#define __DRM_EDP_CORE_H__

#include <sunxi-log.h>
#include <linux/dev_printk.h>
#include <linux/types.h>
#include <video/sunxi_display2.h>
#include <drm/drm_edid.h>
#include <drm/drm_displayid.h>

extern u32 loglevel_debug;

enum edp_hpd_status {
	EDP_HPD_PLUGOUT = 0,
	EDP_HPD_PLUGIN = 1,
};


enum edp_ssc_mode_e {
	SSC_CENTER_MODE = 0,
	SSC_DOWNSPR_MODE,
};

enum edp_pattern_e {
	PATTERN_NONE = 0,
	TPS1,
	TPS2,
	TPS3,
	TPS4,
	PRBS7,
};

enum edp_video_mapping_e {
	RGB_6BIT = 0,
	RGB_8BIT,
	RGB_10BIT,
	RGB_12BIT,
	RGB_16BIT,
	YCBCR444_8BIT,
	YCBCR444_10BIT,
	YCBCR444_12BIT,
	YCBCR444_16BIT,
	YCBCR422_8BIT,
	YCBCR422_10BIT,
	YCBCR422_12BIT,
	YCBCR422_16BIT,
};

#define RET_OK (0)
#define RET_FAIL (-1)
#define RET_AUX_DEFER (-2)
#define RET_AUX_NACK  (-3)
#define RET_AUX_NO_STOP (-4)
#define RET_AUX_TIMEOUT (-5)

#define TRY_CNT_MAX					5
#define TRY_CNT_TIMEOUT				20
#define MAX_VLEVEL					3
#define MAX_PLEVEL					3
#define LINK_STATUS_SIZE			6
#define TRAINING_PATTERN_DISABLE	0
#define TRAINING_PATTERN_1			1
#define TRAINING_PATTERN_2			2
#define TRAINING_PATTERN_3			3
#define TRAINING_PATTERN_4			4

#define SW_VOLTAGE_CHANGE_FLAG		(1 << 0)
#define PRE_EMPHASIS_CHANGE_FLAG	(1 << 1)

#define DPCD_0000H 0x0000
#define DPCD_0001H 0x0001

#define DPCD_0002H 0x0002
#define DPCD_TPS3_SUPPORT_MASK		(1 << 6)

#define DPCD_0003H 0x0003
#define DPCD_FAST_TRAIN_MASK		(1 << 6)

#define DPCD_0004H 0x0004
#define DPCD_0005H 0x0005
#define DPCD_0006H 0x0006
#define DPCD_0100H 0x0100
#define DPCD_0101H 0x0101

#define DPCD_0102H 0x0102
#define DPCD_SCRAMBLING_DISABLE_FLAG	(1 << 5)

#define DPCD_0103H 0x0103
#define DPCD_0104H 0x0104
#define DPCD_0105H 0x0105
#define DPCD_0106H 0x0106
#define DPCD_VLEVEL_SHIFT 0
#define DPCD_PLEVEL_SHIFT 3
#define DPCD_MAX_VLEVEL_REACHED_FLAG	(1 << 2)
#define DPCD_MAX_PLEVEL_REACHED_FLAG	(1 << 5)

#define DPCD_0107H 0x0107
#define DPCD_0108H 0x0108

#define DPCD_010AH 0x010A
#define DPCD_ASSR_ENABLE_MASK       (1 << 0)

#define DPCD_0200H 0x0200
#define DPCD_0201H 0x0201
#define DPCD_0202H 0x0202
#define DPCD_0203H 0x0203
#define LINK_STATUS_BASE DPCD_0202H
#define DPCD_LANE_STATUS_OFFSET(x)	(x / 2)
#define DPCD_LANE_STATUS_SHIFT(x)	(x & 0b1)
#define DPCD_LANE_STATUS_MASK(x)	(0xf << (4 * DPCD_LANE_STATUS_SHIFT(x)))
#define DPCD_CR_DONE(x)				(0x1 << (4 * DPCD_LANE_STATUS_SHIFT(x)))
#define DPCD_EQ_DONE(x)				(0x2 << (4 * DPCD_LANE_STATUS_SHIFT(x)))
#define DPCD_SYMBOL_LOCK(x)			(0x4 << (4 * DPCD_LANE_STATUS_SHIFT(x)))
#define DPCD_EQ_TRAIN_DONE(x)		(DPCD_EQ_DONE(x) | DPCD_SYMBOL_LOCK(x))

#define DPCD_0204H 0x0204
#define DPCD_ALIGN_DONE				(1 << 0)

#define DPCD_0205H 0x0205

#define DPCD_0206H 0x0206
#define DPCD_0207H 0x0207
#define DPCD_LANE_ADJ_OFFSET(x)		((DPCD_0206H - LINK_STATUS_BASE) + (x / 2))
#define VADJUST_SHIFT(x)			(4 * (x & 0b1))
#define PADJUST_SHIFT(x)			((4 * (x & 0b1)) + 2)
#define VADJUST_MASK(x)				(0x3 << VADJUST_SHIFT(x))
#define PADJUST_MASK(x)				(0x3 << PADJUST_SHIFT(x))

#define DPCD_0600H 0x0600
#define DPCD_LOW_POWER_ENTER		0x2
#define DPCD_LOW_POWER_EXIT			0x1

#define DPCD_2200H 0x2200

#define EDP_DPCD_MAX_LANE_MASK					(0x1f << 0)
#define EDP_DPCD_ENHANCE_FRAME_MASK				(1 << 7)
#define EDP_DPCD_TPS3_MASK						(1 << 6)
#define EDP_DPCD_FAST_TRAIN_MASK				(1 << 6)
#define EDP_DPCD_DOWNSTREAM_PORT_MASK			(1 << 0)
#define EDP_DPCD_DOWNSTREAM_PORT_TYPE_MASK		(3 << 1)
#define EDP_DPCD_DOWNSTREAM_PORT_CNT_MASK		(0xf << 0)
#define EDP_DPCD_LOCAL_EDID_MASK				(1 << 1)
#define EDP_DPCD_ASSR_MASK						(1 << 0)
#define EDP_DPCD_FRAME_CHANGE_MASK				(1 << 1)

#define EDP_DBG(fmt, ...)			sunxi_debug(NULL, "[EDP_DBG]: "fmt, ##__VA_ARGS__)
#define EDP_ERR(fmt, ...)			sunxi_err(NULL, "[EDP_ERR]: "fmt, ##__VA_ARGS__)
#define EDP_WRN(fmt, ...)			sunxi_warn(NULL, "[EDP_WRN]: "fmt, ##__VA_ARGS__)
#define EDP_INFO(fmt, ...)			sunxi_info(NULL, "[EDP_INFO]: "fmt, ##__VA_ARGS__)
#define EDP_DEV_ERR(dev, fmt, ...)	sunxi_err(dev, "[EDP_ERR]: "fmt, ##__VA_ARGS__)

#define EDP_DRV_DBG(fmt, ...) \
			do { \
				if (loglevel_debug & 0x1) \
					sunxi_info(NULL, "[EDP_DRV]: "fmt, ##__VA_ARGS__); \
				else \
					sunxi_debug(NULL, "[EDP_DRV]: "fmt, ##__VA_ARGS__); \
			} while (0)

#define EDP_CORE_DBG(fmt, ...) \
			do { \
				if (loglevel_debug & 0x2) \
					sunxi_info(NULL, "[EDP_CORE]: "fmt, ##__VA_ARGS__); \
				else \
					sunxi_debug(NULL, "[EDP_CORE]: "fmt, ##__VA_ARGS__); \
			} while (0)

#define EDP_LOW_DBG(fmt, ...) \
			do { \
				if (loglevel_debug & 0x4) \
					sunxi_info(NULL, "[EDP_LOW]: "fmt, ##__VA_ARGS__); \
				else \
					sunxi_debug(NULL, "[EDP_LOW]: "fmt, ##__VA_ARGS__); \
			} while (0)

#define EDP_EDID_DBG(fmt, ...) \
			do { \
				if (loglevel_debug & 0x8) \
					sunxi_info(NULL, "[EDP_EDID]: "fmt, ##__VA_ARGS__); \
				else \
					sunxi_debug(NULL, "[EDP_EDID]: "fmt, ##__VA_ARGS__); \
			} while (0)

/*edp bit rate  unit:Hz*/
#define BIT_RATE_1G62 ((unsigned long long)1620 * 1000 * 1000)
#define BIT_RATE_2G7  ((unsigned long long)2700 * 1000 * 1000)
#define BIT_RATE_5G4  ((unsigned long long)5400 * 1000 * 1000)
#define BIT_RATE_8G1  ((unsigned long long)8100 * 1000 * 1000)

enum disp_edp_colordepth {
	EDP_8_BIT = 0,
	EDP_10_BIT = 1,
};

struct edp_lane_para {
	u64 bit_rate;
	u32 lane_cnt;
	u32 lane0_sw;
	u32 lane1_sw;
	u32 lane2_sw;
	u32 lane3_sw;
	u32 lane0_pre;
	u32 lane1_pre;
	u32 lane2_pre;
	u32 lane3_pre;
	u32 colordepth;
	u32 color_fmt;
	u32 bpp;
};

struct edp_tx_cap {
	/* decided by edp phy */
	u64 max_rate;
	u32 max_lane;
	bool tps3_support;
	bool fast_train_support;
	bool audio_support;
	bool ssc_support;
	bool psr_support;
	bool assr_support;
	bool mst_support;
};

struct edp_rx_cap {
	/*parse from dpcd*/
	u32 dpcd_rev;
	u64 max_rate;
	u32 max_lane;
	bool tps3_support;
	bool fast_train_support;
	bool fast_train_debug;
	bool downstream_port_support;
	u32 downstream_port_type;
	u32 downstream_port_cnt;
	bool local_edid_support;
	bool is_edp_device;
	bool assr_support;
	bool enhance_frame_support;

	/*parse from edid*/
	u32 mfg_week;
	u32 mfg_year;
	u32 edid_ver;
	u32 edid_rev;
	u32 input_type;
	u32 bit_depth;
	u32 video_interface;
	u32 width_cm;
	u32 height_cm;

	/*parse from edid_ext*/
	bool Ycc444_support;
	bool Ycc422_support;
	bool Ycc420_support;
	bool audio_support;
};

struct edp_tx_core {
	u32 ssc_en;
	s32 ssc_mode;
	u32 psr_en;
	u32 training_param_type;
	u32 interval_CR;
	u32 interval_EQ;
	struct edp_lane_para lane_para;
	struct edp_lane_para debug_lane_para;
	struct edp_lane_para backup_lane_para;

	struct disp_video_timings timings;

	struct edid *edid;

};

struct edid *edp_edid_get(void);
int edp_get_edid_block(void *data, u8 *edid,
			  unsigned int block, size_t len);
s32 edp_edid_put(struct edid *edid);
s32 edp_edid_cea_db_offsets(const u8 *cea, s32 *start, s32 *end);
u8 *sunxi_drm_find_edid_extension(const struct edid *edid,
				   int ext_id, int *ext_index);


u64 edp_source_get_max_rate(void);
u32 edp_source_get_max_lane(void);

bool edp_source_support_tps3(void);
bool edp_source_support_fast_train(void);
bool edp_source_support_audio(void);
bool edp_source_support_ssc(void);
bool edp_source_support_psr(void);
bool edp_source_support_assr(void);
bool edp_source_support_mst(void);

bool edp_core_ssc_is_enabled(void);
bool edp_core_psr_is_enabled(void);
bool edp_core_audio_is_enabled(void);

void edp_core_set_reg_base(uintptr_t base);
void edp_core_show_builtin_patten(u32 pattern);
void edp_core_irq_handler(struct edp_tx_core *edp_core);

s32 edp_core_get_hotplug_state(void);
s32 edp_list_standard_mode_num(void);
s32 edp_core_link_start(void);
s32 edp_core_link_stop(void);
s32 edp_low_power_en(bool en);
s32 edp_core_init_early(void);
s32 edp_core_phy_init(struct edp_tx_core *edp_core);
s32 edp_core_enable(struct edp_tx_core *edp_core);
s32 edp_core_disable(struct edp_tx_core *edp_core);
s32 edp_core_irq_enable(u32 irq_id, bool en);
s32 edp_core_irq_query(void);
s32 edp_core_irq_clear(void);
s32 edp_core_get_cur_line(void);
s32 edp_core_get_start_dly(void);
s32 edp_core_aux_read(s32 addr, s32 len, char *buf);
s32 edp_core_aux_write(s32 addr, s32 len, char *buf);
s32 edp_core_aux_i2c_read(s32 i2c_addr, s32 len, char *buf);
s32 edp_core_aux_i2c_write(s32 i2c_addr, s32 len, char *buf);
s32 edp_core_ssc_enable(bool enable);
s32 edp_core_ssc_set_mode(u32 mode);
s32 edp_core_ssc_get_mode(void);
s32 edp_core_psr_enable(bool enable);
s32 edp_core_assr_enable(bool enable);
s32 edp_core_get_color_fmt(void);
s32 edp_core_set_pattern(u32 pattern);
s32 edp_core_get_pixclk(void);
s32 edp_core_get_train_pattern(void);
s32 edp_core_get_lane_para(struct edp_lane_para *tmp_lane_para);
s32 edp_core_get_tu_size(void);
s32 edp_core_get_valid_symbol_per_tu(void);
s32 edp_core_get_audio_if(void);
s32 edp_core_audio_is_mute(void);
s32 edp_core_get_audio_chn_cnt(void);
s32 edp_core_get_audio_date_width(void);
s32 edp_core_set_video_format(struct edp_tx_core *edp_core);
s32 edp_core_set_video_timings(struct disp_video_timings *tmgs);
s32 edp_core_set_transfer_config(struct edp_tx_core *edp_core);
s32 edp_core_query_lane_capability(struct edp_tx_core *edp_core,
								   struct disp_video_timings *tmgs);

s32 edp_main_link_setup(struct edp_tx_core *edp_core);

#endif /*End of file*/
