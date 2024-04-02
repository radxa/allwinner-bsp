/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * SUNXI NSI driver
 *
 * Copyright (C) 2015 AllWinnertech Ltd.
 * Author: xiafeng <xiafeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/compat.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>

#include "sunxi_nsi.h"
#include <sunxi-sid.h>

#include <asm/io.h>

#define DRIVER_NAME          "NSI"
#define DRIVER_NAME_PMU      DRIVER_NAME"_PMU"
#define NSI_MAJOR         137
#define NSI_MINORS        256

#define MBUS_PRI_MAX      0x3
#define MBUS_QOS_MAX      0x2

#define for_each_ports(port) for (port = 0; port < MBUS_PMU_IAG_MAX; port++)

/* n = 0~32 */
#define IAG_MODE(n)		   (0x0010 + (0x200 * (n)))
#define IAG_PRI_CFG(n)		   (0x0014 + (0x200 * (n)))
#define IAG_INPUT_OUTPUT_CFG(n)	   (0x0018 + (0x200 * (n)))
#define IAG_BAND_WIDTH(n)	   (0x0028 + (0x200 * (n)))
#define IAG_BAND_WIDTH_LIMIT_MAX_BITS (12)
#define IAG_BAND_WIDTH_LIMIT_MAX_VALUE ((1 << IAG_BAND_WIDTH_LIMIT_MAX_BITS) - 1)
#define IAG_SATURATION(n)	   (0x002c + (0x200 * (n)))
#define IAG_SATURATION_LIMIT_MAX_BITS (10)
#define IAG_SATURATION_LIMIT_MAX_VALUE ((1 << IAG_SATURATION_LIMIT_MAX_BITS) - 1)

#if IS_ENABLED(CONFIG_ARCH_SUN55I)
#define IAG_QOS_CFG(n)		   (0x000C + (0x200 * (n)))
#else
#define IAG_QOS_CFG(n)		   (0x0094 + (0x200 * 23) + (0x4 * ((n) / 16)))
#endif

/* Counter n = 0 ~ 19 */
#define MBUS_PMU_ENABLE(n)         (0x00c0 + (0x200 * (n)))
#define MBUS_PMU_CLR(n)            (0x00c4 + (0x200 * (n)))
#define MBUS_PMU_CYCLE(n)          (0x00c8 + (0x200 * (n)))
#define MBUS_PMU_RQ_RD(n)          (0x00cc + (0x200 * (n)))
#define MBUS_PMU_RQ_WR(n)          (0x00d0 + (0x200 * (n)))
#define MBUS_PMU_DT_RD(n)          (0x00d4 + (0x200 * (n)))
#define MBUS_PMU_DT_WR(n)          (0x00d8 + (0x200 * (n)))
#define MBUS_PMU_LA_RD(n)          (0x00dc + (0x200 * (n)))
#define MBUS_PMU_LA_WR(n)          (0x00e0 + (0x200 * (n)))

#define MBUS_PORT_MODE          (MBUS_PMU_MAX + 0)
#define MBUS_PORT_PRI           (MBUS_PMU_MAX + 1)
#define MBUS_INPUT_OUTPUT       (MBUS_PMU_MAX + 2)
#define MBUS_PORT_QOS           (MBUS_PMU_MAX + 3)
#define MBUS_PORT_ABS_BWL	(MBUS_PMU_MAX + 4)
#define MBUS_PORT_ABS_BWLEN	(MBUS_PMU_MAX + 5)
#ifdef AW_NSI_CPU_CHANNEL
#define CPU_ABS_RW		(MBUS_PMU_MAX + 6)
#endif

#ifdef AW_NSI_CPU_CHANNEL
#define CPU_PMU_EN		0x0020
#define CPU_PMU_CLR		0x0024
#define CPU_PMU_PER		0x0028
#define CPU_CHL0_PMU_REQ_R	0x0080
#define CPU_CHL0_PMU_REQ_W	0x0084
#define CPU_CHL0_PMU_DATA_R	0x0088
#define CPU_CHL0_PMU_DATA_W	0x008c
#define CPU_CHL0_PMU_LAT_R	0x0090
#define CPU_CHL0_PMU_LAT_W	0x0094

#define CPU_IAG_MODE		   0x0030
#define CPU_PRI_CFG		   0x0034
#define CPU_INPUT_OUTPUT_CFG	   0x0038
#define CPU_BAND_WIDTH_LIMIT(n)	   (0x0048 + ((n) * 8))
#define CPU_BAND_WIDTH_LIMIT_MAX_BITS (12)
#define CPU_BAND_WIDTH_LIMIT_MAX_VALUE ((1 << CPU_BAND_WIDTH_LIMIT_MAX_BITS) - 1)
#define CPU_SATURATION_LIMIT(n)    (0x004c + ((n) * 8))
#define CPU_SATURATION_LIMIT_MAX_BITS (10)
#define CPU_SATURATION_LIMIT_MAX_VALUE ((1 << CPU_SATURATION_LIMIT_MAX_BITS) - 1)
#define CPU_QOS_CFG		   0x002c
#define CPU_BW_LIMIT_EN_BIT        16
static int limit_mask_enable;
static int limit_mask;
static uint32_t cpu_rw_settings[3];
#endif

enum NSI_TOPOLOGY_TYPE_E {
	NSI_TOPO_V0, /* legacy, all master to one RA and one TA and one DDR, e.g sun50iw10 */
	NSI_TOPO_V1, /* CPU isolated, CPU bypass RA and TA, e.g sun55iw3 */
	NSI_TOPO_V2, /* dual cahnnel, two TA for two seperate dram path */
};

enum NSI_CLK_PATH_E {
	NSI_CLK_PATH_V0, /* legacy, nsi clock is mbus clock */
	NSI_CLK_PATH_V1, /* nsi module clock depart from mbus clock */
};

struct nsi_bus {
	struct cdev cdev;
	struct device *dev;
	void __iomem *base;
	void __iomem *cpu_base;
	struct clk *pclk;  /* PLL clock */
	struct clk *mclk;  /* mbus clock */
	struct reset_control *reset;
	unsigned long rate;

	/*
	 * master dedicated clock
	 * master id -> clk_idx idx -> clks idx -> clk for this master
	 * clk = clks[ clk_idx[ master_id ] ]
	 */
	struct clk **clks;
	u32 *clk_idx;

	/*
	 * how "mater - IA - RA - RDM - RA - TA" connect
	 * different topology have different ways to calculate
	 * bandwidth from regs
	 */
	enum NSI_TOPOLOGY_TYPE_E topo_tpye;

	/*
	 * different clk path have different clks/resets to deal with
	 * use a point array to hold them
	 */
	enum NSI_CLK_PATH_E clk_path_type;
	struct reset_control *resets;
};

struct nsi_pmu_data {
	struct device *dev_nsi;
	unsigned long period;
	spinlock_t bwlock;
};

/* extra one element for total */
unsigned long latency_sum[MBUS_PMU_IAG_MAX + 1];
unsigned long latency_aver[MBUS_PMU_IAG_MAX + 1];
unsigned long request_sum[MBUS_PMU_IAG_MAX + 1];

static struct nsi_pmu_data hw_nsi_pmu;
static struct nsi_bus sunxi_nsi;
static struct class *nsi_pmu_class;

static int nsi_init_status = -EAGAIN;

static DEFINE_MUTEX(nsi_setting);

/**
 * nsi_port_setthd() - set a master priority
 *
 * @pri, priority
 */
int notrace nsi_port_setmode(enum nsi_pmu port, unsigned int mode)
{
	unsigned int value = 0;

	if (port >= MBUS_PMU_IAG_MAX)
		return -ENODEV;

	mutex_lock(&nsi_setting);

#ifdef AW_NSI_CPU_CHANNEL
	if (port == 0) {
		value = readl_relaxed(sunxi_nsi.cpu_base + CPU_IAG_MODE);
		value &= ~0x3;
		writel_relaxed(value | mode, sunxi_nsi.cpu_base + CPU_IAG_MODE);
		mutex_unlock(&nsi_setting);
	} else
#endif
	{
#ifdef AW_NSI_CPU_CHANNEL
		port = port - 1;
#endif
		value = readl_relaxed(sunxi_nsi.base + IAG_MODE(port));
		value &= ~0x3;
		writel_relaxed(value | mode, sunxi_nsi.base + IAG_MODE(port));
	}

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_port_setmode);

