/*!
 * @section LICENSE
 * Copyright 2018 Sony Video & Sound Products Inc.
 * (C) Copyright 2011~2016 Bosch Sensortec GmbH All Rights Reserved
 *
 * This software program is licensed subject to the GNU General
 * Public License (GPL).Version 2,June 1991,
 * available at http://www.fsf.org/copyleft/gpl.html
 *
 * @filename bma4xy_driver.c
 * @date     2018/01/03 13:44
 * @id       "b5ff23a"
 * @version  0.2.6
 *
 * @brief    bma4xy Linux Driver
 */

#define DRIVER_VERSION "0.0.2.6"
#include <linux/types.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include "bma4xy_driver.h"
#include "bs_log.h"
#ifdef CONFIG_ICX_DMP_BOARD_ID
#include <linux/icx_dmp_board_id.h>
#endif

enum BMA4XY_SENSOR_INT_MAP {
	BMA4XY_FFULL_INT = 8,
	BMA4XY_FWM_INT = 9,
	BMA4XY_DRDY_INT = 10,
};

enum BMA4XY_CONFIG_FUN {
	BMA4XY_SIG_MOTION_SENSOR = 0,
	BMA4XY_STEP_DETECTOR_SENSOR = 1,
	BMA4XY_STEP_COUNTER_SENSOR = 2,
	BMA4XY_TILT_SENSOR = 3,
	BMA4XY_PICKUP_SENSOR = 4,
	BMA4XY_GLANCE_DETECTOR_SENSOR = 5,
	BMA4XY_WAKEUP_SENSOR = 6,
	BMA4XY_ANY_MOTION_SENSOR = 7,
	BMA4XY_ORIENTATION_SENSOR = 8,
	BMA4XY_FLAT_SENSOR = 9,
	BMA4XY_TAP_SENSOR = 10,
	BMA4XY_HIGH_G_SENSOR = 11,
	BMA4XY_LOW_G_SENSOR = 12,
	BMA4XY_ACTIVITY_SENSOR = 13,
};

enum BMA4XY_INT_STATUS0 {
	SIG_MOTION_OUT = 0x01,
	STEP_DET_OUT = 0x02,
	TILT_OUT = 0x04,
	PICKUP_OUT = 0x08,
	GLANCE_OUT = 0x10,
	WAKEUP_OUT = 0x20,
	ANY_NO_MOTION_OUT = 0x40,
	ERROR_INT_OUT = 0x80,
};

enum BMA4XY_INT_STATUS1 {
	FIFOFULL_OUT = 0x01,
	FIFOWATERMARK_OUT = 0x02,
	MAG_DRDY_OUT = 0x20,
	ACC_DRDY_OUT = 0x80,
};

/*bma4 fifo analyse return err status*/
enum BMA4_FIFO_ANALYSE_RETURN_T {
	FIFO_OVER_READ_RETURN = -10,
	FIFO_SENSORTIME_RETURN = -9,
	FIFO_SKIP_OVER_LEN = -8,
	FIFO_M_A_OVER_LEN = -5,
	FIFO_M_OVER_LEN = -3,
	FIFO_A_OVER_LEN = -1
};

static void bma4xy_i2c_delay(u32 msec)
{
	if (msec <= 20)
		usleep_range(msec * 1000, msec * 1000 + 1000);
	else
		msleep(msec);
}

uint64_t bma4xy_get_alarm_timestamp(void)
{
	uint64_t ts_ap;
	struct timespec tmp_time;
	get_monotonic_boottime(&tmp_time);
	ts_ap = (uint64_t)tmp_time.tv_sec * 1000000000 + tmp_time.tv_nsec;
	return ts_ap;
}

static int bma4xy_check_chip_id(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t chip_id = 0;

	err = client_data->device.bus_read(client_data->device.dev_addr,
			BMA4_CHIP_ID_ADDR, &chip_id, 1);
	if (err) {
		PERR("error");
		return err;
	}
	PINFO("read chip id result: %#x", chip_id);
	return err;
}

static unsigned char fifo_data[1024];
static void bmi_fifo_frame_bytes_extend_calc(u8 fifo_index,
	u8 *fifo_frmbytes_extend)
{
	switch (fifo_index) {
	case FIFO_HEAD_SKIP_FRAME:
		*fifo_frmbytes_extend = 1;
		break;
	case FIFO_HEAD_M_A:
		*fifo_frmbytes_extend = BMA4_FIFO_MA_LENGTH;
		break;
	case FIFO_HEAD_A:
		*fifo_frmbytes_extend = BMA4_FIFO_A_LENGTH;
		break;
	case FIFO_HEAD_M:
		*fifo_frmbytes_extend = BMA4_FIFO_M_LENGTH;
		break;
	case FIFO_HEAD_SENSOR_TIME:
		*fifo_frmbytes_extend = BMA4_SENSOR_TIME_LENGTH;
		break;
	default:
		*fifo_frmbytes_extend = 0;
		break;
	};
}
int bma4xy_fifo_analysis_handle(struct bma4xy_client_data *client_data,
	u8 *fifo_data, u16 fifo_length)
{
	uint8_t frame_head = 0;
	uint16_t fifo_index = 0;
	int8_t last_return_st = 0;
	uint8_t fifo_frmbytes_extend = 0;
	int err = 0;
	struct bma4_mag_xyzr mag;
	struct bma4_accel acc;
	memset(&acc, 0, sizeof(acc));
	memset(&mag, 0, sizeof(mag));

	for (fifo_index = 0; fifo_index < fifo_length;) {
		/*this use in the Qualcomm platform i2c limit 256 bytes*/
		if (fifo_index < 256) {
			bmi_fifo_frame_bytes_extend_calc(
				fifo_data[fifo_index], &fifo_frmbytes_extend);
			if ((fifo_index + 1+fifo_frmbytes_extend) > 255)
				fifo_index = 256;
		}
		if ((fifo_index > 256) && (fifo_index < 512)) {
			bmi_fifo_frame_bytes_extend_calc(
				fifo_data[fifo_index], &fifo_frmbytes_extend);
			if ((fifo_index + 1+fifo_frmbytes_extend) > 511)
				fifo_index = 512;
		}
		if ((fifo_index > 512) && (fifo_index < 768)) {
			bmi_fifo_frame_bytes_extend_calc(
				fifo_data[fifo_index], &fifo_frmbytes_extend);
			if ((fifo_index + 1 + fifo_frmbytes_extend) > 767)
				fifo_index = 768;
		}
		frame_head = fifo_data[fifo_index];
		switch (frame_head) {
			/*skip frame 0x40 22 0x84*/
		case FIFO_HEAD_SKIP_FRAME:
		/*fifo data frame index + 1*/
			fifo_index = fifo_index + 1;
			if (fifo_index + 1 > fifo_length) {
				last_return_st = FIFO_SKIP_OVER_LEN;
				break;
			}
			/*skip_frame_cnt = fifo_data[fifo_index];*/
			fifo_index = fifo_index + 1;
		break;
		case FIFO_HEAD_M_A:
		{/*fifo data frame index + 1*/
			fifo_index = fifo_index + 1;
			if (fifo_index + MA_BYTES_FRM > fifo_length) {
				last_return_st = FIFO_M_A_OVER_LEN;
				break;
			}
			mag.x = fifo_data[fifo_index + 1] << 8|
				fifo_data[fifo_index + 0];
			mag.y = fifo_data[fifo_index + 3] << 8 |
				fifo_data[fifo_index + 2];
			mag.z = fifo_data[fifo_index + 5] << 8 |
				fifo_data[fifo_index + 4];
			mag.r = fifo_data[fifo_index + 7] << 8 |
				fifo_data[fifo_index + 6];
			acc.x = fifo_data[fifo_index + 9] << 8 |
				fifo_data[fifo_index + 8];
			acc.y = fifo_data[fifo_index + 11] << 8 |
				fifo_data[fifo_index + 10];
			acc.z = fifo_data[fifo_index + 13] << 8 |
				fifo_data[fifo_index + 12];
			fifo_index = fifo_index + MA_BYTES_FRM;
			break;
		}
		case FIFO_HEAD_A:
		{	/*fifo data frame index + 1*/
			fifo_index = fifo_index + 1;
			if (fifo_index + A_BYTES_FRM > fifo_length) {
				last_return_st = FIFO_A_OVER_LEN;
				break;
			}
			acc.x = fifo_data[fifo_index + 1] << 8 |
				fifo_data[fifo_index + 0];
			acc.y = fifo_data[fifo_index + 3] << 8 |
				fifo_data[fifo_index + 2];
			acc.z = fifo_data[fifo_index + 5] << 8 |
				fifo_data[fifo_index + 4];
			fifo_index = fifo_index + A_BYTES_FRM;
			break;
		}
		case FIFO_HEAD_M:
		{	/*fifo data frame index + 1*/
			fifo_index = fifo_index + 1;
			if (fifo_index + M_BYTES_FRM > fifo_length) {
				last_return_st = FIFO_M_OVER_LEN;
				break;
			}
			mag.x = fifo_data[fifo_index + 1] << 8 |
				fifo_data[fifo_index + 0];
			mag.y = fifo_data[fifo_index + 3] << 8 |
				fifo_data[fifo_index + 2];
			mag.z = fifo_data[fifo_index + 5] << 8 |
				fifo_data[fifo_index + 4];
			mag.r = fifo_data[fifo_index + 7] << 8 |
				fifo_data[fifo_index + 6];
			fifo_index = fifo_index + M_BYTES_FRM;
			break;
		}
		/* sensor time frame*/
		case FIFO_HEAD_SENSOR_TIME:
		{
			/*fifo data frame index + 1*/
			fifo_index = fifo_index + 1;
			if (fifo_index + 3 > fifo_length) {
				last_return_st = FIFO_SENSORTIME_RETURN;
				break;
			}
			/*fifo sensor time frame index + 3*/
			fifo_index = fifo_index + 3;
			break;
		}
		case FIFO_HEAD_OVER_READ_MSB:
			/*fifo data frame index + 1*/
			fifo_index = fifo_index + 1;
			if (fifo_index + 1 > fifo_length) {
				last_return_st = FIFO_OVER_READ_RETURN;
				break;
			}
			if (fifo_data[fifo_index] ==
					FIFO_HEAD_OVER_READ_MSB) {
				/*fifo over read frame index + 1*/
				fifo_index = fifo_index + 1;
				break;
			} else {
				last_return_st = FIFO_OVER_READ_RETURN;
				break;
			}
		break;
		default:
			last_return_st = 1;
		break;
		}
			if (last_return_st)
				break;
	}
	return err;
}
static ssize_t bma4xy_show_chip_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	uint8_t chip_id[2] = {0};
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = client_data->device.bus_read(client_data->device.dev_addr,
			BMA4_CHIP_ID_ADDR, chip_id, 2);
	if (err) {
		PERR("falied");
		return err;
	}
	return snprintf(buf, 96, "chip_id=0x%x rev_id=0x%x\n",
		chip_id[0], chip_id[1]);
}

static ssize_t bma4xy_show_acc_op_mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	unsigned char acc_op_mode;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_get_accel_enable(&acc_op_mode, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 96, "1 mean enable now is %d\n", acc_op_mode);
}

#if defined(BMA422)
#define BMA4XY_ACCEL_MIN_VALUE  -2048
#define BMA4XY_ACCEL_MAX_VALUE  2047
#else
#define BMA4XY_ACCEL_MIN_VALUE  -32768
#define BMA4XY_ACCEL_MAX_VALUE  32767
#endif
static int16_t bma4xy_accel_swap_axis(struct bma4xy_client_data *client_data,
				      int axis,
				      struct bma4_accel *rdata)
{
	int16_t data = 0;

	switch (client_data->axis_map[axis]) {
	case 0 :
		data = rdata->x;
		break;
	case 1 :
		data = rdata->y;
		break;
	case 2 :
		data = rdata->z;
		break;
	}

	if (client_data->axis_sign[axis] != 0) {
		if (data <= BMA4XY_ACCEL_MIN_VALUE)
			data = BMA4XY_ACCEL_MIN_VALUE + 1;
		data = -data;
	}

	return data;
}

#if defined(BMA4XY_FOR_SPREADTRUM)

#define BMA4XY_ACCEL_DEFAULT_POLL_INTERVAL_MS	10
#define BMA4XY_ACCEL_MIN_POLL_INTERVAL_MS	10
#define BMA4XY_ACCEL_MAX_POLL_INTERVAL_MS	200
static void bma4xy_accel_work_fn(struct work_struct *work)
{
	struct bma4xy_client_data *client_data;
	struct bma4_accel data = {0};
	#if defined(BMA425)
	uint32_t step_counter_val = 0;
	#endif
	int err;
	client_data = container_of((struct delayed_work *)work,
				struct bma4xy_client_data, accel_poll_work);
	err = bma4_read_accel_xyz(&data, &client_data->device);
	#if defined(BMA425)
	err = bma425_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif

	if (err)
		PERR("read data err");
	input_report_abs(client_data->acc_input, ABS_X,
		(bma4xy_accel_swap_axis(client_data, 0, &data)));
	input_report_abs(client_data->acc_input, ABS_Y,
		(bma4xy_accel_swap_axis(client_data, 1, &data)));
	input_report_abs(client_data->acc_input, ABS_Z,
		(bma4xy_accel_swap_axis(client_data, 2, &data)));
	#if defined(BMA425)
	input_event(client_data->acc_input,
		EV_MSC, MSC_SCAN, step_counter_val);
	#endif
	input_sync(client_data->acc_input);
	if (atomic_read(&client_data->accel_en))
		queue_delayed_work(client_data->data_wq,
			&client_data->accel_poll_work,
			msecs_to_jiffies(client_data->accel_poll_ms));
}

