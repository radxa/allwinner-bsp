/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _DISP_INCLUDE_H_
#define _DISP_INCLUDE_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h"
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm.h>
#include <asm/div64.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <asm/barrier.h>
#include <linux/clk-provider.h>
#include <sunxi-sid.h>

#include <video/sunxi_display2.h>
#include <video/sunxi_metadata.h>
#include "disp_features.h"

#define DISP2_DEBUG_LEVEL 0

#if DISP2_DEBUG_LEVEL == 1
#define __inf(msg...) do { \
			disp_log_info(msg); \
	} while (0)
#define __msg(msg...) do { \
			disp_log_info(msg); \
	} while (0)
#define __here__
#define __debug(msg...)
#elif DISP2_DEBUG_LEVEL == 2
#define __inf(msg...) do { \
			disp_log_info(msg); \
	} while (0)
#define __here__ do { \
			pr_warn("[DISP] %s,line:%d\n", __func__, __LINE__);\
	} while (0)
#define __debug(msg...)  do { \
			disp_log_debug(msg); \
	} while (0)
#else
#define __inf(msg...)
#define __msg(msg...)
#define __here__
#define __debug(msg...)
#endif

#define __wrn(msg...) do { \
			pr_warn(msg); \
		} while (0)


#define DE_INF __inf
#define DE_MSG __msg
#define DE_WRN __wrn
#define DE_DBG __debug
#define DISP_IRQ_RETURN IRQ_HANDLED

#define DEFAULT_PRINT_LEVLE 0
#if defined(CONFIG_AW_FPGA_S4) \
	|| defined(CONFIG_AW_FPGA_V7) \
	|| defined(CONFIG_A67_FPGA)
#define __FPGA_DEBUG__
#endif

#define SETMASK(width, shift)   ((width?((-1U) >> (32-width)):0)  << (shift))
#define CLRMASK(width, shift)   (~(SETMASK(width, shift)))
#define GET_BITS(shift, width, reg)     \
	(((reg) & SETMASK(width, shift)) >> (shift))
#define SET_BITS(shift, width, reg, val) \
	(((reg) & CLRMASK(width, shift)) | (val << (shift)))

#define DISPALIGN(value, align) ((align == 0) ? \
				value : \
				(((value) + ((align) - 1)) & ~((align) - 1)))

#ifndef abs
#define abs(x) (((x)&0x80000000) ? (0-(x)):(x))
#endif

#define LCD_GAMMA_TABLE_SIZE (256 * sizeof(unsigned int))
#define LCD_GAMMA_TABLE_SIZE_10BIT (1024 * sizeof(unsigned int))

#define ONE_SEC 1000000000ull

#define LCD_GPIO_NUM 8
#define LCD_POWER_NUM 4
#define LCD_POWER_STR_LEN 32
#define LCD_GPIO_REGU_NUM 3
#define LCD_GPIO_SCL (LCD_GPIO_NUM-2)
#define LCD_GPIO_SDA (LCD_GPIO_NUM-1)

struct panel_extend_para {
	unsigned int lcd_gamma_en;
	bool is_lcd_gamma_tbl_10bit;
	union{
		unsigned int lcd_gamma_tbl[256];
		unsigned int lcd_gamma_tbl_10bit[1024];
	};
	unsigned int lcd_cmap_en;
	unsigned int lcd_cmap_tbl[2][3][4];
	unsigned int lcd_bright_curve_tbl[256];
	unsigned int mgr_id;
};

struct disp_gpio_info {
	unsigned gpio;
	char name[32];
	int value;
};

struct disp_lcd_cfg {
	bool lcd_used;

	bool lcd_bl_en_used;
	struct disp_gpio_info lcd_bl_en;
	int lcd_bl_gpio_hdl;
	char lcd_bl_en_power[LCD_POWER_STR_LEN];
	struct regulator *bl_regulator;

	u32 lcd_power_used[LCD_POWER_NUM];
	char lcd_power[LCD_POWER_NUM][LCD_POWER_STR_LEN];
	struct regulator *regulator[LCD_POWER_NUM];

#ifdef CONFIG_AW_DRM
	u32 lcd_fix_power_used[LCD_POWER_NUM];
	char lcd_fix_power[LCD_POWER_NUM][LCD_POWER_STR_LEN];
#endif

