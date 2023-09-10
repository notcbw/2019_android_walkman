/*
 * extcon-fusb303d_icx.c
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
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/suspend.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/extcon.h>
#include <linux/wakelock.h>
#include <linux/usb/typec.h>

#include <linux/power/bq25898_icx_charger.h>

#ifdef CONFIG_ICX_SILENT_LPA_LOG
extern bool imx_lpa_is_enabled(void);
#else
static bool imx_lpa_is_enabled(void)
{	/* Always false, output all logs. */
	return false;
}
#endif

#if (!defined(GPIOD_IO_ASSERT))
#define	GPIOD_IO_ASSERT	(1)
#endif /* (!defined(GPIOD_IO_ASSERT)) */

#if (!defined(GPIOD_IO_NEGATE))
#define	GPIOD_IO_NEGATE	(0)
#endif /* (!defined(GPIOD_IO_NEGATE)) */


#ifdef CONFIG_REGMON_DEBUG
#include <misc/regmon.h>
#endif /* CONFIG_REGMON_DEBUG */

#include "extcon-fusb303d_icx.h"
#include <linux/usb/typec_icx.h>

/* FUSB303D Autonomous USB Type-C Port Controller driver.
 *
 * @note FUSB303D supports "src-host" and "snk-device" roles.
 */

/*
 * @ global_definitions
 *
 */

#if (   (defined(PROPERTY_NONE)) \
     || (defined(PROPERTY_FALSE)) \
     || (defined(PROPERTY_TRUE)))
#error Redefined PROPERTY_x
#endif /* (defined(PROPERTY_NONE)) */
#define	PROPERTY_NONE	(-1)
#define	PROPERTY_FALSE	(0)
#define	PROPERTY_TRUE	(1)

static const unsigned int fusb303d_extcon_cables[] = {
	EXTCON_USB_HOST,
	EXTCON_USB,
	EXTCON_CHG_USB_PD,
	EXTCON_NONE,	/*!< Terminator. */
};

/* usb_host and usb_device state. */
#define	FUSB303D_EXTCON_UNKNOWN	(-1)
#define	FUSB303D_EXTCON_NEGATE	(0)	/*! Map to false */
#define	FUSB303D_EXTCON_ASSERT	(1)	/*! Map to true  */


#define	FUSB303D_CONTEXT_MARKER	( \
	  0x0 \
	| (((uint32_t)'F') << 0x00 ) \
	| (((uint32_t)'u') << 0x08 ) \
	| (((uint32_t)'s') << 0x10 ) \
	| (((uint32_t)'b') << 0x18 ) \
	)

struct fusb303d_context {
	struct mutex		mutex_irq_action;
	struct mutex		mutex_chip;
	struct mutex		mutex_extcon;
	struct kref		kref;
	uint32_t		context_marker;

	struct i2c_client	*i2c;
	uint8_t			device_id;
	uint8_t			device_type;
	uint8_t			interrupt_save;
	uint8_t			interrupt1_save;
	uint8_t			status_save;
	uint8_t			status1_save;
	uint8_t			type_save;

	struct wake_lock	wake_lock;

	struct gpio_desc	*en_n_gpio;
	u32			of_auto_sink_th_mv;
#define	FUSB303D_OF_SOURCE_CURRENT_KEEP	(-1)
	long			of_source_current;
	int			of_port_role;
#define	FUSB303D_OF_DISABLE_TIME_MS	(50)
	unsigned int		of_disable_time_ms;
	/*! Reset FUSB303D at initialize procedure. */
	int8_t			of_do_reset;
#define	FUSB303D_OF_SINK_ONLY_NO	(0)
#define	FUSB303D_OF_SINK_ONLY_YES	(1)
	u32			of_sink_only;
#define	FUSB303D_WAKE_LOCK_MS_CHECK	(60 * 1000)
#define	FUSB303D_ATTACH_WAKE_LOCK_MS	(6 * 1000)
	uint			of_attach_wake_lock_ms;
#define	FUSB303D_DETACH_WAKE_LOCK_MS	(5 * 1000)
	uint			of_detach_wake_lock_ms;
#define	FUSB303D_DETACH_SLEEP_TIME_MS	(0)
#define	FUSB303D_DETACH_SLEEP_TIME_CHECK_MS	(1 * 1000)
	uint			of_detach_sleep_time_ms;
	struct extcon_dev	*edev;
	/*! Backing store EN_N, false: EN_N==HIGH, true: EN_N==LOW */
	bool			chip_enabled;

	bool			irq_debug;

#define	DISABLED_TIME_DRP_DEFAULT	(-1)
/* Calculated from FUSB303D VBUS_DET pin RC time constant. */
#define	DISABLED_TIME_SINK_DEFAULT	(690)
#define	DISABLED_TIME_SOURCE_DEFAULT	(-1)

	int			disabled_time_drp;
	int			disabled_time_sink;
	int			disabled_time_source;

	/*! Last EXTCON_USB_HOST state */
	int			usb_host;
	/*! Last EXTCON_USB state */
	int			usb_device;
	/* Last EXTCON_CHG_USB_PD state */
	int			chg_usb_pd;
	/* Last EXTCON_PROP_CHG_MIN state */
	int			prop_chg_min;
	/* USB Type C port context. */
	struct typec_port		*port;
	/* USB Type C capability. */
	struct typec_capability		typec_cap;
	/* USB Type C partner context. */
	struct typec_partner		*partner;
	/* USB Type C partner descriptor. */
	struct typec_partner_desc	typec_partner;

};

#define fusb303d_dev(ctx)	(&((ctx)->i2c->dev))

/* Forward prototypes */

static int fusb303d_connected_to_typec_accessory_refrect(
	struct fusb303d_context *ctx);

static enum typec_pwr_opmode fusb303d_current_to_typec_pwr_opmode(
	long current_ma);

static enum bq25898_icx_pd_properties
	fusb303d_current_bq25898_icx_pd_properties(
	long current_ma);

/* implements functions */

static int str_match_to(const char **list_base, const char *match)
{	const char	*p;
	const char	**list;

	if (!list_base)
		return -1 /* not match */;

	list = list_base;
	while ((p = *list) != NULL) {
		if (strcmp(p, match) == 0)
			return list - list_base; /* pointer subtract */
		list++;
	}
	/* Not match to strings in list. */
	return -1;
}

static const char *str_skip_space(const char *p, ssize_t len)
{	unsigned char	c;

	if ((!p) || (len <= 0))
		return p;

	while (len > 0) {
		c = (unsigned char)(*p);
		if (c == '\0')
			break;
		if (c > (unsigned char) ' ')
			break;
		p++;
		len--;
	}
	return p;
}

static ssize_t str_len_to_space(const char *p_base, ssize_t len)
{	unsigned char	c;
	const char	*p;


	if ((!p_base) || (len <= 0))
		return 0;

	p = p_base;

	while (len > 0) {
		c = (unsigned char)(*p);
		if (c == '\0')
			break;

		if (c <= (unsigned char)' ')
			break;
		p++;
		len--;
	}
	return p - p_base; /* pointer subtract */
}

static char *str_word_trim_cpy(char *buf, ssize_t buf_size,
	const char *p_base, ssize_t p_len)
{	const char *p = p_base;

	ssize_t	l;

	if ((!buf) || (buf_size <= 0))
		return buf;

	*buf = '\0';

	if ((buf_size <= 1) || (!p_base) || (p_len <= 0))
		return buf;

	buf_size--;	/* make space for terminator NUL. */

	p = str_skip_space(p, p_len);
	l = str_len_to_space(p, p_len - (p - p_base));

	if (l >= buf_size)
		l = buf_size; /* truncate string. */

	memcpy(buf, p, l);
	*(buf + l) = '\0';
	return buf;
}


#define	AUTO_SINK_3000MV	(3000)
#define	AUTO_SINK_3100MV	(3100)
#define	AUTO_SINK_3200MV	(3200)
#define	AUTO_SINK_3300MV	(3300)

static uint8_t fusb303d_auto_sink_th_mv_to_reg_th(u32 th_mv)
{	if (th_mv <  AUTO_SINK_3100MV)
		return FUSB303D_AUTO_SNK_TH_3V0;
	if (th_mv <  AUTO_SINK_3200MV)
		return FUSB303D_AUTO_SNK_TH_3V1;
	if (th_mv <  AUTO_SINK_3300MV)
		return FUSB303D_AUTO_SNK_TH_3V2;
	/* 3300mV or higer, AUTO_SINK_3300MV */
	return FUSB303D_AUTO_SNK_TH_3V3;
}

static uint8_t fusb303d_auto_sink_th_mv_to_reg_en(u32 th_mv)
{	if (th_mv == TYPEC_ICX_NO_AUTO_SINK_VOLTAGE)
		return 0x00;
	return FUSB303D_AUTO_SNK_EN;
}

/* Use negative number "undeterminated" role. */
#define	TYPEC_UNDET_ROLE \
	((TYPEC_NO_PREFERRED_ROLE < 0) ? \
	 (TYPEC_NO_PREFERRED_ROLE - 1) : \
	 (-1) \
	)

static const int fusb303d_role_to_type_class_table[] = {
	[TYPEC_ICX_PORT_ROLE_KEEP] =	TYPEC_UNDET_ROLE,
	[TYPEC_ICX_PORT_ROLE_SINK] =	TYPEC_SINK,
	[TYPEC_ICX_PORT_ROLE_SOURCE] =	TYPEC_SOURCE,
	[TYPEC_ICX_PORT_ROLE_DRP] =	TYPEC_NO_PREFERRED_ROLE,
	[TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK] = TYPEC_SINK,
	[TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC] = TYPEC_SOURCE,
	[TYPEC_ICX_PORT_ROLE_UNKNOWN] = TYPEC_UNDET_ROLE,
};

static int fusb303d_role_to_typec_role(int dev_role)
{	int	role;

	if (dev_role < 0)
		return TYPEC_NO_PREFERRED_ROLE;

	if (dev_role >= ARRAY_SIZE(fusb303d_role_to_type_class_table))
		return TYPEC_NO_PREFERRED_ROLE;

	if (dev_role > TYPEC_ICX_PORT_ROLE_VALID_MAX)
		return TYPEC_NO_PREFERRED_ROLE;

	role = fusb303d_role_to_type_class_table[dev_role];

	if (role == TYPEC_UNDET_ROLE)
		return TYPEC_NO_PREFERRED_ROLE;

	return role;
}

static void fusb303d_get(struct fusb303d_context *ctx)
{	kref_get(&(ctx->kref));
}

static void fusb303d_release(struct kref *kref)
{	struct fusb303d_context		*ctx;

	ctx = container_of(kref, struct fusb303d_context, kref);
	if ((ctx->context_marker) != FUSB303D_CONTEXT_MARKER) {
		dev_err(fusb303d_dev(ctx),
			"Broken context marker. "
			"context_marker=0x%.8x\n",
			ctx->context_marker
		);
	}
	ctx->context_marker = 0;

	wake_unlock(&(ctx->wake_lock));
	wake_lock_destroy(&(ctx->wake_lock));
	i2c_set_clientdata(ctx->i2c, NULL);
	devm_kfree(fusb303d_dev(ctx), ctx);
}

static int fusb303d_put(struct fusb303d_context *ctx)
{	return kref_put(&(ctx->kref), fusb303d_release);
}


#define	FUSB303D_TEN_MS			(100)

static int fusb303d_chip_enable(struct fusb303d_context *ctx)
{
	mutex_lock(&(ctx->mutex_chip));
	if (!(ctx->chip_enabled)) {
		if (!IS_ERR_OR_NULL(ctx->en_n_gpio)) {
			dev_info(fusb303d_dev(ctx), "Assert EN_N.\n");
			gpiod_set_value(ctx->en_n_gpio, GPIOD_IO_ASSERT);
			msleep(FUSB303D_TEN_MS);
			ctx->chip_enabled = true;
		}
	}
	mutex_unlock(&(ctx->mutex_chip));
	return 0 /* Success. */;
}

static int fusb303d_chip_disable(struct fusb303d_context *ctx)
{	mutex_lock(&(ctx->mutex_chip));
	if (ctx->chip_enabled) {
		if (!IS_ERR_OR_NULL(ctx->en_n_gpio)) {
			dev_info(fusb303d_dev(ctx), "Negate EN_N.\n");
			gpiod_set_value(ctx->en_n_gpio, GPIOD_IO_NEGATE);
			ctx->chip_enabled = false;
		}
	}
	mutex_unlock(&(ctx->mutex_chip));
	return 0 /* Success */;
}


/* Read FUSB303D register.
 * @pre  Lock	mutex_chip
 */
