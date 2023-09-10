/*
 * drivers/input/touchscreen/himax_hx8526_icx.c
 *
 * Copyright 2015,2016,2017,2018 Sony Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/input/ts_icx.h>
#include <linux/input/himax_hx8526_icx.h>

#include <linux/of.h>
#include <linux/gpio.h>

struct ts_icx_platform_data himax_hx8526_icx_ts_pdata = {

	.xrst_gpio = 64, //GPIO3_IO0
	.xint_gpio = 9, //GPIO1_IO9
	.irqflags  = IRQ_TYPE_EDGE_FALLING,

	.min_x = 0,
	.min_y = 0,
	.max_x = 480 * 2 - 1,
	.max_y = 800 * 2 - 1,
};

//#define HIMAX_HX8526_ICX_DEBUG
#define ERROR(format, args...) printk(KERN_ERR "ERR[%s()]" format, __func__, ##args)

#ifdef HIMAX_HX8526_ICX_DEBUG
#define DPRINTK(format, args...) printk(KERN_INFO "[%s()]" format, __func__, ##args)
#else
#define DPRINTK(format, args...)
#endif

#define HIMAX_HX8526_ICX_TP_ROT_180
/* #define HIMAX_HX8526_ICX_TP_FIXED_MULTI_NUM	(5) */

#define MAX_DATA_SIZE (128)

#define MULTI_NUM_MAX	10
#define MULTI_NUM_LIMIT	 5	/* the number of valid point */

#define COORDINATE_DATA_SIZE(n)	(n * 4)
#define FINGER_DATA_SIZE(n)		(((n + 3) / 4) * 4)
#define ID_INFO_DATA_SIZE		(4)

#define HIMAX_HX8526_ICX_TP_DMA_ALLOC_RETRY	5

/* real finger_size read from TP device is 120 = 20mm   */
/*                              TP size is 480 = 40mm   */
/* maximum xy point sending to app is 960 (= 480 * 2)   */
/* so finger_size sending to app should be done 4 times */
#define HIMAX_HX8526_TS_FINGER_SIZE_COEF	4
#define HIMAX_HX8526_TS_FINGER_SIZE			(120 * HIMAX_HX8526_TS_FINGER_SIZE_COEF)
#define HIMAX_HX8526_TS_Z_MAX		1

#define HIMAX_HX8526_TS_KEEPALIVE_PERIOD	500
#define HIMAX_HX8526_TS_ESD_SUM				0x08

#define HIMAX_HX8526_TS_IGNORE_PERIOD		30
#define HIMAX_HX8526_TS_NOTIFY_RELEASE		30

#define HIMAX_HX8526_TS_ERR_I2C				(-1)
#define HIMAX_HX8526_TS_ERR_SUM				(-2)
#define HIMAX_HX8526_TS_INVALID_DATA		(-3)

#define HIMAX_HX8526_TS_IDLE_MODE_ENABLE		(0x5F)
#define HIMAX_HX8526_TS_IDLE_MODE_DISABLE		(0x57)

static struct workqueue_struct *himax_hx8526_wq;

struct himax_hx8526_ts_data {
	struct i2c_client	   *client;
	struct input_dev	   *input_dev;
	struct work_struct		irq_work;
	struct delayed_work		keepalive_work;
	struct delayed_work		notify_release_work;
	struct mutex			lock;

	uint16_t				addr;
	int						before_finger[MULTI_NUM_MAX];
	unsigned long			last_touch_time;

	int 					(*power)(int on);

	uint8_t					multi_num;
	uint32_t				all_data_size;
	uint32_t				coordinate_data_size;
	uint32_t				finger_data_size;
	uint32_t				finger_data_offset;
	uint32_t				id_info_offset;

	dma_addr_t				buf_phys;
	void				   *buf_virt;

	int						dbgregp;
	int						sleep_state;
	int						last_sleep_state;
	int						is_suspend;

	/* versiont info */
	uint8_t					fw_ver_H;
	uint8_t					fw_ver_L;
	uint8_t					config_ver;

	/* TP info */
	uint32_t				rx_num;
	uint32_t				tx_num;
	uint32_t				x_res;
	uint32_t				y_res;
	uint8_t					xy_reverse;
	uint8_t					int_is_edge;
	uint8_t					idle_mode;
};

struct himax_hx8526_ts_data *himax_hx8526_icx_ts_data = NULL;

struct xy_coordinate_t {
	uint8_t xh;
	uint8_t xl;
	uint8_t yh;
	uint8_t yl;
};

