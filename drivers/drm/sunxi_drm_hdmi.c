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
#include <linux/component.h>
#include <linux/pm_runtime.h>

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
#include <video/sunxi_display2.h>

#if IS_ENABLED(CONFIG_EXTCON)
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include "../drivers/extcon/extcon.h"
#endif

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
#include <media/cec.h>
#include <media/cec-notifier.h>
#endif

#include "sunxi_device/sunxi_hdmi.h"
#include "sunxi_device/sunxi_tcon.h"

#include "sunxi_drm_intf.h"
#include "sunxi_drm_crtc.h"
#include "sunxi_drm_hdmi.h"

#define SUNXI_HDMI_SUPPORT_MAX_RATE  (60)
#define SUNXI_HDMI_EDID_LENGTH		(1024)
#define SUNXI_HDMI_POWER_CNT	     4
#define SUNXI_HDMI_POWER_NAME	     40

#define SUNXI_HDMI_HPD_FORCE_IN		(0x11)
#define SUNXI_HDMI_HPD_FORCE_OUT	(0x10)
#define SUNXI_HDMI_HPD_MASK_NOTIFY	(0x100)
#define SUNXI_HDMI_HPD_MASK_DETECT	(0x1000)

#ifdef SUNXI_HDMI20_USE_HDCP
struct sunxi_hdcp_info_s {
	unsigned int hdcp_type;
	unsigned int hdcp_status;
};
#endif

