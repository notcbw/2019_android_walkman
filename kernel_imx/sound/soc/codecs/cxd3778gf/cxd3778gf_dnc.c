/*
 * cxd3778gf_dnc.c
 *
 * CXD3778GF CODEC driver
 *
 * Copyright 2013, 2014, 2015, 2016, 2017, 2018 Sony Corporation
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

/* #define TRACE_PRINT_ON */
/* #define DEBUG_PRINT_ON */
#define TRACE_TAG "------- "
#define DEBUG_TAG "        "

#include "cxd3778gf_common.h"

static struct cxd3778gf_dnc_interface * dnc_interface = NULL;

int cxd3778gf_dnc_register_module(struct cxd3778gf_dnc_interface * interface)
{
	print_trace("%s()\n",__FUNCTION__);

	dnc_interface = interface;

	return(0);
}

int cxd3778gf_dnc_initialize(void)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->initialize();
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_shutdown(void)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->shutdown();
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_prepare(void)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->prepare();
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_cleanup(void)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->cleanup();
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_judge(struct cxd3778gf_status *status)
{
	int rv = 0;

	print_trace("%s()\n", __func__);

	if (dnc_interface != NULL) {
		rv = dnc_interface->judge(status);
		if (rv < 0)
			return rv;
	}

	print_debug("noise cancel: mode = %d, status = %d\n",
			       status->noise_cancel_mode, rv);

	return rv;
}

int cxd3778gf_dnc_off(struct cxd3778gf_status *status)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->off(status);
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_set_user_gain(int index, int path)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv = dnc_interface->set_user_nc_gain(index, path);
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_get_user_gain(int * index)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	*index=0;

	if(dnc_interface!=NULL){
		rv = dnc_interface->get_user_nc_gain(index);
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_set_base_gain(int left, int right)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->set_base_gain(left,right);
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_get_base_gain(int * left, int * right)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	*left=0;
	*right=0;

	if(dnc_interface!=NULL){
		rv=dnc_interface->get_base_gain(left,right);
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_exit_base_gain_adjustment(int save)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(dnc_interface!=NULL){
		rv=dnc_interface->exit_base_gain_adjustment(save);
		if(rv<0)
			return(rv);
	}

	return(0);
}

int cxd3778gf_dnc_set_user_ambient_gain(int index, int path)
{
	int rv;

	pr_debug("%s()\n", __func__);

	if (dnc_interface != NULL) {
		rv = dnc_interface->set_user_ambient_gain(index, path);
		if (rv < 0)
			return rv;
	}

	return 0;
}

int cxd3778gf_dnc_mute_canvol(void)
{
	pr_debug("%s()\n", __func__);

	if (dnc_interface != NULL && dnc_interface->mute_canvol != NULL)
		dnc_interface->mute_canvol();

	return 0;
}

int cxd3778gf_dnc_set_canvol(struct cxd3778gf_status *status)
{
	pr_debug("%s()\n", __func__);

	if (dnc_interface != NULL && dnc_interface->set_canvol != NULL)
		dnc_interface->set_canvol(status, status->noise_cancel_active);

	return 0;
}

void cxd3778gf_dnc_mute_dnc1monvol(int mute)
{
	if (mute) {
		cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_H, 0x00);
		cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_L, 0x00);
	} else {
		cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_H, 0x20);
		cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_L, 0x00);
	}
}
