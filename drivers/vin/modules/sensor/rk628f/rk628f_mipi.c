/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * A V4L2 driver for tp2815 cameras and TVI Coax protocol.
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  GuCheng <gucheng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio/consumer.h>
#include "rk628_capture.h"
#include "../camera.h"
#include "../sensor_helper.h"
#include "rk628_twi_helper.h"
#include "rk628.h"
#include "rk628_combtxphy.h"
#include "rk628_combrxphy.h"
#include "rk628_csi.h"
#include "rk628_cru.h"
#include "rk628_hdmirx.h"
#include "rk628_mipi_dphy.h"
#include "rk628_post_process.h"
#include "rk628_v4l2_controls.h"

#include "../../../utility/vin_io.h"

MODULE_AUTHOR("GC");
MODULE_DESCRIPTION("A low-level driver for RK628 mipi chip for HDMI to MIPI CSI");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

// using external 24MHz crystal oscillator
// #define MCLK              (24*1000*1000)

#define V4L2_IDENT_SENSOR  0x20230321	// GRF_SOC_VERSION

#define I2C_ADDR  0xa0

#define SENSOR_NAME "rk628f_mipi"

#define RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST (V4L2_EVENT_PRIVATE_START + 1)

#define RK628_EDID_DEBUG 0

#define RK628_I2S_ENABLE 1

static char dev_info[128] = "s:0-w:0-h:0-fps:0-r:0"; //state-width-height-fps-rate
module_param_string(dev_info, dev_info, sizeof(dev_info), S_IRUGO | S_IWUSR);

struct rk628_csi {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct rk628 *rk628;
	struct media_pad pad;
	struct sensor_info info;
	// struct v4l2_subdev sd;
	struct v4l2_dv_timings src_timings;
	struct v4l2_dv_timings timings;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *audio_sampling_rate_ctrl;
	struct v4l2_ctrl *audio_present_ctrl;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	// struct gpio_config enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *power_gpio;
	struct gpio_desc *plugin_det_gpio;
	struct gpio_desc *int_det_gpio;
	//struct clk *soc_24M;
	struct clk *clk_hdmirx_aud;
	struct clk *clk_vop;
	struct clk *clk_rx_read;
	struct delayed_work delayed_work_enable_hotplug;
	struct delayed_work delayed_work_res_change;
	struct work_struct work_isr;
	struct mutex confctl_mutex;
	// struct rkmodule_multi_dev_info multi_dev_info;
	// const struct rk628_csi_mode *cur_mode;
	// const char *module_facing;
	// const char *module_name;
	// const char *len_name;
	//u32 module_index;
	u8 edid_blocks_written;
	u64 lane_mbps;
	u8 csi_lanes_in_use;
	// u32 mbus_fmt_code;
	u8 fps;
	u32 stream_state;
	int hdmirx_irq;
	int plugin_irq;
	int avi_rdy;
	bool nosignal;
	bool rxphy_pwron;
	bool txphy_pwron;
	bool enable_hdcp;
	bool scaler_en;
	bool hpd_output_inverted;
	//bool hdmirx_det_inverted;
	bool avi_rcv_rdy;
	bool vid_ints_en;
	bool continues_clk;
	bool cec_enable;
	struct rk628_hdmirx_cec *cec;
	bool is_dvi;
	struct rk628_hdcp hdcp;
	bool i2s_enable_default;
	HAUDINFO audio_info;
	struct rk628_combtxphy *txphy;
	//struct rk628_dsi dsi;
	//const struct rk628_plat_data *plat_data;
	struct device *classdev;
	bool is_streaming;
	bool csi_ints_en;
	bool stopping;
	bool powered;
	bool cci_registered;
	bool sysfs_registered;
	bool fmt_valid;
	bool hdmirx_irq_requested;
	bool plugin_irq_requested;
	u32 irq_trigger;
	u32 rx_clock_deskew[2];
	int signal_error;
	struct sensor_win_size window;
};

static const s64 link_freq_menu_items[] = { 350000000, 650000000 };

static const struct v4l2_dv_timings_cap rk628_csi_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1280, 3840, 720, 2160, 25000000, 300000000,
			     V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			     V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			     V4L2_DV_BT_CAP_PROGRESSIVE |
			     V4L2_DV_BT_CAP_REDUCED_BLANKING |
			     V4L2_DV_BT_CAP_CUSTOM)
};



static const struct mipi_timing rk628d_csi_mipi = {
	0x4a, 0xf, 0x5d, 0x3a, 0x3a, 0x5a, 0x1f
};

static const struct mipi_timing rk628f_csi0_mipi = {
	0x4a, 0xf, 0x5d, 0x3a, 0x3a, 0x5a, 0x1f
};

static const struct mipi_timing rk628f_csi1_mipi = {
//data-pre, data-zero, data-trail, clk-pre, clk-zero, clk-trail, clk-post
	0x4a, 0xf, 0x66, 0x3a, 0x3a, 0x5a, 0x1f
};

static int rk628_post_process_setup(struct v4l2_subdev *sd);
static void rk628_csi_enable_interrupts(struct v4l2_subdev *sd, bool en);
static void rk628_csi_enable_csi_interrupts(struct v4l2_subdev *sd, bool en);
static void rk628_csi_clear_csi_interrupts(struct v4l2_subdev *sd);
//static int rk628_csi_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd);
static int sensor_s_dv_timings(struct v4l2_subdev *sd, struct v4l2_dv_timings *timings);
static int rk628_write_edid(struct v4l2_subdev *sd, struct v4l2_subdev_edid *edid);
static int mipi_dphy_power_on(struct rk628_csi *csi);
static void mipi_dphy_power_off(struct rk628_csi *csi);
static int rk628_hdmirx_inno_phy_power_on(struct v4l2_subdev *sd);
static int rk628_hdmirx_inno_phy_power_off(struct v4l2_subdev *sd);
static int rk628_hdmirx_phy_setup(struct v4l2_subdev *sd);
static int rk628_csi_format_change(struct v4l2_subdev *sd);
static int enable_stream(struct v4l2_subdev *sd, bool enable);
static int rk628_csi_update_controls(struct v4l2_subdev *sd);
static void rk628_hdmirx_vid_enable(struct v4l2_subdev *sd, bool en);
static int rk628_csi_set_csi(struct v4l2_subdev *sd);
static void rk628_hdmirx_hpd_ctrl(struct v4l2_subdev *sd, bool en);
static bool rk628_rcv_supported_res(struct v4l2_subdev *sd, u32 width, u32 height);
// static void rk628_dsi_set_scs(struct rk628_csi *csi);
// static void rk628_dsi_enable(struct v4l2_subdev *sd);
static void rk628_csi_disable_stream(struct v4l2_subdev *sd);

static inline struct rk628_csi *info_to_csi(struct sensor_info *info)
{
	return container_of(info, struct rk628_csi, info);
}

static inline struct rk628_csi *sd_to_csi(struct v4l2_subdev *sd)
{
	struct sensor_info *info = to_state(sd);
	return info_to_csi(info);
}

static unsigned int rk628_timing_fps(const struct v4l2_dv_timings *t)
{
	const struct v4l2_bt_timings *bt = &t->bt;

	if (t->type != V4L2_DV_BT_656_1120)
		return 0;
	return rk628_capture_fps(bt->width, bt->height,
		V4L2_DV_BT_FRAME_WIDTH(bt), V4L2_DV_BT_FRAME_HEIGHT(bt),
		bt->pixelclock, bt->interlaced);
}

static bool rk628_timings_match(const struct v4l2_dv_timings *a,
				const struct v4l2_dv_timings *b)
{
	struct v4l2_dv_timings adjusted = *b;
	u32 delta;

	if (a->type != V4L2_DV_BT_656_1120 || b->type != V4L2_DV_BT_656_1120)
		return false;
	delta = a->bt.hsync > b->bt.hsync ?
		a->bt.hsync - b->bt.hsync : b->bt.hsync - a->bt.hsync;
	/* RX counter quantization shifts HSYNC/HBP while their sum stays fixed. */
	if (delta <= 2 && a->bt.hfrontporch == b->bt.hfrontporch &&
	    (u64)a->bt.hsync + a->bt.hbackporch == (u64)b->bt.hsync + b->bt.hbackporch) {
		adjusted.bt.hsync = a->bt.hsync;
		adjusted.bt.hbackporch = a->bt.hbackporch;
	}
	return v4l2_match_dv_timings(a, &adjusted, 1000000, false);
}

static void rk628_queue_hotplug(struct rk628_csi *csi, unsigned long delay)
{
	if (!READ_ONCE(csi->stopping))
		mod_delayed_work(system_wq, &csi->delayed_work_enable_hotplug, delay);
}

static void rk628_signal_lost(struct v4l2_subdev *sd, int error)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	const struct v4l2_event event = {
		.type = RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST,
	};

	enable_stream(sd, false);
	csi->nosignal = true;
	csi->signal_error = error;
	csi->fmt_valid = false;
	v4l2_event_queue(sd->devnode, &event);
}

static void rk628_sync_window(struct rk628_csi *csi)
{
	struct sensor_info *info = &csi->info;
	const struct v4l2_bt_timings *bt = &csi->timings.bt;

	mutex_lock(&info->lock);
	memset(&csi->window, 0, sizeof(csi->window));
	csi->window.width = bt->width;
	csi->window.height = bt->height;
	csi->window.width_input = bt->width;
	csi->window.height_input = bt->height;
	csi->window.hts = V4L2_DV_BT_FRAME_WIDTH(bt);
	csi->window.vts = V4L2_DV_BT_FRAME_HEIGHT(bt);
	csi->window.pclk = bt->pixelclock;
	csi->window.mipi_bps = rk628_capture_bps(bt->pixelclock);
	csi->window.fps_fixed = rk628_timing_fps(&csi->timings);
	info->current_wins = &csi->window;
	info->width = bt->width;
	info->height = bt->height;
	info->tpf.numerator = 1;
	info->tpf.denominator = csi->window.fps_fixed;
	mutex_unlock(&info->lock);
	if (csi->link_freq)
		v4l2_ctrl_s_ctrl(csi->link_freq, bt->pixelclock > 150000000);
	if (csi->pixel_rate)
		v4l2_ctrl_s_ctrl_int64(csi->pixel_rate, bt->pixelclock);
}

static bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	bool ret;
	struct rk628_csi *csi = sd_to_csi(sd);

	ret = rk628_hdmirx_tx_5v_power_detect(csi->plugin_det_gpio);
	sensor_dbg("%s: %d\n", __func__, ret);

	return ret;
}

static inline bool no_signal(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	sensor_dbg("%s no signal:%d\n", __func__, csi->nosignal);
	return csi->nosignal;
}

static inline bool audio_present(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	return rk628_hdmirx_audio_present(csi->audio_info);
}

static int get_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	if (no_signal(sd))
		return 0;

	return rk628_hdmirx_audio_fs(csi->audio_info);
}