/**
 * nsi_port_setpri() - set a master priority
 *
 * @qos: the qos value want to set
 */
int notrace nsi_port_setpri(enum nsi_pmu port, unsigned int pri)
{
	unsigned int value;

	if (port >= MBUS_PMU_IAG_MAX)
		return -ENODEV;

	if (pri > MBUS_PRI_MAX)
		return -EPERM;

	mutex_lock(&nsi_setting);

#ifdef AW_NSI_CPU_CHANNEL
	if (port == 0) {
		value = readl_relaxed(sunxi_nsi.cpu_base + CPU_PRI_CFG);
		value &= ~0xf;
		writel_relaxed(value | (pri << 2) | pri,
		       sunxi_nsi.cpu_base + CPU_PRI_CFG);
	} else
#endif
	{
#ifdef AW_NSI_CPU_CHANNEL
		port = port - 1;
#endif
		value = readl_relaxed(sunxi_nsi.base + IAG_PRI_CFG(port));
		value &= ~0xf;
		writel_relaxed(value | (pri << 2) | pri,
		       sunxi_nsi.base + IAG_PRI_CFG(port));

	}

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_port_setpri);

/**
 * nsi_port_setqos() - set a master qos(hpr/lpr)
 *
 * @qos: the qos value want to set
 */
int notrace nsi_port_setqos(enum nsi_pmu port, unsigned int qos)
{
	unsigned int value;

	if (port >= MBUS_PMU_IAG_MAX)
		return -ENODEV;

	if (qos > MBUS_QOS_MAX)
		return -EPERM;

	mutex_lock(&nsi_setting);

#ifdef AW_NSI_CPU_CHANNEL
	if (port == 0) {
		value = readl_relaxed(sunxi_nsi.cpu_base + CPU_QOS_CFG);
		value &= ~(0x3 << ((port % 16) * 2));
		writel_relaxed(value | (qos << ((port % 16) * 2)),
			       sunxi_nsi.cpu_base + CPU_QOS_CFG);
	} else
#endif
	{
#ifdef AW_NSI_CPU_CHANNEL
		port = port - 1;
#endif

		value = readl_relaxed(sunxi_nsi.base + IAG_QOS_CFG(port));
		value &= ~(0x3 << ((port % 16) * 2));
		writel_relaxed(value | (qos << ((port % 16) * 2)),
			       sunxi_nsi.base + IAG_QOS_CFG(port));
	}

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_port_setqos);

/**
 * nsi_port_setio() - set a master's qos in or out
 *
 * @wt: the wait time want to set, based on MCLK
 */
int notrace nsi_port_setio(enum nsi_pmu port, bool io)
{
	unsigned int value;

	if (port >= MBUS_PMU_IAG_MAX)
		return -ENODEV;

	mutex_lock(&nsi_setting);

#ifdef AW_NSI_CPU_CHANNEL
	if (port == 0) {
		value = readl_relaxed(sunxi_nsi.cpu_base + CPU_INPUT_OUTPUT_CFG);
		value &= ~0x1;
		writel_relaxed(value | io,
			       sunxi_nsi.cpu_base + CPU_INPUT_OUTPUT_CFG);

	} else
#endif
	{
#ifdef AW_NSI_CPU_CHANNEL
		port = port - 1;
#endif

		value = readl_relaxed(sunxi_nsi.base + IAG_INPUT_OUTPUT_CFG(port));
		value &= ~0x1;
		writel_relaxed(value | io,
			       sunxi_nsi.base + IAG_INPUT_OUTPUT_CFG(port));
	}

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_port_setio);

/*
 * statistic for different master perform under
 * different clock
 */
static inline notrace unsigned long __rate_for_master(enum nsi_pmu port)
{
	unsigned long rate;
	if (sunxi_nsi.topo_tpye == NSI_TOPO_V2) {
		/* dedicated clk is assigned to each master, use it instead */
		rate = clk_get_rate(sunxi_nsi.clks[sunxi_nsi.clk_idx[port]]);
	} else {
		/* platform using default clk assign happends to have cpu with id 0 */
		if (port == 0) {
			/* cpu pmu clk : pll_ddr/4 */
			rate = clk_get_rate(sunxi_nsi.pclk) / 4;
		} else {
			rate = clk_get_rate(sunxi_nsi.mclk);
		}
	}
	return rate;
}
/*
 * we used bandwidth in time(s,ms,etc) base, nsi
 * count bandwidth in cycle base, translate time
 * based input to cycle based reg value
 */
static inline notrace void __cal_bw_reg_value(enum nsi_pmu port, unsigned int bwl, unsigned int *bw_val, unsigned int *sat_val)
{
	unsigned int n, t;
	unsigned long rate;

	rate = __rate_for_master(port);

	*bw_val = 256 * bwl / (rate / 1000000);
	t = 1000 * 10 / (rate / 1000000);
	n = 5 * 1000 / t * 10;
	*sat_val = n * (*bw_val) / (256 * 16);
}
/**
 * nsi_port_set_abs_bwl() - set a master absolutely bandwidth limit
 *
 * @bwl: the number of bandwidth limit
 */
int notrace nsi_port_set_abs_bwl(enum nsi_pmu port, unsigned int bwl)
{
	unsigned int bw_val, sat_val;
	unsigned int n, t;
	unsigned long mrate = clk_get_rate(sunxi_nsi.mclk);
	unsigned long drate;

	if (port >= MBUS_PMU_IAG_MAX)
		return -ENODEV;

	if (bwl == 0)
		return -EPERM;

	/* cpu pmu clk : pll_ddr/4 */
	drate = clk_get_rate(sunxi_nsi.pclk) / 4;

	/* cpu */
	if (port == 0) {
		bw_val = 256 * bwl / (drate / 1000000);
		t = 1000 * 10 / (drate / 1000000);
	} else {
		bw_val = 256 * bwl / (mrate / 1000000);
		t = 1000 * 10 / (mrate / 1000000);
	}
	n = 5 * 1000 / t * 10;
	sat_val = n * bw_val / (256 * 16);

	mutex_lock(&nsi_setting);

#ifdef AW_NSI_CPU_CHANNEL
	if (port == 0) {
		bw_val = bw_val > CPU_BAND_WIDTH_LIMIT_MAX_VALUE ?
				 CPU_BAND_WIDTH_LIMIT_MAX_VALUE :
				 bw_val;
		sat_val = sat_val > CPU_SATURATION_LIMIT_MAX_VALUE ?
				 CPU_SATURATION_LIMIT_MAX_VALUE :
				 sat_val;
		writel_relaxed(bw_val, sunxi_nsi.cpu_base + CPU_BAND_WIDTH_LIMIT(0));
		writel_relaxed(sat_val, sunxi_nsi.cpu_base + CPU_SATURATION_LIMIT(0));
	} else
#endif
	{
#ifdef AW_NSI_CPU_CHANNEL
		port = port - 1;
#endif
		bw_val = bw_val > IAG_BAND_WIDTH_LIMIT_MAX_VALUE ?
				 IAG_BAND_WIDTH_LIMIT_MAX_VALUE :
				 bw_val;
		sat_val = sat_val > IAG_SATURATION_LIMIT_MAX_VALUE ?
				 IAG_SATURATION_LIMIT_MAX_VALUE :
				 sat_val;
		writel_relaxed(bw_val, sunxi_nsi.base + IAG_BAND_WIDTH(port));
		writel_relaxed(sat_val, sunxi_nsi.base + IAG_SATURATION(port));
	}

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_port_set_abs_bwl);

#ifdef AW_NSI_CPU_CHANNEL
int notrace nsi_set_cpu_rw_bw_en(unsigned int dir_mask, unsigned enabled)
{
	unsigned int value;
	int ret = 0;
	mutex_lock(&nsi_setting);
	if (enabled) {
		value = readl_relaxed(sunxi_nsi.cpu_base + CPU_IAG_MODE);
		if (dir_mask)
			value |= 0x1;
		writel_relaxed(value | dir_mask << CPU_BW_LIMIT_EN_BIT,
			       sunxi_nsi.cpu_base + CPU_IAG_MODE);
	} else {
		value = readl_relaxed(sunxi_nsi.cpu_base + CPU_IAG_MODE);

		/* so caller can store for recovery*/
		ret = value >> CPU_BW_LIMIT_EN_BIT & dir_mask;
		value = value & ~((dir_mask << CPU_BW_LIMIT_EN_BIT) | 1);

		writel_relaxed(value, sunxi_nsi.cpu_base + CPU_IAG_MODE);
	}
	mutex_unlock(&nsi_setting);
	return ret;
}
/**
 * nsi_set_cpu_rw_bwl() - set cpu read or write bandwidth limit independently
 *
 * @bwl: the number of bandwidth limit
 */
