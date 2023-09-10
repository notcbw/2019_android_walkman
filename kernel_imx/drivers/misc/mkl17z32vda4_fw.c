/*
 * MKL17Z32VDA4 ucom firmware update i2c driver
 *
 * Copyright 2016, 2017, 2018 Sony Corporation
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

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#define MAX_RETRY 20
#define MAX_FW_SIZE 33000

#define FRAMING_PACKET_SIZE 6
#define COMMAND_HEADER_SIZE 4
#define PARAMETER_OFFSET (FRAMING_PACKET_SIZE + COMMAND_HEADER_SIZE)
#define MAX_BYTE_PER_PACKET 32
#define MAX_BYTE_PER_RESPONSE 32
#define MAX_COMMAND_PACKET_LEN 32

#define START_ADDRESS 0x00000000

#define TYPE_START_BYTE 0x5A
#define TYPE_ACK 0xA1
#define TYPE_NAK 0xA2


#define log_command_packet(value, size)					\
	do {								\
		print_hex_dump_debug("mkl17z32vda4_fw: framing packet ",\
				DUMP_PREFIX_NONE, 16, 1,		\
				value,					\
				FRAMING_PACKET_SIZE,			\
				false);					\
		print_hex_dump_debug("mkl17z32vda4_fw: command header ",\
				DUMP_PREFIX_NONE, 16, 1,		\
				&value[FRAMING_PACKET_SIZE],		\
				COMMAND_HEADER_SIZE,			\
				false);					\
		print_hex_dump_debug("mkl17z32vda4_fw: parameter ",	\
				DUMP_PREFIX_NONE, 16, 1,		\
				&value[PARAMETER_OFFSET],		\
				size - PARAMETER_OFFSET,		\
				false);					\
	} while (0)

#define log_data_packet(value, size)					\
	do {								\
		print_hex_dump_debug("mkl17z32vda4_fw: framing packet ",\
				DUMP_PREFIX_NONE, 16, 1,		\
				value,					\
				FRAMING_PACKET_SIZE,			\
				false);					\
		print_hex_dump_debug("mkl17z32vda4_fw: data ",		\
				DUMP_PREFIX_NONE, 16, 1,		\
				&value[FRAMING_PACKET_SIZE],		\
				size - FRAMING_PACKET_SIZE,		\
				false);					\
	} while (0)

static const unsigned char mkl17z32vda4_fw_ping_packet[] = {
	0x5a, 0xa6,
};

static const unsigned char ping_response_packet_header[] = {
	0x5a, 0xa7,
};

static const unsigned char ack_packet_header[] = {
	0x5a, 0xa1,
};

static const unsigned char generic_response_packet_header[] = {
	0x5a, 0xa4,
};

static const unsigned char mkl17z32vda4_fw_erase_all_unsecure[] = {
	0x5a, 0xa4, 0x04, 0x00, 0xf6, 0x61,
	0x0d, 0x00, 0x00, 0x00,
};

static const unsigned char mkl17z32vda4_fw_write_packet[] = {
	0x5a, 0xa4, 0x0c, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

static const unsigned char mkl17z32vda4_fw_data_packet[] = {
	0x5a, 0xa5, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

struct mkl17z32vda4_drvdata {
	struct i2c_client *client;
	int ucon_xfwupdate;
	int ucon_req;
	int ucon_i2c_xmaster;
	int ucon_xreset;
};

static int mkl17z32vda4_fw_initialize(struct mkl17z32vda4_drvdata *ddata)
{
	gpio_direction_output(ddata->ucon_req, 0);
	gpio_direction_output(ddata->ucon_xreset, 0);
	gpio_direction_output(ddata->ucon_xfwupdate, 1);

	usleep_range(10000, 11000);

	gpio_set_value(ddata->ucon_req, 1);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_xreset, 1);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_req, 0);

	return 0;
}

static void mkl17z32vda4_fw_enter_boot_mode(struct mkl17z32vda4_drvdata *ddata)
{
	gpio_set_value(ddata->ucon_xreset, 0);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_xfwupdate, 0);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_xreset, 1);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_xfwupdate, 1);
	usleep_range(10000, 11000);

	return;
}

static void mkl17z32vda4_fw_exit_boot_mode(struct mkl17z32vda4_drvdata *ddata)
{
	gpio_set_value(ddata->ucon_xreset, 0);
	gpio_set_value(ddata->ucon_req, 1);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_xreset, 1);
	usleep_range(10000, 11000);
	gpio_set_value(ddata->ucon_req, 0);

	return;
}

static int mkl17z32vda4_fw_write(struct i2c_client *client,
				const unsigned char *value, int size)
{
	int ret;
	dma_addr_t addr = 0;
	unsigned char *buffer = NULL;

	dev_dbg(&client->dev, "%s: write size %d\n", __func__, size);

	ret = i2c_master_send(client, (const char *)value, size);

	return ret;
}

static int mkl17z32vda4_fw_read(struct i2c_client *client,
				unsigned char *value, int size)
{
	int ret;
	dma_addr_t addr = 0;
	unsigned char *buffer = NULL;

	dev_dbg(&client->dev, "%s: read size %d\n", __func__, size);

	ret = i2c_master_recv(client, (char *)value, size);

	return ret;
}

static int mkl17z32vda4_fw_check_packet_header(struct i2c_client *client,
						const unsigned char header[2])
{
	unsigned char value[MAX_BYTE_PER_RESPONSE];
	unsigned int retry = 0;
	int ret;

	while (MAX_RETRY >= retry) {
		ret = mkl17z32vda4_fw_read(client, value, 1);
		if (ret < 0)
			goto out;
		else if (value[0] == header[0])
			break;
		dev_dbg(&client->dev, "%s: ret = %d\n", __func__, ret);
		usleep_range(2000, 2100);
		retry++;
	}

	if (retry > MAX_RETRY) {
		ret = -EBUSY;
		goto out;
	}

	ret = mkl17z32vda4_fw_read(client, value, 1);
	if (ret < 0)
		goto out;

	if (value[0] == header[1]) {
		dev_dbg(&client->dev, "%s: Received packet header, %02X, %02X",
						__func__, header[0], header[1]);
		return 0;
	} else {
		ret = -EIO;
	}

out:
	dev_err(&client->dev, "%s error %d\n", __func__, ret);
	return ret;
}

static int mkl17z32vda4_fw_ping(struct i2c_client *client)
{
	unsigned char value[MAX_BYTE_PER_RESPONSE];
	int err;

	dev_dbg(&client->dev, "%s\n", __func__);

	err = mkl17z32vda4_fw_write(client, mkl17z32vda4_fw_ping_packet,
				ARRAY_SIZE(mkl17z32vda4_fw_ping_packet));
	if (err < 0)
		goto out;

	err = mkl17z32vda4_fw_check_packet_header(client,
				ping_response_packet_header);
	if (err)
		goto out;

	err = mkl17z32vda4_fw_read(client, value, 8);
	if (err < 0)
		goto out;

	dev_dbg(&client->dev, "%s received Ping response\n", __func__);
	dev_dbg(&client->dev, "%s: %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X\n",
		__func__,
		ping_response_packet_header[0], ping_response_packet_header[1],
		value[0], value[1], value[2], value[3],
		value[4], value[5], value[6], value[7]);

	return 0;

out:
	dev_err(&client->dev, "%s error %d\n", __func__, err);
	return err;
}

static int mkl17z32vda4_fw_send_ack(struct i2c_client *client)
{
	int err;

	dev_dbg(&client->dev, "%s: %02X, %02X\n", __func__,
		ack_packet_header[0], ack_packet_header[1]);

	err = mkl17z32vda4_fw_write(client, ack_packet_header,
				ARRAY_SIZE(ack_packet_header));
	if (err < 0)
		dev_err(&client->dev, "%s error %d\n", __func__, err);

	return 0;
}

static int mkl17z32vda4_fw_wait_ack(struct i2c_client *client)
{
	int ret;
	unsigned char value[MAX_BYTE_PER_RESPONSE];
	unsigned int retry = 0;

	dev_dbg(&client->dev, "%s\n", __func__);

	while (MAX_RETRY >= retry) {
		ret = mkl17z32vda4_fw_read(client, value, 1);
		if (ret < 0) {
			dev_err(&client->dev, "%s error %d\n", __func__, ret);
			goto out;
		} else if (value[0] == TYPE_START_BYTE) {
			break;
		}
		dev_dbg(&client->dev, "%s: value = %02X\n", __func__, value[0]);
		usleep_range(2000, 2100);
		retry++;
	}

	if (retry > MAX_RETRY) {
		ret = -EBUSY;
		goto out;
	}

	ret = mkl17z32vda4_fw_read(client, value, 1);
	if (ret < 0) {
		dev_err(&client->dev, "%s error %d\n", __func__, ret);
		goto out;
	}

	switch (value[0]) {
	case TYPE_ACK:
		dev_dbg(&client->dev, "%s: Received ACK, 5A, %02X\n",
						__func__, value[0]);
		ret = 0;
		break;
	case TYPE_NAK:
		dev_dbg(&client->dev, "%s: Received NAK, 5A, %02X\n",
						__func__, value[0]);
		ret = -EAGAIN;
		break;
	default:
		dev_err(&client->dev, "%s: Received err, %02X\n",
						__func__, value[0]);
		mkl17z32vda4_fw_send_ack(client);
		ret = -EIO;
		break;
	}

out:
	return ret;
}

static int mkl17z32vda4_fw_write_with_check_ack(struct i2c_client *client,
					const unsigned char *value, int size)
{
	int ret = -1;
	int count;
	int retry = 0;

	while (MAX_RETRY >= retry) {
		count = mkl17z32vda4_fw_write(client, value, size);
		if (count < 0) {
			ret = count;
			break;
		}
		ret = mkl17z32vda4_fw_wait_ack(client);
		if (ret == 0) {
			ret = count;
			break;
		} else if (ret == -EAGAIN) {
			retry++;
		} else {
			break;
		}
	}

	return ret;
}

static int mkl17z32vda4_fw_wait_generic_response(struct i2c_client *client)
{
	unsigned char value[MAX_BYTE_PER_RESPONSE];
	unsigned char length[2];
	unsigned char crc[2];
	unsigned int payload_len = 0;
	int response = 0;
	int i;
	int ret;

	dev_dbg(&client->dev, "%s\n", __func__);

	ret = mkl17z32vda4_fw_check_packet_header(client,
				generic_response_packet_header);
	if (ret)
		goto error;


	ret = mkl17z32vda4_fw_read(client, length, 2);
	if (ret < 0)
		goto error;

	ret = mkl17z32vda4_fw_read(client, crc, 2);
	if (ret < 0)
		goto error;

	dev_dbg(&client->dev, "%s: length %02X %02X, crc %02X %02X\n",
			__func__, length[0], length[1], crc[0], crc[1]);

	payload_len = length[0] + (length[1] << 8);

	if (payload_len > MAX_COMMAND_PACKET_LEN)
		payload_len = MAX_COMMAND_PACKET_LEN;

	ret = mkl17z32vda4_fw_read(client, value, payload_len);
	if (ret < 0)
		goto error;

	dev_dbg(&client->dev, "%s: Status Code %02X, %02X, %02X, %02X\n",
		__func__, value[0], value[1], value[2], value[3]);

	for (i = 0; i < (payload_len - COMMAND_HEADER_SIZE) / 4; i++)
		dev_dbg(&client->dev, "%s: Command Tag %02X, %02X, %02X, %02X\n",
			__func__,
			value[COMMAND_HEADER_SIZE + i * 4],
			value[COMMAND_HEADER_SIZE + i * 4 + 1],
			value[COMMAND_HEADER_SIZE + i * 4 + 2],
			value[COMMAND_HEADER_SIZE + i * 4 + 3]);

	/*
	 * Send ACK before status error check.
	 * If return without sending ACK, next command will not work
	 * properly even after reset.
	 */
	ret = mkl17z32vda4_fw_send_ack(client);
	if (ret < 0)
		goto error;

	for (i = 0; i < 4; i++)
		response += value[COMMAND_HEADER_SIZE + i] << (8 * i);
	if (response) {
		dev_err(&client->dev, "%s: Status error, code = %d\n",
						__func__, response);
		ret = -EFAULT;
		goto error;
	}

	dev_dbg(&client->dev, "%s: received Generic response\n", __func__);

	return 0;