static const struct drm_prop_enum_list sunxi_hdmi_prop_tmds_mode[] = {
	{ DISP_DVI_HDMI_UNDEFINED,  "default_mode" },
	{ DISP_DVI,                 "dvi_mode"     },
	{ DISP_HDMI,                "hdmi_mode"    },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_color_format[] = {
	{ DISP_CSC_TYPE_RGB,     "rgb"    },
	{ DISP_CSC_TYPE_YUV444,  "yuv444" },
	{ DISP_CSC_TYPE_YUV422,  "yuv422" },
	{ DISP_CSC_TYPE_YUV420,  "yuv420" },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_color_depth[] = {
	{ DISP_DATA_8BITS,   "8_bits"  },
	{ DISP_DATA_10BITS,  "10_bits" },
	{ DISP_DATA_12BITS,  "12_bits" },
	{ DISP_DATA_16BITS,  "16_bits" },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_color_space[] = {
	{ DISP_BT709,  "BT709" },
	{ DISP_BT601,  "BT601" },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_color_range[] = {
	{ DISP_COLOR_RANGE_DEFAULT, "range_default" },
	{ DISP_COLOR_RANGE_0_255,   "range_0_255"   },
	{ DISP_COLOR_RANGE_16_235,  "range_16_235"  },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_eotf[] = {
	{ DISP_EOTF_GAMMA22,   "eotf_SDR"   },
	{ DISP_EOTF_SMPTE2084, "eotf_HDR10"  },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_aspect_ratio[] = {
	/* see avi info R0-R3 */
	{ HDMI_ACTIVE_ASPECT_PICTURE, "ratio_default" },
	{ HDMI_ACTIVE_ASPECT_4_3,     "ratio_4_3"     },
	{ HDMI_ACTIVE_ASPECT_16_9,    "ratio_16_9"    },
	{ HDMI_ACTIVE_ASPECT_14_9,    "ratio_14_9"    },
};

static const struct drm_prop_enum_list sunxi_hdmi_prop_scan_info[] = {
	{ DISP_SCANINFO_NO_DATA,  "none_scan"  },
	{ OVERSCAN,               "over_scan"  },
	{ UNDERSCAN,              "under_scan" },
};

#if IS_ENABLED(CONFIG_EXTCON)
static const unsigned int sunxi_hdmi_cable[] = {
	EXTCON_DISP_HDMI,
	EXTCON_NONE,
};
#endif

enum sunxi_hdmi_ioctl_cmd_e {
	SUNXI_IOCTL_HDMI_NULL            = 0,
	SUNXI_IOCTL_HDMI_HDCP22_LOAD_FW  = 1,
	SUNXI_IOCTL_HDMI_HDCP_ENABLE     = 2,
	SUNXI_IOCTL_HDMI_HDCP_DISABLE    = 3,
	SUNXI_IOCTL_HDMI_HDCP_INFO       = 4,
	SUNXI_IOCTL_HDMI_GET_LOG_SIZE    = 5,
	SUNXI_IOCTL_HDMI_GET_LOG         = 6,
	SUNXI_IOCTL_HDMI_MAX_CMD,
};

struct file_ops {
	int ioctl_cmd;
};

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


struct sunxi_hdmi_log_s {
	int level;  /* debug log level */
	bool buf_en;    /* log buffer state.  enable/disable */
	u32  buf_size;  /* log buffer size. */
};

struct sunxi_hdmi_res_s {
	char  power_name[SUNXI_HDMI_POWER_CNT][SUNXI_HDMI_POWER_NAME];
	struct regulator  *hdmi_regu[SUNXI_HDMI_POWER_CNT];
	struct pinctrl    *hdmi_pctl;
	struct gpio_config ddc_gpio;
};

struct sunxi_hdmi_dts_s {
	unsigned int hdmi_cts;
	unsigned int ddc_gpio_en;
	/* dts cec config */
	unsigned int cec_enable;
	unsigned int cec_super_standby;

	/* dts hdcp config */
	unsigned int support_hdcp;
	unsigned int support_hdcp14;
	unsigned int support_hdcp22;
	/* dts power config */
	unsigned int power_count;
};

struct sunxi_hdmi_clk_s {
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
};

struct sunxi_hdmi_state_s {
	int drm_enable;
	int drm_mode_set;
	int drm_hpd_force;

	int drv_clock;
	int drv_enable;
	int drv_hpd_state;
	int drv_hpd_mask;
	int drv_hdcp;
	int drv_hdcp14;
	int drv_hdcp22;

	sunxi_hdcp_type_t drv_hdcp_type;
};

struct sunxi_hdmi_edid_s {
	struct edid    *edid;
	struct mutex    edid_lock;
	u8     dbg_mode;
	u8     dbg_data[SUNXI_HDMI_EDID_LENGTH];
	u32    dbg_size;
};

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
struct sunxi_hdmi_cec_s {
	struct platform_device	*pdev;
	struct cec_notifier		*notify;
	struct cec_adapter		*adap;
	struct clk		        *cec_clk;
	struct clk		        *cec_clk_parent;
	struct cec_msg          rx_msg;

	int    irq;
	bool   tx_done;
	bool   rx_done;
	u8     tx_status;
	u8     enable;
	u16    logic_addr;
};
#endif

struct sunxi_drm_hdmi {
	/* drm related members */
	struct drm_device        *drm;
	struct drm_encoder        encoder;
	struct drm_connector      connector;
	struct drm_display_mode   drm_mode;
	struct tcon_device       *tcon_dev;
	unsigned int              tcon_id;

	struct drm_property   *prop_color_fm;
	struct drm_property   *prop_color_depth;
	struct drm_property   *prop_color_space;
	struct drm_property   *prop_color_range;
	struct drm_property   *prop_aspect_ratio;
	struct drm_property   *prop_tmds_mode;
	struct drm_property   *prop_eotf;
	struct drm_property   *prop_scan;

	struct platform_device         *pdev;
	int hdmi_irq;
	int reg_bank;

	/* hdmi hpd work */
	struct task_struct   *hpd_task;
#if IS_ENABLED(CONFIG_EXTCON)
	struct extcon_dev    *hpd_extcon;
#endif

	/* sunxi hdmi private devices data */
	dev_t                    hdmi_devid;
	struct cdev              hdmi_cdev;
	struct class            *hdmi_class;
	struct device           *hdmi_dev;
	struct file_operations   hdmi_ops;
	/* sunxi hdmi private devices data */

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
	struct sunxi_hdmi_cec_s       cec_drv;
#endif

	struct i2c_adapter             i2c_adap;
	struct sunxi_hdmi_log_s        hdmi_log;
	struct sunxi_hdmi_state_s      hdmi_state;
	struct sunxi_hdmi_edid_s       hdmi_edid;
	/* sunxi hdmi platform clock */
	struct sunxi_hdmi_clk_s        hdmi_clk;
	/* sunxi hdmi dts config */
	struct sunxi_hdmi_dts_s        hdmi_dts;
	/* suxni hdmi resource config */
	struct sunxi_hdmi_res_s        hdmi_res;
	/* disp hdmi config param */
	struct disp_device_config      disp_config;
	/* disp hdmi timing param */
	struct disp_video_timings      timing;
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
 * sunxi hdmi hdcp function
 ******************************************************************************/
#ifdef SUNXI_HDMI20_USE_HDCP
int sunxi_drv_hdcp_get_state(struct sunxi_drm_hdmi *hdmi)
{
	if (hdmi->hdmi_state.drv_hdcp14 || hdmi->hdmi_state.drv_hdcp22)
		return sunxi_hdcp_get_state();
	else
		return SUNXI_HDCP_DISABLE;
}

int sunxi_drv_hdcp_set_config(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0, old_state = 0;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		goto error_exit;
	}

	if (!hdmi->hdmi_dts.support_hdcp) {
		hdmi_inf("hdmi drv not config hdcp when dts not set hdcp.\n");
		goto normal_exit;
	}

	/* disable config */
	if (hdmi->hdmi_state.drv_hdcp == 0x0) {
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
		if (hdmi->hdmi_state.drv_hdcp22) {
			ret = sunxi_hdcp22_set_config(0x0);
			if (ret != 0)
				hdmi_err("hdmi drv disable hdcp22 failed\n");
		}
#endif
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14)
		if (hdmi->hdmi_state.drv_hdcp14) {
			ret = sunxi_hdcp14_set_config(0x0);
			if (ret != 0)
				hdmi_err("hdmi drv disable hdcp14 failed\n");
		}
#endif
		hdmi->hdmi_state.drv_hdcp_type = SUNXI_HDCP_TYPE_NULL;
		goto normal_exit;
	}

	/* enable config */
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	if (hdmi->hdmi_dts.support_hdcp22) {
		old_state = hdmi->hdmi_state.drv_hdcp22;
		if (old_state == 0x1) {
			hdmi_inf("hdmi drv has been enable hdcp22.");
			goto normal_exit;
		}
		/* check sink support */
		ret = sunxi_hdcp_get_sink_cap();
		if (ret != 0x1) {
			hdmi_err("hdmi drv check sink not support hdcp22.\n");
			goto enable_hdcp14;
		}
		ret = sunxi_hdcp22_set_config(0x1);
		if (ret != 0x0) {
			hdmi_err("hdmi drv enable hdcp22 failed, change use hdcp14\n");
			goto enable_hdcp14;
		}
		hdmi->hdmi_state.drv_hdcp22 = 0x1;
		hdmi->hdmi_state.drv_hdcp_type = SUNXI_HDCP_TYPE_HDCP22;
		hdmi_inf("hdmi drv enable hdcp22 done\n");
		goto normal_exit;
	}
#else
	goto enable_hdcp14;
#endif

enable_hdcp14:
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14)
	if (hdmi->hdmi_dts.support_hdcp14) {
		old_state = hdmi->hdmi_state.drv_hdcp14;
		if (old_state == 0x1) {
			hdmi_inf("hdmi drv has been enable hdcp14.");
			goto normal_exit;
		}

		ret = sunxi_hdcp14_set_config(0x1);
		if (ret != 0) {
			hdmi_err("hdmi drv enable hdcp14 failed\n");
			goto error_exit;
		}

		hdmi_inf("hdmi drv enable hdcp14 done\n");
		hdmi->hdmi_state.drv_hdcp14 = 0x1;
		hdmi->hdmi_state.drv_hdcp_type = SUNXI_HDCP_TYPE_HDCP14;
	}
#endif

normal_exit:
	return 0;
error_exit:
	return -1;
}
#endif

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
static s32 sunxi_drv_audio_enable(u8 enable, u8 channel)
{
	int ret = 0;

	if (enable)
		ret = sunxi_hdmi_audio_setup();

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
static s32 sunxi_drv_audio_config_params(hdmi_audio_t *audio_para)
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

	hdmi_func->hdmi_audio_enable   = sunxi_drv_audio_enable;
	hdmi_func->hdmi_set_audio_para = sunxi_drv_audio_config_params;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdmi_get_func);
#endif

static int _sunxi_drv_hdmi_set_gpio(struct sunxi_drm_hdmi *hdmi, u8 state)
{
	struct sunxi_hdmi_dts_s *pcfg = NULL;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}
	pcfg = &hdmi->hdmi_dts;

	if (pcfg->ddc_gpio_en == 0)
		return 0;

	return gpio_direction_output(hdmi->hdmi_res.ddc_gpio.gpio, state ? 0x1 : 0x0);
}

static int _sunxi_drv_hdmi_set_power_domain(struct device *dev, u8 state)
{
	if (!dev) {
		hdmi_err("check point dev is null\n");
		return -1;
	}

	if (state) {
		pm_runtime_enable(dev);
		pm_runtime_get_sync(dev);
	} else {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}

	hdmi_inf("hdmi drv %s domain\n", state ? "enable" : "disable");
	return 0;
}

static int _sunxi_drv_hdmi_set_power(struct regulator *power, u8 state)
{
	int ret = 0;

	if (!power) {
		hdmi_err("hdmi drv enable power is null\n");
		return -1;
	}

	if (state == SUNXI_HDMI_ENABLE)
		ret = regulator_enable(power);
	else
		ret = regulator_disable(power);

	if (ret != 0) {
		hdmi_err("hdmi drv %s power is failed\n",
			state == SUNXI_HDMI_ENABLE ? "enable" : "disable");
		return ret;
	}

	hdmi_inf("hdmi drv power %s success\n",
		state == SUNXI_HDMI_ENABLE ? "enable" : "disable");
	return 0;
}

static int _sunxi_drv_hdmi_clk_enable(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;
	struct sunxi_hdmi_clk_s *pclk = NULL;

	if (!hdmi) {
		hdmi_err("point pdrv is null\n");
		return -1;
	}

	pclk = &hdmi->hdmi_clk;
	if (!pclk) {
		hdmi_err("point pclk is null\n");
		return -1;
	}

	/* enable hdmi clock */
	if (pclk->rst_bus_sub) {
		ret = reset_control_deassert(pclk->rst_bus_sub);
		if (ret != 0) {
			hdmi_err("clock config deassert sub bus failed\n");
			return -1;
		}
	}

	if (pclk->rst_bus_main) {
		ret = reset_control_deassert(pclk->rst_bus_main);
		if (ret != 0) {
			hdmi_err("clock config deassert main bus failed\n");
			return -1;
		}
	}

	if (pclk->clk_hdmi) {
		ret = clk_prepare_enable(pclk->clk_hdmi);
		if (ret != 0) {
			hdmi_err("clock config enable hdmi clk failed\n");
			return -1;
		}
	}

	if (pclk->clk_hdmi_24M) {
		ret = clk_prepare_enable(pclk->clk_hdmi_24M);
		if (ret != 0) {
			hdmi_err("clock config enable hdmi 24m clk failed\n");
			return -1;
		}
	}

	/* enable ddc clock */
	if (pclk->clk_hdmi_ddc) {
		ret = clk_prepare_enable(pclk->clk_hdmi_ddc);
		if (ret != 0) {
			hdmi_err("clock config enable ddc clk failed\n");
			return -1;
		}
	}

	/* enable hdcp clock */
	if (pclk->rst_bus_hdcp) {
		ret = reset_control_deassert(pclk->rst_bus_hdcp);
		if (ret != 0) {
			hdmi_err("clock config deassert hdcp bus failed\n");
			return -1;
		}
	}

	if (pclk->clk_hdcp_bus) {
		ret = clk_prepare_enable(pclk->clk_hdcp_bus);
		if (ret != 0) {
			hdmi_err("clock config enable hdcp bus clk failed\n");
			return -1;
		}
	}

	if (pclk->clk_hdcp) {
		clk_set_rate(pclk->clk_hdcp, 300000000);
		ret = clk_prepare_enable(pclk->clk_hdcp);
		if (ret != 0) {
			hdmi_err("clock config enable hdcp clk failed\n");
			return -1;
		}
	}

	hdmi->hdmi_state.drv_clock = 0x1;
	return 0;
}

static int _sunxi_drv_hdmi_clk_disable(struct sunxi_drm_hdmi *hdmi)
{
	struct sunxi_hdmi_clk_s *pclk = NULL;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}
	pclk = &hdmi->hdmi_clk;

	/* disable hdcp clock */
	if (pclk->clk_hdcp) {
		clk_disable_unprepare(pclk->clk_hdcp);
	}

	if (pclk->clk_hdcp_bus) {
		clk_disable_unprepare(pclk->clk_hdcp_bus);
	}

	if (pclk->rst_bus_hdcp) {
		reset_control_assert(pclk->rst_bus_hdcp);
	}

	/* disable ddc clock */
	if (pclk->clk_hdmi_ddc) {
		clk_disable_unprepare(pclk->clk_hdmi_ddc);
	}

	/* disable hdmi clock */
	if (pclk->clk_hdmi_24M) {
		clk_disable_unprepare(pclk->clk_hdmi_24M);
	}
	if (pclk->clk_hdmi) {
		clk_disable_unprepare(pclk->clk_hdmi);
	}

	if (pclk->rst_bus_sub) {
		reset_control_assert(pclk->rst_bus_sub);
	}

	if (pclk->rst_bus_main) {
		reset_control_assert(pclk->rst_bus_main);
	}

	hdmi->hdmi_state.drv_clock = 0x0;
	return 0;
}

static int _sunxi_drv_hdmi_read_edid(struct sunxi_drm_hdmi *hdmi)
{
	int ret = -1;

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return ret;
	}

	mutex_lock(&hdmi->hdmi_edid.edid_lock);

	if (hdmi->hdmi_edid.dbg_mode) {
		hdmi_inf("hdmi drv use debug edid\n");
		hdmi->hdmi_edid.edid = (struct edid *)&hdmi->hdmi_edid.dbg_data;
		goto edid_parse;
	}

	hdmi->hdmi_edid.edid = NULL;
	hdmi->hdmi_edid.edid = drm_get_edid(&hdmi->connector, &hdmi->i2c_adap);
	if (!hdmi->hdmi_edid.edid) {
		hdmi_err("hdmi drv get edid failed\n");
		hdmi->hdmi_edid.edid = NULL;
		goto exit;
	}

edid_parse:
	sunxi_edid_parse((u8 *)hdmi->hdmi_edid.edid);
	ret = 0;

exit:
	mutex_unlock(&hdmi->hdmi_edid.edid_lock);
	return ret;
}

static int _sunxi_drv_hdmi_hpd_plugin(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	hdmi_inf("hdmi drv detect hpd connect\n");

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	/* set ddc gpio config */
	ret = _sunxi_drv_hdmi_set_gpio(hdmi, 0x1);
	if (ret != 0) {
		hdmi_err("config gpio output failed!!!\n");
		return -1;
	}


	return 0;
}

static int _sunxi_drv_hdmi_hpd_plugout(struct sunxi_drm_hdmi *hdmi)
{
	hdmi_inf("hdmi drv detect hpd disconnect\n");

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
	cec_notifier_phys_addr_invalidate(hdmi->cec_drv.notify);
#endif

	_sunxi_drv_hdmi_set_gpio(hdmi, 0x0);

	hdmi->hdmi_edid.edid = NULL;
	return 0;
}

static int _sunxi_drv_hdmi_hpd_get(struct sunxi_drm_hdmi *hdmi)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null!!!\n");
		return 0;
	}

	return hdmi->hdmi_state.drv_hpd_state ? 0x1 : 0x0;
}

