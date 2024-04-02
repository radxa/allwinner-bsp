// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright(c) 2019-2020 Allwinnertech Co., Ltd.
 *         http://www.allwinnertech.com
 *
 * Allwinner sunxi crash dump debug
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kmemleak.h>
#include <asm/cacheflush.h>
#include <linux/version.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/pm_opp.h>
#include <linux/math64.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#include <linux/panic_notifier.h>
#endif
#include "sunxi-sip.h"
#include "sunxi-smc.h"
#include "sunxi-crashdump.h"

#define SUNXI_DUMP_COMPATIBLE "sunxi-dump"
static LIST_HEAD(dump_group_list);
static int sunxi_dump = 1;
struct ctl_table_header *cth;

/*
 * Sometimes panic can't stop several CPUs unexpectedly, we'd better wait
 * for them offline in a period of time rather than forever.
 */
#define SUNXI_DUMP_CPU_ONLINE_TIMEOUT (1000 * 30)
#define POLICY_NUM                       4

struct sunxi_dump_group {
	char name[10];
	u32 *reg_buf;
	void __iomem *vir_base;
	phys_addr_t phy_base;
	u32 len;
	struct list_head list;
};

int sunxi_get_freq_info(struct device *dev, struct cpufreq_policy *policy, u64 start_time,
							u64 end_time, unsigned long target_freq, unsigned long last_freq);
int sunxi_store_freqinfo(struct device *dev, unsigned int arry_id, u64 start_time,
							u64 end_time, unsigned long target_freq, unsigned long last_freq);
int sunxi_dump_freq_info(void);

struct current_freq {
	unsigned char policy_name[32];
	unsigned int used;
	u64 cpu_freq_changed_start_time;
	u64 cpu_freq_changed_end_time;
	u64 last_freq_changed_time;
	unsigned long target_freq;
	unsigned long last_freq;
	unsigned long u_volt;
} current_freq_info[POLICY_NUM + 1];

int sunxi_store_freqinfo(struct device *dev, unsigned int arry_id, u64 start_time,
							u64 end_time, unsigned long target_freq, unsigned long last_freq)
{
	struct dev_pm_opp *opp = NULL;
	current_freq_info[arry_id].cpu_freq_changed_start_time = start_time;
	current_freq_info[arry_id].cpu_freq_changed_end_time = end_time;

	if (end_time - start_time)
		current_freq_info[arry_id].last_freq_changed_time = end_time - start_time;

	if (target_freq)
		current_freq_info[arry_id].target_freq = target_freq;

	if (last_freq)
		current_freq_info[arry_id].last_freq = last_freq;

	do {
		if (!dev) {
			pr_err("input dev error!\n");
			break;
		}

		opp = dev_pm_opp_find_freq_ceil(dev, &last_freq);
		if (IS_ERR(opp)) {
			pr_err("%s: unable to find cpu OPP for %lu\n", __func__, last_freq);
			break;
		}
		current_freq_info[arry_id].u_volt = dev_pm_opp_get_voltage(opp);
	} while (false);
	current_freq_info[arry_id].used = 1;
	return 0;
}

