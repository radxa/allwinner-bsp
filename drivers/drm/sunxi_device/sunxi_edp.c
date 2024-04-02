/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * core function of edp driver
 *
 * Copyright (c) 2007-2022 Allwinnertech Co., Ltd.
 * Author: huangyongxing <huangyongxing@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "sunxi_edp.h"
#include "hardware/lowlevel_edp/edp_lowlevel.h"
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>

static const char edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};


/*
 * Search EDID for CEA extension block.
 */
u8 *sunxi_drm_find_edid_extension(const struct edid *edid,
				   int ext_id, int *ext_index)
{
	u8 *edid_ext = NULL;
	int i;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return NULL;

	/* Find CEA extension */
	for (i = *ext_index; i < edid->extensions; i++) {
		edid_ext = (u8 *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == ext_id)
			break;
	}

	if (i >= edid->extensions)
		return NULL;

	*ext_index = i + 1;

	return edid_ext;
}

static s32 edid_block_cnt(struct edid *edid)
{
	return edid->extensions + 1;
}

int edp_get_edid_block(void *data, u8 *block_raw_data,
			  unsigned int block_id, size_t len)
{
	int ret = 0;

	ret = edp_hal_read_edid_block(block_raw_data, block_id, len);
	if (ret < 0) {
		EDP_ERR("EDID: edp read edid block%d failed\n", block_id);
	}

	return ret;
}

s32 edp_edid_put(struct edid *edid)
{
	u32 block_cnt;

	if (edid == NULL) {
		EDP_WRN("EDID: edid is already null\n");
		return 0;
	}

	block_cnt = edid_block_cnt(edid);

	memset(edid, 0, block_cnt * EDID_LENGTH);

	kfree(edid);

	return 0;
}

