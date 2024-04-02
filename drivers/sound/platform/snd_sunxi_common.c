// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner's ALSA SoC Audio driver
 *
 * Copyright (c) 2022, Dby <dby@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#define SUNXI_MODNAME		"sound-common"
#include "snd_sunxi_log.h"
#include <linux/module.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

#include "snd_sunxi_common.h"

/******* jack_state *******/
static int sunxi_jack_state;

void snd_sunxi_jack_state_upto_modparam(enum snd_jack_types type)
{
	sunxi_jack_state = type;
}

module_param_named(jack_state, sunxi_jack_state, int, S_IRUGO | S_IWUSR);

/******* reg label *******/
int snd_sunxi_save_reg(struct regmap *regmap, struct audio_reg_label *reg_labels)
{
	int i = 0;

	SND_LOG_DEBUG("\n");

	while (reg_labels[i].name != NULL) {
		regmap_read(regmap, reg_labels[i].address, &(reg_labels[i].value));
		i++;
	}

	return i;
}
EXPORT_SYMBOL_GPL(snd_sunxi_save_reg);

int snd_sunxi_echo_reg(struct regmap *regmap, struct audio_reg_label *reg_labels)
{
	int i = 0;

	SND_LOG_DEBUG("\n");

	while (reg_labels[i].name != NULL) {
		regmap_write(regmap, reg_labels[i].address, reg_labels[i].value);
		i++;
	}

	return i;
}
EXPORT_SYMBOL_GPL(snd_sunxi_echo_reg);

/******* regulator config *******/
struct snd_sunxi_rglt *snd_sunxi_regulator_init(struct platform_device *pdev)
{
	int ret, i, j;
	struct device *dev = NULL;
	struct device_node *np = NULL;
	struct snd_sunxi_rglt *rglt = NULL;
	struct snd_sunxi_rglt_unit *unit = NULL;
	struct snd_sunxi_rglt_unit *rglt_unit = NULL;
	u32 temp_val;
	char str[32] = {0};
	const char *out_string;
	struct {
		char *name;
		enum SND_SUNXI_RGLT_MODE mode;
	} of_mode_table[] = {
		{ "PMU",	SND_SUNXI_RGLT_PMU },
		{ "AUDIO",	SND_SUNXI_RGLT_AUDIO },
	};

	SND_LOG_DEBUG("\n");

	if (!pdev) {
		SND_LOG_ERR("platform_device invailed\n");
		return NULL;
	}
	dev = &pdev->dev;
	np = pdev->dev.of_node;

	rglt = kzalloc(sizeof(*rglt), GFP_KERNEL);
	if (!rglt) {
		SND_LOGDEV_ERR(dev, "can't allocate snd_sunxi_rglt memory\n");
		return NULL;
	}

	ret = of_property_read_u32(np, "rglt-max", &temp_val);
	if (ret < 0) {
		SND_LOGDEV_DEBUG(dev, "rglt-max get failed\n");
		rglt->unit_cnt = 0;
		return rglt;
	} else {
		rglt->unit_cnt = temp_val;
	}

	rglt_unit = kzalloc(sizeof(*rglt_unit) * rglt->unit_cnt, GFP_KERNEL);
	if (!rglt) {
		SND_LOGDEV_ERR(dev, "can't allocate rglt_unit memory\n");
		kfree(rglt_unit);
		return NULL;
	}

	for (i = 0; i < rglt->unit_cnt; ++i) {
		unit = &rglt_unit[i];
		snprintf(str, sizeof(str), "rglt%d-mode", i);
		ret = of_property_read_string(np, str, &out_string);
		if (ret < 0) {
			SND_LOGDEV_ERR(dev, "get %s failed\n", str);
			goto err;
		} else {
			for (j = 0; i < ARRAY_SIZE(of_mode_table); ++j) {
				if (strcmp(out_string, of_mode_table[i].name) == 0) {
					unit->mode = of_mode_table[i].mode;
					break;
				}
			}
		}
		switch (unit->mode) {
		case SND_SUNXI_RGLT_PMU:
			snprintf(str, sizeof(str), "rglt%d-voltage", i);
			ret = of_property_read_u32(np, str, &temp_val);
			if (ret < 0) {
				SND_LOGDEV_ERR(dev, "get %s failed\n", str);
				goto err;
			} else {
				unit->vcc_vol = temp_val;
			}
			snprintf(str, sizeof(str), "rglt%d", i);
			unit->vcc = regulator_get(dev, str);
			if (IS_ERR_OR_NULL(unit->vcc)) {
				SND_LOGDEV_ERR(dev, "get %s failed\n", str);
				goto err;
			}
			ret = regulator_set_voltage(unit->vcc, unit->vcc_vol, unit->vcc_vol);
			if (ret < 0) {
				SND_LOGDEV_ERR(dev, "set %s voltage failed\n", str);
				goto err;
			}
			ret = regulator_enable(unit->vcc);
			if (ret < 0) {
				SND_LOGDEV_ERR(dev, "enable %s failed\n", str);
				goto err;
			}
			break;
		default:
			SND_LOGDEV_DEBUG(dev, "%u mode no need to procees\n", unit->mode);
			break;
		}
	}

	rglt->unit = rglt_unit;
	rglt->priv = pdev;
	return rglt;
err:
	kfree(rglt_unit);
	kfree(rglt);
	return NULL;

}
EXPORT_SYMBOL_GPL(snd_sunxi_regulator_init);