struct id_info_t {
	uint8_t  point_count;
	uint8_t id1;
	uint8_t id2;
	uint8_t  sum;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void himax_hx8526_ts_early_suspend(struct early_suspend *h);
static void himax_hx8526_ts_late_resume(struct early_suspend *h);
#endif
static int  himax_hx8526_ts_sleep(struct himax_hx8526_ts_data *ts, int en);
static int  himax_hx8526_ts_poweron(struct himax_hx8526_ts_data *ts);
static int  himax_hx8526_ts_reinitialize(struct himax_hx8526_ts_data *ts);

static void himax_hx8526_ts_reset(struct himax_hx8526_ts_data *ts)
{
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;

	gpio_set_value(pdata->xrst_gpio, 0);
}

static void himax_hx8526_ts_unreset(struct himax_hx8526_ts_data *ts)
{
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;

	gpio_set_value(pdata->xrst_gpio, 1);
	msleep(20);
	gpio_set_value(pdata->xrst_gpio, 0);
	msleep(20);
	gpio_set_value(pdata->xrst_gpio, 1);
	msleep(20);
}

static int himax_hx8526_ts_write(struct himax_hx8526_ts_data *ts, uint8_t length, uint8_t *data)
{
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;
	struct i2c_msg msg;
	int ret;

	msg.addr	 = ts->client->addr;
	msg.flags	 = 0;
	msg.len		 = length;
	msg.buf		 = data;

	ret = i2c_transfer(ts->client->adapter, &msg, 1);
	if (ret < 0) {
		pr_err("[HIMAX] failed to write(%d)\n", ret);
		return ret;
	}

	return 0;
}

static int himax_hx8526_ts_read(struct himax_hx8526_ts_data *ts, uint8_t command, uint8_t length, uint8_t *data)
{
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;
	struct i2c_msg msg[2];
	int ret;

	msg[0].addr		= ts->client->addr;
	msg[0].flags	= 0;
	msg[0].len		= 1;
	msg[0].buf		= &command;
	msg[1].addr		= ts->client->addr;
	msg[1].flags	= I2C_M_RD;
	msg[1].len		= length;
	msg[1].buf		= data;

	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		pr_err("[HIMAX] failed to read(%d)\n", ret);
		return ret;
	}

	return 0;
}

static int himax_hx8526_ts_read_touch_info(struct himax_hx8526_ts_data *ts, uint8_t *buf)
{
	uint8_t invalid_data;
	uint8_t sum;
	int i;
	int ret;

	ret = himax_hx8526_ts_read(ts, 0x86, ts->all_data_size, buf);
	if (ret < 0) {
		ERROR("failed to read touch information(%d)\n", ret);

		/* reinitialize */
		ret = himax_hx8526_ts_reinitialize(ts);
		if (ret < 0) {
			ERROR("TS reinitialize failure\n");
		}
		return HIMAX_HX8526_TS_ERR_I2C;
	}

	invalid_data = 1;
	sum = 0;
	for (i = 0; i < ts->all_data_size; i++) {
		if (buf[i] != 0xED) {
			invalid_data = 0;
		}
		sum += buf[i];
	}

	/* all 0xED is invalid data */
	if (invalid_data)
		return HIMAX_HX8526_TS_INVALID_DATA;

	if (sum != 0) {
		ERROR("TS data SUM error(%02Xh)\n", sum);

		/* reinitialize */
		ret = himax_hx8526_ts_reinitialize(ts);
		if (ret < 0) {
			ERROR("TS reinitialize failure\n");
		}
		return HIMAX_HX8526_TS_ERR_SUM;
	}

	return 0;
}

static int himax_hx8526_ts_enable_irq(struct himax_hx8526_ts_data *ts)
{
	uint8_t reg[MAX_DATA_SIZE];
	int ret = 0;

	ret = himax_hx8526_ts_read_touch_info(ts, reg);
	if (ret == HIMAX_HX8526_TS_INVALID_DATA)
		ret = 0;
	else if (ret < 0)
		ERROR("read touch info(%d)\n", ret);

	return ret;
}

