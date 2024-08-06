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

#include "dw_dev.h"
#include "dw_mc.h"
#include "dw_phy.h"

#define PHY_TIMEOUT             200
#define PHY_I2C_SLAVE_ADDR      0x69

#define JTAG_TAP_ADDR_CMD   0
#define JTAG_TAP_WRITE_CMD  1
#define JTAG_TAP_READ_CMD   3

dw_phy_access_t gPHY_interface = DW_PHY_ACCESS_NULL;

#ifdef SUPPORT_PHY_JTAG

static void _phy_jtag_send_data_pulse(u8 tms, u8 tdi)
{
	dw_write(JTAG_PHY_TAP_TCK, (u8) 0);
	udelay(100);
	dw_write_mask(JTAG_PHY_TAP_IN, JTAG_PHY_TAP_IN_JTAG_TMS_MASK, tms);
	dw_write_mask(JTAG_PHY_TAP_IN, JTAG_PHY_TAP_IN_JTAG_TDI_MASK, tdi);
	udelay(100);
	dw_write(JTAG_PHY_TAP_TCK, (u8) 1);
	udelay(100);
}

static void _phy_jtag_tap_soft_reset(void)
{
	int i;

	for (i = 0; i < 5; i++)
		_phy_jtag_send_data_pulse(1, 0);

	_phy_jtag_send_data_pulse(0, 0);
}

static void _phy_jtag_tap_goto_shift_dr(void)
{
	/* RTI -> Select-DR */
	_phy_jtag_send_data_pulse(1, 0);
	/* Select-DR-Scan -> Capture-DR -> Shift-DR */
	_phy_jtag_send_data_pulse(0, 0);
	_phy_jtag_send_data_pulse(0, 0);
}

static void _phy_jtag_tap_goto_shift_ir(void)
{
	/* RTI->Sel IR_Scan */
	_phy_jtag_send_data_pulse(1, 0);
	_phy_jtag_send_data_pulse(1, 0);
	/* Select-IR-Scan -> Shift_IR */
	_phy_jtag_send_data_pulse(0, 0);
	_phy_jtag_send_data_pulse(0, 0);
}

static void _phy_jtag_tap_goto_run_test_idle(void)
{
	/* Exit1_DR -> Update_DR */
	_phy_jtag_send_data_pulse(1, 0);

	/* Update_DR -> Run_Test_Idle */
	_phy_jtag_send_data_pulse(0, 0);
}

static void _phy_jtag_send_value_shift_ir(u8 jtag_addr)
{
	int i;

	for (i = 0; i < 7; i++) {
		_phy_jtag_send_data_pulse(0, jtag_addr & 0x01);
		jtag_addr = jtag_addr >> 1;
	}
	/* Shift_IR -> Exit_IR w/ last MSB bit */
	_phy_jtag_send_data_pulse(1, jtag_addr & 0x01);
}

static u16 _phy_jtag_send_value_shift_dr(u8 cmd, u16 data_in)
{
	int i;
	u32 aux_in = (cmd << 16) | data_in;
	u16 data_out = 0;
	/* Shift_DR */
	for (i = 0; i < 16; i++) {
		_phy_jtag_send_data_pulse(0, aux_in);
		data_out |= (dw_read(JTAG_PHY_TAP_OUT) & 0x01) << i;
		aux_in = aux_in >> 1;
	}
	/* Shift_DR, TAP command bit */
	_phy_jtag_send_data_pulse(0, aux_in);
	aux_in = aux_in >> 1;

	/* Shift_DR -> Exit_DR w/ MSB TAP command bit */
	i++;
	_phy_jtag_send_data_pulse(1, aux_in);
	data_out |= (dw_read(JTAG_PHY_TAP_OUT) & 0x01) << i;

	return data_out;
}

static void _phy_jtag_reset(void)
{
	dw_write(JTAG_PHY_TAP_IN, 0x10);
	udelay(100);
	dw_write(JTAG_PHY_CONFIG, 0);
	udelay(100);
	dw_write(JTAG_PHY_CONFIG, 1); /* enable interface to JTAG */
	_phy_jtag_send_data_pulse(0, 0);
}

static void _phy_jtag_slave_address(u8 jtag_addr)
{
	_phy_jtag_tap_goto_shift_ir();

	/* Shift-IR - write jtag slave address */
	_phy_jtag_send_value_shift_ir(jtag_addr);

	_phy_jtag_tap_goto_run_test_idle();
}

static int _phy_jtag_init(u8 jtag_addr)
{
	dw_write(JTAG_PHY_ADDR, jtag_addr);
	_phy_jtag_reset(void);
	_phy_jtag_tap_soft_reset();
	_phy_jtag_slave_address(jtag_addr);

	return 1;
}

