/*
 * TI BQ25898 charger driver based bq25890_charger.c
 *
 * Copyright (C) 2015 Intel Corporation
 * Copyright 2018, 2019 Sony Video & Sound Products Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
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
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/usb/phy.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/pm_wakeup.h>
#include <linux/wakelock.h>

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/extcon.h>

#include <linux/power/sony_max1704x.h>
#include <linux/power/bq25898_icx_charger.h>
#include <linux/usb/gadget.h>

bool imx_lpa_is_enabled(void);

atomic_t bq25898_detect_vbus = ATOMIC_INIT(0);

#define	BQ25898_DEAD_BATTERY_UV_DEF		(3600 * 1000)
#define	BQ25898_DEAD_BATTERY_CHARGE_TIME_MS	(90 * 1000)
#define	BQ25898_DEAD_BATTERY_PREPARE_UV		(3700 * 1000)
#define	BQ25898_LOW_BATTERY_CAPACITY		5
#define	BQ25898_SOFT_WARM_TEMP_LOW_DEF		(415) /* Celsius x 10 */
#define	BQ25898_SOFT_WARM_TEMP_HIGH_DEF		(436) /* Celsius x 10 */
#define	BQ25898_SOFT_WARM_TEMP_NEAR		(5 * 10)
#define	BQ25898_SOFT_WARM_POLL_LONG_MS		(120 * 1000)
#define	BQ25898_SOFT_WARM_POLL_SHORT_MS		 (30 * 1000)
/* Soft Full polling interval. recommended value is
 * same to BQ25898_SOFT_WARM_POLL_LONG_MS.
 */
#define	BQ25898_SOFT_FULL_POLL_LONG_MS		(120 * 1000)
#define	BQ25898_SOFT_FULL_POLL_SHORT_MS		(60 * 1000)

/* Dead battey boot parameter.
 * int < 0: Do normal boot.
 * int <  3600 * 1000: dead battery boot mode.
 * int >= 3600 * 1000: normal boot mode.
 * Only available in kernel boot parameter.
 */
int bq25898_dead_battery = -1;
module_param_named(dead_battery, bq25898_dead_battery, int, 0644);

#define DEBUG_REGS		(0x00000001)
#define DEBUG_REGW		(0x00000002)
#define DEBUG_REGR		(0x00000004)
#define DEBUG_IRQ		(0x00000008)
#define DEBUG_EVENT		(0x00000010)
#define DEBUG_THREAD		(0x00000020)
#define DEBUG_NO_REG_CHECK	(0x00000040)

/* debug parameter.
 */
uint bq25898_debug = DEBUG_REGS;
module_param_named(debug, bq25898_debug, int, 0644);