static int fusb303d_reg_read_unlocked(struct fusb303d_context *ctx,
	uint8_t addr,
	uint8_t *value)
{
	struct i2c_msg msg[2];
	int rv;

	msg[0].addr = ctx->i2c->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &addr;

	msg[1].addr = ctx->i2c->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = value;

	rv = i2c_transfer(ctx->i2c->adapter, msg, 2);
	if (rv < 0) {
		dev_err(fusb303d_dev(ctx), "%s: Read failed. "
			"addr=0x%.2x, rv=%d\n",
			__func__, addr, rv
		);
		return rv;
	}

	return 0;
}

/* Write FUSB303D
 * @pre  Lock	mutex_chip
 */
static int fusb303d_reg_write_unlocked(struct fusb303d_context *ctx,
	uint8_t addr,
	uint8_t value)
{
	struct i2c_msg msg;
	uint8_t buf[2] = { 0 };
	int rv;

	buf[0] = addr;
	buf[1] = value;
	msg.addr = ctx->i2c->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	rv = i2c_transfer(ctx->i2c->adapter, &msg, 1);
	if (rv < 0) {
		dev_err(fusb303d_dev(ctx), "%s: Write failed. "
			"addr=0x%.2x, value=0x%.2x, rv=%d\n",
			__func__, addr, value, rv
		);
		return rv;
	}

	return 0;
}

/* Read FUSB303D register with lock.
 */
static int fusb303d_reg_read(struct fusb303d_context *ctx,
	uint8_t addr,
	uint8_t *value)
{
	int ret = 0;

	mutex_lock(&ctx->mutex_chip);
	ret = fusb303d_reg_read_unlocked(ctx, addr, value);
	mutex_unlock(&ctx->mutex_chip);
	return ret;
}

/* Write FUSB303D register with lock.
 */
static int __maybe_unused fusb303d_reg_write(struct fusb303d_context *ctx,
	uint8_t addr,
	uint8_t value)
{
	int ret = 0;

	mutex_lock(&ctx->mutex_chip);
	ret = fusb303d_reg_write_unlocked(ctx, addr, value);
	mutex_unlock(&ctx->mutex_chip);
	return ret;
}

static int fusb303d_reg_modify_unlocked(struct fusb303d_context *ctx,
	uint8_t addr,
	uint8_t mask,
	uint8_t value)
{
	uint8_t tmp = 0;
	int rv = 0;

	rv = fusb303d_reg_read_unlocked(ctx, addr, &tmp);
	if (rv != 0) {
		goto out;
	}
	tmp = (tmp & ~mask) | (value & mask);
	rv = fusb303d_reg_write_unlocked(ctx, addr, tmp);
out:
	return rv;
}

static __maybe_unused int fusb303d_reg_modify(struct fusb303d_context *ctx,
	uint8_t addr,
	uint8_t mask,
	uint8_t value)
{	int	ret;

	mutex_lock(&ctx->mutex_chip);
	ret = fusb303d_reg_modify_unlocked(ctx, addr, mask, value);
	mutex_unlock(&ctx->mutex_chip);
	return ret;
}


static int fusb303d_status_read_unlocked(struct fusb303d_context *ctx)
{
	int ret;
	int result;

	result = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_STATUS,
		&(ctx->status_save)
	);

	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_STATUS1,
		&(ctx->status1_save)
	);
	if (ret != 0)
		result = ret;

	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_TYPE,
		&(ctx->type_save)
	);
	if (ret != 0)
		result = ret;

	return result;
}

static bool fusb303d_status_vbusok(struct fusb303d_context *ctx)
{	return (ctx->status_save) & FUSB303D_VBUSOK;
}

static bool fusb303d_status_attach(struct fusb303d_context *ctx)
{	return (ctx->status_save) & FUSB303D_ATTACH;
}

/*! Is state "DebugAccessory.SRC" */
static bool fusb303d_type_debugsrc(struct fusb303d_context *ctx)
{	return (ctx->type_save) & FUSB303D_DEBUGSRC;
}

/*! Is state "DebugAccessory.SNK" */
static bool fusb303d_type_debugsnk(struct fusb303d_context *ctx)
{	return (ctx->type_save) & FUSB303D_DEBUGSNK;
}

/*! Is state "Attached.SNK" */
static bool fusb303d_type_sink(struct fusb303d_context *ctx)
{	return (ctx->type_save) & FUSB303D_SINK;
}

/*! Is state "Attached.SRC" */
static bool fusb303d_type_source(struct fusb303d_context *ctx)
{	return (ctx->type_save) & FUSB303D_SOURCE;
}

/*! Is state AudioAccessory with VBUS */
static bool fusb303d_type_audiovbus(struct fusb303d_context *ctx)
{	return (ctx->type_save) & FUSB303D_AUDIOVBUS;
}

/*! Is state AudioAccessory without VBUS */
static bool fusb303d_type_audio(struct fusb303d_context *ctx)
{	return (ctx->type_save) & FUSB303D_AUDIO;
}

/*! Is CC unattached */
static bool fusb303d_status_unattached(struct fusb303d_context *ctx)
{	return ((ctx->status_save & FUSB303D_BC_LVL_MASK) ==
		FUSB303D_BC_LVL_RA_NONE
	);
}

static void fusb303d_interrupt_show(struct fusb303d_context *ctx)
{
	static const char blank[] = "---.";
	uint8_t	i0;
	uint8_t	i1;

	i0 = ctx->interrupt_save;
	i1 = ctx->interrupt1_save;

	dev_info(fusb303d_dev(ctx), "i0:i1=0x%.2x:0x%.2x"
		"(%s%s%s%s%s%s%s"
		  "%s%s%s%s%s%s)",
		i0, i1,
/*b6*/		((i0 & FUSB303D_I_ORIENT)   ? "ORIE" : blank),
/*b5*/		((i0 & FUSB303D_I_FAULT)    ? "FALT" : blank),
/*b4*/		((i0 & FUSB303D_I_VBUS_CHG) ? "VBCH" : blank),
/*b3*/		((i0 & FUSB303D_I_AUTOSNK)  ? "ASNK" : blank),
/*b2*/		((i0 & FUSB303D_I_BC_LVL)   ? "BCLV" : blank),
/*b1*/		((i0 & FUSB303D_I_DETACH)   ? "DTCH" : blank),
/*b0*/		((i0 & FUSB303D_I_ATTACH)   ? "ATCH" : blank),
/*b6*/		((i1 & FUSB303D_I_REM_VBOFF) ? "RVOF" : blank),
/*b5*/		((i1 & FUSB303D_I_REM_VBON)  ? "RVON" : blank),
/*b3*/		((i1 & FUSB303D_I_REM_FAIL)  ? "RFAL" : blank),
/*b2*/		((i1 & FUSB303D_I_FRC_FAIL)  ? "FFAL" : blank),
/*b1*/		((i1 & FUSB303D_I_FRC_SUCC)  ? "FSUC" : blank),
/*b0*/		((i1 & FUSB303D_I_REMEDY)    ? "RMED" : blank)
	);
}

static int fusb303d_source_current_read_unlocked(struct fusb303d_context *ctx,
	long *source_current)
{
	uint8_t	rval;
	int	ret;

	*source_current = 0;

	rval = 0;
	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_CONTROL, &rval);
	if (ret != 0)
		return ret;

	switch (rval & FUSB303D_HOST_CUR_MASK) {

	case FUSB303D_HOST_CUR_LEGACY:
		*source_current = TYPEC_ICX_CURRENT_500MA;
		break;

	case FUSB303D_HOST_CUR_1500MA:
		*source_current = TYPEC_ICX_CURRENT_1500MA;
		break;

	case FUSB303D_HOST_CUR_3000MA:
		*source_current = TYPEC_ICX_CURRENT_3000MA;
		break;

	default:
		dev_err(fusb303d_dev(ctx), "Reserved value. rval=0x%.2x\n",
			rval
		);
		return -EPROTO;
	}

	return 0;
}

static int fusb303d_source_current_read(struct fusb303d_context *ctx,
	long *source_current)
{	int	ret;

	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_source_current_read_unlocked(ctx, source_current);
	mutex_unlock(&(ctx->mutex_chip));
	return ret;
}

static uint8_t fusb303d_source_current_to_reg(long source_current)
{	uint8_t	wval;

	/* Range [0mA, MAX, round to 3000mA */
	wval = FUSB303D_HOST_CUR_3000MA;

	if (source_current < TYPEC_ICX_CURRENT_3000MA) {
		/* Range [0mA, 3000mA), round to 1500mA */
		wval = FUSB303D_HOST_CUR_1500MA;
	}

	if (source_current < TYPEC_ICX_CURRENT_1500MA) {
		/* Range [0mA, 1500mA), round to 500mA */
		wval = FUSB303D_HOST_CUR_LEGACY;
	}
	return wval;
}

static int fusb303d_source_current_write_unlocked(struct fusb303d_context *ctx,
	long source_current)
{
	uint8_t	wval;
	int	ret;

	if (source_current < 0) {
		/* Do nothing. */
		return 0;
	}

	wval = fusb303d_source_current_to_reg(source_current);

	ret = fusb303d_reg_modify_unlocked(ctx, FUSB303D_REG_CONTROL,
		FUSB303D_HOST_CUR_MASK, wval);
	if (ret < 0) {
		dev_err(fusb303d_dev(ctx), "Can not set HOST_CUR. "
			"source_current=%ld, wval=0x%.2x(masked)\n",
			source_current, wval
		);
	}
	return ret;
}

static int fusb303d_source_current_write(struct fusb303d_context *ctx,
	long source_current)
{	int	ret;

	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_source_current_write_unlocked(ctx, source_current);
	mutex_unlock(&(ctx->mutex_chip));
	return ret;
}

static int fusb303d_port_role_read_unlocked(struct fusb303d_context *ctx,
	int *role)
{
	uint8_t	rval;
	uint8_t	mval;

	int	ret;

	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_PORTROLE, &rval);

	if (ret != 0)
		return ret;

	mval = rval & FUSB303D_TRYDRPSNKSRC_MASK;
	if (    (mval & FUSB303D_TRY_MASK)
	     == (FUSB303D_TRY_SRC | FUSB303D_TRY_SNK)
	) {
		dev_notice(fusb303d_dev(ctx),
			"PORTROLE.TRY is configured with strange value."
			"rval=0x%.2x\n",
			rval
		);
		/* Fix to TRY=Disable(Normal DRP). */
		mval &= ~FUSB303D_TRY_MASK;
	}

	switch (mval & (FUSB303D_DRP | FUSB303D_SNK | FUSB303D_SRC)) {
	case FUSB303D_DRP:
	case FUSB303D_SNK:
	case FUSB303D_SRC:
		/* Normal value. */
		/* Do nothing. */
		break;
	case 0x00:
		/* No role. */
		dev_notice(fusb303d_dev(ctx),
			"PORTROLE is configured with strange value. "
			"rval=0x%.2x\n",
			rval
		);
		break;
	default:
		if (mval & FUSB303D_DRP) {
			/* first priority DRP */
			dev_notice(fusb303d_dev(ctx),
				"PORTROLE is configured with strange value. "
				"rval=0x%.2x\n",
				rval
			);
			/* Fix to DRP. */
			mval &= ~(FUSB303D_SNK | FUSB303D_SRC);
		}

		if (mval & FUSB303D_SNK) {
			/* second priority SNK */
			dev_notice(fusb303d_dev(ctx),
				"PORTROLE is configured with strange value. "
				"rval=0x%.2x\n",
				rval
			);
			/* Fix to SNK */
			mval &= ~(FUSB303D_SRC);
		}
	}

	switch (mval & (FUSB303D_TRYDRPSNKSRC_MASK)) {
	case FUSB303D_SNK:
		*role = TYPEC_ICX_PORT_ROLE_SINK;
		break;
	case FUSB303D_SRC:
		*role = TYPEC_ICX_PORT_ROLE_SOURCE;
		break;
	case FUSB303D_DRP:
		*role = TYPEC_ICX_PORT_ROLE_DRP;
		break;
	case FUSB303D_DRP | FUSB303D_TRY_SNK:
		*role = TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK;
		break;
	case FUSB303D_DRP | FUSB303D_TRY_SRC:
		*role = TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC;
		break;
	default:
		*role = TYPEC_ICX_PORT_ROLE_UNKNOWN;
		break;
	}
	return ret;
}

static int fusb303d_port_role_read(struct fusb303d_context *ctx,
	int *role)
{	int	ret;

	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_port_role_read_unlocked(ctx, role);
	mutex_unlock(&(ctx->mutex_chip));
	return ret;
}

