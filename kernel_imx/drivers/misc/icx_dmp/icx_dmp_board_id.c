/*
 * SVS ICX DMP board ID reader driver
 *
 * Copyright 2018 Sony Video & Sound Products Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio/consumer.h>

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/delay.h>

#include <linux/icx_dmp_board_id.h>

struct icx_dmp_board_id icx_dmp_board_id;
EXPORT_SYMBOL(icx_dmp_board_id);

/* #define DEBUG_SHOW_PINCTRL_NAME */

/* Read GPIO value connected to DIP(array) switch
 * In
 *  @dmp	DMP device context
 *  @i		base (LSB) gpio_descs index number
 *  @n		The number of pins in gpio_descs
 *  @read_bits	read value from DIP switch
 * Return:
 *  int < 0 Indicates error by negative errno number.
 *      ==0 Success
 */
static int icx_dmp_gpio_id_read(struct icx_dmp_board_id *dmp,
	int i, int n, unsigned long *read_bits)
{	struct	gpio_desc	**descs;

	unsigned long	bits;
	int	ret = 0;
	int	val;

	if (read_bits)
		*read_bits = 0;

	i += n;
	descs = &(dmp->gpios->desc[i]);
	bits = 0;
	while (n > 0) {
		struct	gpio_desc	*id_pin;

		descs--;
		id_pin = *descs;
		val = gpiod_get_value(id_pin);
		if (val < 0) {
			ret = val;
			dev_err(icx_dmp_board_id_dev(dmp),
				"Can not read GPIO id pin. "
				"gpio=%d, ret=%d\n",
				desc_to_gpio(id_pin), ret
			);
			goto out;
		}
		bits <<= 1;
		bits |= (((unsigned long)val) & 0x1);
		n--;
	}

out:
	if (read_bits)
		*read_bits = bits;

	return ret;
}

static const char driver_not_init[] = "Driver not initialized.\n";

static ssize_t icx_dmp_config_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{	ssize_t			len;
	struct icx_dmp_board_id	*dmp;

	dmp = dev_get_drvdata(dev);
	if (dmp == NULL) {
		dev_err(dev, driver_not_init);
		return -ENODEV;
	}

	len = snprintf(buf,
		2 + sizeof(unsigned long long) * 2 + 2, "0x%.16llx\n",
		dmp->config
	);
	return len;
}

static ssize_t icx_dmp_setid_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{	ssize_t			len;
	struct icx_dmp_board_id	*dmp;

	dmp = dev_get_drvdata(dev);
	if (dmp == NULL) {
		dev_err(dev, driver_not_init);
		return -ENODEV;
	}

	len = snprintf(buf, sizeof(unsigned long) * 3 + 2, "%lu\n",
		dmp->setid
	);
	return len;
}

static ssize_t icx_dmp_bid_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{	ssize_t			len;
	struct icx_dmp_board_id	*dmp;

	dmp = dev_get_drvdata(dev);
	if (dmp == NULL) {
		dev_err(dev, driver_not_init);
		return -ENODEV;
	}

	len = snprintf(buf, 2 + (sizeof(unsigned long) * 2) + 2,
		"0x%lx\n", dmp->bid
	);
	return len;
}

static ssize_t icx_dmp_sid0_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{	ssize_t			len;
	struct icx_dmp_board_id	*dmp;
	int			level;

	dmp = dev_get_drvdata(dev);
	if (dmp == NULL) {
		dev_err(dev, driver_not_init);
		return -ENODEV;
	}

	level = dmp->sid0;
	len = snprintf(buf, sizeof(int) * 3 + 2, "%d\n", level);

	return len;
}

static ssize_t icx_dmp_sid1_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{	ssize_t			len;
	struct icx_dmp_board_id	*dmp;
	int			level;

	dmp = dev_get_drvdata(dev);
	if (dmp == NULL) {
		dev_err(dev, driver_not_init);
		return -ENODEV;
	}

	level = dmp->sid1;
	len = snprintf(buf, sizeof(int) * 3 + 2, "%d\n", level);
	return len;
}

static DEVICE_ATTR(config, S_IRUGO, icx_dmp_config_show, NULL);
static DEVICE_ATTR(setid, S_IRUGO, icx_dmp_setid_show, NULL);

static DEVICE_ATTR(bid, S_IRUGO, icx_dmp_bid_show, NULL);
static DEVICE_ATTR(sid0, S_IRUGO, icx_dmp_sid0_show, NULL);
static DEVICE_ATTR(sid1, S_IRUGO, icx_dmp_sid1_show, NULL);

static struct attribute *icx_dmp_attrs[] = {
	&dev_attr_config.attr,
	&dev_attr_setid.attr,
	&dev_attr_bid.attr,
	&dev_attr_sid0.attr,
	&dev_attr_sid1.attr,
	NULL
};

static struct attribute_group icx_dmp_attrs_group = {
	.attrs = icx_dmp_attrs,
};

/* Set ID table for DMB BB, LF and later,
 */