int notrace nsi_set_cpu_rw_bwl(unsigned int cpu_port, unsigned int bwl)
{
	unsigned int bw_val, sat_val;
	unsigned int n, t;
	unsigned long drate;
	unsigned int value;

	if (bwl == 0)
		return -EPERM;

	/* cpu pmu clk : pll_ddr/4 */
	drate = clk_get_rate(sunxi_nsi.pclk) / 4;

	bw_val = 256 * bwl / (drate / 1000000);
	t = 1000 * 10 / (drate / 1000000);
	n = 5 * 1000 / t * 10;
	sat_val = n * bw_val / (256 * 16);

	mutex_lock(&nsi_setting);
	value = readl_relaxed(sunxi_nsi.cpu_base + CPU_IAG_MODE);
	writel_relaxed(value & ~(0x1 << (cpu_port + CPU_BW_LIMIT_EN_BIT)), sunxi_nsi.cpu_base + CPU_IAG_MODE);

	bw_val = bw_val > CPU_BAND_WIDTH_LIMIT_MAX_VALUE ?
			 CPU_BAND_WIDTH_LIMIT_MAX_VALUE :
			 bw_val;
	sat_val = sat_val > CPU_SATURATION_LIMIT_MAX_VALUE ?
			  CPU_SATURATION_LIMIT_MAX_VALUE :
			  sat_val;
	writel_relaxed(bw_val, sunxi_nsi.cpu_base + CPU_BAND_WIDTH_LIMIT(cpu_port));
	writel_relaxed(sat_val, sunxi_nsi.cpu_base + CPU_SATURATION_LIMIT(cpu_port));

	if (limit_mask_enable)
		value |= 1 << (CPU_BW_LIMIT_EN_BIT + cpu_port) | 0x1;
	writel_relaxed(value, sunxi_nsi.cpu_base + CPU_IAG_MODE);
	limit_mask |= 1 << cpu_port;
	/* store for suspend/resume */
	cpu_rw_settings[cpu_port] = bwl;

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_set_cpu_rw_bwl);

int notrace nsi_unset_cpu_rw_bwl(unsigned int cpu_port)
{
	unsigned int value;

	mutex_lock(&nsi_setting);
	limit_mask &= ~(1 << cpu_port);
	value = readl_relaxed(sunxi_nsi.cpu_base + CPU_IAG_MODE);
	if ((value >> CPU_BW_LIMIT_EN_BIT & ~(0x1 << cpu_port)) == 0)
		value = 0;
	else
		value = value & ~(0x1 << (cpu_port + CPU_BW_LIMIT_EN_BIT));
	/* store for suspend/resume */
	cpu_rw_settings[cpu_port] = 0;

	writel_relaxed(value, sunxi_nsi.cpu_base + CPU_IAG_MODE);
	mutex_unlock(&nsi_setting);

	return 0;
}
#endif
/**
 * nsi_port_set_abs_bwlen() - enable a master absolutely bandwidth limit
 * function
 *
 * @port: index of the port to setup
 * @en: 0-disable, 1-enable
 */
int notrace nsi_port_set_abs_bwlen(enum nsi_pmu port, bool en)
{
	if (port >= MBUS_PMU_IAG_MAX)
		return -ENODEV;

	mutex_lock(&nsi_setting);

#ifdef AW_NSI_CPU_CHANNEL
	if (port == 0)
		writel_relaxed(en, sunxi_nsi.cpu_base + CPU_IAG_MODE);
	else
#endif
	{
#ifdef AW_NSI_CPU_CHANNEL
		port = port - 1;
#endif
		writel_relaxed(en, sunxi_nsi.base + IAG_MODE(port));
	}

	mutex_unlock(&nsi_setting);

	return 0;
}
EXPORT_SYMBOL_GPL(nsi_port_set_abs_bwlen);

#ifdef AW_NSI_CPU_CHANNEL
static void nsi_cpu_pmu_disable(void)
{
	unsigned int value;

	value = readl_relaxed(sunxi_nsi.cpu_base + CPU_PMU_EN);
	value &= ~(0x1);
	writel_relaxed(value, sunxi_nsi.cpu_base + CPU_PMU_EN);
}

static void nsi_cpu_pmu_enable(void)
{
	unsigned int value;

	value = readl_relaxed(sunxi_nsi.cpu_base + CPU_PMU_EN);
	value |= (0x1);
	writel_relaxed(value, sunxi_nsi.cpu_base + CPU_PMU_EN);
}

static void nsi_cpu_pmu_clear(void)
{
	unsigned int value;

	value = readl_relaxed(sunxi_nsi.cpu_base + CPU_PMU_CLR);
	value |= (0x1);
	writel_relaxed(value, sunxi_nsi.cpu_base + CPU_PMU_CLR);
}
#endif

static void nsi_pmu_disable(enum nsi_pmu port)
{
	unsigned int value;

	value = readl_relaxed(sunxi_nsi.base + MBUS_PMU_ENABLE(port));
	value &= ~(0x1);
	writel_relaxed(value, sunxi_nsi.base + MBUS_PMU_ENABLE(port));
}

static void nsi_pmu_enable(enum nsi_pmu port)
{
	unsigned int value;

	value = readl_relaxed(sunxi_nsi.base + MBUS_PMU_ENABLE(port));
	value |= (0x1);
	writel_relaxed(value, sunxi_nsi.base + MBUS_PMU_ENABLE(port));
}

static void nsi_pmu_clear(enum nsi_pmu port)
{
	unsigned int value = readl_relaxed(sunxi_nsi.base + MBUS_PMU_CLR(port));
	value |= (0x1);
	writel_relaxed(value, sunxi_nsi.base + MBUS_PMU_CLR(port));
}

static inline notrace u64 __period_to_cycle(enum nsi_pmu port,
					    unsigned long period)
{
	return period * (__rate_for_master(port) / 10000000);
}

ssize_t __nsi_pmu_timer_store_v2(unsigned long period)
{
	int port;
#define UPDATE_CYCLE(port, period)                                     \
	do {                                                           \
		writel_relaxed(__period_to_cycle(port, period),        \
			       sunxi_nsi.base + MBUS_PMU_CYCLE(port)); \
		nsi_pmu_disable(port);                                 \
		nsi_pmu_clear(port);                                   \
	} while (0)

	/*
	 * master <-> clk map is done by idx array
	 * dont need seperated code blode for different master
	 * one loop will be enough
	 */
	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		UPDATE_CYCLE(port, period);
	}
	/*
	 * TA idx no continous though, still need other code block
	 */
	UPDATE_CYCLE(MBUS_PMU_TAG, period);
	UPDATE_CYCLE((MBUS_PMU_TAG + 1), period);

	/* period updated, restart pmu */
	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		nsi_pmu_enable(port);
	}
	nsi_pmu_enable(MBUS_PMU_TAG);
	nsi_pmu_enable((MBUS_PMU_TAG + 1));

#undef UPDATE_CYCLE
	return 0;
}

