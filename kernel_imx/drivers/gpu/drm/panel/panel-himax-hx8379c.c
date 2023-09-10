/*
 * Himax HX8379C panel driver.
 *
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver is based on Samsung s6e8aa0 panel driver.
 * This driver is based on Himax hx8369a panel driver.
 */

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#define WRDISBV		0x51
#define WRCTRLD		0x53
#define WRCABC		0x55
#define SETPOWER	0xb1
#define SETDISP		0xb2
#define SETCYC		0xb4
#define SETVCOM		0xb6
#define SETEXTC		0xb9
#define SETMIPI		0xba
#define SETPANEL	0xcc
#define SETGAMMA	0xe0

#define SETTCON		0xc7
#define SETOFFSET	0xd2
#define SETCE		0xe4
#define SETGIP_0	0xd3
#define SETGIP_1	0xd5
#define SETGIP_2	0xd6

static const u32 himax_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

struct hx8379c_panel_desc {
	const struct drm_display_mode *mode;

	/* ms */
	unsigned int power_on_delay;
	unsigned int reset_delay;

	unsigned int dsi_lanes;
};

struct hx8379c {
	struct device *dev;
	struct drm_panel panel;

	const struct hx8379c_panel_desc *pd;

	struct regulator_bulk_data supplies[5];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bs_gpio[4];
	u8 res_sel;
	struct videomode vm;
	bool prepeared;
	bool enabled;
};

static void hx8379c_dsi_set_maximum_return_packet_size(struct hx8379c *ctx,
						       u16 size);

static inline struct hx8379c *panel_to_hx8379c(struct drm_panel *panel)
{
	return container_of(panel, struct hx8379c, panel);
}

static void hx8379c_dcs_write(struct hx8379c *ctx, const char *func,
			      const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	//ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0)
		dev_err(ctx->dev, "%s failed: %ld\n", func, ret);
	msleep(20);
}

static __maybe_unused int hx8379c_generic_read(struct hx8379c *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_generic_read(dsi, &cmd,1, data, len);
	if (ret < 0) 
		dev_err(ctx->dev, "error %d reading generic seq(%#x)\n", ret, cmd);
	return ret;
}

static __maybe_unused int hx8379c_dcs_read(struct hx8379c *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) 
		dev_err(ctx->dev, "error %d reading dcs seq(%#x)\n", ret, cmd);
	return ret;
}

static __maybe_unused int hx8379c_dcs_dump(struct hx8379c *ctx, u8 cmd,size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;
	int i;
	u8 data[128];
	for (i = 0; i < 128; i++) {
		data[i] = 0;
	}
	hx8379c_dsi_set_maximum_return_packet_size(ctx, len);
	ret = mipi_dsi_generic_read(dsi, &cmd,1, data, len);
	if (ret < 0) 
		dev_err(ctx->dev, "error %d reading dcs seq(%#x)\n", ret, cmd);
	pr_notice("%s read %d bytes \n",__func__,ret);
	for (i = 0; i < ret; i++) {
		printk("%x,",data[i]);
		if((i%7) == 0)
			printk("\n");
	}
	printk("\n");

	return ret;
}

#define hx8379c_dcs_write_seq_static(ctx, seq...) \
({ \
	static const u8 d[] = { seq }; \
	hx8379c_dcs_write(ctx, __func__, d, ARRAY_SIZE(d)); \
})

static void hx8379c_dsi_set_display_related_register(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETDISP, 0x80, 0x3c, 0x0a,
		0x03, 0x70, 0x50, 0x11, 0x42, 0x1d);
}

static void hx8379c_dsi_set_display_waveform_cycle(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETCYC, 0x02, 0x7c, 0x02, 0x7c, 0x02,
		0x7c, 0x22, 0x86, 0x23, 0x86);
}

static void hx8379c_dsi_set_tcon(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETTCON, 0x00, 0x00, 0x00, 0xC0);
}

static void hx8379c_dsi_set_offset(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETOFFSET, 0x77);
}

static void hx8379c_dsi_set_color_enhancement(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETCE, 0x00,0x01);
}

