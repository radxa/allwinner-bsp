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

#ifndef _SUNXI_HDMI_H_
#define _SUNXI_HDMI_H_

#include <drm/drm_modes.h>

#include <linux/platform_device.h>
#include <video/sunxi_display2.h>
#include <video/drv_hdmi.h>

#include "lowlevel_hdmi20/dw_i2cm.h"
#include "lowlevel_hdmi20/dw_mc.h"
#include "lowlevel_hdmi20/dw_avp.h"
#include "lowlevel_hdmi20/dw_fc.h"
#include "lowlevel_hdmi20/dw_phy.h"
#include "lowlevel_hdmi20/dw_edid.h"
#include "lowlevel_hdmi20/dw_hdcp.h"
#include "lowlevel_hdmi20/dw_dev.h"
#include "lowlevel_hdmi20/dw_hdcp22.h"
#include "lowlevel_hdmi20/dw_cec.h"
#include "lowlevel_hdmi20/phy_aw.h"
#include "lowlevel_hdmi20/phy_inno.h"
#include "lowlevel_hdmi20/phy_snps.h"

/************************************************
 * @desc: define sunxi hdmi private marco
 ************************************************/
/* define only use hdmi14 */
#if (IS_ENABLED(CONFIG_ARCH_SUN8IW16))
#define SUXNI_HDMI_USE_HDMI14
#endif

/* define use color space convert */
#if (IS_ENABLED(CONFIG_ARCH_SUN8IW16) || IS_ENABLED(CONFIG_ARCH_SUN55IW3))
#define SUNXI_HDMI20_USE_CSC
#endif

/* define use tcon pad */
#if (IS_ENABLED(CONFIG_ARCH_SUN8IW16) || \
	IS_ENABLED(CONFIG_ARCH_SUN8IW20)  || \
	IS_ENABLED(CONFIG_ARCH_SUN20IW1)  || \
	IS_ENABLED(CONFIG_ARCH_SUN50IW9))
#define SUNXI_HDMI20_USE_TCON_PAD
#endif

/* define use hdmi phy model */
#if (IS_ENABLED(CONFIG_ARCH_SUN8IW20))
	#ifndef SUNXI_HDMI20_PHY_AW
	#define SUNXI_HDMI20_PHY_AW   /* allwinner phy */
	#endif
#elif (IS_ENABLED(CONFIG_ARCH_SUN55IW3))
	#ifndef SUNXI_HDMI20_PHY_INNO
	#define SUNXI_HDMI20_PHY_INNO /* innosilicon phy */
	#endif
#else
	#ifndef SUNXI_HDMI20_PHY_SNPS
	#define SUNXI_HDMI20_PHY_SNPS /* synopsys phy */
	#endif
#endif

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14) || IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
#define SUNXI_HDMI20_USE_HDCP
#endif

#define SUNXI_HDMI20_TMDS_CLK_MAX		(340000)

#define SUNXI_HDMI_ENABLE   			(0x1)
#define SUNXI_HDMI_DISABLE  			(0x0)

#define	DISP_CONFIG_UPDATE_NULL			(0x0)
#define	DISP_CONFIG_UPDATE_MODE		    BIT(0)
#define	DISP_CONFIG_UPDATE_FORMAT		BIT(1)
#define	DISP_CONFIG_UPDATE_BITS			BIT(2)
#define	DISP_CONFIG_UPDATE_EOTF		    BIT(3)
#define	DISP_CONFIG_UPDATE_CS			BIT(4)
#define	DISP_CONFIG_UPDATE_DVI			BIT(5)
#define	DISP_CONFIG_UPDATE_RANGE		BIT(6)
#define	DISP_CONFIG_UPDATE_SCAN		    BIT(7)
#define	DISP_CONFIG_UPDATE_RATIO		BIT(8)

