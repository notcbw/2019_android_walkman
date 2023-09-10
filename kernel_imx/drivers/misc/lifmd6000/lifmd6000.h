/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Copyright 2018 Sony Video & Sound Products Inc.
 *
 */

#ifndef LIFMD6000_RME_FW_WRITE_H
#define LIFMD6000_RME_FW_WRITE_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/spi/spi.h>
#include <linux/firmware.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/exclusive_rme_tp.h>
#include <linux/wakelock.h>

#define DSD_RME_NAME			"lifmd6000"
#define DSD_RME_FW_FILE_NAME		"dsd_rme_fw.bit"

#define CS_LOW				0
#define CS_HIGH				1

#define DSD_RME_POWER_ON		1
#define DSD_RME_POWER_OFF		0

#define DSD_RME_WKUP_HIGH		1
#define DSD_RME_WKUP_LOW		0

#define DSD_RME_RST_RESET		0
#define DSD_RME_RST_NORMAL		1

#define DSD_RME_OSC_ENABLE		1
#define DSD_RME_OSC_DISABLE		0

#define DSD_RME_DEVID_LEN		4096
#define DSD_RME_BIT_PER_WORD		8

#define DSD_RME_STATUS_DONE		0x01
#define DSD_RME_STATUS_BUSY		0x10
#define DSD_RME_STATUS_FAIL		0x20

#define DSD_RME_DELAY_TIM_PROBE		5000
#define DSD_RME_DELAY_TIM_RESUME	10
#define DSD_RME_DELAY_TIM_RETRY         1000

struct lifmd6000_writer {
	int gpio_cs;				// RME Chip Select GPIO
	int gpio_en;				// EMR_POWER_EN GPIO
	int gpio_wkup;				// WKUP GPIO
	int gpio_rst;				// UCON_XRESET GPIO

	struct mutex spi_lock;			// SPI Mutex lock
	struct spi_device *spi;			// SPI Info

	struct device *dev;			// Device Info

	struct workqueue_struct *fw_write_wq;	// work queue
	struct delayed_work work_update;	// thread

	uint32_t save_firmware_size;		// firmware size
	uint8_t  *save_firmware_data;		// firmware data pointer

	struct wake_lock rme_write_lock;	// wake_lock info
};

struct spi_command_code {
	uint32_t len;				// Command Length
	const uint8_t command[5];		// Command Data
};

enum {
	SPI_RD_COMMAND_IDCODE_PUB = 0,		// IDCODE_PUB opcode
	SPI_RD_COMMAND_LSC_READ_STATUS,		// LSC_READ_STATUS instruction
	SPI_RD_COMMAND_END
};

const struct spi_command_code spi_rd_command[SPI_RD_COMMAND_END] = {
	// IDCODE_PUB opcode
	[SPI_RD_COMMAND_IDCODE_PUB].len = 4,
	[SPI_RD_COMMAND_IDCODE_PUB].command = {0xE0, 0x00, 0x00, 0x00},

	// LSC_READ_STATUS instruction
	[SPI_RD_COMMAND_LSC_READ_STATUS].len = 4,
	[SPI_RD_COMMAND_LSC_READ_STATUS].command = {0x3C, 0x00, 0x00, 0x00},
};

enum {
	SPI_WR_COMMAND_KEY_ACTIVATION = 0,	// Key activation
	SPI_WR_COMMAND_ISC_ENABLE,		// ISC_ENABLE instruction
	SPI_WR_COMMAND_ISC_ERASE,		// ISC_ERASE instruction
	SPI_WR_COMMAND_LSC_INIT_ADDRESS,	// LSC_INIT_ADDRESS instruction
	SPI_WR_COMMAND_LSC_BITSTREAM_BURST,	// LSC_BITSTREAM_BURST inst
	SPI_WR_COMMAND_ISC_DISABLE,		// ISC_DISABLE instruction
	SPI_WR_COMMAND_NOOP,			// NO-OP instruction
	SPI_WR_COMMAND_END
};

const struct spi_command_code spi_wr_command[SPI_WR_COMMAND_END] = {
	// Key activation
	[SPI_WR_COMMAND_KEY_ACTIVATION].len = 5,
	[SPI_WR_COMMAND_KEY_ACTIVATION].command = {
					0xFF, 0xA4, 0xC6, 0xF4, 0x8A},

	// ISC_ENABLE instruction
	[SPI_WR_COMMAND_ISC_ENABLE].len = 4,
	[SPI_WR_COMMAND_ISC_ENABLE].command = {0xC6, 0x00, 0x00, 0x00},

	// ISC_ERASE instruction
	[SPI_WR_COMMAND_ISC_ERASE].len = 4,
	[SPI_WR_COMMAND_ISC_ERASE].command = {0x0E, 0x00, 0x00, 0x00},

	// LSC_INIT_ADDRESS instruction
	[SPI_WR_COMMAND_LSC_INIT_ADDRESS].len = 4,
	[SPI_WR_COMMAND_LSC_INIT_ADDRESS].command = {0x46, 0x00, 0x00, 0x00},

	// LSC_BITSTREAM_BURST instruction
	[SPI_WR_COMMAND_LSC_BITSTREAM_BURST].len = 4,
	[SPI_WR_COMMAND_LSC_BITSTREAM_BURST].command = {0x7A, 0x00, 0x00, 0x00},

	// ISC_DISABLE instruction
	[SPI_WR_COMMAND_ISC_DISABLE].len = 4,
	[SPI_WR_COMMAND_ISC_DISABLE].command = {0x26, 0x00, 0x00, 0x00},

	// NO-OP instruction
	[SPI_WR_COMMAND_NOOP].len = 4,
	[SPI_WR_COMMAND_NOOP].command = {0xFF, 0xFF, 0xFF, 0xFF},
};
#endif
