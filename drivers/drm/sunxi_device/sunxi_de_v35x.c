/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/* sunxi_de_v35x.c
 *
 * Copyright (C) 2023 Allwinnertech Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <uapi/drm/drm_fourcc.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <drm/drm_print.h>
#include <linux/of_graph.h>
#include <drm/drm_drv.h>
#include <linux/component.h>
#include <linux/iopoll.h>

#include "disp_al_de.h"
#include "sunxi_drm_crtc.h"

#define DISPLAY_NUM_MAX 4
#define MY_BYTE_ALIGN(x) (((x + (4 * 1024 - 1)) >> 12) << 12)

/* TODO remove this macro */
#define OVL_MAX				4
#define OVL_REMAIN			(OVL_MAX - 1)

enum rcq_update {
	RCQ_UPDATE_NONE = 0,
	RCQ_UPDATE_ACCEPT = 1,
	RCQ_UPDATE_FINISHED = 2,
};

struct sunxi_de_out {
	int id;
	struct device *dev;
	struct sunxi_drm_crtc *scrtc;
	struct device_node *port;

	unsigned int vichannel_cnt;
	unsigned int uichannel_cnt;
	unsigned int layer_cnt;
	bool enable;
	struct disp_layer_config_data *layer_cfg;
};

struct sunxi_display_engine {
	struct device *dev;
	void __iomem *reg_base;
	struct clk *mclk;
	struct clk *mclk_bus;
	struct reset_control *rst_bus_de;
	unsigned int irq_no;

	unsigned int chn_cfg_mode;
	unsigned char display_out_cnt;
	struct sunxi_de_out display_out[DISPLAY_NUM_MAX];
};

static const unsigned int ui_layer_formats[] = {
	DRM_FORMAT_RGB888,   DRM_FORMAT_BGR888,

	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888,

	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888, DRM_FORMAT_BGRX8888,

	DRM_FORMAT_RGB565,   DRM_FORMAT_BGR565,

	DRM_FORMAT_ARGB4444, DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_BGRA4444,

	DRM_FORMAT_ARGB1555, DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551, DRM_FORMAT_BGRA5551,
};

static const unsigned int vi_layer_formats[] = {
	DRM_FORMAT_RGB888,   DRM_FORMAT_BGR888,

	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888, DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,

	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888, DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,

	DRM_FORMAT_RGB565,   DRM_FORMAT_BGR565,

	DRM_FORMAT_ARGB4444, DRM_FORMAT_ABGR4444, DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,

	DRM_FORMAT_ARGB1555, DRM_FORMAT_ABGR1555, DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,

	DRM_FORMAT_AYUV,     DRM_FORMAT_YUV444,	  DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV420,   DRM_FORMAT_YVU420,   DRM_FORMAT_YUV411,

	DRM_FORMAT_NV61,     DRM_FORMAT_NV16,	  DRM_FORMAT_NV21,
	DRM_FORMAT_NV12,
};

void *disp_malloc(void *dev, u32 num_bytes, void *phys_addr)
{
	u32 actual_bytes;
	struct device *de_dev = dev;
	void *address = NULL;

	if (!dev) {
		DRM_ERROR("%s fail, dev is NULL\n", __FUNCTION__);
		return NULL;
	}

	if (num_bytes != 0) {
		actual_bytes = MY_BYTE_ALIGN(num_bytes);

		address =
			dma_alloc_coherent(de_dev, actual_bytes,
					   (dma_addr_t *)phys_addr, GFP_KERNEL);
		if (address) {
			DRM_INFO(
				"dma_alloc_coherent ok, address=0x%p, size=0x%x\n",
				(void *)(*(unsigned long *)phys_addr),
				num_bytes);
			return address;
		}

		DRM_ERROR("dma_alloc_coherent fail, size=0x%x\n", num_bytes);
		return NULL;
	}
	DRM_ERROR("%s size is zero\n", __func__);

	return NULL;
}

void disp_free(void *dev, void *virt_addr, void *phys_addr, u32 num_bytes)
{
	u32 actual_bytes;
	struct device *de_dev = dev;

	if (!dev) {
		DRM_ERROR("%s fail, dev is NULL\n", __FUNCTION__);
	}

	actual_bytes = MY_BYTE_ALIGN(num_bytes);
	if (phys_addr && virt_addr)
		dma_free_coherent(de_dev, actual_bytes, virt_addr,
				  (dma_addr_t)phys_addr);
}

