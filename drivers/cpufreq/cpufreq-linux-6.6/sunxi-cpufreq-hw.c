/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
#include <sunxi-log.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/arch_topology.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/ktime.h>
#include <linux/of_address.h>
#include <linux/i2c.h>
#include <linux/twi/sunxi-twi.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include "../../../drivers/opp/opp.h"

#include "cpufreq-dt.h"
#define MAX_PMU 8

struct sunxi_hw_opp_table {
	unsigned long freq_hz;
	unsigned long volt_uv;
};

struct sunxi_clk_factor_freq {
	u32 factor;
	u32 freq;
};

struct pll_nkmp {
	int n;
	int k;
	int m;
	int p;
	int factor;
};

struct sunxi_hw_freq_dev {
	struct device			*dev;
	struct clk			*clk;
	struct sunxi_hw_opp_table	*opp;
	int				opp_count;
	int				hw_tbl_idx;
	unsigned long			prev_freq;
	unsigned long			cur_freq;
	unsigned long			next_freq;
	unsigned long			little_core_cur_freq;
	unsigned long			big_core_cur_freq;
	bool				initialized;
};

struct sunxi_cpufreq_soc_data {
	u32 reg_dvfs_ctrl;
	u32 reg_dvfs_err;
	u32 reg_irq_state;
	u32 reg_dvfs_req;
	u32 reg_dvfs_state;
	u32 reg_dvfs_debug;
	u32 reg_dvfs_timeout;
	u32 reg_vf_level;
	u32 reg_cci_dvfs_fsh_irq_en;
	u32 reg_cci_sw_volt_req;
	u32 vf_table_index;
	u32 vf_table_offset;
	u32 reg_common_dvfs_base;
	u32 vf_level_offset;
	struct sunxi_clk_factor_freq *freq_tbl;
	int freq_tbl_len;
};

struct private_data {
	struct list_head node;

	cpumask_var_t cpus;
	struct device *cpu_dev;
	struct cpufreq_frequency_table *freq_table;
	bool have_static_opps;
	void __iomem *base;
	struct resource *res;
	struct sunxi_hw_freq_dev *hw_freq_dev;
	const struct sunxi_cpufreq_soc_data *soc_data;
	struct i2c_adapter *twi_adapter;
	int cluster_id;
	int opp_token;
	int hw_cpufreq_idx;
};

struct pmu_data {
	u32 pmu_addr[MAX_PMU];
	u32 pmu_reg_addr[MAX_PMU];
	u32 twi_priority[MAX_PMU];
	u32 twi_mask[MAX_PMU];
	int channel[MAX_PMU];
	u32 twi_channel_num;
};

static struct pmu_data twi_pmu_data;
static LIST_HEAD(priv_list);
static void __iomem *cluster_base;
struct cluster_freq_info {
	struct regulator *supply;
	unsigned long cluster_freq_cur;
	int cluster_vol_cur;
	int freq_idx;
};
struct cluster_freq_info cluster_freq[4];
extern bool is_cpufreq_nvmem_finished(void);

#define BITS	(8)
#define DVFS_REQ_EN	(8U)
#define PLLCPU(n, p, freq)	{F_N14X8_P8x2(n, p), freq}
#define INIT_VOLT(value)		(value << 24)
#define INIT_FREQ(value)		(value << 18)
#define INIT_SLOW_BOOST(value)	(value << 17)
#define VOL_DELAY(value)		(value << 8)
#define DVFS_ENABLE				(3)
#define DVFS_SLOW_BOOST_ENABLE	(1 << 17)
#define DVFS_SLOW_BOOST_METHOD	(0 << 16)

#define F_N14X8_P8x2(nv, pv) \
	FACTOR_ALL(nv, 14, 8, 0, 0, 0, 0, 0, 0, pv, 8, 2, 0, 0, 0, 0, 0, 0)

#define FACTOR_ALL(nv, ns, nw, kv, ks, kw, mv, ms, mw, \
		   pv, ps, pw, d1v, d1s, d1w, d2v, d2s, d2w) \
		  ((((nv & ((1 << nw) - 1)) << ns) | \
		    ((kv & ((1 << kw) - 1)) << ks) | \
		    ((mv & ((1 << mw) - 1)) << ms) | \
		    ((pv & ((1 << pw) - 1)) << ps) | \
		    ((d1v & ((1 << d1w) - 1)) << d1s) | \
		    ((d2v & ((1 << d2w) - 1)) << d2s)))

#define MAX_FREQ_NUM 100
#define DVFS_TIMEOUT_STATUS (7U << 16)
#define VF_LEVEL_TIMEOUT (0x3F << 8)
#define DVFS_TIMEOUT (1U << 1)
#define DVFS_DEBUG_ENABLE (1U)
#define DVFS_TIMEOUT_NUM (0x3E80)
#define DVFS_DEBUG_TIMEOUT (DVFS_TIMEOUT_STATUS | VF_LEVEL_TIMEOUT | DVFS_TIMEOUT)
struct sunxi_clk_factor_freq factor_pllcpu_tbl[MAX_FREQ_NUM];
static DEFINE_MUTEX(dvfs_mutex);