ssize_t nsi_pmu_timer_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u64 cycle;
	unsigned long mrate = clk_get_rate(sunxi_nsi.mclk);
	unsigned long drate;
	unsigned char *buffer = kmalloc(64, GFP_KERNEL);
	unsigned long period, flags, port;
	int ret = 0;

	ret = kstrtoul(buf, 10, &period);
	if (ret < 0)
		goto end;

	spin_lock_irqsave(&hw_nsi_pmu.bwlock, flags);

	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		latency_sum[port] = 0;
		latency_aver[port] = 0;
		request_sum[port] = 0;
	}

	if (sunxi_nsi.topo_tpye == NSI_TOPO_V2) {
		__nsi_pmu_timer_store_v2(period);
	} else {
		/*
		 * cpu pmu clk is equal to pll_ddr(pclk) / 4,
		 * if change the nsi's clk in dts, drate need to be adapted too
		 */

		drate = clk_get_rate(sunxi_nsi.pclk) / 4;

		/* set pmu period expect cpu */
		cycle = period * (mrate / 1000000);
#ifdef AW_NSI_CPU_CHANNEL
		for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
#else
		for (port = 1; port < MBUS_PMU_IAG_MAX; port++) {
#endif
			writel_relaxed(cycle, sunxi_nsi.base + MBUS_PMU_CYCLE(port));

			/* disabled the pmu count */
			nsi_pmu_disable(port);

			/* clean the insight counter */
			nsi_pmu_clear(port);

		}

		/*
		 *if mclk = 400MHZ, the signel time = 1000*1000*1000/(400*1000*1000)=2.5ns
		 *if check the 10us, the cycle count = 10*1000ns/2.5us = 4000
		 * us*1000/(1s/400Mhz) = us*1000*400000000(rate)/1000000000
		 */

		/* set cpu pmu period */
		cycle = period * (drate / 1000000);
#ifdef AW_NSI_CPU_CHANNEL
		writel_relaxed(cycle, sunxi_nsi.cpu_base + CPU_PMU_PER);
		hw_nsi_pmu.period = period;
		nsi_cpu_pmu_disable();
		nsi_cpu_pmu_clear();
#else
		writel_relaxed(cycle, sunxi_nsi.base + MBUS_PMU_CYCLE(MBUS_PMU_CPU));
		hw_nsi_pmu.period = period;
		nsi_pmu_disable(MBUS_PMU_CPU);
		nsi_pmu_clear(MBUS_PMU_CPU);
#endif

		writel_relaxed(cycle, sunxi_nsi.base + MBUS_PMU_CYCLE(MBUS_PMU_TAG));
		nsi_pmu_disable(MBUS_PMU_TAG);
		nsi_pmu_clear(MBUS_PMU_TAG);

		/* enable iag pmu */
		for (port = 0; port < MBUS_PMU_IAG_MAX; port++)
			nsi_pmu_enable(port);		/* enabled the pmu count */

#ifdef AW_NSI_CPU_CHANNEL
		nsi_cpu_pmu_enable();
#endif

		/* enable tag pmu */
		nsi_pmu_enable(MBUS_PMU_TAG);
	}

	udelay(10);
	spin_unlock_irqrestore(&hw_nsi_pmu.bwlock, flags);

end:
	kfree(buffer);
	return count;
}

static ssize_t nsi_pmu_timer_show(struct device *dev,
			       struct device_attribute *da, char *buf)
{
	unsigned int len;

	len = sprintf(buf, "%-6lu\n", hw_nsi_pmu.period);

	return len;
}

static const struct of_device_id sunxi_nsi_matches[] = {
#if IS_ENABLED(CONFIG_ARCH_SUN8I)
	{.compatible = "allwinner,sun8i-nsi", },
#endif
#if IS_ENABLED(CONFIG_ARCH_SUN50I)
	{.compatible = "allwinner,sun50i-nsi", },
#endif
#if IS_ENABLED(CONFIG_ARCH_SUN55I)
	{.compatible = "allwinner,sun55i-nsi", },
#endif
	{.compatible = "allwinner,sunxi-nsi-v2"},
	{},
};

#if IS_ENABLED(CONFIG_ARCH_SUN50IW10)
static void set_limit_for_old_version(void)
{
	/* GPU: limiter mode, limit to 1500MB/s, sat as 5us, nsi clk 400MHz */
	writel_relaxed(0x3c0, sunxi_nsi.base + IAG_BAND_WIDTH(1));
	writel_relaxed(0x1d4, sunxi_nsi.base + IAG_SATURATION(1));
	writel_relaxed(0x1, sunxi_nsi.base + IAG_MODE(1));

	/* G2D: limiter mode, limit to 500MB/s, sat as 5us, nsi clk 400MHz */
	writel_relaxed(0x140, sunxi_nsi.base + IAG_BAND_WIDTH(16));
	writel_relaxed(0x9c, sunxi_nsi.base + IAG_SATURATION(16));
	writel_relaxed(0x1, sunxi_nsi.base + IAG_MODE(16));
}

static void set_limit_for_old_version_720p(void)
{
	/* GPU: limiter mode, limit to 1800MB/s, sat as 5us, nsi clk 400MHz */
	writel_relaxed(0x480, sunxi_nsi.base + IAG_BAND_WIDTH(1));
	writel_relaxed(0x232, sunxi_nsi.base + IAG_SATURATION(1));
	writel_relaxed(0x1, sunxi_nsi.base + IAG_MODE(1));
}

static int sunxi_nsi_set_limit(struct device *dev)
{
	int ret;
	struct device_node *disp_np, *dram_np;
	unsigned int ic_version, fb0_width, fb0_height, dram_type;

	disp_np = of_find_node_by_name(NULL, "disp");
	ret = of_property_read_u32(disp_np, "fb0_width", &fb0_width);
	if (ret) {
		dev_err(dev, "get fb0_width failed\n");
		return -EINVAL;
	}
	of_property_read_u32(disp_np, "fb0_height", &fb0_height);
	if (ret) {
		dev_err(dev, "get fb0_height failed\n");
		return -EINVAL;
	}
	dram_np = of_find_node_by_name(NULL, "dram");
	/* dram_para[01] --> dram_type */
	ret = of_property_read_u32(dram_np, "dram_para[01]", &dram_type);
	if (ret) {
		ret = of_property_read_u32(dram_np, "dram_para01", &dram_type);
		if (ret) {
			dev_err(dev, "get dram_type failed\n");
			return -EINVAL;
		}
	}
	/*
	unsigned int dram_clk;

	of_property_read_u32(dram_np, "dram_para[00]", &dram_clk);
	if (ret) {
		dev_err(dev, "get dram_clk failed\n");
		return -EINVAL;
	}
	*/

	ic_version = sunxi_get_soc_ver() & 0x7;
	if (ic_version == 0 || ic_version == 0x3 || ic_version == 0x4) {
		if (fb0_width >= 1080 && fb0_height >= 1080) {
			set_limit_for_old_version();
			dev_info(dev, "set limit to fit 1080P(ABCDE)\n");
		} else {
			set_limit_for_old_version_720p();
			dev_info(dev, "set limit to fit 720P(ABCDE)\n");
		}
	} else {
		if (fb0_width >= 1080 && fb0_height >= 1080 && dram_type == 8) {
			set_limit_for_old_version();
			dev_info(dev, "set limit to fit 1080P-LP4(F)\n");
		} else {
			dev_info(dev, "no limit for F version(except 1080P-LP4)\n");
		}
	}

	return 0;
}

static int sunxi_nsi_unset_limit(struct device *dev)
{
	unsigned int ic_version;

	ic_version = sunxi_get_soc_ver() & 0x7;
	/* always set limit for ABCDE */
	if (ic_version != 0 && ic_version != 0x3 && ic_version != 0x4) {
		/* disable limit for GPU/G2D */
		writel_relaxed(0x0, sunxi_nsi.base + IAG_MODE(1));
		writel_relaxed(0x0, sunxi_nsi.base + IAG_MODE(16));
		dev_info(dev, "unset limit\n");
	}

	return 0;
}
#elif IS_ENABLED(CONFIG_ARCH_SUN55IW3)
static void __cpu_bw_enable(int enable)
{
	if (enable) {
		limit_mask_enable = 1;
		nsi_set_cpu_rw_bw_en(limit_mask, 1);
	} else {
		limit_mask_enable = 0;
		nsi_set_cpu_rw_bw_en(0x7, 0);
	}
}
static int sunxi_nsi_set_limit(struct device *dev)
{
	__cpu_bw_enable(1);
	return 0;
}
static int sunxi_nsi_unset_limit(struct device *dev)
{
	__cpu_bw_enable(0);
	return 0;
}
#else
static int sunxi_nsi_set_limit(struct device *dev)
{
	return -ENODEV;
}
static int sunxi_nsi_unset_limit(struct device *dev)
{
	return -ENODEV;
}
#endif