static int fusb303d_port_role_write_unlocked(struct fusb303d_context *ctx,
	int role)
{
	uint8_t	wval;
	int	ret;

	if (role < 0) {
		/* Do nothing. */
		return 0;
	}

	switch (role) {
	case TYPEC_ICX_PORT_ROLE_NOTHING:
		/* Do nothing. */
		return 0;

	case TYPEC_ICX_PORT_ROLE_KEEP:
		/* Do nothing. */
		return 0;

	case TYPEC_ICX_PORT_ROLE_SINK:
		wval = FUSB303D_SNK;
		ret = fusb303d_reg_modify_unlocked(ctx, FUSB303D_REG_PORTROLE,
			FUSB303D_AUDIOACC | FUSB303D_ORIENTDEB, 0);
		break;

	case TYPEC_ICX_PORT_ROLE_SOURCE:
		wval = FUSB303D_SRC;
		break;

	case TYPEC_ICX_PORT_ROLE_DRP:
		wval = FUSB303D_DRP;
		break;

	case TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK:
		wval = FUSB303D_DRP | FUSB303D_TRY_SNK;
		break;

	case TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC:
		wval = FUSB303D_DRP | FUSB303D_TRY_SRC;
		break;

	default:
		/* Fall unknown to  Dual Role Port. */
		wval = FUSB303D_DRP;
		dev_notice(fusb303d_dev(ctx), "Unknown port_role foece DRP. "
			"role=%d\n",
			role
		);
		break;
	}

	ret = fusb303d_reg_modify_unlocked(ctx, FUSB303D_REG_PORTROLE,
		FUSB303D_TRYDRPSNKSRC_MASK, wval
	);
	return ret;
}

static int fusb303d_port_role_write(struct fusb303d_context *ctx,
	int role)
{	int	ret;

	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_port_role_write_unlocked(ctx, role);
	mutex_unlock(&(ctx->mutex_chip));
	return	ret;
}

static int fusb303d_disabled_state_read_unlocked(struct fusb303d_context *ctx,
	bool *state)
{
	uint8_t	rval;
	int	ret;

	*state = true /* disabled */;
	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_MANUAL, &rval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "%s: Can not read MANUAL. "
			"ret=%d\n",
			__func__, ret
		);
		goto out;
	}
	*state = (rval & FUSB303D_MANUAL_DISABLED);
out:
	return ret;
}

static int fusb303d_disabled_state_read(struct fusb303d_context *ctx,
	bool *state)
{	int	ret;

	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_disabled_state_read_unlocked(ctx, state);
	mutex_unlock(&(ctx->mutex_chip));

	return ret;
}

static int fusb303d_disabled_state_write_unlocked(struct fusb303d_context *ctx,
	bool state)
{
	uint8_t	wval;
	int	ret;

	/* Note: state vs "USB Type-C state".
	 *       See. 4.5.2.2.1 Disabled State.
	 *
	 * state == true
	 *  Enter Type-C Disabled state.
	 *
	 * state == false
	 *  Exit  Type-C Disabled state.
	 */
	wval = (state ? FUSB303D_MANUAL_DISABLED : 0x00);

	ret = fusb303d_reg_modify_unlocked(ctx, FUSB303D_REG_MANUAL,
		FUSB303D_MANUAL_DISABLED, wval
	);
	if (ret != 0)
		dev_err(fusb303d_dev(ctx), "%s: Can not write MANUAL. "
			"state=%d, ret=%d\n",
			__func__, (int)(state), ret
		);
	return ret;
}

static int fusb303d_disabled_state_write(struct fusb303d_context *ctx,
	bool state)
{	int	ret;

	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_disabled_state_write_unlocked(ctx, state);
	mutex_unlock(&(ctx->mutex_chip));
	return ret;
}

#define	FUSB303D_TRESET_MS	(100)	/* Soft Reset Duration in ms. */

static int fusb303d_chip_initialize(struct fusb303d_context *ctx)
{
	int	ret = 0;
	u8	rval;
	u8	wval;
	u32	of_auto_sink_th_mv;
	u32	of_port_role;

	mutex_lock(&(ctx->mutex_chip));

	/* setup control register */
	if (ctx->of_do_reset == PROPERTY_TRUE) {
		/* Do reset chip. */
		ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_RESET,
			FUSB303D_RESET_SW_RES
		);
		if (ret != 0) {
			dev_err(fusb303d_dev(ctx), "%s: Can not reset chip.\n",
				__func__
			);
			goto out;
		}
		/* Wait device become ready. */
		msleep(FUSB303D_TRESET_MS);
	} else {
		/* @todo Implement reconfigure procedure. */
	}

	/* Initialize CONTROL, set read default rval as reset value. */
	rval =   0x00
		| FUSB303D_T_DRP_70MS
		| FUSB303D_DRPTOGGLE_60P
		| FUSB303D_HOST_CUR_LEGACY
		| FUSB303D_INT_MASK
	;
	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_CONTROL, &rval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "%s: Can not read CONTROL. ret=%d\n",
			__func__, ret
		);
		goto out;
	}
	wval =    0x00
		| FUSB303D_T_DRP_70MS
		| FUSB303D_DRPTOGGLE_50P
		| FUSB303D_INT_MASK
	;
	if (ctx->of_source_current >= 0) {
		/* Set source current by device-tree property */
		wval |= fusb303d_source_current_to_reg(ctx->of_source_current);
	} else {
		/* Use default or preset value. */
		wval |= rval & FUSB303D_HOST_CUR_MASK;
	}
	/* Write CONTROL register. */
	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_CONTROL, wval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "%s: Can not write CONTROL. rval=0x%.2x, wval=0x%.2x, ret=%d\n",
			__func__, rval, wval, ret
		);
		goto out;
	}

	/* Initialize CONTROL1, set read default rval as Reset value. */
	rval =   0x00
		| FUSB303D_REMEDY_EN
		| FUSB303D_AUTO_SNK_TH_3V1
		| FUSB303D_AUTO_SNK_EN
		| FUSB303D_TCCDEB_150MS
	;
	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_CONTROL1, &rval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "%s: Can not read CONTROL1. ret=%d\n",
			__func__, ret
		);
		goto out;
	}
	of_auto_sink_th_mv = ctx->of_auto_sink_th_mv;
	wval =    0x00
		| fusb303d_auto_sink_th_mv_to_reg_th(of_auto_sink_th_mv)
		| fusb303d_auto_sink_th_mv_to_reg_en(of_auto_sink_th_mv)
		| (rval & FUSB303D_TCCDEB_MASK) /* Keep TCCDEB */
	;
	/* write CONTROL1 register, Do not enable here. */
	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_CONTROL1, wval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "%s: Can not write CONTROL1. "
			"rval=0x%.2x, wval=0x%.2x, ret=%d\n",
			__func__, rval, wval, ret
		);
		goto out;
	}

	of_port_role = ctx->of_port_role;
	if (ctx->of_sink_only == FUSB303D_OF_SINK_ONLY_YES) {
		/* Sink Only mode. */
		of_port_role = TYPEC_ICX_PORT_ROLE_SINK;
	}
	ret = fusb303d_port_role_write_unlocked(ctx, of_port_role);
	if (ret != 0) {
		/* Fail to setup port role. */
		goto out;
	}

	/* Enable chip */
	wval = FUSB303D_ENABLE;
	ret = fusb303d_reg_modify_unlocked(ctx, FUSB303D_REG_CONTROL1,
		FUSB303D_ENABLE, wval
	);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "%s: Can not write CONTROL1.ENABLE. "
			"wval=0x%.2x, ret=%d\n",
			__func__, wval, ret
		);
		goto out;
	}

out:
	mutex_unlock(&(ctx->mutex_chip));
	return ret;
}

static int fusb303d_chip_interrupt_enable(struct fusb303d_context *ctx)
{
	int	ret = 0;
	uint8_t	rval;
	uint8_t	wval;

	mutex_lock(&(ctx->mutex_chip));

	/* setup mask register */
	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_MASK, 0);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not enable INTERRUPT.\n");
		goto out;
	}

	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_MASK1, 0);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not enable INTERRUPT1.\n");
		goto out;
	}

	/* Unmask Global interrupt gate */
	rval = 0x00
		| FUSB303D_T_DRP_70MS
		| FUSB303D_DRPTOGGLE_50P
		| FUSB303D_HOST_CUR_LEGACY
	;
	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_CONTROL, &rval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not read CONTROL. "
			"ret=%d\n",
			ret
		);
		/* Anyway we continue. */
	}

	wval = rval & ~(FUSB303D_INT_MASK | FUSB303D_DCABLE_EN);

	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_CONTROL, wval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not write CONTROL. "
			"rval=0x%.2x, wval=0x%.2x\n",
			rval, wval
		);
		goto out;
	}
out:
	mutex_unlock(&(ctx->mutex_chip));
	return ret;
}

static int fusb303d_chip_interrupt_disable(struct fusb303d_context *ctx)
{
	int	ret = 0;
	int	result = 0;
	uint8_t	rval;
	uint8_t	wval;

	mutex_lock(&(ctx->mutex_chip));

	/* Mask Global interrupt gate */
	rval = 0x00
		| FUSB303D_T_DRP_70MS
		| FUSB303D_DRPTOGGLE_50P
		| FUSB303D_HOST_CUR_LEGACY
		| FUSB303D_INT_MASK
	;
	ret = fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_CONTROL, &rval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not read CONTROL. "
			"ret=%d\n",
			ret
		);
		result = ret;
		/* Anyway we continue. */
	}

	wval = rval | FUSB303D_INT_MASK;

	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_CONTROL, wval);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not write CONTROL. "
			"rval=0x%.2x, wval=0x%.2x\n",
			rval, wval
		);
		result = ret;
		/* Anyway we continue. */
	}

	/* setup mask register */
	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_MASK, 0xff);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not disable INTERRUPT.\n");
		result = ret;
		/* Anyway we continue. */
	}

	ret = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_MASK1, 0xff);
	if (ret != 0) {
		dev_err(fusb303d_dev(ctx), "Can not disable INTERRUPT1.\n");
		result = ret;
		/* Anyway we continue. */
	}

	mutex_unlock(&(ctx->mutex_chip));
	return result;
}

static __maybe_unused void fusb303d_irq_enable(struct fusb303d_context *ctx)
{	enable_irq(ctx->i2c->irq);
}

static __maybe_unused void fusb303d_irq_disable(struct fusb303d_context *ctx)
{	disable_irq(ctx->i2c->irq);
}


static long fusb303d_sink_current(struct fusb303d_context *ctx)
{
	if (!fusb303d_status_attach(ctx))
		return TYPEC_ICX_CURRENT_0MA;

	if (fusb303d_type_audio(ctx))
		return TYPEC_ICX_CURRENT_0MA;

	if (fusb303d_type_audiovbus(ctx))
		return TYPEC_ICX_CURRENT_500MA;

	if (   (fusb303d_type_source(ctx))
	    || (fusb303d_type_debugsrc(ctx))
	)
		return TYPEC_ICX_CURRENT_0MA;

	if (   (!fusb303d_type_sink(ctx))
	    && (!fusb303d_type_debugsnk(ctx))
	)
		return TYPEC_ICX_CURRENT_0MA;

	switch (ctx->status_save & FUSB303D_BC_LVL_MASK) {
	case FUSB303D_BC_LVL_RA_NONE:
		return TYPEC_ICX_CURRENT_0MA;

	case FUSB303D_BC_LVL_RD_LEGACY:
		return TYPEC_ICX_CURRENT_500MA;

	case FUSB303D_BC_LVL_RD_1500MA:
		return TYPEC_ICX_CURRENT_1500MA;

	case FUSB303D_BC_LVL_RD_3000MA:
		return TYPEC_ICX_CURRENT_3000MA;

	default:
		/* Do nothing, may not come here. */
		break;
	}

	return TYPEC_ICX_CURRENT_0MA;
}

static int fusb303d_connected_to(struct fusb303d_context *ctx)
{
	if (!fusb303d_status_attach(ctx))
		return TYPEC_ICX_CONNECTED_TO_NONE;

	if (fusb303d_type_sink(ctx))
		return TYPEC_ICX_CONNECTED_TO_SOURCE;

	if (fusb303d_type_source(ctx))
		return TYPEC_ICX_CONNECTED_TO_SINK;

	if (fusb303d_type_debugsnk(ctx))
		return TYPEC_ICX_CONNECTED_TO_SOURCE_DEBUG;

	if (fusb303d_type_debugsrc(ctx))
		return TYPEC_ICX_CONNECTED_TO_SINK_DEBUG;

	if (fusb303d_type_audiovbus(ctx))
		return TYPEC_ICX_CONNECTED_TO_AUDIO_VBUS;

	if (fusb303d_type_audio(ctx))
		return TYPEC_ICX_CONNECTED_TO_AUDIO;

	return TYPEC_ICX_CONNECTED_TO_UNKNOWN;
}