static void hx8379c_dsi_set_gip_0(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETGIP_0, 0x00, 0x07, 0x00, 0x00, 0x00,
				0x08, 0x08, 0x32, 0x10, 0x01, 0x00, 0x01, 0x03,
				0x72, 0x03, 0x72, 0x00, 0x08, 0x00, 0x08, 0x33,
				0x33, 0x05, 0x05, 0x37, 0x05, 0x05, 0x37, 0x08,
				0x00, 0x00, 0x00, 0x0a, 0x00, 0x01, 0x01, 0x0f);
	
}
static void hx8379c_dsi_set_gip_1(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETGIP_1, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
		0x01, 0x00, 0x18, 0x18, 0x21, 0x20, 0x18, 0x18,
		0x19, 0x19, 0x23, 0x22, 0x38, 0x38, 0x78, 0x78,
		0x18, 0x18, 0x18, 0x18, 0x00, 0x00);
}

static void hx8379c_dsi_set_gip_2(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETGIP_2, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x06, 0x07, 0x18, 0x18, 0x22, 0x23, 0x19, 0x19,
		0x18, 0x18, 0x20, 0x21, 0x38, 0x38, 0x38, 0x38,
		0x18, 0x18, 0x18, 0x18);
}

static void hx8379c_dsi_set_power(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETPOWER, 0x44, 0x18, 0x18, 0x31, 0x51,
				0x50, 0xd0, 0xd8, 0x58, 0x80, 0x38, 0x38, 0xf8,
				0x33, 0x32, 0x22);
}

static void hx8379c_dsi_set_vcom_voltage(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETVCOM, 0x5e, 0x5e);
}

static void hx8379c_dsi_set_panel(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETPANEL, 0x02);
}

static void hx8379c_dsi_set_gamma_curve(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETGAMMA, 0x00, 0x01, 0x04, 0x20,
		0x24, 0x3f, 0x11, 0x33, 0x09, 0x0a, 0x0c, 0x17,
		0x0f, 0x12, 0x15, 0x13, 0x14, 0x0a, 0x15, 0x16,
		0x18, 0x00, 0x01, 0x04, 0x20, 0x24, 0x3f, 0x11,
		0x33, 0x09, 0x0B, 0x0c, 0x17, 0x0e, 0x11, 0x14,
		0x13, 0x14, 0x0a, 0x15, 0x16, 0x18);
}

static void hx8379c_dsi_panel_init(struct hx8379c *ctx)
{
	hx8379c_dsi_set_power(ctx);
	hx8379c_dsi_set_display_related_register(ctx);
	hx8379c_dsi_set_display_waveform_cycle(ctx);
	hx8379c_dsi_set_tcon(ctx);
	hx8379c_dsi_set_panel(ctx);
	hx8379c_dsi_set_offset(ctx);
	hx8379c_dsi_set_gip_0(ctx);
	hx8379c_dsi_set_gip_1(ctx);
	hx8379c_dsi_set_gip_2(ctx);
	hx8379c_dsi_set_gamma_curve(ctx);
	hx8379c_dsi_set_color_enhancement(ctx);
	hx8379c_dsi_set_vcom_voltage(ctx);
}

static void hx8379c_dsi_set_extension_command(struct hx8379c *ctx)
{
	hx8379c_dcs_write_seq_static(ctx, SETEXTC, 0xff, 0x83, 0x79);
	usleep_range(10000, 11000);
}

static void hx8379c_dsi_set_maximum_return_packet_size(struct hx8379c *ctx,
						       u16 size)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_set_maximum_return_packet_size(dsi, size);
	if (ret < 0)
		dev_err(ctx->dev,
			"error %d setting maximum return packet size to %d\n",
			ret, size);
}

static int hx8379c_dsi_set_sequence(struct hx8379c *ctx)
{
	hx8379c_dsi_set_extension_command(ctx);
	hx8379c_dsi_panel_init(ctx);

	return 0;
}