error:
	dev_err(&client->dev, "%s: error %d\n", __func__, ret);
	return ret;
}

static uint16_t crc16_update(const uint8_t *src, uint32_t len)
{
	uint32_t crc = 0;
	uint32_t j;
	uint32_t i;
	uint32_t byte;
	uint32_t temp;

	for (j = 0; j < len; ++j) {
		if (j == 4 || j == 5)
			continue;
		byte = src[j];
		crc ^= byte << 8;
		for (i = 0; i < 8; ++i) {
			temp = crc << 1;
			if (crc & 0x8000)
				temp ^= 0x1021;
			crc = temp;
		}
	}

	pr_debug("%s: crc result = %02X\n", __func__, (uint16_t)crc);

	return crc;
}

static int mkl17z32vda4_fw_flash_erase_all_unsecure(struct i2c_client *client)
{
	int ret;

	dev_info(&client->dev, "%s: Start\n", __func__);

	ret = mkl17z32vda4_fw_write_with_check_ack(client,
			mkl17z32vda4_fw_erase_all_unsecure,
			ARRAY_SIZE(mkl17z32vda4_fw_erase_all_unsecure));
	if (ret < 0)
		goto error;

	log_command_packet(mkl17z32vda4_fw_erase_all_unsecure,
			ARRAY_SIZE(mkl17z32vda4_fw_erase_all_unsecure));

	ret = mkl17z32vda4_fw_wait_generic_response(client);
	if (ret < 0)
		goto error;

	dev_info(&client->dev, "%s: Finished\n", __func__);

	return 0;

error:
	dev_err(&client->dev, "%s: Failed\n", __func__);
	return ret;
}