static const struct sunxi_cpufreq_soc_data sun65iw1_soc_data = {
	.reg_dvfs_ctrl = 0x0000,
	.reg_dvfs_err = 0x0004,
	.reg_irq_state = 0x0010,
	.reg_dvfs_req = 0x0014,
	.reg_dvfs_state = 0x0018,
	.reg_dvfs_debug = 0x0008,
	.reg_dvfs_timeout = 0x000c,
	.reg_vf_level = 0x0020,
	.reg_cci_dvfs_fsh_irq_en = 0x0060,
	.reg_cci_sw_volt_req = 0x0064,
	.vf_table_index = 15,
	.vf_table_offset = 32,
	.reg_common_dvfs_base = 0x07010400,
	.vf_level_offset = 26,
	.freq_tbl = factor_pllcpu_tbl,
	.freq_tbl_len = sizeof(factor_pllcpu_tbl) / sizeof(struct sunxi_clk_factor_freq),
};

static struct freq_attr *cpufreq_dt_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,   /* Extra space for boost-attr if required */
	NULL,
};

static int sunxi_dvfs_num_domains;
struct i2c_adapter *adapter;
static void sunxi_dvfs_config(struct platform_device *pdev, struct private_data *priv);

static void sunxi_around_rate_init(void)
{
	unsigned int i, p, n;
	for (i = 0; i < 100; i++) {
		if ((i * 24000000U) <= 480000000U) {
			p = 1;
			n = i * 2;
		} else {
			p = 0;
			n = i;
		}
		factor_pllcpu_tbl[i].freq = i * 24000000U;
		factor_pllcpu_tbl[i].factor = F_N14X8_P8x2(n, p);
	}
}

static int sunxi_clk_freq_search(struct sunxi_clk_factor_freq tbl[],
				unsigned long freq, int low, int high)
{
	int mid;
	unsigned long checkfreq;

	if (low > high)
		return (high == -1) ? 0 : high;

	mid = (low + high) / 2;
	checkfreq = tbl[mid].freq;

	if (checkfreq == freq)
		return mid;
	else if (checkfreq > freq)
		return sunxi_clk_freq_search(tbl, freq, low, mid - 1);
	else
		return sunxi_clk_freq_search(tbl, freq, mid + 1, high);
}

static int sunxi_hw_opp_tbl_search(struct sunxi_hw_opp_table opp[],
				unsigned long freq, int high)
{
	int i;

	for (i = 0; i < high; i++) {
		if (opp[i].freq_hz == freq)
			return i;
	}
	return -1;
}

static struct private_data *cpufreq_dt_find_data(int cpu)
{
	struct private_data *priv;

	list_for_each_entry(priv, &priv_list, node) {
		if (cpumask_test_cpu(cpu, priv->cpus))
			return priv;
	}

	return NULL;
}

static int hw_freq_find_nkmp(unsigned long freq, struct pll_nkmp *nkmp, struct sunxi_clk_factor_freq tbl[], int size)
{
	int idx = sunxi_clk_freq_search(tbl, freq, 0, size - 1);

	/* init nkmp */
	nkmp->n = 0;
	nkmp->k = 0;
	nkmp->m = 0;
	nkmp->p = 0;
	nkmp->factor = 0;
	nkmp->factor = tbl[idx].factor;
	/* find the table to get the PLL nkmp value corresponding to each frequency. */
	//sunxi_err(NULL, "hw_freq_find_nkmp tbl_freq %d factor %x\n", tbl[idx].freq, nkmp->factor);
	//sunxi_err(NULL, "hw_freq_find_nkmp freq %ld index %d size %d\n", freq, idx, size);
	return idx;
}

static void sunxi_dvfs_twi_config(struct private_data *priv)
{
	i2c_lock_bus(priv->twi_adapter, I2C_LOCK_SEGMENT);
	if (sunxi_twi_dvfs_disable_all(priv->twi_adapter) < 0) {
		sunxi_err(NULL, "dvfs twi disable all error\n");
	}

	if (sunxi_twi_dvfs_set_slave_addr(priv->twi_adapter, twi_pmu_data.pmu_addr[priv->cluster_id]) < 0) {
		sunxi_err(NULL, "dvfs set slave addr error\n");
	}

	if (sunxi_twi_dvfs_set_interval(priv->twi_adapter, 0) < 0) {
		sunxi_err(NULL, "dvfs set interval error\n");
	}

	if (sunxi_twi_dvfs_chan_init(priv->twi_adapter,
					twi_pmu_data.channel[priv->cluster_id],     //target channel
					twi_pmu_data.twi_priority[priv->cluster_id],//channel priority
					twi_pmu_data.pmu_reg_addr[priv->cluster_id],//pmu regs addr
					twi_pmu_data.twi_mask[priv->cluster_id],    //mask
					true) < 0) {
		sunxi_err(NULL, "dvfs chan init error\n");
	}

	if (sunxi_twi_dvfs_enable(priv->twi_adapter, twi_pmu_data.channel[priv->cluster_id]) < 0) {
		sunxi_err(NULL, "dvfs twi enable error\n");
	}
	i2c_unlock_bus(priv->twi_adapter, I2C_LOCK_SEGMENT);
}