	bool lcd_gpio_used[LCD_GPIO_NUM];
	struct disp_gpio_info lcd_gpio[LCD_GPIO_NUM];
	int gpio_hdl[LCD_GPIO_NUM];
	char lcd_gpio_power[LCD_GPIO_REGU_NUM][LCD_POWER_STR_LEN];
	struct regulator *gpio_regulator[LCD_GPIO_REGU_NUM];

	char lcd_pin_power[LCD_GPIO_REGU_NUM][LCD_POWER_STR_LEN];
	struct regulator *pin_regulator[LCD_GPIO_REGU_NUM];

	u32 backlight_bright;
	/*
	 * IEP-drc backlight dimming rate:
	 * 0 -256 (256: no dimming; 0: the most dimming)
	 */
	u32 backlight_dimming;
	u32 backlight_curve_adjust[101];

	u32 lcd_bright;
	u32 lcd_contrast;
	u32 lcd_saturation;
	u32 lcd_hue;
};

enum disp_pixel_type {
	DISP_PIXEL_TYPE_RGB = 0x0,
	DISP_PIXEL_TYPE_YUV = 0x1,
};

enum disp_layer_dirty_flags {
	LAYER_ATTR_DIRTY = 0x00000001,
	LAYER_VI_FC_DIRTY = 0x00000002,
	LAYER_HADDR_DIRTY = 0x00000004,
	LAYER_SIZE_DIRTY = 0x00000008,
	BLEND_ENABLE_DIRTY = 0x00000010,
	BLEND_ATTR_DIRTY = 0x00000020,
	BLEND_CTL_DIRTY        = 0x00000040,
	BLEND_OUT_DIRTY        = 0x00000080,
	LAYER_ATW_DIRTY        = 0x00000100,
	LAYER_HDR_DIRTY        = 0x00000200,
	LAYER_ALL_DIRTY        = 0x000003ff,
};

enum disp_manager_dirty_flags {
	MANAGER_ENABLE_DIRTY = 0x00000001,
	MANAGER_CK_DIRTY = 0x00000002,
	MANAGER_BACK_COLOR_DIRTY = 0x00000004,
	MANAGER_SIZE_DIRTY = 0x00000008,
	MANAGER_COLOR_RANGE_DIRTY = 0x00000010,
	MANAGER_COLOR_SPACE_DIRTY = 0x00000020,
	MANAGER_BLANK_DIRTY = 0x00000040,
	MANAGER_KSC_DIRTY = 0x00000080,
	MANAGER_PALETTE_DIRTY = 0x00000100,
	MANAGER_CM_DIRTY = 0x00000200,
	MANAGER_ALL_DIRTY = 0x00000fff,
};

/*
 * disp_afbc_info - ARM FrameBuffer Compress buffer info
 *
 * @header_layout: sub sampled info
 *                 0 - 444
 *                 1 - 420 (half horizontal and vertical resolution for chroma)
 *                 2 - 422 (half horizontal resolution for chroma)
 * @block_layout: superbloc layout
 *                 0 - 16x16
 *                 1 - 16x16 420 subsampling
 *                 2 - 16x16 422 subsampling
 *                 3 - 32x8  444 subsampling
 * @inputbits[4]: indicates the number of bits for every component
 * @yuv_transform: yuv transform is used on the decoded data
 * @ block_size[2]: the horizontal/vertical size in units of block
 * @ image_crop[2]: how many pixels to crop to the left/top of the output image
 */

struct disp_afbc_info {
	u32 header_layout;
	u32 block_layout;
	u32 inputbits[4];
	u32 yuv_transform;
	u32 block_size[2];
	u32 image_crop[2];
};

/* disp_atw_info_inner - asynchronous time wrap infomation
 *
 * @used: indicate if the atw funtion is used
 * @mode: atw mode
 * @b_row: the row number of the micro block
 * @b_col: the column number of the micro block
 * @cof_fd: dma_buf fd for the buffer contaied coefficient for atw
 */
struct disp_atw_info_inner {
	bool used;
	enum disp_atw_mode mode;
	unsigned int b_row;
	unsigned int b_col;
	int cof_fd;
	unsigned long long cof_addr;
};