unsigned long sunxi_de_get_freq(struct sunxi_de_out *hwde)
{
	struct sunxi_display_engine *de_drv;
	de_drv = dev_get_drvdata(hwde->dev);
	return clk_get_rate(de_drv->mclk);
}

void sunxi_de_get_layer_formats(struct sunxi_de_out *hwde,
				unsigned int channel_id,
				const unsigned int **formats,
				unsigned int *count)
{
	if (channel_id < hwde->vichannel_cnt) {
		*formats = vi_layer_formats;
		*count = ARRAY_SIZE(vi_layer_formats);
	} else {
		*formats = ui_layer_formats;
		*count = ARRAY_SIZE(ui_layer_formats);
	}
}

int sunxi_de_get_layer_features(struct sunxi_de_out *hwde, unsigned int channel_id)
{
	if (de_feat_is_support_fbd_by_layer(hwde->id, channel_id, 0))
		return SUNXI_PLANE_FEATURE_AFBC;
	return 0;
}

/*
static int sunxi_de_protect_reg_for_rcq(
		struct sunxi_de *de, bool protect)
{
	int ret;

	if (protect && (atomic_read(&rcq_update_flag) == RCQ_UPDATE_ACCEPT)) {
		ret = wait_event_timeout(rcq_update_queue,
				atomic_read(&rcq_update_flag) == RCQ_UPDATE_FINISHED,
				msecs_to_jiffies(1000 / de->info.device_fps + 1));
		if (ret <= 0) {
			DRM_ERROR("Wait rcq timeout!skip frame!\n");
			atomic_set(&rcq_update_flag, RCQ_UPDATE_FINISHED);
			wake_up(&rcq_update_queue);
			de_rtmx_set_rcq_update(de->id, 0);
		} else
			de_rtmx_set_all_rcq_head_dirty(de->id, 0);

	} else if (!protect) {
		de_rtmx_set_all_rcq_head_dirty(de->id, 1);
		de_rtmx_set_rcq_update(de->id, 1);
		atomic_set(&rcq_update_flag, RCQ_UPDATE_ACCEPT);
	}

	return 0;
}
*/

#ifdef SUNXI_DE_V35X_NOT_USED
static int sunxi_de_layer_enhance_apply(unsigned int disp,
					struct disp_layer_config_data *data,
					unsigned int layer_num)
{
	struct disp_enhance_chn_info *ehs_info;
	struct de_rtmx_context *ctx = de_rtmx_get_context(disp);
	struct de_chn_info *chn_info = ctx->chn_info;
	u32 vi_chn_num = de_feat_get_num_vi_chns(disp);
	u32 i;

	ehs_info = kmalloc(sizeof(*ehs_info) * vi_chn_num,
			   GFP_KERNEL | __GFP_ZERO);
	if (ehs_info == NULL) {
		pr_warn("%s failed to kmalloc!\n", __func__);
		return -1;
	}
	memset((void *)ehs_info, 0, sizeof(*ehs_info) * vi_chn_num);
	for (i = 0; i < layer_num; ++i, ++data) {
		if (data->config.enable &&
		    (data->config.channel < vi_chn_num)) {
			struct disp_enhance_layer_info *ehs_layer_info =
				&ehs_info[data->config.channel]
					 .layer_info[data->config.layer_id];

			ehs_layer_info->fb_size.width =
				data->config.info.fb.size[0].width;
			ehs_layer_info->fb_size.height =
				data->config.info.fb.size[0].height;
			ehs_layer_info->fb_crop.x =
				data->config.info.fb.crop.x >> 32;
			ehs_layer_info->fb_crop.y =
				data->config.info.fb.crop.y >> 32;
			ehs_layer_info->en = 1;
			ehs_layer_info->format = data->config.info.fb.format;
		}
	}
	for (i = 0; i < vi_chn_num; i++) {
		ehs_info[i].ovl_size.width = chn_info->ovl_out_win.width;
		ehs_info[i].ovl_size.height = chn_info->ovl_out_win.height;
		ehs_info[i].bld_size.width = chn_info->scn_win.width;
		ehs_info[i].bld_size.height = chn_info->scn_win.height;
	}
	/* set enhance size */
	de_enhance_layer_apply(disp, ehs_info);
	kfree(ehs_info);
	return 0;
}
#endif

