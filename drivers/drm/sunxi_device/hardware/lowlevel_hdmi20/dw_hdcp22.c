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

#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>

#include "esm_lib/include/ESMHost.h"
#include "dw_fc.h"
#include "dw_mc.h"
#include "dw_hdcp.h"
#include "dw_hdcp22.h"

#define ESM_REG_BASE_OFFSET    (0x8000)
#define DW_ESM_FW_SIZE         (256 * 1024)
#define DW_ESM_DATA_SIZE       (128 * 1024)
#define DW_ESM_PAIRDATA_SIZE   (300)

static struct dw_hdcp_s  *hdcp;
static esm_instance_t  *esm;

/**
 * @desc: chose which way to enable hdcp22
 * @enable: 0 - chose to enable hdcp22 by dwc_hdmi inner signal ist_hdcp_capable
 *          1 - chose to enable hdcp22 by hdcp22_ovr_val bit
 */
static void _dw_hdcp2x_set_avmute(u8 val, u8 enable)
{
	dw_write_mask(HDCP22REG_CTRL1,
		HDCP22REG_CTRL1_HDCP22_AVMUTE_OVR_VAL_MASK, val);
	dw_write_mask(HDCP22REG_CTRL1,
		HDCP22REG_CTRL1_HDCP22_AVMUTE_OVR_EN_MASK, enable);
}

/**
 * @desc chose what place hdcp22 hpd come from and config hdcp22 hpd enable or disable
 * @val: 0 - hdcp22 hpd come from phy: phy_stat0.HPD
 *       1 - hdcp22 hpd come from hpd_ovr_val
 * @enable:hpd_ovr_val
 */
static void _dw_hdcp2x_set_hpd(u8 val)
{
	dw_write_mask(HDCP22REG_CTRL,
		HDCP22REG_CTRL_HPD_OVR_VAL_MASK, val);
	dw_write_mask(HDCP22REG_CTRL,
		HDCP22REG_CTRL_HPD_OVR_EN_MASK, 0x1);
}

void _dw_hdcp2x_data_enable(u8 enable)
{
	dw_mc_disable_hdcp_clock(enable ? 0x0 : 0x1);
	_dw_hdcp2x_set_avmute(enable ? 0x0 : 0x1, 0x1);
}

static int
_esm_hpi_read(void *instance, uint32_t offset, uint32_t *data)
{
	*data = *((volatile u32 *)esm->driver->hpi_base + offset);
	mdelay(1);
	return 0;
}

static int
_esm_hpi_write(void *instance, uint32_t offset, uint32_t data)
{
	unsigned long addr =  esm->driver->hpi_base + offset;
	*((volatile u32 *)addr) = data;
	mdelay(1);
	return 0;
}

static int
_esm_data_read(void *instance, uint32_t offset, uint8_t *dest_buf, uint32_t nbytes)
{
	memcpy(dest_buf, (u8 *)(esm->driver->vir_data_base + offset), nbytes);
	return 0;
}

static int
_esm_data_write(void *instance, uint32_t offset, uint8_t *src_buf, uint32_t nbytes)
{
	memcpy((u8 *)(esm->driver->vir_data_base + offset), src_buf, nbytes);
	return 0;
}

static int
_esm_data_set(void *instance, uint32_t offset, uint8_t data, uint32_t nbytes)
{
	memset((u8 *)(esm->driver->vir_data_base + offset), data, nbytes);
	return 0;
}

static int _esm_address_dump(void)
{
	int n = 0;
	char buf[255];

	n += sprintf(buf + n, "[esm address info]\n");
	n += sprintf(buf + n, " - [code addr] phy = 0x%x, vir = 0x%lx, size = 0x%x",
		esm->driver->code_base, esm->driver->vir_code_base, esm->driver->code_size);
	n += sprintf(buf + n, " - [data addr] phy = 0x%x, vir = 0x%lx, size = 0x%x",
		esm->driver->data_base, esm->driver->vir_data_base, esm->driver->data_size);

	hdcp_log("%s", buf);
	return n;
}

static int _dw_hdcp2x_start_auth(void)
{
	int ret = 0;

	mutex_lock(&hdcp->lock_esm_auth);
	ret = (ESM_Authenticate(esm, 1, 1, 0) == ESM_HL_SUCCESS) ? 0 : -1;
	mutex_unlock(&hdcp->lock_esm_auth);

	hdmi_inf("dw esm auth: %s\n", ret == 0 ? "success" : "failed");
	return ret;
}

