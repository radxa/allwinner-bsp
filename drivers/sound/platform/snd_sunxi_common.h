/* SPDX-License-Identifier: GPL-2.0-or-later */
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

#ifndef __SND_SUNXI_COMMON_H
#define __SND_SUNXI_COMMON_H

#include <sound/soc.h>
#include <sound/jack.h>

/******* reg label *******/
#define REG_LABEL(constant)	{#constant, constant, 0}
#define REG_LABEL_END		{NULL, 0, 0}

struct audio_reg_label {
	const char *name;
	const unsigned int address;
	unsigned int value;
};

/* EX:
 * static struct audio_reg_label reg_labels[] = {
 * 	REG_LABEL(SUNXI_REG_0),
 * 	REG_LABEL(SUNXI_REG_1),
 * 	REG_LABEL(SUNXI_REG_n),
 * 	REG_LABEL_END,
 * };
 */
int snd_sunxi_save_reg(struct regmap *regmap, struct audio_reg_label *reg_labels);
int snd_sunxi_echo_reg(struct regmap *regmap, struct audio_reg_label *reg_labels);

/******* regulator config *******/
/* board.dts format
 * &xxx {
 *	rglt-max	= <n>;		n: rglt cnt
 *	rglt0-mode	= "xxx";	PMU; AUDIO;
 *	rglt0-voltage	= <n>;		n: vcc voltage
 *	rglt0-supply	= <&pmu_node>;
 *	rglt1-xxx
 *	...
 * };
 *
 * EX: rglt cnt = 2
 * &xxx {
 *	rglt-max	= <2>;
 *	rglt0-mode	= "AUDIO";
 *	rglt0-voltage	= <1800000>;	1.8v
 *	rglt0-supply	= <>;		AUDIO mode, unnecessary.
 *	rglt1-mode	= PMU;
 *	rglt1-voltage	= <3300000>;	3.3v
 *	rglt1-supply	= <&reg_aldo1>;	pmu node
 * };
 */
enum SND_SUNXI_RGLT_MODE {
	SND_SUNXI_RGLT_NULL = -1,
	SND_SUNXI_RGLT_PMU = 0,
	SND_SUNXI_RGLT_AUDIO,
	SND_SUNXI_RGLT_USER,
};

struct snd_sunxi_rglt_unit {
	enum SND_SUNXI_RGLT_MODE mode;
	u32 vcc_vol;
	struct regulator *vcc;
};

struct snd_sunxi_rglt {
	u32 unit_cnt;
	struct snd_sunxi_rglt_unit *unit;
	void *priv;
};

struct snd_sunxi_rglt *snd_sunxi_regulator_init(struct platform_device *pdev);
void snd_sunxi_regulator_exit(struct snd_sunxi_rglt *rglt);
int snd_sunxi_regulator_enable(struct snd_sunxi_rglt *rglt);
void snd_sunxi_regulator_disable(struct snd_sunxi_rglt *rglt);

/******* pa config *******/
/* board.dts format
 * &xxx {
 *	pa-pin-max	= <n>;		n: pa pin cnt
 *	pa-pin-0	= <&pio xxx>;
 *	pa-cfg-mode-0	= <0>;		0: level contrl; 1: pulse contrl; 0xFF: user contrl
 *	pa-pin-level-0	= <1>;		turn on pa, 0: low level; 1: high level
 *	pa-pin-msleep-0	= <n>;		Sleep before turn on pa
 *
 *	pa-pin-1	= <&pio xxx>;
 *	pa-cfg-mode-1	= <1>;		0: level contrl; 1: pulse contrl; 0xFF: user contrl
 *	pa-pin-level-1	= <1>;		turn on pa, 0: low level; 1: high level
 *	pa-pin-msleep-1	= <n>;		Sleep before turn on pa
 * 	pa-pin-duty-1		= <n>;	pulse Width(us)
 * 	pa-pin-period-1		= <n>;	pulse period(us)
 *	pa-pin-polarity-1	= <1>;	pulse polarity
 * 	pa-pin-periodcnt-1	= <n>;	number of pulses
 *	...
 * };
 */
enum SND_SUNXI_PACFG_MODE {
	SND_SUNXI_PA_CFG_NULL = -1,
	SND_SUNXI_PA_CFG_LEVEL = 0,
	SND_SUNXI_PA_CFG_PULSE,
	SND_SUNXI_PA_CFG_USER,
	SND_SUNXI_PA_CFG_CNT,
};

