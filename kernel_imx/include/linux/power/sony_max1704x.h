/*
 * sony_max1704x.h
 *
 * Copyright 2017,2018,2019 Sony Video & Sound Products Inc.
 * Author: Sony Video & Sound Products Inc.
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

#ifndef __SONY_MAX1704X_H__
#define __SONY_MAX1704X_H__

#define MAX1704X_REG_VCELL   0x02
#define MAX1704X_REG_SOC     0x04
#define MAX1704X_REG_MODE    0x06
#define MAX1704X_REG_VERSION 0x08
#define MAX1704X_REG_HIBRT   0x0A
#define MAX1704X_REG_CONFIG  0x0C
#define MAX1704X_REG_OCV     0x0E
#define MAX1704X_REG_VALRT   0x14
#define MAX1704X_REG_CRATE   0x16
#define MAX1704X_REG_VRESET  0x18
#define MAX1704X_REG_STATUS  0x1A
#define MAX1704X_REG_LOCK    0x3E
#define MAX1704X_REG_TABLE   0x40
#define MAX1704X_REG_RCOMP   0x80
#define MAX1704X_REG_CMD     0xFE

#define MAX1704X_MODE_ENSLEEP 0x2000

#define MAX1704X_HIBRT_DISABLE 0x0000
#define MAX1704X_HIBRT_DEFAULT 0x8030
#define MAX1704X_HIBRT_ALWAYS  0xFFFF

#define MAX1704X_CONFIG_ALRT  0x0020
#define MAX1704X_CONFIG_ALSC  0x0040
#define MAX1704X_CONFIG_SLEEP 0x0080
#define MAX1704X_CONFIG_RCOMP 0xFF00

#define MAX1704X_VRESET_MASK  0xFE00

#define MAX1704X_STATUS_RI    0x0100
#define MAX1704X_STATUS_SC    0x2000
#define MAX1704X_STATUS_ALART_MASK 0x3E00

#define MAX1704X_LOCK_ENABLE  0x0000
#define MAX1704X_LOCK_DISABLE 0x4A57

#define MAX1704X_LOCK_TRY_COUNT 3000
#define MAX1704X_LOAD_TRY_COUNT 10

#define MAX1704X_VCELL_RATE 78125ll /* 78125nV / 1digit */
#define MAX1704X_SOC_RATE     256   /* 1/256%  / 1digit */

#define MAX1704X_VOLTAGE_MAX 8200000
#define MAX1704X_CAPACITY_MAX 100

#define MAX1704X_MODEL_DATA_SIZE 64
#define MAX1704X_RCOMP_DATA_SIZE 32

struct sony_max1704x_thermal_info {
	int temperature; /* degree x10 */
	int voltage;     /* millivolts */
};

struct sony_max1704x_platform_data {
	unsigned int  i2c_speed;
	int           vcell_reset;
	int           shutdown_voltage;
	int           startup_guard_voltage;
	int           startup_guard_time;
	int           status_interval;
	int           full_battery_capacity;
	int           vcell_rate;
	/* temp adjust */
	int                                      adjust_interval;
	int                                      thermal_channel;
	int                                      thermal_samples;
	const struct sony_max1704x_thermal_info *thermal_table;
	int                                      thermal_table_count;
	/* model data */
	int		empty_adjustment;
	int		full_adjustment;
	unsigned short	rcomp_ini;
	int		temp_co_up;
	int		temp_co_down;
	unsigned short	ocv_test;
	unsigned short	check_min;
	unsigned short	check_max;
	unsigned char	rcomp_data[MAX1704X_RCOMP_DATA_SIZE];
	unsigned char	model_data[MAX1704X_MODEL_DATA_SIZE];
};

/* Get Property Proxy
 * @psp	Power Supply Class Property No
 * @val	points property value to store
 * Return values:
 *  int == 0: Success
 *      == -EINVAL: Not supported property psp.
 *      == -EBUSY:  Requested property isn't available now.
 *      == -ENODEV: Driver or device is not present.
 *      == -EPROBE_DEFER: Not Initialized.
 */
int max1704x_get_property_proxy(
	enum power_supply_property psp,
	union power_supply_propval *val);

#endif