int sunxi_de_layer_update(struct sunxi_de_out *hwde,  const struct disp_layer_config_inner *data)
{
	int nr = hwde->id;
	int i = data->channel * OVL_MAX + data->layer_id;
	struct disp_layer_config_data *cfg = &hwde->layer_cfg[i];
	if (cfg->config.channel != data->channel || cfg->config.layer_id != data->layer_id) {
		DRM_ERROR("fatal err layer cfg not found %d %d %d %d\n",
				cfg->config.channel, data->channel, cfg->config.layer_id, data->layer_id);
		return -1;
	}
	DRM_DEBUG_DRIVER(
		"%s id %d ch %d layer %d en %d crop %d %d %d %d "
		"frame%d %d %d %d addr %lx fbd %d alpha%d:%d z%d color %d\n",
		__FUNCTION__, nr, data->channel, data->layer_id,
		data->enable,/* (int)(data->info.fb.size[0].width),
		(int)(data->info.fb.size[0].height),*/
		(int)(data->info.fb.crop.x >> 32),
		(int)(data->info.fb.crop.y >> 32),
		(int)(data->info.fb.crop.width >> 32),
		(int)(data->info.fb.crop.height >> 32),
		(int)(data->info.screen_win.x),
		(int)(data->info.screen_win.y),
		(int)(data->info.screen_win.width),
		(int)(data->info.screen_win.height),
		(unsigned long)data->info.fb.addr[0], data->info.fb.fbd_en,
		(int)(data->info.alpha_mode), (int)(data->info.alpha_value),
		data->info.zorder, data->info.mode
		);
	memcpy(&cfg->config, data, sizeof(*data));
	cfg->flag = LAYER_ALL_DIRTY;
	return 0;
}

/*
int sunxi_de_single_layer_apply(struct sunxi_de_out *hwde,
				struct disp_layer_config_data *data)
{
	int nr = hwde->id;
	int layer_id;
	struct disp_layer_config_data *data_tmp = NULL;

	layer_id = data->config.channel * OVL_MAX + data->config.layer_id;
	data_tmp = &hwde->layer_config_data[layer_id];
	memcpy(data_tmp, data, sizeof(*data));
	DRM_DEBUG_DRIVER(
		"%s id %d ch %d layer %d en %d w %d h %d crop %d %d %d %d frame%d %d %d %d addr %lx\n",
		__FUNCTION__, nr, data->config.channel, data->config.layer_id,
		data->config.enable, (int)(data->config.info.fb.size[0].width),
		(int)(data->config.info.fb.size[0].height),
		(int)(data->config.info.fb.crop.x >> 32),
		(int)(data->config.info.fb.crop.y >> 32),
		(int)(data->config.info.fb.crop.width >> 32),
		(int)(data->config.info.fb.crop.height >> 32),
		(int)(data->config.info.screen_win.x),
		(int)(data->config.info.screen_win.y),
		(int)(data->config.info.screen_win.width),
		(int)(data->config.info.screen_win.height),
		(unsigned long)data->config.info.fb.addr[0]);
	if (data_tmp->config.channel == 0)
		data_tmp->config.info.zorder = 16; //????????????

	de_rtmx_layer_apply(nr, hwde->layer_config_data, hwde->layer_cnt);
	sunxi_de_layer_enhance_apply(nr, hwde->layer_config_data,
				     hwde->layer_cnt);
	return 0;
}
*/

/*
static irqreturn_t sunxi_de_rcq_finish_irq_handler(int irq, void *arg)
{
	unsigned int irq_state;
	struct sunxi_de *hwde = (struct sunxi_de *)arg;

	irq_state = de_top_query_state_with_clear(hwde->id,
		DISP_AL_IRQ_STATE_RCQ_ACCEPT | DISP_AL_IRQ_STATE_RCQ_FINISH);

	if (irq_state & DISP_AL_IRQ_STATE_RCQ_FINISH) {
		DRM_INFO("DISP_AL_IRQ_STATE_RCQ_FINISH line %d\n", readline());
	} else if (irq_state & DISP_AL_IRQ_STATE_RCQ_ACCEPT) {
		DRM_INFO("DISP_AL_IRQ_STATE_RCQ_ACCEPT line %d\n", read_line());
	}

	return IRQ_HANDLED;
}
*/
static inline bool is_update_finished(int disp)
{
	u32 state = de_top_query_state_with_clear(disp,
						  DISP_AL_IRQ_STATE_RCQ_FINISH);
	return (state & DISP_AL_IRQ_STATE_RCQ_FINISH) ? true : false;
}