static void rk628_hdmirx_ctrl_enable(struct v4l2_subdev *sd, int en)
{
	u32 mask;
	struct rk628_csi *csi = sd_to_csi(sd);

	if (en) {
		/* don't enable audio until N CTS updated */
		mask = HDMI_ENABLE_MASK;
		sensor_dbg("%s: %#x %d\n", __func__, mask, en);
		rk628_i2c_update_bits(csi->rk628, HDMI_RX_DMI_DISABLE_IF,
				   mask, HDMI_ENABLE(1) | AUD_ENABLE(1));
	} else {
		mask = AUD_ENABLE_MASK | HDMI_ENABLE_MASK;
		sensor_dbg("%s: %#x %d\n", __func__, mask, en);
		rk628_i2c_update_bits(csi->rk628, HDMI_RX_DMI_DISABLE_IF,
				   mask, HDMI_ENABLE(0) | AUD_ENABLE(0));
	}
}

static int rk628_csi_get_detected_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	__maybe_unused struct v4l2_bt_timings *bt = &timings->bt;
	int ret;

	ret = rk628_hdmirx_get_timings(csi->rk628, timings);
	if (ret)
		return ret;

	if (!rk628_timing_fps(timings))
		return -ERANGE;
	csi->rk628->dual_mipi = false;

	sensor_dbg("hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d, interlace:%d\n",
		 bt->hfrontporch, bt->hsync, bt->hbackporch, bt->vfrontporch, bt->vsync,
		 bt->vbackporch, bt->interlaced);

	csi->src_timings = *timings;
	if (csi->scaler_en)
		*timings = csi->timings;

	return ret;
}

static void rk628_hdmirx_plugout(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	rk628_signal_lost(sd, -ENOLINK);
	rk628_csi_enable_interrupts(sd, false);
	cancel_delayed_work(&csi->delayed_work_res_change);
	rk628_hdmirx_audio_cancel_work_audio(csi->audio_info, false);
	rk628_hdmirx_hpd_ctrl(sd, false);
	rk628_hdmirx_inno_phy_power_off(sd);
	rk628_hdmirx_controller_reset(csi->rk628);
}

static void rk628_hdmirx_config_all(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = rk628_hdmirx_phy_setup(sd);

	if (!ret && !rk628_hdmirx_scdc_ced_err(csi->rk628))
		ret = rk628_csi_format_change(sd);
	else if (!ret)
		ret = -ENOLCK;
	if (!ret)
		return;
	rk628_signal_lost(sd, ret == -ERANGE ? -ERANGE : -ENOLCK);
	if (ret != -ERANGE)
		rk628_queue_hotplug(csi, msecs_to_jiffies(800));
}

static void sensor_uevent_notifiy(struct v4l2_subdev *sd, int w, int h, int fps, int status, int audiosamplingrate)
{
	char state[16], width[16], height[16], frame[16], audio_sampling_rate[32];
	char *envp[7] = {
		"SYSTEM=HDMIIN",
		NULL,
		NULL,
		NULL,
		NULL,
		NULL };
	snprintf(state, sizeof(state), "STATE=%d", status);
	snprintf(width, sizeof(width), "WIDTH=%d", w);
	snprintf(height, sizeof(height), "HEIGHT=%d", h);
	snprintf(frame, sizeof(frame), "FPS=%d", fps);
	snprintf(audio_sampling_rate, sizeof(audio_sampling_rate), "RATE=%d", audiosamplingrate);
	envp[1] = width;
	envp[2] = height;
	envp[3] = frame;
	envp[4] = state;
	envp[5] = audio_sampling_rate;
	snprintf(dev_info, sizeof(dev_info), "s:%d-w:%d-h:%d-fps:%d-r:%d", status, w, h, fps, audiosamplingrate);
	kobject_uevent_env(&sd->dev->kobj, KOBJ_CHANGE, envp);
}

static inline unsigned int fps_calc(const struct v4l2_bt_timings *t);
static void rk628_csi_delayed_work_enable_hotplug(struct work_struct *work)
{
	struct rk628_csi *csi = container_of(to_delayed_work(work),
		struct rk628_csi, delayed_work_enable_hotplug);
	struct v4l2_subdev *sd = &csi->info.sd;
	bool plugin;

	mutex_lock(&csi->confctl_mutex);
	if (csi->stopping)
		goto out;
	plugin = tx_5v_power_present(sd);
	v4l2_ctrl_s_ctrl(csi->detect_tx_5v_ctrl, plugin);
	if (!plugin) {
		rk628_hdmirx_plugout(sd);
		sensor_uevent_notifiy(sd, 0, 0, 0, 0, 0);
		goto out;
	}
	rk628_signal_lost(sd, -ENOLCK);
	csi->avi_rcv_rdy = false;
	rk628_set_bg_enable(csi->rk628, false);
	rk628_csi_enable_interrupts(sd, false);
	rk628_hdmirx_audio_setup(csi->audio_info);
	rk628_hdmirx_controller_setup(csi->rk628);
	rk628_hdmirx_hpd_ctrl(sd, csi->edid_blocks_written != 0);
	rk628_hdmirx_config_all(sd);
	rk628_csi_enable_interrupts(sd, true);
	if (!csi->nosignal)
		sensor_uevent_notifiy(sd, csi->timings.bt.width,
				      csi->timings.bt.height, rk628_timing_fps(&csi->timings),
				      1, get_audio_sampling_rate(sd));
out:
	if (!csi->stopping)
		rk628_csi_update_controls(sd);
	mutex_unlock(&csi->confctl_mutex);
}

static void rk628_delayed_work_res_change(struct work_struct *work)
{
	struct rk628_csi *csi = container_of(to_delayed_work(work),
		struct rk628_csi, delayed_work_res_change);

	rk628_queue_hotplug(csi, 0);
}

static void rk628_hdmirx_hpd_ctrl(struct v4l2_subdev *sd, bool en)
{
	u8 en_level, set_level;
	struct rk628_csi *csi = sd_to_csi(sd);
	unsigned int rdval = 0xffffffff;

	sensor_dbg("%s: %sable, hpd invert:%d\n", __func__,
			en ? "en" : "dis", csi->hpd_output_inverted);
	en_level = csi->hpd_output_inverted ? 0 : 1;
	set_level = en ? en_level : !en_level;
	rk628_i2c_update_bits(csi->rk628, HDMI_RX_HDMI_SETUP_CTRL,
			HOT_PLUG_DETECT_MASK, HOT_PLUG_DETECT(set_level));

	if (csi->cec_enable && csi->cec)
		rk628_hdmirx_cec_hpd(csi->cec, en);

	read_rk628(sd, 0x00030000, &rdval);
	sensor_print("rk628_hdmirx_hpd_ctrl read RK628F HDMI_RX_HDMI_SETUP_CTRL = 0x%08x\n", rdval);
}


static int rk628_csi_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	return v4l2_ctrl_s_ctrl(csi->detect_tx_5v_ctrl, tx_5v_power_present(sd));
}

static int rk628_csi_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	return v4l2_ctrl_s_ctrl(csi->audio_sampling_rate_ctrl, get_audio_sampling_rate(sd));
}

static int rk628_csi_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	return v4l2_ctrl_s_ctrl(csi->audio_present_ctrl, audio_present(sd));
}

static int rk628_csi_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret |= rk628_csi_s_ctrl_detect_tx_5v(sd);
	ret |= rk628_csi_s_ctrl_audio_sampling_rate(sd);
	ret |= rk628_csi_s_ctrl_audio_present(sd);

	return ret;
}

static void rk628_csi0_cru_reset(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	rk628_control_assert(csi->rk628, RGU_CSI);
	udelay(10);
	rk628_control_deassert(csi->rk628, RGU_CSI);
}

static void rk628_csi1_cru_reset(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	rk628_control_assert(csi->rk628, RGU_CSI1);
	udelay(10);
	rk628_control_deassert(csi->rk628, RGU_CSI1);
}

static void rk628_csi_soft_reset(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	rk628_i2c_write(csi->rk628, CSITX_SYS_CTRL0_IMD, 0x1);
	usleep_range(1000, 1100);
	rk628_i2c_write(csi->rk628, CSITX_SYS_CTRL0_IMD, 0x0);

	if (csi->rk628->version >= RK628F_VERSION) {
		rk628_i2c_write(csi->rk628, CSITX1_SYS_CTRL0_IMD, 0x1);
		usleep_range(1000, 1100);
		rk628_i2c_write(csi->rk628, CSITX1_SYS_CTRL0_IMD, 0x0);
	}
}