static int _phy_jtag_read(u16 addr, u16 *pvalue)
{
	_phy_jtag_tap_goto_shift_dr();

	/* Shift-DR (shift 16 times) and -> Exit1 -DR */
	_phy_jtag_send_value_shift_dr(JTAG_TAP_ADDR_CMD, addr << 8);

	_phy_jtag_tap_goto_run_test_idle();
	_phy_jtag_tap_goto_shift_dr();

	*pvalue = _phy_jtag_send_value_shift_dr(JTAG_TAP_READ_CMD, 0xFFFF);

	_phy_jtag_tap_goto_run_test_idle();

	return 0;
}

static int _phy_jtag_write(u16 addr,  u16 value)
{
	_phy_jtag_tap_goto_shift_dr();

	/* Shift-DR (shift 16 times) and -> Exit1 -DR */
	_phy_jtag_send_value_shift_dr(JTAG_TAP_ADDR_CMD, addr << 8);

	_phy_jtag_tap_goto_run_test_idle();
	_phy_jtag_tap_goto_shift_dr();

	_phy_jtag_send_value_shift_dr(JTAG_TAP_WRITE_CMD, value);
	_phy_jtag_tap_goto_run_test_idle();

	return 0;
}
#endif

static void _dw_phy_power_down(u8 bit)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_SPARES_2_MASK, (bit ? 1 : 0));
}

static void _dw_phy_enable_tmds(u8 bit)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_SPARES_1_MASK, (bit ? 1 : 0));
}

static void _dw_phy_data_enable_polarity(u8 bit)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_SELDATAENPOL_MASK, (bit ? 1 : 0));
}

static void _dw_phy_interface_control(u8 bit)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_SELDIPIF_MASK, (bit ? 1 : 0));
}

static int _dw_phy_lock_state(void)
{
	return dw_read_mask((PHY_STAT0), PHY_STAT0_TX_PHY_LOCK_MASK);
}

static void _dw_phy_interrupt_mask(u8 mask)
{
	dw_write_mask(PHY_MASK0, mask, 0xff);
}

static void _dw_phy_i2c_config(void)
{
	dw_write(JTAG_PHY_CONFIG, JTAG_PHY_CONFIG_I2C_JTAGZ_MASK);
}

static void _dw_phy_i2c_mask_interrupts(int mask)
{
	dw_write_mask(PHY_I2CM_INT,
			PHY_I2CM_INT_DONE_MASK_MASK, mask ? 1 : 0);
	dw_write_mask(PHY_I2CM_CTLINT,
			PHY_I2CM_CTLINT_ARBITRATION_MASK_MASK, mask ? 1 : 0);
	dw_write_mask(PHY_I2CM_CTLINT,
			PHY_I2CM_CTLINT_NACK_MASK_MASK, mask ? 1 : 0);
}

static void _dw_phy_i2c_mask_state(void)
{
	u8 mask = 0;

	mask |= IH_I2CMPHY_STAT0_I2CMPHYERROR_MASK;
	mask |= IH_I2CMPHY_STAT0_I2CMPHYDONE_MASK;
	dw_write_mask(IH_I2CMPHY_STAT0, mask, 0);
}

static void _dw_phy_i2c_slave_address(u8 value)
{
	dw_write_mask(PHY_I2CM_SLAVE,
			PHY_I2CM_SLAVE_SLAVEADDR_MASK, value);
}

static int _dw_phy_i2c_write(u8 addr, u16 data)
{
	int timeout = PHY_TIMEOUT;
	u32 status  = 0;

	/* Set address */
	dw_write(PHY_I2CM_ADDRESS, addr);

	/* Set value */
	dw_write(PHY_I2CM_DATAO_1, (u8) ((data >> 8) & 0xFF));
	dw_write(PHY_I2CM_DATAO_0, (u8) (data & 0xFF));

	dw_write(PHY_I2CM_OPERATION, PHY_I2CM_OPERATION_WR_MASK);

	do {
		udelay(10);
		status = dw_read_mask(IH_I2CMPHY_STAT0,
				IH_I2CMPHY_STAT0_I2CMPHYERROR_MASK |
				IH_I2CMPHY_STAT0_I2CMPHYDONE_MASK);
	} while (status == 0 && (timeout--));

	dw_write(IH_I2CMPHY_STAT0, status); /* clear read status */

	if (status & IH_I2CMPHY_STAT0_I2CMPHYERROR_MASK) {
		hdmi_err("I2C PHY write failed\n");
		return -1;
	}

	if (status & IH_I2CMPHY_STAT0_I2CMPHYDONE_MASK)
		return 0;

	hdmi_wrn("ASSERT I2C Write timeout - check PHY - exiting\n");
	return -1;
}

