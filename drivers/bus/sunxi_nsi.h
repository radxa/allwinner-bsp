/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * SUNXI MBUS support
 *
 * Copyright (C) 2015 AllWinnertech Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_SUNXI_MBUS_H
#define __LINUX_SUNXI_MBUS_H

#include <linux/types.h>

/* MBUS PMU ids */
enum nsi_pmu {
#if IS_ENABLED(CONFIG_ARCH_SUN50IW10)
	MBUS_PMU_CPU    = 0,
	MBUS_PMU_GPU     = 1,
	MBUS_PMU_SD1     = 2,
	MBUS_PMU_MSTG    = 3,
	MBUS_PMU_GMAC0   = 4,
	MBUS_PMU_GMAC1   = 5,
	MBUS_PMU_USB0    = 6,
	MBUS_PMU_USB1    = 7,
	MBUS_PMU_NDFC    = 8,
	MBUS_PMU_DMAC    = 9,
	MBUS_PMU_CE      = 10,
	MBUS_PMU_DE0     = 11,
	MBUS_PMU_DE1     = 12,
	MBUS_PMU_VE      = 13,
	MBUS_PMU_CSI     = 14,
	MBUS_PMU_ISP     = 15,
	MBUS_PMU_G2D     = 16,
	MBUS_PMU_EINK    = 17,
	MBUS_PMU_IOMMU   = 18,
	MBUS_PMU_SYS_CPU = 19,
	MBUS_PMU_IAG_MAX,
	MBUS_PMU_TAG = 23,
#elif IS_ENABLED(CONFIG_ARCH_SUN50IW12)
	MBUS_PMU_CPU    = 0,
	MBUS_PMU_GPU     = 1,
	MBUS_PMU_VE_R			= 2,
	MBUS_PMU_VE				= 3,
	MBUS_PMU_TVDISP_MBUS	= 4,
	MBUS_PMU_TVDISP_AXI		= 5,
	MBUS_PMU_VE_RW			= 6,
	MBUS_PMU_TVFE			= 7,
	MBUS_PMU_NDFC			= 8,
	MBUS_PMU_DMAC			= 9,
	MBUS_PMU_CE				= 10,
	MBUS_PMU_IOMMU			= 11,
	MBUS_PMU_TVCAP			= 12,
	MBUS_PMU_GMAC0			= 13,
	MBUS_PMU_MSTG			= 14,
	MBUS_PMU_MIPS			= 15,
	MBUS_PMU_USB0			= 16,
	MBUS_PMU_USB1			= 17,
	MBUS_PMU_USB2			= 18,
	MBUS_PMU_MSTG1			= 19,
	MBUS_PMU_MSTG2			= 20,
	MBUS_PMU_NPD			= 21,
	MBUS_PMU_IAG_MAX,
	/* use RA1 to get total bandwidth, because no TA pmu for sun50iw12 */
	MBUS_PMU_TAG = 24,
#elif IS_ENABLED(CONFIG_ARCH_SUN55IW3)
	MBUS_PMU_GPU			= 0,
	MBUS_PMU_GIC			= 1,
	MBUS_PMU_USB3			= 2,
	MBUS_PMU_PCIE			= 3,
	MBUS_PMU_CE			= 4,
	MBUS_PMU_NPU			= 5,
	MBUS_PMU_ISP			= 6,
	MBUS_PMU_DSP			= 7,
	MBUS_PMU_G2D			= 8,
	MBUS_PMU_DI			= 9,
	MBUS_PMU_IOMMU			= 10,
	MBUS_PMU_VE_R			= 11,
	MBUS_PMU_VE_RW			= 12,
	MBUS_PMU_DE			= 13,
	MBUS_PMU_CSI			= 14,
	MBUS_PMU_NAND			= 15,
	MBUS_PMU_MATRIX			= 16,
	MBUS_PMU_SPI			= 17,
	MBUS_PMU_GMAC0			= 18,
	MBUS_PMU_GMAC1			= 19,
	MBUS_PMU_SMHC0			= 20,
	MBUS_PMU_SMHC1			= 21,
	MBUS_PMU_SMHC2			= 22,
	MBUS_PMU_USB0			= 23,
	MBUS_PMU_USB1			= 24,
	MBUS_PMU_USB2			= 25,
	MBUS_PMU_NPD			= 26,
	MBUS_PMU_DMAC			= 27,
	MBUS_PMU_DMA			= 28,
	MBUS_PMU_IAG_MAX,
	MBUS_PMU_TAG			= 31,
#elif IS_ENABLED(CONFIG_ARCH_SUN60IW2)
	MBUS_PMU_GMAC			= 0,
	MBUS_PMU_MSI_LITE0			= 1,
	MBUS_PMU_DE			= 2,
	MBUS_PMU_EINK			= 3,
	MBUS_PMU_DI			= 4,
	MBUS_PMU_G2D			= 5,
	MBUS_PMU_GPU			= 6,
	MBUS_PMU_VE0			= 7,
	MBUS_PMU_VE1			= 8,
	MBUS_PMU_VE2		= 9,
	MBUS_PMU_GIC			= 10,
	MBUS_PMU_MSI_LITE1		= 11,
	MBUS_PMU_MSI_LITE2			= 12,
	MBUS_PMU_USB_PCIE			= 13,
	MBUS_PMU_IOMMU0			= 14,
	MBUS_PMU_IOMMU1			= 15,
	MBUS_PMU_ISP			= 16,
	MBUS_PMU_CSI			= 17,
	MBUS_PMU_NPU			= 18,
	MBUS_PMU_CPU			= 19,
	MBUS_PMU_CPU1			= 20,
	MBUS_PMU_IAG_MAX,
	MBUS_PMU_TAG			= 31,
#endif
	MBUS_PMU_MAX,
};

