/*
 * Copyright 2016,2018 Sony Corporation
 */
/*
 * include/linux/ts_icx.h - platform data structure
 *
 * Copyright (C) 2015 Sony Corporation
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

#ifndef _LINUX_TS_ICX_H
#define _LINUX_TS_ICX_H

struct ts_icx_platform_data {
	uint32_t      flags;
	unsigned int  i2c_timing;

	unsigned int  xrst_gpio; 
	unsigned int  xint_gpio;
	unsigned int  xint_eint;
	unsigned long irqflags;

	int8_t sensitivity_adjust;

	int min_x;
	int min_y;
	int max_x;
	int max_y; /* without virtual key area */

	int (*init)(void);
	int (*power)(int on);	/* Only valid in first array entry */
};

#endif /* _LINUX_TS_ICX_H */