#define DEV_INFO_REGS(dev, format, ...) \
do { \
	if (bq25898_debug & DEBUG_REGS) { \
		/* Debug on state register */ \
		dev_info(dev, format, ## __VA_ARGS__); \
	} \
} while (0)

#define DEV_INFO_REGW(dev, format, ...) \
do { \
	if (bq25898_debug & DEBUG_REGW) { \
		/* Debug on register write */ \
		dev_info(dev, format, ## __VA_ARGS__); \
	} \
} while (0)

#define DEV_INFO_REGR(dev, format, ...) \
do { \
	if (bq25898_debug & DEBUG_REGW) { \
		/* Debug on register write */ \
		dev_info(dev, format, ## __VA_ARGS__); \
	} \
} while (0)

#define DEV_INFO_IRQ(dev, format, ...) \
do { \
	if (bq25898_debug & DEBUG_IRQ) { \
		/* Debug on IRQ */ \
		dev_info(dev, format, ## __VA_ARGS__); \
	} \
} while (0)

#define DEV_INFO_EVENT(dev, format, ...) \
do { \
	if (bq25898_debug & DEBUG_EVENT) { \
		/* Debug on event */ \
		dev_info(dev, format, ## __VA_ARGS__); \
	} \
} while (0)

#define DEV_INFO_THREAD(dev, format, ...) \
do { \
	if (bq25898_debug & DEBUG_THREAD) { \
		/* Debug thread loop */ \
		dev_info(dev, format, ## __VA_ARGS__); \
	} \
} while (0)


#define BQ25898_MANUFACTURER		"Texas Instruments"

/* Configure BOOST delayed after interrupt. */
#define	BOOST_CONFIG_DELAY_MS		(100)	/* ms */

/* Invalid register field value.
 * @note All register fields less than 0xff.
 */
#define	INVALID_REG_FIELD_VALUE		(0xff)


/* Register rename table.
 * Data sheet | Driver code
 * ------------------------
 * ICHGR      | ICHG_R
 */

enum bq25898_fields {
	F_REG00,						/* Reg00.7-0 */
	F_EN_HIZ, F_EN_ILIM, F_IINLIM,				/* Reg00.7-0 */

	F_REG01,						/* Reg01.7-0 */
	F_DPLUS_DAC, F_DMINUS_DAC,				/* Reg01.7-2 */
	F_EN_12V, F_VDPM_OS,					/* Reg01.1-0 */

	F_REG02,						/* Reg02.7-0 */
	F_CONV_START, F_CONV_RATE, F_BOOST_FREQ, F_ICO_EN,	/* Reg02.7-4 */
	F_HVDCP_EN, F_MAXC_EN, F_FORCE_DPM, F_AUTO_DPDM_EN,	/* Reg02.3-0 */

	F_REG03,						/* Reg03.7-0 */
	F_VOK_OTG_EN, F_WD_RST, F_OTG_CONFIG,			/* Reg03.7-5 */
	F_CHG_CONFIG,						/* Reg03.4   */
	F_SYS_MIN, F_MIN_VBAT_SEL,				/* Reg03.3-0 */

	F_REG04,						/* Reg04.7-0 */
	F_EN_PUMPX, F_ICHG,					/* Reg04.7-0 */

	F_REG05,						/* Reg05.7-0 */
	F_IPRECHG,						/* Reg05.7-4 */
	F_ITERM,						/* Reg05.3-0 */

	F_REG06,						/* Reg06.7-0 */
	F_VREG,							/* Reg06.7-2 */
	F_BATLOWV, F_VRECHG,					/* Reg06.0-2 */

	F_REG07,						/* Reg07.7-0 */
	F_EN_TERM, F_STAT_DIS, F_WATCHDOG,			/* Reg07.7-4 */
	F_EN_TIMER, F_CHG_TIMER, F_JEITA_ISET,			/* Reg07.3-0 */

	F_REG08,						/* Reg08.7-0 */
	F_BAT_COMP, F_VCLAMP,					/* Reg08.7-2 */
	F_TREG,							/* Reg08.1-0 */

	F_REG09,						/* Reg09.7-0 */
	F_FORCE_ICO, F_TMR2X_EN, F_BATFET_DIS, F_JEITA_VSET,	/* Reg09.7-4 */
	F_BATFET_DLY, F_BATFET_RST_EN, F_PUMPX_UP, F_PUMPX_DN,	/* Reg09.3-0 */

	F_REG0A,						/* Reg0A.7-0 */
	F_BOOSTV,						/* Reg0A.7-4 */
	F_PFM_OTG_DIS, F_BOOST_LIM,				/* Reg0A.3-0 */
	/* Do not use F_VBUS..F_VSYS_STAT,
	 * Use F_REG0B only.
	 */
	F_REG0B,						/* Reg0B.7-0 */
	F_VBUS_STAT, F_CHRG_STAT,				/* Reg0B.7-3 */
	F_PG_STAT, F_VSYS_STAT,					/* Reg0B.2-0 */
	/* Do not use F_WATCHDOG_FAULT..F_NTC_FAULT
	 * Use F_REG0C only, REG0C changes value after read,
	 * See. 9.2.9.3 Interrupt to Host (INT).
	 */
	F_REG0C,						/* Reg0C.7-0 */
	F_WATCHDOG_FAULT, F_BOOST_FAULT, F_CHRG_FAULT,		/* Reg0C.7-4 */
	F_BAT_FAULT, F_NTC_FAULT,				/* Reg0C.2-0 */

	F_REG0D,						/* Reg0D.7-0 */
	F_FORCE_VINDPM, F_VINDPM,				/* Reg0D.7-0 */

	F_REG0E,						/* Reg0E.7-0 */
	F_THERM_STAT, F_BATV,					/* Reg0E.7-0 */

	F_REG0F,						/* Reg0F.7-0 */
	F_SYSV,							/* Reg0F.7-0 */

	F_REG10,						/* Reg10.7-0 */
	F_TSPCT,						/* Reg10.7-0 */

	F_REG11,						/* Reg11.7-0 */
	F_VBUS_GD, F_VBUSV,					/* Reg11.7-0 */

	F_REG12,						/* Reg12.7-0 */
	F_ICHG_R,						/* Reg12.7-0 */

	F_REG13,						/* Reg13.7-0 */
	F_VDPM_STAT, F_IDPM_STAT, F_IDPM_LIM,			/* Reg13.7-0 */

	F_REG14,						/* Reg14.7-0 */
	F_REG_RST, F_ICO_OPTIMIZED, F_PN,			/* Reg14.7-3 */
	F_TS_PROFILE, F_DEV_REV,				/* Reg14.2-0 */

	F_ICHG_WARM,	/* Virtual, Charge Current at warm condition. */
	F_VREG_WARM,	/* Virtual, Charge Voltage at warm condition. */
	F_FIELDS_NUM
};

#define	F_FIELDS_VIRTUAL_REG	(F_ICHG_WARM)

/* NOTE: If REG02.AUTO_DPDM_EN == 0, VBUS_STAT shows only two
 * states. It shows REG0B_VBUS_STAT_NO_INPUT or REG0B_VBUS_STAT_OTG.
 * No other states will not be seen even if power comes on VBUS input.
 */
#define	REG0B_VBUS_STAT_MASK		 (0xe0)
#define	REG0B_VBUS_STAT_NO_INPUT	 (0x00)
#define	REG0B_VBUS_STAT_USB_HOST_SDP	 (0x20)	/* PSEL == High (Fixed) */
#define	REG0B_VBUS_STAT_ADAPTER		 (0x40)	/* PSEL == Low */
#define	REG0B_VBUS_STAT_USB_DCP		 (0x60)	/* Not Used */
#define	REG0B_VBUS_STAT_USB_HV_DCP	 (0x80)	/* Not Used */
#define	REG0B_VBUS_STAT_UNKNOWN_ADAPTER	 (0xa0)	/* Not Used */
#define	REG0B_VBUS_STAT_NON_STANDARD	 (0xc0)	/* Not Used */
#define	REG0B_VBUS_STAT_OTG		 (0xe0)

#define	REG0B_CHRG_STAT_MASK		(0x18)
#define	REG0B_CHRG_STAT_NOT_CHARGING	(0x00)
#define	REG0B_CHRG_STAT_PRE_CHARGE	(0x08)
#define	REG0B_CHRG_STAT_FAST_CHARGE	(0x10)
#define	REG0B_CHRG_STAT_CHARGE_DONE	(0x18) /* CHARGE_TERMINATION_DONE */

#define	REG0B_PG_STAT_POWER_GOOD	(0x04)

#define	REG0B_VSYS_STAT_REG_VSYSMIN	(0x01)


/* Rename table:
 * Charge Safety Timer Expiration: TIMER_EXPIRE
 */
#define	REG0C_WATCHDOG_FAULT		 (0x80)
#define	REG0C_BOOST_FAULT		 (0x40)
#define	REG0C_CHRG_FAULT_MASK		 (0x30)
#define	REG0C_CHRG_FAULT_NORMAL		 (0x00)
#define	REG0C_CHRG_FAULT_INPUT_FAULT	 (0x10)
#define	REG0C_CHRG_FAULT_THRMAL_SHUTDOWN (0x20)
#define	REG0C_CHRG_FAULT_TIMER_EXPIRE	 (0x30)

#define	REG0C_BAT_FAULT_OVP		(0x08)

#define	REG0C_NTC_FAULT_MASK		(0x07)
#define	REG0C_NTC_FAULT_NORMAL		(0x00)
#define	REG0C_NTC_FAULT_TS_WARM		(0x02)
#define	REG0C_NTC_FAULT_TS_COOL		(0x03)
#define	REG0C_NTC_FAULT_TS_COLD		(0x05)
#define	REG0C_NTC_FAULT_TS_HOT		(0x06)
#define	REG0C_NTC_FAULT_UNKNOWN		(0xFF)

#define	REG14_REGRST			(0x80)
#define	REG14_ICO_OPTIMIZED		(0x40)
#define	REG14_PN_MASK			(0x38)
#define	REG14_PN_BQ25898D		(0x10)
#define	REG14_PN_BQ25898		(0x00)
#define	REG14_TS_PROFILE		(0x08)
#define	REG14_DEV_REV_MASK		(0x03)
#define	REG14_DEV_REV_00		(0x00)
#define	REG14_DEV_REV_01		(0x01)
#define	REG14_DEV_REV_02		(0x02)
#define	REG14_DEV_REV_03		(0x03)

/* REG0C.TSPCT Access through regmap */
#define	F_TSPCT_MIN	(0x00)
#define	F_TSPCT_MAX	(0x7f)

/* EN_HIZ values */
#define	F_EN_HIZ_CONNECT	(0)
#define	F_EN_HIZ_HIGH_Z		(1)

/* CHG_CONFIG values */
#define	F_CHG_CONFIG_RETAIN	(0)
#define	F_CHG_CONFIG_CHARGE	(1)
/* We can change BQ25898.CHG_CONFIG bit
 * only if VBUS is not sourced from
 * USB Type-C Connector.
 */

enum adc_config_modes {
	ADC_CONFIG_STOP	=	(0x00),	/* Stop ADC */
	ADC_CONFIG_ONE_SHOT =	(0x01),	/* One shot conversion */
	ADC_CONFIG_AUTO =	(0x02),	/* Continuous conversion */
};

enum vbus_boost_action {
	VBUS_BOOST_OFF =	((int)false),	/* alias false */
	VBUS_BOOST_ON =		((int)true),	/* alias true  */
	VBUS_BOOST_KEEP =	(0x02),
};

/*
 * Most of the val -> idx conversions can be computed, given the minimum,
 * maximum and the step between values. For the rest of conversions, we use
 * lookup tables.
 */
enum bq25898_table_ids {
	/* range tables */
	TBL_ICHG,
	TBL_ITERM,
	TBL_IPRECHG,
	TBL_VREG,
	TBL_SYS_MIN,
	TBL_BAT_COMP,
	TBL_VCLAMP,
	TBL_IINLIM,
	TBL_BOOSTV,
	TBL_BATV,
	TBL_VBUSV,
	TBL_ICHG_R,
	TBL_VINDPM,

	/* lookup tables */
	TBL_BOOST_LIM,
	TBL_TREG,
	TBL_CHG_TIMER,

	/* x to y tables */
	TBL_EN_ILIM,
	TBL_BOOST_FREQ,
	TBL_JEITA_ISET,
	TBL_JEITA_VSET,
	TBL_FORCE_VINDPM,
	TBL_EN_TIMER,
	TBL_AUTO_DPDM_EN,
	TBL_NUMS,
};


/* Invalid init data.
 * @note All members are less than 0xff.
 */
#define	INIT_DATA_INVALID	(INVALID_REG_FIELD_VALUE)

struct init_field_val {
	enum bq25898_fields	field;	/* regmap field number */
	u8			val;	/* Use LSB 8bits. */
	const char		*prop;	/* Device Tree Property */
	enum bq25898_table_ids	trans;	/* Translate index. */
};

enum bq25898_tspct_mode {
	TSPCT_SELF_STAND,	/* Self powred */
	TSPCT_VBUS_INCOMING,	/* VBUS powered */
	TSPCT_MODE_ALL
};

#define	BQ25898_NTC_TABLE_SIZE	(F_TSPCT_MAX - F_TSPCT_MIN + 1)

struct bq25898_state {
	u8 reg0b_evented;	/* TC */
	u8 reg0c_evented;	/* TC */
	u8 reg0b_current;	/* TC */
	u8 reg0c_current;	/* TC */
	u8 reg0b_prev;		/* TC */
	u8 reg0c_prev;		/* TC */

	u8 vbus_evented;	/* TC */
	u8 chrg_evented;	/* TC */
	u8 pg_evented;		/* TC */
	u8 vsys_evented;	/* TC */
	u8 watchdog_fault_evented; /* TC */
	u8 boost_fault_evented;	   /* TC */
	u8 chrg_fault_evented;	   /* TC */
	u8 bat_fault_evented;	   /* TC */
	u8 ntc_fault_evented;	   /* TC */

	u8 vbus_current;	/* TC, ST */
	u8 chrg_current;	/* TC, ST */
	u8 pg_current;		/* TC, ST */
	u8 vsys_current;	/* TC, ST */
	u8 watchdog_fault_current; /* TC, ST */
	u8 boost_fault_current;	/* TC, ST */
	u8 chrg_fault_current;	/* TC, ST */
	u8 bat_fault_current;	/* TC, ST */
	u8 ntc_fault_current;	/* TC, ST */

	u8 pg_previous;		/* TC */

	enum bq25898_tspct_mode	tspct_mode_current; /* TC, ST */
	int temp_current; /* TC, ST, Celsius x10 temperature (TSPCT) */
	int temp_current_raw; /* TC  Celsius x10 temperature, raw (TSPCT) */
	int temp_prev; /* TC, ST, Celsius x10 temperature (TSPCT) */
	int batv_current; /* TC Battery Voltage in uV  */
};

#define	EXTCON_OF_CHARGER	(0)
#define	EXTCON_OF_TYPEC		(1)
#define	EXTCON_OF_USB_CONFIG	(2)

#define	EXTCON_REGISTER_TRY_MAX_MS	(10 * 1000)
#define	EXTCON_REGISTER_TRY_WAIT_MS	 (1 * 1000)

#define	CHG_USB_NONE		(0)
#define	CHG_USB_UNCONFIG	(1)
#define	CHG_USB_HOT_COLD	(2)
#define	CHG_USB_EMG		(3)

#define	CHG_USB_SDP		(EXTCON_CHG_USB_SDP)
#define	CHG_USB_DCP		(EXTCON_CHG_USB_DCP)
#define	CHG_USB_CDP		(EXTCON_CHG_USB_CDP)
#define	CHG_USB_ACA		(EXTCON_CHG_USB_ACA)
#define	CHG_USB_FAST		(EXTCON_CHG_USB_FAST)
#define	CHG_USB_SLOW		(EXTCON_CHG_USB_SLOW)
#define	CHG_USB_ALL		(CHG_USB_SLOW + 1)

#if ((CHG_USB_EMG >= CHG_USB_SDP)  || (CHG_USB_EMG >= CHG_USB_DCP) \
  || (CHG_USB_EMG >= CHG_USB_CDP)  || (CHG_USB_EMG >= CHG_USB_ACA) \
  || (CHG_USB_EMG >= CHG_USB_FAST) || (CHG_USB_EMG >= CHG_USB_SLOW) \
)
#error CHG_x constants overwrap EXTCON_CHG_x constant.
#endif /* complex expression */

#if ((CHG_USB_ALL <= CHG_USB_SDP)  || (CHG_USB_ALL <= CHG_USB_DCP) \
  || (CHG_USB_ALL <= CHG_USB_CDP)  || (CHG_USB_ALL <= CHG_USB_ACA) \
  || (CHG_USB_ALL <= CHG_USB_FAST) || (CHG_USB_ALL <= CHG_USB_SLOW) \
)
#error CHG_x constants overwrap EXTCON_CHG_x constant.
#endif /* complex expression */

/* Event bit numbers (bit position), */
#define	CHARGER_EVENT_EXIT		(0x00)
#define	CHARGER_EVENT_SYNC		(0x01)
#define	CHARGER_EVENT_IRQ		(0x02)
#define	CHARGER_EVENT_VBUS_CHANGE	(0x03)
#define	CHARGER_EVENT_CHG_USB		(0x04)
#define	CHARGER_EVENT_USB_OTG		(0x05)
#define	CHARGER_EVENT_USB_HOST		(0x06)
#define	CHARGER_EVENT_CHG_USB_PD	(0x07)
#define	CHARGER_EVENT_USB_CONFIG	(0x08)
#define	CHARGER_EVENT_VBUS_DRAW		(0x09)
#define	CHARGER_EVENT_FAULT		(0x0a)
#define	CHARGER_EVENT_WAKE_NOW		(0x0b)
#define	CHARGER_EVENT_SYNC_DIAG		(0x0c)
#define	CHARGER_EVENT_USB_DEV		(0x0d)
#define	CHARGER_EVENT_TEMP_POLL		(0x0e)
#define	CHARGER_EVENT_SOFT_WARM		(0x0f)
#define	CHARGER_EVENT_SUSPEND		(0x10)
#define	CHARGER_EVENT_RESUME		(0x11)


#define	EXTCON_CHG_USB_TERMINATOR	(-1 /* negative value */)

const static int chg_usb_extcon_order[] = {
	EXTCON_CHG_USB_SDP,	/* least preferred type. */
	EXTCON_CHG_USB_ACA,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_SLOW,
	EXTCON_CHG_USB_FAST,	/* most preferred type. */
	EXTCON_CHG_USB_TERMINATOR
};

#define	CHG_USB_EXTCON_ALL_NUM	\
	(ARRAY_SIZE(chg_usb_extcon_order) - 1)

enum charge_config_index {
	CHARGE_CONFIG_NONE, /* Internal use. */
	CHARGE_CONFIG_USB_SDP,
	CHARGE_CONFIG_SUSPEND_PC,
	CHARGE_CONFIG_UNCONFIG,
	CHARGE_CONFIG_CONFIG,
	CHARGE_CONFIG_DEAD_BATTERY,
	CHARGE_CONFIG_USB_CDP,
	CHARGE_CONFIG_USB_DCP,
	CHARGE_CONFIG_TYPEC_1500,
	CHARGE_CONFIG_TYPEC_3000,
	CHARGE_CONFIG_USB_MISC,
	CHARGE_CONFIG_USB_APPLE,
	CHARGE_CONFIG_FULL_SUSPEND,
	CHARGE_CONFIG_CHRG_FAULT,
	CHARGE_CONFIG_BAT_FAULT,
	CHARGE_CONFIG_NO_POWER,
	CHARGE_CONFIG_NO_POWER_LOW,
	CHARGE_CONFIG_DIAG_VBUS,
	CHARGE_CONFIG_NUMS,
};

struct charge_config {
	uint8_t		flags;	   /*!< Misc flags. */
	uint8_t		en_hiz;    /*!< EN_HIZ */
	uint8_t		iinlim;	   /*!< IINLIM, configure after ICHG. */
	uint8_t		chg_config;/*!< CHG_CONFIG */
};

static const char *charge_config_root = "svs,charge";

static const char *const charge_config_names[] = {
	[CHARGE_CONFIG_NONE] = "none",
	[CHARGE_CONFIG_USB_SDP] = "sdp",
	[CHARGE_CONFIG_SUSPEND_PC] = "suspend-pc",
	[CHARGE_CONFIG_UNCONFIG] = "unconfig",
	[CHARGE_CONFIG_CONFIG] = "config",
	[CHARGE_CONFIG_DEAD_BATTERY] = "dead-battery",
	[CHARGE_CONFIG_USB_CDP] = "cdp",
	[CHARGE_CONFIG_USB_DCP] = "dcp",
	[CHARGE_CONFIG_TYPEC_1500] = "typec-1500",
	[CHARGE_CONFIG_TYPEC_3000] = "typec-3000",
	[CHARGE_CONFIG_USB_MISC] = "usb-misc",
	[CHARGE_CONFIG_USB_APPLE] = "usb-apple",
	[CHARGE_CONFIG_FULL_SUSPEND] = "full-suspend",
	[CHARGE_CONFIG_CHRG_FAULT] = "chrg-fault",
	[CHARGE_CONFIG_BAT_FAULT] = "bat-fault",
	[CHARGE_CONFIG_NO_POWER] = "no-power",
	[CHARGE_CONFIG_NO_POWER_LOW] = "no-power-low",
	[CHARGE_CONFIG_DIAG_VBUS] = "*diag-vbus",
	[CHARGE_CONFIG_NUMS] = NULL,
};

/* Default charge configuration.
 */
static struct charge_config charge_config_def = {
	.flags = 0,
	.en_hiz = 0,		/* Turn On VBUS power sink path switch. */
	.iinlim = 0x0a,		/* 500mA */
	.chg_config = 0x1,	/* Enable Charge */
};

struct bq25898_device;

struct bq25898_nb_container {
	struct notifier_block	nb;
	struct bq25898_device	*bq;
};

#define	BQ25898_DEVICE_FLAGS_REMOVE	(0x00)
#define	BQ25898_DEVICE_FLAGS_IRQ	(0x01)
#define	BQ25898_DEVICE_FLAGS_SUSPENDING	(0x02)

#define	BQ25898_DIAG_EXPIRE_MS		(180 * 1000)

/* Locking notation and order
 * 1. SI: Lock with sem_diag
 * 2. ST: Lock with sem_this
 * 3. SD: Lock with sem_dev
 * TC: Use in initialize and charger_thread context only.
 */

struct bq25898_device {
	unsigned long		flags;
	struct i2c_client	*client;
	struct device		*dev;
	struct power_supply	*charger;
	struct regulator_dev	*rdev_charger;
	struct regmap *rmap;
	struct regmap_field	 *rmap_fields[F_FIELDS_NUM];
	struct init_field_val	*init_data;

	u8			reg14;

	struct semaphore	sem_dev;	/* =SD */
	struct semaphore	sem_this;	/* =ST */
	struct semaphore	sem_diag;	/* =SI */
	struct {
		struct usb_phy		*phy;
		struct notifier_block	nb;
		spinlock_t		lock;     /* =LU */
		unsigned long		event_cb; /* LU */
		unsigned long		event;
	} usb_otg;

	struct {
		struct extcon_dev	*edev;
		struct notifier_block	nb_host;
		struct notifier_block	nb_dev;
		struct notifier_block	nb_chg;
		bool			nb_registered_host;
		bool			nb_registered_dev;
		bool			nb_registered_chg;
		bool			state_host;      /* TC */
		bool			state_dev;       /* TC */
		bool			state_chg;       /* TC */
		enum bq25898_icx_pd_properties prop_chg; /* TC */
	} typec;

	struct {
		struct extcon_dev		*edev;
		struct bq25898_nb_container	nbc[CHG_USB_EXTCON_ALL_NUM];
		unsigned long			reged;
		int				type;    /* TC */
	} chg_usb;

	struct {
		struct extcon_dev		*edev;
		struct notifier_block		nb;
		bool				registered;
		bool				state;		/* TC */
		int				draw_current;	/* TC */
	} usb_config;

	unsigned long		charger_event;
	unsigned long		charger_event_catch; /* TC */
	wait_queue_head_t	charger_wq;
	struct task_struct	*charger_thread;
	struct completion	charger_sync;
	struct completion	charger_sync_diag;
	struct completion	charger_exit;
	struct completion	charger_suspend;
	struct completion	charger_resume;

	u8				iinlim_current;	/* TC */
	struct bq25898_state		state;	    /* TC, ST (*1) */
	enum charge_config_index	cc_new;	    /* TC */
	enum charge_config_index	cc_current; /* TC */
	enum vbus_boost_action		vbus_boost; /* TC */
	bool				startup_done; /* TC */
	bool				cc_force_config; /* TC */
	bool				temp_trip;      /* TC */
	bool				status_update;  /* TC */
	bool				ps_prop_notify; /* TC */
	bool				ps_prop_notify_pend; /* TC */
	bool				dead_battery;    /* TC */
	bool				watchdog_clear;  /* TC */
	bool				temp_poll;	 /* ST */
	bool				vbus_boost_trip; /* TC */
	bool				ichg_down_new;	 /* TC */
	bool				vreg_down_new;	 /* TC */
	bool				ichg_down_current; /* TC */
	bool				vreg_down_current; /* TC */
	bool				irq_wake_of;	/* Init */
	u8				pg_suspend;     /* TC, ST */
	int				batv_suspend;	/* TC, ST */
	int				temp_suspend;	/* TC, ST */
	int				dead_battery_uv; /* TC */
	int				dead_battery_prepare_uv; /* TC */
	unsigned int			dead_battery_time_ms; /* TC */
	int				low_battery_capacity;
	int				soft_warm_temp_low; /* ST */
	int				soft_warm_temp_high; /* ST */
#define	SOFT_WARM_STATE_LOW	(0) /* Initial value. */
#define	SOFT_WARM_STATE_HIGH	(1)
	int				soft_warm_state; /* TC */
#define	SOFT_FULL_NOTYET	(0) /* Initial value. */
#define	SOFT_FULL_DETECT	(1)
#define	SOFT_FULL_STABLE	(2)
#define	SOFT_FULL_SUSPEND	(3)
	int				soft_full_state; /* TC */
#define	RESUMED_TIMER_DONE	(0) /* Initial value. */
#define	RESUMED_TIMER_STOP	(1)
#define	RESUMED_TIMER_RUN	(2)
	int				resumed_timer; /* TC */
	int				poll_count;  /* TC */
	int				poll_period_ms; /* TC */
	long				timeout_ms;	/* TC */
	u64				jiffies_start;	/* TC */
	u64				jiffies_now;	/* TC */
	u64				jiffies_diag_vbus;	/* ST */
	u64				jiffies_temp_poll; /* ST */
	u64				jiffies_soft_full_detect; /* ST */
	u64				jiffies_resume;	/* TC */
#define	DIAG_VBUS_SINK_CURRENT_NORMAL	(-1)
#define	DIAG_VBUS_SINK_CURRENT_ENABLE	(0)
#define	DIAG_VBUS_SINK_CURRENT_HIZ	(0)
	int				diag_vbus_sink_current; /* ST */
	spinlock_t			rdev_lock;	/* =LR */
	int				rdev_charger_current_cb; /* LR */
	int				rdev_charger_current;    /* TC */
	struct regulator_config		rdev_charger_conf;
	struct regulator_init_data	rdev_charger_init_data;
	struct regulator_linear_range	rdev_charger_range_volt;
	struct regulator_desc		rdev_charger_desc;
	struct charge_config		charge_configs[CHARGE_CONFIG_NUMS];
	s16	ntc_table[TSPCT_MODE_ALL][BQ25898_NTC_TABLE_SIZE];

	/* (*1) Almost part of bq25898_state members are
	 * used in charger_thread context only (TC), but
	 * some members be shared with other thread (ST).
	 */
};

struct bq25898_proxy {
	int			status;	/* Return Status */
	struct bq25898_device	*bq;	/* Driver context. */
};

/* Semaphore to lock proxy */
DEFINE_SEMAPHORE(bq25898_proxy_sem);

static struct bq25898_proxy bq25898_proxy = {
	.status = -EPROBE_DEFER,
	.bq = NULL,
};

static const struct regmap_range bq25898_readonly_reg_ranges[] = {
	regmap_reg_range(0x0b, 0x0c), /* *_STAT, *_FAULT */
	regmap_reg_range(0x0e, 0x13), /* THERM_*, BATV, ... */
};

static const struct regmap_access_table bq25898_writeable_regs = {
	.no_ranges = bq25898_readonly_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(bq25898_readonly_reg_ranges),
};

static const struct regmap_range bq25898_volatile_reg_ranges[] = {
	regmap_reg_range(0x00, 0x00),
	regmap_reg_range(0x02, 0x14),
};

static const struct regmap_access_table bq25898_volatile_regs = {
	.yes_ranges = bq25898_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bq25898_volatile_reg_ranges),
};

static const struct regmap_config bq25898_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0x14,
	.cache_type = REGCACHE_NONE,

	.wr_table = &bq25898_writeable_regs,
	.volatile_table = &bq25898_volatile_regs,
};

static const struct reg_field bq25898_reg_fields[] = {
	/* REG00 */
	[F_REG00]		= REG_FIELD(0x00, 0, 7),
	[F_EN_HIZ]		= REG_FIELD(0x00, 7, 7),
	[F_EN_ILIM]		= REG_FIELD(0x00, 6, 6),
	[F_IINLIM]		= REG_FIELD(0x00, 0, 5),
	/* REG01 */
	[F_REG01]		= REG_FIELD(0x01, 0, 7),
	[F_DPLUS_DAC]		= REG_FIELD(0x01, 5, 7),
	[F_DMINUS_DAC]		= REG_FIELD(0x01, 2, 3),
	[F_EN_12V]		= REG_FIELD(0x01, 1, 1),
	[F_VDPM_OS]		= REG_FIELD(0x01, 0, 0),
	/* REG02 */
	[F_REG02]		= REG_FIELD(0x02, 0, 7),
	[F_CONV_START]		= REG_FIELD(0x02, 7, 7),
	[F_CONV_RATE]		= REG_FIELD(0x02, 6, 6),
	[F_BOOST_FREQ]		= REG_FIELD(0x02, 5, 5),
	[F_ICO_EN]		= REG_FIELD(0x02, 4, 4),
	[F_HVDCP_EN]		= REG_FIELD(0x02, 3, 3),
	[F_MAXC_EN]		= REG_FIELD(0x02, 2, 2),
	[F_FORCE_DPM]		= REG_FIELD(0x02, 1, 1),
	[F_AUTO_DPDM_EN]	= REG_FIELD(0x02, 0, 0),
	/* REG03 */
	[F_REG03]		= REG_FIELD(0x03, 0, 7),
	[F_VOK_OTG_EN]		= REG_FIELD(0x03, 7, 7),
	[F_WD_RST]		= REG_FIELD(0x03, 6, 6),
	[F_OTG_CONFIG]		= REG_FIELD(0x03, 5, 5),
	[F_CHG_CONFIG]		= REG_FIELD(0x03, 4, 4),
	[F_SYS_MIN]		= REG_FIELD(0x03, 1, 3),
	[F_MIN_VBAT_SEL]	= REG_FIELD(0x03, 0, 0),
	/* REG04 */
	[F_REG04]		= REG_FIELD(0x04, 0, 7),
	[F_EN_PUMPX]		= REG_FIELD(0x04, 7, 7),
	[F_ICHG]		= REG_FIELD(0x04, 0, 6),
	/* REG05 */
	[F_REG05]		= REG_FIELD(0x05, 0, 7),
	[F_IPRECHG]		= REG_FIELD(0x05, 4, 7),
	[F_ITERM]		= REG_FIELD(0x05, 0, 3),
	/* REG06 */
	[F_REG06]		= REG_FIELD(0x06, 0, 7),
	[F_VREG]		= REG_FIELD(0x06, 2, 7),
	[F_BATLOWV]		= REG_FIELD(0x06, 1, 1),
	[F_VRECHG]		= REG_FIELD(0x06, 0, 0),
	/* REG07 */
	[F_REG07]		= REG_FIELD(0x07, 0, 7),
	[F_EN_TERM]		= REG_FIELD(0x07, 7, 7),
	[F_STAT_DIS]		= REG_FIELD(0x07, 6, 6),
	[F_WATCHDOG]		= REG_FIELD(0x07, 4, 5),
	[F_EN_TIMER]		= REG_FIELD(0x07, 3, 3),
	[F_CHG_TIMER]		= REG_FIELD(0x07, 1, 2),
	[F_JEITA_ISET]		= REG_FIELD(0x07, 0, 0),
	/* REG08 */
	[F_REG08]		= REG_FIELD(0x08, 0, 7),
	[F_BAT_COMP]		= REG_FIELD(0x08, 6, 7),
	[F_VCLAMP]		= REG_FIELD(0x08, 2, 4),
	[F_TREG]		= REG_FIELD(0x08, 0, 1),
	/* REG09 */
	[F_REG09]		= REG_FIELD(0x09, 0, 7),
	[F_FORCE_ICO]		= REG_FIELD(0x09, 7, 7),
	[F_TMR2X_EN]		= REG_FIELD(0x09, 6, 6),
	[F_BATFET_DIS]		= REG_FIELD(0x09, 5, 5),
	[F_JEITA_VSET]		= REG_FIELD(0x09, 4, 4),
	[F_BATFET_DLY]		= REG_FIELD(0x09, 3, 3),
	[F_BATFET_RST_EN]	= REG_FIELD(0x09, 2, 2),
	[F_PUMPX_UP]		= REG_FIELD(0x09, 1, 1),
	[F_PUMPX_DN]		= REG_FIELD(0x09, 0, 0),
	/* REG0A */
	[F_REG0A]		= REG_FIELD(0x0A, 0, 7),
	[F_BOOSTV]		= REG_FIELD(0x0A, 4, 7),
	[F_PFM_OTG_DIS]		= REG_FIELD(0x0A, 3, 3),
	[F_BOOST_LIM]		= REG_FIELD(0x0A, 0, 2),
	/* REG0B */
	[F_REG0B]		= REG_FIELD(0x0B, 0, 7),
	[F_VBUS_STAT]		= REG_FIELD(0x0B, 5, 7),
	[F_CHRG_STAT]		= REG_FIELD(0x0B, 3, 4),
	[F_PG_STAT]		= REG_FIELD(0x0B, 2, 2),
	[F_VSYS_STAT]		= REG_FIELD(0x0B, 0, 0),
	/* REG0C */
	[F_REG0C]		= REG_FIELD(0x0C, 0, 7),
	[F_WATCHDOG_FAULT]	= REG_FIELD(0x0C, 7, 7),
	[F_BOOST_FAULT]		= REG_FIELD(0x0C, 6, 6),
	[F_CHRG_FAULT]		= REG_FIELD(0x0C, 4, 5),
	[F_BAT_FAULT]		= REG_FIELD(0x0C, 3, 3),
	[F_NTC_FAULT]		= REG_FIELD(0x0C, 0, 2),
	/* REG0D */
	[F_REG0D]		= REG_FIELD(0x0D, 0, 7),
	[F_FORCE_VINDPM]	= REG_FIELD(0x0D, 7, 7),
	[F_VINDPM]		= REG_FIELD(0x0D, 0, 6),
	/* REG0E */
	[F_REG0E]		= REG_FIELD(0x0E, 0, 7),
	[F_THERM_STAT]		= REG_FIELD(0x0E, 7, 7),
	[F_BATV]		= REG_FIELD(0x0E, 0, 6),
	/* REG0F */
	[F_REG0F]		= REG_FIELD(0x0F, 0, 7),
	[F_SYSV]		= REG_FIELD(0x0F, 0, 6),
	/* REG10 */
	[F_REG10]		= REG_FIELD(0x10, 0, 7),
	[F_TSPCT]		= REG_FIELD(0x10, 0, 6),
	/* REG11 */
	[F_REG11]		= REG_FIELD(0x11, 0, 7),
	[F_VBUS_GD]		= REG_FIELD(0x11, 7, 7),
	[F_VBUSV]		= REG_FIELD(0x11, 0, 6),
	/* REG12 */
	[F_REG12]		= REG_FIELD(0x12, 0, 7),
	[F_ICHG_R]		= REG_FIELD(0x12, 0, 6),
	/* REG13 */
	[F_REG13]		= REG_FIELD(0x13, 0, 7),
	[F_VDPM_STAT]		= REG_FIELD(0x13, 7, 7),
	[F_IDPM_STAT]		= REG_FIELD(0x13, 6, 6),
	[F_IDPM_LIM]		= REG_FIELD(0x13, 0, 5),
	/* REG14 */
	[F_REG14]		= REG_FIELD(0x14, 0, 7),
	[F_REG_RST]		= REG_FIELD(0x14, 7, 7),
	[F_ICO_OPTIMIZED]	= REG_FIELD(0x14, 6, 6),
	[F_PN]			= REG_FIELD(0x14, 3, 5),
	[F_TS_PROFILE]		= REG_FIELD(0x14, 2, 2),
	[F_DEV_REV]		= REG_FIELD(0x14, 0, 1)
};

#define	TBL_RANGE	(0)
#define	TBL_LOOKUP	(1)
#define	TBL_XY		(2)

struct bq25898_range {
	u32 min;
	u32 max;
	u32 step;
};

struct bq25898_lookup {
	const u32	*tbl;
	size_t		size;
};

struct bq25898_xy_entry {
	u32	x;
	u8	y;
};

struct bq25898_xy {
	const struct bq25898_xy_entry	*tbl;
	size_t				size;
};

/* Boost mode current limit lookup table, in uA */
static const u32 bq25898_boost_lim_tbl[] = {
	 500000,  800000, 1000000, 1200000,
	1500000, 1800000, 2100000, 2400000
};

/* Fast Charge Timer Setting */
static const u32 bq25898_chg_timer_tbl[] = {5, 8, 12, 20};

/* Thermal Regulation Threshold lookup table, in degrees Celsius */
static const u32 bq25898_treg_tbl[] = { 60, 80, 100, 120 };

/* Boost frequeny lookup table, in Hz */
static const struct bq25898_xy_entry bq25898_boost_freq_tbl[] = {
	{15000000, 0}, {500000, 1}
};

/* JEITA Low Temperature Current Setting lookup table, in PerCent */
static const struct bq25898_xy_entry bq25898_jeita_iset_tbl[] = {
	{50, 0}, {20, 1}
};

/* JEITA High Temperature Voltage Setting lookup table, in uV */
static const struct bq25898_xy_entry bq25898_jeita_vset_tbl[] = {
	{200000, 0}, {0, 1}
};

/* Force VINDPM mode */
static const struct bq25898_xy_entry bq25898_force_vindpm_tbl[] = {
	{0, 0}, {1, 1}
};

/* Charging Safety Timer Enable */
static const struct bq25898_xy_entry bq25898_en_timer_tbl[] = {
	{0, 0}, {1, 1}
};

/* enable ILIM pin */
static const struct bq25898_xy_entry bq25898_en_ilim_tbl[] = {
	{0, 0}, {1, 1}
};

/* Automatic D+/D- Detection Enable */
static const struct bq25898_xy_entry bq25898_auto_dpdm_en_tbl[] = {
	{0, 0}, {1, 1}
};

struct bq25898_trans_entry {
	int	type;
	union {
		struct bq25898_range  rt;
		struct bq25898_lookup lt;
		struct bq25898_xy     xy;
	}	table;
};

#define	TBL_RANGE_DEF(min, max, step)	\
	{.type =  TBL_RANGE, \
	 .table = {.rt = {(min), (max), (step)}}, \
	}

#define	TBL_LOOKUP_DEF(tbl)	\
	{.type =  TBL_LOOKUP, \
	 .table = {.lt = {(tbl), ARRAY_SIZE(tbl)}}, \
	}

#define	TBL_XY_DEF(tbl)	\
	{.type =  TBL_XY, \
	 .table = {.xy = {(tbl), ARRAY_SIZE(tbl)}}, \
	}

#define	BQ25898_BOOSTV_UV_DEF	(5062000)
#define	BQ25898_IINLIM_UA_MAX	(3250000)
#define	BQ25898_IINLIM_UA_STEP	  (50000)

static const struct bq25898_trans_entry bq25898_trans_tables[] = {
	/* range tables */
	[TBL_ICHG] =	 TBL_RANGE_DEF      (0,  4032000,  64000), /* uA */
	[TBL_ITERM] =	 TBL_RANGE_DEF  (64000,  1024000,  64000), /* uA */
	[TBL_IPRECHG] =	 TBL_RANGE_DEF  (64000,  1024000,  64000), /* uA */
	[TBL_VREG] =	 TBL_RANGE_DEF(3840000,  4608000,  16000), /* uV */
	[TBL_SYS_MIN] =  TBL_RANGE_DEF(3000000,  3700000, 100000), /* uV */
	[TBL_BAT_COMP] = TBL_RANGE_DEF      (0,      140,     20), /* mOhm */
	[TBL_VCLAMP] =	 TBL_RANGE_DEF      (0,   224000,  32000), /* uV */
	[TBL_IINLIM] =   TBL_RANGE_DEF (100000, BQ25898_IINLIM_UA_MAX,  BQ25898_IINLIM_UA_STEP),  /* uA */
	[TBL_BOOSTV] =	 TBL_RANGE_DEF(4550000,  5510000,  64000), /* uV */
	[TBL_BATV] =	 TBL_RANGE_DEF(2304000,  4844000,  20000), /* uV */
	[TBL_VBUSV] =	 TBL_RANGE_DEF(2600000, 15300000, 100000), /* uV */
	[TBL_ICHG_R] =	 TBL_RANGE_DEF      (0,  6350000,  50000), /* uA */
	[TBL_VINDPM] =	 TBL_RANGE_DEF(2600000, 15300000, 100000), /* uV */

	/* lookup tables */
	[TBL_BOOST_LIM] =  TBL_LOOKUP_DEF(bq25898_boost_lim_tbl),
	[TBL_TREG] =	   TBL_LOOKUP_DEF(bq25898_treg_tbl),
	[TBL_CHG_TIMER] =  TBL_LOOKUP_DEF(bq25898_chg_timer_tbl),

	/* xy tables. */
	[TBL_EN_ILIM] =      TBL_XY_DEF(bq25898_en_ilim_tbl),
	[TBL_BOOST_FREQ] =   TBL_XY_DEF(bq25898_boost_freq_tbl),
	[TBL_JEITA_ISET] =   TBL_XY_DEF(bq25898_jeita_iset_tbl),
	[TBL_JEITA_VSET] =   TBL_XY_DEF(bq25898_jeita_vset_tbl),
	[TBL_FORCE_VINDPM] = TBL_XY_DEF(bq25898_force_vindpm_tbl),
	[TBL_EN_TIMER] =     TBL_XY_DEF(bq25898_en_timer_tbl),
	[TBL_AUTO_DPDM_EN] = TBL_XY_DEF(bq25898_auto_dpdm_en_tbl),
};

#if (defined(TBL_ENTRY_PTR))
#error Re-define TBL_ENTRY_PTR, fix macro definition.
#endif /* (defined(TBL_ENTRY_PTR)) */

#define	TBL_ENTRY_PTR(entry) (&(bq25898_trans_tables[(entry)]));

static unsigned int reg_field_reg(const struct reg_field *rf, int index)
{	return rf[index].reg;
}

#define REG_FIELD_REG(index) \
	(((index) < F_FIELDS_NUM) ? \
		reg_field_reg(bq25898_reg_fields, (index)) : 0xff)

static unsigned int reg_field_lsb(const struct reg_field *rf, int index)
{	return rf[index].lsb;
}

#define REG_FIELD_LSB(index) \
	(((index) < F_FIELDS_NUM) ? \
		reg_field_lsb(bq25898_reg_fields, (index)) : 0xff)

/* Forward prototypes */

static bool bq25898_state_tspct_valid_unlocked(struct bq25898_device *bq);
static bool bq25898_state_tspct_valid(struct bq25898_device *bq);
static void bq25898_extcon_chg_usb_get_state(struct bq25898_device *bq);
static void bq25898_charger_thread_wake(struct bq25898_device *bq);


static int bq25898_field_read(struct bq25898_device *bq,
			      enum bq25898_fields field)
{
	int ret;
	int val;

	ret = regmap_field_read(bq->rmap_fields[field], &val);
	if (ret < 0) {
		dev_err(bq->dev, "Can not read register. field=%d, reg=0x%.2x.%x, ret=%d\n",
			field,
			REG_FIELD_REG(field), REG_FIELD_LSB(field),
			ret
		);
		return ret;
	}

	return val;
}


static int bq25898_field_write(struct bq25898_device *bq,
			       enum bq25898_fields field, u8 val)
{
	int	ret;
	ret = regmap_field_write(bq->rmap_fields[field], val);
	if (ret < 0)
		dev_err(bq->dev, "Can not write register. field=%d, reg=0x%.2x.%x, field.val=0x%.2x, ret=%d\n",
			field,
			REG_FIELD_REG(field), REG_FIELD_LSB(field),
			val, ret
		);

	return ret;
}


static int bq25898_watchdog_write(struct bq25898_device *bq, bool wdog)
{	int	ret;

	/* disable watchdog */
	ret = bq25898_field_write(bq, F_WATCHDOG, (u8)wdog);
	if (ret < 0) {
		/* Fail to update WATCHDOG */
		dev_err(bq->dev, "Can not set WATCHDOG. wdog=%d, ret=%d\n",
			(int)wdog, ret
		);
	}
	return ret;
}

static int bq25898_adc_config_unlocked(struct bq25898_device *bq,
	enum adc_config_modes conf
)
{	int	ret = 0;
	int	result = 0;

	uint8_t	start = 0;
	uint8_t	rate =  0;

	switch (conf) {
	default:
	case ADC_CONFIG_STOP:
		start = 0;
		rate =  0;
		break;
	case ADC_CONFIG_ONE_SHOT:
		start = 1;
		rate =  0;
		break;
	case ADC_CONFIG_AUTO:
		start = 1;
		rate =  1;
		break;
	}
	ret = bq25898_field_write(bq, F_CONV_RATE, rate);
	result = result ? result : ret;

	ret = bq25898_field_write(bq, F_CONV_START, start);
	result = result ? result : ret;

	return result;
}

static int bq25898_adc_config(struct bq25898_device *bq,
	enum adc_config_modes conf
)
{	int ret;

	down(&(bq->sem_dev));
	ret = bq25898_adc_config_unlocked(bq, conf);
	up(&(bq->sem_dev));
	return ret;
}

static int bq25898_vbus_boost_config(struct bq25898_device *bq, bool on)
{	int	ret;

	DEV_INFO_REGW(bq->dev, "VBUS boost config. on=%d\n", (int)(on));
	down(&(bq->sem_dev));
	ret = bq25898_field_write(bq, F_OTG_CONFIG, (__force u8)on);
	if (ret < 0) {
		dev_err(bq->dev,
			"Can not write OTG_CONFIG. ret=%d\n",
			ret
		);
		/* Anyway, contine. */
	}
	up(&(bq->sem_dev));
	return ret;
}

/* Check battery charge current and voltage shuld be down.
 * @pre sorf_warm_state and soft_full_state are determined.
 */
static void bq25898_charge_i_v_down_work(struct bq25898_device *bq)
{	int	soft_warm_state;
	int	soft_full_state;

	soft_warm_state = bq->soft_warm_state;
	soft_full_state = bq->soft_full_state;

	bq->ichg_down_new = false;
	bq->vreg_down_new = false;

	switch (soft_warm_state) {
	case SOFT_WARM_STATE_HIGH:
		/* High temperature. */
		bq->ichg_down_new = true;
		bq->vreg_down_new = true;
		break;
	default:
		/* do nothing. */
		break;
	}

	switch (soft_full_state) {
	case SOFT_FULL_SUSPEND:
		/* Charge full. */
		bq->vreg_down_new = true;
		break;
	default:
		/* do nothing. */
		break;
	}
}


static int bq25898_charge_config(struct bq25898_device *bq,
	enum charge_config_index ccindex, bool ichg_down, bool vreg_down)
{	int	ret = 0;
	int	result = 0;

	enum bq25898_fields		field;
	u8				val;
	struct init_field_val		*init;
	struct charge_config		*cconf;

	if ((ccindex < 0) && (ccindex >= CHARGE_CONFIG_NUMS)) {
		/* Out of index do nothing. */
		/* It may be usesd intentionaly. */
		return 0;
	}

	dev_info(bq->dev, "Configure charger. config=%s:%s:%s\n",
		charge_config_names[ccindex],
		(ichg_down ? "idn" : "inm"),
		(vreg_down ? "vdn" : "vnm")
	);

	atomic_set(&bq25898_detect_vbus, ccindex == CHARGE_CONFIG_USB_SDP
				     || ccindex == CHARGE_CONFIG_SUSPEND_PC
				     || ccindex == CHARGE_CONFIG_UNCONFIG
				     || ccindex == CHARGE_CONFIG_CONFIG
				     || ccindex == CHARGE_CONFIG_DEAD_BATTERY
				     || ccindex == CHARGE_CONFIG_USB_CDP
				     || ccindex == CHARGE_CONFIG_USB_DCP
				     || ccindex == CHARGE_CONFIG_TYPEC_1500
				     || ccindex == CHARGE_CONFIG_TYPEC_3000
				     || ccindex == CHARGE_CONFIG_USB_MISC
				     || ccindex == CHARGE_CONFIG_USB_APPLE);

	cconf = &(bq->charge_configs[ccindex]);
	down(&(bq->sem_dev));

	if (cconf->en_hiz == 0) {
		ret = bq25898_field_write(bq,
			F_EN_HIZ, cconf->en_hiz
		);
		result = result ? result : ret;
	}

	if (cconf->chg_config == 0) {
		ret = bq25898_field_write(bq,
			F_CHG_CONFIG, cconf->chg_config
		);
		result = result ? result : ret;
	}

	init = bq->init_data;
	while ((field = init->field) < F_FIELDS_NUM) {
		enum bq25898_fields	field_write;

		field_write = field;

		val = init->val;
		if (val == INVALID_REG_FIELD_VALUE) {
			/* No more entry. */
			break;
		}

		if (field == F_ICHG) {
			/* Charge Current. */
			if (ichg_down) {
				/* Down charge current and voltage. */
				goto next_field;
			}
			/* Normal condition.
			 * Use normal ICHG value.
			 */
		}

		if (field == F_ICHG_WARM) {
			/* Virtual Register, Charge Current at soft warm. */
			if (!ichg_down) {
				/* Normal condition. */
				goto next_field;
			}
			/* "Soft warm" or "Soft full" condition.
			 * Use Soft warm ICHG value.
			 */
			field_write = F_ICHG;
		}

		if (field == F_VREG) {
			/* Charge Voltage. */
			if (vreg_down) {
				/* Down charge current and voltage. */
				goto next_field;
			}
			/* Normal temperature condition.
			 * Use normal VREG value.
			 */
		}

		if (field == F_VREG_WARM) {
			/* Virtual Register, Charge Voltage. */
			if (!vreg_down) {
				/* Normal condition. */
				goto next_field;
			}
			/* "Soft warm" or "Soft full" condition.
			 * Use Soft warm VREG value.
			 */
			field_write = F_VREG;
		}

		ret = bq25898_field_write(bq, field_write, val);
		result = result ? result : ret;
next_field:
		init++;
	}

	if (cconf->iinlim != INVALID_REG_FIELD_VALUE) {
		ret = bq25898_field_write(bq,
			F_IINLIM, cconf->iinlim
		);
		result = result ? result : ret;
	} else {
		ret = bq25898_field_write(bq,
			F_IINLIM, bq->iinlim_current
		);
		result = result ? result : ret;
	}

	if (cconf->en_hiz != 0) {
		ret = bq25898_field_write(bq,
			F_EN_HIZ, cconf->en_hiz
		);
		result = result ? result : ret;
	}

	if (cconf->chg_config != 0) {
		ret = bq25898_field_write(bq,
			F_CHG_CONFIG, cconf->chg_config
		);
		result = result ? result : ret;
	}

	up(&(bq->sem_dev));
	return result;
}

static bool bq25898_charge_config_check(struct bq25898_device *bq,
	enum charge_config_index ccindex, bool ichg_down, bool vreg_down)
{	int	ret = 0;
	bool	result = true;
	u8	reg_val;
	u8	exp_val;
	enum bq25898_fields		field;
	enum bq25898_fields		field_read;
	struct init_field_val		*init;
	struct charge_config		*cconf;

	if ((ccindex < 0) && (ccindex >= CHARGE_CONFIG_NUMS)) {
		/* Out of index do nothing. */
		/* It may be usesd intentionaly. */
		return true;
	}

	cconf = &(bq->charge_configs[ccindex]);
	init = bq->init_data;
	down(&(bq->sem_dev));

	while ((field = init->field) < F_FIELDS_NUM) {
		field_read = field;
		exp_val = init->val;
		if (exp_val == INVALID_REG_FIELD_VALUE) {
			/* Terminator. */
			break;
		}

		if (field == F_ICHG) {
			/* Charge Current. */
			if (ichg_down) {
				/* Battery Current and Voltage down. */
				goto next_field;
			}
			/* Normal condition.
			 * Use normal ICHG value.
			 */
		}

		if (field == F_ICHG_WARM) {
			/* Virtual Register, Charge Current at soft warm. */
			if (!ichg_down) {
				/* Normal battery current and voltage. */
				goto next_field;
			}
			/* "Soft warm condition" or "Soft full condition"
			 * Use Soft warm ICHG value.
			 */
			field_read = F_ICHG;
		}

		if (field == F_VREG) {
			/* Charge Voltage. */
			if (vreg_down) {
				/* Battery Current and Voltage down. */
				goto next_field;
			}
			/* Normal temperature condition.
			 * Use normal VREG value.
			 */
		}

		if (field == F_VREG_WARM) {
			/* Virtual Register, Charge Voltage at soft warm. */
			if (!vreg_down) {
				/* Battery Current and Voltage down. */
				goto next_field;
			}
			/* Soft warm condition.
			 * Use Soft warm VREG value.
			 */
			field_read = F_VREG;
		}

		ret = bq25898_field_read(bq, field_read);
		if (ret < 0) {
			result = false;
			goto out;
		}
		reg_val = ret;
		if (reg_val != exp_val) {
			if (field_read != F_ICHG) {
				/* Ignore changed ICHG. */
				dev_notice(bq->dev,
					"Unexpected reset. reg=0x%.2x.%x, field=0x%x, exp=0x%.2x, reg=0x%.2x\n",
					REG_FIELD_REG(field_read),
					REG_FIELD_LSB(field_read),
					field_read,
					exp_val,
					reg_val
				);
			}
			result = false;
			goto out;
		}
next_field:
		init++;
	}

	ret = bq25898_field_read(bq, F_EN_HIZ);
	if (ret < 0) {
		result = false;
		goto out;
	}

	reg_val = ret;
	if (reg_val != cconf->en_hiz) {
		dev_notice(bq->dev,
			"Unexpected reset. EN_HIZ=0x%.2x\n", reg_val);
		result = false;
		goto out;
	}

	if (cconf->iinlim != INVALID_REG_FIELD_VALUE) {
		ret = bq25898_field_read(bq, F_IINLIM);
		if (ret < 0) {
			result = false;
			goto out;
		}

		reg_val = ret;
		if (reg_val != cconf->iinlim) {
			dev_notice(bq->dev,
				"Unexpected reset (1). IINLIM=0x%.2x\n",
				reg_val)
			;
			result = false;
			goto out;
		}
	} else {
		ret = bq25898_field_read(bq, F_IINLIM);
		if (ret < 0) {
			result = false;
			goto out;
		}

		reg_val = ret;
		if (reg_val != bq->iinlim_current) {
			dev_notice(bq->dev,
				"Unexpected reset (2). IINLIM=0x%.2x\n",
				reg_val
			);
			result = false;
			goto out;
		}

	}

	ret = bq25898_field_read(bq, F_CHG_CONFIG);
	if (ret < 0) {
		result = false;
		goto out;
	}

	reg_val = ret;
	if (reg_val != cconf->chg_config) {
		dev_notice(bq->dev,
			"Unexpected reset. CHG_CONFIG=0x%.2x\n", reg_val);
		result = false;
		goto out;
	}

out:
	up(&(bq->sem_dev));
	return result;
}


#define	NOT_FOUND_INDEX	(INVALID_REG_FIELD_VALUE)

/* Convert value to register value.
 * @note Value which is not on grid(step) are rounded to
 *       lower index.
 */
static u8 bq25898_range_to_idx(u32 value, const struct bq25898_range *range)
{
	u32	idx;
	u32	idx_max;
	u32	offset;

	if ((value < range->min) || (value >= (range->max + range->step)))
		return NOT_FOUND_INDEX;

	offset = value - range->min;
	idx = offset / range->step;
	idx_max = (range->max - range->min) / range->step;
	if (idx > idx_max) {
		/* It may not happen, because we checked value with
		 * range first step.
		 */
		return NOT_FOUND_INDEX;
	}
	return (__force u8)idx;
}

static u8 bq25898_lookup_to_idx(u32 value, const struct bq25898_lookup *lookup)
{
	u8		idx;
	const u32	*tbl;
	size_t		size;

	tbl = lookup->tbl;

	if (value < tbl[0])
		return NOT_FOUND_INDEX;

	size = lookup->size;

	for (idx = 1;
		(idx < size) && (tbl[idx] <= value);
	     idx++)
		;
	return idx - 1;
}

static u8 bq25898_xy_to_idx(u32 value, const struct bq25898_xy *xy)
{
	u8				idx;
	const struct bq25898_xy_entry	*tbl;
	size_t				size;

	tbl =  xy->tbl;
	size = xy->size;

	idx = 0;
	while (idx < size) {
		if (tbl[idx].x == value)
			return tbl[idx].y;

		idx++;
	}

	return NOT_FOUND_INDEX;
}


static u8 bq25898_trans_to_idx(u32 value, enum bq25898_table_ids id)
{
	const struct bq25898_trans_entry	*tbl;

	if (id >= TBL_NUMS) {
		pr_err("%s.id: Internal error, invalid table id. "
			"value=%u, id=%d\n",
		__func__, (unsigned)value, id
		);
		return NOT_FOUND_INDEX;
	}

	tbl = &(bq25898_trans_tables[id]);
	switch (tbl->type) {
	case TBL_RANGE:
		return bq25898_range_to_idx(value, &(tbl->table.rt));
		break;
	case TBL_LOOKUP:
		return bq25898_lookup_to_idx(value, &(tbl->table.lt));
		break;
	case TBL_XY:
		return bq25898_xy_to_idx(value, &(tbl->table.xy));
		break;
	default:
		/* do nothing, follows internal error. */
		break;
	}
	pr_err("%s.type: Internal error, invalid table id. "
		"value=%u, id=%d\n",
		__func__, (unsigned)value, id
	);
	return NOT_FOUND_INDEX;
}

static u32 bq25898_range_to_val(u8 idx, const struct bq25898_range *rtbl)
{
	u32	val;

	val = rtbl->min + idx * rtbl->step;
	if (val > rtbl->max) {
		val = rtbl->max;
		pr_err("%s: Internal error, too large idx. "
			"idx=0x%x, val=%u\n",
			__func__, (unsigned)idx, (unsigned)val
		);
	}
	return val;
}

static u32 bq25898_lookup_to_val(u8 idx, const struct bq25898_lookup *lookup)
{
	u32		val;
	const u32	*tbl;

	tbl = lookup->tbl;

	if (idx >= lookup->size) {
		val = tbl[lookup->size - 1];
		pr_err("%s: Internal error, too large idx. "
			"idx=0x%x, val=%u\n",
			__func__, (unsigned)idx, (unsigned)val
		);
		return val;
	}
	val = tbl[idx];
	return val;
}

static u32 bq25898_xy_to_val(u8 idx, const struct bq25898_xy *xy)
{
	u32				val;
	const struct bq25898_xy_entry	*tbl;

	size_t				i;
	size_t				size;

	tbl =  xy->tbl;
	size = xy->size;

	if (idx >= size) {
		val = tbl[size - 1].x;
		pr_err("%s: Internal error, too large idx. "
			"idx=0x%x, val=%u\n",
			__func__, (unsigned)idx, (unsigned)val
		);
		return val;
	}
	i = 0;
	while (i < size) {
		if (tbl[i].y == idx)
			return tbl[i].x;
		i++;
	}
	return ~(u32)0;
}

static u32 bq25898_trans_to_val(u8 idx, enum bq25898_table_ids id)
{
	const struct bq25898_trans_entry	*tbl;

	if (id >= TBL_NUMS) {
		pr_err("%s.id: Internal error, invalid table id. "
			"idx=0x%x, id=%d\n",
		__func__, (unsigned)idx, id
		);
		return ~(u32)0;
	}

	tbl = &(bq25898_trans_tables[id]);

	switch (tbl->type) {
	case TBL_RANGE:
		return bq25898_range_to_val(idx, &(tbl->table.rt));
		break;
	case TBL_LOOKUP:
		return bq25898_lookup_to_val(idx, &(tbl->table.lt));
		break;
	case TBL_XY:
		return bq25898_xy_to_val(idx, &(tbl->table.xy));
		break;
	default:
		/* do nothing, follows internal error. */
		break;
	}
	pr_err("%s.type: Internal error, invalid table id. "
		"idx=0x%x, id=%d\n",
		__func__, (unsigned)idx, id
	);
	return ~(u32)0;
}

#define	BQ25898_TSPCT_INVALID_TEMP	(32767)
/* Note define HOT_TEMP as EMERGENCY temperature.
 * Initiate hot shutdown sequence.
 * Choose exists maximum temperature value mapped from forall TSPCT.
 */
#define	BQ25898_TSPCT_WARM_TEMP		  (500) /*  50.0 Celsius */
#define	BQ25898_TSPCT_COOL_TEMP		   (50) /*   5.0 Celsius */

static int bq25898_tspct_to_tx10(struct bq25898_device *bq, u8 tspct)
{	enum	bq25898_tspct_mode	mode;
	int	temp;

	if (tspct > F_TSPCT_MAX) {
		/* Invalid register value. */
		dev_err(bq->dev, "Invalid TSPCT value. tspct=0x%.2x\n",
			tspct
		);
		return BQ25898_TSPCT_INVALID_TEMP;
	}

	mode = bq->state.tspct_mode_current;
	if (mode >= TSPCT_MODE_ALL) {
		/* Invalid mode. */
		dev_err(bq->dev, "Invalid TSPCT mode. mode=%d\n",
			(int)mode
		);
		return BQ25898_TSPCT_INVALID_TEMP;
	}
	temp = bq->ntc_table[mode][tspct];
	if (temp == BQ25898_TSPCT_INVALID_TEMP) {
		/* Unexpected temperature. */
		dev_warn(bq->dev, "Odd temperature. temp=%d, tspct=0x%.2x, mode=%d\n",
			temp, tspct, (int)mode
		);
	}
	return temp;
}

static int bq25898_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct bq25898_device *bq;
	int ret;
	uint8_t		vbus;
	uint8_t		chrg;
	uint8_t		pg;
	uint8_t		watchdog_fault;
	uint8_t		boost_fault;
	uint8_t		chrg_fault;
	uint8_t		bat_fault;
	uint8_t		ntc_fault;

	uint32_t	real_val;
	int		temp_val;
	int		temp_false_hot;
	int		temp_cold;
	int		temp_current;

	if (psy == NULL) {
		/* Proxy call. */
		/* Should be called from proxy, lock bq25898_proxy. */
		bq = bq25898_proxy.bq;
		if (bq == NULL) {
			/* Proxy is not ready. */
			return -ENODEV;
		}
	} else {
		/* Power supply call */
		bq = power_supply_get_drvdata(psy);
	}

	down(&(bq->sem_this));
	vbus =  bq->state.vbus_current;
	chrg =  bq->state.chrg_current;
	pg =    bq->state.pg_current;

	watchdog_fault = bq->state.watchdog_fault_current;
	boost_fault = bq->state.boost_fault_current;
	chrg_fault =  bq->state.chrg_fault_current;
	bat_fault =   bq->state.bat_fault_current;
	ntc_fault =   bq->state.ntc_fault_current;
	up(&(bq->sem_this));

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (pg == 0) {
			/* No power or weak power */
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			return 0;
		}
		switch (vbus) {
		case REG0B_VBUS_STAT_OTG:
			/* Boost mode */
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			return 0;
		default:
			/* Charging or Discharging. */
			break;
		}
		switch (chrg_fault) {
		case REG0C_CHRG_FAULT_INPUT_FAULT:
		case REG0C_CHRG_FAULT_THRMAL_SHUTDOWN:
		case REG0C_CHRG_FAULT_TIMER_EXPIRE:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			return 0;
		default:
			/* Need more checks */
			break;
		}
		/* @note We may need check more FAULT status. */
		switch (chrg) {
		case REG0B_CHRG_STAT_NOT_CHARGING:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			return 0;
		case REG0B_CHRG_STAT_PRE_CHARGE:
		case REG0B_CHRG_STAT_FAST_CHARGE:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			return 0;
		case REG0B_CHRG_STAT_CHARGE_DONE:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			return 0;
		default:
			/* May not come here. */
			break;
		}
		/* May not come here. */
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ25898_MANUFACTURER;
		return 0;

	case POWER_SUPPLY_PROP_ONLINE:
		if (pg == 0) {
			val->intval = 0;
			return 0;
		}
		switch (vbus) {
		case REG0B_VBUS_STAT_OTG:
			val->intval = 0;
			return 0;
		default:
			break;
		}
		val->intval = 1;
		return 0;

	case POWER_SUPPLY_PROP_HEALTH:
		if (watchdog_fault != 0) {
			val->intval = POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;
			return 0;
		}
		if (bat_fault != 0) {
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			return 0;
		}
		if (boost_fault != 0) {
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			return 0;
		}
		switch (chrg_fault) {
		case REG0C_CHRG_FAULT_NORMAL:
			break;
		case REG0C_CHRG_FAULT_INPUT_FAULT:
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			return 0;
		case REG0C_CHRG_FAULT_THRMAL_SHUTDOWN:
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			return 0;
		case REG0C_CHRG_FAULT_TIMER_EXPIRE:
			val->intval = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
			return 0;
		default:
			/* May not come here. */
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			return 0;
		}

		switch (ntc_fault) {
		case REG0C_NTC_FAULT_TS_COLD:
			val->intval = POWER_SUPPLY_HEALTH_COLD;
			return 0;
		case REG0C_NTC_FAULT_TS_HOT:
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			return 0;
		default:
			/* Translate WARM and COOL to GOOD */
			break;
		}
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		/* Return Fast Charge mode current setting. */
		down(&(bq->sem_dev));
		ret = bq25898_field_read(bq, F_ICHG);
		up(&(bq->sem_dev));
		if (ret < 0) {
			val->intval = 0;
			return ret;
		}
		real_val = bq25898_trans_to_val(ret, TBL_ICHG);
		val->intval = real_val;
		return 0;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		/* Return MAX Fast Charge mode current setting. */
		val->intval = bq25898_trans_tables[TBL_ICHG].table.rt.max;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		/* Return configured VREG value. */
		down(&(bq->sem_dev));
		ret = bq25898_field_read(bq, F_VREG);
		up(&(bq->sem_dev));
		if (ret < 0) {
			val->intval = 0;
			return ret;
		}
		real_val = bq25898_trans_to_val(ret, TBL_VREG);
		val->intval = real_val;
		return 0;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		/* Return MAX VREG value. */
		val->intval = bq25898_trans_tables[TBL_VREG].table.rt.max;
		return 0;

	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		/* Return configured ITERM value. */
		down(&(bq->sem_dev));
		ret = bq25898_field_read(bq, F_ITERM);
		up(&(bq->sem_dev));
		if (ret < 0) {
			val->intval = 0;
			return ret;
		}
		real_val = bq25898_trans_to_val(ret, TBL_ITERM);
		val->intval = real_val;
		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* Return current VBUS value. */
		down(&(bq->sem_dev));
		ret = bq25898_field_read(bq, F_VBUSV);
		up(&(bq->sem_dev));
		if (ret < 0) {
			val->intval = 0;
			return ret;
		}
		real_val = bq25898_trans_to_val(ret, TBL_VBUSV);
		val->intval = real_val;
		return 0;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		/* Return current IINLIM value. */
		down(&(bq->sem_dev));
		ret = bq25898_field_read(bq, F_IINLIM);
		up(&(bq->sem_dev));
		if (ret < 0) {
			val->intval = 0;
			return ret;
		}
		val->intval = bq25898_trans_to_val(ret, TBL_IINLIM);
		return 0;

	case  POWER_SUPPLY_PROP_TEMP:
		if (!bq25898_state_tspct_valid(bq)) {
			/* TSPCT isn't ready. */
			return -EBUSY;
		}
		down(&(bq->sem_this));
		down(&(bq->sem_dev));
		/* Follow current values in atomic. */
		ntc_fault =    bq->state.ntc_fault_current;
		temp_current = bq->state.temp_current;
		temp_false_hot = bq25898_tspct_to_tx10(bq, F_TSPCT_MIN);
		temp_cold = bq25898_tspct_to_tx10(bq, F_TSPCT_MAX);
		ret = bq25898_field_read(bq, F_TSPCT);
		up(&(bq->sem_dev));
		if (ret < 0) {
			val->intval = 0;
			up(&(bq->sem_this));
			return ret;
		}
		temp_val = bq25898_tspct_to_tx10(bq, ret);
		if (temp_val < temp_false_hot) {
			/* Valid temperature. */
			val->intval = temp_val;
			up(&(bq->sem_this));
			return 0;
		}
		/* Invalid temperature. */
		if ((!(bq->temp_poll)) &&
		     (!test_bit(BQ25898_DEVICE_FLAGS_SUSPENDING,
			&(bq->flags)))
		) {
			/* "Not temperature polling mode" and
			 * "Not suspending"
			 */
			/* Start temperature polling. */
			set_bit(CHARGER_EVENT_TEMP_POLL, &(bq->charger_event));
			bq25898_charger_thread_wake(bq);
		}
		up(&(bq->sem_this));

		if (temp_current < temp_false_hot) {
			/* Read valid temperature in
			 * charger_thread.
			 */
			val->intval = temp_current;
			dev_notice(bq->dev,
				"Use temperature in thread. temp=%d\n",
				temp_current
			);
			return 0;
		}

		/* We don't know a valid temperature. */
		switch (ntc_fault) {
		case REG0C_NTC_FAULT_TS_HOT:
			val->intval = temp_false_hot;
			dev_warn(bq->dev,
				"Use hot temperature. temp=%d\n",
				val->intval
			);
			return 0;
		case REG0C_NTC_FAULT_TS_WARM:
			val->intval = BQ25898_TSPCT_WARM_TEMP;
			dev_warn(bq->dev,
				"Use warm temperature. temp=%d\n",
				val->intval
			);
			return 0;
		case REG0C_NTC_FAULT_TS_COOL:
			val->intval = BQ25898_TSPCT_COOL_TEMP;
			dev_warn(bq->dev,
				"Use cool temperature. temp=%d\n",
				val->intval
			);
			return 0;
		case REG0C_NTC_FAULT_TS_COLD:
			val->intval = temp_cold;
			dev_warn(bq->dev,
				"Use cold temperature. temp=%d\n",
				val->intval
			);
			return 0;
		default:
			/* May not come here. */
			break;
		}
		val->intval = temp_false_hot;
		dev_warn(bq->dev, "No valid temperature. temp=%d\n",
			val->intval
		);
		return -EBUSY;
	default:
		return -EINVAL;
	}
	return 0;
}

