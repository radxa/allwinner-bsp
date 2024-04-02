// SPDX-License-Identifier: GPL-2.0-or-later
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

#include "snd_sunxi_adapter.h"
#include "snd_sunxi_log.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
struct snd_soc_dai *sunxi_adpt_rtd_cpu_dai(struct snd_soc_pcm_runtime *rtd)
{
	return asoc_rtd_to_cpu(rtd, 0);
}

void sunxi_adpt_rt_delay(struct snd_pcm_runtime *runtime, struct dma_tx_state state)
{
	runtime->delay = bytes_to_frames(runtime, (state.in_flight_bytes >> 1));
}
#else
struct snd_soc_dai *sunxi_adpt_rtd_cpu_dai(struct snd_soc_pcm_runtime *rtd)
{
	return rtd->cpu_dai;
}

void sunxi_adpt_rt_delay(struct snd_pcm_runtime *runtime, struct dma_tx_state state)
{
	return;
}
#endif
EXPORT_SYMBOL_GPL(sunxi_adpt_rtd_cpu_dai);
EXPORT_SYMBOL_GPL(sunxi_adpt_rt_delay);

int snd_sunxi_card_jack_new(struct snd_soc_card *card, const char *id, int type,
			    struct snd_soc_jack *jack)
{
	int ret;

	mutex_init(&jack->mutex);
	jack->card = card;
	INIT_LIST_HEAD(&jack->pins);
	INIT_LIST_HEAD(&jack->jack_zones);
	BLOCKING_INIT_NOTIFIER_HEAD(&jack->notifier);

	ret = snd_jack_new(card->snd_card, id, type, &jack->jack, false, false);
	switch (ret) {
	case -EPROBE_DEFER:
	case -ENOTSUPP:
	case 0:
		break;
	default:
		SND_LOGDEV_ERR(card->dev, "ASoC: error on %s: %d\n", card->name, ret);
	}

	return ret;
}
