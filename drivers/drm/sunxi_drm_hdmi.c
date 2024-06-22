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

#include <linux/delay.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/component.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_probe_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
#include <drm/drm_scdc_helper.h>
#else
#include <drm/display/drm_scdc_helper.h>
#endif

#include <sunxi-gpio.h>
#include <uapi/drm/drm_fourcc.h>
#include <video/sunxi_metadata.h>

#if IS_ENABLED(CONFIG_EXTCON)
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include "../drivers/extcon/extcon.h"
#endif

#include <media/cec.h>
#include <media/cec-notifier.h>

#include "sunxi_device/sunxi_hdmi.h"
#include "sunxi_device/sunxi_tcon.h"

#include "sunxi_drm_intf.h"
#include "sunxi_drm_crtc.h"
#include "sunxi_drm_hdmi.h"

#if IS_ENABLED(CONFIG_AW_SMC)
extern int sunxi_smc_refresh_hdcp(void);
#endif

#define SUNXI_HDMI_EDID_LENGTH		(1024)
#define SUNXI_HDMI_POWER_CNT	     4
#define SUNXI_HDMI_POWER_NAME	     40

#define SUNXI_HDMI_HPD_FORCE_IN		(0x11)
#define SUNXI_HDMI_HPD_FORCE_OUT	(0x10)
#define SUNXI_HDMI_HPD_MASK_NOTIFY	(0x100)
#define SUNXI_HDMI_HPD_MASK_DETECT	(0x1000)

static char *sunxi_hdmi_color_string[] = {
	"RGB888_8BITS",  "YUV444_8BITS",  "YUV422_8BITS",  "YUV420_8BITS",
	"RGB888_10BITS", "YUV444_10BITS", "YUV422_10BITS", "YUV420_10BITS",
	"RGB888_12BITS", "YUV444_12BITS", "YUV422_12BITS", "YUV420_12BITS",
	"RGB888_16BITS", "YUV444_16BITS", "YUV422_16BITS", "YUV420_16BITS",
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_tmds_mode[] = {
	{ DISP_DVI_HDMI_UNDEFINED,  "default"   },
	{ DISP_DVI,                 "dvi_mode"  },
	{ DISP_HDMI,                "hdmi_mode" },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_color_space[] = {
	{ DISP_BT709,    "BT709"     },
	{ DISP_BT601,    "BT601"     },
	{ DISP_BT2020NC, "BT2020NC" },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_eotf[] = {
	{ DISP_EOTF_GAMMA22,      "SDR"   },
	{ DISP_EOTF_SMPTE2084,    "HDR10" },
	{ DISP_EOTF_ARIB_STD_B67, "HLG"   },
};

#if IS_ENABLED(CONFIG_EXTCON)
static const unsigned int sunxi_hdmi_cable[] = {
	EXTCON_DISP_HDMI,
	EXTCON_NONE,
};
#endif

enum sunxi_hdmi_reg_bank_e {
	SUNXI_HDMI_REG_BANK_CTRL    = 0,
	SUNXI_HDMI_REG_BANK_PHY     = 1,
	SUNXI_HDMI_REG_BANK_SCDC    = 2,
	SUNXI_HDMI_REG_BANK_HPI     = 3
};

enum sunxi_hdmi_log_index_e {
	LOG_INDEX_NUL   = DW_LOG_INDEX_NUL,
	LOG_INDEX_VIDEO = DW_LOG_INDEX_VIDEO,
	LOG_INDEX_AUDIO = DW_LOG_INDEX_AUDIO,
	LOG_INDEX_EDID  = DW_LOG_INDEX_EDID,
	LOG_INDEX_HDCP  = DW_LOG_INDEX_HDCP,
	LOG_INDEX_CEC   = DW_LOG_INDEX_CEC,
	LOG_INDEX_PHY   = DW_LOG_INDEX_PHY,
	LOG_INDEX_TRACE = DW_LOG_INDEX_TRACE,
	LOG_INDEX_MAX
};

struct sunxi_hdmi_res_s {
	struct clk  *clk_hdmi;
	struct clk  *clk_hdmi_24M;
	struct clk  *clk_hdmi_bus;
	struct clk  *clk_hdmi_ddc;
	struct clk  *clk_tcon_tv;
	struct clk  *clk_hdcp;
	struct clk  *clk_hdcp_bus;
	struct clk  *clk_cec;
	struct clk  *clk_cec_parent;
	struct reset_control  *rst_bus_sub;
	struct reset_control  *rst_bus_main;
	struct reset_control  *rst_bus_hdcp;

	char  power_name[SUNXI_HDMI_POWER_CNT][SUNXI_HDMI_POWER_NAME];
	struct regulator  *hdmi_regu[SUNXI_HDMI_POWER_CNT];
};

struct sunxi_hdmi_state_s {
	int drm_enable;
	int drm_mode_set;
	int drm_hpd_force;

	int drv_clock;
	int drv_enable;
	int drv_hpd_state;
	int drv_hpd_mask;

	/* dts control value */
	unsigned int drv_dts_cec;
	unsigned int drv_dts_hdcp1x;
	unsigned int drv_dts_hdcp2x;
	unsigned int drv_dts_power_cnt;

	/* hdcp control state */
	int drv_hdcp_state;
	int drv_hdcp_enable;
	int drv_hdcp_support;
	sunxi_hdcp_type_t drv_hdcp_type;

	int drv_pm_state; /* 1: suspend. 0:resume*/
	u32 drv_color_cap;
	u32 drv_color_mode;

	int drv_reg_bank;
	enum disp_data_bits   drv_max_bits;

	/* edid control */
	u8	drv_edid_dbg_mode;
	u8	drv_edid_dbg_data[SUNXI_HDMI_EDID_LENGTH];
	u8	drv_edid_dbg_size;
	struct mutex	drv_edid_lock;
	struct edid    *drv_edid_data;
};

struct sunxi_hdmi_cec_s {
	struct cec_notifier		*notify;
	struct cec_adapter		*adap;
	struct cec_msg          rx_msg;

	int    irq;
	bool   tx_done;
	bool   rx_done;
	u8     tx_status;
	u8     enable;
	u16    logic_addr;
};

struct sunxi_drm_hdmi {
	/* drm related members */
	struct platform_device   *pdev;
	struct drm_device        *drm;
	struct drm_encoder        encoder;
	struct drm_connector      connector;
	struct drm_display_mode   drm_mode;
	struct drm_display_mode   drm_mode_adjust;
	struct i2c_adapter        i2c_adap;
	struct device            *tcon_dev;
	unsigned int              tcon_id;
	dev_t                     hdmi_devid;
	struct class             *hdmi_class;
	struct device            *hdmi_dev;

	/* drm property */
	struct drm_property		*prop_color_space;
	struct drm_property		*prop_tmds_mode;
	struct drm_property		*prop_eotf;
	struct drm_property		*prop_color_cap;
	struct drm_property		*prop_color_mode;

	int hdmi_irq;

	/* hdmi hpd work */
	struct task_struct   *hpd_task;
#if IS_ENABLED(CONFIG_EXTCON)
	struct extcon_dev    *hpd_extcon;
#endif

	struct sunxi_hdmi_cec_s        hdmi_cec;

	struct sunxi_hdmi_state_s      hdmi_ctrl;

	struct sunxi_hdmi_res_s        hdmi_res;

	struct disp_device_config      disp_config;
	struct disp_video_timings      disp_timing;

	/* suxni hdmi core */
	struct sunxi_hdmi_s            hdmi_core;
};

static const struct drm_display_mode _sunxi_hdmi_default_modes[] = {
	/* 4 - 1280x720@60Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 16 - 1920x1080@60Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 31 - 1920x1080@50Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 19 - 1280x720@50Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 17 - 720x576@50Hz 4:3 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 2 - 720x480@60Hz 4:3 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
};
/*******************************************************************************
 * drm sunxi hdmi encoder and connector container_of
 ******************************************************************************/
static inline struct sunxi_drm_hdmi *
drm_connector_to_sunxi_drm_hdmi(struct drm_connector *connector)
{
	return container_of(connector, struct sunxi_drm_hdmi, connector);
}

static inline struct sunxi_drm_hdmi *
drm_encoder_to_sunxi_drm_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sunxi_drm_hdmi, encoder);
}

/*******************************************************************************
 * sunxi hdmi driver hdcp function
 ******************************************************************************/
static int _sunxi_drv_hdcp_loading_fw(struct device *dev)
{
	char fw_name[36] = "esm.fex";
	const struct firmware *fw;
	int ret = 0;

	ret = request_firmware_direct(&fw, (const char *)fw_name, dev);
	if (ret < 0) {
		ret = 0;
		goto exit;
	}

	if (fw->size && fw->data) {
		ret = sunxi_hdcp2x_fw_loading(fw->data, fw->size);
		if (ret == 0) {
			/* esm fw loading success */
			ret = fw->size;
			goto exit;
		}
	}
	ret = 0;

exit:
	release_firmware(fw);
	return ret;
}

static int _sunxi_drv_hdcp_get_state(struct sunxi_drm_hdmi *hdmi)
{
	int ret = SUNXI_HDCP_DISABLE;

	if (hdmi->hdmi_ctrl.drv_hdcp_enable)
		ret = sunxi_hdcp_get_state();

	return ret;
}

static void _sunxi_drv_hdcp_state_polling(struct sunxi_drm_hdmi *hdmi)
{
	const char *state[] = {"disable", "processing", "failed", "success"};
	int ret = SUNXI_HDCP_DISABLE;

	/* check hpd plugin */
	if (!hdmi->hdmi_ctrl.drv_hpd_state)
		return;

	ret = _sunxi_drv_hdcp_get_state(hdmi);

	/* check hdcp state change */
	if (hdmi->hdmi_ctrl.drv_hdcp_state == ret)
		return;

	hdmi_inf("hdmi drv check hdcp state: [%s] -> [%s]\n",
		state[hdmi->hdmi_ctrl.drv_hdcp_state], state[ret]);
	hdmi->hdmi_ctrl.drv_hdcp_state = ret;
}

static void _sunxi_drv_hdcp_update_support(struct sunxi_drm_hdmi *hdmi)
{
	u8 ret = 0;

	/* check tx dts state */
	if (hdmi->hdmi_ctrl.drv_dts_hdcp1x && sunxi_hdcp1x_get_sink_cap())
		ret |= BIT(SUNXI_HDCP_TYPE_HDCP14);

	if (hdmi->hdmi_ctrl.drv_dts_hdcp2x &&
			sunxi_hdcp2x_get_sink_cap() &&
			sunxi_hdcp2x_fw_state())
		ret |= BIT(SUNXI_HDCP_TYPE_HDCP22);

	hdmi->hdmi_ctrl.drv_hdcp_support = ret;
}

static void _sunxi_drv_hdcp_release(struct sunxi_drm_hdmi *hdmi)
{
	hdmi->hdmi_ctrl.drv_hdcp_support = 0x0;
	hdmi->hdmi_ctrl.drv_hdcp_type = SUNXI_HDCP_TYPE_NULL;
}

static void _sunxi_drv_hdcp_disable(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	switch (hdmi->hdmi_ctrl.drv_hdcp_type) {
	case SUNXI_HDCP_TYPE_HDCP22:
		ret = sunxi_hdcp2x_config(0x0);
		hdmi_inf("hdmi drv disable hdcp2x %s\n", ret != 0 ? "failed" : "success");
		break;
	case SUNXI_HDCP_TYPE_HDCP14:
		ret = sunxi_hdcp1x_config(0x0);
		hdmi_inf("hdmi drv disable hdcp1x %s\n", ret != 0 ? "failed" : "success");
		break;
	case SUNXI_HDCP_TYPE_NULL:
	default:
		break;
	}

	_sunxi_drv_hdcp_release(hdmi);
}

/**
 * @desc: sunxi enable hdcp
 * if HDCP2x is supported, HDCP2x is used first.
 * otherwise HDCP1x is used.
 */
static int _sunxi_drv_hdcp_enable(struct sunxi_drm_hdmi *hdmi)
{
	u8 ret = 0, cap = 0;

	cap = hdmi->hdmi_ctrl.drv_hdcp_support;
	if (cap == 0) {
		_sunxi_drv_hdcp_release(hdmi);
		hdmi_inf("hdmi check unsupport hdcp\n");
		return 0;
	}

	if (hdmi->hdmi_ctrl.drv_hdcp_type != SUNXI_HDCP_TYPE_NULL)
		return 0;

	/* check and config hdcp2x */
	if (cap & BIT(SUNXI_HDCP_TYPE_HDCP22)) {
		ret = sunxi_hdcp2x_config(0x1);
		if (ret != 0x0) {
			if (cap & BIT(SUNXI_HDCP_TYPE_HDCP14)) {
				hdmi_inf("hdmi drv set hdcp2x failed and auto change to hdcp1x\n");
				goto config_hdcp1x;
			}
			hdmi_inf("hdmi drv only set hdcp2x but failed\n");
			return -1;
		}

		hdmi->hdmi_ctrl.drv_hdcp_type = SUNXI_HDCP_TYPE_HDCP22;
		hdmi_inf("hdmi drv enable hdcp22 done\n");
		return 0;
	}

config_hdcp1x:
	/* check and config hdcp1x */
	if (cap & BIT(SUNXI_HDCP_TYPE_HDCP14)) {
		ret = sunxi_hdcp1x_config(0x1);
		if (ret != 0) {
			hdmi_err("hdmi drv enable hdcp14 failed\n");
			return -1;
		}
		hdmi->hdmi_ctrl.drv_hdcp_type = SUNXI_HDCP_TYPE_HDCP14;
		hdmi_inf("hdmi drv enable hdcp14 done\n");
		return 0;
	}

	hdmi_inf("hdmi drv not-set hdcp\n");
	return 0;
}

#if IS_ENABLED(CONFIG_AW_DRM_HDMI20)
/*******************************************************************************
 * sunxi hdmi snd function
 ******************************************************************************/
/**
 * @desc: sound hdmi audio enable
 * @enable: 1 - enable hdmi audio
 *          0 - disable hdmi audio
 * @channel:
 * @return: 0 - success
 *         -1 - failed
 */
static s32 _sunxi_drv_audio_enable(u8 enable, u8 channel)
{
	int ret = 0;

	if (enable)
		ret = sunxi_hdmi_audio_enable();

	hdmi_inf("sunxi hdmi audio %s %s\n",
		enable ? "enable" : "disable", ret ? "failed" : "success");
	return 0;
}

/**
 * @desc: sound hdmi audio param config
 * @audio_para: audio params
 * @return: 0 - success
 *         -1 - failed
 */
static s32 _sunxi_drv_audio_set_info(hdmi_audio_t *audio_para)
{
	int ret = 0;

	ret = sunxi_hdmi_audio_set_info(audio_para);
	hdmi_inf("sunxi hdmi audio update params %s\n", ret ? "failed" : "success");

	return 0;
}

int snd_hdmi_get_func(__audio_hdmi_func *hdmi_func)
{
	if (!hdmi_func) {
		hdmi_err("point hdmi_func is null\n");
		return -1;
	}

	hdmi_func->hdmi_audio_enable   = _sunxi_drv_audio_enable;
	hdmi_func->hdmi_set_audio_para = _sunxi_drv_audio_set_info;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdmi_get_func);
#endif

static int _sunxi_drv_hdmi_regulator_on(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0, loop = 0;

	for (loop = 0; loop < hdmi->hdmi_ctrl.drv_dts_power_cnt; loop++) {
		if (!hdmi->hdmi_res.hdmi_regu[loop])
			continue;

		ret = regulator_enable(hdmi->hdmi_res.hdmi_regu[loop]);
		hdmi_inf("hdmi drv enable regulator %s %s\n",
			hdmi->hdmi_res.power_name[loop], ret != 0 ? "failed" : "success");
	}
	return 0;
}

static int _sunxi_drv_hdmi_regulator_off(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0, loop = 0;

	for (loop = 0; loop < hdmi->hdmi_ctrl.drv_dts_power_cnt; loop++) {
		if (!hdmi->hdmi_res.hdmi_regu[loop])
			continue;
		ret = regulator_disable(hdmi->hdmi_res.hdmi_regu[loop]);
		hdmi_inf("hdmi drv disable regulator %s %s\n",
			hdmi->hdmi_res.power_name[loop], ret != 0 ? "failed" : "success");
	}
	return 0;
}

static int _sunxi_drv_hdmi_clock_on(struct sunxi_drm_hdmi *hdmi)
{
	struct sunxi_hdmi_res_s *pclk = &hdmi->hdmi_res;

	if (pclk->rst_bus_sub)
		reset_control_deassert(pclk->rst_bus_sub);

	if (pclk->rst_bus_main)
		reset_control_deassert(pclk->rst_bus_main);

	if (pclk->clk_hdmi)
		clk_prepare_enable(pclk->clk_hdmi);

	if (pclk->clk_hdmi_24M)
		clk_prepare_enable(pclk->clk_hdmi_24M);

	/* enable ddc clock */
	if (pclk->clk_hdmi_ddc)
		clk_prepare_enable(pclk->clk_hdmi_ddc);

	/* enable hdcp clock */
	if (pclk->rst_bus_hdcp)
		reset_control_deassert(pclk->rst_bus_hdcp);

	if (pclk->clk_hdcp_bus)
		clk_prepare_enable(pclk->clk_hdcp_bus);

	if (pclk->clk_hdcp) {
		clk_set_rate(pclk->clk_hdcp, 300000000);
		clk_prepare_enable(pclk->clk_hdcp);
	}

	hdmi->hdmi_ctrl.drv_clock = 0x1;
	return 0;
}

static int _sunxi_drv_hdmi_clock_off(struct sunxi_drm_hdmi *hdmi)
{
	struct sunxi_hdmi_res_s *pclk = NULL;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}
	pclk = &hdmi->hdmi_res;

	/* disable hdcp clock */
	if (pclk->clk_hdcp)
		clk_disable_unprepare(pclk->clk_hdcp);

	if (pclk->clk_hdcp_bus)
		clk_disable_unprepare(pclk->clk_hdcp_bus);

	if (pclk->rst_bus_hdcp)
		reset_control_assert(pclk->rst_bus_hdcp);

	/* disable ddc clock */
	if (pclk->clk_hdmi_ddc)
		clk_disable_unprepare(pclk->clk_hdmi_ddc);

	/* disable hdmi clock */
	if (pclk->clk_hdmi_24M)
		clk_disable_unprepare(pclk->clk_hdmi_24M);

	if (pclk->clk_hdmi)
		clk_disable_unprepare(pclk->clk_hdmi);

	if (pclk->rst_bus_sub)
		reset_control_assert(pclk->rst_bus_sub);

	if (pclk->rst_bus_main)
		reset_control_assert(pclk->rst_bus_main);

	hdmi->hdmi_ctrl.drv_clock = 0x0;
	return 0;
}

static int _sunxi_drv_hdmi_read_edid(struct sunxi_drm_hdmi *hdmi)
{
	int ret = -1;

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return ret;
	}

	mutex_lock(&hdmi->hdmi_ctrl.drv_edid_lock);

	if (hdmi->hdmi_ctrl.drv_edid_data) {
		ret = 0;
		goto exit;
	}

	if (hdmi->hdmi_ctrl.drv_edid_dbg_mode) {
		hdmi_inf("hdmi drv use debug edid\n");
		hdmi->hdmi_ctrl.drv_edid_data = (struct edid *)&hdmi->hdmi_ctrl.drv_edid_dbg_data;
		goto edid_parse;
	}

	hdmi->hdmi_ctrl.drv_edid_data = NULL;
	hdmi->hdmi_ctrl.drv_edid_data = drm_get_edid(&hdmi->connector, &hdmi->i2c_adap);
	if (!hdmi->hdmi_ctrl.drv_edid_data) {
		hdmi_err("hdmi drv i2c read edid failed\n");
		hdmi->hdmi_ctrl.drv_edid_data = NULL;
		ret = -1;
		goto exit;
	}

edid_parse:
	sunxi_hdmi_edid_parse((u8 *)hdmi->hdmi_ctrl.drv_edid_data);
	ret = 0;

exit:
	mutex_unlock(&hdmi->hdmi_ctrl.drv_edid_lock);
	return ret;
}