void snd_sunxi_regulator_exit(struct snd_sunxi_rglt *rglt)
{
	int i;
	struct platform_device *pdev = NULL;
	struct device *dev = NULL;
	struct snd_sunxi_rglt_unit *unit = NULL;

	SND_LOG_DEBUG("\n");

	if (!rglt) {
		SND_LOG_ERR("snd_sunxi_rglt invailed\n");
		return;
	}
	pdev = (struct platform_device *)rglt->priv;
	dev = &pdev->dev;

	for (i = 0; i < rglt->unit_cnt; ++i) {
		unit = &rglt->unit[i];
		switch (unit->mode) {
		case SND_SUNXI_RGLT_PMU:
			if (!IS_ERR_OR_NULL(unit->vcc)) {
				regulator_disable(unit->vcc);
				regulator_put(unit->vcc);
			}
			break;
		default:
			break;
		}
	}

	if (rglt->unit)
		kfree(rglt->unit);
	kfree(rglt);
}
EXPORT_SYMBOL_GPL(snd_sunxi_regulator_exit);

int snd_sunxi_regulator_enable(struct snd_sunxi_rglt *rglt)
{
	int ret, i;
	struct platform_device *pdev = NULL;
	struct device *dev = NULL;
	struct snd_sunxi_rglt_unit *unit = NULL;

	SND_LOG_DEBUG("\n");

	if (!rglt) {
		SND_LOG_ERR("snd_sunxi_rglt invailed\n");
		return -1;
	}
	pdev = (struct platform_device *)rglt->priv;
	dev = &pdev->dev;

	for (i = 0; i < rglt->unit_cnt; ++i) {
		unit = &rglt->unit[i];
		switch (unit->mode) {
		case SND_SUNXI_RGLT_PMU:
			if (!IS_ERR_OR_NULL(unit->vcc)) {
				ret = regulator_enable(unit->vcc);
				if (ret) {
					SND_LOGDEV_ERR(dev, "enable vcc failed\n");
					return -1;
				}
			}
			break;
		default:
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sunxi_regulator_enable);

void snd_sunxi_regulator_disable(struct snd_sunxi_rglt *rglt)
{
	int i;
	struct platform_device *pdev = NULL;
	struct device *dev = NULL;
	struct snd_sunxi_rglt_unit *unit = NULL;

	SND_LOG_DEBUG("\n");

	if (!rglt) {
		SND_LOG_ERR("snd_sunxi_rglt invailed\n");
		return;
	}
	pdev = (struct platform_device *)rglt->priv;
	dev = &pdev->dev;

	for (i = 0; i < rglt->unit_cnt; ++i) {
		unit = &rglt->unit[i];
		switch (unit->mode) {
		case SND_SUNXI_RGLT_PMU:
			if (!IS_ERR_OR_NULL(unit->vcc))
				regulator_disable(unit->vcc);
			break;
		default:
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(snd_sunxi_regulator_disable);

/******* pa config *******/
DEFINE_SPINLOCK(pa_enable_lock);

static int pacfg_level_trig_init(struct snd_sunxi_pacfg *pa_cfg)
{
	int ret;
	u32 gpio_tmp;
	u32 temp_val;
	char str[32] = {0};
	u32 index = pa_cfg->index;
	struct platform_device *pdev = pa_cfg->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct pacfg_level_trig *level_trig = pa_cfg->level_trig;

	SND_LOG_DEBUG("\n");

	snprintf(str, sizeof(str), "pa-pin-%d", index);
	ret = of_get_named_gpio(np, str, 0);
	if (ret < 0) {
		SND_LOG_ERR("%s get failed\n", str);
		return -1;
	}
	gpio_tmp = ret;
	if (!gpio_is_valid(gpio_tmp)) {
		SND_LOG_ERR("%s (%u) is invalid\n", str, gpio_tmp);
		return -1;
	}
	ret = devm_gpio_request(&pdev->dev, gpio_tmp, str);
	if (ret) {
		SND_LOG_ERR("%s (%u) request failed\n", str, gpio_tmp);
		return -1;
	}
	level_trig->pin = gpio_tmp;

	snprintf(str, sizeof(str), "pa-pin-level-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default low\n", str);
		level_trig->level = 0;
	} else {
		if (temp_val > 0)
			level_trig->level = 1;
	}
	snprintf(str, sizeof(str), "pa-pin-msleep-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default 0\n", str);
		level_trig->msleep = 0;
	} else {
		level_trig->msleep = temp_val;
	}
	gpio_direction_output(level_trig->pin, !level_trig->level);

	return 0;
}

static void pacfg_level_trig_exit(struct snd_sunxi_pacfg *pa_cfg)
{
	(void)pa_cfg;
}

static void pacfg_level_trig_enable(struct work_struct *work)
{
	struct snd_sunxi_pacfg *pa_cfg = container_of(work,
						      struct snd_sunxi_pacfg, pa_en_work);
	struct pacfg_level_trig *level_trig = pa_cfg->level_trig;

	SND_LOG_DEBUG("\n");

	msleep(level_trig->msleep);
	gpio_set_value(level_trig->pin, level_trig->level);
}

static void pacfg_level_trig_disable(struct snd_sunxi_pacfg *pa_cfg)
{
	struct pacfg_level_trig *level_trig = pa_cfg->level_trig;

	SND_LOG_DEBUG("\n");

	gpio_set_value(level_trig->pin, !level_trig->level);
}

static int pacfg_pulse_trig_init(struct snd_sunxi_pacfg *pa_cfg)
{
	int ret;
	u32 gpio_tmp;
	u32 temp_val;
	char str[32] = {0};
	u32 index = pa_cfg->index;
	struct platform_device *pdev = pa_cfg->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct pacfg_pulse_trig *pulse_trig = pa_cfg->pulse_trig;

	SND_LOG_DEBUG("\n");

	snprintf(str, sizeof(str), "pa-pin-%d", index);
	ret = of_get_named_gpio(np, str, 0);
	if (ret < 0) {
		SND_LOG_ERR("%s get failed\n", str);
		return -1;
	}
	gpio_tmp = ret;
	if (!gpio_is_valid(gpio_tmp)) {
		SND_LOG_ERR("%s (%u) is invalid\n", str, gpio_tmp);
		return -1;
	}
	ret = devm_gpio_request(&pdev->dev, gpio_tmp, str);
	if (ret) {
		SND_LOG_ERR("%s (%u) request failed\n", str, gpio_tmp);
		return -1;
	}
	pulse_trig->pin = gpio_tmp;

	/* get level */
	snprintf(str, sizeof(str), "pa-pin-level-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default low\n", str);
		pulse_trig->level = 0;
	} else {
		if (temp_val > 0)
			pulse_trig->level = 1;
	}

	snprintf(str, sizeof(str), "pa-pin-msleep-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default 0\n", str);
		pulse_trig->msleep = 0;
	} else {
		pulse_trig->msleep = temp_val;
	}

	/* get polarity */
	snprintf(str, sizeof(str), "pa-pin-polarity-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default low\n", str);
		pulse_trig->polarity = 0;
	} else {
		if (temp_val > 0)
			pulse_trig->polarity = 1;
	}

	/* get duty_us */
	snprintf(str, sizeof(str), "pa-pin-duty-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default 0\n", str);
		pulse_trig->duty_us = 0;
	} else {
		pulse_trig->duty_us = temp_val;
	}

	/* get period_us */
	snprintf(str, sizeof(str), "pa-pin-period-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default 10\n", str);
		pulse_trig->period_us = 10;
	} else {
		if (temp_val == 0 || temp_val < pulse_trig->duty_us) {
			SND_LOG_WARN("%s invailed, duty_us * 2\n", str);
			pulse_trig->period_us = pulse_trig->duty_us * 2;
		} else {
			pulse_trig->period_us = temp_val;
		}
	}

	/* get period_cnt */
	snprintf(str, sizeof(str), "pa-pin-periodcnt-%d", index);
	ret = of_property_read_u32(np, str, &temp_val);
	if (ret < 0) {
		SND_LOG_WARN("%s get failed, default 1\n", str);
		pulse_trig->period_cnt = 5;
	} else {
		pulse_trig->period_cnt = temp_val;
	}
	gpio_direction_output(pulse_trig->pin, !pulse_trig->level);

	return 0;
}