#if IS_ENABLED(CONFIG_ARCH_SUN50IW10)
static const char *const pmu_name[] = {
	"cpu", "gpu", "sd1", "mstg", "gmac0", "gmac1", "usb0", "usb1", "ndfc",
	"dmac", "ce", "de0", "de1", "ve", "csi", "isp", "g2d", "eink", "iommu",
	"sys_cpu", "total",
};
#elif IS_ENABLED(CONFIG_ARCH_SUN50IW12)
static const char *const pmu_name[] = {
	"cpu", "gpu", "ve_r", "ve", "tvd_mbus", "tvd_axi", "ve_rw", "tvfe", "ndfc", "dmac", "ce",
	"iommu", "tvcap", "gmac0", "mstg", "mips", "usb0", "usb1", "usb2", "mstg1", "mstg2",
	"npd", "total",
};
#elif IS_ENABLED(CONFIG_ARCH_SUN55IW3)
static const char *const pmu_name[] = {
	"cpu", "gpu", "gic", "usb3", "pcie", "ce", "npu", "isp", "dsp", "g2d", "di", "iommu",
	"ve_r", "ve_rw", "de", "csi", "nand", "matrix", "spi", "gmac0", "gmac1", "smhc0",
	"smhc1", "smhc2", "usb0", "usb1", "usb2", "npd", "dmac", "dma", "total"
};
#elif IS_ENABLED(CONFIG_ARCH_SUN60IW2)
static const char *const pmu_name[] = {
	"gmac", "msi_list0", "de", "eink", "di", "g2d", "gpu", "ve0", "ve1", "ve2", "gic",
	"msi_lite1", "msi_lite2", "usb_pcie", "iommu0", "iommu1", "isp", "csi", "npu", "cpu0", "cpu1",
};
#endif

#define get_name(n)      pmu_name[n]

#if IS_ENABLED(CONFIG_AW_NSI)
extern int nsi_port_setpri(enum nsi_pmu port, unsigned int pri);
extern int nsi_port_setqos(enum nsi_pmu port, unsigned int qos);
extern bool nsi_probed(void);
#endif

#if IS_ENABLED(CONFIG_ARCH_SUN55IW3)
#define AW_NSI_CPU_CHANNEL	1
#endif

#define nsi_disable_port_by_index(dev) \
	nsi_port_control_by_index(dev, false)
#define nsi_enable_port_by_index(dev) \
	nsi_port_control_by_index(dev, true)

#endif
