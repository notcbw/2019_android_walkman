/*
 * sony_max1704x.c
 *
 * Copyright 2017,2018,2019 Sony Video & Sound Products Inc.
 * Author: Sony Video & Sound Products Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc
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
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/semaphore.h>
#include <linux/suspend.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/power_supply.h>
#include <linux/power/bq25898_icx_charger.h>

#include <linux/power/sony_max1704x.h>

/*
 ***********************
 * @ global_definitions
 ***********************
 */

#define MAX1704X_ENV_SIZE 1024

#define VERIFY_AND_FIX 1
#define LOAD_MODEL !(VERIFY_AND_FIX)

#define print_debug(ctx, fmt, args ...) \
	dev_dbg(&ctx->i2c->dev, fmt "\n", ##args)

#define print_info(ctx, fmt, args ...) \
	dev_info(&ctx->i2c->dev, fmt "\n", ##args)

#define print_warning(ctx, fmt, args ...) \
	dev_warn(&ctx->i2c->dev, fmt "\n", ##args)

#define print_error(ctx, fmt, args ...) \
	dev_err(&ctx->i2c->dev, "%s(): " fmt "\n", __func__, ##args)

#define lib_failure(ctx, name, err) \
	dev_err(&ctx->i2c->dev, "%s error %d occurred.\n", name, err)

#define back_trace(ctx) \
	dev_err(&ctx->i2c->dev, "%s(): [%d]\n", __func__, __LINE__)

#define TRY(_f) \
do { \
	rv = (_f); \
	if (rv < 0) { \
		goto fin; \
	} \
} while (0)

struct max1704x_context {
	struct i2c_client *i2c;

	struct power_supply *battery;

	struct sony_max1704x_platform_data pdat;

	struct mutex mutex_reg;

	bool state_adjust;
	struct delayed_work work_adjust;

	bool shutdown_request;

	char env[MAX1704X_ENV_SIZE];

	u64 jiffies_start;
	u64 jiffies_prev;
};

struct max1704x_proxy {
	int			status;	/* Return Status */
	struct max1704x_context	*ctx;	/* Driver context. */
};

/* Semaphore to lock proxy */
DEFINE_SEMAPHORE(max1704x_proxy_sem);

static struct max1704x_proxy max1704x_proxy = {
	.status = -EPROBE_DEFER,
	.ctx = NULL,
};

/*
 ***********************
 * @ environment_access
 ***********************
 */

static char *max1704x_findenv(char *env, const char *name, size_t len)
{
	char *ep;

	for (ep = env; *ep; ep += strlen(ep) + 1)
		if (strncmp(ep, name, len) == 0 && ep[len] == '=')
			break;
	return ep;
}

static char *max1704x_getenv(char *env, const char *name)
{
	char *ep;
	size_t len;

	if (name == NULL)
		return NULL;
	len = strlen(name);
	if (len == 0)
		return NULL;

	ep = max1704x_findenv(env, name, len);
	if (*ep)
		return ep + len + 1;

	return NULL;
}

static void max1704x_addenv(char *env, const char *combined, size_t len)
{
	char *ep, *ep2, *ep3;
	size_t namelen, length;

	if (combined == NULL)
		return;
	ep = memchr(combined, '=', len);
	if (ep == NULL)
		return;
	namelen = ep - combined;
	if (namelen == 0)
		return;

	ep = max1704x_findenv(env, combined, namelen);
	if (*ep) {
		ep2 = ep + strlen(ep) + 1;
		for (ep3 = ep2; *ep3; ep3 += strlen(ep3) + 1)
			continue;
		length = ep3 - ep2;
		if (length > 0) {
			memcpy(ep, ep2, length);
			ep += length;
		}
		*ep = 0;
	}
	if (len - namelen > 1 && ep + len + 1 < env + MAX1704X_ENV_SIZE) {
		memcpy(ep, combined, len);
		ep[len] = ep[len + 1] = 0;
	}
}

/*
 ********************
 * @ register_access
 ********************
 */

static int max1704x_read_buf_core(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint8_t size,
	void *buf)
{
	struct i2c_msg msg[2];
	int rv;

	msg[0].addr = ctx->i2c->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &addr;

	msg[1].addr = ctx->i2c->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = size;
	msg[1].buf = buf;

	rv = i2c_transfer(ctx->i2c->adapter, msg, 2);
	if (rv < 0) {
		lib_failure(ctx, "i2c_transfer(read):", rv);
		back_trace(ctx);
		return rv;
	}

	return 0;
}

static int max1704x_write_buf_core(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint8_t size,
	const void *buf)
{
	struct i2c_msg msg;
	int rv;
	uint8_t write_buf[80];

	write_buf[0] = addr;
	memcpy(write_buf+1, buf, size);
	msg.addr = ctx->i2c->addr;
	msg.flags = 0;
	msg.len = size + 1;
	msg.buf = write_buf;

	rv = i2c_transfer(ctx->i2c->adapter, &msg, 1);
	if (rv < 0) {
		lib_failure(ctx, "i2c_transfer(write):", rv);
		back_trace(ctx);
		return rv;
	}

	return 0;
}

static int max1704x_read_reg(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint16_t *value)
{
	int rv = 0;

	mutex_lock(&ctx->mutex_reg);

	rv = max1704x_read_buf_core(ctx, addr, 2, value);
	if (rv < 0)
		goto fin;

	*value = be16_to_cpu(*value);
fin:
	mutex_unlock(&ctx->mutex_reg);
	return rv;
}

static int max1704x_write_reg(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint16_t value)
{
	int rv = 0;

	mutex_lock(&ctx->mutex_reg);

	value = cpu_to_be16(value);

	rv = max1704x_write_buf_core(ctx, addr, 2, &value);

	mutex_unlock(&ctx->mutex_reg);
	return rv;
}

static int max1704x_modify_reg(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint16_t mask,
	uint16_t value)
{
	uint16_t tmp;
	int rv = 0;

	mutex_lock(&ctx->mutex_reg);

	rv = max1704x_read_buf_core(ctx, addr, 2, &tmp);
	if (rv < 0)
		goto fin;

	tmp = cpu_to_be16((be16_to_cpu(tmp) & ~mask) | (value & mask));

	rv = max1704x_write_buf_core(ctx, addr, 2, &tmp);

fin:
	mutex_unlock(&ctx->mutex_reg);
	return rv;
}

#ifdef USE_MAX1704X_READ_BUF

static int max1704x_read_buf(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint8_t size,
	void *buf)
{
	int rv = 0;

	mutex_lock(&ctx->mutex_reg);

	rv = max1704x_read_buf_core(ctx, addr, size, buf);

	mutex_unlock(&ctx->mutex_reg);
	return rv;
}

#endif

static int max1704x_write_buf(
	struct max1704x_context *ctx,
	uint8_t addr,
	uint8_t size,
	const void *buf)
{
	int rv = 0;

	mutex_lock(&ctx->mutex_reg);

	rv = max1704x_write_buf_core(ctx, addr, size, buf);

	mutex_unlock(&ctx->mutex_reg);
	return rv;
}

/*
 ***********************
 * @ monotonic_routines
 ***********************
 */

static int max1704x_unlock_model_access(
	struct max1704x_context *ctx,
	u_int16_t *ocv)
{
	int rv = 0;
	uint16_t val;
	int n;

	for (n = 0; n < MAX1704X_LOCK_TRY_COUNT; n++) {

		/* unlock model access */
		TRY(max1704x_write_reg(
			ctx,
			MAX1704X_REG_LOCK,
			MAX1704X_LOCK_DISABLE));

		/* verify model access unlocked */
		TRY(max1704x_read_reg(ctx, MAX1704X_REG_OCV, &val));
		if (val != 0xFFFF)
			break;
	}
	if (n == MAX1704X_LOCK_TRY_COUNT) {
		print_error(ctx, "can not unlock model access.");
		rv = -EIO;
		goto fin;
	}

	if (ocv)
		*ocv = val;

fin:
	return rv;
}

static int max1704x_lock_model_access(
	struct max1704x_context *ctx)
{
	int rv = 0;

	/* lock model access */
	TRY(max1704x_write_reg(
		ctx,
		MAX1704X_REG_LOCK,
		MAX1704X_LOCK_ENABLE));

	/* wait chip ready (150ms-600ms) */
	msleep(160);

fin:
	return rv;
}

static int max1704x_load_model_data(
	struct max1704x_context *ctx)
{
	int rv = 0;
	uint16_t val;
	uint16_t ocv;
	int n;
	int load_or_verify = LOAD_MODEL;

	/* read STATUS */
	TRY(max1704x_read_reg(ctx, MAX1704X_REG_STATUS, &val));
	if (val & MAX1704X_STATUS_RI)
		load_or_verify = LOAD_MODEL;
	else
		load_or_verify = VERIFY_AND_FIX;

	for (n = 0; n < MAX1704X_LOAD_TRY_COUNT; n++) {

		/* unlock model access */
		TRY(max1704x_unlock_model_access(ctx, &ocv));

		if (load_or_verify == LOAD_MODEL) {
			/* load model data */
			TRY(max1704x_write_buf(
				ctx,
				MAX1704X_REG_TABLE,
				MAX1704X_MODEL_DATA_SIZE,
				ctx->pdat.model_data));

			/* load rcomp data */
			TRY(max1704x_write_buf(
				ctx,
				MAX1704X_REG_RCOMP,
				MAX1704X_RCOMP_DATA_SIZE,
				ctx->pdat.rcomp_data));
		}

		/* write OCV test value */
		TRY(max1704x_write_reg(
			ctx,
			MAX1704X_REG_OCV,
			ctx->pdat.ocv_test));

		/* disable hibernate */
		TRY(max1704x_write_reg(
			ctx,
			MAX1704X_REG_HIBRT,
			MAX1704X_HIBRT_DISABLE));

		/* lock model access */
		TRY(max1704x_lock_model_access(ctx));

		/* read SOC and compare to expected result */
		TRY(max1704x_read_reg(ctx, MAX1704X_REG_SOC, &val));
		val = (val >> 8) & 0xFF;

		if (val >= ctx->pdat.check_min
		&&  val <= ctx->pdat.check_max)
			break;

		load_or_verify = LOAD_MODEL;
	}
	if (n == MAX1704X_LOAD_TRY_COUNT) {
		print_error(ctx, "can not load model data.");
		rv = -EIO;
		goto fin;
	}

	/* unlock model access */
	TRY(max1704x_write_reg(
		ctx,
		MAX1704X_REG_LOCK,
		MAX1704X_LOCK_DISABLE));

	/* setup RCOMP */
	TRY(max1704x_modify_reg(
		ctx,
		MAX1704X_REG_CONFIG,
		MAX1704X_CONFIG_RCOMP | MAX1704X_CONFIG_SLEEP |
		MAX1704X_CONFIG_ALSC | MAX1704X_CONFIG_ALRT,
		(ctx->pdat.rcomp_ini << 8) | MAX1704X_CONFIG_ALSC));

	/* setup OCV */
	TRY(max1704x_write_reg(
		ctx,
		MAX1704X_REG_OCV,
		ocv));

	/* restore hibernate */
	TRY(max1704x_write_reg(
		ctx,
		MAX1704X_REG_HIBRT,
		MAX1704X_HIBRT_DEFAULT));

	/* lock model access */
	TRY(max1704x_lock_model_access(ctx));

	/* clear RI */
	TRY(max1704x_modify_reg(
		ctx,
		MAX1704X_REG_STATUS,
		MAX1704X_STATUS_ALART_MASK | MAX1704X_STATUS_RI,
		0));

	print_info(ctx, "complete to load model data.");

fin:
	return rv;
}

static int max1704x_initialize_chip(
	struct max1704x_context *ctx)
{
	int rv = 0;
	int vreset_value =
		(uint64_t)ctx->pdat.vcell_reset * 1000 / MAX1704X_VCELL_RATE;

	/* load model data */
	TRY(max1704x_load_model_data(ctx));

	/* disable sleep mode */
	TRY(max1704x_write_reg(
		ctx,
		MAX1704X_REG_MODE,
		0x0000));

	/* set VRESET value */
	TRY(max1704x_modify_reg(
		ctx,
		MAX1704X_REG_VRESET,
		MAX1704X_VRESET_MASK,
		vreset_value));

fin:
	return rv;
}

static int max1704x_get_battery_voltage_core(
	struct max1704x_context *ctx,
	int *microvolts)
{
	uint16_t vcell;
	uint64_t tmp;
	int rv;

	rv = max1704x_read_reg(ctx, MAX1704X_REG_VCELL, &vcell);
	if (rv < 0)
		return rv;

	tmp = MAX1704X_VCELL_RATE * vcell;
	do_div(tmp, 1000);
	*microvolts = (int)tmp;
	if (ctx->pdat.vcell_rate)
		*microvolts *= ctx->pdat.vcell_rate;

	if (ctx->pdat.shutdown_voltage != 0) {
		bool need_shutdown = *microvolts < ctx->pdat.shutdown_voltage;

		if (need_shutdown
		    && ctx->pdat.startup_guard_time != 0
		    && jiffies_to_msecs(get_jiffies_64() - ctx->jiffies_start)
			<= ctx->pdat.startup_guard_time * 1000
		    && ctx->pdat.startup_guard_voltage != 0
		    && *microvolts >= ctx->pdat.startup_guard_voltage)
			need_shutdown = false;

		if (need_shutdown != ctx->shutdown_request) {
			if (need_shutdown)
				print_warning(ctx,
					      "low voltage: %d [uV]",
					      *microvolts);
			ctx->shutdown_request = need_shutdown;
			power_supply_changed(ctx->battery);
		}
	}

	return 0;
}

static int max1704x_get_battery_capacity_core(
	struct max1704x_context *ctx,
	int *percent)
{
	uint16_t soc;
	int rv;

	rv = max1704x_read_reg(ctx, MAX1704X_REG_SOC, &soc);
	if (rv < 0)
		return rv;

	*percent = (int)(soc / MAX1704X_SOC_RATE);

	return 0;
}

static int max1704x_get_battery_capacity(
	struct max1704x_context *ctx,
	int *percent)
{
	int rv;

	rv = max1704x_get_battery_capacity_core(ctx, percent);
	if (rv < 0)
		return rv;

	if (*percent < ctx->pdat.empty_adjustment)
		*percent = ctx->pdat.empty_adjustment;
	else if (*percent > ctx->pdat.full_adjustment)
		*percent = ctx->pdat.full_adjustment;

	return 0;
}

static int max1704x_get_status(
	struct max1704x_context *ctx,
	int *status)
{
	u16 crate;
	int rv, capacity;
	int tmp_status = POWER_SUPPLY_STATUS_UNKNOWN;

	rv = max1704x_read_reg(ctx, MAX1704X_REG_CRATE, &crate);
	if (rv < 0)
		return rv;

	if (crate >> 15)
		tmp_status = POWER_SUPPLY_STATUS_DISCHARGING;
	else {
		rv = max1704x_get_battery_capacity(ctx, &capacity);
		if (rv < 0)
			return rv;
		if (capacity >= ctx->pdat.full_adjustment)
			tmp_status = POWER_SUPPLY_STATUS_FULL;
		else
			tmp_status = POWER_SUPPLY_STATUS_CHARGING;
	}

	if (status)
		*status = tmp_status;
	return 0;
}

/*
 ***********************
 * @ interrupt_routines
 ***********************
 */

static irqreturn_t max1704x_irq_handler_thread(int irq, void *private)
{
	struct max1704x_context *ctx = private;
	uint16_t config;
	uint16_t status;
	int count = 0;
	int rv;
	const int max_retry = 10;
#ifdef CONFIG_ICX_SILENT_LPA_LOG
	extern bool imx_lpa_is_enabled(void);
#endif

	do {
		rv = max1704x_read_reg(ctx, MAX1704X_REG_CONFIG, &config);
		if (rv < 0)
			goto handled;
	} while (!(config & MAX1704X_CONFIG_ALRT) && ++count <= max_retry);

	rv = max1704x_read_reg(ctx, MAX1704X_REG_STATUS, &status);
	if (rv < 0)
		goto handled;

#ifdef CONFIG_ICX_SILENT_LPA_LOG
	if (!imx_lpa_is_enabled()){
		if (count)
			print_info(ctx, "alert: config=%04X, status=%04X, retry=%d",
				config, status, count);
		else
			print_info(ctx, "alert: config=%04X, status=%04X",
				config, status);
	}
#else
	if (count)
		print_info(ctx, "alert: config=%04X, status=%04X, retry=%d",
			config, status, count);
	else
		print_info(ctx, "alert: config=%04X, status=%04X",
			config, status);
#endif

	/* clear alert */
	rv = max1704x_write_reg(ctx, MAX1704X_REG_STATUS,
		status & ~MAX1704X_STATUS_ALART_MASK);
	rv = max1704x_write_reg(ctx, MAX1704X_REG_CONFIG,
		config & ~MAX1704X_CONFIG_ALRT);

	if (status & MAX1704X_STATUS_SC) {
		int percent = 0;

		rv = max1704x_get_battery_capacity_core(ctx, &percent);
		if (percent < ctx->pdat.empty_adjustment
		    || percent > ctx->pdat.full_adjustment)
			print_info(ctx, "capacity=%d", percent);
	}

	power_supply_changed(ctx->battery);

handled:
	return IRQ_HANDLED;
}

/*
 *************************
 * @ temp_adjust_routines
 *************************
 */

/* Note: about the following value:200 and scaling factor:100000, 10   */
/* - Must increase the scaling factor if number of digits after the    */
/*   decimal point of given original parameter is increased.           */
/* - All the scaling factor used for this parameter must be unified.   */
static uint16_t max1704x_calc_rcomp(
	struct max1704x_context *ctx,
	int temp)
{
	int coef;
	int diff;
	int rcomp;

	if (temp > 200)
		coef = ctx->pdat.temp_co_up;
	else
		coef = ctx->pdat.temp_co_down;

	diff = coef * (temp - 200);

	rcomp = (int)ctx->pdat.rcomp_ini + (diff / 100000 / 10);

	print_debug(
		ctx,
		"coef(x100000)=%d, diff(x100000)=%d, rcomp_ini=%u, rcomp=%d",
		coef,
		diff,
		ctx->pdat.rcomp_ini,
		rcomp);

	if (rcomp < 0)
		rcomp = 0;

	if (rcomp > 255)
		rcomp = 255;

	return (uint16_t)rcomp;
}

static void max1704x_adjust_worker(struct work_struct *work)
{
	static const char * const power_supply_health_text[] = {
		"Unknown", "Good", "Overheat", "Dead", "Over voltage",
		"Unspecified failure", "Cold", "Watchdog timer expire",
		"Safety timer expire"
	};
	static const char * const power_supply_status_text[] = {
		"Unknown", "Charging", "Discharging", "Not charging", "Full"
	};
	struct max1704x_context *ctx = container_of(
		work,
		struct max1704x_context,
		work_adjust.work);
	u64 jiffies_now = get_jiffies_64();
	int capacity = 0, voltage = 0, temp = 0, health = 0, status = 0;
#if defined(CONFIG_CHARGER_BQ25898_ICX)
	uint16_t rcomp;
	int rv;
	union power_supply_propval val;

	rv = bq25898_power_supply_get_property_proxy(
		POWER_SUPPLY_PROP_TEMP, &val);
	if (rv == 0) {
		temp = val.intval;
		print_debug(ctx, "thermal: %d[degCx10]", temp);
		rcomp = max1704x_calc_rcomp(ctx, temp);
		max1704x_modify_reg(
			ctx,
			MAX1704X_REG_CONFIG,
			MAX1704X_CONFIG_RCOMP | MAX1704X_CONFIG_ALRT,
			(rcomp << 8) | MAX1704X_CONFIG_ALRT);
	}
#endif

	if (ctx->pdat.status_interval != 0
	    && jiffies_to_msecs(jiffies_now - ctx->jiffies_prev)
	    >= ctx->pdat.status_interval * 1000) {
		ctx->jiffies_prev = jiffies_now;
		max1704x_get_battery_voltage_core(ctx, &voltage);
		max1704x_get_battery_capacity(ctx, &capacity);
#if defined(CONFIG_CHARGER_BQ25898_ICX)
		rv = bq25898_power_supply_get_property_proxy(
			POWER_SUPPLY_PROP_STATUS, &val);
		if (rv == 0)
			status = val.intval;
		rv = bq25898_power_supply_get_property_proxy(
			POWER_SUPPLY_PROP_HEALTH, &val);
		if (rv == 0)
			health = val.intval;
#endif
		print_info(ctx,
			"capacity=%d voltage=%d temp=%d health=%s status=%s",
			capacity, voltage, temp,
			power_supply_health_text[health],
			power_supply_status_text[status]);
	} else if (ctx->pdat.shutdown_voltage != 0) {
		max1704x_get_battery_voltage_core(ctx, &voltage);
	}

	if (ctx->state_adjust) {
		schedule_delayed_work(
			&ctx->work_adjust,
			msecs_to_jiffies(ctx->pdat.adjust_interval * 1000));
	}
}

/*
 ********************
 * @ get_property
 ********************
 */

static int max1704x_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max1704x_context *ctx;
	char *var;
	uint16_t soc;
	int rv, voltage;

	if (psy == NULL) {
		/* Proxy call. */
		/* Should be called from proxy, lock bq25898_proxy. */
		ctx = max1704x_proxy.ctx;
		if (ctx == NULL) {
			/* Proxy is not ready. */
			return -ENODEV;
		}
	} else {
		/* Power supply call */
		ctx = power_supply_get_drvdata(psy);
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		var = max1704x_getenv(ctx->env, "status");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
#if defined(CONFIG_CHARGER_BQ25898_ICX)
		return bq25898_power_supply_get_property_proxy(psp, val);
#else
		return max1704x_get_status(ctx, &val->intval);
#endif
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		var = max1704x_getenv(ctx->env, "voltage_now");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
		return max1704x_get_battery_voltage_core(ctx, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		var = max1704x_getenv(ctx->env, "charge_full");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
		val->intval = ctx->pdat.full_battery_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		var = max1704x_getenv(ctx->env, "charge_counter");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
		rv = max1704x_read_reg(ctx, MAX1704X_REG_SOC, &soc);
		if (rv < 0)
			return rv;
		if (soc > 100 * MAX1704X_SOC_RATE)
			soc = 100 * MAX1704X_SOC_RATE;
		val->intval = (int)((int64_t)ctx->pdat.full_battery_capacity
				 * soc / (100 * MAX1704X_SOC_RATE));
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		var = max1704x_getenv(ctx->env, "capacity");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
		if (ctx->shutdown_request) {
			val->intval = 0;
			break;
		}
		rv = max1704x_get_battery_capacity(ctx, &val->intval);
		if (rv != 0 || val->intval != 0)
			return rv;
		if (ctx->pdat.startup_guard_time == 0
		    || jiffies_to_msecs(get_jiffies_64() - ctx->jiffies_start)
			> ctx->pdat.startup_guard_time * 1000
		    || ctx->pdat.startup_guard_voltage == 0
		    || max1704x_get_battery_voltage_core(ctx, &voltage) != 0
		    || voltage < ctx->pdat.startup_guard_voltage)
			break;
		val->intval = 1;
		print_info(ctx, "startup guard, voltage: %d [uV]", voltage);
		break;
#if defined(CONFIG_CHARGER_BQ25898_ICX)
	case POWER_SUPPLY_PROP_HEALTH:
		var = max1704x_getenv(ctx->env, "health");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
		return bq25898_power_supply_get_property_proxy(psp, val);
	case POWER_SUPPLY_PROP_TEMP:
		var = max1704x_getenv(ctx->env, "temp");
		if (var && kstrtoint(var, 0, &val->intval) == 0)
			break;
		return bq25898_power_supply_get_property_proxy(psp, val);
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 *************************
 * @ user_access_routines
 *************************
 */

static ssize_t max1704x_status_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct max1704x_context *ctx;
	ssize_t result = 0;
	int voltage;
	int capacity;
	uint16_t config;
	uint16_t crate;
	uint16_t status;
	char tmp[64] = "";
	int rv;
#if defined(CONFIG_CHARGER_BQ25898_ICX)
	union power_supply_propval val;
#endif

	ctx = dev_get_drvdata(dev);

	rv = max1704x_get_battery_voltage_core(ctx, &voltage);
	if (rv < 0)
		return rv;

	rv = max1704x_get_battery_capacity_core(ctx, &capacity);
	if (rv < 0)
		return rv;

	rv = max1704x_read_reg(ctx, MAX1704X_REG_CONFIG, &config);
	if (rv < 0)
		return rv;

	rv = max1704x_read_reg(ctx, MAX1704X_REG_CRATE, &crate);
	if (rv < 0)
		return rv;

	rv = max1704x_read_reg(ctx, MAX1704X_REG_STATUS, &status);
	if (rv < 0)
		return rv;

#if defined(CONFIG_CHARGER_BQ25898_ICX)
	rv = bq25898_power_supply_get_property_proxy(
		POWER_SUPPLY_PROP_TEMP, &val);
	if (rv == 0)
		snprintf(tmp, sizeof(tmp), "temperature = %d [degCx10]\n",
			val.intval);
#endif

	result = snprintf(
		buf,
		PAGE_SIZE,
		"voltage     = %d [uV]\n"
		"capacity    = %d [%%]\n"
		"CONFIG      = %04X\n"
		"CRATE       = %04X\n"
		"STATUS      = %04X\n"
		"%s",
		voltage,
		capacity,
		config,
		crate,
		status,
		tmp);

	return result;
}

static DEVICE_ATTR(status, 0444, max1704x_status_show, NULL);

static ssize_t max1704x_env_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct max1704x_context *ctx = dev_get_drvdata(dev);
	char *ep = ctx->env;
	ssize_t size = 0;

	while (*ep) {
		size += snprintf(buf + size, PAGE_SIZE - size, "%s\n", ep);
		ep += strlen(ep) + 1;
	}

	return size;
}

static ssize_t max1704x_env_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct max1704x_context *ctx = dev_get_drvdata(dev);
	const char *bp, *bp2;

	for (bp = buf; bp && bp < buf + size; bp = bp2) {
		size_t len = buf + size - bp;

		bp2 = memchr(bp, '\n', len);
		if (bp2)
			len = bp2++ - bp;
		max1704x_addenv(ctx->env, bp, len);
	}
	power_supply_changed(ctx->battery);
	return size;
}

static DEVICE_ATTR(env, 0644, max1704x_env_show, max1704x_env_store);

static struct attribute *max1704x_attributes[] = {
	&dev_attr_status.attr,
	&dev_attr_env.attr,
	NULL,
};

static const struct attribute_group max1704x_attr_group = {
	.name = "max1704x",
	.attrs = max1704x_attributes,
};

/*
 ******************************
 * @ power_management_routines
 ******************************
 */

static int max1704x_pm_suspend(struct device *dev)
{
	struct max1704x_context *ctx;

	ctx = dev_get_drvdata(dev);

	print_debug(ctx, "suspend");

	if (ctx->pdat.adjust_interval > 0) {
		ctx->state_adjust = false;
		cancel_delayed_work_sync(&ctx->work_adjust);
	}

	return 0;
}

static int max1704x_pm_resume(struct device *dev)
{
	struct max1704x_context *ctx;

	ctx = dev_get_drvdata(dev);

	print_debug(ctx, "resume");

	if (ctx->pdat.adjust_interval > 0) {
		ctx->state_adjust = true;
		schedule_delayed_work(
			&ctx->work_adjust,
			msecs_to_jiffies(ctx->pdat.adjust_interval * 1000));
	}

	return 0;
}

static int max1704x_pm_poweroff(struct device *dev)
{
	struct max1704x_context *ctx;

	ctx = dev_get_drvdata(dev);

	print_info(ctx, "poweroff");

	if (ctx->pdat.adjust_interval > 0) {
		ctx->state_adjust = false;
		cancel_delayed_work_sync(&ctx->work_adjust);
	}

	return 0;
}

static const struct dev_pm_ops max1704x_pm_ops = {
	.suspend  = max1704x_pm_suspend,
	.resume   = max1704x_pm_resume,
	.poweroff = max1704x_pm_poweroff,
};

/*
 *******************
 * @ entry_routines
 *******************
 */

static enum power_supply_property max1704x_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CAPACITY,
#if defined(CONFIG_CHARGER_BQ25898_ICX)
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TEMP,
#endif
};

static const struct power_supply_desc max1704x_battery_desc = {
	.name		= "max1704x_battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= max1704x_get_property,
	.properties	= max1704x_battery_props,
	.num_properties	= ARRAY_SIZE(max1704x_battery_props),
};

static int max1704x_parse_dt(struct device *dev,
				struct sony_max1704x_platform_data *pdat)
{
	struct device_node *node = dev->of_node;
	uint32_t value;
	int ret;

	if (!node)
		return -ENODEV;

	if (of_property_read_u32(node, "adjust_interval", &value) == 0)
		pdat->adjust_interval = value;
	if (of_property_read_u32(node, "vcell_reset", &value) == 0)
		pdat->vcell_reset = value;
	if (of_property_read_u32(node, "shutdown_voltage", &value) == 0)
		pdat->shutdown_voltage = value;
	if (of_property_read_u32(node, "startup_guard_time", &value) == 0)
		pdat->startup_guard_time = value;
	if (of_property_read_u32(node, "startup_guard_voltage", &value) == 0)
		pdat->startup_guard_voltage = value;
	if (of_property_read_u32(node, "status_interval", &value) == 0)
		pdat->status_interval = value;
	if (of_property_read_u32(node, "full_battery_capacity", &value) == 0)
		pdat->full_battery_capacity = value;
	if (of_property_read_u32(node, "empty_adjustment", &value) == 0)
		pdat->empty_adjustment = value;
	if (of_property_read_u32(node, "full_adjustment", &value) == 0)
		pdat->full_adjustment = value;
	if (of_property_read_u32(node, "rcomp0", &value) == 0)
		pdat->rcomp_ini = value;
	if (of_property_read_u32(node, "temp_co_up", &value) == 0)
		pdat->temp_co_up = value;
	if (of_property_read_u32(node, "temp_co_down", &value) == 0)
		pdat->temp_co_down = value;
	if (of_property_read_u32(node, "ocv_test", &value) == 0)
		pdat->ocv_test = value;
	if (of_property_read_u32(node, "soc_check_a", &value) == 0)
		pdat->check_min = value;
	if (of_property_read_u32(node, "soc_check_b", &value) == 0)
		pdat->check_max = value;
	ret = of_property_read_u32(node, "bits", &value);
	if (of_property_read_u32(node, "rcomp_seg", &value) == 0) {
		size_t i = 0;

		while (i < sizeof(pdat->rcomp_data)) {
			pdat->rcomp_data[i++] = (value >> 8) & 0xff;
			pdat->rcomp_data[i++] = value & 0xff;
		}
	}
	ret = of_property_read_variable_u8_array(node, "model_data",
		pdat->model_data, sizeof(pdat->model_data),
		sizeof(pdat->model_data));

	return 0;
}

static int max1704x_probe_proxy(struct max1704x_context *ctx)
{	struct max1704x_proxy	*proxy;
	int	result = 0;

	down(&(max1704x_proxy_sem));
	proxy = &max1704x_proxy;
	proxy->status = 0;
	if (proxy->ctx) {
		/* Unexpected two or more device context. */
		dev_warn(&ctx->i2c->dev,
			"Unexpected two or more device context. proxy_ctx=0x%p, ctx=0x%p\n",
			proxy->ctx, ctx
		);
		result = -EBUSY;
	} else {
		/* First to configure proxy */
		proxy->ctx = ctx;
	}
	up(&(max1704x_proxy_sem));
	return result;
}

static int max1704x_remove_proxy(struct max1704x_context *ctx)
{	struct max1704x_proxy	*proxy;
	int	result = 0;

	down(&(max1704x_proxy_sem));
	proxy = &max1704x_proxy;
	proxy->status = -ENODEV;
	proxy->ctx = NULL;
	up(&(max1704x_proxy_sem));
	return result;
}

int max1704x_get_property_proxy(
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct max1704x_proxy	*proxy;
	int	status = 0;

	down(&(max1704x_proxy_sem));
	proxy = &max1704x_proxy;
	status = proxy->status;
	if (status != 0) {
		pr_notice("%s: Call proxy while not ready. status=%d\n",
			__func__, status
		);
		goto out;
	}
	status = max1704x_get_property(NULL, psp, val);
out:
	up(&(max1704x_proxy_sem));
	return status;
}

static int max1704x_probe(
	struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(i2c->dev.parent);
	struct power_supply_config psy_cfg = {};
	struct max1704x_context *ctx = NULL;
	int error = 0;
	uint16_t value;
	int rv, capacity, voltage;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	if (i2c->irq <= 0) {
		error = -ENOENT;
		pr_err("%s(): IRQ is not configured. Check device-tree. "
			"ret=%d\n", __func__, error);
		return error;
	}

	ctx = devm_kzalloc(&i2c->dev, sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;

	ctx->jiffies_start = get_jiffies_64();

	ctx->i2c = i2c;
	i2c_set_clientdata(i2c, ctx);
	error = max1704x_parse_dt(&i2c->dev, &ctx->pdat);
	if (error) {
		pr_err("%s(): Parse error. Check device-tree. "
			"ret=%d\n", __func__, error);
		return error;
	}

	psy_cfg.drv_data = ctx;
	ctx->battery = power_supply_register(&i2c->dev,
				&max1704x_battery_desc, &psy_cfg);
	if (IS_ERR(ctx->battery)) {
		pr_err("%s(): failed: power supply register\n", __func__);
		return PTR_ERR(ctx->battery);
	}

	mutex_init(&ctx->mutex_reg);

	/* check chip */
	rv = max1704x_read_reg(ctx, MAX1704X_REG_VERSION, &value);
	if (rv < 0) {
		back_trace(ctx);
		error = rv;
		goto error;
	}
	print_info(ctx, "version = %04X", value);

	/* initialize chip */
	rv = max1704x_initialize_chip(ctx);
	if (rv < 0) {
		back_trace(ctx);
		error = rv;
		goto error;
	}

	/* check voltage and capacity */
	rv = max1704x_get_battery_capacity(ctx, &capacity);
	if (rv < 0)
		goto error;
	rv = max1704x_get_battery_voltage_core(ctx, &voltage);
	if (rv < 0)
		goto error;
	print_info(ctx, "capacity=%d voltage=%d", capacity, voltage);

	/* setup alert interrupt */
	rv = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
		max1704x_irq_handler_thread,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		dev_name(&i2c->dev),
		ctx
	);
	if (rv) {
		pr_err("%s(): Can not request IRQ. irq=%d, ret=%d\n",
			__func__, i2c->irq, rv);
		goto error;
	}

	max1704x_modify_reg(ctx, MAX1704X_REG_STATUS,
		MAX1704X_STATUS_ALART_MASK, 0x0000);
	max1704x_modify_reg(ctx, MAX1704X_REG_CONFIG,
		MAX1704X_CONFIG_ALRT | MAX1704X_CONFIG_ALSC,
		MAX1704X_CONFIG_ALSC);

	/* init adjust work */
	if (ctx->pdat.adjust_interval > 0) {
		INIT_DELAYED_WORK(&ctx->work_adjust, max1704x_adjust_worker);
		ctx->state_adjust = true;
		schedule_delayed_work(
			&ctx->work_adjust,
			msecs_to_jiffies(ctx->pdat.adjust_interval * 1000));
	}

	/* create sysfs */
	rv = sysfs_create_group(&i2c->dev.kobj, &max1704x_attr_group);
	if (rv < 0) {
		lib_failure(ctx, "sysfs_create_group():", rv);
		back_trace(ctx);
		error = rv;
		goto error_adj;
	}

	/* Ignore error. */
	(void) max1704x_probe_proxy(ctx);
	return 0;

error_adj:
	if (ctx->pdat.adjust_interval > 0) {
		ctx->state_adjust = false;
		cancel_delayed_work_sync(&ctx->work_adjust);
	}
	devm_free_irq(&i2c->dev, i2c->irq, ctx);

error:
	power_supply_unregister(ctx->battery);
	return error;
}

static int max1704x_remove(struct i2c_client *i2c)
{
	struct max1704x_context *ctx;

	ctx = i2c_get_clientdata(i2c);

	(void) max1704x_remove_proxy(ctx);

	/* remove sysfs */
	sysfs_remove_group(&i2c->dev.kobj, &max1704x_attr_group);

	/* cancel temp adjust */
	if (ctx->pdat.adjust_interval > 0) {
		ctx->state_adjust = false;
		cancel_delayed_work_sync(&ctx->work_adjust);
	}

	devm_free_irq(&i2c->dev, i2c->irq, ctx);
	power_supply_unregister(ctx->battery);
	return 0;
}

static struct i2c_device_id max1704x_i2c_id[] = {
	{ "max1704x", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, max1704x_i2c_id);

static const struct of_device_id max1704x_of_match[] = {
	{ .compatible = "svs,max1704x", },
	{ },
};
MODULE_DEVICE_TABLE(of, max1704x_of_match);

static struct i2c_driver max1704x_driver = {
	.driver = {
		.name = "max1704x",
		.of_match_table = of_match_ptr(max1704x_of_match),
		.pm = &max1704x_pm_ops,
	},
	.probe = max1704x_probe,
	.remove = max1704x_remove,
	.id_table = max1704x_i2c_id,
};

module_i2c_driver(max1704x_driver);

MODULE_DESCRIPTION("MAX1704X driver");
MODULE_AUTHOR("SONY");
MODULE_LICENSE("GPL");
