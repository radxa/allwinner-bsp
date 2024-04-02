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

#include "esm_lib/include/ESMHost.h"
#include "dw_hdcp.h"
#include "dw_hdcp22.h"

#define PAIRDATA_SIZE    300
#define ESM_REG_BASE_OFFSET     (0x8000)
#define HDCP22_SIZE_KB(x)       ((x) * 1024)
#define HDCP22_FIRMWARE_SIZE    HDCP22_SIZE_KB(256)
#define HDCP22_DATA_SIZE        HDCP22_SIZE_KB(128)

/* static u8 set_cap; */
static struct mutex authen_mutex;
static struct mutex esm_ctrl;
static u8 esm_on;
static u8 esm_enable;
static u8 esm_set_cap;
static u8 authenticate_state;
static u8 pairdata[PAIRDATA_SIZE];
static wait_queue_head_t               esm_wait;
static struct workqueue_struct 		   *hdcp22_workqueue;
static struct work_struct			   hdcp22_work;
static esm_instance_t  *esm;

#define sunxi_point_free(x) \
	do { \
		if (x) { \
			kfree(x); \
			x = NULL; \
		} \
	} while (0);

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

static int _dw_esm_info_print(void)
{
	int i = 0;
	int n = 0;
	char buf[255];

	n += sprintf(buf + n, "[esm address info]\n");
	n += sprintf(buf + n, " - [code addr] phy = 0x%x, vir = 0x%lx, size = 0x%x",
		esm->driver->code_base, esm->driver->vir_code_base, esm->driver->code_size);
	n += sprintf(buf + n, " - [data addr] phy = 0x%x, vir = 0x%lx, size = 0x%x",
		esm->driver->data_base, esm->driver->vir_data_base, esm->driver->data_size);
	for (i = 0; i < 10; i++) {
		n += sprintf(buf + n, " - [firmware_%d]: 0x%x\n",
			i, *((u8 *)(esm->driver->vir_code_base + i)));
	}

	hdcp_log("%s", buf);
	return n;
}

static int _dw_esm_start_auth(void)
{
	ESM_STATUS err;

	mutex_lock(&authen_mutex);
	err = ESM_Authenticate(esm, 1, 1, 0);
	if (err != 0) {
		authenticate_state = 0;
		hdmi_inf("esm authenticate failed\n");
		mutex_unlock(&authen_mutex);
		return -1;
	} else {
		authenticate_state = 1;
		mutex_unlock(&authen_mutex);
		return 0;
	}
}

static void _dw_esm_auth_workqueue(struct work_struct *work)
{
	int wait_time = 0;

	log_trace();

	if (esm_enable)
		return;
	esm_enable = 1;

	hdcp_log("sleep to wait for hdcp2.2 authentication\n");
	wait_time = wait_event_interruptible_timeout(esm_wait, !esm_enable,
						       msecs_to_jiffies(3000));
	if (wait_time > 0) {
		hdmi_inf("Force to wake up, waiting time is less than 3s\n");
		return;
	}

	if (_dw_esm_start_auth()) {
		hdmi_err("hdcp2.2 set authenticate failed\n");
		return;
	}

	esm_enable = 1;
}

static int _dw_esm_encrypt_status_check(void)
{
	esm_status_t Status = {0, 0, 0, 0};
	uint32_t state = 0;

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

			memset(pairdata, 0, PAIRDATA_SIZE);
			if (ESM_SavePairing(esm, pairdata, &esm->esm_pair_size) != 0)
				hdmi_err("ESM_SavePairing failed\n");

			return 0;
		}

		if (Status.esm_auth_fail) {
			hdmi_err("esm status check result,failed:%d\n", Status.esm_auth_fail);
			return -1;
		}

		if (Status.esm_auth_pass) {
			memset(pairdata, 0, PAIRDATA_SIZE);
			if (ESM_SavePairing(esm, pairdata, &esm->esm_pair_size) != 0)
				hdmi_err("ESM_SavePairing failed\n");
			return 0;
		}

		return -2;
	}
	return -1;
}

static int _dw_esm_init_value(void)
{
	esm_on = 0;
	esm_enable = 0;
	esm_set_cap = 0;
	authenticate_state = 0;
	memset(pairdata, 0, PAIRDATA_SIZE);

	if (esm) {
		kfree(esm);
		esm = NULL;
	}
	esm = kmalloc(sizeof(esm_instance_t), GFP_KERNEL | __GFP_ZERO);
	if (!esm) {
		hdmi_err("esm instance alloc failed!!!\n");
		return -1;
	}

	if (esm->driver) {
		kfree(esm->driver);
		esm->driver = NULL;
	}
	esm->driver = kmalloc(sizeof(esm_host_driver_t), GFP_KERNEL | __GFP_ZERO);
	if (esm->driver == NULL) {
		hdmi_err("esm host driver alloc failed\n ");
		return -1;
	}

	mutex_init(&authen_mutex);
	mutex_init(&esm_ctrl);

	init_waitqueue_head(&esm_wait);
	hdcp22_workqueue = create_workqueue("hdcp22_workqueue");
	INIT_WORK(&hdcp22_work, _dw_esm_auth_workqueue);
	return 0;
}

/* Check esm encrypt status and handle the status
 * return value: 1-indicate that esm is being in authenticate;
 *               0-indicate that esm authenticate is sucessful
 *              -1-indicate that esm authenticate is failed
 *              -2-indicate that esm authenticate is in idle state
 * */