static void himax_hx8526_ts_keepalive_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct himax_hx8526_ts_data *ts = container_of(dwork, struct himax_hx8526_ts_data, keepalive_work);
	uint8_t buf;
	int ret = 0;

	mutex_lock(&ts->lock);

	if (!ts->sleep_state) {

		ret = himax_hx8526_ts_read(ts, 0xDC, sizeof(buf), &buf);
		if ((ret < 0) || !(buf & HIMAX_HX8526_TS_ESD_SUM)) {
			pr_err("[%s] ESD triggered(ret=%d, ESD_SUM=%02x)\n", __func__, ret, buf);

			/* reinitialize */
			ret = himax_hx8526_ts_reinitialize(ts);
			if (ret < 0) {
				pr_err("[%s] reinitialize failure\n", __func__);
			}
		}

		queue_delayed_work(himax_hx8526_wq, &ts->keepalive_work, HIMAX_HX8526_TS_KEEPALIVE_PERIOD);
	}

	mutex_unlock(&ts->lock);
}

static void himax_hx8526_ts_notify_release_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct himax_hx8526_ts_data *ts = container_of(dwork, struct himax_hx8526_ts_data, notify_release_work);

	input_report_abs(ts->input_dev, ABS_PRESSURE, 0);
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_sync(ts->input_dev);
}

static void himax_hx8526_ts_irq_work_func(struct work_struct *work)
{
	struct himax_hx8526_ts_data *ts = container_of(work, struct himax_hx8526_ts_data, irq_work);
#ifdef HIMAX_HX8526_ICX_TP_ROT_180
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;
#endif
	uint8_t reg[MAX_DATA_SIZE];

	struct xy_coordinate_t *xy_coordinate_p;
	uint8_t 			   *finger_data_p;
	struct id_info_t	   *id_info_p;
	int 	 finger_num;
	uint8_t  finger;
	uint16_t id_info;
	uint32_t delta;
	int x, y, wx, wy, z;
	int i;
	int ret;

	mutex_lock(&ts->lock);
	ret = himax_hx8526_ts_read_touch_info(ts, reg);
	mutex_unlock(&ts->lock);
	if (ret < 0) {
		if (ret != HIMAX_HX8526_TS_INVALID_DATA)
			ERROR("read touch info(%d)\n", ret);
		return;
	}

	delta = jiffies_to_msecs(jiffies - ts->last_touch_time);
	ts->last_touch_time = jiffies;
	if (delta >= HIMAX_HX8526_TS_IGNORE_PERIOD) {
		/* ignore the first touch */
		return;
	}

	xy_coordinate_p = (struct xy_coordinate_t *)reg;
	finger_data_p   = reg + ts->finger_data_offset;
	id_info_p       = (struct id_info_t *)(reg + ts->id_info_offset);

	if ((id_info_p->id2 == 0xFF) && (id_info_p->id1 == 0xFF))
		id_info = 0;
	else
		id_info = (((uint16_t)id_info_p->id2 & 0x03) << 8) | (uint16_t)id_info_p->id1;
	finger_num = id_info_p->point_count & 0x0F;

	for (i = 0; i < ts->multi_num; i++) {
		finger = (id_info >> i) & 0x0001;

		if (finger == 0 && ts->before_finger[i] == 0)
			continue;
		ts->before_finger[i] = finger;

		x  = ((xy_coordinate_p[i].xh << 8) | xy_coordinate_p[i].xl) * 2;
		y  = ((xy_coordinate_p[i].yh << 8) | xy_coordinate_p[i].yl) * 2;
		wx = finger_data_p[i] * HIMAX_HX8526_TS_FINGER_SIZE_COEF;
		wy = wx;
		z  = finger;
#ifdef HIMAX_HX8526_ICX_TP_ROT_180
		x = pdata->max_x - x;
		y = pdata->max_y - y;
#endif

		if (!z)
			continue;
#if 0 /* debug */
		printk("i = %d, x = %d, y = %d, wx = %d, wy = %d, z = %d\n",
			i, x, y, wx, wy, z);
#endif
		input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, wy);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MINOR, wx);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, wx);
		input_report_abs(ts->input_dev, ABS_PRESSURE, z);
		input_mt_sync(ts->input_dev);
	}

	cancel_delayed_work_sync(&ts->notify_release_work);

	if (finger_num == 0x0F) {
		input_report_abs(ts->input_dev, ABS_PRESSURE, 0);
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
	}
	else {
		input_report_key(ts->input_dev, BTN_TOUCH, 1);

		queue_delayed_work(himax_hx8526_wq,
						   &ts->notify_release_work,
						   HIMAX_HX8526_TS_NOTIFY_RELEASE);
	}

	input_sync(ts->input_dev);
}