/*! Notify USB Host event.
 *  @todo Implement USB otg controller and chage/boost battery driver.
 *  @ctx   points driver context
 *  @state true: Event connect as USB host, false: Disconnect.
 */
static void fusb303d_event_usb_host(struct fusb303d_context *ctx,
	bool state)
{	int	ret;

	mutex_lock(&(ctx->mutex_extcon));
	if (ctx->usb_host != ((int)(state))) {
		/* Different from privious state. */
		dev_info(fusb303d_dev(ctx), "Event EXTCON_USB_HOST. state=%d\n",
			(int)(state)
		);
		ctx->usb_host = state;
		ret = extcon_set_state_sync(ctx->edev,
			EXTCON_USB_HOST, state
		);
		if (ret != 0)
			dev_warn(fusb303d_dev(ctx), "Fail to sync USB_HOST. ret=%d\n",
				ret
			);

		/* Switch to other threads and process. */
		if (state) {
			/* Start USB host role. */
			schedule();
		} else {
			/* End USB host role. */
			if (ctx->of_detach_sleep_time_ms == 0) {
				/* No sleep time. */
				schedule();
			} else {
				/* Sleep a while. */
				msleep(ctx->of_detach_sleep_time_ms);
			}
		}
	}
	mutex_unlock(&(ctx->mutex_extcon));
}

/*! Notify USB Device event.
 *  @ctx   points driver context
 *  @state true: Event connect as USB device, false: Disconnect.
 */
static void fusb303d_event_usb_device(struct fusb303d_context *ctx,
	bool state)
{	long				sink_current;
	enum bq25898_icx_pd_properties	pd;
	union  extcon_property_value	pd_prop;
	int				ret;

	mutex_lock(&(ctx->mutex_extcon));
	if (ctx->usb_device != ((int)(state))) {
		/* Different from privious state. */
		dev_info(fusb303d_dev(ctx), "Event EXTCON_USB. state=%d\n",
			(int)(state)
		);
		ctx->usb_device = state;
		ret = extcon_set_state_sync(ctx->edev, EXTCON_USB, state);
		if (ret != 0)
			dev_warn(fusb303d_dev(ctx),
				"Fail to sync EXTCON_USB. ret=%d\n",
				ret
			);

		/* Switch to other threads and process. */
		if (state) {
			/* Start USB device role. */
			schedule();
		} else {
			/* End USB device role. */
			if (ctx->of_detach_sleep_time_ms == 0) {
				/* No sleep time. */
				schedule();
			} else {
				/* Sleep a while. */
				msleep(ctx->of_detach_sleep_time_ms);
			}
		}
	}
	sink_current = fusb303d_sink_current(ctx);
	pd = fusb303d_current_bq25898_icx_pd_properties(sink_current);
	pd_prop.intval = pd;
	if ((ctx->chg_usb_pd != state) || (ctx->prop_chg_min != pd)) {
		dev_info(fusb303d_dev(ctx), "Event EXTCON_CHG_USB_PD. state=%d, pd=%d\n",
			(int)(state), (int)pd
		);
		ret = extcon_set_state(ctx->edev, EXTCON_CHG_USB_PD, state);
		if (ret != 0) {
			/* Fail to set state. */
			dev_warn(fusb303d_dev(ctx),
				"Fail to set state CHG_USB_PD. ret=%d, state=%d\n",
				ret, (int)state
			);
		}
		ret = extcon_set_property_sync(ctx->edev, EXTCON_CHG_USB_PD,
			EXTCON_PROP_CHG_MIN, pd_prop
		);
		if (ret != 0) {
			/* Fail to set property. */
			dev_warn(fusb303d_dev(ctx),
				"Fail to set property PROP_CHG_MIN. ret=%d\n",
				ret
			);
		}
		ctx->chg_usb_pd = state;
		ctx->prop_chg_min = pd;
	}
	mutex_unlock(&(ctx->mutex_extcon));
}

static void fusb303d_typec_notify_host(struct fusb303d_context *ctx)
{
	long			source_current;
	enum typec_pwr_opmode	pwr;

	typec_set_pwr_role(ctx->port,  TYPEC_SOURCE);
	source_current = 0;
	(void) fusb303d_source_current_read(ctx, &source_current);
	/* Ignore Error, treat as Standard Host Source(500mA). */
	pwr = fusb303d_current_to_typec_pwr_opmode(source_current);
	typec_set_pwr_opmode(ctx->port, pwr);
	typec_set_data_role(ctx->port, TYPEC_HOST);
}

static void fusb303d_typec_notify_device(struct fusb303d_context *ctx)
{
	long			sink_current;
	enum typec_pwr_opmode	pwr;

	typec_set_pwr_role(ctx->port,  TYPEC_SINK);
	sink_current = fusb303d_sink_current(ctx);
	pwr = fusb303d_current_to_typec_pwr_opmode(sink_current);
	typec_set_data_role(ctx->port, TYPEC_DEVICE);
	typec_set_pwr_opmode(ctx->port, pwr);
}

static void fusb303d_typec_notify_detach(struct fusb303d_context *ctx)
{	int	current_role;
	int	typec_role;
	int	ret;

	/* Detached */
	current_role = TYPEC_ICX_PORT_ROLE_UNKNOWN;
	ret = fusb303d_port_role_read(ctx, &current_role);
	if (ret)
		current_role = TYPEC_ICX_PORT_ROLE_DRP;

	typec_role = fusb303d_role_to_typec_role(current_role);
	switch (typec_role) {
	case TYPEC_SINK:
		fusb303d_typec_notify_device(ctx);
		break;
	case TYPEC_SOURCE:
		fusb303d_typec_notify_host(ctx);
		break;
	case TYPEC_NO_PREFERRED_ROLE:
		/* Keep Current role. */
		break;
	}
}

/*! Action at "IRQ asserted" or "status change" or "First status check"
 *  @ctx    points driver context
 *  @at_irq true: Action at Interrupt, false: Action at Probe
 */
static void fusb303d_irq_action(struct fusb303d_context *ctx, bool at_irq)
{
	bool	detach;
	bool	attach;
	struct typec_partner	*partner;

	mutex_lock(&(ctx->mutex_irq_action));

	if (at_irq) {
		/* Action at IRQ. */
		if (ctx->interrupt_save & FUSB303D_I_BC_LVL) {
			ctx->interrupt_save &= ~FUSB303D_I_BC_LVL;
			fusb303d_event_usb_device(ctx, true);
		}

		detach = (ctx->interrupt_save &
				(FUSB303D_I_DETACH));
		attach = (ctx->interrupt_save & FUSB303D_I_ATTACH);
		/* Do detach event first. */
		if (detach) {
			/* Detach Event. */
			partner = ctx->partner;
			if (partner) {
				/* Registered partner. */
				typec_unregister_partner(partner);
				ctx->partner = NULL;
			} else {
				dev_notice(fusb303d_dev(ctx),
					"Consecutive detach event.\n"
				);
			}
			fusb303d_event_usb_host(ctx, false);
			fusb303d_event_usb_device(ctx, false);
			fusb303d_typec_notify_detach(ctx);
		}

		if (attach && detach) {
			/* Interrupted by Attach and Detach */
			/* Get current status. */
			mutex_lock(&(ctx->mutex_chip));
			fusb303d_status_read_unlocked(ctx);
			mutex_unlock(&(ctx->mutex_chip));
		}
	} else {
		/* Action at Probe. */
		detach = false;
		mutex_lock(&(ctx->mutex_chip));
		fusb303d_status_read_unlocked(ctx);
		mutex_unlock(&(ctx->mutex_chip));
	}

	/* Attached now?  */
	attach = fusb303d_status_attach(ctx);

	if (attach) {
		/* "Attach Event" or "Keep attaching" */
		bool	type_handled = false;

		if (   fusb303d_type_sink(ctx)
		    || fusb303d_type_debugsnk(ctx)
		) {
			/* Attached.SNK. or DebugAccessory.SNK
			 * FUSB303D is attached/attaching as a Sink,
			 */
			/* cable is Source. */
			if (fusb303d_status_vbusok(ctx) &&
			   (!fusb303d_status_unattached(ctx))
			) {
				/* "Powered VBUS" and "CC Attached" */
				if (!imx_lpa_is_enabled()) {
					/* Not LPA mode, normal log output. */
					uint8_t		val;
					fusb303d_reg_read(ctx, FUSB303D_REG_PORTROLE, &val);
					dev_info(fusb303d_dev(ctx),
						"Attached.SNK. status_save=0x%.2x portrole= 0x%02x\n",
						ctx->status_save, val
					);
				}
				fusb303d_event_usb_device(ctx, true);
				fusb303d_typec_notify_device(ctx);
				if (!at_irq && ctx->of_attach_wake_lock_ms) {
					/* Wake lock a while,
					 * when attached.
					 */
					long	timeout;

					timeout = msecs_to_jiffies(
						ctx->of_attach_wake_lock_ms);
					wake_lock_timeout(&(ctx->wake_lock),
						timeout);
				}
				type_handled = true;
			}
		}

		if (   fusb303d_type_source(ctx)
		    || fusb303d_type_debugsrc(ctx)
		) {
			/* Attached.SRC or DebugAccessory.SRC 
			 * FUSB303D is attached/attaching as a Source,
			 */
			/* cable is Sink. */
			dev_info(fusb303d_dev(ctx),
				"Attached.SRC. "
				"status_save=0x%.2x\n",
				ctx->status_save
			);
			fusb303d_event_usb_host(ctx, true);
			fusb303d_typec_notify_host(ctx);
			if (!at_irq && ctx->of_attach_wake_lock_ms) {
				/* Wake lock a while,
				 * when attached.
				 */
				long	timeout;

				timeout = msecs_to_jiffies(
					ctx->of_attach_wake_lock_ms);
				wake_lock_timeout(&(ctx->wake_lock),
					timeout);
			}
			type_handled = true;
		}

		if (!type_handled) {
			dev_info(fusb303d_dev(ctx),
				"No reaction to connected cable. "
				"status_save=0x%.2x, "
				"TYPE=0x%.2x\n",
				ctx->status_save,
				ctx->type_save
			);
		}

		if (fusb303d_connected_to_typec_accessory_refrect(ctx))
			dev_warn(fusb303d_dev(ctx),
				"Unknown partner. status_save=0x%.2x, TYPE=0x%.2x\n",
				ctx->status_save,
				ctx->type_save
			);

		if (ctx->partner == NULL)
			ctx->partner = typec_register_partner(ctx->port,
				&(ctx->typec_partner));

		if (ctx->partner == NULL)
			dev_warn(fusb303d_dev(ctx),
				"Can not register partner.\n"
			);
	}

	mutex_unlock(&(ctx->mutex_irq_action));
}

/* Interrupt Handler on thread
 * @irq    IRQ number, ignore.
 * @dev_id Points structure fusb303_context.
 * @return int==IRQ_HANDLED: Interrupt is handled. \
 *         int==IRQ_NONE: Interrupted but it may caused by other device.
 */
static irqreturn_t fusb303d_irq_handler(int irq, void *dev_id)
{
	struct fusb303d_context *ctx = dev_id;
	int	rv;

	fusb303d_get(ctx);
	mutex_lock(&(ctx->mutex_chip));

	fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_INTERRUPT, 
		&(ctx->interrupt_save)
	);
	fusb303d_reg_read_unlocked(ctx, FUSB303D_REG_INTERRUPT1,
		&(ctx->interrupt1_save)
	);
	fusb303d_status_read_unlocked(ctx);

	if (   (((ctx->interrupt_save) &  FUSB303D_I_ATT_ALL) == 0)
	    && (((ctx->interrupt1_save) & FUSB303D_I_REM_FRC_ALL) == 0)
	) {
		/* Not me. */
		dev_err(fusb303d_dev(ctx), "May IRQ shared?\n"
		);
		fusb303d_interrupt_show(ctx);
		mutex_unlock(&(ctx->mutex_chip));
		fusb303d_put(ctx);
		return IRQ_NONE;
	}

	if (ctx->irq_debug)
		fusb303d_interrupt_show(ctx);

	rv = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_INTERRUPT,
		ctx->interrupt_save
	);
	if (rv != 0) {
		dev_err(fusb303d_dev(ctx), "Can not clear INTERRUPT.\n");
		fusb303d_interrupt_show(ctx);
		/* Anyway we continue. */
	}
	rv = fusb303d_reg_write_unlocked(ctx, FUSB303D_REG_INTERRUPT1,
		ctx->interrupt1_save
	);
	if (rv != 0) {
		dev_err(fusb303d_dev(ctx), "Can not clear INTERRUPT1.\n");
		fusb303d_interrupt_show(ctx);
		/* Anyway we continue. */
	}
	mutex_unlock(&(ctx->mutex_chip));

	fusb303d_irq_action(ctx, true);
	fusb303d_put(ctx);

	return IRQ_HANDLED;
}


