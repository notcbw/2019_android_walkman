/*
 * extcon-fusb303d_icx.h
 *
 * Copyright 2017, 2018 Sony Video & Sound Products Inc.
 * Author: Sony Video & Sound Products Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __FUSB303D_ICX_H__
#define __FUSB303D_ICX_H__

#include <linux/usb/typec.h>

#define FUSB303D_REG_RESERVED00		(0x00)
#define FUSB303D_REG_DEVICE_ID		(0x01)
#define FUSB303D_REG_DEVICE_TYPE	(0x02)
#define FUSB303D_REG_PORTROLE		(0x03)
#define FUSB303D_REG_CONTROL		(0x04)
#define FUSB303D_REG_CONTROL1		(0x05)
#define FUSB303D_REG_MANUAL		(0x09)
#define FUSB303D_REG_RESET		(0x0A)
#define FUSB303D_REG_MASK		(0x0E)
#define FUSB303D_REG_MASK1		(0x0F)
#define FUSB303D_REG_STATUS		(0x11)
#define FUSB303D_REG_STATUS1		(0x12)
#define FUSB303D_REG_TYPE		(0x13)
#define FUSB303D_REG_INTERRUPT		(0x14)
#define FUSB303D_REG_INTERRUPT1 	(0x15)

/* DEVICE_ID register */
#define	FUSB303D_VER_ID_MASK		(0xf0)
#define	FUSB303D_VER_ID_FUSB303_A	(0x10)
#define	FUSB303D_REV_ID_MASK		(0x0f)

/* DEVICE_TYPE */
#define	FUSB303D_DEVICE_TYPE_MASK	(0xff) /* All 8bits. */

/* PORTROLE register */
#define FUSB303D_ORIENTDEB	(0x40)
#define FUSB303D_TRY_MASK	(0x30)
#define FUSB303D_TRY_DISABLE	(0x30)
#define FUSB303D_TRY_SRC	(0x20)
#define FUSB303D_TRY_SNK	(0x10)
#define FUSB303D_TRY_NORMAL	(0x00)
#define FUSB303D_AUDIOACC	(0x08)
#define FUSB303D_DRP		(0x04)
#define FUSB303D_SNK		(0x02)
#define FUSB303D_SRC		(0x01)

#define FUSB303D_TRYDRPSNKSRC_MASK	( \
	  0x00 \
	| FUSB303D_TRY_MASK \
	| FUSB303D_DRP \
	| FUSB303D_SNK \
	| FUSB303D_SRC \
	)

/* CONTROL register */
#define FUSB303D_T_DRP_MASK	(0xC0)
#define FUSB303D_T_DRP_90MS	(0xC0)
#define FUSB303D_T_DRP_80MS	(0x80)
#define FUSB303D_T_DRP_70MS	(0x40)
#define FUSB303D_T_DRP_60MS	(0x00)
#define FUSB303D_DRPTOGGLE_MASK	(0x30)
#define FUSB303D_DRPTOGGLE_30P	(0x30) /*!< 30%: Unattached.SNK */
#define FUSB303D_DRPTOGGLE_40P	(0x20) /*!< 40%: Unattached.SNK */
#define FUSB303D_DRPTOGGLE_50P	(0x10) /*!< 50%: Unattached.SNK */
#define FUSB303D_DRPTOGGLE_60P	(0x00) /*!< 60%: Unattached.SNK */
#define FUSB303D_DCABLE_EN	(0x08)
#define FUSB303D_HOST_CUR_MASK	 (0x06)
#define FUSB303D_HOST_CUR_3000MA (0x06)
#define FUSB303D_HOST_CUR_1500MA (0x04)
#define FUSB303D_HOST_CUR_LEGACY (0x02)
#define FUSB303D_INT_MASK	 (0x01)

#define	FUSB303D_HOST_CUR_3000MA_MA	(TYPEC_CURRENT_3000MA)
#define	FUSB303D_HOST_CUR_1500MA_MA	(TYPEC_CURRENT_1500MA)
#define	FUSB303D_HOST_CUR_LEGACY_MA	(TYPEC_CURRENT_500MA)

