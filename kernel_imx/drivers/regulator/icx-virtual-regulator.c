/*
 * icx-virtual-regulator.c
 *
 * Copyright 2010 Wolfson Microelectronics PLC.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>

struct virtual_regulator_data {
	struct device *dev;
	struct regulator_dev *rdev;
	struct gpio_desc *gpio;
	int	enabled;
	unsigned int sel;
};

static struct regulator_init_data virtual_regulator_initdata = {
};

static int virtual_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct virtual_regulator_data *data = rdev_get_drvdata(rdev);

	return data->enabled;
}

static int virtual_regulator_enable(struct regulator_dev *rdev)
{
	struct virtual_regulator_data *data = rdev_get_drvdata(rdev);

	data->enabled = 1;
	dev_info(data->dev, "Negate LPA_POFF.\n");
	if (IS_ERR_OR_NULL(data->gpio))
		return 0;
	gpiod_set_value(data->gpio, 0);
	return 0;
}

static int virtual_regulator_disable(struct regulator_dev *rdev)
{
	struct virtual_regulator_data *data = rdev_get_drvdata(rdev);
	extern bool imx_lpa_is_enabled(void);

	data->enabled = 0;
	if (!imx_lpa_is_enabled())
		return 0;
	dev_info(data->dev, "Assert LPA_POFF.\n");
	if (IS_ERR_OR_NULL(data->gpio))
		return 0;
	gpiod_set_value(data->gpio, 1);
	return 0;
}

static struct regulator_ops virtual_regulator_ops = {
	.enable = virtual_regulator_enable,
	.disable = virtual_regulator_disable,
	.is_enabled = virtual_regulator_is_enabled,
};

static const struct regulator_desc virtual_regulator_desc = {
	.name = "virtual-regulator",
	.id = -1,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &virtual_regulator_ops,
};

static int virtual_regulator_probe(struct platform_device *pdev)
{
	struct virtual_regulator_data *data;
	struct regulator_config config = { };
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Memory allocation failed for data\n");
		return -ENOMEM;
	}

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);

	data->gpio = devm_gpiod_get_optional(data->dev, "lpa-poff", GPIOD_OUT_LOW);
	if (data->gpio == NULL)
		dev_notice(data->dev, "Can not read property. (lpa-poff-gpio)\n");

	config.dev = data->dev;
	config.init_data = &virtual_regulator_initdata;
	config.driver_data = data;

	data->rdev = regulator_register(&virtual_regulator_desc, &config);
	if (IS_ERR(data->rdev)) {
		ret = PTR_ERR(data->rdev);
		dev_err(data->dev, "Failed to register regulator: %d\n", ret);
		goto err;
	}

	dev_info(data->dev, "%s: Registered.\n", __func__);

	return 0;

err:
	kfree(data);
	return ret;
}

#if defined(CONFIG_OF)
static const struct of_device_id virtual_regulator_of_match[] = {
	{ .compatible = "sony,icx-virtual-regulator", },
	{},
};
#endif

static struct platform_driver virtual_regulator_driver = {
	.probe		= virtual_regulator_probe,
	.driver		= {
		.name		= "reg-virtual",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(virtual_regulator_of_match),
	},
};

module_platform_driver(virtual_regulator_driver);

MODULE_AUTHOR("Sony Home Entertainment & Sound Products Inc.");
MODULE_DESCRIPTION("ICX virtual regulator.");
MODULE_LICENSE("GPL");