static irqreturn_t himax_hx8526_ts_irq_handler(int irq, void * data)
{
	struct  himax_hx8526_ts_data *ts =  himax_hx8526_icx_ts_data;

	queue_work(himax_hx8526_wq, &ts->irq_work);
	return IRQ_HANDLED;
}

static int himax_hx8526_ts_sleep_in(struct himax_hx8526_ts_data *ts)
{
	uint8_t reg;
	int ret = 0;

	reg = 0x82;
	ret = himax_hx8526_ts_write(ts, 1, &reg);
	if (ret < 0) {
		pr_err("Sensing Off error %d\n", ret);
		return ret;
	}
	msleep(50);

	reg = 0x80;
	ret = himax_hx8526_ts_write(ts, 1, &reg);
	if (ret < 0) {
		pr_err("Sleep In error %d\n", ret);
		return ret;
	}
	msleep(50);

	ts->sleep_state = 1;

	return 0;
}

static int himax_hx8526_ts_sleep_out(struct himax_hx8526_ts_data *ts)
{
	uint8_t reg;
	int ret = 0;

	reg = 0x83;
	ret = himax_hx8526_ts_write(ts, 1, &reg);
	if (ret < 0) {
		pr_err("Sensing On error(1) %d\n", ret);
		return ret;
	}
	msleep(50);

	reg = 0x81;
	ret = himax_hx8526_ts_write(ts, 1, &reg);
	if (ret < 0) {
		pr_err("Sleep Out error(1) %d\n", ret);
		return ret;
	}
	msleep(50);

	ts->sleep_state = 0;
	
	return 0;
}

static int himax_hx8526_ts_sleep(struct himax_hx8526_ts_data *ts, int en)
{
	int ret = 0;

	if (en) {
		if (ts->sleep_state == 0) {
			ret = himax_hx8526_ts_sleep_in(ts);
			if (ret < 0) {
				pr_err("ERR: ts_sleep in %d\n", ret);
				return ret;
			}
		}
	}
	else {
		if (ts->sleep_state == 1) {
			ret = himax_hx8526_ts_sleep_out(ts);
			if (ret < 0) {
				pr_err("ERR: ts_sleep out %d\n", ret);
				return ret;
			}
		}
		himax_hx8526_ts_enable_irq(ts);
	}

	return 0;
}


/******************/
/* Debug Routines */
/******************/

static ssize_t dbgregp_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);

	if (!buf)
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "0x%x\n", ts->dbgregp);
}

static ssize_t dbgregp_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	long tmp;

	if (!buf)
		return -EINVAL;

	if (kstrtol(buf, 0, &tmp))
		return -EINVAL;

	ts->dbgregp = tmp;

	return count;
}

static ssize_t dbgreg_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	uint8_t val;
	int ret;

	if (!buf)
		return -EINVAL;

	ret = himax_hx8526_ts_read(ts, ts->dbgregp, sizeof(val), &val);
	if (ret < 0)
		return ret;

	return snprintf(buf, PAGE_SIZE, "0x%x\n", val);
}

static ssize_t dbgreg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	int ret;
	uint8_t data[2];
	long tmp;

	if (!buf)
		return -EINVAL;

	if (kstrtol(buf, 0, &tmp))
		return -EINVAL;

	data[0] = ts->dbgregp;
	data[1] = tmp;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0)
		return ret;
	return count;
}

static ssize_t xrst_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;
	int ret;

	if (!buf)
		return -EINVAL;

	gpio_get_value(pdata->xrst_gpio);

	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t xrst_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;
	int ret = 0;
	long tmp;

	if (!buf)
		return -EINVAL;

	if (kstrtol(buf, 0, &tmp))
		return -EINVAL;

	if (tmp)
		gpio_set_value(pdata->xrst_gpio, 1);
	else
		gpio_set_value(pdata->xrst_gpio, 0);
	ret =0;
	if (ret < 0)
		return ret;
	else
		return count;
}

static ssize_t sleep_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);

	if (!buf)
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%d\n", ts->sleep_state);
}

static ssize_t sleep_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	int ret = 0;
	long tmp;

	if (!buf)
		return -EINVAL;

	if (kstrtol(buf, 0, &tmp))
		return -EINVAL;

	if (tmp)
		cancel_delayed_work_sync(&ts->keepalive_work);

	mutex_lock(&ts->lock);

	if (tmp) {
		if (ts->is_suspend == 0) {
			if (ts->sleep_state == 0)
				ret = himax_hx8526_ts_sleep_in(ts);
		} else {
			ts->last_sleep_state = 1;
		}
	}
	else {
		if (ts->is_suspend == 0) {
			if (ts->sleep_state == 1) {
				ret = himax_hx8526_ts_sleep_out(ts);
				queue_delayed_work(himax_hx8526_wq,
								   &ts->keepalive_work,
								   HIMAX_HX8526_TS_KEEPALIVE_PERIOD);
			}
			himax_hx8526_ts_enable_irq(ts);
		} else {
			ts->last_sleep_state = 0;
		}
	}

	mutex_unlock(&ts->lock);

	if (ret < 0)
		return ret;
	else
		return count;
}