static int bq25898_state_read_evented_unlocked(struct bq25898_device *bq)
{	int		ret = 0;
	int		reg0b;
	int		reg0c;
	struct bq25898_state	*state = &(bq->state);

	reg0b = bq25898_field_read(bq, F_REG0B);
	if (reg0b < 0) {
		ret = reg0b;
		goto out;
	}

	reg0c = bq25898_field_read(bq, F_REG0C);
	if (reg0c < 0) {
		ret = reg0c;
		goto out;
	}

	state->reg0b_evented = reg0b;
	state->reg0c_evented = reg0c;

	state->vbus_evented = reg0b & REG0B_VBUS_STAT_MASK;
	state->chrg_evented = reg0b & REG0B_CHRG_STAT_MASK;
	state->pg_evented =   reg0b & REG0B_PG_STAT_POWER_GOOD;
	state->vsys_evented = reg0b & REG0B_VSYS_STAT_REG_VSYSMIN;

	state->watchdog_fault_evented = reg0c & REG0C_WATCHDOG_FAULT;
	state->boost_fault_evented = reg0c & REG0C_BOOST_FAULT;
	state->chrg_fault_evented =  reg0c & REG0C_CHRG_FAULT_MASK;
	state->bat_fault_evented =   reg0c & REG0C_BAT_FAULT_OVP;
	state->ntc_fault_evented =   reg0c & REG0C_NTC_FAULT_MASK;

	DEV_INFO_REGS(bq->dev, "State at IRQ. REG0B:REG0C=0x%.2x:0x%.2x\n",
		state->reg0b_evented, state->reg0c_evented
	);
out:
	return ret;
}

static int __maybe_unused bq25898_state_read_evented(
	struct bq25898_device *bq
)
{	int		ret = 0;

	down(&(bq->sem_this));
	down(&(bq->sem_dev));

	ret = bq25898_state_read_evented_unlocked(bq);

	up(&(bq->sem_dev));
	up(&(bq->sem_this));
	return ret;
}