static void sunxi_dvfs_req_set(struct private_data *priv, int idx)
{
	unsigned int value;
	unsigned int cci_idx;
	unsigned int cluster_idx = idx;
	writel_relaxed(cluster_idx, priv->base + priv->soc_data->reg_dvfs_req);
	cluster_idx |= (0x1 << DVFS_REQ_EN);
	writel_relaxed(cluster_idx, priv->base + priv->soc_data->reg_dvfs_req);
	if (priv->cluster_id == 1) {
		cci_idx = 1;
		writel_relaxed(cci_idx, cluster_base + priv->soc_data->reg_dvfs_req + 0x300);
		cci_idx |= (0x1 << DVFS_REQ_EN);
		writel_relaxed(cci_idx, cluster_base + priv->soc_data->reg_dvfs_req + 0x300);
	}
	readl_poll_timeout(priv->base + priv->soc_data->reg_irq_state, value, value & 0x1, 300, 20000);
}

static void sunxi_dvfs_twi_set(struct private_data *priv, int idx)
{
	if (sunxi_twi_dvfs_set_slave_addr(priv->twi_adapter, twi_pmu_data.pmu_addr[priv->cluster_id]) < 0) {
		sunxi_err(NULL, "dvfs set slave addr error\n");
		return;
	}

	if (sunxi_twi_dvfs_enable(priv->twi_adapter, twi_pmu_data.channel[priv->cluster_id]) < 0) {
		sunxi_err(NULL, "dvfs twi enable error\n");
		return;
	}
}

static int sunxi_twi_set_dvfs(struct private_data *priv, int idx)
{
	unsigned int value;
	i2c_lock_bus(priv->twi_adapter, I2C_LOCK_SEGMENT);
	value = readl(priv->base + priv->soc_data->reg_irq_state);
	if (value & 0x1) {
		value |= 0x1;
		writel_relaxed(value, priv->base + priv->soc_data->reg_irq_state);
	}

	if (sunxi_twi_dvfs_disable_all(priv->twi_adapter) < 0) {
		sunxi_err(NULL, "dvfs twi disable all error\n");
		i2c_unlock_bus(priv->twi_adapter, I2C_LOCK_SEGMENT);
		return -1;
	}
	sunxi_dvfs_twi_set(priv, priv->cluster_id);
	sunxi_dvfs_req_set(priv, idx);
	i2c_unlock_bus(priv->twi_adapter, I2C_LOCK_SEGMENT);
	return 0;
}

static void sunxi_dvfs_timeout(struct cpufreq_policy *policy)
{
	unsigned int value;
	struct private_data *priv = policy->driver_data;
	value = readl(priv->base + priv->soc_data->reg_dvfs_debug);
	if (value & DVFS_DEBUG_TIMEOUT) {
		sunxi_err(NULL, "HW DVFS timeout, reg 008 status 0x%x , cpu %d\n", value, policy->cpu);
	}
}

static int set_target(struct cpufreq_policy *policy, unsigned int index)
{
	mutex_lock(&dvfs_mutex);
	struct private_data *priv = policy->driver_data;
	struct sunxi_hw_opp_table *opp = priv->hw_freq_dev->opp;
	unsigned long freq = policy->freq_table[index].frequency * 1000;
	unsigned int len = priv->soc_data->vf_table_index;
	int idx;

	sunxi_dvfs_timeout(policy);
	idx = sunxi_hw_opp_tbl_search(opp, freq, len) + 1;
	if (!sunxi_twi_set_dvfs(priv, idx)) {
		priv->hw_cpufreq_idx = idx;
	}
	mutex_unlock(&dvfs_mutex);
	return 0;
}

