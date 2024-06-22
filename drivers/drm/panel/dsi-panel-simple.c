// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic dsi panel driver
 *
 * Copyright (C) 2023 Allwinner.
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_edid.h>
#include <drm/drm_property.h>
#include <linux/backlight.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_crtc.h>

#define POWER_MAX 3
#define GPIO_MAX  3

struct panel_cmd_header {
	u8 data_type;
	u8 delay;
	u8 payload_length;
} __packed;

struct panel_cmd_desc {
	struct panel_cmd_header header;
	u8 *payload;
};

struct panel_cmd_seq {
	struct panel_cmd_desc *cmds;
	unsigned int cmd_cnt;
};

struct panel_desc {

	struct videomode video_mode;
	struct {
		unsigned int width;
		unsigned int height;
	} size;

	 struct {
//		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
//		unsigned int unprepare;
		unsigned int reset;
//		unsigned int init;
	} delay;

	struct panel_cmd_seq *init_seq;
	struct panel_cmd_seq *exit_seq;
};

struct panel_dsi {
	struct drm_panel panel;
	struct device *dev;
	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;
	unsigned int bus_format;

	struct regulator *supply[POWER_MAX];
	struct gpio_desc *enable_gpio[GPIO_MAX];
	struct gpio_desc *reset_gpio;

	enum drm_panel_orientation orientation;
};

struct panel_desc_dsi {
	struct panel_desc desc;
	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
};
/*
static const struct drm_display_mode auo_b080uan01_mode = {
	.clock = 154500,
	.hdisplay = 1200,
	.hsync_start = 1200 + 62,
	.hsync_end = 1200 + 62 + 4,
	.htotal = 1200 + 62 + 4 + 62,
	.vdisplay = 1920,
	.vsync_start = 1920 + 9,
	.vsync_end = 1920 + 9 + 2,
	.vtotal = 1920 + 9 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc_dsi auo_b080uan01 = {
	.desc = {
		.modes = &auo_b080uan01_mode,
		.bpc = 8,
		.size = {
			.width = 108,
			.height = 272,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};
*/
static inline struct panel_dsi *to_panel_dsi(struct drm_panel *panel)
{
	return container_of(panel, struct panel_dsi, panel);
}

static void panel_dsi_sleep(unsigned int msec)
{
	if (msec > 20)
		msleep(msec);
	else
		usleep_range(msec * 1000, (msec + 1) * 1000);
}

static int panel_dsi_cmd_seq(struct panel_dsi *dsi_panel,
		struct panel_cmd_seq *seq)
{
	struct device *dev = dsi_panel->panel.dev;
	struct mipi_dsi_device *dsi = dsi_panel->dsi;
	unsigned int i;
	int err;

	if (!seq)
		return -EINVAL;

	for (i = 0; i < seq->cmd_cnt; i++) {
		struct panel_cmd_desc *cmd = &seq->cmds[i];

		switch (cmd->header.data_type) {
		case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		case MIPI_DSI_GENERIC_LONG_WRITE:
			err = mipi_dsi_generic_write(dsi, cmd->payload,
						cmd->header.payload_length);
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_LONG_WRITE:
			err = mipi_dsi_dcs_write_buffer(dsi, cmd->payload,
					cmd->header.payload_length);
			break;
		default:
			return -EINVAL;
		}

		if (err < 0)
			dev_err(dev, "failed to write dcs cmd: %d\n", err);

		if (cmd->header.delay)
			panel_dsi_sleep(cmd->header.delay);
	}

	return 0;
}

static int panel_dsi_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct panel_dsi *dsi_panel = to_panel_dsi(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;

	drm_display_mode_from_videomode(&dsi_panel->desc->video_mode, mode);
	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
/*
	connector->display_info.width_mm = dsi_panel->desc->size.width;
	connector->display_info.height_mm = dsi_panel->desc->size.height;
	drm_display_info_set_bus_formats(&connector->display_info,
					&dsi_panel->desc->bus_format, 1);
	connector->display_info.bus_flags =
		dsi_panel->data_mirror ? DRM_BUS_FLAG_DATA_LSB_TO_MSB :
				DRM_BUS_FLAG_DATA_MSB_TO_LSB;
*/
	drm_connector_set_panel_orientation(connector, dsi_panel->orientation);

	return 1;
}

static int panel_dsi_disable(struct drm_panel *panel)
{
	if (panel->backlight)
		backlight_disable(panel->backlight);

	return 0;
}