static int __load_master_clk_mapping(struct device *dev)
{
	struct device_node *np = dev->of_node;
	uint32_t clk_array[(MBUS_PMU_TAG + 1) * 2];
	uint32_t clks_count;
	uint32_t count;
	int i;

	/* 1. load possible clocks */
	clks_count = of_property_count_strings(np, "clock-names");

	sunxi_nsi.clks =
		devm_kzalloc(dev, sizeof(void *) * clks_count, GFP_KERNEL);
	if (!sunxi_nsi.clks)
		return -ENOMEM;

	for (i = 0; i < clks_count; i++) {
		const char *name;
		of_property_read_string_index(np, "clock-names", i, &name);
		sunxi_nsi.clks[i] = devm_clk_get(dev, name);
		if (IS_ERR_OR_NULL(sunxi_nsi.clks[i]))
			return -1;
	}

	/* 2. load master <-> clock mapping */
	/* 2.1 default clk -- mclk aka mbus */
	sunxi_nsi.clk_idx = devm_kzalloc(
		dev,
		sizeof(*sunxi_nsi.clk_idx) * (MBUS_PMU_TAG + 1 /* for TA1 */),
		GFP_KERNEL);
	if (!sunxi_nsi.clk_idx)
		return -ENOMEM;

	for_each_ports(i) {
		sunxi_nsi.clk_idx[i] = 1; /*  */
	}
	/* 2.2 load overlay */
	count = of_property_read_variable_u32_array(
		np, "master_clks", clk_array, 0, (MBUS_PMU_TAG + 1) * 2);
	if (count <= 1) {
		return -1;
	}
	for (i = 0; i < count; i += 2) {
		sunxi_nsi.clk_idx[clk_array[i]] = clk_array[i + 1];
	}

#ifdef TEST_DUMP
	//test dump clk list
	dev_err(dev, "IA clks:\n");
	for_each_ports(i) {
		const char *name;
		of_property_read_string_index(np, "clock-names",
					      sunxi_nsi.clk_idx[i], &name);
		dev_err(dev, "%s: %s \n", get_name(i), name);
	}
#endif
	return 0;
}

static int nsi_topo_type_process(struct device *dev)
{
	int ret;
	u32 topo_type;
	struct device_node *np = dev->of_node;
	ret = of_property_read_u32(np, "topology_type", &topo_type);
	if (ret) {
		/* not set, use default value for backward compability */
		topo_type = 0;
	}

	sunxi_nsi.topo_tpye = topo_type;
	switch (sunxi_nsi.topo_tpye) {
	case NSI_TOPO_V2: {
		/* nsi with v2 topo may assign any clock to any master, load that mapping */
		ret = __load_master_clk_mapping(dev);
		if (ret) {
			return ret;
		}

	} break;
	default:
		pr_err("no topo process for topo type:%d\n", topo_type);
		break;
	}
	return 0;
}

static int nsi_clk_path_process(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct clk *mclk;
	struct clk *pclk;
	unsigned int bus_rate, ret;
	u32 clk_path;

	ret = of_property_read_u32(np, "clk_path_type", &clk_path);
	if (ret) {
		/* not set, use default value for backward compability */
		clk_path = NSI_CLK_PATH_V0;
	}

	sunxi_nsi.clk_path_type = clk_path;
	switch (clk_path) {
	case NSI_CLK_PATH_V1:
		/* nsi with clk path v1 have dedicate clock, prepare that too */
		mclk = devm_clk_get(dev, "nsi_cfg");
		if (IS_ERR_OR_NULL(mclk)) {
			pr_err("Unable to acquire nsi_cfg clock, return %x\n",
			       PTR_ERR_OR_ZERO(mclk));
			return -1;
		}

		mclk = devm_clk_get(dev, "nsi");
		if (IS_ERR_OR_NULL(mclk)) {
			pr_err("Unable to acquire nsi clock, return %x\n",
			       PTR_ERR_OR_ZERO(mclk));
			return -1;
		}

		pclk = devm_clk_get(dev, "nsip");
		if (IS_ERR_OR_NULL(pclk)) {
			pr_err("Unable to acquire pll clock, return %x\n",
			       PTR_ERR_OR_ZERO(mclk));
			return -1;
		}

		ret = of_property_read_u32_index(np, "clock-frequency", 1,
						 &bus_rate);
		if (ret) {
			pr_err("Get clock-frequency property failed\n");
			return -1;
		}

		ret = clk_set_parent(mclk, pclk);
		if (ret != 0) {
			pr_err("clk_set_parent() failed! return\n");
			return -1;
		}

		sunxi_nsi.rate = clk_round_rate(mclk, bus_rate);

		if (clk_set_rate(mclk, sunxi_nsi.rate)) {
			pr_err("clk_set_rate failed\n");
			return -1;
		}

		sunxi_nsi.resets = devm_reset_control_array_get_shared(dev);
		if (IS_ERR(sunxi_nsi.resets)) {
			dev_err(dev, "get resets for v1 path failed %ld\n",
				PTR_ERR(sunxi_nsi.resets));
			return PTR_ERR(sunxi_nsi.resets);
		}

		ret = reset_control_deassert(sunxi_nsi.resets);
		if (ret) {
			dev_err(dev, "deasser reset for v1 path failed %d\n",
				ret);
			return ret;
		}

		if (clk_prepare_enable(mclk)) {
			pr_err("Couldn't enable nsi clock\n");
			return -EBUSY;
		}
		break;
	default:
		pr_err("no topo process for clk path type:%d\n", clk_path);
		break;
	}

	return 0;
}

static int nsi_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;
	struct resource res;
	struct clk *mclk;
	struct clk *pclk;
	struct reset_control *reset;
	unsigned int bus_rate;

	if (!np)
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res);
	if (!ret)
		sunxi_nsi.base = ioremap(res.start, resource_size(&res));

	if (ret || !sunxi_nsi.base) {
		WARN(1, "unable to ioremap nsi ctrl\n");
		return -ENXIO;
	}
#ifdef AW_NSI_CPU_CHANNEL
	ret = of_address_to_resource(np, 1, &res);
	if (!ret)
		sunxi_nsi.cpu_base = ioremap(res.start, resource_size(&res));

	if (ret || !sunxi_nsi.cpu_base) {
		WARN(1, "unable to ioremap nsi_cpu ctrl\n");
		return -ENXIO;
	}
#endif
	reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR_OR_NULL(reset)) {
		pr_err("Unable to get reset, return %x\n",
		PTR_ERR_OR_ZERO(reset));
		return -1;
	}

	mclk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR_OR_NULL(mclk)) {
		pr_err("Unable to acquire nsi clock, return %x\n",
		PTR_ERR_OR_ZERO(mclk));
		return -1;
	}

	pclk = devm_clk_get(&pdev->dev, "pll");
	if (IS_ERR_OR_NULL(pclk)) {
		pr_err("Unable to acquire pll clock, return %x\n",
		PTR_ERR_OR_ZERO(mclk));
		return -1;
	}

	ret = of_property_read_u32(np, "clock-frequency", &bus_rate);
	if (ret) {
		pr_err("Get clock-frequency property failed\n");
		return -1;
	}

	ret = clk_set_parent(mclk, pclk);
	if (ret != 0) {
		pr_err("clk_set_parent() failed! return\n");
		return -1;
	}

	sunxi_nsi.rate = clk_round_rate(mclk, bus_rate);

	if (clk_set_rate(mclk, sunxi_nsi.rate)) {
		pr_err("clk_set_rate failed\n");
		return -1;
	}

	sunxi_nsi.mclk = mclk;
	sunxi_nsi.pclk = pclk;
	sunxi_nsi.reset = reset;

	if (reset_control_deassert(sunxi_nsi.reset)) {
		pr_err("Couldnt reset control deassert\n");
		return -EBUSY;
	}

	if (clk_prepare_enable(mclk)) {
		pr_err("Couldn't enable nsi clock\n");
		return -EBUSY;
	}

	ret = nsi_clk_path_process(&pdev->dev);
	if (ret) {
		return ret;
	}
	ret = nsi_topo_type_process(&pdev->dev);
	if (ret) {
		return ret;
	}

	return 0;
}


static int nsi_init(struct platform_device *pdev)
{
	if (nsi_init_status != -EAGAIN)
		return nsi_init_status;

	if (nsi_init_status == -EAGAIN)
		nsi_init_status = nsi_probe(pdev);

#if IS_ENABLED(CONFIG_ARCH_SUN50IW10)
	sunxi_nsi_set_limit(&pdev->dev);
#endif

	return nsi_init_status;
}