static int
_sunxi_drv_hdmi_get_color_mode(struct sunxi_drm_hdmi *hdmi)
{
	struct disp_device_config *info = sunxi_hdmi_get_disp_info();
	u32 bit_map = info->format + (info->bits * 4);

	hdmi->hdmi_ctrl.drv_color_mode = BIT(bit_map);
	return 0;
}

static int
_sunxi_drv_hdmi_set_color_mode(struct sunxi_drm_hdmi *hdmi, uint64_t val)
{
	struct disp_device_config *info = &hdmi->disp_config;
	u32 map_bit = ffs(val) - 1;

	if (!(val & hdmi->hdmi_ctrl.drv_color_cap)) {
		hdmi_inf("hdmi drv check set color mode: %s unsupport.\n",
		sunxi_hdmi_color_string[map_bit]);
		return -1;
	}

	info->format = (enum disp_csc_type)(map_bit % 4);
	info->bits   = (enum disp_data_bits)(map_bit / 4);

	hdmi_inf("hdmi drv set color mode: %s-%s\n",
		sunxi_hdmi_color_format_string(info->format),
		sunxi_hdmi_color_depth_string(info->bits));
	return 0;
}

static int
_sunxi_drv_hdmi_set_color_capality(struct sunxi_drm_hdmi *hdmi)
{
	u32 pixel_clk = (u32)hdmi->drm_mode.clock;
	u32 vic = (u32)drm_match_cea_mode(&hdmi->drm_mode);
	u32 color_cap = sunxi_hdmi_get_color_capality(vic);
	int i = 0, ret = 0;
	enum disp_data_bits max_bits = hdmi->hdmi_ctrl.drv_max_bits;
	u8 format = 0, bits = 0;

	if (max_bits == DISP_DATA_8BITS)
		color_cap &= 0x000F;
	else if (max_bits == DISP_DATA_10BITS)
		color_cap &= 0x00FF;
	else if (max_bits == DISP_DATA_12BITS)
		color_cap &= 0x0FFF;
	else if (max_bits == DISP_DATA_16BITS)
		color_cap &= 0xFFFF;

	if (pixel_clk <= 1000)
		goto exit_none;

	for (i = 0; i < SUNXI_COLOR_MAX_MASK; i++) {
		if ((color_cap & BIT(i)) == 0)
			continue;

		format = i % 4;
		bits   = i / 4;
		ret = sunxi_hdmi_video_check_tmds_clock(format, bits, pixel_clk);
		if (ret == 0) {
			hdmi_inf("hdmi drv remove vic[%d] %s whne clock to max\n",
				vic, sunxi_hdmi_color_string[i]);
			color_cap &= ~BIT(i);
		}
	}

exit_none:
	hdmi->hdmi_ctrl.drv_color_cap = color_cap;
	return 0;
}

static int _sunxi_drv_hdmi_hpd_get(struct sunxi_drm_hdmi *hdmi)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null!!!\n");
		return 0;
	}

	return hdmi->hdmi_ctrl.drv_hpd_state ? 0x1 : 0x0;
}

static int _sunxi_drv_hdmi_hpd_set(struct sunxi_drm_hdmi *hdmi, u8 state)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null!!!\n");
		return -1;
	}

	hdmi->hdmi_ctrl.drv_hpd_state = state;

	if ((hdmi->hdmi_ctrl.drv_hpd_mask & SUNXI_HDMI_HPD_MASK_NOTIFY) ==
			SUNXI_HDMI_HPD_MASK_NOTIFY)
		return 0;

#if IS_ENABLED(CONFIG_EXTCON)
	if (hdmi->hpd_extcon)
		extcon_set_state_sync(hdmi->hpd_extcon, EXTCON_DISP_HDMI,
			state ? true : false);
#endif

	if (hdmi->drm)
		drm_helper_hpd_irq_event(hdmi->drm);

	return 0;
}

/*******************************************************************************
 * drm sunxi hdmi driver functions
 ******************************************************************************/
