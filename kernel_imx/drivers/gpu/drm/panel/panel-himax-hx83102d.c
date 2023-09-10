/*
 * Himax HX83102d panel driver.
 *
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc.
 * Copyright 2018,2019 Sony Video & Sound Products Inc.
 * Copyright 2019,2020 Sony Home Entertainment & Sound Products Inc.
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
#include <drm/drm_modeset_helper.h>

#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>

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
#define SETBANK		0xbd
#define SETPTBA		0xbf
#define SETPANEL	0xcc
#define SETGAMMA	0xe0

#define SETTCON		0xc7
#define SETOFFSET	0xd2
#define SETCE		0xe4
#define SETGIP_0	0xd3
#define SETGIP_1	0xd5
#define SETGIP_2	0xd6

#define SPECIFIC_B0	0xb0
#define SPECIFIC_CB	0xcb
#define SPECIFIC_E7	0xe7

#define ESD_CHECK_PERIOD	(1 * HZ)
#define ESD_CHECK_TIME		(5 * HZ)

static const u32 himax_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

struct hx83102d_panel_desc {
	const struct drm_display_mode *mode;

	/* ms */
	unsigned int power_on_delay;
	unsigned int reset_delay;

	unsigned int dsi_lanes;
};

struct hx83102d {
	struct device *dev;
	struct drm_panel panel;

	const struct hx83102d_panel_desc *pd;
	struct delayed_work esdwork;
	int te_irq;
	int te_count;
	u16 fps;
	struct completion te_comp;
	struct workqueue_struct *esdqueue;
	struct regulator_bulk_data supplies[5];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bs_gpio[4];
	u8 res_sel;
	struct videomode vm;
	bool prepared;
	bool enabled;
	struct gpio_desc *lcd_pwrp_gpio;
	struct gpio_desc *lcd_pwrn_gpio;
	struct gpio_desc *lcd_te_gpio;
};

static void hx83102d_dsi_set_maximum_return_packet_size(struct hx83102d *ctx,
						       u16 size);

extern int himax_common_suspend_export(void);
extern int himax_common_resume_export(void);
extern int himax_common_remove_export(void);

static irqreturn_t hx83102d_te_isr(int irq, void *data)
{
	struct hx83102d *ctx = data;

	ctx->te_count++;
	return IRQ_HANDLED;
}

static int hx83102d_panel_restore_thread(void *arg)
{
	struct hx83102d *ctx = (struct hx83102d *)arg;
	struct drm_device *dev = ctx->panel.drm;
	int ret;

	dev_info(ctx->dev, "panel restore start.\n");
	ret = drm_mode_config_helper_suspend(dev);
	if (ret < 0) {
		dev_err(ctx->dev, "%s suspend failed: %d\n", __func__, ret);
		return ret;
	}
	ret = drm_mode_config_helper_resume(dev);
	if (ret < 0) {
		dev_err(ctx->dev, "%s resume failed: %d\n", __func__, ret);
		return ret;
	}

	dev_info(ctx->dev, "panel restore done.\n");
	return 0;
}

static void hx83102d_esd_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct hx83102d *ctx = container_of(dwork, struct hx83102d, esdwork);
	struct task_struct *th;
	int ret;
	u16 detect_time, fps_detect, fps_lower_limit;
	u64 start_jiff, diff_jiff;

	ctx->te_count = 0;
	start_jiff = jiffies;
	enable_irq(ctx->te_irq);
	ret = wait_for_completion_timeout(&ctx->te_comp, ESD_CHECK_TIME);
	disable_irq(ctx->te_irq);
	diff_jiff = jiffies - start_jiff;

	dev_dbg(ctx->dev, "te_count = %d %llu HZ:%d timeout:%d\n",
		ctx->te_count, diff_jiff, HZ, ret);
	if (diff_jiff >= ESD_CHECK_TIME) {
		detect_time = (diff_jiff * 100) / HZ;
		fps_detect = (ctx->te_count * 100) / detect_time;
		fps_lower_limit = ctx->fps - (ctx->fps / 10);
		if (fps_detect <= fps_lower_limit) {
			dev_warn(ctx->dev,
				"Electrostatic detection! te_count = %d %llu HZ:%d fps:%d\n",
				ctx->te_count, diff_jiff, HZ, fps_detect);
			th = kthread_run(hx83102d_panel_restore_thread, ctx,
				"hx83102d_panel_restore_thread");
			if (IS_ERR(th))
				dev_err(ctx->dev, "te_count kthread_run err.\n");
		}
	}
	queue_delayed_work(ctx->esdqueue, &ctx->esdwork, ESD_CHECK_PERIOD);
}

