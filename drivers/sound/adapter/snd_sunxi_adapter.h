/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner's ALSA SoC Audio driver
 *
 * Copyright (c) 2021, <lijingpsw@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/version.h>
#include <linux/dmaengine.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#ifndef __SND_SUNXI_ADAPTER_H
#define __SND_SUNXI_ADAPTER_H

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#define sunxi_adpt_rtd_codec_dai(rtd, i, dai)	for_each_rtd_codec_dais(rtd, i, dai)
#else
#define sunxi_adpt_rtd_codec_dai(rtd, i, dai)	for_each_rtd_codec_dai(rtd, i, dai)
#endif

struct snd_soc_dai *sunxi_adpt_rtd_cpu_dai(struct snd_soc_pcm_runtime *rtd);
int snd_sunxi_card_jack_new(struct snd_soc_card *card, const char *id, int type,
			    struct snd_soc_jack *jack);
void sunxi_adpt_rt_delay(struct snd_pcm_runtime *runtime, struct dma_tx_state state);

#endif /* __SND_SUNXI_ADAPTER_H */
