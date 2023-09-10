// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018,2019 Sony Video & Sound Products Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

struct gps_ctl_data {
	struct device		*dev;
	struct gpio_desc	*xrst_gpio;
	struct gpio_desc	*boot_gpio;
	unsigned long		reset_time_us;
};

static struct gps_ctl_data *gps_pb;	/* Platform data */
static unsigned int status_xrst;	/* xrst status */
static unsigned int status_boot;	/* boot status */
static unsigned int status_busy;	/* busy status */

static void gps_ctl_xrst_switch(struct gps_ctl_data *pb, int onoff)
{
	pr_debug("gps_ctl: %s() onoff=%d\n", __func__, onoff);
	if (pb->xrst_gpio) {
		if (onoff) {
			pr_info("gps_ctl: xrst=on\n");
			gpiod_set_value_cansleep(pb->xrst_gpio, 1);
		} else {
			pr_info("gps_ctl: xrst=off\n");
			gpiod_set_value_cansleep(pb->xrst_gpio, 1);
			usleep_range(pb->reset_time_us,
				pb->reset_time_us + (pb->reset_time_us / 10));
			gpiod_set_value_cansleep(pb->xrst_gpio, 0);
		}
	}
}

static void gps_ctl_boot_switch(struct gps_ctl_data *pb, int onoff)
{
	pr_debug("gps_ctl: %s() onoff=%d\n", __func__, onoff);
	if (pb->boot_gpio) {
		if (onoff) {
			pr_info("gps_ctl: boot=on\n");
			gpiod_set_value_cansleep(pb->boot_gpio, 1);
		} else {
			pr_info("gps_ctl: boot=off\n");
			gpiod_set_value_cansleep(pb->boot_gpio, 0);
		}
	}
}

static int gps_ctl_xrst_set(const char *str, const struct kernel_param *kp)
{
	long val;
	int ret = 0;

	pr_debug("gps_ctl: %s()\n", __func__);
	if (!str) {
		pr_err("%s: Pointer to parameter is NULL.\n", __func__);
		return -EINVAL;
	}

	ret = kstrtol(str, 0, &val);
	if (ret == 0) {
		gps_ctl_xrst_switch(gps_pb, val);
		return param_set_uint(str, kp);
	}
	return -EINVAL;
}

module_param_call(xrst, gps_ctl_xrst_set, param_get_int, &status_xrst, 0644);

static int gps_ctl_boot_set(const char *str, const struct kernel_param *kp)
{
	long val;
	int ret = 0;

	pr_debug("gps_ctl: %s()\n", __func__);
	if (!str) {
		pr_err("%s: Pointer to parameter is NULL.\n", __func__);
		return -EINVAL;
	}

	ret = kstrtoul(str, 0, &val);
	if (ret == 0) {
		gps_ctl_boot_switch(gps_pb, val);
		return param_set_uint(str, kp);
	}
	return -EINVAL;
}

module_param_call(boot, gps_ctl_boot_set, param_get_int, &status_boot, 0644);

static int gps_ctl_busy_set(const char *str, const struct kernel_param *kp)
{
	long val;
	int ret = 0;

	pr_debug("gps_ctl: %s()\n", __func__);
	if (!str) {
		pr_err("%s: Pointer to parameter is NULL.\n", __func__);
		return -EINVAL;
	}

	ret = kstrtoul(str, 0, &val);
	if (ret == 0) {
		pr_info("gps_ctl: busy=%u\n", val);
		return param_set_uint(str, kp);
	}
	return -EINVAL;
}

module_param_call(busy, gps_ctl_busy_set, param_get_int, &status_busy, 0644);

static int gps_ctl_probe(struct platform_device *pdev)
{
	struct device_node *node;
	struct gps_ctl_data *pb;

	u32 value;
	int ret;

	pr_debug("gps_ctl: %s()\n", __func__);
	node = pdev->dev.of_node;

	pb = devm_kzalloc(&pdev->dev, sizeof(struct gps_ctl_data), GFP_KERNEL);
	if (!pb)
		return -ENOMEM;
	pb->dev = &pdev->dev;
	pb->xrst_gpio = NULL;
	pb->boot_gpio = NULL;
	pb->reset_time_us = 100000;

	/* Get XRST gpio */
	pb->xrst_gpio = devm_gpiod_get(&pdev->dev, "xrst", GPIOD_OUT_LOW);
	if (IS_ERR(pb->xrst_gpio)) {
		pr_info("gps_ctl: xrst-gpios is disabled\n");
		pb->xrst_gpio = NULL;
	}
	/* Get BOOT_REC gpio */
	pb->boot_gpio = devm_gpiod_get(&pdev->dev, "boot", GPIOD_OUT_LOW);
	if (IS_ERR(pb->boot_gpio)) {
		pr_info("gps_ctl: boot-gpios is disabled\n");
		pb->boot_gpio = NULL;
	}

	/* Get reset blanking time */
	ret = of_property_read_u32(node, "reset_time_us", &value);
	if (ret >= 0)
		pb->reset_time_us = value;
	pr_info("gps_ctl: reset_time_us_gpio=%lu\n", pb->reset_time_us);

	platform_set_drvdata(pdev, pb);

	gps_pb = pb;
	status_xrst = 0;
	status_boot = 0;
	status_busy = 1;
	gps_ctl_boot_switch(pb, 0);
	gps_ctl_xrst_switch(pb, 0);

	return 0;
}

static int gps_ctl_remove(struct platform_device *pdev)
{
	struct gps_ctl_data *pb;

	pr_debug("gps_ctl: %s()\n", __func__);
	pb = platform_get_drvdata(pdev);
	gps_ctl_xrst_switch(pb, 1);

	return 0;
}

static void gps_ctl_shutdown(struct platform_device *pdev)
{
	struct gps_ctl_data *pb;

	pr_debug("gps_ctl: %s()\n", __func__);
	pb = platform_get_drvdata(pdev);
	gps_ctl_xrst_switch(pb, 1);
}

static const struct of_device_id gps_ctl_of_match[] = {
	{ .compatible = "sony,gps_ctl", },
	{ },
};
MODULE_DEVICE_TABLE(of, gps_ctl_of_match);

static struct platform_driver gps_ctl_driver = {
	.driver		= {
		.name		= "gps_ctl",
		.of_match_table	= of_match_ptr(gps_ctl_of_match),
	},
	.probe		= gps_ctl_probe,
	.remove		= gps_ctl_remove,
	.shutdown	= gps_ctl_shutdown,
};

module_platform_driver(gps_ctl_driver);

MODULE_AUTHOR("Sony Video & Sound Products Inc.");
MODULE_DESCRIPTION("GPS Pin Control Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gps_ctl");
