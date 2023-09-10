/*
 * CXD3778GF CODEC external i2c driver
 *
 * Copyright 2013, 2014, 2015, 2016, 2017, 2018 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "cxd3778gf_common.h"

#define DEVCHECK_ADDRESS 0x02
#define GAIN_SET_ADDRESS 0xDC
#define FW_MAJOR_ADDRESS 0xF6
#define FW_MINOR_ADDRESS 0xF7
#define CONTROL_ADDRESS  0xFB

#define DEVCHECK_FLAG_DEFAULT  0x00
#define DEVCHECK_FLAG_OK       0x01
#define DEVCHECK_FLAG_I2C_ERR  0x10
#define DEVCHECK_FLAG_CLIP     0x20
#define DEVCHECK_FLAG_BUSY     0x80

#define DEVCHECK_INTERVAL_MS 500

#define CONTROL_FB_MONITOR_ON 0x02

#define AIN1_PREAMP_MASK 0x03

#define AIN_PREAMP_0  0
#define AIN_PREAMP_6  1
#define AIN_PREAMP_9  2
#define AIN_PREAMP_12 3

static struct i2c_client *i2c_client;

struct cxd3778gf_ext_data {
	struct task_struct *thread;
	struct device *dev;
	struct mutex lock;
	bool   preamp_restored;
	int ucon_xfwupdate;
	int ucon_req;
	int ucon_i2c_xmaster;
	int ucon_xreset;
	int da_xrst;
};

static int cxd3778gf_ext_hwinit(struct cxd3778gf_ext_data *data)
{
	gpio_direction_output(data->ucon_i2c_xmaster, 1);
	gpio_direction_output(data->ucon_req, 0);
	gpio_direction_output(data->ucon_xreset, 0);
	gpio_direction_output(data->ucon_xfwupdate, 1);

	usleep_range(10000, 11000);
	gpio_set_value(data->ucon_req, 1);
	usleep_range(10000, 11000);
	gpio_set_value(data->ucon_xreset, 1);
	usleep_range(10000, 11000);
	gpio_set_value(data->ucon_req, 0);

	return 0;
}

static void cxd3778gf_ext_enable_bus(struct cxd3778gf_ext_data *data, int enable)
{
	if (enable)
		gpio_set_value(data->ucon_i2c_xmaster, 0);
	else
		gpio_set_value(data->ucon_i2c_xmaster, 1);

	return;
}

static void cxd3778gf_ext_reset_external(struct cxd3778gf_ext_data *data)
{
	gpio_set_value(data->ucon_xreset, 0);
	usleep_range(10000, 11000);
	gpio_set_value(data->ucon_xreset, 1);

	return;
}

static void cxd3778gf_ext_force_disable(struct cxd3778gf_ext_data *data)
{
	gpio_set_value(data->da_xrst, 0);
	return;
}

static int cxd3778gf_ext_device_error_flag_parse(int value)
{
	int ret = DEVCHECK_FLAG_OK;

	if (value & DEVCHECK_FLAG_BUSY) {
		ret = DEVCHECK_FLAG_BUSY;
		return ret;
	}
	if (value & DEVCHECK_FLAG_I2C_ERR) {
		ret = DEVCHECK_FLAG_I2C_ERR;
		return ret;
	}
	if (value & DEVCHECK_FLAG_CLIP) {
		ret = DEVCHECK_FLAG_CLIP;
		return ret;
	}
	if (value & DEVCHECK_FLAG_OK) {
		ret = DEVCHECK_FLAG_OK;
		return ret;
	}

	return ret;
}

static void cxd3778gf_ext_force_shutdown(struct device *dev)
{
	struct cxd3778gf_ext_data *data;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}

	data = dev_get_drvdata(&i2c_client->dev);

	dev_err(dev, "%s: Fatal error happened. Force shutdown\n",
							__func__);
	cxd3778gf_ext_force_disable(data);

	cxd3778gf_extcon_set_ucom_value(UCOM_EXTCON_DEVCHKERR);

	return;
}

static void cxd3778gf_ext_change_preamp_gain(struct device *dev)
{
	int ret;

	struct cxd3778gf_ext_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->lock);

	if (data->preamp_restored) {
		dev_dbg(dev, "%s: Skip, AIN PreAMP was changed once and restored\n",
								      __func__);
		goto out;
	}

	ret = cxd3778gf_register_modify(CXD3778GF_AIN_PREAMP,
					AIN_PREAMP_0, AIN1_PREAMP_MASK);
	if (ret) {
		dev_err(dev, "%s: Failed to set AIN PreAMP %d\n",
						   __func__, ret);
		goto out;
	}

	/* Release Monvol mute by ucom */
	ret = cxd3778gf_register_write(CONTROL_ADDRESS, 0x07);
	if (ret) {
		dev_err(dev, "%s: Failed to send command to ucom %d\n",
						    __func__, ret);
		goto out;
	}

	dev_dbg(dev, "%s: AIN PreAMP gain has been changed to low\n", __func__);