static int panel_dsi_unprepare(struct drm_panel *panel)
{
	struct panel_dsi *dsi_panel = to_panel_dsi(panel);
	int i;

	if (dsi_panel->desc->exit_seq)
		if (dsi_panel->dsi)
			panel_dsi_cmd_seq(dsi_panel, dsi_panel->desc->exit_seq);

	for (i = GPIO_MAX; i > 0; i--) {
		if (dsi_panel->enable_gpio[i - 1]) {
			gpiod_set_value_cansleep(dsi_panel->enable_gpio[i - 1], 0);
			if (dsi_panel->desc->delay.enable)
				panel_dsi_sleep(dsi_panel->desc->delay.disable);
		}
	}

	if (dsi_panel->reset_gpio)
		gpiod_set_value_cansleep(dsi_panel->reset_gpio, 0);
	if (dsi_panel->desc->delay.reset)
		panel_dsi_sleep(dsi_panel->desc->delay.reset);

	for (i = POWER_MAX; i > 0; i--) {
		if (dsi_panel->supply[i - 1]) {
			regulator_disable(dsi_panel->supply[i - 1]);
			msleep(10);
		}
	}

	return 0;
}

static int panel_dsi_prepare(struct drm_panel *panel)
{
	struct panel_dsi *dsi_panel = to_panel_dsi(panel);
	int err, i;

	for (i = 0; i < POWER_MAX; i++) {
		if (dsi_panel->supply[i]) {
			err = regulator_enable(dsi_panel->supply[i]);
			if (err < 0) {
				dev_err(dsi_panel->dev, "failed to enable supply%d: %d\n",
					i, err);
				return err;
			}
			msleep(10);
		}
	}

	for (i = 0; i < GPIO_MAX; i++) {
		if (dsi_panel->enable_gpio[i]) {
			gpiod_set_value_cansleep(dsi_panel->enable_gpio[i], 1);

			if (dsi_panel->desc->delay.enable)
				panel_dsi_sleep(dsi_panel->desc->delay.enable);
		}
	}

	if (dsi_panel->reset_gpio)
		gpiod_set_value_cansleep(dsi_panel->reset_gpio, 1);
	if (dsi_panel->desc->delay.reset)
		panel_dsi_sleep(dsi_panel->desc->delay.reset);
/*
	for (i = 0; i < dsi_panel->desc->reset_num; i++) {
		if (dsi_panel->reset_gpio)
			gpiod_set_value_cansleep(dsi_panel->reset_gpio, 1);
		if (dsi_panel->desc->delay.reset)
			panel_dsi_sleep(dsi_panel->desc->delay.reset);
	}
*/
	if (dsi_panel->desc->init_seq)
		if (dsi_panel->dsi)
			panel_dsi_cmd_seq(dsi_panel, dsi_panel->desc->init_seq);

	return 0;
}

static int panel_dsi_enable(struct drm_panel *panel)
{
	if (panel->backlight)
		backlight_enable(panel->backlight);

	return 0;
}

static const struct drm_panel_funcs panel_dsi_funcs = {
	.unprepare = panel_dsi_unprepare,
	.disable = panel_dsi_disable,
	.prepare = panel_dsi_prepare,
	.enable = panel_dsi_enable,
	.get_modes = panel_dsi_get_modes,
//	.get_timings = panel_dsi_get_timings,
};

static int panel_dsi_parse_dt(struct panel_dsi *dsi_panel)
{
	struct device_node *np = dsi_panel->dev->of_node;
	char *power_name = NULL;
	char *gpio_name = NULL;
	int ret, i;

	ret = of_drm_get_panel_orientation(np, &dsi_panel->orientation);
	if (ret < 0) {
		dsi_panel->orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;
	}

	for (i = 0; i < POWER_MAX; i++) {
		power_name = kasprintf(GFP_KERNEL, "power%d", i);
		dsi_panel->supply[i] = devm_regulator_get_optional(dsi_panel->dev, power_name);
		if (IS_ERR(dsi_panel->supply[i])) {
			ret = PTR_ERR(dsi_panel->supply[i]);

			if (ret != -ENODEV) {
				if (ret != -EPROBE_DEFER)
					dev_err(dsi_panel->dev,
						"failed to request regulator(%s): %d\n",
						power_name, ret);
				return ret;
			}

			dsi_panel->supply[i] = NULL;
		}
	}

	/* Get GPIOs and backlight controller. */
	for (i = 0; i < GPIO_MAX; i++) {
		gpio_name = kasprintf(GFP_KERNEL, "enable%d", i);
		dsi_panel->enable_gpio[i] =
			devm_gpiod_get_optional(dsi_panel->dev, gpio_name, GPIOD_OUT_HIGH);
		if (IS_ERR(dsi_panel->enable_gpio[i])) {
			ret = PTR_ERR(dsi_panel->enable_gpio[i]);
			dev_err(dsi_panel->dev, "failed to request %s GPIO: %d\n", gpio_name,
				ret);
			return ret;
		}
	}

	dsi_panel->reset_gpio =
		devm_gpiod_get_optional(dsi_panel->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dsi_panel->reset_gpio)) {
		ret = PTR_ERR(dsi_panel->reset_gpio);
		dev_err(dsi_panel->dev, "failed to request %s GPIO: %d\n", "reset",
			ret);
		return ret;
	}

	return 0;
}
static int panel_simple_parse_cmd_seq(struct device *dev,
				const u8 *data, int length,
				struct panel_cmd_seq *seq)
{
	struct panel_cmd_header *header;
	struct panel_cmd_desc *desc;
	char *buf, *d;
	unsigned int i, cnt, len;