int dw_esm_status_check_and_handle(void)
{
	int encrypt_status = -1;

	if (!esm_enable)
		return 0;

	mutex_lock(&esm_ctrl);
	encrypt_status = _dw_esm_encrypt_status_check();
	if (encrypt_status == -1) {
		dw_hdcp22_data_enable(0);
		msleep(25);
		if (!_dw_esm_start_auth()) {
			mutex_unlock(&esm_ctrl);
			return 1;
		} else {
			mutex_unlock(&esm_ctrl);
			return -1;
		}
	} else if (encrypt_status == -2) {
		mutex_unlock(&esm_ctrl);
		return -2;
	} else if (encrypt_status == 0) {
		dw_hdcp22_data_enable(1);
		mutex_unlock(&esm_ctrl);
		return 0;
	}
	mutex_unlock(&esm_ctrl);
	return -2;
}


int dw_esm_open(void)
{
	ESM_STATUS err;
	esm_config_t esm_config;

	if (esm_on != 0) {
		hdmi_inf("esm has been booted\n");
		goto set_capability;
	}
	esm_on = 1;

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

	_dw_esm_info_print();

	err = ESM_Initialize(esm,
			esm->driver->code_base,
			esm->driver->code_size,
			0, esm->driver, &esm_config);
	if (err != ESM_SUCCESS) {
		hdmi_err("esm boots fail!\n");
		return -1;
	} else {
		hdcp_log("esm boots successfully\n");
	}

	if (ESM_LoadPairing(esm, pairdata, esm->esm_pair_size) < 0)
		hdmi_inf("ESM Load Pairing failed\n");

set_capability:
	if (esm_set_cap)
		return 0;
	esm_set_cap = 1;
	ESM_Reset(esm);
	/* Enable logging */
	ESM_LogControl(esm, 1, 0);
	/* ESM_EnableLowValueContent(esm); */
	if (ESM_SetCapability(esm) != ESM_HL_SUCCESS) {
		hdmi_err("esm set capability fail, maybe remote Rx is not 2.2 capable!\n");
		return -1;
	}
	msleep(50);
	queue_work(hdcp22_workqueue, &hdcp22_work);
	return 0;
}

/* for:
	hdmi_plugin<--->hdmi_plugout
	hdmi_suspend<--->hdmi_resume
*/
void dw_esm_close(void)
{
	esm_enable = 0;
	esm_on = 0;
}

void dw_esm_disable(void)
{
	if (esm_enable) {
		ESM_Authenticate(esm, 0, 0, 0);
		msleep(20);
	}
	esm_enable = 0;
	esm_set_cap = 0;
	wake_up(&esm_wait);
}

void dw_esm_exit(void)
{
	sunxi_point_free(hdcp22_workqueue);

	sunxi_point_free(esm->driver);

	sunxi_point_free(esm);
}

int dw_esm_init(void)
{
	struct dw_hdmi_dev_s   *hdmi = dw_get_hdmi();
	struct dw_hdcp_s *hdcp = &hdmi->hdcp_dev;
	struct device    *dev   = hdmi->dev;
	int ret = 0;

	hdcp->esm_firm_size = HDCP22_FIRMWARE_SIZE;
	hdcp->esm_firm_vir_addr = (unsigned long)dma_alloc_coherent(dev,
		hdcp->esm_firm_size, &hdcp->esm_firm_phy_addr, GFP_KERNEL | __GFP_ZERO);

	hdcp->esm_data_size = HDCP22_DATA_SIZE;
	hdcp->esm_data_vir_addr = (unsigned long)dma_alloc_coherent(dev,
		hdcp->esm_data_size, &hdcp->esm_data_phy_addr, GFP_KERNEL | __GFP_ZERO);

	hdcp->esm_hpi_base = (unsigned long)(hdmi->addr + ESM_REG_BASE_OFFSET);
	hdcp->esm_firm_phy_addr -= 0x40000000;
	hdcp->esm_data_phy_addr -= 0x40000000;

	ret = _dw_esm_init_value();
	if (ret != 0) {
		hdmi_err("dw esm init value failed\n");
		return -1;
	}

	esm->driver->hpi_base      = hdcp->esm_hpi_base;
	esm->driver->code_base     = (u32)hdcp->esm_firm_phy_addr;
	esm->driver->code_size     = hdcp->esm_firm_size;
	esm->driver->data_base     = (u32)hdcp->esm_data_phy_addr;
	esm->driver->data_size     = hdcp->esm_data_size;
	esm->driver->vir_code_base = hdcp->esm_firm_vir_addr;
	esm->driver->vir_data_base = hdcp->esm_data_vir_addr;

	esm->driver->hpi_read   = _esm_hpi_read;
	esm->driver->hpi_write  = _esm_hpi_write;
	esm->driver->data_read  = _esm_data_read;
	esm->driver->data_write = _esm_data_write;
	esm->driver->data_set   = _esm_data_set;

	esm->driver->instance = NULL;
	esm->driver->idle     = NULL;

	_dw_esm_info_print();

	return 0;
}

ssize_t dw_esm_dump(char *buf)
{
	ssize_t n = 0;

	n += sprintf(buf + n, "     - esm %s and %s, capability: %s\n",
		esm_on ? "on" : "off",
		esm_enable ? "enable" : "disable",
		esm_set_cap ? "set" : "unset");
	n += sprintf(buf + n, "     - code addr: 0x%x, size: 0x%x\n",
		esm->driver->code_base, esm->driver->code_size);
	n += sprintf(buf + n, "     - data addr: 0x%x, size: 0x%x\n",
		esm->driver->data_base, esm->driver->data_size);

	return n;
}