static int bq25898_state_read_current_unlocked(struct bq25898_device *bq)
{	int		ret = 0;
	int		reg0b;
	int		reg0c;
	int		tspct;
	int		temp_current;
	int		temp_false_hot;
	int		batv;

	enum bq25898_tspct_mode	tspct_mode;

	struct bq25898_state	*state = &(bq->state);

	reg0b = bq25898_field_read(bq, F_REG0B);
	if (reg0b < 0) {
		ret = reg0b;
		goto out;
	}

	reg0c = bq25898_field_read(bq, F_REG0C);
	if (reg0c < 0) {
		ret = reg0c;
		goto out;
	}

	tspct = bq25898_field_read(bq, F_TSPCT);
	if (tspct < 0) {
		ret = tspct;
		goto out;
	}

	batv = bq25898_field_read(bq, F_BATV);
	if (batv < 0) {
		ret = batv;
		goto out;
	}

	state->reg0b_prev = state->reg0b_current;
	state->reg0c_prev = state->reg0c_current;

	bq->state.pg_previous = bq->state.pg_current;

	state->reg0b_current = reg0b;
	state->reg0c_current = reg0c;

	state->vbus_current = reg0b & REG0B_VBUS_STAT_MASK;
	state->chrg_current = reg0b & REG0B_CHRG_STAT_MASK;
	state->pg_current =   reg0b & REG0B_PG_STAT_POWER_GOOD;
	state->vsys_current = reg0b & REG0B_VSYS_STAT_REG_VSYSMIN;

	state->watchdog_fault_current = reg0c & REG0C_WATCHDOG_FAULT;
	state->boost_fault_current = reg0c & REG0C_BOOST_FAULT;
	state->chrg_fault_current = reg0c & REG0C_CHRG_FAULT_MASK;
	state->bat_fault_current =  reg0c & REG0C_BAT_FAULT_OVP;
	state->ntc_fault_current =  reg0c & REG0C_NTC_FAULT_MASK;

	tspct_mode = TSPCT_SELF_STAND;
	if (state->pg_current != 0) {
		/* power good, come on vbus. */
		switch (state->vbus_current) {
		case REG0B_VBUS_STAT_OTG:
			/* VBUS boosting, self stand. */
			break;
		default:
			tspct_mode = TSPCT_VBUS_INCOMING;
			break;
		}
	}
	state->tspct_mode_current = tspct_mode;
	temp_current = bq25898_tspct_to_tx10(bq, tspct);
	temp_false_hot = bq25898_tspct_to_tx10(bq, F_TSPCT_MIN);
	if (temp_current < temp_false_hot) {
		/* Read valid temperature. */
		state->temp_prev = state->temp_current;
		state->temp_current = temp_current;
	}
	state->temp_current_raw = temp_current;
	state->batv_current = bq25898_trans_to_val(batv, TBL_BATV);

	if ((state->reg0b_prev != state->reg0b_current) ||
	    (state->reg0c_prev != state->reg0c_current)) {
		DEV_INFO_REGS(bq->dev,
			"State current. REG0B:REG0C=0x%.2x:0x%.2x\n",
			state->reg0b_current, state->reg0c_current
		);
	}
out:
	return ret;
}

static int __maybe_unused bq25898_state_read_current(
	struct bq25898_device *bq
)
{	int		ret = 0;

	down(&(bq->sem_this));
	down(&(bq->sem_dev));

	ret = bq25898_state_read_current_unlocked(bq);

	up(&(bq->sem_dev));
	up(&(bq->sem_this));
	return ret;

}

static int bq25898_state_read_irq(struct bq25898_device *bq)
{	int		ret = 0;

	down(&(bq->sem_this));
	down(&(bq->sem_dev));

	ret = bq25898_state_read_evented_unlocked(bq);
	if (ret < 0)
		goto out;

	ret = bq25898_state_read_current_unlocked(bq);
	if (ret < 0)
		goto out;

out:
	up(&(bq->sem_dev));
	up(&(bq->sem_this));
	return ret;
}

static bool bq25898_state_tspct_valid_unlocked(struct bq25898_device *bq)
{	bool	result = false;

	if (bq->state.pg_current != 0) {
		/* "Power Good" */
		/* We can read valid TSPCT. */
		result = true;
		goto out;
	}

	switch (bq->state.vbus_current) {
	case REG0B_VBUS_STAT_OTG:
		/* Boost Converter feeds power to TS LDO. */
		/* We can read valid TSPCT. */
		result = true;
		goto out;
	default:
		break;
	}

	/* "VBUS is not powered" or "Not boosting" */
	if (bq->state.vsys_current == 0) {
		/* VBAT > VSYSMIN */
		/* We can read valid TSPCT. */
		result = true;
	}
out:
	return result;
}

static bool bq25898_state_tspct_valid(struct bq25898_device *bq)
{	bool	result;

	down(&(bq->sem_this));
	result = bq25898_state_tspct_valid_unlocked(bq);
	up(&(bq->sem_this));

	return result;
}

static bool bq25898_state_fault_irq(struct bq25898_device *bq)
{	bool	result = false;

	struct	bq25898_state	*state = &(bq->state);

	down(&(bq->sem_this));

	if (state->watchdog_fault_evented) {
		result = true;
		goto out;
	}

	if (state->boost_fault_evented) {
		result = true;
		goto out;
	}

	if (state->chrg_fault_evented != REG0C_CHRG_FAULT_NORMAL) {
		result = true;
		goto out;
	}

	if (state->bat_fault_evented != 0) {
		result = true;
		goto out;
	}

	/* We doesn't treat temperature fault here.
	 * Treat temperature fault around temp_trip
	 */
out:
	up(&(bq->sem_this));
	return result;
}

static bool bq25898_state_fault_current(struct bq25898_device *bq)
{	bool	result = false;

	struct	bq25898_state	*state = &(bq->state);

	down(&(bq->sem_this));

	if (state->watchdog_fault_current) {
		result = true;
		goto out;
	}

	/* There is no need to polling
	 * boost fault condition.
	 * We will clear VBUS boosting trip when
	 * disconnecting device (USB sink role device).
	 */

	if (state->chrg_fault_current != REG0C_CHRG_FAULT_NORMAL) {
		result = true;
		goto out;
	}

	if (state->bat_fault_current != 0) {
		result = true;
		goto out;
	}

	/* We doesn't treat temperature fault here.
	 * Treat temperature fault around temp_trip.
	 */
out:
	up(&(bq->sem_this));
	return result;
}


static bool __maybe_unused bq25898_state_fault_temp_evented(
	struct bq25898_device *bq)
{	bool	result = false;
	struct	bq25898_state	*state = &(bq->state);

	down(&(bq->sem_this));

	switch (state->ntc_fault_evented) {
	case REG0C_NTC_FAULT_TS_COLD:
	case REG0C_NTC_FAULT_TS_COOL:
	case REG0C_NTC_FAULT_TS_WARM:
	case REG0C_NTC_FAULT_TS_HOT:
		result = true;
		goto out;
		break;
	default:
		break;
	}
out:
	up(&(bq->sem_this));
	return result;
}

/* Check if completely full
 * This condition is related to power supply property
 * POWER_SUPPLY_PROP_STATUS. Especially POWER_SUPPLY_STATUS_FULL.
 * When you change this condition or power supply property,
 * you will make conditional conflicts be clear.
 * @pre Lock sem_this
 */
static bool bq25898_state_suspend_ready_full(
	struct bq25898_device *bq)
{	bool	result = false; /* Not full. */

	uint8_t		vbus;
	uint8_t		chrg;
	uint8_t		pg;
	uint8_t		watchdog_fault;
	uint8_t		boost_fault;
	uint8_t		chrg_fault;
	uint8_t		bat_fault;
	uint8_t		ntc_fault;

	/* note: Lock may not be blocked.  */
	vbus =  bq->state.vbus_current;
	chrg =  bq->state.chrg_current;
	pg =    bq->state.pg_current;

	watchdog_fault = bq->state.watchdog_fault_current;
	boost_fault = bq->state.boost_fault_current;
	chrg_fault =  bq->state.chrg_fault_current;
	bat_fault =   bq->state.bat_fault_current;
	ntc_fault =   bq->state.ntc_fault_current;

	if (pg == 0) {
		/* No power or weak power */
		result = false;
		goto out;
	}

	switch (vbus) {
	case REG0B_VBUS_STAT_OTG:
		/* Boost mode */
		result = false;
		goto out;
	default:
		/* Charging or Discharging. */
		break;
	}
	switch (chrg_fault) {
	case REG0C_CHRG_FAULT_INPUT_FAULT:
	case REG0C_CHRG_FAULT_THRMAL_SHUTDOWN:
	case REG0C_CHRG_FAULT_TIMER_EXPIRE:
		result = false;
		goto out;
	default:
		/* Need more checks */
		break;
	}
	switch (ntc_fault) {
	case REG0C_NTC_FAULT_TS_COLD:
	case REG0C_NTC_FAULT_TS_HOT:
		/* Stop charging. */
		result = false;
		goto out;
	default:
		/* Check more conditions. */
		/* Under WARM or COOL condition,
		 * It charges battery slowly.
		 */
		break;
	}
	/* @note We may need check more FAULT status. */
	switch (chrg) {
	case REG0B_CHRG_STAT_NOT_CHARGING:
		result = false;
		goto out;
	case REG0B_CHRG_STAT_PRE_CHARGE:
	case REG0B_CHRG_STAT_FAST_CHARGE:
		result = false;
		goto out;
	case REG0B_CHRG_STAT_CHARGE_DONE:
		break;
	default:
		/* May not come here. */
		result = false;
		goto out;
	}
	result = true;
out:
	return result;
}


static bool __maybe_unused bq25898_state_fault_temp_current(
	struct bq25898_device *bq)

{	bool	result = false;
	struct	bq25898_state	*state = &(bq->state);

	down(&(bq->sem_this));

	switch (state->ntc_fault_current) {
	case REG0C_NTC_FAULT_TS_COLD:
	case REG0C_NTC_FAULT_TS_COOL:
	case REG0C_NTC_FAULT_TS_WARM:
	case REG0C_NTC_FAULT_TS_HOT:
		result = true;
		goto out;
		break;
	default:
		break;
	}
out:
	up(&(bq->sem_this));
	return result;
}

static void bq25898_irq_work_pre(struct bq25898_device *bq)
{
	if (bq25898_state_fault_irq(bq)) {
		/* Some fault happen, look around, and re-configure. */
		bq->cc_force_config = true;
	}

	/*! @note lock held in regmap. */
	/* @note Setting "BOONST_CONFIG=1" may be ignored
	 * by device, when coming power to VBUS,
	 * charging battery.
	 */
	bq->vbus_boost = VBUS_BOOST_ON;
}

static void bq25898_irq_work_post(struct bq25898_device *bq __maybe_unused)
{
	/*! @note currently nothing to do */
}

/* Event:   CHARGER_EVENT_CHG_USB
 * Updates: chg_usb.type
 */
static void bq25898_chg_usb_catch(struct bq25898_device *bq)
{	bq25898_extcon_chg_usb_get_state(bq);
	DEV_INFO_EVENT(bq->dev, "Catch CHG_USB event. type=%d\n",
		bq->chg_usb.type
	);
}

/* Event:   CHARGER_EVENT_USB_OTG
 * Updates: usb_otg.event
 */
static void bq25898_usb_otg_catch(struct bq25898_device *bq)
{
	unsigned long	event;
	unsigned long	flags;

	spin_lock_irqsave(&(bq->usb_otg.lock), flags);
	event = bq->usb_otg.event = bq->usb_otg.event_cb;
	spin_unlock_irqrestore(&(bq->usb_otg.lock), flags);

	DEV_INFO_EVENT(bq->dev, "USB OTG event. event=%lu\n", event);
	switch (event) {
	case USB_EVENT_VBUS:
		dev_info(bq->dev, "Off VBUS boost.\n");
		/* Connected cable feeds VBUS power,
		 * Off Boosting regulator.
		 * @note hard wired logic controls automatically boost
		 * regulator when OTG_EN=High and OTG_CONFIG=1.
		 * So VBUS power sourceing collision doesn't happen.
		 */
		bq->vbus_boost = VBUS_BOOST_OFF;
		break;

	case USB_EVENT_ID:
		dev_info(bq->dev, "On VBUS boost.\n");
		/* Pull down ID pin, be Host. */
		bq->vbus_boost = VBUS_BOOST_ON;
		break;

	case USB_EVENT_NONE:
		dev_info(bq->dev, "Prepare VBUS boost.\n");
		/* Note prepare VBUS boost mode when nothing connects
		 * to Type-C receptacle.
		 */
		bq->vbus_boost = VBUS_BOOST_ON;
		break;
	}
}

/* Event:   CHARGER_EVENT_USB_HOST
 * Updates: typec.state_host
 */
static int bq25898_usb_host_catch(struct bq25898_device *bq)
{	int	state;
	int	result = 0;

	state = extcon_get_state(bq->typec.edev, EXTCON_USB_HOST);
	if (state < 0) {
		dev_err(bq->dev, "Can not get extcon USB_HOST state. state=%d",
			state
		);
		result = state;
		state = 0;
	}
	bq->typec.state_host = state;
	DEV_INFO_EVENT(bq->dev, "Catch USB_HOST event. state=%d\n", state);
	return result;
}

/* Event:   CHARGER_EVENT_USB_DEV
 * Updates: typec.state_dev
 */
static int bq25898_usb_dev_catch(struct bq25898_device *bq)
{	int	state;
	int	result = 0;

	state = extcon_get_state(bq->typec.edev, EXTCON_USB);
	if (state < 0) {
		dev_err(bq->dev, "Can not get extcon USB state. state=%d",
			state
		);
		result = state;
		state = 0;
	}
	bq->typec.state_dev = state;
	DEV_INFO_EVENT(bq->dev, "Catch USB event. state=%d\n", state);

	return result;
}

/* Event:   CHARGER_EVENT_USB_DEV
 * Sesssion end procedure.
 */
static void bq25898_usb_dev_session_end(struct bq25898_device *bq)
{	/* Turn into "not addressed" (reset) state. */
	bq->usb_config.draw_current = 0;
}


/* Event:   CHARGER_EVENT_CHG_USB_PD
 * Updates: typec.state_chg, typec.prop_chg
 */
static int bq25898_chg_usb_pd_catch(struct bq25898_device *bq)
{	union extcon_property_value	prop_val;
	int				state;
	int				ret;
	int				result = 0;

	state = extcon_get_state(bq->typec.edev, EXTCON_CHG_USB_PD);
	if (state < 0) {
		dev_err(bq->dev, "Can not get extcon USB_HOST state. state=%d",
			state
		);
		result = result ? result : state;
		state = 0;
	}
	prop_val.intval = 0;
	ret = extcon_get_property(bq->typec.edev,
		EXTCON_CHG_USB_PD, EXTCON_PROP_CHG_MIN, &prop_val);
	if (ret != 0) {
		dev_err(bq->dev, "Can not get extcon CHG_USB_PD prop. ret=%d",
			ret
		);
		result = result ? result : ret;
		prop_val.intval = BQ25898_ICX_PD_SINK_STD;
	}

	bq->typec.state_chg = state;
	bq->typec.prop_chg =  (enum bq25898_icx_pd_properties)prop_val.intval;
	DEV_INFO_EVENT(bq->dev, "Catch CHG_USB_PD event. state=%d, prop=%d\n",
		state, prop_val.intval
	);
	return result;
}

#define USB_CONFIG_DRAW_CURRENT_UA_DEF	(500 * 1000)

/* Event:   CHARGER_EVENT_USB_CONFIG
 * Updates: usb_config.state, usb_config.draw_current
 */
static int bq25898_usb_config_catch(struct bq25898_device *bq)
{	union extcon_property_value	prop_val;
	int				state;
	int				ret;
	int				result = 0;

	state = extcon_get_state(bq->usb_config.edev, EXTCON_CHG_USB_SDP);
	if (state < 0) {
		dev_err(bq->dev,
			"Can not get extcon USB config state. state=%d",
			state
		);
		result = result ? result : state;
		state = 0;
	}
	prop_val.intval = 0;
	ret = extcon_get_property(bq->usb_config.edev,
		EXTCON_CHG_USB_SDP, EXTCON_PROP_CHG_MIN, &prop_val);
	if (ret != 0) {
		dev_err(bq->dev,
			"Can not get extcon CHG_USB_SDP prop. ret=%d",
			ret
		);
		result = result ? result : ret;
		prop_val.intval = USB_CONFIG_DRAW_CURRENT_UA_DEF;
	}

	bq->usb_config.state = (__force bool)state;
	bq->usb_config.draw_current = prop_val.intval;
	DEV_INFO_EVENT(bq->dev,
		"Catch USB config event. state=%d, draw_current=%d\n",
		state, prop_val.intval
	);
	return result;
}


/* Event:   CHARGER_EVENT_VBUS_DRAW
 * Updates: rdev_charger_current_cb
 */

static void bq25898_vbus_draw_catch(struct bq25898_device *bq)
{	unsigned long	flags;
	int		charger_current;

	spin_lock_irqsave(&(bq->rdev_lock), flags);
	charger_current = bq->rdev_charger_current_cb;
	bq->rdev_charger_current = charger_current;
	spin_unlock_irqrestore(&(bq->rdev_lock), flags);

	DEV_INFO_EVENT(bq->dev, "Catch vbus draw event. current=%d\n",
		charger_current
	);
}

static void bq25898_cc_new_update(struct bq25898_device *bq,
	enum charge_config_index cc
)
{	if (bq->cc_new < cc) {
		/* Requested higer priority configuration. */
		bq->cc_new = cc;
	}
}

static void bq25898_suspend_pre_work(struct bq25898_device *bq)
{	if (!test_bit(CHARGER_EVENT_SUSPEND, &(bq->charger_event_catch))) {
		/* There is no suspend event. */
		return;
	}
	/* Suspend call. */
	set_bit(BQ25898_DEVICE_FLAGS_SUSPENDING, &(bq->flags));
	bq->resumed_timer = RESUMED_TIMER_STOP;
	down(&(bq->sem_this));
	bq->pg_suspend = bq->state.pg_current;
	bq->batv_suspend = bq->state.batv_current;
	bq->temp_suspend = bq->state.temp_current_raw;
	up(&(bq->sem_this));
}

#define	RESUMED_TIMER_DONE_MS	(4 * 1000)

static void bq25898_resume_pre_work(struct bq25898_device *bq)
{	u64	jiffies_delta;
	u64	jiffies_max;

	if (test_bit(CHARGER_EVENT_RESUME, &(bq->charger_event_catch))) {
		/* Resume call. */
		/* Start Resumed timer, when it keeps resumed state
		 * for a while, we back IINLIM settings normal charging.
		 */
		bq->resumed_timer = RESUMED_TIMER_RUN;
		bq->jiffies_resume = bq->jiffies_now;
	}
	switch (bq->resumed_timer) {
	case RESUMED_TIMER_RUN:
		jiffies_delta =
			bq->jiffies_now - bq->jiffies_resume;
		jiffies_max = msecs_to_jiffies(
			RESUMED_TIMER_DONE_MS);
		if (jiffies_delta >= jiffies_max) {
			/* Keep resumed state for a while. */
			bq->resumed_timer = RESUMED_TIMER_DONE;
		}
		break;
	case RESUMED_TIMER_STOP:
	case RESUMED_TIMER_DONE:
		/* Keep this state. */
		break;
	default:
		/* Unknown state. */
		bq->resumed_timer = RESUMED_TIMER_DONE;
		break;
	}
}

static void bq25898_resume_post_work(struct bq25898_device *bq)
{	if (!test_bit(CHARGER_EVENT_RESUME, &(bq->charger_event_catch))) {
		/* There is no resume event. */
		return;
	}
	clear_bit(BQ25898_DEVICE_FLAGS_SUSPENDING, &(bq->flags));

	down(&(bq->sem_this));

	if ((bq->batv_suspend != bq->state.batv_current) ||
	    (bq->temp_suspend != bq->state.temp_current_raw)
	) {
		/* Battery status changed. */
		bq->ps_prop_notify = true;
	}
	up(&(bq->sem_this));
}

#define	VBUS_UNCONFIGURED_CURRENT_UA	(100 * 1000)

static void bq25898_vbus_draw_work(struct bq25898_device *bq)
{	int	draw_uA;

	draw_uA = bq->rdev_charger_current;

	if (draw_uA <= VBUS_UNCONFIGURED_CURRENT_UA) {
		/* Connected, Addressed, and Unconfigured. */
		bq25898_cc_new_update(bq, CHARGE_CONFIG_UNCONFIG);
		return;
	}
	/* Configured State. */
	bq25898_cc_new_update(bq, CHARGE_CONFIG_CONFIG);
}

static void bq25898_usb_config_work(struct bq25898_device *bq)
{	int	draw_uA;

	if (bq->typec.state_dev == false) {
		/* Not active device session. */
		return;
	}

	/* Device session. */
	draw_uA = bq->usb_config.draw_current;

	if (draw_uA <= VBUS_UNCONFIGURED_CURRENT_UA) {
		/* Connected, Addressed, and Unconfigured. */
		bq25898_cc_new_update(bq, CHARGE_CONFIG_UNCONFIG);
		return;
	}
	/* Configured State. */
	bq25898_cc_new_update(bq, CHARGE_CONFIG_CONFIG);
}

static void bq25898_dead_battery_work(struct bq25898_device *bq)
{	if (bq->dead_battery) {
		if (bq->state.batv_current >= bq->dead_battery_uv) {
			/* Exit dead battery mode. */
			bq->dead_battery = false;
			return;
		}
		/* Dead Battey boot. */
		bq25898_cc_new_update(bq, CHARGE_CONFIG_DEAD_BATTERY);
	}
}

static void bq25898_diag_vbus_work(struct bq25898_device *bq)
{	uint8_t		en_hiz;
	uint8_t		iinlim;
	u32		uA;

	down(&(bq->sem_this));
	if (bq->diag_vbus_sink_current < DIAG_VBUS_SINK_CURRENT_ENABLE) {
		/* Normal operation. */
		goto out;
	}

	/* Diag VBUS mode. */

	if (bq->state.pg_current == 0) {
		if (bq->state.pg_previous ==
			REG0B_PG_STAT_POWER_GOOD) {
			/* VBUS power down. */
			dev_notice(bq->dev,
				"VBUS turn off, exit diag VBUS mode.\n"
			);
			bq->diag_vbus_sink_current =
				DIAG_VBUS_SINK_CURRENT_NORMAL;
			goto out;
		}
	}
	if (test_bit(CHARGER_EVENT_SYNC_DIAG, &(bq->charger_event_catch))) {
		/* Diag Request */
		bq->cc_force_config = true;
	}

	/* Prepare charge configuration. */
	en_hiz =
	  (bq->diag_vbus_sink_current == DIAG_VBUS_SINK_CURRENT_HIZ);
	bq->charge_configs[CHARGE_CONFIG_DIAG_VBUS].en_hiz = en_hiz;
	uA = (__force u32)(bq->diag_vbus_sink_current) * 1000;
	iinlim = bq25898_trans_to_idx(uA, TBL_IINLIM);
	if (iinlim == INVALID_REG_FIELD_VALUE) {
		/* Turns invalid value to zero. */
		dev_notice(bq->dev,
			"Invalid diag_vbus_sink_current, set IINLIM=0x0.\n"
		);
		iinlim = 0;
	}
	bq->charge_configs[CHARGE_CONFIG_DIAG_VBUS].iinlim = iinlim;
	bq25898_cc_new_update(bq, CHARGE_CONFIG_DIAG_VBUS);
out:
	up(&(bq->sem_this));
}


static void bq25898_chg_usb_work(struct bq25898_device *bq)
{	int	connected_id;
	enum	charge_config_index cc_new = CHARGE_CONFIG_NONE;

	connected_id = bq->chg_usb.type;

	switch (connected_id) {
	case EXTCON_CHG_USB_SDP:
		cc_new = CHARGE_CONFIG_USB_SDP;
		break;
	case EXTCON_CHG_USB_ACA:
		cc_new = CHARGE_CONFIG_USB_APPLE;
		break;
	case EXTCON_CHG_USB_CDP:
		cc_new = CHARGE_CONFIG_USB_CDP;
		break;
	case EXTCON_CHG_USB_DCP:
		cc_new = CHARGE_CONFIG_USB_DCP;
		break;
	case EXTCON_CHG_USB_SLOW:
		cc_new = CHARGE_CONFIG_USB_MISC;
		break;
	case EXTCON_CHG_USB_FAST:
		cc_new = CHARGE_CONFIG_USB_DCP;
		break;
	default:
		return;
	}
	bq25898_cc_new_update(bq, cc_new);
}

static void bq25898_chg_usb_pd_work(struct bq25898_device *bq)
{	enum	bq25898_icx_pd_properties	prop;
	enum	charge_config_index cc_new = CHARGE_CONFIG_NONE;

	prop = (bq->typec.prop_chg) & BQ25898_ICX_PD_POWER_MASK;

	switch (prop) {
	case BQ25898_ICX_PD_UNKNOWN:
		return;
	case BQ25898_ICX_PD_SINK_STD:
		cc_new = CHARGE_CONFIG_USB_SDP;
		break;
	case BQ25898_ICX_PD_SINK_1R5A:
		cc_new = CHARGE_CONFIG_TYPEC_1500;
		break;
	case BQ25898_ICX_PD_SINK_3R0A:
		cc_new = CHARGE_CONFIG_TYPEC_3000;
		break;
	default:
		return;
	}
	bq25898_cc_new_update(bq, cc_new);
}

