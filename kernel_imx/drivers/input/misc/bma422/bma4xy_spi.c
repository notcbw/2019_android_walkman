/*!
 * @section LICENSE
 * (C) Copyright 2011~2016 Bosch Sensortec GmbH All Rights Reserved
 *
 * This software program is licensed subject to the GNU General
 * Public License (GPL).Version 2,June 1991,
 * available at http://www.fsf.org/copyleft/gpl.html
 *
 * @filename bma4xy_spi.c
 * @date     2016/03/23 13:44
 * @id       "2b07039"
 * @version  0.2.4
 *
 * @brief    bma4xy SPI bus Driver
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "bma4xy_driver.h"
#include "bs_log.h"

#define BMA4XY_MAX_BUFFER_SIZE      32

static struct spi_device *bma4xy_spi_client;

static s8 bma4xy_spi_write_block(uint8_t dev_addr,
	uint8_t reg_addr, uint8_t *data, uint8_t len)
{
	struct spi_device *client = bma4xy_spi_client;
	uint8_t buffer[BMA4XY_MAX_BUFFER_SIZE + 1];
	struct spi_transfer xfer = {
		.tx_buf = buffer,
		.len = len + 1,
	};
	struct spi_message msg;

	if (len > BMA4XY_MAX_BUFFER_SIZE)
		return -EINVAL;

	buffer[0] = reg_addr&0x7F;/* write: MSB = 0 */
	memcpy(&buffer[1], data, len);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	return spi_sync(client, &msg);
}

static s8 bma4xy_spi_read_block(uint8_t dev_addr,
	uint8_t reg_addr, uint8_t *data, uint16_t len)
{
	struct spi_device *client = bma4xy_spi_client;
	u8 reg = reg_addr | 0x80;/* read: MSB = 1 */
	struct spi_transfer xfer[2] = {
		[0] = {
			.tx_buf = &reg,
			.len = 1,
		},
		[1] = {
			.rx_buf = data,
			.len = len,
		}
	};
	struct spi_message msg;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer[0], &msg);
	spi_message_add_tail(&xfer[1], &msg);
	return spi_sync(client, &msg);
}

int bma4xy_spi_write_config_stream(uint8_t dev_addr,
	uint8_t reg_addr, const uint8_t *data, uint8_t len)
{
	struct spi_device *client = bma4xy_spi_client;
	uint8_t buffer[BMA4XY_MAX_BUFFER_SIZE + 1];
	struct spi_transfer xfer = {
		.tx_buf = buffer,
		.len = len + 1,
	};
	struct spi_message msg;

	if (len > BMA4XY_MAX_BUFFER_SIZE)
		return -EINVAL;

	buffer[0] = reg_addr&0x7F;/* write: MSB = 0 */
	memcpy(&buffer[1], data, len);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	return spi_sync(client, &msg);
}

int bma4xy_write_config_stream(uint8_t dev_addr,
	uint8_t reg_addr, const uint8_t *data, uint8_t len)
{
	int err;
	err = bma4xy_spi_write_config_stream(dev_addr, reg_addr, data, len);
	return err;
}

static int bma4xy_spi_probe(struct spi_device *client)
{
	int status;
	int err = 0;
	struct bma4xy_client_data *client_data = NULL;

	if (NULL == bma4xy_spi_client)
		bma4xy_spi_client = client;
	else{
		PERR("This driver does not support multiple clients!");
		return -EBUSY;
	}
	client->bits_per_word = 8;
	status = spi_setup(client);
	if (status < 0) {
		PERR("spi_setup failed!");
		return status;
	}
	client_data = kzalloc(sizeof(struct bma4xy_client_data), GFP_KERNEL);
	if (NULL == client_data) {
		PERR("no memory available");
		err = -ENOMEM;
		goto exit_err_clean;
	}

	client_data->device.bus_read = bma4xy_spi_read_block;
	client_data->device.bus_write = bma4xy_spi_write_block;

	return bma4xy_probe(client_data, &client->dev);

exit_err_clean:
	if (err)
		bma4xy_spi_client = NULL;
	return err;
}

static int bma4xy_spi_remove(struct spi_device *client)
{
	int err = 0;
	err = bma4xy_remove(&client->dev);
	bma4xy_spi_client = NULL;
	return err;
}

static int bma4xy_spi_suspend(struct spi_device *client, pm_message_t mesg)
{
	int err = 0;
	err = bma4xy_suspend(&client->dev);
	return err;
}

static int bma4xy_spi_resume(struct spi_device *client)
{
	int err = 0;
	err = bma4xy_resume(&client->dev);
	return err;
}

static const struct spi_device_id bma4xy_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(spi, bma4xy_id);
static const struct of_device_id bma4xy_of_match[] = {
	{ .compatible = "bma4xy", },
	{ }
};

MODULE_DEVICE_TABLE(spi, bma4xy_of_match);

static struct spi_driver bmi_spi_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name  = SENSOR_NAME,
		.of_match_table = bma4xy_of_match,
	},
	.id_table = bma4xy_id,
	.probe    = bma4xy_spi_probe,
	.remove   = bma4xy_spi_remove,
	.suspend = bma4xy_spi_suspend,
	.resume = bma4xy_spi_resume,
};

static int __init bmi_spi_init(void)
{
	return spi_register_driver(&bmi_spi_driver);
}

static void __exit bmi_spi_exit(void)
{
	spi_unregister_driver(&bmi_spi_driver);
}


MODULE_AUTHOR("Contact <contact@bosch-sensortec.com>");
MODULE_DESCRIPTION("BMA4XY SPI DRIVER");
MODULE_LICENSE("GPL v2");

module_init(bmi_spi_init);
module_exit(bmi_spi_exit);