static int cpufreq_init(struct cpufreq_policy *policy)
{
	struct private_data *priv;
	struct device *cpu_dev;
	struct clk *cpu_clk;
	unsigned int transition_latency;
	int ret;

	priv = cpufreq_dt_find_data(policy->cpu);
	if (!priv) {
		sunxi_err(NULL, "failed to find data for cpu%d\n", policy->cpu);
		return -ENODEV;
	}
	cpu_dev = priv->cpu_dev;

	cpu_clk = clk_get(cpu_dev, NULL);
	if (IS_ERR(cpu_clk)) {
		ret = PTR_ERR(cpu_clk);
		sunxi_err(cpu_dev, "%s: failed to get clk: %d\n", __func__, ret);
		return ret;
	}

	transition_latency = dev_pm_opp_get_max_transition_latency(cpu_dev);
	if (!transition_latency)
		transition_latency = CPUFREQ_ETERNAL;

	cpumask_copy(policy->cpus, priv->cpus);
	policy->driver_data = priv;
	policy->clk = cpu_clk;
	policy->freq_table = priv->freq_table;
	policy->suspend_freq = dev_pm_opp_get_suspend_opp_freq(cpu_dev) / 1000;
	policy->cpuinfo.transition_latency = transition_latency;
	policy->dvfs_possible_from_any_cpu = true;

	/* Support turbo/boost mode */
	if (policy_has_boost_freq(policy)) {
		/* This gets disabled by core on driver unregister */
		ret = cpufreq_enable_boost_support();
		if (ret)
			goto out_clk_put;
		cpufreq_dt_attr[1] = &cpufreq_freq_attr_scaling_boost_freqs;
	}

	return 0;

out_clk_put:
	clk_put(cpu_clk);

	return ret;
}

static int sunxi_create_hw_vf_table(int cpu)
{
	struct dev_pm_opp *opp;
	struct device *cpu_dev;
	struct private_data *priv = cpufreq_dt_find_data(cpu);
	struct sunxi_hw_freq_dev *hw_freq_dev;
	const struct sunxi_cpufreq_soc_data *soc;
	unsigned long freq = 0, volt;
	struct pll_nkmp nkmp;
	int i, ret, tmp_reg;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		sunxi_err(NULL, "failed to find cpu dev%d\n", cpu);
		return -EPROBE_DEFER;
	}

	if (!priv) {
		sunxi_err(NULL, "failed to find data for cpu%d\n", cpu);
		return -ENODEV;
	}

	priv->hw_freq_dev = devm_kzalloc(cpu_dev, sizeof(struct sunxi_hw_freq_dev), GFP_KERNEL);
	if (!priv->hw_freq_dev) {
		sunxi_err(NULL, "failed to devm_kzalloc\n");
		return -ENOMEM;
	}

	hw_freq_dev = priv->hw_freq_dev;
	soc = priv->soc_data;
	hw_freq_dev->opp_count = dev_pm_opp_get_opp_count(cpu_dev);
	hw_freq_dev->opp = devm_kmalloc_array(cpu_dev, soc->vf_table_index,
					      sizeof(struct sunxi_hw_opp_table), GFP_KERNEL);
	hw_freq_dev->hw_tbl_idx = 0;
	for (i = 0, freq = 0; i < hw_freq_dev->opp_count; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(cpu_dev, &freq);
		if (IS_ERR(opp)) {
			dev_pm_opp_put(opp);
			sunxi_err(NULL, "failed to find opp\n");
			return -1;
		}
		volt = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);

		/* setting hw vf table */
		if (freq != 0 && volt != 0) {
			hw_freq_dev->opp[hw_freq_dev->hw_tbl_idx].freq_hz = freq;
			hw_freq_dev->opp[hw_freq_dev->hw_tbl_idx].volt_uv = volt;
			ret = hw_freq_find_nkmp(freq, &nkmp,
						soc->freq_tbl,
						soc->freq_tbl_len);
			/* freq / volt */
			tmp_reg = ((hw_freq_dev->hw_tbl_idx + 1) << soc->vf_level_offset) | nkmp.factor | (((volt / 10000) - 50) & 0xff);
			writel_relaxed(tmp_reg, priv->base +
				       soc->reg_vf_level +
				       hw_freq_dev->hw_tbl_idx * soc->vf_table_offset / BITS);

			if (hw_freq_dev->hw_tbl_idx <= priv->soc_data->vf_table_index) {
				hw_freq_dev->hw_tbl_idx++;
			} else {
				pr_err("current opp table index %d is higher than hw vf table index %d",
					hw_freq_dev->hw_tbl_idx, priv->soc_data->vf_table_index);
				return -1;
			}
			//pr_err("hw vf table idx %d freq %ld volt %ld", i, freq, volt);
		}
	}

	return 0;
}

static int cpufreq_hw_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int cpufreq_hw_resume(struct cpufreq_policy *policy)
{
	mutex_lock(&dvfs_mutex);
	struct private_data *priv = policy->driver_data;
	struct device *cpu_dev = get_cpu_device(policy->cpu);
	struct platform_device *pdev = to_platform_device(cpu_dev);

	sunxi_dvfs_config(pdev, priv);
	sunxi_dvfs_twi_config(priv);
	sunxi_twi_set_dvfs(priv, priv->hw_cpufreq_idx);
	mutex_unlock(&dvfs_mutex);
	return 0;
}

static int cpufreq_online(struct cpufreq_policy *policy)
{
	/* We did light-weight tear down earlier, nothing to do here */
	return 0;
}

static int cpufreq_offline(struct cpufreq_policy *policy)
{
	/*
	 * Preserve policy->driver_data and don't free resources on light-weight
	 * tear down.
	 */
	return 0;
}