/*
 ***************************
 * @ driver_access_routines
 ***************************
 */


/* Show USB Type-C Disabled State
 */
static ssize_t fusb303d_disabled_state_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t result = -EIO;
	bool	state;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	state = false;
	ret = fusb303d_disabled_state_read(ctx, &state);
	if (ret < 0) {
		result = (ssize_t)ret;
		goto out;
	}

	result = snprintf(buf, PAGE_SIZE, "%d\n", (int)(state));
out:
	fusb303d_put(ctx);
	return result;
}

/* Store USB Type-C Disabled State
 */
static ssize_t fusb303d_disabled_state_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context *ctx;
	int	ret;
	ssize_t result = -EIO;
	long	state;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. "
			"size=%lu. result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	state = 0;
	ret = kstrtol(buf, 0, &state);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(fusb303d_dev(ctx), "Invalid value. "
			"result=%ld\n",
			(long)result
		);
		goto out;
	}

	if ((state != 0) && (state != 1)) {
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Value should be 0 or 1. "
			"result=%ld\n",
			result
		);
		goto out;
	}

	ret = fusb303d_disabled_state_write(ctx, (bool)state);
	if (ret < 0) {
		result = (ssize_t)ret;
		goto out;
	} else {
		dev_info(fusb303d_dev(ctx), "Set disabled state. "
			"state=%ld\n",
			state
		);
	}

	/* force cast */ /* Consume all written datas. */
	result = (__force ssize_t)size;

out:
	fusb303d_put(ctx);
	return result;
}

/*! Device Attribute disable_state
 *  Mode:
 *   0644
 *  Description:
 *   Enter or Exit "Disabled State".
 *   See USB Type-C Cable and Connector Specification
 *   4.5.2.2.1 Disabled State.
 *  Usage:
 *   echo 0 > disabled_state # Exit Disabled State, normal operation.
 *   echo 1 > disabled_state # Enter Disabled State.
 *   cat disabled_state # Print current disabled_state value.
 */
static DEVICE_ATTR(disabled_state, S_IWUSR | S_IRUGO,
	fusb303d_disabled_state_show,
	fusb303d_disabled_state_store
	);

static const char *typec_icx_port_role_text[TYPEC_ICX_PORT_ROLE_NUMS + 1] = {
	[TYPEC_ICX_PORT_ROLE_KEEP] =		"keep",
	[TYPEC_ICX_PORT_ROLE_SINK] =		"sink",
	[TYPEC_ICX_PORT_ROLE_SOURCE] =		"source",
	[TYPEC_ICX_PORT_ROLE_DRP] =		"drp",
	[TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK] =	"drp-try.snk",
	[TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC] =	"drp-try.src",
	[TYPEC_ICX_PORT_ROLE_UNKNOWN] =		"unknown",
	[TYPEC_ICX_PORT_ROLE_NUMS] = 		NULL
};

/* Show USB port_role setting.
 */
static ssize_t fusb303d_port_role_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t		result = -EIO;
	int		role;
	int		ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	ret = fusb303d_port_role_read(ctx, &role);
	if (ret != 0) {
		result = ret;
		goto out;
	}

	if ((role < 0) || (role >= TYPEC_ICX_PORT_ROLE_NUMS)) {
		result = -EIO;
		dev_err(fusb303d_dev(ctx),
			"%s: Internal error, out of range role. "
			"role=%d, "
			"result=%ld\n",
			__func__, role, (long)result
		);
		goto out;
	}

	result = snprintf(buf, PAGE_SIZE, "%s\n",
		typec_icx_port_role_text[role]
	);
out:
	fusb303d_put(ctx);
	return result;
}

/*! Store USB port_role setting.
 */
static ssize_t fusb303d_port_role_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context	*ctx;
	ssize_t			result = -EIO;
	int			role;
	char			tmp[32];
	int			ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. "
			"size=%lu. result=%ld\n",
			(unsigned long)size, (ssize_t)result
		);
		goto out;
	}

	if (!buf) {
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Null points buf.\n");
		goto out;
	}
	str_word_trim_cpy(tmp, sizeof(tmp), buf, size);
	role = str_match_to(typec_icx_port_role_text, tmp);
	if ((role < 0) || (role >= TYPEC_ICX_PORT_ROLE_UNKNOWN)) {
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Unknown or invalid role. "
			"tmp=%s, result=%ld\n",
			tmp, (ssize_t)result
		);
		goto out;
	}
	ret = fusb303d_port_role_write(ctx, role);
	if (ret < 0) {
		result = (ssize_t)ret;
		goto out;
	}
	dev_info(fusb303d_dev(ctx), "Write role. "
		"tmp=%s\n",
		tmp
	);
	/* force cast */ /* Consume all written datas. */
	result = (__force ssize_t)size;
out:
	fusb303d_put(ctx);
	return result;
}

static DEVICE_ATTR(port_role, S_IWUSR | S_IRUGO,
	fusb303d_port_role_show,
	fusb303d_port_role_store
	);

/* Show Source Current (in Source role)
 */
static ssize_t fusb303d_source_current_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t result = -EIO;
	long	source_current;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	source_current = 0;
	ret = fusb303d_source_current_read(ctx, &source_current);
	if (ret != 0) {
		result = (ssize_t)ret;
		goto out;
	}

	result = snprintf(buf, PAGE_SIZE, "%ld\n", source_current);
out:
	fusb303d_put(ctx);
	return result;
}

/* Store Source Current (in Source role)
 */
static ssize_t fusb303d_source_current_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context *ctx;
	ssize_t	result = -EIO;
	long	source_current;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. "
			"size=%lu. result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	source_current = 0;
	ret = kstrtol(buf, 0, &source_current);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(fusb303d_dev(ctx), "Invalid value. "
			"result=%ld\n",
			(long)result
		);
		goto out;
	}

	ret = fusb303d_source_current_write(ctx, source_current);
	if (ret < 0) {
		result = ret;
		goto out;
	} else {
		dev_info(fusb303d_dev(ctx), "Set source current. "
			"source_current=%ld\n",
			source_current
		);
	}

	/* force cast, consume all written datas. */
	result = (__force ssize_t)size;
out:
	fusb303d_put(ctx);
	return result;
}

/* device attibute source_current
 * Mode:
 *  0644
 * Description:
 *  VBUS Source current draw from port to attachment.
 *  i.e. pull-up resistor value or current-source parameter
 *  to CC signal in Source Role.
 * Usage:
 *  echo current_in_mA > source_current # Set VBUS Source current in mA
 *  cat source_current # Get VBUS Source current in mA.
 *
 */
static DEVICE_ATTR(source_current, S_IWUSR | S_IRUGO,
	fusb303d_source_current_show,
	fusb303d_source_current_store
	);

static const char *typec_connect_to_text[TYPEC_ICX_CONNECTED_TO_NUMS] = {
	[TYPEC_ICX_CONNECTED_TO_NONE] =		"none",
	[TYPEC_ICX_CONNECTED_TO_SOURCE] =	"source",
	[TYPEC_ICX_CONNECTED_TO_SINK] =		"sink",
	[TYPEC_ICX_CONNECTED_TO_SOURCE_DEBUG] =	"source_debug",
	[TYPEC_ICX_CONNECTED_TO_SINK_DEBUG] =	"sink_debug",
	[TYPEC_ICX_CONNECTED_TO_AUDIO_VBUS] =	"audio_vbus",
	[TYPEC_ICX_CONNECTED_TO_AUDIO] =	"audio",
	[TYPEC_ICX_CONNECTED_TO_UNKNOWN] =	"unknown",
};

static ssize_t fusb303d_connect_to_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t	result = -EIO;
	int	connect_to;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_status_read_unlocked(ctx);
	mutex_unlock(&(ctx->mutex_chip));

	if (ret < 0) {
		result = (ssize_t)ret;
		goto out;
	}

	connect_to = fusb303d_connected_to(ctx);
	if (connect_to < 0) {
		result = (ssize_t)connect_to;
		goto out;
	}

	if (connect_to >= TYPEC_ICX_CONNECTED_TO_NUMS) {
		result = -EIO;
		dev_err(fusb303d_dev(ctx),
			"%s: Internal error. connect_to=%d",
			__func__, connect_to
		);
		goto out;
	}
	result = snprintf(buf, PAGE_SIZE, "%s\n",
		typec_connect_to_text[connect_to]
	);
out:
	fusb303d_put(ctx);
	return result;
}

static DEVICE_ATTR(connect_to, S_IRUGO,
	fusb303d_connect_to_show,
	NULL
	);

static ssize_t fusb303d_sink_current_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t	result = -EIO;
	long	sink_current = 0;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	mutex_lock(&(ctx->mutex_chip));
	ret = fusb303d_status_read_unlocked(ctx);
	mutex_unlock(&(ctx->mutex_chip));
	if (ret != 0) {
		result = (ssize_t)ret;
		goto out_err;
	}
	sink_current = fusb303d_sink_current(ctx);
	if (ret != 0) {
		result = (ssize_t)ret;
		goto out_err;
	}
	result = snprintf(buf, PAGE_SIZE, "%ld\n", sink_current);
out_err:
	fusb303d_put(ctx);
	return result;
}

static DEVICE_ATTR(sink_current, S_IRUGO,
	fusb303d_sink_current_show,
	NULL
	);

/* Show IRQ debug configuration
 */
static ssize_t fusb303d_irq_debug_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t result;
	int	irq_debug;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	irq_debug = ctx->irq_debug;

	result = snprintf(buf, PAGE_SIZE, "%d\n", irq_debug);

	fusb303d_put(ctx);
	return result;
}

/* Store IRQ debug configuration
 */
static ssize_t fusb303d_irq_debug_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context *ctx;
	ssize_t	result;
	int	ret;
	long	irq_debug;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. size=%lu. result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	irq_debug = 0;
	ret = kstrtol(buf, 0, &irq_debug);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(fusb303d_dev(ctx), "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	ctx->irq_debug = (__force bool)irq_debug;

	/* force cast, consume all written datas. */
	result = (__force ssize_t)size;
out:
	fusb303d_put(ctx);
	return result;
}

static DEVICE_ATTR(irq_debug, S_IWUSR | S_IRUSR,
	fusb303d_irq_debug_show,
	fusb303d_irq_debug_store
	);


/* Show "Disabled" state time to be drp
 */
static ssize_t fusb303d_disabled_time_drp_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t result;
	int	dt;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);

	dt = ctx->disabled_time_drp;
	result = snprintf(buf, PAGE_SIZE, "%d\n", dt);

	fusb303d_put(ctx);
	return result;
}

/* Store "Disabled" state time to be sink
 */
static ssize_t fusb303d_disabled_time_drp_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context *ctx;
	ssize_t	result;
	long	dt;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. size=%lu. result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	dt = -1L;
	ret = kstrtol(buf, 0, &dt);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(fusb303d_dev(ctx), "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	ctx->disabled_time_drp = (__force int)dt;

	/* force cast, consume all written datas. */
	result = (__force ssize_t)size;
out:
	fusb303d_put(ctx);
	return result;
}

static DEVICE_ATTR(disabled_time_drp, S_IWUSR | S_IRUSR,
	fusb303d_disabled_time_drp_show,
	fusb303d_disabled_time_drp_store
	);

/* Show "Disabled" state time to be sink
 */
static ssize_t fusb303d_disabled_time_sink_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t result;
	int	dt;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);

	dt = ctx->disabled_time_sink;
	result = snprintf(buf, PAGE_SIZE, "%d\n", dt);

	fusb303d_put(ctx);
	return result;
}

/* Store "Disabled" state time to be sink
 */
static ssize_t fusb303d_disabled_time_sink_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context *ctx;
	ssize_t	result;
	long	dt;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. size=%lu. result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	dt = -1L;
	ret = kstrtol(buf, 0, &dt);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(fusb303d_dev(ctx), "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	ctx->disabled_time_sink = (__force int)dt;

	/* force cast, consume all written datas. */
	result = (__force ssize_t)size;
out:
	fusb303d_put(ctx);
	return result;
}

static DEVICE_ATTR(disabled_time_sink, S_IWUSR | S_IRUSR,
	fusb303d_disabled_time_sink_show,
	fusb303d_disabled_time_sink_store
	);

/* Show "Disabled" state time to be source
 */
static ssize_t fusb303d_disabled_time_source_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct fusb303d_context *ctx;
	ssize_t result;
	int	dt;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);

	dt = ctx->disabled_time_source;
	result = snprintf(buf, PAGE_SIZE, "%d\n", dt);

	fusb303d_put(ctx);
	return result;
}