s32 edp_edid_cea_db_offsets(const u8 *cea, s32 *start, s32 *end)
{
	/* DisplayID CTA extension blocks and top-level CEA EDID
	 * block header definitions differ in the following bytes:
	 *   1) Byte 2 of the header specifies length differently,
	 *   2) Byte 3 is only present in the CEA top level block.
	 *
	 * The different definitions for byte 2 follow.
	 *
	 * DisplayID CTA extension block defines byte 2 as:
	 *   Number of payload bytes
	 *
	 * CEA EDID block defines byte 2 as:
	 *   Byte number (decimal) within this block where the 18-byte
	 *   DTDs begin. If no non-DTD data is present in this extension
	 *   block, the value should be set to 04h (the byte after next).
	 *   If set to 00h, there are no DTDs present in this block and
	 *   no non-DTD data.
	 */
	if (cea[0] == DATA_BLOCK_CTA) {
		*start = 3;
		*end = *start + cea[2];
	} else if (cea[0] == CEA_EXT) {
		/* Data block offset in CEA extension block */
		*start = 4;
		*end = cea[2];
		if (*end == 0)
			*end = 127;
		if (*end < 4 || *end > 127)
			return -ERANGE;
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

/*edp_hal_xxx means xxx function is from lowlevel*/
s32 edp_core_init_early(void)
{
	return edp_hal_init_early();
}

s32 edp_core_phy_init(struct edp_tx_core *edp_core)
{
	return edp_hal_phy_init(edp_core);
}

void edp_core_set_reg_base(uintptr_t base)
{
	edp_hal_set_reg_base(base);
}

/*
 * -1: state_not_change
 *  0: change_to_plugout
 *  1: change_to_plugin
 */
s32 edp_core_get_hotplug_state(void)
{
	if (edp_hal_get_hotplug_change())
		return edp_hal_get_hotplug_state();
	else
		return -1;
}

/* set color space, color depth */
s32 edp_core_set_video_format(struct edp_tx_core *edp_core)
{
	s32 ret = RET_OK;

	ret = edp_hal_set_video_format(edp_core);
	if (ret)
		EDP_ERR("edp_core_set_video_format fail!\n");

	return ret;
}

s32 edp_core_set_video_timings(struct disp_video_timings *tmgs)
{
	s32 ret = RET_OK;

	ret = edp_hal_set_video_timings(tmgs);
	if (ret)
		EDP_ERR("edp_core_set_video_timings fail!\n");

	return ret;
}

s32 edp_core_set_transfer_config(struct edp_tx_core *edp_core)
{
	s32 ret = RET_OK;
	struct edp_lane_para *lane_para = &edp_core->lane_para;

	ret = edp_hal_set_transfer_config(edp_core);
	if (ret) {
		EDP_ERR("edp_core_set_transfer_config fail! Maybe pixelclk out of lane's capability!\n");
		EDP_ERR("Check lane param! Lane param now: lane_cnt:%d lane_rate:%lld bpp:%d pixel_clk:%d\n",
			lane_para->lane_cnt, lane_para->bit_rate, lane_para->bpp, edp_core->timings.pixel_clk);
	}

	return ret;
}

s32 edp_core_enable(struct edp_tx_core *edp_core)
{
	return edp_hal_enable(edp_core);
}

s32 edp_core_disable(struct edp_tx_core *edp_core)
{
	return edp_hal_disable(edp_core);
}

u64 edp_source_get_max_rate(void)
{
	return edp_hal_get_max_rate();
}

u32 edp_source_get_max_lane(void)
{
	return edp_hal_get_max_lane();
}

bool edp_source_support_tps3(void)
{
	return edp_hal_support_tps3();
}

bool edp_source_support_fast_train(void)
{
	return edp_hal_support_fast_train();
}

bool edp_source_support_audio(void)
{
	return edp_hal_support_audio();
}

bool edp_source_support_ssc(void)
{
	return edp_hal_support_ssc();
}

bool edp_source_support_psr(void)
{
	return edp_hal_support_psr();
}

bool edp_source_support_assr(void)
{
	return edp_hal_support_assr();
}

bool edp_source_support_mst(void)
{
	return edp_hal_support_mst();
}


s32 edp_core_query_max_pixclk(struct edp_tx_core *edp_core,
			      struct disp_video_timings *tmgs)
{
	u32 pixclk_max = 0;
	u32 pixclk = tmgs->pixel_clk;
	u32 lane_cnt_max = edp_source_get_max_lane();
	/* lane's bandwidth = lane_rate / 10, such as 2.7G's bandwidth = 270M */
	u64 lane_bandwidth_max = edp_source_get_max_rate() / 10;
	u32 bpp = edp_core->lane_para.bpp;

	/* 1 component's byte = bpp / 8bit */
	/* pixclk * bpp / 8 = lane_bandwidth * lane_cnt */
	pixclk_max = (lane_bandwidth_max / bpp) * lane_cnt_max * 8 ;
	EDP_CORE_DBG("lane_cnt_max:%d lane_bandwidth_max:%lld bpp:%d pixclk_support_max:%d pixclk_cur:%d\n",
		     lane_cnt_max, lane_bandwidth_max, bpp, pixclk_max, pixclk);

	if (pixclk > pixclk_max) {
		EDP_ERR("pixclk setting out of lane's max capability! pixclk_support_max:%d pixclk_cur:%d\n",
			pixclk_max, pixclk);
		return RET_FAIL;
	}

	return RET_OK;
}

s32 edp_core_query_current_pixclk(struct edp_tx_core *edp_core,
				  struct disp_video_timings *tmgs)
{
	u32 pixclk_cur_max = 0;
	u32 pixclk = tmgs->pixel_clk;
	u32 lane_cnt_cur = edp_core->lane_para.lane_cnt;
	/* lane's bandwidth = lane_rate / 10, such as 2.7G's bandwidth = 270M */
	u64 lane_bandwidth_cur = edp_core->lane_para.bit_rate / 10;
	u32 bpp = edp_core->lane_para.bpp;

	if ((lane_cnt_cur == 0) || (lane_bandwidth_cur == 0) || (pixclk == 0)) {
		EDP_ERR("lane param or timings not set, lane_cnt:%d lane_rate:%lld pixclk:%d\n",
			lane_cnt_cur, lane_bandwidth_cur * 10, pixclk);
		EDP_ERR("Maybe lane param not set in dts, or DPCD/EDID read fail via AUX, please check!\n");
		return RET_FAIL;
	}

	/* 1 component's byte = bpp / 8bit */
	/* pixclk * bpp / 8 = lane_bandwidth * lane_cnt */
	pixclk_cur_max = (lane_bandwidth_cur / bpp) * lane_cnt_cur * 8 ;
	EDP_CORE_DBG("lane_cnt_cur:%d lane_bandwidth_cur:%lld bpp:%d pixclk_cur_max:%d pixclk_cur:%d\n",
		     lane_cnt_cur, lane_bandwidth_cur, bpp, pixclk_cur_max, pixclk);

	if (pixclk > pixclk_cur_max) {
		EDP_ERR("pixclk setting out of current lane's capability! pixclk_cur_max:%d pixclk_cur:%d\n",
			pixclk_cur_max, pixclk);
		return RET_FAIL;
	}

	return RET_OK;
}

s32 edp_core_query_transfer_unit(struct edp_tx_core *edp_core,
				 struct disp_video_timings *tmgs)
{
	return edp_hal_query_transfer_unit(edp_core, tmgs);
}


s32 edp_core_query_lane_capability(struct edp_tx_core *edp_core,
				   struct disp_video_timings *tmgs)
{
	s32 ret = 0;

	/* check if pixclk out of lane's max capability */
	ret = edp_core_query_max_pixclk(edp_core, tmgs);
	if (ret)
		return RET_FAIL;

	/* check if pixclk out of current lane config's capability */
	ret = edp_core_query_current_pixclk(edp_core, tmgs);
	if (ret)
		return RET_FAIL;

	/*
	 * some controller's lane has valid link symbol limit per
	 * lane (64 is defined as MAX in DP spec), query if pixclk
	 * out of lane's link symbol limit
	 */
	ret = edp_core_query_transfer_unit(edp_core, tmgs);
	if (ret)
		return RET_FAIL;

	return RET_OK;
}

s32 edp_get_link_status(char *link_status)
{
	return edp_core_aux_read(DPCD_0202H, LINK_STATUS_SIZE, link_status);
}

bool edp_sink_support_fast_train(void)
{

	char tmp_rx_buf[16];

	memset(tmp_rx_buf, 0, sizeof(tmp_rx_buf));

	if (edp_core_aux_read(DPCD_0003H, 1, &tmp_rx_buf[0]) < 0)
		return false;

	if (tmp_rx_buf[0] & DPCD_FAST_TRAIN_MASK)
		return true;
	else
		return false;
}

bool edp_sink_support_tps3(void)
{
	char tmp_rx_buf[16];

	memset(tmp_rx_buf, 0, sizeof(tmp_rx_buf));

	if (edp_core_aux_read(DPCD_0002H, 1, &tmp_rx_buf[0]) < 0)
		return false;

	if (tmp_rx_buf[0] & DPCD_TPS3_SUPPORT_MASK)
		return true;
	else
		return false;
}


void edp_core_set_lane_para(struct edp_tx_core *edp_core)
{
	u64 bit_rate = edp_core->lane_para.bit_rate;
	u32 lane_cnt = edp_core->lane_para.lane_cnt;

	edp_hal_set_lane_rate(bit_rate);
	edp_hal_set_lane_cnt(lane_cnt);
}

void edp_lane_training_para_reset(struct edp_lane_para *lane_para)
{
	lane_para->lane0_sw = 0;
	lane_para->lane1_sw = 0;
	lane_para->lane2_sw = 0;
	lane_para->lane3_sw = 0;
	lane_para->lane0_pre = 0;
	lane_para->lane1_pre = 0;
	lane_para->lane2_pre = 0;
	lane_para->lane3_pre = 0;
}

void edp_core_set_training_para(struct edp_tx_core *edp_core)
{
	u32 sw[4];
	u32 pre[4];
	u32 lane_count = edp_core->lane_para.lane_cnt;
	u32 param_type = edp_core->training_param_type;
	u32 i = 0;

	sw[0] = edp_core->lane_para.lane0_sw;
	sw[1] = edp_core->lane_para.lane1_sw;
	sw[2] = edp_core->lane_para.lane2_sw;
	sw[3] = edp_core->lane_para.lane3_sw;
	pre[0] = edp_core->lane_para.lane0_pre;
	pre[1] = edp_core->lane_para.lane1_pre;
	pre[2] = edp_core->lane_para.lane2_pre;
	pre[3] = edp_core->lane_para.lane3_pre;

	for (i = 0; i < lane_count; i++) {
		EDP_CORE_DBG("set training para: lane[%d] sw:%d pre:%d\n", i, sw[i], pre[i]);
		edp_hal_set_lane_sw_pre(i, sw[i], pre[i], param_type);
	}
}

s32 edp_core_set_training_pattern(u32 pattern)
{
	edp_hal_set_training_pattern(pattern);
	return RET_OK;
}



s32 edp_dpcd_set_lane_para(struct edp_tx_core *edp_core)
{
	char tmp_tx_buf[16];
	s32 ret = RET_FAIL;

	memset(tmp_tx_buf, 0, sizeof(tmp_tx_buf));
	tmp_tx_buf[0] = edp_core->lane_para.bit_rate / 10000000 / 27;
	tmp_tx_buf[1] = edp_core->lane_para.lane_cnt;

	ret = edp_core_aux_write(DPCD_0100H, 2,  &tmp_tx_buf[0]);

	return ret;
}

s32 edp_dpcd_set_training_para(struct edp_tx_core *edp_core)
{
	char tmp_tx_buf[16];
	u32 sw[4];
	u32 pre[4];
	u32 lane_count = edp_core->lane_para.lane_cnt;
	u32 i = 0;

	sw[0] = edp_core->lane_para.lane0_sw;
	sw[1] = edp_core->lane_para.lane1_sw;
	sw[2] = edp_core->lane_para.lane2_sw;
	sw[3] = edp_core->lane_para.lane3_sw;
	pre[0] = edp_core->lane_para.lane0_pre;
	pre[1] = edp_core->lane_para.lane1_pre;
	pre[2] = edp_core->lane_para.lane2_pre;
	pre[3] = edp_core->lane_para.lane3_pre;

	memset(tmp_tx_buf, 0, sizeof(tmp_tx_buf));

	for (i = 0; i < lane_count; i++) {
		tmp_tx_buf[i] = (pre[i] << DPCD_PLEVEL_SHIFT) | (sw[i] << DPCD_VLEVEL_SHIFT);
		if (sw[i] == MAX_VLEVEL)
			tmp_tx_buf[i] |= DPCD_MAX_VLEVEL_REACHED_FLAG;
		if (pre[i] == MAX_PLEVEL)
			tmp_tx_buf[i] |= DPCD_MAX_PLEVEL_REACHED_FLAG;
	}

	edp_core_aux_write(DPCD_0103H, lane_count, &tmp_tx_buf[0]);

	return RET_OK;
}

s32 edp_dpcd_set_training_pattern(u8 pattern)
{
	char tmp_tx_buf[16];

	memset(tmp_tx_buf, 0, sizeof(tmp_tx_buf));

	tmp_tx_buf[0] = pattern;

	if (pattern && (pattern != TRAINING_PATTERN_4))
		tmp_tx_buf[0] |= DPCD_SCRAMBLING_DISABLE_FLAG;

	edp_core_aux_write(DPCD_0102H, 1, &tmp_tx_buf[0]);

	return RET_OK;
}

s32 edp_training_pattern_clear(struct edp_tx_core *edp_core)
{
	s32 ret = RET_OK;

	edp_core_set_training_pattern(TRAINING_PATTERN_DISABLE);
	ret = edp_dpcd_set_training_pattern(TRAINING_PATTERN_DISABLE);
	if (ret < 0)
		return ret;

	usleep_range(edp_core->interval_EQ, 2 * edp_core->interval_EQ);

	return ret;
}

bool edp_cr_training_done(char *link_status, u32 lane_count, u32 *fail_lane)
{
	u32 i = 0;
	u8 lane_status = 0;
	u32 cr_fail = 0;

	for (i = 0; i < lane_count; i++) {
		lane_status = (link_status[DPCD_LANE_STATUS_OFFSET(i)]) & DPCD_LANE_STATUS_MASK(i);
		EDP_CORE_DBG("DPCD[0x%x] = 0x%x\n", LINK_STATUS_BASE + DPCD_LANE_STATUS_OFFSET(i),\
		       link_status[DPCD_LANE_STATUS_OFFSET(i)]);

		EDP_CORE_DBG("CR_lane%d_status = 0x%x CR_DONE:%s\n", i, lane_status,
			     lane_status & DPCD_CR_DONE(i) ? "yes" : "no");

		if ((lane_status & DPCD_CR_DONE(i)) != DPCD_CR_DONE(i)) {
			EDP_CORE_DBG("lane_%d CR training fail!\n", i);
			cr_fail |= (1 << i);
		}
	}

	if (cr_fail) {
		*fail_lane = cr_fail;
		return false;
	} else {
		EDP_CORE_DBG("CR training success!\n");
		return true;
	}
}

bool edp_eq_training_done(char *link_status, u32 lane_count, u32 *fail_lane)
{
	u32 i = 0;
	u8 lane_status = 0;
	u8 align_status = 0;
	u32 eq_fail = 0;

	align_status = (link_status[DPCD_0204H - LINK_STATUS_BASE]);
	for (i = 0; i < lane_count; i++) {
		lane_status = (link_status[DPCD_LANE_STATUS_OFFSET(i)]) & DPCD_LANE_STATUS_MASK(i);
		EDP_CORE_DBG("DPCD[0x%x] = 0x%x\n", LINK_STATUS_BASE + DPCD_LANE_STATUS_OFFSET(i),\
		       link_status[DPCD_LANE_STATUS_OFFSET(i)]);

		EDP_CORE_DBG("EQ_lane%d_status = 0x%x EQ_DONE:%s SYMBOL_LOCK:%s\n", i, lane_status,
			     lane_status & DPCD_EQ_DONE(i) ? "yes" : "no",
			     lane_status & DPCD_SYMBOL_LOCK(i) ? "yes" : "no");

		if ((lane_status & DPCD_EQ_TRAIN_DONE(i)) !=  DPCD_EQ_TRAIN_DONE(i)) {
			EDP_CORE_DBG("lane_%d EQ training fail!\n", i);
			eq_fail |= (1 << i);
		}
	}

	EDP_CORE_DBG("Align_status = 0x%x INTERLANE_ALIGN:%s\n", align_status,
		     align_status & DPCD_ALIGN_DONE ? "yes" : "no");

	if ((align_status & DPCD_ALIGN_DONE) != DPCD_ALIGN_DONE) {
		EDP_CORE_DBG("EQ training align fail!\n");
		*fail_lane = eq_fail;
		return false;
	}

	if (eq_fail) {
		*fail_lane = eq_fail;
		return false;
	} else {
		EDP_CORE_DBG("EQ training success!\n");
		return true;
	}
}


bool edp_swing_level_reach_max(struct edp_tx_core *edp_core)
{
	u32 lane_count = edp_core->lane_para.lane_cnt;
	u32 sw[4];
	u32 i = 0;

	sw[0] = edp_core->lane_para.lane0_sw;
	sw[1] = edp_core->lane_para.lane1_sw;
	sw[2] = edp_core->lane_para.lane2_sw;
	sw[3] = edp_core->lane_para.lane3_sw;

	for (i = 0; i < lane_count; i++) {
		if (sw[i] == MAX_VLEVEL)
			return true;
	}

	return false;
}

u32 edp_adjust_train_para(char *link_status, struct edp_tx_core *edp_core)
{
	u32 old_sw[4];
	u32 old_pre[4];
	u32 adjust_sw[4];
	u32 adjust_pre[4];
	u32 i = 0;
	struct edp_lane_para *lane_para = &edp_core->lane_para;
	u32 lane_count = lane_para->lane_cnt;
	u32 change;

	change = 0;

	old_sw[0] = lane_para->lane0_sw;
	old_sw[1] = lane_para->lane1_sw;
	old_sw[2] = lane_para->lane2_sw;
	old_sw[3] = lane_para->lane3_sw;
	old_pre[0] = lane_para->lane0_pre;
	old_pre[1] = lane_para->lane1_pre;
	old_pre[2] = lane_para->lane2_pre;
	old_pre[3] = lane_para->lane3_pre;

	for (i = 0; i < lane_count; i++) {
		adjust_sw[i] = (link_status[DPCD_LANE_ADJ_OFFSET(i)] & VADJUST_MASK(i)) >> VADJUST_SHIFT(i);
		EDP_CORE_DBG("DPCD[0x%x] = 0x%x, adjust_sw[%d] = 0x%x\n", LINK_STATUS_BASE + DPCD_LANE_ADJ_OFFSET(i),\
		       link_status[DPCD_LANE_ADJ_OFFSET(i)], i, adjust_sw[i]);

		adjust_pre[i] = (link_status[DPCD_LANE_ADJ_OFFSET(i)] & PADJUST_MASK(i)) >> PADJUST_SHIFT(i);
		EDP_CORE_DBG("DPCD[0x%x] = 0x%x, adjust_pre[%d] = 0x%x\n", LINK_STATUS_BASE + DPCD_LANE_ADJ_OFFSET(i),\
		       link_status[DPCD_LANE_ADJ_OFFSET(i)], i, adjust_pre[i]);
	}

	lane_para->lane0_sw = adjust_sw[0];
	lane_para->lane1_sw = adjust_sw[1];
	lane_para->lane2_sw = adjust_sw[2];
	lane_para->lane3_sw = adjust_sw[3];

	lane_para->lane0_pre = adjust_pre[0];
	lane_para->lane1_pre = adjust_pre[1];
	lane_para->lane2_pre = adjust_pre[2];
	lane_para->lane3_pre = adjust_pre[3];

	for (i = 0; i < lane_count; i++) {
		if (old_sw[i] != adjust_sw[i])
			change |= SW_VOLTAGE_CHANGE_FLAG;
	}

	for (i = 0; i < lane_count; i++) {
		if (old_pre[i] != adjust_pre[i])
			change |= PRE_EMPHASIS_CHANGE_FLAG;
	}

	EDP_CORE_DBG("edp lane para change: %d\n", change);
	return change;
}

s32 edp_link_cr_training(struct edp_tx_core *edp_core)
{
	char link_status[16];
	u32 try_cnt = 0;
	u32 lane_count = edp_core->lane_para.lane_cnt;
	u32 change;
	u32 timeout = 0;
	u32 fail_lane;

	edp_core_set_training_pattern(TRAINING_PATTERN_1);
	edp_dpcd_set_training_pattern(TRAINING_PATTERN_1);

	for (try_cnt = 0; try_cnt < TRY_CNT_MAX; try_cnt++) {
		usleep_range(5 * edp_core->interval_CR, 10 * edp_core->interval_CR);

		memset(link_status, 0, sizeof(link_status));
		fail_lane = 0;
		edp_get_link_status(link_status);
		if (edp_cr_training_done(link_status, lane_count, &fail_lane)) {
			EDP_CORE_DBG("edp training1(clock recovery training) success!\n");
			return RET_OK;
		}

		if (timeout >= TRY_CNT_TIMEOUT) {
			EDP_ERR("edp training1(clock recovery training) timeout!\n");
			return RET_FAIL;
		}

		if (edp_swing_level_reach_max(edp_core)) {
			EDP_ERR("swing voltage reach max level, training1(clock recovery training) fail!\n");
			return RET_FAIL;
		}

		change = edp_adjust_train_para(link_status, edp_core);
		if (change & SW_VOLTAGE_CHANGE_FLAG) {
			try_cnt = 0;
		}

		if ((change & SW_VOLTAGE_CHANGE_FLAG) || (change & PRE_EMPHASIS_CHANGE_FLAG)) {
			edp_core_set_training_para(edp_core);
			edp_dpcd_set_training_para(edp_core);
		}

		timeout++;
	}

	EDP_ERR("CR training result: lane0:%s lane1:%s lane2:%s lane3:%s\n",
		fail_lane & (1 << 0) ? "FAIL" : "PASS",
		fail_lane & (1 << 1) ? "FAIL" : "PASS",
		fail_lane & (1 << 2) ? "FAIL" : "PASS",
		fail_lane & (1 << 3) ? "FAIL" : "PASS");
	EDP_ERR("retry 5 times but still fail, training1(clock recovery training) fail!\n");
	return RET_FAIL;
}

s32 edp_link_eq_training(struct edp_tx_core *edp_core)
{
	char link_status[16];
	u32 lane_count = edp_core->lane_para.lane_cnt;
	u32 try_cnt = 0;
	bool change;
	u32 fail_lane;

	if (edp_source_support_tps3() && edp_sink_support_tps3()) {
		edp_core_set_training_pattern(TRAINING_PATTERN_3);
		edp_dpcd_set_training_pattern(TRAINING_PATTERN_3);
	} else {
		edp_core_set_training_pattern(TRAINING_PATTERN_2);
		edp_dpcd_set_training_pattern(TRAINING_PATTERN_2);
	}

	for (try_cnt = 0; try_cnt < TRY_CNT_MAX; try_cnt++) {
		usleep_range(5 * edp_core->interval_EQ, 10 * edp_core->interval_EQ);
		memset(link_status, 0, sizeof(link_status));
		fail_lane = 0;
		edp_get_link_status(link_status);
		if (edp_eq_training_done(link_status, lane_count, &fail_lane)) {
			EDP_CORE_DBG("edp training2 (equalization training) success!\n");
			return RET_OK;
		}

		change = edp_adjust_train_para(link_status, edp_core);
		if ((change & SW_VOLTAGE_CHANGE_FLAG) || (change & PRE_EMPHASIS_CHANGE_FLAG)) {
			edp_core_set_training_para(edp_core);
			edp_dpcd_set_training_para(edp_core);
		}
	}

	EDP_ERR("EQ training result: lane0:%s lane1:%s lane2:%s lane3:%s\n",
		fail_lane & (1 << 0) ? "FAIL" : "PASS",
		fail_lane & (1 << 1) ? "FAIL" : "PASS",
		fail_lane & (1 << 2) ? "FAIL" : "PASS",
		fail_lane & (1 << 3) ? "FAIL" : "PASS");
	EDP_ERR("retry 5 times but still fail, training2(equalization training) fail!\n");
	return RET_FAIL;
}

s32 edp_fast_link_train(struct edp_tx_core *edp_core)
{
	char link_status[LINK_STATUS_SIZE];
	u32 lane_count = edp_core->lane_para.lane_cnt;
	u32 fail_lane = 0;

	edp_core_set_lane_para(edp_core);

	edp_core_set_training_para(edp_core);

	edp_core_set_training_pattern(TRAINING_PATTERN_1);
	usleep_range(5 * edp_core->interval_CR, 10 * edp_core->interval_CR);

	if (edp_source_support_tps3() && edp_sink_support_tps3()) {
		edp_core_set_training_pattern(TRAINING_PATTERN_3);
	} else {
		edp_core_set_training_pattern(TRAINING_PATTERN_2);
	}
	usleep_range(5 * edp_core->interval_EQ, 10 * edp_core->interval_EQ);

	memset(link_status, 0, sizeof(link_status));
	if (loglevel_debug & 0x2) {
		edp_get_link_status(link_status);
		if (!edp_cr_training_done(link_status, lane_count, &fail_lane)) {
			EDP_ERR("CR training result: lane0:%s lane1:%s lane2:%s lane3:%s\n",
				fail_lane & (1 << 0) ? "FAIL" : "PASS",
				fail_lane & (1 << 1) ? "FAIL" : "PASS",
				fail_lane & (1 << 2) ? "FAIL" : "PASS",
				fail_lane & (1 << 3) ? "FAIL" : "PASS");
			EDP_ERR("edp fast train fail in training1");
			return RET_FAIL;
		}

		fail_lane = 0;
		if (!edp_eq_training_done(link_status, lane_count, &fail_lane)) {
			EDP_ERR("EQ training result: lane0:%s lane1:%s lane2:%s lane3:%s\n",
				fail_lane & (1 << 0) ? "FAIL" : "PASS",
				fail_lane & (1 << 1) ? "FAIL" : "PASS",
				fail_lane & (1 << 2) ? "FAIL" : "PASS",
				fail_lane & (1 << 3) ? "FAIL" : "PASS");
			EDP_ERR("edp fast train fail in training2");
			return RET_FAIL;
		}
	}

	return RET_OK;
}

s32 edp_full_link_train(struct edp_tx_core *edp_core)
{
	s32 ret = RET_OK;

	edp_core_set_lane_para(edp_core);
	ret = edp_dpcd_set_lane_para(edp_core);
	if (ret < 0)
		return ret;

	edp_core_set_training_para(edp_core);
	ret = edp_dpcd_set_training_para(edp_core);
	if (ret < 0)
		return ret;

	ret = edp_link_cr_training(edp_core);
	if (ret < 0)
		return ret;

	ret = edp_link_eq_training(edp_core);
	if (ret < 0)
		return ret;

	return ret;
}


s32 edp_link_training(struct edp_tx_core *edp_core)
{
	if (edp_source_support_fast_train() \
		&& edp_sink_support_fast_train())
		return edp_fast_link_train(edp_core);
	else
		return edp_full_link_train(edp_core);
}

s32 edp_main_link_setup(struct edp_tx_core *edp_core)
{
	s32 ret = 0;

	/* DP Link CTS need ech training start from level-0 */
	edp_lane_training_para_reset(&edp_core->lane_para);

	ret = edp_link_training(edp_core);
	if (ret < 0)
		return ret;
	ret = edp_training_pattern_clear(edp_core);

	return ret;
}

s32 edp_core_link_start(void)
{
	return edp_hal_link_start();
}

s32 edp_core_link_stop(void)
{
	return edp_hal_link_stop();
}

s32 edp_low_power_en(bool en)
{
	char tmp_tx_buf[16];

	memset(tmp_tx_buf, 0, sizeof(tmp_tx_buf));

	if (en)
		tmp_tx_buf[0] = DPCD_LOW_POWER_ENTER;
	else
		tmp_tx_buf[0] = DPCD_LOW_POWER_EXIT;

	edp_core_aux_write(DPCD_0600H, 1, &tmp_tx_buf[0]);

	return RET_OK;
}

void edp_core_irq_handler(struct edp_tx_core *edp_core)
{
	edp_hal_irq_handler(edp_core);
}

s32 edp_core_irq_enable(u32 irq_id, bool en)
{
	if (en)
		return edp_hal_irq_enable(irq_id);
	else
		return edp_hal_irq_disable(irq_id);
}

s32 edp_core_irq_query(void)
{
	return edp_hal_irq_query();
}

s32 edp_core_irq_clear(void)
{
	return edp_hal_irq_clear();
}

s32 edp_core_get_cur_line(void)
{
	return edp_hal_get_cur_line();
}

s32 edp_core_get_start_dly(void)
{
	return edp_hal_get_start_dly();
}

void edp_core_show_builtin_patten(u32 pattern)
{
	edp_hal_show_builtin_patten(pattern);
}

s32 edp_core_aux_read(s32 addr, s32 lenth, char *buf)
{
	u32 retry_cnt = 0;
	s32 ret = 0;

	while (retry_cnt < 7) {
		ret = edp_hal_aux_read(addr, lenth, buf, retry_cnt ? true : false);
		/*
		 * for CTS 4.2.1.1, 4.2.2.5, add retry when AUX_NACK, AUX_DEFER,
		 * AUX_TIMEOUT, AUX_NO_STOP
		 */
		if ((ret != RET_AUX_NACK) &&
		    (ret != RET_AUX_TIMEOUT) &&
		    (ret != RET_AUX_DEFER) &&
		    (ret != RET_AUX_NO_STOP))
			break;
		/* at least 400us between two request is request in dp cts */
		usleep_range(500, 550);
		retry_cnt++;
	}

	if (ret != RET_OK)
		EDP_ERR("edp_aux_read fail, addr:0x%x lenth:0x%x\n", addr, lenth);

	return ret;
}

s32 edp_core_aux_write(s32 addr, s32 lenth, char *buf)
{
	u32 retry_cnt = 0;
	s32 ret = 0;

	while (retry_cnt < 7) {
		ret = edp_hal_aux_write(addr, lenth, buf, retry_cnt ? true : false);
		/*
		 * for CTS 4.2.1.1, 4.2.2.5, add retry when AUX_NACK, AUX_DEFER,
		 * AUX_TIMEOUT, AUX_NO_STOP
		 */
		if ((ret != RET_AUX_NACK) &&
		    (ret != RET_AUX_TIMEOUT) &&
		    (ret != RET_AUX_DEFER) &&
		    (ret != RET_AUX_NO_STOP))
			break;
		/* at least 400us between two request is request in dp cts */
		usleep_range(500, 550);
		retry_cnt++;
	}

	if (ret != RET_OK)
		EDP_ERR("edp_aux_write fail, addr:0x%x lenth:0x%x\n", addr, lenth);

	return ret;
}

s32 edp_core_aux_i2c_read(s32 i2c_addr, s32 lenth, char *buf)
{
	u32 retry_cnt = 0;
	s32 ret = 0;
	while (retry_cnt < 7) {
		ret = edp_hal_aux_i2c_read(i2c_addr, lenth, buf, retry_cnt ? true : false);
		/*
		 * for CTS 4.2.1.1, 4.2.2.5, add retry when AUX_NACK, AUX_DEFER,
		 * AUX_TIMEOUT, AUX_NO_STOP
		 */
		if ((ret != RET_AUX_NACK) &&
		    (ret != RET_AUX_TIMEOUT) &&
		    (ret != RET_AUX_DEFER) &&
		    (ret != RET_AUX_NO_STOP))
			break;
		/* at least 400us between two request is request in dp cts */
		usleep_range(500, 550);
		retry_cnt++;
	}

	if (ret != RET_OK)
		EDP_ERR("edp_aux_i2c_read fail, addr:0x%x lenth:0x%x\n", i2c_addr, lenth);

	return ret;
}

s32 edp_core_aux_i2c_write(s32 i2c_addr, s32 lenth, char *buf)
{
	u32 retry_cnt = 0;
	s32 ret = 0;
	while (retry_cnt < 7) {
		ret = edp_hal_aux_i2c_write(i2c_addr, lenth, buf, retry_cnt ? true : false);
		/*
		 * for CTS 4.2.1.1, 4.2.2.5, add retry when AUX_NACK, AUX_DEFER,
		 * AUX_TIMEOUT, AUX_NO_STOP
		 */
		if ((ret != RET_AUX_NACK) &&
		    (ret != RET_AUX_TIMEOUT) &&
		    (ret != RET_AUX_DEFER) &&
		    (ret != RET_AUX_NO_STOP))
			break;
		/* at least 400us between two request is request in dp cts */
		usleep_range(500, 550);
		retry_cnt++;
	}

	if (ret != RET_OK)
		EDP_ERR("edp_aux_i2c_write fail, addr:0x%x lenth:0x%x\n", i2c_addr, lenth);

	return ret;
}

s32 edp_core_audio_enable(void)
{
	return edp_hal_audio_enable();
}

s32 edp_core_audio_disable(void)
{
	return edp_hal_audio_disable();
}

s32 edp_core_ssc_enable(bool enable)
{
	return edp_hal_ssc_enable(enable);
}

bool edp_core_ssc_is_enabled(void)
{
	return edp_hal_ssc_is_enabled();
}

s32 edp_core_ssc_get_mode(void)
{
	return edp_hal_ssc_get_mode();
}

s32 edp_core_ssc_set_mode(u32 mode)
{
	return edp_hal_ssc_set_mode(mode);
}

s32 edp_core_psr_enable(bool enable)
{
	return edp_hal_psr_enable(enable);
}

bool edp_core_psr_is_enabled(void)
{
	return edp_hal_psr_is_enabled();
}

s32 edp_core_assr_enable(bool enable)
{
	s32 ret = RET_FAIL;
	char tmp_rx_buf[16];

	ret = edp_hal_assr_enable(enable);
	if (ret)
		return ret;

	memset(tmp_rx_buf, 0, sizeof(tmp_rx_buf));
	ret = edp_core_aux_read(DPCD_010AH, 1, &tmp_rx_buf[0]);
	if (ret)
		return ret;

	if (enable)
		tmp_rx_buf[0] |= DPCD_ASSR_ENABLE_MASK;
	else
		tmp_rx_buf[0] &= ~DPCD_ASSR_ENABLE_MASK;

	ret = edp_core_aux_write(DPCD_010AH, 1,  &tmp_rx_buf[0]);

	return ret;
}

s32 edp_core_set_pattern(u32 pattern)
{
	return edp_hal_set_pattern(pattern);
}

s32 edp_core_get_color_fmt(void)
{
	return edp_hal_get_color_fmt();
}

s32 edp_core_get_pixclk(void)
{
	return edp_hal_get_pixclk();
}

s32 edp_core_get_train_pattern(void)
{
	return edp_hal_get_train_pattern();
}

s32 edp_core_get_lane_para(struct edp_lane_para *tmp_lane_para)
{
	return edp_hal_get_lane_para(tmp_lane_para);
}

s32 edp_core_get_tu_size(void)
{
	return edp_hal_get_tu_size();
}

s32 edp_core_get_valid_symbol_per_tu(void)
{
	return edp_hal_get_valid_symbol_per_tu();
}

bool edp_core_audio_is_enabled(void)
{
	return edp_hal_audio_is_enabled();
}

s32 edp_core_get_audio_if(void)
{
	return edp_hal_get_audio_if();
}

s32 edp_core_audio_is_mute(void)
{
	return edp_hal_audio_is_mute();
}

s32 edp_core_get_audio_chn_cnt(void)
{
	return edp_hal_get_audio_chn_cnt();
}

s32 edp_core_get_audio_date_width(void)
{
	return edp_hal_get_audio_date_width();
}