static int enable_csitx(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret;

	ret = rk628_i2c_update_bits(csi->rk628, GRF_SYSTEM_CON3,
				    GRF_DPHY_CH1_EN_MASK, 0);
	if (ret)
		return ret;
	ret = rk628_i2c_update_bits(csi->rk628, GRF_POST_PROC_CON,
				    SW_SPLIT_EN, 0);
	if (ret)
		return ret;
	ret = rk628_csi_set_csi(sd);
	if (ret)
		return ret;
	rk628_csi_soft_reset(sd);
	usleep_range(5000, 5500);
	ret = rk628_i2c_write(csi->rk628, CSITX_INTR_EN_IMD, 0x0fff0000);
	if (ret)
		return ret;
	ret = rk628_i2c_update_bits(csi->rk628, CSITX_CSITX_EN,
				    DPHY_EN_MASK | CSITX_EN_MASK, DPHY_EN(1) | CSITX_EN(1));
	if (ret)
		return ret;
	ret = rk628_i2c_write(csi->rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
	if (ret)
		return ret;
	ret = rk628_i2c_write(csi->rk628, CSITX_ERR_INTR_CLR_IMD, 0xffffffff);
	if (ret)
		return ret;
	return rk628_i2c_write(csi->rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
}

static void rk628_csi_disable_stream(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	rk628_i2c_update_bits(csi->rk628, CSITX_CSITX_EN,
				DPHY_EN_MASK | CSITX_EN_MASK,
				DPHY_EN(0) | CSITX_EN(0));
	rk628_i2c_update_bits(csi->rk628, CSITX_SYS_CTRL3_IMD, CONT_MODE_CLK_CLR_MASK,
			csi->continues_clk ? CONT_MODE_CLK_CLR(1) : CONT_MODE_CLK_CLR(0));
	rk628_i2c_write(csi->rk628, CSITX_CONFIG_DONE, CONFIG_DONE);

	if (csi->rk628->version >= RK628F_VERSION) {
		rk628_i2c_update_bits(csi->rk628, CSITX1_CSITX_EN,
					DPHY_EN_MASK | CSITX_EN_MASK,
					DPHY_EN(0) | CSITX_EN(0));
		rk628_i2c_update_bits(csi->rk628, CSITX1_SYS_CTRL3_IMD, CONT_MODE_CLK_CLR_MASK,
				csi->continues_clk ? CONT_MODE_CLK_CLR(1) : CONT_MODE_CLK_CLR(0));
		rk628_i2c_write(csi->rk628, CSITX1_CONFIG_DONE, CONFIG_DONE);
	}
}

static int rk628_mipi_txdata_reset(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret, release_ret;

	ret = rk628_control_assert(csi->rk628, RGU_TXDATA);
	if (ret)
		goto release;
	ret = rk628_control_assert(csi->rk628, RGU_TXBYTEHS);
	if (!ret)
		usleep_range(1000, 1100);
release:
	/* Release both resets even after a partial I2C failure. */
	release_ret = rk628_control_deassert(csi->rk628, RGU_TXDATA);
	if (!ret)
		ret = release_ret;
	release_ret = rk628_control_deassert(csi->rk628, RGU_TXBYTEHS);
	return ret ? ret : release_ret;
}

static int enable_stream(struct v4l2_subdev *sd, bool en)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret;

	if (!en) {
		csi->is_streaming = false;
		if (!csi->powered)
			return 0;
		rk628_csi_enable_csi_interrupts(sd, false);
		rk628_hdmirx_vid_enable(sd, false);
		rk628_csi_disable_stream(sd);
		csi->is_streaming = false;
		return 0;
	}
	if (csi->stopping)
		return -ENODEV;
	if (!tx_5v_power_present(sd))
		return -ENOLINK;
	if (csi->nosignal)
		return csi->signal_error;
	if (!rk628_timing_fps(&csi->timings))
		return -ERANGE;
	if (rk628_hdmirx_scdc_ced_err(csi->rk628))
		return -ENOLCK;
	ret = enable_csitx(sd);
	if (ret) {
		enable_stream(sd, false);
		return ret;
	}
	rk628_hdmirx_vid_enable(sd, true);
	if (csi->rk628->version >= RK628F_VERSION) {
		msleep(20);
		ret = rk628_mipi_txdata_reset(sd);
		if (ret) {
			enable_stream(sd, false);
			return ret;
		}
	}
	rk628_csi_enable_csi_interrupts(sd, true);
	csi->is_streaming = true;
	return 0;
}

static int rk628_post_process_setup(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	struct v4l2_bt_timings *bt = &csi->src_timings.bt;
	struct v4l2_bt_timings *dst_bt = &csi->timings.bt;
	struct videomode src, dst;
	u64 dst_pclk;

	src.hactive = bt->width;
	src.hfront_porch = bt->hfrontporch;
	src.hsync_len = bt->hsync;
	src.hback_porch = bt->hbackporch;
	src.vactive = bt->height;
	src.vfront_porch = bt->vfrontporch;
	src.vsync_len = bt->vsync;
	src.vback_porch = bt->vbackporch;
	src.pixelclock = bt->pixelclock;
	src.flags = 0;
	if (bt->interlaced == V4L2_DV_INTERLACED)
		src.flags |= DISPLAY_FLAGS_INTERLACED;
	if (!src.pixelclock) {
		enable_stream(sd, false);
		csi->nosignal = true;
		rk628_queue_hotplug(csi, HZ / 20);
		return -ENOLCK;
	}

	dst.hactive = dst_bt->width;
	dst.hfront_porch = dst_bt->hfrontporch;
	dst.hsync_len = dst_bt->hsync;
	dst.hback_porch = dst_bt->hbackporch;
	dst.vactive = dst_bt->height;
	dst.vfront_porch = dst_bt->vfrontporch;
	dst.vsync_len = dst_bt->vsync;
	dst.vback_porch = dst_bt->vbackporch;
	dst.pixelclock = dst_bt->pixelclock;

	rk628_post_process_en(csi->rk628, &src, &dst, &dst_pclk);
	dst_bt->pixelclock = dst_pclk;
	return 0;
}

static int rk628_csi_set_csi(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	u8 video_fmt;
	u8 lanes = csi->csi_lanes_in_use;
	u8 lane_num;
	u32 wc_usrdef, val;
	int ret;

	lane_num = lanes - 1;
	csi->rk628->dphy_lane_en = (1 << (lanes + 1)) - 1;
	wc_usrdef = csi->timings.bt.width * 2;
	if (csi->rk628->dual_mipi)
		wc_usrdef = csi->timings.bt.width;
	sensor_print("%s mipi mode, word count user define: %d\n", csi->rk628->dual_mipi ? "dual" : "single", wc_usrdef);
	rk628_csi_disable_stream(sd);
	usleep_range(5000, 5500);
	rk628_csi0_cru_reset(sd);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_csi1_cru_reset(sd);
	rk628_mipi_dphy_reset(csi->rk628);
	ret = rk628_post_process_setup(sd);
	if (ret)
		return ret;

	if (csi->txphy_pwron) {
		sensor_dbg("%s: txphy already power on, power off\n", __func__);
		mipi_dphy_power_off(csi);
		csi->txphy_pwron = false;
	}

	ret = mipi_dphy_power_on(csi);
	if (ret) {
		mipi_dphy_power_off(csi);
		return ret;
	}
	csi->txphy_pwron = true;
	sensor_dbg("%s: txphy power on!\n", __func__);
	usleep_range(1000, 1500);

	if (csi->rk628->version <= RK628D_VERSION) {
		rk628_i2c_update_bits(csi->rk628, CSITX_CSITX_EN,
			VOP_UV_SWAP_MASK |
			VOP_YUV422_EN_MASK |
			VOP_P2_EN_MASK |
			LANE_NUM_MASK |
			DPHY_EN_MASK |
			CSITX_EN_MASK,
			VOP_UV_SWAP(1) |
			VOP_YUV422_EN(1) |
			VOP_P2_EN(1) |
			LANE_NUM(lane_num) |
			DPHY_EN(0) |
			CSITX_EN(0));
		rk628_i2c_update_bits(csi->rk628, CSITX_SYS_CTRL1,
			BYPASS_SELECT_MASK,
			BYPASS_SELECT(1));
	} else {
		rk628_i2c_update_bits(csi->rk628, CSITX_CSITX_EN,
			VOP_UV_SWAP_MASK |
			VOP_YUV422_EN_MASK |
			VOP_YUV422_MODE_MASK |
			VOP_P2_EN_MASK |
			LANE_NUM_MASK |
			DPHY_EN_MASK |
			CSITX_EN_MASK,
			VOP_UV_SWAP(0) |
			VOP_YUV422_EN(1) |
			VOP_YUV422_MODE(2) |
			VOP_P2_EN(1) |
			LANE_NUM(lane_num) |
			DPHY_EN(0) |
			CSITX_EN(0));
		rk628_i2c_update_bits(csi->rk628, CSITX_SYS_CTRL1,
			BYPASS_SELECT_MASK,
			BYPASS_SELECT(0));
	}

	rk628_i2c_write(csi->rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
	rk628_i2c_write(csi->rk628, CSITX_SYS_CTRL2, VOP_WHOLE_FRM_EN | VSYNC_ENABLE);
	if (csi->continues_clk)
		rk628_i2c_update_bits(csi->rk628, CSITX_SYS_CTRL3_IMD,
			CONT_MODE_CLK_CLR_MASK |
			CONT_MODE_CLK_SET_MASK |
			NON_CONTINUOUS_MODE_MASK,
			CONT_MODE_CLK_CLR(0) |
			CONT_MODE_CLK_SET(1) |
			NON_CONTINUOUS_MODE(0));
	else
		rk628_i2c_update_bits(csi->rk628, CSITX_SYS_CTRL3_IMD,
			CONT_MODE_CLK_CLR_MASK |
			CONT_MODE_CLK_SET_MASK |
			NON_CONTINUOUS_MODE_MASK,
			CONT_MODE_CLK_CLR(0) |
			CONT_MODE_CLK_SET(0) |
			NON_CONTINUOUS_MODE(1));

	rk628_i2c_write(csi->rk628, CSITX_VOP_PATH_CTRL,
			VOP_WC_USERDEFINE(wc_usrdef) |
			VOP_DT_USERDEFINE(YUV422_8BIT) |
			VOP_PIXEL_FORMAT(0) |
			VOP_WC_USERDEFINE_EN(1) |
			VOP_DT_USERDEFINE_EN(1) |
			VOP_PATH_EN(1));
	rk628_i2c_update_bits(csi->rk628, CSITX_DPHY_CTRL,
				CSI_DPHY_EN_MASK,
				CSI_DPHY_EN(csi->rk628->dphy_lane_en));
	rk628_i2c_update_bits(csi->rk628, CSITX_VOP_FILTER_CTRL,
				VOP_FILTER_EN_MASK | VOP_FILTER_MASK,
				VOP_FILTER_EN(1) | VOP_FILTER(3));
	rk628_i2c_write(csi->rk628, CSITX_CONFIG_DONE, CONFIG_DONE_IMD);
	sensor_dbg("%s csi config done\n", __func__);

	if (csi->rk628->dual_mipi) {
		rk628_i2c_update_bits(csi->rk628, CSITX1_CSITX_EN,
				VOP_UV_SWAP_MASK |
				VOP_YUV422_EN_MASK |
				VOP_YUV422_MODE_MASK |
				VOP_P2_EN_MASK |
				LANE_NUM_MASK |
				DPHY_EN_MASK |
				CSITX_EN_MASK,
				VOP_UV_SWAP(0) |
				VOP_YUV422_EN(1) |
				VOP_YUV422_MODE(2) |
				VOP_P2_EN(1) |
				LANE_NUM(lane_num) |
				DPHY_EN(0) |
				CSITX_EN(0));
		rk628_i2c_update_bits(csi->rk628, CSITX1_SYS_CTRL1,
				BYPASS_SELECT_MASK,
				BYPASS_SELECT(0));
		rk628_i2c_write(csi->rk628, CSITX1_CONFIG_DONE, CONFIG_DONE_IMD);
		rk628_i2c_write(csi->rk628, CSITX1_SYS_CTRL2, VOP_WHOLE_FRM_EN | VSYNC_ENABLE);
		if (csi->continues_clk)
			rk628_i2c_update_bits(csi->rk628, CSITX1_SYS_CTRL3_IMD,
				CONT_MODE_CLK_CLR_MASK |
				CONT_MODE_CLK_SET_MASK |
				NON_CONTINUOUS_MODE_MASK,
				CONT_MODE_CLK_CLR(0) |
				CONT_MODE_CLK_SET(1) |
				NON_CONTINUOUS_MODE(0));
		else
			rk628_i2c_update_bits(csi->rk628, CSITX1_SYS_CTRL3_IMD,
				CONT_MODE_CLK_CLR_MASK |
				CONT_MODE_CLK_SET_MASK |
				NON_CONTINUOUS_MODE_MASK,
				CONT_MODE_CLK_CLR(0) |
				CONT_MODE_CLK_SET(0) |
				NON_CONTINUOUS_MODE(1));

		rk628_i2c_write(csi->rk628, CSITX1_VOP_PATH_CTRL,
				VOP_WC_USERDEFINE(wc_usrdef) |
				VOP_DT_USERDEFINE(YUV422_8BIT) |
				VOP_PIXEL_FORMAT(0) |
				VOP_WC_USERDEFINE_EN(1) |
				VOP_DT_USERDEFINE_EN(1) |
				VOP_PATH_EN(1));
		rk628_i2c_update_bits(csi->rk628, CSITX1_DPHY_CTRL,
					CSI_DPHY_EN_MASK,
					CSI_DPHY_EN(csi->rk628->dphy_lane_en));
		rk628_i2c_update_bits(csi->rk628, CSITX1_VOP_FILTER_CTRL,
				VOP_FILTER_EN_MASK | VOP_FILTER_MASK,
				VOP_FILTER_EN(1) | VOP_FILTER(3));
		rk628_i2c_write(csi->rk628, CSITX1_CONFIG_DONE, CONFIG_DONE_IMD);
		sensor_dbg("%s csi1 config done\n", __func__);
	}

	csi->avi_rdy = rk628_is_avi_ready(csi->rk628, csi->avi_rcv_rdy);

	ret = rk628_i2c_read(csi->rk628, HDMI_RX_PDEC_AVI_PB, &val);
	if (ret)
		return ret;
	video_fmt = (val & VIDEO_FORMAT_MASK) >> 5;
	sensor_dbg("%s PDEC_AVI_PB:%#x, video format:%d\n",
			__func__, val, video_fmt);
	if (csi->rk628->version == RK628D_VERSION) {
		if (video_fmt) {
			/* yuv data: cfg SW_YUV2VYU_SWP */
			rk628_i2c_write(csi->rk628, GRF_CSC_CTRL_CON,
				SW_YUV2VYU_SWP(1) |
				SW_R2Y_EN(0));
		} else {
			/* rgb data: cfg SW_R2Y_EN */
			rk628_i2c_write(csi->rk628, GRF_CSC_CTRL_CON,
				SW_YUV2VYU_SWP(0) |
				SW_R2Y_EN(1) | SW_R2Y_CSC_MODE(2));
		}
	} else {
		rk628_i2c_write(csi->rk628, GRF_CSC_CTRL_CON, SW_YUV2VYU_SWP(1));
		rk628_post_process_csc_en(csi->rk628);
	}
	/* if avi packet is not stable, reset ctrl*/
	if (!csi->avi_rdy) {
		csi->nosignal = true;
		csi->signal_error = -ENOLCK;
		rk628_queue_hotplug(csi, HZ / 20);
		return -ENOLCK;
	}
	return 0;
}

static int rk628_hdmirx_inno_phy_power_on(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret, f;

	sensor_print("%s begin... csi->rxphy_pwron: %s", __func__, csi->rxphy_pwron ? "true" : "false");

	/* Bit31 is used to distinguish HDMI cable mode and direct connection
	 * mode in the rk628_combrxphy driver.
	 * Bit31: 0 -direct connection mode;
	 *        1 -cable mode;
	 * The cable mode is to know the input clock frequency through cdr_mode
	 * in the rk628_combrxphy driver, and the cable mode supports up to
	 * 297M, so 297M is passed uniformly here.
	 */
	f = 297000 | BIT(31);

	if (csi->rxphy_pwron) {
		sensor_dbg("rxphy already power on, power off!\n");
		ret = rk628_rxphy_power_off(csi->rk628);
		if (ret)
			sensor_err("hdmi rxphy power off failed!\n");
		else
			csi->rxphy_pwron = false;
		usleep_range(100, 110);
	}

	if (csi->rxphy_pwron == false) {
		rk628_hdmirx_ctrl_enable(sd, 0);
		ret = rk628_rxphy_power_on(csi->rk628, f);
		if (ret) {
			csi->rxphy_pwron = false;
			sensor_err("hdmi rxphy power on failed\n");
		} else {
			csi->rxphy_pwron = true;
		}
		rk628_hdmirx_ctrl_enable(sd, 1);
		msleep(100);
	}

	return ret;
}

static int rk628_hdmirx_inno_phy_power_off(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	if (csi->rk628->version >= RK628F_VERSION)
		return 0;

	if (csi->rxphy_pwron) {
		sensor_dbg("rxphy power off!\n");
		rk628_rxphy_power_off(csi->rk628);
		csi->rxphy_pwron = false;
	}
	usleep_range(100, 100);

	return 0;
}

static void rk628_hdmirx_vid_enable(struct v4l2_subdev *sd, bool en)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	sensor_dbg("%s: %sable\n", __func__, en ? "en" : "dis");
	if (en) {
		if (!csi->i2s_enable_default)
			rk628_hdmirx_audio_i2s_ctrl(csi->audio_info, true);
		rk628_i2c_update_bits(csi->rk628, HDMI_RX_DMI_DISABLE_IF,
				      VID_ENABLE_MASK, VID_ENABLE(1));
	} else {
		if (!csi->i2s_enable_default)
			rk628_hdmirx_audio_i2s_ctrl(csi->audio_info, false);
		rk628_i2c_update_bits(csi->rk628, HDMI_RX_DMI_DISABLE_IF,
				      VID_ENABLE_MASK, VID_ENABLE(0));
	}
}

static bool rk628_rcv_supported_res(struct v4l2_subdev *sd, u32 width,
		u32 height)
{
	// u32 i;
	struct rk628_csi *csi = sd_to_csi(sd);

	if (csi->rk628->version >= RK628F_VERSION)
		return true;

	return true;

	// for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
	// 	if ((supported_modes[i].width == width) &&
	// 	    (supported_modes[i].height == height)) {
	// 		break;
	// 	}
	// }
	// if (i == ARRAY_SIZE(supported_modes)) {
	// 	sensor_err("%s do not support res wxh: %dx%d\n", __func__, width, height);
	// 	return false;
	// } else {
	// 	return true;
	// }
}

static int rk628_hdmirx_phy_setup(struct v4l2_subdev *sd)
{
	u32 i, cnt, val;
	u32 width, height, frame_width, frame_height, status;
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	for (i = 0; i < RXPHY_CFG_MAX_TIMES; i++) {
		if (csi->rk628->version < RK628F_VERSION)
			ret = rk628_hdmirx_inno_phy_power_on(sd);
		else
			rk628_hdmirx_verisyno_phy_power_on(csi->rk628);
		if (ret < 0) {
			msleep(50);
			continue;
		}
		cnt = 0;

		do {
			msleep(20);
			cnt++;
			rk628_i2c_read(csi->rk628, HDMI_RX_MD_HACT_PX, &val);
			width = val & 0xffff;	// 0x780 (1920)
			rk628_i2c_read(csi->rk628, HDMI_RX_MD_VAL, &val);
			height = val & 0xffff;	// 0x870 (2160)
			rk628_i2c_read(csi->rk628, HDMI_RX_MD_HT1, &val);
			frame_width = (val >> 16) & 0xffff;	// 0x898 (2200)
			rk628_i2c_read(csi->rk628, HDMI_RX_MD_VTL, &val);
			frame_height = val & 0xffff;		// 0x8ca (2250)
			rk628_i2c_read(csi->rk628, HDMI_RX_SCDC_REGS1, &val);	// 0xffff0f00
			status = val;
			sensor_dbg("%s read wxh:%dx%d, total:%dx%d, SCDC_REGS1:%#x, cnt:%d\n",
				 __func__, width, height, frame_width, frame_height, status, cnt);

			rk628_i2c_read(csi->rk628, HDMI_RX_PDEC_STS, &val);
			if (csi->rk628->version < RK628F_VERSION && (val & DVI_DET)) {
				csi->is_dvi = true;
				dev_info(csi->dev, "DVI mode detected\n");
			} else {
				csi->is_dvi = false;
			}

			if (!tx_5v_power_present(sd)) {
				sensor_print("HDMI pull out, return!\n");
				return -1;
			}

			if (cnt >= 15)
				break;
		} while (((status & 0xfff) < 0xf00) || (!rk628_rcv_supported_res(sd, width, height)));

		if (((status & 0xfff) < 0xf00) || (!rk628_rcv_supported_res(sd, width, height))) {
			sensor_err("%s hdmi rxphy lock failed, retry:%d\n", __func__, i);
			continue;
		} else {
			if (csi->rk628->version >= RK628F_VERSION)
				rk628_hdmirx_phy_prepclk_cfg(csi->rk628);
			break;
		}
	}

	if (i == RXPHY_CFG_MAX_TIMES)
		return -1;

	return 0;
}

static int rk628_csi_initial(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	struct v4l2_subdev_edid def_edid;
	u32 mask = SW_OUTPUT_MODE_MASK;
	u32 val = SW_OUTPUT_MODE(OUTPUT_MODE_CSI);

	/* select int io function */
	rk628_i2c_write(csi->rk628, GRF_GPIO3AB_SEL_CON, 0x30002000);
	rk628_i2c_write(csi->rk628, GRF_GPIO1AB_SEL_CON, HIWORD_UPDATE(0xf, 11, 8));
	/* I2S_SCKM0 */
	rk628_i2c_write(csi->rk628, GRF_GPIO0AB_SEL_CON, HIWORD_UPDATE(0x1, 2, 2));
	/* I2SLR_M0 */
	rk628_i2c_write(csi->rk628, GRF_GPIO0AB_SEL_CON, HIWORD_UPDATE(0x1, 3, 3));
	/* I2SM0D0 */
	rk628_i2c_write(csi->rk628, GRF_GPIO0AB_SEL_CON, HIWORD_UPDATE(0x1, 5, 4));
	/* hdmirx int en */
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_i2c_write(csi->rk628, GRF_INTR0_EN, 0x02000200);
	else
		rk628_i2c_write(csi->rk628, GRF_INTR0_EN, 0x01000100);

	udelay(10);
	rk628_control_assert(csi->rk628, RGU_HDMIRX);
	rk628_control_assert(csi->rk628, RGU_HDMIRX_PON);
	rk628_control_assert(csi->rk628, RGU_CSI);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_control_assert(csi->rk628, RGU_CSI1);
	udelay(10);
	rk628_control_deassert(csi->rk628, RGU_HDMIRX);
	rk628_control_deassert(csi->rk628, RGU_HDMIRX_PON);
	rk628_control_deassert(csi->rk628, RGU_CSI);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_control_deassert(csi->rk628, RGU_CSI1);
	udelay(10);

	if (csi->rk628->version >= RK628F_VERSION) {
		mask = SW_OUTPUT_COMBTX_MODE_MASK;
		val = SW_OUTPUT_COMBTX_MODE(OUTPUT_MODE_CSI - 1);
	}
	rk628_i2c_update_bits(csi->rk628, GRF_SYSTEM_CON0,
			SW_INPUT_MODE_MASK |
			mask |
			SW_EFUSE_HDCP_EN_MASK |
			SW_HSYNC_POL_MASK |
			SW_VSYNC_POL_MASK |
			SW_I2S_DATA_OEN_MASK,
			SW_INPUT_MODE(INPUT_MODE_HDMI) |
			val |
			SW_EFUSE_HDCP_EN(0) |
			SW_HSYNC_POL(1) |
			SW_VSYNC_POL(1) |
			SW_I2S_DATA_OEN(0));
	rk628_hdmirx_controller_reset(csi->rk628);

	def_edid.pad = 0;
	def_edid.start_block = 0;
	def_edid.blocks = 2;
	def_edid.edid = (u8 *)rk628_capture_edid;
	rk628_hdmirx_set_hdcp(csi->rk628, &csi->hdcp, false);
	return rk628_write_edid(sd, &def_edid);
}

static int rk628_csi_initial_setup(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = rk628_csi_initial(sd);

	if (ret)
		return ret;
	if (csi->rk628->version >= RK628F_VERSION) {
		csi->rk628->mipi_timing[0] = rk628f_csi0_mipi;
		csi->rk628->mipi_timing[1] = rk628f_csi1_mipi;
	} else {
		csi->rk628->mipi_timing[0] = rk628d_csi_mipi;
	}

	csi->rk628->dphy_lane_en = 0x1f;
	rk628_mipi_dphy_reset(csi->rk628);

	if (tx_5v_power_present(sd))
		rk628_queue_hotplug(csi, msecs_to_jiffies(4000));
	return 0;
}

static int rk628_csi_format_change(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event event = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};
	int ret = rk628_csi_get_detected_timings(sd, &timings);

	if (ret)
		return ret;
	enable_stream(sd, false);
	csi->timings = timings;
	csi->fmt_valid = false;
	rk628_sync_window(csi);
	csi->nosignal = false;
	csi->signal_error = 0;
	if (sd->devnode)
		v4l2_subdev_notify_event(sd, &event);
	return 0;
}