/* Store "Disabled" state time to be source
 */
static ssize_t fusb303d_disabled_time_source_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct fusb303d_context *ctx;
	ssize_t	result;
	long	dt;
	int	ret;

	ctx = dev_get_drvdata(dev);

	fusb303d_get(ctx);
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(fusb303d_dev(ctx), "Too long write. size=%lu. result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	dt = -1L;
	ret = kstrtol(buf, 0, &dt);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(fusb303d_dev(ctx), "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	ctx->disabled_time_source = (__force int)dt;

	/* force cast, consume all written datas. */
	result = (__force ssize_t)size;
out:
	fusb303d_put(ctx);
	return result;
}


static DEVICE_ATTR(disabled_time_source, S_IWUSR | S_IRUSR,
	fusb303d_disabled_time_source_show,
	fusb303d_disabled_time_source_store
	);

static struct attribute *fusb303d_attributes[] = {
	&dev_attr_disabled_state.attr,
	&dev_attr_port_role.attr,
	&dev_attr_source_current.attr,
	&dev_attr_connect_to.attr,
	&dev_attr_sink_current.attr,
	&dev_attr_irq_debug.attr,
	&dev_attr_disabled_time_drp.attr,
	&dev_attr_disabled_time_sink.attr,
	&dev_attr_disabled_time_source.attr,
	NULL,
};

static const struct attribute_group fusb303d_attr_group = {
	.attrs = fusb303d_attributes,
};

/* Set port role common procedure - USB Type-C Class interface
 */
static int fusb303d_typec_dev_role_set(struct fusb303d_context *ctx,
	int dev_role)
{	int	result = 0;
	int	ret;
	int	disabled_time;

	mutex_lock(&(ctx->mutex_chip));
	switch (dev_role) {
	case TYPEC_ICX_PORT_ROLE_DRP:
	default:
		disabled_time = ctx->disabled_time_drp;
		break;

	case TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK:
		disabled_time = ctx->disabled_time_sink;
		break;

	case TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC:
		disabled_time = ctx->disabled_time_source;
		break;
	}

	if (disabled_time >= 0) {
		ret = fusb303d_disabled_state_write_unlocked(ctx, true);
		if (ret)
			result = ret;

		msleep(disabled_time);
	}


	ret = fusb303d_port_role_write_unlocked(ctx, dev_role);
	if (ret)
		result = ret;

	if (disabled_time >= 0) {
		ret = fusb303d_disabled_state_write_unlocked(ctx, false);
		if (ret)
			result = ret;
	}
	mutex_unlock(&(ctx->mutex_chip));
	return result;
}

/* Set port role - USB Type-C Class interface
 */
static int fusb303d_typec_try_role(const struct typec_capability *cap,
	int role)
{	struct fusb303d_context *ctx;
	int	dev_role;
	int	result = 0;
	int	ret;

	ctx = container_of(cap, struct fusb303d_context, typec_cap);
	fusb303d_get(ctx);
	dev_info(fusb303d_dev(ctx), "Try role. role=%d\n", role);
	dev_role = TYPEC_ICX_PORT_ROLE_DRP;
	switch (role) {
	case TYPEC_SINK:
		dev_role = TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK;
		break;
	case TYPEC_SOURCE:
		dev_role = TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC;
		break;
	default:
		/* DRP role, it may be TYPEC_NO_PREFERRED_ROLE */
		break;
	}
	ret = fusb303d_typec_dev_role_set(ctx, dev_role);
	if (ret)
		result = ret;

	fusb303d_put(ctx);
	return result;
}

/* Set port type - USB Type-C Class interface
 * NOTE: Here treat port type as port role,
 * FUSB303D can only perform as follows,
 *  DFP: PowerRole=Souce, DataRole=Host
 *  UFP: PowerRole=Sink,  DataRole=Device
 *  DRP: Determinate a mode from above two modes.
 */
static int fusb303d_typec_port_type_set(const struct typec_capability *cap,
	enum typec_port_type role)
{	struct fusb303d_context *ctx;
	int	dev_role;
	int	result = 0;
	int	ret;

	ctx = container_of(cap, struct fusb303d_context, typec_cap);
	fusb303d_get(ctx);
	dev_info(fusb303d_dev(ctx), "Set port type. role=%d\n", (int)role);

	dev_role = TYPEC_ICX_PORT_ROLE_DRP;
	switch (role) {
	case TYPEC_PORT_DFP:
		dev_role = TYPEC_ICX_PORT_ROLE_DRP_TRY_SRC;
		break;
	case TYPEC_PORT_UFP:
		dev_role = TYPEC_ICX_PORT_ROLE_DRP_TRY_SNK;
		break;
	case TYPEC_PORT_DRP:
		/* Fall through. */
	default:
		dev_role = TYPEC_ICX_PORT_ROLE_DRP;
		break;
	}

	ret = fusb303d_typec_dev_role_set(ctx, dev_role);
	if (ret)
		result = ret;

	fusb303d_put(ctx);
	return result;
}

static const enum typec_accessory connect_to_typec_accessory_tbl[] = {
	[TYPEC_ICX_CONNECTED_TO_NONE] =		TYPEC_ACCESSORY_NONE,
	[TYPEC_ICX_CONNECTED_TO_SOURCE] =	TYPEC_ACCESSORY_NONE,
	[TYPEC_ICX_CONNECTED_TO_SINK] =		TYPEC_ACCESSORY_NONE,
	[TYPEC_ICX_CONNECTED_TO_SOURCE_DEBUG] =	TYPEC_ACCESSORY_DEBUG,
	[TYPEC_ICX_CONNECTED_TO_SINK_DEBUG] =	TYPEC_ACCESSORY_DEBUG,
	[TYPEC_ICX_CONNECTED_TO_AUDIO_VBUS] =	TYPEC_ACCESSORY_AUDIO,
	[TYPEC_ICX_CONNECTED_TO_AUDIO] =	TYPEC_ACCESSORY_AUDIO,
	[TYPEC_ICX_CONNECTED_TO_UNKNOWN] =	TYPEC_ACCESSORY_NONE,
};

/* Refrect connect_to to Type C partner - USB Type-C Class interface
 * Pre-condition:
 *  read status by calling fusb303d_status_read_unlocked()
 */
static int fusb303d_connected_to_typec_accessory_refrect(
	struct fusb303d_context *ctx)
{	int	connect_to;
	int	result = 0;

	enum typec_accessory accessory = TYPEC_ACCESSORY_NONE;

	connect_to = fusb303d_connected_to(ctx);
	if (connect_to < 0) {
		result = connect_to;
		goto out;
	}

	if (connect_to >= TYPEC_ICX_CONNECTED_TO_NUMS) {
		result = -EIO;
		goto out;
	}
	accessory = connect_to_typec_accessory_tbl[connect_to];
out:
	ctx->typec_partner.accessory = accessory;
	return result;
}


/* Translate CC current value to typec_pwr_opmode - USB Type-C Class interface
 * @current_ma Current in mA detected on CC signal line.
 */
static enum typec_pwr_opmode fusb303d_current_to_typec_pwr_opmode(
	long current_ma)
{	/* Note: TYPEC_ICX_CURRENT_xxx has real current value in mA */
	if (current_ma < TYPEC_ICX_CURRENT_1500MA)
		return TYPEC_PWR_MODE_USB; /* 0..1499mA */

	if (current_ma < TYPEC_ICX_CURRENT_3000MA)
		return TYPEC_PWR_MODE_1_5A; /* 1500..2999mA */

	/* above 3000mA */
	return TYPEC_PWR_MODE_3_0A;
}

/* Translate CC current value to bq25898_icx_pd_properties
 *  Charger interface
 * @current_ma Current in mA detected on CC signal line.
 */
static enum bq25898_icx_pd_properties
	fusb303d_current_bq25898_icx_pd_properties(
	long current_ma
)
{	/* Note: TYPEC_ICX_CURRENT_xxx has real current value in mA */
	if (current_ma <= 0) {
		/* Unknown or Source(host) role. */
		return BQ25898_ICX_PD_UNKNOWN;
	}

	if (current_ma < TYPEC_ICX_CURRENT_1500MA) {
		/* Standard, 500mA in [0mA..1500mA) */
		return BQ25898_ICX_PD_SINK_STD;
	}

	if (current_ma < TYPEC_ICX_CURRENT_3000MA) {
		/* 1.5A in [1500mA..300mA) */
		return BQ25898_ICX_PD_SINK_1R5A;
	}

	/* above 3000mA */
	return BQ25898_ICX_PD_SINK_3R0A;
}


#ifdef CONFIG_REGMON_DEBUG

/* Foward definition. */
static int fusb303d_regmon_write_reg(void *private_data,
	unsigned int address,
	unsigned int value
);

static int fusb303d_regmon_read_reg(void *private_data,
	unsigned int address,
	unsigned int *value
);

static regmon_reg_info_t fusb303d_regmon_reg_info[] = {
	{ "DEVICE_ID",   0x01 },
	{ "DEVICE_TYPE", 0x02 },
	{ "PORTROLE",    0x03 },
	{ "CONTROL",     0x04 },
	{ "CONTROL1",    0x05 },
	{ "MANUAL",      0x09 },
	{ "RESET",       0x0A },
	{ "MASK",        0x0E },
	{ "MASK1",       0x0F },
	{ "STATUS",      0x11 },
	{ "STATUS1",     0x12 },
	{ "TYPE",        0x13 },
	{ "INTERRUPT",   0x14 },
	{ "INTERRUPT1",  0x15 },
};

struct fusb303d_regmon {
	struct mutex			mutex;
	regmon_customer_info_t		regmon;
};

struct fusb303d_regmon fusb303d_regmon_global = {
	.mutex = __MUTEX_INITIALIZER(fusb303d_regmon_global.mutex),
	.regmon = {
		.name           = "fusb303d",
		.reg_info       = fusb303d_regmon_reg_info,
		.reg_info_count = ARRAY_SIZE(fusb303d_regmon_reg_info),
		.write_reg      = fusb303d_regmon_write_reg,
		.read_reg       = fusb303d_regmon_read_reg,
		/*! Will be filled with driver context pointer. */
		.private_data   = NULL,
	},
};

static int fusb303d_regmon_write_reg(void *private_data,
	unsigned int address,
	unsigned int value
)
{
	struct fusb303d_context	*ctx;
	struct fusb303d_regmon	*rmon = &fusb303d_regmon_global;
	int ret;
	
	mutex_lock(&(rmon->mutex));
	if (rmon->regmon.private_data == NULL) {
		ret = -ENODEV;
		pr_err("%s: Regmon is not ready. ret=%d\n",
			__func__, ret
		);
		goto out;
	}
	ctx = private_data;
	if (ctx == NULL) {
		ret = -ENODEV;
		pr_err("%s: Driver context is not ready. ret=%d\n",
			__func__, ret
		);
		goto out;
	}

	fusb303d_get(ctx);
	ret = fusb303d_reg_write(ctx, (uint8_t)address, (uint8_t)value);
	fusb303d_put(ctx);
out:
	mutex_unlock(&(rmon->mutex));
	return ret;
}

static int fusb303d_regmon_read_reg(
	void *private_data,
	unsigned int address,
	unsigned int *value
)
{
	struct fusb303d_context *ctx;
	struct fusb303d_regmon	*rmon = &fusb303d_regmon_global;
	uint8_t		val;
	int		ret;

	mutex_lock(&(rmon->mutex));
	if (rmon->regmon.private_data == NULL) {
		ret = -ENODEV;
		pr_err("%s: Regmon is not ready. ret=%d\n",
			__func__, ret
		);
		goto out;
	}

	ctx = private_data;
	if (ctx == NULL) {
		ret = -ENODEV;
		pr_err("%s: Driver context is not ready. ret=%d\n",
			__func__, ret
		);
		goto out;
	}

	if (!value) {
		ret = -EIO;
		pr_err("%s: Regmon passed NULL pointer to value. ret=%d\n",
			__func__, ret
		);
		goto out;
	}

	fusb303d_get(ctx);
	*value = 0;

	ret = fusb303d_reg_read(ctx, (uint8_t)address, &val);
	*value = (unsigned int)val;
	fusb303d_put(ctx);
out:
	mutex_unlock(&(rmon->mutex));
	return ret;
}


static void fusb303d_regmon_add(struct fusb303d_context *ctx)
{
	struct fusb303d_regmon	*rmon = &fusb303d_regmon_global;

	mutex_lock(&(rmon->mutex));
	if (rmon->regmon.private_data == NULL) {
		rmon->regmon.private_data = ctx;
		regmon_add(&(rmon->regmon));
	} else {
		pr_notice("%s: Driver does not support "
			"regmon interface to two or more FUSB303D "
			"devices.\n",
			__func__
		);
	}
	mutex_unlock(&(rmon->mutex));
}


static void fusb303d_regmon_delete(struct fusb303d_context *ctx)
{
	struct fusb303d_regmon	*rmon = &fusb303d_regmon_global;

	mutex_lock(&(rmon->mutex));
	if (rmon->regmon.private_data == ctx) {
		rmon->regmon.private_data = NULL;
		regmon_del(&(rmon->regmon));
	}
	mutex_unlock(&(rmon->mutex));
}

#else /* CONFIG_REGMON_DEBUG */

static void fusb303d_regmon_add(struct fusb303d_context *ctx)
{	/* Do nothing. */
}

static void fusb303d_regmon_delete(struct fusb303d_context *ctx)
{	/* Do nothing. */
}

#endif /* CONFIG_REGMON_DEBUG */

/*! suspend - dev_pm_ops call back
 *
 */
static int fusb303d_suspend(struct device *dev)
{
	struct i2c_client	*i2c;
	struct fusb303d_context	*ctx;

	i2c = to_i2c_client(dev);
	ctx = i2c_get_clientdata(i2c);

	if (ctx == NULL) {
		dev_err(dev, "%s: NULL points driver context.\n",
			__func__
		);
		return -ENODEV;
	}

	return 0;
}

/*! resume - dev_pm_ops call back
 *
 */
static int fusb303d_resume(struct device *dev)
{
	struct i2c_client	*i2c;
	struct fusb303d_context *ctx;

	i2c = to_i2c_client(dev);
	ctx = i2c_get_clientdata(i2c);

	if (ctx == NULL){
		dev_err(dev, "%s: NULL points driver context.\n",
			__func__
		);
		return -ENODEV;
	}

	/*! @todo implement resume procedure. */
	fusb303d_get(ctx);
	fusb303d_irq_action(ctx, false);
	fusb303d_put(ctx);

	return 0;
}

/*! poweroff - dev_pm_ops call back
 *
 */
static int fusb303d_poweroff(struct device *dev)
{
	struct i2c_client	*i2c;
	struct fusb303d_context *ctx;

	i2c = to_i2c_client(dev);
	ctx = i2c_get_clientdata(i2c);

	if (ctx == NULL){
		dev_err(dev, "%s: NULL points driver context.\n",
			__func__
		);
		return -ENODEV;
	}

	ctx = i2c_get_clientdata(i2c);
	/* disable USBC_XINT interrupt */
	fusb303d_chip_interrupt_disable(ctx);
	irq_set_irq_wake(i2c->irq, 0);
	fusb303d_irq_disable(ctx);

	/* power off */
	fusb303d_chip_disable(ctx);
	wake_unlock(&(ctx->wake_lock));

	/* Keep context intentionally. */
	return 0;
}

static const char *prop_string_invalid =	"???";

/*! property en-n-gpio
 *  Description:
 *   GPIO port connectedto FUSB303D.EN_N pin.
 */
static const char *prop_en_n_gpios = "en-n"; /* "en-n-gpios" in device tree */

/*! property port-role
 *  Description:
 *   Default port (power delivery) role, choose one of,
 *    Role String	Description
 *    -----------------------------------
 *    No property	keep role configured in boot loader.
 *    "keep"		keep role configured in boot loader.
 *    "sink"		set sink role to port.
 *    "source,		set source role to port.
 *    "drp"		set dual role to port.
 *    "drp-try.snk"	set dual role with Try.SNK state to port.
 *    "drp-try.src"	set dual role with Try.SRC state to port.
 */
static const char *prop_port_role =		"port-role";

/*! property source-current
 *  Description:
 *   Default source current in Power Delivery Source Role.
 *   Specify current in mA.
 */
static const char *prop_source_current =	"source-current";

/*! property auto-sink-voltage
 *  Description:
 *   Default Auto Sink VDD Voltage
 *   Specify voltage in mV
 *   Value 0xffffffff means do not use Auto Sink feature.
 */
static const char *prop_auto_sink_voltage =	"auto-sink-voltage";

/*! property disabled-state
 * @todo will be implemented.
 * Description:
 *  Enter or not Enter Disable State at initialize.
 *   No property: Keep Type-C role state
 *   "keep"  Keep Type-C role state
 *   "enter" Enter Type-C role state "Disabled State"
 *   "exit"  Exit  Type-C role state "Disabled State"
 */
static __maybe_unused const char *prop_disabled_state = "disabled-state";

/*! property disabled-state-exit
 * @todo will be implemented.
 * Description:
 *  No property: Do not any action.
 *  number time in milli seconds to exit "Disabled State".
 *  Value 0xffffffff means do not any action exit "Disabled State".
 */
static __maybe_unused const char *prop_disabled_state_exit = "disabled-state-exit";

/*! property do-reset
 * @todo will be implemented.
 * Description:
 *  Do reset at initialize.
 *   No property: Do not reset.
 *   0: Do not reset at initialize.
 *   1: Do reset at at initialize.
 */
static __maybe_unused const char *prop_do_reset = "do-reset";

/*! property debounce-time
 * @todo will be implemented.
 * Description:
 *  Debounce time for attaching a device in milli seconds.
 *   No property: Chip default.
 *   0: Keep chip default time.
 *   120, 130, ..., 180: Debounce time.
 */
static __maybe_unused const char *prop_debounce_time = "debounce-time";

/*! property dangling-cable
 * @todo will be implemented.
 * Description:
 *  Dangling Cable handling.
 *  No property: Chip default.
 *  0: Disable Dangling Cable internal methods to achive a stable attach.
 *  1: Enable  Dangling Cable internal methods to achive a stable attach.
 */
static __maybe_unused const char *prop_dangling_cable = "dangling-cable";

/*! property drp-toggle-snk
 * @todo will be implemented.
 * Description:
 *  DRP togging duty percentage of Unattached.SNK.
 *   No property: Chip default.
 *   60: 60% in Unattached.SNK and 40% in Unattached.SRC
 *   50: 50% in Unattached.SNK and 50% in Unattached.SRC
 *   40: 40% in Unattached.SNK and 60% in Unattached.SRC
 *   30: 30% in Unattached.SNK and 70% in Unattached.SRC
 */
static __maybe_unused const char *prop_drp_toggle_snk = "drp_toggle_snk";

/*! property disabled-time-sink
 * Description:
 *  Disable CC signal lines time at changing role to sink in milli seconds.
 */
static const char *prop_disabled_time_sink = "disabled-time-sink";

/*! property sink-only
 *  Description:
 *   Fix port role sink only
 *   No property: Dual role (normal operation).
 *   0: Dual role (normal operation).
 *   1: Sink only (Disable power source function,
 *      avoid TW CNS15364 restriction).
 */
static const char *prop_sink_only = "sink-only";

/*! property attach-wakelock-ms
 *  Description:
 *   Wake lock time in milli seconds, when attached.
 */
static const char *prop_attach_wake_lock_ms = "attach-wakelock-ms";


/*! property detach-wakelock-ms
 *  Description:
 *   Wake lock time in milli seconds, when detached.
 */
static const char *prop_detach_wake_lock_ms = "detach-wakelock-ms";

/*! property detach-sleep-time-ms
 *  Description:
 *   Wake lock time in milli seconds, when detached.
 */
static const char *prop_detach_sleep_time_ms = "detach-sleep-time-ms";

static int fusb303d_probe_of(struct fusb303d_context *ctx)
{	int			ret;
	int			result = 0;
	struct device		*dev;
	struct device_node	*np;
	bool			of_bool;
	u32			of_val;
	const char		*of_str;

	dev = fusb303d_dev(ctx);
	np = dev_of_node(dev);
	if (np == NULL) {
		result = 0;
		dev_notice(dev,
			"No device tree. prop=%s, ret=%d",
			prop_source_current, result
		);
		/* It's not error. */
		goto out;
	}

	/* Get EN_N GPIO */
	ctx->en_n_gpio = devm_gpiod_get(dev, prop_en_n_gpios,
		GPIOD_OUT_LOW
	);
	if (ctx->en_n_gpio == NULL) {
		dev_notice(dev,
			"Can not read property. "
			"prop=%s-gpios\n",
			prop_en_n_gpios
		);
		/* It's not error. */
	}

	/* Get do-reset */
	of_bool = of_property_read_bool(np, prop_do_reset);
	if (!of_bool) {
		dev_notice(dev,
			"Can not read property. prop=%s",
			prop_do_reset
		);
		/* It's not error. */
	} else {
		/* Property sets do-reset. */
		of_val = PROPERTY_TRUE;
		ret = of_property_read_u32(np, prop_do_reset, &of_val);
		if (ret != 0) {
			dev_notice(dev,
				"Can not read property. prop=%s, ret=%d",
				prop_do_reset, ret
			);
			/* It's not error. */
		} else {
			if (of_val) {
				ctx->of_do_reset = PROPERTY_TRUE;
			} else {
				ctx->of_do_reset = PROPERTY_FALSE;
			}
		}
	}

	/* Get auto sink VBAT voltage. */
	of_val = TYPEC_ICX_NO_AUTO_SINK_VOLTAGE;
	ret = of_property_read_u32(np, prop_auto_sink_voltage, &of_val);
	if (ret != 0) {
		dev_notice(dev,
			"Can not read property. prop=%s, ret=%d",
			prop_auto_sink_voltage, ret
		);
		/* It's not error. */
	} else {
		/* Property sets auto_sink_voltage. */
		ctx->of_auto_sink_th_mv = of_val;
	}

	of_val = TYPEC_ICX_CURRENT_500MA;
	ret = of_property_read_u32(np, prop_source_current, &of_val);
	if (ret != 0) {
		dev_notice(dev,
			"Can not read property. prop=%s, ret=%d",
			prop_source_current, ret
		);
		/* It's not error. */
	} else {
		/* Property sets source current. */
		/* set default source current. */
		ctx->of_source_current = (__force long)of_val;
	}

	/* Set default port_role. */
	of_str = prop_string_invalid;
	ret = of_property_read_string(np, prop_port_role, &of_str);
	if (ret != 0) {
		dev_notice(dev,
			"Can not read property. prop=%s, ret=%d",
			prop_port_role, ret
		);
		/* It's not error. */
	} else {
		char	tmp[32];

		str_word_trim_cpy(tmp, sizeof(tmp), of_str, strlen(of_str));
		ctx->of_port_role = str_match_to(typec_icx_port_role_text, tmp);
	}

	of_val = DISABLED_TIME_SINK_DEFAULT;
	ret = of_property_read_u32(np, prop_disabled_time_sink, &of_val);
	if (ret != 0) {
		dev_notice(dev,
			"Can not read property. prop=%s, ret=%d",
			prop_disabled_time_sink, ret
		);
		/* It's not error. */
	} else {
		/* Property sets disabled state time to changing
		 * role to sink.
		 * Note: This property is also controllable from device
		 * attribute node disabled_time_sink.
		 * Treat this value as signed value.
		 */
		ctx->disabled_time_sink = (__force int32_t)of_val;
	}

	of_val = FUSB303D_OF_SINK_ONLY_NO;
	ret = of_property_read_u32(np, prop_sink_only, &of_val);
	switch (ret) {
	case -ENODATA:
	case -EOVERFLOW:
		/* Exist property, but missing or shorter value. */
		of_val = FUSB303D_OF_SINK_ONLY_YES;
		break;
	case -EINVAL:
		/* Not exist property. */
		of_val = FUSB303D_OF_SINK_ONLY_NO;
		break;
	default:
		if (ret < 0) {
			/* Some error, unknown. */
			of_val = FUSB303D_OF_SINK_ONLY_NO;
		} else {
			/* Success. */
			if (of_val != 0) {
				/* Extra value. */
				of_val = FUSB303D_OF_SINK_ONLY_YES;
			}
		}
		break;
	}
	ctx->of_sink_only = of_val;

	of_val = FUSB303D_ATTACH_WAKE_LOCK_MS;
	ctx->of_attach_wake_lock_ms = (uint)(of_val);
	ret = of_property_read_u32(np, prop_attach_wake_lock_ms, &of_val);
	if (ret == 0) {
		/* Property present. */
		ctx->of_attach_wake_lock_ms = (uint)(of_val);
		if (ctx->of_attach_wake_lock_ms >=
			FUSB303D_WAKE_LOCK_MS_CHECK) {
			/* Too long wake lock time, warn it. */
			dev_warn(dev, "Too long wake lock time. attach_wake_lock_ms=%u\n",
				ctx->of_attach_wake_lock_ms
			);
		}
	}

	of_val = FUSB303D_DETACH_WAKE_LOCK_MS;
	ctx->of_detach_wake_lock_ms = (uint)(of_val);
	ret = of_property_read_u32(np, prop_detach_wake_lock_ms, &of_val);
	if (ret == 0) {
		/* Property present. */
		ctx->of_detach_wake_lock_ms = (uint)(of_val);
		if (ctx->of_detach_wake_lock_ms >=
			FUSB303D_WAKE_LOCK_MS_CHECK) {
			/* Too long wake lock time, warn it. */
			dev_warn(dev, "Too long wake lock time. detach_wake_lock_ms=%u\n",
				ctx->of_detach_wake_lock_ms
			);
		}
	}

	of_val = FUSB303D_DETACH_SLEEP_TIME_MS;
	ctx->of_detach_sleep_time_ms = (uint)(of_val);
	ret = of_property_read_u32(np, prop_detach_sleep_time_ms, &of_val);
	if (ret == 0) {
		/* Property present. */
		ctx->of_detach_sleep_time_ms = (uint)(of_val);
		if (ctx->of_detach_sleep_time_ms >=
			FUSB303D_DETACH_SLEEP_TIME_CHECK_MS) {
			/* Too long sleep time, warn it. */
			dev_warn(dev, "Too long sleep time. detach_sleep_time_ms=%u\n",
				ctx->of_detach_sleep_time_ms
			);
		}
	}

out:
	return result;
}

/* Setup USB Type C port capability
 */
static int fusb303d_init_typec_cap(struct fusb303d_context *ctx)
{	int	current_role = TYPEC_ICX_PORT_ROLE_UNKNOWN;
	int	ret;
	int	result = 0;

	mutex_lock(&(ctx->mutex_chip));
	if (ctx->of_sink_only == FUSB303D_OF_SINK_ONLY_NO) {
		/* Port can change to UFP, DFP. */
		ret = fusb303d_port_role_read_unlocked(ctx, &current_role);
		if (ret) {
			result = ret;
			current_role = TYPEC_ICX_PORT_ROLE_DRP;
		}
		ctx->typec_cap.type = TYPEC_PORT_DRP;
		/* Not supported data role and power role swap. */
		ctx->typec_cap.try_role = fusb303d_typec_try_role;
		ctx->typec_cap.port_type_set = fusb303d_typec_port_type_set;
	} else {
		/* Fix port sink only. */
		current_role = TYPEC_ICX_PORT_ROLE_SINK;
		ctx->typec_cap.type = TYPEC_PORT_UFP;
		ctx->typec_cap.try_role = NULL;
		ctx->typec_cap.port_type_set = NULL;
	}
	ctx->typec_cap.revision = FUSB303D_TYPEC_REVISION;
	ctx->typec_cap.pd_revision = 0; /* Not Supported USB PD */
	/* typec class driver uses prefer_role member as
	 * backing store. The device attribute "preferred_role" uses
	 * this member. When storing value to this attribute
	 * call try_role() method.
	 */
	ctx->typec_cap.prefer_role =
		fusb303d_role_to_typec_role(current_role);
	ctx->typec_cap.accessory[0] = TYPEC_ACCESSORY_AUDIO;
	ctx->typec_cap.accessory[1] = TYPEC_ACCESSORY_DEBUG;
	/* Not supported data role and power role swap. */
	ctx->typec_cap.dr_set = NULL;
	ctx->typec_cap.pr_set = NULL;
	ctx->typec_cap.vconn_set = NULL;
	ctx->typec_cap.activate_mode = NULL;
	mutex_unlock(&(ctx->mutex_chip));
	return result;
}

/* Initialize USB Type C partner descriptor
 */
static void fusb303d_init_typec_partner(struct fusb303d_context *ctx)
{	/* FUSB303D can't handle USB PD specification. */
	ctx->typec_partner.usb_pd = 0;
	ctx->typec_partner.accessory = TYPEC_ACCESSORY_NONE;
	/* FUSB303D can't initiate PD identity. */
	ctx->typec_partner.identity = NULL;
}

static int fusb303d_probe(
	struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct device		*dev;
	struct fusb303d_context *ctx = NULL;
	int		result = 0;
	int		ret;

	dev = &(i2c->dev);
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		result = -ENOMEM;
		dev_err(&(i2c->dev), "Can not allocate device context. "
			"result=%d\n",
			result
		);
		goto out_alloc_error;
	}

	mutex_init(&(ctx->mutex_irq_action));
	mutex_init(&(ctx->mutex_chip));
	mutex_init(&(ctx->mutex_extcon));
	wake_lock_init(&(ctx->wake_lock), WAKE_LOCK_SUSPEND, dev_name(dev));
	kref_init(&(ctx->kref));

	ctx->context_marker = FUSB303D_CONTEXT_MARKER;
	ctx->i2c = i2c;

	ctx->of_auto_sink_th_mv = TYPEC_ICX_NO_AUTO_SINK_VOLTAGE;
	ctx->of_source_current = FUSB303D_OF_SOURCE_CURRENT_KEEP;
	ctx->of_port_role = TYPEC_ICX_PORT_ROLE_NOTHING;
	ctx->of_disable_time_ms = FUSB303D_OF_DISABLE_TIME_MS;
	ctx->of_do_reset = PROPERTY_NONE;
	ctx->of_sink_only = FUSB303D_OF_SINK_ONLY_NO;

	ctx->disabled_time_drp =    DISABLED_TIME_DRP_DEFAULT;
	ctx->disabled_time_sink =   DISABLED_TIME_SINK_DEFAULT;
	ctx->disabled_time_source = DISABLED_TIME_SOURCE_DEFAULT;

	ctx->usb_host = FUSB303D_EXTCON_UNKNOWN;
	ctx->usb_device = FUSB303D_EXTCON_UNKNOWN;
	ctx->chg_usb_pd = FUSB303D_EXTCON_UNKNOWN;
	ctx->prop_chg_min = BQ25898_ICX_PD_UNKNOWN;

	i2c_set_clientdata(i2c, ctx);

	/* Allocate extcon device */
	ctx->edev = devm_extcon_dev_allocate(&(i2c->dev), 
		fusb303d_extcon_cables
	);

	if (IS_ERR_OR_NULL(ctx->edev)) {
		result = PTR_ERR(ctx->edev);
		dev_err(&(i2c->dev), "Failed to allocate extcon device. result=%d\n",
			result
		);
		goto out_wake_lock_destroy;
	}

	/* Register extcon device. */
	ret = devm_extcon_dev_register(&(i2c->dev), ctx->edev);
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev), "Failed to register extcon device. "
			"result=%d\n",
			result
		);
		goto out_wake_lock_destroy;
	}

	/* Set property supports */
	ret = extcon_set_property_capability(ctx->edev, EXTCON_CHG_USB_PD,
		EXTCON_PROP_CHG_MIN
	);
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev), "Failed to set property capability. result=%d\n",
			result
		);
		goto out_wake_lock_destroy;
	}

	ret = fusb303d_probe_of(ctx);
	if (ret != 0) {
		result = ret;
		goto out_wake_lock_destroy;
	}

	fusb303d_chip_enable(ctx);

	/* Read DEVICE ID */
	ctx->device_id = ~((uint8_t)(0x00));
	ret = fusb303d_reg_read(ctx, FUSB303D_REG_DEVICE_ID, &(ctx->device_id));
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev),
			"Can not read DEVICE_ID. "
			"result=%d\n",
			result
		);
		goto out_disable;
	}

	/* Read DEVICE TYPE */
	ctx->device_type = ~((uint8_t)(0x00));
	ret = fusb303d_reg_read(ctx, FUSB303D_REG_DEVICE_TYPE, &(ctx->device_type));
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev),
			"Can not read DEVICE_TYPE. "
			"result=%d\n",
			result
		);
		goto out_disable;
	}

	dev_info(&(i2c->dev), "Hello device. DEVICE_ID=0x%.2x, DEVICE_TYPE=0x%.2x\n",
		ctx->device_id, ctx->device_type
	);

	/* initialize chip */
	ret = fusb303d_chip_initialize(ctx);
	if (ret < 0) {
		result = ret;
		dev_err(&(i2c->dev),
			"Can not initialize chip. "
			"result=%d\n",
			result
		);
		goto out_disable;
	}

	ret = fusb303d_init_typec_cap(ctx);
	if (ret != 0) {
		result = ret;
		goto out_disable;
	}

	fusb303d_init_typec_partner(ctx);

	ctx->port = typec_register_port(&(i2c->dev), &(ctx->typec_cap));
	if (!(ctx->port)) {
		result = -EINVAL;
		dev_err(&(i2c->dev), "Can not register typec port class driver. result=%d\n",
			result
		);
		goto out_disable;
	}

	/* Look around device status, and refrect to Type C class
	 * device status.
	 */
	fusb303d_irq_action(ctx, false);

	/* create sysfs */
	ret = sysfs_create_group(&i2c->dev.kobj, &fusb303d_attr_group);
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev),
			"Can not create device attribute node. "
			"result=%d\n",
			result
		);
		goto out_unregister_port;
	}

	ret = devm_request_threaded_irq(&(i2c->dev), i2c->irq, NULL,
		fusb303d_irq_handler,
		IRQF_ONESHOT | IRQF_TRIGGER_LOW,
		dev_name(&(i2c->dev)),
		ctx
	);
	/* @note here IRQ is enabled. */
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev), "Can not request IRQ handler. "
			"irq=%d, "
			"result=%d\n",
			i2c->irq, result
		);
		goto out_remove_sysfs;
	}

	device_set_wakeup_capable(&(i2c->dev), true);
	device_wakeup_enable(&(i2c->dev));

	ret = irq_set_irq_wake(i2c->irq, 1);
	if (ret != 0) {
		result = ret;
		dev_err(&(i2c->dev), "Can not configure IRQ wake. "
			"irq=%d, "
			"result=%d\n",
			i2c->irq, result
		);
		goto out_shut_irq;
	}

	ret = fusb303d_chip_interrupt_enable(ctx);
	if (ret != 0) {
		result = ret;
		goto out_shut_irq;
	}

	/* register regmon */
	fusb303d_regmon_add(ctx);

	return 0;