/*
static void nsi_exit(void)
{
	mutex_lock(&nsi_proing);
	nsi_init_status = nsi_remove();
	mutex_unlock(&nsi_proing);

	nsi_init_status = -EAGAIN;
}
*/

static ssize_t nsi_pmu_bandwidth_show(struct device *dev,
			struct device_attribute *da, char *buf)
{
	unsigned long bread, bwrite, bandrw[MBUS_PMU_IAG_MAX];
	unsigned long bwtotal = 0;
#ifdef AW_NSI_CPU_CHANNEL
	unsigned long cpu_bread, cpu_bwrite, cpu_total;
#endif
	unsigned int port, len = 0;
	unsigned long flags = 0;
	char bwbuf[16];

	spin_lock_irqsave(&hw_nsi_pmu.bwlock, flags);

	/* read the iag pmu bandwidth, which is total master bandwidth */
#ifdef AW_NSI_CPU_CHANNEL
	cpu_bread = readl_relaxed(sunxi_nsi.cpu_base + CPU_CHL0_PMU_DATA_R);
	cpu_bwrite = readl_relaxed(sunxi_nsi.cpu_base + CPU_CHL0_PMU_DATA_W);

	cpu_total = cpu_bread + cpu_bwrite;
	len += sprintf(bwbuf, "%lu  ", cpu_total/1024);
	strcat(buf, bwbuf);
#endif
	/* read the iag pmu bandwidth, which is total master bandwidth */
	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		bread = readl_relaxed(sunxi_nsi.base + MBUS_PMU_DT_RD(port));
		bwrite = readl_relaxed(sunxi_nsi.base + MBUS_PMU_DT_WR(port));
		bandrw[port] = bread + bwrite;
		len += sprintf(bwbuf, "%lu  ", bandrw[port]/1024);
		strcat(buf, bwbuf);
	}

#ifdef CONFIG_ARCH_SUN55I
	for (port = 0; port < MBUS_PMU_IAG_MAX; port++)
		bwtotal += bandrw[port];
#else
	/* read the tag pmu bandwidth, which is total ddr bandwidth */
	bread = readl_relaxed(sunxi_nsi.base + MBUS_PMU_DT_RD(MBUS_PMU_TAG));
	bwrite = readl_relaxed(sunxi_nsi.base + MBUS_PMU_DT_WR(MBUS_PMU_TAG));
	bwtotal = bread + bwrite;

	if (sunxi_nsi.topo_tpye == NSI_TOPO_V2) {
		bread = readl_relaxed(sunxi_nsi.base + MBUS_PMU_DT_RD(MBUS_PMU_TAG + 1));
		bwrite = readl_relaxed(sunxi_nsi.base + MBUS_PMU_DT_WR(MBUS_PMU_TAG + 1));
		bwtotal += bread + bwrite;
	}
#endif

#ifdef AW_NSI_CPU_CHANNEL
	len += sprintf(bwbuf, "%lu\n", (cpu_total + bwtotal)/1024);
#else
	len += sprintf(bwbuf, "%lu\n", (bwtotal) / 1024);
#endif
	strcat(buf, bwbuf);

	spin_unlock_irqrestore(&hw_nsi_pmu.bwlock, flags);
	return len;
}

static ssize_t nsi_pmu_latency_show(struct device *dev,
			struct device_attribute *da, char *buf)
{
	unsigned long laread, lawrite, larw[MBUS_PMU_IAG_MAX];
	unsigned long request;
	unsigned int port, len = 0;
	unsigned long flags = 0;
	char labuf[16];

	spin_lock_irqsave(&hw_nsi_pmu.bwlock, flags);

	/* read the iag pmu latency and request */
	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		laread = readl_relaxed(sunxi_nsi.base + MBUS_PMU_LA_RD(port));
		lawrite = readl_relaxed(sunxi_nsi.base + MBUS_PMU_LA_WR(port));
		request = readl_relaxed(sunxi_nsi.base + MBUS_PMU_RQ_RD(port)) +
						+ readl_relaxed(sunxi_nsi.base + MBUS_PMU_RQ_WR(port));

		if (request == 0) {
			larw[port] = 0;
		} else {
			larw[port] = (laread + lawrite) / request;
			latency_sum[port] += (laread + lawrite);
			request_sum[port] += request;
		}
	}
	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		len += sprintf(labuf, "%lu  ", larw[port]);
		strcat(buf, labuf);
	}

	/* read the tag pmu latency and request, which is total ddr latency and request */
	laread = readl_relaxed(sunxi_nsi.base + MBUS_PMU_LA_RD(MBUS_PMU_TAG));
	lawrite = readl_relaxed(sunxi_nsi.base + MBUS_PMU_LA_WR(MBUS_PMU_TAG));
	request = readl_relaxed(sunxi_nsi.base + MBUS_PMU_RQ_RD(MBUS_PMU_TAG)) +
						+ readl_relaxed(sunxi_nsi.base + MBUS_PMU_RQ_WR(MBUS_PMU_TAG));

	if (sunxi_nsi.topo_tpye == NSI_TOPO_V2) {
		laread += readl_relaxed(sunxi_nsi.base + MBUS_PMU_LA_RD(MBUS_PMU_TAG + 1));
		lawrite += readl_relaxed(sunxi_nsi.base + MBUS_PMU_LA_WR(MBUS_PMU_TAG + 1));
		request += readl_relaxed(sunxi_nsi.base + MBUS_PMU_RQ_RD(MBUS_PMU_TAG + 1)) +
			   +readl_relaxed(sunxi_nsi.base + MBUS_PMU_RQ_WR(MBUS_PMU_TAG + 1));
	}

	len += sprintf(labuf, "%lu\n", (laread + lawrite) / request);
	strcat(buf, labuf);
	latency_sum[port] += (laread + lawrite);
	request_sum[port] += request;

	spin_unlock_irqrestore(&hw_nsi_pmu.bwlock, flags);
	return len;
}

static ssize_t nsi_pmu_latency_aver_show(struct device *dev,
			struct device_attribute *da, char *buf)
{
	unsigned int port, len = 0;
	unsigned long flags = 0;
	char labuf[32];

	spin_lock_irqsave(&hw_nsi_pmu.bwlock, flags);

	for (port = 0; port < MBUS_PMU_IAG_MAX; port++) {
		len += sprintf(labuf, "%lu  ", latency_sum[port] / request_sum[port]);
		strcat(buf, labuf);
	}
	len += sprintf(labuf, "%lu\n", latency_sum[port] / request_sum[port]);
	strcat(buf, labuf);

	spin_unlock_irqrestore(&hw_nsi_pmu.bwlock, flags);
	return len;
}


static ssize_t nsi_available_pmu_show(struct device *dev,
			struct device_attribute *da, char *buf)
{
	ssize_t i, len = 0;

	for (i = 0; i < ARRAY_SIZE(pmu_name); i++)
		len += sprintf(buf + len, "%s  ", get_name(i));

	len += sprintf(buf + len, "\n");

	return len;
}