static int _sunxi_drv_hdmi_hpd_set(struct sunxi_drm_hdmi *hdmi, u8 state)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null!!!\n");
		return -1;
	}

	hdmi->hdmi_state.drv_hpd_state = state;
	return 0;
}

static int _sunxi_drv_hdmi_hpd_repo(struct sunxi_drm_hdmi *hdmi)
{
	u8 state = 0;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	if (hdmi->hdmi_state.drv_hpd_mask & SUNXI_HDMI_HPD_MASK_NOTIFY)
		return 0;

	state = _sunxi_drv_hdmi_hpd_get(hdmi);
#if IS_ENABLED(CONFIG_EXTCON)
	extcon_set_state_sync(hdmi->hpd_extcon, EXTCON_DISP_HDMI,
			state ? true : false);
#endif
	return 0;
}

/*******************************************************************************
 * drm sunxi hdmi driver functions
 ******************************************************************************/
static int _sunxi_drv_hdmi_set_rate(struct sunxi_hdmi_clk_s *p_clk)
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

	if (hdmi->hdmi_state.drv_enable) {
		hdmi_inf("hdmi drv has been enable!\n");
		return 0;
	}

	_sunxi_drv_hdmi_set_rate(&hdmi->hdmi_clk);

	/* hdmi driver video ops enable */
	sunxi_hdmi_config();

	/* enable hdcp */
#ifdef SUNXI_HDMI20_USE_HDCP
	sunxi_drv_hdcp_set_config(hdmi);
#endif

	hdmi->hdmi_state.drv_enable = 0x1;
	return 0;
}

static int _sunxi_drv_hdmi_disable(struct sunxi_drm_hdmi *hdmi)
{
	if (!hdmi) {
		hdmi_err("point hdmi is null");
		return -1;
	}

	if (!hdmi->hdmi_state.drv_enable) {
		hdmi_inf("hdmi drv has been disable!\n");
		return 0;
	}

	sunxi_hdmi_disconfig();

#ifdef SUNXI_HDMI20_USE_HDCP
	hdmi->hdmi_state.drv_hdcp = 0x0;
	sunxi_drv_hdcp_set_config(hdmi);
#endif

	/* disable hdmi module */
	sunxi_hdmi_close();

	/* TODO, tcon pad release */

	hdmi->hdmi_state.drv_enable = 0x0;
	return 0;
}

static void _sunxi_drv_hdmi_vblank_enable(bool enable, void *data)
{
	/* TODO, NULL */
	return;
}

int _sunxi_drv_hdmi_check_disp_info(struct sunxi_drm_hdmi *hdmi)
{
	struct disp_device_config *info = sunxi_hdmi_video_get_info();
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

/**
 * @return: 0 - normal
 *          1 - hpd force mode
 *          2 - hpd bypass mode
 */
static int _sunxi_drv_hdmi_check_hpd_mask(struct sunxi_drm_hdmi *hdmi)
{
	if ((hdmi->hdmi_state.drv_hpd_mask& SUNXI_HDMI_HPD_FORCE_IN) ==
			SUNXI_HDMI_HPD_FORCE_IN) {
		if (!_sunxi_drv_hdmi_hpd_get(hdmi)) {
			_sunxi_drv_hdmi_hpd_set(hdmi, 0x1);
			return 1;
		} else
			return 2;
	} else if ((hdmi->hdmi_state.drv_hpd_mask & SUNXI_HDMI_HPD_FORCE_OUT) ==
			SUNXI_HDMI_HPD_FORCE_OUT) {
		if (_sunxi_drv_hdmi_hpd_get(hdmi)) {
			_sunxi_drv_hdmi_hpd_set(hdmi, 0x0);
			return 1;
		} else
			return 2;
	} else if ((hdmi->hdmi_state.drv_hpd_mask & SUNXI_HDMI_HPD_MASK_DETECT) ==
			SUNXI_HDMI_HPD_MASK_DETECT)
		return 2;
	else
		return 0;
}

static int _sunxi_drv_hdmi_thread(void *parg)
{
	int temp_hpd = 0, msecs = 0;
	struct sunxi_drm_hdmi *hdmi = (struct sunxi_drm_hdmi *)parg;

	if (!hdmi) {
		hdmi_err("%s param hdmi is null!!!\n", __func__);
		return -1;
	}

	while (1) {
		if (kthread_should_stop()) {
			hdmi_wrn("hdmi drv hpd thread is stop!!!\n");
			break;
		}

		temp_hpd = _sunxi_drv_hdmi_check_hpd_mask(hdmi);
		if (temp_hpd == 0x1)
			goto handle_change;
		else if (temp_hpd == 0x2)
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
		if (temp_hpd) {
			msecs = 1;
			_sunxi_drv_hdmi_hpd_plugin(hdmi);
		} else {
			msecs = 1;
			_sunxi_drv_hdmi_hpd_plugout(hdmi);
		}

		_sunxi_drv_hdmi_hpd_set(hdmi, temp_hpd);

		_sunxi_drv_hdmi_hpd_repo(hdmi);
		if (hdmi->drm)
			drm_helper_hpd_irq_event(hdmi->drm);

next_loop:
		msleep(20);
	}

	return 0;
}

/*******************************************************************************
 * @desc: sunxi hdmi driver init function
 ******************************************************************************/
static int
_sunxi_drv_hdmi_init_dts_info(struct sunxi_drm_hdmi *hdmi)
{
	u32 value = 0;
	int ret = 0;
	struct device *dev = &hdmi->pdev->dev;
	struct sunxi_hdmi_dts_s *pcfg = &hdmi->hdmi_dts;
	struct sunxi_hdmi_clk_s  *pclk = &hdmi->hdmi_clk;

	if (!dev) {
		hdmi_err("point dev is null\n");
		return -1;
	}

	if (!pcfg) {
		hdmi_err("point pcfg is null\n");
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
#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
	hdmi->cec_drv.irq = hdmi->hdmi_irq;
#endif

	ret = of_property_read_u32(dev->of_node, "hdmi_cts_compatibility", &value);
	pcfg->hdmi_cts = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "ddc_en_io_ctrl", &value);
	pcfg->ddc_gpio_en = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_cec_support", &value);
	pcfg->cec_enable = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_cec_super_standby", &value);
	pcfg->cec_super_standby = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_hdcp_enable", &value);
	pcfg->support_hdcp = (ret != 0x0) ? 0x0 : value;

	/* new node, some plat maybe not set, so default hdcp14 enable */
	ret = of_property_read_u32(dev->of_node, "hdmi_hdcp14_enable", &value);
	pcfg->support_hdcp14 = (ret != 0x0) ? 0x1 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_hdcp22_enable", &value);
	pcfg->support_hdcp22 = (ret != 0x0) ? 0x0 : value;

	ret = of_property_read_u32(dev->of_node, "hdmi_power_cnt", &value);
	pcfg->power_count = (ret != 0x0) ? 0x0 : value;

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

static int
_sunxi_drv_hdmi_init_notify(struct sunxi_drm_hdmi *hdmi)
{
#if IS_ENABLED(CONFIG_EXTCON)
	struct device *dev = NULL;
#endif
	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

#if IS_ENABLED(CONFIG_EXTCON)
	dev = &hdmi->pdev->dev;
	hdmi->hpd_extcon = devm_extcon_dev_allocate(dev, sunxi_hdmi_cable);
	if (IS_ERR(hdmi->hpd_extcon)) {
		hdmi_err("init notify alloc extcon node failed!!!\n");
		return -1;
	}

	if (devm_extcon_dev_register(dev, hdmi->hpd_extcon) != 0) {
		hdmi_err("init notify register extcon node failed!!!\n");
		return -1;
	}

	hdmi->hpd_extcon->name = "drm-hdmi";
#else
	hdmi_wrn("not set hdmi notify node!\n");
	return 0;
#endif

	/* init notify hpd unplug */
	_sunxi_drv_hdmi_hpd_repo(hdmi);

	return 0;
}

static int
_sunxi_drv_hdmi_init_resource(struct device *dev, struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0, loop = 0;
	const char *power_string;
	char power_name[40];

	struct sunxi_hdmi_dts_s *pcfg = &hdmi->hdmi_dts;
	struct sunxi_hdmi_res_s *pres = &hdmi->hdmi_res;
	struct regulator *regulator = NULL;

	if (!pcfg) {
		hdmi_err("point cfg is null\n");
		return -1;
	}

	if (!pres) {
		hdmi_err("point res is null\n");
		return -1;
	}

	if (!dev) {
		hdmi_err("point dev is null\n");
		return -1;
	}

	/* get power regulator */
	for (loop = 0; loop < pcfg->power_count; loop++) {
		sprintf(power_name, "hdmi_power%d", loop);
		ret = of_property_read_string(dev->of_node, power_name, &power_string);
		if (ret != 0) {
			hdmi_wrn("hdmi dts not set: hdmi_power%d!\n", loop);
			continue;
		} else {
			memcpy((void *)pres->power_name[loop],
					power_string, strlen(power_string) + 1);
			hdmi_inf("hdmi drv power name: %s\n", pres->power_name[loop]);
			regulator = regulator_get(dev, pres->power_name[loop]);
			if (!regulator) {
				hdmi_wrn("init resource to get %s regulator is failed\n",
					pres->power_name[loop]);
				continue;
			} else {
				pres->hdmi_regu[loop] = regulator;
				_sunxi_drv_hdmi_set_power(regulator, SUNXI_HDMI_ENABLE);
			}
		}
	}

	_sunxi_drv_hdmi_clk_enable(hdmi);

	mutex_init(&hdmi->hdmi_edid.edid_lock);

	hdmi->hdmi_state.drv_hdcp_type = SUNXI_HDCP_TYPE_NULL;

	/* sunxi hdmi init notify node */
	_sunxi_drv_hdmi_init_notify(hdmi);

	return 0;
}

static int _sunxi_drv_hdmi_init_config_value(struct sunxi_drm_hdmi *hdmi)
{
	struct disp_device_config *init_info = &hdmi->disp_config;

	init_info->type         = DISP_OUTPUT_TYPE_HDMI;
	init_info->format       = DISP_CSC_TYPE_RGB;
	init_info->bits         = DISP_DATA_8BITS;
	init_info->eotf         = DISP_EOTF_GAMMA22; /* SDR */
	init_info->cs           = DISP_BT709;
	init_info->dvi_hdmi     = DISP_HDMI;
	init_info->range        = DISP_COLOR_RANGE_DEFAULT;
	init_info->scan         = DISP_SCANINFO_NO_DATA;
	init_info->aspect_ratio = HDMI_ACTIVE_ASPECT_PICTURE;

	return 0;
}

static int _sunxi_hdmi_i2c_xfer(struct i2c_adapter *adap,
			    struct i2c_msg *msgs, int num)
{
	return sunxi_hdmi_i2c_xfer(msgs, num);
}

static u32 _sunxi_hdmi_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm sunxi_hdmi_i2cm_algo = {
	.master_xfer	= _sunxi_hdmi_i2c_xfer,
	.functionality	= _sunxi_hdmi_i2c_func,
};

static int _sunxi_drv_hdmi_init_i2cm_adap(struct sunxi_drm_hdmi *hdmi)
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

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
static int _sunxi_drv_cec_set_clock(struct sunxi_drm_hdmi *hdmi, u8 state)
{
	struct sunxi_hdmi_clk_s *pclk = &hdmi->hdmi_clk;
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

static int _sunxi_drv_cec_enable(struct sunxi_drm_hdmi *hdmi, u8 state)
{
	if (hdmi->cec_drv.enable == state) {
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

	hdmi->cec_drv.enable = state;
	hdmi_inf("sunxi cec drv %s finish\n",
			state == SUNXI_HDMI_ENABLE ? "enable" : "disable");
	return 0;
}

static void _sunxi_cec_adap_del(void *data)
{
	struct sunxi_hdmi_cec_s *cec = data;

	cec_delete_adapter(cec->adap);
}

static int _sunxi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);

	log_trace1(enable);
	_sunxi_drv_cec_enable(hdmi, enable);
	return 0;
}

static int _sunxi_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);

	hdmi_inf("cec adapter set logical addr: 0x%x\n", addr);
	if (addr == CEC_LOG_ADDR_INVALID)
		hdmi->cec_drv.logic_addr = 0x0;
	else
		hdmi->cec_drv.logic_addr = BIT(addr);

	sunxi_cec_set_logic_addr(hdmi->cec_drv.logic_addr);
	return 0;
}