static void rk628_csi_enable_csi_interrupts(struct v4l2_subdev *sd, bool en)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	rk628_csi_clear_csi_interrupts(sd);
	//disabled csi state ints
	rk628_i2c_write(csi->rk628, CSITX_INTR_EN_IMD, 0x0fff0000);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_i2c_write(csi->rk628, CSITX1_INTR_EN_IMD, 0x0fff0000);

	//enable csi error ints
	if (en) {
		if (csi->rk628->version >= RK628F_VERSION) {
			rk628_i2c_update_bits(csi->rk628,
					GRF_INTR0_EN, CSI_INT_EN_MASK | CSI_INT_WRITE_EN_MASK,
					CSI_INT_EN(3) | CSI_INT_WRITE_EN(3));
			rk628_i2c_write(csi->rk628, CSITX_ERR_INTR_EN_IMD, 0x0fff0fff);
			rk628_i2c_write(csi->rk628, CSITX1_ERR_INTR_EN_IMD, 0x0fff0fff);
		} else {
			rk628_i2c_update_bits(csi->rk628,
					GRF_INTR0_EN, CSI_INT_EN_MASK | CSI_INT_WRITE_EN_MASK,
					CSI_INT_EN(1) | CSI_INT_WRITE_EN(1));
			rk628_i2c_write(csi->rk628, CSITX_ERR_INTR_EN_IMD, 0x0fff0fff);
		}
		csi->csi_ints_en = true;
	} else {
		if (csi->rk628->version >= RK628F_VERSION) {
			rk628_i2c_update_bits(csi->rk628,
					GRF_INTR0_EN, CSI_INT_EN_MASK | CSI_INT_WRITE_EN_MASK,
					CSI_INT_EN(0) | CSI_INT_WRITE_EN(3));
			rk628_i2c_write(csi->rk628, CSITX_ERR_INTR_EN_IMD, 0x0fff0000);
			rk628_i2c_write(csi->rk628, CSITX1_ERR_INTR_EN_IMD, 0x0fff0000);
		} else {
			rk628_i2c_update_bits(csi->rk628,
					GRF_INTR0_EN, CSI_INT_EN_MASK | CSI_INT_WRITE_EN_MASK,
					CSI_INT_EN(0) | CSI_INT_WRITE_EN(1));
			rk628_i2c_write(csi->rk628, CSITX_ERR_INTR_EN_IMD, 0x0fff0000);
		}
		csi->csi_ints_en = false;
	}
}