static int mkl17z32vda4_fw_write_data_packet(struct i2c_client *client,
				const u8 *data, int offset, int byte_count)
{
	unsigned char value[64];
	uint16_t crc;
	int ret;
	int i;
	int count;

	memcpy(value, mkl17z32vda4_fw_data_packet,
		ARRAY_SIZE(mkl17z32vda4_fw_data_packet));

	value[2] = byte_count;
	value[3] = 0;

	for (i = 0; i < byte_count; i++)
		value[FRAMING_PACKET_SIZE + i] =
				*((unsigned char *)data +
				(offset * MAX_BYTE_PER_PACKET + i));

	crc = crc16_update(value,
			FRAMING_PACKET_SIZE + byte_count);
	value[4] = crc & 0xff;
	value[5] = crc >> 8;

	count = mkl17z32vda4_fw_write_with_check_ack(client, value,
			FRAMING_PACKET_SIZE + byte_count);
	if (count < FRAMING_PACKET_SIZE) {
		dev_err(&client->dev, "%s: write error\n", __func__);
		ret = count;
		goto out;
	} else {
		ret = count - FRAMING_PACKET_SIZE;
	}
	log_data_packet(value, FRAMING_PACKET_SIZE + byte_count);

out:
	return ret;
}

static int mkl17z32vda4_fw_write_memory(struct i2c_client *client,
					const struct firmware *fw)
{
	unsigned int num_full_write = fw->size / MAX_BYTE_PER_PACKET;
	unsigned int rem = fw->size % MAX_BYTE_PER_PACKET;
	unsigned char value[64];
	uint16_t crc;
	int count = 0;
	int i;
	int ret;

	dev_info(&client->dev, "%s: Start\n", __func__);

	memcpy(value, mkl17z32vda4_fw_write_packet,
			ARRAY_SIZE(mkl17z32vda4_fw_write_packet));
	value[2] = 0x0c;
	value[3] = 0;

	for (i = 0; i < 4; i++)
		value[PARAMETER_OFFSET + i] = START_ADDRESS >> (8 * i);
	for (i = 0; i < 4; i++)
		value[PARAMETER_OFFSET + 4 + i] = fw->size >> (8 * i);

	crc = crc16_update(value, FRAMING_PACKET_SIZE + value[2]);
	value[4] = crc & 0xff;
	value[5] = crc >> 8;

	ret = mkl17z32vda4_fw_write_with_check_ack(client, value,
			ARRAY_SIZE(mkl17z32vda4_fw_write_packet));
	if (ret < 0)
		goto error;

	log_command_packet(value, ARRAY_SIZE(mkl17z32vda4_fw_write_packet));

	ret = mkl17z32vda4_fw_wait_generic_response(client);
	if (ret < 0)
		goto error;

	for (i = 0; i < num_full_write; i++) {
		ret = mkl17z32vda4_fw_write_data_packet(client, fw->data,
					i, MAX_BYTE_PER_PACKET);
		if (ret < 0)
			goto error;
		else
			count += ret;
	}

	if (rem) {
		dev_dbg(&client->dev, "%s: sending Remainder Data packet\n",
								__func__);
		ret = mkl17z32vda4_fw_write_data_packet(client, fw->data,
					num_full_write, rem);
		if (ret < 0)
			goto error;
		else
			count += ret;
	}

	ret = mkl17z32vda4_fw_wait_generic_response(client);
	if (ret < 0)
		goto error;

	dev_info(&client->dev, "%s: Finished to flash FW size %d bytes\n",
							__func__, count);

	return count;

error:
	dev_err(&client->dev, "%s: Failed\n", __func__);
	return ret;
}