static int _sunxi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
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

	suxni_cec_message_send(msg->msg, msg->len, times);
	return 0;
}

static irqreturn_t _sunxi_cec_hardirq(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);
	int len;

	u8 stat = sunxi_cec_get_irq_state();

	if (stat == SUNXI_CEC_IRQ_NULL)
		goto none_exit;

	if (stat & SUNXI_CEC_IRQ_ERR_INITIATOR) {
		hdmi->cec_drv.tx_status = CEC_TX_STATUS_ERROR;
		hdmi->cec_drv.tx_done   = true;
		goto wake_exit;
	} else if (stat & SUNXI_CEC_IRQ_DONE) {
		hdmi->cec_drv.tx_status = CEC_TX_STATUS_OK;
		hdmi->cec_drv.tx_done = true;
		goto wake_exit;
	} else if (stat & SUNXI_CEC_IRQ_NACK) {
		hdmi->cec_drv.tx_status = CEC_TX_STATUS_NACK;
		hdmi->cec_drv.tx_done = true;
		goto wake_exit;
	}

	if (stat & SUNXI_CEC_IRQ_EOM) {
		len = sunxi_cec_message_receive((u8 *)&hdmi->cec_drv.rx_msg.msg);
		if (len < 0)
			goto none_exit;

		hdmi->cec_drv.rx_msg.len = len;
		smp_wmb();
		hdmi->cec_drv.rx_done = true;
		goto wake_exit;
	}

none_exit:
	return IRQ_NONE;
wake_exit:
	return IRQ_WAKE_THREAD;
}

static irqreturn_t _sunxi_cec_thread(int irq, void *data)
{
	struct cec_adapter *adap    = data;
	struct sunxi_drm_hdmi *hdmi = cec_get_drvdata(adap);

	if (hdmi->cec_drv.tx_done) {
		hdmi->cec_drv.tx_done = false;
		cec_transmit_attempt_done(adap, hdmi->cec_drv.tx_status);
	}

	if (hdmi->cec_drv.rx_done) {
		hdmi->cec_drv.rx_done = false;
		smp_rmb();
		cec_received_msg(adap, &hdmi->cec_drv.rx_msg);
	}
	return IRQ_HANDLED;
}

static const struct cec_adap_ops _sunxi_cec_ops = {
	.adap_enable   = _sunxi_cec_adap_enable,
	.adap_log_addr = _sunxi_cec_adap_log_addr,
	.adap_transmit = _sunxi_cec_adap_transmit,
};

static int _sunxi_drv_hdmi_init_cec_adap(struct sunxi_drm_hdmi *hdmi)
{
	int ret = -1;
	struct sunxi_hdmi_cec_s *cec = &hdmi->cec_drv;
	struct platform_device *pdev = hdmi->pdev;

	if (!hdmi->hdmi_dts.cec_enable) {
		hdmi_inf("hdmi dts not set use cec\n");
		return 0;
	}

	cec->adap = cec_allocate_adapter(&_sunxi_cec_ops, hdmi, "sunxi_cec",
			CEC_CAP_DEFAULTS | CEC_CAP_CONNECTOR_INFO, CEC_MAX_LOG_ADDRS);

	if (IS_ERR(cec->adap))
		return PTR_ERR(cec->adap);

	cec->adap->owner = THIS_MODULE;

	ret = devm_add_action(&pdev->dev, _sunxi_cec_adap_del, cec);
	if (ret) {
		cec_delete_adapter(cec->adap);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, cec->irq,
			_sunxi_cec_hardirq, _sunxi_cec_thread,
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

	devm_remove_action(&pdev->dev, _sunxi_cec_adap_del, cec);

	hdmi_inf("hdmi drv init cec adapter finish\n");
	return 0;
}
#endif

static int _sunxi_drv_hdmi_init_adapter(struct sunxi_drm_hdmi *hdmi)
{
	int ret = 0;

	ret = _sunxi_drv_hdmi_init_i2cm_adap(hdmi);
	if (ret != 0) {
		hdmi_err("hdmi drv init i2c adapter failed\n");
		return -1;
	}

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
	ret = _sunxi_drv_hdmi_init_cec_adap(hdmi);
	if (ret != 0) {
		hdmi_err("hdmi drv init cec adapter failed\n");
		return -1;
	}
#endif

	sunxi_hdmi_adap_bind(&hdmi->i2c_adap);
	return 0;
}

static int _sunxi_drv_hdmi_init_thread(struct sunxi_drm_hdmi *hdmi)
{
	hdmi->hpd_task =
		kthread_create(_sunxi_drv_hdmi_thread, (void *)hdmi, "hdmi hpd");
	if (!hdmi->hpd_task) {
		hdmi_err("init thread handle hpd is failed!!!\n");
		hdmi->hpd_task = NULL;
		return -1;
	}
	wake_up_process(hdmi->hpd_task);

	return 0;
}

static int _sunxi_drv_hdmi_init(struct sunxi_drm_hdmi *hdmi)
{
	int ret = -1;
	struct sunxi_hdmi_s  *pcore  = &hdmi->hdmi_core;
	struct platform_device *pdev = hdmi->pdev;

	/* parse sunxi hdmi dts */
	ret = _sunxi_drv_hdmi_init_dts_info(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init dts failed!!!\n");
		return -1;
	} else
		hdmi_inf("hdmi drv init dts done\n");

	/* sunxi hdmi resource alloc and enable */
	ret = _sunxi_drv_hdmi_init_resource(&pdev->dev, hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init resource failed!!!\n");
		return -1;
	} else
		hdmi_inf("hdmi drv init resource done\n");

	/* sunxi hdmi config value init */
	ret = _sunxi_drv_hdmi_init_config_value(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init config value failed\n");
	}

	/* sunxi hdmi core level init */
	ret = sunxi_hdmi_init(pcore);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init core failed!!!\n");
		return -1;
	}

	ret = _sunxi_drv_hdmi_init_adapter(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init adapter failed\n");
		return -1;
	}

	ret = _sunxi_drv_hdmi_init_thread(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init thread failed!!!\n");
		return -1;
	}
	return 0;
}