static unsigned long icx_dmp_sid_table_bblf
	[ICX_DMP_PIN_LEVELS /* sid1 */][ICX_DMP_PIN_LEVELS /* sid0 */] = {
	/*           0,                     1,                     2 */
/* 0 */	{ICX_DMP_SETID_UNKNOWN, ICX_DMP_SETID_UNKNOWN, ICX_DMP_SETID_ICX_1293},
/* 1 */	{ICX_DMP_SETID_UNKNOWN, ICX_DMP_SETID_UNKNOWN, ICX_DMP_SETID_ICX_1295},
/* 2 */	{ICX_DMP_SETID_UNKNOWN, ICX_DMP_SETID_UNKNOWN, ICX_DMP_SETID_UNKNOWN}
};

static void icx_dmp_sid_decode(struct icx_dmp_board_id *dmp)
{	int	sid0;
	int	sid1;

	sid0 = dmp->sid0;
	sid1 = dmp->sid1;

	if ((sid0 < 0) || (sid0 > ICX_DMP_PIN_OPEN) ||
	    (sid1 < 0) || (sid1 > ICX_DMP_PIN_OPEN)
	) {
		dmp->setid = ICX_DMP_SETID_UNKNOWN;
		return;
	}

	dmp->setid = icx_dmp_sid_table_bblf[sid1][sid0];
}

static void icx_dmp_cd_ins_decode(struct icx_dmp_board_id *dmp)
{	if (dmp->setid == ICX_DMP_SETID_UNKNOWN) {
		dmp->cd_ins_level = ICX_DMP_CD_INS_LEVEL_DEFAULT;
		dmp->cd_ins_pull =  ICX_DMP_CD_INS_PULL_DEFAULT;
		return;
	}

	switch (dmp->bid) {
	case ICX_DMP_BID_UNKNOWN:
		dmp->cd_ins_pull = ICX_DMP_CD_INS_PULL_DEFAULT;
		break;
	case ICX_DMP_BID_BB:
		dmp->cd_ins_pull = ICX_DMP_CD_INS_PULL_UP;
		dmp->cd_ins_level = ICX_DMP_CD_INS_LEVEL_LOW;
		/* BB always use PullUp & Active Low condition. */
		return;
	default:
		/* Not BB. Board has proper CD_INS pull resistor. */
		dmp->cd_ins_pull = ICX_DMP_CD_INS_PULL_NONE;
		break;
	}

	/* LF and later set board. */
	switch (dmp->setid) {
	case ICX_DMP_SETID_ICX_1293:
		dmp->cd_ins_level = ICX_DMP_CD_INS_LEVEL_LOW;
		break;
	case ICX_DMP_SETID_ICX_1295:
		dmp->cd_ins_level = ICX_DMP_CD_INS_LEVEL_HIGH;
		break;
	default:
		/* Not BB, use defaule (same as EVK). */
		dmp->cd_ins_pull = ICX_DMP_CD_INS_PULL_NONE;
		break;
	}
}

#define	ICX_DMP_SID_DOWN_STABLE_MS	(5)

static int icx_dmp_read_sid01(struct icx_dmp_board_id *dmp)
{	int	ret;
	int	result = 0;
	int	level;

	struct gpio_desc	*pin;

	/* Remove pull up/down */
	ret = pinctrl_select_state(dmp->pinctrl, dmp->pinctrl_default);
	if (ret) {
		result = ret;
		dev_err(icx_dmp_board_id_dev(dmp),
			"%s.00: Can not set default pinctrl state. ret=%d\n",
			__func__, ret
		);
		goto out;
	}

	msleep(ICX_DMP_SID_DOWN_STABLE_MS);

	/* We may see that SID1 is open.
	 * Resistor connected to SID1 is weaker
	 * than internal pull resistors. So we don't check
	 * SID1 is open.
	 */

	pin = dmp->gpios->desc[ICX_DMP_ID_GPIOS_SID1];
	level = gpiod_get_value(pin);
	dmp->sid1 = level;
	if (level < 0) {
		result = level;
		dev_err(icx_dmp_board_id_dev(dmp),
			"dev, Can not read SID1 pin. level=%d\n",
			level
		);
		goto out;
	}

	/* Read SID0, it may be tri state. */
	dmp->sid0 = ICX_DMP_PIN_OPEN;

out:
	(void) pinctrl_select_state(dmp->pinctrl, dmp->pinctrl_default);
	/* Ignore Error. */
	return result;
}