/* disp_fb_info_inner - image buffer info on the inside
 *
 * @addr: buffer address for each plane
 * @size: size<width,height> for each buffer, unit:pixels
 * @align: align for each buffer, unit:bytes
 * @format: pixel format
 * @color_space: color space
 * @trd_right_addr: the right-eye buffer address for each plane,
 *                  valid when frame-packing 3d buffer input
 * @pre_multiply: indicate the pixel use premultiplied alpha
 * @crop: crop rectangle for buffer to be display
 * @flag: indicate stereo/non-stereo buffer
 * @scan: indicate interleave/progressive scan type, and the scan order
 * @metadata_buf: the phy_address to the buffer contained metadata for fbc/hdr
 * @metadata_size: the size of metadata buffer, unit:bytes
 * @metadata_flag: the flag to indicate the type of metadata buffer
 *	0     : no metadata
 *	1 << 0: hdr static metadata
 *	1 << 1: hdr dynamic metadata
 *	1 << 4:	frame buffer compress(fbc) metadata
 *	x     : all type could be "or" together
 */
struct disp_fb_info_inner {
	int fd;
	struct dma_buf           *dmabuf;
	unsigned long long       addr[3];
//	struct disp_rectsz       size[3];
//	unsigned int             align[3];
	unsigned int             pitch[3];
	enum disp_pixel_format   format;
	enum disp_color_space    color_space;
	int                      trd_right_fd;
	unsigned int             trd_right_addr[3];
	bool                     pre_multiply;
	struct disp_rect64       crop;
	enum disp_buffer_flags   flags;
	enum disp_scan_flags     scan;
	enum disp_eotf           eotf;
	int                      depth;
	unsigned int             fbd_en;
	unsigned int             tfbd_en;
	unsigned int             lbc_en;
	int                      metadata_fd;
	unsigned long long       metadata_buf;
	unsigned int             metadata_size;
	unsigned int             metadata_flag;
	struct disp_lbc_info     lbc_info;
	struct disp_tfbc_info    tfbc_info;
	struct disp_afbc_info    afbc_info;
	struct dma_buf           *metadata_dmabuf;
	struct sunxi_metadata    *p_metadata;
	struct afbc_header       *p_afbc_header;
};

/**
 * disp_snr_info
 */
struct disp_snr_info_inner {
	unsigned char en;
	unsigned char demo_en;
	struct disp_rect demo_win;
	unsigned char y_strength;
	unsigned char u_strength;
	unsigned char v_strength;
	unsigned char th_ver_line;
	unsigned char th_hor_line;
};

/* disp_layer_info_inner - layer info on the inside
 *
 * @mode: buffer/clolor mode, when in color mode, the layer is widthout buffer
 * @zorder: the zorder of layer, 0~max-layer-number
 * @alpha_mode:
 *	0: pixel alpha;
 *	1: global alpha
 *	2: mixed alpha, compositing width pixel alpha before global alpha
 * @alpha_value: global alpha value, valid when alpha_mode is not pixel alpha
 * @screen_win: the rectangle on the screen for fb to be display
 * @b_trd_out: indicate if 3d display output
 * @out_trd_mode: 3d output mode, valid when b_trd_out is true
 * @color: the color value to be display, valid when layer is in color mode
 * @fb: the framebuffer info related width the layer, valid when in buffer mode
 * @id: frame id, the user could get the frame-id display currently by
 *	DISP_LAYER_GET_FRAME_ID ioctl
 * @atw: asynchronous time wrap information
 */
struct disp_layer_info_inner {
	enum disp_layer_mode      mode;
	unsigned char             zorder;
	unsigned char             alpha_mode;
	unsigned char             alpha_value;
	struct disp_rect          screen_win;
	bool                      b_trd_out;
	enum disp_3d_out_mode     out_trd_mode;
	union {
		unsigned int               color;
		struct disp_fb_info_inner  fb;
	};

	unsigned int              id;
	struct disp_atw_info_inner atw;
#if defined(DE_VERSION_V33X) || defined(DE_VERSION_V35X)
	int transform;
	struct disp_snr_info_inner snr;
#endif
};

/* disp_layer_config_inner - layer config on the inside
 *
 * @info: layer info
 * @enable: indicate to enable/disable the layer
 * @channel: the channel index of the layer, 0~max-channel-number
 * @layer_id: the layer index of the layer widthin it's channel
 */
struct disp_layer_config_inner {
	struct disp_layer_info_inner info;
	bool enable;
	unsigned int channel;
	unsigned int layer_id;
};

struct disp_layer_config_data {
	struct disp_layer_config_inner config;
	enum disp_layer_dirty_flags flag;
};

