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
#include "dw_mc.h"
#include "dw_i2cm.h"

/* SDL: 50K */
#define DW_I2CM_SFR_CLK				2400
#define DW_I2CM_SS_SCL_HIGH_TIME	9184  /* 5333 4000 4737 5625 */
#define DW_I2CM_SS_SCL_LOW_TIME		10204 /* 4700 5263 6250 */

#define DW_DDC_CI_ADDR				0x37
#define DW_DDC_ADDR					0x50
#define DW_DDC_SEGMENT_ADDR			0x30

#define DW_I2CM_TIMEOUT			500
#define DW_I2CM_DIV_FACTOR		100000

struct dw_i2cm_s *i2c_dev;

/**
 * calculate the fast sped high time counter - round up
 */
static u16 _dw_i2cm_scl_calc(u16 sfrClock, u16 sclMinTime)
{
	unsigned long tmp_scl_period = 0;

	if (((sfrClock * sclMinTime) % DW_I2CM_DIV_FACTOR) != 0) {
		tmp_scl_period = (unsigned long)((sfrClock * sclMinTime)
			+ (DW_I2CM_DIV_FACTOR
			- ((sfrClock * sclMinTime) % DW_I2CM_DIV_FACTOR)))
			/ DW_I2CM_DIV_FACTOR;
	} else {
		tmp_scl_period = (unsigned long)(sfrClock * sclMinTime)
							/ DW_I2CM_DIV_FACTOR;
	}
	return (u16)(tmp_scl_period);
}

static void _dw_i2cm_set_div(void)
{
	u16 temp_low = 0x0, temp_hight = 0x0;

	temp_low   = _dw_i2cm_scl_calc(i2c_dev->sfrClock, i2c_dev->ss_low_ckl);
	temp_hight = _dw_i2cm_scl_calc(i2c_dev->sfrClock, i2c_dev->ss_high_ckl);

	dw_write(I2CM_SS_SCL_LCNT_1_ADDR, (u8)(temp_low >> 8));
	dw_write(I2CM_SS_SCL_LCNT_0_ADDR, (u8)(temp_low >> 0));

	dw_write(I2CM_SS_SCL_HCNT_1_ADDR, (u8)(temp_hight >> 8));
	dw_write(I2CM_SS_SCL_HCNT_0_ADDR, (u8)(temp_hight >> 0));
}

int _dw_i2cm_read(unsigned char *buf, unsigned int length)
{
	u8 state = 0x0;
	int timeout = DW_I2CM_TIMEOUT;
	u8 retry_cnt = 5;

	log_trace1(length);

	if (!buf) {
		hdmi_err("check buf is null\n");
		return -1;
	}

	if (!i2c_dev->is_regaddr) {
		hdmi_inf("dw i2c set read register address to 0\n");
		i2c_dev->slave_reg = 0x0;
		i2c_dev->is_regaddr = true;
	}

	while (length) {
		length--;
		dw_write_mask(I2CM_ADDRESS, I2CM_ADDRESS_ADDRESS_MASK, i2c_dev->slave_reg++);
		if (i2c_dev->is_segment)
			dw_write(I2CM_OPERATION, I2CM_OPERATION_RD_EXT_MASK);
		else
			dw_write(I2CM_OPERATION, I2CM_OPERATION_RD_MASK);

		do {
			udelay(20);
			state = dw_read(IH_I2CM_STAT0);
		} while ((state == 0) && (timeout--));

		if ((state == 0) && (timeout == 0)) {
			/* TODO, i2c may has issue. try to sw reset */
			hdmi_inf("i2c read wait state timeout\n");
			return -2;
		}

		dw_write(IH_I2CM_STAT0, state);

		if (state & IH_I2CM_STAT0_I2CMASTERERROR_MASK) {
			hdmi_err("dw i2c read error, retry count: %d\n", retry_cnt);
			if (retry_cnt) {
				length++;
				i2c_dev->slave_reg--;
				retry_cnt--;
				continue;
			}
			return -1;
		}

		retry_cnt = 5;
		if (state & IH_I2CM_STAT0_I2CMASTERDONE_MASK) {
			*buf++ = (u8) dw_read(I2CM_DATAI);
		} else {
			if (retry_cnt) {
				length++;
				i2c_dev->slave_reg--;
				retry_cnt--;
				continue;
			}
			hdmi_err("i2c read 0x%x timeout\n", i2c_dev->slave_reg);
			return -1;
		}
	}
	i2c_dev->is_segment = false;

	return 0;
}