static int cpufreq_exit(struct cpufreq_policy *policy)
{
	clk_put(policy->clk);
	return 0;
}

static int __maybe_unused sunxi_get_power(struct device *dev, unsigned long *uW,
				     unsigned long *kHz)
{
	struct dev_pm_opp *opp, *iter;
	struct device_node *np;
	struct opp_table *opp_table;
	unsigned long mV, Hz;
	u32 cap;
	u64 tmp;
	int ret;
	int index = 0;

	np = of_node_get(dev->of_node);
	if (!np)
		return -EINVAL;

	ret = of_property_read_u32(np, "dynamic-power-coefficient", &cap);
	of_node_put(np);
	if (ret)
		return -EINVAL;

	Hz = *kHz * 1000;
	opp = dev_pm_opp_find_freq_ceil(dev, &Hz);
	if (IS_ERR(opp))
		return -EINVAL;

	mV = dev_pm_opp_get_voltage(opp) / 1000;

	opp_table = opp->opp_table;
	list_for_each_entry(iter, &opp_table->opp_list, node) {
		index++;
		if (iter->rates[0] >= Hz)
			break;
	}
	dev_pm_opp_put(opp);
	if (!mV)
		return -EINVAL;

	tmp = (u64)cap * mV * mV * (Hz / 1000000);
	/* Provide power in micro-Watts */
	do_div(tmp, 1000000);

	// avoid inefficient opp when several opp is in same voltage
	*uW = (unsigned long)tmp + ((index * Hz / 1000000) / 4);
	*kHz = Hz / 1000;

	return 0;
}

static void sunxi_cpufreq_register_em(struct cpufreq_policy *policy)
{
	struct device *dev = get_cpu_device(policy->cpu);
	struct cpumask *cpus = policy->related_cpus;
	struct em_data_callback em_cb;
	struct device_node *np;
	int ret, nr_opp;
	u32 cap;

	if (IS_ERR_OR_NULL(dev)) {
		ret = -EINVAL;
		goto failed;
	}

	nr_opp = dev_pm_opp_get_opp_count(dev);
	if (nr_opp <= 0) {
		ret = -EINVAL;
		goto failed;
	}

	np = of_node_get(dev->of_node);
	if (!np) {
		ret = -EINVAL;
		goto failed;
	}

	/*
	 * Register an EM only if the 'dynamic-power-coefficient' property is
	 * set in devicetree. It is assumed the voltage values are known if that
	 * property is set since it is useless otherwise. If voltages are not
	 * known, just let the EM registration fail with an error to alert the
	 * user about the inconsistent configuration.
	 */
	ret = of_property_read_u32(np, "dynamic-power-coefficient", &cap);
	of_node_put(np);
	if (ret || !cap) {
		dev_dbg(dev, "Couldn't find proper 'dynamic-power-coefficient' in DT\n");
		ret = -EINVAL;
		goto failed;
	}

	EM_SET_ACTIVE_POWER_CB(em_cb, sunxi_get_power);

	ret = em_dev_register_perf_domain(dev, nr_opp, &em_cb, cpus, true);
	if (ret)
		goto failed;

failed:
	dev_dbg(dev, "Couldn't register Energy Model %d\n", ret);
}

static struct cpufreq_driver dt_cpufreq_driver = {
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK |
		 CPUFREQ_IS_COOLING_DEV,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = set_target,
	.get = cpufreq_generic_get,
	.init = cpufreq_init,
	.exit = cpufreq_exit,
	.online = cpufreq_online,
	.offline = cpufreq_offline,
	.register_em = sunxi_cpufreq_register_em,
	.name = "sunxi-cpufreq-hw",
	.attr = cpufreq_dt_attr,
	.suspend = cpufreq_hw_suspend,
	.resume = cpufreq_hw_resume,
};