#define SUNXI_KFREE_POINT(p)    \
	do {                        \
		if (p) {                \
			kfree(p);           \
			p = NULL;           \
		}                       \
	} while (0)

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
enum sunxi_cec_irq_state_e {
	SUNXI_CEC_IRQ_NULL          = 0x0,
	SUNXI_CEC_IRQ_DONE          = IH_CEC_STAT0_DONE_MASK,
	SUNXI_CEC_IRQ_EOM           = IH_CEC_STAT0_EOM_MASK,
	SUNXI_CEC_IRQ_NACK          = IH_CEC_STAT0_NACK_MASK,
	SUNXI_CEC_IRQ_ARB           = IH_CEC_STAT0_ARB_LOST_MASK,
	SUNXI_CEC_IRQ_ERR_INITIATOR = IH_CEC_STAT0_ERROR_INITIATOR_MASK,
	SUNXI_CEC_IRQ_ERR_FOLLOW    = IH_CEC_STAT0_ERROR_FOLLOW_MASK,
	SUNXI_CEC_IRQ_WAKEUP        = IH_CEC_STAT0_WAKEUP_MASK,
};

enum sunxi_cec_wait_bit_times_e {
	SUNXI_CEC_WAIT_3BIT = DW_CEC_WAIT_3BIT,
	SUNXI_CEC_WAIT_5BIT = DW_CEC_WAIT_5BIT,
	SUNXI_CEC_WAIT_7BIT = DW_CEC_WAIT_7BIT,
	SUNXI_CEC_WAIT_NULL = DW_CEC_WAIT_NULL,
};
#endif

enum sunxi_hdcp_state_e {
	SUNXI_HDCP_DISABLE       = 0,
	SUNXI_HDCP_ING           = 1,
	SUNXI_HDCP_FAILED        = 2,
	SUNXI_HDCP_SUCCESS       = 3,
};

typedef enum {
    SUNXI_PHY_ACCESS_NULL    = DW_PHY_ACCESS_NULL,
    SUNXI_PHY_ACCESS_I2C     = DW_PHY_ACCESS_I2C,
    SUNXI_PHY_ACCESS_JTAG    = DW_PHY_ACCESS_JTAG,
} sunxi_hdmi_phy_access_e;

typedef enum {
	SUNXI_HDCP_TYPE_NULL          = DW_HDCP_TYPE_NULL,
	SUNXI_HDCP_TYPE_HDCP14        = DW_HDCP_TYPE_HDCP14,
	SUNXI_HDCP_TYPE_HDCP22        = DW_HDCP_TYPE_HDCP22,
} sunxi_hdcp_type_t;

#define HDMI_VIC_3D_OFFSET    (0x80)
enum HDMI_CEA_VIC {
	HDMI_VIC_NULL       = 0,
	HDMI_VIC_640x480P60 = 1,
	HDMI_VIC_720x480P60_4_3,
	HDMI_VIC_720x480P60_16_9,
	HDMI_VIC_1280x720P60,
	HDMI_VIC_1920x1080I60,
	HDMI_VIC_720x480I_4_3,
	HDMI_VIC_720x480I_16_9,
	HDMI_VIC_720x240P_4_3,
	HDMI_VIC_720x240P_16_9,
	HDMI_VIC_1920x1080P60 = 16,
	HDMI_VIC_720x576P_4_3,
	HDMI_VIC_720x576P_16_9,
	HDMI_VIC_1280x720P50,
	HDMI_VIC_1920x1080I50,
	HDMI_VIC_720x576I_4_3,
	HDMI_VIC_720x576I_16_9,
	HDMI_VIC_1920x1080P50 = 31,
	HDMI_VIC_1920x1080P24,
	HDMI_VIC_1920x1080P25,
	HDMI_VIC_1920x1080P30,
	HDMI_VIC_1280x720P24 = 60,
	HDMI_VIC_1280x720P25,
	HDMI_VIC_1280x720P30,
	HDMI_VIC_3840x2160P24 = 93,
	HDMI_VIC_3840x2160P25,
	HDMI_VIC_3840x2160P30,
	HDMI_VIC_3840x2160P50,
	HDMI_VIC_3840x2160P60,
	HDMI_VIC_4096x2160P24,
	HDMI_VIC_4096x2160P25,
	HDMI_VIC_4096x2160P30,
	HDMI_VIC_4096x2160P50,
	HDMI_VIC_4096x2160P60,
	HDMI_VIC_2560x1440P60 = 0x201,
	HDMI_VIC_1440x2560P70 = 0x202,
	HDMI_VIC_1080x1920P60 = 0x203,
	HDMI_VIC_1680x1050P60 = 0x204,
	HDMI_VIC_1600x900P60  = 0x205,
	HDMI_VIC_1280x1024P60 = 0x206,
	HDMI_VIC_1024x768P60  = 0x207,
	HDMI_VIC_800x600P60   = 0x208,
	HDMI_VIC_MAX,
};