out:
	mutex_unlock(&data->lock);

	return;
}

static int cxd3778gf_ext_device_check(void *ddata)
{
	int ret;
	int value;
	int prev_value = -1;
	struct cxd3778gf_ext_data *data = (struct cxd3778gf_ext_data *)ddata;

	dev_info(&i2c_client->dev, "%s: start\n", __func__);

	ret = cxd3778gf_register_use_ext_bus(true);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus (%d)\n",
								__func__, ret);
		cxd3778gf_ext_force_shutdown(data->dev);
		return ret;
	}

	while (1) {
		msleep_interruptible(DEVCHECK_INTERVAL_MS);

		if (kthread_should_stop())
			break;

		ret = cxd3778gf_register_read(DEVCHECK_ADDRESS, &value);
		if (ret) {
			dev_err(data->dev, "%s: i2c error\n", __func__);
			cxd3778gf_ext_force_shutdown(data->dev);
			break;
		}

		dev_dbg(data->dev, "%s: Thread is running, value = %d\n",
							  __func__, value);

		if (value == prev_value) {
			dev_err(data->dev, "%s: Register toggle error\n",
								 __func__);
			cxd3778gf_ext_force_shutdown(data->dev);
			break;
		} else {
			prev_value = value;
		}

		if (DEVCHECK_FLAG_DEFAULT == value)
			continue;

		switch (cxd3778gf_ext_device_error_flag_parse(value)) {
		case DEVCHECK_FLAG_OK:
			break;
		case DEVCHECK_FLAG_I2C_ERR:
			dev_err(data->dev, "%s: ucon <-> cxd3778gf i2c connection error\n",
				__func__);
			cxd3778gf_ext_force_shutdown(data->dev);
			goto out;
		case DEVCHECK_FLAG_CLIP:
			dev_notice(data->dev, "%s: Detect low tone clip\n",
								  __func__);
			cxd3778gf_ext_change_preamp_gain(data->dev);
			break;
		case DEVCHECK_FLAG_BUSY:
			dev_warn(data->dev, "%s: Sound buffering busy\n",
								__func__);
			break;
		default:
			break;
		}
	}

out:
	ret = cxd3778gf_register_use_ext_bus(false);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus (%d)\n",
								__func__, ret);
		cxd3778gf_ext_force_shutdown(data->dev);
	}

	data->thread = NULL;
	dev_info(&i2c_client->dev, "%s: leave\n", __func__);

	return ret;
}

/**
 * cxd3778gf_ext_enable_i2c_bus - enable / disable external i2c bus.
 * @enable: set 1 to enable or set 0 to disable
 */
void cxd3778gf_ext_enable_i2c_bus(int enable)
{
	struct cxd3778gf_ext_data *data;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}
	data = dev_get_drvdata(&i2c_client->dev);

	dev_dbg(&i2c_client->dev, "%s: %d\n", __func__, enable);

	cxd3778gf_ext_enable_bus(data, enable);

	usleep_range(10000, 11000);

	return;
}

/**
 * cxd3778gf_ext_reset - reset external microcomputer.
 */
void cxd3778gf_ext_reset(void)
{
	struct cxd3778gf_ext_data *data;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}
	data = dev_get_drvdata(&i2c_client->dev);

	dev_dbg(&i2c_client->dev, "%s\n", __func__);

	cxd3778gf_ext_reset_external(data);

	usleep_range(10000, 11000);

	return;
}

/**
 * cxd3778gf_ext_start_fmonitor - start feedback monitoring.
 */
void cxd3778gf_ext_start_fmonitor(void)
{
	int ret;
	struct cxd3778gf_ext_data *data;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}

	data = dev_get_drvdata(&i2c_client->dev);

	mutex_lock(&data->lock);

	if (data->thread) {
		dev_warn(&i2c_client->dev, "%s: Device check thread already exists\n",
								      __func__);
		goto out_mutex;
	}

	ret = cxd3778gf_register_use_ext_bus(true);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus (%d)\n",
								__func__, ret);
		goto out_mutex;
	}

	ret = cxd3778gf_register_write(CONTROL_ADDRESS, 0x02);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to start feedback monitoring (%d)\n",
								__func__, ret);
		cxd3778gf_ext_force_shutdown(data->dev);
		goto out;
	}

	data->thread = kthread_run(cxd3778gf_ext_device_check, (void *)data,
						"cxd3778gf_ext_thread");
	if (IS_ERR(data->thread))
		dev_err(&i2c_client->dev, "%s: failed to create thread %ld\n",
					  __func__, PTR_ERR(data->thread));