static void bq25898_fault_work(struct bq25898_device *bq)
{
	if (bq->state.watchdog_fault_current) {
		/* Now, watchdog state. */
		bq->watchdog_clear = true;
	}

	switch (bq->state.chrg_fault_current) {
	case REG0C_CHRG_FAULT_INPUT_FAULT:
	case REG0C_CHRG_FAULT_THRMAL_SHUTDOWN:
		bq25898_cc_new_update(bq, CHARGE_CONFIG_NO_POWER);
		break;
	case REG0C_CHRG_FAULT_TIMER_EXPIRE:
		bq25898_cc_new_update(bq, CHARGE_CONFIG_CHRG_FAULT);
		break;
	default:
		break;
	}

	if (bq->state.bat_fault_current != 0) {
		/* VBAT > VBATOVP */
		bq25898_cc_new_update(bq, CHARGE_CONFIG_BAT_FAULT);
	}

	if (bq->temp_trip) {
		/* Tripped NTC temperature. */
		/* Notify temperature always.
		 * BQ25898 may stop and start charging
		 * at HOT, WARM, COOL, and COLD condition
		 * without interrupt.
		 */
		bq->ps_prop_notify = true;
		if (!bq25898_state_fault_temp_current(bq)) {
			/* NTC temperature become safe to charge. */
			bq->temp_trip = false;
			/* May be change status after a moment. */
			bq->status_update = true;
			dev_notice(bq->dev, "Recover temp trip. ntc_fault=0x%x, temp_x10=%d\n",
				bq->state.ntc_fault_current,
				bq->state.temp_current_raw
			);
		}
	} else {
		/* Not tripped NTC temperature. */
		if (bq25898_state_fault_temp_current(bq)) {
			/* NTC temperature become unsafe to charge */
			bq->temp_trip = true;
			bq->ps_prop_notify = true;
			/* May be change status after a moment. */
			bq->status_update = true;
			dev_notice(bq->dev, "Enter temp trip. ntc_fault=0x%x, temp_x10=%d\n",
				bq->state.ntc_fault_current,
				bq->state.temp_current_raw
			);
			/* We don't use temperature trip configuration.
			 * We continue configure VBUS input
			 * current IINLIM according to VBUS source
			 * even if temperature fault condition.
			 * BQ25898 automatically handle HOT and COLD
			 * condition.
			 */
		}
		/* @note when VBUS is not come in, and temperature is
		 * COLD, COOL, WARM or HOT. BQ25898 raise interrupt
		 * periodically. BQ25898's NTC_FAULT status flaps
		 * between Normal and Not-Normal(C/C/W/H).
		 */
	}
}

static void bq25898_temp_poll_catch(struct bq25898_device *bq)
{
	down(&(bq->sem_this));
	if (bq->temp_poll) {
		/* Already temp_polling mode. */
		goto out;
	}
	bq->jiffies_temp_poll = bq->jiffies_now;
	bq->temp_poll = true;
out:
	up(&(bq->sem_this));
}

#define BQ25898_TEMP_POLL_DURATION_MS	(10000)

static void bq25898_temp_poll_work(struct bq25898_device *bq)
{
	u64	jiffies_delta;
	u64	max;
	int	temp_false_hot;

	down(&(bq->sem_this));
	temp_false_hot = bq25898_tspct_to_tx10(bq, F_TSPCT_MIN);

	if (!(bq->temp_poll)) {
		/* Not temperature polling mode. */
		if (!bq25898_state_tspct_valid_unlocked(bq)) {
			/* TSPCT isn't ready. */
			goto out;
		}
		if (bq->state.temp_current_raw >= temp_false_hot) {
			/* Currently we read invalid temperature. */
			bq->jiffies_temp_poll = bq->jiffies_now;
			bq->temp_poll = true;
		}
		goto out;
	}

	/* temperature polling mode. */
	if (bq->state.temp_current_raw < temp_false_hot) {
		/* NTC recovers, notify power supply property. */
		bq->ps_prop_notify = true;
		bq->temp_poll = false;
		goto out;
	}

	jiffies_delta = bq->jiffies_now - bq->jiffies_temp_poll;
	max = msecs_to_jiffies(BQ25898_TEMP_POLL_DURATION_MS);

	if (jiffies_delta > max) {
		/* NTC doesn't recover. */
		bq->temp_poll = false;
		/* NTC shows really hot, or may be broken. */
		dev_warn(bq->dev, "NTC may be really hot.\n");
		bq->state.temp_prev = bq->state.temp_current;
		bq->state.temp_current = bq->state.temp_current_raw;
		goto out;
	}
out:
	up(&(bq->sem_this));
}

static void bq25898_soft_warm_work(struct bq25898_device *bq)
{
	int	temp_current;
	int	low;
	int	high;
	int	soft_warm_state_prev;

	down(&(bq->sem_this));
	temp_current = bq->state.temp_current;
	low =  bq->soft_warm_temp_low;
	high = bq->soft_warm_temp_high;

	if (low > high) {
		dev_warn(bq->dev, "soft_warm_temp_low is grater than soft_warm_temp_high. low=%d, high=%d\n",
			low, high
		);
		goto out;
	}

	soft_warm_state_prev = bq->soft_warm_state;

	switch (bq->soft_warm_state) {
	case SOFT_WARM_STATE_LOW:
		/* Currently low temperature state. */
		if (temp_current >= high) {
			/* Transit to high temperature. */
			bq->soft_warm_state = SOFT_WARM_STATE_HIGH;
		}
		break;
	case SOFT_WARM_STATE_HIGH:
		/* Currently high temperature state. */
		if (temp_current <= low) {
			/* Transit to low temperature. */
			bq->soft_warm_state = SOFT_WARM_STATE_LOW;
		}
		break;
	default:
		/* Unexpected state, back to stable value. */
		bq->soft_warm_state = SOFT_WARM_STATE_LOW;
		break;
	}

	if (temp_current <= low) {
		/* Absolutely low temp. */
		bq->soft_warm_state = SOFT_WARM_STATE_LOW;
	}

	if (temp_current >= high) {
		/* Absolutely high temp. */
		bq->soft_warm_state = SOFT_WARM_STATE_HIGH;
	}

	if (soft_warm_state_prev != bq->soft_warm_state) {
		/* Update soft warm state. */
		dev_info(bq->dev, "Soft Warm update. temp_current=%d, prev=%d, state=%d\n",
			temp_current, soft_warm_state_prev, bq->soft_warm_state
		);
		bq->cc_force_config = true;
	}
out:
	up(&(bq->sem_this));
}

static void bq25898_soft_full_work(struct bq25898_device *bq)
{
	int	soft_full_state;
	bool	suspend_ready_full;
	u64	jiffies_delta;
	u64	jiffies_max;


	down(&(bq->sem_this));
	switch (bq->chg_usb.type) {
	case EXTCON_CHG_USB_ACA: /* A-type USB AC Charger. */
	case EXTCON_CHG_USB_DCP: /* DCP USB AC Charger. */
	case EXTCON_CHG_USB_SLOW: /* MISC USB AC Charger. */
	case EXTCON_CHG_USB_FAST: /* A kind of USB AC Charger. */
		/* Continue. */
		break;
	default:
		/* It may start communication on data line. */
		bq->soft_full_state = SOFT_FULL_NOTYET;
		goto out;
	}

	suspend_ready_full = bq25898_state_suspend_ready_full(bq);
	soft_full_state = bq->soft_full_state;

	if (!suspend_ready_full) {
		/* Not full. */
		if (soft_full_state != SOFT_FULL_NOTYET) {
			/* Break full state. */
			dev_info(bq->dev, "Back to FULL_NOTYET. state=%d\n",
				soft_full_state
			);
		}
		bq->soft_full_state = SOFT_FULL_NOTYET;
		goto out;
	}

	switch (soft_full_state) {
	case SOFT_FULL_NOTYET:
		/* Not Full state. */
		if (suspend_ready_full) {
			/* Turn to full state. */
			bq->jiffies_soft_full_detect = bq->jiffies_now;
			bq->soft_full_state = SOFT_FULL_DETECT;
			dev_info(bq->dev, "Detect charged full.\n");
		}
		break;
	case SOFT_FULL_DETECT:
		/* Still remain full state. */
		/* NOTE: Wait Full state becomes stable and
		 * notify CHARGER event to USB gadget parties.
		 * This consecutive procedure will event CHARGER
		 * after event VBUS breaking wakelock surely.
		 * See drivers/usb/phy/otg-wakelock.c
		 */
		jiffies_delta =
			bq->jiffies_now - bq->jiffies_soft_full_detect;
		jiffies_max = msecs_to_jiffies(
			BQ25898_SOFT_FULL_POLL_SHORT_MS);
		if (jiffies_delta >= jiffies_max) {
			/* Full state stable enough. */
			/* Break OTG wake lock. */
			dev_info(bq->dev, "Break otg-wakelock.\n");
			call_usb_gadget_notifiers(USB_EVENT_CHARGER);
			bq->soft_full_state = SOFT_FULL_STABLE;
			break;
		}
		/* Continue polling. */
		break;
	case SOFT_FULL_STABLE:
		if (test_bit(BQ25898_DEVICE_FLAGS_SUSPENDING, &(bq->flags))) {
			/* Suspending, */
			bq->soft_full_state = SOFT_FULL_SUSPEND;
		}
		break;
	case SOFT_FULL_SUSPEND:
		if (bq->resumed_timer == RESUMED_TIMER_DONE) {
			/* Keep resumed state. */
			bq->soft_full_state = SOFT_FULL_STABLE;
			break;
		}
		/* NOTE: Keeping this state will stops charging
		 * long time. Because we down VREG, it keeps
		 * full until battery voltage becoming below VREG.
		 */
		break;
	default:
		/* May not come here. */
		dev_warn(bq->dev, "Unexpected soft_full_state. state=%d\n",
			soft_full_state
		);
		bq->soft_full_state = SOFT_FULL_NOTYET;
		break;
	}
out:
	up(&(bq->sem_this));
}

static void bq25898_ps_changed_work(struct bq25898_device *bq)
{
	int	ret;
	int	batv;
	int	capacity;

	bool	notify_now = false;
	bool	suspending = false;
	bool	resume = false;
	bool	battery_low = false;
	bool	pg_changed = false;

	union power_supply_propval val;

	resume = test_bit(CHARGER_EVENT_RESUME, &(bq->charger_event_catch));
	suspending = test_bit(BQ25898_DEVICE_FLAGS_SUSPENDING, &(bq->flags));

	if (resume) {
		/* Resume event. */
		down(&(bq->sem_this));
		pg_changed = (bq->pg_suspend != bq->state.pg_current);
		up(&(bq->sem_this));
		notify_now |= pg_changed;
	}

	if (suspending) {
		/* "Suspending" */
		/* Pend notify. */
		/* note: We suppress notifying changes
		 * while suspending. Because power_supply_changed()
		 * aborts susped process.
		 */
		bq->ps_prop_notify_pend |= bq->ps_prop_notify;
	}

	batv = bq->state.batv_current;
	battery_low |= (batv < bq->dead_battery_prepare_uv);

	val.intval = 0;
	ret = max1704x_get_property_proxy(POWER_SUPPLY_PROP_CAPACITY, &val);
	if (ret == 0) {
		capacity = val.intval;
		battery_low |= (capacity <= bq->low_battery_capacity);
	}

	notify_now |= ((resume) && (battery_low));

	switch (bq->resumed_timer) {
	case RESUMED_TIMER_RUN:
		/* Resumed, but we may suspend soon.
		 * Especially, in LPA mode, suspend and resume
		 * in short time.
		 */
		bq->ps_prop_notify_pend |= bq->ps_prop_notify;
		break;

	case RESUMED_TIMER_DONE:
		/* after resumed a while, or starting. */
		notify_now |= bq->ps_prop_notify_pend;
		notify_now |= bq->ps_prop_notify;
		break;
	default:
		/* Do nothing. */
		break;
	}

	if (notify_now) {
		/* Notify some updates. */
		bq->ps_prop_notify_pend = false;
		power_supply_changed(bq->charger);
	}
}

static void bq25898_charger_thread_wake_raw(struct bq25898_device *bq)
{	wake_up(&(bq->charger_wq));
}

static void bq25898_charger_thread_wake(struct bq25898_device *bq)
{	if (test_bit(BQ25898_DEVICE_FLAGS_REMOVE, &(bq->flags)) == 0) {
		/* Driver alive, accept events. */
		bq25898_charger_thread_wake_raw(bq);
	}
}

static void bq25898_charger_thread_stop(struct bq25898_device *bq)
{	/* Stop events */
	if (!test_and_set_bit(BQ25898_DEVICE_FLAGS_REMOVE, &(bq->flags))) {
		/* Terminate thread. */
		kthread_stop(bq->charger_thread);
		set_bit(CHARGER_EVENT_EXIT, &(bq->charger_event));
		/* event EXIT without event gating. */
		bq25898_charger_thread_wake_raw(bq);
		wait_for_completion(&(bq->charger_exit));
	}
}