/* CONTROL1 register */
#define FUSB303D_REMEDY_EN	  (0x80)
#define FUSB303D_AUTO_SNK_TH_MASK (0x60)
#define FUSB303D_AUTO_SNK_TH_3V3  (0x60)
#define FUSB303D_AUTO_SNK_TH_3V2  (0x40)
#define FUSB303D_AUTO_SNK_TH_3V1  (0x20)
#define FUSB303D_AUTO_SNK_TH_3V0  (0x00)
#define FUSB303D_AUTO_SNK_EN      (0x10)
#define FUSB303D_ENABLE           (0x08)
#define FUSB303D_TCCDEB_MASK      (0x07)
#define FUSB303D_TCCDEB_180MS     (0x06)
#define FUSB303D_TCCDEB_170MS     (0x05)
#define FUSB303D_TCCDEB_160MS     (0x04)
#define FUSB303D_TCCDEB_150MS     (0x03)
#define FUSB303D_TCCDEB_140MS     (0x02)
#define FUSB303D_TCCDEB_130MS     (0x01)
#define FUSB303D_TCCDEB_120MS     (0x00)

/* MANUAL register
 * @note FORCE_SRC .. UNATT_SRC, and ERR_REC bits behave
 * Write One Slef clearling
 */
#define FUSB303D_MANUAL_FORCE_SRC	(0x20)
#define FUSB303D_MANUAL_FORCE_SNK	(0x10)
#define FUSB303D_MANUAL_UNATT_SNK	(0x08)
#define FUSB303D_MANUAL_UNATT_SRC	(0x04)
#define FUSB303D_MANUAL_DISABLED	(0x02)
#define FUSB303D_MANUAL_ERROR_REC	(0x01)

/* RESET register */
#define FUSB303D_RESET_SW_RES	(0x01)

/* MASK register */
#define FUSB303D_M_ORIENT	(0x40)
#define FUSB303D_M_FAULT	(0x20)
#define FUSB303D_M_VBUS_CHG	(0x10)
#define FUSB303D_M_AUTOSNK	(0x08)
#define FUSB303D_M_BC_LVL	(0x04)
#define FUSB303D_M_DETACH	(0x02)
#define FUSB303D_M_ATTACH	(0x01)

/* STATUS register */
#define FUSB303D_AUTOSNK	(0x80)
#define FUSB303D_VSAFE0V	(0x40)
#define FUSB303D_ORIENT_MASK	(0x30)
#define FUSB303D_ORIENT_FAULT	(0x30)
#define FUSB303D_ORIENT_CC2	(0x20)
#define FUSB303D_ORIENT_CC1	(0x10)
#define FUSB303D_ORIENT_NONE	(0x00)
#define FUSB303D_VBUSOK		(0x08)
#define FUSB303D_BC_LVL_MASK	  (0x06)
#define FUSB303D_BC_LVL_RD_3000MA (0x06)
#define FUSB303D_BC_LVL_RD_1500MA (0x04)
#define FUSB303D_BC_LVL_RD_LEGACY (0x02)
#define FUSB303D_BC_LVL_RA_NONE   (0x00)
#define FUSB303D_ATTACH		  (0x01)

/* STATUS1 register */
#define FUSB303D_FAULT		(0x02)
#define FUSB303D_REMEDY		(0x01)

/* TYPE register */
#define FUSB303D_TYPE_MASK	(0x7B)
#define FUSB303D_DEBUGSRC	(0x40)
#define FUSB303D_DEBUGSNK	(0x20)
#define FUSB303D_SINK		(0x10)
#define FUSB303D_SOURCE		(0x08)
#define FUSB303D_ACTIVECABLE	(0x04)
#define FUSB303D_AUDIOVBUS	(0x02)
#define FUSB303D_AUDIO		(0x01)

/* MASK and INTERRUPT register */
#define FUSB303D_I_ORIENT	(0x40)
#define FUSB303D_I_FAULT	(0x20)
#define FUSB303D_I_VBUS_CHG	(0x10)
#define FUSB303D_I_AUTOSNK	(0x08)
#define FUSB303D_I_BC_LVL	(0x04)
#define FUSB303D_I_DETACH	(0x02)
#define FUSB303D_I_ATTACH	(0x01)

#define FUSB303D_I_ATT_ALL	(0x7f)

/* MASK1 and INTERRUPT1 register */
#define FUSB303D_I_REM_VBOFF	(0x40)
#define FUSB303D_I_REM_VBON	(0x20)
#define FUSB303D_I_REM_FAIL	(0x08)
#define FUSB303D_I_FRC_FAIL	(0x04)
#define FUSB303D_I_FRC_SUCC	(0x02)
#define FUSB303D_I_REMEDY	(0x01)

#define FUSB303D_I_REM_FRC_ALL	(0x6f)

/* Supports USB Type-C Revision 1.2
 * Note: It may be less revision 1.1
 * FUSB303D supports subset of USB Type-C specification.
 */
#define	FUSB303D_TYPEC_REVISION	(USB_TYPEC_REV_1_2)

#endif /* __FUSB303D_ICX_H__ */
