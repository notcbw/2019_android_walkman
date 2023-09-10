/*
 * cxd3778gf_ext.h
 *
 * CXD3778GF external i2c driver
 *
 * Copyright (c) 2013,2014,2015,2016,2017,2018 Sony Corporation
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

#ifndef _CXD3778GF_EXT_HEADER_
#define _CXD3778GF_EXT_HEADER_

#define CXD3778GF_EXT_WAIT_TIME 2000

void cxd3778gf_ext_enable_i2c_bus(int);
void cxd3778gf_ext_reset(void);
void cxd3778gf_ext_start_fmonitor(void);
void cxd3778gf_ext_stop_fmonitor(void);
void cxd3778gf_ext_set_gain_index(int index);
void cxd3778gf_ext_restore_preamp(void);

#endif /* _CXD3778GF_EXT_HEADER_ */