static int _sunxi_drv_hdmi_set_rate(struct sunxi_hdmi_res_s *p_clk)
{
	unsigned long clk_rate = 0;

	if (!p_clk) {
		hdmi_err("point p_clk is null\n");
		return -1;
	}

	clk_rate = clk_get_rate(p_clk->clk_tcon_tv);
	if (clk_rate == 0) {
		hdmi_err("tcon clock rate is 0");
		return -1;
	}

	clk_set_rate(p_clk->clk_hdmi, clk_rate);
	return 0;
}

static int _sunxi_drv_hdmi_enable(struct sunxi_drm_hdmi *hdmi)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	if (hdmi->hdmi_ctrl.drv_enable) {
		hdmi_inf("hdmi drv has been enable!\n");
		return 0;
	}

	_sunxi_drv_hdmi_set_rate(&hdmi->hdmi_res);

	/* hdmi driver video ops enable */
	sunxi_hdmi_config();

	if (hdmi->hdmi_ctrl.drv_hdcp_enable)
		_sunxi_drv_hdcp_enable(hdmi);

	hdmi->hdmi_ctrl.drv_enable = 0x1;
	hdmi_inf("hdmi drv enable output done\n");
	return 0;
}

static int _sunxi_drv_hdmi_disable(struct sunxi_drm_hdmi *hdmi)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null");
		return -1;
	}

	if (!hdmi->hdmi_ctrl.drv_enable) {
		hdmi_inf("hdmi drv has been disable!\n");
		return 0;
	}

	_sunxi_drv_hdcp_disable(hdmi);

	sunxi_hdmi_disconfig();

	hdmi->hdmi_ctrl.drv_enable = 0x0;
	hdmi_trace("hdmi drv disable done\n");
	return 0;
}

static bool _sunxi_drv_hdmi_fifo_check(void *data)
{
	struct sunxi_drm_hdmi *hdmi = (struct sunxi_drm_hdmi *)data;
	return sunxi_tcon_check_fifo_status(hdmi->tcon_dev);
}

static bool  _sunxi_drv_hdmi_is_sync_time_enough(void *data)
{
	struct sunxi_drm_hdmi *hdmi = (struct sunxi_drm_hdmi *)data;
	return sunxi_tcon_is_sync_time_enough(hdmi->tcon_dev);
}

static void _sunxi_drv_hdmi_vblank_enable(bool enable, void *data)
{
	struct sunxi_drm_hdmi *hdmi = (struct sunxi_drm_hdmi *)data;

	sunxi_tcon_enable_vblank(hdmi->tcon_dev, enable);
}

static int _sunxi_drv_hdmi_select_output(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;
	struct disp_device_config *config = NULL;
	u32 vic = (u32)drm_match_cea_mode(&hdmi->drm_mode_adjust);

	config = &hdmi->disp_config;

	/* if dvi mode, use base config info */
	if (config->dvi_hdmi == DISP_DVI) {
		config->format = DISP_CSC_TYPE_RGB;
		config->bits   = DISP_DATA_8BITS;
		config->eotf   = DISP_EOTF_GAMMA22; /* SDR */
		config->cs     = DISP_BT709;
		hdmi_inf("hdmi drv select dvi output\n");
		goto check_clock;
	}

	sunxi_hdmi_disp_select_eotf(config);

	sunxi_hdmi_disp_select_space(config, vic);

format_select:
	sunxi_hdmi_disp_select_format(config, vic);

check_clock:
	ret = sunxi_hdmi_video_check_tmds_clock(config->format,
			config->bits, hdmi->drm_mode_adjust.clock);
	if (ret == 0x0) {
		/* 1. Reduce color depth */
		if ((config->bits < DISP_DATA_16BITS) &&
				(config->bits != DISP_DATA_8BITS)) {
			config->bits--;
			hdmi_inf("hdmi drv auto download bits: %s\n",
				sunxi_hdmi_color_depth_string(config->bits));
			goto format_select;
		}
		if ((config->format < DISP_CSC_TYPE_YUV420) &&
				(config->format != DISP_CSC_TYPE_YUV420)) {
			config->format++;
			hdmi_inf("hdmi drv auto download format: %s\n",
				sunxi_hdmi_color_format_string(config->format));
			goto format_select;
		}
		hdmi_inf("hdmi drv select output failed when clock overflow\n");
		return -1;
	}

	config->range = (config->format == DISP_CSC_TYPE_RGB) ?
			DISP_COLOR_RANGE_0_255 : DISP_COLOR_RANGE_16_235;
    config->scan  = DISP_SCANINFO_NO_DATA;
    config->aspect_ratio = HDMI_ACTIVE_ASPECT_PICTURE;

	return 0;
}

int _sunxi_drv_hdmi_check_disp_info(struct sunxi_drm_hdmi *hdmi)
{
	struct disp_device_config *info = sunxi_hdmi_get_disp_info();
	struct disp_device_config *config = &hdmi->disp_config;
	int count = 0;

	if (info->format != config->format)
		count++;

	if (info->bits != config->bits)
		count++;

	if (info->dvi_hdmi != config->dvi_hdmi)
		count++;

	if (info->eotf != config->eotf)
		count++;

	if (info->cs != config->cs)
		count++;

	if (info->range != config->range)
		count++;

	if (info->scan != config->scan)
		count++;

	if (info->aspect_ratio != config->aspect_ratio)
		count++;

	return count;
}

static int _sunxi_drv_hdmi_hpd_plugin(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	hdmi_inf("hdmi drv detect hpd connect\n");

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	ret = _sunxi_drv_hdmi_read_edid(hdmi);
	if (ret != 0) {
		hdmi_err("hdmi drv plugin read edid failed\n");
	}

	_sunxi_drv_hdcp_update_support(hdmi);
	return 0;
}

static int _sunxi_drv_hdmi_hpd_plugout(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	hdmi_inf("hdmi drv detect hpd disconnect\n");

	cec_notifier_phys_addr_invalidate(hdmi->hdmi_cec.notify);

	hdmi->hdmi_ctrl.drv_edid_data = NULL;
	ret = _sunxi_drv_hdmi_disable(hdmi);
	if (ret != 0)
		hdmi_err("sunxi hdmi driver disable failed when plugout.\n");

	return 0;
}

/**
 * @return: 0 - normal
 *          1 - hpd force mode
 *          2 - hpd bypass mode
 */
static int _sunxi_drv_hdmi_check_hpd_mask(struct sunxi_drm_hdmi *hdmi)
{
	if ((hdmi->hdmi_ctrl.drv_hpd_mask & SUNXI_HDMI_HPD_FORCE_IN) ==
			SUNXI_HDMI_HPD_FORCE_IN) {
		if (!_sunxi_drv_hdmi_hpd_get(hdmi)) {
			_sunxi_drv_hdmi_hpd_set(hdmi, 0x1);
			hdmi_inf("hdmi drv force mode set plugin\n");
			return 1;
		} else
			return 2;
	} else if ((hdmi->hdmi_ctrl.drv_hpd_mask & SUNXI_HDMI_HPD_FORCE_OUT) ==
			SUNXI_HDMI_HPD_FORCE_OUT) {
		if (_sunxi_drv_hdmi_hpd_get(hdmi)) {
			_sunxi_drv_hdmi_hpd_set(hdmi, 0x0);
			hdmi_inf("hdmi drv force mode set plugout\n");
			return 1;
		} else
			return 2;
	} else if ((hdmi->hdmi_ctrl.drv_hpd_mask & SUNXI_HDMI_HPD_MASK_DETECT) ==
			SUNXI_HDMI_HPD_MASK_DETECT)
		return 2;
	else
		return 0;
}

static int _sunxi_drv_hdmi_thread(void *parg)
{
	int temp_hpd = 0, ret = 0;
	struct sunxi_drm_hdmi *hdmi = (struct sunxi_drm_hdmi *)parg;

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return -1;
	}

	while (1) {
		ret = kthread_should_stop();
		if (ret) {
			hdmi_wrn("hdmi drv hpd thread is stop!!!\n");
			break;
		}

		/* check software debug mode */
		ret = _sunxi_drv_hdmi_check_hpd_mask(hdmi);
		if (ret == 0x1) {
			temp_hpd = _sunxi_drv_hdmi_hpd_get(hdmi);
			goto handle_change;
		} else if (ret == 0x2)
			goto next_loop;

		/* check physical hpd state */
		temp_hpd = sunxi_hdmi_get_hpd();
		if (temp_hpd == _sunxi_drv_hdmi_hpd_get(hdmi))
			goto next_loop;

		mdelay(50);

		temp_hpd = sunxi_hdmi_get_hpd();
		if (temp_hpd == _sunxi_drv_hdmi_hpd_get(hdmi))
			goto next_loop;

handle_change:
		if (temp_hpd)
			_sunxi_drv_hdmi_hpd_plugin(hdmi);
		else
			_sunxi_drv_hdmi_hpd_plugout(hdmi);

		_sunxi_drv_hdmi_hpd_set(hdmi, temp_hpd);

next_loop:
		_sunxi_drv_hdcp_state_polling(hdmi);
		msleep(20);
	}

	return 0;
}

/*******************************************************************************
 * @desc: sunxi hdmi cec adapter function
 ******************************************************************************/
static int _sunxi_drv_cec_set_clock(struct sunxi_drm_hdmi *hdmi, u8 state)
{
	struct sunxi_hdmi_res_s *pclk = &hdmi->hdmi_res;
	int ret = -1;

	if (!pclk) {
		hdmi_err("point pclk is null\n");
		return -1;
	}

	if (!pclk->clk_cec) {
		hdmi_err("point cec clk is null\n");
		return -1;
	}

	if (pclk->clk_cec_parent)
		clk_set_parent(pclk->clk_cec, pclk->clk_cec_parent);

	if (state == SUNXI_HDMI_ENABLE) {
		ret = clk_prepare_enable(pclk->clk_cec);
		if (ret != 0) {
			hdmi_err("clock config enable cec clk failed\n");
			return -1;
		}
	} else {
		clk_disable_unprepare(pclk->clk_cec);
	}

	return 0;
}

static irqreturn_t _sunxi_drv_cec_hardirq(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);
	int len;

	u8 stat = sunxi_cec_get_irq_state();

	if (stat == SUNXI_CEC_IRQ_NULL)
		goto none_exit;

	if (stat & SUNXI_CEC_IRQ_ERR_INITIATOR) {
		hdmi->hdmi_cec.tx_status = CEC_TX_STATUS_ERROR;
		hdmi->hdmi_cec.tx_done   = true;
		goto wake_exit;
	} else if (stat & SUNXI_CEC_IRQ_DONE) {
		hdmi->hdmi_cec.tx_status = CEC_TX_STATUS_OK;
		hdmi->hdmi_cec.tx_done = true;
		goto wake_exit;
	} else if (stat & SUNXI_CEC_IRQ_NACK) {
		hdmi->hdmi_cec.tx_status = CEC_TX_STATUS_NACK;
		hdmi->hdmi_cec.tx_done = true;
		goto wake_exit;
	}

	if (stat & SUNXI_CEC_IRQ_EOM) {
		len = sunxi_cec_message_receive((u8 *)&hdmi->hdmi_cec.rx_msg.msg);
		if (len < 0)
			goto none_exit;

		hdmi->hdmi_cec.rx_msg.len = len;
		smp_wmb();
		hdmi->hdmi_cec.rx_done = true;
		goto wake_exit;
	}

none_exit:
	return IRQ_NONE;
wake_exit:
	return IRQ_WAKE_THREAD;
}

static irqreturn_t _sunxi_drv_cec_thread(int irq, void *data)
{
	struct cec_adapter *adap    = data;
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);

	if (hdmi->hdmi_cec.tx_done) {
		hdmi->hdmi_cec.tx_done = false;
		cec_transmit_attempt_done(adap, hdmi->hdmi_cec.tx_status);
	}

	if (hdmi->hdmi_cec.rx_done) {
		hdmi->hdmi_cec.rx_done = false;
		smp_rmb();
		cec_received_msg(adap, &hdmi->hdmi_cec.rx_msg);
	}
	return IRQ_HANDLED;
}

static void _sunxi_drv_cec_adap_delect(void *data)
{
	struct sunxi_hdmi_cec_s *cec = data;

	cec_delete_adapter(cec->adap);
}