static int _dw_phy_i2c_read(u8 addr, u16 *value)
{
	int timeout = PHY_TIMEOUT;
	u32 status  = 0;

	/* Set address */
	dw_write(PHY_I2CM_ADDRESS, addr);

	dw_write(PHY_I2CM_OPERATION, PHY_I2CM_OPERATION_RD_MASK);

	do {
		udelay(10);
		status = dw_read_mask(IH_I2CMPHY_STAT0,
				IH_I2CMPHY_STAT0_I2CMPHYERROR_MASK |
				IH_I2CMPHY_STAT0_I2CMPHYDONE_MASK);
	} while (status == 0 && (timeout--));

	dw_write(IH_I2CMPHY_STAT0, status); /* clear read status */

	if (status & IH_I2CMPHY_STAT0_I2CMPHYERROR_MASK) {
		hdmi_inf(" I2C Read failed\n");
		return -1;
	}

	if (status & IH_I2CMPHY_STAT0_I2CMPHYDONE_MASK) {

		*value = ((u16) (dw_read((PHY_I2CM_DATAI_1)) << 8)
				| dw_read((PHY_I2CM_DATAI_0)));
		return 0;
	}

	hdmi_inf(" ASSERT I2C Read timeout - check PHY - exiting\n");
	return -1;
}

static int _dw_phy_set_slave_address(u8 value)
{
	switch (gPHY_interface) {
#ifdef SUPPORT_PHY_JTAG
	case DW_PHY_ACCESS_JTAG:
		_phy_jtag_slave_address(0xD4);
		return 0;
#endif
	case DW_PHY_ACCESS_I2C:
		_dw_phy_i2c_slave_address(value);
		return 0;
	default:
		hdmi_err("PHY interface not defined");
	}
	return -1;
}

static int _dw_phy_set_interface(dw_phy_access_t interface)
{
	if (gPHY_interface == interface) {
		hdmi_trace("dw phy set interface %s mode\n",
				interface == DW_PHY_ACCESS_I2C ? "i2c" : "jtag");

		(gPHY_interface == DW_PHY_ACCESS_I2C) ?
				_dw_phy_set_slave_address(PHY_I2C_SLAVE_ADDR) : 0;

		return 0;
	}

	switch (interface) {
#ifdef SUPPORT_PHY_JTAG
	case DW_PHY_ACCESS_JTAG:
		_phy_jtag_init(0xD4);
		break;
#endif
	case DW_PHY_ACCESS_I2C:
		_dw_phy_i2c_config();
		_dw_phy_set_slave_address(PHY_I2C_SLAVE_ADDR);
		break;
	default:
		hdmi_err("dw phy interface %d not defined\n", interface);
		return -1;
	}
	gPHY_interface = interface;
	hdmi_trace("dw phy interface set to %s\n",
			interface == DW_PHY_ACCESS_I2C ? "I2C" : "JTAG");
	return 0;
}

void dw_phy_svsret(void)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_SVSRET_MASK, 0x0);
	udelay(5);
	dw_write_mask(PHY_CONF0, PHY_CONF0_SVSRET_MASK, 0x1);
}

void dw_phy_power_enable(u8 enable)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_PDDQ_MASK, !enable);
	dw_write_mask(PHY_CONF0, PHY_CONF0_TXPWRON_MASK, enable);
}

void dw_phy_i2c_fast_mode(u8 bit)
{
	dw_write_mask(PHY_I2CM_DIV, PHY_I2CM_DIV_FAST_STD_MODE_MASK, bit);
}

void dw_phy_i2c_master_reset(void)
{
	dw_write_mask(PHY_I2CM_SOFTRSTZ,
			PHY_I2CM_SOFTRSTZ_I2C_SOFTRSTZ_MASK, 1);
}

int dw_phy_reconfigure_interface(void)
{
	switch (gPHY_interface) {
#ifdef SUPPORT_PHY_JTAG
	case DW_PHY_ACCESS_JTAG:
		_phy_jtag_init(0xD4);
		break;
#endif
	case DW_PHY_ACCESS_I2C:
		_dw_phy_i2c_config();
		_dw_phy_set_slave_address(PHY_I2C_SLAVE_ADDR);
		break;
	default:
		hdmi_err("PHY interface not defined");
		return -1;
	}
	hdmi_trace("dw phy interface reconfiguration set to %s\n",
		gPHY_interface == DW_PHY_ACCESS_I2C ? "i2c" : "jtag");
	return 0;

}

int dw_phy_standby(void)
{
	u8 phy_mask = 0;

	phy_mask |= PHY_MASK0_TX_PHY_LOCK_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_0_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_1_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_2_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_3_MASK;
	_dw_phy_interrupt_mask(phy_mask);
	_dw_phy_enable_tmds(0);
	_dw_phy_power_down(0);	/* disable PHY */
	dw_phy_power_enable(0);

	hdmi_trace("dw phy standby done\n");
	return 0;
}

int dw_phy_enable_hpd_sense(void)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_ENHPDRXSENSE_MASK, 0x1);
	return true;
}