out:
	ret = cxd3778gf_register_use_ext_bus(false);
out_mutex:
	mutex_unlock(&data->lock);

	return;
}

/**
 * cxd3778gf_ext_stop_fmonitor - stop feedback monitoring.
 */
void cxd3778gf_ext_stop_fmonitor(void)
{
	int ret;
	struct cxd3778gf_ext_data *data;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}

	data = dev_get_drvdata(&i2c_client->dev);

	mutex_lock(&data->lock);

	ret = cxd3778gf_register_use_ext_bus(true);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus (%d)\n",
								__func__, ret);
		goto out;
	}

	if (NULL != data->thread) {
		kthread_stop(data->thread);
		data->thread = NULL;
	}

	data->preamp_restored = false;

	ret = cxd3778gf_register_write(CONTROL_ADDRESS, 0x01);
	if (ret)
		dev_err(&i2c_client->dev, "%s: Fail to stop feedback monitoring (%d)\n",
								__func__, ret);

	ret = cxd3778gf_register_use_ext_bus(false);

out:
	mutex_unlock(&data->lock);

	return;
}

/**
 * cxd3778gf_ext_set_gain_index - set user ambient gain index value to ucom.
 */
void cxd3778gf_ext_set_gain_index(int index)
{
	int ret;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}

	if (USER_DNC_GAIN_INDEX_MAX < index)
		index = USER_DNC_GAIN_INDEX_MAX;

	ret = cxd3778gf_register_use_ext_bus(true);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus %d\n",
								__func__, ret);
		return;
	}

	ret = cxd3778gf_register_write(GAIN_SET_ADDRESS, index);
	if (ret)
		dev_err(&i2c_client->dev, "%s: Fail to set gain index %d\n",
								__func__, ret);

	ret = cxd3778gf_register_use_ext_bus(false);
	if (ret)
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus %d\n",
								__func__, ret);

	return;
}

void cxd3778gf_ext_restore_preamp(void)
{
	int ret;
	unsigned int buf = 0;
	struct cxd3778gf_ext_data *data;

	if (!i2c_client) {
		pr_err("%s: i2c_client not registered\n", __func__);
		return;
	}

	data = dev_get_drvdata(&i2c_client->dev);

	mutex_lock(&data->lock);

	ret = cxd3778gf_register_read(CXD3778GF_AIN_PREAMP, &buf);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to read PreAMP setting %d\n",
								__func__, ret);
		goto out_mutex;
	}

	if (AIN_PREAMP_6 == (buf & AIN1_PREAMP_MASK)) {
		dev_dbg(&i2c_client->dev, "%s: Skip restoring preamp\n",
							       __func__);
		goto out_mutex;
	}

	ret = cxd3778gf_register_use_ext_bus(true);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus %d\n",
								__func__, ret);
		goto out_mutex;
	}

	/* Set Monvol mute by ucom */
	ret = cxd3778gf_register_write(CONTROL_ADDRESS, 0x06);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Failed to send command to ucom %d\n",
								__func__, ret);
		goto out;
	}

	ret = cxd3778gf_register_modify(CXD3778GF_AIN_PREAMP,
					AIN_PREAMP_6, AIN1_PREAMP_MASK);
	if (ret) {
		dev_err(&i2c_client->dev, "%s: Failed to set AIN PreAMP %d\n",
								__func__, ret);
		goto out;
	}

	data->preamp_restored = true;
	dev_dbg(&i2c_client->dev, "%s: AIN PreAMP successfully restored\n",
								  __func__);

out:
	ret = cxd3778gf_register_use_ext_bus(false);
	if (ret)
		dev_err(&i2c_client->dev, "%s: Fail to switch i2c bus %d\n",
								__func__, ret);

out_mutex:
	mutex_unlock(&data->lock);

	return;
}