static void sunxi_boot_freq_info(struct platform_device *pdev, struct private_data *priv)
{
	int ret;
	struct clk *cpu_clk;
	int cluster_id = priv->cluster_id;

	if (cluster_id == 0) {
		if (!cluster_freq[cluster_id].supply)
			cluster_freq[cluster_id].supply = regulator_get(&pdev->dev, "cluster0");
	} else if (cluster_id == 1) {
		if (!cluster_freq[cluster_id].supply)
			cluster_freq[cluster_id].supply = regulator_get(&pdev->dev, "cluster1");
	} else if (cluster_id == 2) {
		if (!cluster_freq[cluster_id].supply)
			cluster_freq[cluster_id].supply = regulator_get(&pdev->dev, "cluster2");
	}

	if (IS_ERR(cluster_freq[cluster_id].supply)) {
		sunxi_err(&pdev->dev, "cluster: get supply err\n");
	}

	cluster_freq[cluster_id].cluster_vol_cur = regulator_get_voltage(cluster_freq[cluster_id].supply);
	//sunxi_err(&pdev->dev, "cluster vol is %d\n", cluster_freq[cluster_id].cluster_vol_cur);

	cpu_clk = clk_get(priv->cpu_dev, NULL);
	if (IS_ERR(cpu_clk)) {
		ret = PTR_ERR(cpu_clk);
		sunxi_err(priv->cpu_dev, "%s: failed to get clk: %d\n", __func__, ret);
		return;
	}
	cluster_freq[cluster_id].cluster_freq_cur = clk_get_rate(cpu_clk);
	clk_put(cpu_clk);
	//sunxi_err(&pdev->dev, "cluster freq is %lu\n", cluster_freq[cluster_id].cluster_freq_cur);

	cluster_freq[cluster_id].freq_idx = sunxi_hw_opp_tbl_search(priv->hw_freq_dev->opp,
							cluster_freq[cluster_id].cluster_freq_cur,
							priv->soc_data->vf_table_index) + 1;
	//sunxi_err(&pdev->dev, "cluster freq id %d\n", cluster_freq[cluster_id].freq_idx);
	writel_relaxed(DVFS_DEBUG_ENABLE, priv->base + priv->soc_data->reg_dvfs_debug);
	//timeout config: 31.25us * DVFS_TIMEOUT_NUM
	writel_relaxed(DVFS_TIMEOUT_NUM, priv->base + priv->soc_data->reg_dvfs_timeout);
}

static void sunxi_dvfs_config(struct platform_device *pdev, struct private_data *priv)
{
	unsigned int value, val_f, val_m;
	sunxi_boot_freq_info(pdev, priv);
	val_m = (cluster_freq[priv->cluster_id].cluster_vol_cur / 10000) - 50;
	val_f = cluster_freq[priv->cluster_id].freq_idx;
	value = readl_relaxed(priv->base);
	value |= INIT_VOLT(val_m);
	value |= INIT_FREQ(val_f);
	value |= VOL_DELAY(4);
	value |= DVFS_ENABLE;
	if (priv->cluster_id == 2) {
		value |= DVFS_SLOW_BOOST_ENABLE;
		value |= DVFS_SLOW_BOOST_METHOD;
		value &= ~(VOL_DELAY(255));
		value |= VOL_DELAY(1);
	}
	writel_relaxed(value, priv->base);
	//sunxi_err(&pdev->dev, "current cluster %d DFVS 0x%0x\n", priv->cluster_id, readl_relaxed(priv->base));
}

/*
 * An earlier version of opp-v1 bindings used to name the regulator
 * "cpu0-supply", we still need to handle that for backwards compatibility.
 */
static const char *find_supply_name(struct device *dev)
{
	struct device_node *np;
	struct property *pp;
	int cpu = dev->id;
	const char *name = NULL;

	np = of_node_get(dev->of_node);

	/* This must be valid for sure */
	if (WARN_ON(!np))
		return NULL;

	/* Try "cpu0" for older DTs */
	if (!cpu) {
		pp = of_find_property(np, "cpu0-supply", NULL);
		if (pp) {
			name = "cpu0";
			goto node_put;
		}
	}

	pp = of_find_property(np, "cpu-supply", NULL);
	if (pp) {
		name = "cpu";
		goto node_put;
	}

	sunxi_debug(dev, "no regulator for cpu%d\n", cpu);
node_put:
	of_node_put(np);
	return name;
}