static int _sunxi_drv_hdmi_exit(struct platform_device *pdev)
{
	struct sunxi_drm_hdmi *hdmi = platform_get_drvdata(pdev);
	int i = 0;

	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	if (hdmi->hpd_task) {
		/* destory hdmi hpd thread */
		kthread_stop(hdmi->hpd_task);
		hdmi->hpd_task = NULL;
	}

	i2c_del_adapter(&hdmi->i2c_adap);

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
	cec_notifier_cec_adap_unregister(hdmi->cec_drv.notify, hdmi->cec_drv.adap);
	cec_unregister_adapter(hdmi->cec_drv.adap);
#endif

	for (i = 0; i < hdmi->hdmi_dts.power_count; i++) {
		_sunxi_drv_hdmi_set_power(hdmi->hdmi_res.hdmi_regu[i], SUNXI_HDMI_DISABLE);
		regulator_put(hdmi->hdmi_res.hdmi_regu[i]);
	}

	hdmi = NULL;
	return 0;
}

static ssize_t sunxi_hdmi_cmd_reg_read_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\n[register read]\n");
	n += sprintf(buf + n, "Demo: echo offset,count > read\n");
	n += sprintf(buf + n, " - offset: register offset address.\n");
	n += sprintf(buf + n, " - count: read count register\n");
	return n;
}

ssize_t sunxi_hdmi_cmd_reg_read_store(struct device *dev,
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
		switch (hdmi->reg_bank) {
		case SUNXI_HDMI_REG_BANK_PHY:
			sunxi_hdmi_phy_read((u8)start_reg, &r_value);
			hdmi_inf("phy read: 0x%x = 0x%x\n", (u8)start_reg, r_value);
			break;
		case SUNXI_HDMI_REG_BANK_SCDC:
#ifndef SUXNI_HDMI_USE_HDMI14
			r_value = sunxi_hdmi_scdc_read((u8)start_reg);
			hdmi_inf("scdc read: 0x%x = 0x%x\n", (u8)start_reg, (u8)r_value);
#endif
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

static ssize_t sunxi_hdmi_cmd_reg_write_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\n[register write]\n");
	n += sprintf(buf + n, "Demo: echo offset,value > write\n");
	n += sprintf(buf + n, " - offset: register offset address.\n");
	n += sprintf(buf + n, " - value: write value\n");
	return n;
}

ssize_t sunxi_hdmi_cmd_reg_write_store(struct device *dev,
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
	switch (hdmi->reg_bank) {
	case SUNXI_HDMI_REG_BANK_PHY:
		sunxi_hdmi_phy_write((u8)reg_addr, (u32)value);
		hdmi_inf("phy write: 0x%x = 0x%x\n", (u8)reg_addr, (u32)value);
		break;
	case SUNXI_HDMI_REG_BANK_SCDC:
#ifndef SUXNI_HDMI_USE_HDMI14
		sunxi_hdmi_scdc_write(reg_addr, value);
		hdmi_inf("scdc write: 0x%x = 0x%x\n", (u8)reg_addr, (u8)value);
#endif
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

static ssize_t sunxi_hdmi_cmd_reg_bank_show(struct device *dev,
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
		hdmi->reg_bank);
	return n;
}

ssize_t sunxi_hdmi_cmd_reg_bank_store(struct device *dev,
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

	hdmi->reg_bank = bank;
	hdmi_inf("switch reg bank index: %d\n", hdmi->reg_bank);
	return count;
}

static ssize_t sunxi_hdmi_cmd_pattern_show(struct device *dev,
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

ssize_t sunxi_hdmi_cmd_pattern_store(struct device *dev,
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

static ssize_t sunxi_hdmi_cmd_debug_show(struct device *dev,
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
	n += sprintf(buf + n, "current log level: %d\n", hdmi->hdmi_log.level);

	return n;
}

ssize_t sunxi_hdmi_cmd_debug_store(struct device *dev,
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

	hdmi->hdmi_log.level = level;
	suxni_hdmi_set_loglevel(level);
	return count;
}

static ssize_t sunxi_hdmi_cmd_hdmi_source_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0, loop = 0;
	u8 data = 0, status = 0;
	struct sunxi_drm_hdmi   *hdmi = dev_get_drvdata(dev);
	struct sunxi_hdmi_dts_s *pdts = &hdmi->hdmi_dts;
	struct sunxi_hdmi_res_s *pres = &hdmi->hdmi_res;
	struct drm_display_info *info = &hdmi->connector.display_info;

	n += sprintf(buf + n, "\n----------------- sunxi hdmi -----------------\n");
	n += sprintf(buf + n, " - dts: cts [%d], ddc gpio [%d], cec [%d],",
		pdts->hdmi_cts, pdts->ddc_gpio_en, pdts->cec_enable);
	n += sprintf(buf + n, "  hdcp [%d], hdcp14 [%d], hdcp22 [%d]\n",
		pdts->support_hdcp, pdts->support_hdcp14, pdts->support_hdcp22);

	n += sprintf(buf + n, " - drm state: enable [%d], mode_set [%d], mode_info [%d*%d], get_hpd [%d], hpd_force [%d]\n",
		hdmi->hdmi_state.drm_enable, hdmi->hdmi_state.drm_mode_set,
		hdmi->drm_mode.hdisplay, hdmi->drm_mode.vdisplay,
		_sunxi_drv_hdmi_hpd_get(hdmi), hdmi->hdmi_state.drm_hpd_force);

	n += sprintf(buf + n, " - drv state: hpd_thread[%s], clock[%s], status[%s]",
		hdmi->hpd_task ? "valid" : "invalid",
		hdmi->hdmi_state.drv_clock ? "enable" : "disable",
		hdmi->hdmi_state.drv_enable ? "enable" : "disable");
	for (loop = 0; loop < pdts->power_count; loop++)
		n += sprintf(buf + n, ", power_%s is %s", pres->power_name[loop],
			regulator_is_enabled(pres->hdmi_regu[loop]) ? "enable" : "disable");
	n += sprintf(buf + n, "\n");

	n += sunxi_hdmi_dump(buf + n);

	if (info->hdmi.scdc.supported) {
		n += sprintf(buf + n, "\n----------------- sink info -----------------\n");
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
	}

	return n;
}

static ssize_t sunxi_hdmi_cmd_hdmi_source_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t sunxi_hdmi_cmd_hpd_mask_show(struct device *dev,
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
	n += sprintf(buf + n, "Current hpd mask value: 0x%x\n", hdmi->hdmi_state.drv_hpd_mask);

	return n;
}

static ssize_t sunxi_hdmi_cmd_hpd_mask_store(struct device *dev,
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

	hdmi->hdmi_state.drv_hpd_mask = (u32)val;
	hdmi_inf("set hpd mask: 0x%x\n", (u32)val);

	return count;
}

static ssize_t sunxi_hdmi_cmd_edid_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	int n = 0;

	n += sprintf(buf + n, "\n[edid debug]\n");
	n += sprintf(buf + n, " Demo: echo value > edid_debug\n");
	n += sprintf(buf + n, " - 0x1: enable use edid debug data\n");
	n += sprintf(buf + n, " - 0x0: disable use edid debug data\n");
	n += sprintf(buf + n, "Current edid debug mode: %d\n", hdmi->hdmi_edid.dbg_mode);

	return n;
}

static ssize_t sunxi_hdmi_cmd_edid_debug_store(struct device *dev,
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

	hdmi->hdmi_edid.dbg_mode = (u8)val;
	hdmi_inf("edid debug mode set: %d\n", (u8)val);
	return count;
}

static ssize_t sunxi_hdmi_cmd_edid_data_store(struct device *dev,
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

	memset(hdmi->hdmi_edid.dbg_data, 0x0, SUNXI_HDMI_EDID_LENGTH);
	memcpy(hdmi->hdmi_edid.dbg_data, buf, count);

	hdmi->hdmi_edid.dbg_size = count;
	hdmi->hdmi_edid.dbg_mode = true;

	return count;
}

#ifdef SUNXI_HDMI20_USE_HDCP
static ssize_t sunxi_hdmi_cmd_hdcp_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int n = 0;
	n += sprintf(buf + n, "\n[hdcp enable]\n");
	n += sprintf(buf + n, "Demo: echo index > hdcp_enable\n");
	n += sprintf(buf + n, " - 1: enable  0: disable\n");
	return n;
}

static ssize_t sunxi_hdmi_cmd_hdcp_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	if (count < 1)
		return -EINVAL;

	if (strncmp(buf, "1", 1) == 0) {
		hdmi->hdmi_state.drv_hdcp = 0x1;
		sunxi_drv_hdcp_set_config(hdmi);
		hdmi_inf("enable hdcp\n");
	} else {
		hdmi->hdmi_state.drv_hdcp = 0x0;
		sunxi_drv_hdcp_set_config(hdmi);
		hdmi_inf("disable hdcp\n");
	}

	return count;
}