static int _sunxi_drv_cec_adap_enable(struct cec_adapter *adap, bool state)
{
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);

	if (hdmi->hdmi_cec.enable == state) {
		hdmi_inf("sunxi cec drv has been %s\n", state ?  "enable" : "disable");
		return 0;
	}

	if (state == SUNXI_HDMI_ENABLE) {
		/* enable cec clock */
		_sunxi_drv_cec_set_clock(hdmi, SUNXI_HDMI_ENABLE);
		/* enable cec hardware */
		sunxi_cec_enable(SUNXI_HDMI_ENABLE);
	} else {
		/* disbale cec hardware */
		sunxi_cec_enable(SUNXI_HDMI_DISABLE);
		/* disable cec clock */
		_sunxi_drv_cec_set_clock(hdmi, SUNXI_HDMI_DISABLE);
	}

	hdmi->hdmi_cec.enable = state;
	hdmi_inf("sunxi cec drv %s finish\n",
			state == SUNXI_HDMI_ENABLE ? "enable" : "disable");

	return 0;
}

static int _sunxi_drv_cec_adap_set_logicaddr(struct cec_adapter *adap, u8 addr)
{
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);

	hdmi_inf("cec adapter set logical addr: 0x%x\n", addr);
	if (addr == CEC_LOG_ADDR_INVALID)
		hdmi->hdmi_cec.logic_addr = 0x0;
	else
		hdmi->hdmi_cec.logic_addr = BIT(addr);

	sunxi_cec_set_logic_addr(hdmi->hdmi_cec.logic_addr);
	return 0;
}

static int _sunxi_drv_cec_adap_send(struct cec_adapter *adap, u8 attempts,
				u32 signal_free_time, struct cec_msg *msg)
{
	u8 times = SUNXI_CEC_WAIT_NULL;

	switch (signal_free_time) {
	case CEC_SIGNAL_FREE_TIME_RETRY:
		times = SUNXI_CEC_WAIT_3BIT;
		break;
	case CEC_SIGNAL_FREE_TIME_NEW_INITIATOR:
	default:
		times = SUNXI_CEC_WAIT_5BIT;
		break;
	case CEC_SIGNAL_FREE_TIME_NEXT_XFER:
		times = SUNXI_CEC_WAIT_7BIT;
		break;
	}

	sunxi_cec_message_send(msg->msg, msg->len, times);
	return 0;
}

static const struct cec_adap_ops _sunxi_cec_ops = {
	.adap_enable   = _sunxi_drv_cec_adap_enable,
	.adap_log_addr = _sunxi_drv_cec_adap_set_logicaddr,
	.adap_transmit = _sunxi_drv_cec_adap_send,
};


/*******************************************************************************
 * @desc: sunxi hdmi i2c adapter function
 ******************************************************************************/
static int _sunxi_drv_i2cm_xfer(struct i2c_adapter *adap,
			    struct i2c_msg *msgs, int num)
{
	return sunxi_hdmi_i2cm_xfer(msgs, num);
}

static u32 _sunxi_drv_i2cm_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm sunxi_hdmi_i2cm_algo = {
	.master_xfer	= _sunxi_drv_i2cm_xfer,
	.functionality	= _sunxi_drv_i2cm_func,
};

/*******************************************************************************
 * @desc: sunxi hdmi driver pm function
 ******************************************************************************/
#ifdef CONFIG_PM_SLEEP
static int _sunxi_hdmi_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_drm_hdmi  *hdmi = platform_get_drvdata(pdev);

	if (hdmi->hdmi_ctrl.drv_pm_state)
		goto suspend_exit;

	if (hdmi->hpd_task) {
		kthread_stop(hdmi->hpd_task);
		hdmi->hpd_task = NULL;
	}

	pm_runtime_put_sync(dev);

suspend_exit:
	hdmi->hdmi_ctrl.drv_edid_data = NULL;
	hdmi->hdmi_ctrl.drv_pm_state = 0x1;
	hdmi_inf("sunxi hdmi pm suspend done\n");
	return 0;
}

static int _sunxi_hdmi_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_drm_hdmi  *hdmi = platform_get_drvdata(pdev);

	if (!hdmi->hdmi_ctrl.drv_pm_state)
		goto resume_exit;

	pm_runtime_get_sync(dev);

	hdmi->hpd_task =
		kthread_create(_sunxi_drv_hdmi_thread, (void *)hdmi, "hdmi hpd");
	if (!hdmi->hpd_task) {
		hdmi_err("init thread handle hpd is failed!!!\n");
		hdmi->hpd_task = NULL;
		return -1;
	}
	wake_up_process(hdmi->hpd_task);

resume_exit:
	hdmi->hdmi_ctrl.drv_pm_state = 0x0;
	hdmi_inf("sunxi hdmi pm resume done\n");
	return 0;
}
#endif
/*******************************************************************************
 * @desc: sunxi hdmi driver init function
 ******************************************************************************/
static int
_sunxi_hdmi_init_dts(struct sunxi_drm_hdmi *hdmi)
{
	u32 value = 0;
	int ret = 0;
	struct device *dev = &hdmi->pdev->dev;
	struct sunxi_hdmi_res_s *pclk = &hdmi->hdmi_res;

	if (!dev) {
		hdmi_err("point dev is null\n");
		return -1;
	}

	if (!pclk) {
		hdmi_err("point clk is null\n");
		return -1;
	}

	hdmi->hdmi_core.reg_base = (uintptr_t __force)of_iomap(dev->of_node, 0);
	if (hdmi->hdmi_core.reg_base == 0) {
		hdmi_err("unable to map hdmi registers");
		return -1;
	}

	hdmi->hdmi_irq = platform_get_irq(hdmi->pdev, 0);
	if (hdmi->hdmi_irq < 0) {
		hdmi_err("hdmi drv detect dts not set irq. cec invalid\n");
		hdmi->hdmi_irq = -1;
	}

	hdmi->hdmi_cec.irq = hdmi->hdmi_irq;

	ret = of_property_read_u32(dev->of_node, "hdmi_cec_enable", &value);
	hdmi->hdmi_ctrl.drv_dts_cec = (ret != 0x0) ? 0x0 : value;

	/* new node, some plat maybe not set, so default hdcp14 enable */
	ret = of_property_read_u32(dev->of_node, "hdmi_hdcp1x_enable", &value);
	hdmi->hdmi_ctrl.drv_dts_hdcp1x = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_hdcp2x_enable", &value);
	hdmi->hdmi_ctrl.drv_dts_hdcp2x = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_power_cnt", &value);
	hdmi->hdmi_ctrl.drv_dts_power_cnt = (ret != 0x0) ? 0x0 : value;

	/* parse tcon clock */
	pclk->clk_tcon_tv = devm_clk_get(dev, "clk_tcon_tv");
	if (IS_ERR(pclk->clk_tcon_tv))
		pclk->clk_tcon_tv = NULL;

	/* parse hdmi clock */
	pclk->clk_hdmi = devm_clk_get(dev, "clk_hdmi");
	if (IS_ERR(pclk->clk_hdmi))
		pclk->clk_hdmi = NULL;

	/* parse hdmi 24M clock */
	pclk->clk_hdmi_24M = devm_clk_get(dev, "clk_hdmi_24M");
	if (IS_ERR(pclk->clk_hdmi_24M))
		pclk->clk_hdmi_24M = NULL;

	/* parse hdmi bus clock */
	pclk->clk_hdmi_bus = devm_clk_get(dev, "clk_bus_hdmi");
	if (IS_ERR(pclk->clk_hdmi_bus))
		pclk->clk_hdmi_bus = NULL;

	/* parse hdmi ddc clock */
	pclk->clk_hdmi_ddc = devm_clk_get(dev, "clk_ddc");
	if (IS_ERR(pclk->clk_hdmi_ddc))
		pclk->clk_hdmi_ddc = NULL;

	/* parse hdmi cec clock */
	pclk->clk_cec = devm_clk_get(dev, "clk_cec");
	if (IS_ERR(pclk->clk_cec))
		pclk->clk_cec = NULL;

	/* parse hdmi cec parent clock */
	pclk->clk_cec_parent = clk_get_parent(pclk->clk_cec);
	if (IS_ERR(pclk->clk_cec_parent))
		pclk->clk_cec_parent = NULL;

	/* parse hdmi hdcp clock */
	pclk->clk_hdcp = devm_clk_get(dev, "clk_hdcp");
	if (IS_ERR(pclk->clk_hdcp))
		pclk->clk_hdcp = NULL;

	/* parse hdmi hdcp bus clock */
	pclk->clk_hdcp_bus = devm_clk_get(dev, "clk_bus_hdcp");
	if (IS_ERR(pclk->clk_hdcp_bus))
		pclk->clk_hdcp_bus = NULL;

	/* parse hdmi hdcp bus reset clock */
	pclk->rst_bus_hdcp = devm_reset_control_get(dev, "rst_bus_hdcp");
	if (IS_ERR(pclk->rst_bus_hdcp))
		pclk->rst_bus_hdcp = NULL;

	/* parse hdmi sub bus reset clock */
	pclk->rst_bus_sub = devm_reset_control_get(dev, "rst_bus_sub");
	if (IS_ERR(pclk->rst_bus_sub))
		pclk->rst_bus_sub = NULL;

	/* parse hdmi sub main reset clock */
	pclk->rst_bus_main = devm_reset_control_get(dev, "rst_bus_main");
	if (IS_ERR(pclk->rst_bus_main))
		pclk->rst_bus_main = NULL;

	return 0;
}

static int _sunxi_hdmi_init_resource(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0, loop = 0;
	const char *power_string;
	char power_name[40];

	struct device *dev = &hdmi->pdev->dev;
	struct sunxi_hdmi_res_s *pres = &hdmi->hdmi_res;
	struct regulator *regu = NULL;

	if (!pres) {
		hdmi_err("point res is null\n");
		return -1;
	}

	if (!dev) {
		hdmi_err("point dev is null\n");
		return -1;
	}

	/* get power regulator */
	for (loop = 0; loop < hdmi->hdmi_ctrl.drv_dts_power_cnt; loop++) {
		sprintf(power_name, "hdmi_power%d", loop);
		ret = of_property_read_string(dev->of_node, power_name, &power_string);
		if (ret != 0) {
			hdmi_wrn("hdmi dts not set: hdmi_power%d!\n", loop);
			continue;
		} else {
			memcpy((void *)pres->power_name[loop],
					power_string, strlen(power_string) + 1);
			hdmi_inf("hdmi drv power name: %s\n", pres->power_name[loop]);
			regu = regulator_get(dev, pres->power_name[loop]);
			if (!regu) {
				hdmi_wrn("init resource to get %s regulator is failed\n",
					pres->power_name[loop]);
				continue;
			}
			pres->hdmi_regu[loop] = regu;
		}
	}

	hdmi->hpd_task =
		kthread_create(_sunxi_drv_hdmi_thread, (void *)hdmi, "hdmi hpd");
	if (!hdmi->hpd_task) {
		hdmi_err("init thread handle hpd is failed!!!\n");
		hdmi->hpd_task = NULL;
		return -1;
	}

#if IS_ENABLED(CONFIG_EXTCON)
	hdmi->hpd_extcon =
		devm_extcon_dev_allocate(dev, sunxi_hdmi_cable);
	if (IS_ERR(hdmi->hpd_extcon)) {
		hdmi_err("init notify alloc extcon node failed!!!\n");
		return -1;
	}

	ret = devm_extcon_dev_register(dev, hdmi->hpd_extcon);
	if (ret != 0) {
		hdmi_err("init notify register extcon node failed!!!\n");
		return -1;
	}

	hdmi->hpd_extcon->name = "drm-hdmi";
#else
	hdmi_wrn("not set hdmi notify node!\n");
#endif

	_sunxi_drv_hdmi_regulator_on(hdmi);

	_sunxi_drv_hdmi_clock_on(hdmi);

	_sunxi_drv_hdmi_hpd_set(hdmi, 0x0);

	mutex_init(&hdmi->hdmi_ctrl.drv_edid_lock);

	return 0;
}