out_shut_irq:
	fusb303d_chip_interrupt_disable(ctx);
	irq_set_irq_wake(i2c->irq, 0);
	devm_free_irq(&(i2c->dev), i2c->irq, ctx);

out_remove_sysfs:
	sysfs_remove_group(&i2c->dev.kobj, &fusb303d_attr_group);

out_unregister_port:
	typec_unregister_port(ctx->port);

out_disable:
	fusb303d_chip_disable(ctx);

out_wake_lock_destroy:
	wake_lock_destroy(&(ctx->wake_lock));
	fusb303d_put(ctx);

out_alloc_error:
	return result;
}

static int fusb303d_remove(struct i2c_client *i2c)
{
	struct fusb303d_context *ctx;

	ctx = i2c_get_clientdata(i2c);

	/* delete regmon */
	fusb303d_regmon_delete(ctx);
	/* disable USBC_XINT interrupt */
	fusb303d_chip_interrupt_disable(ctx);
	irq_set_irq_wake(i2c->irq, 0);
	devm_free_irq(&(i2c->dev), i2c->irq, ctx);
	/* Rmove typec port class driver. */
	if (ctx->port)
		typec_unregister_port(ctx->port);
	ctx->port = NULL;
	/* remove sysfs */
	sysfs_remove_group(&i2c->dev.kobj, &fusb303d_attr_group);

	/* power off */
	fusb303d_chip_disable(ctx);

	/* Free context. */
	fusb303d_put(ctx);
	return 0;
}


const struct of_device_id fusb303d_of_match[] = {
	{ .compatible = "sony,fusb303d", },
	{ /* Fill Zero */ }
};

static struct i2c_device_id fusb303d_i2c_id[] = {
	{"fusb303d", 0},
	{ /* Fill Zero */ }
};

MODULE_DEVICE_TABLE(i2c, fusb303d_i2c_id);

static const struct dev_pm_ops fusb303d_pm_ops = {
	.suspend =  fusb303d_suspend,
	.resume =   fusb303d_resume,
	.poweroff = fusb303d_poweroff,
};

static struct i2c_driver fusb303d_driver = {
	.driver = {
		.name =		  "fusb303d",
		.of_match_table = fusb303d_of_match,
		.pm =		  &fusb303d_pm_ops,
	},
	.probe =    fusb303d_probe,
	.remove =   fusb303d_remove,
	.id_table = fusb303d_i2c_id,
};

module_i2c_driver(fusb303d_driver);

MODULE_DESCRIPTION("FUSB303D driver");
MODULE_AUTHOR("Sony Video & Sound Products Inc.");
MODULE_LICENSE("GPL");