static ssize_t attr_fw_version_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	int ret;
	unsigned int major = 0;
	unsigned int minor = 0;
	unsigned int value = 0;
	struct cxd3778gf_ext_data *data = dev_get_drvdata(dev);

	dev_info(dev, "Check fw version\n");

	mutex_lock(&data->lock);

	ret = cxd3778gf_register_use_ext_bus(true);
	if (ret) {
		dev_err(dev, "%s: Fail to switch i2c bus (%d)\n",
						   __func__, ret);
		mutex_unlock(&data->lock);
		return ret;
	}

	ret = cxd3778gf_register_read(CONTROL_ADDRESS, &value);
	if (ret)
		dev_info(dev, "%s: ucom may be in deep sleep\n", __func__);


	if (CONTROL_FB_MONITOR_ON != value) {
		/* Once switch bus for reset */
		ret = cxd3778gf_register_use_ext_bus(false);
		if (ret) {
			dev_err(dev, "%s: Fail to switch i2c bus (%d)\n",
						   __func__, ret);
			mutex_unlock(&data->lock);
			return ret;
		}

		cxd3778gf_ext_reset();

		ret = cxd3778gf_register_use_ext_bus(true);
		if (ret) {
			dev_err(dev, "%s: Fail to switch i2c bus (%d)\n",
						   __func__, ret);
			mutex_unlock(&data->lock);
			return ret;
		}
	} else {
		dev_dbg(dev, "%s: skip ucom reset\n", __func__);
	}

	ret = cxd3778gf_register_read(FW_MAJOR_ADDRESS, &major);
	if (ret) {
		dev_err(dev, "%s: Fail to read FW major version (%d)\n",
							  __func__, ret);
		goto err;
	}

	ret = cxd3778gf_register_read(FW_MINOR_ADDRESS, &minor);
	if (ret) {
		dev_err(dev, "%s: Fail to read FW minor version (%d)\n",
							  __func__, ret);
		goto err;
	}

	/* Set deep sleep if feedback monitoring is not running */
	if (CONTROL_FB_MONITOR_ON != value) {
		ret = cxd3778gf_register_write(CONTROL_ADDRESS, 0x01);
		if (ret)
			dev_err(&i2c_client->dev, "%s: Fail to enter deep sleep (%d)\n",
								__func__, ret);
	} else {
		dev_dbg(dev, "%s: skip ucom sleep\n", __func__);
	}

	ret = cxd3778gf_register_use_ext_bus(false);
	mutex_unlock(&data->lock);

	dev_info(dev, "FW version: %02x.%02x\n", major, minor);

	return snprintf(buf, PAGE_SIZE, "%02x_%02x\n", major, minor);

err:
	ret = cxd3778gf_register_use_ext_bus(false);
	mutex_unlock(&data->lock);

	return snprintf(buf, PAGE_SIZE, "00_00\n");
}

static struct device_attribute cxd3778gf_ext_attr =
	__ATTR(fw_version, S_IRUSR, attr_fw_version_show, NULL);

static int cxd3778gf_ext_i2c_suspend(struct device *dev)
{
	dev_dbg(dev, "%s()\n", __func__);
	return 0;
}

static int cxd3778gf_ext_i2c_resume(struct device *dev)
{
	dev_dbg(dev, "%s()\n", __func__);
	return 0;
}

static int cxd3778gf_ext_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret;
	struct cxd3778gf_ext_data *data;
	struct device *dev;
	struct device_node *np;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	dev = &(client->dev);
	np = dev->of_node;

	data->ucon_xfwupdate = of_get_named_gpio(np, "ucon_xfwupdate-gpio", 0);
	ret = gpio_is_valid(data->ucon_xfwupdate);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_xfwupdate get failed, ret = %d\n",
			__func__, ret);
		goto err;
	}

	data->ucon_req = of_get_named_gpio(np, "ucon_req-gpio", 0);
	ret = gpio_is_valid(data->ucon_req);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_req get failed, ret = %d\n",
				__func__, ret);
		goto err;
	}

	data->ucon_xreset = of_get_named_gpio(np, "ucon_xreset-gpio", 0);
	ret = gpio_is_valid(data->ucon_xreset);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_xreset get failed, ret = %d\n",
				__func__, ret);
		goto err;
	}

	data->ucon_i2c_xmaster = of_get_named_gpio(np, "ucon_i2c_xmaster-gpio",0);
	ret = gpio_is_valid(data->ucon_i2c_xmaster);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_i2c_xmaster get failed, ret = %d\n",
				__func__, ret);
		goto err;
	}

	data->da_xrst = of_get_named_gpio(np, "da_xrst-gpio",0);
	ret = gpio_is_valid(data->da_xrst);
	if (ret < 0) {
		dev_err(&client->dev, "%s: da_xrst get failed, ret = %d\n",
				__func__, ret);
		goto err;
	}

	ret = cxd3778gf_ext_hwinit(data);
	if (ret) {
		dev_err(&client->dev, "%s hwinit failed, ret = %d\n", __func__,
									   ret);
		ret = -ENODEV;
		goto err;
	}
	dev_dbg(&client->dev, "%s hwinit done\n", __func__);

	data->dev = &client->dev;

	dev_set_drvdata(&client->dev, data);

	ret = device_create_file(&client->dev, &cxd3778gf_ext_attr);
	if (ret) {
		dev_err(&client->dev, "%s: Unable to create interface\n",
								__func__);
		goto err;
	}

	ret = cxd3778gf_ext_register_initialize(client);
	if (ret) {
		dev_err(&client->dev, "%s: Failed to initialize ext i2c client\n",
								__func__);
		goto err_attr;
	}

	mutex_init(&data->lock);
	data->preamp_restored = false;
	i2c_client = client;

	cxd3778gf_ext_enable_i2c_bus(0);

	return 0;