static void pacfg_pulse_trig_exit(struct snd_sunxi_pacfg *pa_cfg)
{
	(void)pa_cfg;
}

static void pacfg_pulse_trig_enable(struct work_struct *work)
{
	struct snd_sunxi_pacfg *pa_cfg = container_of(work,
						      struct snd_sunxi_pacfg, pa_en_work);
	struct pacfg_pulse_trig *pulse_trig = pa_cfg->pulse_trig;
	unsigned long flags;
	u32 i;

	SND_LOG_DEBUG("\n");

	msleep(pulse_trig->msleep);

	spin_lock_irqsave(&pa_enable_lock, flags);

	gpio_set_value(pulse_trig->pin, !pulse_trig->level);
	for (i = 0; i < pulse_trig->period_cnt; ++i) {
		if (pulse_trig->duty_us) {
			gpio_set_value(pulse_trig->pin, pulse_trig->polarity);
			udelay(pulse_trig->duty_us);
		}
		if (pulse_trig->period_us - pulse_trig->duty_us) {
			gpio_set_value(pulse_trig->pin, !pulse_trig->polarity);
			udelay(pulse_trig->period_us - pulse_trig->duty_us);
		}
	}

	gpio_set_value(pulse_trig->pin, pulse_trig->level);

	spin_unlock_irqrestore(&pa_enable_lock, flags);
}

static void pacfg_pulse_trig_disable(struct snd_sunxi_pacfg *pa_cfg)
{
	struct pacfg_pulse_trig *pulse_trig = pa_cfg->pulse_trig;

	SND_LOG_DEBUG("\n");

	gpio_set_value(pulse_trig->pin, !pulse_trig->level);
}

static int pacfg_user_trig_init(struct snd_sunxi_pacfg *pa_cfg)
{
	(void)pa_cfg;
	return 0;	/* if success */
}

static void pacfg_user_trig_exit(struct snd_sunxi_pacfg *pa_cfg)
{
	(void)pa_cfg;
}

static int pacfg_user_trig_enable(struct snd_sunxi_pacfg *pa_cfg)
{
	(void)pa_cfg;
	return 0;	/* if success */
}

static void pacfg_user_trig_disable(struct snd_sunxi_pacfg *pa_cfg)
{
	(void)pa_cfg;
}

struct snd_sunxi_pacfg *snd_sunxi_pa_pin_init(struct platform_device *pdev, u32 *pa_pin_max)
{
	int ret, i;
	u32 pin_max;
	u32 temp_val;
	char str[32] = {0};
	struct snd_sunxi_pacfg *pa_cfg;
	enum SND_SUNXI_PACFG_MODE pacfg_mode = SND_SUNXI_PA_CFG_LEVEL;
	struct device_node *np = pdev->dev.of_node;

	SND_LOG_DEBUG("\n");

	*pa_pin_max = 0;
	ret = of_property_read_u32(np, "pa-pin-max", &temp_val);
	if (ret < 0) {
		SND_LOG_DEBUG("pa-pin-max get failed, default 0\n");
		return NULL;
	} else {
		pin_max = temp_val;
	}

	pa_cfg = kzalloc(sizeof(*pa_cfg) * pin_max, GFP_KERNEL);
	if (!pa_cfg) {
		SND_LOG_ERR("can't snd_sunxi_pacfg memory\n");
		return NULL;
	}

