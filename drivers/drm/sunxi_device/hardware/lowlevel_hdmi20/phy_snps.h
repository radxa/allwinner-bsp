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

#ifndef _PHY_SNPS_H
#define _PHY_SNPS_H

#include "dw_dev.h"
#include "dw_avp.h"
#include "dw_mc.h"

#define SNPS_TIMEOUT			100
#define PHY_I2C_SLAVE_ADDR		0x69

#define PHY_MODEL_301		301
#define PHY_MODEL_303		303
#define PHY_MODEL_108		108

#define OPMODE_PLLCFG 0x06 /* Mode of Operation and PLL  Dividers Control Register */
#define CKSYMTXCTRL   0x09 /* Clock Symbol and Transmitter Control Register */
#define PLLCURRCTRL   0x10 /* PLL Current Control Register */
#define VLEVCTRL      0x0E /* Voltage Level Control Register */
#define PLLGMPCTRL    0x15 /* PLL Gmp Control Register */
#define TXTERM        0x19 /* Transmission Termination Register */

struct snps_phy301_param_s {
	u32			clock;/* phy clock: unit:kHZ */
	dw_pixel_repetition_t	pixel;
	dw_color_depth_t		color;
	dw_phy_operation_mode_t  opmode;
	u16			oppllcfg;
	u16			pllcurrctrl;
	u16			pllgmpctrl;
	u16                     txterm;
	u16                     vlevctrl;
	u16                     cksymtxctrl;
};

struct snps_phy303_param_s {
	u32			clock;
	dw_pixel_repetition_t	pixel;
	dw_color_depth_t		color;
	dw_phy_operation_mode_t        opmode;
	u16			oppllcfg;
	u16			pllcurrctrl;
	u16			pllgmpctrl;
	u16                     txterm;
	u16                     vlevctrl;
	u16                     cksymtxctrl;
};

int snps_phy_config(void);

int snps_phy_write(u8 addr, void *data);

int snps_phy_read(u8 addr, void *data);

#endif	/* _PHY_SNPS_H */