static int _sunxi_hdmi_init_value(struct sunxi_drm_hdmi *hdmi)
{
	struct disp_device_config *info = &hdmi->disp_config;

	info->format       = DISP_CSC_TYPE_RGB;
	info->bits         = DISP_DATA_8BITS;
	info->eotf         = DISP_EOTF_GAMMA22; /* SDR */
	info->cs           = DISP_BT709;
	info->dvi_hdmi     = DISP_HDMI;
	info->range        = DISP_COLOR_RANGE_DEFAULT;
	info->scan         = DISP_SCANINFO_NO_DATA;
	info->aspect_ratio = HDMI_ACTIVE_ASPECT_PICTURE;

	_sunxi_drv_hdcp_release(hdmi);

	hdmi->hdmi_ctrl.drv_color_cap  = BIT(SUNXI_COLOR_RGB888_8BITS);
	hdmi->hdmi_ctrl.drv_color_mode = BIT(SUNXI_COLOR_RGB888_8BITS);
	hdmi->hdmi_ctrl.drv_max_bits   = DISP_DATA_10BITS;
	return 0;
}

static int _sunxi_hdmi_init_i2cm_adap(struct sunxi_drm_hdmi *hdmi)
{
	struct i2c_adapter *adap = &hdmi->i2c_adap;
	int ret = 0;

	if (!adap) {
		hdmi_err("point adap is null\n");
		return -1;
	}

	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = &hdmi->pdev->dev;
	adap->algo = &sunxi_hdmi_i2cm_algo;
	strlcpy(adap->name, "SUNXI HDMI", sizeof(adap->name));

	i2c_set_adapdata(adap, hdmi);
	ret = i2c_add_adapter(adap);
	if (ret) {
		hdmi_err("hdmi drv register %s i2c adapter failed\n", adap->name);
		return -1;
	}
	hdmi_inf("hdmi drv init i2c adapter finish\n");
	return 0;
}

static int _sunxi_hdmi_init_cec_adap(struct sunxi_drm_hdmi *hdmi)
{
	int ret = -1;
	struct sunxi_hdmi_cec_s *cec = &hdmi->hdmi_cec;
	struct platform_device *pdev = hdmi->pdev;

	if (!hdmi->hdmi_ctrl.drv_dts_cec) {
		hdmi_inf("hdmi dts not set use cec\n");
		return 0;
	}

	cec->adap = cec_allocate_adapter(&_sunxi_cec_ops, hdmi, "sunxi_cec",
			CEC_CAP_DEFAULTS | CEC_CAP_CONNECTOR_INFO, CEC_MAX_LOG_ADDRS);

	if (IS_ERR(cec->adap))
		return PTR_ERR(cec->adap);

	cec->adap->owner = THIS_MODULE;

	ret = devm_add_action(&pdev->dev, _sunxi_drv_cec_adap_delect, cec);
	if (ret) {
		cec_delete_adapter(cec->adap);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, cec->irq,
			_sunxi_drv_cec_hardirq, _sunxi_drv_cec_thread,
			IRQF_SHARED, "sunxi-cec", cec->adap);
	if (ret < 0)
		return ret;

	cec->notify = cec_notifier_cec_adap_register(&pdev->dev,
						     NULL, cec->adap);
	if (!cec->notify)
		return -ENOMEM;

	ret = cec_register_adapter(cec->adap, &pdev->dev);
	if (ret < 0) {
		cec_notifier_cec_adap_unregister(cec->notify, cec->adap);
		return ret;
	}

	devm_remove_action(&pdev->dev, _sunxi_drv_cec_adap_delect, cec);

	hdmi_inf("hdmi drv init cec adapter finish\n");
	return 0;
}


static int _sunxi_hdmi_init_adapter(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	ret = _sunxi_hdmi_init_i2cm_adap(hdmi);
	if (ret != 0) {
		hdmi_err("hdmi drv init i2c adapter failed\n");
		return -1;
	}

	ret = _sunxi_hdmi_init_cec_adap(hdmi);
	if (ret != 0) {
		hdmi_err("hdmi drv init cec adapter failed\n");
		return -1;
	}

	return 0;
}

static int _sunxi_hdmi_init_driver(struct sunxi_drm_hdmi *hdmi)
{
	int ret = -1;

	/* parse sunxi hdmi dts */
	ret = _sunxi_hdmi_init_dts(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init dts failed!!!\n");
		return -1;
	} else
		hdmi_inf("hdmi drv init dts done\n");

	/* sunxi hdmi resource alloc and enable */
	ret = _sunxi_hdmi_init_resource(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init resource failed!!!\n");
		return -1;
	} else
		hdmi_inf("hdmi drv init resource done\n");

	/* sunxi hdmi config value init */
	ret = _sunxi_hdmi_init_value(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init config value failed\n");
	}

	/* hdmi init register adapter */
	ret = _sunxi_hdmi_init_adapter(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init adapter failed\n");
		return -1;
	}

	/* sunxi hdmi core level init */
	hdmi->hdmi_core.pdev = hdmi->pdev;
	hdmi->hdmi_core.i2c_adap  = &hdmi->i2c_adap;
	hdmi->hdmi_core.connect   = &hdmi->connector;
	ret = sunxi_hdmi_init(&hdmi->hdmi_core);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init core failed!!!\n");
		return -1;
	}

	return 0;
}

static int _sunxi_hdmi_drv_exit(struct platform_device *pdev)
{
	struct sunxi_drm_hdmi *hdmi = platform_get_drvdata(pdev);

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	if (hdmi->hpd_task) {
		/* destory hdmi hpd thread */
		kthread_stop(hdmi->hpd_task);
		hdmi->hpd_task = NULL;
	}

	sunxi_hdmi_exit();

	i2c_del_adapter(&hdmi->i2c_adap);

	cec_notifier_cec_adap_unregister(hdmi->hdmi_cec.notify, hdmi->hdmi_cec.adap);

	cec_unregister_adapter(hdmi->hdmi_cec.adap);

	_sunxi_drv_hdmi_regulator_off(hdmi);

	hdmi = NULL;
	return 0;
}

static ssize_t _sunxi_hdmi_sysfs_reg_read_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\n[register read]\n");
	n += sprintf(buf + n, "Demo: echo offset,count > read\n");
	n += sprintf(buf + n, " - offset: register offset address.\n");
	n += sprintf(buf + n, " - count: read count register\n");
	return n;
}

ssize_t _sunxi_hdmi_sysfs_reg_read_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long start_reg = 0;
	unsigned long read_count = 0;
	u32 r_value = 0;
	u32 i;
	u8 *separator;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	separator = strchr(buf, ',');
	if (separator != NULL) {
		if (sunxi_parse_dump_string(buf, count, &start_reg, &read_count)) {
			hdmi_err("input param failed! Use cat read node get more info!\n");
			return 0;
		}
		goto cmd_read;
	} else {
		separator = strchr(buf, ' ');
		if (separator != NULL) {
			start_reg = simple_strtoul(buf, NULL, 0);
			read_count = simple_strtoul(separator + 1, NULL, 0);
			goto cmd_read;
		} else {
			start_reg = simple_strtoul(buf, NULL, 0);
			read_count = 1;
			goto cmd_read;
		}
	}

cmd_read:
	hdmi_inf("read offset: 0x%lx, count: %ld\n", start_reg, read_count);
	for (i = 0; i < read_count; i++) {
		switch (hdmi->hdmi_ctrl.drv_reg_bank) {
		case SUNXI_HDMI_REG_BANK_PHY:
			sunxi_hdmi_phy_read((u8)start_reg, &r_value);
			hdmi_inf("phy read: 0x%x = 0x%x\n", (u8)start_reg, r_value);
			break;
		case SUNXI_HDMI_REG_BANK_SCDC:
			r_value = sunxi_hdmi_scdc_read((u8)start_reg);
			hdmi_inf("scdc read: 0x%x = 0x%x\n", (u8)start_reg, (u8)r_value);
			break;
		case SUNXI_HDMI_REG_BANK_HPI:
			hdmi_inf("current unsupport hpi read\n");
			break;
		default:
			r_value = sunxi_hdmi_ctrl_read(start_reg);
			hdmi_inf("ctrl read: 0x%lx = 0x%x\n", start_reg, r_value);
			break;
		}
		start_reg++;
	}
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_reg_write_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\n[register write]\n");
	n += sprintf(buf + n, "Demo: echo offset,value > write\n");
	n += sprintf(buf + n, " - offset: register offset address.\n");
	n += sprintf(buf + n, " - value: write value\n");
	return n;
}

ssize_t _sunxi_hdmi_sysfs_reg_write_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long reg_addr = 0;
	unsigned long value = 0;
	u8 *separator1 = NULL;
	u8 *separator2 = NULL;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	separator1 = strchr(buf, ',');
	separator2 = strchr(buf, ' ');
	if (separator1 != NULL) {
		if (sunxi_parse_dump_string(buf, count, &reg_addr, &value)) {
			hdmi_err("input param failed! Use cat read node get more info!\n");
			return 0;
		}
		goto cmd_write;
	} else if (separator2 != NULL) {
		reg_addr = simple_strtoul(buf, NULL, 0);
		value = simple_strtoul(separator2 + 1, NULL, 0);
		goto cmd_write;
	} else {
		hdmi_err("input param failed! Use cat read node get more info!\n");
		return 0;
	}

cmd_write:
	switch (hdmi->hdmi_ctrl.drv_reg_bank) {
	case SUNXI_HDMI_REG_BANK_PHY:
		sunxi_hdmi_phy_write((u8)reg_addr, (u32)value);
		hdmi_inf("phy write: 0x%x = 0x%x\n", (u8)reg_addr, (u32)value);
		break;
	case SUNXI_HDMI_REG_BANK_SCDC:
		sunxi_hdmi_scdc_write(reg_addr, value);
		hdmi_inf("scdc write: 0x%x = 0x%x\n", (u8)reg_addr, (u8)value);
		break;
	case SUNXI_HDMI_REG_BANK_HPI:
		hdmi_inf("current unsupport hpi write\n");
		break;
	default:
		sunxi_hdmi_ctrl_write((uintptr_t)reg_addr, (u32)value);
		hdmi_inf("ctrl write: 0x%lx = 0x%x\n", reg_addr, (u32)value);
		break;
	}

	return count;
}

static ssize_t _sunxi_hdmi_sysfs_reg_bank_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	n += sprintf(buf + n, "\nhdmi register bank\n");
	n += sprintf(buf + n, "Demo: echo index > rw_bank\n");
	n += sprintf(buf + n, " - %d: switch control write and read.\n", SUNXI_HDMI_REG_BANK_CTRL);
	n += sprintf(buf + n, " - %d: switch phy write and read.\n", SUNXI_HDMI_REG_BANK_PHY);
	n += sprintf(buf + n, " - %d: switch scdc write and read\n", SUNXI_HDMI_REG_BANK_SCDC);
	n += sprintf(buf + n, " - %d: switch esm hpi write and read\n", SUNXI_HDMI_REG_BANK_HPI);

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return n;
	}
	n += sprintf(buf + n, "current reg bank index: %d\n",
		hdmi->hdmi_ctrl.drv_reg_bank);
	return n;
}

ssize_t _sunxi_hdmi_sysfs_reg_bank_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u8 bank = 0;
	char *end;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	bank = (u8)simple_strtoull(buf, &end, 0);
	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return count;
	}

	hdmi->hdmi_ctrl.drv_reg_bank = bank;
	hdmi_inf("switch reg bank index: %d\n", hdmi->hdmi_ctrl.drv_reg_bank);
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_pattern_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\nhdmi module pattern\n");
	n += sprintf(buf + n, "Demo: echo index > pattern\n");
	n += sprintf(buf + n, " - 1: [frame composer] red pattern.\n");
	n += sprintf(buf + n, " - 2: [frame composer] green pattern.\n");
	n += sprintf(buf + n, " - 3: [frame composer] blue pattern.\n");

	return n;
}