struct disp_manager_info {
	struct disp_color back_color;
	struct disp_colorkey ck;
	struct disp_rect size;
	enum disp_csc_type cs;
	enum disp_color_space color_space;
	u32 color_range;
	u32 interlace;
	bool enable;
	/* index of device */
	u32 disp_device;
	/* indicate the index of timing controller */
	u32 hwdev_index;
	/* true: disable all layer; false: enable layer if requested */
	bool blank;
	u32 de_freq;
	enum disp_eotf eotf; /* sdr/hdr10/hlg */
	enum disp_data_bits data_bits;
	u32 device_fps;
	struct disp_ksc_info ksc;
	struct disp_palette_config palette;
	long long color_matrix[12];
	bool color_matrix_identity;
#ifdef CONFIG_AW_DRM
	enum disp_output_type conn_type;
#endif
};

struct disp_manager_data {
	struct disp_manager_info config;
	enum disp_manager_dirty_flags flag;
};

struct disp_enhance_info {
	/* basic adjust */
	/*
	 * enhance parameters : 0~10, bigger value, stronger enhance level
	 * mode : combination of enhance_mode and dev_type
	 * enhance_mode : bit31~bit16 of mode
	 *              : 0-disable; 1-enable; 2-demo(enable half window)
	 * dev_type : bit15~bit0 of mode
	 *          : 0-lcd; 1-tv(hdmi, cvbs, vga, ypbpr)
	 */
	u32 bright;
	u32 contrast;
	u32 saturation;
	u32 hue;
	u32         edge;
	u32         detail;
	u32         denoise;
	u32 mode;
	/* ehnance */
	u32 sharp;		/* 0-off; 1~3-on. */
	u32 auto_contrast;	/* 0-off; 1~3-on. */
	u32 auto_color;		/* 0-off; 1-on. */
	u32 fancycolor_red;	/* 0-Off; 1-2-on. */
	u32 fancycolor_green;	/* 0-Off; 1-2-on. */
	u32 fancycolor_blue;	/* 0-Off; 1-2-on. */
	struct disp_rect window;
	u32 enable;
	struct disp_rect size;
	u32 demo_enable;	/* 1: enable demo mode */
};

enum disp_enhance_dirty_flags {
	ENH_NONE_DIRTY       = 0x0,
	ENH_ENABLE_DIRTY     = 0x1 << 0,  /* enable dirty */
	ENH_SIZE_DIRTY       = 0x1 << 1,  /* size dirty */
	ENH_FORMAT_DIRTY     = 0x1 << 2,  /* overlay format dirty */
	ENH_BYPASS_DIRTY     = 0x1 << 3, /* bypass dirty */
	ENH_INIT_DIRTY       = 0x1 << 8,  /* initial parameters dirty */
	ENH_MODE_DIRTY       = 0X1 << 9,  /* enhance mode dirty */
	ENH_BRIGHT_DIRTY     = 0x1 << 10,  /* brightness level dirty */
	ENH_CONTRAST_DIRTY   = 0x1 << 11,  /* contrast level dirty */
	ENH_EDGE_DIRTY       = 0x1 << 12,  /* edge level dirty */
	ENH_DETAIL_DIRTY     = 0x1 << 13,  /* detail level dirty */
	ENH_SAT_DIRTY        = 0x1 << 14,  /* saturation level dirty */
	ENH_DNS_DIRTY        = 0x1 << 15, /* de-noise level dirty */
	ENH_USER_DIRTY       = 0xff00,     /* dirty by user */
	ENH_ALL_DIRTY        = 0xffff      /* all dirty */

};

struct disp_enhance_config {
	struct disp_enhance_info info;
	enum disp_enhance_dirty_flags flags;
};

struct peak_pq_cfg {
	u32 hp_ratio;
	u32 bp0_ratio;
	u32 bp1_ratio;
	u32 corth;
	u32 neg_gain;
	u32 dif_up;
	u32 beta;
	u32 gain;
};

struct g_pq_cfg {
	struct peak_pq_cfg peak;
};

enum disp_smbl_dirty_flags {
	SMBL_DIRTY_NONE = 0x00000000,
	SMBL_DIRTY_ENABLE = 0x00000001,
	SMBL_DIRTY_WINDOW = 0x00000002,
	SMBL_DIRTY_SIZE = 0x00000004,
	SMBL_DIRTY_BL = 0x00000008,
	SMBL_DIRTY_ALL = 0x0000000F,
};

struct disp_smbl_info {
	struct disp_rect window;
	u32 enable;
	struct disp_rect size;
	u32 backlight;
	u32 backlight_dimming;
	enum disp_smbl_dirty_flags flags;
};