static inline struct hx83102d *panel_to_hx83102d(struct drm_panel *panel)
{
	return container_of(panel, struct hx83102d, panel);
}

static int hx83102d_dcs_write(struct hx83102d *ctx, const char *func,
			      const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	//ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "%s failed: %ld\n", func, ret);
		return ret;
	}

	return ret;
}

#define hx83102d_dcs_write_seq_static(ctx, seq...) \
({ \
	static const u8 d[] = { seq }; \
	hx83102d_dcs_write(ctx, __func__, d, ARRAY_SIZE(d)); \
})

static void hx83102d_dsi_panel_init(struct hx83102d *ctx)
{
	//set_power
	hx83102d_dcs_write_seq_static(ctx, SETPOWER, 0x20, 0x11, 0x27, 0x27,
			0x22, 0x77, 0x2f, 0x43, 0x18, 0x18, 0x18);
	//set_display_related_register
	hx83102d_dcs_write_seq_static(ctx, SETDISP, 0x00, 0x00, 0x05,
		0x00, 0x00, 0x0E, 0x92, 0x4D, 0x00, 0x00, 0x00,
		0x00, 0x14, 0x20);
	//set_display_waveform_cycle
	hx83102d_dcs_write_seq_static(ctx, SETCYC, 0x14, 0x60, 0x14, 0x60, 0x14,
		0x60, 0x14, 0x60, 0x03, 0xff, 0x01, 0x20, 0x00, 0xff);

	//set_panel
	hx83102d_dcs_write_seq_static(ctx, SETPANEL, 0x02);
	//set_gip_0
	hx83102d_dcs_write_seq_static(ctx, SETGIP_0, 0x33, 0x00, 0x3c, 0x03,
		0x00, 0x08, 0x00, 0x37, 0x00, 0x33, 0x3b, 0x0a, 0x0a,
		0x00, 0x00, 0x32, 0x10, 0x06, 0x00, 0x06, 0x00,
		0x00, 0x02, 0x00, 0x02, 0x00, 0x05, 0x15, 0x05,
		0x15, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
		0x1a, 0x00, 0x00);
	//set_gip_1
	hx83102d_dcs_write_seq_static(ctx, SETGIP_1, 0x18, 0x18, 0x18, 0x18,
		0x19, 0x19, 0x39, 0x39, 0x24, 0x24, 0x04, 0x05,
		0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x18, 0x18,
		0x38, 0x38, 0x20, 0x21, 0x22, 0x23, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	//set_gip_2
	hx83102d_dcs_write_seq_static(ctx, SETGIP_2, 0x18, 0x18, 0x19, 0x19,
		0x18, 0x18, 0x39, 0x39, 0x24, 0x24, 0x03, 0x02,
		0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x18, 0x18,
		0x38, 0x38, 0x23, 0x22, 0x21, 0x20, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	//set_specific_e7
	hx83102d_dcs_write_seq_static(ctx, SPECIFIC_E7, 0xff, 0x0f, 0x00, 0x00);
	//set_bank_1
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x01);
	//set_specific_e7
	hx83102d_dcs_write_seq_static(ctx, SPECIFIC_E7, 0x01);
	//set_bank_0
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	//set_vcom_voltage
	hx83102d_dcs_write_seq_static(ctx, SETVCOM, 0x90, 0x90, 0xe0);
	//set_gamma_curve
	hx83102d_dcs_write_seq_static(ctx, SETGAMMA, 0x00, 0x05, 0x0F, 0x17,
		0x1F, 0x35, 0x4F, 0x56, 0x5C, 0x5B, 0x75, 0x7B,
		0x82, 0x92, 0x92, 0x9C, 0xA6, 0xBC, 0xBC, 0x5E,
		0x65, 0x6F, 0x7F, 0x00, 0x05, 0x0F, 0x17, 0x1F,
		0x35, 0x4F, 0x56, 0x5C, 0x5B, 0x75, 0x7B, 0x82,
		0x92, 0x92, 0x9C, 0xA6, 0xBC, 0xBC, 0x5E, 0x65,
		0x6F, 0x7F);
	//set_specific_b0
	hx83102d_dcs_write_seq_static(ctx, SPECIFIC_B0, 0x00, 0x20);
	//set_mipi
	hx83102d_dcs_write_seq_static(ctx, SETMIPI, 0x70, 0x23,
	 0xA8, 0x93, 0xB2, 0x80, 0x80, 0x01, 0x10, 0x00, 0x00,
	  0x00, 0x0C, 0x3D, 0x82, 0x77, 0x04, 0x01, 0x04);
	//set_bank_1
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x01);
	//set_specific_cb
	hx83102d_dcs_write_seq_static(ctx, SPECIFIC_CB, 0x01);
	//set_bank_0
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	//set_power_option
	hx83102d_dcs_write_seq_static(ctx, SETPTBA, 0xFC, 0x00,
	 0x05, 0x9E, 0xF6, 0x00, 0x45);
	//set_bank_2
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x02);
	//set_display_waveform_cycle_2
	hx83102d_dcs_write_seq_static(ctx, SETCYC, 0xC2, 0x00, 0x33, 0x00, 0x33,
							 0x88, 0xB3, 0x00);
	//set_bank_0
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	//set_bank_1
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x01);
	//set_gip_0
	hx83102d_dcs_write_seq_static(ctx, SETGIP_0, 0x09, 0x00, 0x78);
	//set_bank_0
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	//set_bank_2
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x02);
	//set_power
	hx83102d_dcs_write_seq_static(ctx, SETPOWER, 0x7F, 0x07, 0xFF);
	//set_bank_0
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
}