static ssize_t sunxi_hdmi_cmd_hdcp_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	char hdcp_type = (char)hdmi->hdmi_state.drv_hdcp_type;
	memcpy(buf, &hdcp_type, 1);
	return 1;
}

static ssize_t sunxi_hdmi_cmd_hdcp_type_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t sunxi_hdmi_cmd_hdcp_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 count = sizeof(u8);
	struct sunxi_drm_hdmi *hdmi = dev_get_drvdata(dev);
	u8 statue = sunxi_drv_hdcp_get_state(hdmi);

	memcpy(buf, &statue, count);

	return count;
}

static ssize_t sunxi_hdmi_cmd_hdcp_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
static ssize_t sunxi_hdmi_cmd_esm_dump_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t n = 0;
	unsigned long *fw_addr = NULL;
	u32 fw_size = sunxi_hdcp22_get_fw_size();

	fw_addr = sunxi_hdcp22_get_fw_addr();

	n += sprintf(buf + n, "esm fw vir addr = %p, size = 0x%04x\n",
		fw_addr, fw_size);
	return n;
}

static ssize_t sunxi_hdmi_cmd_esm_dump_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long *fw_addr = NULL;
	u32 fw_size = sunxi_hdcp22_get_fw_size();
	fw_addr = sunxi_hdcp22_get_fw_addr();

	if (!fw_addr) {
		hdmi_err("point fw_addr is null\n");
		return -1;
	}

	if (fw_size == 0) {
		hdmi_err("esm config fw size is 0\n");
		return -1;
	}

	memcpy(fw_addr, buf, count);
	fw_addr = fw_addr + count;
	hdmi_inf("esm dump confgi fw addr = %p, size = 0x%04x\n",
		fw_addr, (unsigned int)count);
	return count;
}
#endif /* CONFIG_AW_HDMI20_HDCP22 */
#endif /* CONFIG_AW_HDMI20_HDCP14 || CONFIG_AW_HDMI20_HDCP22 */

static DEVICE_ATTR(reg_read, 0664,
	sunxi_hdmi_cmd_reg_read_show, sunxi_hdmi_cmd_reg_read_store);
static DEVICE_ATTR(reg_write, 0664,
	sunxi_hdmi_cmd_reg_write_show, sunxi_hdmi_cmd_reg_write_store);
static DEVICE_ATTR(reg_bank, 0664,
	sunxi_hdmi_cmd_reg_bank_show, sunxi_hdmi_cmd_reg_bank_store);
static DEVICE_ATTR(pattern, 0664,
	sunxi_hdmi_cmd_pattern_show, sunxi_hdmi_cmd_pattern_store);
static DEVICE_ATTR(debug, 0664,
	sunxi_hdmi_cmd_debug_show, sunxi_hdmi_cmd_debug_store);
static DEVICE_ATTR(hdmi_source, 0664,
	sunxi_hdmi_cmd_hdmi_source_show, sunxi_hdmi_cmd_hdmi_source_store);
static DEVICE_ATTR(hpd_mask, 0664,
	sunxi_hdmi_cmd_hpd_mask_show, sunxi_hdmi_cmd_hpd_mask_store);
static DEVICE_ATTR(edid_debug, 0664,
	sunxi_hdmi_cmd_edid_debug_show, sunxi_hdmi_cmd_edid_debug_store);
static DEVICE_ATTR(edid_data, 0664,
	NULL, sunxi_hdmi_cmd_edid_data_store);
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
static DEVICE_ATTR(hdcp_enable, 0664,
	sunxi_hdmi_cmd_hdcp_enable_show, sunxi_hdmi_cmd_hdcp_enable_store);
static DEVICE_ATTR(hdcp_type, 0664,
	sunxi_hdmi_cmd_hdcp_type_show, sunxi_hdmi_cmd_hdcp_type_store);
static DEVICE_ATTR(hdcp_state, 0664,
	sunxi_hdmi_cmd_hdcp_state_show, sunxi_hdmi_cmd_hdcp_state_store);
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
static DEVICE_ATTR(esm_dump, 0664,
	sunxi_hdmi_cmd_esm_dump_show, sunxi_hdmi_cmd_esm_dump_store);
#endif
#endif

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
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || \
		IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	&dev_attr_hdcp_enable.attr,
	&dev_attr_hdcp_type.attr,
	&dev_attr_hdcp_state.attr,
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	&dev_attr_esm_dump.attr,
#endif
#endif
	NULL
};

static struct attribute_group _sunxi_hdmi_group = {
	.name = "attr",
	.attrs = _sunxi_hdmi_attrs,
};

/*******************************************************************************
 * @desc: sunxi hdmi device fops function
 ******************************************************************************/
static int _sunxi_dev_hdmi_fops_open(struct inode *inode, struct file *filp)
{
	struct sunxi_drm_hdmi *hdmi = NULL;

	if (!inode) {
		hdmi_err("point inode is null\n");
		return -1;
	}

	if (!filp) {
		hdmi_err("point filp is null\n");
		return -1;
	}

	hdmi = container_of(inode->i_cdev, struct sunxi_drm_hdmi, hdmi_cdev);
	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		return -1;
	}

	filp->private_data = hdmi;
	return 0;
}

static int _sunxi_dev_hdmi_fops_close(struct inode *inode, struct file *filp)
{
	struct file_ops *fops = (struct file_ops *)filp->private_data;
	kfree(fops);
	return 0;
}

static ssize_t _sunxi_dev_hdmi_fops_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t _sunxi_dev_hdmi_fops_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int _sunxi_dev_hdmi_fops_mmap(struct file *filp,
		struct vm_area_struct *vma)
{
	return 0;
}

static long _sunxi_dev_hdmi_fops_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || IS_ENABLED(CONFIG_AW_HDMI20_HDCP22) || \
	IS_ENABLED(CONFIG_AW_HDMI2_LOG_BUFFER)
	struct sunxi_drm_hdmi *hdmi = (struct sunxi_drm_hdmi *)filp->private_data;
	unsigned long *p_arg = (unsigned long *)arg;
	int ret = 0;
#endif
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	struct sunxi_hdcp_info_s hdcp_info;
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	/* for hdcp keys */
	unsigned int set_size;
	unsigned long *vir_addr;
	u32 use_size;
#endif
#endif

	switch (cmd) {
	case SUNXI_IOCTL_HDMI_NULL:
		break;

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
	case SUNXI_IOCTL_HDMI_HDCP22_LOAD_FW:
		use_size = sunxi_hdcp22_get_fw_size();
		set_size = p_arg[1];
		vir_addr = sunxi_hdcp22_get_fw_addr();
		if (set_size > use_size) {
			hdmi_err("%s: hdcp22 firmware is too big! arg_size:0x%x esm_size:%d\n",
				__func__, set_size, use_size);
			return -EINVAL;
		}

		memset(vir_addr, 0, use_size);
		if (copy_from_user((void *)vir_addr,
				(void __user *)p_arg[0], set_size)) {
			hdmi_err("copy from user fail when hdcp load firmware\n");
			return -EINVAL;
		}
		hdmi_inf("ioctl hdcp22 load firmware has commpleted!\n");
		break;
#endif

	case SUNXI_IOCTL_HDMI_HDCP_ENABLE:
		hdmi->hdmi_state.drv_hdcp = 0x1;
		sunxi_drv_hdcp_set_config(hdmi);
		break;

	case SUNXI_IOCTL_HDMI_HDCP_DISABLE:
		hdmi->hdmi_state.drv_hdcp = 0x0;
		sunxi_drv_hdcp_set_config(hdmi);
		break;

	case SUNXI_IOCTL_HDMI_HDCP_INFO:
		hdcp_info.hdcp_type = hdmi->hdmi_state.drv_hdcp_type;
		hdcp_info.hdcp_status = (unsigned int)sunxi_drv_hdcp_get_state(hdmi);
		hdmi_inf("hdmi ioctl get hdcp type: %d, hdcp state: %d\n",
			hdcp_info.hdcp_type, hdcp_info.hdcp_status);

		ret = copy_to_user((void __user *)p_arg[0], (void *)&hdcp_info,
								sizeof(struct sunxi_hdcp_info_s));
		if (ret != 0) {
			hdmi_err("copy to user fail ret = %d when get hdcp info!!!\n", ret);
			return -EINVAL;
		}

		break;
#endif

#if IS_ENABLED(CONFIG_AW_HDMI2_LOG_BUFFER)
	case SUNXI_IOCTL_HDMI_GET_LOG_SIZE:
		{
			unsigned int size = aw_hdmi_log_get_max_size();

			if (copy_to_user((void __user *)p_arg[0], (void *)&size, sizeof(unsigned int))) {
				hdmi_err("%s: copy to user fail when get hdmi log size!\n", __func__);
				return -EINVAL;
			}
			break;
		}

	case SUNXI_IOCTL_HDMI_GET_LOG:
		{
			char *addr = aw_hdmi_log_get_address();
			unsigned int start = aw_hdmi_log_get_start_index();
			unsigned int max_size = aw_hdmi_log_get_max_size();
			unsigned int used_size = aw_hdmi_log_get_used_size();

			if (used_size < max_size) {
				if (copy_to_user((void __user *)p_arg[0], (void *)addr, used_size)) {
					aw_hdmi_log_put_address();
					hdmi_err("%s: copy to user fail when get hdmi log!\n", __func__);
					return -EINVAL;
				}
			} else {
				if (copy_to_user((void __user *)p_arg[0], (void *)(addr + start),
							max_size - start)) {
					aw_hdmi_log_put_address();
					hdmi_err("%s: copy to user fail when get hdmi log!\n", __func__);
					return -EINVAL;
				}
				if (copy_to_user((void __user *)(p_arg[0] + (max_size - start)),
							(void *)addr, start)) {
					aw_hdmi_log_put_address();
					hdmi_err("%s: copy to user fail when get hdmi log!\n", __func__);
					return -EINVAL;
				}
			}
			aw_hdmi_log_put_address();
			break;
		}
#endif

	default:
		hdmi_err("%s: cmd %d invalid\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static long _sunxi_dev_hdmi_fops_unlock_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	unsigned long arg64[3] = {0};

	if (copy_from_user((void *)arg64, (void __user *)arg,
						3 * sizeof(unsigned long))) {
		hdmi_err("%s: copy from user fail when hdmi ioctl!!!\n", __func__);
		return -EFAULT;
	}

	return _sunxi_dev_hdmi_fops_ioctl(filp, cmd, (unsigned long)arg64);
}

#if IS_ENABLED(CONFIG_COMPAT)
static long _sunxi_dev_hdmi_fops_compat_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	compat_uptr_t arg32[3] = {0};
	unsigned long arg64[3] = {0};

	if (!arg)
		return _sunxi_dev_hdmi_fops_ioctl(filp, cmd, 0);

	if (copy_from_user((void *)arg32, (void __user *)arg,
						3 * sizeof(compat_uptr_t))) {
		hdmi_err("%s: copy from user fail when hdmi compat ioctl!!!\n", __func__);
		return -EFAULT;
	}

	arg64[0] = (unsigned long)arg32[0];
	arg64[1] = (unsigned long)arg32[1];
	arg64[2] = (unsigned long)arg32[2];
	return _sunxi_dev_hdmi_fops_ioctl(filp, cmd, (unsigned long)arg64);
}
#endif