static unsigned int nsi_get_value(struct nsi_pmu_data *data,
				   unsigned int index, char *buf)
{
	unsigned int i, size = 0;
	unsigned long value;
	__maybe_unused unsigned long mrate, drate;
	unsigned int bw_val;
#ifdef AW_NSI_CPU_CHANNEL
	char name[3][6] = {"total", "read", "write"};
#endif

	switch (index) {
	case MBUS_PORT_MODE:
		for_each_ports(i) {
#ifdef AW_NSI_CPU_CHANNEL
			if (i == 0)
				value = readl_relaxed(sunxi_nsi.cpu_base +
						CPU_IAG_MODE);
			else
#endif
			{
#ifdef AW_NSI_CPU_CHANNEL
				value = readl_relaxed(sunxi_nsi.base + IAG_MODE(i - 1));
#else
				value = readl_relaxed(sunxi_nsi.base + IAG_MODE(i));
#endif
			}

			size += sprintf(buf + size, "master[%2d]:%-8s qos_mode:%lu\n",
					i, get_name(i), (value & 0x3));
		}
		break;

	case MBUS_PORT_PRI:
		for_each_ports(i) {
#ifdef AW_NSI_CPU_CHANNEL
			if (i == 0)
				value = readl_relaxed(sunxi_nsi.cpu_base +
						CPU_PRI_CFG);
			else
#endif
			{
#ifdef AW_NSI_CPU_CHANNEL
				value = readl_relaxed(sunxi_nsi.base + IAG_PRI_CFG(i - 1));
#else
				value = readl_relaxed(sunxi_nsi.base + IAG_PRI_CFG(i));
#endif
			}

			size += sprintf(buf + size, "master[%2d]:%-8s read_prio:%lu write_prio:%lu\n",
					i, get_name(i), (value >> 2), (value & 0x3));
		}
		break;

	case MBUS_PORT_QOS:
		for_each_ports(i) {
#ifdef AW_NSI_CPU_CHANNEL
			if (i == 0)
				value = readl_relaxed(sunxi_nsi.cpu_base +
						CPU_QOS_CFG);
			else
#endif
			{
#ifdef AW_NSI_CPU_CHANNEL
				value = readl_relaxed(sunxi_nsi.base + IAG_QOS_CFG(i - 1));
#else
				value = readl_relaxed(sunxi_nsi.base + IAG_QOS_CFG(i));
#endif
			}

			size += sprintf(buf + size, "master[%2d]:%-8s qos(0-lpr 1-hpr):%lu \n",
					i, get_name(i), (value >> ((i % 16) * 2)) & 0x3);
		}
		break;

	case MBUS_INPUT_OUTPUT:
		for_each_ports(i) {
#ifdef AW_NSI_CPU_CHANNEL
			if (i == 0)
				value = readl_relaxed(sunxi_nsi.cpu_base +
						CPU_INPUT_OUTPUT_CFG);
			else
#endif
			{
#ifdef AW_NSI_CPU_CHANNEL
				value = readl_relaxed(sunxi_nsi.base + IAG_INPUT_OUTPUT_CFG(i - 1));
#else
				value = readl_relaxed(sunxi_nsi.base + IAG_INPUT_OUTPUT_CFG(i));
#endif
			}

			size += sprintf(buf + size, "master[%2d]:%-8s qos_sel(0-out 1-input):%lu\n",
					i, get_name(i), (value & 1));
		}
		break;

	case MBUS_PORT_ABS_BWL:
		mrate = clk_get_rate(sunxi_nsi.mclk);
		drate = clk_get_rate(sunxi_nsi.pclk) / 4;

		for_each_ports(i) {
#ifdef AW_NSI_CPU_CHANNEL
			if (i == 0)
				bw_val = readl_relaxed(sunxi_nsi.cpu_base +
						CPU_BAND_WIDTH_LIMIT(0));
			else
#endif
			{
#ifdef AW_NSI_CPU_CHANNEL
				bw_val = readl_relaxed(sunxi_nsi.base + IAG_BAND_WIDTH(i - 1));
#else
				bw_val = readl_relaxed(sunxi_nsi.base + IAG_BAND_WIDTH(i));
#endif
			}

			if (i == 0)
				value = bw_val * (drate / 1000000) / 256;
			else
				value = bw_val * (mrate / 1000000) / 256;
			if ((value % 100) == 99)
				value += 1;
			size += sprintf(buf + size, "master[%2d]:%-8s BWLimit(MB/s):%lu \n",
					i, get_name(i), value);
		}
		break;

	case MBUS_PORT_ABS_BWLEN:
		for_each_ports(i) {
#ifdef AW_NSI_CPU_CHANNEL
			if (i == 0)
				value = readl_relaxed(sunxi_nsi.cpu_base +
						CPU_IAG_MODE);
			else
#endif
			{
#ifdef AW_NSI_CPU_CHANNEL
				value = readl_relaxed(sunxi_nsi.base + IAG_MODE(i - 1));
#else
				value = readl_relaxed(sunxi_nsi.base + IAG_MODE(i));
#endif
			}

			size += sprintf(buf + size, "master[%2d]:%-8s BWLimit_en:%lu \n",
					i, get_name(i), value);
		}
		break;

#ifdef AW_NSI_CPU_CHANNEL
	case CPU_ABS_RW:
		drate = clk_get_rate(sunxi_nsi.pclk) / 4;

		for (i = 0; i < 3; i++) {
			bw_val = readl_relaxed(sunxi_nsi.cpu_base + CPU_BAND_WIDTH_LIMIT(i));

			value = bw_val * (drate / 1000000) / 256;

			if ((value % 100) == 99)
				value += 1;

			size += sprintf(buf + size, "(%d) CPU %s bandwidth limit: %lu(MB/s) \n", i, name[i], value);
		}

		break;
#endif

	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		value = 0;
		break;

	}

	return size;
}

static ssize_t nsi_show_value(struct device *dev,
			       struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	unsigned int len = 0;

	if (attr->index >= MBUS_PMU_MAX) {
		len = nsi_get_value(&hw_nsi_pmu, attr->index, buf);
		len = (len < PAGE_SIZE) ? len : PAGE_SIZE;
	}

	return len;
/*
	return snprintf(buf, PAGE_SIZE, "%u\n",
			nsi_update_device(&hw_nsi_pmu, attr->index));
*/
}

static unsigned int nsi_set_value(struct nsi_pmu_data *data, unsigned int index,
				   enum nsi_pmu port, unsigned int val)
{
	unsigned int value;

	switch (index) {
	case MBUS_PORT_MODE:
		nsi_port_setmode(port, val);
		break;
	case MBUS_PORT_PRI:
		nsi_port_setpri(port, val);
		break;
	case MBUS_PORT_QOS:
		nsi_port_setqos(port, val);
		break;
	case MBUS_INPUT_OUTPUT:
		nsi_port_setio(port, val);
		break;
	case MBUS_PORT_ABS_BWL:
		nsi_port_set_abs_bwl(port, val);
		break;
	case MBUS_PORT_ABS_BWLEN:
		nsi_port_set_abs_bwlen(port, val);
		break;
#ifdef AW_NSI_CPU_CHANNEL
	case CPU_ABS_RW:
		nsi_set_cpu_rw_bwl((unsigned int)port, val);
		break;
#endif
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		value = 0;
		break;
	}

	return 0;
}

static ssize_t nsi_store_value(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	unsigned long port, val;
	unsigned char buffer[64];
	unsigned char *pbuf, *pbufi;
	int err;

	if (strlen(buf) >= 64) {
		dev_err(dev, "arguments out of range!\n");
		return -EINVAL;
	}

	while (*buf == ' ') /* find the first unblank character */
		buf++;
	strncpy(buffer, buf, strlen(buf));

	pbufi = buffer;
	while (*pbufi != ' ') /* find the first argument */
		pbufi++;
	*pbufi = 0x0;
	pbuf = (unsigned char *)buffer;
	err = kstrtoul(pbuf, 10, &port);
	if (err < 0)
		return err;
	if (port >= MBUS_PMU_MAX) {
		dev_err(dev, "master is illegal\n");
		return -EINVAL;
	}

	pbuf = ++pbufi;
	while (*pbuf == ' ') /* remove extra space character */
		pbuf++;
	pbufi = pbuf;
	while ((*pbufi != ' ') && (*pbufi != '\n'))
		pbufi++;
	*pbufi = 0x0;

	err = kstrtoul(pbuf, 10, &val);
	if (err < 0)
		return err;

	nsi_set_value(&hw_nsi_pmu, nr,
		(enum nsi_pmu)port, (unsigned int)val);

	return count;
}


/* get all masters' mode or set a master's mode */
static SENSOR_DEVICE_ATTR(port_mode, 0644,
			  nsi_show_value, nsi_store_value, MBUS_PORT_MODE);
/* get all masters' prio or set a master's prio */
static SENSOR_DEVICE_ATTR(port_prio, 0644,
			  nsi_show_value, nsi_store_value, MBUS_PORT_PRI);
/* get all masters' prio or set a master's qos(hpr/lpr) */
static SENSOR_DEVICE_ATTR(port_qos, 0644,
			  nsi_show_value, nsi_store_value, MBUS_PORT_QOS);
/* get all masters' inout or set a master's inout */
static SENSOR_DEVICE_ATTR(port_select, 0644,
			  nsi_show_value, nsi_store_value, MBUS_INPUT_OUTPUT);
static SENSOR_DEVICE_ATTR(port_abs_bwl, 0644,
			  nsi_show_value, nsi_store_value, MBUS_PORT_ABS_BWL);