static int hx8379c_dsi_disable(struct drm_panel *panel)
{
	struct hx8379c *ctx = panel_to_hx8379c(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = &dsi->dev;
	int ret;

	if (!ctx->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		DRM_DEV_ERROR(dev, "Fail to set display off\n");
		return ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		DRM_DEV_ERROR(dev, "Fail to enter sleep mode\n");
		return ret;
	}

	usleep_range(10000, 15000);

	ctx->enabled = false;

	return 0;
}

static int hx8379c_dsi_unprepare(struct drm_panel *panel)
{
	struct hx8379c *ctx = panel_to_hx8379c(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = &dsi->dev;

	if (!ctx->prepeared)
		return 0;

	if (ctx->enabled) {
		DRM_DEV_ERROR(dev, "Fail uprepare panel still enabled\n");
		return -EPERM;
	}

	/* reset panel */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(15000, 17000);
	gpiod_set_value(ctx->reset_gpio, 1);

	ctx->prepeared = false;

	return 0;
}

static int hx8379c_dsi_prepare(struct drm_panel *panel)
{
	struct hx8379c *ctx = panel_to_hx8379c(panel);

	if (ctx->prepeared)
		return 0;

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(20000, 25000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(20000, 25000);

	msleep(120);
	ctx->prepeared = true;

	return 0;
}

static int hx8379c_dsi_enable(struct drm_panel *panel)
{
	struct hx8379c *ctx = panel_to_hx8379c(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = &dsi->dev;
	int ret;

	dev_info(ctx->dev, "hx8379c enable\n");

	if (ctx->enabled)
		return 0;

	if (!ctx->prepeared) {
		DRM_DEV_ERROR(dev, "Fail panel not prepared\n");
		return -EPERM;
	}

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	hx8379c_dsi_set_sequence(ctx);
	/* Set tear ON */
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Fail to set tear on\n");
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(20000, 25000);

	ctx->enabled = true;

	return 0;
}

static int hx8379c_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct hx8379c *ctx = panel_to_hx8379c(panel);
	struct drm_display_mode *mode;
	u32 *bus_flags = &connector->display_info.bus_flags;
	int ret;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		pr_err("Failed to create display mode!\n");
		return 0;
	}
	drm_display_mode_from_videomode(&ctx->vm, mode);
	mode->width_mm = 40;
	mode->height_mm = 67;
	mode->vrefresh = 60;//
	connector->display_info.bpc = 8;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	if (ctx->vm.flags & DISPLAY_FLAGS_DE_HIGH)
		*bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	if (ctx->vm.flags & DISPLAY_FLAGS_DE_LOW)
		*bus_flags |= DRM_BUS_FLAG_DE_LOW;
	if (ctx->vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_NEGEDGE;
	if (ctx->vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_POSEDGE;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
			himax_bus_formats, ARRAY_SIZE(himax_bus_formats));
	if (ret)
		return ret;

	drm_mode_probed_add(panel->connector, mode);
	return 1;
}

static const struct drm_panel_funcs hx8379c_dsi_drm_funcs = {
	.disable = hx8379c_dsi_disable,
	.unprepare = hx8379c_dsi_unprepare,
	.prepare = hx8379c_dsi_prepare,
	.enable = hx8379c_dsi_enable,
	.get_modes = hx8379c_get_modes,
};

/*
 * The clock might 33Mhz(60Hz refresh rate)
 */
static const struct display_timing himax_default_timing = {
	.pixelclock = { 40000000, 29000000, 40000000 },
	.hactive = { 480, 480, 480 },
	.hfront_porch = { 35, 35, 35 },
	.hsync_len = { 37, 37, 37 },
	.hback_porch = { 35, 35, 35 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 5, 5, 5 },
	.vsync_len = { 2, 2, 2 },
	.vback_porch = { 10, 10, 10 },
};

static const struct of_device_id hx8379c_dsi_of_match[] = {
	{
		.compatible = "himax,hx8379c",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, hx8379c_dsi_of_match);

static int hx8379c_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct of_device_id *of_id =
			of_match_device(hx8379c_dsi_of_match, dev);
	struct hx8379c *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	if (of_id) {
		ctx->pd = of_id->data;
	} else {
		dev_err(dev, "cannot find compatible device\n");
		return -ENODEV;
	}

	videomode_from_timing(&himax_default_timing, &ctx->vm);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 2;

	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
			    MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_VIDEO_SYNC_PULSE | MIPI_DSI_MODE_EOT_PACKET;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &hx8379c_dsi_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	dev_info(ctx->dev, "hx8379c probed\n");
	return ret;
}

static int hx8379c_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct hx8379c *ctx = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret)
		DRM_DEV_ERROR(dev, "Fail to detach from host\n");
	drm_panel_detach(&ctx->panel);

	if (ctx->panel.dev)
		drm_panel_remove(&ctx->panel);
	return 0;
}

static void hx8379c_dsi_shutdown(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;

	dev_dbg(dev, "hx8379c shutdown\n");
}


static struct mipi_dsi_driver hx8379c_dsi_driver = {
	.probe = hx8379c_dsi_probe,
	.remove = hx8379c_dsi_remove,
	.shutdown = hx8379c_dsi_shutdown,
	.driver = {
		.name = "panel-hx8379c-dsi",
		.of_match_table = hx8379c_dsi_of_match,
	},
};
module_mipi_dsi_driver(hx8379c_dsi_driver);

MODULE_DESCRIPTION("Himax HX8379C panel driver");
MODULE_LICENSE("GPL v2");
