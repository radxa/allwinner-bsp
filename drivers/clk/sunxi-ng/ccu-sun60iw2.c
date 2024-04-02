// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (c) 2023 rengaomin@allwinnertech.com
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_mult.h"
#include "ccu_nk.h"
#include "ccu_nkm.h"
#include "ccu_nkmp.h"
#include "ccu_nm.h"

#include "ccu-sun60iw2.h"
/* ccu_des_start */

#define SUN60IW2_PLL_REF_CTRL_REG   0x0000
static struct ccu_nkmp pll_ref_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(16, 8), /* output divider */
	.common		= {
		.reg		= 0x0000,
		.hw.init	= CLK_HW_INIT("refpll", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_DDR_CTRL_REG   0x0020
static struct ccu_nkmp pll_ddr_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 2), /* output divider */
	.common		= {
		.reg		= 0x0020,
		.hw.init	= CLK_HW_INIT("ddrpll", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_PERI0_CTRL_REG   0x00A0
static SUNXI_CCU_NM_WITH_GATE_LOCK(pll_peri0_clk, "pll-peri0",
		"dcxo", 0x00A0,
		8, 7,	/* N */
		1, 1,	/* M */
		BIT(27),	/* gate */
		BIT(28),	/* lock */
		CLK_SET_RATE_UNGATE | CLK_IS_CRITICAL);

static SUNXI_CCU_M(pll_peri0_2x_clk, "pll-peri0-2x",
		"pll-peri0", 0x00A0,
		20, 3,
		CLK_SET_RATE_PARENT);

static CLK_FIXED_FACTOR_HW(pll_peri0_div3_clk, "pll-peri0-div3",
		&pll_peri0_2x_clk.common.hw,
		6, 1, 0);

static SUNXI_CCU_M(pll_peri0_800m_clk, "pll-peri0-800m",
		"pll-peri0", 0x00A0,
		16, 3,
		0);

static SUNXI_CCU_M(pll_peri0_480m_clk, "pll-peri0-480m",
		"pll-peri0", 0x00A0,
		2, 3,
		0);

static CLK_FIXED_FACTOR(pll_peri0_600m_clk, "pll-peri0-600m", "pll-peri0-2x", 2, 1, 0);
static CLK_FIXED_FACTOR(pll_peri0_400m_clk, "pll-peri0-400m", "pll-peri0-2x", 3, 1, 0);
static CLK_FIXED_FACTOR(pll_peri0_300m_clk, "pll-peri0-300m", "pll-peri0-600m", 2, 1, 0);
static CLK_FIXED_FACTOR(pll_peri0_200m_clk, "pll-peri0-200m", "pll-peri0-400m", 2, 1, 0);
static CLK_FIXED_FACTOR(pll_peri0_160m_clk, "pll-peri0-160m", "pll-peri0-480m", 3, 1, 0);
static CLK_FIXED_FACTOR(pll_peri0_150m_clk, "pll-peri0-150m", "pll-peri0-300m", 2, 1, 0);

#define SUN60IW2_PLL_PERI1_CTRL_REG   0x00C0
static SUNXI_CCU_NM_WITH_GATE_LOCK(pll_peri1_clk, "pll-peri1",
		"dcxo", 0x00C0,
		8, 7,	/* N */
		1, 1,	/* M */
		BIT(27),	/* gate */
		BIT(28),	/* lock */
		CLK_SET_RATE_UNGATE | CLK_IS_CRITICAL);

static SUNXI_CCU_M(pll_peri1_2x_clk, "pll-peri1-2x",
		"pll-peri1", 0x00C0,
		20, 3,
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M(pll_peri1_800m_clk, "pll-peri1-800m",
		"pll-peri1", 0x00C0,
		16, 3,
		0);

static SUNXI_CCU_M(pll_peri1_480m_clk, "pll-peri1-480m",
		"pll-peri1", 0x00C0,
		2, 3,
		0);

static CLK_FIXED_FACTOR(pll_peri1_600m_clk, "pll-peri1-600m", "pll-peri1-2x", 2, 1, 0);
static CLK_FIXED_FACTOR(pll_peri1_400m_clk, "pll-peri1-400m", "pll-peri1-2x", 3, 1, 0);
static CLK_FIXED_FACTOR(pll_peri1_300m_clk, "pll-peri1-300m", "pll-peri1-600m", 2, 1, 0);
static CLK_FIXED_FACTOR(pll_peri1_200m_clk, "pll-peri1-200m", "pll-peri1-400m", 2, 1, 0);
static CLK_FIXED_FACTOR(pll_peri1_160m_clk, "pll-peri1-160m", "pll-peri1-480m", 3, 1, 0);
static CLK_FIXED_FACTOR(pll_peri1_150m_clk, "pll-peri1-150m", "pll-peri1-300m", 2, 1, 0);

#define SUN60IW2_PLL_GPU0_CTRL_REG   0x00E0
static struct ccu_nkmp pll_gpu0_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 2), /* output divider */
	.common		= {
		.reg		= 0x00E0,
		.hw.init	= CLK_HW_INIT("gpu0pll", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_VIDEO0_CTRL_REG   0x0120
static struct ccu_nkmp pll_video0_4x = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0120,
		.hw.init	= CLK_HW_INIT("video0pll4x", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

static struct ccu_nkmp pll_video0_3x = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(16, 3), /* output divider */
	.common		= {
		.reg		= 0x0120,
		.hw.init	= CLK_HW_INIT("video0pll3x", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};
#define SUN60IW2_PLL_VIDEO1_CTRL_REG   0x0140
static struct ccu_nkmp pll_video1_4x = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0140,
		.hw.init	= CLK_HW_INIT("video1pll4x", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_VIDEO2_CTRL_REG   0x0160
static struct ccu_nkmp pll_video2_4x = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0160,
		.hw.init	= CLK_HW_INIT("video2pll4x", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_VE0_CTRL_REG   0x0220
static struct ccu_nkmp pll_ve0_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0220,
		.hw.init	= CLK_HW_INIT("ve0pll", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_VE1_CTRL_REG   0x0240
static struct ccu_nkmp pll_ve1_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0240,
		.hw.init	= CLK_HW_INIT("ve1pll", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_AUDIO0_CTRL_REG   0x0260
static struct ccu_nkmp pll_audio0_4x = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0260,
		.hw.init	= CLK_HW_INIT("audio0pll4x", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_AUDIO1_CTRL_REG   0x0280
static struct ccu_nkmp pll_audio1_div2 = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x0280,
		.hw.init	= CLK_HW_INIT("audio1pll-div2", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_NPU_CTRL_REG   0x02A0
static struct ccu_nkmp pll_npu_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x02A0,
		.hw.init	= CLK_HW_INIT("pll-npu", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

#define SUN60IW2_PLL_DE_CTRL_REG   0x02E0
static struct ccu_nkmp pll_de_4x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(20, 3), /* output divider */
	.common		= {
		.reg		= 0x02E0,
		.hw.init	= CLK_HW_INIT("depll4x", "dcxo",
				&ccu_nkmp_ops,
				CLK_SET_RATE_UNGATE |
				CLK_IS_CRITICAL),
	},
};

static const char * const apb0_parents[] = { "dcxo", "osc32k", "iosc", "pll-peri0-600m" };
SUNXI_CCU_M_WITH_MUX(ahb_clk, "ahb", apb0_parents,
		0x0500, 0, 5, 24, 2, CLK_SET_RATE_PARENT);

SUNXI_CCU_M_WITH_MUX(apb0_clk, "apb0", apb0_parents,
		0x0510, 0, 5, 24, 2, CLK_SET_RATE_PARENT);

SUNXI_CCU_M_WITH_MUX(apb1_clk, "apb1", apb0_parents,
		0x0518, 0, 5, 24, 2, CLK_SET_RATE_PARENT);

static const char * const trace_parents[] = { "pll-peri0-400m", "clk16m-rc", "peri0-300m", "dcxo", "clk32k" };

static SUNXI_CCU_M_WITH_MUX_GATE(trace_clk, "trace",
		trace_parents, 0x0540,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const gic_parents[] = { "pll-peri0-400m", "pll-peri0-600m", "pll-peri0-480m", "dcxo", "clk32k" };

static SUNXI_CCU_M_WITH_MUX_GATE(gic_clk, "gic",
		gic_parents, 0x0560,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const cpu_peri_parents[] = { "pll-peri0-400m", "pll-peri0-600m", "pll-peri0-480m", "dcxo", "clk-32k" };

static SUNXI_CCU_M_WITH_MUX_GATE(cpu_peri_clk, "cpu-peri",
		cpu_peri_parents, 0x0568,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(its_pcie0_aclk, "its-pcie0-aclk",
		"dcxo",
		0x0574, BIT(1), 0);

static SUNXI_CCU_GATE(its_pcie0_hclk, "its-pcie0-hclk",
		"dcxo",
		0x0574, BIT(0), 0);

static const char * const nsi_parents[] = { "pll-peri0-800m", "depll3x", "peri0-480m", "peri0-600m", "ddrpll", "dcxo" };

static SUNXI_CCU_M_WITH_MUX_GATE(nsi_clk, "nsi",
		nsi_parents, 0x0580,
		0, 5,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(nsi_cfg_clk, "nsi-cfg",
		"dcxo",
		0x0584, BIT(0), 0);

static const char * const mbus_parents[] = { "pll-npu", "ddrpll", "peri0-480m", "dcxo", "peri0-600m" };

static SUNXI_CCU_M_WITH_MUX_GATE(mbus_clk, "mbus",
		mbus_parents, 0x0588,
		0, 5,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(iommu0_sys_hclk, "iommu0-sys-hclk",
		"dcxo",
		0x058C, BIT(2), 0);

static SUNXI_CCU_GATE(iommu0_sys_pclk, "iommu0-sys-pclk",
		"dcxo",
		0x058C, BIT(1), 0);

static SUNXI_CCU_GATE(iommu0_sys_mclk, "iommu0-sys-mclk",
		"dcxo",
		0x058C, BIT(0), 0);

static SUNXI_CCU_GATE(msi_lite0_clk, "msi-lite0",
		"dcxo",
		0x0594, BIT(0), 0);

static SUNXI_CCU_GATE(msi_lite1_clk, "msi-lite1",
		"dcxo",
		0x059C, BIT(0), 0);

static SUNXI_CCU_GATE(msi_lite2_clk, "msi-lite2",
		"dcxo",
		0x05A4, BIT(0), 0);

static SUNXI_CCU_GATE(iommu1_sys_hclk, "iommu1-sys-hclk",
		"dcxo",
		0x05B4, BIT(2), 0);

static SUNXI_CCU_GATE(iommu1_sys_pclk, "iommu1-sys-pclk",
		"dcxo",
		0x05B4, BIT(1), 0);

static SUNXI_CCU_GATE(iommu1_sys_mclk, "iommu1-sys-mclk",
		"dcxo",
		0x05B4, BIT(0), 0);

static SUNXI_CCU_GATE(cpus_hclk_gate_clk, "cpus-hclk-gate",
		"dcxo",
		0x05C0, BIT(28), 0);

static SUNXI_CCU_GATE(store_ahb_gate_clk, "store-ahb-gate",
		"dcxo",
		0x05C0, BIT(24), 0);

static SUNXI_CCU_GATE(msilite0_ahb_gate_clk, "msilite0-ahb-gate",
		"dcxo",
		0x05C0, BIT(16), 0);

static SUNXI_CCU_GATE(usb_sys_ahb_gate_clk, "usb-sys-ahb-gate",
		"dcxo",
		0x05C0, BIT(9), 0);

static SUNXI_CCU_GATE(serdes_ahb_gate_clk, "serdes-ahb-gate",
		"dcxo",
		0x05C0, BIT(8), 0);

static SUNXI_CCU_GATE(gpu0_ahb_gate_clk, "gpu0-ahb-gate",
		"dcxo",
		0x05C0, BIT(7), 0);

static SUNXI_CCU_GATE(npu_ahb_gate_clk, "npu-ahb-gate",
		"dcxo",
		0x05C0, BIT(6), 0);

static SUNXI_CCU_GATE(de_ahb_gate_clk, "de-ahb-gate",
		"dcxo",
		0x05C0, BIT(5), 0);

static SUNXI_CCU_GATE(vid_out1_ahb_gate_clk, "vid-out1-ahb-gate",
		"dcxo",
		0x05C0, BIT(4), 0);

static SUNXI_CCU_GATE(vid_out0_ahb_gate_clk, "vid-out0-ahb-gate",
		"dcxo",
		0x05C0, BIT(3), 0);

static SUNXI_CCU_GATE(vid_in_ahb_gate_clk, "vid-in-ahb-gate",
		"dcxo",
		0x05C0, BIT(2), 0);

static SUNXI_CCU_GATE(ve_ahb_gate_clk, "ve-ahb-gate",
		"dcxo",
		0x05C0, BIT(0), 0);

static SUNXI_CCU_GATE(msilite2_mbus_gate_clk, "msilite2-mbus-gate",
		"dcxo",
		0x05E0, BIT(31), 0);

static SUNXI_CCU_GATE(store_mbus_gate_clk, "store-mbus-gate",
		"dcxo",
		0x05E0, BIT(30), 0);

static SUNXI_CCU_GATE(msilite0_mbus_gate_clk, "msilite0-mbus-gate",
		"dcxo",
		0x05E0, BIT(29), 0);

static SUNXI_CCU_GATE(serdes_mbus_gate_clk, "serdes-mbus-gate",
		"dcxo",
		0x05E0, BIT(28), 0);

static SUNXI_CCU_GATE(vid_in_mbus_gate_clk, "vid-in-mbus-gate",
		"dcxo",
		0x05E0, BIT(24), 0);

static SUNXI_CCU_GATE(npu_mbus_gate_clk, "npu-mbus-gate",
		"dcxo",
		0x05E0, BIT(18), 0);

static SUNXI_CCU_GATE(gpu0_mbus_gate_clk, "gpu0-mbus-gate",
		"dcxo",
		0x05E0, BIT(16), 0);

static SUNXI_CCU_GATE(ve_mbus_gate_clk, "ve-mbus-gate",
		"dcxo",
		0x05E0, BIT(12), 0);

static SUNXI_CCU_GATE(desys_mbus_gate_clk, "desys-mbus-gate",
		"dcxo",
		0x05E0, BIT(11), 0);

static SUNXI_CCU_GATE(gmac1_mclk, "gmac1-mclk",
		"dcxo",
		0x05E4, BIT(12), 0);

static SUNXI_CCU_GATE(gmac0_mclk, "gmac0-mclk",
		"dcxo",
		0x05E4, BIT(11), 0);

static SUNXI_CCU_GATE(isp_mclk, "isp-mclk",
		"dcxo",
		0x05E4, BIT(9), 0);

static SUNXI_CCU_GATE(csi_mclk, "csi-mclk",
		"dcxo",
		0x05E4, BIT(8), 0);

static SUNXI_CCU_GATE(nand_mclk, "nand-mclk",
		"dcxo",
		0x05E4, BIT(5), 0);

static SUNXI_CCU_GATE(dma1_mclk, "dma1-mclk",
		"dcxo",
		0x05E4, BIT(3), 0);

static SUNXI_CCU_GATE(ce_mclk, "ce-mclk",
		"dcxo",
		0x05E4, BIT(2), 0);

static SUNXI_CCU_GATE(ve_mclk, "ve-mclk",
		"dcxo",
		0x05E4, BIT(1), 0);

static SUNXI_CCU_GATE(dma0_mclk, "dma0-mclk",
		"dcxo",
		0x05E4, BIT(0), 0);

static SUNXI_CCU_GATE(dma0_clk, "dma0",
		"dcxo",
		0x0704, BIT(0), 0);

static SUNXI_CCU_GATE(dma1_clk, "dma1",
		"dcxo",
		0x070C, BIT(0), 0);

static SUNXI_CCU_GATE(spinlock_clk, "spinlock",
		"dcxo",
		0x0724, BIT(0), 0);

static SUNXI_CCU_GATE(msgbox0_clk, "msgbox0",
		"dcxo",
		0x0744, BIT(0), 0);

static SUNXI_CCU_GATE(pwm0_clk, "pwm0",
		"dcxo",
		0x0784, BIT(0), 0);

static SUNXI_CCU_GATE(pwm1_clk, "pwm1",
		"dcxo",
		0x078C, BIT(0), 0);

static SUNXI_CCU_GATE(dbgsys_clk, "dbgsys",
		"dcxo",
		0x07A4, BIT(0), 0);

static SUNXI_CCU_GATE(sysdap_clk, "sysdap",
		"dcxo",
		0x07AC, BIT(0), 0);

static const char * const timer_clk_parents[] = { "sys24M", "clk16m-rc", "clk-32k", "pll-peri0-200m", "dcxo" };

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk0, "timer0-clk0",
		timer_clk_parents, 0x0800,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk1, "timer0-clk1",
		timer_clk_parents, 0x0804,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk2, "timer0-clk2",
		timer_clk_parents, 0x0808,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk3, "timer0-clk3",
		timer_clk_parents, 0x080C,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk4, "timer0-clk4",
		timer_clk_parents, 0x0810,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk5, "timer0-clk5",
		timer_clk_parents, 0x0814,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk6, "timer0-clk6",
		timer_clk_parents, 0x0818,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk7, "timer0-clk7",
		timer_clk_parents, 0x081C,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk8, "timer0-clk8",
		timer_clk_parents, 0x0820,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(timer0_clk9, "timer0-clk9",
		timer_clk_parents, 0x0824,
		0, 3,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(timer0_clk, "timer0",
		"dcxo",
		0x0850, BIT(0), 0);

static const char * const avs_parents[] = { "sys24M", "dcxo" };

static SUNXI_CCU_MUX_WITH_GATE(avs_clk, "avs",
		avs_parents, 0x0880,
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const de0_parents[] = { "pll-peri0-480m", "video2pll4x", "video0pll4x", "pll-peri0-300m", "pll-peri0-400m", "depll4x", "depll3x" };

static SUNXI_CCU_M_WITH_MUX_GATE(de0_clk, "de0",
		de0_parents, 0x0A00,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(de0_gate_clk, "de0-gate",
		"dcxo",
		0x0A04, BIT(0), 0);

static const char * const di_parents[] = { "video2pll4x", "peri0-400m", "video0pll4x", "peri0-600m", "peri0-480m" };

static SUNXI_CCU_M_WITH_MUX_GATE(di_clk, "di",
		di_parents, 0x0A20,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(di_gate_clk, "di-gate",
		"dcxo",
		0x0A24, BIT(0), 0);

static const char * const g2d_parents[] = { "pll-peri0-400m", "video2pll4x", "pll-peri0-300m", "video0pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(g2d_clk, "g2d",
		g2d_parents, 0x0A40,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(g2d_gate_clk, "g2d-gate",
		"dcxo",
		0x0A44, BIT(0), 0);

static const char * const eink_parents[] = { "pll-peri0-400m", "pll-peri0-480m" };

static SUNXI_CCU_M_WITH_MUX_GATE(eink_clk, "eink",
		eink_parents, 0x0A60,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const eink_panel_parents[] = { "pll-peri0-300m", "video1pll4x", "video1pll3x", "video0pll4x", "video0pll3x" };

static SUNXI_CCU_M_WITH_MUX_GATE(eink_panel_clk, "eink-panel",
		eink_panel_parents, 0x0A64,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(eink_gate_clk, "eink-gate",
		"dcxo",
		0x0A6C, BIT(0), 0);

static const char * const ve_enc0_parents[] = { "pll-peri0-800m", "pll-npu", "depll3x", "pll-peri0-480m", "pll-peri0-600m", "ve1pll", "ve0pll" };

static SUNXI_CCU_M_WITH_MUX_GATE(ve_enc0_clk, "ve-enc0",
		ve_enc0_parents, 0x0A80,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const ve_dec_parents[] = { "pll-peri0-800m", "pll-npu", "depll3x", "pll-peri0-480m", "pll-peri0-600m", "ve0pll", "ve1pll" };

static SUNXI_CCU_M_WITH_MUX_GATE(ve_dec_clk, "ve-dec",
		ve_dec_parents, 0x0A88,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ve_dec_bus_clk, "ve-dec-gate",
		"dcxo",
		0x0A8C, BIT(2), 0);

static SUNXI_CCU_GATE(ve_enc_bus_clk, "ve-enc0-bus",
		"dcxo",
		0x0A8C, BIT(0), 0);

static const char * const ce_parents[] = { "peri0-400m", "dcxo", "peri0-600m" };

static SUNXI_CCU_M_WITH_MUX_GATE(ce_clk, "ce",
		ce_parents, 0x0AC0,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ce_sys_clk, "ce-sys",
		"dcxo",
		0x0AC4, BIT(1), 0);

static SUNXI_CCU_GATE(ce_bus_clk, "ce-gate",
		"dcxo",
		0x0AC4, BIT(0), 0);

static const char * const npu_parents[] = { "pll-peri0-600m", "depll3x", "ve1pll", "ve0pll", "pll-peri0-480m", "pll-peri0-800m", "pll-npu" };

static SUNXI_CCU_M_WITH_MUX_GATE(npu_clk, "npu",
		npu_parents, 0x0B00,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(npu_bus_clk, "npu-gate",
		"dcxo",
		0x0B04, BIT(0), 0);

static const char * const gpu0_parents[] = { "pll-peri0-600m", "pll-peri0-200m", "pll-peri0-300m", "pll-peri0-400m", "pll-peri0-800m", "gpupll" };

static SUNXI_CCU_M_WITH_MUX_GATE(gpu0_clk, "gpu0",
		gpu0_parents, 0x0B20,
		0, 4,
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(gpu0_bus_clk, "gpu0-gate",
		"dcxo",
		0x0B24, BIT(0), 0);

static const char * const dram0_parents[] = { "ddrpll", "pll-peri1-300m", "pll-peri1-600m", "pll-de-3x", "pll-npu"};
static SUNXI_CCU_M_WITH_MUX_GATE(dram0_clk, "dram0",
		dram0_parents, 0x0C00,
		0, 5,
		16, 1,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(dram0_bus_clk, "dram0-gate",
		"dcxo",
		0x0C0C, BIT(0), 0);

static const char * const nand0_clk0_parents[] = { "pll-peri1-300m", "pll-peri0-300m", "pll-peri1-400m", "dcxo", "pll-peri0-400m" };

static SUNXI_CCU_M_WITH_MUX_GATE(nand0_clk0_clk, "nand0-clk0",
		nand0_clk0_parents, 0x0C80,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const nand0_clk1_parents[] = { "pll-peri1-300m", "pll-peri0-300m", "pll-peri1-400m", "dcxo", "pll-peri0-400m" };

static SUNXI_CCU_M_WITH_MUX_GATE(nand0_clk1_clk, "nand0-clk1",
		nand0_clk1_parents, 0x0C84,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(nand0_bus_clk, "nand0-bus",
		"dcxo",
		0x0C8C, BIT(0), 0);

static const char * const smhc0_parents[] = { "pll-peri1-300m", "pll-peri0-300m", "pll-peri1-400m", "sys24M", "pll-peri0-400m" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(smhc0_clk, "smhc0",
		smhc0_parents, 0x0D00,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(smhc0_gate_clk, "smhc0-gate",
		"dcxo",
		0x0D0C, BIT(0), 0);

static const char * const smhc1_parents[] = { "pll-peri1-300m", "pll-peri0-300m", "pll-peri1-400m", "sys24M", "pll-peri0-400m" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(smhc1_clk, "smhc1",
		smhc1_parents, 0x0D10,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(smhc1_gate_clk, "smhc1-gate",
		"dcxo",
		0x0D1C, BIT(0), 0);

static const char * const smhc2_parents[] = { "pll-peri1-600m", "pll-peri0-600m", "pll-peri1-800m", "sys24M", "pll-peri0-800m" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(smhc2_clk, "smhc2",
		smhc2_parents, 0x0D20,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(smhc2_gate_clk, "smhc2-gate",
		"dcxo",
		0x0D2C, BIT(0), 0);

static const char * const smhc3_parents[] = { "pll-peri1-600m", "pll-peri0-600m", "pll-peri1-800m", "sys24M", "pll-peri0-800m" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(smhc3_clk, "smhc3",
		smhc3_parents, 0x0D30,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(smhc3_bus_clk, "smhc3-bus",
		"dcxo",
		0x0D3C, BIT(0), 0);

static const char * const ufs_axi_parents[] = { "pll-peri0-200m", "pll-peri0-300m" };

static SUNXI_CCU_M_WITH_MUX_GATE(ufs_axi_clk, "ufs-axi",
		ufs_axi_parents, 0x0D80,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const ufs_cfg_parents[] = { "dcxo", "pll-peri0-480m" };

static SUNXI_CCU_M_WITH_MUX_GATE(ufs_cfg_clk, "ufs-cfg",
		ufs_cfg_parents, 0x0D84,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ufs_clk, "ufs",
		"dcxo",
		0x0D8C, BIT(0), 0);

static SUNXI_CCU_GATE(uart0_clk, "uart0",
		"dcxo",
		0x0E00, BIT(0), 0);

static SUNXI_CCU_GATE(uart1_clk, "uart1",
		"dcxo",
		0x0E04, BIT(0), 0);

static SUNXI_CCU_GATE(uart2_clk, "uart2",
		"dcxo",
		0x0E08, BIT(0), 0);

static SUNXI_CCU_GATE(uart3_clk, "uart3",
		"dcxo",
		0x0E0C, BIT(0), 0);

static SUNXI_CCU_GATE(uart4_clk, "uart4",
		"dcxo",
		0x0E10, BIT(0), 0);

static SUNXI_CCU_GATE(uart5_clk, "uart5",
		"dcxo",
		0x0E14, BIT(0), 0);

static SUNXI_CCU_GATE(uart6_clk, "uart6",
		"dcxo",
		0x0E18, BIT(0), 0);

static SUNXI_CCU_GATE(twi0_clk, "twi0",
		"dcxo",
		0x0E80, BIT(0), 0);

static SUNXI_CCU_GATE(twi1_clk, "twi1",
		"dcxo",
		0x0E84, BIT(0), 0);

static SUNXI_CCU_GATE(twi2_clk, "twi2",
		"dcxo",
		0x0E88, BIT(0), 0);

static SUNXI_CCU_GATE(twi3_clk, "twi3",
		"dcxo",
		0x0E8C, BIT(0), 0);

static SUNXI_CCU_GATE(twi4_clk, "twi4",
		"dcxo",
		0x0E90, BIT(0), 0);

static SUNXI_CCU_GATE(twi5_clk, "twi5",
		"dcxo",
		0x0E94, BIT(0), 0);

static SUNXI_CCU_GATE(twi6_clk, "twi6",
		"dcxo",
		0x0E98, BIT(0), 0);

static SUNXI_CCU_GATE(twi7_clk, "twi7",
		"dcxo",
		0x0E9C, BIT(0), 0);

static SUNXI_CCU_GATE(twi8_clk, "twi8",
		"dcxo",
		0x0EA0, BIT(0), 0);

static SUNXI_CCU_GATE(twi9_clk, "twi9",
		"dcxo",
		0x0EA4, BIT(0), 0);

static SUNXI_CCU_GATE(twi10_clk, "twi10",
		"dcxo",
		0x0EA8, BIT(0), 0);

static SUNXI_CCU_GATE(twi11_clk, "twi11",
		"dcxo",
		0x0EAC, BIT(0), 0);

static SUNXI_CCU_GATE(twi12_clk, "twi12",
		"dcxo",
		0x0EB0, BIT(0), 0);

static const char * const spi0_parents[] = { "pll-peri0-200m", "pll-peri1-480m", "pll-peri0-480m", "pll-peri1-200m", "hosc", "pll-peri1-300m", "pll-peri0-300m", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(spi0_clk, "spi0",
		spi0_parents, 0x0F00,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(spi0_bus_clk, "spi0-bus",
		"dcxo",
		0x0F04, BIT(0), 0);

static const char * const spi1_parents[] = { "pll-peri0-200m", "pll-peri1-480m", "pll-peri0-480m", "pll-peri1-200m", "hosc", "pll-peri1-300m", "pll-peri0-300m", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(spi1_clk, "spi1",
		spi1_parents, 0x0F08,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(spi1_bus_clk, "spi1-bus",
		"dcxo",
		0x0F0C, BIT(0), 0);

static const char * const spi2_parents[] = { "pll-peri0-200m", "pll-peri1-480m", "pll-peri0-480m", "pll-peri1-200m", "hosc", "pll-peri1-300m", "pll-peri0-300m", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(spi2_clk, "spi2",
		spi2_parents, 0x0F10,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(spi2_bus_clk, "spi2-bus",
		"dcxo",
		0x0F14, BIT(0), 0);

static const char * const spif_parents[] = { "pll-peri0-300m", "pll-peri1-160m", "pll-peri0-160m", "pll-peri1-300m", "hosc", "pll-peri1-400m", "pll-peri0-400m", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(spif_clk, "spif",
		spif_parents, 0x0F18,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(spif_bus_clk, "spif-bus",
		"dcxo",
		0x0F1C, BIT(0), 0);

static const char * const spi3_parents[] = { "pll-peri0-200m", "pll-peri1-480m", "pll-peri0-480m", "pll-peri1-200m", "hosc", "pll-peri1-300m", "pll-peri0-300m", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(spi3_clk, "spi3",
		spi3_parents, 0x0F20,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(spi3_bus_clk, "spi3-bus",
		"dcxo",
		0x0F24, BIT(0), 0);

static const char * const spi4_parents[] = { "pll-peri0-200m", "pll-peri1-480m", "pll-peri0-480m", "pll-peri1-200m", "hosc", "pll-peri1-300m", "pll-peri0-300m", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(spi4_clk, "spi4",
		spi4_parents, 0x0F28,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(spi4_bus_clk, "spi4-bus",
		"dcxo",
		0x0F2C, BIT(0), 0);

static const char * const gpadc0_24m_parents[] = { "sys24M", "dcxo" };

static SUNXI_CCU_M_WITH_MUX_GATE(gpadc0_24m_clk, "gpadc0-24m",
		gpadc0_24m_parents, 0x0FC0,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(gpadc0_clk, "gpadc0",
		"dcxo",
		0x0FC4, BIT(0), 0);

static SUNXI_CCU_GATE(ths0_clk, "ths0",
		"dcxo",
		0x0FE4, BIT(0), 0);

static const char * const irrx_parents[] = { "ext-32k", "dcxo", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(irrx_clk, "irrx",
		irrx_parents, 0x1000,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		0);

static SUNXI_CCU_GATE(irrx_gate_clk, "irrx-gate",
		"dcxo",
		0x1004, BIT(0), 0);

static const char * const irtx_parents[] = { "pll-peri1-600m", "dcxo", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(irtx_clk, "irtx",
		irtx_parents, 0x1008,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		0);

static SUNXI_CCU_GATE(irtx_gate_clk, "irtx-gate",
		"dcxo",
		0x100C, BIT(0), 0);

static SUNXI_CCU_GATE(lradc_clk, "lradc",
		"dcxo",
		0x1024, BIT(0), 0);

static const char * const sgpio_parents[] = { "ext-32k", "dcxo" };

static SUNXI_CCU_M_WITH_MUX_GATE(sgpio_clk, "sgpio",
		sgpio_parents, 0x1060,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		0);

static SUNXI_CCU_GATE(sgpio_bus_clk, "sgpio-bus",
		"dcxo",
		0x1064, BIT(0), 0);

static const char * const lpc_parents[] = { "pll-peri0-300m", "video2pll3x" };

static SUNXI_CCU_M_WITH_MUX_GATE(lpc_clk, "lpc",
		lpc_parents, 0x1080,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(lpc_bus_clk, "lpc-gate",
		"dcxo",
		0x1084, BIT(0), 0);

static const char * const i2spcm0_parents[] = { "audio1pll-div2", "peri0-200m", "audio0pll4x", "audio1pll-div5" };

static SUNXI_CCU_M_WITH_MUX_GATE(i2spcm0_clk, "i2spcm0",
		i2spcm0_parents, 0x1200,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(i2spcm0_gate_clk, "i2spcm0-bus",
		"dcxo",
		0x120C, BIT(0), 0);

static const char * const i2spcm1_parents[] = { "audio1pll-div2", "peri0-200m", "audio0pll4x", "audio1pll-div5" };

static SUNXI_CCU_M_WITH_MUX_GATE(i2spcm1_clk, "i2spcm1",
		i2spcm1_parents, 0x1210,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(i2spcm1_bus_clk, "i2spcm1-bus",
		"dcxo",
		0x121C, BIT(0), 0);

static const char * const i2spcm2_parents[] = { "audio1pll-div2", "pll-peri0-200m", "audio0pll4x", "audio1pll-div5" };

static SUNXI_CCU_M_WITH_MUX_GATE(i2spcm2_clk, "i2spcm2",
		i2spcm2_parents, 0x1220,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const i2spcm2_asrc_parents[] = { "pll-peri1-300m", "audio1pll-div5", "pll-peri0-300m", "audio0pll4x", "audio1pll-div2" };

static SUNXI_CCU_M_WITH_MUX_GATE(i2spcm2_asrc_clk, "i2spcm2-asrc",
		i2spcm2_asrc_parents, 0x1224,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(i2spcm2_bus_clk, "i2spcm2-bus",
		"dcxo",
		0x122C, BIT(0), 0);

static const char * const i2spcm3_parents[] = { "audio1pll-div2", "pll-peri0-200m", "audio0pll4x", "audio1pll-div5" };

static SUNXI_CCU_M_WITH_MUX_GATE(i2spcm3_clk, "i2spcm3",
		i2spcm3_parents, 0x1230,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(i2spcm3_bus_clk, "i2spcm3-bus",
		"dcxo",
		0x123C, BIT(0), 0);

static const char * const spdif_tx_parents[] = { "audio1pll-div2", "audio0pll4x", "audio1pll-div5" };

static SUNXI_CCU_M_WITH_MUX_GATE(spdif_tx_clk, "spdif-tx",
		spdif_tx_parents, 0x1280,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const spdif_rx_parents[] = { "pll-peri0-300m", "pll-peri0-200m", "pll-peri0-400m" };

static SUNXI_CCU_M_WITH_MUX_GATE(spdif_rx_clk, "spdif-rx",
		spdif_rx_parents, 0x1284,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(spdif_clk, "spdif",
		"dcxo",
		0x128C, BIT(0), 0);

static const char * const dmic_parents[] = { "audio1pll-div2", "audio0pll4x", "audio1pll-div5" };

static SUNXI_CCU_M_WITH_MUX_GATE(dmic_clk, "dmic",
		dmic_parents, 0x12C0,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(dmic_bus_clk, "dmic-bus",
		"dcxo",
		0x12CC, BIT(0), 0);

static SUNXI_CCU_GATE(usb_clk, "usb",
		"dcxo",
		0x1300, BIT(31), 0);

static SUNXI_CCU_GATE(usb0_device_clk, "usb0-device",
		"dcxo",
		0x1304, BIT(8), 0);

static SUNXI_CCU_GATE(usb0_ehci_clk, "usb0-ehci",
		"dcxo",
		0x1304, BIT(4), 0);

static SUNXI_CCU_GATE(usb0_ohci_clk, "usb0-ohci",
		"dcxo",
		0x1304, BIT(0), 0);

static SUNXI_CCU_GATE(usb1_clk, "usb1",
		"dcxo",
		0x1308, BIT(31), 0);

static SUNXI_CCU_GATE(usb1_ehci_clk, "usb1-ehci",
		"dcxo",
		0x130C, BIT(4), 0);

static SUNXI_CCU_GATE(usb1_ohci_clk, "usb1-ohci",
		"dcxo",
		0x130C, BIT(0), 0);

static const char * const usb_ref_parents[] = { "dcxo", "sys24M" };

static SUNXI_CCU_MUX_WITH_GATE(usb_ref_clk, "usb-ref",
		usb_ref_parents, 0x1340,
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const usb2_u2_ref_parents[] = { "dcxo", "sys24M" };

static SUNXI_CCU_MUX_WITH_GATE(usb2_u2_ref_clk, "usb2-u2-ref",
		usb2_u2_ref_parents, 0x1348,
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const usb2_suspend_parents[] = { "clk32k", "dcxo" };

static SUNXI_CCU_M_WITH_MUX_GATE(usb2_suspend_clk, "usb2-suspend",
		usb2_suspend_parents, 0x1350,
		0, 5,	/* M */
		24, 1,	/* mux */
		BIT(31), /* gate */
		0);

static const char * const usb2_mf_parents[] = { "dcxo", "pll-peri0-300m", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(usb2_mf_clk, "usb2-mf",
		usb2_mf_parents, 0x1354,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31), /* gate */
		0);

static const char * const usb2_u3_utmi_parents[] = { "dcxo", "pll-peri0-300m", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(usb2_u3_utmi_clk, "usb2-u3-utmi",
		usb2_u3_utmi_parents, 0x1360,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31), /* gate */
		0);

static const char * const usb2_u2_pipe_parents[] = { "dcxo", "pll-peri0-480m", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(usb2_u2_pipe_clk, "usb2-u2-pipe",
		usb2_u2_pipe_parents, 0x1364,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31), /* gate */
		0);

static const char * const pcie0_aux_parents[] = { "dcxo", "clk32k" };

static SUNXI_CCU_M_WITH_MUX_GATE(pcie0_aux_clk, "pcie0-aux",
		pcie0_aux_parents, 0x1380,
		0, 5,	/* M */
		24, 1,	/* mux */
		BIT(31),	/* gate */
		0);

static const char * const pcie0_axi_slv_parents[] = { "pll-peri0-480m", "pll-peri0-600m", "pll-peri0-400m" };

static SUNXI_CCU_M_WITH_MUX_GATE(pcie0_axi_slv_clk, "pcie0-axi-slv",
		pcie0_axi_slv_parents, 0x1384,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const serdes_phy_cfg_parents[] = { "pll-peri0-600m", "dcxo" };

static SUNXI_CCU_M_WITH_MUX_GATE(serdes_phy_cfg_clk, "serdes-phy-cfg",
		serdes_phy_cfg_parents, 0x13C0,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const gmac_ptp_parents[] = { "pll-peri0-200m", "dcxo", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(gmac_ptp_clk, "gmac-ptp",
		gmac_ptp_parents, 0x1400,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(gmac0_phy_clk, "gmac0-phy",
		"dcxo",
		0x1410, BIT(31), 0);

static SUNXI_CCU_GATE(gmac0_clk, "gmac0",
		"dcxo",
		0x141C, BIT(0), 0);

static SUNXI_CCU_GATE(gmac1_phy_clk, "gmac1-phy",
		"dcxo",
		0x1420, BIT(31), 0);

static SUNXI_CCU_GATE(gmac1_clk, "gmac1",
		"dcxo",
		0x142C, BIT(0), 0);

static const char * const vo0_tconlcd0_parents[] = { "video1pll4x", "pll_peri0_2x", "video0pll4x", "video2pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(vo0_tconlcd0_clk, "vo0-tconlcd0",
		vo0_tconlcd0_parents, 0x1500,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(vo0_tconlcd0_bus_clk, "vo0-tconlcd0-bus",
		"dcxo",
		0x1504, BIT(0), 0);

static const char * const vo0_tconlcd1_parents[] = { "video1pll4x", "pll_peri0_2x", "video0pll4x", "video2pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(vo0_tconlcd1_clk, "vo0-tconlcd1",
		vo0_tconlcd1_parents, 0x1508,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(vo0_tconlcd1_bus_clk, "vo0-tconlcd1-bus",
		"dcxo",
		0x150C, BIT(0), 0);

static const char * const vo0_tconlcd2_parents[] = { "video1pll4x", "pll_peri0_2x", "video0pll4x", "video2pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(vo0_tconlcd2_clk, "vo0-tconlcd2",
		vo0_tconlcd2_parents, 0x1510,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(vo0_tconlcd2_bus_clk, "vo0-tconlcd2-bus",
		"dcxo",
		0x1514, BIT(0), 0);

static const char * const dsi0_parents[] = { "pll-peri0-200m", "dcxo", "pll-peri0-150m" };

static SUNXI_CCU_M_WITH_MUX_GATE(dsi0_clk, "dsi0",
		dsi0_parents, 0x1580,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(dsi0_bus_clk, "dsi0-bus",
		"dcxo",
		0x1584, BIT(0), 0);

static const char * const dsi1_parents[] = { "pll-peri0-200m", "dcxo", "pll-peri0-150m" };

static SUNXI_CCU_M_WITH_MUX_GATE(dsi1_clk, "dsi1",
		dsi1_parents, 0x1588,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(dsi1_bus_clk, "dsi1-bus",
		"dcxo",
		0x158C, BIT(0), 0);

static const char * const combphy0_parents[] = { "video0pll3x", "video2pll4x", "pll_peri0_2x", "video0pll4x", "video1pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(combphy0_clk, "combphy0",
		combphy0_parents, 0x15C0,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static const char * const combphy1_parents[] = { "video0pll3x", "video2pll4x", "pll_peri0_2x", "video0pll4x", "video1pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(combphy1_clk, "combphy1",
		combphy1_parents, 0x15C4,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(tcontv0_clk, "tcontv0",
		"dcxo",
		0x1604, BIT(0), 0);

static SUNXI_CCU_GATE(tcontv1_clk, "tcontv1",
		"dcxo",
		0x160C, BIT(0), 0);

static const char * const edp_tv_parents[] = { "video1pll4x", "pll_peri0_2x", "video0pll4x", "video2pll4x" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(edp_tv_clk, "edp-tv",
		edp_tv_parents, 0x1640,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(edp_clk, "edp",
		"dcxo",
		0x164C, BIT(0), 0);

static const char * const hdmi_ref_parents[] = { "hdmi-cec-clk32k", "clk32k" };

static SUNXI_CCU_MUX_WITH_GATE(hdmi_ref_clk, "hdmi-ref",
		hdmi_ref_parents, 0x1680,
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const hdmi_tv_parents[] = { "video1pll4x", "pll_peri0_2x", "video0pll4x", "video2pll4x" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(hdmi_tv_clk, "hdmi-tv",
		hdmi_tv_parents, 0x1684,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(hdmi_clk, "hdmi",
		"dcxo",
		0x168C, BIT(0), 0);

static const char * const hdmi_sfr_parents[] = { "dcxo", "sys24M" };

static SUNXI_CCU_MUX_WITH_GATE(hdmi_sfr_clk, "hdmi-sfr",
		hdmi_sfr_parents, 0x1690,
		24, 3,	/* mux */
		BIT(31), 0);

static SUNXI_CCU_GATE(hdcp_esm_clk, "hdcp-esm",
		"dcxo",
		0x1694, BIT(31), 0);

static SUNXI_CCU_GATE(dpss_top0_clk, "dpss-top0",
		"dcxo",
		0x16C4, BIT(0), 0);

static SUNXI_CCU_GATE(dpss_top1_clk, "dpss-top1",
		"dcxo",
		0x16CC, BIT(0), 0);

static const char * const ledc_parents[] = { "pll-peri0-600m", "dcxo", "hosc" };

static SUNXI_CCU_M_WITH_MUX_GATE(ledc_clk, "ledc",
		ledc_parents, 0x1700,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ledc_bus_clk, "ledc-bus",
		"dcxo",
		0x1704, BIT(0), 0);

static SUNXI_CCU_GATE(dsc_clk, "dsc",
		"dcxo",
		0x1744, BIT(0), 0);

static const char * const csi_master0_parents[] = { "video0pll3x", "video2pll3x", "video2pll4x", "video1pll3x", "video1pll4x", "video0pll4x", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(csi_master0_clk, "csi-master0",
		csi_master0_parents, 0x1800,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const csi_master1_parents[] = { "video0pll3x", "video2pll3x", "video2pll4x", "video1pll3x", "video1pll4x", "video0pll4x", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(csi_master1_clk, "csi-master1",
		csi_master1_parents, 0x1804,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const csi_master2_parents[] = { "video0pll3x", "video2pll3x", "video2pll4x", "video1pll3x", "video1pll4x", "video0pll4x", "dcxo" };

static SUNXI_CCU_MP_WITH_MUX_GATE_NO_INDEX(csi_master2_clk, "csi-master2",
		csi_master2_parents, 0x1808,
		0, 5,	/* M */
		8, 5,	/* N */
		24, 3,	/* mux */
		BIT(31), 0);

static const char * const csi_parents[] = { "pll-peri0-480m", "video1pll4x", "video0pll4x", "pll-peri0-600m", "pll-peri0-400m", "depll4x", "video2pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(csi_clk, "csi",
		csi_parents, 0x1840,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(csi_bus_clk, "csi-bus",
		"dcxo",
		0x1844, BIT(0), 0);

static const char * const isp_parents[] = { "pll-peri0-400m", "video1pll4x", "video0pll4x", "pll-peri0-600m", "pll-peri0-480m", "video2pll4x" };

static SUNXI_CCU_M_WITH_MUX_GATE(isp_clk, "isp",
		isp_parents, 0x1860,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(ddrpll_gate_clk, "ddrpll-gate",
		"dcxo",
		0x1904, BIT(16), 0);

static SUNXI_CCU_GATE(ddrpll_auto__clk, "ddrpll-auto",
		"dcxo",
		0x1904, BIT(0), 0);

static SUNXI_CCU_GATE(peri0_300m_dsp__clk, "peri0-300m-dsp",
		"dcxo",
		0x1908, BIT(31), 0);

static SUNXI_CCU_GATE(pll_peri0_2x_gate_clk, "pll_peri0_2x-gate",
		"dcxo",
		0x1908, BIT(27), 0);

static SUNXI_CCU_GATE(peri0_800m_gate_clk, "peri0-800m-gate",
		"dcxo",
		0x1908, BIT(26), 0);

static SUNXI_CCU_GATE(peri0_600m_gate_clk, "peri0-600m-gate",
		"dcxo",
		0x1908, BIT(25), 0);

static SUNXI_CCU_GATE(peri0_480m_gate_all_clk, "peri0-480m-gate-all",
		"dcxo",
		0x1908, BIT(24), 0);

static SUNXI_CCU_GATE(peri0_480m_gate_clk, "peri0-480m-gate",
		"dcxo",
		0x1908, BIT(23), 0);

static SUNXI_CCU_GATE(peri0_160m_gate_clk, "peri0-160m-gate",
		"dcxo",
		0x1908, BIT(22), 0);

static SUNXI_CCU_GATE(peri0_300m_gate_clk, "peri0-300m-all",
		"dcxo",
		0x1908, BIT(21), 0);

static SUNXI_CCU_GATE(peri0_300m_sw_clk, "peri0-300m-sw",
		"dcxo",
		0x1908, BIT(20), 0);

static SUNXI_CCU_GATE(peri0_150m_gate_clk, "peri0-150m-gate",
		"dcxo",
		0x1908, BIT(19), 0);

static SUNXI_CCU_GATE(peri0_400m_gate_clk, "peri0-400m-all",
		"dcxo",
		0x1908, BIT(18), 0);

static SUNXI_CCU_GATE(peri0_400m_sw_clk, "peri0-400m-sw",
		"dcxo",
		0x1908, BIT(17), 0);

static SUNXI_CCU_GATE(peri0_200m_gate_clk, "peri0-200m-gate",
		"dcxo",
		0x1908, BIT(16), 0);

static SUNXI_CCU_GATE(pll_peri0_2x_auto_clk, "pll_peri0_2x-auto",
		"dcxo",
		0x1908, BIT(11), 0);

static SUNXI_CCU_GATE(peri0_800m_auto_clk, "peri0-800m-auto",
		"dcxo",
		0x1908, BIT(10), 0);

static SUNXI_CCU_GATE(peri0_600m_auto__clk, "peri0-600m-auto",
		"dcxo",
		0x1908, BIT(9), 0);

static SUNXI_CCU_GATE(peri0_480m_auto_gate_clk, "peri0-480m-auto-gate",
		"dcxo",
		0x1908, BIT(8), 0);

static SUNXI_CCU_GATE(peri0_480m_auto__clk, "peri0-480m-auto",
		"dcxo",
		0x1908, BIT(7), 0);

static SUNXI_CCU_GATE(peri0_160m_auto__clk, "peri0-160m-auto",
		"dcxo",
		0x1908, BIT(6), 0);

static SUNXI_CCU_GATE(peri0_300m_auto_gate_clk, "peri0-300m-auto-gate",
		"dcxo",
		0x1908, BIT(5), 0);

static SUNXI_CCU_GATE(peri0_300m_auto__clk, "peri0-300m-auto",
		"dcxo",
		0x1908, BIT(4), 0);

static SUNXI_CCU_GATE(peri0_150m_auto__clk, "peri0-150m-auto",
		"dcxo",
		0x1908, BIT(3), 0);

static SUNXI_CCU_GATE(peri0_400m_auto_gate_clk, "peri0-400m-auto-gate",
		"dcxo",
		0x1908, BIT(2), 0);

static SUNXI_CCU_GATE(peri0_400m_auto__clk, "peri0-400m-auto",
		"dcxo",
		0x1908, BIT(1), 0);

static SUNXI_CCU_GATE(peri0_200m_auto__clk, "peri0-200m-auto",
		"dcxo",
		0x1908, BIT(0), 0);

static SUNXI_CCU_GATE(peri1_300m_dsp__clk, "peri1-300m-dsp",
		"dcxo",
		0x190C, BIT(31), 0);

static SUNXI_CCU_GATE(peri1_800m_gate_clk, "peri1-800m-gate",
		"dcxo",
		0x190C, BIT(27), 0);

static SUNXI_CCU_GATE(peri1_600m_gate_clk, "peri1-600m-gate",
		"dcxo",
		0x190C, BIT(26), 0);

static SUNXI_CCU_GATE(peri1_600m_sw_clk, "peri1-600m-sw",
		"dcxo",
		0x190C, BIT(25), 0);

static SUNXI_CCU_GATE(peri1_480m_gate_clk, "peri1-480m-gate",
		"dcxo",
		0x190C, BIT(24), 0);

static SUNXI_CCU_GATE(peri1_480m_sw_clk, "peri1-480m-sw",
		"dcxo",
		0x190C, BIT(23), 0);

static SUNXI_CCU_GATE(peri1_160m_gate_clk, "peri1-160m-gate",
		"dcxo",
		0x190C, BIT(22), 0);

static SUNXI_CCU_GATE(peri1_300m_gate_clk, "peri1-300m-gate",
		"dcxo",
		0x190C, BIT(21), 0);

static SUNXI_CCU_GATE(peri1_300m_sw_clk, "peri1-300m-sw",
		"dcxo",
		0x190C, BIT(20), 0);

static SUNXI_CCU_GATE(peri1_150m_gate_clk, "peri1-150m-gate",
		"dcxo",
		0x190C, BIT(19), 0);

static SUNXI_CCU_GATE(peri1_400m_gate__clk, "peri1-400m-all",
		"dcxo",
		0x190C, BIT(18), 0);

static SUNXI_CCU_GATE(peri1_400m_gate_clk, "peri1-400m-gate",
		"dcxo",
		0x190C, BIT(17), 0);

static SUNXI_CCU_GATE(peri1_200m_gate_clk, "peri1-200m-gate",
		"dcxo",
		0x190C, BIT(16), 0);

static SUNXI_CCU_GATE(peri1_800m_auto__clk, "peri1-800m-auto",
		"dcxo",
		0x190C, BIT(11), 0);

static SUNXI_CCU_GATE(peri1_600m_auto_gate_clk, "peri1-600m-auto-gate",
		"dcxo",
		0x190C, BIT(10), 0);

static SUNXI_CCU_GATE(peri1_600m_auto__clk, "peri1-600m-auto",
		"dcxo",
		0x190C, BIT(9), 0);

static SUNXI_CCU_GATE(peri1_480m_auto_gate_clk, "peri1-480m-auto-gate",
		"dcxo",
		0x190C, BIT(8), 0);

static SUNXI_CCU_GATE(peri1_480m_auto__clk, "peri1-480m-auto",
		"dcxo",
		0x190C, BIT(7), 0);

static SUNXI_CCU_GATE(peri1_160m_auto__clk, "peri1-160m-auto",
		"dcxo",
		0x190C, BIT(6), 0);

static SUNXI_CCU_GATE(peri1_300m_auto_gate_clk, "peri1-300m-auto-gate",
		"dcxo",
		0x190C, BIT(5), 0);

static SUNXI_CCU_GATE(peri1_300m_auto__clk, "peri1-300m-auto",
		"dcxo",
		0x190C, BIT(4), 0);

static SUNXI_CCU_GATE(peri1_150m_auto__clk, "peri1-150m-auto",
		"dcxo",
		0x190C, BIT(3), 0);

static SUNXI_CCU_GATE(peri1_400m_auto_gate_clk, "peri1-400m-auto-gate",
		"dcxo",
		0x190C, BIT(2), 0);

static SUNXI_CCU_GATE(peri1_400m_auto__clk, "peri1-400m-auto",
		"dcxo",
		0x190C, BIT(1), 0);

static SUNXI_CCU_GATE(peri1_200m_auto__clk, "peri1-200m-auto",
		"dcxo",
		0x190C, BIT(0), 0);

static SUNXI_CCU_GATE(video2pll3x_gate_clk, "video2pll3x-gate",
		"dcxo",
		0x1910, BIT(22), 0);

static SUNXI_CCU_GATE(video1pll3x_gate_clk, "video1pll3x-gate",
		"dcxo",
		0x1910, BIT(21), 0);

static SUNXI_CCU_GATE(video0pll3x_gate_clk, "video0pll3x-gate",
		"dcxo",
		0x1910, BIT(20), 0);

static SUNXI_CCU_GATE(video2pll4x_gate_clk, "video2pll4x-gate",
		"dcxo",
		0x1910, BIT(18), 0);

static SUNXI_CCU_GATE(video1pll4x_gate_clk, "video1pll4x-gate",
		"dcxo",
		0x1910, BIT(17), 0);

static SUNXI_CCU_GATE(video0pll4x_gate_clk, "video0pll4x-gate",
		"dcxo",
		0x1910, BIT(16), 0);

static SUNXI_CCU_GATE(video2pll3x_auto__clk, "video2pll3x-auto",
		"dcxo",
		0x1910, BIT(6), 0);

static SUNXI_CCU_GATE(video1pll3x_auto__clk, "video1pll3x-auto",
		"dcxo",
		0x1910, BIT(5), 0);

static SUNXI_CCU_GATE(video0pll3x_auto__clk, "video0pll3x-auto",
		"dcxo",
		0x1910, BIT(4), 0);

static SUNXI_CCU_GATE(video2pll4x_auto__clk, "video2pll4x-auto",
		"dcxo",
		0x1910, BIT(2), 0);

static SUNXI_CCU_GATE(video1pll4x_auto__clk, "video1pll4x-auto",
		"dcxo",
		0x1910, BIT(1), 0);

static SUNXI_CCU_GATE(video0pll4x_auto__clk, "video0pll4x-auto",
		"dcxo",
		0x1910, BIT(0), 0);

static SUNXI_CCU_GATE(gpu0pll_gate_clk, "gpu0pll-gate",
		"dcxo",
		0x1914, BIT(16), 0);

static SUNXI_CCU_GATE(gpu0pll_auto__clk, "gpu0pll-auto",
		"dcxo",
		0x1914, BIT(0), 0);

static SUNXI_CCU_GATE(ve1pll_gate_clk, "ve1pll-gate",
		"dcxo",
		0x1918, BIT(17), 0);

static SUNXI_CCU_GATE(ve0pll_gate_clk, "ve0pll-gate",
		"dcxo",
		0x1918, BIT(16), 0);

static SUNXI_CCU_GATE(ve1pll_auto__clk, "ve1pll-auto",
		"dcxo",
		0x1918, BIT(1), 0);

static SUNXI_CCU_GATE(ve0pll_auto__clk, "ve0pll-auto",
		"dcxo",
		0x1918, BIT(0), 0);

static SUNXI_CCU_GATE(audio1pll_div5_gate_clk, "audio1pll-div5-gate",
		"dcxo",
		0x191C, BIT(18), 0);

static SUNXI_CCU_GATE(audio1pll_div2_gate_clk, "audio1pll-div2-gate",
		"dcxo",
		0x191C, BIT(17), 0);

static SUNXI_CCU_GATE(audio0pll4x_gate_clk, "audio0pll4x-gate",
		"dcxo",
		0x191C, BIT(16), 0);

static SUNXI_CCU_GATE(audio1pll_div5_auto__clk, "audio1pll-div5-auto",
		"dcxo",
		0x191C, BIT(2), 0);

static SUNXI_CCU_GATE(audio1pll_div2_auto__clk, "audio1pll-div2-auto",
		"dcxo",
		0x191C, BIT(1), 0);

static SUNXI_CCU_GATE(audio0pll4x_auto__clk, "audio0pll4x-auto",
		"dcxo",
		0x191C, BIT(0), 0);

static SUNXI_CCU_GATE(npupll_gate_clk, "npupll-gate",
		"dcxo",
		0x1920, BIT(16), 0);

static SUNXI_CCU_GATE(npupll_auto_clk, "npupll-auto",
		"dcxo",
		0x1920, BIT(0), 0);

static SUNXI_CCU_GATE(depll3x_gate_clk, "depll3x-gate",
		"dcxo",
		0x1928, BIT(17), 0);

static SUNXI_CCU_GATE(depll4x_gate_clk, "depll4x-gate",
		"dcxo",
		0x1928, BIT(16), 0);

static SUNXI_CCU_GATE(depll3x_auto_clk, "depll3x-auto",
		"dcxo",
		0x1928, BIT(1), 0);

static SUNXI_CCU_GATE(depll4x_auto_clk, "depll4x-auto",
		"dcxo",
		0x1928, BIT(0), 0);

static SUNXI_CCU_GATE(ddrpll_ga_clk, "ddrpll-ga",
		"dcxo",
		0x1984, BIT(16), 0);

static SUNXI_CCU_GATE(pll_peri0_2x_ga_clk, "pll_peri0-2x-stat",
		"dcxo",
		0x1988, BIT(27), 0);

static SUNXI_CCU_GATE(peri0_800m_ga_clk, "peri0-800m-stat",
		"dcxo",
		0x1988, BIT(26), 0);

static SUNXI_CCU_GATE(peri0_600m_ga_clk, "peri0-600m-stat",
		"dcxo",
		0x1988, BIT(25), 0);

static SUNXI_CCU_GATE(peri0_480m_gate_a_clk, "peri0-480m-all",
		"dcxo",
		0x1988, BIT(24), 0);

static SUNXI_CCU_GATE(peri0_480m_ga_clk, "peri0-480m-stat",
		"dcxo",
		0x1988, BIT(23), 0);

static SUNXI_CCU_GATE(peri0_160m_stat_clk, "peri0-160m-status",
		"dcxo",
		0x1988, BIT(22), 0);

static SUNXI_CCU_GATE(peri0_300m_gate_a_clk, "peri0-300m-gate",
		"dcxo",
		0x1988, BIT(21), 0);

static SUNXI_CCU_GATE(peri0_300m_ga_clk, "peri0-300m-stat",
		"dcxo",
		0x1988, BIT(20), 0);

static SUNXI_CCU_GATE(peri0_150m_ga_clk, "peri0-150m-stat",
		"dcxo",
		0x1988, BIT(19), 0);

static SUNXI_CCU_GATE(peri0_400m_gate_a_clk, "peri0-400m-stat",
		"dcxo",
		0x1988, BIT(18), 0);

static SUNXI_CCU_GATE(peri0_400m_ga_clk, "peri0-400m-ga",
		"dcxo",
		0x1988, BIT(17), 0);

static SUNXI_CCU_GATE(peri0_200m_ga_clk, "peri0-200m-ga",
		"dcxo",
		0x1988, BIT(16), 0);

static SUNXI_CCU_GATE(peri1_800m_ga_clk, "peri1-800m-ga",
		"dcxo",
		0x198C, BIT(27), 0);

static SUNXI_CCU_GATE(peri1_600m_gate_a_clk, "peri1-600m-all-stat",
		"dcxo",
		0x198C, BIT(26), 0);

static SUNXI_CCU_GATE(peri1_600m_stat_clk, "peri1-600m-stat",
		"dcxo",
		0x198C, BIT(25), 0);

static SUNXI_CCU_GATE(peri1_480m_gate_a_clk, "peri1-480m-gate-a",
		"dcxo",
		0x198C, BIT(24), 0);

static SUNXI_CCU_GATE(peri1_480m_ga_clk, "peri1-480m-ga",
		"dcxo",
		0x198C, BIT(23), 0);

static SUNXI_CCU_GATE(peri1_160m_ga_clk, "peri1-160m-ga",
		"dcxo",
		0x198C, BIT(22), 0);

static SUNXI_CCU_GATE(peri1_300m_gate_a_clk, "peri1-300m-gate-a",
		"dcxo",
		0x198C, BIT(21), 0);

static SUNXI_CCU_GATE(peri1_300m_ga_clk, "peri1-300m-ga",
		"dcxo",
		0x198C, BIT(20), 0);

static SUNXI_CCU_GATE(peri1_150m_ga_clk, "peri1-150m-ga",
		"dcxo",
		0x198C, BIT(19), 0);

static SUNXI_CCU_GATE(peri1_400m_gate_a_clk, "peri1-400m-gate-a",
		"dcxo",
		0x198C, BIT(18), 0);

static SUNXI_CCU_GATE(peri1_400m_ga_clk, "peri1-400m-ga",
		"dcxo",
		0x198C, BIT(17), 0);

static SUNXI_CCU_GATE(peri1_200m_ga_clk, "peri1-200m-ga",
		"dcxo",
		0x198C, BIT(16), 0);

static SUNXI_CCU_GATE(video2pll3x_ga_clk, "video2pll3x-ga",
		"dcxo",
		0x1990, BIT(22), 0);

static SUNXI_CCU_GATE(video1pll3x_ga_clk, "video1pll3x-ga",
		"dcxo",
		0x1990, BIT(21), 0);

static SUNXI_CCU_GATE(video0pll3x_ga_clk, "video0pll3x-ga",
		"dcxo",
		0x1990, BIT(20), 0);

static SUNXI_CCU_GATE(video2pll4x_ga_clk, "video2pll4x-ga",
		"dcxo",
		0x1990, BIT(18), 0);

static SUNXI_CCU_GATE(video1pll4x_ga_clk, "video1pll4x-ga",
		"dcxo",
		0x1990, BIT(17), 0);

static SUNXI_CCU_GATE(video0pll4x_ga_clk, "video0pll4x-ga",
		"dcxo",
		0x1990, BIT(16), 0);

static SUNXI_CCU_GATE(gpu0pll_ga_clk, "gpu0pll-ga",
		"dcxo",
		0x1994, BIT(16), 0);

static SUNXI_CCU_GATE(ve1pll_ga_clk, "ve1pll-ga",
		"dcxo",
		0x1998, BIT(17), 0);

static SUNXI_CCU_GATE(ve0pll_ga_clk, "ve0pll-ga",
		"dcxo",
		0x1998, BIT(16), 0);

static SUNXI_CCU_GATE(audio1pll_div5_ga_clk, "audio1pll-div5-ga",
		"dcxo",
		0x199C, BIT(18), 0);

static SUNXI_CCU_GATE(audio1pll_div2_ga_clk, "audio1pll-div2-ga",
		"dcxo",
		0x199C, BIT(17), 0);

static SUNXI_CCU_GATE(audio0pll4x_ga_clk, "audio0pll4x-ga",
		"dcxo",
		0x199C, BIT(16), 0);

static SUNXI_CCU_GATE(npupll_ga_clk, "npupll-ga",
		"dcxo",
		0x19A0, BIT(16), 0);

static SUNXI_CCU_GATE(depll3x_ga_clk, "depll3x-ga",
		"dcxo",
		0x19A8, BIT(17), 0);

static SUNXI_CCU_GATE(depll4x_ga_clk, "depll4x-ga",
		"dcxo",
		0x19A8, BIT(16), 0);

static SUNXI_CCU_GATE(res_dcap_24m_clk, "res-dcap-24m",
		"dcxo",
		0x1A00, BIT(3), 0);

static const char * const apb2jtag_parents[] = { "sys24M", "ext-32k", "rc-16M", "pll-peri0-400m", "pll-peri1-480m", "pll-peri0-200m", "pll-peri1-200m" };
static SUNXI_CCU_M_WITH_MUX_GATE(apb2jtag_clk, "apb2jtag",
		apb2jtag_parents, 0x1C00,
		0, 5,	/* M */
		24, 3,	/* mux */
		BIT(31),	/* gate */
		0);

/* ccu_des_end */

/* rst_def_start */
static struct ccu_reset_map sun60iw2_ccu_resets[] = {
	[RST_BUS_ITS_PCIE0]		= { 0x0574, BIT(16) },
	[RST_BUS_NSI]			= { 0x0580, BIT(30) },
	[RST_BUS_NSI_CFG]		= { 0x0584, BIT(16) },
	[RST_BUS_IOMMU0_SY]		= { 0x058c, BIT(16) },
	[RST_BUS_MSI_LITE0_MBU]		= { 0x0594, BIT(17) },
	[RST_BUS_MSI_LITE0_AHB]		= { 0x0594, BIT(16) },
	[RST_BUS_MSI_LITE1_MBU]		= { 0x059c, BIT(17) },
	[RST_BUS_MSI_LITE1_AHB]		= { 0x059c, BIT(16) },
	[RST_BUS_MSI_LITE2_MBU]		= { 0x05a4, BIT(17) },
	[RST_BUS_MSI_LITE2_AHB]		= { 0x05a4, BIT(16) },
	[RST_BUS_IOMMU1_SY]		= { 0x05b4, BIT(16) },
	[RST_BUS_DMA0]			= { 0x0704, BIT(16) },
	[RST_BUS_DMA1]			= { 0x070c, BIT(16) },
	[RST_BUS_SPINLOCK]		= { 0x0724, BIT(16) },
	[RST_BUS_MSGBOX0]		= { 0x0744, BIT(16) },
	[RST_BUS_PWM0]			= { 0x0784, BIT(16) },
	[RST_BUS_PWM1]			= { 0x078c, BIT(16) },
	[RST_BUS_DBGSY]			= { 0x07a4, BIT(16) },
	[RST_BUS_SYSDAP]		= { 0x07ac, BIT(16) },
	[RST_BUS_TIMER0]		= { 0x0850, BIT(16) },
	[RST_BUS_DE0]			= { 0x0a04, BIT(16) },
	[RST_BUS_DI]			= { 0x0a24, BIT(16) },
	[RST_BUS_G2D]			= { 0x0a44, BIT(16) },
	[RST_BUS_EINK]			= { 0x0a6c, BIT(16) },
	[RST_BUS_DE_SY]			= { 0x0a74, BIT(16) },
	[RST_BUS_VE_DEC]		= { 0x0a8c, BIT(18) },
	[RST_BUS_VE_ENC0]		= { 0x0a8c, BIT(16) },
	[RST_BUS_CE_SY]			= { 0x0ac4, BIT(17) },
	[RST_BUS_CE]			= { 0x0ac4, BIT(16) },
	[RST_BUS_NPU_AHB]		= { 0x0b04, BIT(18) },
	[RST_BUS_NPU_AXI]		= { 0x0b04, BIT(17) },
	[RST_BUS_NPU_CORE]		= { 0x0b04, BIT(16) },
	[RST_BUS_GPU0]			= { 0x0b24, BIT(16) },
	[RST_BUS_DRAM0]			= { 0x0c0c, BIT(16) },
	[RST_BUS_NAND0]			= { 0x0c8c, BIT(16) },
	[RST_BUS_SMHC0]			= { 0x0d0c, BIT(16) },
	[RST_BUS_SMHC1]			= { 0x0d1c, BIT(16) },
	[RST_BUS_SMHC2]			= { 0x0d2c, BIT(16) },
	[RST_BUS_SMHC3]			= { 0x0d3c, BIT(16) },
	[RST_BUS_UFS_AXI]		= { 0x0d8c, BIT(17) },
	[RST_BUS_UF]			= { 0x0d8c, BIT(16) },
	[RST_BUS_UART0]			= { 0x0e00, BIT(16) },
	[RST_BUS_UART1]			= { 0x0e04, BIT(16) },
	[RST_BUS_UART2]			= { 0x0e08, BIT(16) },
	[RST_BUS_UART3]			= { 0x0e0c, BIT(16) },
	[RST_BUS_UART4]			= { 0x0e10, BIT(16) },
	[RST_BUS_UART5]			= { 0x0e14, BIT(16) },
	[RST_BUS_UART6]			= { 0x0e18, BIT(16) },
	[RST_BUS_TWI0]			= { 0x0e80, BIT(16) },
	[RST_BUS_TWI1]			= { 0x0e84, BIT(16) },
	[RST_BUS_TWI2]			= { 0x0e88, BIT(16) },
	[RST_BUS_TWI3]			= { 0x0e8c, BIT(16) },
	[RST_BUS_TWI4]			= { 0x0e90, BIT(16) },
	[RST_BUS_TWI5]			= { 0x0e94, BIT(16) },
	[RST_BUS_TWI6]			= { 0x0e98, BIT(16) },
	[RST_BUS_TWI7]			= { 0x0e9c, BIT(16) },
	[RST_BUS_TWI8]			= { 0x0ea0, BIT(16) },
	[RST_BUS_TWI9]			= { 0x0ea4, BIT(16) },
	[RST_BUS_TWI10]			= { 0x0ea8, BIT(16) },
	[RST_BUS_TWI11]			= { 0x0eac, BIT(16) },
	[RST_BUS_TWI12]			= { 0x0eb0, BIT(16) },
	[RST_BUS_SPI0]			= { 0x0f04, BIT(16) },
	[RST_BUS_SPI1]			= { 0x0f0c, BIT(16) },
	[RST_BUS_SPI2]			= { 0x0f14, BIT(16) },
	[RST_BUS_SPIF]			= { 0x0f1c, BIT(16) },
	[RST_BUS_SPI3]			= { 0x0f24, BIT(16) },
	[RST_BUS_SPI4]			= { 0x0f2c, BIT(16) },
	[RST_BUS_GPADC0]		= { 0x0fc4, BIT(16) },
	[RST_BUS_THS0]			= { 0x0fe4, BIT(16) },
	[RST_BUS_IRRX]			= { 0x1004, BIT(16) },
	[RST_BUS_IRTX]			= { 0x100c, BIT(16) },
	[RST_BUS_LRADC]			= { 0x1024, BIT(16) },
	[RST_BUS_SGPIO]			= { 0x1064, BIT(16) },
	[RST_BUS_LPC]			= { 0x1084, BIT(16) },
	[RST_BUS_I2SPCM0]		= { 0x120c, BIT(16) },
	[RST_BUS_I2SPCM1]		= { 0x121c, BIT(16) },
	[RST_BUS_I2SPCM2]		= { 0x122c, BIT(16) },
	[RST_BUS_I2SPCM3]		= { 0x123c, BIT(16) },
	[RST_BUS_SPDIF]			= { 0x128c, BIT(16) },
	[RST_BUS_DMIC]			= { 0x12cc, BIT(16) },
	[RST_USB_0_PHY_RSTN]		= { 0x1300, BIT(30) },
	[RST_USB_0_DEVICE]		= { 0x1304, BIT(24) },
	[RST_USB_0_EHCI]		= { 0x1304, BIT(20) },
	[RST_USB_0_OHCI]		= { 0x1304, BIT(16) },
	[RST_USB_1_PHY_RSTN]		= { 0x1308, BIT(30) },
	[RST_USB_1_EHCI]		= { 0x130c, BIT(20) },
	[RST_USB_1_OHCI]		= { 0x130c, BIT(16) },
	[RST_USB_2]			= { 0x135c, BIT(16) },
	[RST_BUS_PCIE0]			= { 0x138c, BIT(17) },
	[RST_BUS_PCIE0_PWRUP]		= { 0x138c, BIT(16) },
	[RST_BUS_SERDE]			= { 0x13c4, BIT(16) },
	[RST_BUS_GMAC0_AXI]		= { 0x141c, BIT(17) },
	[RST_BUS_GMAC0]			= { 0x141c, BIT(16) },
	[RST_BUS_GMAC1_AXI]		= { 0x142c, BIT(17) },
	[RST_BUS_GMAC1]			= { 0x142c, BIT(16) },
	[RST_BUS_VO0_TCONLCD0]		= { 0x1504, BIT(16) },
	[RST_BUS_VO0_TCONLCD1]		= { 0x150c, BIT(16) },
	[RST_BUS_VO0_TCONLCD2]		= { 0x1514, BIT(16) },
	[RST_BUS_LVDS0]			= { 0x1544, BIT(16) },
	[RST_BUS_LVDS1]			= { 0x154c, BIT(16) },
	[RST_BUS_DSI0]			= { 0x1584, BIT(16) },
	[RST_BUS_DSI1]			= { 0x158c, BIT(16) },
	[RST_BUS_TCONTV0]		= { 0x1604, BIT(16) },
	[RST_BUS_TCONTV1]		= { 0x160c, BIT(16) },
	[RST_BUS_EDP]			= { 0x164c, BIT(16) },
	[RST_BUS_HDMI_HDCP]		= { 0x168c, BIT(18) },
	[RST_BUS_HDMI_SUB]		= { 0x168c, BIT(17) },
	[RST_BUS_HDMI_MAIN]		= { 0x168c, BIT(16) },
	[RST_BUS_DPSS_TOP0]		= { 0x16c4, BIT(16) },
	[RST_BUS_DPSS_TOP1]		= { 0x16cc, BIT(16) },
	[RST_BUS_VIDEO_OUT0]		= { 0x16e4, BIT(16) },
	[RST_BUS_VIDEO_OUT1]		= { 0x16ec, BIT(16) },
	[RST_BUS_LEDC]			= { 0x1704, BIT(16) },
	[RST_BUS_DSC]			= { 0x1744, BIT(16) },
	[RST_BUS_CSI]			= { 0x1844, BIT(16) },
	[RST_BUS_VIDEO_IN]		= { 0x1884, BIT(16) },
	[RST_BUS_APB2JTAG]		= { 0x1C04, BIT(16) },
};
/* rst_def_end */

/* ccu_def_start */
static struct clk_hw_onecell_data sun60iw2_hw_clks = {
	.hws    = {
		[CLK_REFPLL]			= &pll_ref_clk.common.hw,
		[CLK_DDRPLL]			= &pll_ddr_clk.common.hw,
		[CLK_PLL_PERI0]			= &pll_peri0_clk.common.hw,
		[CLK_PLL_PERI0_2X]		= &pll_peri0_2x_clk.common.hw,
		[CLK_PERI0_DIV3]		= &pll_peri0_div3_clk.hw,
		[CLK_PLL_PERI0_800M]		= &pll_peri0_800m_clk.common.hw,
		[CLK_PLL_PERI0_480M]		= &pll_peri0_480m_clk.common.hw,
		[CLK_PLL_PERI0_600M]		= &pll_peri0_600m_clk.hw,
		[CLK_PLL_PERI0_400M]		= &pll_peri0_400m_clk.hw,
		[CLK_PLL_PERI0_300M]		= &pll_peri0_300m_clk.hw,
		[CLK_PLL_PERI0_200M]		= &pll_peri0_200m_clk.hw,
		[CLK_PLL_PERI0_160M]		= &pll_peri0_160m_clk.hw,
		[CLK_PLL_PERI0_150M]		= &pll_peri0_150m_clk.hw,
		[CLK_PLL_PERI1]			= &pll_peri1_clk.common.hw,
		[CLK_PLL_PERI1_2X]		= &pll_peri1_2x_clk.common.hw,
		[CLK_PLL_PERI1_800M]		= &pll_peri1_800m_clk.common.hw,
		[CLK_PLL_PERI1_480M]		= &pll_peri1_480m_clk.common.hw,
		[CLK_PLL_PERI1_600M]		= &pll_peri1_600m_clk.hw,
		[CLK_PLL_PERI1_400M]		= &pll_peri1_400m_clk.hw,
		[CLK_PLL_PERI1_300M]		= &pll_peri1_300m_clk.hw,
		[CLK_PLL_PERI1_200M]		= &pll_peri1_200m_clk.hw,
		[CLK_PLL_PERI1_160M]		= &pll_peri1_160m_clk.hw,
		[CLK_PLL_PERI1_150M]		= &pll_peri1_150m_clk.hw,
		[CLK_GPU0PLL]			= &pll_gpu0_clk.common.hw,
		[CLK_VIDEO0PLL4X]		= &pll_video0_4x.common.hw,
		[CLK_VIDEO0PLL3X]		= &pll_video0_3x.common.hw,
		[CLK_VIDEO1PLL4X]		= &pll_video1_4x.common.hw,
		[CLK_VIDEO2PLL4X]		= &pll_video2_4x.common.hw,
		[CLK_VE0PLL]			= &pll_ve0_clk.common.hw,
		[CLK_VE1PLL]			= &pll_ve1_clk.common.hw,
		[CLK_AUDIO0PLL4X]		= &pll_audio0_4x.common.hw,
		[CLK_AUDIO1PLL_DIV2]		= &pll_audio1_div2.common.hw,
		[CLK_NPUPLL]			= &pll_npu_clk.common.hw,
		[CLK_DEPLL4X]			= &pll_de_4x_clk.common.hw,
		[CLK_AHB]			= &ahb_clk.common.hw,
		[CLK_APB0]			= &apb0_clk.common.hw,
		[CLK_APB1]			= &apb1_clk.common.hw,
		[CLK_TRACE]			= &trace_clk.common.hw,
		[CLK_GIC]			= &gic_clk.common.hw,
		[CLK_CPU_PERI]			= &cpu_peri_clk.common.hw,
		[CLK_ITS_PCIE0_ACLK]		= &its_pcie0_aclk.common.hw,
		[CLK_ITS_PCIE0_HCLK]		= &its_pcie0_hclk.common.hw,
		[CLK_NSI]			= &nsi_clk.common.hw,
		[CLK_NSI_CFG]			= &nsi_cfg_clk.common.hw,
		[CLK_MBUS]			= &mbus_clk.common.hw,
		[CLK_IOMMU0_SYS_HCLK]		= &iommu0_sys_hclk.common.hw,
		[CLK_IOMMU0_SYS_PCLK]		= &iommu0_sys_pclk.common.hw,
		[CLK_IOMMU0_SYS_MCLK]		= &iommu0_sys_mclk.common.hw,
		[CLK_MSI_LITE0]			= &msi_lite0_clk.common.hw,
		[CLK_MSI_LITE1]			= &msi_lite1_clk.common.hw,
		[CLK_MSI_LITE2]			= &msi_lite2_clk.common.hw,
		[CLK_IOMMU1_SYS_HCLK]		= &iommu1_sys_hclk.common.hw,
		[CLK_IOMMU1_SYS_PCLK]		= &iommu1_sys_pclk.common.hw,
		[CLK_IOMMU1_SYS_MCLK]		= &iommu1_sys_mclk.common.hw,
		[CLK_CPUS_HCLK_GATE]		= &cpus_hclk_gate_clk.common.hw,
		[CLK_STORE_AHB_GATE]		= &store_ahb_gate_clk.common.hw,
		[CLK_MSILITE0_AHB_GATE]		= &msilite0_ahb_gate_clk.common.hw,
		[CLK_USB_SYS_AHB_GATE]		= &usb_sys_ahb_gate_clk.common.hw,
		[CLK_SERDES_AHB_GATE]		= &serdes_ahb_gate_clk.common.hw,
		[CLK_GPU0_AHB_GATE]		= &gpu0_ahb_gate_clk.common.hw,
		[CLK_NPU_AHB_GATE]		= &npu_ahb_gate_clk.common.hw,
		[CLK_DE_AHB_GATE]		= &de_ahb_gate_clk.common.hw,
		[CLK_VID_OUT1_AHB_GATE]		= &vid_out1_ahb_gate_clk.common.hw,
		[CLK_VID_OUT0_AHB_GATE]		= &vid_out0_ahb_gate_clk.common.hw,
		[CLK_VID_IN_AHB_GATE]		= &vid_in_ahb_gate_clk.common.hw,
		[CLK_VE_AHB_GATE]		= &ve_ahb_gate_clk.common.hw,
		[CLK_MSILITE2_MBUS_GATE]	= &msilite2_mbus_gate_clk.common.hw,
		[CLK_STORE_MBUS_GATE]		= &store_mbus_gate_clk.common.hw,
		[CLK_MSILITE0_MBUS_GATE]	= &msilite0_mbus_gate_clk.common.hw,
		[CLK_SERDES_MBUS_GATE]		= &serdes_mbus_gate_clk.common.hw,
		[CLK_VID_IN_MBUS_GATE]		= &vid_in_mbus_gate_clk.common.hw,
		[CLK_NPU_MBUS_GATE]		= &npu_mbus_gate_clk.common.hw,
		[CLK_GPU0_MBUS_GATE]		= &gpu0_mbus_gate_clk.common.hw,
		[CLK_VE_MBUS_GATE]		= &ve_mbus_gate_clk.common.hw,
		[CLK_DESYS_MBUS_GATE]		= &desys_mbus_gate_clk.common.hw,
		[CLK_GMAC1_MBUS]		= &gmac1_mclk.common.hw,
		[CLK_GMAC0_MBUS]		= &gmac0_mclk.common.hw,
		[CLK_ISP_MBUS]			= &isp_mclk.common.hw,
		[CLK_CSI_MBUS]			= &csi_mclk.common.hw,
		[CLK_NAND_MBUS]			= &nand_mclk.common.hw,
		[CLK_DMA1_MBUS]			= &dma1_mclk.common.hw,
		[CLK_CE_MBUS]			= &ce_mclk.common.hw,
		[CLK_VE_MBUS]			= &ve_mclk.common.hw,
		[CLK_DMA0_MBUS]			= &dma0_mclk.common.hw,
		[CLK_DMA0_BUS]			= &dma0_clk.common.hw,
		[CLK_DMA1_BUS]			= &dma1_clk.common.hw,
		[CLK_SPINLOCK]			= &spinlock_clk.common.hw,
		[CLK_MSGBOX0]			= &msgbox0_clk.common.hw,
		[CLK_PWM0]			= &pwm0_clk.common.hw,
		[CLK_PWM1]			= &pwm1_clk.common.hw,
		[CLK_DBGSYS]			= &dbgsys_clk.common.hw,
		[CLK_SYSDAP]			= &sysdap_clk.common.hw,
		[CLK_TIMER0_CLK0]		= &timer0_clk0.common.hw,
		[CLK_TIMER0_CLK1]		= &timer0_clk1.common.hw,
		[CLK_TIMER0_CLK2]		= &timer0_clk2.common.hw,
		[CLK_TIMER0_CLK3]		= &timer0_clk3.common.hw,
		[CLK_TIMER0_CLK4]		= &timer0_clk4.common.hw,
		[CLK_TIMER0_CLK5]		= &timer0_clk5.common.hw,
		[CLK_TIMER0_CLK6]		= &timer0_clk6.common.hw,
		[CLK_TIMER0_CLK7]		= &timer0_clk7.common.hw,
		[CLK_TIMER0_CLK8]		= &timer0_clk8.common.hw,
		[CLK_TIMER0_CLK9]		= &timer0_clk9.common.hw,
		[CLK_TIMER0]			= &timer0_clk.common.hw,
		[CLK_AVS]			= &avs_clk.common.hw,
		[CLK_DE0]			= &de0_clk.common.hw,
		[CLK_BUS_DE0]			= &de0_gate_clk.common.hw,
		[CLK_DI]			= &di_clk.common.hw,
		[CLK_BUS_DI]			= &di_gate_clk.common.hw,
		[CLK_G2D]			= &g2d_clk.common.hw,
		[CLK_BUS_G2D]			= &g2d_gate_clk.common.hw,
		[CLK_EINK]			= &eink_clk.common.hw,
		[CLK_EINK_PANEL]		= &eink_panel_clk.common.hw,
		[CLK_BUS_EINK]			= &eink_gate_clk.common.hw,
		[CLK_VE_ENC0]			= &ve_enc0_clk.common.hw,
		[CLK_VE_DEC]			= &ve_dec_clk.common.hw,
		[CLK_BUS_VE_DEC]		= &ve_dec_bus_clk.common.hw,
		[CLK_BUS_VE_ENC]		= &ve_enc_bus_clk.common.hw,
		[CLK_CE]			= &ce_clk.common.hw,
		[CLK_CE_SYS]			= &ce_sys_clk.common.hw,
		[CLK_BUS_CE]			= &ce_bus_clk.common.hw,
		[CLK_NPU]			= &npu_clk.common.hw,
		[CLK_BUS_NPU]			= &npu_bus_clk.common.hw,
		[CLK_GPU0]			= &gpu0_clk.common.hw,
		[CLK_BUS_GPU0]			= &gpu0_bus_clk.common.hw,
		[CLK_DRAM0]			= &dram0_clk.common.hw,
		[CLK_BUS_DRAM0]			= &dram0_bus_clk.common.hw,
		[CLK_NAND0_CLK0]		= &nand0_clk0_clk.common.hw,
		[CLK_NAND0_CLK1]		= &nand0_clk1_clk.common.hw,
		[CLK_BUS_NAND0]			= &nand0_bus_clk.common.hw,
		[CLK_SMHC0]			= &smhc0_clk.common.hw,
		[CLK_BUS_SMHC0]			= &smhc0_gate_clk.common.hw,
		[CLK_SMHC1]			= &smhc1_clk.common.hw,
		[CLK_BUS_SMHC1]			= &smhc1_gate_clk.common.hw,
		[CLK_SMHC2]			= &smhc2_clk.common.hw,
		[CLK_BUS_SMHC2]			= &smhc2_gate_clk.common.hw,
		[CLK_SMHC3]			= &smhc3_clk.common.hw,
		[CLK_BUS_SMHC3]			= &smhc3_bus_clk.common.hw,
		[CLK_UFS_AXI]			= &ufs_axi_clk.common.hw,
		[CLK_UFS_CFG]			= &ufs_cfg_clk.common.hw,
		[CLK_UFS]			= &ufs_clk.common.hw,
		[CLK_UART0]			= &uart0_clk.common.hw,
		[CLK_UART1]			= &uart1_clk.common.hw,
		[CLK_UART2]			= &uart2_clk.common.hw,
		[CLK_UART3]			= &uart3_clk.common.hw,
		[CLK_UART4]			= &uart4_clk.common.hw,
		[CLK_UART5]			= &uart5_clk.common.hw,
		[CLK_UART6]			= &uart6_clk.common.hw,
		[CLK_TWI0]			= &twi0_clk.common.hw,
		[CLK_TWI1]			= &twi1_clk.common.hw,
		[CLK_TWI2]			= &twi2_clk.common.hw,
		[CLK_TWI3]			= &twi3_clk.common.hw,
		[CLK_TWI4]			= &twi4_clk.common.hw,
		[CLK_TWI5]			= &twi5_clk.common.hw,
		[CLK_TWI6]			= &twi6_clk.common.hw,
		[CLK_TWI7]			= &twi7_clk.common.hw,
		[CLK_TWI8]			= &twi8_clk.common.hw,
		[CLK_TWI9]			= &twi9_clk.common.hw,
		[CLK_TWI10]			= &twi10_clk.common.hw,
		[CLK_TWI11]			= &twi11_clk.common.hw,
		[CLK_TWI12]			= &twi12_clk.common.hw,
		[CLK_SPI0]			= &spi0_clk.common.hw,
		[CLK_BUS_SPI0]			= &spi0_bus_clk.common.hw,
		[CLK_SPI1]			= &spi1_clk.common.hw,
		[CLK_BUS_SPI1]			= &spi1_bus_clk.common.hw,
		[CLK_SPI2]			= &spi2_clk.common.hw,
		[CLK_BUS_SPI2]			= &spi2_bus_clk.common.hw,
		[CLK_SPIF]			= &spif_clk.common.hw,
		[CLK_BUS_SPIF]			= &spif_bus_clk.common.hw,
		[CLK_SPI3]			= &spi3_clk.common.hw,
		[CLK_BUS_SPI3]			= &spi3_bus_clk.common.hw,
		[CLK_SPI4]			= &spi4_clk.common.hw,
		[CLK_BUS_SPI4]			= &spi4_bus_clk.common.hw,
		[CLK_GPADC0_24M]		= &gpadc0_24m_clk.common.hw,
		[CLK_GPADC0]			= &gpadc0_clk.common.hw,
		[CLK_THS0]			= &ths0_clk.common.hw,
		[CLK_IRRX]			= &irrx_clk.common.hw,
		[CLK_BUS_IRRX]			= &irrx_gate_clk.common.hw,
		[CLK_IRTX]			= &irtx_clk.common.hw,
		[CLK_BUS_IRTX]			= &irtx_gate_clk.common.hw,
		[CLK_LRADC]			= &lradc_clk.common.hw,
		[CLK_SGPIO]			= &sgpio_clk.common.hw,
		[CLK_BUS_SGPIO]			= &sgpio_bus_clk.common.hw,
		[CLK_LPC]			= &lpc_clk.common.hw,
		[CLK_BUS_LPC]			= &lpc_bus_clk.common.hw,
		[CLK_I2SPCM0]			= &i2spcm0_clk.common.hw,
		[CLK_BUS_I2SPCM0]		= &i2spcm0_gate_clk.common.hw,
		[CLK_I2SPCM1]			= &i2spcm1_clk.common.hw,
		[CLK_BUS_I2SPCM1]		= &i2spcm1_bus_clk.common.hw,
		[CLK_I2SPCM2]			= &i2spcm2_clk.common.hw,
		[CLK_I2SPCM2_ASRC]		= &i2spcm2_asrc_clk.common.hw,
		[CLK_BUS_I2SPCM2]		= &i2spcm2_bus_clk.common.hw,
		[CLK_I2SPCM3]			= &i2spcm3_clk.common.hw,
		[CLK_BUS_I2SPCM3]		= &i2spcm3_bus_clk.common.hw,
		[CLK_SPDIF_TX]			= &spdif_tx_clk.common.hw,
		[CLK_SPDIF_RX]			= &spdif_rx_clk.common.hw,
		[CLK_SPDIF]			= &spdif_clk.common.hw,
		[CLK_DMIC]			= &dmic_clk.common.hw,
		[CLK_BUS_DMIC]			= &dmic_bus_clk.common.hw,
		[CLK_USB]			= &usb_clk.common.hw,
		[CLK_USB0_DEVICE]		= &usb0_device_clk.common.hw,
		[CLK_USB0_EHCI]			= &usb0_ehci_clk.common.hw,
		[CLK_USB0_OHCI]			= &usb0_ohci_clk.common.hw,
		[CLK_USB1]			= &usb1_clk.common.hw,
		[CLK_USB1_EHCI]			= &usb1_ehci_clk.common.hw,
		[CLK_USB1_OHCI]			= &usb1_ohci_clk.common.hw,
		[CLK_USB_REF]			= &usb_ref_clk.common.hw,
		[CLK_USB2_U2_REF]		= &usb2_u2_ref_clk.common.hw,
		[CLK_USB2_SUSPEND]		= &usb2_suspend_clk.common.hw,
		[CLK_USB2_MF]			= &usb2_mf_clk.common.hw,
		[CLK_USB2_U3_UTMI]		= &usb2_u3_utmi_clk.common.hw,
		[CLK_USB2_U2_PIPE]		= &usb2_u2_pipe_clk.common.hw,
		[CLK_PCIE0_AUX]			= &pcie0_aux_clk.common.hw,
		[CLK_PCIE0_AXI_SLV]		= &pcie0_axi_slv_clk.common.hw,
		[CLK_SERDES_PHY_CFG]		= &serdes_phy_cfg_clk.common.hw,
		[CLK_GMAC_PTP]			= &gmac_ptp_clk.common.hw,
		[CLK_GMAC0_PHY]			= &gmac0_phy_clk.common.hw,
		[CLK_GMAC0]			= &gmac0_clk.common.hw,
		[CLK_GMAC1_PHY]			= &gmac1_phy_clk.common.hw,
		[CLK_GMAC1]			= &gmac1_clk.common.hw,
		[CLK_VO0_TCONLCD0]		= &vo0_tconlcd0_clk.common.hw,
		[CLK_BUS_VO0_TCONLCD0]		= &vo0_tconlcd0_bus_clk.common.hw,
		[CLK_VO0_TCONLCD1]		= &vo0_tconlcd1_clk.common.hw,
		[CLK_BUS_VO0_TCONLCD1]		= &vo0_tconlcd1_bus_clk.common.hw,
		[CLK_VO0_TCONLCD2]		= &vo0_tconlcd2_clk.common.hw,
		[CLK_BUS_VO0_TCONLCD2]		= &vo0_tconlcd2_bus_clk.common.hw,
		[CLK_DSI0]			= &dsi0_clk.common.hw,
		[CLK_BUS_DSI0]			= &dsi0_bus_clk.common.hw,
		[CLK_DSI1]			= &dsi1_clk.common.hw,
		[CLK_BUS_DSI1]			= &dsi1_bus_clk.common.hw,
		[CLK_COMBPHY0]			= &combphy0_clk.common.hw,
		[CLK_COMBPHY1]			= &combphy1_clk.common.hw,
		[CLK_TCONTV0]			= &tcontv0_clk.common.hw,
		[CLK_TCONTV1]			= &tcontv1_clk.common.hw,
		[CLK_EDP_TV]			= &edp_tv_clk.common.hw,
		[CLK_EDP]			= &edp_clk.common.hw,
		[CLK_HDMI_REF]			= &hdmi_ref_clk.common.hw,
		[CLK_HDMI_TV]			= &hdmi_tv_clk.common.hw,
		[CLK_HDMI]			= &hdmi_clk.common.hw,
		[CLK_HDMI_SFR]			= &hdmi_sfr_clk.common.hw,
		[CLK_HDCP_ESM]			= &hdcp_esm_clk.common.hw,
		[CLK_DPSS_TOP0]			= &dpss_top0_clk.common.hw,
		[CLK_DPSS_TOP1]			= &dpss_top1_clk.common.hw,
		[CLK_LEDC]			= &ledc_clk.common.hw,
		[CLK_BUS_LEDC]			= &ledc_bus_clk.common.hw,
		[CLK_DSC]			= &dsc_clk.common.hw,
		[CLK_CSI_MASTER0]		= &csi_master0_clk.common.hw,
		[CLK_CSI_MASTER1]		= &csi_master1_clk.common.hw,
		[CLK_CSI_MASTER2]		= &csi_master2_clk.common.hw,
		[CLK_CSI]			= &csi_clk.common.hw,
		[CLK_BUS_CSI]			= &csi_bus_clk.common.hw,
		[CLK_ISP]			= &isp_clk.common.hw,
		[CLK_DDRPLL_GATE]		= &ddrpll_gate_clk.common.hw,
		[CLK_DDRPLL_AUTO]		= &ddrpll_auto__clk.common.hw,
		[CLK_PERI0_300M_DSP]		= &peri0_300m_dsp__clk.common.hw,
		[CLK_PERI0PLL2X_GATE]		= &pll_peri0_2x_gate_clk.common.hw,
		[CLK_PERI0_800M_GATE]		= &peri0_800m_gate_clk.common.hw,
		[CLK_PERI0_600M_GATE]		= &peri0_600m_gate_clk.common.hw,
		[CLK_PERI0_480M_GATE_ALL]	= &peri0_480m_gate_all_clk.common.hw,
		[CLK_PERI0_480M_GATE_SW]	= &peri0_480m_gate_clk.common.hw,
		[CLK_PERI0_160M_GATE]		= &peri0_160m_gate_clk.common.hw,
		[CLK_PERI0_300M_GATE]		= &peri0_300m_gate_clk.common.hw,
		[CLK_PERI0_300M_SW]		= &peri0_300m_sw_clk.common.hw,
		[CLK_PERI0_150M_GATE]		= &peri0_150m_gate_clk.common.hw,
		[CLK_PERI0_400M_GATE]		= &peri0_400m_gate_clk.common.hw,
		[CLK_PERI0_400M_SW]		= &peri0_400m_sw_clk.common.hw,
		[CLK_PERI0_200M_GATE]		= &peri0_200m_gate_clk.common.hw,
		[CLK_PERI0PLL2X_AUTO]		= &pll_peri0_2x_auto_clk.common.hw,
		[CLK_PERI0_800M_AUTO]		= &peri0_800m_auto_clk.common.hw,
		[CLK_PERI0_600M_AUTO]		= &peri0_600m_auto__clk.common.hw,
		[CLK_PERI0_480M_AUTO_GATE]	= &peri0_480m_auto_gate_clk.common.hw,
		[CLK_PERI0_480M_AUTO]		= &peri0_480m_auto__clk.common.hw,
		[CLK_PERI0_160M_AUTO]		= &peri0_160m_auto__clk.common.hw,
		[CLK_PERI0_300M_AUTO_GATE]	= &peri0_300m_auto_gate_clk.common.hw,
		[CLK_PERI0_300M_AUTO]		= &peri0_300m_auto__clk.common.hw,
		[CLK_PERI0_150M_AUTO]		= &peri0_150m_auto__clk.common.hw,
		[CLK_PERI0_400M_AUTO_GATE]	= &peri0_400m_auto_gate_clk.common.hw,
		[CLK_PERI0_400M_AUTO]		= &peri0_400m_auto__clk.common.hw,
		[CLK_PERI0_200M_AUTO]		= &peri0_200m_auto__clk.common.hw,
		[CLK_PERI1_300M_DSP]		= &peri1_300m_dsp__clk.common.hw,
		[CLK_PERI1_800M_GATE]		= &peri1_800m_gate_clk.common.hw,
		[CLK_PERI1_600M_GATE]		= &peri1_600m_gate_clk.common.hw,
		[CLK_PERI1_600M_SW]		= &peri1_600m_sw_clk.common.hw,
		[CLK_PERI1_480M_GATE]		= &peri1_480m_gate_clk.common.hw,
		[CLK_PERI1_480M_SW]		= &peri1_480m_sw_clk.common.hw,
		[CLK_PERI1_160M_GATE]		= &peri1_160m_gate_clk.common.hw,
		[CLK_PERI1_300M_GATE]		= &peri1_300m_gate_clk.common.hw,
		[CLK_PERI1_300M_SW]		= &peri1_300m_sw_clk.common.hw,
		[CLK_PERI1_150M_GATE]		= &peri1_150m_gate_clk.common.hw,
		[CLK_PERI1_400M_GATE]		= &peri1_400m_gate__clk.common.hw,
		[CLK_PERI1_400M_SW]		= &peri1_400m_gate_clk.common.hw,
		[CLK_PERI1_200M_GATE]		= &peri1_200m_gate_clk.common.hw,
		[CLK_PERI1_800M_AUTO]		= &peri1_800m_auto__clk.common.hw,
		[CLK_PERI1_600M_AUTO_GATE]	= &peri1_600m_auto_gate_clk.common.hw,
		[CLK_PERI1_600M_AUTO]		= &peri1_600m_auto__clk.common.hw,
		[CLK_PERI1_480M_AUTO_GATE]	= &peri1_480m_auto_gate_clk.common.hw,
		[CLK_PERI1_480M_AUTO]		= &peri1_480m_auto__clk.common.hw,
		[CLK_PERI1_160M_AUTO]		= &peri1_160m_auto__clk.common.hw,
		[CLK_PERI1_300M_AUTO_GATE]	= &peri1_300m_auto_gate_clk.common.hw,
		[CLK_PERI1_300M_AUTO]		= &peri1_300m_auto__clk.common.hw,
		[CLK_PERI1_150M_AUTO]		= &peri1_150m_auto__clk.common.hw,
		[CLK_PERI1_400M_AUTO_GATE]	= &peri1_400m_auto_gate_clk.common.hw,
		[CLK_PERI1_400M_AUTO]		= &peri1_400m_auto__clk.common.hw,
		[CLK_PERI1_200M_AUTO]		= &peri1_200m_auto__clk.common.hw,
		[CLK_VIDEO2PLL3X_GATE]		= &video2pll3x_gate_clk.common.hw,
		[CLK_VIDEO1PLL3X_GATE]		= &video1pll3x_gate_clk.common.hw,
		[CLK_VIDEO0PLL3X_GATE]		= &video0pll3x_gate_clk.common.hw,
		[CLK_VIDEO2PLL4X_GATE]		= &video2pll4x_gate_clk.common.hw,
		[CLK_VIDEO1PLL4X_GATE]		= &video1pll4x_gate_clk.common.hw,
		[CLK_VIDEO0PLL4X_GATE]		= &video0pll4x_gate_clk.common.hw,
		[CLK_VIDEO2PLL3X_AUTO]		= &video2pll3x_auto__clk.common.hw,
		[CLK_VIDEO1PLL3X_AUTO]		= &video1pll3x_auto__clk.common.hw,
		[CLK_VIDEO0PLL3X_AUTO]		= &video0pll3x_auto__clk.common.hw,
		[CLK_VIDEO2PLL4X_AUTO]		= &video2pll4x_auto__clk.common.hw,
		[CLK_VIDEO1PLL4X_AUTO]		= &video1pll4x_auto__clk.common.hw,
		[CLK_VIDEO0PLL4X_AUTO]		= &video0pll4x_auto__clk.common.hw,
		[CLK_GPU0PLL_GATE]		= &gpu0pll_gate_clk.common.hw,
		[CLK_GPU0PLL_AUTO]		= &gpu0pll_auto__clk.common.hw,
		[CLK_VE1PLL_GATE]		= &ve1pll_gate_clk.common.hw,
		[CLK_VE0PLL_GATE]		= &ve0pll_gate_clk.common.hw,
		[CLK_VE1PLL_AUTO]		= &ve1pll_auto__clk.common.hw,
		[CLK_VE0PLL_AUTO]		= &ve0pll_auto__clk.common.hw,
		[CLK_AUDIO1PLL_DIV5_GATE]	= &audio1pll_div5_gate_clk.common.hw,
		[CLK_AUDIO1PLL_DIV2_GATE]	= &audio1pll_div2_gate_clk.common.hw,
		[CLK_AUDIO0PLL4X_GATE]		= &audio0pll4x_gate_clk.common.hw,
		[CLK_AUDIO1PLL_DIV5_AUTO]	= &audio1pll_div5_auto__clk.common.hw,
		[CLK_AUDIO1PLL_DIV2_AUTO]	= &audio1pll_div2_auto__clk.common.hw,
		[CLK_AUDIO0PLL4X_AUTO]		= &audio0pll4x_auto__clk.common.hw,
		[CLK_NPUPLL_GATE]		= &npupll_gate_clk.common.hw,
		[CLK_NPUPLL_AUTO]		= &npupll_auto_clk.common.hw,
		[CLK_DEPLL3X_GATE]		= &depll3x_gate_clk.common.hw,
		[CLK_DEPLL4X_GATE]		= &depll4x_gate_clk.common.hw,
		[CLK_DEPLL3X_AUTO]		= &depll3x_auto_clk.common.hw,
		[CLK_DEPLL4X_AUTO]		= &depll4x_auto_clk.common.hw,
		[CLK_DDRPLL_STAT]			= &ddrpll_ga_clk.common.hw,
		[CLK_PERI0PLL2X_STAT]		= &pll_peri0_2x_ga_clk.common.hw,
		[CLK_PERI0_800M_STAT]		= &peri0_800m_ga_clk.common.hw,
		[CLK_PERI0_600M_STAT]		= &peri0_600m_ga_clk.common.hw,
		[CLK_PERI0_480M_GATE_A]		= &peri0_480m_gate_a_clk.common.hw,
		[CLK_PERI0_480M_STAT]		= &peri0_480m_ga_clk.common.hw,
		[CLK_PERI0_160M_STAT]		= &peri0_160m_stat_clk.common.hw,
		[CLK_PERI0_300M_GATE_A]		= &peri0_300m_gate_a_clk.common.hw,
		[CLK_PERI0_300M_STAT]		= &peri0_300m_ga_clk.common.hw,
		[CLK_PERI0_150M_STAT]		= &peri0_150m_ga_clk.common.hw,
		[CLK_PERI0_400M_GATE_A]		= &peri0_400m_gate_a_clk.common.hw,
		[CLK_PERI0_400M_STATUS]		= &peri0_400m_ga_clk.common.hw,
		[CLK_PERI0_200M_STAT]		= &peri0_200m_ga_clk.common.hw,
		[CLK_PERI1_800M_STAT]		= &peri1_800m_ga_clk.common.hw,
		[CLK_PERI1_600M_GATE_A]		= &peri1_600m_gate_a_clk.common.hw,
		[CLK_PERI1_600M_STAT]		= &peri1_600m_stat_clk.common.hw,
		[CLK_PERI1_480M_GATE_A]		= &peri1_480m_gate_a_clk.common.hw,
		[CLK_PERI1_480M_STAT]		= &peri1_480m_ga_clk.common.hw,
		[CLK_PERI1_160M_STAT]		= &peri1_160m_ga_clk.common.hw,
		[CLK_PERI1_300M_GATE_A]		= &peri1_300m_gate_a_clk.common.hw,
		[CLK_PERI1_300M_STAT]		= &peri1_300m_ga_clk.common.hw,
		[CLK_PERI1_150M_STAT]		= &peri1_150m_ga_clk.common.hw,
		[CLK_PERI1_400M_GATE_A]		= &peri1_400m_gate_a_clk.common.hw,
		[CLK_PERI1_400M_STAT]		= &peri1_400m_ga_clk.common.hw,
		[CLK_PERI1_200M_STAT]		= &peri1_200m_ga_clk.common.hw,
		[CLK_VIDEO2PLL3X_STAT]		= &video2pll3x_ga_clk.common.hw,
		[CLK_VIDEO1PLL3X_STAT]		= &video1pll3x_ga_clk.common.hw,
		[CLK_VIDEO0PLL3X_STAT]		= &video0pll3x_ga_clk.common.hw,
		[CLK_VIDEO2PLL4X_STAT]		= &video2pll4x_ga_clk.common.hw,
		[CLK_VIDEO1PLL4X_STAT]		= &video1pll4x_ga_clk.common.hw,
		[CLK_VIDEO0PLL4X_STAT]		= &video0pll4x_ga_clk.common.hw,
		[CLK_GPU0PLL_STAT]		= &gpu0pll_ga_clk.common.hw,
		[CLK_VE1PLL_STAT]		= &ve1pll_ga_clk.common.hw,
		[CLK_VE0PLL_STAT]		= &ve0pll_ga_clk.common.hw,
		[CLK_AUDIO1PLL_DIV5_STAT]	= &audio1pll_div5_ga_clk.common.hw,
		[CLK_AUDIO1PLL_DIV2_STAT]	= &audio1pll_div2_ga_clk.common.hw,
		[CLK_AUDIO0PLL4X_STAT]		= &audio0pll4x_ga_clk.common.hw,
		[CLK_NPUPLL_STAT]		= &npupll_ga_clk.common.hw,
		[CLK_DEPLL3X_STAT]		= &depll3x_ga_clk.common.hw,
		[CLK_DEPLL4X_STAT]		= &depll4x_ga_clk.common.hw,
		[CLK_RES_DCAP_24M]		= &res_dcap_24m_clk.common.hw,
		[CLK_APB2JTAG]			= &apb2jtag_clk.common.hw,
	},
	.num = CLK_NUMBER,
};
/* ccu_def_end */

static struct ccu_common *sun60iw2_ccu_clks[] = {
	&pll_ref_clk.common,
	&pll_ddr_clk.common,
	&pll_peri0_clk.common,
	&pll_peri0_2x_clk.common,
	&pll_peri0_800m_clk.common,
	&pll_peri0_480m_clk.common,
	&pll_peri1_clk.common,
	&pll_peri1_2x_clk.common,
	&pll_peri1_800m_clk.common,
	&pll_peri1_480m_clk.common,
	&pll_gpu0_clk.common,
	&pll_video0_4x.common,
	&pll_video0_3x.common,
	&pll_video1_4x.common,
	&pll_video2_4x.common,
	&pll_ve0_clk.common,
	&pll_ve1_clk.common,
	&pll_audio0_4x.common,
	&pll_audio1_div2.common,
	&pll_npu_clk.common,
	&pll_de_4x_clk.common,
	&ahb_clk.common,
	&apb0_clk.common,
	&apb1_clk.common,
	&trace_clk.common,
	&gic_clk.common,
	&cpu_peri_clk.common,
	&its_pcie0_aclk.common,
	&its_pcie0_hclk.common,
	&nsi_clk.common,
	&nsi_cfg_clk.common,
	&mbus_clk.common,
	&iommu0_sys_hclk.common,
	&iommu0_sys_pclk.common,
	&iommu0_sys_mclk.common,
	&msi_lite0_clk.common,
	&msi_lite1_clk.common,
	&msi_lite2_clk.common,
	&iommu1_sys_hclk.common,
	&iommu1_sys_pclk.common,
	&iommu1_sys_mclk.common,
	&cpus_hclk_gate_clk.common,
	&store_ahb_gate_clk.common,
	&msilite0_ahb_gate_clk.common,
	&usb_sys_ahb_gate_clk.common,
	&serdes_ahb_gate_clk.common,
	&gpu0_ahb_gate_clk.common,
	&npu_ahb_gate_clk.common,
	&de_ahb_gate_clk.common,
	&vid_out1_ahb_gate_clk.common,
	&vid_out0_ahb_gate_clk.common,
	&vid_in_ahb_gate_clk.common,
	&ve_ahb_gate_clk.common,
	&msilite2_mbus_gate_clk.common,
	&store_mbus_gate_clk.common,
	&msilite0_mbus_gate_clk.common,
	&serdes_mbus_gate_clk.common,
	&vid_in_mbus_gate_clk.common,
	&npu_mbus_gate_clk.common,
	&gpu0_mbus_gate_clk.common,
	&ve_mbus_gate_clk.common,
	&desys_mbus_gate_clk.common,
	&gmac1_mclk.common,
	&gmac0_mclk.common,
	&isp_mclk.common,
	&csi_mclk.common,
	&nand_mclk.common,
	&dma1_mclk.common,
	&ce_mclk.common,
	&ve_mclk.common,
	&dma0_mclk.common,
	&dma0_clk.common,
	&dma1_clk.common,
	&spinlock_clk.common,
	&msgbox0_clk.common,
	&pwm0_clk.common,
	&pwm1_clk.common,
	&dbgsys_clk.common,
	&sysdap_clk.common,
	&timer0_clk0.common,
	&timer0_clk1.common,
	&timer0_clk2.common,
	&timer0_clk3.common,
	&timer0_clk4.common,
	&timer0_clk5.common,
	&timer0_clk6.common,
	&timer0_clk7.common,
	&timer0_clk8.common,
	&timer0_clk9.common,
	&timer0_clk.common,
	&avs_clk.common,
	&de0_clk.common,
	&de0_gate_clk.common,
	&di_clk.common,
	&di_gate_clk.common,
	&g2d_clk.common,
	&g2d_gate_clk.common,
	&eink_clk.common,
	&eink_panel_clk.common,
	&eink_gate_clk.common,
	&ve_enc0_clk.common,
	&ve_dec_clk.common,
	&ve_dec_bus_clk.common,
	&ve_enc_bus_clk.common,
	&ce_clk.common,
	&ce_sys_clk.common,
	&ce_bus_clk.common,
	&npu_clk.common,
	&npu_bus_clk.common,
	&gpu0_clk.common,
	&gpu0_bus_clk.common,
	&dram0_clk.common,
	&dram0_bus_clk.common,
	&nand0_clk0_clk.common,
	&nand0_clk1_clk.common,
	&nand0_bus_clk.common,
	&smhc0_clk.common,
	&smhc0_gate_clk.common,
	&smhc1_clk.common,
	&smhc1_gate_clk.common,
	&smhc2_clk.common,
	&smhc2_gate_clk.common,
	&smhc3_clk.common,
	&smhc3_bus_clk.common,
	&ufs_axi_clk.common,
	&ufs_cfg_clk.common,
	&ufs_clk.common,
	&uart0_clk.common,
	&uart1_clk.common,
	&uart2_clk.common,
	&uart3_clk.common,
	&uart4_clk.common,
	&uart5_clk.common,
	&uart6_clk.common,
	&twi0_clk.common,
	&twi1_clk.common,
	&twi2_clk.common,
	&twi3_clk.common,
	&twi4_clk.common,
	&twi5_clk.common,
	&twi6_clk.common,
	&twi7_clk.common,
	&twi8_clk.common,
	&twi9_clk.common,
	&twi10_clk.common,
	&twi11_clk.common,
	&twi12_clk.common,
	&spi0_clk.common,
	&spi0_bus_clk.common,
	&spi1_clk.common,
	&spi1_bus_clk.common,
	&spi2_clk.common,
	&spi2_bus_clk.common,
	&spif_clk.common,
	&spif_bus_clk.common,
	&spi3_clk.common,
	&spi3_bus_clk.common,
	&spi4_clk.common,
	&spi4_bus_clk.common,
	&gpadc0_24m_clk.common,
	&gpadc0_clk.common,
	&ths0_clk.common,
	&irrx_clk.common,
	&irrx_gate_clk.common,
	&irtx_clk.common,
	&irtx_gate_clk.common,
	&lradc_clk.common,
	&sgpio_clk.common,
	&sgpio_bus_clk.common,
	&lpc_clk.common,
	&lpc_bus_clk.common,
	&i2spcm0_clk.common,
	&i2spcm0_gate_clk.common,
	&i2spcm1_clk.common,
	&i2spcm1_bus_clk.common,
	&i2spcm2_clk.common,
	&i2spcm2_asrc_clk.common,
	&i2spcm2_bus_clk.common,
	&i2spcm3_clk.common,
	&i2spcm3_bus_clk.common,
	&spdif_tx_clk.common,
	&spdif_rx_clk.common,
	&spdif_clk.common,
	&dmic_clk.common,
	&dmic_bus_clk.common,
	&usb_clk.common,
	&usb0_device_clk.common,
	&usb0_ehci_clk.common,
	&usb0_ohci_clk.common,
	&usb1_clk.common,
	&usb1_ehci_clk.common,
	&usb1_ohci_clk.common,
	&usb_ref_clk.common,
	&usb2_u2_ref_clk.common,
	&usb2_suspend_clk.common,
	&usb2_mf_clk.common,
	&usb2_u3_utmi_clk.common,
	&usb2_u2_pipe_clk.common,
	&pcie0_aux_clk.common,
	&pcie0_axi_slv_clk.common,
	&serdes_phy_cfg_clk.common,
	&gmac_ptp_clk.common,
	&gmac0_phy_clk.common,
	&gmac0_clk.common,
	&gmac1_phy_clk.common,
	&gmac1_clk.common,
	&vo0_tconlcd0_clk.common,
	&vo0_tconlcd0_bus_clk.common,
	&vo0_tconlcd1_clk.common,
	&vo0_tconlcd1_bus_clk.common,
	&vo0_tconlcd2_clk.common,
	&vo0_tconlcd2_bus_clk.common,
	&dsi0_clk.common,
	&dsi0_bus_clk.common,
	&dsi1_clk.common,
	&dsi1_bus_clk.common,
	&combphy0_clk.common,
	&combphy1_clk.common,
	&tcontv0_clk.common,
	&tcontv1_clk.common,
	&edp_tv_clk.common,
	&edp_clk.common,
	&hdmi_ref_clk.common,
	&hdmi_tv_clk.common,
	&hdmi_clk.common,
	&hdmi_sfr_clk.common,
	&hdcp_esm_clk.common,
	&dpss_top0_clk.common,
	&dpss_top1_clk.common,
	&ledc_clk.common,
	&ledc_bus_clk.common,
	&dsc_clk.common,
	&csi_master0_clk.common,
	&csi_master1_clk.common,
	&csi_master2_clk.common,
	&csi_clk.common,
	&csi_bus_clk.common,
	&isp_clk.common,
	&ddrpll_gate_clk.common,
	&ddrpll_auto__clk.common,
	&peri0_300m_dsp__clk.common,
	&pll_peri0_2x_gate_clk.common,
	&peri0_800m_gate_clk.common,
	&peri0_600m_gate_clk.common,
	&peri0_480m_gate_all_clk.common,
	&peri0_480m_gate_clk.common,
	&peri0_160m_gate_clk.common,
	&peri0_300m_gate_clk.common,
	&peri0_300m_sw_clk.common,
	&peri0_150m_gate_clk.common,
	&peri0_400m_gate_clk.common,
	&peri0_400m_sw_clk.common,
	&peri0_200m_gate_clk.common,
	&pll_peri0_2x_auto_clk.common,
	&peri0_800m_auto_clk.common,
	&peri0_600m_auto__clk.common,
	&peri0_480m_auto_gate_clk.common,
	&peri0_480m_auto__clk.common,
	&peri0_160m_auto__clk.common,
	&peri0_300m_auto_gate_clk.common,
	&peri0_300m_auto__clk.common,
	&peri0_150m_auto__clk.common,
	&peri0_400m_auto_gate_clk.common,
	&peri0_400m_auto__clk.common,
	&peri0_200m_auto__clk.common,
	&peri1_300m_dsp__clk.common,
	&peri1_800m_gate_clk.common,
	&peri1_600m_gate_clk.common,
	&peri1_600m_sw_clk.common,
	&peri1_480m_gate_clk.common,
	&peri1_480m_sw_clk.common,
	&peri1_160m_gate_clk.common,
	&peri1_300m_gate_clk.common,
	&peri1_300m_sw_clk.common,
	&peri1_150m_gate_clk.common,
	&peri1_400m_gate__clk.common,
	&peri1_400m_gate_clk.common,
	&peri1_200m_gate_clk.common,
	&peri1_800m_auto__clk.common,
	&peri1_600m_auto_gate_clk.common,
	&peri1_600m_auto__clk.common,
	&peri1_480m_auto_gate_clk.common,
	&peri1_480m_auto__clk.common,
	&peri1_160m_auto__clk.common,
	&peri1_300m_auto_gate_clk.common,
	&peri1_300m_auto__clk.common,
	&peri1_150m_auto__clk.common,
	&peri1_400m_auto_gate_clk.common,
	&peri1_400m_auto__clk.common,
	&peri1_200m_auto__clk.common,
	&video2pll3x_gate_clk.common,
	&video1pll3x_gate_clk.common,
	&video0pll3x_gate_clk.common,
	&video2pll4x_gate_clk.common,
	&video1pll4x_gate_clk.common,
	&video0pll4x_gate_clk.common,
	&video2pll3x_auto__clk.common,
	&video1pll3x_auto__clk.common,
	&video0pll3x_auto__clk.common,
	&video2pll4x_auto__clk.common,
	&video1pll4x_auto__clk.common,
	&video0pll4x_auto__clk.common,
	&gpu0pll_gate_clk.common,
	&gpu0pll_auto__clk.common,
	&ve1pll_gate_clk.common,
	&ve0pll_gate_clk.common,
	&ve1pll_auto__clk.common,
	&ve0pll_auto__clk.common,
	&audio1pll_div5_gate_clk.common,
	&audio1pll_div2_gate_clk.common,
	&audio0pll4x_gate_clk.common,
	&audio1pll_div5_auto__clk.common,
	&audio1pll_div2_auto__clk.common,
	&audio0pll4x_auto__clk.common,
	&npupll_gate_clk.common,
	&npupll_auto_clk.common,
	&depll3x_gate_clk.common,
	&depll4x_gate_clk.common,
	&depll3x_auto_clk.common,
	&depll4x_auto_clk.common,
	&ddrpll_ga_clk.common,
	&pll_peri0_2x_ga_clk.common,
	&peri0_800m_ga_clk.common,
	&peri0_600m_ga_clk.common,
	&peri0_480m_gate_a_clk.common,
	&peri0_480m_ga_clk.common,
	&peri0_160m_stat_clk.common,
	&peri0_300m_gate_a_clk.common,
	&peri0_300m_ga_clk.common,
	&peri0_150m_ga_clk.common,
	&peri0_400m_gate_a_clk.common,
	&peri0_400m_ga_clk.common,
	&peri0_200m_ga_clk.common,
	&peri1_800m_ga_clk.common,
	&peri1_600m_gate_a_clk.common,
	&peri1_600m_stat_clk.common,
	&peri1_480m_gate_a_clk.common,
	&peri1_480m_ga_clk.common,
	&peri1_160m_ga_clk.common,
	&peri1_300m_gate_a_clk.common,
	&peri1_300m_ga_clk.common,
	&peri1_150m_ga_clk.common,
	&peri1_400m_gate_a_clk.common,
	&peri1_400m_ga_clk.common,
	&peri1_200m_ga_clk.common,
	&video2pll3x_ga_clk.common,
	&video1pll3x_ga_clk.common,
	&video0pll3x_ga_clk.common,
	&video2pll4x_ga_clk.common,
	&video1pll4x_ga_clk.common,
	&video0pll4x_ga_clk.common,
	&gpu0pll_ga_clk.common,
	&ve1pll_ga_clk.common,
	&ve0pll_ga_clk.common,
	&audio1pll_div5_ga_clk.common,
	&audio1pll_div2_ga_clk.common,
	&audio0pll4x_ga_clk.common,
	&npupll_ga_clk.common,
	&depll3x_ga_clk.common,
	&depll4x_ga_clk.common,
	&res_dcap_24m_clk.common,
	&apb2jtag_clk.common,
};

static const u32 pll_regs[] = {
	SUN60IW2_PLL_REF_CTRL_REG,
	SUN60IW2_PLL_DDR_CTRL_REG,
	SUN60IW2_PLL_PERI0_CTRL_REG,
	SUN60IW2_PLL_PERI1_CTRL_REG,
	SUN60IW2_PLL_GPU0_CTRL_REG,
	SUN60IW2_PLL_VIDEO0_CTRL_REG,
	SUN60IW2_PLL_VIDEO1_CTRL_REG,
	SUN60IW2_PLL_VIDEO2_CTRL_REG,
	SUN60IW2_PLL_VE0_CTRL_REG,
	SUN60IW2_PLL_VE1_CTRL_REG,
	SUN60IW2_PLL_AUDIO0_CTRL_REG,
	SUN60IW2_PLL_NPU_CTRL_REG,
	SUN60IW2_PLL_DE_CTRL_REG,
};
static const struct sunxi_ccu_desc sun60iw2_ccu_desc = {
	.ccu_clks	= sun60iw2_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun60iw2_ccu_clks),

	.hw_clks	= &sun60iw2_hw_clks,

	.resets		= sun60iw2_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun60iw2_ccu_resets),
};

static void __init of_sun60iw2_ccu_init(struct device_node *node)
{
	void __iomem *reg;
	int i;
	u32 val;

	reg = of_iomap(node, 0);
	if (IS_ERR(reg))
		return;

	for (i = 0; i < ARRAY_SIZE(pll_regs); i++) {
		val = readl(reg + pll_regs[i]);
		val |= BIT(29);
		val |= BIT(31);
		writel(val, reg + pll_regs[i]);
	}
	sunxi_ccu_probe(node, reg, &sun60iw2_ccu_desc);
}

CLK_OF_DECLARE(sun60iw2_ccu_init, "allwinner,sun60iw2-ccu", of_sun60iw2_ccu_init);

MODULE_DESCRIPTION("Allwinner sun60iw2 clk driver");
MODULE_AUTHOR("rengaomin<rengaomin@allwinnertech.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.7.2");
