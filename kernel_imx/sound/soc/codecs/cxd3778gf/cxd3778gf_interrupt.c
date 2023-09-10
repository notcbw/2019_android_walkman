/*
 * Copyright 2016, 2018 Sony Corporation
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 */
/*
 * cxd3778gf_interrupt.c
 *
 * CXD3778GF CODEC driver
 *
 * Copyright (c) 2013-2016 Sony Corporation
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

#define HP_DET_EINT 35 /* GPIO 53 */

static irqreturn_t cxd3778gf_interrupt(int irq, void * data);

static int initialized = FALSE;

static irqreturn_t hp_det_irq_handler(int irq, void * data);
static irqreturn_t btl_det_irq_handler(int irq, void * data);

int cxd3778gf_interrupt_initialize(struct cxd3778gf_driver_data *ddata,
				   unsigned int type,
				   int headphone_detect_mode)
{
	int rv;
	int hp_detect_irq;

	print_trace("%s()\n",__FUNCTION__);

	if (initialized==FALSE) {
		if (headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT) {

			irq_set_irq_type (
				cxd3778gf_get_xpcm_det_irq(),
				IRQ_TYPE_EDGE_BOTH
			);

			rv = request_irq (
				cxd3778gf_get_xpcm_det_irq(),
				hp_det_irq_handler,
				0,
				"cxd3778gf_headphone_detect",
				ddata
			);
			if (rv < 0) {
				print_fail("request_irq(): code %d error occured.\n",rv);
				back_trace();
				return(rv);
			}

			if (type == TYPE_Z) {
				irq_set_irq_type(
					cxd3778gf_get_btl_det_irq(),
					IRQ_TYPE_EDGE_BOTH
				);

				rv = request_irq(
					cxd3778gf_get_btl_det_irq(),
					btl_det_irq_handler,
					0,
					"cxd3778gf_btl_detect",
					ddata
				);
				if (rv < 0) {
					print_fail("request_irq(): code %d error occured.\n",rv);
					back_trace();
					return(rv);
				}
			}

			if (ddata->codec->dev)
				device_init_wakeup(ddata->codec->dev, true);

			initialized=TRUE;
		}
	}

	return(0);
}

int cxd3778gf_interrupt_finalize(struct cxd3778gf_driver_data *ddata,
				 unsigned int type,
				 int headphone_detect_mode)
{
	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	if (headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT) {
		free_irq(cxd3778gf_get_xpcm_det_irq(), ddata);

		if (type == TYPE_Z)
			free_irq(cxd3778gf_get_btl_det_irq(), ddata);
	}
	initialized=FALSE;

	return(0);
}

void cxd3778gf_interrupt_enable_wake(struct cxd3778gf_driver_data *ddata,
				     unsigned int type,
				     int headphone_detect_mode)
{
	struct device *dev = ddata->codec->dev;

	if(!initialized)
		return;

	if ((headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT) &&
	    device_may_wakeup(dev)) {
		enable_irq_wake(cxd3778gf_get_xpcm_det_irq());

		if (type == TYPE_Z)
			enable_irq_wake(cxd3778gf_get_btl_det_irq());
	}
}

void cxd3778gf_interrupt_disable_wake(struct cxd3778gf_driver_data *ddata,
				      unsigned int type,
				      int headphone_detect_mode)
{
	struct device *dev = ddata->codec->dev;

	if(!initialized)
		return;

	if ((headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT) &&
	    device_may_wakeup(dev)) {
		disable_irq_wake(cxd3778gf_get_xpcm_det_irq());

		if (type == TYPE_Z)
			disable_irq_wake(cxd3778gf_get_btl_det_irq());
	}
}

void cxd3778gf_interrupt_enable_irq(unsigned int type,
				    int headphone_detect_mode)
{
	if(!initialized)
		return;

	if (headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT) {
		enable_irq(cxd3778gf_get_xpcm_det_irq());

		if (type == TYPE_Z)
			enable_irq(cxd3778gf_get_btl_det_irq());
	}
}

void cxd3778gf_interrupt_disable_irq(unsigned int type,
				     int headphone_detect_mode)
{
	if(!initialized)
		return;

	if (headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT) {
		disable_irq(cxd3778gf_get_xpcm_det_irq());

		if (type == TYPE_Z)
			disable_irq(cxd3778gf_get_btl_det_irq());
	}
}

static irqreturn_t cxd3778gf_interrupt(int irq, void * data)
{
#ifdef TRACE_PRINT_ON
	printk(KERN_INFO "####### cxd3778gf INTERRUPT\n");
#endif

	cxd3778gf_handle_pcm_event((struct cxd3778gf_driver_data *)data);

	return(IRQ_HANDLED);
}

static irqreturn_t hp_det_irq_handler(int irq, void * data)
{
#ifdef TRACE_PRINT_ON
        printk(KERN_INFO "####### cxd3778gf INTERRUPT HPDET\n");
#endif

	cxd3778gf_handle_hp_det_event((struct cxd3778gf_driver_data *)data);

	return(IRQ_HANDLED);
}

static irqreturn_t btl_det_irq_handler(int irq, void *data)
{
#ifdef TRACE_PRINT_ON
        printk(KERN_INFO "####### cxd3778gf INTERRUPT BTLDET\n");
#endif

	cxd3778gf_handle_hp_det_event((struct cxd3778gf_driver_data *)data);

	return(IRQ_HANDLED);
}