static void rk628_csi_enable_interrupts(struct v4l2_subdev *sd, bool en)
{
	u32 pdec_ien, md_ien;
	u32 pdec_mask = 0, md_mask = 0;
	struct rk628_csi *csi = sd_to_csi(sd);

	pdec_mask = AVI_RCV_ENSET | AVI_CKS_CHG_ICLR;
	md_mask = VACT_LIN_ENSET | HACT_PIX_ENSET | HS_CLK_ENSET |
		  DE_ACTIVITY_ENSET | VS_ACT_ENSET | HS_ACT_ENSET | VS_CLK_ENSET;
	sensor_dbg("%s: %sable\n", __func__, en ? "en" : "dis");
	/* clr irq */
	rk628_i2c_write(csi->rk628, HDMI_RX_MD_ICLR, md_mask);
	rk628_i2c_write(csi->rk628, HDMI_RX_PDEC_ICLR, pdec_mask);
	if (en) {
		rk628_i2c_write(csi->rk628, HDMI_RX_MD_IEN_SET, md_mask);
		rk628_i2c_write(csi->rk628, HDMI_RX_PDEC_IEN_SET, pdec_mask);
		csi->vid_ints_en = true;
	} else {
		rk628_i2c_write(csi->rk628, HDMI_RX_MD_IEN_CLR, md_mask);
		rk628_i2c_write(csi->rk628, HDMI_RX_PDEC_IEN_CLR, pdec_mask);
		rk628_i2c_write(csi->rk628, HDMI_RX_AUD_FIFO_IEN_CLR, 0x1f);
		if (csi->cec && csi->cec->adap) {
			rk628_i2c_write(csi->rk628, HDMI_RX_AUD_CEC_IEN_SET, 0);
			rk628_i2c_write(csi->rk628, HDMI_RX_AUD_CEC_IEN_CLR, ~0);
		}
		csi->vid_ints_en = false;
	}
	usleep_range(5000, 5000);
	rk628_i2c_read(csi->rk628, HDMI_RX_MD_IEN, &md_ien);
	rk628_i2c_read(csi->rk628, HDMI_RX_PDEC_IEN, &pdec_ien);
	sensor_dbg("%s MD_IEN:%#x, PDEC_IEN:%#x\n", __func__, md_ien, pdec_ien);
}

static void rk628_csi_clear_csi_interrupts(struct v4l2_subdev *sd)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	//clr int status
	rk628_i2c_write(csi->rk628, CSITX_ERR_INTR_CLR_IMD, 0xffffffff);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_i2c_write(csi->rk628, CSITX1_ERR_INTR_CLR_IMD, 0xffffffff);

	if (csi->rk628->version >= RK628F_VERSION)
		rk628_i2c_update_bits(csi->rk628, GRF_INTR0_CLR_EN, CSI_INT_EN_MASK |
				CSI_INT_WRITE_EN_MASK, CSI_INT_EN(3) | CSI_INT_WRITE_EN(3));
	else
		rk628_i2c_update_bits(csi->rk628, GRF_INTR0_CLR_EN, CSI_INT_EN_MASK |
				CSI_INT_WRITE_EN_MASK, CSI_INT_EN(1) | CSI_INT_WRITE_EN(1));
}

static void rk628_csi_error_process(struct v4l2_subdev *sd)
{
	rk628_signal_lost(sd, -ENOLCK);
	rk628_queue_hotplug(sd_to_csi(sd), msecs_to_jiffies(100));
}

static void rk628_work_isr(struct work_struct *work)
{
	struct rk628_csi *csi = container_of(work, struct rk628_csi, work_isr);
	struct v4l2_subdev *sd = &csi->info.sd;
	u32 md_ints, pdec_ints, fifo_ints, hact, vact;
	bool plugin;
	void *audio_info = csi->audio_info;
	bool handled = false;
	u32 csi0_raw_ints, csi1_raw_ints = 0x0;
	u32 int0_status;

	mutex_lock(&csi->confctl_mutex);
	if (csi->stopping)
		goto out_unlock;
	rk628_i2c_read(csi->rk628, GRF_INTR0_STATUS, &int0_status);
	sensor_dbg("%s: int0 status: 0x%x\n", __func__, int0_status);

	rk628_i2c_read(csi->rk628, HDMI_RX_MD_ISTS, &md_ints);
	rk628_i2c_read(csi->rk628, HDMI_RX_PDEC_ISTS, &pdec_ints);
	if (csi->rk628->version >= RK628F_VERSION &&
	    rk628_hdmirx_is_signal_change_ists(csi->rk628))
		rk628_set_bg_enable(csi->rk628, true);

	plugin = tx_5v_power_present(sd);
	if (!plugin) {
		rk628_signal_lost(sd, -ENOLINK);
		rk628_queue_hotplug(csi, msecs_to_jiffies(50));
		rk628_csi_enable_interrupts(sd, false);
		rk628_csi_enable_csi_interrupts(sd, false);
		goto __clear_int;
	}

	if (csi->rk628->version < RK628F_VERSION) {
		if (rk628_audio_ctsnints_enabled(audio_info)) {
			if (pdec_ints & (ACR_N_CHG_ICLR | ACR_CTS_CHG_ICLR)) {
				rk628_csi_isr_ctsn(audio_info, pdec_ints);
				pdec_ints &= ~(ACR_CTS_CHG_ICLR | ACR_CTS_CHG_ICLR);
				handled = true;
			}
		}
		if (rk628_audio_fifoints_enabled(audio_info)) {
			rk628_i2c_read(csi->rk628, HDMI_RX_AUD_FIFO_ISTS, &fifo_ints);
			if (fifo_ints & 0x18) {
				rk628_csi_isr_fifoints(audio_info, fifo_ints);
				handled = true;
			}
		}
	}
	if (csi->vid_ints_en) {
		sensor_dbg("%s: md_ints: %#x, pdec_ints:%#x, plugin: %d\n",
			 __func__, md_ints, pdec_ints, plugin);

		if (rk628_hdmirx_is_signal_change_ists(csi->rk628)) {
			rk628_i2c_read(csi->rk628, HDMI_RX_MD_HACT_PX, &hact);
			rk628_i2c_read(csi->rk628, HDMI_RX_MD_VAL, &vact);
			sensor_dbg("%s: HACT:%#x, VACT:%#x\n",
				 __func__, hact, vact);

			rk628_csi_enable_interrupts(sd, false);
			rk628_signal_lost(sd, -ENOLCK);
			sensor_dbg("rk628_work_isr v4l2_event_queue evt_signal_lost: V4L2_EVENT_SRC_CH_RESOLUTION\n");
			schedule_delayed_work(&csi->delayed_work_res_change, msecs_to_jiffies(100));

			sensor_dbg("%s: hact/vact change, md_ints: %#x\n",
				 __func__, (u32)(md_ints & (VACT_LIN_ISTS | HACT_PIX_ISTS)));
			handled = true;
		}

		if ((pdec_ints & AVI_RCV_ISTS) && plugin && !csi->avi_rcv_rdy) {
			sensor_dbg("%s: AVI RCV INT!\n", __func__);
			csi->avi_rcv_rdy = true;
			/* After get the AVI_RCV interrupt state, disable interrupt. */
			rk628_i2c_write(csi->rk628, HDMI_RX_PDEC_IEN_CLR, AVI_RCV_ISTS);

			handled = true;
		}
	}

	if (int0_status & (BIT(6) | BIT(7))) {
		rk628_i2c_read(csi->rk628, CSITX_ERR_INTR_RAW_STATUS_IMD, &csi0_raw_ints);
		if (csi->rk628->version >= RK628F_VERSION)
			rk628_i2c_read(csi->rk628, CSITX1_ERR_INTR_RAW_STATUS_IMD, &csi1_raw_ints);

		if (csi0_raw_ints || csi1_raw_ints) {
			sensor_print("%s: csi interrupt: csi0_raw_ints: 0x%x, csi1_raw_ints: 0x%x!\n",
						__func__, csi0_raw_ints, csi1_raw_ints);
			rk628_csi_error_process(sd);
		}
		handled = true;
	}

	if (!handled)
		sensor_dbg("%s: unhandled interrupt!\n", __func__);

__clear_int:
	/* clear interrupts */
	rk628_i2c_write(csi->rk628, HDMI_RX_MD_ICLR, 0xffffffff);
	rk628_i2c_write(csi->rk628, HDMI_RX_PDEC_ICLR, 0xffffffff);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_i2c_write(csi->rk628, GRF_INTR0_CLR_EN, 0x02000200);
	else
		rk628_i2c_write(csi->rk628, GRF_INTR0_CLR_EN, 0x01000100);
	rk628_csi_clear_csi_interrupts(sd);

out_unlock:
	mutex_unlock(&csi->confctl_mutex);
}


