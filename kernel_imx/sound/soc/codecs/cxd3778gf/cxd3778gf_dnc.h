/*
 * cxd3778gf_dnc.h
 *
 * CXD3778GF CODEC driver
 *
 * Copyright 2013-2016, 2017 Sony Corporation
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

#ifndef _CXD3778GF_DNC_HEADER_
#define _CXD3778GF_DNC_HEADER_

int cxd3778gf_dnc_register_module(struct cxd3778gf_dnc_interface * interface);
int cxd3778gf_dnc_initialize(void);
int cxd3778gf_dnc_shutdown(void);
int cxd3778gf_dnc_prepare(void);
int cxd3778gf_dnc_cleanup(void);
int cxd3778gf_dnc_judge(struct cxd3778gf_status *status);
int cxd3778gf_dnc_off(struct cxd3778gf_status *status);
int cxd3778gf_dnc_set_user_gain(int index, int path);
int cxd3778gf_dnc_get_user_gain(int * index);
int cxd3778gf_dnc_set_base_gain(int left, int right);
int cxd3778gf_dnc_get_base_gain(int * left, int * right);
int cxd3778gf_dnc_exit_base_gain_adjustment(int save);
int cxd3778gf_dnc_set_user_ambient_gain(int index, int path);
int cxd3778gf_dnc_mute_canvol(void);
int cxd3778gf_dnc_set_canvol(struct cxd3778gf_status *status);
void cxd3778gf_dnc_mute_dnc1monvol(int mute);

#endif