static SENSOR_DEVICE_ATTR(port_abs_bwlen, 0644,
			  nsi_show_value, nsi_store_value, MBUS_PORT_ABS_BWLEN);

#ifdef AW_NSI_CPU_CHANNEL
static SENSOR_DEVICE_ATTR(cpu_rw_bwl, 0644,
			  nsi_show_value, nsi_store_value, CPU_ABS_RW);
#endif

/* get all masters' sample timer or set a master's timer */
static struct device_attribute dev_attr_pmu_timer =
	__ATTR(pmu_timer, 0644, nsi_pmu_timer_show, nsi_pmu_timer_store);
static struct device_attribute dev_attr_pmu_bandwidth =
	__ATTR(pmu_bandwidth, 0444, nsi_pmu_bandwidth_show, NULL);
static struct device_attribute dev_attr_available_pmu =
	__ATTR(available_pmu, 0444, nsi_available_pmu_show, NULL);
static struct device_attribute dev_attr_pmu_latency =
	__ATTR(pmu_latency, 0444, nsi_pmu_latency_show, NULL);
static struct device_attribute dev_attr_pmu_latency_aver =
	__ATTR(pmu_latency_aver, 0444, nsi_pmu_latency_aver_show, NULL);

/* pointers to created device attributes */
static struct attribute *nsi_attributes[] = {
	&dev_attr_pmu_timer.attr,
	&dev_attr_pmu_bandwidth.attr,
	&dev_attr_available_pmu .attr,
	&dev_attr_pmu_latency.attr,
	&dev_attr_pmu_latency_aver.attr,

	&sensor_dev_attr_port_mode.dev_attr.attr,
	&sensor_dev_attr_port_prio.dev_attr.attr,
	&sensor_dev_attr_port_qos.dev_attr.attr,
	&sensor_dev_attr_port_select.dev_attr.attr,
	&sensor_dev_attr_port_abs_bwl.dev_attr.attr,
	&sensor_dev_attr_port_abs_bwlen.dev_attr.attr,
#ifdef AW_NSI_CPU_CHANNEL
	&sensor_dev_attr_cpu_rw_bwl.dev_attr.attr,
#endif
	NULL,
};

static struct attribute_group nsi_group = {
	.attrs = nsi_attributes,
};

static const struct attribute_group *nsi_groups[] = {
	&nsi_group,
	NULL,
};

static int nsi_pmu_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	nsi_init(pdev);
	sunxi_nsi.dev = dev;

	if (hw_nsi_pmu.dev_nsi)
		return 0;

	hw_nsi_pmu.dev_nsi = device_create_with_groups(nsi_pmu_class,
					dev, MKDEV(NSI_MAJOR, 0), NULL,
					nsi_groups, "hwmon%d", 0);

	if (IS_ERR(hw_nsi_pmu.dev_nsi)) {
		ret = PTR_ERR(hw_nsi_pmu.dev_nsi);
		goto out_err;
	}

	spin_lock_init(&hw_nsi_pmu.bwlock);

	return 0;

out_err:
	dev_err(&(pdev->dev), "probed failed\n");

	return ret;
}

static int nsi_pmu_remove(struct platform_device *pdev)
{
	if (hw_nsi_pmu.dev_nsi) {
		device_remove_groups(hw_nsi_pmu.dev_nsi, nsi_groups);
		device_destroy(nsi_pmu_class, MKDEV(NSI_MAJOR, 0));
		hw_nsi_pmu.dev_nsi = NULL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_PM)
static int sunxi_nsi_suspend(struct device *dev)
{
	dev_info(dev, "suspend okay\n");

	return 0;
}

static int sunxi_nsi_resume(struct device *dev)
{
#if IS_ENABLED(CONFIG_ARCH_SUN50IW10)
	sunxi_nsi_set_limit(dev);
#endif
#ifdef AW_NSI_CPU_CHANNEL
	int i;
	for (i = 0; i < 3; i++) {
		if (cpu_rw_settings[i])
			nsi_set_cpu_rw_bwl(i, cpu_rw_settings[i]);
	}
#endif

	dev_info(dev, "resume okay\n");
	return 0;
}

static const struct dev_pm_ops sunxi_nsi_pm_ops = {
	.suspend = sunxi_nsi_suspend,
	.resume = sunxi_nsi_resume,
};

#define SUNXI_MBUS_PM_OPS (&sunxi_nsi_pm_ops)
#else
#define SUNXI_MBUS_PM_OPS NULL
#endif

static struct platform_driver nsi_pmu_driver = {
	.driver = {
		.name   = DRIVER_NAME_PMU,
		.pm     = SUNXI_MBUS_PM_OPS,
		.of_match_table = sunxi_nsi_matches,
	},
	.probe = nsi_pmu_probe,
	.remove = nsi_pmu_remove,
};

struct sunxi_nsi_data {
	__u32 id;
	__u32 val;
};

enum NSI_IOCTL_CMD {
	IOCTL_UNKNOWN = 0x100,
	NSI_SET_LIMIT,
	NSI_UNSET_LIMIT,
	NSI_SET_MASTER_LIMIT,
	NSI_UNSET_MASTER_LIMIT,
};

static long nsi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct sunxi_nsi_data nsi_data;

	if (copy_from_user(&nsi_data, (void __user *)arg, sizeof(nsi_data)) != 0)
		return -EFAULT;

	switch (cmd) {
	case NSI_SET_LIMIT:
		sunxi_nsi_set_limit(sunxi_nsi.dev);
		break;
	case NSI_UNSET_LIMIT:
		sunxi_nsi_unset_limit(sunxi_nsi.dev);
		break;
#ifdef AW_NSI_CPU_CHANNEL
	case NSI_SET_MASTER_LIMIT:
		nsi_set_cpu_rw_bwl(nsi_data.id, nsi_data.val);
		break;
	case NSI_UNSET_MASTER_LIMIT:
		nsi_unset_cpu_rw_bwl(nsi_data.id);
		break;
#endif
	default:
		pr_err("nsi: err ioctl cmd %d, process %s\n", cmd, current->comm);
		return -ENOTTY;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long nsi_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long translated_arg = (unsigned long)compat_ptr(arg);

	return nsi_ioctl(file, cmd, translated_arg);

}
#endif

static const struct file_operations nsi_fops = {
	.owner	 = THIS_MODULE,
	.unlocked_ioctl	= nsi_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = nsi_compat_ioctl,
#endif
};

static int nsi_pmu_init(void)
{
	int ret;
	struct device *dev;

	ret = register_chrdev_region(MKDEV(NSI_MAJOR, 0), NSI_MINORS, "nsi");
	if (ret)
		goto out_err;

	cdev_init(&sunxi_nsi.cdev, &nsi_fops);
	sunxi_nsi.cdev.owner = THIS_MODULE;
	ret = cdev_add(&sunxi_nsi.cdev, MKDEV(NSI_MAJOR, 1), 1);
	if (ret) {
		pr_err("add cdev fail\n");
		goto out_err;
	}

	nsi_pmu_class = class_create(THIS_MODULE, "nsi-pmu");
	if (IS_ERR(nsi_pmu_class)) {
		ret = PTR_ERR(nsi_pmu_class);
		goto out_err;
	}

	dev = device_create(nsi_pmu_class, NULL, MKDEV(NSI_MAJOR, 1), NULL,
			"nsi");
	if (IS_ERR(dev)) {
		pr_err("device_create failed!\n");
		goto out_err;
	}

	ret = platform_driver_register(&nsi_pmu_driver);
	if (ret) {
		pr_err("register sunxi nsi platform driver failed\n");
		goto drv_err;
	}

	return ret;

drv_err:
	platform_driver_unregister(&nsi_pmu_driver);
out_err:
	unregister_chrdev_region(MKDEV(NSI_MAJOR, 0), NSI_MINORS);
	return ret;
}

static void nsi_pmu_exit(void)
{
	cdev_del(&sunxi_nsi.cdev);
	platform_driver_unregister(&nsi_pmu_driver);
	class_destroy(nsi_pmu_class);
	unregister_chrdev_region(MKDEV(NSI_MAJOR, 0), NSI_MINORS);
}

device_initcall(nsi_pmu_init);
module_exit(nsi_pmu_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SUNXI NSI support");
MODULE_AUTHOR("huangshuosheng");
MODULE_VERSION("1.1.0");