static bool bq25898_charger_thread_should_work(struct bq25898_device *bq)
{
	/* Catch event and propagate to thread inside. */

	if (test_and_clear_bit(CHARGER_EVENT_EXIT, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_EXIT, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_SYNC, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_SYNC, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_IRQ, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_IRQ, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_CHG_USB, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_CHG_USB, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_USB_OTG, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_USB_OTG, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_USB_HOST, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_USB_HOST, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_CHG_USB_PD, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_CHG_USB_PD, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_USB_CONFIG, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_USB_CONFIG, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_VBUS_DRAW, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_VBUS_DRAW, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_WAKE_NOW, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_WAKE_NOW, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_SYNC_DIAG, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_SYNC_DIAG, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_USB_DEV, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_USB_DEV, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_TEMP_POLL, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_TEMP_POLL, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_SOFT_WARM, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_SOFT_WARM, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_SUSPEND, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_SUSPEND, &(bq->charger_event_catch));

	if (test_and_clear_bit(CHARGER_EVENT_RESUME, &(bq->charger_event)))
		set_bit(CHARGER_EVENT_RESUME, &(bq->charger_event_catch));

	return ((bq->charger_event_catch != 0)) || (kthread_should_stop());
}

static void bq25898_timeout_update(struct bq25898_device *bq, long to)
{	if (bq->timeout_ms > to) {
		/* Requested more shorter timeout. */
		bq->timeout_ms = to;
	}
}

#define	BQ25898_POLLING_COUNT		(2)
#define	BQ25898_POLLING_MS		(2000)
#define	BQ25898_TEMP_TRIP_POLL_MS	(20000)
#define	BQ25898_STATUS_UPDATE_MS	(2000)
#define	BQ25898_FAULT_POLL_MS		(10000)
#define	BQ25898_POLL_NOW_MS		(10)
#define	BQ25898_TEMP_POLL_MS		(2000)

#define	BQ25898_STARTUP_POLLING_MS		(5000)
#define	BQ25898_STARTUP_POLLING_INTERVAL_MS	(1000)

/* Do polling at starting thread.
 * Some condition(s) will be fixed after starting charger thread.
 * To catch delayed condition(s), polling a while after starting thread.
 */
static void bq25898_thread_startup_timeout(struct bq25898_device *bq)
{	u64	jiffies_delta;
	u64	jiffies_max;

	if ((bq->startup_done)) {
		/* Done startup polling. */
		return;
	}

	/* startup polling. */
	jiffies_delta = bq->jiffies_now - bq->jiffies_start;
	jiffies_max = msecs_to_jiffies(BQ25898_STARTUP_POLLING_MS);
	if (jiffies_delta <= jiffies_max) {
		/* Startup phase. */
		bq25898_timeout_update(bq,
			BQ25898_STARTUP_POLLING_INTERVAL_MS
		);
	} else {
		/* Done startup phase. */
		bq->startup_done = true;
	}
}

static void bq25898_charger_dead_battery_timeout(struct bq25898_device *bq)
{	long	remains_ms;
	u64	jiffies_delta;
	long	jiffies_max;

	if (!(bq->dead_battery)) {
		/* Don't need dead battery charging. */
		return;
	}

	/* Dead battery boot. */
	remains_ms = bq->dead_battery_time_ms;
	jiffies_max = msecs_to_jiffies(remains_ms);
	jiffies_delta = bq->jiffies_now - bq->jiffies_start;
	if (jiffies_delta >= jiffies_max) {
		/* Wake now, exit dead battery mode. */
		bq->dead_battery = false;
		set_bit(CHARGER_EVENT_WAKE_NOW, &(bq->charger_event));
		bq25898_charger_thread_wake(bq);
	} else {
		/* Wake at done dead battery charging. */
		remains_ms -= jiffies_to_msecs(jiffies_delta);
		bq25898_timeout_update(bq, min(remains_ms, (long)(5) * 1000));
	}
}

static void bq25898_soft_warm_timeout(struct bq25898_device *bq)
{	int	low;
	int	high;

	int	temp_current;
	int	tm;
	int	tp;

	down(&(bq->sem_this));
	low = bq->soft_warm_temp_low;
	high = bq->soft_warm_temp_high;

	if ((low == S32_MAX) || (high == S32_MAX)) {
		/* No software warm charge control. */
		goto out;
	}

	if (bq->state.pg_current == 0) {
		/* No power comes on VBUS. */
		goto out;
	}

	temp_current = bq->state.temp_current;
	tm = high - BQ25898_SOFT_WARM_TEMP_NEAR;
	tp = high + BQ25898_SOFT_WARM_TEMP_NEAR;

	if ((tm <= temp_current) && (temp_current <= tp)) {
		/* near soft warm high threshold temperature. */
		bq25898_timeout_update(bq, BQ25898_SOFT_WARM_POLL_SHORT_MS);
		goto out;
	}

	tm = low - BQ25898_SOFT_WARM_TEMP_NEAR;
	tp = low + BQ25898_SOFT_WARM_TEMP_NEAR;

	if ((tm <= temp_current) && (temp_current <= tp)) {
		/* near soft warm low threshold temperature. */
		bq25898_timeout_update(bq, BQ25898_SOFT_WARM_POLL_SHORT_MS);
		goto out;
	}

	bq25898_timeout_update(bq, BQ25898_SOFT_WARM_POLL_LONG_MS);
out:
	up(&(bq->sem_this));
}

static void bq25898_soft_full_timeout(struct bq25898_device *bq)
{	int	soft_full_state;

	down(&(bq->sem_this));
	soft_full_state = bq->soft_full_state;
	up(&(bq->sem_this));
	switch (soft_full_state) {
	case SOFT_FULL_NOTYET:
		bq25898_timeout_update(bq, BQ25898_SOFT_FULL_POLL_LONG_MS);
		break;
	case SOFT_FULL_DETECT:
	case SOFT_FULL_STABLE:
	case SOFT_FULL_SUSPEND:
		bq25898_timeout_update(bq, BQ25898_SOFT_FULL_POLL_SHORT_MS);
		break;
	default:
		/* Will not come here. */
		break;
	}
}

static void bq25898_resumed_timer_timeout(struct bq25898_device *bq)
{	switch (bq->resumed_timer) {
	case RESUMED_TIMER_RUN:
		bq25898_timeout_update(bq, RESUMED_TIMER_DONE_MS / 2);
		break;
	default:
		break;
	}
}

#define DIAG_VBUS_TIMEOUT_MS	(180 * 1000)

static void bq25898_diag_vbus_timeout(struct bq25898_device *bq)
{	long	remains_ms;
	u64	jiffies_delta;
	u64	jiffies_max;

	down(&(bq->sem_this));
	if (bq->diag_vbus_sink_current < DIAG_VBUS_SINK_CURRENT_ENABLE) {
		/* Normal operation, do nothing. */
		goto out;
	}

	/* Diag VBUS operation. */
	jiffies_delta = bq->jiffies_now - bq->jiffies_diag_vbus;
	jiffies_max = msecs_to_jiffies(DIAG_VBUS_TIMEOUT_MS);
	if (jiffies_delta >= jiffies_max) {
		/* Wake now, exit diag VBUS mode. */
		dev_notice(bq->dev,
			"Time out, exit diag VBUS mode.\n"
		);
		bq->diag_vbus_sink_current = DIAG_VBUS_SINK_CURRENT_NORMAL;
		set_bit(CHARGER_EVENT_WAKE_NOW, &(bq->charger_event));
		bq25898_charger_thread_wake(bq);
	} else {
		/* Wake at done diag vbus mode. */
		remains_ms = jiffies_to_msecs(jiffies_max - jiffies_delta);
		dev_info(bq->dev, "Diag VBUS timer start. remains_ms=%ld\n",
			remains_ms
		);
		bq25898_timeout_update(bq, remains_ms);
	}
out:
	up(&(bq->sem_this));
}

static int bq25898_charger_thread(void *arg)
{	struct bq25898_device *bq = arg;

	dev_info(bq->dev, "Start charger thread.\n");
	bq->jiffies_start = get_jiffies_64();
	while (!kthread_should_stop()) {
		int		wait_ready = 0;
		unsigned long	j_timeout = 0;

		bq->jiffies_now = get_jiffies_64();
		bq->cc_new = CHARGE_CONFIG_NONE;
		bq->vbus_boost = VBUS_BOOST_KEEP;
		bq->cc_force_config = false;
		bq->ps_prop_notify = false;
		bq->watchdog_clear = false;

		bq->timeout_ms = LONG_MAX;

		if (bq->poll_count >= 0) {
			/* Now polling mode. */
			bq->poll_count--;
			bq25898_timeout_update(bq, BQ25898_POLLING_MS);
		}

		if (bq->temp_trip) {
			/* Temp fault trip. */
			bq25898_timeout_update(bq, BQ25898_TEMP_TRIP_POLL_MS);
		}

		if (bq->status_update) {
			/* Need update status after a moment. */
			bq25898_timeout_update(bq,
				BQ25898_STATUS_UPDATE_MS
			);
			bq->ps_prop_notify = true;
			bq->status_update = false;
		}

		bq25898_thread_startup_timeout(bq);
		bq25898_charger_dead_battery_timeout(bq);
		bq25898_soft_warm_timeout(bq);
		bq25898_soft_full_timeout(bq);
		bq25898_resumed_timer_timeout(bq);
		bq25898_diag_vbus_timeout(bq);

		if (bq25898_state_fault_current(bq)) {
			/* some fault happen. */
			dev_warn(bq->dev, "Fault condition, enter polling.\n");
			bq25898_timeout_update(bq, BQ25898_FAULT_POLL_MS);
		}

		if (bq->temp_poll) {
			dev_notice(bq->dev, "Enter temperature polling.\n");
			bq25898_timeout_update(bq, BQ25898_TEMP_POLL_MS);
		}
		if (bq->timeout_ms < LONG_MAX) {
			/* polling mode or some one request timeout */
			j_timeout = msecs_to_jiffies(bq->timeout_ms);
		}

		bq->charger_event_catch = 0;
		/* Thread alive. */
		if (j_timeout) {
			wait_ready = wait_event_freezable_timeout(
				bq->charger_wq,
				bq25898_charger_thread_should_work(bq),
				j_timeout);
			DEV_INFO_THREAD(bq->dev,
				"Wake thread. event_catch=0x%.8lx, wait_ready=%d, j_timeout=%lu\n",
				bq->charger_event_catch, wait_ready, j_timeout
			);
		} else {
			wait_event_freezable(
				bq->charger_wq,
				bq25898_charger_thread_should_work(bq)
			);
			DEV_INFO_THREAD(bq->dev, "Wake thread. event_catch=0x%.8lx\n",
				bq->charger_event_catch
			);
		}
		bq->jiffies_now = get_jiffies_64();

		bq25898_resume_pre_work(bq);

		if (!test_bit(CHARGER_EVENT_IRQ,
			&(bq->charger_event_catch))) {
			/* "Not IRQ event" */
			bq25898_state_read_current(bq);
		}

		if (test_bit(CHARGER_EVENT_IRQ,
			&(bq->charger_event_catch))) {
			/* USB IRQ event. */
			bq25898_irq_work_pre(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_CHG_USB,
			&(bq->charger_event_catch)) ||
			(!(bq->startup_done))) {
			/* "USB Chager event." or "startup in progress" */
			bq25898_chg_usb_catch(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_USB_OTG,
			&(bq->charger_event_catch))) {
			/* OTG event. */
			bq25898_usb_otg_catch(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_USB_HOST,
			&(bq->charger_event_catch))) {
			/* USB Host event. */
			(void) bq25898_usb_host_catch(bq);
			if (bq->typec.state_host == 0) {
				/* End Source (host) role. */
				/* Clear trip condition. */
				bq->vbus_boost_trip = false;
				/* Prepare VBUS boost. */
				bq->vbus_boost = VBUS_BOOST_ON;
			} else {
				/* Start Source (host) role. */
				/* Prepare VBUS_BOOST.
				 * @note Prepare VBUS_BOOST at...
				 * + connecting and disconnecting from
				 *   USB Sink (USB device)
				 * + disconnecting from USB Source (USB host).
				 * At most cases, we already
				 * set VBUS_BOOST_ON (OTG_CONFIG=1).
				 */
				bq->vbus_boost = VBUS_BOOST_ON;
			}
		}

		if (test_and_clear_bit(CHARGER_EVENT_USB_DEV,
			&(bq->charger_event_catch))) {
			/* USB Device event. */
			(void) bq25898_usb_dev_catch(bq);
			if (bq->typec.state_dev == false) {
				/* End device session. */
				bq25898_usb_dev_session_end(bq);
			}
		}


		if (test_and_clear_bit(CHARGER_EVENT_CHG_USB_PD,
			&(bq->charger_event_catch))) {
			/* Type-C CC signal current level. */
			(void) bq25898_chg_usb_pd_catch(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_USB_CONFIG,
			&(bq->charger_event_catch))) {
			/* Type-C CC signal current level. */
			(void) bq25898_usb_config_catch(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_VBUS_DRAW,
			&(bq->charger_event_catch))) {
			/* Type-C CC signal current level. */
			bq25898_vbus_draw_catch(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_TEMP_POLL,
			&(bq->charger_event_catch))) {
			/* Start temperature polling. */
			bq25898_temp_poll_catch(bq);
		}

		bq25898_suspend_pre_work(bq);

		/* Look USB PHY and "Control Request" event. */
		bq25898_vbus_draw_work(bq);
		bq25898_usb_config_work(bq);
		bq25898_dead_battery_work(bq);
		bq25898_chg_usb_work(bq);
		bq25898_chg_usb_pd_work(bq);
		bq25898_diag_vbus_work(bq);
		bq25898_temp_poll_work(bq);
		bq25898_soft_warm_work(bq);
		bq25898_soft_full_work(bq);

		if (test_and_clear_bit(CHARGER_EVENT_IRQ,
			&(bq->charger_event_catch))) {
			/* USB IRQ event. */
			bq25898_irq_work_post(bq);
		}

		if (test_and_clear_bit(CHARGER_EVENT_EXIT,
			&(bq->charger_event_catch))) {
			/* Terminate thread. */
			break;
		}

		if (test_and_clear_bit(CHARGER_EVENT_SOFT_WARM,
				&(bq->charger_event_catch))) {
			/* Update Software Warm temperature. */
			bq->cc_force_config = true;
		}

		bq25898_fault_work(bq);

		if (bq->state.pg_current == 0) {
			/* VBUS is not powered. */
			if (bq->state.pg_previous != 0) {
				/* VBUS power is lost. */
				/* Prepare VBUS_BOOST mode.
				 * Note: Do once after PG_STAT
				 * changed.
				 */
				bq->vbus_boost = VBUS_BOOST_ON;
			}
			bq25898_cc_new_update(bq, CHARGE_CONFIG_NO_POWER);
		}

		bq25898_charge_i_v_down_work(bq);

		if ((bq->cc_new == bq->cc_current) &&
		    (bq->ichg_down_new == bq->ichg_down_current) &&
		    (bq->vreg_down_new == bq->vreg_down_current) &&
		    (!(bq->cc_force_config))
		) {
			/* Nothing will be change. */
			bool	keep_reg = true;

			if (((bq25898_debug & DEBUG_NO_REG_CHECK) == 0) &&
			    (!(test_bit(BQ25898_DEVICE_FLAGS_SUSPENDING,
				&(bq->flags))))
			) {
				/* (not "No Register Check") and
				 * (not Around suspending)
				 */
				keep_reg = bq25898_charge_config_check(bq,
					bq->cc_current,
					bq->ichg_down_current,
					bq->vreg_down_current
				);
				bq->cc_force_config |= !keep_reg;
			}
		}

		if (bq->watchdog_clear) {
			/* Need watchdog clear. */
			dev_warn(bq->dev, "Clear watchdog.\n");
			(void) bq25898_watchdog_write(bq, false);
		}

		if (((bq->cc_new != CHARGE_CONFIG_NONE) &&
		     (bq->cc_new != bq->cc_current)
		    ) ||
		    (bq->ichg_down_new != bq->ichg_down_current) ||
		    (bq->vreg_down_new != bq->vreg_down_current) ||
		    (bq->cc_force_config)
		) {
			u8 iinlim;
			/* Update charger configuration. */
			bq25898_charge_config(bq,
				bq->cc_new,
				bq->ichg_down_new,
				bq->vreg_down_new
			);
			bq->cc_current = bq->cc_new;
			bq->ichg_down_current = bq->ichg_down_new;
			bq->vreg_down_current = bq->vreg_down_new;
			iinlim = bq->charge_configs[bq->cc_current].iinlim;
			if (iinlim != INVALID_REG_FIELD_VALUE) {
				/* Update VBUS draw current. */
				bq->iinlim_current = iinlim;
			}
			if (!test_bit(BQ25898_DEVICE_FLAGS_SUSPENDING,
				&(bq->flags))
			) {
				/* Not suspendig. */
				bq->ps_prop_notify = true;
				/* Start polling. */
				bq->poll_count = BQ25898_POLLING_COUNT;
				bq->poll_period_ms = BQ25898_POLLING_MS;
			}
		}
		if (bq->state.boost_fault_current != 0) {
			/* VBUS boosting in fault condition. */
			dev_warn(bq->dev,
				"BOOST_FAULT condition, off boosting.\n"
			);
			bq->vbus_boost_trip = true;
			bq->vbus_boost = VBUS_BOOST_OFF;
		}
		if (bq->vbus_boost_trip) {
			/* Trip VBUS boosting. */
			/* Keep trip condition until cable is
			 * removed from Type-C connector.
			 */
			bq->vbus_boost = VBUS_BOOST_OFF;
		}
		if (bq->vbus_boost != VBUS_BOOST_KEEP) {
			/* Update VBUS boost configuration. */
			bq25898_vbus_boost_config(bq,
				(bool)(bq->vbus_boost));
			bq->ps_prop_notify = true;
		}

		bq25898_resume_post_work(bq);
		bq25898_ps_changed_work(bq);

		if (test_and_clear_bit(CHARGER_EVENT_SYNC,
			&(bq->charger_event_catch))
		) {	/* Sync with other thread. */
			complete(&(bq->charger_sync));
		}

		if (test_and_clear_bit(CHARGER_EVENT_SYNC_DIAG,
			&(bq->charger_event_catch))
		) {
			/* Sync with diag thread. */
			complete(&(bq->charger_sync_diag));
		}

		if (test_and_clear_bit(CHARGER_EVENT_SUSPEND,
			&(bq->charger_event_catch))
		) {
			/* Sync with suspend thread. */
			complete(&(bq->charger_suspend));
		}

		if (test_and_clear_bit(CHARGER_EVENT_RESUME,
			&(bq->charger_event_catch))
		) {
			/* Sync with resume thread. */
			complete(&(bq->charger_resume));
		}

	}
	dev_info(bq->dev, "End charger thread. \n");
	/* Event to thread terminator. */
	complete(&(bq->charger_exit));
	return 0;
}

static irqreturn_t bq25898_irq_handler_thread(int irq, void *private)
{
	struct bq25898_device *bq = private;
	int ret;

	ret = bq25898_state_read_irq(bq);
	if (ret < 0)
		goto handled;

	set_bit(CHARGER_EVENT_IRQ, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);

handled:
	return IRQ_HANDLED;
}

static int bq25898_typec_host_event_cb(struct notifier_block *nb,
	unsigned long event, void *ptr)
{	struct bq25898_device	*bq;

	bq = container_of(nb, struct bq25898_device, typec.nb_host);
	DEV_INFO_EVENT(bq->dev, "Callback USB_HOST\n");
	set_bit(CHARGER_EVENT_USB_HOST, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);

	return NOTIFY_OK;
}

static int bq25898_typec_dev_event_cb(struct notifier_block *nb,
	unsigned long event, void *ptr)
{	struct bq25898_device	*bq;

	bq = container_of(nb, struct bq25898_device, typec.nb_dev);
	DEV_INFO_EVENT(bq->dev, "Callback USB_DEV\n");
	set_bit(CHARGER_EVENT_USB_DEV, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);

	return NOTIFY_OK;
}


static int bq25898_typec_chg_event_cb(struct notifier_block *nb,
	unsigned long event, void *ptr)
{	struct bq25898_device		*bq;

	bq = container_of(nb, struct bq25898_device, typec.nb_chg);
	DEV_INFO_EVENT(bq->dev, "Callback CHG_USB_PD\n");
	set_bit(CHARGER_EVENT_CHG_USB_PD, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);

	return NOTIFY_OK;
}

static int bq25898_usb_config_event_cb(struct notifier_block *nb,
	unsigned long event, void *ptr)
{	struct bq25898_device	*bq;

	bq = container_of(nb, struct bq25898_device, usb_config.nb);
	DEV_INFO_EVENT(bq->dev, "Callback USB config.\n");
	set_bit(CHARGER_EVENT_USB_CONFIG, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);

	return NOTIFY_OK;
}


static struct device *bq25898_rdev_to_dev(struct regulator_dev *rdev)
{	struct device	*dev;

	if (!rdev)
		return NULL;

	dev = rdev->dev.parent;
	return dev;
}


static int bq25898_rdev_charger_get_current_limit(struct regulator_dev *rdev)
{	struct device		*dev;
	struct bq25898_device	*bq;
	int			current_limit;

	dev = bq25898_rdev_to_dev(rdev);
	bq = dev_get_drvdata(dev);
	down(&(bq->sem_this));
	current_limit = bq->rdev_charger_current_cb;
	up(&(bq->sem_this));
	DEV_INFO_EVENT(dev, "%s: Called. current_limit=%d\n",
		__func__, current_limit
	);
	return current_limit;
}

static int bq25898_rdev_charger_set_current_limit(struct regulator_dev *rdev,
	int min_uA, int max_uA)
{	struct device		*dev;
	struct bq25898_device	*bq;
	unsigned long		flags;

	dev = bq25898_rdev_to_dev(rdev);
	bq = dev_get_drvdata(dev);
	DEV_INFO_EVENT(dev, "%s: Called. min_uA=%d, max_uA=%d\n",
		__func__, min_uA, max_uA
	);

	if (max_uA >= (BQ25898_IINLIM_UA_MAX + BQ25898_IINLIM_UA_STEP)) {
		/* We reject set current range function argument max_uA equal
		 * to regulator class maximum value in
		 * regulation_constraints.max_uA member.
		 * Because regulator class core driver sets constrained range
		 * according to regulation_constraints members at register
		 * call, but we don't prefer to configure input current
		 * with maximum at probe procedure.
		 */
		dev_warn(dev, "Ignore maximum current request. max_uA=%d\n", max_uA);
	} else {
		/* Valid range. */
		spin_lock_irqsave(&(bq->rdev_lock), flags);
		bq->rdev_charger_current_cb = max_uA;
		set_bit(CHARGER_EVENT_VBUS_DRAW, &(bq->charger_event));
		spin_unlock_irqrestore(&(bq->rdev_lock), flags);
		bq25898_charger_thread_wake(bq);
	}
	/* Always success. */
	return 0;
}

static int bq25898_rdev_charger_get_status(struct regulator_dev *rdev)
{	/* Return Always on. */
	return  REGULATOR_STATUS_ON;
}

static const char bq25898_rdev_charger_name[] = "vbus_draw";

static struct regulator_ops bq25898_rdev_charger_ops = {
	.get_current_limit =	bq25898_rdev_charger_get_current_limit,
	.set_current_limit =	bq25898_rdev_charger_set_current_limit,
	.get_status =		bq25898_rdev_charger_get_status,
};

static struct regulator_init_data bq25898_rdev_charger_init_data_def = {
	.supply_regulator = NULL, /* No parent. */
	.constraints = {
		.name = bq25898_rdev_charger_name,
		/* Will be updated. */
		.min_uV = BQ25898_BOOSTV_UV_DEF,
		/* Will be updated. */
		.max_uV = BQ25898_BOOSTV_UV_DEF,
		.uV_offset = 0,
		.min_uA =    0,
		.max_uA =  BQ25898_IINLIM_UA_MAX + BQ25898_IINLIM_UA_STEP,
		.ilim_uA = BQ25898_IINLIM_UA_MAX + BQ25898_IINLIM_UA_STEP,
		/* No affects to entire system supply current. */
		.system_load = 0,
		.valid_modes_mask = REGULATOR_MODE_INVALID,
		.valid_ops_mask =
			REGULATOR_CHANGE_CURRENT |
			REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 0,
	.consumer_supplies = NULL,
};

enum bq25898_reg_ids {
	BQ25898_REG_ID_VBUS_DRAW,
};

static struct regulator_linear_range
	bq25898_rdev_charger_range_volt_def = {
	.min_uV = BQ25898_BOOSTV_UV_DEF, /* Will be updated. */
	.min_sel = 0,
	.max_sel = 0,
	.uV_step = 0,
};

static struct regulator_desc bq25898_rdev_charger_desc_def = {
	.name =		bq25898_rdev_charger_name,
	.id =		BQ25898_REG_ID_VBUS_DRAW,
	.ops =		&bq25898_rdev_charger_ops,
	.type =		REGULATOR_CURRENT,
	.owner =	THIS_MODULE,
	/* Will be updated. */
	.min_uV =	BQ25898_BOOSTV_UV_DEF,
	.uV_step =	0,
	/* Will be updated. */
	.fixed_uV =	BQ25898_BOOSTV_UV_DEF,
	.linear_ranges = &bq25898_rdev_charger_range_volt_def,
	.n_linear_ranges = 1,
};

static int bq25898_chip_reset(struct bq25898_device *bq)
{
	int ret = 0;
	int rst_check_counter = 10;

	down(&(bq->sem_dev));
	ret = bq25898_field_write(bq, F_REG_RST, 1);
	if (ret < 0)
		goto out;

	do {
		ret = bq25898_field_read(bq, F_REG_RST);
		if (ret < 0)
			goto out;

		rst_check_counter--;
		if (rst_check_counter < 0)
			break;

		usleep_range(5, 10);
	} while (ret == 1);

	if (rst_check_counter < 0) {
		dev_err(bq->dev, "Reset timeout.\n");
		return -ETIMEDOUT;
	}
out:
	up(&(bq->sem_dev));
	return ret;
}

/* default fields values */

static const char of_prop_ichg[] =	 "ti,charge-current";
static const char of_prop_ichg_warm[] =	 "ti,charge-current-warm";
static const char of_prop_iterm[] =	 "ti,termination-current";
static const char of_prop_iprechg[] =	 "ti,precharge-current";
static const char of_prop_vreg[] =	 "ti,battery-regulation-voltage";
static const char of_prop_vreg_warm[] =	 "ti,battery-regulation-voltage-warm";
static const char of_prop_jeita_iset[] = "ti,jeita-low-temp-current";
static const char of_prop_jeita_vset[] = "ti,jeita-high-temp-voltage";
static const char of_prop_sys_min[] =	 "ti,minimum-sys-voltage";
static const char of_prop_bat_comp[] =	 "ti,battery-comp-resister";
static const char of_prop_vclamp[] =	 "ti,battery-comp-clamp-voltage";
static const char of_prop_en_ilim[] =	 "ti,use-ilim-pin";
static const char of_prop_force_vindpm[] = "ti,force-input-voltage-limit";
static const char of_prop_vindpm[] =	  "ti,input-voltage-limit";
static const char of_prop_boost_freq[] = "ti,boost-frequency";
static const char of_prop_boostv[] =	 "ti,boost-voltage";
static const char of_prop_boost_lim[] =	 "ti,boost-max-current";
static const char of_prop_treg[] =	 "ti,thermal-regulation-threshold";
static const char of_prop_en_timer[] =	 "ti,safety-timer-enable";
static const char of_prop_chg_timer[] =	 "ti,fast-charge-timer";
static const char of_prop_auto_dpdm_en[] = "ti,auto-dpdm-enable";

static const struct init_field_val bq25898_init_defaults[] = {
	{F_ICHG,	 0x0b /* 704mA */,  of_prop_ichg, TBL_ICHG},
	{F_ICHG_WARM,	 0x02 /* 128mA */,  of_prop_ichg_warm, TBL_ICHG},
	{F_ITERM,	 0x00 /*  64mA */,  of_prop_iterm, TBL_ITERM},
	{F_IPRECHG,	 0x01 /* 128mA */,  of_prop_iprechg, TBL_IPRECHG},
	{F_VREG,	 0x15 /* 4.176V */, of_prop_vreg, TBL_VREG},
	{F_VREG_WARM,	 0x0d /* 4.048V */, of_prop_vreg_warm, TBL_VREG},
	{F_JEITA_ISET,	 0x00 /* 50% */,    of_prop_jeita_iset, TBL_JEITA_ISET},
	{F_JEITA_VSET,	 0x00 /* -200mV */, of_prop_jeita_vset, TBL_JEITA_VSET},
	{F_SYS_MIN,	 0x02 /* 3.2V */,   of_prop_sys_min, TBL_SYS_MIN},
	{F_BAT_COMP,	 0x00 /* 0mohm */,  of_prop_bat_comp, TBL_BAT_COMP},
	{F_VCLAMP,	 0x00 /* 0mV */,    of_prop_vclamp,   TBL_VCLAMP},
	{F_EN_ILIM,	 0x01 /* Enable */, of_prop_en_ilim,  TBL_EN_ILIM},
	{F_FORCE_VINDPM, 0x01 /* Foece VINDPM mode. */, of_prop_force_vindpm, TBL_FORCE_VINDPM},
	{F_VINDPM,       0x12 /* 4.4V */,   of_prop_vindpm, TBL_VINDPM},
	{F_BOOST_FREQ,   0x00 /* 1.5MHz */, of_prop_boost_freq, TBL_BOOST_FREQ},
	{F_BOOSTV,	 0x08 /* 5.062V */, of_prop_boostv, TBL_BOOSTV},
	{F_BOOST_LIM,	 0x00 /* 500mA */,  of_prop_boost_lim, TBL_BOOST_LIM},
	{F_TREG,	 0x03 /* 120 Celsius */,  of_prop_treg, TBL_TREG},
	{F_EN_TIMER,     0x01 /* Enable timer */, of_prop_en_timer, TBL_EN_TIMER},
	{F_CHG_TIMER,    0x01 /* 8 Hours */, of_prop_chg_timer, TBL_CHG_TIMER},
	{F_AUTO_DPDM_EN, 0x00 /* disable */, of_prop_auto_dpdm_en, TBL_AUTO_DPDM_EN},
	{F_FIELDS_NUM,   INVALID_REG_FIELD_VALUE /* TERMINATOR */, NULL, TBL_NUMS},
};

const struct init_field_val *init_field_val_find(
	const struct init_field_val *init, enum bq25898_fields to_find
)
{	enum bq25898_fields field;

	if (init == NULL)
		return init;

	while ((field = init->field) < F_FIELDS_NUM) {
		if (init->val == INVALID_REG_FIELD_VALUE)
			return NULL;

		if (field == to_find)
			return init;

		init++;
	}
	return NULL;
}

static int bq25898_hw_init(struct bq25898_device *bq)
{
	int ret;

	ret = bq25898_chip_reset(bq);
	if (ret < 0) {
		dev_err(bq->dev, "Can not reset chip. ret=%d\n",
			ret
		);
		return ret;
	}

	ret = bq25898_watchdog_write(bq, false);
	if (ret < 0) {
		/* Can not set watchdog. */
		return ret;
	}

	if (bq->dead_battery == false) {
		bq->cc_current = CHARGE_CONFIG_UNCONFIG;
		ret = bq25898_charge_config(bq,
			CHARGE_CONFIG_UNCONFIG, false, false);
		if (ret != 0)
			return ret;
	} else {
		bq->cc_current = CHARGE_CONFIG_DEAD_BATTERY;
		ret = bq25898_charge_config(bq,
			CHARGE_CONFIG_DEAD_BATTERY, false, false);
		if (ret != 0)
			return ret;
	}
	bq->iinlim_current = bq->charge_configs[bq->cc_current].iinlim;

	/* Configure ADC for continuous conversions.
	 * We read temp continuously, and feed temp to gauge.
	 */
	ret = bq25898_adc_config(bq, ADC_CONFIG_AUTO);
	if (ret < 0)
		return ret;

	ret = bq25898_state_read_irq(bq);
	if (ret < 0)
		return ret;

	bq->state.pg_previous = ~((uint8_t)0);

	/* Check watchdog again. */
	if (bq->state.watchdog_fault_evented != 0) {
		/* Watchdog fault at start. */
		ret = bq25898_watchdog_write(bq, false);
		if (ret < 0) {
			/* Can not set watchdog. */
			return ret;
		}
	}
	return 0;
}

static enum power_supply_property bq25898_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	/* Not Available to Userland Applications. */
	/* POWER_SUPPLY_PROP_TEMP, */
};

static char *bq25898_charger_supplied_to[] = {
	"main-battery",
};

static const struct power_supply_desc bq25898_power_supply_desc = {
	.name = "bq25898-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = bq25898_power_supply_props,
	.num_properties = ARRAY_SIZE(bq25898_power_supply_props),
	.get_property = bq25898_power_supply_get_property,
};

static int bq25898_power_supply_init(struct bq25898_device *bq)
{
	struct power_supply_config psy_cfg = { .drv_data = bq, };

	psy_cfg.supplied_to = bq25898_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(bq25898_charger_supplied_to);

	bq->charger = power_supply_register(bq->dev, &bq25898_power_supply_desc,
					    &psy_cfg);

	return PTR_ERR_OR_ZERO(bq->charger);
}

static int bq25898_usb_notifier(struct notifier_block *nb, unsigned long val,
				void *priv)
{
	struct bq25898_device *bq =
			container_of(nb, struct bq25898_device, usb_otg.nb);
	unsigned long	flags;

	spin_lock_irqsave(&(bq->usb_otg.lock), flags);
	bq->usb_otg.event_cb = val;
	set_bit(CHARGER_EVENT_USB_OTG, &(bq->charger_event));
	spin_unlock_irqrestore(&(bq->usb_otg.lock), flags);
	bq25898_charger_thread_wake(bq);
	return NOTIFY_OK;
}

/* Show diag_vbus_sink_current
 */
static ssize_t bq25898_diag_vbus_sink_current_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct bq25898_device	*bq;
	ssize_t			result;

	bq = dev_get_drvdata(dev);

	result = snprintf(buf, PAGE_SIZE, "%d\n",
		bq->diag_vbus_sink_current
	);
	return result;
}

/* Store diag_vbus_sink_current
 */