static ssize_t attr_fwupdate(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	int count = 0;
	struct mkl17z32vda4_drvdata *ddata = dev_get_drvdata(dev);
	const struct firmware *fw;
	char fw_name[size];

	if (ddata->client == NULL) {
		dev_err(dev, "i2c_client is not registered\n");
		err = -ENODEV;
		goto out;
	}

	strncpy(fw_name, buf, size);
	if ('\n' == fw_name[size - 1])
		fw_name[size - 1] = '\0';

	err = request_firmware(&fw, fw_name, dev);
	if (err) {
		dev_err(dev, "Coudn't load ucom fw\n");
		err = -ENOENT;
		goto out;
	}

	if (fw->size > MAX_FW_SIZE) {
		dev_err(dev, "Too large FW\n");
		err = -EFBIG;
		goto out_size;
	}

	dev_info(dev, "%s: Loaded FW. Start update\n", __func__);

	mkl17z32vda4_fw_enter_boot_mode(ddata);

	/* Establish i2c peripheral connection */
	err = mkl17z32vda4_fw_ping(ddata->client);
	if (err) {
		dev_err(dev, "%s: ping reseponse error\n", __func__);
		goto out_update;
	}

	err = mkl17z32vda4_fw_flash_erase_all_unsecure(ddata->client);
	if (err < 0) {
		dev_err(dev, "%s: failed to erase flash memory\n", __func__);
		goto out_update;
	}

	count = mkl17z32vda4_fw_write_memory(ddata->client, fw);
	if (count == fw->size) {
		err = 0;
	} else {
		dev_err(dev, "%s: failed to write memory\n", __func__);
		err = -ENOSPC;
	}

out_update:
	mkl17z32vda4_fw_exit_boot_mode(ddata);

	dev_info(dev, "%s: Ucom fw update %s\n", __func__,
			err ? "failed" : "finished");

out_size:
	release_firmware(fw);

out:
	return err ? err : size;
}

