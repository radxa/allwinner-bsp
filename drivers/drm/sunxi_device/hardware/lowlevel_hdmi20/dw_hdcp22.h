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

#ifndef _DW_HDCP22_H
#define _DW_HDCP22_H

#include <linux/workqueue.h>

int dw_esm_init(void);

void dw_esm_exit(void);
int dw_esm_open(void);
void dw_esm_close(void);
void dw_esm_disable(void);

int dw_esm_status_check_and_handle(void);
ssize_t dw_esm_dump(char *buf);

#endif /* _DW_HDCP22_H */