ssize_t _sunxi_hdmi_sysfs_pattern_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int bit = 0;
	char *end;
	u32 value[32] = {0xFF0000, 0x00FF00, 0x0000FF};

	bit = (u8)simple_strtoull(buf, &end, 0);
	if (bit < 0 || bit > 32) {
		hdmi_err("input index %d unsupport\n", bit);
		return 0;
	}

	if (bit == 0) {
		sunxi_hdmi_video_set_pattern(0x0, 0x0);
		hdmi_inf("disable force video output\n");
		return count;
	}

	sunxi_hdmi_video_set_pattern(0x1, value[bit - 1]);
	if (bit <= 3) {
		hdmi_inf("check frame composer pattern can cover issue?\n");
		return count;
	}

	return count;
}

static ssize_t _sunxi_hdmi_sysfs_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return n;
	}

	n += sprintf(buf + n, "\n[hdmi debug log level]\n");
	n += sprintf(buf + n, "Demo: echo index > debug\n");
	n += sprintf(buf + n, " - %d: enable video log\n", LOG_INDEX_VIDEO);
	n += sprintf(buf + n, " - %d: enable audio log\n", LOG_INDEX_AUDIO);
	n += sprintf(buf + n, " - %d: enable edid log\n", LOG_INDEX_EDID);
	n += sprintf(buf + n, " - %d: enable hdcp log\n", LOG_INDEX_HDCP);
	n += sprintf(buf + n, " - %d: enable cec log\n", LOG_INDEX_CEC);
	n += sprintf(buf + n, " - %d: enable phy log\n", LOG_INDEX_PHY);
	n += sprintf(buf + n, " - %d: enable trace log\n", LOG_INDEX_TRACE);
	n += sprintf(buf + n, "current log level: %d\n", sunxi_hdmi_get_loglevel());

	return n;
}

ssize_t _sunxi_hdmi_sysfs_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u8 level = 0;
	char *end;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return count;
	}

	level = (u8)simple_strtoull(buf, &end, 0);

	suxni_hdmi_set_loglevel(level);
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hdmi_source_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0, loop = 0;
	u8 data = 0, status = 0;
	struct sunxi_drm_hdmi   *hdmi = dev_get_drvdata(dev);
	struct sunxi_hdmi_res_s *pres = &hdmi->hdmi_res;
	struct drm_display_info *info = &hdmi->connector.display_info;

	n += sprintf(buf + n, "\n");
	n += sprintf(buf + n, "----------------- hdmi tx info -----------------\n");
	n += sprintf(buf + n, "[dts config]\n");
	n += sprintf(buf + n, " - cec: [%s], hdcp1x: [%s], hdcp2x: [%s]\n",
		hdmi->hdmi_ctrl.drv_dts_cec ? "enable" : "disable",
		hdmi->hdmi_ctrl.drv_dts_hdcp1x ? "enable" : "disable",
		hdmi->hdmi_ctrl.drv_dts_hdcp2x ? "enable" : "disable");

	n += sprintf(buf + n, "[drm state]\n");
	n += sprintf(buf + n, " - atomic_enable: [%s], mode_set: [%s], mode_info: [%d*%d], hpd_force: [%s]\n",
		hdmi->hdmi_ctrl.drm_enable ? "enable" : "disable",
		hdmi->hdmi_ctrl.drm_mode_set ? "yes" : "not",
		hdmi->drm_mode.hdisplay, hdmi->drm_mode.vdisplay,
		hdmi->hdmi_ctrl.drm_hpd_force == DRM_FORCE_ON ? "on" : "off");

	n += sprintf(buf + n, "[drv state]\n");
	n += sprintf(buf + n, " - hpd_thread: [%s], hpd_state: [%s], hpd mask: [0x%x]\n",
		hdmi->hpd_task ? "valid" : "invalid",
		hdmi->hdmi_ctrl.drv_hpd_state ? "plugin" : "plugout",
		hdmi->hdmi_ctrl.drv_hpd_mask);
	n += sprintf(buf + n, " - clock: [%s], output: [%s]",
		hdmi->hdmi_ctrl.drv_clock ? "enable" : "disable",
		hdmi->hdmi_ctrl.drv_enable ? "enable" : "disable");
	for (loop = 0; loop < hdmi->hdmi_ctrl.drv_dts_power_cnt; loop++)
		n += sprintf(buf + n, ", power_%s: [%s]", pres->power_name[loop],
			regulator_is_enabled(pres->hdmi_regu[loop]) ? "enable" : "disable");
	n += sprintf(buf + n, "\n");
	n += sprintf(buf + n, " - color_cap: [0x%x]\n", hdmi->hdmi_ctrl.drv_color_cap);
	n += sprintf(buf + n, "\n");

	n += sunxi_hdmi_tx_dump(buf + n);

	if (!_sunxi_drv_hdmi_hpd_get(hdmi))
		goto exit_dump;

	n += sprintf(buf + n, "\n");
	n += sprintf(buf + n, "----------------- hdmi rx info -----------------\n");
	n += sunxi_hdmi_rx_dump(buf + n);
	n += sprintf(buf + n, "[scdc]\n");
	if (info->hdmi.scdc.supported) {
		data = sunxi_hdmi_scdc_read(SCDC_TMDS_CONFIG);
		status = sunxi_hdmi_scdc_read(SCDC_SCRAMBLER_STATUS) & SCDC_SCRAMBLING_STATUS;
		n += sprintf(buf + n, " - clock ratio[%s], scramble enable[%d], scramble status[%d]\n",
			(data & SCDC_TMDS_BIT_CLOCK_RATIO_BY_40) ? "1/40" : "1/10",
			(data & SCDC_SCRAMBLING_ENABLE), status);
		data = sunxi_hdmi_scdc_read(SCDC_STATUS_FLAGS_0);
		n += sprintf(buf + n, " - clk_lock[%d], ch0_lock[%d], ch1_lock[%d], ch2_lock[%d]\n",
			(data & SCDC_CLOCK_DETECT), (data & SCDC_CH0_LOCK) >> 1,
			(data & SCDC_CH1_LOCK) >> 2, (data & SCDC_CH2_LOCK) >> 3);
#ifdef SUNXI_HDMI20_USE_HDCP
		data = sunxi_hdcp_get_sink_cap();
		n += sprintf(buf + n, " - hdcp22: %s\n", data ? "support" : "unsupport");
#endif
	} else {
		n += sprintf(buf + n, " - unsupport scdc\n");
	}

exit_dump:
	n += sprintf(buf + n, "\n");

	return n;
}

static ssize_t _sunxi_hdmi_sysfs_hdmi_source_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hpd_mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	int n = 0;

	n += sprintf(buf + n, "\n[debug hpd mask]\n");
	n += sprintf(buf + n, " Demo: echo value > hpd_mask\n");
	n += sprintf(buf + n, " - 0x%x: force hpd plugin.\n", SUNXI_HDMI_HPD_FORCE_IN);
	n += sprintf(buf + n, " - 0x%x: force hpd plugout.\n", SUNXI_HDMI_HPD_FORCE_OUT);
	n += sprintf(buf + n, " - 0x%x: mask hpd notify.\n", SUNXI_HDMI_HPD_MASK_NOTIFY);
	n += sprintf(buf + n, " - 0x%x: mask hpd detect.\n", SUNXI_HDMI_HPD_MASK_DETECT);
	n += sprintf(buf + n, "Current hpd mask value: 0x%x\n", hdmi->hdmi_ctrl.drv_hpd_mask);

	return n;
}

static ssize_t _sunxi_hdmi_sysfs_hpd_mask_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long val;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	if (count < 1)
		return -EINVAL;

	err = kstrtoul(buf, 16, &val);
	if (err) {
		hdmi_err("%s: parse buf error: %s\n", __func__, buf);
		return err;
	}

	hdmi->hdmi_ctrl.drv_hpd_mask = (u32)val;
	hdmi_inf("set hpd mask: 0x%x\n", (u32)val);

	return count;
}

static ssize_t _sunxi_hdmi_sysfs_edid_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	int n = 0;

	n += sprintf(buf + n, "\n[edid debug]\n");
	n += sprintf(buf + n, " Demo: echo value > edid_debug\n");
	n += sprintf(buf + n, " - 0x1: enable use edid debug data\n");
	n += sprintf(buf + n, " - 0x0: disable use edid debug data\n");
	n += sprintf(buf + n, "Current edid debug mode: %d\n",
			hdmi->hdmi_ctrl.drv_edid_dbg_mode);

	return n;
}

static ssize_t _sunxi_hdmi_sysfs_edid_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	if (count < 1)
		return -EINVAL;

	ret = kstrtoul(buf, 16, &val);
	if (ret) {
		hdmi_err("%s: parse buf error: %s\n", __func__, buf);
		return ret;
	}

	hdmi->hdmi_ctrl.drv_edid_dbg_mode = (u8)val;
	hdmi_inf("edid debug mode set: %d\n", (u8)val);
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_edid_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t _sunxi_hdmi_sysfs_edid_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	if (count < 1) {
		hdmi_err("edid debug data is too small\n");
		return -EINVAL;
	}

	if (count > SUNXI_HDMI_EDID_LENGTH) {
		hdmi_err("edid debug data is too large\n");
		return -EINVAL;
	}

	memset(hdmi->hdmi_ctrl.drv_edid_dbg_data, 0x0, SUNXI_HDMI_EDID_LENGTH);
	memcpy(hdmi->hdmi_ctrl.drv_edid_dbg_data, buf, count);

	hdmi->hdmi_ctrl.drv_edid_dbg_size = count;
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\n[hdcp enable]\n");
	n += sprintf(buf + n, "Demo: echo index > hdcp_enable\n");
	n += sprintf(buf + n, " - 1: enable  0: disable\n");
	return n;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);

	if (count < 1)
		return -EINVAL;

	if (strncmp(buf, "1", 1) == 0) {
		hdmi->hdmi_ctrl.drv_hdcp_enable = 0x1;
		_sunxi_drv_hdcp_enable(hdmi);
	} else {
		hdmi->hdmi_ctrl.drv_hdcp_enable = 0x0;
		_sunxi_drv_hdcp_disable(hdmi);
	}

	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	char hdcp_type = (char)hdmi->hdmi_ctrl.drv_hdcp_type;
	memcpy(buf, &hdcp_type, 1);
	return 1;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_type_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 count = sizeof(u8);
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	u8 statue = _sunxi_drv_hdcp_get_state(hdmi);

	memcpy(buf, &statue, count);

	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_loader_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;

	n += sprintf(buf + n, "\n[hdcp loader]\n");
	n += sprintf(buf + n, " - current loader state: %s\n",
			sunxi_hdcp2x_fw_state() ? "loaded" : "not-loaded");
	n += sprintf(buf + n, " - echo 1 > hdcp_loader   #enable hdcp fw load\n");
	n += sprintf(buf + n, "\nNote: You need to configure the firmware path\n");
	n += sprintf(buf + n, "1. Store the firmware in the path you specify, such as /data/fw/\n");
	n += sprintf(buf + n, "2. Set the upper level path of the firmware\n");
	n += sprintf(buf + n, " - demo: echo /data/fw > /sys/module/firmware_class/parameters/path\n");
	n += sprintf(buf + n, "3. Use this cmd for enable hdcp loader\n");

	return n;
}

static ssize_t _sunxi_hdmi_sysfs_hdcp_loader_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	int ret = 0;

	if (count < 1)
		return -EINVAL;

	if (strncmp(buf, "1", 1) == 0) {
		ret = _sunxi_drv_hdcp_loading_fw(dev);
		if (ret == 0) {
			hdmi_inf("sunxi hdcp loading fw failed\n");
			return -EINVAL;
		}
		_sunxi_drv_hdcp_update_support(hdmi);
		return count;
	}

	hdmi_inf("unsupport cmd args: %s\n", buf);
	return -EINVAL;
}

static DEVICE_ATTR(reg_read, 0664,
	_sunxi_hdmi_sysfs_reg_read_show, _sunxi_hdmi_sysfs_reg_read_store);
static DEVICE_ATTR(reg_write, 0664,
	_sunxi_hdmi_sysfs_reg_write_show, _sunxi_hdmi_sysfs_reg_write_store);
static DEVICE_ATTR(reg_bank, 0664,
	_sunxi_hdmi_sysfs_reg_bank_show, _sunxi_hdmi_sysfs_reg_bank_store);
static DEVICE_ATTR(pattern, 0664,
	_sunxi_hdmi_sysfs_pattern_show, _sunxi_hdmi_sysfs_pattern_store);