#define SUNXI_HDMI_VIC_1080P_24_3D_FP	(HDMI_VIC_1920x1080P24 + HDMI_VIC_3D_OFFSET)
#define SUNXI_HDMI_VIC_720P_50_3D_FP	(HDMI_VIC_1280x720P50  + HDMI_VIC_3D_OFFSET)
#define SUNXI_HDMI_VIC_720P_60_3D_FP	(HDMI_VIC_1280x720P60  + HDMI_VIC_3D_OFFSET)

struct sunxi_hdmi_specify_mode_s {
	int code;
	struct drm_display_mode mode;
};

struct sunxi_hdmi_vic_mode {
	char name[25];
	int  vic_code;
};

struct sunxi_disp_hdmi_mode {
    enum disp_tv_mode mode;
    int  hdmi_mode; /* vic code */
};

/******************************************************************
 * @desc: sunxi hdmi level struct
 * funsion hdmi20 hdmi21
 *****************************************************************/
struct sunxi_hdmi_s {
    struct platform_device    *pdev;

	struct disp_device_config disp_info;
	struct drm_connector      *connect;

    int   blacklist_index;

    uintptr_t reg_base;

    int hdcp14_done;
    int hdcp22_done;

    struct i2c_adapter *i2c;

	struct dw_phy_ops_s   *phy_func;

    struct dw_hdmi_dev_s   dw_hdmi;
};

void sunxi_hdmi_ctrl_write(uintptr_t addr, u32 data);

u32 sunxi_hdmi_ctrl_read(uintptr_t addr);
/*******************************************************************************
 * @desc: sunxi hdmi scdc core api
 ******************************************************************************/
#ifndef SUXNI_HDMI_USE_HDMI14
/**
 * @desc: sunxi hdmi scdc read
 * @addr: scdc offset address
 * @return: scdc read value
 */
u8 sunxi_hdmi_scdc_read(u8 addr);
/**
 * @desc: sunxi hdmi scdc write
 * @addr: scdc offset address
 * @data: scdc write data
 */
void sunxi_hdmi_scdc_write(u8 addr, u8 data);
#endif

/*******************************************************************************
 * @desc: sunxi hdmi phy core api
 ******************************************************************************/
/**
 * @desc: sunxi hdmi phy write
 * @addr: phy write address
 * @data: phy write value
 */
int sunxi_hdmi_phy_write(u8 addr, u32 data);
/**
 * @desc: sunxi hdmi phy read
 * @addr: phy read address
 * @data: phy read value
 */
int sunxi_hdmi_phy_read(u8 addr, u32 *data);

/**
 * @desc: sunxi hdmi phy reset config flow
 */
void sunxi_hdmi_phy_reset(void);
/**
 * @desc: sunxi hdmi phy resume config flow
 */
int sunxi_hdmi_phy_resume(void);
/**
 * @desc: sunxi hdmi phy get hpd status
 * @return: 1 - detect hpd
 *          0 - undetect hpd
 */