struct disp_csc_config {
	u32 in_fmt;
	u32 in_mode;
	u32 in_color_range;
	u32 out_fmt;
	u32 out_mode;
	u32 out_color_range;
	u32 brightness;
	u32 contrast;
	u32 saturation;
	u32 hue;
	u32 enhance_mode;
	u32 color;
	u32 in_eotf;
	u32 out_eotf;
};

enum {
	DE_RGB = 0,
	DE_YUV = 1,
};

enum disp_capture_dirty_flags {
	CAPTURE_DIRTY_ADDRESS = 0x00000001,
	CAPTURE_DIRTY_WINDOW = 0x00000002,
	CAPTURE_DIRTY_SIZE = 0x00000004,
	CAPTURE_DIRTY_ALL = 0x00000007,
};
/* disp_s_frame_inner - display simple frame buffer
 *
 * @format: pixel format of fb
 * @size: size for each plane
 * @crop: crop zone to be fill image data
 * @fd: dma_buf fd
 * @addr: buffer addr for each plane
 */
struct disp_s_frame_inner {
	enum disp_pixel_format format;
	struct disp_rectsz size[3];
	struct disp_rect crop;
	unsigned long long addr[3];
	int fd;
};

/* disp_capture_info_inner - display capture information
 *
 * @window: the rectange on the screen to be capture
 * @out_frame: the framebuffer to be restore capture image data
 */
struct disp_capture_info_inner {
	struct disp_rect window;
	struct disp_s_frame_inner out_frame;
};
/* disp_capture_config - configuration for capture function
 *
 * @in_frame: input frame information
 * @out_frame: output framebuffer infomation
 * @disp: indicate which disp channel to be capture
 * @flags: caputre flags
 */
struct disp_capture_config {
	struct disp_s_frame_inner in_frame;	/* only format/size/crop valid */
	struct disp_s_frame_inner out_frame;
	u32 disp;		/* which disp channel to be capture */
	enum disp_capture_dirty_flags flags;
};

enum disp_lcd_if {
	LCD_IF_HV = 0,
	LCD_IF_CPU = 1,
	LCD_IF_LVDS = 3,
	LCD_IF_DSI = 4,
	LCD_IF_EDP = 5,
	LCD_IF_EXT_DSI = 6,
	LCD_IF_VDPO = 7,
};

enum disp_lcd_hv_if {
	LCD_HV_IF_PRGB_1CYC = 0,	/* parallel hv */
	LCD_HV_IF_SRGB_3CYC = 8,	/* serial hv */
	LCD_HV_IF_DRGB_4CYC = 10,	/* Dummy RGB */
	LCD_HV_IF_RGBD_4CYC = 11,	/* RGB Dummy */
	LCD_HV_IF_CCIR656_2CYC = 12,
};

enum disp_lcd_hv_srgb_seq {
	LCD_HV_SRGB_SEQ_RGB_RGB = 0,
	LCD_HV_SRGB_SEQ_RGB_BRG = 1,
	LCD_HV_SRGB_SEQ_RGB_GBR = 2,
	LCD_HV_SRGB_SEQ_BRG_RGB = 4,
	LCD_HV_SRGB_SEQ_BRG_BRG = 5,
	LCD_HV_SRGB_SEQ_BRG_GBR = 6,
	LCD_HV_SRGB_SEQ_GRB_RGB = 8,
	LCD_HV_SRGB_SEQ_GRB_BRG = 9,
	LCD_HV_SRGB_SEQ_GRB_GBR = 10,
};

enum disp_lcd_hv_syuv_seq {
	LCD_HV_SYUV_SEQ_YUYV = 0,
	LCD_HV_SYUV_SEQ_YVYU = 1,
	LCD_HV_SYUV_SEQ_UYUV = 2,
	LCD_HV_SYUV_SEQ_VYUY = 3,
};

enum disp_lcd_hv_syuv_fdly {
	LCD_HV_SYUV_FDLY_0LINE = 0,
	LCD_HV_SRGB_FDLY_2LINE = 1,	/* ccir ntsc */
	LCD_HV_SRGB_FDLY_3LINE = 2,	/* ccir pal */
};

enum disp_lcd_cpu_if {
	LCD_CPU_IF_RGB666_18PIN = 0,
	LCD_CPU_IF_RGB666_9PIN = 10,
	LCD_CPU_IF_RGB666_6PIN = 12,
	LCD_CPU_IF_RGB565_16PIN = 8,
	LCD_CPU_IF_RGB565_8PIN = 14,
};