static int _sunxi_dev_hdmi_init(struct sunxi_drm_hdmi *hdmi)
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

	hdmi->hdmi_ops.owner			= THIS_MODULE;
	hdmi->hdmi_ops.open				= _sunxi_dev_hdmi_fops_open;
	hdmi->hdmi_ops.release			= _sunxi_dev_hdmi_fops_close;
	hdmi->hdmi_ops.write			= _sunxi_dev_hdmi_fops_write;
	hdmi->hdmi_ops.read				= _sunxi_dev_hdmi_fops_read;
	hdmi->hdmi_ops.mmap				= _sunxi_dev_hdmi_fops_mmap;
	hdmi->hdmi_ops.unlocked_ioctl	= _sunxi_dev_hdmi_fops_unlock_ioctl;
#if  IS_ENABLED(CONFIG_COMPAT)
	hdmi->hdmi_ops.compat_ioctl		= _sunxi_dev_hdmi_fops_compat_ioctl;
#endif

	cdev_init(&hdmi->hdmi_cdev, &hdmi->hdmi_ops);
	hdmi->hdmi_cdev.owner = THIS_MODULE;

	/* creat hdmi device */
	if (cdev_add(&hdmi->hdmi_cdev, hdmi->hdmi_devid, 0x1)) {
		hdmi_err("hdmi init dev failed when add char dev\n");
		return -1;
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

static int _sunxi_dev_hdmi_exit(struct platform_device *pdev)
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
		hdmi_err("sunxi hdmi driver disable failed\n");
		return;
	}

	ret = sunxi_tcon_hdmi_mode_exit(hdmi->tcon_dev);
	if (ret != 0) {
		hdmi_err("sunxi tcon hdmi mode exit failed\n");
		return;
	}

	_sunxi_drv_hdmi_clk_disable(hdmi);
	mdelay(10);
	_sunxi_drv_hdmi_clk_enable(hdmi);

	hdmi->hdmi_state.drm_enable   = 0x0;
	hdmi->hdmi_state.drm_mode_set = 0x0;
	hdmi_dbg("%s finish\n", __func__);
}

static void _sunxi_hdmi_drm_encoder_atomic_enable(struct drm_encoder *encoder,
		struct drm_atomic_state *state)
{
	struct sunxi_drm_hdmi *hdmi = drm_encoder_to_sunxi_drm_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	int hw_id = sunxi_drm_crtc_get_hw_id(crtc);
	int ret = 0;

	_sunxi_drv_hdmi_clk_enable(hdmi);

	ret = sunxi_hdmi_video_set_info(&hdmi->disp_config);
	if (ret != 0)
		hdmi_inf("drm atomic enable fill config failed\n");

	/* set tcon config data */
	memcpy(&hdmi->tcon_dev->cfg.timing, &hdmi->timing, sizeof(hdmi->timing));
	hdmi->tcon_dev->cfg.format      = hdmi->disp_config.format;
	hdmi->tcon_dev->cfg.de_id       = hw_id;
	hdmi->tcon_dev->cfg.irq_handler = scrtc_state->crtc_irq_handler;
	hdmi->tcon_dev->cfg.irq_data    = scrtc_state->base.crtc;

	/* tcon hdmi enable */
	ret = sunxi_tcon_hdmi_mode_init(hdmi->tcon_dev);
	if (ret != 0) {
		hdmi_err("sunxi tcon hdmi mode init failed\n");
		return;
	}

	ret = _sunxi_drv_hdmi_enable(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi driver enable failed\n");
		return;
	}
	sunxi_hdmi_correct_config();

	hdmi->hdmi_state.drm_enable = 0x1;
	hdmi_dbg("%s finish\n", __func__);
}

static int _sunxi_hdmi_drm_encoder_atomic_check(struct drm_encoder *encoder,
		    struct drm_crtc_state *crtc_state,
		    struct drm_connector_state *conn_state)
{
	struct sunxi_crtc_state *scrtc_state = to_sunxi_crtc_state(crtc_state);
	struct sunxi_drm_hdmi *hdmi = drm_encoder_to_sunxi_drm_hdmi(encoder);
	int change_cnt = 0;

	change_cnt = _sunxi_drv_hdmi_check_disp_info(hdmi);

	if (change_cnt == 0)
		return 0;

	scrtc_state->color_fmt     = hdmi->disp_config.format;
	scrtc_state->color_depth   = hdmi->disp_config.bits;
	scrtc_state->tcon_id       = hdmi->tcon_id;
	scrtc_state->enable_vblank = _sunxi_drv_hdmi_vblank_enable;
	scrtc_state->vblank_enable_data = hdmi;

	crtc_state->mode_changed = true;
	hdmi_dbg("%s finish\n", __func__);
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

	hdmi_inf("drm mode set: %d*%d\n", mode->hdisplay, mode->vdisplay);

	memcpy(&hdmi->drm_mode, mode, sizeof(struct drm_display_mode));

	ret = sunxi_hdmi_mode_convert(&hdmi->drm_mode);
	if (ret != 0) {
		hdmi_err("drm mode set convert failed\n");
		return;
	}

	ret = drm_mode_to_sunxi_video_timings(&hdmi->drm_mode, &hdmi->timing);
	if (ret != 0) {
		hdmi_err("drm mode timing %d*%d convert disp timing failed\n",
			hdmi->drm_mode.hdisplay, hdmi->drm_mode.vdisplay);
		return;
	}

	hdmi->hdmi_state.drm_mode_set = 0x1;
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

	if (!hdmi->hdmi_edid.edid) {
		ret = _sunxi_drv_hdmi_read_edid(hdmi);
		if (ret != 0) {
			hdmi_err("drm get mode read edid failed\n");
			goto use_default;
		}
	}

	drm_connector_update_edid_property(connector, hdmi->hdmi_edid.edid);

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
	cec_notifier_set_phys_addr_from_edid(hdmi->cec_drv.notify, hdmi->hdmi_edid.edid);
#endif

	ret = drm_add_edid_modes(connector, hdmi->hdmi_edid.edid);
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
	if (rate > SUNXI_HDMI_SUPPORT_MAX_RATE) {
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
	hdmi_inf("drm hdmi detect hpd: %s\n", ret ? "plugin" : "plugout");
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

	hdmi->hdmi_state.drm_hpd_force = (int)connector->force;
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

	struct disp_device_config *info = sunxi_hdmi_video_get_info();

	if (hdmi->prop_tmds_mode == property)
		*val = info->dvi_hdmi;
	else if (hdmi->prop_color_fm == property)
		*val = info->format;
	else if (hdmi->prop_color_depth == property)
		*val = info->bits;
	else if (hdmi->prop_color_space == property)
		*val = info->cs;
	else if (hdmi->prop_eotf == property)
		*val = info->eotf;
	else if (hdmi->prop_color_range == property)
		*val = info->range;
	else if (hdmi->prop_aspect_ratio == property)
		*val = info->aspect_ratio;
	else if (hdmi->prop_scan == property)
		*val = info->scan;
	else {
		hdmi_err("drm hdmi get invalid property: %s\n", property->name);
		return -1;
	}

	hdmi_dbg("drm hdmi get property %s = 0x%x\n", property->name, (u32)*val);
	return 0;
}

static int _sunxi_hdmi_drm_connector_atomic_set_property(
		struct drm_connector *connector, struct drm_connector_state *state,
		struct drm_property *property, uint64_t val)
{
	struct sunxi_drm_hdmi *hdmi = drm_connector_to_sunxi_drm_hdmi(connector);