u8 sunxi_hdmi_get_hpd(void);

/*******************************************************************************
 * @desc: sunxi hdmi audio core api
 ******************************************************************************/
/**
 * @desc: suxni hdmi audio update params
 * @params: audio set params point
 * @return: update status
 */
int sunxi_hdmi_audio_set_info(hdmi_audio_t *params);
/**
 * @desc: suxni hdmi audio main config flow
 * @return: config status
 */
int sunxi_hdmi_audio_setup(void);

/*******************************************************************************
 * @desc: sunxi hdmi video core api
 ******************************************************************************/
/**
 * @desc: sunxi hdmi video set pattern
 * @bit: 1 - enable pattern
 *       0 - disable pattern
 * @value: pattern rgb value
 */
void sunxi_hdmi_video_set_pattern(u8 bit, u32 value);

int sunxi_hdmi_i2c_xfer(struct i2c_msg *msgs, int num);

int sunxi_hdmi_disconfig(void);

/*******************************************************************************
 * @desc: sunxi hdmi hdcp core api
 ******************************************************************************/
#ifdef SUNXI_HDMI20_USE_HDCP
int sunxi_hdcp_get_sink_cap(void);

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP14)
int sunxi_hdcp14_set_config(u8 state);
#endif

#if IS_ENABLED(CONFIG_AW_HDMI20_HDCP22)
int sunxi_hdcp22_set_config(u8 state);
#endif

/**
 * @desc: sunxi hdmi hdcp get encry state
 * @return: 0 - disabled
 *          1 - processing
 *          2 - failed
 *          3 - success
 */
u8 sunxi_hdcp_get_state(void);

unsigned long *sunxi_hdcp22_get_fw_addr(void);
u32 sunxi_hdcp22_get_fw_size(void);
#endif

#if IS_ENABLED(CONFIG_AW_HDMI20_CEC)
void sunxi_cec_enable(u8 state);

void sunxi_cec_set_logic_addr(u16 addr);

void suxni_cec_message_send(u8 *buf, u8 len, u8 times);

int sunxi_cec_message_receive(u8 *buf);

u8 sunxi_cec_get_irq_state(void);

#endif /* CONFIG_AW_HDMI20_CEC */

/*******************************************************************************
 * @desc: sunxi hdmi edid core api
 ******************************************************************************/
/**
 * @desc: sunxi hdmi edid update video params by sink edid info
 */
void sunxi_hdmi_update_prefered_video(void);

int sunxi_edid_parse(u8 *buffer);

/**
 * @desc: sunxi hdmi edid correct hardware config
 */
void sunxi_hdmi_correct_config(void);

/*******************************************************************************
 * @desc: sunxi hdmi core api
 ******************************************************************************/
/**
 * @desc: sunxi hdmi set debug log level
 * @level: log level
 */
int suxni_hdmi_set_loglevel(u8 level);
/**
 * @desc: sunxi hdmi main close
 */
int sunxi_hdmi_close(void);
/**
 * @desc: sunxi hdmi main config
 */
int sunxi_hdmi_config(void);

int sunxi_hdmi_mode_convert(struct drm_display_mode *mode);

int sunxi_hdmi_init(struct sunxi_hdmi_s *core);

void sunxi_hdmi_exit(struct sunxi_hdmi_s *g_aw_core);

struct disp_device_config *sunxi_hdmi_video_get_info(void);

int sunxi_hdmi_video_set_info(struct disp_device_config *config);
int sunxi_hdmi_adap_bind(struct i2c_adapter *i2c_adap);
int sunxi_hdmi_connect_creat(struct drm_connector *data);
/**
 * @desc: sunxi hdmi core dump. point different hw
 * @buf: save dump info buffer
 */
ssize_t sunxi_hdmi_dump(char *buf);

#endif /* _SUNXI_HDMI_H_ */