static DEVICE_ATTR(debug, 0664,
	_sunxi_hdmi_sysfs_debug_show, _sunxi_hdmi_sysfs_debug_store);
static DEVICE_ATTR(hdmi_source, 0664,
	_sunxi_hdmi_sysfs_hdmi_source_show, _sunxi_hdmi_sysfs_hdmi_source_store);
static DEVICE_ATTR(hpd_mask, 0664,
	_sunxi_hdmi_sysfs_hpd_mask_show, _sunxi_hdmi_sysfs_hpd_mask_store);
static DEVICE_ATTR(edid_debug, 0664,
	_sunxi_hdmi_sysfs_edid_debug_show, _sunxi_hdmi_sysfs_edid_debug_store);
static DEVICE_ATTR(edid_data, 0664,
	_sunxi_hdmi_sysfs_edid_data_show, _sunxi_hdmi_sysfs_edid_data_store);
static DEVICE_ATTR(hdcp_enable, 0664,
	_sunxi_hdmi_sysfs_hdcp_enable_show, _sunxi_hdmi_sysfs_hdcp_enable_store);
static DEVICE_ATTR(hdcp_type, 0664,
	_sunxi_hdmi_sysfs_hdcp_type_show, _sunxi_hdmi_sysfs_hdcp_type_store);
static DEVICE_ATTR(hdcp_state, 0664,
	_sunxi_hdmi_sysfs_hdcp_state_show, _sunxi_hdmi_sysfs_hdcp_state_store);
static DEVICE_ATTR(hdcp_loader, 0664,
	_sunxi_hdmi_sysfs_hdcp_loader_show, _sunxi_hdmi_sysfs_hdcp_loader_store);

static struct attribute *_sunxi_hdmi_attrs[] = {
	&dev_attr_reg_write.attr,
	&dev_attr_reg_read.attr,
	&dev_attr_reg_bank.attr,
	&dev_attr_pattern.attr,
	&dev_attr_debug.attr,
	&dev_attr_hdmi_source.attr,
	&dev_attr_hpd_mask.attr,
	&dev_attr_edid_debug.attr,
	&dev_attr_edid_data.attr,
	&dev_attr_hdcp_enable.attr,
	&dev_attr_hdcp_type.attr,
	&dev_attr_hdcp_state.attr,
	&dev_attr_hdcp_loader.attr,
	NULL
};

static struct attribute_group _sunxi_hdmi_group = {
	.name = "attr",
	.attrs = _sunxi_hdmi_attrs,
};

/*******************************************************************************
 * @desc: sunxi hdmi device fops function
 ******************************************************************************/
static int _sunxi_hdmi_init_sysfs(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	ret = alloc_chrdev_region(&hdmi->hdmi_devid, 0, 1, "hdmi");
	if (ret != 0) {
		hdmi_err("hdmi init dev failed when alloc dev id\n");
		goto err_region;
	}

	/* creat hdmi class */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	hdmi->hdmi_class = class_create(THIS_MODULE, "hdmi");
#else
	hdmi->hdmi_class = class_create("hdmi");
#endif
	if (IS_ERR(hdmi->hdmi_class)) {
		hdmi_err("hdmi init dev failed when create hdmi class\n");
		goto err_class;
	}

	/* creat hdmi device */
	hdmi->hdmi_dev = device_create(hdmi->hdmi_class,
			NULL, hdmi->hdmi_devid, NULL, "hdmi");
	if (IS_ERR(hdmi->hdmi_dev)) {
		hdmi_err("hdmi init dev failed when creat hdmi class device\n");
		goto err_device;
	}

	/* creat hdmi attr */
	ret = sysfs_create_group(&hdmi->hdmi_dev->kobj, &_sunxi_hdmi_group);
	if (ret != 0) {
		hdmi_err("hdmi init dev failed when creat group\n");
		goto err_group;
	}

	dev_set_drvdata(hdmi->hdmi_dev, hdmi);
	return 0;

err_group:
	device_destroy(hdmi->hdmi_class, hdmi->hdmi_devid);
err_device:
	class_destroy(hdmi->hdmi_class);
err_class:
	unregister_chrdev_region(hdmi->hdmi_devid, 1);
err_region:
	return -1;
}

static int _sunxi_hdmi_sysfs_exit(struct platform_device *pdev)
{
	struct sunxi_drm_hdmi *hdmi = platform_get_drvdata(pdev);

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	sysfs_remove_group(&hdmi->hdmi_dev->kobj, &_sunxi_hdmi_group);
	device_destroy(hdmi->hdmi_class, hdmi->hdmi_devid);
	class_destroy(hdmi->hdmi_class);
	unregister_chrdev_region(hdmi->hdmi_devid, 1);

	return 0;
}

/*******************************************************************************
 * drm sunxi hdmi encoder helper functions
 ******************************************************************************/
static void _sunxi_hdmi_drm_encoder_atomic_disable(struct drm_encoder *encoder,
		struct drm_atomic_state *state)
{
	struct sunxi_drm_hdmi *hdmi = drm_encoder_to_sunxi_drm_hdmi(encoder);
	int ret = 0;

	ret = _sunxi_drv_hdmi_disable(hdmi);
	if (ret != 0) {
		hdmi_err("hdmi drv disable failed\n");
		return;
	}

	ret = sunxi_tcon_mode_exit(hdmi->tcon_dev);
	if (ret != 0) {
		hdmi_err("hdmi tcon mode exit failed\n");
		return;
	}

	if (hdmi->hdmi_ctrl.drv_pm_state) {
		_sunxi_drv_hdmi_clock_off(hdmi);
		_sunxi_drv_hdmi_regulator_off(hdmi);
		hdmi_inf("drm hdmi disable resource when pm\n");
	}

	hdmi->hdmi_ctrl.drm_enable   = 0x0;
	hdmi->hdmi_ctrl.drm_mode_set = 0x0;
	hdmi->hdmi_ctrl.drv_enable   = 0x0;
	hdmi_inf("drm hdmi atomic disable done >>>>>>>>>>>>>>>>\n");
}

static void _sunxi_hdmi_drm_encoder_atomic_enable(struct drm_encoder *encoder,
		struct drm_atomic_state *state)
{
	struct sunxi_drm_hdmi *hdmi = drm_encoder_to_sunxi_drm_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct disp_output_config disp_cfg;
	int hw_id = sunxi_drm_crtc_get_hw_id(crtc);
	int ret = 0;

	if (hdmi->hdmi_ctrl.drv_pm_state) {
		_sunxi_drv_hdmi_regulator_on(hdmi);
		_sunxi_drv_hdmi_clock_on(hdmi);
		hdmi_inf("drm hdmi enable resource when pm\n");
	}

	/* set tcon config data */
	memset(&disp_cfg, 0x0, sizeof(disp_cfg));
	memcpy(&disp_cfg.timing, &hdmi->disp_timing, sizeof(struct disp_video_timings));
	disp_cfg.type        = INTERFACE_HDMI;
	disp_cfg.de_id       = hw_id;
	disp_cfg.format      = hdmi->disp_config.format;
	disp_cfg.irq_handler = sunxi_crtc_event_proc;
	disp_cfg.irq_data    = scrtc_state->base.crtc;

	/* tcon hdmi enable */
	ret = sunxi_tcon_mode_init(hdmi->tcon_dev, &disp_cfg);
	if (ret != 0) {
		hdmi_err("sunxi tcon hdmi mode init failed\n");
		return;
	}

	ret = _sunxi_drv_hdmi_enable(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi driver enable failed\n");
		return;
	}

	hdmi->hdmi_ctrl.drm_enable = 0x1;
	hdmi_inf("drm hdmi atomic enable >>>>>>>>>>>>>>>>\n");
}

static int _sunxi_hdmi_drm_encoder_atomic_check(struct drm_encoder *encoder,
		    struct drm_crtc_state *crtc_state,
		    struct drm_connector_state *conn_state)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_hdmi *hdmi = drm_encoder_to_sunxi_drm_hdmi(encoder);
	int change_cnt = 0, ret = 0;

	scrtc_state->tcon_id             = hdmi->tcon_id;
	scrtc_state->output_dev_data     = hdmi;
	scrtc_state->check_status        = _sunxi_drv_hdmi_fifo_check;
	scrtc_state->enable_vblank       = _sunxi_drv_hdmi_vblank_enable;
	scrtc_state->is_sync_time_enough = _sunxi_drv_hdmi_is_sync_time_enough;

	change_cnt = _sunxi_drv_hdmi_check_disp_info(hdmi);
	if (change_cnt) {
		hdmi_inf("drm hdmi check disp info change\n");
		crtc_state->mode_changed = true;
	}

	memcpy(&hdmi->drm_mode_adjust,
		&crtc_state->mode, sizeof(struct drm_display_mode));

	ret = _sunxi_drv_hdmi_select_output(hdmi);
	if (ret != 0) {
		hdmi_err("drm hdmi set not change when select output failed\n");
		crtc_state->mode_changed = false;
	}

	hdmi_inf("drm hdmi check mode: %s >>>>>>>>>>>>>>>>\n",
		crtc_state->mode_changed ? "change" : "unchange");
	return 0;
}

static void _sunxi_hdmi_drm_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjust_mode)
{
	int ret = 0;
	struct sunxi_drm_hdmi *hdmi = drm_encoder_to_sunxi_drm_hdmi(encoder);

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return;
	}

	memcpy(&hdmi->drm_mode, mode, sizeof(struct drm_display_mode));

	ret = sunxi_hdmi_set_disp_mode(&hdmi->drm_mode);
	if (ret != 0) {
		hdmi_err("drm mode set convert failed\n");
		return;
	}

	ret = sunxi_hdmi_set_disp_info(&hdmi->disp_config);
	if (ret != 0)
		hdmi_inf("drm atomic enable fill config failed\n");

	sunxi_hdmi_select_output_packets(mode->flags);

	ret = drm_mode_to_sunxi_video_timings(&hdmi->drm_mode, &hdmi->disp_timing);
	if (ret != 0) {
		hdmi_err("drm mode disp_timing %d*%d convert disp disp_timing failed\n",
			hdmi->drm_mode.hdisplay, hdmi->drm_mode.vdisplay);
		return;
	}

	hdmi->hdmi_ctrl.drm_mode_set = 0x1;
	hdmi_inf("drm hdmi mode set: %d*%d >>>>>>>>>>>>>>>>\n",
			mode->hdisplay, mode->vdisplay);
}

/*******************************************************************************
 * drm sunxi hdmi connect helper functions
 ******************************************************************************/
static int _sunxi_hdmi_drm_connector_get_modes(struct drm_connector *connector)
{
	struct sunxi_drm_hdmi   *hdmi = drm_connector_to_sunxi_drm_hdmi(connector);
	struct drm_display_mode *mode = NULL;
	struct drm_display_info *info = &connector->display_info;
	int ret = 0, i = 0;

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return -1;
	}

	ret = _sunxi_drv_hdmi_read_edid(hdmi);
	if (ret != 0) {
		hdmi_err("drm get mode read edid failed\n");
		goto use_default;
	}

	drm_connector_update_edid_property(connector, hdmi->hdmi_ctrl.drv_edid_data);

	cec_notifier_set_phys_addr_from_edid(hdmi->hdmi_cec.notify,
			hdmi->hdmi_ctrl.drv_edid_data);

	ret = drm_add_edid_modes(connector, hdmi->hdmi_ctrl.drv_edid_data);
	hdmi_inf("drm get edid support modes: %d\n", ret);
	return ret;

use_default:
	/* can not get edid, but use default mode! */
	hdmi_wrn("hdmi drv use default edid modes\n");
	for (i = 0; i < ARRAY_SIZE(_sunxi_hdmi_default_modes); i++) {
		const struct drm_display_mode *temp_mode = &_sunxi_hdmi_default_modes[i];
		mode = drm_mode_duplicate(connector->dev, temp_mode);
		if (mode) {
			if (i != 0) {
				mode->type = DRM_MODE_TYPE_PREFERRED;
				mode->picture_aspect_ratio = HDMI_PICTURE_ASPECT_NONE;
			}
			drm_mode_probed_add(connector, mode);
			ret++;
		}
	}

	info->hdmi.y420_dc_modes = 0;
	info->color_formats = 0;

	return ret;
}