int sunxi_get_freq_info(struct device *dev, struct cpufreq_policy *policy, u64 start_time,
							u64 end_time, unsigned long target_freq, unsigned long last_freq)
{
	int i, policy_exist = 0;

	/* dram freq data */
	if (policy == NULL) {
		sunxi_store_freqinfo(dev, POLICY_NUM, start_time, end_time, target_freq, last_freq);
		return 0;
	}

	for (i = 0; i < POLICY_NUM; i++) {
		/* update current policy action */
		if (!strcmp(current_freq_info[i].policy_name, policy->kobj.name)) {
			sunxi_store_freqinfo(dev, i, start_time, end_time, target_freq, last_freq);
			policy_exist = 1;
			break;
		}
	}

	/* add new policy action */
	if (!policy_exist) {
		for (i = 0; i < POLICY_NUM; i++) {
			if (current_freq_info[i].used == 0) {
				strcpy(current_freq_info[i].policy_name, policy->kobj.name);
				sunxi_store_freqinfo(dev, i, start_time, end_time, target_freq, last_freq);
				break;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(sunxi_get_freq_info);

int sunxi_dump_freq_info(void)
{
	int i;
	pr_err("start dump cpuinfo:\n");
	for (i = 0; i < POLICY_NUM; i++) {
		if (current_freq_info[i].used == 1) {
			pr_err("policy_name = %s \n", current_freq_info[i].policy_name);
			pr_err("cpu_freq_changed_start_time = %llu(us) cpu_freq_changed_end_time = %llu(us) last_freq_changed_time = %llu(us) \n",
				div_u64(current_freq_info[i].cpu_freq_changed_start_time, 1000),
				div_u64(current_freq_info[i].cpu_freq_changed_end_time, 1000),
				div_u64(current_freq_info[i].last_freq_changed_time, 1000));
			pr_err("last_freq = %lu(khz) target_freq = %lu(khz) u_volt = %lu(uv) \n\n",
				current_freq_info[i].last_freq,
				current_freq_info[i].target_freq,
				current_freq_info[i].u_volt);
		}
	}

	if (current_freq_info[POLICY_NUM].used == 1) {
		pr_err("dram_freq_changed_start_time = %llu(us) dram_freq_changed_end_time = %llu(us) last_freq_changed_time = %llu(us) \n",
				div_u64(current_freq_info[POLICY_NUM].cpu_freq_changed_start_time, 1000),
				div_u64(current_freq_info[POLICY_NUM].cpu_freq_changed_end_time, 1000),
				div_u64(current_freq_info[POLICY_NUM].last_freq_changed_time, 1000));
		pr_err("last_freq = %lu(khz) target_freq = %lu(khz)\n\n",
				current_freq_info[POLICY_NUM].last_freq,
				current_freq_info[POLICY_NUM].target_freq);
	}

	return 0;
}
EXPORT_SYMBOL(sunxi_dump_freq_info);

static int sunxi_dump_group_reg(struct sunxi_dump_group *group)
{
	u32 *buf = group->reg_buf;
	void __iomem *membase = group->vir_base;
	u32 len = ALIGN(group->len, 4);
	int i;

	for (i = 0; i < len; i += 4)
		*(buf++) = readl(membase + i);

	return 0;
}

int sunxi_dump_group_dump(void)
{
	struct sunxi_dump_group *dump_group;

	list_for_each_entry(dump_group, &dump_group_list, list) {
		sunxi_dump_group_reg(dump_group);
	}
	return 0;
}
EXPORT_SYMBOL(sunxi_dump_group_dump);

static int sunxi_dump_group_register(const char *name, phys_addr_t start, u32 len)
{
	struct sunxi_dump_group *dump_group = NULL;

	dump_group = kmalloc(sizeof(*dump_group), GFP_KERNEL);
	if (!dump_group)
		return -ENOMEM;

	memcpy(dump_group->name, name, sizeof(dump_group->name));
	dump_group->phy_base = start;
	dump_group->len = len;
	dump_group->vir_base = ioremap(dump_group->phy_base, dump_group->len);
	if (!dump_group->vir_base) {
		pr_err("%s can't iomap\n", dump_group->name);
		return -EINVAL;
	}
	dump_group->reg_buf = kmalloc(dump_group->len, GFP_KERNEL);
	if (!dump_group->reg_buf)
		return -ENOMEM;

	list_add_tail(&dump_group->list, &dump_group_list);
	return 0;
}

void sunxi_dump_group_unregister(void)
{

	struct sunxi_dump_group *dump_group;

	list_for_each_entry(dump_group, &dump_group_list, list) {
		if (dump_group->vir_base)
			iounmap(dump_group->vir_base);

		if (dump_group->reg_buf)
			kfree(dump_group->reg_buf);

		list_del(&dump_group->list);

		kfree(dump_group);
	}

}

int sunxi_set_crashdump_mode(void)
{
#if IS_ENABLED(CONFIG_ARM64)
	invoke_scp_fn_smc(ARM_SVC_SUNXI_CRASHDUMP_START, 0, 0, 0);
#endif
#if IS_ENABLED(CONFIG_ARM)
	sunxi_optee_call_crashdump();
#endif

	return 0;
}

static int sunxi_dump_panic_event(struct notifier_block *self, unsigned long val, void *reason)
{
	unsigned int i, online_time;
	unsigned long __maybe_unused offset;

	if (!sunxi_dump) {
		pr_emerg("crashdump disabled\n");
		return NOTIFY_DONE;
	}

#if IS_REACHABLE(CONFIG_AW_DISP2)
	sunxi_kernel_panic_printf("CRASHDUMP NOW, PLEASE CONNECT TO TIGERDUMP TOOL\n AND KEEP POWER ON");
#endif

	flush_cache_all();
	mdelay(1000);

	sunxi_dump_group_dump();

	for (i = 0; i < num_possible_cpus(); i++) {
		if (i == smp_processor_id())
			continue;

		/*
		 * Notice: record the online time of per cpu,
		 * ignore those more than 30 seconds.
		 */
		online_time = 0;
		while (1) {
			if (!cpu_online(i))
				break;
			mdelay(10);
			online_time += 10;
			if (online_time >= SUNXI_DUMP_CPU_ONLINE_TIMEOUT)
				break;
		}
	}

	/* Notice: make sure to print the full stack trace */
	mdelay(5000);

	pr_emerg("\033[31mEnter crashdump, Please connect PC tool TigerDump\033[0m\n");
	/* Support to provide debug information for arm64 */
#if IS_ENABLED(CONFIG_ARM64)
	offset = kaslr_offset();
	pr_emerg("kimage_voffset: 0x%llx, kaslr: 0x%lx\n", kimage_voffset, offset);
#endif
	sunxi_dump_freq_info();
	sunxi_set_crashdump_mode();
	pr_emerg("crashdump exit\n");

#if IS_ENABLED(CONFIG_PANIC_DEBUG)
	if (reason != NULL && strstr(reason, "Watchdog detected hard LOCKUP"))
		while (1)
			cpu_relax();
#endif

	return NOTIFY_DONE;
}

static struct notifier_block sunxi_dump_panic_event_nb = {
	.notifier_call = sunxi_dump_panic_event,
	/* .priority = INT_MAX, */
};

static struct ctl_table sunxi_dump_sysctl_table[] = {
	{
		.procname = "sunxi_dump",
		.data = &sunxi_dump,
		.maxlen = sizeof(sunxi_dump),
		.mode = 0644,
		.proc_handler = proc_dointvec,
	},
	{ }
};

static struct ctl_table sunxi_dump_sysctl_root[] = {
	{
		.procname = "kernel",
		.mode = 0555,
		.child = sunxi_dump_sysctl_table,
	},
	{ }
};

int sunxi_crash_dump2pc_init(void)
{
	struct device_node *node;
	int i = 0;
	const char *name = NULL;
	struct resource res;

	node = of_find_compatible_node(NULL, NULL, SUNXI_DUMP_COMPATIBLE);

	for (i = 0; ; i++) {
		if (of_address_to_resource(node, i, &res))
			break;
		if (of_property_read_string_index(node, "group-names", i, &name))
			break;
		sunxi_dump_group_register(name, res.start, resource_size(&res));
	}

	/* register sunxi dump sysctl */
	cth = register_sysctl_table(sunxi_dump_sysctl_root);
	kmemleak_not_leak(cth);

	/* register sunxi dump panic notifier */
	atomic_notifier_chain_register(&panic_notifier_list, &sunxi_dump_panic_event_nb);

	return 0;
}

void sunxi_crash_dump2pc_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &sunxi_dump_panic_event_nb);
	unregister_sysctl_table(cth);
	sunxi_dump_group_unregister();
}
