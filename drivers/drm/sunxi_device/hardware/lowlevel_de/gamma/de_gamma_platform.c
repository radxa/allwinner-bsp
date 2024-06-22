/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2023 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "de_gamma_platform.h"

struct de_version_gamma {
	unsigned int version;
	unsigned int gamma_cnt;
	struct de_gamma_dsc *gammas;
};

static struct de_gamma_dsc de210_gammas[] = {
	{
		.id = 0,
		.gamma_lut_len = 256,
		.support_ctc = true,
		.support_cm = true,
		.support_demo_skin = true,
	},
};

static struct de_version_gamma de210 = {
	.version = 0x210,
	.gamma_cnt = ARRAY_SIZE(de210_gammas),
	.gammas = de210_gammas,
};

static struct de_gamma_dsc de352_gammas[] = {
	{
		.id = 0,
		.gamma_lut_len = 1024,
		.support_ctc = true,
		.support_cm = true,
		.support_demo_skin = true,
	},
	{
		.id = 1,
		.gamma_lut_len = 1024,
		.support_ctc = true,
		.support_cm = true,
		.support_demo_skin = true,
	},
};

static struct de_version_gamma de352 = {
	.version = 0x352,
	.gamma_cnt = ARRAY_SIZE(de352_gammas),
	.gammas = de352_gammas,
};

static struct de_version_gamma *de_version[] = {
	&de210, &de352
};

const struct de_gamma_dsc *get_gamma_dsc(struct module_create_info *info)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(de_version); i++) {
		if (de_version[i]->version == info->de_version) {
			for (j = 0; j < de_version[i]->gamma_cnt; j++) {
				if (de_version[i]->gammas[j].id == info->id)
					return &de_version[i]->gammas[j];
			}
		}
	}
	return NULL;
}

