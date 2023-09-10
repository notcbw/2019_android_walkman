/*!
 * @section LICENSE
 * Copyright 2018 Sony Video & Sound Products Inc.
 * (C) Copyright 2011~2016 Bosch Sensortec GmbH All Rights Reserved
 *
 * This software program is licensed subject to the GNU General
 * Public License (GPL).Version 2,June 1991,
 * available at http://www.fsf.org/copyleft/gpl.html
 *
 * @filename bma4xy_i2c.c
 * @date     2016/10/19 13:44
 * @id       "2b07039"
 * @version  0.2.4
 *
 * @brief    bma4xy I2C bus Driver
 */


#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/input.h>
#include "bma4xy_driver.h"
#include "bs_log.h"

static struct i2c_client *bma4xy_i2c_client;
static int bma4xy_i2c_read(struct i2c_client *client,
	uint8_t reg_addr, uint8_t *data, uint16_t len)
{
	int32_t retry;

	struct i2c_msg msg[] = {
		{
		.addr = client->addr,
		.flags = 0,
		.len = 1,
		.buf = &reg_addr,
		},

		{
		.addr = client->addr,
		.flags = I2C_M_RD,
		.len = len,
		.buf = data,
		},
	};
	for (retry = 0; retry < BMA4XY_MAX_RETRY_I2C_XFER; retry++) {
		if (i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)) > 0)
			break;
		else
			usleep_range(BMA4XY_I2C_WRITE_DELAY_TIME * 1000,
				BMA4XY_I2C_WRITE_DELAY_TIME * 1000);
	}

	if (BMA4XY_MAX_RETRY_I2C_XFER <= retry) {
		PERR("I2C xfer error");
		return -EIO;
	}

	return 0;
}

static int bma4xy_i2c_write(struct i2c_client *client,
	uint8_t reg_addr, uint8_t *data, uint16_t len)
{
	int32_t retry;

	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = len + 1,
		.buf = NULL,
	};
	msg.buf = kmalloc(len + 1, GFP_KERNEL);
	if (!msg.buf) {
		PERR("Allocate mem failed");
		return -ENOMEM;
	}
	msg.buf[0] = reg_addr;
	memcpy(&msg.buf[1], data, len);
	for (retry = 0; retry < BMA4XY_MAX_RETRY_I2C_XFER; retry++) {
		if (i2c_transfer(client->adapter, &msg, 1) > 0)
			break;
		else
			usleep_range(BMA4XY_I2C_WRITE_DELAY_TIME * 1000,
				BMA4XY_I2C_WRITE_DELAY_TIME * 1000);
	}
	kfree(msg.buf);
	if (BMA4XY_MAX_RETRY_I2C_XFER <= retry) {
		PERR("I2C xfer error");
		return -EIO;
	}

	return 0;
}

int bma4xy_i2c_write_config_stream(struct i2c_client *client,
	uint8_t reg_addr, const uint8_t *data, uint16_t len)
{
	int32_t retry;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = len + 1,
		.buf = NULL,
	};

	msg.buf = kmalloc(len + 1, GFP_KERNEL);
	if (!msg.buf) {
		PERR("Allocate mem failed");
		return -ENOMEM;
	}
	msg.buf[0] = reg_addr;
	memcpy(&msg.buf[1], data, len);
	for (retry = 0; retry < BMA4XY_MAX_RETRY_I2C_XFER; retry++) {
		if (i2c_transfer(client->adapter, &msg, 1) > 0)
			break;
		else
			usleep_range(BMA4XY_I2C_WRITE_DELAY_TIME * 1000,
				BMA4XY_I2C_WRITE_DELAY_TIME * 1000);
	}
	kfree(msg.buf);
	if (BMA4XY_MAX_RETRY_I2C_XFER <= retry) {
		PERR("I2C xfer error");
		return -EIO;
	}

	return 0;
}

static s8 bma4xy_i2c_read_wrapper(uint8_t dev_addr,
	uint8_t reg_addr, uint8_t *data, uint16_t len)
{
	int err;
	err = bma4xy_i2c_read(bma4xy_i2c_client, reg_addr, data, len);
	return err;
}

static s8 bma4xy_i2c_write_wrapper(uint8_t dev_addr,
	uint8_t reg_addr, uint8_t *data, uint16_t len)
{
	int err;
	err = bma4xy_i2c_write(bma4xy_i2c_client, reg_addr, data, len);
	return err;
}

int bma4xy_write_config_stream(uint8_t dev_addr,
	uint8_t reg_addr, const uint8_t *data, uint8_t len)
{
	int err;
	err = bma4xy_i2c_write_config_stream(
		bma4xy_i2c_client, reg_addr, data, len);
	return err;
}
static int bma4xy_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int err = 0;
	struct bma4xy_client_data *client_data = NULL;

	PDEBUG("entrance");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality error!");
		err = -EIO;
		goto exit_err_clean;
	}

	if (NULL == bma4xy_i2c_client) {
		bma4xy_i2c_client = client;
	} else {
		err = -EBUSY;
		if (bma4xy_i2c_client->addr == client->addr) {
			PERR("this driver does not support multiple clients");
			goto exit_err_clean;
		} else {
			PERR("driver with other address has already be loaded");
			goto exit_err;
		}
	}

	client_data = kzalloc(sizeof(struct bma4xy_client_data),
						GFP_KERNEL);
	if (NULL == client_data) {
		PERR("no memory available");
		err = -ENOMEM;
		goto exit_err_clean;
	}
	/* h/w init */
	client_data->device.bus_read = bma4xy_i2c_read_wrapper;
	client_data->device.bus_write = bma4xy_i2c_write_wrapper;
	err = bma4xy_probe(client_data, &client->dev);
	if (err)
		goto exit_err_clean;

	return 0;

exit_err_clean:
	if (err)
		bma4xy_i2c_client = NULL;
exit_err:
	return err;
}

static int bma4xy_i2c_suspend(struct device *dev, pm_message_t mesg)
{
	int err = 0;
	err = bma4xy_suspend(dev);
	return err;
}

static int bma4xy_i2c_resume(struct device *dev)
{
	int err = 0;
	err = bma4xy_resume(dev);
	return err;
}

static int bma4xy_i2c_remove(struct i2c_client *client)
{
	int err = 0;
	err = bma4xy_remove(&client->dev);
	bma4xy_i2c_client = NULL;
	return err;
}

static const struct i2c_device_id bma4xy_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma4xy_id);
static const struct of_device_id bma4xy_of_match[] = {
	{ .compatible = "bosch,bma4xy_18", },
	{ .compatible = "bosch,bma4xy_19", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bma4xy_of_match);

static struct i2c_driver bma4xy_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = SENSOR_NAME,
		.of_match_table = bma4xy_of_match,
		.suspend = bma4xy_i2c_suspend,
		.resume = bma4xy_i2c_resume,
	},
	.class = I2C_CLASS_HWMON,
	.id_table = bma4xy_id,
	.probe = bma4xy_i2c_probe,
	.remove = bma4xy_i2c_remove,
};

static int __init BMA4xy_init(void)
{
	return i2c_add_driver(&bma4xy_driver);
}

static void __exit BMA4xy_exit(void)
{
	i2c_del_driver(&bma4xy_driver);
}

MODULE_AUTHOR("contact@bosch-sensortec.com>");
MODULE_DESCRIPTION("BMA4XY SENSOR DRIVER");
MODULE_LICENSE("GPL v2");

module_init(BMA4xy_init);
module_exit(BMA4xy_exit);