static ssize_t clear_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct himax_hx8526_ts_data *ts = dev_get_drvdata(dev);
	uint8_t reg = 0x88;
	int ret;

	ret = himax_hx8526_ts_write(ts, 1, &reg);

	if (ret < 0)
		return ret;
	else
		return count;
}

static DEVICE_ATTR(regp, 0600, dbgregp_show, dbgregp_store);
static DEVICE_ATTR(regv, 0600, dbgreg_show, dbgreg_store);
static DEVICE_ATTR(xrst, 0600, xrst_show, xrst_store);
static DEVICE_ATTR(sleep, 0600, sleep_show, sleep_store);
static DEVICE_ATTR(clear, 0200, NULL, clear_store);


/*******/
/* I/F */
/*******/

static int himax_hx8526_ts_read_version(struct himax_hx8526_ts_data *ts)
{
	uint8_t buf[3];
	int ret = 0;

	/* read IC part number */
	ret = himax_hx8526_ts_read(ts, 0xD1, sizeof(buf), buf);
	if (ret < 0) {
		pr_err("[%s] couldn't read IC part number\n", __func__);
		return ret;
	} else {
		printk("%s: Device ID = %02x %02x %02x\n", HIMAX_HX8526_ICX_NAME,
												   buf[0], buf[1], buf[2]);
	}

	/* read FW version */
	ret = himax_hx8526_ts_read(ts, 0x33, 1, &buf[0]);
	if (ret < 0) {
		pr_err("[%s] failed to read FW version %d\n", __func__, ret);
		return ret;
	}
	ret = himax_hx8526_ts_read(ts, 0x32, 1, &buf[1]);
	if (ret < 0) {
		pr_err("[%s] failed to read FW version %d\n", __func__, ret);
		return ret;
	}
	printk("%s: FW version = %02x %02x\n", HIMAX_HX8526_ICX_NAME,
										   buf[0], buf[1]);
	ts->fw_ver_H = buf[0];
	ts->fw_ver_L = buf[1];

	/* read Config version */
	ret = himax_hx8526_ts_read(ts, 0x39, 1, &buf[0]);
	if (ret < 0) {
		pr_err("[%s] failed to read Config version %d\n", __func__, ret);
		return ret;
	}
	printk("%s: Config version = %02x\n", HIMAX_HX8526_ICX_NAME, buf[0]);
	ts->config_ver = buf[0];

	return 0;
}

static int himax_hx8526_ts_set_idle_mode(
	struct himax_hx8526_ts_data *ts,
	uint8_t mode)
{
	uint8_t data[3] = {0};
	int ret = 0;

	data[0] = 0x8C;
	data[1] = 0x14;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 14\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x8B;
	data[1] = 0x00;
	data[2] = 0x02;
	ret = himax_hx8526_ts_write(ts, 3, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8B 00 02\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x40;
	data[1] = mode;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to set IDLE threshold\n", __func__);
		return ret;
	}

	data[0] = 0x8C;
	data[1] = 0x00;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 00\n", __func__);
		return ret;
	}
	msleep(10);

	return 0;
}