static irqreturn_t rk628_csi_irq_handler(int irq, void *dev_id)
{
	struct rk628_csi *csi = dev_id;

	rk628_work_isr(&csi->work_isr);
	return IRQ_HANDLED;
}

static inline unsigned int fps_calc(const struct v4l2_bt_timings *t)
{
	if (!V4L2_DV_BT_FRAME_HEIGHT(t) || !V4L2_DV_BT_FRAME_WIDTH(t))
		return 0;

	return DIV_ROUND_CLOSEST((unsigned int)t->pixelclock,
			V4L2_DV_BT_FRAME_HEIGHT(t) * V4L2_DV_BT_FRAME_WIDTH(t));
}

static int sensor_get_edid(struct v4l2_subdev *sd,
		struct v4l2_subdev_edid *edid)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	if (edid->pad)
		return -EINVAL;
	memset(edid->reserved, 0, sizeof(edid->reserved));
	mutex_lock(&csi->confctl_mutex);
	if (!edid->start_block && !edid->blocks) {
		edid->blocks = csi->edid_blocks_written;
	} else if (!csi->edid_blocks_written) {
		ret = -ENODATA;
	} else if (!edid->blocks || edid->start_block >= csi->edid_blocks_written) {
		ret = -EINVAL;
	} else {
		edid->blocks = min_t(u32, edid->blocks,
				     csi->edid_blocks_written - edid->start_block);
		memcpy(edid->edid, rk628_capture_edid + edid->start_block * 128,
		       edid->blocks * 128);
	}
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

/* Caller holds confctl_mutex, including during probe/resume. */
static int rk628_write_edid(struct v4l2_subdev *sd,
			    struct v4l2_subdev_edid *edid)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	unsigned int i;
	int ret, restore;

	if (edid->pad || edid->start_block || edid->blocks > 2)
		return -EINVAL;
	rk628_hdmirx_hpd_ctrl(sd, false);
	csi->edid_blocks_written = 0;
	if (!edid->blocks)
		return 0;
	ret = rk628_i2c_update_bits(csi->rk628, GRF_SYSTEM_CON0,
				    SW_ADAPTER_I2CSLADR_MASK | SW_EDID_MODE_MASK,
				    SW_ADAPTER_I2CSLADR(0) | SW_EDID_MODE(1));
	if (ret)
		return ret;
	for (i = 0; i < edid->blocks * 128; i++) {
		ret = rk628_i2c_write(csi->rk628, EDID_BASE + i * 4, edid->edid[i]);
		if (ret)
			break;
	}
	restore = rk628_i2c_update_bits(csi->rk628, GRF_SYSTEM_CON0,
					SW_EDID_MODE_MASK, SW_EDID_MODE(0));
	if (ret || restore)
		return ret ? ret : restore;
	csi->edid_blocks_written = edid->blocks;
	return 0;
}

static int sensor_set_edid(struct v4l2_subdev *sd,
		struct v4l2_subdev_edid *edid)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret;

	if (edid->pad || edid->start_block)
		return -EINVAL;
	/* Keep the advertised modes within the single-port capture contract. */
	if (edid->blocks && (edid->blocks != 2 || !edid->edid ||
			     memcmp(edid->edid, rk628_capture_edid, sizeof(rk628_capture_edid))))
		return -EINVAL;
	mutex_lock(&csi->confctl_mutex);
	if (csi->stopping) {
		ret = -ENODEV;
	} else if (csi->is_streaming) {
		ret = -EBUSY;
	} else {
		rk628_signal_lost(sd, -ENOLCK);
		ret = rk628_write_edid(sd, edid);
		if (ret || edid->blocks)
			rk628_queue_hotplug(csi, msecs_to_jiffies(100));
	}
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static int mipi_dphy_power_on(struct rk628_csi *csi)
{
	unsigned int val;
	u32 bus_width, mask;
	int ret;

	if (csi->timings.bt.pixelclock > 150000000 || csi->csi_lanes_in_use <= 2) {
		csi->lane_mbps = MIPI_DATARATE_MBPS_HIGH;
	} else {
		csi->lane_mbps = MIPI_DATARATE_MBPS_LOW;
	}
	if (csi->rk628->dual_mipi)
		csi->lane_mbps = MIPI_DATARATE_MBPS_HIGH;
	bus_width =  csi->lane_mbps << 8;
	bus_width |= COMBTXPHY_MODULEA_EN;
	if (csi->rk628->version >= RK628F_VERSION)
		bus_width |= COMBTXPHY_MODULEB_EN;
	sensor_dbg("%s mipi bitrate:%llu mbps\n", __func__,
			csi->lane_mbps);
	rk628_txphy_set_bus_width(csi->rk628, bus_width);
	rk628_txphy_set_mode(csi->rk628, PHY_MODE_VIDEO_MIPI);

	rk628_mipi_dphy_init_hsfreqrange(csi->rk628, csi->lane_mbps, 0);
	if (csi->rk628->version >= RK628F_VERSION)
		rk628_mipi_dphy_init_hsfreqrange(csi->rk628, csi->lane_mbps, 1);

	if (csi->rk628->dual_mipi) {
		rk628_mipi_dphy_init_hsmanual(csi->rk628, true, 0);
		rk628_mipi_dphy_init_hsmanual(csi->rk628, true, 1);
	} else if (csi->lane_mbps == MIPI_DATARATE_MBPS_HIGH && !csi->rk628->dual_mipi) {
		rk628_mipi_dphy_init_hsmanual(csi->rk628, true, 0);
		rk628_mipi_dphy_init_hsmanual(csi->rk628, false, 1);
	} else {
		rk628_mipi_dphy_init_hsmanual(csi->rk628, false, 0);
		rk628_mipi_dphy_init_hsmanual(csi->rk628, false, 1);
	}

	usleep_range(1500, 2000);
	rk628_txphy_power_on(csi->rk628);

	usleep_range(1500, 2000);
	mask = DPHY_PLL_LOCK;
	ret = rk628_i2c_read(csi->rk628, CSITX_CSITX_STATUS1, &val);
	if (ret)
		return ret;
	if ((val & mask) != mask) {
		dev_err(csi->dev, "PHY is not locked\n");
		return -ENOLCK;
	}
	if (csi->rk628->dual_mipi) {
		rk628_i2c_read(csi->rk628, CSITX1_CSITX_STATUS1, &val);
		if ((val & mask) != mask) {
			dev_err(csi->dev, "PHY1 is not locked\n");
			return -ENOLCK;
		}
	}
	udelay(10);

	return 0;
}

static void mipi_dphy_power_off(struct rk628_csi *csi)
{
	rk628_txphy_power_off(csi->rk628);
}

static irqreturn_t plugin_detect_irq(int irq, void *dev_id)
{
	struct rk628_csi *csi = dev_id;

	mutex_lock(&csi->confctl_mutex);
	if (!csi->stopping) {
		rk628_signal_lost(&csi->info.sd,
				  tx_5v_power_present(&csi->info.sd) ? -ENOLCK : -ENOLINK);
		rk628_queue_hotplug(csi, msecs_to_jiffies(50));
	}
	mutex_unlock(&csi->confctl_mutex);
	return IRQ_HANDLED;
}

static int rk628_csi_power_on(struct rk628_csi *csi)
{
	gpiod_set_value_cansleep(csi->power_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(csi->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(csi->reset_gpio, 0);
	usleep_range(10000, 11000);
	csi->powered = true;
	return 0;
}

static int rk628_csi_power_off(struct rk628_csi *csi)
{
	gpiod_set_value_cansleep(csi->reset_gpio, 1);
	gpiod_set_value_cansleep(csi->power_gpio, 0);
	csi->powered = false;
	return 0;
}

static void rk628_stop(struct rk628_csi *csi)
{
	mutex_lock(&csi->confctl_mutex);
	csi->stopping = true;
	mutex_unlock(&csi->confctl_mutex);
	if (csi->hdmirx_irq_requested)
		disable_irq(csi->hdmirx_irq);
	if (csi->plugin_irq_requested)
		disable_irq(csi->plugin_irq);
	cancel_delayed_work_sync(&csi->delayed_work_res_change);
	cancel_delayed_work_sync(&csi->delayed_work_enable_hotplug);
	cancel_work_sync(&csi->work_isr);
	if (csi->audio_info) {
		rk628_hdmirx_audio_cancel_work_audio(csi->audio_info, true);
		if (csi->rk628->version < RK628F_VERSION)
			rk628_hdmirx_audio_cancel_work_rate_change(csi->audio_info, true);
	}
	mutex_lock(&csi->confctl_mutex);
	if (csi->powered) {
		rk628_signal_lost(&csi->info.sd, -ENOLINK);
		rk628_hdmirx_hpd_ctrl(&csi->info.sd, false);
		if (csi->txphy_pwron) {
			mipi_dphy_power_off(csi);
			csi->txphy_pwron = false;
		}
		rk628_csi_power_off(csi);
	}
	mutex_unlock(&csi->confctl_mutex);
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int rk628_csi_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));

	rk628_stop(sd_to_csi(sd));
	return 0;
}