enum disp_lcd_cpu_mode {
	LCD_CPU_AUTO_MODE = 0,
	LCD_CPU_TRIGGER_MODE = 1,
};

enum disp_lcd_te {
	LCD_TE_DISABLE = 0,
	LCD_TE_RISING = 1,
	LCD_TE_FALLING = 2,
};

enum disp_lcd_lvds_if {
	LCD_LVDS_IF_SINGLE_LINK = 0,
	LCD_LVDS_IF_DUAL_LINK = 1,
	LCD_LVDS_IF_DUAL_LINK_SAME_SRC = 2,
};

enum disp_lcd_lvds_colordepth {
	LCD_LVDS_8bit = 0,
	LCD_LVDS_6bit = 1,
};

enum disp_lcd_lvds_mode {
	LCD_LVDS_MODE_NS = 0,
	LCD_LVDS_MODE_JEIDA = 1,
};

enum disp_lcd_dsi_if {
	LCD_DSI_IF_VIDEO_MODE = 0,
	LCD_DSI_IF_COMMAND_MODE = 1,
	LCD_DSI_IF_BURST_MODE = 2,
};

enum disp_lcd_dsi_lane {
	LCD_DSI_1LANE = 1,
	LCD_DSI_2LANE = 2,
	LCD_DSI_3LANE = 3,
	LCD_DSI_4LANE = 4,
};

enum disp_lcd_dsi_format {
	LCD_DSI_FORMAT_RGB888 = 0,
	LCD_DSI_FORMAT_RGB666 = 1,
	LCD_DSI_FORMAT_RGB666P = 2,
	LCD_DSI_FORMAT_RGB565 = 3,
};

enum disp_lcd_frm {
	LCD_FRM_BYPASS = 0,
	LCD_FRM_RGB666 = 1,
	LCD_FRM_RGB565 = 2,
};

enum disp_lcd_cmap_color {
	LCD_CMAP_B0 = 0x0,
	LCD_CMAP_G0 = 0x1,
	LCD_CMAP_R0 = 0x2,
	LCD_CMAP_B1 = 0x4,
	LCD_CMAP_G1 = 0x5,
	LCD_CMAP_R1 = 0x6,
	LCD_CMAP_B2 = 0x8,
	LCD_CMAP_G2 = 0x9,
	LCD_CMAP_R2 = 0xa,
	LCD_CMAP_B3 = 0xc,
	LCD_CMAP_G3 = 0xd,
	LCD_CMAP_R3 = 0xe,
};

struct __disp_dsi_dphy_timing_t {
	unsigned int lp_clk_div;
	unsigned int hs_prepare;
	unsigned int hs_trail;
	unsigned int clk_prepare;
	unsigned int clk_zero;
	unsigned int clk_pre;
	unsigned int clk_post;
	unsigned int clk_trail;
	unsigned int hs_dly_mode;
	unsigned int hs_dly;
	unsigned int lptx_ulps_exit;
	unsigned int hstx_ana0;
	unsigned int hstx_ana1;
};

/**
 * lcd tcon mode(dual tcon drive dual dsi)
 */
enum disp_lcd_tcon_mode {
	DISP_TCON_NORMAL_MODE = 0,
	DISP_TCON_MASTER_SYNC_AT_FIRST_TIME,
	DISP_TCON_MASTER_SYNC_EVERY_FRAME,
	DISP_TCON_SLAVE_MODE,
	DISP_TCON_DUAL_DSI,
};

enum disp_lcd_dsi_port {
	DISP_LCD_DSI_SINGLE_PORT = 0,
	DISP_LCD_DSI_DUAL_PORT,
};

struct disp_lcd_esd_info {
	/* 1:reset all module include tcon; 0:reset panel only */
	unsigned char level;
	/* unit:frame */
	unsigned short freq;
	/* 1:in disp isr; 0:in reflush work */
	unsigned char esd_check_func_pos;
	/* count */
	unsigned int cnt;
	/* reset count */
	unsigned int rst_cnt;
};

enum div_flag {
	INCREASE        = 1,
	DECREASE        = -1,
};

struct clk_div_ajust {
	enum div_flag clk_div_increase_or_decrease;
	int div_multiple;
};

struct disp_panel_para {
	enum disp_lcd_if lcd_if;