static int hx83102d_dsi_set_extension_command(struct hx83102d *ctx)
{
	int ret;
	const u8 hx83102d_extension_command[] = { SETEXTC, 0x83, 0x10, 0x2d };

	ret = hx83102d_dcs_write(ctx, __func__, hx83102d_extension_command,
				ARRAY_SIZE(hx83102d_extension_command));
	if (ret < 0) {
		dev_err(ctx->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	usleep_range(10000, 11000);
	return ret;
}

static void hx83102d_dsi_set_maximum_return_packet_size(struct hx83102d *ctx,
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

static int hx83102d_dsi_set_sequence(struct hx83102d *ctx)
{
	int ret;

	ret = hx83102d_dsi_set_extension_command(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}
	hx83102d_dsi_panel_init(ctx);

	return ret;
}

static int hx83102d_dsi_disable(struct drm_panel *panel)
{
	struct hx83102d *ctx = panel_to_hx83102d(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = &dsi->dev;
	int ret;

	if (!ctx->enabled)
		return 0;

	himax_common_suspend_export();

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		DRM_DEV_ERROR(dev, "Fail to display off, continue disable sequence\n");
		/* Continue to turn off LCD and handle panel "state" properly */
	}

	usleep_range(5000, 10000);

	complete(&ctx->te_comp);
	cancel_delayed_work_sync(&ctx->esdwork);

	/* TE pulse is turn off by setting to sleep mode */
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		DRM_DEV_ERROR(dev, "Fail to Sleep In, continue disable sequence\n");
		/* Continue to turn off LCD and handle panel "state" properly */
	}

	usleep_range(10000, 15000);

	dev_info(ctx->dev, "hx83102d disabled\n");

	ctx->enabled = false;

	return 0;
}

static int hx83102d_dsi_unprepare(struct drm_panel *panel)
{
	struct hx83102d *ctx = panel_to_hx83102d(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = &dsi->dev;

	if (!ctx->prepared)
		return 0;

	if (ctx->enabled) {
		DRM_DEV_ERROR(dev, "Fail uprepare panel still enabled\n");
		return -EPERM;
	}

	/* reset panel */
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5000, 5500);

	gpiod_set_value(ctx->lcd_pwrn_gpio, 0);
	usleep_range(5000, 5500);
	gpiod_set_value(ctx->lcd_pwrp_gpio, 0);

	dev_dbg(ctx->dev, "hx83102d unprepared\n");
	ctx->prepared = false;

	return 0;
}

static int hx83102d_dsi_prepare(struct drm_panel *panel)
{
	struct hx83102d *ctx = panel_to_hx83102d(panel);
	static int is_first_boot = 1;

	if (ctx->prepared)
		return 0;

	gpiod_set_value(ctx->lcd_pwrp_gpio, 1);
	usleep_range(5000, 5500);
	gpiod_set_value(ctx->lcd_pwrn_gpio, 1);
	usleep_range(2000, 2200);

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(2000, 2200);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(1000, 1100);

	if (is_first_boot) {
		msleep(120);
		is_first_boot = 0;
	}

	dev_dbg(ctx->dev, "hx83102d prepared\n");
	ctx->prepared = true;

	return 0;
}

static int hx83102d_dsi_enable(struct drm_panel *panel)
{
	struct hx83102d *ctx = panel_to_hx83102d(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = &dsi->dev;
	int ret;

	if (ctx->enabled)
		return 0;

	if (!ctx->prepared) {
		DRM_DEV_ERROR(dev, "Fail panel not prepared\n");
		return -EPERM;
	}

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	ret = hx83102d_dsi_set_sequence(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "%s failed: %d\n", __func__, ret);
		return -EIO;
	}
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
	msleep(70);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(40000, 44000);

	dev_info(ctx->dev, "hx83102d enabled\n");

	himax_common_resume_export();
	if(ctx->te_comp.done)
		init_completion(&ctx->te_comp);
	queue_delayed_work(ctx->esdqueue, &ctx->esdwork, ESD_CHECK_PERIOD);
	ctx->enabled = true;

	return 0;
}

static int hx83102d_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct hx83102d *ctx = panel_to_hx83102d(panel);
	struct drm_display_mode *mode;
	u32 *bus_flags = &connector->display_info.bus_flags;
	int ret;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		pr_err("Failed to create display mode!\n");
		return 0;
	}
	drm_display_mode_from_videomode(&ctx->vm, mode);
	mode->width_mm = 45;
	mode->height_mm = 80;
	mode->vrefresh = 60;
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

static ssize_t pwr_en_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct hx83102d *ctx = dev_get_drvdata(dev);
	int pwrp, pwrn;

	if (!buf)
		return -EINVAL;

	pwrp = gpiod_get_value(ctx->lcd_pwrp_gpio);
	if (pwrp < 0)
		return pwrp;

	pwrn = gpiod_get_value(ctx->lcd_pwrn_gpio);
	if (pwrn < 0)
		return pwrn;

	return snprintf(buf, PAGE_SIZE, "%d %d\n", pwrp, pwrn);
}