static int rk628_csi_resume(struct device *dev)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret;

	mutex_lock(&csi->confctl_mutex);
	rk628_csi_power_on(csi);
	rk628_cru_initialize(csi->rk628);
	ret = rk628_csi_initial_setup(sd);
	if (ret) {
		rk628_csi_power_off(csi);
		mutex_unlock(&csi->confctl_mutex);
		return ret;
	}
	csi->stopping = false;
	mutex_unlock(&csi->confctl_mutex);
	if (csi->hdmirx_irq_requested)
		enable_irq(csi->hdmirx_irq);
	if (csi->plugin_irq_requested)
		enable_irq(csi->plugin_irq);
	rk628_queue_hotplug(csi, msecs_to_jiffies(100));
	return 0;
}

#endif

#if IS_ENABLED(CONFIG_PM)
static int sensor_runtime_suspend(struct device *dev)
{
	sensor_dbg("%s\n", __func__);
	return 0;
}

static int sensor_runtime_resume(struct device *dev)
{
	sensor_dbg("%s\n", __func__);
	return 0;
}

static int sensor_runtime_idle(struct device *dev)
{
	if (dev) {
		pm_runtime_mark_last_busy(dev);
		pm_request_autosuspend(dev);
	} else {
		sensor_err("%s, sensor device is null\n", __func__);
	}
	return 0;
}
#endif

static const struct dev_pm_ops sensor_runtime_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rk628_csi_suspend, rk628_csi_resume)
	SET_RUNTIME_PM_OPS(sensor_runtime_suspend, sensor_runtime_resume,
			       sensor_runtime_idle)
};

/*
 * The default register settings
 *
 */
static struct regval_list sensor_default_regs[] = {
};



static int sensor_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	ret = 0;
	switch (on) {
	case STBY_ON:
		sensor_dbg("STBY_ON!\n");
		cci_lock(sd);
		cci_unlock(sd);
		break;
	case STBY_OFF:
		sensor_dbg("STBY_OFF!\n");
		cci_lock(sd);
		cci_unlock(sd);
		break;
	case PWR_ON:
		sensor_dbg("PWR_ON! do nothing!\n");
		//cci_lock(sd);
		// vin_gpio_set_status(sd, PWDN, 1);
		// vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		// usleep_range(10000, 11000);
		// vin_gpio_set_status(sd, RESET, 1);
		// vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		// usleep_range(10000, 11000);
		// vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		// usleep_range(10000, 11000);
		// vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		// usleep_range(10000, 11000);
		//cci_unlock(sd);
		break;
	case PWR_OFF:
		sensor_dbg("PWR_OFF! do nothing!\n");
		//cci_lock(sd);
		//vin_gpio_set_status(sd, PWDN, CSI_GPIO_LOW);
		//usleep_range(1000, 1200);
		//cci_unlock(sd);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
	switch (val) {
	case 0:
		sensor_dbg("sensor_reset val: 0, do nothing!\n");
		// vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		// usleep_range(10000, 11000);
		break;
	case 1:
		sensor_dbg("sensor_reset val: 1, do nothing!\n");
		// vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		// usleep_range(10000, 11000);
		// vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		// usleep_range(10000, 11000);
		// vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		// usleep_range(10000, 11000);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	int ret, i = 0;
	unsigned int rdval = 0xffffffff;

	read_rk628(sd, 0x00000200, &rdval);

	while ((V4L2_IDENT_SENSOR != rdval)) {
		i++;
		ret = read_rk628(sd, 0x00000200, &rdval);
		if (i > 4) {
			sensor_print("warning: GRF_SOC_VERSION(0x%08x) is NOT equal to 0x%08x\n", rdval, V4L2_IDENT_SENSOR);
			break;
		}
	}
	if (rdval != V4L2_IDENT_SENSOR)
		return -ENODEV;

	sensor_print("read RK628F GRF_SOC_VERSION = 0x%08x\n", rdval);
	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	return sensor_detect(sd);
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	if (cmd != GET_CURRENT_WIN_CFG)
		return -EINVAL;
	mutex_lock(&csi->confctl_mutex);
	if (csi->nosignal)
		ret = csi->signal_error;
	else
		memcpy(arg, &csi->window, sizeof(csi->window));
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static struct sensor_format_struct sensor_formats[] = {
	{
		.desc		= "UYVY 4:2:2",
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.regs 		= sensor_default_regs,
		.regs_size	= ARRAY_SIZE(sensor_default_regs),
		.bpp		= 2,
	}
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */
static const struct v4l2_dv_timings rk628_modes[] = {
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080P60,
	V4L2_DV_BT_CEA_3840X2160P30,
};

static int sensor_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
		struct v4l2_mbus_config *cfg)
{
	struct rk628_csi *csi = sd_to_csi(sd);

	if (pad)
		return -EINVAL;
	memset(cfg, 0, sizeof(*cfg));
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	/* The Allwinner receiver uses its private lane/channel masks here. */
	cfg->bus.mipi_csi2.num_data_lanes =
		V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNEL_0;
	if (!csi->continues_clk)
		cfg->bus.mipi_csi2.flags = V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
	return 0;
}

static int sensor_s_dv_timings(struct v4l2_subdev *sd,
			       struct v4l2_dv_timings *timings)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	if (!rk628_timing_fps(timings))
		return -ERANGE;
	mutex_lock(&csi->confctl_mutex);
	if (csi->nosignal)
		ret = csi->signal_error;
	else if (csi->is_streaming)
		ret = -EBUSY;
	else if (!rk628_timings_match(&csi->timings, timings))
		ret = -EINVAL;
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static int sensor_g_dv_timings(struct v4l2_subdev *sd,
			       struct v4l2_dv_timings *timings)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	mutex_lock(&csi->confctl_mutex);
	if (csi->nosignal)
		ret = csi->signal_error;
	else
		*timings = csi->timings;
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static int sensor_query_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_dv_timings *timings)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret;

	mutex_lock(&csi->confctl_mutex);
	if (csi->stopping) {
		ret = -ENODEV;
	} else if (!tx_5v_power_present(sd)) {
		ret = -ENOLINK;
	} else if (csi->nosignal) {
		ret = csi->signal_error;
	} else {
		ret = rk628_csi_get_detected_timings(sd, timings);
		if (!ret && !rk628_timings_match(&csi->timings, timings))
			ret = -ENOLCK;
		if (ret) {
			rk628_signal_lost(sd, ret == -ERANGE ? -ERANGE : -ENOLCK);
			rk628_queue_hotplug(csi, msecs_to_jiffies(50));
		}
	}
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static int sensor_enum_dv_timings(struct v4l2_subdev *sd,
				  struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad || timings->index >= ARRAY_SIZE(rk628_modes))
		return -EINVAL;
	timings->timings = rk628_modes[timings->index];
	return 0;
}

static int sensor_dv_timings_cap(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad)
		return -EINVAL;
	*cap = rk628_csi_timings_cap;
	return 0;
}

static void rk628_fill_format(struct rk628_csi *csi,
			      struct v4l2_mbus_framefmt *fmt)
{
	struct mbus_framefmt_res *res;

	fmt->width = csi->window.width;
	fmt->height = csi->window.height;
	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_REC709;
	res = (void *)fmt->reserved;
	res->res_mipi_bps = csi->window.mipi_bps;
	res->res_combo_mode = csi->info.combo_mode;
	res->res_time_hs = csi->info.time_hs;
	res->fps = csi->window.fps_fixed;
	res->res_wdr_mode = 0;
	res->res_lp_mode = 0;
	res->res_deskew = csi->rx_clock_deskew[csi->window.mipi_bps > 1000000000U];
	res->pclk_dly = 0;
}

static int rk628_get_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state, struct v4l2_subdev_format *fmt)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	if (fmt->pad)
		return -EINVAL;
	mutex_lock(&csi->confctl_mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY && state && state->pads) {
		fmt->format = state->pads->try_fmt;
	} else if (csi->nosignal) {
		ret = csi->signal_error;
	} else {
		memset(&fmt->format, 0, sizeof(fmt->format));
		rk628_fill_format(csi, &fmt->format);
	}
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static int rk628_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state, struct v4l2_subdev_format *fmt)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	int ret = 0;

	if (fmt->pad)
		return -EINVAL;
	mutex_lock(&csi->confctl_mutex);
	if (csi->nosignal) {
		ret = csi->signal_error;
	} else {
		rk628_fill_format(csi, &fmt->format);
		if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
			if (state && state->pads)
				state->pads->try_fmt = fmt->format;
		} else if (csi->is_streaming) {
			ret = -EBUSY;
		} else {
			csi->fmt_valid = true;
		}
	}
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static int rk628_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad || fse->index >= ARRAY_SIZE(rk628_modes))
		return -EINVAL;
	fse->min_width = rk628_modes[fse->index].bt.width;
	fse->max_width = fse->min_width;
	fse->min_height = rk628_modes[fse->index].bt.height;
	fse->max_height = fse->min_height;
	return 0;
}

static int sensor_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		sensor_dbg("sensor subscribe event -- V4L2_EVENT_SOURCE_CHANGE\n");
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		sensor_dbg("sensor subscribe event -- V4L2_EVENT_CTRL\n");
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	case RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return -EINVAL;
	}
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rk628_csi *csi = sd_to_csi(sd);
	struct v4l2_dv_timings actual;
	int ret = 0;

	mutex_lock(&csi->confctl_mutex);
	if (enable) {
		if (csi->stopping) {
			ret = -ENODEV;
			goto out;
		}
		if (!tx_5v_power_present(sd)) {
			ret = -ENOLINK;
			goto out;
		}
		if (csi->nosignal) {
			ret = csi->signal_error;
			goto out;
		}
		if (!csi->fmt_valid) {
			ret = -EPIPE;
			goto out;
		}
		ret = rk628_csi_get_detected_timings(sd, &actual);
		if (!ret && !rk628_timings_match(&actual, &csi->timings))
			ret = -EPIPE;
		if (ret) {
			rk628_signal_lost(sd, ret == -ERANGE ? -ERANGE : -ENOLCK);
			rk628_queue_hotplug(csi, msecs_to_jiffies(50));
			goto out;
		}
	}
	ret = enable_stream(sd, enable);
	if (enable && ret) {
		rk628_signal_lost(sd, ret == -ERANGE ? -ERANGE : -ENOLCK);
		rk628_queue_hotplug(csi, msecs_to_jiffies(100));
	}
out:
	mutex_unlock(&csi->confctl_mutex);
	return ret;
}

static const struct v4l2_ctrl_config rk628_csi_ctrl_audio_sampling_rate = {
	.id = RK_V4L2_CID_AUDIO_SAMPLING_RATE,
	.name = "Audio sampling rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 768000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config rk628_csi_ctrl_audio_present = {
	.id = RK_V4L2_CID_AUDIO_PRESENT,
	.name = "Audio present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.reset = sensor_reset,
	.init = sensor_init,
	.s_power = sensor_power,
	.subscribe_event = sensor_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = sensor_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
	.compat_ioctl32 = sensor_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.s_dv_timings = sensor_s_dv_timings,
	.g_dv_timings = sensor_g_dv_timings,
	.query_dv_timings = sensor_query_dv_timings,
	.s_stream = sensor_s_stream,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_dv_timings = sensor_enum_dv_timings,
	.dv_timings_cap = sensor_dv_timings_cap,
	.enum_mbus_code = sensor_enum_mbus_code,
	.enum_frame_size = rk628_enum_frame_size,
	.get_fmt = rk628_get_fmt,
	.set_fmt = rk628_set_fmt,
	.get_edid = sensor_get_edid,
	.set_edid = sensor_set_edid,
	.get_mbus_config = sensor_g_mbus_config,
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.pad = &sensor_pad_ops,
};