	if (hdmi->prop_tmds_mode == property)
		hdmi->disp_config.dvi_hdmi = val;
	else if (hdmi->prop_color_fm == property)
		hdmi->disp_config.format = val;
	else if (hdmi->prop_color_depth == property)
		hdmi->disp_config.bits = val;
	else if (hdmi->prop_color_space == property)
		hdmi->disp_config.cs = val;
	else if (hdmi->prop_eotf == property)
		hdmi->disp_config.eotf = val;
	else if (hdmi->prop_color_range == property)
		hdmi->disp_config.range = val;
	else if (hdmi->prop_aspect_ratio == property)
		hdmi->disp_config.aspect_ratio = val;
	else if (hdmi->prop_scan == property)
		hdmi->disp_config.scan = val;
	else {
		hdmi_err("drm hdmi set invalid property: %s\n", property->name);
		return -1;
	}
	hdmi_inf("drm hdmi set property %s = 0x%x\n", property->name, (u32)val);

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

static void _sunxi_hdmi_drm_property_init(struct sunxi_drm_hdmi *hdmi)
{
	struct drm_connector *connector = &hdmi->connector;
	struct disp_device_config *info = &hdmi->disp_config;

	hdmi->prop_tmds_mode = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "tmds_mode", sunxi_hdmi_prop_tmds_mode,
			ARRAY_SIZE(sunxi_hdmi_prop_tmds_mode), info->dvi_hdmi);
	if (!hdmi->prop_tmds_mode)
		hdmi_err("drm hdmi init [tmds_mode] property failed\n");
	else
		hdmi_dbg("drm hdmi init [tmds_mode] property default value: %d\n",
			info->dvi_hdmi);

	hdmi->prop_color_depth = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "color_depth", sunxi_hdmi_prop_color_depth,
			ARRAY_SIZE(sunxi_hdmi_prop_color_depth), info->bits);
	if (!hdmi->prop_color_depth)
		hdmi_err("drm hdmi init [color_depth] property failed\n");
	else
		hdmi_dbg("drm hdmi init [color_depth] property default value: %d\n",
			info->bits);

	hdmi->prop_color_fm = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "color_format", sunxi_hdmi_prop_color_format,
			ARRAY_SIZE(sunxi_hdmi_prop_color_format), info->format);
	if (!hdmi->prop_color_fm)
		hdmi_err("drm hdmi init [color_format] property failed\n");
	else
		hdmi_dbg("drm hdmi init [color_format] property default value: %d\n",
			info->format);

	hdmi->prop_color_space = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "color_space", sunxi_hdmi_prop_color_space,
			ARRAY_SIZE(sunxi_hdmi_prop_color_space), info->cs);
	if (!hdmi->prop_color_space)
		hdmi_err("drm hdmi init [color_space] property failed\n");
	else
		hdmi_dbg("drm hdmi init [color_space] property default value: %d\n",
			info->cs);

	hdmi->prop_color_range = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "color_range", sunxi_hdmi_prop_color_range,
			ARRAY_SIZE(sunxi_hdmi_prop_color_range), info->range);
	if (!hdmi->prop_color_range)
		hdmi_err("drm hdmi init [color_range] property failed\n");
	else
		hdmi_dbg("drm hdmi init [color_range] property default value: %d\n",
			info->range);

	hdmi->prop_eotf = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "eotf", sunxi_hdmi_prop_eotf,
			ARRAY_SIZE(sunxi_hdmi_prop_eotf), info->eotf);
	if (!hdmi->prop_eotf)
		hdmi_err("drm hdmi init [eotf] property failed\n");
	else
		hdmi_dbg("drm hdmi init [eotf] property default value: %d\n",
			info->eotf);

	hdmi->prop_aspect_ratio = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "aspect_ratio", sunxi_hdmi_prop_aspect_ratio,
			ARRAY_SIZE(sunxi_hdmi_prop_aspect_ratio), info->aspect_ratio);
	if (!hdmi->prop_aspect_ratio)
		hdmi_err("drm hdmi init [aspect_ratio] property failed\n");
	else
		hdmi_dbg("drm hdmi init [aspect_ratio] property default value: %d\n",
			info->aspect_ratio);

	hdmi->prop_scan = sunxi_drm_create_attach_property_enum(connector->dev,
			&connector->base, "scan", sunxi_hdmi_prop_scan_info,
			ARRAY_SIZE(sunxi_hdmi_prop_scan_info), info->scan);
	if (!hdmi->prop_scan)
		hdmi_err("drm hdmi init [scan] property failed\n");
	else
		hdmi_dbg("drm hdmi init [scan] property default value: %d\n",
			info->scan);
}

int sunxi_hdmi_drm_create(struct tcon_device *tcon)
{
	struct sunxi_drm_hdmi *hdmi = NULL;
	struct device *tcon_dev = tcon->dev;
	unsigned int id = tcon->hw_id;
	struct drm_device *drm = tcon->drm;
	struct device_node *dev_node = NULL;
	struct platform_device *pdev;
	int ret;

	if (!tcon) {
		hdmi_err("point tcon_dev is null\n");
		return -1;
	}

	if (!drm) {
		hdmi_err("point drm is null\n");
		return -1;
	}

	dev_node = of_graph_get_remote_node(tcon_dev->of_node, 1, 0);
	if (!dev_node) {
		hdmi_err("remote node get failed!!!\n");
		return -1;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev) {
		hdmi_err("con not get pdev!\n");
		return -1;
	}

	hdmi = (struct sunxi_drm_hdmi *)platform_get_drvdata(pdev);
	if (!hdmi) {
		hdmi_err("get platform device data failed!!!\n");
		return -1;
	}

	hdmi->tcon_dev = tcon;
	hdmi->tcon_id  = id;
	hdmi->drm = drm;

	/* drm encoder register */
	drm_encoder_helper_add(&hdmi->encoder, &sunxi_hdmi_encoder_helper_funcs);
	ret = drm_simple_encoder_init(drm, &hdmi->encoder, DRM_MODE_ENCODER_TMDS);
	if (ret) {
		hdmi_err("hdmi init encoder for tcon %d failed!!!\n", id);
		return ret;
	}
	hdmi->encoder.possible_crtcs =
		drm_of_find_possible_crtcs(drm, tcon_dev->of_node);

	/* drm connector register */
	hdmi->connector.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->connector.interlace_allowed = true;
	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;
	drm_connector_helper_add(&hdmi->connector, &sunxi_hdmi_connector_helper_funcs);
	ret = drm_connector_init_with_ddc(drm, &hdmi->connector,
			&sunxi_hdmi_connector_funcs, DRM_MODE_CONNECTOR_HDMIA, &hdmi->i2c_adap);
	if (ret) {
		drm_encoder_cleanup(&hdmi->encoder);
		hdmi_err("hdmi init connector for tcon %d failed!!!\n", id);
		return ret;
	}

	drm_connector_attach_encoder(&hdmi->connector, &hdmi->encoder);

	_sunxi_hdmi_drm_property_init(hdmi);

	sunxi_hdmi_connect_creat(&hdmi->connector);

	hdmi_inf("drm hdmi create finish\n");
	return 0;
}

static int
sunxi_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	int ret = 0;
	struct sunxi_drm_hdmi  *hdmi = NULL;
	struct platform_device *pdev = to_platform_device(dev);

	ret = _sunxi_drv_hdmi_set_power_domain(dev, SUNXI_HDMI_ENABLE);
	if (ret != 0) {
		hdmi_err("hdmi drv set power domain failed\n");
		return -1;
	}

	hdmi = devm_kzalloc(dev, sizeof(struct sunxi_drm_hdmi), GFP_KERNEL);
	if (!hdmi) {
		hdmi_err("point hdmi is null\n");
		goto bind_ng;
	}

	platform_set_drvdata(pdev, hdmi);
	hdmi->pdev = pdev;
	hdmi->hdmi_core.pdev = pdev;

	ret = _sunxi_dev_hdmi_init(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi dev sysfs fail\n");
		goto bind_ng;
	}

	ret = _sunxi_drv_hdmi_init(hdmi);
	if (ret != 0) {
		hdmi_err("sunxi hdmi init drv fail\n");
		goto bind_ng;
	}

	hdmi_inf("hdmi drv bind done\n");
	return 0;

bind_ng:
	_sunxi_drv_hdmi_set_power_domain(dev, SUNXI_HDMI_DISABLE);
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

	ret = _sunxi_dev_hdmi_exit(pdev);
	if (ret != 0)
		hdmi_wrn("hdmi dev exit failed\n\n");

	ret = _sunxi_drv_hdmi_exit(pdev);
	if (ret != 0)
		hdmi_wrn("hdmi drv exit failed\n");

	ret = _sunxi_drv_hdmi_set_power_domain(dev, SUNXI_HDMI_DISABLE);
	if (ret != 0)
		hdmi_wrn("hdmi drv set power domain failed\n");

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
	return component_add(&pdev->dev, &sunxi_hdmi_compoent_ops);
}

static int sunxi_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sunxi_hdmi_compoent_ops);
	return 0;
}

struct platform_driver sunxi_hdmi_platform_driver = {
	.probe  = sunxi_hdmi_probe,
	.remove = sunxi_hdmi_remove,
	.driver = {
		   .name = "allwinner,sunxi-hdmi",
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_hdmi_match,
	},
};
