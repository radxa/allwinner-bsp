// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (C) 2022 Allwinner Co., Ltd.
 *
 * The pcie_dma_chnl_request() is used to apply for pcie DMA channels;
 * The pcie_dma_mem_xxx() is to initiate DMA read and write operations;
 *
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/reset.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include "pcie-sunxi-dma.h"

static u32 SUNXI_DMA_DEFAULT_CNT = 0x04;  /* Default value for DMA wr/rd channel */

static u32 SUNXI_DMA_WR_CHN_CNT;
static u32 SUNXI_DMA_RD_CHN_CNT;

static dma_channel_t *dma_wr_chn;
static dma_channel_t *dma_rd_chn;

static struct dma_trx_obj *obj_global;

dma_hdl_t sunxi_pcie_dma_chan_request(enum dma_dir dma_trx)
{
	int i = 0;
	dma_channel_t *pchan = NULL;

	if (dma_trx == PCIE_DMA_WRITE) {
		for (i = 0; i < SUNXI_DMA_WR_CHN_CNT; i++) {
			pchan = &dma_wr_chn[i];

			if (!pchan->dma_used) {
				pchan->dma_used = 1;
				pchan->chnl_num = i;
				spin_lock_init(&pchan->lock);
				return (dma_hdl_t)pchan;
			}
		}
	} else if (dma_trx == PCIE_DMA_READ) {
		for (i = 0; i < SUNXI_DMA_RD_CHN_CNT; i++) {
			pchan = &dma_rd_chn[i];

			if (pchan->dma_used == 0) {
				pchan->dma_used = 1;
				pchan->chnl_num = i;
				spin_lock_init(&pchan->lock);
				return (dma_hdl_t)pchan;
			}
		}
	} else {
		pr_err("ERR: unsupported type:%d \n", dma_trx);
	}

	return (dma_hdl_t)NULL;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_chan_request);

int sunxi_pcie_dma_chan_release(u32 channel, enum dma_dir dma_trx)
{
	if ((channel > SUNXI_DMA_WR_CHN_CNT) || (channel > SUNXI_DMA_RD_CHN_CNT)) {
		pr_err("ERR: the channel num:%d is error\n", channel);
		return -1;
	}

	if (PCIE_DMA_WRITE == dma_trx) {
		dma_wr_chn[channel].dma_used = 0;
		dma_wr_chn[channel].chnl_num = 0;
	} else if (PCIE_DMA_READ == dma_trx) {
		dma_rd_chn[channel].dma_used = 0;
		dma_rd_chn[channel].chnl_num = 0;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_chan_release);

int sunxi_pcie_dma_get_chan(struct platform_device *pdev)
{
	int ret, num;

	ret = of_property_read_u32(pdev->dev.of_node, "num-edma", &num);
	if (ret) {
		dev_info(&pdev->dev, "PCIe get num-edma failed, use default num=%d\n",
						SUNXI_DMA_DEFAULT_CNT);
		num = SUNXI_DMA_DEFAULT_CNT;
	}
	SUNXI_DMA_WR_CHN_CNT = SUNXI_DMA_RD_CHN_CNT = num;  /* set the eDMA wr/rd channel num */

	dma_wr_chn = devm_kcalloc(&pdev->dev, SUNXI_DMA_WR_CHN_CNT, sizeof(*dma_wr_chn), GFP_KERNEL);
	dma_rd_chn = devm_kcalloc(&pdev->dev, SUNXI_DMA_RD_CHN_CNT, sizeof(*dma_rd_chn), GFP_KERNEL);
	if (!dma_wr_chn || !dma_rd_chn) {
		dev_err(&pdev->dev, "PCIe edma kzalloc failed\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_get_chan);

int sunxi_pcie_dma_mem_read(phys_addr_t sar_addr, phys_addr_t dar_addr, unsigned int size)
{
	struct dma_table read_table = {0};
	int ret;

	if (likely(obj_global->config_dma_trx_func)) {
		ret = obj_global->config_dma_trx_func(&read_table, sar_addr, dar_addr, size, PCIE_DMA_READ);

		if (ret < 0) {
			pr_err("pcie dma mem read error ! \n");
			return -EINVAL;
		}
	} else {
		pr_err("config_dma_trx_func is NULL ! \n");
		return -EINVAL;
	}

	obj_global->start_dma_trx_func(&read_table, obj_global);

	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_mem_read);

int sunxi_pcie_dma_mem_write(phys_addr_t sar_addr, phys_addr_t dar_addr, unsigned int size)
{
	struct dma_table write_table = {0};
	int ret;

	if (likely(obj_global->config_dma_trx_func)) {
		ret = obj_global->config_dma_trx_func(&write_table, sar_addr, dar_addr, size, PCIE_DMA_WRITE);

		if (ret < 0) {
			pr_err("pcie dma mem write error ! \n");
			return -EINVAL;
		}
	} else {
		pr_err("config_dma_trx_func is NULL ! \n");
		return -EINVAL;
	}

	obj_global->start_dma_trx_func(&write_table, obj_global);

	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_mem_write);

struct dma_trx_obj *sunxi_pcie_dma_obj_probe(struct device *dev)
{
	struct dma_trx_obj *obj;

	obj = devm_kzalloc(dev, sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj_global = obj;
	obj->dev = dev;

	INIT_LIST_HEAD(&obj->dma_list);
	spin_lock_init(&obj->dma_list_lock);

	mutex_init(&obj->count_mutex);

	return obj;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_obj_probe);

int sunxi_pcie_dma_obj_remove(struct device *dev)
{
	memset(dma_wr_chn, 0, sizeof(dma_channel_t) * SUNXI_DMA_WR_CHN_CNT);
	memset(dma_rd_chn, 0, sizeof(dma_channel_t) * SUNXI_DMA_RD_CHN_CNT);

	obj_global->dma_list.next = NULL;
	obj_global->dma_list.prev = NULL;
	mutex_destroy(&obj_global->count_mutex);

	obj_global = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_pcie_dma_obj_remove);
