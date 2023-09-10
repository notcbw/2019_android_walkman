/*
 * cxd3778gf_interrupt.h
 *
 * CXD3778GF CODEC driver
 *
 * Copyright (c) 2013,2014,2015,2016,2018 Sony Corporation
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
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

#ifndef _CXD3778GF_INTERRUPT_HEADER_
#define _CXD3778GF_INTERRUPT_HEADER_

int cxd3778gf_interrupt_initialize(struct cxd3778gf_driver_data *ddata,
				   unsigned int type,
				   int headphone_detect_type);
int cxd3778gf_interrupt_finalize(struct cxd3778gf_driver_data *ddata,
				 unsigned int type,
				 int headphone_detect_type);
void cxd3778gf_interrupt_enable_wake(struct cxd3778gf_driver_data *ddata,
				     unsigned int type,
				     int headphone_detect_mode);
void cxd3778gf_interrupt_disable_wake(struct cxd3778gf_driver_data *ddata,
				      unsigned int type,
				      int headphone_detect_mode);
void cxd3778gf_interrupt_enable_irq(unsigned int type,
				    int headphone_detect_mode);
void cxd3778gf_interrupt_disable_irq(unsigned int type,
				     int headphone_detect_mode);
#endif