	for (i = 0; i < pin_max; i++) {
		pa_cfg[i].index = i;
		pa_cfg[i].pdev = pdev;
		snprintf(str, sizeof(str), "pa-cfg-mode-%d", i);
		ret = of_property_read_u32(np, str, &temp_val);
		if (ret < 0) {
			SND_LOG_DEBUG("%s get failed, default level trigger mode\n", str);
			pacfg_mode = SND_SUNXI_PA_CFG_LEVEL;
		} else {
			if (temp_val < SND_SUNXI_PA_CFG_CNT)
				pacfg_mode = temp_val;
			else if (temp_val == 0xff)
				pacfg_mode = SND_SUNXI_PA_CFG_USER;
			else
				pacfg_mode = SND_SUNXI_PA_CFG_LEVEL;
		}
		pa_cfg[i].mode = pacfg_mode;

		switch (pacfg_mode) {
		case SND_SUNXI_PA_CFG_LEVEL:
			pa_cfg[i].level_trig = kzalloc(sizeof(*pa_cfg[i].level_trig), GFP_KERNEL);
			if (!pa_cfg[i].level_trig) {
				SND_LOG_ERR("no memory\n");
				goto err;
			}
			ret = pacfg_level_trig_init(&pa_cfg[i]);
			if (ret) {
				SND_LOG_ERR("pacfg_level_trig_init failed\n");
				pa_cfg[i].used = false;
				continue;
			}
			INIT_WORK(&pa_cfg[i].pa_en_work, pacfg_level_trig_enable);
			pa_cfg[i].used = true;
			break;
		case SND_SUNXI_PA_CFG_PULSE:
			pa_cfg[i].pulse_trig = kzalloc(sizeof(*pa_cfg[i].pulse_trig), GFP_KERNEL);
			if (!pa_cfg[i].pulse_trig) {
				SND_LOG_ERR("no memory\n");
				goto err;
			}
			ret = pacfg_pulse_trig_init(&pa_cfg[i]);
			if (ret) {
				SND_LOG_ERR("pacfg_pulse_trig_init failed\n");
				pa_cfg[i].used = false;
				continue;
			}
			INIT_WORK(&pa_cfg[i].pa_en_work, pacfg_pulse_trig_enable);
			pa_cfg[i].used = true;
			break;
		case SND_SUNXI_PA_CFG_USER:
			pa_cfg[i].user_trig = kzalloc(sizeof(*pa_cfg[i].user_trig), GFP_KERNEL);
			if (!pa_cfg[i].user_trig) {
				SND_LOG_ERR("no memory\n");
				goto err;
			}
			ret = pacfg_user_trig_init(&pa_cfg[i]);
			if (ret) {
				SND_LOG_ERR("pacfg_user_trig_init failed\n");
				pa_cfg[i].used = false;
				continue;
			}
			pa_cfg[i].used = true;
			break;
		default:
			SND_LOG_WARN("unsupport pa config mode %d\n", pacfg_mode);
			pa_cfg[i].used = false;
			break;
		}
	}

	*pa_pin_max = pin_max;
	snd_sunxi_pa_pin_disable(pa_cfg, pin_max);
	return pa_cfg;
err:
	for (i = 0; i < pin_max; i++) {
		kfree(pa_cfg[i].level_trig);
		kfree(pa_cfg[i].pulse_trig);
		kfree(pa_cfg[i].user_trig);
	}
	kfree(pa_cfg);
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_sunxi_pa_pin_init);

void snd_sunxi_pa_pin_exit(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max)
{
	int i;

	SND_LOG_DEBUG("\n");

	snd_sunxi_pa_pin_disable(pa_cfg, pa_pin_max);

	for (i = 0; i < pa_pin_max; i++) {
		if (!pa_cfg[i].used)
			continue;

		switch (pa_cfg[i].mode) {
		case SND_SUNXI_PA_CFG_LEVEL:
			pacfg_level_trig_exit(&pa_cfg[i]);
			cancel_work_sync(&pa_cfg[i].pa_en_work);
			break;
		case SND_SUNXI_PA_CFG_PULSE:
			pacfg_pulse_trig_exit(&pa_cfg[i]);
			cancel_work_sync(&pa_cfg[i].pa_en_work);
			break;
		case SND_SUNXI_PA_CFG_USER:
			pacfg_user_trig_exit(&pa_cfg[i]);
			break;
		default:
			SND_LOG_WARN("unsupport pa config mode %d\n", pa_cfg[i].mode);
			break;
		}
	}

	for (i = 0; i < pa_pin_max; i++) {
		kfree(pa_cfg[i].level_trig);
		kfree(pa_cfg[i].pulse_trig);
		kfree(pa_cfg[i].user_trig);
	}
	kfree(pa_cfg);
}
EXPORT_SYMBOL_GPL(snd_sunxi_pa_pin_exit);

/*
 * the example of adding component of pa
 * ucontrol->value.integer.value[0] -- value get from user or set by user.
 *                                  -- 0 - "Off", 1 - "On".
 */
#define PA_ADD_KCONTROL 0
#if PA_ADD_KCONTROL
static int sunxi_pa_param1_get_val(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = xxx;
	return 0;
}