static void _dw_hdcp2x_auth_work(struct work_struct *work)
{
	int wait_time = 0;

	if (hdcp->esm_auth_done)
		return;

	hdcp_log("sleep to wait for hdcp2.2 authentication\n");
	wait_time = wait_event_interruptible_timeout(hdcp->hdcp2x_waitqueue, true,
						       msecs_to_jiffies(3000));
	if (wait_time > 0) {
		hdmi_inf("Force to wake up, waiting time is less than 3s\n");
		return;
	}

	if (_dw_hdcp2x_start_auth()) {
		hdmi_err("hdcp2.2 set authenticate failed\n");
		return;
	}

	hdcp->esm_auth_done = 0x1;
}

static int _dw_hdcp2x_get_encry_state(void)
{
	esm_status_t Status = {0, 0, 0, 0};
	uint32_t state = 0;
	ESM_STATUS ret = ESM_HL_SUCCESS;

	if (ESM_GetState(esm, &state, &Status) == ESM_HL_SUCCESS) {
		if (Status.esm_sync_lost) {
			hdmi_err("[hdcp2.2-error]esm sync lost!\n");
			return -1;
		}

		if (Status.esm_exception) {
			/* Got an exception. can check */
			/* bits for more detail */
			if (Status.esm_exception & 0x80000000)
				hdmi_inf("hardware exception\n");
			else
				hdmi_inf("solfware exception\n");

			hdmi_inf("exception line number:%d\n",
				       (Status.esm_exception >> 10) & 0xfffff);
			hdmi_inf("exception flag:%d\n",
					(Status.esm_exception >> 1) & 0x1ff);
			hdmi_inf("exception Type:%s\n",
					(Status.esm_exception & 0x1) ? "notify" : "abort");
			if (((Status.esm_exception >> 1) & 0x1ff) != 109)
				return -1;

			if (hdcp->esm_pairdata) {
				memset(hdcp->esm_pairdata, 0, DW_ESM_PAIRDATA_SIZE);
				ret = ESM_SavePairing(esm, hdcp->esm_pairdata,
						&esm->esm_pair_size);
				if (ret != ESM_HL_SUCCESS)
					hdmi_err("ESM_SavePairing failed\n");
			}

			return 0;
		}

		if (Status.esm_auth_fail) {
			hdmi_err("esm status check result,failed:%d\n", Status.esm_auth_fail);
			return -1;
		}

		if (Status.esm_auth_pass) {
			memset(hdcp->esm_pairdata, 0, DW_ESM_PAIRDATA_SIZE);
			ret = ESM_SavePairing(esm, hdcp->esm_pairdata, &esm->esm_pair_size);
			if (ret != ESM_HL_SUCCESS)
				hdmi_err("ESM_SavePairing failed\n");
			return 0;
		}

		return -2;
	}
	return -1;
}

static int _dw_hdcp2x_open(void)
{
	ESM_STATUS err;
	esm_config_t esm_config;

	if (hdcp->esm_open) {
		hdmi_inf("esm has been booted\n");
		goto set_capability;
	}

	if (esm->driver == NULL) {
		hdmi_err("esm driver is null!!!\n");
		return -1;
	}

	if (esm->driver->vir_data_base == 0) {
		hdmi_err("esm driver virtual address not set!\n");
		return -1;
	}

	memset((void *)esm->driver->vir_data_base, 0, esm->driver->data_size);
	memset(&esm_config, 0, sizeof(esm_config_t));

	_esm_address_dump();

	err = ESM_Initialize(esm,
			esm->driver->code_base,
			esm->driver->code_size,
			0, esm->driver, &esm_config);
	if (err != ESM_SUCCESS) {
		hdmi_err("esm boots fail!\n");
		return -1;
	}
	hdcp_log("esm boots successfully\n");

	if (ESM_LoadPairing(esm, hdcp->esm_pairdata, esm->esm_pair_size) < 0)
		hdmi_inf("ESM Load Pairing failed\n");
	hdcp->esm_open = 0x1;

set_capability:
	if (hdcp->esm_cap_done)
		return 0;

	ESM_Reset(esm);
	/* Enable logging */
	ESM_LogControl(esm, 1, 0);
	/* ESM_EnableLowValueContent(esm); */
	if (ESM_SetCapability(esm) != ESM_HL_SUCCESS) {
		hdmi_err("esm set capability fail, maybe remote Rx is not 2.2 capable!\n");
		return -1;
	}
	hdcp->esm_cap_done = 0x1;
	msleep(50);
	queue_work(hdcp->hdcp2x_workqueue, &hdcp->hdcp2x_work);
	return 0;
}

