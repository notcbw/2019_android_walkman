/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 */
/*
 * cxd3778gf_extcon.h
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

#ifndef _CXD3778GF_EXTCON_HEADER_
#define _CXD3778GF_EXTCON_HEADER_

int cxd3778gf_extcon_initialize(struct device *dev);
int cxd3778gf_extcon_set_headphone_value(int value);
int cxd3778gf_extcon_set_antenna_value(int value);
int cxd3778gf_extcon_set_ucom_value(int value);

#define UCOM_EXTCON_DEFAULT   0
#define UCOM_EXTCON_DEVCHKERR 1

#endif