/* ----------------------------------------------------------------------- */
static struct cci_driver cci_drv = {
	.name = SENSOR_NAME,
	.addr_width = CCI_BITS_8,
	.data_width = CCI_BITS_8,
};

static ssize_t get_det_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cci_driver *driver = dev_get_drvdata(dev);
	struct v4l2_subdev *sd = driver->sd;
	bool plugin = tx_5v_power_present(sd);

	sensor_dbg("%s, online: %d\n", __func__, plugin);
	return sprintf(buf, "0x%x\n", plugin);
}

static struct device_attribute detect_dev_attrs = {
	.attr = {
		.name = "online",
		.mode = S_IRUGO,
	},
	.show = get_det_status_show,
	.store = NULL,
};

static int rk628_csi_gpio_init(struct rk628_csi *csi)
{
	struct device_node *np = of_find_node_by_name(NULL, "sensor_detect");
	struct fwnode_handle *fwnode;
	u32 value;
	int ret = 0;

	if (!np)
		return -EINVAL;
	if (of_find_property(np, "allwinner,rx-clock-deskew", NULL)) {
		if (of_property_count_u32_elems(np, "allwinner,rx-clock-deskew") != 2 ||
		    of_property_read_u32_array(np, "allwinner,rx-clock-deskew",
					       csi->rx_clock_deskew, 2) ||
		    !csi->rx_clock_deskew[0] || csi->rx_clock_deskew[0] > 31 ||
		    !csi->rx_clock_deskew[1] || csi->rx_clock_deskew[1] > 31) {
			ret = -EINVAL;
			goto out;
		}
	}
	fwnode = of_fwnode_handle(np);
	csi->reset_gpio = devm_fwnode_gpiod_get_index(csi->dev, fwnode,
						      "reset", 0, GPIOD_OUT_HIGH, "rk628-reset");
	if (IS_ERR(csi->reset_gpio)) {
		ret = PTR_ERR(csi->reset_gpio);
		goto out;
	}
	csi->power_gpio = devm_fwnode_gpiod_get_index(csi->dev, fwnode,
						      "power", 0, GPIOD_OUT_LOW, "rk628-power");
	if (IS_ERR(csi->power_gpio)) {
		ret = PTR_ERR(csi->power_gpio);
		if (ret != -ENOENT)
			goto out;
		csi->power_gpio = NULL;
		ret = 0;
	}
	csi->plugin_det_gpio = devm_fwnode_gpiod_get_index(csi->dev, fwnode,
							   "hotplug", 0, GPIOD_IN, "rk628-hotplug");
	if (IS_ERR(csi->plugin_det_gpio)) {
		ret = PTR_ERR(csi->plugin_det_gpio);
		goto out;
	}
	csi->int_det_gpio = devm_fwnode_gpiod_get_index(csi->dev, fwnode,
							"interrupt", 0, GPIOD_IN,
							"rk628-interrupt");
	if (IS_ERR(csi->int_det_gpio)) {
		ret = PTR_ERR(csi->int_det_gpio);
		goto out;
	}
	value = 1;
	of_property_read_u32(np, "continues-clk", &value);
	if (value > 1) {
		ret = -EINVAL;
		goto out;
	}
	csi->continues_clk = !!value;
	value = 0;
	of_property_read_u32(np, "hpd-output-inverted", &value);
	if (value > 1) {
		ret = -EINVAL;
		goto out;
	}
	csi->hpd_output_inverted = !!value;
	csi->irq_trigger = IRQ_TYPE_LEVEL_HIGH;
	of_property_read_u32(np, "irq-trigger-type", &csi->irq_trigger);
	if (csi->irq_trigger != IRQ_TYPE_LEVEL_HIGH &&
	    csi->irq_trigger != IRQ_TYPE_LEVEL_LOW &&
	    csi->irq_trigger != IRQ_TYPE_EDGE_RISING &&
	    csi->irq_trigger != IRQ_TYPE_EDGE_FALLING)
		ret = -EINVAL;
out:
	of_node_put(np);
	return ret;
}

static void rk628_release(struct rk628_csi *csi)
{
	rk628_stop(csi);
	if (csi->plugin_irq_requested) {
		devm_free_irq(csi->dev, csi->plugin_irq, csi);
		csi->plugin_irq_requested = false;
	}
	if (csi->hdmirx_irq_requested) {
		devm_free_irq(csi->dev, csi->hdmirx_irq, csi);
		csi->hdmirx_irq_requested = false;
	}
	debugfs_remove_recursive(csi->rk628->debug_dir);
	if (csi->audio_info)
		rk628_hdmirx_audio_destroy(csi->audio_info);
	if (csi->sysfs_registered)
		device_remove_file(&cci_drv.cci_device, &detect_dev_attrs);
	if (csi->cci_registered) {
		cci_dev_remove_helper(csi->i2c_client, &cci_drv);
		media_entity_cleanup(&csi->info.sd.entity);
	}
	v4l2_ctrl_handler_free(&csi->hdl);
	mutex_destroy(&csi->info.lock);
	mutex_destroy(&csi->confctl_mutex);
}

static int sensor_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct rk628_csi *csi;
	struct sensor_info *info;
	struct v4l2_subdev *sd;
	int ret;

	csi = devm_kzalloc(dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;
	csi->dev = dev;
	csi->i2c_client = client;
	csi->rk628 = rk628_i2c_register(client);
	if (!csi->rk628)
		return -ENOMEM;
	ret = rk628_csi_gpio_init(csi);
	if (ret)
		return dev_err_probe(dev, ret, "GPIO configuration failed\n");
	csi->rk628->hdmirx_det_gpio = csi->plugin_det_gpio;
	csi->csi_lanes_in_use = 4;
	csi->nosignal = true;
	csi->signal_error = -ENOLCK;
	info = &csi->info;
	sd = &info->sd;
	mutex_init(&csi->confctl_mutex);
	mutex_init(&info->lock);
	INIT_DELAYED_WORK(&csi->delayed_work_enable_hotplug, rk628_csi_delayed_work_enable_hotplug);
	INIT_DELAYED_WORK(&csi->delayed_work_res_change, rk628_delayed_work_res_change);
	INIT_WORK(&csi->work_isr, rk628_work_isr);
	v4l2_ctrl_handler_init(&csi->hdl, 5);
	info->fmt = &sensor_formats[0];
	info->fmt_pt = sensor_formats;
	info->fmt_num = N_FMTS;
	info->win_pt = &csi->window;
	info->current_wins = &csi->window;
	info->win_size_num = 1;
	info->enum_scale_range = true;
	info->combo_mode = CMB_TERMINAL_RES | CMB_PHYA_OFFSET2 | MIPI_NORMAL_MODE;
	info->time_hs = 0x30;
	info->stream_seq = MIPI_BEFORE_SENSOR;
	info->not_detect_use_count = 1;

#if IS_ENABLED(CONFIG_SAME_I2C)
	info->sensor_i2c_addr = client->addr;

#endif
	info->tpf.numerator = 1;
	info->tpf.denominator = 30;
	cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv);
	csi->cci_registered = true;
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
	rk628_csi_power_on(csi);
	ret = sensor_detect(sd);
	if (ret)
		goto fail;
	rk628_cru_initialize(csi->rk628);
	rk628_version_parse(csi->rk628);
	if (!rk628_txphy_register(csi->rk628)) {
		ret = -ENOMEM;
		goto fail;
	}
	csi->link_freq = v4l2_ctrl_new_int_menu(&csi->hdl, NULL,
		V4L2_CID_LINK_FREQ, ARRAY_SIZE(link_freq_menu_items) - 1, 0,
		link_freq_menu_items);
	csi->pixel_rate = v4l2_ctrl_new_std(&csi->hdl, NULL,
		V4L2_CID_PIXEL_RATE, 0, 300000000, 1, 297000000);
	csi->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&csi->hdl, NULL,
						   V4L2_CID_DV_RX_POWER_PRESENT, 0, 1, 0, 0);
	csi->audio_sampling_rate_ctrl = v4l2_ctrl_new_custom(&csi->hdl,
		&rk628_csi_ctrl_audio_sampling_rate, NULL);
	csi->audio_present_ctrl = v4l2_ctrl_new_custom(&csi->hdl,
		&rk628_csi_ctrl_audio_present, NULL);
	sd->ctrl_handler = &csi->hdl;
	if (csi->hdl.error) {
		ret = csi->hdl.error;
		goto fail;
	}
	csi->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	csi->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	csi->audio_info = rk628_hdmirx_audioinfo_alloc(dev, &csi->confctl_mutex,
						       csi->rk628, false);
	if (!csi->audio_info) {
		ret = -ENOMEM;
		goto fail;
	}
	mutex_lock(&csi->confctl_mutex);
	ret = rk628_csi_initial_setup(sd);
	mutex_unlock(&csi->confctl_mutex);
	if (ret)
		goto fail;
	csi->hdmirx_irq = gpiod_to_irq(csi->int_det_gpio);
	csi->plugin_irq = gpiod_to_irq(csi->plugin_det_gpio);
	if (csi->hdmirx_irq < 0 || csi->plugin_irq < 0) {
		ret = csi->hdmirx_irq < 0 ? csi->hdmirx_irq : csi->plugin_irq;
		goto fail;
	}
	ret = devm_request_threaded_irq(dev, csi->hdmirx_irq, NULL,
					rk628_csi_irq_handler,
					csi->irq_trigger | IRQF_ONESHOT,
					SENSOR_NAME, csi);
	if (ret)
		goto fail;
	csi->hdmirx_irq_requested = true;
	ret = devm_request_threaded_irq(dev, csi->plugin_irq, NULL,
					plugin_detect_irq,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT, "rk628-hotplug", csi);
	if (ret)
		goto fail;
	csi->plugin_irq_requested = true;
	ret = device_create_file(&cci_drv.cci_device, &detect_dev_attrs);
	if (ret)
		goto fail;
	csi->sysfs_registered = true;
	rk628_debugfs_create(csi->rk628);
	rk628_queue_hotplug(csi, msecs_to_jiffies(100));
	return 0;
fail:
	rk628_release(csi);
	return ret;
}

static void sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	rk628_release(sd_to_csi(sd));
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = SENSOR_NAME,
		   .pm = &sensor_runtime_pm_ops,
		   },
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};

static __init int init_sensor(void)
{
	return cci_dev_init_helper(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

VIN_INIT_DRIVERS(init_sensor);
module_exit(exit_sensor);