int _dw_i2cm_write(unsigned char *buf, unsigned int length)
{
	u8 state = 0x0;
	int timeout = DW_I2CM_TIMEOUT;
	u8 retry_cnt = 5;

	log_trace1(length);

	if (!i2c_dev->is_regaddr) {
		i2c_dev->slave_reg = buf[0];
		length--;
		buf++;
		i2c_dev->is_regaddr = true;
	}

	while (length--) {
		dw_write(I2CM_DATAO, *buf++);
		dw_write(I2CM_ADDRESS, i2c_dev->slave_reg++);
		dw_write(I2CM_OPERATION, I2CM_OPERATION_WR_MASK);

		do {
			udelay(20);
			state = dw_read(IH_I2CM_STAT0);
		} while ((state == 0) && (timeout--));

		if ((state == 0) && (timeout == 0)) {
			/* TODO, i2c may has issue. try to sw reset */
			return -2;
		}

		dw_write(IH_I2CM_STAT0, state);
		if (state & IH_I2CM_STAT0_I2CMASTERERROR_MASK) {
			hdmi_err("dw i2c write error, retry count: %d\n", retry_cnt);
			if (retry_cnt) {
				length++;
				i2c_dev->slave_reg--;
				retry_cnt--;
				continue;
			}
			return -1;
		}
	}

	return 0;
}

int dw_i2cm_xfer(struct i2c_msg *msgs, int num)
{
	u8 addr = msgs[0].addr;
	int i = 0, ret = 0;

	if (addr == DW_DDC_CI_ADDR)
		return -EOPNOTSUPP;

	for (i = 0; i < num; i++) {
		if (msgs[i].len == 0) {
			hdmi_err("hdmi unsupported transfer %d/%d, no data\n",
				i + 1, num);
			return -EOPNOTSUPP;
		}
	}

	if ((addr == DW_DDC_SEGMENT_ADDR) && (msgs[0].len == 1))
		addr = DW_DDC_ADDR;

	dw_write_mask(I2CM_SLAVE, I2CM_SLAVE_SLAVEADDR_MASK, addr);

	i2c_dev->is_regaddr = false;
	i2c_dev->is_segment = false;

	for (i = 0; i < num; i++) {
		if (msgs[i].addr == DW_DDC_SEGMENT_ADDR && msgs[i].len == 1) {
			i2c_dev->is_segment = true;
			dw_write_mask(I2CM_SEGADDR, I2CM_SEGADDR_SEG_ADDR_MASK, DW_DDC_SEGMENT_ADDR);
			dw_write(I2CM_SEGPTR, *msgs[i].buf);
		} else {
			if (msgs[i].flags & I2C_M_RD)
				ret = _dw_i2cm_read(msgs[i].buf, msgs[i].len);
			else
				ret = _dw_i2cm_write(msgs[i].buf, msgs[i].len);
		}
		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	return ret;
}

int dw_i2cm_adap_bind(struct i2c_adapter *adap)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();
	struct dw_i2cm_s  *dw_i2cm = &hdmi->i2cm_dev;

	dw_i2cm->adapter = adap;
	return 0;
}

int dw_i2cm_re_init(void)
{
	dw_write_mask(I2CM_SOFTRSTZ, I2CM_SOFTRSTZ_I2C_SOFTRSTZ_MASK, 0x0);

	dw_write_mask(I2CM_DIV, I2CM_DIV_FAST_STD_MODE_MASK, i2c_dev->mode);

	_dw_i2cm_set_div();

	return 0;
}

int dw_i2cm_init(void)
{
	struct dw_hdmi_dev_s  *hdmi = dw_get_hdmi();
	struct dw_i2cm_s    *dw_i2cm = &hdmi->i2cm_dev;

	dw_i2cm->mode        = DW_I2CM_MODE_STANDARD;
	dw_i2cm->sfrClock    = DW_I2CM_SFR_CLK;
	dw_i2cm->ss_low_ckl  = DW_I2CM_SS_SCL_LOW_TIME;
	dw_i2cm->ss_high_ckl = DW_I2CM_SS_SCL_HIGH_TIME;

	i2c_dev = dw_i2cm;

	/* software reset */
	dw_i2cm_re_init();

	return 0;
}