	enum disp_lcd_hv_if lcd_hv_if;
	enum disp_lcd_hv_srgb_seq lcd_hv_srgb_seq;
	enum disp_lcd_hv_syuv_seq lcd_hv_syuv_seq;
	enum disp_lcd_hv_syuv_fdly lcd_hv_syuv_fdly;

	enum disp_lcd_lvds_if lcd_lvds_if;
	enum disp_lcd_lvds_colordepth lcd_lvds_colordepth;
	enum disp_lcd_lvds_mode lcd_lvds_mode;
	unsigned int lcd_lvds_io_polarity;
	unsigned int lcd_lvds_clk_polarity;

	enum disp_lcd_cpu_if lcd_cpu_if;
	enum disp_lcd_te lcd_cpu_te;
	enum disp_lcd_dsi_port lcd_dsi_port_num;
	enum disp_lcd_tcon_mode  lcd_tcon_mode;
	unsigned int             lcd_slave_stop_pos;
	unsigned int             lcd_sync_pixel_num;
	unsigned int             lcd_sync_line_num;
	unsigned int             lcd_slave_tcon_num;
	enum disp_lcd_cpu_mode lcd_cpu_mode;

	enum disp_lcd_dsi_if lcd_dsi_if;
	enum disp_lcd_dsi_lane lcd_dsi_lane;
	enum disp_lcd_dsi_format lcd_dsi_format;
	unsigned int lcd_dsi_eotp;
	unsigned int lcd_dsi_vc;
	enum disp_lcd_te lcd_dsi_te;

	unsigned int             lcd_tcon_en_odd_even;

	unsigned int lcd_dsi_dphy_timing_en;
	struct __disp_dsi_dphy_timing_t *lcd_dsi_dphy_timing_p;

	unsigned int lcd_fsync_en;
	unsigned int lcd_fsync_act_time;
	unsigned int lcd_fsync_dis_time;
	unsigned int lcd_fsync_pol;

	unsigned int lcd_dclk_freq;
	unsigned int lcd_x;	/* horizontal resolution */
	unsigned int lcd_y;	/* vertical resolution */
	unsigned int lcd_width;	/* width of lcd in mm */
	unsigned int lcd_height;	/* height of lcd in mm */
	unsigned int lcd_xtal_freq;

	unsigned int lcd_pwm_used;
	unsigned int lcd_pwm_ch;
	unsigned int lcd_pwm_freq;
	unsigned int lcd_pwm_pol;

	unsigned int lcd_rb_swap;
	unsigned int lcd_rgb_endian;

	unsigned int lcd_vt;
	unsigned int lcd_ht;
	unsigned int lcd_vbp;
	unsigned int lcd_hbp;
	unsigned int lcd_vspw;
	unsigned int lcd_hspw;

	unsigned int lcd_interlace;
	unsigned int lcd_hv_clk_phase;
	unsigned int lcd_hv_sync_polarity;

	unsigned int lcd_frm;
	unsigned int lcd_gamma_en;
	unsigned int lcd_cmap_en;
	unsigned int lcd_bright_curve_en;
	unsigned int lcd_start_delay;

	char lcd_size[8];	/* e.g. 7.9, 9.7 */
	char lcd_model_name[32];
	char lcd_driver_name[32];

	unsigned int tcon_index;	/* not need to config for user */
	unsigned int lcd_fresh_mode;	/* not need to config for user */
	unsigned int lcd_dclk_freq_original;/* not need to config for user */
	unsigned int ccir_clk_div;/* not need to config for user */
	unsigned int input_csc;/* not need to config for user */
	unsigned int lcd_gsensor_detect;
	unsigned int lcd_hv_data_polarity;
	struct clk_div_ajust tcon_clk_div_ajust;
};

enum disp_mod_id {
	DISP_MOD_DE = 0,
#if defined(CONFIG_INDEPENDENT_DE)
	DISP_MOD_DE1,
#endif
	DISP_MOD_DE_FREEZE,
	DISP_MOD_DEVICE,	/* for timing controller common module */
#if defined(CONFIG_INDEPENDENT_DE)
	DISP_MOD_DEVICE1,
#endif
	DISP_MOD_LCD0,
	DISP_MOD_LCD1,
	DISP_MOD_LCD2,
	DISP_MOD_LCD3,
	DISP_MOD_LCD4,
	DISP_MOD_DSI0,
	DISP_MOD_DSI1,
	DISP_MOD_DSI2,
	DISP_MOD_DSI3,
	DISP_MOD_HDMI,
	DISP_MOD_LVDS,
	DISP_MOD_LVDS1,
	DISP_MOD_EINK,
	DISP_MOD_EDMA,
	DISP_MOD_VDPO,
#if defined(CONFIG_INDEPENDENT_DE)
	DISP_MOD_DPSS0,
	DISP_MOD_DPSS1,
#endif
	DISP_MOD_NUM,
};