static ssize_t pwr_en_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct hx83102d *ctx = dev_get_drvdata(dev);
	long pwr;

	if (!buf)
		return -EINVAL;

	if (kstrtol(buf, 0, &pwr))
		return -EINVAL;

	if (pwr) {
		gpiod_set_value(ctx->lcd_pwrp_gpio, 1);
		usleep_range(5000, 5500);
		gpiod_set_value(ctx->lcd_pwrn_gpio, 1);
		usleep_range(2000, 2200);
	} else {
		gpiod_set_value(ctx->lcd_pwrn_gpio, 0);
		usleep_range(5000, 5500);
		gpiod_set_value(ctx->lcd_pwrp_gpio, 0);
	}

	return count;
}

static ssize_t panel_enabled_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct hx83102d *ctx = dev_get_drvdata(dev);

	if (!buf)
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%d\n", ctx->enabled);
}

static ssize_t panel_prepared_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct hx83102d *ctx = dev_get_drvdata(dev);

	if (!buf)
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%d\n", ctx->prepared);
}

void hx83102d_inicode_dump(struct hx83102d *ctx,
			u8 cmd, size_t len, char *regname, char *buf)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;
	int i;
	u8 data[128] = {0};
	char str1[256] = {0};
	char str2[8] = {0};

	hx83102d_dsi_set_maximum_return_packet_size(ctx, len);
	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq(%#x)\n", ret, cmd);
		return;
	}

	snprintf(str1, strlen(regname) + 13, "%s(%lu bytes)=", regname, len);
	for (i = 0; i < len; i++) {
		snprintf(str2, 6, " 0x%02x ", data[i]);
		strncat(str1, str2, 6);
	}
	strncat(buf, str1, strlen(str1));
	strncat(buf, "\n", 1);
	dev_dbg(ctx->dev, "%s str1=%s\n", __func__, str1);

}