static int himax_hx8526_ts_initialize_param(struct himax_hx8526_ts_data *ts)
{
	int ret = 0;

	/* set IDLE mode */
	ret = himax_hx8526_ts_set_idle_mode(ts, HIMAX_HX8526_TS_IDLE_MODE_DISABLE);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int himax_hx8526_ts_information(struct himax_hx8526_ts_data *ts)
{
	int ret = 0;
#ifdef HIMAX_HX8526_ICX_TP_FIXED_MULTI_NUM
	ts->multi_num = HIMAX_HX8526_ICX_TP_FIXED_MULTI_NUM;
#else
	uint8_t data[12] = {0};

	data[0] = 0x8C;
	data[1] = 0x14;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 14\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x8B;
	data[1] = 0x00;
	data[2] = 0x70;
	ret = himax_hx8526_ts_write(ts, 3, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8B 00 70\n", __func__);
		return ret;
	}
	msleep(10);

	ret = himax_hx8526_ts_read(ts, 0x5A, 12, data);
	if (ret < 0) {
		pr_err("[%s] failed to read coordinate information\n", __func__);
		return ret;
	}
	DPRINTK("HX_RX_NUM  : %02X\n", data[0]);
	DPRINTK("HX_TX_NUM  : %02X\n", data[1]);
	DPRINTK("HX_MAX_PT  : %02X\n", data[2] >> 4);
	ts->multi_num = data[2] >> 4;
	DPRINTK("HX_BT_NUM  : %02X\n", data[2] & 0x0F);
	DPRINTK("HX_XY_REVERSE : %02X\n", data[4] & 0x04);
	DPRINTK("HX_X_RES : %02X%02X\n", data[6], data[7]);
	DPRINTK("HX_Y_RES : %02X%02X\n", data[8], data[9]);
	ts->rx_num = data[0];
	ts->tx_num = data[1];
	ts->x_res = ((uint32_t)data[6] << 8) | data[7];
	ts->y_res = ((uint32_t)data[8] << 8) | data[9];
	ts->xy_reverse = (data[4] >> 2) & 0x01;

	data[0] = 0x8C;
	data[1] = 0x00;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 00\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x8C;
	data[1] = 0x14;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 14\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x8B;
	data[1] = 0x00;
	data[2] = 0x02;
	ret = himax_hx8526_ts_write(ts, 3, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8B 00 02\n", __func__);
		return ret;
	}
	msleep(10);

	ret = himax_hx8526_ts_read(ts, 0x5A, 10, data);
	if (ret < 0) {
		pr_err("[%s] failed to read edge information\n", __func__);
		return ret;
	}
	DPRINTK("HX_INT_IS_EDGE  : %02X\n", data[1] & 0x01);
	ts->int_is_edge = data[1] & 0x01;

	data[0] = 0x8C;
	data[1] = 0x00;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 00\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x8C;
	data[1] = 0x14;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 14\n", __func__);
		return ret;
	}
	msleep(10);

	data[0] = 0x8B;
	data[1] = 0x00;
	data[2] = 0x02;
	ret = himax_hx8526_ts_write(ts, 3, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8B 00 02\n", __func__);
		return ret;
	}
	msleep(10);

	ret = himax_hx8526_ts_read(ts, 0x5A, 1, data);
	if (ret < 0) {
		pr_err("[%s] failed to read idle threshold\n", __func__);
		return ret;
	}
	DPRINTK("IDLE_MODE  : %02X\n", data[0]);
	ts->idle_mode = data[0];

	data[0] = 0x8C;
	data[1] = 0x00;
	ret = himax_hx8526_ts_write(ts, 2, data);
	if (ret < 0) {
		pr_err("[%s] failed to write 8C 00\n", __func__);
		return ret;
	}
	msleep(10);

	/* read Config version */
	ret = himax_hx8526_ts_read(ts, 0x39, 1, &data[0]);
	if (ret < 0) {
		pr_err("[%s] failed to read Config version %d\n", __func__, ret);
		return ret;
	}
	DPRINTK("%s: Config version = %02x\n", HIMAX_HX8526_ICX_NAME, data[0]);
#endif

	ts->coordinate_data_size = COORDINATE_DATA_SIZE(ts->multi_num);
	ts->finger_data_size     = FINGER_DATA_SIZE(ts->multi_num);
	ts->all_data_size        = ts->coordinate_data_size + ts->finger_data_size + ID_INFO_DATA_SIZE;
	ts->finger_data_offset   = ts->coordinate_data_size;
	ts->id_info_offset       = ts->finger_data_offset + ts->finger_data_size;

	if (ts->multi_num > MULTI_NUM_LIMIT) {
		ts->multi_num = MULTI_NUM_LIMIT;
	}

	return 0;
}