static int sunxi_pa_param1_set_val(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct sunxi_codec *codec = snd_soc_component_get_drvdata(component);
	struct snd_sunxi_pacfg *pa_cfg = codec->pa_cfg;

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		xxx
		break;
	case 1:
		xxx
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
#endif

int snd_sunxi_pa_pin_probe(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max,
			   struct snd_soc_component *component)
{
	/* the example of adding component of pa:
	 * 1.Define optional text.
	 * 2.Define enum by SOC_ENUM_SINGLE_EXT_DECL().
	 * 3.Define controls[] for each pa.
	 * 4.Add componnet by snd_soc_add_component_controls().
	 */
#if PA_ADD_KCONTROL
	char *sunxi_pa_param1_text[] = {"Off", "On"};

	SOC_ENUM_SINGLE_EXT_DECL(sunxi_pa_param1_enum, sunxi_pa_param1_text);

	struct snd_kcontrol_new sunxi_pa_controls[] = {
		SOC_ENUM_EXT("PA PARAM1", sunxi_pa_param1_enum,
			     sunxi_pa_param1_get_val,
			     sunxi_pa_param1_set_val),
		...
	};

	SND_LOG_DEBUG("\n");

	ret = snd_soc_add_component_controls(component, sunxi_pa_controls,
					     ARRAY_SIZE(sunxi_pa_controls));
	if (ret)
		SND_LOG_ERR("register pa kcontrols failed\n");
#endif

	(void)pa_cfg;
	(void)pa_pin_max;
	(void)component;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_sunxi_pa_pin_probe);

void snd_sunxi_pa_pin_remove(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max)
{
	(void)pa_cfg;
	(void)pa_pin_max;
}
EXPORT_SYMBOL_GPL(snd_sunxi_pa_pin_remove);

int snd_sunxi_pa_pin_enable(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max)
{
	int i, ret;

	SND_LOG_DEBUG("\n");

	if (pa_pin_max < 1) {
		SND_LOG_DEBUG("no pa pin config\n");
		return 0;
	}

	for (i = 0; i < pa_pin_max; i++) {
		if (!pa_cfg[i].used)
			continue;

		switch (pa_cfg[i].mode) {
		case SND_SUNXI_PA_CFG_LEVEL:
			schedule_work(&pa_cfg[i].pa_en_work);
			break;
		case SND_SUNXI_PA_CFG_PULSE:
			schedule_work(&pa_cfg[i].pa_en_work);
			break;
		case SND_SUNXI_PA_CFG_USER:
			ret = pacfg_user_trig_enable(&pa_cfg[i]);
			if (ret) {
				SND_LOG_ERR("pacfg_user_trig_enable failed\n");
				continue;
			}
			break;
		default:
			SND_LOG_WARN("unsupport pa config mode %d\n", pa_cfg[i].mode);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sunxi_pa_pin_enable);

void snd_sunxi_pa_pin_disable(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max)
{
	int i;

	SND_LOG_DEBUG("\n");

	if (pa_pin_max < 1) {
		SND_LOG_DEBUG("no pa pin config\n");
		return;
	}

	for (i = 0; i < pa_pin_max; i++) {
		if (!pa_cfg[i].used)
			continue;

		switch (pa_cfg[i].mode) {
		case SND_SUNXI_PA_CFG_LEVEL:
			pacfg_level_trig_disable(&pa_cfg[i]);
			break;
		case SND_SUNXI_PA_CFG_PULSE:
			pacfg_pulse_trig_disable(&pa_cfg[i]);
			break;
		case SND_SUNXI_PA_CFG_USER:
			pacfg_user_trig_disable(&pa_cfg[i]);
			break;
		default:
			SND_LOG_WARN("unsupport pa config mode %d\n", pa_cfg[i].mode);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(snd_sunxi_pa_pin_disable);

/******* hdmi format config *******/
DEFINE_SPINLOCK(hdmi_fmt_lock);

static enum HDMI_FORMAT g_hdmi_fmt;

enum HDMI_FORMAT snd_sunxi_hdmi_get_fmt(void)
{
	enum HDMI_FORMAT tmp_hdmi_fmt;
	unsigned long flags;

	spin_lock_irqsave(&hdmi_fmt_lock, flags);
	tmp_hdmi_fmt = g_hdmi_fmt;
	spin_unlock_irqrestore(&hdmi_fmt_lock, flags);

	return tmp_hdmi_fmt;
}
EXPORT_SYMBOL_GPL(snd_sunxi_hdmi_get_fmt);

int snd_sunxi_hdmi_set_fmt(int hdmi_fmt)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi_fmt_lock, flags);
	g_hdmi_fmt = hdmi_fmt;
	spin_unlock_irqrestore(&hdmi_fmt_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sunxi_hdmi_set_fmt);

int snd_sunxi_hdmi_get_dai_type(struct device_node *np, unsigned int *dai_type)
{
	int ret;
	const char *str;

	SND_LOG_DEBUG("\n");

	if (!np) {
		SND_LOG_ERR("np is err\n");
		return -1;
	}

	ret = of_property_read_string(np, "dai-type", &str);
	if (ret < 0) {
		*dai_type = SUNXI_DAI_I2S_TYPE;
	} else {
		if (strcmp(str, "hdmi") == 0) {
			*dai_type = SUNXI_DAI_HDMI_TYPE;
		} else {
			*dai_type = SUNXI_DAI_I2S_TYPE;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sunxi_hdmi_get_dai_type);

/******* PCM uncommon format config *******/
static LIST_HEAD(ucfmt_list);
static DEFINE_MUTEX(ucfmt_mutex);

int snd_sunxi_ucfmt_register_cb(ucfmt_callback_f callback, const char *dai_name)
{
	struct snd_sunxi_ucfmt *ucfmt, *c;
	unsigned int i;

	SND_LOG_DEBUG("\n");

	if (!callback) {
		SND_LOG_ERR("callback invalid\n");
		return -1;
	}

	if (!dai_name) {
		SND_LOG_ERR("dai name invalid\n");
		return -1;
	}

	mutex_lock(&ucfmt_mutex);

	list_for_each_entry_safe(ucfmt, c, &ucfmt_list, list) {
		if (!strcmp(ucfmt->cpu_dai_cb.dai_name, dai_name)) {
			ucfmt->cpu_dai_cb.callback = callback;
			mutex_unlock(&ucfmt_mutex);
			return 0;
		}
		for (i = 0; i < ucfmt->num_codecs; i++) {
			if (!strcmp(ucfmt->codec_dai_cbs[i].dai_name, dai_name)) {
				ucfmt->codec_dai_cbs[i].callback = callback;
				mutex_unlock(&ucfmt_mutex);
				return 0;
			}
		}
	}

	mutex_unlock(&ucfmt_mutex);

	SND_LOG_ERR("can't find any ucfmt\n");
	return -1;
}
EXPORT_SYMBOL(snd_sunxi_ucfmt_register_cb);

void snd_sunxi_ucfmt_unregister_cb(const char *dai_name)
{
	struct snd_sunxi_ucfmt *ucfmt, *c;
	unsigned int i;

	SND_LOG_DEBUG("\n");

	if (!dai_name) {
		SND_LOG_ERR("dai name invalid\n");
		return;
	}

	mutex_lock(&ucfmt_mutex);

	list_for_each_entry_safe(ucfmt, c, &ucfmt_list, list) {
		if (!strcmp(ucfmt->cpu_dai_cb.dai_name, dai_name)) {
			ucfmt->cpu_dai_cb.callback = NULL;
			mutex_unlock(&ucfmt_mutex);
			return;
		}
		for (i = 0; i < ucfmt->num_codecs; i++) {
			if (!strcmp(ucfmt->codec_dai_cbs[i].dai_name, dai_name)) {
				ucfmt->codec_dai_cbs[i].callback = NULL;
				mutex_unlock(&ucfmt_mutex);
				return;
			}
		}
	}

	mutex_unlock(&ucfmt_mutex);
}
EXPORT_SYMBOL(snd_sunxi_ucfmt_unregister_cb);

int snd_sunxi_ucfmt_probe(struct snd_soc_card *card, struct snd_sunxi_ucfmt **ucfmt)
{
	struct snd_soc_dai_link *dai_link = card->dai_link;
	struct snd_soc_dai *cpu_dai = snd_soc_find_dai(dai_link->cpus);
	struct snd_soc_dai *codec_dai;
	struct device *dev = card->dev;
	struct device_node *top_np = dev->of_node;
	struct snd_sunxi_ucfmt *ucfmt_ref;
	int ret;
	unsigned int i, temp_val;

	SND_LOG_DEBUG("\n");

	if (!card) {
		SND_LOG_ERR("soundcard invalid\n");
		return -1;
	}

	ucfmt_ref = kzalloc(sizeof(struct snd_sunxi_ucfmt), GFP_KERNEL);
	if (!ucfmt_ref)
		return -ENOMEM;

	ucfmt_ref->cpu_dai_cb.dai_name = cpu_dai->name;

	ucfmt_ref->num_codecs = dai_link->num_codecs;
	ucfmt_ref->codec_dai_cbs = kcalloc(ucfmt_ref->num_codecs,
					   sizeof(struct snd_sunxi_ucfmt_cb), GFP_KERNEL);
	if (!ucfmt_ref->codec_dai_cbs)
		return -ENOMEM;
	for (i = 0; i < ucfmt_ref->num_codecs; i++) {
		codec_dai = snd_soc_find_dai(&dai_link->codecs[i]);
		ucfmt_ref->codec_dai_cbs[i].dai_name = codec_dai->name;
	}

	ucfmt_ref->tx_lsb_first = of_property_read_bool(top_np, "tx-lsb-first");
	ucfmt_ref->rx_lsb_first = of_property_read_bool(top_np, "rx-lsb-first");

	ucfmt_ref->fmt = dai_link->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	ret = of_property_read_u32(top_np, "data-late", &temp_val);
	if (ret < 0 || temp_val > 3) {
		SND_LOG_WARN("set data late to default\n");
		if (ucfmt_ref->fmt == SND_SOC_DAIFMT_I2S)
			ucfmt_ref->data_late = 1;
		else if (ucfmt_ref->fmt == SND_SOC_DAIFMT_RIGHT_J
			 || ucfmt_ref->fmt == SND_SOC_DAIFMT_LEFT_J)
			ucfmt_ref->data_late = 0;
		else if (ucfmt_ref->fmt == SND_SOC_DAIFMT_DSP_A)
			ucfmt_ref->data_late = 1;
		else if (ucfmt_ref->fmt == SND_SOC_DAIFMT_DSP_B)
			ucfmt_ref->data_late = 0;
	} else if (ucfmt_ref->fmt == SND_SOC_DAIFMT_DSP_A
		   || ucfmt_ref->fmt == SND_SOC_DAIFMT_DSP_B) {
		ucfmt_ref->data_late = temp_val;
	}

	mutex_lock(&ucfmt_mutex);
	list_add_tail(&ucfmt_ref->list, &ucfmt_list);
	mutex_unlock(&ucfmt_mutex);

	*ucfmt = ucfmt_ref;

	return 0;
}
EXPORT_SYMBOL(snd_sunxi_ucfmt_probe);

void snd_sunxi_ucfmt_remove(struct snd_sunxi_ucfmt *ucfmt)
{
	struct snd_sunxi_ucfmt *ucfmt_del, *c;

	SND_LOG_DEBUG("\n");

	if (!ucfmt) {
		SND_LOG_ERR("ucfmt invailed\n");
		return;
	}

	mutex_lock(&ucfmt_mutex);
	list_for_each_entry_safe(ucfmt_del, c, &ucfmt_list, list) {
		if (!strcmp(ucfmt_del->cpu_dai_cb.dai_name, ucfmt->cpu_dai_cb.dai_name)) {
			SND_LOG_DEBUG("ucfmt(cpu dai:%s) del\n",
				      ucfmt_del->cpu_dai_cb.dai_name);
			list_del(&ucfmt_del->list);

			mutex_unlock(&ucfmt_mutex);

			kfree(ucfmt_del->codec_dai_cbs);
			kfree(ucfmt_del);
		}
	}
	mutex_unlock(&ucfmt_mutex);
}
EXPORT_SYMBOL(snd_sunxi_ucfmt_remove);

/******* sysfs dump *******/
struct snd_sunxi_dev {
	dev_t snd_dev;
	struct class *snd_class;
	char *snd_dev_name;
	char *snd_class_name;
};

static LIST_HEAD(dump_list);
static struct mutex dump_mutex;

int snd_sunxi_dump_register(struct snd_sunxi_dump *dump)
{
	struct snd_sunxi_dump *dump_tmp, *c;

	SND_LOG_DEBUG("\n");

	if (!dump) {
		SND_LOG_ERR("snd sunxi dump invailed\n");
		return -1;
	}
	if (!dump->name) {
		SND_LOG_ERR("snd sunxi dump name null\n");
		return -1;
	}

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_tmp, c, &dump_list, list) {
		if (!strcmp(dump_tmp->name, dump->name)) {
			SND_LOG_ERR("snd dump(%s) already exist\n", dump->name);
			mutex_unlock(&dump_mutex);
			return -1;
		}
	}

	dump->use = false;
	list_add_tail(&dump->list, &dump_list);
	SND_LOG_DEBUG("snd dump(%s) add\n", dump->name);

	mutex_unlock(&dump_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sunxi_dump_register);

void snd_sunxi_dump_unregister(struct snd_sunxi_dump *dump)
{
	struct snd_sunxi_dump *dump_del, *c;

	SND_LOG_DEBUG("\n");

	if (!dump) {
		SND_LOG_ERR("snd sunxi dump invailed\n");
		return;
	}

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_del, c, &dump_list, list) {
		if (!strcmp(dump_del->name, dump->name)) {
			SND_LOG_DEBUG("snd dump(%s) del\n", dump_del->name);
			list_del(&dump_del->list);
		}
	}

	mutex_unlock(&dump_mutex);
}
EXPORT_SYMBOL_GPL(snd_sunxi_dump_unregister);

static ssize_t snd_sunxi_version_show(struct class *class, struct class_attribute *attr, char *buf)
{
	size_t count = 0, cound_tmp = 0;
	struct snd_sunxi_dump *dump_tmp, *c;
	struct snd_sunxi_dump *dump = NULL;

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_tmp, c, &dump_list, list) {
		dump = dump_tmp;
		if (dump && dump->dump_version) {
			count += sprintf(buf + count, "module(%s) version: ", dump->name);
			dump->dump_version(dump->priv, buf + count, &cound_tmp);
			count += cound_tmp;
		}
	}

	mutex_unlock(&dump_mutex);

	return count;
}

static ssize_t snd_sunxi_help_show(struct class *class, struct class_attribute *attr, char *buf)
{
	size_t count = 0, cound_tmp = 0;
	struct snd_sunxi_dump *dump_tmp, *c;
	struct snd_sunxi_dump *dump = NULL;

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_tmp, c, &dump_list, list)
		if (dump_tmp->use)
			dump = dump_tmp;

	mutex_unlock(&dump_mutex);

	count += sprintf(buf + count, "== module help ==\n");
	count += sprintf(buf + count, "1. get optional modules: cat module\n");
	count += sprintf(buf + count, "2. set current module  : echo {module name} > module\n");

	if (dump && dump->dump_help) {
		count += sprintf(buf + count, "== current module(%s) help ==\n", dump->name);
		dump->dump_help(dump->priv, buf + count, &cound_tmp);
		count += cound_tmp;
	} else if (dump && !dump->dump_help) {
		count += sprintf(buf + count, "== current module(%s), but not help ==\n", dump->name);
	} else {
		count += sprintf(buf + count, "== current module(NULL) ==\n");
	}

	return count;
}

static ssize_t snd_sunxi_module_show(struct class *class, struct class_attribute *attr, char *buf)
{
	size_t count = 0;
	struct snd_sunxi_dump *dump_tmp, *c;
	struct snd_sunxi_dump *dump = NULL;
	unsigned int module_num = 0;

	count += sprintf(buf + count, "optional modules:\n");
	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_tmp, c, &dump_list, list) {
		count += sprintf(buf + count, "%u. %s\n", ++module_num, dump_tmp->name);
		if (dump_tmp->use)
			dump = dump_tmp;
	}

	mutex_unlock(&dump_mutex);

	if (dump)
		count += sprintf(buf + count, "current module(%s)\n", dump->name);
	else
		count += sprintf(buf + count, "current module(NULL)\n");

	return count;
}

static ssize_t snd_sunxi_module_store(struct class *class, struct class_attribute *attr,
				      const char *buf, size_t count)
{
	struct snd_sunxi_dump *dump, *c;
	int scanf_cnt = 0;
	char arg1[32] = {0};

	scanf_cnt = sscanf(buf, "%31s", arg1);
	if (scanf_cnt != 1)
		return count;

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump, c, &dump_list, list) {
		if (!strcmp(arg1, dump->name))
			dump->use = true;
		else
			dump->use = false;
	}

	mutex_unlock(&dump_mutex);

	return count;
}

static ssize_t snd_sunxi_dump_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int ret;
	size_t count = 0, cound_tmp = 0;
	struct snd_sunxi_dump *dump_tmp, *c;
	struct snd_sunxi_dump *dump = NULL;

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_tmp, c, &dump_list, list)
		if (dump_tmp->use)
			dump = dump_tmp;

	mutex_unlock(&dump_mutex);

	if (dump && dump->dump_show) {
		count += sprintf(buf + count, "module(%s)\n", dump->name);
		ret = dump->dump_show(dump->priv, buf + count, &cound_tmp);
		if (ret)
			pr_err("module(%s) show failed\n", dump->name);
		count += cound_tmp;
	} else if (dump && !dump->dump_show) {
		count += sprintf(buf + count, "current module(%s), but not show\n", dump->name);
	} else {
		count += sprintf(buf + count, "current module(NULL)\n");
	}

	return count;
}

