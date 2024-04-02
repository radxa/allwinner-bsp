/* SPDX-License-Identifier: GPL-2.0-or-later */
/*******************************************************************************
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 ******************************************************************************/

#ifndef _SUNXI_DRM_HDMI_H_
#define _SUNXI_DRM_HDMI_H_

/*****************************************************
 * sunxi hdmi public function
 ****************************************************/
#if IS_ENABLED(CONFIG_AW_DRM_HDMI_TX)
int sunxi_hdmi_drm_create(struct tcon_device *tcon);
#else
int sunxi_hdmi_drm_create(struct tcon_device *tcon)
{
	return 0;
}
#endif

static inline void hdmi_log(const char *fmt, ...)
{
}

#endif /* _SUNXI_DRM_HDMI_H_ */
