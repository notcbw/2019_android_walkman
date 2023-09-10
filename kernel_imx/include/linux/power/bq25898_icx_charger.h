/*
 * TI BQ25898 (ICX) charger driver header to share chager API
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

#include <linux/extcon.h>
#include <linux/power_supply.h>

#if (!defined(INCLUDE_BQ25898_CHARGER_ICX_H))
#define INCLUDE_BQ25898_CHARGER_ICX_H

/* Type C CC signal detector result, propergate to charger driver.
 */
enum bq25898_icx_pd_properties {
	BQ25898_ICX_PD_UNKNOWN = 0,
	BQ25898_ICX_PD_SINK_STD,
	BQ25898_ICX_PD_SINK_1R5A,
	BQ25898_ICX_PD_SINK_3R0A,
	BQ25898_ICX_PD_POWER_MASK = 0x0f,
	/* Alias */
	BQ25898_ICX_PD_SINK_0R5A = BQ25898_ICX_PD_SINK_STD,
};

/* Power Supply Property Proxy
 * @psp	Power Supply Class Property No
 * @val	points property value to store
 * Return values:
 *  int == 0: Success
 *      == -EINVAL: Not supported property psp.
 *      == -EBUSY:  Requested property isn't available now.
 *      == -ENODEV: Driver or device is not present.
 *      == -EPROBE_DEFER: Not Initialized.
 */
int bq25898_power_supply_get_property_proxy(
	enum power_supply_property psp,
	union power_supply_propval *val);

extern atomic_t bq25898_detect_vbus;

#endif /* (!defined(INCLUDE_BQ25898_CHARGER_ICX_H)) */