	if (!seq)
		return -EINVAL;

	buf = devm_kmemdup(dev, data, length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	d = buf;
	len = length;
	cnt = 0;
	while (len > sizeof(*header)) {
		header = (struct panel_cmd_header *)d;

		d += sizeof(*header);
		len -= sizeof(*header);

		if (header->payload_length > len)
			return -EINVAL;

		d += header->payload_length;
		len -= header->payload_length;
		cnt++;
	}

	if (len)
		return -EINVAL;

	seq->cmd_cnt = cnt;
	seq->cmds = devm_kcalloc(dev, cnt, sizeof(*desc), GFP_KERNEL);
	if (!seq->cmds)
		return -ENOMEM;

	d = buf;
	len = length;
	for (i = 0; i < cnt; i++) {
		header = (struct panel_cmd_header *)d;
		len -= sizeof(*header);
		d += sizeof(*header);

		desc = &seq->cmds[i];
		desc->header = *header;
		desc->payload = d;

		d += header->payload_length;
		len -= header->payload_length;
	}

	return 0;
}

static int panel_of_get_desc_data(struct device *dev,
					struct panel_desc *desc)
{
	struct device_node *np = dev->of_node;
	struct display_timings *disp = NULL;
	struct display_timing *timing = NULL;
	const void *data;
	int len;
	int err;

	disp = of_get_display_timings(np);
	if (!disp) {
		dev_err(dev, "%pOF: problems parsing panel-timin\n",
			np);
		return -ENODEV;
	}
	timing = display_timings_get(disp, disp->native_mode);
	if (!timing) {
		dev_err(dev, "%pOF: problems parsing panel-timin\n",
			np);
		return -ENODEV;
	}
	videomode_from_timing(timing, &desc->video_mode);

	of_property_read_u32(np, "width-mm", &desc->size.width);
	of_property_read_u32(np, "height-mm", &desc->size.height);

/*	mode = devm_kzalloc(dev, sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return -ENOMEM;
	err = of_get_drm_display_mode(np, mode, &bus_flags, OF_USE_NATIVE_MODE);
	if (!err) {
		desc->modes = mode;
		desc->num_modes = 1;
		desc->bus_flags = bus_flags;

		of_property_read_u32(np, "bpc", &desc->bpc);
		of_property_read_u32(np, "bus-format", &desc->bus_format);
		of_property_read_u32(np, "width-mm", &desc->size.width);
		of_property_read_u32(np, "height-mm", &desc->size.height);
	}
*/
//	of_property_read_u32(np, "prepare-delay-ms", &desc->delay.prepare);
	of_property_read_u32(np, "enable-delay-ms", &desc->delay.enable);
	of_property_read_u32(np, "disable-delay-ms", &desc->delay.disable);
//      of_property_read_u32(np, "unprepare-delay-ms", &desc->delay.unprepare);
	of_property_read_u32(np, "reset-delay-ms", &desc->delay.reset);
//	of_property_read_u32(np, "init-delay-ms", &desc->delay.init);


	data = of_get_property(np, "panel-init-sequence", &len);
	if (data) {
		desc->init_seq = devm_kzalloc(dev, sizeof(*desc->init_seq),
					GFP_KERNEL);
		if (!desc->init_seq)
			return -ENOMEM;

		err = panel_simple_parse_cmd_seq(dev, data, len,
						 desc->init_seq);
		if (err) {
			dev_err(dev, "failed to parse init sequence\n");
			return err;
		}
	}

