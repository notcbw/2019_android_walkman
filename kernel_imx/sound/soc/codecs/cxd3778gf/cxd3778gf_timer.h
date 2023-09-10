/*
 * Copyright 2016 Sony Corporation
 * File changed on 2016-01-20
 */
/*
 * cxd3778gf_timer.h
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

#ifndef _CXD3778GF_TIMER_HEADER_
#define _CXD3778GF_TIMER_HEADER_

int cxd3778gf_timer_initialize(unsigned int type, int headphone_detect_mode);
int cxd3778gf_timer_finalize(unsigned int type, int headphone_detect_mode);

#endif