static int bma4xy_accel_set_enable(
	struct bma4xy_client_data *client_data, bool enable)
{
	PINFO("bma4xy_accel_set_enable enable=%d", enable);
	if (enable) {
		queue_delayed_work(client_data->data_wq,
				&client_data->accel_poll_work,
				msecs_to_jiffies(client_data->accel_poll_ms));
		atomic_set(&client_data->accel_en, 1);
	} else {
		atomic_set(&client_data->accel_en, 0);
		cancel_delayed_work_sync(&client_data->accel_poll_work);
	}
	return 0;
}

static int bma4xy_accel_set_poll_delay(
	struct bma4xy_client_data *client_data,
	unsigned long delay)
{
	PINFO("bma4xy_accel_set_poll_delay delay_ms=%ld", delay);
	if (delay < BMA4XY_ACCEL_MIN_POLL_INTERVAL_MS)
		delay = BMA4XY_ACCEL_MIN_POLL_INTERVAL_MS;
	if (delay > BMA4XY_ACCEL_MAX_POLL_INTERVAL_MS)
		delay = BMA4XY_ACCEL_MAX_POLL_INTERVAL_MS;
	client_data->accel_poll_ms = delay;
	if (!atomic_read(&client_data->accel_en))
		goto exit;
	cancel_delayed_work_sync(&client_data->accel_poll_work);
	queue_delayed_work(client_data->data_wq,
			&client_data->accel_poll_work,
			msecs_to_jiffies(client_data->accel_poll_ms));
exit:
	return 0;
}

static ssize_t bma4xy_show_acc_poll_delay(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	return snprintf(buf,
			sizeof(client_data->accel_poll_ms),
			"%d\n",
			client_data->accel_poll_ms);
}

static ssize_t bma4xy_store_acc_poll_delay(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long data;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &data);
	if (err)
		return err;

	err = bma4xy_accel_set_poll_delay(client_data, data);
	if (err) {
		PERR("faliled");
		return -EIO;
	}
	return count;
}
#endif

static ssize_t bma4xy_store_acc_op_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long op_mode;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &op_mode);
	if (err)
		return err;
	if (op_mode == 2 &&
		(client_data->sigmotion_enable == 0) &&
		(client_data->stepdet_enable == 0) &&
		(client_data->stepcounter_enable == 0) &&
		(client_data->tilt_enable == 0) &&
		(client_data->pickup_enable == 0) &&
		(client_data->glance_enable == 0) &&
		(client_data->wakeup_enable == 0) &&
		(client_data->activity_enable == 0)) {
			#ifdef BMA4XY_FOR_SPREADTRUM
			bma4xy_accel_set_enable(client_data,
			BMA4_DISABLE);
			#endif
			err = bma4_set_accel_enable(
				BMA4_DISABLE, &client_data->device);
			PDEBUG("acc_op_mode %ld", op_mode);
		}
	else if (op_mode == 0) {
		err = bma4_set_accel_enable(BMA4_ENABLE, &client_data->device);
		#ifdef BMA4XY_FOR_SPREADTRUM
		bma4xy_accel_set_enable(client_data,
		BMA4_ENABLE);
		#endif
		PDEBUG("acc_op_mode %ld", op_mode);
	}
	if (err) {
		PERR("failed");
		return err;
	} else {
		client_data->pw.acc_pm = op_mode;
		return count;
	}
}

static ssize_t bma4xy_show_acc_value(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct bma4_accel data;
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_read_accel_xyz(&data, &client_data->device);
	if (err < 0)
		return err;
	return snprintf(buf, 48, "%hd %hd %hd\n",
			bma4xy_accel_swap_axis(client_data, 0, &data),
			bma4xy_accel_swap_axis(client_data, 1, &data),
			bma4xy_accel_swap_axis(client_data, 2, &data));
}

static ssize_t bma4xy_show_acc_range(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma4_accel_config acc_config;

	err = bma4_get_accel_config(&acc_config, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 16, "%d\n", acc_config.range);
}

static ssize_t bma4xy_store_acc_range(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long acc_range;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma4_accel_config acc_config;

	err = kstrtoul(buf, 10, &acc_range);
	if (err)
		return err;
	err = bma4_get_accel_config(&acc_config, &client_data->device);
	acc_config.range = (uint8_t)(acc_range);
	err += bma4_set_accel_config(&acc_config, &client_data->device);
	if (err) {
		PERR("faliled");
		return -EIO;
	}
	return count;
}

static ssize_t bma4xy_show_acc_odr(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma4_accel_config acc_config;

	err = bma4_get_accel_config(&acc_config, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	client_data->acc_odr = acc_config.odr;
	return snprintf(buf, 16, "%d\n", client_data->acc_odr);
}

static ssize_t bma4xy_store_acc_odr(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long acc_odr;
	uint8_t data = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &acc_odr);
	if (err)
		return err;
	data = (uint8_t)acc_odr;
	if (acc_odr == 4)
		data = 0x74;
	else
		data |= 0xA0;
	err = client_data->device.bus_write(client_data->device.dev_addr,
		0x40, &data, 1);
	if (err) {
		PERR("faliled");
		return -EIO;
	}
	PDEBUG("acc odr = %d", (uint8_t)acc_odr);
	client_data->acc_odr = acc_odr;
	return count;
}

static ssize_t bma4xy_show_selftest(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	return snprintf(buf, PAGE_SIZE, "%d %d %d %d\n",
			client_data->selftest,
			client_data->selftest_diff.x,
			client_data->selftest_diff.y,
			client_data->selftest_diff.z);
}
static ssize_t bma4xy_store_selftest(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	uint8_t result = 0;
	struct selftest_delta_limit result_accel_data_diff = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_perform_accel_selftest(
		&result, &result_accel_data_diff, &client_data->device);
	if (err) {
		PERR("write failed");
		client_data->selftest = -1;
		client_data->selftest_diff.x = err;
		client_data->selftest_diff.y = 0;
		client_data->selftest_diff.z = 0;
		return err;
	}
	client_data->selftest_diff = result_accel_data_diff;
	if (result == 0) {
		PDEBUG("Selftest successsful");
		client_data->selftest = 1;
	} else
		client_data->selftest = 0;
	return count;
}

static ssize_t bma4xy_show_foc(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	return snprintf(buf, 64,
		"Use echo g_sign aixs > foc to begin foc\n");
}
static ssize_t bma4xy_store_foc(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int g_value[3] = {0};
	int32_t data[3] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	int err = 0;

	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	err = sscanf(buf, "%11d %11d %11d",
		&g_value[0], &g_value[1], &g_value[2]);
	PDEBUG("g_value0=%d, g_value1=%d, g_value2=%d",
		g_value[0], g_value[1], g_value[2]);
	if (err != 3) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	data[0] = g_value[0] * BMA4XY_MULTIPLIER;
	data[1] = g_value[1] * BMA4XY_MULTIPLIER;
	data[2] = g_value[2] * BMA4XY_MULTIPLIER;
	err = bma4_perform_accel_foc(data, &client_data->device);
	if (err) {
		PERR("write failed");
		return err;
	}
	PINFO("FOC successsfully");
	return count;
}
static ssize_t bma4xy_show_config_function(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	return snprintf(buf, PAGE_SIZE,
		"sig_motion0=%d step_detector1=%d step_counter2=%d\n"
		"tilt3=%d pickup4=%d glance_detector5=%d wakeup6=%d\n"
		"any_motion7=%d nomotion8=%d\n"
		"orientation9=%d flat10=%d\n"
		"high_g11=%d low_g12=%d activity13=%d\n",
		client_data->sigmotion_enable, client_data->stepdet_enable,
		client_data->stepcounter_enable, client_data->tilt_enable,
		client_data->pickup_enable, client_data->glance_enable,
		client_data->wakeup_enable, client_data->anymotion_enable,
		client_data->nomotion_enable, client_data->orientation_enable,
		client_data->flat_enable, client_data->highg_enable,
		client_data->lowg_enable, client_data->activity_enable);
}