static ssize_t driver_ic_reg_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct hx83102d *ctx = dev_get_drvdata(dev);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char *dump_buf;
	ssize_t len = 0;
	struct task_struct *th;

	if (!buf)
		return -EINVAL;

	if (!ctx->enabled) {
		dev_err(ctx->dev, "%s Panel is not enable!\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s Panel is not enable!\n",
			__func__);
	}

	dump_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (dump_buf == NULL)
		return -ENOMEM;

	hx83102d_inicode_dump(ctx, SETPOWER, 11, "SETPOWER", dump_buf);
	hx83102d_inicode_dump(ctx, SETDISP, 14, "SETDISP", dump_buf);
	hx83102d_inicode_dump(ctx, SETCYC, 14, "SETCYC", dump_buf);
	hx83102d_inicode_dump(ctx, SETPANEL, 1, "SETPANEL", dump_buf);
	hx83102d_inicode_dump(ctx, SETGIP_0, 40, "SETGIP_0", dump_buf);
	hx83102d_inicode_dump(ctx, SETGIP_1, 44, "SETGIP_1", dump_buf);
	hx83102d_inicode_dump(ctx, SETGIP_2, 44, "SETGIP_2", dump_buf);
	hx83102d_inicode_dump(ctx, SPECIFIC_E7, 4, "SPECIFIC_E7", dump_buf);
	//bank 01
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x01);
	hx83102d_inicode_dump(ctx, SPECIFIC_E7, 1, "SPECIFIC_E7", dump_buf);
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	hx83102d_inicode_dump(ctx, SETVCOM, 3, "SETVCOM", dump_buf);
	hx83102d_inicode_dump(ctx, SETGAMMA, 46, "SETGAMMA", dump_buf);
	hx83102d_inicode_dump(ctx, SPECIFIC_B0, 2, "SPECIFIC_B0", dump_buf);
	hx83102d_inicode_dump(ctx, SETMIPI, 19, "SETMIPI", dump_buf);
	//bank 01
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x01);
	hx83102d_inicode_dump(ctx, SPECIFIC_CB, 1, "SPECIFIC_CB", dump_buf);
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	hx83102d_inicode_dump(ctx, SETPTBA, 7, "SETPTBA", dump_buf);
	//bank 02
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x02);
	hx83102d_inicode_dump(ctx, SETCYC, 8, "SETCYC", dump_buf);
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	//bank 01
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x01);
	hx83102d_inicode_dump(ctx, SETGIP_0, 3, "SETGIP_0", dump_buf);
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);
	//bank 02
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x02);
	hx83102d_inicode_dump(ctx, SETPOWER, 3, "SETPOWER", dump_buf);
	hx83102d_dcs_write_seq_static(ctx, SETBANK, 0x00);

	len = strlen(dump_buf);
	dev_dbg(ctx->dev, "%s dump_buf len = %ld\n", __func__, len);
	memcpy(buf, dump_buf, len);
	kfree(dump_buf);

	//restore to stabilize panel status
	th = kthread_run(hx83102d_panel_restore_thread, ctx,
		"hx83102d_panel_restore_thread");
	if (IS_ERR(th))
		dev_err(ctx->dev, "driver ic reg show kthread_run err.\n");

	msleep(1000);

	return len;

}

static DEVICE_ATTR(pwr_en, 0600, pwr_en_show, pwr_en_store);
static DEVICE_ATTR(panel_enabled, 0600, panel_enabled_show, NULL);
static DEVICE_ATTR(panel_prepared, 0600, panel_prepared_show, NULL);
static DEVICE_ATTR(driver_ic_reg, 0400, driver_ic_reg_show, NULL);

static struct attribute *hx83102d_attributes[] = {
	&dev_attr_pwr_en.attr,
	&dev_attr_panel_enabled.attr,
	&dev_attr_panel_prepared.attr,
	&dev_attr_driver_ic_reg.attr,
	NULL
};

static struct attribute_group hx83102d_attribute_group = {
	.name = "debug",
	.attrs = hx83102d_attributes,
};

static const struct drm_panel_funcs hx83102d_dsi_drm_funcs = {
	.disable = hx83102d_dsi_disable,
	.unprepare = hx83102d_dsi_unprepare,
	.prepare = hx83102d_dsi_prepare,
	.enable = hx83102d_dsi_enable,
	.get_modes = hx83102d_get_modes,
};

/*
 * The clock might 71Mhz(60Hz refresh rate)
 */