void sunxi_de_atomic_begin(struct sunxi_de_out *hwde)
{
	/* TODO this cause vep/dep reg which is not update to real reg lost update */
	de_rtmx_set_rcq_update(hwde->id, 0);
	de_rtmx_set_all_rcq_head_dirty(hwde->id, 0);
}

/* TODO vep dep enhance (thread/workqueue) may write rcq reg without protect, this may cause hw go wrong
 */
void sunxi_de_atomic_flush(struct sunxi_de_out *hwde)
{
	int i;
	int disp = hwde->id;
	bool is_finished = false;
	bool timeout = false;
	struct disp_layer_config_data *cfg = hwde->layer_cfg;
	if (!hwde->enable) {
		DRM_INFO("%s de %d not enable, skip\n", __func__, disp);
		return;
	}

	is_update_finished(disp);

	de_rtmx_layer_apply(disp, cfg, hwde->layer_cnt);
	for (i = 0; i < hwde->layer_cnt; i++) {
		cfg[i].flag = 0;
	}

	de_rtmx_set_rcq_update(hwde->id, 1);
	if (hwde->enable)
	timeout =
		read_poll_timeout(is_update_finished, is_finished,
				  is_finished, 100, 50000, false, disp);
	if (timeout)
		DRM_INFO("%s timeout\n", __func__);
}

int sunxi_de_enable(struct sunxi_de_out *hwde,
		    const struct disp_manager_info *info, bool sw)
{
	int ret = 0;
	int nr = hwde->id;
	struct sunxi_display_engine *de_drv = dev_get_drvdata(hwde->dev);
	struct disp_manager_data mdata;

	DRM_INFO("%s index %d\n", __FUNCTION__, nr);
	if (hwde->enable) {
		DRM_INFO("[SUNXI-DE]WARN:sunxi has been enable,"
			 "do NOT enable it again\n");
		return 0;
	}

	hwde->enable = true;
	ret = reset_control_deassert(de_drv->rst_bus_de);
	if (ret) {
		DRM_ERROR("reset_control_deassert for rst_bus_de failed\n\n");
		return -1;
	}

	ret = clk_prepare_enable(de_drv->mclk);
	if (ret < 0) {
		DRM_ERROR("Enable de module clk failed\n\n");
		return -1;
	}

	ret = clk_prepare_enable(de_drv->mclk_bus);
	if (ret < 0) {
		DRM_ERROR("Enable de module bus clk failed\n");
		return -1;
	}
	pm_runtime_get_sync(hwde->dev);

	de_rtmx_start(nr);
	mdata.flag = MANAGER_ALL_DIRTY;
	memcpy(&mdata.config, info, sizeof(*info));
	mdata.config.de_freq = clk_get_rate(de_drv->mclk);
	de_rtmx_mgr_apply(nr, &mdata);
	sunxi_de_atomic_flush(hwde);
	DRM_INFO("%s end sw=%d\n", __FUNCTION__, sw);
	return 0;
}

void sunxi_de_disable(struct sunxi_de_out *hwde)
{
	int nr = hwde->id;
	struct sunxi_display_engine *de_drv = dev_get_drvdata(hwde->dev);
	DRM_INFO("%s de %d\n", __FUNCTION__, nr);
	if (!hwde->enable) {
		DRM_INFO("[SUNXI-DE]WARN:sunxi has NOT been enable,"
			 "can NOT enable it\n");
		return;
	}

	hwde->enable = false;
	de_top_enable_irq(nr, (DE_IRQ_FLAG_RCQ_FINISH)&DE_IRQ_FLAG_MASK, 0);
	de_rtmx_stop(nr);

	pm_runtime_put_sync(hwde->dev);
	clk_disable_unprepare(de_drv->mclk);
	return;
}

int sunxi_de_event_proc(struct sunxi_de_out *hwde)
{
	return 0;
}