err_attr:
	device_remove_file(&client->dev, &cxd3778gf_ext_attr);
err:
	return ret;
}

static int cxd3778gf_ext_i2c_remove(struct i2c_client *client)
{
	int ret;
	unsigned int val = 0;

	dev_dbg(&client->dev, "%s()\n", __func__);

	device_remove_file(&client->dev, &cxd3778gf_ext_attr);

	/* Force disable Ambient since microcomputer will not be available */
	ret = cxd3778gf_get_noise_cancel_status(&val);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Failed to get noise cancel mode\n",
								     __func__);
	} else if (AMBIENT == val) {
		dev_info(&client->dev, "%s: Force disable Ambient mode\n",
								 __func__);
		cxd3778gf_put_noise_cancel_mode(NOISE_CANCEL_MODE_OFF);
		cxd3778gf_ext_enable_i2c_bus(0);
	}

	ret = cxd3778gf_ext_register_finalize();

	return ret;
}

static void cxd3778gf_ext_i2c_poweroff(struct i2c_client *client)
{
	int ret;
	unsigned int val = 0;

	dev_dbg(&client->dev, "%s()\n", __func__);

	device_remove_file(&client->dev, &cxd3778gf_ext_attr);

	/* Force disable Ambient since microcomputer will not be available */
	ret = cxd3778gf_get_noise_cancel_status(&val);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Failed to get noise cancel mode\n",
								     __func__);
	} else if (AMBIENT == val) {
		dev_info(&client->dev, "%s: Force disable Ambient mode\n",
								 __func__);
		cxd3778gf_put_noise_cancel_mode(NOISE_CANCEL_MODE_OFF);
		cxd3778gf_ext_enable_i2c_bus(0);
	}

	ret = cxd3778gf_ext_register_finalize();

	return;
}

static const struct i2c_device_id cxd3778gf_ext_id[] = {
	{CXD3778GF_EXT_DEVICE_NAME, 0},
	{}
};

static const struct of_device_id cxd3778gf_ext_dt_ids[] = {
	{ .compatible = "sony,cxd3778gf_ext", },
	{ }
};
MODULE_DEVICE_TABLE(of, cxd3778gf_ext_dt_ids);

static const struct dev_pm_ops cxd3778gf_ext_i2c_pm_ops = {
	.suspend = cxd3778gf_ext_i2c_suspend,
	.resume  = cxd3778gf_ext_i2c_resume,
};

static struct i2c_driver cxd3778gf_ext_i2c_driver = {
	.driver = {
		.name  = CXD3778GF_EXT_DEVICE_NAME,
		.owner = THIS_MODULE,
		.pm    = &cxd3778gf_ext_i2c_pm_ops,
		.of_match_table = of_match_ptr(cxd3778gf_ext_dt_ids),
	},
	.id_table = cxd3778gf_ext_id,
	.probe    = cxd3778gf_ext_i2c_probe,
	.remove   = cxd3778gf_ext_i2c_remove,
	.shutdown = cxd3778gf_ext_i2c_poweroff,
};

static int __init cxd3778gf_ext_init(void)
{
	int ret;

	ret = i2c_add_driver(&cxd3778gf_ext_i2c_driver);
	if (ret)  {
		pr_err("i2c_add_driver(): code %d error occurred\n", ret);
		back_trace();
	}

	return ret;
}

static void __exit cxd3778gf_ext_exit(void)
{
	i2c_del_driver(&cxd3778gf_ext_i2c_driver);

	return;
}

late_initcall(cxd3778gf_ext_init);
module_exit(cxd3778gf_ext_exit);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("CXD3778GF CODEC Ext driver");
MODULE_LICENSE("GPL");
