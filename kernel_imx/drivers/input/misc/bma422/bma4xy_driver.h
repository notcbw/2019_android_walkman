/*!
 * @section LICENSE
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 * Copyright 2018 Sony Video & Sound Products Inc.
 * (C) Copyright 2011~2016 Bosch Sensortec GmbH All Rights Reserved
 *
 * This software program is licensed subject to the GNU General
 * Public License (GPL).Version 2,June 1991,
 * available at http://www.fsf.org/copyleft/gpl.html
 *
 * @filename bma4xy_driver.h
 * @date     2017/04/17 13:44
 * @id       "b5ff23a"
 * @version  0.2.6
 *
 * @brief   The head file of BMA4XY device driver core code
 */
#include "bma4.h"
#ifndef _BMA4XY_DRIVER_H
#define _BMA4XY_DRIVER_H
#include <linux/types.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include "bma4_defs.h"
#include "bma4.h"
#if defined(BMA420)
#include "bma420.h"
#endif
#if defined(BMA421)
#include "bma421.h"
#endif
#if defined(BMA421LOSC)
#include "bma421losc.h"
#endif
#if defined(BMA421L)
#include "bma421l.h"
#endif
#if defined(BMA422)
#include "bma422.h"
#endif
#if defined(BMA455)
#include "bma455.h"
#endif
#if defined(BMA456)
#include "bma456.h"
#endif
#if defined(BMA424SC)
#include "bma424sc.h"
#endif
#if defined(BMA424O)
#include "bma424o.h"
#endif
#if defined(BMA424)
#include "bma424.h"
#endif
#if defined(BMA421OSC)
#include "bma421osc.h"
#endif


/* sensor name definition*/
#define SENSOR_NAME "bma4xy_acc"
#define SENSOR_NAME_UC "bma4xy_uc"
/* #define BMA4XY_AUX_INTERFACE_SUPPORT */
/* #define BMA4XY_FOR_SPREADTRUM 1 */
#define BMA4XY_LOAD_CONFIG_FILE_IN_INIT 1
/* generic */
#define CHECK_CHIP_ID_TIME_MAX   5
#define FIFO_DATA_BUFSIZE_BMA4XY    1024
#define BMA4XY_ENABLE_INT1  0
#define BMA4XY_ENABLE_INT2  1
#define BMA4XY_I2C_WRITE_DELAY_TIME 1
#define BMA4XY_MAX_RETRY_I2C_XFER (10)
#define BMA4XY_MAX_RETRY_WAIT_DRDY (100)
#define BMA4XY_I2C_WRITE_DELAY_TIME 1
#define BMA4XY_MAX_RETRY_WAKEUP (5)
#define BMA4XY_DELAY_MIN (1)
#define BMA4XY_DELAY_DEFAULT (200)
#define BYTES_PER_LINE (16)
#define REL_UC_STATUS    1

/*fifo definition*/
#define A_BYTES_FRM      6
#define M_BYTES_FRM      8
#define MA_BYTES_FRM     14

/*bma4xy power mode */
struct pw_mode {
	uint8_t acc_pm;
	uint8_t mag_pm;
};

struct bma4xy_client_data {
	struct bma4_dev device;
	struct device *dev;
	struct input_dev *acc_input;
	struct input_dev *uc_input;
	uint8_t fifo_mag_enable;
	uint8_t fifo_acc_enable;
	uint32_t fifo_bytecount;
	struct pw_mode pw;
	uint8_t acc_odr;
#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
	uint8_t mag_odr;
	uint8_t mag_chip_id;
#endif
#if defined(BMA4XY_FOR_SPREADTRUM)
	struct delayed_work accel_poll_work;
	u32 accel_poll_ms;
	u32 accel_latency_ms;
	atomic_t accel_en;
	struct workqueue_struct *data_wq;
#endif
	int IRQ[2];
	char IRQ_name[2][32];
	uint8_t gpio_pin[2];
	char gpio_name[2][32];
	struct work_struct irq_work;
	uint16_t fw_version;
	char *config_stream_name;
	int reg_sel;
	int reg_len;
	struct wake_lock wakelock;
	struct delayed_work delay_work_sig;
	atomic_t in_suspend;
	uint8_t tap_type;
	int8_t selftest;
	struct selftest_delta_limit selftest_diff;
	uint8_t sigmotion_enable;
	uint8_t stepdet_enable;
	uint8_t stepcounter_enable;
	uint8_t tilt_enable;
	uint8_t pickup_enable;
	uint8_t glance_enable;
	uint8_t wakeup_enable;
	uint8_t anymotion_enable;
	uint8_t nomotion_enable;
	uint8_t orientation_enable;
	uint8_t flat_enable;
	uint8_t tap_enable;
	uint8_t highg_enable;
	uint8_t lowg_enable;
	uint8_t activity_enable;
	uint8_t err_int_trigger_num;
	uint32_t step_counter_val;
	uint32_t step_counter_temp;
	uint8_t axis_map[3];
	uint8_t axis_sign[3];
};
int bma4xy_probe(struct bma4xy_client_data *client_data, struct device *dev);
int bma4xy_write_config_stream(uint8_t dev_addr,
	uint8_t reg_addr, const uint8_t *data, uint8_t len);
int bma4xy_suspend(struct device *dev);
int bma4xy_resume(struct device *dev);
int bma4xy_remove(struct device *dev);
#endif