static ssize_t attr_fwerase(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	struct mkl17z32vda4_drvdata *ddata = dev_get_drvdata(dev);

	if (ddata->client == NULL) {
		dev_err(dev, "i2c_client is not registered\n");
		err = -ENODEV;
		goto out;
	}

	if (sysfs_streq(buf, "1")) {
		dev_info(dev, "Erase FW\n");
	} else {
		dev_err(dev, "Invalid command\n");
		err = -EINVAL;
		goto out;
	}

	mkl17z32vda4_fw_enter_boot_mode(ddata);

	/* Establish i2c peripheral connection */
	err = mkl17z32vda4_fw_ping(ddata->client);
	if (err) {
		dev_err(dev, "%s: ping reseponse error\n", __func__);
		goto out_update;
	}

	err = mkl17z32vda4_fw_flash_erase_all_unsecure(ddata->client);
	if (err < 0)
		dev_err(dev, "%s: failed to erase flash memory\n", __func__);

out_update:
	mkl17z32vda4_fw_exit_boot_mode(ddata);

	dev_info(dev, "%s: Ucom fw erase %s\n", __func__,
			err ? "failed" : "finished");

out:
	return err ? err : size;
}

static const struct device_attribute mkl17z32vda4_fw_attrs[] = {
	__ATTR(fwupdate, S_IWUSR, NULL, attr_fwupdate),
	__ATTR(fwerase, S_IWUSR, NULL, attr_fwerase),
};

static int create_sysfs_interfaces(struct device *dev)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(mkl17z32vda4_fw_attrs); i++) {
		ret = device_create_file(dev, mkl17z32vda4_fw_attrs + i);
		if (ret)
			goto error;
	}

	return 0;

error:
	for (; i <= 0; i--)
		device_remove_file(dev, mkl17z32vda4_fw_attrs + i);
	dev_err(dev, "%s: Unable to create interface\n", __func__);
	return -ENODEV;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mkl17z32vda4_fw_attrs); i++)
		device_remove_file(dev, mkl17z32vda4_fw_attrs + i);

	return;
}

static int mkl17z32vda4_fw_i2c_suspend(struct device *dev)
{
	dev_dbg(dev, "%s()\n", __func__);
	return 0;
}