static ssize_t snd_sunxi_dump_store(struct class *class, struct class_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	struct snd_sunxi_dump *dump_tmp, *c;
	struct snd_sunxi_dump *dump = NULL;

	mutex_lock(&dump_mutex);

	list_for_each_entry_safe(dump_tmp, c, &dump_list, list)
		if (dump_tmp->use)
			dump = dump_tmp;

	mutex_unlock(&dump_mutex);

	if (dump && dump->dump_store) {
		ret = dump->dump_store(dump->priv, buf, count);
		if (ret)
			pr_err("module(%s) store failed\n", dump->name);
	}

	return count;
}

static struct class_attribute snd_class_attrs[] = {
	__ATTR(version, 0644, snd_sunxi_version_show, NULL),
	__ATTR(help, 0644, snd_sunxi_help_show, NULL),
	__ATTR(module, 0644, snd_sunxi_module_show, snd_sunxi_module_store),
	__ATTR(dump, 0644, snd_sunxi_dump_show, snd_sunxi_dump_store),
};

#if IS_ENABLED(CONFIG_SND_SOC_SUNXI_DEBUG)
static int snd_sunxi_debug_create(struct snd_sunxi_dev *sunxi_dev)
{
	int ret, i;
	unsigned int debug_node_cnt;

	SND_LOG_DEBUG("\n");

	debug_node_cnt = ARRAY_SIZE(snd_class_attrs);
	for (i = 0; i < debug_node_cnt; i++) {
		ret = class_create_file(sunxi_dev->snd_class, &snd_class_attrs[i]);
		if (ret) {
			SND_LOG_ERR("class_create_file %s failed\n",
				    snd_class_attrs[i].attr.name);
			return -1;
		}
	}

	return 0;
}