static enum drm_mode_status _sunxi_hdmi_drm_connector_mode_valid(
		struct drm_connector *connector, struct drm_display_mode *mode)
{
	int rate = drm_mode_vrefresh(mode);

	/* check frame rate support */
	if (rate > 60) {
		return MODE_BAD;
	}

	return MODE_OK;
}

/*******************************************************************************
 * drm sunxi hdmi connect functions
 ******************************************************************************/
static enum drm_connector_status
_sunxi_hdmi_drm_connector_detect(struct drm_connector *connector, bool force)
{
	struct sunxi_drm_hdmi *hdmi =  drm_connector_to_sunxi_drm_hdmi(connector);
	int ret = 0;

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return connector_status_unknown;
	}

	ret = _sunxi_drv_hdmi_hpd_get(hdmi);
	hdmi_inf("drm hdmi detect: %s\n", ret ? "connect" : "disconnect");
	return ret == 1 ? connector_status_connected : connector_status_disconnected;
}

static void _sunxi_hdmi_drm_connector_force(struct drm_connector *connector)
{
	struct sunxi_drm_hdmi *hdmi = drm_connector_to_sunxi_drm_hdmi(connector);
	char *force_string[] = {"detect", "off", "on", "dvi-on"};

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return;
	}

	hdmi->hdmi_ctrl.drm_hpd_force = (int)connector->force;
	if (connector->force == DRM_FORCE_ON)
		_sunxi_drv_hdmi_hpd_plugin(hdmi);
	else if (connector->force == DRM_FORCE_OFF)
		_sunxi_drv_hdmi_hpd_plugout(hdmi);

	hdmi_inf("drm hdmi set force hpd: %s\n", force_string[connector->force]);
}

static int _sunxi_hdmi_drm_connector_atomic_get_property(
		struct drm_connector *connector, const struct drm_connector_state *state,
		struct drm_property *property, uint64_t *val)
{
	struct sunxi_drm_hdmi *hdmi = drm_connector_to_sunxi_drm_hdmi(connector);
	struct disp_device_config *info = sunxi_hdmi_get_disp_info();

	if (hdmi->prop_tmds_mode == property)
		*val = info->dvi_hdmi;
	else if (hdmi->prop_color_space == property)
		*val = info->cs;
	else if (hdmi->prop_eotf == property)
		*val = info->eotf;
	else if (hdmi->prop_color_cap == property) {
		_sunxi_drv_hdmi_set_color_capality(hdmi);
		*val = hdmi->hdmi_ctrl.drv_color_cap;
	} else if (hdmi->prop_color_mode == property) {
		_sunxi_drv_hdmi_get_color_mode(hdmi);
		*val = hdmi->hdmi_ctrl.drv_color_mode;
	} else {
		hdmi_err("drm hdmi unsupport property: %s\n", property->name);
		return -1;
	}

	return 0;
}

static int _sunxi_hdmi_drm_connector_atomic_set_property(
		struct drm_connector *connector, struct drm_connector_state *state,
		struct drm_property *property, uint64_t val)
{
	struct sunxi_drm_hdmi *hdmi = drm_connector_to_sunxi_drm_hdmi(connector);
	int ret = 0;

	if (hdmi->prop_tmds_mode == property)
		hdmi->disp_config.dvi_hdmi = val;
	else if (hdmi->prop_color_space == property)
		hdmi->disp_config.cs = val;
	else if (hdmi->prop_eotf == property)
		hdmi->disp_config.eotf = val;
	else if (hdmi->prop_color_mode == property)
		ret = _sunxi_drv_hdmi_set_color_mode(hdmi, val);
	else {
		hdmi_err("drm hdmi unsupport property: %s\n", property->name);
		return -1;
	}

	hdmi_trace("drm hdmi set property %s: 0x%x %s\n",
		property->name, (u32)val, (ret == 0) ? "success" : "failed");

	return 0;
}

static const struct drm_connector_funcs
sunxi_hdmi_connector_funcs = {
	.detect                 = _sunxi_hdmi_drm_connector_detect,
	.force                  = _sunxi_hdmi_drm_connector_force,
	.atomic_get_property    = _sunxi_hdmi_drm_connector_atomic_get_property,
	.atomic_set_property    = _sunxi_hdmi_drm_connector_atomic_set_property,
	.fill_modes		        = drm_helper_probe_single_connector_modes,
	.destroy		        = drm_connector_cleanup,
	.reset			        = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs
sunxi_hdmi_connector_helper_funcs = {
	.get_modes	= _sunxi_hdmi_drm_connector_get_modes,
	.mode_valid = _sunxi_hdmi_drm_connector_mode_valid,
};

static const struct drm_encoder_helper_funcs
sunxi_hdmi_encoder_helper_funcs = {
	.atomic_disable		= _sunxi_hdmi_drm_encoder_atomic_disable,
	.atomic_enable		= _sunxi_hdmi_drm_encoder_atomic_enable,
	.atomic_check		= _sunxi_hdmi_drm_encoder_atomic_check,
	.mode_set           = _sunxi_hdmi_drm_encoder_mode_set,
};

static void _sunxi_hdmi_init_drm_property(struct sunxi_drm_hdmi *hdmi)
{
	struct drm_connector *connector = &hdmi->connector;
	struct disp_device_config *info = &hdmi->disp_config;
	uint64_t min_val = 0x0, max_val = 0xFFFF;

	hdmi->prop_color_cap = sunxi_drm_create_attach_property_range(connector->dev,
			&connector->base, "color_capability",
			min_val, max_val, hdmi->hdmi_ctrl.drv_color_cap);

	hdmi->prop_color_mode = sunxi_drm_create_attach_property_range(connector->dev,
			&connector->base, "color_mode",
			min_val, max_val, hdmi->hdmi_ctrl.drv_color_mode);

	hdmi->prop_color_space = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "color_space", sunxi_hdmi_prop_color_space,
			ARRAY_SIZE(sunxi_hdmi_prop_color_space), info->cs);

	hdmi->prop_eotf = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "color_eotf", sunxi_hdmi_prop_eotf,
			ARRAY_SIZE(sunxi_hdmi_prop_eotf), info->eotf);

	hdmi->prop_tmds_mode = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "tmds_mode", sunxi_hdmi_prop_tmds_mode,
			ARRAY_SIZE(sunxi_hdmi_prop_tmds_mode), info->dvi_hdmi);
}

static int _sunxi_hdmi_init_drm(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	/* drm encoder register */
	drm_encoder_helper_add(&hdmi->encoder, &sunxi_hdmi_encoder_helper_funcs);
	ret = drm_simple_encoder_init(hdmi->drm, &hdmi->encoder, DRM_MODE_ENCODER_TMDS);
	if (ret) {
		hdmi_err("hdmi init encoder for tcon %d failed!!!\n", hdmi->tcon_id);
		return ret;
	}
	hdmi->encoder.possible_crtcs =
		drm_of_find_possible_crtcs(hdmi->drm, hdmi->tcon_dev->of_node);

	/* drm connector register */
	hdmi->connector.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->connector.interlace_allowed = true;
	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;
	drm_connector_helper_add(&hdmi->connector, &sunxi_hdmi_connector_helper_funcs);
	ret = drm_connector_init_with_ddc(hdmi->drm, &hdmi->connector,
			&sunxi_hdmi_connector_funcs, DRM_MODE_CONNECTOR_HDMIA, &hdmi->i2c_adap);
	if (ret) {
		drm_encoder_cleanup(&hdmi->encoder);
		hdmi_err("hdmi init connector for tcon %d failed!!!\n", hdmi->tcon_id);
		return ret;
	}

	drm_connector_attach_encoder(&hdmi->connector, &hdmi->encoder);

	_sunxi_hdmi_init_drm_property(hdmi);

	hdmi_inf("drm hdmi create finish\n");
	return 0;
}

static int _sunxi_hdmi_init_get_tcon(struct device *dev)
{
	struct device_node *tcon_tv_node;
	struct device_node *hdmi_endpoint;
	struct platform_device *hdmi_pdev = to_platform_device(dev);
	struct platform_device *tcon_pdev = NULL;
	struct sunxi_drm_hdmi *hdmi = platform_get_drvdata(hdmi_pdev);

	hdmi_endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	if (!hdmi_endpoint) {
		hdmi_err("sunxi hdmi endpoint not fount\n");
		return -1;
	}

	tcon_tv_node = of_graph_get_remote_port_parent(hdmi_endpoint);
	if (!tcon_tv_node) {
		hdmi_err("sunxi hdmi can not get tcon tv node\n");
		goto put_endpoint;
	}

	tcon_pdev = of_find_device_by_node(tcon_tv_node);
	if (!tcon_pdev) {
		hdmi_err("sunxi hdmi can not get tcon device\n");
		goto put_tcon;
	}

	hdmi->tcon_dev = &tcon_pdev->dev;
	hdmi->tcon_id = sunxi_tcon_of_get_id(hdmi->tcon_dev);
	platform_device_put(tcon_pdev);

	return 0;
put_tcon:
	of_node_put(tcon_tv_node);
put_endpoint:
	of_node_put(hdmi_endpoint);

	return -1;
}

static int
sunxi_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	int ret = 0;
	struct sunxi_drm_hdmi  *hdmi = NULL;
	struct platform_device *pdev = to_platform_device(dev);

	hdmi_inf("hdmi devices bind start >>>>>>>>>>\n");

	hdmi = devm_kzalloc(dev, sizeof(struct sunxi_drm_hdmi), GFP_KERNEL);
	if (!hdmi) {
		hdmi_err("alloc hdmi buffer failed\n");
		goto bind_ng;
	}

	platform_set_drvdata(pdev, hdmi);

	hdmi->pdev = pdev;
	hdmi->drm = (struct drm_device *)data;

	ret = _sunxi_hdmi_init_get_tcon(dev);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init get tcon_dev failed\n");
		goto bind_ng;
	}

	/* init hdmi device sysfs */
	ret = _sunxi_hdmi_init_sysfs(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init sysfs fail\n");
		goto bind_ng;
	}

	/* init hdmi driver hardware */
	ret = _sunxi_hdmi_init_driver(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init drv fail\n");
		goto bind_ng;
	}

	ret = _sunxi_hdmi_init_drm(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init creat connect failed\n");
		goto bind_ng;
	}

	if (hdmi->hpd_task)
		wake_up_process(hdmi->hpd_task);

	hdmi_inf("hdmi devices bind end <<<<<<<<<<\n\n");
	return 0;

bind_ng:
	hdmi_err("sunxi hdmi bind failed!!!!!\n");
	return -1;
}

static void
sunxi_hdmi_unbind(struct device *dev, struct device *master, void *data)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);

	if (!pdev) {
		hdmi_err("point pdev is null\n");
		return;
	}

	ret = _sunxi_hdmi_sysfs_exit(pdev);
	if (ret != 0)
		hdmi_wrn("hdmi dev exit failed\n\n");

	ret = _sunxi_hdmi_drv_exit(pdev);
	if (ret != 0)
		hdmi_wrn("hdmi drv exit failed\n");

	hdmi_inf("hdmi drv unbind done\n");
}

static const struct of_device_id sunxi_hdmi_match[] = {
	{ .compatible =	"allwinner,sunxi-hdmi" },
	{ }
};

static const struct component_ops sunxi_hdmi_compoent_ops = {
	.bind   = sunxi_hdmi_bind,
	.unbind = sunxi_hdmi_unbind,
};

static int sunxi_hdmi_probe(struct platform_device *pdev)
{
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return component_add(&pdev->dev, &sunxi_hdmi_compoent_ops);
}

static int sunxi_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sunxi_hdmi_compoent_ops);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct dev_pm_ops sunxi_hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(_sunxi_hdmi_pm_suspend,
				_sunxi_hdmi_pm_resume)
};

struct platform_driver sunxi_hdmi_platform_driver = {
	.probe  = sunxi_hdmi_probe,
	.remove = sunxi_hdmi_remove,
	.driver = {
		   .name = "allwinner,sunxi-hdmi",
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_hdmi_match,
		   .pm = &sunxi_hdmi_pm_ops,
	},
};