static int sunxi_de_v35x_al_init(struct device *dev,
				 struct disp_bsp_init_para *para)
{
	if (de_top_mem_pool_alloc(dev))
		return -1;

	de_enhance_init(para);
	//de_smbl_init(para->reg_base[DISP_MOD_DE]);
	de_rtmx_init(para);
	de_wb_init(para);
	return 0;
}

static int sunxi_de_v35x_al_exit(struct device *dev)
{
	//	de_wb_exit();
	//	de_rtmx_exit();
	//	de_smbl_exit();
	//	de_enhance_exit();
	de_top_mem_pool_free(dev);
	return 0;
}

static int sunxi_de_init_al(struct sunxi_display_engine *de_drv)
{
	struct disp_bsp_init_para para;
	struct de_feat_init de_feat;

	de_feat.chn_cfg_mode = de_drv->chn_cfg_mode;
	de_feat_init_config(&de_feat);
	memset(&para, 0, sizeof(para));

	para.reg_base[DISP_MOD_DE] = (uintptr_t)de_drv->reg_base;
	para.irq_no[DISP_MOD_DE] = de_drv->irq_no;
	return sunxi_de_v35x_al_init(de_drv->dev, &para);
}

static int sunxi_de_parse_dts(struct device *dev,
			      struct sunxi_display_engine *engine)
{
	struct device_node *node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	node = dev->of_node;
	if (!node) {
		DRM_ERROR("get sunxi-de node err.\n ");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	engine->reg_base = devm_ioremap_resource(dev, res);

	DRM_INFO("[SUNXI-DE] reg 0x%lx\n", (unsigned long)engine->reg_base);
	if (!engine->reg_base) {
		DRM_ERROR("unable to map de registers\n");
		return -EINVAL;
	}

	engine->mclk = devm_clk_get(dev, "clk_de");
	if (IS_ERR(engine->mclk)) {
		DRM_ERROR("fail to get clk for de\n");
		return -EINVAL;
	}

	engine->mclk_bus = devm_clk_get(dev, "clk_bus_de");
	if (IS_ERR(engine->mclk)) {
		DRM_ERROR("fail to get bus clk for de\n");
		return -EINVAL;
	}

	engine->rst_bus_de = devm_reset_control_get_shared(dev, "rst_bus_de");
	if (IS_ERR(engine->rst_bus_de)) {
		DRM_ERROR("fail to get reset clk for rst_bus_de\n");
		return -EINVAL;
	}

	engine->irq_no = platform_get_irq(pdev, 0);
	if (!engine->irq_no) {
		DRM_ERROR("irq_of_parse_and_map de irq fail\n");
		return -EINVAL;
	}

	if (of_property_read_u32(node, "chn_cfg_mode", &engine->chn_cfg_mode) <
	    0) {
		DRM_ERROR("failed to read chn_cfg_mode\n");
		return -EINVAL;
	}

	return 0;
}

static inline int get_de_output_display_cnt(struct device *dev)
{
	struct device_node *port;
	int i = 0;

	while (true) {
		port = of_graph_get_port_by_id(dev->of_node, i);
		if (!port)
			break;
		i++;
		of_node_put(port);
	};
	return i;
}

static int sunxi_display_engine_init(struct device *dev)
{
	int ret;
	struct sunxi_display_engine *engine;

	DRM_INFO("[SUNXI-DE] %s start\n", __FUNCTION__);
	engine = devm_kzalloc(dev, sizeof(*engine), GFP_KERNEL);
	engine->dev = dev;
	dev_set_drvdata(dev, engine);

	ret = sunxi_de_parse_dts(dev, engine);
	if (ret < 0) {
		DRM_ERROR("Parse de dts failed!\n");
		goto de_err;
	}

	if (sunxi_de_init_al(engine) < 0) {
		DRM_ERROR("sunxi_de_init_al failed!\n");
		goto de_err;
	}

	engine->display_out_cnt = get_de_output_display_cnt(dev);
	DRM_INFO("[SUNXI-DE] %s display port cnt %d\n", __FUNCTION__,
		 engine->display_out_cnt);
	if ((engine->display_out_cnt <= 0) ||
	    (engine->display_out_cnt > DISPLAY_NUM_MAX)) {
		DRM_ERROR("get wrong display_out_cnt");
		goto de_err;
	}
	return 0;
de_err:
	DRM_ERROR("%s FAILED\n", __FUNCTION__);
	return -EINVAL;
}

static int sunxi_display_engine_exit(struct device *dev)
{
	sunxi_de_v35x_al_exit(dev);
	return 0;
}

static int sunxi_de_bind(struct device *dev, struct device *master, void *data)
{
	int ret, i, j;
	struct drm_device *drm = data;
	struct sunxi_display_engine *engine = dev_get_drvdata(dev);

	DRM_INFO("[SUNXI-DE] %s %d\n", __FUNCTION__, __LINE__);
	if (of_find_property(dev->of_node, "iommus", NULL)) {
		ret = of_dma_configure(drm->dev, dev->of_node, true);
		if (ret) {
			DRM_ERROR("of_dma_configure fail\n");
			return ret;
		}
	}

	for (i = 0; i < engine->display_out_cnt; i++) {
		struct sunxi_de_out *display_out = &engine->display_out[i];
		struct sunxi_de_info info;
		display_out->id = i;
		display_out->dev = dev;
		display_out->vichannel_cnt = de_feat_get_num_vi_chns(i);
		display_out->uichannel_cnt = de_feat_get_num_ui_chns(i);
		display_out->port = of_graph_get_port_by_id(dev->of_node, i);
		display_out->layer_cnt = (display_out->vichannel_cnt +
					  display_out->uichannel_cnt) *
					 OVL_MAX;
		display_out->layer_cfg = devm_kzalloc(dev, display_out->layer_cnt * sizeof(*display_out->layer_cfg), GFP_KERNEL);
		if (!display_out->layer_cfg)
			return -ENOMEM;
		for (j = 0; j < display_out->layer_cnt; j++) {
			display_out->layer_cfg[j].flag = LAYER_ALL_DIRTY;
			display_out->layer_cfg[j].config.enable = 0;
			display_out->layer_cfg[j].config.channel = j / OVL_MAX;
			display_out->layer_cfg[j].config.layer_id = j % OVL_MAX;
		}
		memset(&info, 0, sizeof(info));
		info.drm = drm;
		info.de_out = display_out;
		info.port = display_out->port;
		info.hw_id = display_out->id;
		info.v_chn_cnt = display_out->vichannel_cnt;
		info.u_chn_cnt = display_out->uichannel_cnt;
		display_out->scrtc = sunxi_drm_crtc_init_one(&info);
		DRM_INFO("%s crtc init for de %d %lx ok\n", __FUNCTION__,
			 info.hw_id, (unsigned long)display_out->layer_cfg);
	}
	return 0;
}

static void sunxi_de_unbind(struct device *dev, struct device *master,
			    void *data)
{
	int i;
	struct sunxi_display_engine *engine = dev_get_drvdata(dev);
	DRM_INFO("[SUNXI-DE] %s %d \n", __FUNCTION__, __LINE__);
	for (i = 0; i < engine->display_out_cnt; i++) {
		sunxi_drm_crtc_destory(engine->display_out[i].scrtc);
	}
}

static const struct component_ops sunxi_de_component_ops = {
	.bind = sunxi_de_bind,
	.unbind = sunxi_de_unbind,
};

static int sunxi_de_probe(struct platform_device *pdev)
{
	int ret;
	DRM_INFO("[SUNXI-DE] %s %d \n", __FUNCTION__, __LINE__);
	ret = sunxi_display_engine_init(&pdev->dev);
	if (ret)
		goto OUT;
	ret = component_add(&pdev->dev, &sunxi_de_component_ops);
	if (ret) {
		DRM_ERROR("failed to add component de\n");
		goto EXIT;
	}
	pm_runtime_enable(&pdev->dev);
	return 0;
EXIT:
	sunxi_display_engine_exit(&pdev->dev);
OUT:
	return ret;
}

static int sunxi_de_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	component_del(&pdev->dev, &sunxi_de_component_ops);
	sunxi_display_engine_exit(&pdev->dev);
	return 0;
}

static const struct of_device_id sunxi_de_match[] = {
	{
		.compatible = "allwinner,display-engine",
	},
	{},
};

struct platform_driver sunxi_de_platform_driver = {
	.probe = sunxi_de_probe,
	.remove = sunxi_de_remove,
	.driver = {
		   .name = "de",
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_de_match,
	},
};