static int himax_hx8526_ts_poweron(struct himax_hx8526_ts_data *ts)
{
	int ret = 0;

	ret = himax_hx8526_ts_sleep_out(ts);
	if (ret < 0) {
		return ret;
	}

	ret = himax_hx8526_ts_sleep_in(ts);
	if (ret < 0) {
		return ret;
	}

	/* initialize TP parameters */
	ret = himax_hx8526_ts_initialize_param(ts);
	if (ret < 0) {
		pr_err("[%s] fail to initilize parameters\n", __func__);
		return ret;
	}

	/* read touch info. and settings from touch controller */
	ret = himax_hx8526_ts_information(ts);
	if (ret < 0) {
		pr_err("[%s] fail to read TS information\n", __func__);
		return ret;
	}

	ret = himax_hx8526_ts_sleep_out(ts);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int himax_hx8526_ts_reinitialize(struct himax_hx8526_ts_data *ts)
{
	int sleep_state;
	int ret = 0;

	/* reset */
	himax_hx8526_ts_reset(ts);
	msleep(20);
	himax_hx8526_ts_unreset(ts);

	sleep_state = ts->sleep_state;

	/* reinitialize */
	ret = himax_hx8526_ts_poweron(ts);
	if (ret < 0) {
		pr_err("[%s] power on failure\n", __func__);
	}
/*
	if (sleep_state) {
		ret = himax_hx8526_ts_sleep_in(ts);
		if (ret < 0) {
			pr_err("[%s] sleep in failure\n", __func__);
		}
	}*/

	return ret;
}

static int himax_hx8526_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct himax_hx8526_ts_data *ts;
	struct ts_icx_platform_data *pdata = &himax_hx8526_icx_ts_pdata;
	int i;
	int ret = 0;

	gpio_request(pdata->xrst_gpio, "tp_xrst");
	gpio_export(pdata->xrst_gpio, true);
	gpio_direction_output(pdata->xrst_gpio, 0);

	gpio_request(pdata->xint_gpio, "tp_xint");
	gpio_export(pdata->xint_gpio, true);
	gpio_direction_input(pdata->xint_gpio);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		ERROR("need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	INIT_WORK(&ts->irq_work, himax_hx8526_ts_irq_work_func);
	INIT_DELAYED_WORK(&ts->keepalive_work, himax_hx8526_ts_keepalive_work_func);
	INIT_DELAYED_WORK(&ts->notify_release_work, himax_hx8526_ts_notify_release_work_func);
	mutex_init(&ts->lock);
	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->power = pdata->power;
	if (ts->power) {
		ret = ts->power(1);
		if (ret < 0) {
			ERROR("power on failed\n");
			goto err_power_failed;
		}
	}

	himax_hx8526_ts_reinitialize(ts);

	ret = himax_hx8526_ts_read_version(ts);
	if (ret < 0) {
		pr_err("[%s] fail to read TS version\n", __func__);
		goto err_detect_failed;
	}

	ret = himax_hx8526_ts_poweron(ts);
	if (ret < 0) {
		pr_err("[%s] power on failure\n", __func__);
		goto err_detect_failed;
	}

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		ERROR("Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = HIMAX_HX8526_ICX_NAME;
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);

	input_set_abs_params(ts->input_dev, ABS_X, pdata->min_x, pdata->max_x, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, pdata->min_y, pdata->max_y, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, pdata->min_x, pdata->max_x, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, pdata->min_y, pdata->max_y, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, HIMAX_HX8526_TS_FINGER_SIZE, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, HIMAX_HX8526_TS_FINGER_SIZE, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MINOR, 0, HIMAX_HX8526_TS_FINGER_SIZE, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, HIMAX_HX8526_TS_Z_MAX, 0, 0);

	input_set_events_per_packet(ts->input_dev,
								(7 * ts->multi_num) + 1 + 1); /* (events/touch=6) * point + BTN_TOUCH + SYNC */

	ret = input_register_device(ts->input_dev);
	if (ret) {
		ERROR("Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	himax_hx8526_icx_ts_data = ts; 

	irq_set_irq_type(gpio_to_irq(pdata->xint_gpio), pdata->irqflags);
	request_irq(gpio_to_irq(pdata->xint_gpio), himax_hx8526_ts_irq_handler, 0, "himax_hx8526_ts", NULL );

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = himax_hx8526_ts_early_suspend;
	ts->early_suspend.resume  = himax_hx8526_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	device_create_file(&client->dev, &dev_attr_regv);
	device_create_file(&client->dev, &dev_attr_regp);
	device_create_file(&client->dev, &dev_attr_xrst);
	device_create_file(&client->dev, &dev_attr_sleep);
	device_create_file(&client->dev, &dev_attr_clear);

	ts->last_touch_time = jiffies;

	himax_hx8526_ts_enable_irq(ts);
	queue_delayed_work(himax_hx8526_wq, &ts->keepalive_work, HIMAX_HX8526_TS_KEEPALIVE_PERIOD);

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
err_detect_failed:
	dma_free_coherent(NULL, MAX_DATA_SIZE, ts->buf_virt, ts->buf_phys);
err_dma_alloc_coherent_failed:
err_power_failed:
	if (himax_hx8526_wq)
		destroy_workqueue(himax_hx8526_wq);
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static int himax_hx8526_ts_remove(struct i2c_client *client)
{
	struct himax_hx8526_ts_data *ts = i2c_get_clientdata(client);

	//mt_eint_mask(client->irq);

	cancel_work_sync(&ts->irq_work);
	cancel_delayed_work_sync(&ts->keepalive_work);
	cancel_delayed_work_sync(&ts->notify_release_work);
	if (himax_hx8526_wq)
		destroy_workqueue(himax_hx8526_wq);

	//unregister_early_suspend(&ts->early_suspend);
	//mt_eint_registration(client->irq, pdata->irqflags, NULL, 0);
	input_unregister_device(ts->input_dev);
	if (ts->buf_virt) {
		dma_free_coherent(NULL, MAX_DATA_SIZE, ts->buf_virt, ts->buf_phys);
	}

	kfree(ts);

	return 0;
}

static void himax_hx8526_ts_shutdown(struct i2c_client *client)
{
	himax_hx8526_ts_remove(client);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void himax_hx8526_ts_early_suspend(struct early_suspend *h)
{
	struct  himax_hx8526_ts_data *ts;

	ts = container_of(h, struct  himax_hx8526_ts_data, early_suspend);

	//mt_eint_mask(ts->client->irq);

	cancel_work_sync(&ts->irq_work);
	cancel_delayed_work_sync(&ts->keepalive_work);

	mutex_lock(&ts->lock);

	ts->last_sleep_state = ts->sleep_state;

	if (ts->power)
		ts->power(0);
	else
		himax_hx8526_ts_sleep(ts, 1);

	ts->is_suspend = 1;

	mutex_unlock(&ts->lock);
}

static void himax_hx8526_ts_late_resume(struct early_suspend *h)
{
	struct  himax_hx8526_ts_data *ts;

	ts = container_of(h, struct  himax_hx8526_ts_data, early_suspend);

	mutex_lock(&ts->lock);

	if (ts->last_sleep_state == 0) {
		if (ts->power)
			ts->power(1);
		else
			himax_hx8526_ts_sleep(ts, 0);

		queue_delayed_work(himax_hx8526_wq,
						   &ts->keepalive_work,
						   HIMAX_HX8526_TS_KEEPALIVE_PERIOD);
	}

	ts->is_suspend = 0;

	//mt_eint_ack(ts->client->irq);
	//mt_eint_unmask(ts->client->irq);

	mutex_unlock(&ts->lock);
}
#endif


static struct of_device_id hx8526_match_table[] = {
        { .compatible = "sony,himax_hx8526",},
        { },
};

MODULE_DEVICE_TABLE(of, hx8526_match_table);

static const struct i2c_device_id himax_hx8526_ts_id[] = {
	{ HIMAX_HX8526_ICX_NAME, 0 },
	{ }
};

static struct i2c_driver himax_hx8526_ts_driver = {
	.probe		= himax_hx8526_ts_probe,
	.remove		= himax_hx8526_ts_remove,
	.shutdown   = himax_hx8526_ts_shutdown,
#ifndef CONFIG_HAS_EARLYSUSPEND
	//.suspend	= himax_hx8526_ts_suspend,
	//.resume		= himax_hx8526_ts_resume,
#endif
	.id_table	= himax_hx8526_ts_id,
	.driver = {
		.name	= HIMAX_HX8526_ICX_NAME,
		.of_match_table = of_match_ptr(hx8526_match_table),
	},
};

static int __init himax_hx8526_ts_init(void)
{
	int ret = 0;

	himax_hx8526_wq = create_singlethread_workqueue("himax_hx8526_wq");
	if (!himax_hx8526_wq)
		return -ENOMEM;

	ret = i2c_add_driver(&himax_hx8526_ts_driver);
	if (ret < 0)
		destroy_workqueue(himax_hx8526_wq);

	return ret;
}

static void __exit himax_hx8526_ts_exit(void)
{
	i2c_del_driver(&himax_hx8526_ts_driver);
	if (himax_hx8526_wq)
		destroy_workqueue(himax_hx8526_wq);
}

module_init(himax_hx8526_ts_init);
module_exit(himax_hx8526_ts_exit);

MODULE_DESCRIPTION("Himax HX8526-E30 Touchscreen Driver");
MODULE_LICENSE("GPL");