int dw_hdcp2x_get_encrypt_state(void)
{
	int state = -1;

	if (!hdcp->esm_auth_done)
		return DW_HDCP_DISABLE;

	mutex_lock(&hdcp->lock_esm_handler);

	state = _dw_hdcp2x_get_encry_state();

	/* esm state success */
	if (state == 0) {
		_dw_hdcp2x_data_enable(1);
		state = DW_HDCP_SUCCESS;
		goto esm_exit;
	}

	/* esm state failed */
	if (state == -1) {
		_dw_hdcp2x_data_enable(0);
		msleep(25);
		/* try to re-auth */
		if (!_dw_hdcp2x_start_auth()) {
			state = DW_HDCP_ING;
			goto esm_exit;
		}
		/* re-auth failed and set failed */
		state = DW_HDCP_FAILED;
		goto esm_exit;
	}

	/* esm state idle or other, set disable */
	state = DW_HDCP_DISABLE;

esm_exit:
	mutex_unlock(&hdcp->lock_esm_handler);
	return state;
}

/* configure hdcp2.2 and enable hdcp2.2 encrypt */
int dw_hdcp2x_enable(void)
{
	/* 1 - set main controller hdcp clock disable */
	_dw_hdcp2x_data_enable(0);

	/* 2 - set hdcp keepout */
	dw_fc_video_set_hdcp_keepout(true);

	/* 3 - Select DVI or HDMI mode */
	dw_hdcp_sync_tmds_mode();

	/* 4 - Set the Data enable, Hsync, and VSync polarity */
	dw_hdcp_sync_data_polarity();

	dw_write_mask(0x4003, 1 << 5, 0x1);

	dw_hdcp_set_data_path(0x1);

	_dw_hdcp2x_set_hpd(0x1);

	/* disable avmute overwrite */
	_dw_hdcp2x_set_avmute(0x0, 0x0);

	dw_write_mask(0x4003, 1 << 4, 0x1);

	/* mask the interrupt of hdcp22 event */
	dw_write_mask(HDCP22REG_MASK, 0xff, 0);

	if (_dw_hdcp2x_open() < 0)
		return -1;

	dw_hdcp_set_enable_type(DW_HDCP_TYPE_HDCP22);

	return 0;
}

int dw_hdcp2x_disable(void)
{
	dw_mc_disable_hdcp_clock(1);

	dw_write_mask(0x4003, 1 << 5, 0x0);

	dw_hdcp_set_data_path(0x0);

	_dw_hdcp2x_set_hpd(0x0);

	dw_write_mask(0x4003, 1 << 4, 0x0);

	if (hdcp->esm_auth_done) {
		mutex_lock(&hdcp->lock_esm_auth);
		ESM_Authenticate(esm, 0, 0, 0);
		mutex_unlock(&hdcp->lock_esm_auth);
		msleep(20);
	}

	hdcp->esm_auth_done = 0x0;
	hdcp->esm_cap_done  = 0x0;
	wake_up(&hdcp->hdcp2x_waitqueue);

	dw_hdcp_set_enable_type(DW_HDCP_TYPE_NULL);
	hdcp_log("hdcp22 disconfig done!\n");
	return 0;
}

int dw_hdcp2x_firmware_state(void)
{
	return hdcp->esm_loading;
}

int dw_hdcp2x_firmware_update(const u8 *data, size_t size)
{
	int i = 0;
	char *esm_addr = (char *)esm->driver->vir_code_base;

	if (!data) {
		hdmi_err("hdcp2x fw buffer is null\n");
		return -1;
	}

	if (!esm_addr) {
		hdmi_err("dw hdcp2x esm address unvalid\n");
		return -1;
	}

	for (i = 0; i < 0xF; i++)
		hdcp_log("data[%d]: 0x%2x\n", i, *(data + i));
	for (i = size - 0xF; i < size; i++)
		hdcp_log("data[%d]: 0x%2x\n", i, *(data + i));

	memcpy(esm_addr, data, size);
	hdcp->esm_size = (u32)size;
	hdcp->esm_loading = 0x1;

	hdmi_inf("dw hdcp2x loading firmware size %d finish.\n", (u32)size);
	return 0;
}