static int dt_cpufreq_early_init(struct device *dev, int cpu)
{
	struct private_data *priv = NULL;
	struct platform_device *pdev = to_platform_device(dev);
	struct device *cpu_dev = NULL;
	struct resource *res = NULL;
	void __iomem *base;
	bool fallback = false;
	const char *reg_name[] = { NULL, NULL };
	int ret;

	/* Check if this CPU is already covered by some other policy */
	if (cpufreq_dt_find_data(cpu))
		return 0;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return -EPROBE_DEFER;

	priv = devm_kzalloc(dev, sizeof(struct private_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!alloc_cpumask_var(&priv->cpus, GFP_KERNEL))
		return -ENOMEM;

	cpumask_set_cpu(cpu, priv->cpus);
	priv->cpu_dev = cpu_dev;
	priv->twi_adapter = adapter;
	priv->cluster_id = topology_cluster_id(cpu);
	sunxi_err(&pdev->dev, "get cluster_id %d\n", priv->cluster_id);

	/*
	 * OPP layer will be taking care of regulators now, but it needs to know
	 * the name of the regulator first.
	 */
	reg_name[0] = find_supply_name(cpu_dev);
	if (reg_name[0]) {
		priv->opp_token = dev_pm_opp_set_regulators(cpu_dev, reg_name);
		if (priv->opp_token < 0) {
			ret = dev_err_probe(cpu_dev, priv->opp_token,
					    "failed to set regulators\n");
			goto free_cpumask;
		}
	}

	/* Get OPP-sharing information from "operating-points-v2" bindings */
	ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, priv->cpus);
	if (ret) {
		if (ret != -ENOENT)
			goto out;

		/*
		 * operating-points-v2 not supported, fallback to all CPUs share
		 * OPP for backward compatibility if the platform hasn't set
		 * sharing CPUs.
		 */
		if (dev_pm_opp_get_sharing_cpus(cpu_dev, priv->cpus))
			fallback = true;
	}

	/*
	 * Initialize OPP tables for all priv->cpus. They will be shared by
	 * all CPUs which have marked their CPUs shared with OPP bindings.
	 *
	 * For platforms not using operating-points-v2 bindings, we do this
	 * before updating priv->cpus. Otherwise, we will end up creating
	 * duplicate OPPs for the CPUs.
	 *
	 * OPPs might be populated at runtime, don't fail for error here unless
	 * it is -EPROBE_DEFER.
	 */
	ret = dev_pm_opp_of_cpumask_add_table(priv->cpus);
	if (!ret) {
		priv->have_static_opps = true;
	} else if (ret == -EPROBE_DEFER) {
		goto out;
	}

	/*
	 * The OPP table must be initialized, statically or dynamically, by this
	 * point.
	 */
	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		sunxi_err(cpu_dev, "OPP table can't be empty\n");
		ret = -ENODEV;
		goto out;
	}

	if (fallback) {
		cpumask_setall(priv->cpus);
		ret = dev_pm_opp_set_sharing_cpus(cpu_dev, priv->cpus);
		if (ret)
			sunxi_err(cpu_dev, "%s: failed to mark OPPs as shared: %d\n",
				__func__, ret);
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &priv->freq_table);
	if (ret) {
		sunxi_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out;
	}

	/* get cpufreq hw soc data */
	priv->soc_data = of_device_get_match_data(dev);
	if (!priv->soc_data) {
		sunxi_err(dev, "failed to get soc_data %d\n", cpu);
		return -ENOENT;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, sunxi_dvfs_num_domains);
	if (!res) {
		sunxi_err(dev, "failed to get MEM res\n");
		return -ENXIO;
	}

	sunxi_dvfs_num_domains++;
	if (!request_mem_region(res->start, resource_size(res), res->name)) {
		dev_err(dev, "failed to request resource %pR\n", res);
		return -EBUSY;
	}
	base = ioremap(res->start, resource_size(res));
	if (IS_ERR(base)) {
		sunxi_err(dev, "%s:Failed to ioremap() io memory region.\n", __func__);
		ret = -ENOMEM;
		goto release_region;
	}
	priv->base = base;
	priv->res = res;

	list_add(&priv->node, &priv_list);

	/* check opp table hw prop, and write in dvfs registers */
	ret = sunxi_create_hw_vf_table(cpu);
	if (ret) {
		sunxi_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out;
	}
	cluster_freq[priv->cluster_id].supply = NULL;
	sunxi_dvfs_config(pdev, priv);
	sunxi_dvfs_twi_config(priv);

	return 0;

out:
	if (priv->have_static_opps)
		dev_pm_opp_of_cpumask_remove_table(priv->cpus);
	dev_pm_opp_put_regulators(priv->opp_token);
free_cpumask:
	free_cpumask_var(priv->cpus);
release_region:
	release_mem_region(res->start, resource_size(res));
	return ret;
}

static void dt_cpufreq_release(void)
{
	struct private_data *priv, *tmp;

	list_for_each_entry_safe(priv, tmp, &priv_list, node) {
		if (cluster_freq[priv->cluster_id].supply)
			regulator_put(cluster_freq[priv->cluster_id].supply);
		dev_pm_opp_free_cpufreq_table(priv->cpu_dev, &priv->freq_table);
		iounmap(priv->base);
		release_mem_region(priv->res->start, resource_size(priv->res));
		if (priv->have_static_opps)
			dev_pm_opp_of_cpumask_remove_table(priv->cpus);
		dev_pm_opp_put_regulators(priv->opp_token);
		free_cpumask_var(priv->cpus);
		list_del(&priv->node);
	}
}