static void snd_sunxi_debug_remove(struct snd_sunxi_dev *sunxi_dev)
{
	int i;
	unsigned int debug_node_cnt;

	SND_LOG_DEBUG("\n");

	debug_node_cnt = ARRAY_SIZE(snd_class_attrs);
	for (i = 0; i < debug_node_cnt; i++)
		class_remove_file(sunxi_dev->snd_class, &snd_class_attrs[i]);
}
#else
static int snd_sunxi_debug_create(struct snd_sunxi_dev *sunxi_dev)
{
	(void)snd_class_attrs;

	SND_LOG_DEBUG("unsupport debug\n");
	(void)sunxi_dev;
	return 0;
}

static void snd_sunxi_debug_remove(struct snd_sunxi_dev *sunxi_dev)
{
	SND_LOG_DEBUG("unsupport debug\n");
	(void)sunxi_dev;
}
#endif

static int _snd_sunxi_dev_init(struct snd_sunxi_dev *sunxi_dev)
{
	int ret;

	SND_LOG_DEBUG("\n");

	if (IS_ERR_OR_NULL(sunxi_dev)) {
		SND_LOG_ERR("snd_sunxi_dev is NULL\n");
		return -1;
	}
	if (IS_ERR_OR_NULL(sunxi_dev->snd_dev_name) ||
	    IS_ERR_OR_NULL(sunxi_dev->snd_class_name)) {
		SND_LOG_ERR("snd_sunxi_dev name member is NULL\n");
		return -1;
	}

	ret = alloc_chrdev_region(&sunxi_dev->snd_dev, 0, 1, sunxi_dev->snd_dev_name);
	if (ret) {
		SND_LOG_ERR("alloc_chrdev_region failed\n");
		goto err_alloc_chrdev;
	}
	SND_LOG_DEBUG("sunxi_dev major = %u, sunxi_dev minor = %u\n",
		      MAJOR(sunxi_dev->snd_dev), MINOR(sunxi_dev->snd_dev));

	sunxi_dev->snd_class = class_create(THIS_MODULE, sunxi_dev->snd_class_name);
	if (IS_ERR_OR_NULL(sunxi_dev->snd_class)) {
		SND_LOG_ERR("class_create failed\n");
		goto err_class_create;
	}

	ret = snd_sunxi_debug_create(sunxi_dev);
	if (ret) {
		SND_LOG_ERR("snd_sunxi_debug_create failed\n");
		goto err_class_create_file;
	}

	mutex_init(&dump_mutex);

	return 0;

err_class_create_file:
	class_destroy(sunxi_dev->snd_class);
err_class_create:
	unregister_chrdev_region(sunxi_dev->snd_dev, 1);
err_alloc_chrdev:
	return -1;
}