static const struct display_timing himax_default_timing = {
	.pixelclock = { 66000000, 66000000, 66000000 },
	.hactive = { 720, 720, 720 },
	.hfront_porch = { 20, 20, 20 },
	.hsync_len = { 12, 12, 12 },
	.hback_porch = { 10, 10, 10 },
	.vactive = { 1280, 1280, 1280 },
	.vfront_porch = { 146, 146, 146 },
	.vsync_len = { 2, 2, 2 },
	.vback_porch = { 14, 14, 14 },
};

static const struct of_device_id hx83102d_dsi_of_match[] = {
	{
		.compatible = "himax,hx83102d",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, hx83102d_dsi_of_match);

static int hx83102d_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct of_device_id *of_id =
			of_match_device(hx83102d_dsi_of_match, dev);
	struct hx83102d *ctx;
	int hsync_period, vsync_period;
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

	init_completion(&ctx->te_comp);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	//TODO
	ctx->lcd_pwrp_gpio = devm_gpiod_get(dev, "pwrp", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->lcd_pwrp_gpio)) {
		dev_err(dev, "cannot get pwrp-gpios %ld\n",
			PTR_ERR(ctx->lcd_pwrp_gpio));
		return PTR_ERR(ctx->lcd_pwrp_gpio);
	}
	ctx->lcd_pwrn_gpio = devm_gpiod_get(dev, "pwrn", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->lcd_pwrn_gpio)) {
		dev_err(dev, "cannot get pwrn-gpios %ld\n",
			PTR_ERR(ctx->lcd_pwrn_gpio));
		return PTR_ERR(ctx->lcd_pwrn_gpio);
	}

	ctx->lcd_te_gpio = devm_gpiod_get(dev, "te", GPIOD_IN);
	if (IS_ERR(ctx->lcd_te_gpio)) {
		dev_err(dev, "cannot get te-gpios %ld\n",
			PTR_ERR(ctx->lcd_te_gpio));
		return PTR_ERR(ctx->lcd_te_gpio);
	}

	ctx->te_irq = gpiod_to_irq(ctx->lcd_te_gpio);

	ret = request_irq(ctx->te_irq, hx83102d_te_isr,
		IRQF_TRIGGER_RISING, "lcd te", ctx);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}
	disable_irq(ctx->te_irq);

	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 2;

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_EOT_PACKET;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &hx83102d_dsi_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	ret = sysfs_create_group(&dev->kobj, &hx83102d_attribute_group);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Fail to create sysfs for debug\n");

	hsync_period = ctx->vm.hactive + ctx->vm.hfront_porch
		+ ctx->vm.hback_porch + ctx->vm.hsync_len;
	vsync_period = ctx->vm.vactive + ctx->vm.vfront_porch
		+ ctx->vm.vback_porch + ctx->vm.vsync_len;
	ctx->fps = ctx->vm.pixelclock / (hsync_period * vsync_period);

	ctx->esdqueue = create_workqueue("lcd esd work");
	INIT_DELAYED_WORK(&ctx->esdwork, hx83102d_esd_work);

	dev_info(ctx->dev, "hx83102d probed\n");
	return ret;
}

static int hx83102d_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct hx83102d *ctx = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	sysfs_remove_group(&dev->kobj, &hx83102d_attribute_group);

	ret = mipi_dsi_detach(dsi);
	if (ret)
		DRM_DEV_ERROR(dev, "Fail to detach from host\n");
	drm_panel_detach(&ctx->panel);

	if (ctx->panel.dev)
		drm_panel_remove(&ctx->panel);
	return 0;
}

static void hx83102d_dsi_shutdown(struct mipi_dsi_device *dsi)
{
	struct hx83102d *ctx = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;

	hx83102d_dsi_disable(&ctx->panel);
	himax_common_remove_export();
	hx83102d_dsi_unprepare(&ctx->panel);

	dev_dbg(dev, "hx83102d shutdown\n");
}


static struct mipi_dsi_driver hx83102d_dsi_driver = {
	.probe = hx83102d_dsi_probe,
	.remove = hx83102d_dsi_remove,
	.shutdown = hx83102d_dsi_shutdown,
	.driver = {
		.name = "panel-hx83102d-dsi",
		.of_match_table = hx83102d_dsi_of_match,
	},
};
module_mipi_dsi_driver(hx83102d_dsi_driver);

MODULE_DESCRIPTION("Himax HX83102D panel driver");
MODULE_LICENSE("GPL v2");