static ssize_t bq25898_diag_vbus_sink_current_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct bq25898_device	*bq;
	int	ret;
	ssize_t result = -EIO;
	long	sink_current;

	bq = dev_get_drvdata(dev);

	down(&(bq->sem_diag));
	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(bq->dev, "Too long write. size=%lu, result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	sink_current = DIAG_VBUS_SINK_CURRENT_NORMAL;
	ret = kstrtol(buf, 0, &sink_current);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(bq->dev, "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	down(&(bq->sem_this));
	bq->jiffies_diag_vbus = get_jiffies_64();
	bq->diag_vbus_sink_current = sink_current;
	up(&(bq->sem_this));

	set_bit(CHARGER_EVENT_SYNC_DIAG, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	wait_for_completion(&(bq->charger_sync_diag));

	/* force cast */ /* Consume all written datas. */
	result = (__force ssize_t)size;

out:
	up(&(bq->sem_diag));
	return result;
}

/*! Force configure VBUS sink current - Device Attribute diag interface
 *  Mode:
 *   0644
 *  Description:
 *   Diagnostic interface
 *   Set VBUS current
 *  Usage:
 *  Write String: Function
 *   "-1":  Exit diag mode
 *   "0":   Configure VBUS HiZ
 *   "500": Set VBUS maximum current 500mA
 *   cat disabled_state # Print current disabled_state value.
 */
static DEVICE_ATTR(diag_vbus_sink_current, 0644,
	bq25898_diag_vbus_sink_current_show,
	bq25898_diag_vbus_sink_current_store
	);

/* Show diag_temp
 */
static ssize_t bq25898_diag_temp_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct bq25898_device	*bq;
	ssize_t	result = -EIO;
	int	temp = 0;
	int	ret;

	bq = dev_get_drvdata(dev);
	down(&(bq->sem_diag));

	set_bit(CHARGER_EVENT_SYNC_DIAG, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	wait_for_completion(&(bq->charger_sync_diag));

	down(&(bq->sem_dev));
	ret = bq25898_field_read(bq, F_TSPCT);
	up(&(bq->sem_dev));
	if (ret < 0) {
		result = -EIO;
		goto out;
	}
	temp = bq25898_tspct_to_tx10(bq, ret);
	result = snprintf(buf, PAGE_SIZE, "%d\n", temp);
out:
	up(&(bq->sem_diag));
	return result;
}

/*! NTC temperature - Device Attribute diag interface
 *  Mode:
 *   0444
 *  Description:
 *   Diagnostic interface
 *   Read NTC temperature
 *  Usage:
 *   Read string: mean
 *   "32767":  Invalid NTC ADC result
 *   "-33": -3.3 Celsius
 *   "276": 27.6 Celsius
 */
static DEVICE_ATTR(diag_temp, 0444,
	bq25898_diag_temp_show,
	NULL
	);

/* Show soft_warm_temp_low
 */
static ssize_t bq25898_soft_warm_temp_low_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct bq25898_device	*bq;
	ssize_t			result;

	bq = dev_get_drvdata(dev);
	down(&(bq->sem_this));
	result = snprintf(buf, PAGE_SIZE, "%d\n",
		bq->soft_warm_temp_low
	);
	up(&(bq->sem_this));

	return result;
}

/* Store soft_warm_temp_low
 */
static ssize_t bq25898_soft_warm_temp_low_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct bq25898_device	*bq;
	int	ret;
	ssize_t result = -EIO;
	long	soft_warm_temp;

	bq = dev_get_drvdata(dev);

	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(bq->dev, "Too long write. size=%lu, result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	soft_warm_temp = S32_MAX;
	ret = kstrtol(buf, 0, &soft_warm_temp);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(bq->dev, "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	down(&(bq->sem_this));
	bq->soft_warm_temp_low = soft_warm_temp;
	up(&(bq->sem_this));
	set_bit(CHARGER_EVENT_SOFT_WARM, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	/* force cast */ /* Consume all written datas. */
	result = (__force ssize_t)size;

out:
	return result;
}

/*! Software warm temperature low - Device Attribute diag interface
 *  Mode:
 *   0644
 *  Description:
 *   Configure software wram temperature low side in Celsius x 10
 *  Usage:
 *  Write String: Function
 *   integer value != S32_MAX: software wram temperature in Celsius x 10
 *        Examples,
 *        100: 10.0 Celsius
 *        450: 45.0 Celsius
 *   0x7fffffff or 2147483647: No software warm temperature
 *   cat soft_warm_temp # Print current software warm temperature.
 */
static DEVICE_ATTR(soft_warm_temp_low, 0644,
	bq25898_soft_warm_temp_low_show,
	bq25898_soft_warm_temp_low_store
	);

/* Show soft_warm_temp_high
 */
static ssize_t bq25898_soft_warm_temp_high_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct bq25898_device	*bq;
	ssize_t			result;

	bq = dev_get_drvdata(dev);
	down(&(bq->sem_this));
	result = snprintf(buf, PAGE_SIZE, "%d\n",
		bq->soft_warm_temp_high
	);
	up(&(bq->sem_this));

	return result;
}

/* Store soft_warm_temp_high
 */
static ssize_t bq25898_soft_warm_temp_high_store(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	struct bq25898_device	*bq;
	int	ret;
	ssize_t result = -EIO;
	long	soft_warm_temp;

	bq = dev_get_drvdata(dev);

	if (size >= PAGE_SIZE) {
		/* Too long write. */
		result = -EINVAL;
		dev_err(bq->dev, "Too long write. size=%lu, result=%ld\n",
			(unsigned long)size, (long)result
		);
		goto out;
	}

	soft_warm_temp = S32_MAX;
	ret = kstrtol(buf, 0, &soft_warm_temp);
	if (ret != 0) {
		result = (ssize_t)ret;
		dev_err(bq->dev, "Invalid value. result=%ld\n",
			(long)result
		);
		goto out;
	}

	down(&(bq->sem_this));
	bq->soft_warm_temp_high = soft_warm_temp;
	up(&(bq->sem_this));
	set_bit(CHARGER_EVENT_SOFT_WARM, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	/* force cast */ /* Consume all written datas. */
	result = (__force ssize_t)size;

out:
	return result;
}

/*! Software warm temperature high - Device Attribute diag interface
 *  Mode:
 *   0644
 *  Description:
 *   Configure software wram temperature low side in Celsius x 10
 *  Usage:
 *  Write String: Function
 *   integer value != S32_MAX: software wram temperature in Celsius x 10
 *        Examples,
 *        100: 10.0 Celsius
 *        450: 45.0 Celsius
 *   0x7fffffff or 2147483647: No software warm temperature
 *   cat soft_warm_temp_high # Print current software warm temperature.
 */
static DEVICE_ATTR(soft_warm_temp_high, 0644,
	bq25898_soft_warm_temp_high_show,
	bq25898_soft_warm_temp_high_store
	);



static struct attribute *bq25898_attributes[] = {
	&dev_attr_diag_vbus_sink_current.attr,
	&dev_attr_diag_temp.attr,
	&dev_attr_soft_warm_temp_low.attr,
	&dev_attr_soft_warm_temp_high.attr,
	NULL,
};

static const struct attribute_group bq25898_attr_group = {
	.attrs = bq25898_attributes,
};

static int bq25898_of_read_charge_props(struct bq25898_device *bq)
{	struct device_node		*np;
	struct device_node		*np_child;
	struct charge_config		*cconf;

	int			result = 0;
	int			ret = 0;
	int			i;

	cconf = &(bq->charge_configs[0]);
	i = 0;
	while (i < CHARGE_CONFIG_NUMS) {
		/* Copy default to all configs. */
		/* structure copy. */
		*cconf = charge_config_def;
		cconf++;
		i++;
	}

	np = bq->dev->of_node;
	np = of_find_node_by_name(np, charge_config_root);
	if (np == NULL) {
		dev_warn(bq->dev, "No charge configuration table.\n");
		return -ENOENT;
	}

	cconf = &(bq->charge_configs[0]);
	i = 0;
	while (i < CHARGE_CONFIG_NUMS) {
		const char	*node_name;
		const char	*prop;
		u32		prop_val;
		u8		reg_val;
		const char	*no_property =
			"No property in dt. node=%s, prop=%s\n";
		const char	*invalid_value =
			"Invalid property value. prop=%s, prop_val=%s\n";

		node_name = charge_config_names[i];
		if (node_name == NULL) {
			/* Driver Internal Configuration. */
			i++;
			continue;
		}
		if (node_name[0] == '*') {
			/* Driver Internal Configuration. */
			i++;
			continue;
		}
		np_child = of_find_node_by_name(np, node_name);
		if ((np_child == NULL) && (i != CHARGE_CONFIG_NONE)) {
			/* No entry in device tree. */
			dev_warn(bq->dev, "No configuration in dt. node=%s\n",
				node_name
			);
			result = result ? result : -ENOENT;
			goto next_config;
		}
		prop = "en-hiz";
		prop_val = 0;
		ret = of_property_read_u32(np_child, prop, &prop_val);
		if (ret != 0) {
			dev_warn(bq->dev, no_property, node_name, prop);
			result = result ? result : -ENOENT;
		} else {
			if ((prop_val != 0) && (prop_val != 1)) {
				dev_warn(bq->dev,
					invalid_value, prop, prop_val
				);
				result = result ? result : -EINVAL;
			} else {
				cconf->en_hiz = prop_val;
			}
		}
		prop = "iinlim";
		prop_val = 0;
		ret = of_property_read_u32(np_child, prop, &prop_val);
		if (ret != 0) {
			dev_warn(bq->dev, no_property, node_name, prop);
			result = result ? result : -ENOENT;
		} else {
			if (prop_val == ~((u32)0)) {
				/* Keep IINLIM. */
				cconf->iinlim = INVALID_REG_FIELD_VALUE;
			} else {
				reg_val = bq25898_trans_to_idx(prop_val,
					TBL_IINLIM
				);
				if (reg_val == INVALID_REG_FIELD_VALUE) {
					dev_warn(bq->dev, invalid_value,
						prop, prop_val
					);
					result = result ? result : -ENOENT;
				} else {
					cconf->iinlim = reg_val;
				}
			}
		}
		prop = "chg-config";
		prop_val = 0;
		ret = of_property_read_u32(np_child, prop, &prop_val);
		if (ret != 0) {
			dev_warn(bq->dev, no_property, node_name, prop);
			result = result ? result : -ENOENT;
		} else {
			if ((prop_val != 0) && (prop_val != 1)) {
				dev_warn(bq->dev,
					invalid_value, prop, prop_val
				);
				result = result ? result : -EINVAL;
			} else {
				cconf->chg_config = prop_val;
			}
		}
		of_node_put(np_child);
next_config:
		cconf++;
		i++;
	}
	of_node_put(np);
	return result;
}

static int bq25898_of_read_init_props(struct bq25898_device *bq)
{
	int	ret = 0;
	enum bq25898_fields		field;
	struct init_field_val		*init = bq->init_data;

	while ((field = init->field) < F_FIELDS_NUM) {
		u8		conv_val;
		u32		of_val;
		const char	*prop_name;

		prop_name = init->prop;

		if (prop_name == NULL) {
			/* immutable initialize field. */
			init++;
			continue;
		}

		of_val = NOT_FOUND_INDEX;
		ret = device_property_read_u32(bq->dev, prop_name, &of_val);
		if (ret != 0) {
			/* No property, it's not an error. */
			init++;
			continue;
		}

		conv_val = bq25898_trans_to_idx(of_val, init->trans);

		if (conv_val == NOT_FOUND_INDEX) {
			/* Invalid property value. */
			/* Warn it, and continue. */
			dev_warn(bq->dev, "Invalid property value. prop_name=%s, of_val=0x%x",
				prop_name, of_val
			);
		} else {
			/* Valid property value, update init data. */
			init->val = conv_val;
		}
		init++;
	}
	return 0;
}

static int bq25898_of_read_dead_battery_props(struct bq25898_device *bq)
{	int	ret = 0;
	int	result = 0;
	u32	of_val;

	of_val = bq->dead_battery_uv = BQ25898_DEAD_BATTERY_UV_DEF;
	ret = device_property_read_u32(bq->dev,
		"svs,dead-battery", &of_val);
	if (ret == 0) {
		/* Set dead battry level. */
		bq->dead_battery_uv = (int)of_val;
		result = result ? result : ret;
	}
	of_val = bq->dead_battery_time_ms =
		BQ25898_DEAD_BATTERY_CHARGE_TIME_MS;
	ret = device_property_read_u32(bq->dev,
		"svs,dead-battery-time", &of_val);
	if (ret == 0) {
		/* Set dead battry level. */
		bq->dead_battery_time_ms = (unsigned int)of_val;
		result = result ? result : ret;
	}

	of_val = bq->dead_battery_prepare_uv =
		BQ25898_DEAD_BATTERY_PREPARE_UV;
	ret = device_property_read_u32(bq->dev,
		"svs,dead-battery-prepare", &of_val);
	if (ret == 0) {
		/* Set dead battry prepare level. */
		bq->dead_battery_prepare_uv = (int)of_val;
		result = result ? result : ret;
	}

	of_val = bq->low_battery_capacity = BQ25898_LOW_BATTERY_CAPACITY;
	ret = device_property_read_u32(bq->dev,
		"svs,low-battery-capacity", &of_val);
	if (ret == 0) {
		/* Set low battry capacity level. */
		bq->low_battery_capacity = (int)of_val;
		result = result ? result : ret;
	}
	return result;
}

#define	SELF_STAND_TEMP_09_MIN		(0x09)
#define	SELF_STAND_TEMP_09_MAX		(0x13)

#define	SELF_STAND_TEMP_09	((187753 * 256 + 50) / 100)
#define	SELF_STAND_TEMP_13	 ((82112 * 256 + 50) / 100)
#define	SELF_STAND_TEMP_09_DELTA	\
	((SELF_STAND_TEMP_13 - SELF_STAND_TEMP_09) / \
	(SELF_STAND_TEMP_09_MAX - SELF_STAND_TEMP_09_MIN))

#if ((SELF_STAND_TEMP_09_MIN > SELF_STAND_TEMP_09_MAX) || \
     (SELF_STAND_TEMP_09_MAX >= BQ25898_NTC_TABLE_SIZE) || \
     (SELF_STAND_TEMP_09_DELTA >= 0))
#error Incorrect NTC temperature approximation parameter SELF_STAND_TEMP_09_x
#endif /* expression */


#define	SELF_STAND_TEMP_14_MIN	(0x14)
#define	SELF_STAND_TEMP_14_MAX	(0x7f)

#define	SELF_STAND_TEMP_14	((79270 * 256 + 50) / 100)
#define	SELF_STAND_TEMP_7F	((-7567 * 256 + 50) / 100)
#define	SELF_STAND_TEMP_14_DELTA \
	((SELF_STAND_TEMP_7F - SELF_STAND_TEMP_14) / \
	(SELF_STAND_TEMP_14_MAX - SELF_STAND_TEMP_14_MIN))

#if ((SELF_STAND_TEMP_14_MIN > SELF_STAND_TEMP_14_MAX) || \
     (SELF_STAND_TEMP_14_MAX >= BQ25898_NTC_TABLE_SIZE) || \
     (SELF_STAND_TEMP_14_DELTA >= 0))
#error Incorrect NTC temperature approximation parameter SELF_STAND_TEMP_14_x
#endif /* expression */

static void bq25898_ntc_self_stand_fill(s16 *table)
{	int	i;
	int	temp;

	i = 0;
	while (i < SELF_STAND_TEMP_09_MAX) {
		table[i] = BQ25898_TSPCT_INVALID_TEMP;
		i++;
	}

	i = SELF_STAND_TEMP_09_MIN;
	while (i <= SELF_STAND_TEMP_09_MAX) {
		temp =  SELF_STAND_TEMP_09;
		temp += i * SELF_STAND_TEMP_09_DELTA;
		temp /= 256;
		table[i] = temp;
		i++;
	}

	i = SELF_STAND_TEMP_14_MIN;
	while (i <= SELF_STAND_TEMP_14_MAX) {
		temp =  SELF_STAND_TEMP_14;
		temp += i * SELF_STAND_TEMP_14_DELTA;
		temp /= 256;
		table[i] = temp;
		i++;
	}
}

#define	VBUS_INCOMING_TEMP_00_MIN	(0x00)
#define	VBUS_INCOMING_TEMP_00_MAX	(0x7f)

#define	VBUS_INCOMING_TEMP_00	((61732 * 256 + 50) / 100)
#define	VBUS_INCOMING_TEMP_7F	((-8385 * 256 + 50) / 100)
#define	VBUS_INCOMING_TEMP_00_DELTA	\
	((VBUS_INCOMING_TEMP_7F - VBUS_INCOMING_TEMP_00) / \
	(VBUS_INCOMING_TEMP_00_MAX - VBUS_INCOMING_TEMP_00_MIN))

#if ((VBUS_INCOMING_TEMP_00_MIN > VBUS_INCOMING_TEMP_00_MAX) || \
     (VBUS_INCOMING_TEMP_00_MAX >= BQ25898_NTC_TABLE_SIZE) || \
     (VBUS_INCOMING_TEMP_00_DELTA >= 0))
#error Incorrect NTC temperature approximation parameter VBUS_INCOMING
#endif /* expression */

static void bq25898_ntc_vbus_incoming_fill(s16 *table)
{	int	i;
	int	temp;

	i = VBUS_INCOMING_TEMP_00_MIN;
	while (i <= VBUS_INCOMING_TEMP_00_MAX) {
		temp =  VBUS_INCOMING_TEMP_00;
		temp += i * VBUS_INCOMING_TEMP_00_DELTA;
		temp /= 256;
		table[i] = temp;
		i++;
	}
}

static int bq25898_of_read_ntc_table(struct bq25898_device *bq)
{	int	result = 0;
	int	ret = 0;
	const char	*self_stand =    "svs,ntc-self-stand";
	const char	*vbus_incoming = "svs,ntc-vbus-incoming";

	bq25898_ntc_self_stand_fill(
		&(bq->ntc_table[TSPCT_SELF_STAND][0])
	);
	bq25898_ntc_vbus_incoming_fill(
		&(bq->ntc_table[TSPCT_VBUS_INCOMING][0])
	);

	ret = device_property_read_u16_array(bq->dev,
		self_stand,
		&(bq->ntc_table[TSPCT_SELF_STAND][0]),
		ARRAY_SIZE(bq->ntc_table[TSPCT_SELF_STAND])
	);
	if (ret != 0) {
		dev_warn(bq->dev, "No NTC temperature table. prop=%s\n",
			self_stand
		);
		result = result ? result : ret;
	}

	ret = device_property_read_u16_array(bq->dev,
		vbus_incoming,
		&(bq->ntc_table[TSPCT_VBUS_INCOMING][0]),
		ARRAY_SIZE(bq->ntc_table[TSPCT_VBUS_INCOMING])
	);
	if (ret != 0) {
		dev_warn(bq->dev, "No NTC temperature table. prop=%s\n",
			vbus_incoming
		);
		result = result ? result : ret;
	}
	return result;
}

static int bq25898_of_read_ntc_props(struct bq25898_device *bq)
{	int	ret = 0;
	int	result = 0;
	int	temp;
	u32	of_val;

	of_val = bq->soft_warm_temp_low = BQ25898_SOFT_WARM_TEMP_LOW_DEF;
	ret = device_property_read_u32(bq->dev,
		"svs,soft-warm-temp-low", &of_val);
	if (ret == 0) {
		/* Set soft warm temperature. */
		bq->soft_warm_temp_low = (int)of_val;
		result = result ? result : ret;
	}

	of_val = bq->soft_warm_temp_high = BQ25898_SOFT_WARM_TEMP_HIGH_DEF;
	ret = device_property_read_u32(bq->dev,
		"svs,soft-warm-temp-high", &of_val);
	if (ret == 0) {
		/* Set soft warm temperature. */
		bq->soft_warm_temp_high = (int)of_val;
		result = result ? result : ret;
	}

	if (bq->soft_warm_temp_low > bq->soft_warm_temp_high) {
		dev_notice(bq->dev, "Swap soft-warm-temp-low with soft-warm-temp-high. low=%d, high=%d\n",
			bq->soft_warm_temp_low, bq->soft_warm_temp_high
		);
		temp = bq->soft_warm_temp_low;
		bq->soft_warm_temp_low = bq->soft_warm_temp_high;
		bq->soft_warm_temp_high = temp;
	}

	return result;
}

static void bq25898_of_read_irq_wake_props(struct bq25898_device *bq)
{
	bq->irq_wake_of = device_property_read_bool(bq->dev, "svs,irq-wake");
}

static int bq25898_of_probe(struct bq25898_device *bq)
{
	/* ignore error */
	(void) bq25898_of_read_init_props(bq);
	/* ignore error */
	(void) bq25898_of_read_charge_props(bq);
	/* ignore error */
	(void) bq25898_of_read_dead_battery_props(bq);
	/* ignore error */
	(void) bq25898_of_read_ntc_table(bq);
	/* ignore error */
	(void) bq25898_of_read_ntc_props(bq);
	bq25898_of_read_irq_wake_props(bq);
	return 0;
}

static int bq25898_probe_extcon_typec_get(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int		ret = 0;

	/* Register for USB TYPE-C USB Power role notifier */
	if (!of_property_read_bool(dev->of_node, "extcon")) {
		dev_notice(dev, "No extcon property in device-tree.\n");
		return -ENOENT;
	}
	if (bq->typec.edev != NULL) {
		/* Already get extcon TYPE-C detector. */
		return 0;
	}
	bq->typec.edev = extcon_get_edev_by_phandle(dev, EXTCON_OF_TYPEC);
	if (IS_ERR_OR_NULL(bq->typec.edev)) {
		/* note we may defer probe when we got -EPROBE_DEFER error. */
		ret = PTR_ERR(bq->typec.edev);
		dev_warn(dev, "Can not get TYPEC extcon. ret=%d\n",
			ret
		);
		return ret;
	}
	DEV_INFO_EVENT(dev, "Get extcon TYPEC and CHG_USB_PD.\n");

	return ret;
}

static int bq25898_probe_extcon_typec_link(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int		result = 0;
	int		ret = 0;

	if (IS_ERR_OR_NULL(bq->typec.edev)) {
		/* Can not get Type-C detector extcon */
		/* Ignore this error. */
		return 0;
	}

	/* Register USB host call back. */
	bq->typec.nb_host.notifier_call = bq25898_typec_host_event_cb;
	ret = extcon_register_notifier(bq->typec.edev,
		EXTCON_USB_HOST,
		&(bq->typec.nb_host)
	);
	if (ret) {
		dev_notice(dev,
			"Failed to register extcon USB_HOST call back. ret=%d\n",
			ret
		);
		result = result ? result : ret;
		/* anyway, continue. */;
	} else {
		/* Success register call back. */
		bq->typec.nb_registered_host = true;
	}

	/* Register USB device call back. */
	bq->typec.nb_dev.notifier_call = bq25898_typec_dev_event_cb;
	ret = extcon_register_notifier(bq->typec.edev,
		EXTCON_USB,
		&(bq->typec.nb_dev)
	);
	if (ret) {
		dev_notice(dev,
			"Failed to register extcon USB call back. ret=%d\n",
			ret
		);
		result = result ? result : ret;
		/* anyway, continue. */;
	} else {
		/* Success register call back. */
		bq->typec.nb_registered_dev = true;
	}

	ret = bq25898_usb_host_catch(bq);
	if (ret < 0) {
		/* unexpected, fail to get state. */
		result = result ? result : ret;
	}

	ret = bq25898_usb_dev_catch(bq);
	if (ret < 0) {
		/* unexpected, fail to get state. */
		result = result ? result : ret;
	}

	ret = bq25898_chg_usb_pd_catch(bq);
	if (ret < 0) {
		/* unexpected, fail to get state and property. */
		result = result ? result : ret;
	}

	/* Register USB PD(charger) call back. */
	bq->typec.nb_chg.notifier_call = bq25898_typec_chg_event_cb;
	ret = extcon_register_notifier(bq->typec.edev, EXTCON_CHG_USB_PD,
		&(bq->typec.nb_chg)
	);
	if (ret) {
		dev_err(dev,
			"Failed to register extcon CHG_USB_PD call back. ret=%d\n",
			ret
		);
		result = result ? result : ret;
		/* anyway, continue. */;
	} else {
		/* Success register call back. */
		bq->typec.nb_registered_chg = true;
	}

	return ret;
}


static int bq25898_remove_extcon_typec(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int	result = 0;
	int	ret = 0;

	if (IS_ERR_OR_NULL(bq->typec.edev)) {
		/* Not linked USB_HOST extcon device. */
		return 0;
	}

	if (bq->typec.nb_registered_host) {
		ret = extcon_unregister_notifier(
			bq->typec.edev,
			EXTCON_USB_HOST,
			&(bq->typec.nb_host)
		);
		if (ret != 0) {
			/* Can not unregister call back. */
			dev_err(dev, "Failed to unregister EXTCON_USB_HOST. ret=%d\n",
				ret
			);
			/* But we will fail retrying unregister.
			 * Do not try again.
			 */
			result = result ? result : ret;
		}
	}
	bq->typec.nb_registered_host = false;

	if (bq->typec.nb_registered_dev) {
		ret = extcon_unregister_notifier(
			bq->typec.edev,
			EXTCON_USB,
			&(bq->typec.nb_dev)
		);
		if (ret != 0) {
			/* Can not unregister call back. */
			dev_err(dev, "Failed to unregister EXTCON_USB. ret=%d\n",
				ret
			);
			/* But we will fail retrying unregister.
			 * Do not try again.
			 */
			result = result ? result : ret;
		}
	}
	bq->typec.nb_registered_dev = false;


	if (bq->typec.nb_registered_chg) {
		ret = extcon_unregister_notifier(bq->typec.edev,
			EXTCON_CHG_USB_PD, &(bq->typec.nb_chg)
		);
		if (ret != 0) {
			/* Can not unregister call back. */
			dev_err(dev, "Failed to unregister EXTCON_USB_HOST. ret=%d\n",
				ret
			);
			/* But we will fail retrying unregister.
			 * Do not try again.
			 */
			result = result ? result : ret;
		}
	}
	bq->typec.nb_registered_chg = false;

	bq->typec.edev = NULL;
	return ret;
}

static void bq25898_extcon_chg_usb_get_state(struct bq25898_device *bq)
{	int		i;
	int		connected_id;
	int		extcon_id;
	int		state;

	if (bq->chg_usb.edev == NULL) {
		/* no USB AC charger extcon device. */
		return;
	}

	connected_id = EXTCON_CHG_USB_TERMINATOR;
	i = 0;
	while ((extcon_id = chg_usb_extcon_order[i]) >= 0) {
		if (((bq->chg_usb.reged) & (0x1 << i)) == 0) {
			/* not registered. */
			i++;
			continue;
		}
		state = extcon_get_state(bq->chg_usb.edev,
			extcon_id
		);
		if (state > 0) {
			/* Detects USB AC adaptor. */
			connected_id = extcon_id;
		}
		i++;
	}
	if (connected_id >= 0) {
		/* detected USB AC charger. */
		bq->chg_usb.type = connected_id;
	} else {
		/* Unknown USB AC charger. */
		bq->chg_usb.type = EXTCON_CHG_USB_TERMINATOR;
	}
}

static int bq25898_chg_usb_event_cb(struct notifier_block *nb,
	unsigned long event, void *ptr)
{	struct bq25898_nb_container	*nbc;
	struct bq25898_device		*bq;

	nbc = container_of(nb, struct bq25898_nb_container, nb);
	bq = nbc->bq;
	DEV_INFO_EVENT(bq->dev, "Callback CHG_USB\n");
	set_bit(CHARGER_EVENT_CHG_USB, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);

	return NOTIFY_OK;
}


static int bq25898_probe_extcon_chg_usb_get(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int		ret = 0;

	/* Register for USB Charger notifier */
	if (!of_property_read_bool(dev->of_node, "extcon")) {
		dev_notice(dev, "No extcon property in device-tree.\n"
		);
		return -ENOENT /* anyway, continue. */;
	}
	if (bq->chg_usb.edev != NULL) {
		/* Already get extcon source. */
		return 0;
	}

	bq->chg_usb.edev = extcon_get_edev_by_phandle(dev, EXTCON_OF_CHARGER);
	if (IS_ERR_OR_NULL(bq->chg_usb.edev)) {
		/* note we may defer probe when we got -EPROBE_DEFER error. */
		ret = PTR_ERR(bq->chg_usb.edev);
		dev_warn(dev, "Can not get CHG_USB_x extcon. ret=%d\n",
			ret
		);
		return ret;
	}
	DEV_INFO_EVENT(dev, "Get extcon CHG_USB_x. edev=0x%p\n",
		 bq->chg_usb.edev
	);
	return ret;
}

static int bq25898_probe_extcon_chg_usb_link(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int		ret = 0;
	int		i;
	int		extcon_id;

	if (IS_ERR_OR_NULL(bq->chg_usb.edev)) {
		/* May failed get USB AC charger detector extcon device. */
		/* Ignore error. */
		return 0;
	}

	bq25898_extcon_chg_usb_get_state(bq);
	DEV_INFO_EVENT(bq->dev, "Get CHG_USB type. type=%d\n",
		bq->chg_usb.type
	);

	i = 0;
	while ((extcon_id = chg_usb_extcon_order[i]) >= 0) {
		struct bq25898_nb_container	*nbc;

		nbc = &(bq->chg_usb.nbc[i]);
		/* @note It may set notifier_call again. */
		nbc->nb.notifier_call = bq25898_chg_usb_event_cb;
		nbc->bq = bq;
		if (((bq->chg_usb.reged) & (0x1 << i)) != 0) {
			/* Already registered. */
			i++;
			continue;
		}
		/* Try register notifier call back. */
		ret = extcon_register_notifier(bq->chg_usb.edev,
			extcon_id,
			&(nbc->nb)
		);
		if (ret == 0) {
			/* Success register notifier call back. */
			bq->chg_usb.reged |= (0x1 << i);
		} else {
			/* Failed register notifier call back. */
			dev_notice(dev, "Failed register notifier call back. i=%d\n",
				i
			);
		}
		i++;
	}

	return ret;
}