	data = of_get_property(np, "panel-exit-sequence", &len);
	if (data) {
		desc->exit_seq = devm_kzalloc(dev, sizeof(*desc->exit_seq),
					GFP_KERNEL);
		if (!desc->exit_seq)
			return -ENOMEM;

		err = panel_simple_parse_cmd_seq(dev, data, len,
						desc->exit_seq);
		if (err) {
			dev_err(dev, "failed to parse exit sequence\n");
			return err;
		}
	}

	return 0;
}

static int panel_dsi_of_get_desc_data(struct device *dev,
					struct panel_desc_dsi *desc)
{
	struct device_node *np = dev->of_node;
	u32 val;
	int ret;

	ret = panel_of_get_desc_data(dev, &desc->desc);
	if (ret)
		return ret;

	if (!of_property_read_u32(np, "dsi,flags", &val))
		desc->flags = val;
	if (!of_property_read_u32(np, "dsi,format", &val))
		desc->format = val;
	if (!of_property_read_u32(np, "dsi,lanes", &val))
		desc->lanes = val;

	return 0;
}

static const struct of_device_id dsi_of_match[] = {
	{
		.compatible = "panel-dsi",
		.data = NULL,
	},
/*	{
		.compatible = "auo,b080uan01",
		.data = &auo_b080uan01
	}, */
	{ /* Sentinel */ },
};
static int panel_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct panel_dsi *dsi_panel;
	struct device *dev = &dsi->dev;
	const struct panel_desc_dsi *desc;
	struct panel_desc_dsi *d;
	const struct of_device_id *id;
	int ret;

	DRM_WARN("[DSI-PANEL] panel_dsi_probe start\n");
	dsi_panel = devm_kzalloc(dev, sizeof(*dsi_panel), GFP_KERNEL);
	if (!dsi_panel)
		return -ENOMEM;

	id = of_match_node(dsi_of_match, dsi->dev.of_node);

	if (!id->data) {
		d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
		if (!d)
		return -ENOMEM;

		ret = panel_dsi_of_get_desc_data(dev, d);
		if (ret) {
			dev_err(dev, "failed to get desc data: %d\n", ret);
			return ret;
		}
	}

	desc = id->data ? id->data : d;
	dsi_panel->dev = dev;
	dsi_panel->desc = &desc->desc;
	dsi_panel->dsi = dsi;

	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	ret = panel_dsi_parse_dt(dsi_panel);
	if (ret < 0)
		return ret;

	/* Register the panel. */
	drm_panel_init(&dsi_panel->panel, dev, &panel_dsi_funcs,
			DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&dsi_panel->panel);
	if (ret)
		return ret;

	drm_panel_add(&dsi_panel->panel);

	dev_set_drvdata(dev, dsi_panel);
	mipi_dsi_attach(dsi);
	DRM_WARN("[DSI-PANEL] panel_dsi_probe finish\n");

	return 0;
}

static int panel_simple_remove(struct device *dev)
{
	struct panel_dsi *panel = dev_get_drvdata(dev);

	drm_panel_remove(&panel->panel);

	panel_dsi_disable(&panel->panel);
	panel_dsi_unprepare(&panel->panel);

	return 0;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int panel_dsi_remove(struct mipi_dsi_device *dsi)
#else
static void panel_dsi_remove(struct mipi_dsi_device *dsi)
#endif
{
/*	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);
*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return panel_simple_remove(&dsi->dev);
#else
	panel_simple_remove(&dsi->dev);
#endif
}

MODULE_DEVICE_TABLE(of, dsi_of_match);

static struct mipi_dsi_driver panel_dsi_driver = {
	.probe = panel_dsi_probe,
	.remove = panel_dsi_remove,
	.driver = {
		.name = "panel-dsi",
		.of_match_table = dsi_of_match,
	},
};

static int __init panel_dsi_init(void)
{
	int err;

	DRM_WARN("[DSI-PANEL] panel_dsi_init start\n");
	err = mipi_dsi_driver_register(&panel_dsi_driver);
	if (err < 0) {
		DRM_WARN("[DSI-PANEL] dsi driver regsiter fail\n");
		return err;
	}
	DRM_WARN("[DSI-PANEL] dsi driver regsiter finsh\n");
	return 0;
}
static void __exit panel_dsi_exit(void)
{
	mipi_dsi_driver_unregister(&panel_dsi_driver);
}

module_init(panel_dsi_init);
module_exit(panel_dsi_exit);
//module_mipi_dsi_driver(panel_dsi_driver);

MODULE_AUTHOR("xiaozhineng <xiaozhineng@allwinnertech.com>");
MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("dsi Panel Driver");
MODULE_LICENSE("GPL");