struct pacfg_level_trig {
	u32 pin;
	u32 msleep;
	bool level;
};

struct pacfg_pulse_trig {
	u32 pin;
	u32 msleep;
	bool level;

	u32 duty_us;
	u32 period_us;
	u32 period_cnt;
	bool polarity;
};

struct pacfg_user_trig {
	u32 user_control;
};

struct snd_sunxi_pacfg {
	struct pacfg_level_trig *level_trig;
	struct pacfg_pulse_trig *pulse_trig;
	struct pacfg_user_trig *user_trig;
	enum SND_SUNXI_PACFG_MODE mode;
	bool used;
	u32 index;
	struct platform_device *pdev;
	struct work_struct pa_en_work;
};

struct snd_sunxi_pacfg *snd_sunxi_pa_pin_init(struct platform_device *pdev, u32 *pa_pin_max);
void snd_sunxi_pa_pin_exit(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max);
int snd_sunxi_pa_pin_probe(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max,
			   struct snd_soc_component *component);
void snd_sunxi_pa_pin_remove(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max);
int snd_sunxi_pa_pin_enable(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max);
void snd_sunxi_pa_pin_disable(struct snd_sunxi_pacfg *pa_cfg, u32 pa_pin_max);

/******* hdmi format config *******/
#define	SUNXI_DAI_I2S_TYPE	0
#define	SUNXI_DAI_HDMI_TYPE	1

enum HDMI_FORMAT {
	HDMI_FMT_NULL = 0,
	HDMI_FMT_PCM = 1,
	HDMI_FMT_AC3,
	HDMI_FMT_MPEG1,
	HDMI_FMT_MP3,
	HDMI_FMT_MPEG2,
	HDMI_FMT_AAC,
	HDMI_FMT_DTS,
	HDMI_FMT_ATRAC,
	HDMI_FMT_ONE_BIT_AUDIO,
	HDMI_FMT_DOLBY_DIGITAL_PLUS,
	HDMI_FMT_DTS_HD,
	HDMI_FMT_MAT,
	HDMI_FMT_DST,
	HDMI_FMT_WMAPRO,
};

enum HDMI_FORMAT snd_sunxi_hdmi_get_fmt(void);
int snd_sunxi_hdmi_set_fmt(int hdmi_fmt);
int snd_sunxi_hdmi_get_dai_type(struct device_node *np, unsigned int *dai_type);

/******* PCM uncommon format config *******/
struct snd_sunxi_ucfmt;

typedef int (*ucfmt_callback_f)(struct snd_sunxi_ucfmt *ucfmt, struct snd_soc_dai *dai);

struct snd_sunxi_ucfmt_cb {
	const char *dai_name;
	ucfmt_callback_f callback;
};

struct snd_sunxi_ucfmt {
	struct list_head list;

	unsigned int fmt;

	u32 data_late;
	bool tx_lsb_first;
	bool rx_lsb_first;

	struct snd_sunxi_ucfmt_cb cpu_dai_cb;
	struct snd_sunxi_ucfmt_cb *codec_dai_cbs;
	u32 num_codecs;
};

int snd_sunxi_ucfmt_register_cb(ucfmt_callback_f callback, const char *dai_name);
void snd_sunxi_ucfmt_unregister_cb(const char *dai_name);
int snd_sunxi_ucfmt_probe(struct snd_soc_card *card, struct snd_sunxi_ucfmt **ucfmt);
void snd_sunxi_ucfmt_remove(struct snd_sunxi_ucfmt *ucfmt);

/******* sysfs dump *******/
struct snd_sunxi_dump {
	struct list_head list;

	void (*dump_version)(void *priv, char *buf, size_t *count);
	void (*dump_help)(void *priv, char *buf, size_t *count);
	int (*dump_show)(void *priv, char *buf, size_t *count);
	int (*dump_store)(void *priv, const char *buf, size_t count);

	const char *name;
	void *priv;
	bool use;
};

int snd_sunxi_dump_register(struct snd_sunxi_dump *dump);
void snd_sunxi_dump_unregister(struct snd_sunxi_dump *dump);

/* update jack_statet to module parameter */
void snd_sunxi_jack_state_upto_modparam(enum snd_jack_types type);

#endif /* __SND_SUNXI_COMMON_H */