static int sunxi_dvfs_dts_config(struct platform_device *pdev)
{
	int ret = 1;
	struct device_node *np = NULL;

	np = of_parse_phandle(pdev->dev.of_node, "dvfs_twi", 0);
	if (!np) {
		sunxi_err(&pdev->dev, "dvfs_twi not found\n");
		return -EPROBE_DEFER;
	}

	adapter = of_get_i2c_adapter_by_node(np);
	if (!adapter) {
		sunxi_err(&pdev->dev, "get_i2c_adapter error\n");
		return -EPROBE_DEFER;
	}

	np = pdev->dev.of_node;
	if (!np) {
		sunxi_err(&pdev->dev, "hw_dvfs_config_init failed\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32(np, "twi_channel_num", &twi_pmu_data.twi_channel_num);
	if (ret) {
		sunxi_err(&pdev->dev, "twi_channel_num get failed\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32_array(np, "pmu_addr", twi_pmu_data.pmu_addr, twi_pmu_data.twi_channel_num);
	if (ret) {
		sunxi_err(&pdev->dev, "pmu_addr get failed\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32_array(np, "pmu_reg_addr", twi_pmu_data.pmu_reg_addr, twi_pmu_data.twi_channel_num);
	if (ret) {
		sunxi_err(&pdev->dev, "pmu_reg_addr get failed\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32_array(np, "twi_priority", twi_pmu_data.twi_priority, twi_pmu_data.twi_channel_num);
	if (ret) {
		sunxi_err(&pdev->dev, "twi_priority get failed\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32_array(np, "twi_mask", twi_pmu_data.twi_mask, twi_pmu_data.twi_channel_num);
	if (ret) {
		sunxi_err(&pdev->dev, "twi_mask get failed\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32_array(np, "channel", twi_pmu_data.channel, twi_pmu_data.twi_channel_num);
	if (ret) {
		sunxi_err(&pdev->dev, "channel get failed\n");
		return -EPROBE_DEFER;
	}

	return 1;
}

void sunxi_dvfs_other_config(void)
{
	cluster_base = ioremap(0x07010000, 0x410);
	//cci config
	writel_relaxed(0x04030028, cluster_base+0x300+0x20);//0.9V, src4, div 1
	writel_relaxed(0x100C2128, cluster_base+0x300+0x2c);//0.9V, src4, div 1
	writel_relaxed(0x142a8241, cluster_base+0x300+0x30);//1.15V, src2, div 2

	//COMMON HOLD COUNTER
	writel_relaxed(0x5, cluster_base+0x400);

	//CLUSTER0_DVFS_FSH_IRQ_EN
	writel_relaxed(0x1, cluster_base+0x60);//CLUSTER0_DVFS_FSH_IRQ_EN
	writel_relaxed(0x1, cluster_base+0x200+0x60);/*CLUSTER1_DVFS_FSH_IRQ_EN*/
	writel_relaxed(0x1, cluster_base+0x100+0x60);/*CLUSTER2_DVFS_FSH_IRQ_EN*/
}

static int sunxi_cpufreq_hw_probe(struct platform_device *pdev)
{
	struct cpufreq_dt_platform_data *data = dev_get_platdata(&pdev->dev);
	int ret, cpu;

	/* if cpufreq-nvmem not probe, probe later */
	ret = is_cpufreq_nvmem_finished();
	if (!ret) {
		sunxi_err(&pdev->dev, "cpufreq cpu get failed\n");
		return -EPROBE_DEFER;
	}

	sunxi_around_rate_init();
	ret = sunxi_dvfs_dts_config(pdev);
	if (!ret) {
		sunxi_err(&pdev->dev, "sunxi_dvfs_dts_config failed\n");
		return -EPROBE_DEFER;
	}

	/* Request resources early so we can return in case of -EPROBE_DEFER */
	for_each_possible_cpu(cpu) {
		ret = dt_cpufreq_early_init(&pdev->dev, cpu);
		if (ret)
			goto err;
	}
	sunxi_dvfs_other_config();

	if (data) {
		if (data->have_governor_per_policy)
			dt_cpufreq_driver.flags |= CPUFREQ_HAVE_GOVERNOR_PER_POLICY;

		dt_cpufreq_driver.resume = data->resume;
		if (data->suspend)
			dt_cpufreq_driver.suspend = data->suspend;
		if (data->get_intermediate) {
			dt_cpufreq_driver.target_intermediate = data->target_intermediate;
			dt_cpufreq_driver.get_intermediate = data->get_intermediate;
		}
	}

	ret = cpufreq_register_driver(&dt_cpufreq_driver);
	if (ret) {
		sunxi_err(&pdev->dev, "failed register driver: %d\n", ret);
		goto err;
	}

	sunxi_err(&pdev->dev, "cpufreq-hw register success!\n");

	return 0;
err:
	dt_cpufreq_release();
	return ret;
}

static void sunxi_cpufreq_hw_remove(struct platform_device *pdev)
{

	cpufreq_unregister_driver(&dt_cpufreq_driver);
	dt_cpufreq_release();
}

static const struct of_device_id sunxi_cpufreq_hw_match[] = {
	{ .compatible = "allwinner,sun65iw1-hw-dvfs", .data = &sun65iw1_soc_data },
	{ },
};

static struct platform_driver dt_cpufreq_platdrv = {
	.driver = {
		.name	= "sunxi-cpufreq-hw",
		.of_match_table = sunxi_cpufreq_hw_match,
	},
	.probe		= sunxi_cpufreq_hw_probe,
	.remove_new	= sunxi_cpufreq_hw_remove,
};
module_platform_driver(dt_cpufreq_platdrv);

MODULE_AUTHOR("zhaozeyan<zhaozeyan@allwinnertech.com>");
MODULE_DESCRIPTION("sunxi hardware cpufreq driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.1");
