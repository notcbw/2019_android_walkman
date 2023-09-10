/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 */

#ifndef _IMX_CXD3778GF_HEADER_
#define _IMX_CXD3778GF_HEADER_

#if defined(CONFIG_SND_SOC_IMX_CXD3778GF)
extern int imx_cxd3778gf_mute_on_trigger(void);
#else
static inline int imx_cxd3778gf_mute_on_trigger(void)
{
	return 0;
}
#endif

#endif