static void _snd_sunxi_dev_exit(struct snd_sunxi_dev *sunxi_dev)
{
	SND_LOG_DEBUG("\n");

	if (IS_ERR_OR_NULL(sunxi_dev)) {
		SND_LOG_ERR("snd_sunxi_dev is NULL\n");
		return;
	}
	if (IS_ERR_OR_NULL(sunxi_dev->snd_class)) {
		SND_LOG_ERR("snd_sunxi_dev class is NULL\n");
		return;
	}

	snd_sunxi_debug_remove(sunxi_dev);

	class_destroy(sunxi_dev->snd_class);
	unregister_chrdev_region(sunxi_dev->snd_dev, 1);

	mutex_destroy(&dump_mutex);
}

static struct snd_sunxi_dev sunxi_dev = {
	.snd_dev_name = "snd_sunxi_dev",
	.snd_class_name = "snd_sunxi",
};

int __init snd_sunxi_dev_init(void)
{
	SND_LOG_DEBUG("\n");
	return _snd_sunxi_dev_init(&sunxi_dev);
}

void __exit snd_sunxi_dev_exit(void)
{
	SND_LOG_DEBUG("\n");
	_snd_sunxi_dev_exit(&sunxi_dev);
}

module_init(snd_sunxi_dev_init);
module_exit(snd_sunxi_dev_exit);

MODULE_AUTHOR("Dby@allwinnertech.com");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1.0");
MODULE_DESCRIPTION("sunxi common interface");