static int mkl17z32vda4_fw_i2c_resume(struct device *dev)
{
	dev_dbg(dev, "%s()\n", __func__);
	return 0;
}

static int mkl17z32vda4_fw_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct mkl17z32vda4_drvdata *ddata;
	int rv;
	struct device *dev;
	struct device_node *np;

	dev_dbg(&client->dev, "%s()\n", __func__);

	ddata = devm_kzalloc(&client->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata) {
		dev_err(&client->dev, "%s: failed to allocate\n", __func__);
		ret = -ENOMEM;
		goto out;
	}
	ddata->client = client;
	i2c_set_clientdata(client, ddata);
	dev_set_drvdata(&client->dev, ddata);

	dev = &(ddata->client->dev);
	np = dev->of_node;

	ddata->ucon_xfwupdate = of_get_named_gpio(np, "ucon_xfwupdate-gpio", 0);
	ret = gpio_is_valid(ddata->ucon_xfwupdate);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_xfwupdate get failed, ret = %d\n",
				__func__, ret);
		goto out;
	}

	ddata->ucon_req = of_get_named_gpio(np, "ucon_req-gpio", 0);
	ret = gpio_is_valid(ddata->ucon_req);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_req get failed, ret = %d\n",
				__func__, ret);
		goto out;
	}

	ddata->ucon_xreset = of_get_named_gpio(np, "ucon_xreset-gpio", 0);
	ret = gpio_is_valid(ddata->ucon_xreset);
	if (ret < 0) {
		dev_err(&client->dev, "%s: ucon_xreset get failed, ret = %d\n",
				__func__, ret);
		goto out;
	}

	ret = mkl17z32vda4_fw_initialize(ddata);

	if (ret < 0) {
		dev_err(&client->dev, "%s: hwinit failed, ret = %d\n",
					__func__, ret);
		goto out;
	} else {
		dev_info(&client->dev, "%s: hwinit done\n", __func__);
	}

	ret = create_sysfs_interfaces(&client->dev);

out:
	return ret;
}

static int mkl17z32vda4_fw_i2c_remove(struct i2c_client *client)
{
	dev_dbg(&client->dev, "%s()\n", __func__);

	remove_sysfs_interfaces(&client->dev);

	return 0;
}

static void mkl17z32vda4_fw_i2c_poweroff(struct i2c_client *client)
{
	dev_dbg(&client->dev, "%s()\n", __func__);

	remove_sysfs_interfaces(&client->dev);

	return;
}

static const struct i2c_device_id mkl17z32vda4_fw_id[] = {
	{"mkl17z32vda4_fw", 0},
	{}
};

static const struct of_device_id mkl17z32vda4_fw_dt_ids[] = {
        { .compatible = "fsl,mkl17z32vda4_fw", },
        { }
};
MODULE_DEVICE_TABLE(of, mkl17z32vda4_fw_dt_ids);

static const struct dev_pm_ops mkl17z32vda4_fw_i2c_pm_ops = {
	.suspend = mkl17z32vda4_fw_i2c_suspend,
	.resume  = mkl17z32vda4_fw_i2c_resume,
};

static struct i2c_driver mkl17z32vda4_fw_i2c_driver = {
	.driver = {
		.name  = "mkl17z32vda4_fw",
		.owner = THIS_MODULE,
		.pm    = &mkl17z32vda4_fw_i2c_pm_ops,
		.of_match_table = of_match_ptr(mkl17z32vda4_fw_dt_ids),
	},
	.id_table = mkl17z32vda4_fw_id,
	.probe    = mkl17z32vda4_fw_i2c_probe,
	.remove   = mkl17z32vda4_fw_i2c_remove,
	.shutdown = mkl17z32vda4_fw_i2c_poweroff,
};

static int __init mkl17z32vda4_fw_init(void)
{
	int ret;

	pr_debug("%s()\n", __func__);

	ret = i2c_add_driver(&mkl17z32vda4_fw_i2c_driver);
	if (ret)
		pr_err("i2c_add_driver(): code %d error occurred\n", ret);

	return ret;
}

static void __exit mkl17z32vda4_fw_exit(void)
{
	pr_debug("%s()\n", __func__);

	i2c_del_driver(&mkl17z32vda4_fw_i2c_driver);

	return;
}

module_init(mkl17z32vda4_fw_init);
module_exit(mkl17z32vda4_fw_exit);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("MKL17Z32VDA4 FW update driver");
MODULE_LICENSE("GPL");