static int bq25898_remove_extcon_chg_usb(struct bq25898_device *bq)
{	int		result = 0;
	int		ret = 0;
	int		i;
	int		extcon_id;

	if (IS_ERR_OR_NULL(bq->chg_usb.edev)) {
		/* Not probed EXTCON CHG_USB */
		return 0;
	}
	i = 0;
	while ((extcon_id = chg_usb_extcon_order[i]) >= 0) {
		struct bq25898_nb_container	*nbc;

		if ((bq->chg_usb.reged & (0x1 << i)) == 0) {
			/* not registered. */
			i++;
			continue;
		}
		nbc = &(bq->chg_usb.nbc[i]);
		ret = extcon_unregister_notifier(
			bq->chg_usb.edev,
			extcon_id,
			&(nbc->nb)
		);
		if (ret != 0) {
			dev_err(bq->dev, "Fail to unregister CHG_USB_x callback. i=%d\n",
				i
			);
			/* Update function result. */
			result = result ? result : ret;
		}
		i++;
	}
	bq->chg_usb.edev = NULL;
	return result;
}

static int bq25898_probe_extcon_usb_config_get(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int		ret = 0;

	/* Register for USB configuration draw current notifier */
	if (!of_property_read_bool(dev->of_node, "extcon")) {
		dev_notice(dev, "No extcon property in device-tree.\n");
		return -ENOENT;
	}
	if (bq->usb_config.edev != NULL) {
		/* Already get extcon TYPE-C detector. */
		return 0;
	}
	bq->usb_config.edev =
		extcon_get_edev_by_phandle(dev, EXTCON_OF_USB_CONFIG);
	if (IS_ERR_OR_NULL(bq->usb_config.edev)) {
		/* note we may defer probe
		 * when we got -EPROBE_DEFER error.
		 */
		ret = PTR_ERR(bq->usb_config.edev);
		dev_warn(dev, "Can not get USB config extcon. ret=%d\n",
			ret
		);
		return ret;
	}
	DEV_INFO_EVENT(dev, "Get extcon USB config.\n");

	return ret;
}

static int bq25898_probe_extcon_usb_config_link(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int		result = 0;
	int		ret = 0;

	if (IS_ERR_OR_NULL(bq->usb_config.edev)) {
		/* Can not get USB configuration extcon */
		/* Ignore this error. */
		return 0;
	}

	/* Register USB config call back. */
	bq->usb_config.nb.notifier_call = bq25898_usb_config_event_cb;
	ret = extcon_register_notifier(bq->usb_config.edev,
		EXTCON_CHG_USB_SDP,
		&(bq->usb_config.nb)
	);
	if (ret) {
		dev_notice(dev,
			"Failed to register extcon USB config call back. ret=%d\n",
			ret
		);
		result = result ? result : ret;
		/* anyway, continue. */;
	} else {
		/* Success register notifier callback. */
		bq->usb_config.registered = true;
	}

	ret = bq25898_usb_config_catch(bq);
	if (ret < 0) {
		/* unexpected, fail to get state. */
		result = result ? result : ret;
	}

	return ret;
}


static int bq25898_remove_extcon_usb_config(struct bq25898_device *bq)
{	struct device	*dev = bq->dev;
	int	result = 0;
	int	ret = 0;

	if (IS_ERR_OR_NULL(bq->usb_config.edev)) {
		/* Not linked USB_HOST extcon device. */
		return 0;
	}

	if (bq->usb_config.registered) {
		ret = extcon_unregister_notifier(
			bq->usb_config.edev,
			EXTCON_CHG_USB_SDP,
			&(bq->usb_config.nb)
		);
		if (ret != 0) {
			/* Can not unregister call back. */
			dev_err(dev, "Failed to unregister USB config. ret=%d\n",
				ret
			);
			/* But we will fail retrying unregister.
			 * Do not try again.
			 */
			result = result ? result : ret;
		}
	}
	bq->usb_config.registered = false;
	bq->usb_config.edev = NULL;
	return ret;
}


static int bq25898_usb_otg_get(struct bq25898_device *bq)
{	int ret = 0;

	if (bq->usb_otg.phy != NULL) {
		/* Already get USB OTG. */
		return 0;
	}
	bq->usb_otg.phy = devm_usb_get_phy(bq->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(bq->usb_otg.phy)) {
		/* note we may defer probe when we got -EPROBE_DEFER error. */
		ret = PTR_ERR(bq->usb_otg.phy);
		dev_warn(bq->dev, "Can not get USB OTG. ret=%d\n",
			ret
		);
		return ret;
	}
	return ret;
}

static int bq25898_usb_otg_link(struct bq25898_device *bq)
{	int	ret = 0;

	if (IS_ERR_OR_NULL(bq->usb_otg.phy)) {
		/* Can not get USB OTG */
		return 0;
	}

	bq->usb_otg.nb.notifier_call = bq25898_usb_notifier;
	ret = usb_register_notifier(bq->usb_otg.phy, &bq->usb_otg.nb);
	if (ret != 0) {
		/* Can not add call back. */
		dev_err(bq->dev, "Fail to add USB OTG call back. ret=%d\n",
			ret
		);
		return ret;
	}
	return ret;
}

static void bq25898_remove_usb_otg(struct bq25898_device *bq)
{
	if (IS_ERR_OR_NULL(bq->usb_otg.phy)) {
		/* Not linked USB OTG */
		return;
	}
	usb_unregister_notifier(bq->usb_otg.phy, &(bq->usb_otg.nb));
}

#define	VBUS_DRAW_DEFAULT_CURRENT_UA	(100 * 1000)

static int bq25898_rdev_charger_register(struct bq25898_device *bq)
{	int	ret = 0;
	int	boost_uv = 0;
	const struct init_field_val	*init;

	bq->rdev_charger_current = VBUS_DRAW_DEFAULT_CURRENT_UA;
	/* struct copy. */
	bq->rdev_charger_init_data = bq25898_rdev_charger_init_data_def;
	bq->rdev_charger_range_volt = bq25898_rdev_charger_range_volt_def;
	bq->rdev_charger_desc = bq25898_rdev_charger_desc_def;
	init = init_field_val_find(bq->init_data, F_BOOSTV);
	if (init) {
		/* Find boost mode output voltage. */
		boost_uv = bq25898_trans_to_val(init->val, TBL_BOOSTV);
	} else {
		/* Unexpected, Not found boost mode output voltage. */
		dev_warn(bq->dev, "Can not found BOOST_V in init_data.\n");
	}
	bq->rdev_charger_init_data.constraints.min_uV = boost_uv;
	bq->rdev_charger_init_data.constraints.max_uV = boost_uv;
	bq->rdev_charger_range_volt.min_uV = boost_uv;
	bq->rdev_charger_desc.min_uV = boost_uv;
	bq->rdev_charger_desc.fixed_uV = boost_uv;
	bq->rdev_charger_desc.linear_ranges = &(bq->rdev_charger_range_volt);
	bq->rdev_charger_conf.dev = bq->dev;
	bq->rdev_charger_conf.init_data = &(bq->rdev_charger_init_data);
	bq->rdev_charger = devm_regulator_register(bq->dev,
		&(bq->rdev_charger_desc),
		&(bq->rdev_charger_conf)
	);
	if (IS_ERR_OR_NULL(bq->rdev_charger)) {
		ret = PTR_ERR(bq->rdev_charger);
		dev_warn(bq->dev,
			"Can not register regurator. name=%s, ret=%d\n",
			bq->rdev_charger_desc.name, ret
		);
		/* Simplify context. */
		bq->rdev_charger = NULL;
	}
	return ret;
}

static int bq25898_probe_proxy(struct bq25898_device *bq)
{	struct bq25898_proxy	*proxy;
	int	result = 0;

	down(&(bq25898_proxy_sem));
	proxy = &bq25898_proxy;
	proxy->status = 0;
	if (proxy->bq) {
		/* Unexpected two or more device context. */
		dev_warn(bq->dev, "Unexpected two or more device context. proxy_bq=0x%p, bq=0x%p\n",
			proxy->bq, bq
		);
		result = -EBUSY;
	} else {
		/* First to configure proxy */
		proxy->bq = bq;
	}
	up(&(bq25898_proxy_sem));
	return result;
}

static int bq25898_remove_proxy(struct bq25898_device *bq)
{	struct bq25898_proxy	*proxy;
	int	result = 0;

	down(&(bq25898_proxy_sem));
	proxy = &bq25898_proxy;
	proxy->status = -ENODEV;
	proxy->bq = NULL;
	up(&(bq25898_proxy_sem));
	return result;
}

int bq25898_power_supply_get_property_proxy(
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct bq25898_proxy	*proxy;
	int	status = 0;

	down(&(bq25898_proxy_sem));
	proxy = &bq25898_proxy;
	status = proxy->status;
	if (status != 0) {
		pr_notice("%s: Call proxy while not ready. status=%d\n",
			__func__, status
		);
		goto out;
	}
	status = bq25898_power_supply_get_property(NULL, psp, val);
out:
	up(&(bq25898_proxy_sem));
	return status;
}

void bq25898_probe_diag(struct bq25898_device *bq)
{	bq->diag_vbus_sink_current = DIAG_VBUS_SINK_CURRENT_NORMAL;
}

#define BQ25898_PROBE_DEFER_MAX	(10)

static int bq25898_probe_defer; /* zero at init. */

static int bq25898_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct bq25898_device		*bq;
	struct init_field_val		*init_data;
	int	status_0b;

	int ret;
	int i;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	if (client->irq <= 0) {
		ret = -ENOENT;
		dev_err(dev, "IRQ is not configured. Check device-tree. ret=%d\n",
			ret
		);
		return ret;
	}

	bq = devm_kzalloc(dev, sizeof(*bq), GFP_KERNEL);
	if (!bq) {
		dev_err(dev,
			"Not enough memory to allocate context. ret=%d\n",
			-ENOMEM
		);
		return -ENOMEM;
	}
	init_data = devm_kzalloc(dev,
		sizeof(bq25898_init_defaults), GFP_KERNEL);
	if (!init_data) {
		dev_err(dev, "Not enough memory to allocate init_data. ret=%d\n",
			-ENOMEM
		);
		return -ENOMEM;
	}
	bq->init_data = init_data;
	memcpy(init_data, bq25898_init_defaults, sizeof(bq25898_init_defaults));

	bq->client = client;
	bq->dev = dev;

	sema_init(&(bq->sem_dev), 1);
	sema_init(&(bq->sem_this), 1);
	sema_init(&(bq->sem_diag), 1);
	spin_lock_init(&(bq->usb_otg.lock));
	spin_lock_init(&(bq->rdev_lock));
	init_waitqueue_head(&(bq->charger_wq));
	init_completion(&(bq->charger_sync));
	init_completion(&(bq->charger_sync_diag));
	init_completion(&(bq->charger_exit));
	init_completion(&(bq->charger_suspend));
	init_completion(&(bq->charger_resume));
	bq25898_probe_diag(bq);

	bq->jiffies_temp_poll = bq->jiffies_now =
		bq->jiffies_start = get_jiffies_64();

	/* Register for USB TYPE-C USB Host (Power role source) notifier */
	ret = bq25898_probe_extcon_typec_get(bq);
	if (ret == -EPROBE_DEFER) {
		/* Defer probe. */
		dev_notice(bq->dev, "Defer probe, waiting extcon TYPEC. probe_defer=%d\n",
			bq25898_probe_defer
		);
		if (bq25898_probe_defer < BQ25898_PROBE_DEFER_MAX) {
			/* Not many retries */
			/* Wait until extcon become ready. */
			bq25898_probe_defer++;
			goto extcon_fail;
		}
	}

	ret = bq25898_probe_extcon_chg_usb_get(bq);
	if (ret == -EPROBE_DEFER) {
		/* Defer probe. */
		dev_notice(bq->dev, "Defer probe, waiting extcon USB_CHG_x. probe_defer=%d\n",
			bq25898_probe_defer
		);
		if (bq25898_probe_defer < BQ25898_PROBE_DEFER_MAX) {
			/* Not many retries */
			/* Wait until extcon become ready. */
			bq25898_probe_defer++;
			goto extcon_fail;
		}
	}

	ret = bq25898_probe_extcon_usb_config_get(bq);
	if (ret == -EPROBE_DEFER) {
		/* Defer probe. */
		dev_notice(bq->dev, "Defer probe, waiting extcon USB config. probe_defer=%d\n",
			bq25898_probe_defer
		);
		if (bq25898_probe_defer < BQ25898_PROBE_DEFER_MAX) {
			/* Not many retries */
			/* Wait until extcon become ready. */
			bq25898_probe_defer++;
			goto extcon_fail;
		}
	}

	ret = bq25898_usb_otg_get(bq);
	if (ret == -EPROBE_DEFER) {
		/* Defer probe. */
		if (bq25898_probe_defer < BQ25898_PROBE_DEFER_MAX) {
			bq25898_probe_defer++;
			dev_info(dev, "Defer probe USB OTG. probe_defer=%d\n",
				bq25898_probe_defer
			);
			goto extcon_fail;
		}
	}

	ret = bq25898_probe_extcon_typec_link(bq);
	if (ret != 0) {
		/* Can not setup callback. */
		goto extcon_fail;
	}

	ret = bq25898_probe_extcon_chg_usb_link(bq);
	if (ret != 0) {
		/* Can not setup callback. */
		goto extcon_fail;
	}

	ret = bq25898_probe_extcon_usb_config_link(bq);
	if (ret != 0) {
		/* Can not setup callback. */
		goto extcon_fail;
	}

	ret = bq25898_usb_otg_link(bq);
	if (ret != 0) {
		/* Can not setup callback. */
		goto extcon_fail;
	}

	bq->rmap = devm_regmap_init_i2c(client, &bq25898_regmap_config);
	if (IS_ERR(bq->rmap)) {
		dev_err(dev, "Failed to allocate register map.\n");
		ret = PTR_ERR(bq->rmap);
		goto extcon_fail;
	}

	for (i = 0; i < ARRAY_SIZE(bq25898_reg_fields); i++) {
		const struct reg_field *reg_fields = bq25898_reg_fields;

		bq->rmap_fields[i] = devm_regmap_field_alloc(dev, bq->rmap,
							     reg_fields[i]);
		if (IS_ERR(bq->rmap_fields[i])) {
			dev_err(dev, "cannot allocate regmap field\n");
			ret = PTR_ERR(bq->rmap_fields[i]);
			goto extcon_fail;
		}
	}

	i2c_set_clientdata(client, bq);

	ret = bq25898_field_read(bq, F_REG14);
	bq->reg14 = ret;
	if (ret < 0) {
		dev_err(dev, "Cannot read chip ID. ret=%d\n", ret);
		goto extcon_fail;
	}

	if ((bq->reg14 & REG14_PN_MASK) != REG14_PN_BQ25898) {
		dev_err(dev, "Unsupported device. reg14=0x%.2x\n", bq->reg14);
		ret = -ENODEV;
		goto extcon_fail;
	}

	status_0b = bq25898_field_read(bq, F_REG0B);
	if (status_0b < 0) {
		dev_err(dev, "Cannot read status. status_0b=%d\n",
			status_0b
		);
		ret = status_0b;
		goto extcon_fail;
	}
	dev_info(dev, "Hello device. reg14=0x%.2x, status_0b=0x%.2x\n",
		bq->reg14, status_0b
	);

	if (!dev->platform_data) {
		ret = bq25898_of_probe(bq);
		if (ret != 0) {
			dev_err(dev, "Cannot read device properties.\n");
			goto extcon_fail;
		}
	} else {
		dev_err(dev, "Use device tree, "
			" doesn't support platform_data.\n");
		ret = -ENODEV;
		goto extcon_fail;
	}

	bq->dead_battery = false;
	if ((bq25898_dead_battery >= 0) &&
	    (bq25898_dead_battery < bq->dead_battery_uv)
	) {
		dev_notice(dev, "Dead battery boot. dead_battery=%d\n",
			bq25898_dead_battery
		);
		bq->dead_battery = true;
	}
	ret = bq25898_hw_init(bq);
	if (ret < 0) {
		dev_err(dev, "Can not initialize the chip.\n");
		goto extcon_fail;
	}
	ret = devm_request_threaded_irq(dev, client->irq, NULL,
		bq25898_irq_handler_thread,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		dev_name(&(client->dev)),
		bq
	);

	if (ret) {
		dev_err(dev, "Can not request IRQ. irq=%d, ret=%d\n",
			client->irq, ret
		);
		goto irq_fail;
	}
	set_bit(BQ25898_DEVICE_FLAGS_IRQ, &(bq->flags));

	if (bq->irq_wake_of) {
		/* Use IRQ wake. */
		device_set_wakeup_capable(dev, true);

		ret = device_set_wakeup_enable(dev, true);
		if (ret) {
			dev_err(dev, "Can not enable device wakeup. ret=%d\n",
				ret
			);
			goto irq_free;
		}

		ret = enable_irq_wake(client->irq);
		if (ret) {
			dev_err(dev, "Can not enable IRQ wake. irq=%d, ret=%d\n",
				client->irq, ret
			);
			goto irq_free;
		}
	} else {
		/* Don't use IRQ wake. */
		dev_notice(dev, "No IRQ wake.\n");
	}
	bq->temp_trip = false;
	bq->status_update = false;

	/* create sysfs */
	ret = sysfs_create_group(&(dev->kobj), &bq25898_attr_group);
	if (ret != 0) {
		dev_err(dev, "Can not create device attribute node. ret=%d\n",
			ret
		);
		goto irq_free;
	}

	bq->charger_thread = kthread_create(bq25898_charger_thread, bq,
		dev_name(dev)
	);
	if (IS_ERR_OR_NULL(bq->charger_thread)) {
		ret = PTR_ERR(bq->charger_thread);
		dev_err(dev, "Can not create thread. charger_thread=%d\n",
			ret
		);
		goto sysfs_free;
	}

	ret = bq25898_power_supply_init(bq);
	if (ret < 0) {
		dev_err(dev, "Failed to register power supply.\n");
		goto thread_stop;
	}

	/* Initially enable VBUS boost, hard wired logic controls
	 * Boosting battery and output power to VBUS.
	 * @note When OTG act as Device(Sink),
	 *       this configuration process will be fail.
	 *       BQ25898.OTG_CONFIG register doesn't change to 1 at
	 *       charging.
	 */
	ret = bq25898_vbus_boost_config(bq, true);
	if (ret != 0) {
		/* Can't configure VBUS_BOOST. */
		goto thread_stop;
	}

	/* Ignore error. */
	(void) bq25898_rdev_charger_register(bq);

	if (wake_up_process(bq->charger_thread) == 0) {
		/* Charger thread already running,
		 * Here, we first start thread.
		 */
		dev_warn(dev, "Charger thread already running.\n");
	}
	/* Do thread's work. */
	set_bit(CHARGER_EVENT_SYNC, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	wait_for_completion(&(bq->charger_sync));
	/* Ignore error. */
	(void) bq25898_probe_proxy(bq);
	return 0;
thread_stop:
	/* Stop events. */
	set_bit(BQ25898_DEVICE_FLAGS_REMOVE, &(bq->flags));
	kthread_stop(bq->charger_thread);
sysfs_free:
	sysfs_remove_group(&(dev->kobj), &bq25898_attr_group);
irq_free:
	if (bq->irq_wake_of) {
		/* Use IRQ wake. */
		disable_irq_wake(client->irq);
	}
	clear_bit(BQ25898_DEVICE_FLAGS_IRQ, &(bq->flags));
	devm_free_irq(dev, client->irq, bq);
irq_fail:

extcon_fail:
	bq25898_remove_extcon_chg_usb(bq);
	bq25898_remove_extcon_typec(bq);
	bq25898_remove_extcon_usb_config(bq);
	bq25898_remove_usb_otg(bq);
	return ret;
}

static int bq25898_remove(struct i2c_client *client)
{
	struct bq25898_device *bq = i2c_get_clientdata(client);
	dev_warn(bq->dev, "Unexpected remove call.\n");

	(void) bq25898_remove_proxy(bq);
	sysfs_remove_group(&(bq->dev->kobj), &bq25898_attr_group);
	bq25898_charger_thread_stop(bq);

	/* remove external event sources. */
	bq25898_remove_extcon_chg_usb(bq);
	bq25898_remove_extcon_typec(bq);
	bq25898_remove_extcon_usb_config(bq);
	bq25898_remove_usb_otg(bq);
	power_supply_unregister(bq->charger);

	if (!IS_ERR_OR_NULL(bq->usb_otg.phy))
		usb_unregister_notifier(bq->usb_otg.phy, &(bq->usb_otg.nb));

	bq25898_adc_config(bq, ADC_CONFIG_STOP);
	/* reset all registers to default values */
	bq25898_chip_reset(bq);
	if (bq->irq_wake_of) {
		/* Use IRQ wake. */
		disable_irq_wake(client->irq);
	}
	if (test_and_clear_bit(BQ25898_DEVICE_FLAGS_IRQ, &(bq->flags))) {
		/* IRQ requested. */
		devm_free_irq(bq->dev, client->irq, bq);
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bq25898_suspend(struct device *dev)
{
	struct bq25898_device *bq = dev_get_drvdata(dev);
	int	ret = 0;


	set_bit(CHARGER_EVENT_SUSPEND, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	wait_for_completion(&(bq->charger_suspend));
	ret = bq25898_adc_config(bq, ADC_CONFIG_STOP);
	return ret;
}

/* Power Off BQ25898
 * @bq driver context
 * @note This driver can't handle "partial power off".
 * "partial power off" means that power off BQ25898, but
 * other devices still keep power on.
 */
static void bq25898_poweroff(struct bq25898_device *bq)
{
	struct i2c_client	*client;
	int			batv, ret, capacity;
	union power_supply_propval val;

	client = container_of(bq->dev, struct i2c_client, dev);
	bq25898_charger_thread_stop(bq);

	bq25898_adc_config(bq, ADC_CONFIG_STOP);
	if (bq->state.pg_current == 0) {
		/* VBUS is not powered. */
		batv = bq->state.batv_current;
		val.intval = 0;
		ret = max1704x_get_property_proxy(
			POWER_SUPPLY_PROP_CAPACITY, &val
		);
		capacity = val.intval;
		if (batv >= bq->dead_battery_prepare_uv &&
		    !(ret == 0 && capacity <= bq->low_battery_capacity)) {
			/* Battery is charged enough to power up. */
			bq25898_charge_config(bq,
				CHARGE_CONFIG_NO_POWER,
				bq->ichg_down_current,
				bq->vreg_down_current
			);
		} else {
			/* Near dead battery voltage. */
			bq25898_charge_config(bq,
				CHARGE_CONFIG_NO_POWER_LOW,
				bq->ichg_down_current,
				bq->vreg_down_current
			);
		}
	}
	/* Anyway we force enable charge. */
	bq25898_field_write(bq, F_CHG_CONFIG, F_CHG_CONFIG_CHARGE);
	bq25898_field_write(bq, F_EN_HIZ, F_EN_HIZ_CONNECT);
	if (bq->irq_wake_of) {
		/* Use IRQ wake. */
		disable_irq_wake(client->irq);
	}
	if (test_and_clear_bit(BQ25898_DEVICE_FLAGS_IRQ, &(bq->flags))) {
		/* IRQ requested. */
		devm_free_irq(bq->dev, client->irq, bq);
	}
}

static int bq25898_pm_poweroff(struct device *dev)
{	struct bq25898_device *bq;

	bq = dev_get_drvdata(dev);
	bq25898_poweroff(bq);

	return 0; /* Always success. */
}


void bq25898_i2c_shutdown(struct i2c_client *client)
{	struct bq25898_device *bq = i2c_get_clientdata(client);

	bq25898_poweroff(bq);
}


static int bq25898_resume(struct device *dev)
{
	struct bq25898_device *bq = dev_get_drvdata(dev);

	/* NOTE: Here, we doesn't change soft_full_state,
	 * soft_full_state will be changed in charger thread.
	 */
	/* Configure ADC for continuous conversions.
	 * We read temp continuously, and feed temp to gauge.
	 */
	bq25898_adc_config(bq, ADC_CONFIG_AUTO);
	set_bit(CHARGER_EVENT_RESUME, &(bq->charger_event));
	bq25898_charger_thread_wake(bq);
	wait_for_completion(&(bq->charger_resume));
	return 0;
}
#endif

static const struct dev_pm_ops bq25898_pm = {
	.suspend = bq25898_suspend,
	.resume =  bq25898_resume,
	.freeze = bq25898_suspend,
	.thaw =   bq25898_resume,
	.poweroff = bq25898_pm_poweroff,
	.restore =  bq25898_resume,
};

static const struct i2c_device_id bq25898_i2c_ids[] = {
	{ "bq25898-icx", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq25898_i2c_ids);

static const struct of_device_id bq25898_of_match[] = {
	{ .compatible = "ti,bq25898-icx", },
	{ },
};
MODULE_DEVICE_TABLE(of, bq25898_of_match);

static const struct acpi_device_id bq25898_acpi_match[] = {
	{"BQ258980", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, bq25898_acpi_match);

static struct i2c_driver bq25898_driver = {
	.driver = {
		.name = "bq25898-icx-charger",
		.of_match_table = of_match_ptr(bq25898_of_match),
		.acpi_match_table = ACPI_PTR(bq25898_acpi_match),
		.pm = &bq25898_pm,
	},
	.probe = bq25898_probe,
	.remove = bq25898_remove,
	.shutdown = bq25898_i2c_shutdown,
	.id_table = bq25898_i2c_ids,
};
module_i2c_driver(bq25898_driver);

MODULE_AUTHOR("Laurentiu Palcu <laurentiu.palcu@intel.com>");
MODULE_DESCRIPTION("bq25898 charger driver");
MODULE_LICENSE("GPL");