int dw_phy_disable_hpd_sense(void)
{
	dw_write_mask(PHY_CONF0, PHY_CONF0_ENHPDRXSENSE_MASK, 0x0);
	return true;
}

u8 dw_phy_get_hpd_rxsense(void)
{
	return dw_read_mask(PHY_CONF0, PHY_CONF0_ENHPDRXSENSE_MASK);
}

u8 dw_phy_hot_plug_state(void)
{
	return dw_read_mask((PHY_STAT0), PHY_STAT0_HPD_MASK);
}

u8 dw_phy_get_rxsense(void)
{
	return (u8)(dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_0_MASK) |
		dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_1_MASK) |
		dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_2_MASK) |
		dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_3_MASK));
}

u8 dw_phy_rxsense_state(void)
{
	u8 state = 0;
	state |= dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_3_MASK) << 3;
	state |= dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_2_MASK) << 2;
	state |= dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_1_MASK) << 1;
	state |= dw_read_mask((PHY_STAT0), PHY_STAT0_RX_SENSE_0_MASK) << 0;
	return state;
}

u8 dw_phy_pll_lock_state(void)
{
	return dw_read_mask((PHY_STAT0), PHY_STAT0_TX_PHY_LOCK_MASK);
}

u8 dw_phy_power_state(void)
{
	return dw_read_mask(PHY_CONF0, PHY_CONF0_TXPWRON_MASK);
}

/* wait PHY_TIMEOUT no of cycles at most for the PLL lock signal to raise ~around 20us max */
int dw_phy_wait_lock(void)
{
	int i = 0;

	for (i = 0; i < PHY_TIMEOUT; i++) {
		udelay(5);
		if (_dw_phy_lock_state() & 0x1) {
			return 1;
		}
	}
	return 0;
}

int dw_phy_write(u8 addr, u16 data)
{
	switch (gPHY_interface) {
#ifdef SUPPORT_PHY_JTAG
	case DW_PHY_ACCESS_JTAG:
		return _phy_jtag_write(addr, data);
#endif
	case DW_PHY_ACCESS_I2C:
		return _dw_phy_i2c_write(addr, data);
	default:
		hdmi_err("PHY interface not defined");
	}
	return -1;
}

int dw_phy_read(u8 addr, u16 *value)
{
	switch (gPHY_interface) {
#ifdef SUPPORT_PHY_JTAG
	case DW_PHY_ACCESS_JTAG:
		return _phy_jtag_read(addr, value);
#endif
	case DW_PHY_ACCESS_I2C:
		return _dw_phy_i2c_read(addr, value);
	default:
		hdmi_err("PHY interface not defined");
	}
	return -1;
}

int dw_phy_initialize(void)
{
	u8 phy_mask = 0;

	if (_dw_phy_set_interface(DW_PHY_ACCESS_I2C) < 0) {
		hdmi_err("set phy interface i2c failed!\n");
		return false;
	}

	dw_phy_power_enable(1);

	phy_mask |= PHY_MASK0_TX_PHY_LOCK_MASK;
	phy_mask |= PHY_STAT0_HPD_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_0_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_1_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_2_MASK;
	phy_mask |= PHY_MASK0_RX_SENSE_3_MASK;
	_dw_phy_interrupt_mask(phy_mask);

	_dw_phy_data_enable_polarity(0x1);

	_dw_phy_interface_control(0);

	_dw_phy_enable_tmds(0);

	_dw_phy_power_down(0);	/* disable PHY */

	_dw_phy_i2c_mask_interrupts(0);

	_dw_phy_i2c_mask_state();

	return true;
}

int dw_phy_init(void)
{
	struct dw_hdmi_dev_s *hdmi = dw_get_hdmi();

	gPHY_interface = DW_PHY_ACCESS_I2C;

	/* enable hpd exsense */
	dw_phy_enable_hpd_sense();

	if (hdmi->phy_ext->phy_init)
		hdmi->phy_ext->phy_init();

	return 0;
}

ssize_t dw_phy_dump(char *buf)
{
	int n = 0;

	n += sprintf(buf + n, "[dw phy]\n");
	n += sprintf(buf + n, " - hpd state   : [%s]\n",
			dw_phy_hot_plug_state() ? "detect" : "undetect");
	n += sprintf(buf + n, " - hpd rxsense : [%s]\n",
			dw_phy_get_hpd_rxsense() ? "enable" : "disable");
	n += sprintf(buf + n, " - tmds rxsense: [0x%X]\n",
			dw_phy_rxsense_state());
	n += sprintf(buf + n, " - mpll state  : [%s]\n",
			dw_phy_pll_lock_state() ? "lock" : "unlock");
	n += sprintf(buf + n, " - power state : [%s]\n",
			dw_phy_power_state() ? "enable" : "disable");

	return n;
}