int dw_hdcp2x_init(void)
{
	struct dw_hdmi_dev_s  *hdmi = dw_get_hdmi();

	hdcp = &hdmi->hdcp_dev;

	hdcp->esm_loading = 0x0;
	hdcp->esm_open = 0x0;
	hdcp->esm_auth_done = 0x0;
	hdcp->esm_cap_done  = 0x0;

	if (hdcp->esm_pairdata != NULL) {
		kfree(hdcp->esm_pairdata);
		hdcp->esm_pairdata = NULL;
	}
	hdcp->esm_pairdata = kzalloc(DW_ESM_PAIRDATA_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!hdcp->esm_pairdata) {
		hdmi_err("esm alloc pair data buffer failed\n");
		return -1;
	}

	if (esm != NULL) {
		kfree(esm);
		esm = NULL;
	}
	esm = kzalloc(sizeof(esm_instance_t), GFP_KERNEL | __GFP_ZERO);
	if (!esm) {
		hdmi_err("esm instance alloc failed!!!\n");
		return -1;
	}

	if (esm->driver != NULL) {
		kfree(esm->driver);
		esm->driver = NULL;
	}
	esm->driver = kzalloc(sizeof(esm_host_driver_t), GFP_KERNEL | __GFP_ZERO);
	if (!esm->driver) {
		hdmi_err("esm host driver alloc failed\n ");
		return -1;
	}

	mutex_init(&hdcp->lock_esm_auth);
	mutex_init(&hdcp->lock_esm_handler);

	init_waitqueue_head(&hdcp->hdcp2x_waitqueue);

	hdcp->hdcp2x_workqueue = create_workqueue("hdcp2x-workqueue");
	INIT_WORK(&hdcp->hdcp2x_work, _dw_hdcp2x_auth_work);

	/* esm firmware dma address alloc */
	hdcp->esm_fw_size = DW_ESM_FW_SIZE;
	hdcp->esm_fw_addr_vir = (unsigned long)dma_alloc_coherent(hdmi->dev,
		hdcp->esm_fw_size, &hdcp->esm_fw_addr, GFP_KERNEL | __GFP_ZERO);
	hdcp->esm_fw_addr -= 0x40000000;

	/* esm data dma address alloc */
	hdcp->esm_data_size = DW_ESM_DATA_SIZE;
	hdcp->esm_data_addr_vir = (unsigned long)dma_alloc_coherent(hdmi->dev,
		hdcp->esm_data_size, &hdcp->esm_data_addr, GFP_KERNEL | __GFP_ZERO);
	hdcp->esm_data_addr -= 0x40000000;

	hdcp->esm_hpi_addr = (unsigned long)(hdmi->addr + ESM_REG_BASE_OFFSET);

	esm->driver->code_base     = (u32)hdcp->esm_fw_addr;
	esm->driver->code_size     = hdcp->esm_fw_size;
	esm->driver->vir_code_base = hdcp->esm_fw_addr_vir;

	esm->driver->data_base     = (u32)hdcp->esm_data_addr;
	esm->driver->data_size     = hdcp->esm_data_size;
	esm->driver->vir_data_base = hdcp->esm_data_addr_vir;

	esm->driver->hpi_base   = hdcp->esm_hpi_addr;
	esm->driver->hpi_read   = _esm_hpi_read;
	esm->driver->hpi_write  = _esm_hpi_write;
	esm->driver->data_read  = _esm_data_read;
	esm->driver->data_write = _esm_data_write;
	esm->driver->data_set   = _esm_data_set;

	esm->driver->instance = NULL;
	esm->driver->idle     = NULL;

	_esm_address_dump();

	return 0;
}

void dw_hdcp2x_exit(void)
{
	if (hdcp->hdcp2x_workqueue != NULL) {
		kfree(hdcp->hdcp2x_workqueue);
		hdcp->hdcp2x_workqueue = NULL;
	}

	if (esm->driver != NULL) {
		kfree(esm->driver);
		esm->driver = NULL;
	}

	if (esm != NULL) {
		kfree(esm);
		esm = NULL;
	}

	if (hdcp != NULL) {
		kfree(hdcp);
		hdcp = NULL;
	}
}

ssize_t dw_hdcp2x_dump(char *buf)
{
	int i = 0;
	ssize_t n = 0;
	char *esm_addr = (char *)esm->driver->vir_code_base;

	n += sprintf(buf + n, "     [esm firmware]\n");
	n += sprintf(buf + n, "       - state: [%s], size: [%d]\n",
		hdcp->esm_loading ? "loaded" : "not-loaded", hdcp->esm_size);
	n += sprintf(buf + n, "       - last 8byte:");
	for (i = 0; i < hdcp->esm_size - 8; i++)
		n += sprintf(buf + n, " 0x%2x", (u32)*(esm_addr + i));
	n += sprintf(buf + n, "\n");
	n += sprintf(buf + n, "       - code addr: 0x%x, size: 0x%x\n",
		esm->driver->code_base, esm->driver->code_size);
	n += sprintf(buf + n, "       - data addr: 0x%x, size: 0x%x\n",
		esm->driver->data_base, esm->driver->data_size);

	n += sprintf(buf + n, "     [esm hardware]\n");
	n += sprintf(buf + n, "     - state: [%s], auth: [%s], capability: [%s]\n",
		hdcp->esm_open ? "on" : "off",
		hdcp->esm_auth_done ? "enable" : "disable",
		hdcp->esm_cap_done ? "set" : "unset");

	return n;
}

