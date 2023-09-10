/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 */
/*
 * cxd3778gf_extcon.c
 *
 * CXD3778GF CODEC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* #define TRACE_PRINT_ON */
/* #define DEBUG_PRINT_ON */
#define TRACE_TAG "------- "
#define DEBUG_TAG "        "

#include "cxd3778gf_common.h"
static int initialized = FALSE;

#ifdef CONFIG_EXTCON

static const unsigned int cxd3778gf_extcon_cables[] = {
	EXTCON_JACK_HEADPHONE,
	EXTCON_NONE,
};

static const unsigned int cxd3778gf_extcon_cables_btl[] = {
	EXTCON_JACK_HEADPHONE,
	EXTCON_NONE,
};

/* this function is used for force shutdown in case of ucom abormal */
static const unsigned int cxd3778gf_extcon_ucom[] = {
        EXTCON_MECHANICAL,
        EXTCON_NONE,
};

static struct extcon_dev *headphone_extcon_dev;
static struct extcon_dev *headphone_btl_extcon_dev;
static struct extcon_dev *ucom_extcon_dev;

#endif

int cxd3778gf_extcon_initialize(struct device *dev)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

#ifdef CONFIG_EXTCON
	headphone_extcon_dev = devm_extcon_dev_allocate(dev, cxd3778gf_extcon_cables);
	if (IS_ERR(headphone_extcon_dev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -1;
	}

	headphone_btl_extcon_dev = devm_extcon_dev_allocate(dev, cxd3778gf_extcon_cables_btl);
	if (IS_ERR(headphone_btl_extcon_dev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -1;
	}

	rv = devm_extcon_dev_register(dev, headphone_extcon_dev);
	if (rv < 0) {
		dev_err(dev, "extcon_dev_register(headphone): code %d error occurred.\n", rv);
		back_trace();
		return rv;
	}

	rv = devm_extcon_dev_register(dev, headphone_btl_extcon_dev);
	if (rv < 0) {
		dev_err(dev, "extcon_dev_register(btl): code %d error occurred.\n", rv);
		back_trace();
		return rv;
	}

	ucom_extcon_dev = devm_extcon_dev_allocate(dev, cxd3778gf_extcon_ucom);
	if (IS_ERR(ucom_extcon_dev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -1;
	}

	rv = devm_extcon_dev_register(dev, ucom_extcon_dev);
	if (rv < 0) {
		dev_err(dev, "extcon_dev_register(headphone): code %d error occurred.\n", rv);
		back_trace();
		return rv;
	}
#endif

	initialized=TRUE;

	return 0;
}

int cxd3778gf_extcon_set_headphone_value(int value)
{
#ifdef CONFIG_EXTCON
	bool cable_state;
#endif

	print_trace("%s(%d)\n",__FUNCTION__,value);

#ifdef CONFIG_EXTCON
	switch (value) {
	case 0:
	case 1:
		cable_state = (value == 1);
		extcon_set_state_sync(headphone_extcon_dev, EXTCON_JACK_HEADPHONE, cable_state);
		break;
	case 2:
	case 3:
		cable_state = (value == 3);
		extcon_set_state_sync(headphone_btl_extcon_dev, EXTCON_JACK_HEADPHONE, cable_state);
		break;
	default:
		return -1;
	}
#endif

	return 0;
}

int cxd3778gf_extcon_set_ucom_value(int value)
{
	print_trace("%s(%d)\n", __func__, value);

#ifdef CONFIG_EXTCON
	extcon_set_state_sync(ucom_extcon_dev, EXTCON_MECHANICAL, value);
#endif
	return 0;
}