struct disp_bootloader_info {
	int sync;		/* 1: sync width bootloader */
	int disp;		/* output disp at bootloader period */
	int type;		/* output type at bootloader period */
	int mode; /* output mode at bootloader period */
	int format;/* YUV or RGB */
	int bits; /* color deep */
	int eotf;
	int cs; /* color space */
	enum disp_dvi_hdmi	dvi_hdmi;
	enum disp_color_range	range;
	enum disp_scan_info		scan;
	unsigned int			aspect_ratio;

};

#define DEBUG_TIME_SIZE 100
struct disp_bsp_init_para {
	uintptr_t reg_base[DISP_MOD_NUM];
	u32 irq_no[DISP_MOD_NUM];

#if defined(CONFIG_AW_DRM) || defined(SUPPORT_VGA)
	struct clk *mclk[DISP_MOD_NUM];
#endif
	struct clk *clk_de[DE_NUM];
	struct clk *clk_bus_de[DE_NUM];
#if defined(HAVE_DEVICE_COMMON_MODULE)
	struct clk *clk_bus_extra;
#endif
	struct clk *clk_bus_dpss_top[DISP_DEVICE_NUM];
	struct clk *clk_tcon[DISP_DEVICE_NUM];
	struct clk *clk_bus_tcon[DISP_DEVICE_NUM];
	struct clk *clk_mipi_dsi[CLK_DSI_NUM];
	struct clk *clk_bus_mipi_dsi[CLK_DSI_NUM];

	struct reset_control *rst_bus_de[DE_NUM];
	struct reset_control *rst_bus_de_sys[DE_NUM];
#if defined(HAVE_DEVICE_COMMON_MODULE)
	struct reset_control *rst_bus_extra;
#endif
	struct reset_control *rst_bus_dpss_top[DISP_DEVICE_NUM];
	struct reset_control *rst_bus_video_out[DISP_DEVICE_NUM];
	struct reset_control *rst_bus_mipi_dsi[DEVICE_DSI_NUM];

#if defined(DE_VERSION_V35X)
	struct clk *clk_mipi_dsi_combphy[CLK_DSI_NUM];
#endif
	struct reset_control *rst_bus_tcon[DISP_DEVICE_NUM];
	struct reset_control *rst_bus_lvds[DEVICE_LVDS_NUM];

	s32 (*disp_int_process)(u32 sel);
	s32 (*vsync_event)(u32 sel);
	s32 (*start_process)(void);
	s32 (*capture_event)(u32 sel);
	s32 (*shadow_protect)(u32 sel, bool protect);
	struct disp_bootloader_info boot_info;
	struct disp_feat_init feat_init;
};

typedef void (*LCD_FUNC) (unsigned int sel);
typedef void (*EDP_FUNC) (unsigned int sel);
struct disp_lcd_function {
	LCD_FUNC func;
	unsigned int delay;	/* ms */
};

#define LCD_MAX_SEQUENCES 7
struct disp_lcd_flow {
	struct disp_lcd_function func[LCD_MAX_SEQUENCES];
	unsigned int func_num;
	unsigned int cur_step;
};

struct disp_lcd_panel_fun {
	void (*cfg_panel_info)(struct panel_extend_para *info);
	int (*cfg_open_flow)(unsigned int sel);
	int (*cfg_close_flow)(unsigned int sel);
	int (*lcd_user_defined_func)(unsigned int sel, unsigned int para1,
				      unsigned int para2, unsigned int para3);
	int (*set_bright)(unsigned int sel, unsigned int bright);
	/* check if panel is ok.return 0 if ok */
	int (*esd_check)(unsigned int sel);
	/* reset panel flow */
	int (*reset_panel)(unsigned int sel);
	/* see the definition of struct disp_lcd_esd_info */
	int (*set_esd_info)(struct disp_lcd_esd_info *p_info);
};

void aw_memcpy_toio(volatile void __iomem *to, const void *from, size_t count);
void aw_memcpy_fromio(void *to, const volatile void __iomem *from, size_t count);

#endif