static int icx_dmp_probe(struct platform_device *pdev)
{
	struct device *dev = &(pdev->dev);
	struct icx_dmp_board_id	*dmp = &icx_dmp_board_id;

	int	ret;
	int	init_result = ICX_DMP_INIT_DONE;
	bool	default_pinctrl_at_out = true;

	dmp->pdev = pdev;
	dmp->bid =  ICX_DMP_BID_UNKNOWN;
	dmp->sid0 = ICX_DMP_PIN_OPEN;
	dmp->sid1 = ICX_DMP_PIN_OPEN;
	dmp->sysfs_ret = -ENOENT;

	if (!device_property_read_bool(dev, "svs,icx-dmp")) {
		dev_warn(dev, "Configured non ICX DMP board.\n");
		init_result = -ENODEV;
		default_pinctrl_at_out = false;
		goto out;
	}

	dmp->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(dmp->pinctrl)) {
		ret = PTR_ERR(dmp->pinctrl);
		dev_err(dev, "Can not get pinctrl. ret=%d\n", ret);
		init_result = -ENODEV;
		default_pinctrl_at_out = false;
		goto out;
	}

	dmp->pinctrl_default = pinctrl_lookup_state(dmp->pinctrl,
		PINCTRL_STATE_DEFAULT
	);
	if (IS_ERR_OR_NULL(dmp->pinctrl_default)) {
		ret = PTR_ERR(dmp->pinctrl_default);
		dev_err(icx_dmp_board_id_dev(dmp),
			"Can not lookup default pin state. "
			"ret=%d\n",
			ret
		);
		init_result = PTR_ERR(dmp->pinctrl_default);
		default_pinctrl_at_out = false;
		goto out;
	}

	/* read "id-gpios" property */
	dmp->gpios = devm_gpiod_get_array(dev, "id", GPIOD_IN);
	if (IS_ERR_OR_NULL(dmp->gpios)) {
		ret = PTR_ERR(dmp->gpios);
		dev_err(dev, "Can not get DMP id-gpios. ret=%d\n", ret);
		init_result = PTR_ERR(dmp->gpios);
		default_pinctrl_at_out = false;
		goto out;
	}

	if (dmp->gpios->ndescs != ICX_DMP_ID_GPIOS_NUMS) {
		dev_err(dev, "The number of DMP id-gpios is invalid. "
			"ndescs=%u, expected=%u\n",
			dmp->gpios->ndescs, ICX_DMP_ID_GPIOS_NUMS
		);
		init_result = -EINVAL;
		default_pinctrl_at_out = false;
		goto out;
	}

	/* Now, we got pinctrl groups and GPIO descriptors,
	 * change pull up/down configuration and read GPIO.
	 */

	ret = icx_dmp_gpio_id_read(dmp,
		ICX_DMP_ID_GPIOS_BID_BASE,
		ICX_DMP_ID_GPIOS_BID_NUM, &(dmp->bid)
	);
	/* @note When we got an error, but we continue probe process. */
	if (ret != 0)
		init_result = -ENODEV;

	ret = icx_dmp_read_sid01(dmp);
	if (ret)
		init_result = ret;

	default_pinctrl_at_out = false;

	icx_dmp_sid_decode(dmp);
	icx_dmp_cd_ins_decode(dmp);
out:
	dev_set_drvdata(dev, dmp);
	ret = sysfs_create_group(&(dev->kobj), &icx_dmp_attrs_group);
	dmp->sysfs_ret = ret;
	/* @note we continue driver probe, if we can't create sysfs nodes. */
	if (ret != 0)
		dev_err(dev, "Can not create sysfs nodes. ret=%d\n", ret);


	if (default_pinctrl_at_out) {
		dev_warn(dev,
			"Set GPIO pins default state, "
			"it may open pin(s) without pull resistors.\n");
		pinctrl_select_state(dmp->pinctrl, dmp->pinctrl_default);
	}

	if (init_result != ICX_DMP_INIT_DONE)
		dmp->setid = ICX_DMP_SETID_UNKNOWN;

	atomic_set(&(dmp->init), init_result);

	dev_info(dev, "Board Info. "
		"bid=0x%lx, sid0=%d, sid1=%d, setid=%lu, config=0x%.16llx\n",
		dmp->bid,
		dmp->sid0,
		dmp->sid1,
		dmp->setid,
		dmp->config
	);
	/* Always success. */
	return 0;
}

static int icx_dmp_remove(struct platform_device *pdev)
{
	struct device	*dev = &(pdev->dev);
	struct icx_dmp_board_id	*dmp = dev_get_drvdata(dev);

	if (dmp->sysfs_ret == 0)
		sysfs_remove_group(&(dev->kobj), &icx_dmp_attrs_group);

	return 0;
}

static const struct of_device_id icx_dmp_board_id_of_match[] = {
	{ .compatible = "svs,icx-dmp-board-id", },
	{ },
};

MODULE_DEVICE_TABLE(of, icx_dmp_board_id_of_match);

static struct platform_driver icx_dmp_board_id_driver = {
	.probe		= icx_dmp_probe,
	.remove		= icx_dmp_remove,
	.driver		= {
		.name	= "icx_dmp_board_id",
		/* currently we doesn't need pm functions. */
		.of_match_table = icx_dmp_board_id_of_match,
	},
};
module_platform_driver(icx_dmp_board_id_driver);

MODULE_AUTHOR("Sony Video & Sound Products Inc.");
MODULE_DESCRIPTION("ICX DMP device");
MODULE_LICENSE("GPL");