static ssize_t bma4xy_store_config_function(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int config_func = 0;
	int enable = 0;
	uint8_t feature = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	ret = sscanf(buf, "%11d %11d", &config_func, &enable);
	PDEBUG("config_func = %d, enable=%d", config_func, enable);
	if (ret != 2) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	if (config_func < 0 || config_func > 14)
		return -EINVAL;
	switch (config_func) {
	case BMA4XY_SIG_MOTION_SENSOR:
		#if defined(BMA422)
		feature = BMA422_SIG_MOTION;
		#endif
		#if defined(BMA424)
		feature = BMA424_SIG_MOTION;
		#endif
		#if defined(BMA455)
		feature = BMA455_SIG_MOTION;
		#endif
		client_data->sigmotion_enable = enable;
		break;
	case BMA4XY_STEP_DETECTOR_SENSOR:
		#if defined(BMA421)
		if (bma421_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA421OSC)
		if (bma421osc_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA424SC)
		if (bma424sc_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA424O)
		if (bma424o_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA421LOSC)
		if (bma421losc_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA421L)
		if (bma421l_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA422)
		if (bma422_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA424)
		if (bma424_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA455)
		if (bma455_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		#if defined(BMA456)
		if (bma456_step_detector_enable(
			enable, &client_data->device) < 0) {
			PERR("set BMA4XY_STEP_DETECTOR_SENSOR error");
			return -EINVAL;
		}
		#endif
		client_data->stepdet_enable = enable;
		break;
	case BMA4XY_STEP_COUNTER_SENSOR:
		#if defined(BMA421)
		feature = BMA421_STEP_CNTR;
		#endif
		#if defined(BMA421OSC)
		feature = BMA421OSC_STEP_CNTR;
		#endif
		#if defined(BMA424SC)
		feature = BMA424SC_STEP_CNTR;
		#endif
		#if defined(BMA424O)
		feature = BMA424O_STEP_CNTR;
		#endif
		#if defined(BMA421LOSC)
		feature = BMA421LOSC_STEP_CNTR;
		#endif
		#if defined(BMA421L)
		feature = BMA421L_STEP_CNTR;
		#endif
		#if defined(BMA422)
		feature = BMA422_STEP_CNTR;
		#endif
		#if defined(BMA424)
		feature = BMA424_STEP_CNTR;
		#endif
		#if defined(BMA455)
		feature = BMA455_STEP_CNTR;
		#endif
		#if defined(BMA456)
		feature = BMA456_STEP_CNTR;
		#endif
		client_data->stepcounter_enable = enable;
		break;
	case BMA4XY_TILT_SENSOR:
		#if defined(BMA422)
		feature = BMA422_TILT;
		#endif
		#if defined(BMA424)
		feature = BMA424_TILT;
		#endif
		#if defined(BMA424O)
		feature = BMA424O_TILT;
		#endif
		#if defined(BMA455)
		feature = BMA455_TILT;
		#endif
		#if defined(BMA456)
		feature = BMA456_WRIST_TILT;
		#endif
		client_data->tilt_enable = enable;
		break;
	case BMA4XY_PICKUP_SENSOR:
		#if defined(BMA422)
		feature = BMA422_PICKUP;
		#endif
		#if defined(BMA424)
		feature = BMA424_PICKUP;
		#endif
		#if defined(BMA424O)
		feature = BMA424O_UPHOLD_TO_WAKE;
		#endif
		#if defined(BMA455)
		feature = BMA455_PICKUP;
		#endif
		client_data->pickup_enable = enable;
		break;
	case BMA4XY_GLANCE_DETECTOR_SENSOR:
		#if defined(BMA422)
		feature = BMA422_GLANCE;
		#endif
		#if defined(BMA424)
		feature = BMA424_GLANCE;
		#endif
		#if defined(BMA455)
		feature = BMA455_GLANCE;
		#endif
		client_data->glance_enable = enable;
		break;
	case BMA4XY_WAKEUP_SENSOR:
		#if defined(BMA422)
		feature = BMA422_WAKEUP;
		#endif
		#if defined(BMA424)
		feature = BMA424_WAKEUP;
		#endif
		#if defined(BMA455)
		feature = BMA455_WAKEUP;
		#endif
		#if defined(BMA456)
		feature = BMA456_WAKEUP;
		#endif
		client_data->wakeup_enable = enable;
		break;
	case BMA4XY_ANY_MOTION_SENSOR:
		#if defined(BMA420)
		feature = BMA420_ANY_MOTION;
		#endif
		#if defined(BMA421)
		feature = BMA421_ANY_MOTION;
		#endif
		#if defined(BMA421OSC)
		feature = BMA421OSC_ANY_MOTION;
		#endif
		#if defined(BMA424SC)
		feature = BMA424SC_ANY_MOTION;
		#endif
		#if defined(BMA424O)
		feature = BMA424O_ANY_MOTION;
		#endif
		#if defined(BMA421LOSC)
		feature = BMA421LOSC_ANY_MOTION;
		#endif
		#if defined(BMA421L)
		feature = BMA421L_ANY_MOTION;
		#endif
		#if defined(BMA422)
		feature = BMA422_ANY_MOTION;
		#endif
		#if defined(BMA424)
		feature = BMA424_ANY_MOTION;
		#endif
		#if defined(BMA455)
		feature = BMA455_ANY_MOTION;
		#endif
		#if defined(BMA456)
		feature = BMA456_ANY_MOTION;
		#endif
		client_data->anymotion_enable = enable;
		break;
	case BMA4XY_ORIENTATION_SENSOR:
		#if defined(BMA420)
		feature = BMA420_ORIENTATION;
		#endif
		client_data->orientation_enable = enable;
		break;
	case BMA4XY_FLAT_SENSOR:
		#if defined(BMA420)
		feature = BMA420_FLAT;
		#endif
		#if defined(BMA421LOSC)
		feature = BMA421LOSC_FLAT;
		#endif
		#if defined(BMA421L)
		feature = BMA421L_FLAT;
		#endif
		client_data->flat_enable = enable;
		break;
	case BMA4XY_TAP_SENSOR:
		#if defined(BMA420)
		feature = BMA420_TAP;
		#endif
		client_data->tap_enable = enable;
		break;
	case BMA4XY_HIGH_G_SENSOR:
		#if defined(BMA420)
		feature = BMA420_HIGH_G;
		#endif
		client_data->highg_enable = enable;
		break;
	case BMA4XY_LOW_G_SENSOR:
		#if defined(BMA420)
		feature = BMA420_LOW_G;
		#endif
		client_data->lowg_enable = enable;
		break;
	case BMA4XY_ACTIVITY_SENSOR:
		#if defined(BMA421)
		feature = BMA421_ACTIVITY;
		#endif
		#if defined(BMA421L)
		feature = BMA421L_ACTIVITY;
		#endif
		#if defined(BMA424SC)
		feature = BMA424SC_ACTIVITY;
		#endif
		#if defined(BMA424O)
		feature = BMA424O_ACTIVITY;
		#endif
		client_data->activity_enable = enable;
		break;
	default:
		PERR("Invalid sensor handle: %d", config_func);
		return -EINVAL;
	}
	#if defined(BMA420)
	if (bma420_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma420 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA421)
	if (bma421_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma421 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA421OSC)
	if (bma421osc_feature_enable(
		feature, enable, &client_data->device) < 0) {
		PERR("set bma421 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA424SC)
	if (bma424sc_feature_enable(
		feature, enable, &client_data->device) < 0) {
		PERR("set bma421 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA424O)
	if (bma424o_feature_enable(
		feature, enable, &client_data->device) < 0) {
		PERR("set bma421 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA421LOSC)
	if (bma421losc_feature_enable(
		feature, enable, &client_data->device) < 0) {
		PERR("set bma421 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA421L)
	if (bma421l_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma421 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA422)
	if (bma422_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma422 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA424)
	if (bma424_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma422 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA455)
	if (bma455_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma455 virtual error");
		return -EINVAL;
	}
	#endif
	#if defined(BMA456)
	if (bma456_feature_enable(feature, enable, &client_data->device) < 0) {
		PERR("set bma455 virtual error");
		return -EINVAL;
	}
	#endif
	return count;
}
static ssize_t bma4xy_store_axis_remapping(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
#if defined(BMA420) || defined(BMA421) || defined(BMA421LOSC) || \
defined(BMA421L) || defined(BMA422) || defined(BMA455) || \
defined(BMA424SC) || defined(BMA424) || defined(BMA421OSC) || \
defined(BMA424O) || defined(BMA456)
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
#endif
	int data[6] = {0};
	#if defined(BMA420)
	struct bma420_axes_remap axis_remap_data;
	#elif defined(BMA421)
	struct bma421_axes_remap axis_remap_data;
	#elif defined(BMA421OSC)
	struct bma421osc_axes_remap axis_remap_data;
	#elif defined(BMA424SC)
	struct bma424sc_axes_remap axis_remap_data;
	#elif defined(BMA424O)
	struct bma424o_axes_remap axis_remap_data;
	#elif defined(BMA421LOSC)
	struct bma421losc_axes_remap axis_remap_data;
	#elif defined(BMA421L)
	struct bma421l_axes_remap axis_remap_data;
	#elif defined(BMA422)
	struct bma422_axes_remap axis_remap_data;
	#elif defined(BMA424)
	struct bma424_axes_remap axis_remap_data;
	#elif defined(BMA455)
	struct bma455_axes_remap axis_remap_data;
	#elif defined(BMA456)
	struct bma456_axes_remap axis_remap_data;
	#endif

	err = sscanf(buf, "%11d %11d %11d %11d %11d %11d",
	&data[0], &data[1], &data[2], &data[3], &data[4], &data[5]);
	if (err != 6) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	axis_remap_data.x_axis = (uint8_t)data[0];
	axis_remap_data.x_axis_sign = (uint8_t)data[1];
	axis_remap_data.y_axis = (uint8_t)data[2];
	axis_remap_data.y_axis_sign = (uint8_t)data[3];
	axis_remap_data.z_axis = (uint8_t)data[4];
	axis_remap_data.z_axis_sign = (uint8_t)data[5];
	PDEBUG("x_axis = %d x_axis_sign=%d",
	axis_remap_data.x_axis, axis_remap_data.x_axis_sign);
	PDEBUG("y_axis = %d y_axis_sign=%d",
	axis_remap_data.y_axis, axis_remap_data.y_axis_sign);
	PDEBUG("z_axis = %d z_axis_sign=%d",
	axis_remap_data.z_axis, axis_remap_data.z_axis_sign);
	#if defined(BMA420)
	err = bma420_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA421)
	err = bma421_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return -EIO;
	}
	return count;
}
static ssize_t bma4xy_show_fifo_length(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	uint16_t fifo_bytecount = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_get_fifo_length(&fifo_bytecount, &client_data->device);
	if (err) {
		PERR("read falied");
		return err;
	}
	return snprintf(buf, 96, "%d\n", fifo_bytecount);
}
static ssize_t bma4xy_store_fifo_flush(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long enable;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &enable);
	if (err)
		return err;
	if (enable)
		err = bma4_set_command_register(0xb0, &client_data->device);
	if (err) {
		PERR("write failed");
		return -EIO;
	}
	return count;
}
static ssize_t bma4xy_show_fifo_acc_enable(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	uint8_t fifo_acc_enable;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_get_fifo_config(&fifo_acc_enable, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 16, "%x\n", fifo_acc_enable);
}
static ssize_t bma4xy_store_fifo_acc_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long data;
	unsigned char fifo_acc_enable;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &data);
	if (err)
		return err;
	fifo_acc_enable = (unsigned char)data;
	err = bma4_set_fifo_config(
		BMA4_FIFO_ACCEL, fifo_acc_enable, &client_data->device);
	if (err) {
		PERR("faliled");
		return -EIO;
	}
	client_data->fifo_acc_enable = fifo_acc_enable;
	return count;
}

static ssize_t bma4xy_show_load_config_stream(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	return snprintf(buf, 48, "config stream %s\n",
		client_data->config_stream_name);
}

int bma4xy_init_after_config_stream_load(
	struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t int_enable = 0x0a;
	uint8_t latch_enable = 0x01;
	uint8_t int1_map = 0xff;
	#if defined(BMA420)
	struct bma420_axes_remap axis_remap_data;
	#elif defined(BMA421)
	struct bma421_axes_remap axis_remap_data;
	#elif defined(BMA421OSC)
	struct bma421osc_axes_remap axis_remap_data;
	#elif defined(BMA424SC)
	struct bma424sc_axes_remap axis_remap_data;
	#elif defined(BMA424O)
	struct bma424o_axes_remap axis_remap_data;
	#elif defined(BMA421LOSC)
	struct bma421losc_axes_remap axis_remap_data;
	#elif defined(BMA421L)
	struct bma421l_axes_remap axis_remap_data;
	#elif defined(BMA422)
	struct bma422_axes_remap axis_remap_data;
	#elif defined(BMA424)
	struct bma424_axes_remap axis_remap_data;
	#elif defined(BMA455)
	struct bma455_axes_remap axis_remap_data;
	#elif defined(BMA456)
	struct bma456_axes_remap axis_remap_data;
	#endif
	err = bma4_write_regs(
	BMA4_INT_MAP_1_ADDR, &int1_map, 1, &client_data->device);
	bma4xy_i2c_delay(10);
	err += bma4_write_regs(
	BMA4_INT1_IO_CTRL_ADDR, &int_enable, 1, &client_data->device);
	bma4xy_i2c_delay(1);
	err += bma4_write_regs(
	BMA4_INTR_LATCH_ADDR, &latch_enable, 1, &client_data->device);
	bma4xy_i2c_delay(1);
	if (err)
		PERR("map and enable interrupr1 failed err=%d", err);
	memset(&axis_remap_data, 0, sizeof(axis_remap_data));
	axis_remap_data.x_axis = 0;
	axis_remap_data.x_axis_sign = 0;
	axis_remap_data.y_axis = 1;
	axis_remap_data.y_axis_sign = 1;
	axis_remap_data.z_axis = 2;
	axis_remap_data.z_axis_sign = 1;
	#if defined(BMA424O)
	axis_remap_data.x_axis = 1;
	axis_remap_data.x_axis_sign = 0;
	axis_remap_data.y_axis = 0;
	axis_remap_data.y_axis_sign = 0;
	axis_remap_data.z_axis = 2;
	axis_remap_data.z_axis_sign = 1;
	#endif
	#if defined(BMA420)
	err = bma420_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA421)
	err = bma421_set_remap_axes(&axis_remap_data, &client_data->device);
	err = bma421_select_platform(BMA421_PHONE_CONFIG, &client_data->device);
	if (err)
		PERR("set bma421 step_count select platform error");
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_set_remap_axes(&axis_remap_data, &client_data->device);
	err = bma424sc_select_platform(
		BMA424SC_PHONE_CONFIG, &client_data->device);
	if (err)
		PERR("set bma421 step_count select platform error");
	#endif
	#if defined(BMA424O)
	err = bma424o_set_remap_axes(&axis_remap_data, &client_data->device);
	err = bma424o_select_platform(
		BMA424O_PHONE_CONFIG, &client_data->device);
	if (err)
		PERR("set bma424O step_count select platform error");
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_set_remap_axes(&axis_remap_data, &client_data->device);
	#endif
	if (err) {
		PERR("write axis_remap failed");
		return err;
	}
	return err;
}

void bma4xy_init_axis_mapping(
	struct bma4xy_client_data *client_data)
{
	const char *axis;
	int i;
	int err;

	for (i = 0; i < 3; i++) {
		axis = NULL;
		err = of_property_read_string_index(client_data->dev->of_node,
						    "bma4xy,axis_map",
						    i,
						    &axis);
		if ((err == 0) && axis) {
			if (axis[0] == '-')
				client_data->axis_sign[i] = 1;
			else
				client_data->axis_sign[i] = 0;

			switch (axis[1]) {
			case 'x' :
				client_data->axis_map[i] = 0;
				break;
			case 'y' :
				client_data->axis_map[i] = 1;
				break;
			case 'z' :
				client_data->axis_map[i] = 2;
				break;
			default :
				client_data->axis_map[i] = i;
				break;
			}
		} else {
			client_data->axis_sign[i] = 0;
			client_data->axis_map[i] = i;
		}
	}

	#if defined(CONFIG_ICX_DMP_BOARD_ID)
	/* overwrite for BB */
	if (icx_dmp_board_id.bid == ICX_DMP_BID_BB) {
		client_data->axis_sign[0] = 1;
		client_data->axis_map[0] = 1;  /* X <- inverted Y */
		client_data->axis_sign[1] = 0;
		client_data->axis_map[1] = 0;  /* Y <- X */
		client_data->axis_sign[2] = 0;
		client_data->axis_map[2] = 2;  /* Z <- Z */
	}
	#endif
}

int bma4xy_init_fifo_config(
	struct bma4xy_client_data *client_data)
{
	int err = 0;
	err = bma4_set_fifo_config(
		BMA4_FIFO_HEADER, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("enable fifo header failed err=%d", err);
	err = bma4_set_fifo_config(
		BMA4_FIFO_TIME, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("enable fifo timer failed err=%d", err);
	return err;
}

int bma4xy_update_config_stream(
	struct bma4xy_client_data *client_data, int choose)
{
	const struct firmware *config_entry;
	char *name;
	int err = 0;

	uint8_t crc_check = 0;

	switch (choose) {
	case 1:
	name = "android.tbin";
	break;
	case 2:
	name = "legacy.tbin";
	break;
	default:
	PERR("no choose fw = %d,use dafault", choose);
	name = "bma4xy_config_stream";
	break;
	}
	PDEBUG("choose the config_stream %s", name);
	if ((choose == 1) || (choose == 2)) {
		err = request_firmware(&config_entry,
			name, &client_data->acc_input->dev);
		if (err < 0) {
			PERR("Failed to get config_stream from vendor path");
			return -EINVAL;
		}
		client_data->config_stream_name = name;
		client_data->device.config_file_ptr = config_entry->data;
		bma4_write_config_file(&client_data->device);
		err = bma4_read_regs(BMA4_INTERNAL_STAT,
		&crc_check, 1, &client_data->device);
		if (err)
			PERR("reading CRC failer");
		if (crc_check != BMA4_ASIC_INITIALIZED)
			PERR("crc check error %x", crc_check);
		release_firmware(config_entry);
		bma4xy_i2c_delay(10);
	} else if (choose == 3) {
		#if defined(BMA420)
		err = bma420_write_config_file(&client_data->device);
		#endif
		#if defined(BMA421)
		err = bma421_write_config_file(&client_data->device);
		#endif
		#if defined(BMA421OSC)
		err = bma421osc_write_config_file(&client_data->device);
		#endif
		#if defined(BMA424SC)
		err = bma424sc_write_config_file(&client_data->device);
		#endif
		#if defined(BMA424O)
		err = bma424o_write_config_file(&client_data->device);
		#endif
		#if defined(BMA421LOSC)
		err = bma421losc_write_config_file(&client_data->device);
		#endif
		#if defined(BMA421L)
		err = bma421l_write_config_file(&client_data->device);
		#endif
		#if defined(BMA422)
		err = bma422_write_config_file(&client_data->device);
		#endif
		#if defined(BMA424)
		err = bma424_write_config_file(&client_data->device);
		#endif
		#if defined(BMA455)
		err = bma455_write_config_file(&client_data->device);
		#endif
		#if defined(BMA456)
		err = bma456_write_config_file(&client_data->device);
		#endif
		if (err)
			PERR("download config stream failer");
		bma4xy_i2c_delay(200);
		err = bma4_read_regs(BMA4_INTERNAL_STAT,
		&crc_check, 1, &client_data->device);
		if (err)
			PERR("reading CRC failer");
		if (crc_check != BMA4_ASIC_INITIALIZED)
			PERR("crc check error %x", crc_check);
	}
	return err;
}

static ssize_t bma4xy_store_load_config_stream(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long choose = 0;
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &choose);
	if (err)
		return err;
	PDEBUG("config_stream_choose %ld", choose);
	err = bma4xy_update_config_stream(client_data, choose);
	if (err) {
		PERR("config_stream load error");
		return count;
	}
	err = bma4xy_init_after_config_stream_load(client_data);
	if (err) {
		PERR("bma4xy_init_after_config_stream_load error");
		return count;
	}
	return count;
}

static ssize_t bma4xy_show_fifo_watermark(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	uint16_t data;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_get_fifo_wm(&data, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 48, "%d\n", data);
}

static ssize_t bma4xy_store_fifo_watermark(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long data;
	unsigned char fifo_watermark;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &data);
	if (err)
		return err;
	fifo_watermark = (unsigned char)data;
	err = bma4_set_fifo_wm(fifo_watermark, &client_data->device);
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}

static ssize_t bma4xy_show_fifo_data_out_frame(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	int err = 0;
	uint16_t fifo_bytecount = 0;

	if (!client_data->fifo_mag_enable && !client_data->fifo_acc_enable) {
		PERR("no select sensor fifo");
		return -EINVAL;
	}
	err = bma4_get_fifo_length(&fifo_bytecount, &client_data->device);
	if (err < 0) {
		PERR("read fifo_len err=%d", err);
		return -EINVAL;
	}
	if (fifo_bytecount == 0)
		return 0;
	err =  bma4_read_regs(BMA4_FIFO_DATA_ADDR,
		buf, fifo_bytecount, &client_data->device);
	if (err) {
		PERR("read fifo leght err");
		return -EINVAL;
	}
	return fifo_bytecount;
}

static ssize_t bma4xy_show_reg_sel(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	return snprintf(buf, 64, "reg=0X%02X, len=%d\n",
		client_data->reg_sel, client_data->reg_len);
}

static ssize_t bma4xy_store_reg_sel(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	int err;

	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	err = sscanf(buf, "%11X %11d",
		&client_data->reg_sel, &client_data->reg_len);
	if (err != 2) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	return count;
}

static ssize_t bma4xy_show_reg_val(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	int err = 0;
	u8 reg_data[128], i;
	int pos;

	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	err = client_data->device.bus_read(client_data->device.dev_addr,
		client_data->reg_sel,
		reg_data, client_data->reg_len);
	if (err < 0) {
		PERR("Reg op failed");
		return err;
	}
	pos = 0;
	for (i = 0; i < client_data->reg_len; ++i) {
		pos += snprintf(buf + pos, 16, "%02X", reg_data[i]);
		buf[pos++] = (i + 1) % 16 == 0 ? '\n' : ' ';
	}
	if (buf[pos - 1] == ' ')
		buf[pos - 1] = '\n';
	return pos;
}

static ssize_t bma4xy_store_reg_val(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	int err;
	u8 reg_data[128];
	int i, j, status, digit;

	if (client_data == NULL) {
		PERR("Invalid client_data pointer");
		return -ENODEV;
	}
	status = 0;
	for (i = j = 0; i < count && j < client_data->reg_len; ++i) {
		if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t' ||
			buf[i] == '\r') {
			status = 0;
			++j;
			continue;
		}
		digit = buf[i] & 0x10 ? (buf[i] & 0xF) : ((buf[i] & 0xF) + 9);
		PDEBUG("digit is %d", digit);
		switch (status) {
		case 2:
			++j; /* Fall thru */
		case 0:
			reg_data[j] = digit;
			status = 1;
			break;
		case 1:
			reg_data[j] = reg_data[j] * 16 + digit;
			status = 2;
			break;
		}
	}
	if (status > 0)
		++j;
	if (j > client_data->reg_len)
		j = client_data->reg_len;
	else if (j < client_data->reg_len) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	PDEBUG("Reg data read as");
	for (i = 0; i < j; ++i)
		PDEBUG("%d", reg_data[i]);
	err = client_data->device.bus_write(client_data->device.dev_addr,
		client_data->reg_sel,
		reg_data, client_data->reg_len);
	if (err < 0) {
		PERR("Reg op failed");
		return err;
	}
	return count;
}

static ssize_t bma4xy_show_driver_version(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 128,
		"Driver version: %s\n", DRIVER_VERSION);
}
static ssize_t bma4xy_show_config_file_version(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	uint16_t version = 0;
#if defined(BMA420) || defined(BMA421) || defined(BMA421LOSC) || \
defined(BMA422) || defined(BMA455) || defined(BMA424SC) || defined(BMA424) || \
defined(BMA421OSC) || defined(BMA424O) || defined(BMA421L) || defined(BMA456)
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
#endif

	#if defined(BMA420)
	err = bma420_get_config_id(&version, &client_data->device);
	#elif defined(BMA421)
	err = bma421_get_config_id(&version, &client_data->device);
	#elif defined(BMA421OSC)
	err = bma421osc_get_config_id(&version, &client_data->device);
	#elif defined(BMA424SC)
	err = bma424sc_get_config_id(&version, &client_data->device);
	#elif defined(BMA424O)
	err = bma424o_get_config_id(&version, &client_data->device);
	#elif defined(BMA421LOSC)
	err = bma421losc_get_config_id(&version, &client_data->device);
	#elif defined(BMA421L)
	err = bma421l_get_config_id(&version, &client_data->device);
	#elif defined(BMA422)
	err = bma422_get_config_id(&version, &client_data->device);
	#elif defined(BMA424)
	err = bma424_get_config_id(&version, &client_data->device);
	#elif defined(BMA455)
	err = bma455_get_config_id(&version, &client_data->device);
	#elif defined(BMA456)
	err = bma456_get_config_id(&version, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 128,
		"Driver version: %s Config_stream version :0x%x\n",
		DRIVER_VERSION, version);
}

static ssize_t bma4xy_show_avail_sensor(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	uint16_t avail_sensor = 0;
	#if defined(BMA420)
		avail_sensor = 420;
	#elif defined(BMA421)
		avail_sensor = 421;
	#elif defined(BMA421OSC)
		avail_sensor = 421;
	#elif defined(BMA424SC)
		avail_sensor = 421;
	#elif defined(BMA424O)
		avail_sensor = 422;
	#elif defined(BMA421LOSC)
		avail_sensor = 421;
	#elif defined(BMA421L)
		avail_sensor = 421;
	#elif defined(BMA422)
		avail_sensor = 422;
	#elif defined(BMA424)
		avail_sensor = 422;
	#elif defined(BMA455)
		avail_sensor = 455;
	#elif defined(BMA456)
		avail_sensor = 455;
	#endif
	return snprintf(buf, 32, "%d\n", avail_sensor);
}
#if defined(BMA422) || defined(BMA455) || defined(BMA424)
static ssize_t bma4xy_show_sig_motion_config(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	#if defined(BMA422)
	struct bma422_sig_motion_config sig_m_config;
	#elif defined(BMA455)
	struct bma455_sig_motion_config sig_m_config;
	#elif defined(BMA424)
	struct bma424_sig_motion_config sig_m_config;
	#endif

	#if defined(BMA422)
	err = bma422_get_sig_motion_config(&sig_m_config, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_get_sig_motion_config(&sig_m_config, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_get_sig_motion_config(&sig_m_config, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, PAGE_SIZE,
	"threshold =0x%x skiptime= 0x%x prooftime = 0x%x\n",
	sig_m_config.threshold, sig_m_config.skiptime, sig_m_config.prooftime);
}

static ssize_t bma4xy_store_sig_motion_config(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned int data[3] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	#if defined(BMA422)
	struct bma422_sig_motion_config sig_m_config;
	#elif defined(BMA455)
	struct bma455_sig_motion_config sig_m_config;
	#elif defined(BMA424)
	struct bma424_sig_motion_config sig_m_config;
	#endif

	err = sscanf(buf, "%11x %11x %11x", &data[0], &data[1], &data[2]);
	if (err != 3) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	memset(&sig_m_config, 0, sizeof(sig_m_config));
	sig_m_config.threshold = (uint16_t)data[0];
	sig_m_config.skiptime = (uint16_t)data[1];
	sig_m_config.prooftime = (uint8_t)data[2];
	#if defined(BMA422)
	err = bma422_set_sig_motion_config(&sig_m_config, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_set_sig_motion_config(&sig_m_config, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_set_sig_motion_config(&sig_m_config, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
#endif
#if defined(BMA422) || defined(BMA455) || defined(BMA424) || defined(BMA424O)
static ssize_t bma4xy_show_tilt_threshold(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	uint8_t tilt_threshold;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	#if defined(BMA422)
	err = bma422_tilt_get_threshold(&tilt_threshold, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_tilt_get_threshold(&tilt_threshold, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_tilt_get_threshold(&tilt_threshold, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_tilt_get_threshold(&tilt_threshold, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 32, "%d\n", tilt_threshold);
}
static ssize_t bma4xy_store_tilt_threshold(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long tilt_threshold;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &tilt_threshold);
	if (err)
		return err;
	PDEBUG("tilt_threshold %ld", tilt_threshold);
	#if defined(BMA422)
	err = bma422_tilt_set_threshold(tilt_threshold, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_tilt_set_threshold(tilt_threshold, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_tilt_set_threshold(tilt_threshold, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_tilt_set_threshold(tilt_threshold, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
#endif
#if !defined(BMA420)
static ssize_t bma4xy_show_step_counter_val(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	uint32_t step_counter_val = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	#if defined(BMA421)
	err = bma421_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_step_counter_output(
	&step_counter_val, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	PDEBUG("val %u", step_counter_val);
	if (client_data->err_int_trigger_num == 0) {
		client_data->step_counter_val = step_counter_val;
		PDEBUG("report val %u", client_data->step_counter_val);
		err = snprintf(buf, 96, "%u\n", client_data->step_counter_val);
		client_data->step_counter_temp = client_data->step_counter_val;
	} else {
		PDEBUG("after err report val %u",
			client_data->step_counter_val + step_counter_val);
		err = snprintf(buf, 96, "%u\n",
			client_data->step_counter_val + step_counter_val);
		client_data->step_counter_temp =
			client_data->step_counter_val + step_counter_val;
	}
	return err;
}
static ssize_t bma4xy_show_step_counter_watermark(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	uint16_t watermark;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	#if defined(BMA421)
	err = bma421_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_step_counter_get_watermark(
	&watermark, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 32, "%d\n", watermark);
}
static ssize_t bma4xy_store_step_counter_watermark(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long step_watermark;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &step_watermark);
	if (err)
		return err;
	PDEBUG("watermark step_counter %ld", step_watermark);
	#if defined(BMA421)
	err = bma421_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_step_counter_set_watermark(
	step_watermark, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}

static ssize_t bma4xy_show_step_counter_parameter(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	#if defined(BMA421)
	struct bma421_stepcounter_settings setting;
	#elif defined(BMA421OSC)
	struct bma421osc_stepcounter_settings setting;
	#elif defined(BMA424SC)
	struct bma424sc_stepcounter_settings setting;
	#elif defined(BMA424O)
	struct bma424o_stepcounter_settings setting;
	#elif defined(BMA421LOSC)
	struct bma421losc_stepcounter_settings setting;
	#elif defined(BMA421L)
	struct bma421l_stepcounter_settings setting;
	#elif defined(BMA422)
	struct bma422_stepcounter_settings setting;
	#elif defined(BMA424)
	struct bma424_stepcounter_settings setting;
	#elif defined(BMA455)
	struct bma455_stepcounter_settings setting;
	#elif defined(BMA456)
	struct bma456_stepcounter_settings setting;
	#endif
	#if defined(BMA421)
	err = bma421_stepcounter_get_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_stepcounter_get_parameter(
	&setting, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_stepcounter_get_parameter(
	&setting, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_stepcounter_get_parameter(
	&setting, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_stepcounter_get_parameter(
	&setting, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_stepcounter_get_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_stepcounter_get_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_stepcounter_get_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_stepcounter_get_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_stepcounter_get_parameter(&setting, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}

#if defined(BMA421LOSC) || defined(BMA422)
	return snprintf(buf, PAGE_SIZE,
	"parameter1 =0x%x parameter2= 0x%x\n"
	"parameter3 = 0x%x parameter4 = 0x%x\n"
	"parameter5 = 0x%x parameter6 = 0x%x\n"
	"parameter7 = 0x%x\n",
	setting.param1, setting.param2, setting.param3, setting.param4,
	setting.param5, setting.param6, setting.param7);
#elif defined(BMA455) || defined(BMA424) || defined(BMA421OSC)
	return snprintf(buf, PAGE_SIZE,
	"parameter1 =0x%x parameter2= 0x%x\n"
	"parameter3 = 0x%x parameter4 = 0x%x\n"
	"parameter5 = 0x%x parameter6 = 0x%x\n"
	"parameter7 = 0x%x\n",
	setting.param1, setting.param2, setting.param3, setting.param4,
	setting.param5, setting.param6, setting.param7);
#elif defined(BMA421) || defined(BMA424SC) || \
defined(BMA421L) || defined(BMA424O) || defined(BMA456)
	return snprintf(buf, PAGE_SIZE,
	"parameter1 =0x%x parameter2= 0x%x\n"
	"parameter3 = 0x%x parameter4 = 0x%x\n"
	"parameter5 = 0x%x parameter6 = 0x%x\n"
	"parameter7 = 0x%x parameter8 = 0x%x\n"
	"parameter9 = 0x%x parameter10 = 0x%x\n"
	"parameter11 = 0x%x parameter12 = 0x%x\n"
	"parameter13 = 0x%x parameter14 = 0x%x\n"
	"parameter15 = 0x%x parameter16 = 0x%x\n"
	"parameter17 = 0x%x parameter18 = 0x%x\n"
	"parameter19 = 0x%x parameter20 = 0x%x\n"
	"parameter21 = 0x%x parameter22 = 0x%x\n"
	"parameter23 = 0x%x parameter24 = 0x%x\n"
	"parameter25 = 0x%x\n",
	setting.param1, setting.param2, setting.param3, setting.param4,
	setting.param5, setting.param6, setting.param7, setting.param8,
	setting.param9, setting.param10, setting.param11, setting.param12,
	setting.param13, setting.param14, setting.param15, setting.param16,
	setting.param17, setting.param18, setting.param19, setting.param20,
	setting.param21, setting.param22, setting.param23, setting.param24,
	setting.param25);
#endif
}
static ssize_t bma4xy_store_step_counter_parameter(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	#if defined(BMA421)
	unsigned int data[25] = {0};
	struct bma421_stepcounter_settings setting;
	#elif defined(BMA424SC)
	unsigned int data[25] = {0};
	struct bma424sc_stepcounter_settings setting;
	#elif defined(BMA456)
	unsigned int data[25] = {0};
	struct bma456_stepcounter_settings setting;
	#elif defined(BMA424O)
	unsigned int data[25] = {0};
	struct bma424o_stepcounter_settings setting;
	#elif defined(BMA421LOSC)
	unsigned int data[7] = {0};
	struct bma421losc_stepcounter_settings setting;
	#elif defined(BMA421L)
	unsigned int data[25] = {0};
	struct bma421l_stepcounter_settings setting;
	#elif defined(BMA421OSC)
	unsigned int data[7] = {0};
	struct bma421osc_stepcounter_settings setting;
	#elif defined(BMA422)
	unsigned int data[7] = {0};
	struct bma422_stepcounter_settings setting;
	#elif defined(BMA424)
	unsigned int data[7] = {0};
	struct bma424_stepcounter_settings setting;
	#elif defined(BMA455)
	unsigned int data[7] = {0};
	struct bma455_stepcounter_settings setting;
	#endif

#if defined(BMA421LOSC) || defined(BMA422) || defined(BMA455) || \
defined(BMA424) || defined(BMA421OSC)
	err = sscanf(buf, "%11x %11x %11x %11x %11x %11x %11x",
	&data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &data[6]);
	if (err != 7) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	setting.param1 = (uint16_t)data[0];
	setting.param2 = (uint16_t)data[1];
	setting.param3 = (uint16_t)data[2];
	setting.param4 = (uint16_t)data[3];
	setting.param5 = (uint16_t)data[4];
	setting.param6 = (uint16_t)data[5];
	setting.param7 = (uint16_t)data[6];
#elif defined(BMA421) || defined(BMA424SC) || \
defined(BMA421L) || defined(BMA424O) || defined(BMA456)
	err = sscanf(buf,
	"%11x %11x %11x %11x %11x %11x %11x %11x\n"
	"%11x %11x %11x %11x %11x %11x %11x %11x\n"
	"%11x %11x %11x %11x %11x %11x %11x %11x\n"
	"%11x\n",
	&data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &data[6],
	&data[7], &data[8], &data[9], &data[10], &data[11], &data[12],
	&data[13],
	&data[14], &data[15], &data[16], &data[17], &data[18], &data[19],
	&data[20],
	&data[21], &data[22], &data[23], &data[24]);
	if (err != 25) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	setting.param1 = (uint16_t)data[0];
	setting.param2 = (uint16_t)data[1];
	setting.param3 = (uint16_t)data[2];
	setting.param4 = (uint16_t)data[3];
	setting.param5 = (uint16_t)data[4];
	setting.param6 = (uint16_t)data[5];
	setting.param7 = (uint16_t)data[6];
	setting.param8 = (uint16_t)data[7];
	setting.param9 = (uint16_t)data[8];
	setting.param10 = (uint16_t)data[9];
	setting.param11 = (uint16_t)data[10];
	setting.param12 = (uint16_t)data[11];
	setting.param13 = (uint16_t)data[12];
	setting.param14 = (uint16_t)data[13];
	setting.param15 = (uint16_t)data[14];
	setting.param16 = (uint16_t)data[15];
	setting.param17 = (uint16_t)data[16];
	setting.param18 = (uint16_t)data[17];
	setting.param19 = (uint16_t)data[18];
	setting.param20 = (uint16_t)data[19];
	setting.param21 = (uint16_t)data[20];
	setting.param22 = (uint16_t)data[21];
	setting.param23 = (uint16_t)data[22];
	setting.param24 = (uint16_t)data[23];
	setting.param25 = (uint16_t)data[24];
#endif
	#if defined(BMA421)
	err = bma421_stepcounter_set_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_stepcounter_set_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_stepcounter_set_parameter(
	&setting, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_stepcounter_set_parameter(
		&setting, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_stepcounter_set_parameter(
		&setting, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_stepcounter_set_parameter(
	&setting, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_stepcounter_set_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_stepcounter_set_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_stepcounter_set_parameter(&setting, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_stepcounter_set_parameter(&setting, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}

static ssize_t bma4xy_store_step_counter_reset(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long reset_counter;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &reset_counter);
	if (err)
		return err;
	PDEBUG("reset_counter %ld", reset_counter);
	#if defined(BMA421)
	err = bma421_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_reset_step_counter(&client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_reset_step_counter(&client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	client_data->step_counter_val = 0;
	client_data->step_counter_temp = 0;
	return count;
}
#endif
static ssize_t bma4xy_show_anymotion_config(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	#if defined(BMA420)
	struct bma420_anymotion_config any_motion;
	#elif defined(BMA421)
	struct bma421_anymotion_config any_motion;
	#elif defined(BMA421OSC)
	struct bma421osc_anymotion_config any_motion;
	#elif defined(BMA424SC)
	struct bma424sc_anymotion_config any_motion;
	#elif defined(BMA424O)
	struct bma424o_anymotion_config any_motion;
	#elif defined(BMA421LOSC)
	struct bma421losc_anymotion_config any_motion;
	#elif defined(BMA421L)
	struct bma421l_anymotion_config any_motion;
	#elif defined(BMA422)
	struct bma422_anymotion_config any_motion;
	#elif defined(BMA424)
	struct bma424_anymotion_config any_motion;
	#elif defined(BMA455)
	struct bma455_anymotion_config any_motion;
	#elif defined(BMA456)
	struct bma456_anymotion_config any_motion;
	#endif

	#if defined(BMA420)
	err = bma420_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA421)
	err = bma421_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_get_any_motion_config(
	&any_motion, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_get_any_motion_config(
	&any_motion, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_get_any_motion_config(&any_motion, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, PAGE_SIZE,
	"duration =0x%x threshold= 0x%x nomotion_sel = 0x%x\n",
	any_motion.duration, any_motion.threshold,
	any_motion.nomotion_sel);

}
static ssize_t bma4xy_store_anymotion_config(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned int data[3] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	#if defined(BMA420)
	struct bma420_anymotion_config any_motion;
	#elif defined(BMA421)
	struct bma421_anymotion_config any_motion;
	#elif defined(BMA421OSC)
	struct bma421osc_anymotion_config any_motion;
	#elif defined(BMA424SC)
	struct bma424sc_anymotion_config any_motion;
	#elif defined(BMA424O)
	struct bma424o_anymotion_config any_motion;
	#elif defined(BMA421LOSC)
	struct bma421losc_anymotion_config any_motion;
	#elif defined(BMA421L)
	struct bma421l_anymotion_config any_motion;
	#elif defined(BMA422)
	struct bma422_anymotion_config any_motion;
	#elif defined(BMA424)
	struct bma424_anymotion_config any_motion;
	#elif defined(BMA455)
	struct bma455_anymotion_config any_motion;
	#elif defined(BMA456)
	struct bma456_anymotion_config any_motion;
	#endif

	err = sscanf(buf, "%11x %11x %11x", &data[0], &data[1], &data[2]);
	if (err != 3) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	memset(&any_motion, 0, sizeof(any_motion));
	any_motion.duration = (uint16_t)data[0];
	any_motion.threshold = (uint16_t)data[1];
	any_motion.nomotion_sel = (uint8_t)data[2];
	#if defined(BMA420)
	err = bma420_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA421)
	err = bma421_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_set_any_motion_config(
	&any_motion, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_set_any_motion_config(
	&any_motion, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_set_any_motion_config(&any_motion, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
static ssize_t bma4xy_store_anymotion_enable_axis(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long data;
	uint8_t enable_axis;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &data);
	if (err)
		return err;
	PDEBUG("enable_axis %ld", data);
	enable_axis = (uint8_t)data;
	#if defined(BMA420)
	err = bma420_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA421)
	err = bma421_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_anymotion_enable_axis(
	enable_axis, &client_data->device);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA424O)
	err = bma424o_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_anymotion_enable_axis(
	enable_axis, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA422)
	err = bma422_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA424)
	err = bma424_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA455)
	err = bma455_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	#if defined(BMA456)
	err = bma456_anymotion_enable_axis(enable_axis, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}


#if defined(BMA420)
static ssize_t bma4xy_show_orientation_config(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_orientation_config orientation;

	err = bma420_get_orientation_config(&orientation, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, PAGE_SIZE,
	"upside_down =0x%x mode= 0x%x\n"
	"blocking = 0x%x hysteresis = 0x%x\n",
	orientation.upside_down, orientation.mode,
	orientation.blocking, orientation.hysteresis);
}
static ssize_t bma4xy_store_orientation_config(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned int data[4] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_orientation_config orientation;

	err = sscanf(buf, "%11x %11x %11x %11x",
	&data[0], &data[1], &data[2], &data[3]);
	if (err != 4) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	memset(&orientation, 0, sizeof(orientation));
	orientation.upside_down = (uint8_t)data[0];
	orientation.mode = (uint8_t)data[1];
	orientation.blocking = (uint8_t)data[2];
	orientation.hysteresis = (uint16_t)data[3];
	err = bma420_set_orientation_config(&orientation, &client_data->device);
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
#endif
#if defined(BMA420) || defined(BMA420L)
static ssize_t bma4xy_show_flat_config(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_flat_config flat;

	#if defined(BMA420)
	err = bma420_get_flat_config(&flat, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_get_flat_config(&flat, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_get_flat_config(&flat, &client_data->device);
	#endif
	if (err) {
		PERR("read failed %d", client_data->flat_enable);
		return err;
	}
	return snprintf(buf, PAGE_SIZE,
	"theta =0x%x blocking= 0x%x\n"
	"hysteresis = 0x%x hold_time = 0x%x\n",
	flat.theta, flat.blocking,
	flat.hysteresis, flat.hold_time);
}
static ssize_t bma4xy_store_flat_config(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned int data[4] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_flat_config flat;

	err = sscanf(buf, "%11x %11x %11x %11x",
	&data[0], &data[1], &data[2], &data[3]);
	if (err != 4) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	memset(&flat, 0, sizeof(flat));
	flat.theta = (uint8_t)data[0];
	flat.blocking = (uint8_t)data[1];
	flat.hysteresis = (uint8_t)data[2];
	flat.hold_time = (uint8_t)data[3];
	#if defined(BMA420)
	err = bma420_set_flat_config(&flat, &client_data->device);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_set_flat_config(&flat, &client_data->device);
	#endif
	#if defined(BMA421L)
	err = bma421l_set_flat_config(&flat, &client_data->device);
	#endif
	if (err) {
		PERR("write failed %d", client_data->flat_enable);
		return err;
	}
	return count;
}
#endif
#if defined(BMA420)
static ssize_t bma4xy_show_highg_config(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_high_g_config highg_config;

	err = bma420_get_high_g_config(&highg_config, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, PAGE_SIZE,
	"threshold =0x%x hysteresis= 0x%x duration = 0x%x\n",
	highg_config.threshold, highg_config.hysteresis,
	highg_config.duration);
}
static ssize_t bma4xy_store_highg_config(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned int data[3] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_high_g_config highg_config;

	err = sscanf(buf, "%11x %11x %11x", &data[0], &data[1], &data[2]);
	if (err != 3) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	memset(&highg_config, 0, sizeof(highg_config));
	highg_config.threshold = (uint16_t)data[0];
	highg_config.hysteresis = (uint16_t)data[1];
	highg_config.duration = (uint8_t)data[2];
	err = bma420_set_high_g_config(&highg_config, &client_data->device);
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
#endif
#if defined(BMA420)
static ssize_t bma4xy_show_lowg_config(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_low_g_config lowg_config;

	err = bma420_get_low_g_config(&lowg_config, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, PAGE_SIZE,
	"threshold = 0x%x hysteresis= 0x%x duration = 0x%x\n",
	lowg_config.threshold, lowg_config.hysteresis,
	lowg_config.duration);

}
static ssize_t bma4xy_store_lowg_config(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned int data[3] = {0};
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma420_low_g_config lowg_config;

	err = sscanf(buf, "%11x %11x %11x", &data[0], &data[1], &data[2]);
	if (err != 3) {
		PERR("Invalid argument");
		return -EINVAL;
	}
	memset(&lowg_config, 0, sizeof(lowg_config));
	lowg_config.threshold = (uint16_t)data[0];
	lowg_config.hysteresis = (uint16_t)data[1];
	lowg_config.duration = (uint8_t)data[2];
	err = bma420_set_low_g_config(&lowg_config, &client_data->device);
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
#endif
#if defined(BMA420)
static ssize_t bma4xy_show_tap_type(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	return snprintf(buf, 8, "%d\n", client_data->tap_type);
}
static ssize_t bma4xy_store_tap_type(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int32_t err = 0;
	unsigned long data;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 16, &data);
	if (err)
		return err;
	client_data->tap_type = (uint8_t)data;
	#ifdef BMA420
	err = bma420_set_tap_selection(
	client_data->tap_type, &client_data->device);
	#endif
	if (err) {
		PERR("write failed");
		return err;
	}
	return count;
}
#endif

#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
static ssize_t bma4xy_show_mag_sensor(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	uint16_t mag_sensor = 0;
	#if defined(CONFIG_BMA4XY_AUX_AK09916)
		mag_sensor = 9916;
	#else
		mag_sensor = 150;
	#endif
	return snprintf(buf, 32, "%d\n", mag_sensor);
}
static ssize_t bma4xy_show_mag_chip_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	return snprintf(buf, 48, "0x%x\n", client_data->mag_chip_id);
}
static ssize_t bma4xy_show_mag_op_mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err = 0;
	unsigned char mag_op_mode = 0;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_get_mag_enable(&mag_op_mode, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 96, "1 mean enable now is %d\n", mag_op_mode);
}
static ssize_t bma4xy_store_mag_op_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long op_mode;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &op_mode);
	if (err)
		return err;
	if (op_mode == 2)
		err = bma4_set_mag_enable(BMA4_DISABLE, &client_data->device);
	else if (op_mode == 0)
		err = bma4_set_mag_enable(BMA4_ENABLE, &client_data->device);
	if (err) {
		PERR("failed");
		return err;
	}
	client_data->pw.mag_pm = op_mode;
	return count;
}
static ssize_t bma4xy_show_fifo_mag_enable(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	uint8_t fifo_mag_enable;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = bma4_get_fifo_config(&fifo_mag_enable, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 32, "0x%x\n", fifo_mag_enable);
}
static ssize_t bma4xy_store_fifo_mag_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	int err;
	unsigned long data;
	unsigned char fifo_mag_enable;

	err = kstrtoul(buf, 10, &data);
	if (err)
		return err;
	fifo_mag_enable = (unsigned char)data;
	err = bma4_set_fifo_config(
		BMA4_FIFO_MAG, fifo_mag_enable, &client_data->device);
	if (err) {
		PERR("faliled");
		return -EIO;
	}
	client_data->fifo_mag_enable = fifo_mag_enable;
	return count;
}
static ssize_t bma4xy_show_mag_value(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct bma4_mag data;
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	/* raw data with compensation */
	#ifdef AKM09916
	err = bma4_bst_akm09916_compensate_xyz(&data, &client_data->device);
	#else
	err = bma4_bmm150_mag_compensate_xyz(&data, &client_data->device);
	#endif
	if (err < 0) {
		memset(&data, 0, sizeof(data));
		PERR("mag not ready!");
	}
	return snprintf(buf, 96, "%hd %hd %hd\n", data.x,
					data.y, data.z);
}
struct bma4_mag mag_compensate;
static ssize_t bma4xy_show_mag_compensate_xyz(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	memcpy(buf, &mag_compensate, sizeof(mag_compensate));
	return sizeof(mag_compensate);
}
static ssize_t bma4xy_store_mag_compensate_xyz(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bma4_mag_xyzr mag_raw;
	memset(&mag_compensate, 0, sizeof(mag_compensate));
	memset(&mag_raw, 0, sizeof(mag_raw));
	#if defined(AKM09916)
	mag_compensate.x = (buf[1] << 8 | buf[0]);
	mag_compensate.y = (buf[3] << 8 | buf[2]);
	mag_compensate.z = (buf[5] << 8 | buf[4]);
	#else
	mag_raw.x = (buf[1] << 8 | buf[0]);
	mag_raw.y = (buf[3] << 8 | buf[2]);
	mag_raw.z = (buf[5] << 8 | buf[4]);
	mag_raw.r = (buf[7] << 8 | buf[6]);
	mag_raw.x = mag_raw.x >> 3;
	mag_raw.y = mag_raw.y >> 3;
	mag_raw.z = mag_raw.z >> 1;
	mag_raw.r = mag_raw.r >> 2;
	bma4_bmm150_mag_compensate_xyz_raw(
	&mag_compensate, mag_raw);
	#endif
	return count;
}
static ssize_t bma4xy_show_mag_odr(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int err;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma4_aux_mag_config mag_config;
	err = bma4_get_aux_mag_config(&mag_config, &client_data->device);
	if (err) {
		PERR("read failed");
		return err;
	}
	return snprintf(buf, 16, "%d\n", mag_config.odr);
}
static ssize_t bma4xy_store_mag_odr(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long mag_odr;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);
	struct bma4_aux_mag_config mag_config;

	err = kstrtoul(buf, 10, &mag_odr);
	if (err)
		return err;
	err = bma4_get_aux_mag_config(&mag_config, &client_data->device);
	mag_config.odr = mag_odr;
	err += bma4_set_aux_mag_config(&mag_config, &client_data->device);
	if (err) {
		PERR("faliled");
		return -EIO;
	}
	client_data->mag_odr = mag_odr;
	PERR("mag_odr =%ld", mag_odr);
	return count;
}
#endif
#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
static int bma4xy_aux_init(struct bma4xy_client_data *client_data)
{
	int err = 0;
	#if defined(AKM09916)
	err = bma4_bst_akm_mag_interface_init(
		BMA4_AUX_AKM09916_I2C_ADDR,
		&client_data->mag_chip_id, &client_data->device);
	#else
	err = bma4_bmm150_mag_interface_init(
		&client_data->mag_chip_id, &client_data->device);
	#endif
	if (err) {
		PERR("read failed");
		return err;
	}
	PDEBUG("mag_chip_id = 0x%x\n", client_data->mag_chip_id);
	return err;
}
#endif
#if defined(BMA420)
int bma420_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA420_ANY_MOTION;
	if (client_data->orientation_enable == BMA4_ENABLE)
		feature = feature | BMA420_ORIENTATION;
	if (client_data->flat_enable == BMA4_ENABLE)
		feature = feature | BMA420_FLAT;
	if (client_data->tap_enable == BMA4_ENABLE)
		feature = feature | BMA420_TAP;
	if (client_data->highg_enable == BMA4_ENABLE)
		feature = feature | BMA420_HIGH_G;
	if (client_data->lowg_enable == BMA4_ENABLE)
		feature = feature | BMA420_LOW_G;
	err = bma420_feature_enable(feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;

}
#endif
#if defined(BMA421)
int bma421_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma421_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA421_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA421_ANY_MOTION;
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA421_STEP_CNTR;
	if (client_data->activity_enable == BMA4_ENABLE)
		feature = feature | BMA421_ACTIVITY;
	err = bma421_feature_enable(feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif
#if defined(BMA421OSC)
int bma421osc_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma421osc_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA421_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA421OSC_ANY_MOTION;
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA421OSC_STEP_CNTR;
	err = bma421osc_feature_enable(
		feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

#if defined(BMA424SC)
int bma424sc_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma424sc_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA424sc_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA424SC_ANY_MOTION;
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA424SC_STEP_CNTR;
	if (client_data->activity_enable == BMA4_ENABLE)
		feature = feature | BMA424SC_ACTIVITY;
	err = bma424sc_feature_enable(
		feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

#if defined(BMA424O)
int bma424o_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma424o_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA424sc_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA424O_ANY_MOTION;
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA424O_STEP_CNTR;
	if (client_data->activity_enable == BMA4_ENABLE)
		feature = feature | BMA424O_ACTIVITY;
	err = bma424o_feature_enable(
		feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

#if defined(BMA421LOSC)
int bma421losc_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma421losc_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA421LOSC_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA421LOSC_ANY_MOTION;
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA421LOSC_STEP_CNTR;
	if (client_data->flat_enable == BMA4_ENABLE)
		feature = feature | BMA421LOSC_FLAT;
	err = bma421losc_feature_enable(
		feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif
#if defined(BMA421L)
int bma421l_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma421l_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA421LOSC_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA421L_ANY_MOTION;
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA421L_STEP_CNTR;
	if (client_data->flat_enable == BMA4_ENABLE)
		feature = feature | BMA421L_FLAT;
	if (client_data->activity_enable == BMA4_ENABLE)
		feature = feature | BMA421L_ACTIVITY;
	err = bma421l_feature_enable(
		feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

#if defined(BMA422)
int bma422_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->sigmotion_enable == BMA4_ENABLE)
		feature = feature | BMA422_SIG_MOTION;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma422_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA422_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA422_STEP_CNTR;
	if (client_data->tilt_enable == BMA4_ENABLE)
		feature = feature | BMA422_TILT;
	if (client_data->pickup_enable == BMA4_ENABLE)
		feature = feature | BMA422_PICKUP;
	if (client_data->glance_enable == BMA4_ENABLE)
		feature = feature | BMA422_GLANCE;
	if (client_data->wakeup_enable == BMA4_ENABLE)
		feature = feature | BMA422_WAKEUP;
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA422_ANY_MOTION;
	err = bma422_feature_enable(feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif
#if defined(BMA424)
int bma424_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->sigmotion_enable == BMA4_ENABLE)
		feature = feature | BMA424_SIG_MOTION;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma424_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA422_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA424_STEP_CNTR;
	if (client_data->tilt_enable == BMA4_ENABLE)
		feature = feature | BMA424_TILT;
	if (client_data->pickup_enable == BMA4_ENABLE)
		feature = feature | BMA424_PICKUP;
	if (client_data->glance_enable == BMA4_ENABLE)
		feature = feature | BMA424_GLANCE;
	if (client_data->wakeup_enable == BMA4_ENABLE)
		feature = feature | BMA424_WAKEUP;
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA424_ANY_MOTION;
	err = bma424_feature_enable(feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

#if defined(BMA455)
int bma455_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->sigmotion_enable == BMA4_ENABLE)
		feature = feature | BMA455_SIG_MOTION;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma455_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA455_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA455_STEP_CNTR;
	if (client_data->tilt_enable == BMA4_ENABLE)
		feature = feature | BMA455_TILT;
	if (client_data->pickup_enable == BMA4_ENABLE)
		feature = feature | BMA455_PICKUP;
	if (client_data->glance_enable == BMA4_ENABLE)
		feature = feature | BMA455_GLANCE;
	if (client_data->wakeup_enable == BMA4_ENABLE)
		feature = feature | BMA455_WAKEUP;
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA455_ANY_MOTION;
	err = bma455_feature_enable(feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

#if defined(BMA456)
int bma456_config_feature(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t feature = 0;
	if (client_data->stepdet_enable == BMA4_ENABLE) {
		if (bma456_step_detector_enable(
			BMA4_ENABLE, &client_data->device) < 0)
			PERR("set BMA455_STEP_DECTOR error");
	}
	bma4xy_i2c_delay(2);
	if (client_data->stepcounter_enable == BMA4_ENABLE)
		feature = feature | BMA456_STEP_CNTR;
	if (client_data->tilt_enable == BMA4_ENABLE)
		feature = feature | BMA456_WRIST_TILT;
	if (client_data->wakeup_enable == BMA4_ENABLE)
		feature = feature | BMA456_WAKEUP;
	if (client_data->anymotion_enable == BMA4_ENABLE)
		feature = feature | BMA456_ANY_MOTION;
	err = bma456_feature_enable(feature, BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set feature err");
	return err;
}
#endif

int bma4xy_reinit_after_error_interrupt(
	struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint8_t data = 0;
	uint8_t crc_check = 0;
	#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
	struct bma4_aux_mag_config mag_config;
	#endif
	client_data->err_int_trigger_num += 1;
	client_data->step_counter_val = client_data->step_counter_temp;
	/*reset the bma4xy*/
	err = bma4_set_command_register(0xB6, &client_data->device);
	if (!err)
		PDEBUG("reset chip");
	/*reinit the auc interface*/
	#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
	err = bma4xy_aux_init(client_data);
	if (err)
		PERR("aux init failed");
	#endif
	/*reinit the fifo config*/
	err = bma4xy_init_fifo_config(client_data);
	if (err)
		PERR("fifo init failed");
	/*reload the config_stream*/
	err = bma4_write_config_file(&client_data->device);
	if (err)
		PERR("download config stream failer");
	bma4xy_i2c_delay(200);
	err = bma4_read_regs(BMA4_INTERNAL_STAT,
	&crc_check, 1, &client_data->device);
	if (err)
		PERR("reading CRC failer");
	if (crc_check != BMA4_ASIC_INITIALIZED)
		PERR("crc check error %x", crc_check);
	/*reconfig interrupt and remap*/
	err = bma4xy_init_after_config_stream_load(client_data);
	if (err)
		PERR("reconfig interrupt and remap error");
	/*reinit the feature*/
	#if defined(BMA420)
	err = bma420_config_feature(client_data);
	#endif
	#if defined(BMA421)
	err = bma421_config_feature(client_data);
	#endif
	#if defined(BMA421OSC)
	err = bma421osc_config_feature(client_data);
	#endif
	#if defined(BMA424SC)
	err = bma424sc_config_feature(client_data);
	#endif
	#if defined(BMA424O)
	err = bma424o_config_feature(client_data);
	#endif
	#if defined(BMA421LOSC)
	err = bma421losc_config_feature(client_data);
	#endif
	#if defined(BMA421L)
	err = bma421l_config_feature(client_data);
	#endif
	#if defined(BMA422)
	err = bma422_config_feature(client_data);
	#endif
	#if defined(BMA424)
	err = bma424_config_feature(client_data);
	#endif
	#if defined(BMA455)
	err = bma455_config_feature(client_data);
	#endif
	#if defined(BMA456)
	err = bma456_config_feature(client_data);
	#endif
	if (err)
		PERR("reinit the virtual sensor error");
	/*reinit acc*/
	if (client_data->acc_odr != 0) {
		data = client_data->acc_odr;
		if (data == 4)
			data = 0x74;
		else
			data |= 0xA0;
		err = client_data->device.bus_write(
			client_data->device.dev_addr,
			0x40, &data, 1);
		if (err)
			PERR("set acc_odr faliled");
		bma4xy_i2c_delay(2);
	}
	if (client_data->pw.acc_pm == 0)
		err = bma4_set_accel_enable(BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set acc_op_mode failed");
	bma4xy_i2c_delay(2);
	err = bma4_set_fifo_config(BMA4_FIFO_ACCEL,
		client_data->fifo_acc_enable, &client_data->device);
	if (err)
		PERR("set acc_fifo_enable faliled");
	bma4xy_i2c_delay(5);
	#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
	/*reinit mag*/
	if (client_data->pw.mag_pm == 0)
		err = bma4_set_mag_enable(BMA4_ENABLE, &client_data->device);
	if (err)
		PERR("set mag op_mode failed");
	bma4xy_i2c_delay(2);
	mag_config.odr = client_data->mag_odr;
	if (client_data->mag_odr != 0) {
		err = bma4_set_aux_mag_config(&mag_config,
			&client_data->device);
		if (err)
			PERR("set the mag odr faliled");
		bma4xy_i2c_delay(2);
	}
	err = bma4_set_fifo_config(BMA4_FIFO_MAG,
		client_data->fifo_mag_enable, &client_data->device);
	if (err)
		PERR("set mag_fifo_enable faliled");
	bma4xy_i2c_delay(2);
	#endif
	return 0;
}

static ssize_t bma4xy_show_err_int(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 128, "please check sensor normal working");
}
static ssize_t bma4xy_store_err_int(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int err = 0;
	unsigned long op_mode;
	struct input_dev *input = to_input_dev(dev);
	struct bma4xy_client_data *client_data = input_get_drvdata(input);

	err = kstrtoul(buf, 10, &op_mode);
	if (err)
		return err;
	err = bma4xy_reinit_after_error_interrupt(client_data);
	if (err)
		return err;
	return count;
}
static DEVICE_ATTR(chip_id, S_IRUGO,
	bma4xy_show_chip_id, NULL);
static DEVICE_ATTR(acc_op_mode, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_acc_op_mode, bma4xy_store_acc_op_mode);
static DEVICE_ATTR(acc_value, S_IRUGO,
	bma4xy_show_acc_value, NULL);
static DEVICE_ATTR(acc_range, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_acc_range, bma4xy_store_acc_range);
static DEVICE_ATTR(acc_odr, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_acc_odr, bma4xy_store_acc_odr);
static DEVICE_ATTR(acc_fifo_enable, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_fifo_acc_enable, bma4xy_store_fifo_acc_enable);
#if defined(BMA4XY_FOR_SPREADTRUM)
static DEVICE_ATTR(acc_poll_delay, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_acc_poll_delay, bma4xy_store_acc_poll_delay);
#endif
static DEVICE_ATTR(fifo_flush, S_IWUSR|S_IWGRP,
	NULL, bma4xy_store_fifo_flush);
static DEVICE_ATTR(selftest, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_selftest, bma4xy_store_selftest);
static DEVICE_ATTR(avail_sensor, S_IRUGO,
	bma4xy_show_avail_sensor, NULL);
static DEVICE_ATTR(fifo_length, S_IRUGO,
	bma4xy_show_fifo_length, NULL);
static DEVICE_ATTR(fifo_watermark, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_fifo_watermark, bma4xy_store_fifo_watermark);
static DEVICE_ATTR(load_fw, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_load_config_stream, bma4xy_store_load_config_stream);
static DEVICE_ATTR(reg_sel, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_reg_sel, bma4xy_store_reg_sel);
static DEVICE_ATTR(reg_val, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_reg_val, bma4xy_store_reg_val);
static DEVICE_ATTR(driver_version, S_IRUGO,
	bma4xy_show_driver_version, NULL);
static DEVICE_ATTR(config_file_version, S_IRUGO,
	bma4xy_show_config_file_version, NULL);
static DEVICE_ATTR(fifo_data_frame, S_IRUGO,
	bma4xy_show_fifo_data_out_frame, NULL);
static DEVICE_ATTR(foc, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_foc, bma4xy_store_foc);
static DEVICE_ATTR(config_function, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_config_function, bma4xy_store_config_function);
static DEVICE_ATTR(axis_remapping, S_IWUSR|S_IWGRP,
	NULL, bma4xy_store_axis_remapping);
#if defined(BMA422) || defined(BMA455) || defined(BMA424)
static DEVICE_ATTR(sig_config, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_sig_motion_config, bma4xy_store_sig_motion_config);
#endif
#if !defined(BMA420)
static DEVICE_ATTR(step_counter_val, S_IRUGO,
	bma4xy_show_step_counter_val, NULL);
static DEVICE_ATTR(step_counter_watermark, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_step_counter_watermark,
	bma4xy_store_step_counter_watermark);
static DEVICE_ATTR(step_counter_parameter, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_step_counter_parameter,
	bma4xy_store_step_counter_parameter);
static DEVICE_ATTR(step_counter_reset, S_IWUSR|S_IWGRP,
	NULL, bma4xy_store_step_counter_reset);
#endif
#if defined(BMA422) || defined(BMA455) || defined(BMA424) || defined(BMA424O)
static DEVICE_ATTR(tilt_threshold, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_tilt_threshold, bma4xy_store_tilt_threshold);
#endif
#if defined(BMA420)
static DEVICE_ATTR(tap_type, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_tap_type, bma4xy_store_tap_type);
#endif
static DEVICE_ATTR(anymotion_config, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_anymotion_config, bma4xy_store_anymotion_config);
static DEVICE_ATTR(anymotion_config_enable_axis, S_IWUSR|S_IWGRP,
	NULL, bma4xy_store_anymotion_enable_axis);
#if defined(BMA420)
static DEVICE_ATTR(orientation_config, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_orientation_config, bma4xy_store_orientation_config);
#endif
#if defined(BMA420)
static DEVICE_ATTR(flat_config, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_flat_config, bma4xy_store_flat_config);
#endif
#if defined(BMA420)
static DEVICE_ATTR(highg_config, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_highg_config, bma4xy_store_highg_config);
#endif
#if defined(BMA420)
static DEVICE_ATTR(lowg_config, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_lowg_config, bma4xy_store_lowg_config);
#endif
#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
static DEVICE_ATTR(mag_chip_id, S_IRUGO,
	bma4xy_show_mag_chip_id, NULL);
static DEVICE_ATTR(mag_sensor, S_IRUGO,
	bma4xy_show_mag_sensor, NULL);
static DEVICE_ATTR(mag_op_mode, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_mag_op_mode, bma4xy_store_mag_op_mode);
static DEVICE_ATTR(mag_value, S_IRUGO,
	bma4xy_show_mag_value, NULL);
static DEVICE_ATTR(mag_odr, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_mag_odr, bma4xy_store_mag_odr);
static DEVICE_ATTR(mag_fifo_enable, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_fifo_mag_enable, bma4xy_store_fifo_mag_enable);
static DEVICE_ATTR(mag_compensate, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_mag_compensate_xyz, bma4xy_store_mag_compensate_xyz);
#endif
static DEVICE_ATTR(err_int, S_IRUGO|S_IWUSR|S_IWGRP,
	bma4xy_show_err_int, bma4xy_store_err_int);
static struct attribute *bma4xy_attributes[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_acc_op_mode.attr,
	&dev_attr_acc_value.attr,
	&dev_attr_acc_range.attr,
	&dev_attr_acc_odr.attr,
	&dev_attr_acc_fifo_enable.attr,
#if defined(BMA4XY_FOR_SPREADTRUM)
	&dev_attr_acc_poll_delay.attr,
#endif
	&dev_attr_selftest.attr,
	&dev_attr_avail_sensor.attr,
	&dev_attr_foc.attr,
	&dev_attr_fifo_length.attr,
	&dev_attr_fifo_watermark.attr,
	&dev_attr_fifo_flush.attr,
	&dev_attr_driver_version.attr,
	&dev_attr_load_fw.attr,
	&dev_attr_fifo_data_frame.attr,
	&dev_attr_config_file_version.attr,
	&dev_attr_reg_sel.attr,
	&dev_attr_reg_val.attr,
	&dev_attr_config_function.attr,
	&dev_attr_axis_remapping.attr,
#if defined(BMA422) || defined(BMA455) || defined(BMA424)
	&dev_attr_sig_config.attr,
#endif
#if defined(BMA422) || defined(BMA421) || defined(BMA424) || \
defined(BMA421LOSC) || defined(BMA455) || defined(BMA424SC) || \
defined(BMA421L) || defined(BMA421OSC) || defined(BMA424O) || defined(BMA456)
	&dev_attr_step_counter_val.attr,
	&dev_attr_step_counter_watermark.attr,
	&dev_attr_step_counter_parameter.attr,
	&dev_attr_step_counter_reset.attr,
#endif
#if defined(BMA422) || defined(BMA455) || defined(BMA424) || defined(BMA424O)
	&dev_attr_tilt_threshold.attr,
#endif
	&dev_attr_anymotion_config.attr,
	&dev_attr_anymotion_config_enable_axis.attr,
#if defined(BMA420)
	&dev_attr_tap_type.attr,
#endif
#if defined(BMA420)
	&dev_attr_orientation_config.attr,
#endif
#if defined(BMA420)
	&dev_attr_flat_config.attr,
#endif
#if defined(BMA420)
	&dev_attr_highg_config.attr,
#endif
#if defined(BMA420)
	&dev_attr_lowg_config.attr,
#endif
#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
	&dev_attr_mag_chip_id.attr,
	&dev_attr_mag_sensor.attr,
	&dev_attr_mag_op_mode.attr,
	&dev_attr_mag_value.attr,
	&dev_attr_mag_odr.attr,
	&dev_attr_mag_fifo_enable.attr,
	&dev_attr_mag_compensate.attr,
#endif
	&dev_attr_err_int.attr,
	NULL
};

static struct attribute_group bma4xy_attribute_group = {
	.attrs = bma4xy_attributes
};

#if defined(BMA4XY_ENABLE_INT1) || defined(BMA4XY_ENABLE_INT2)
static void bma4xy_fifowm_int_handle(struct bma4xy_client_data *client_data)
{
	int err = 0;
	uint16_t fifo_bytecount = 0;

	err = bma4_get_fifo_length(&fifo_bytecount, &client_data->device);
	if (fifo_bytecount == 0 || err) {
		PERR("read fifo length zero");
		return;
	}
	if (fifo_bytecount > FIFO_DATA_BUFSIZE_BMA4XY) {
		PERR("read fifo length bigger than 1024 length =%d",
		fifo_bytecount);
		return;
	}
	memset(fifo_data, 0, 1024);
	err = client_data->device.bus_read(client_data->device.dev_addr,
			BMA4_FIFO_DATA_ADDR, fifo_data,
			fifo_bytecount + 4);
	if (err)
		PERR("read fifo err");
	err = bma4xy_fifo_analysis_handle(client_data, fifo_data,
	client_data->fifo_bytecount + 4);
	if (err)
		PERR("analyze handle failed:%d", err);
}

static void bma4xy_uc_function_handle(
	struct bma4xy_client_data *client_data, uint8_t status)
{
	int err = 0;
	#if defined(BMA420)
	unsigned char uc_gpio[2] = {0};
	#endif
	if (status & ERROR_INT_OUT) {
		err = bma4xy_reinit_after_error_interrupt(client_data);
		if (err)
			PERR("reinit failed");
	}
#if defined(BMA422) || defined(BMA421) || defined(BMA424) || \
defined(BMA421L) || defined(BMA421LOSC) || defined(BMA455) || \
defined(BMA424SC) || defined(BMA421OSC) || defined(BMA424O) || defined(BMA456)
	input_event(client_data->uc_input, EV_MSC, REL_UC_STATUS,
		(uint32_t)(status));
	PDEBUG("%x", (uint32_t)(status));
	input_sync(client_data->uc_input);
#endif
#if defined(BMA420)
	if (client_data->orientation_enable)
		err = bma4_read_regs(BMA4_STEP_CNT_OUT_0_ADDR,
		&uc_gpio[0], 1, &client_data->device);
	if (client_data->highg_enable)
		err += bma4_read_regs(BMA4_HIGH_G_OUT_ADDR,
		&uc_gpio[1], 1, &client_data->device);
	if (err) {
		PERR("read uc_gpio failed");
		return;
	}
	PDEBUG("%d %d", uc_gpio[0], uc_gpio[1]);
	input_event(client_data->uc_input, EV_MSC, REL_UC_STATUS,
		(uint32_t)((uc_gpio[1]<<16) | (uc_gpio[0]<<8) | status));
	input_sync(client_data->uc_input);
#endif
}

static void bma4xy_irq_work_func(struct work_struct *work)
{
	struct bma4xy_client_data *client_data = container_of(work,
		struct bma4xy_client_data, irq_work);
	unsigned char int_status[2] = {0, 0};
#if defined(BMA421) || defined(BMA424SC) || defined(BMA421L) || defined(BMA424O) || defined(BMA456)
	uint8_t data = 0;
#endif
	int err = 0;
	int in_suspend_copy;
	in_suspend_copy = atomic_read(&client_data->in_suspend);

	/*read the interrut status two register*/
	err = client_data->device.bus_read(client_data->device.dev_addr,
				BMA4_INT_STAT_0_ADDR, int_status, 2);
	if (err)
		return;
	PDEBUG("int_status0 = 0x%x int_status1 =0x%x",
		int_status[0], int_status[1]);
	if (in_suspend_copy &&
		((int_status[0] & STEP_DET_OUT) == 0x02)) {
		return;
	}
	#if defined(BMA421)
	if ((int_status[0] & 0x04) == 0x04) {
		bma421_activity_output(&data, &client_data->device);
		PDEBUG("activity status 0x%x", data);
	}
	#endif
	#if defined(BMA424SC)
	if ((int_status[0] & 0x04) == 0x04) {
		bma424sc_activity_output(&data, &client_data->device);
		PDEBUG("activity status 0x%x", data);
	}
	#endif
	#if defined(BMA424O)
	if ((int_status[0] & 0x04) == 0x04) {
		bma424o_activity_output(&data, &client_data->device);
		PDEBUG("activity status 0x%x", data);
	}
	#endif
	#if defined(BMA421L)
	if ((int_status[0] & 0x04) == 0x04) {
		bma421l_activity_output(&data, &client_data->device);
		PDEBUG("activity status 0x%x", data);
	}
	#endif
	#if defined(BMA456)
	if ((int_status[0] & 0x04) == 0x04) {
		bma456_activity_output(&data, &client_data->device);
		PDEBUG("activity status 0x%x", data);
	}
	#endif
	if (int_status[1] & FIFOFULL_OUT)
		bma4xy_fifowm_int_handle(client_data);
	if (int_status[1] & FIFOWATERMARK_OUT)
		bma4xy_fifowm_int_handle(client_data);
	if (int_status[0])
		bma4xy_uc_function_handle(client_data, (uint8_t)int_status[0]);
}

static void bma4xy_delay_sigmo_work_func(struct work_struct *work)
{
	struct bma4xy_client_data *client_data =
	container_of(work, struct bma4xy_client_data,
	delay_work_sig.work);
	unsigned char int_status[2] = {0, 0};
	int err = 0;
	/*read the interrut status two register*/
	err = client_data->device.bus_read(client_data->device.dev_addr,
				BMA4_INT_STAT_0_ADDR, int_status, 2);
	if (err)
		return;
	PDEBUG("int_status0 = %x int_status1 =%x",
		int_status[0], int_status[1]);
	input_event(client_data->uc_input, EV_MSC, REL_UC_STATUS,
		(uint32_t)(SIG_MOTION_OUT));
	PDEBUG("%x", (uint32_t)(SIG_MOTION_OUT));
	input_sync(client_data->uc_input);
}

static irqreturn_t bma4xy_irq_handle(int irq, void *handle)
{
	struct bma4xy_client_data *client_data = handle;
	int in_suspend_copy;
	in_suspend_copy = atomic_read(&client_data->in_suspend);
	/*this only deal with SIG_motion CTS test*/
	if ((in_suspend_copy == 1) &&
		((client_data->sigmotion_enable == 1) &&
		(client_data->tilt_enable != 1) &&
		(client_data->pickup_enable != 1) &&
		(client_data->glance_enable != 1) &&
		(client_data->wakeup_enable != 1))) {
		wake_lock_timeout(&client_data->wakelock, HZ);
		schedule_delayed_work(&client_data->delay_work_sig,
			msecs_to_jiffies(50));
	} else if ((in_suspend_copy == 1) &&
		((client_data->sigmotion_enable == 1) ||
		(client_data->tilt_enable == 1) ||
		(client_data->pickup_enable == 1) ||
		(client_data->glance_enable == 1) ||
		(client_data->wakeup_enable == 1))) {
		wake_lock_timeout(&client_data->wakelock, HZ);
		schedule_work(&client_data->irq_work);
	} else
		schedule_work(&client_data->irq_work);
	return IRQ_HANDLED;
}

static irqreturn_t bma4xy_irq_handle1(int irq, void *handle)
{
	return bma4xy_irq_handle(irq, handle);
}

static irqreturn_t bma4xy_irq_handle2(int irq, void *handle)
{
	return bma4xy_irq_handle(irq, handle);
}

static irqreturn_t (*bma4xy_irq_handle_table[2])(int irq, void *handle) = {
	&bma4xy_irq_handle1,
	&bma4xy_irq_handle2,
};

static int bma4xy_request_irq(struct bma4xy_client_data *client_data, int id)
{
	int err = 0;
	char gpio_compat[32];

	snprintf(gpio_compat,
		 sizeof(gpio_compat),
		 "bma4xy,gpio_irq%d", id + 1);
	snprintf(client_data->gpio_name[id],
		 sizeof(client_data->gpio_name[id]),
		 "bma4xy_interrupt%d", id + 1);
	snprintf(client_data->IRQ_name[id],
		 sizeof(client_data->IRQ_name[id]),
		 SENSOR_NAME "%d", id + 1);

	client_data->gpio_pin[id] = of_get_named_gpio_flags(
		client_data->dev->of_node,
		gpio_compat, 0, NULL);
	PDEBUG("BMA4xy qpio number int%d:%d", id, client_data->gpio_pin[id]);
	err = gpio_request_one(client_data->gpio_pin[id],
				GPIOF_IN, client_data->gpio_name[id]);
	if (err < 0)
		return err;
	err = gpio_direction_input(client_data->gpio_pin[id]);
	if (err < 0)
		return err;
	client_data->IRQ[id] = gpio_to_irq(client_data->gpio_pin[id]);
	err = request_irq(client_data->IRQ[id], bma4xy_irq_handle_table[id],
			IRQF_TRIGGER_RISING,
			client_data->IRQ_name[id], client_data);
	if (err < 0)
		return err;

	if (client_data->irq_work.func == NULL)
		INIT_WORK(&client_data->irq_work, bma4xy_irq_work_func);
	if (client_data->delay_work_sig.work.func == NULL)
		INIT_DELAYED_WORK(&client_data->delay_work_sig,
			bma4xy_delay_sigmo_work_func);
	return err;
}
#endif
static int bma4xy_acc_input_init(struct bma4xy_client_data *client_data)
{
	struct input_dev *dev;
	int err = 0;

	dev = input_allocate_device();
	if (NULL == dev)
		return -ENOMEM;

	dev->name = SENSOR_NAME;
	dev->id.bustype = BUS_I2C;
	input_set_capability(dev, EV_ABS, ABS_MISC);
	input_set_capability(dev, EV_MSC, REL_UC_STATUS);
	#if defined(BMA425)
	input_set_capability(dev, EV_MSC, MSC_SCAN);
	#endif
	input_set_abs_params(dev, ABS_X,
	BMA4XY_ACCEL_MIN_VALUE, BMA4XY_ACCEL_MAX_VALUE,
	0, 0);
	input_set_abs_params(dev, ABS_Y,
	BMA4XY_ACCEL_MIN_VALUE, BMA4XY_ACCEL_MAX_VALUE,
	0, 0);
	input_set_abs_params(dev, ABS_Z,
	BMA4XY_ACCEL_MIN_VALUE, BMA4XY_ACCEL_MAX_VALUE,
	0, 0);
	input_set_drvdata(dev, client_data);
	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		return err;
	}
	client_data->acc_input = dev;
	return 0;
}

static void bma4xy_acc_input_destroy(struct bma4xy_client_data *client_data)
{
	struct input_dev *dev = client_data->acc_input;
	input_unregister_device(dev);
	input_free_device(dev);
}

static int bma4xy_uc_function_input_init(struct bma4xy_client_data *client_data)
{
	struct input_dev *dev;
	int err = 0;

	dev = input_allocate_device();
	if (NULL == dev)
		return -ENOMEM;
	dev->name = SENSOR_NAME_UC;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_MSC, REL_UC_STATUS);
	input_set_drvdata(dev, client_data);
	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		return err;
	}
	client_data->uc_input = dev;
	return 0;
}

static void bma4xy_uc_function_input_destroy(
	struct bma4xy_client_data *client_data)
{
	struct input_dev *dev = client_data->acc_input;
	input_unregister_device(dev);
	input_free_device(dev);
}

int bma4xy_probe(struct bma4xy_client_data *client_data, struct device *dev)
{
	int err = 0;
	PINFO("function entrance");
	/* check chip id */
	err = bma4xy_check_chip_id(client_data);
	if (!err) {
		PINFO("Bosch Sensortec Device %s detected", SENSOR_NAME);
	} else {
		PERR("Bosch Sensortec Device not found, chip id mismatch");
		err = -1;
		goto exit_err_clean;
	}
	dev_set_drvdata(dev, client_data);
	client_data->dev = dev;
	client_data->device.delay = bma4xy_i2c_delay;
	/*acc input device init */
	err = bma4xy_acc_input_init(client_data);
	if (err < 0)
		goto exit_err_clean;
	/* sysfs node creation */
	err = sysfs_create_group(&client_data->acc_input->dev.kobj,
			&bma4xy_attribute_group);
	if (err < 0)
		goto exit_err_clean;
	err = bma4xy_uc_function_input_init(client_data);
	if (err < 0)
		goto exit_err_clean;
	#if defined(BMA420)
	err = bma420_init(&client_data->device);
	#elif defined(BMA421)
	err = bma421_init(&client_data->device);
	#elif defined(BMA421OSC)
	err = bma421osc_init(&client_data->device);
	#elif defined(BMA424SC)
	err = bma424sc_init(&client_data->device);
	#elif defined(BMA424O)
	err = bma424o_init(&client_data->device);
	#elif defined(BMA421LOSC)
	err = bma421losc_init(&client_data->device);
	#elif defined(BMA421L)
	err = bma421l_init(&client_data->device);
	#elif defined(BMA422)
	err = bma422_init(&client_data->device);
	#elif defined(BMA424)
	err = bma424_init(&client_data->device);
	#elif defined(BMA455)
	err = bma455_init(&client_data->device);
	#elif defined(BMA456)
	err = bma456_init(&client_data->device);
	#endif
	if (err < 0)
		PERR("init failed");
	err = bma4_set_command_register(0xB6, &client_data->device);
	if (!err)
		PDEBUG("reset chip");
	bma4xy_i2c_delay(10);
	client_data->tap_type = 0;
	/*request irq and config*/
	#if defined(BMA4XY_ENABLE_INT1)
	err = bma4xy_request_irq(client_data, BMA4XY_ENABLE_INT1);
	if (err < 0)
		PERR("Request irq1 failed");
	#endif
	#if defined(BMA4XY_ENABLE_INT2)
	err = bma4xy_request_irq(client_data, BMA4XY_ENABLE_INT2);
	if (err < 0)
		PERR("Request irq2 failed");
	#endif
	wake_lock_init(&client_data->wakelock, WAKE_LOCK_SUSPEND, "bma4xy");
	#ifdef BMA4XY_AUX_INTERFACE_SUPPORT
	err = bma4xy_aux_init(client_data);
	if (err)
		PERR("aux init failed");
	#endif
	err = bma4xy_init_fifo_config(client_data);
	if (err)
		PERR("fifo init failed");
	#ifdef BMA4XY_LOAD_CONFIG_FILE_IN_INIT
		err = bma4xy_update_config_stream(client_data, 3);
	if (err)
		PERR("config_stream load error");
	err = bma4xy_init_after_config_stream_load(client_data);
	if (err)
		PERR("bma4xy_init_after_config_stream_load error");
	#endif
	bma4xy_init_axis_mapping(client_data);
	#ifdef BMA4XY_FOR_SPREADTRUM
	client_data->accel_poll_ms = BMA4XY_ACCEL_DEFAULT_POLL_INTERVAL_MS;
	client_data->data_wq = create_freezable_workqueue("bma4xy_data_work");
	if (!client_data->data_wq) {
		PERR("Cannot create workqueue!");
		goto exit_err_clean;
	}
	INIT_DELAYED_WORK(&client_data->accel_poll_work,
		bma4xy_accel_work_fn);
	#endif
	PINFO("sensor %s probed successfully", SENSOR_NAME);
	return 0;
exit_err_clean:
	if (err) {
		if (client_data != NULL)
			kfree(client_data);
		return err;
	}
	return err;
}

int bma4xy_suspend(struct device *dev)
{
	int err = 0;
	struct bma4xy_client_data *client_data = dev_get_drvdata(dev);

	PINFO("suspend function entrance");
	#if defined(BMA4XY_ENABLE_INT1)
	enable_irq_wake(client_data->IRQ[BMA4XY_ENABLE_INT1]);
	#endif
	#if defined(BMA4XY_ENABLE_INT2)
	enable_irq_wake(client_data->IRQ[BMA4XY_ENABLE_INT2]);
	#endif
	atomic_set(&client_data->in_suspend, 1);
	return err;
}
EXPORT_SYMBOL(bma4xy_suspend);
int bma4xy_resume(struct device *dev)
{
	int err = 0;
	struct bma4xy_client_data *client_data = dev_get_drvdata(dev);

	PINFO("resume function entrance");
	#if defined(BMA4XY_ENABLE_INT1)
	disable_irq_wake(client_data->IRQ[BMA4XY_ENABLE_INT1]);
	#endif
	#if defined(BMA4XY_ENABLE_INT2)
	disable_irq_wake(client_data->IRQ[BMA4XY_ENABLE_INT2]);
	#endif
	atomic_set(&client_data->in_suspend, 0);
	return err;
}
EXPORT_SYMBOL(bma4xy_resume);
int bma4xy_remove(struct device *dev)
{
	int err = 0;
	struct bma4xy_client_data *client_data = dev_get_drvdata(dev);

	if (NULL != client_data) {
		bma4xy_i2c_delay(BMA4XY_I2C_WRITE_DELAY_TIME);
		sysfs_remove_group(&client_data->acc_input->dev.kobj,
				&bma4xy_attribute_group);
		bma4xy_acc_input_destroy(client_data);
		bma4xy_uc_function_input_destroy(client_data);
		wake_unlock(&client_data->wakelock);
		wake_lock_destroy(&client_data->wakelock);
		kfree(client_data);
	}
	return err;
}
EXPORT_SYMBOL(bma4xy_remove);
