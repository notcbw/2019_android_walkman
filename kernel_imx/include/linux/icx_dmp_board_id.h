/*
 * SVS ICX DMP board ID reader driver
 *
 * Copyright 2018 Sony Video & Sound Products Inc.
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
 *
 */

#if (!defined(ICX_DMP_BOARD_ID_H_INCLUDED))
#define ICX_DMP_BOARD_ID_H_INCLUDED

#if (defined(__KERNEL__))
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#endif /* (defined(__KERNEL__)) */

/* GPIO pin levels. */
#define	ICX_DMP_PIN_LOW		(0)
#define	ICX_DMP_PIN_HIGH	(1)
#define	ICX_DMP_PIN_OPEN	(2)	/* Open or Unknown. */
#define	ICX_DMP_PIN_LEVELS	(3)

/* atomic_t icx_dmp_board_id.init values
 * init will be negative errno number.
 */
#define	ICX_DMP_INIT_NOTYET	(0)
#define	ICX_DMP_INIT_DONE	(1)

/* icx_dmp_board_id.setid values */
#define	ICX_DMP_SETID_UNKNOWN	(0)
#define	ICX_DMP_SETID_ICX_1293	(1293)
#define	ICX_DMP_SETID_ICX_1295	(1295)

/* icx_dmp_board_id.bid values */
#define	ICX_DMP_BID_BB		(0x0)
#define	ICX_DMP_BID_LF		(0x1)
#define	ICX_DMP_BID_UNKNOWN	(~0UL)

/* icx_dmp_board_id.sid1 values
 * This value represents raw switch value.
 * See ICX_DMP_PIN_* macros.
 */
#define	ICX_DMP_SID1_ICX_1293	(0)
#define	ICX_DMP_SID1_ICX_1295	(1)

#if (defined(__KERNEL__))

/* gpio_descs index numbers */
#define	ICX_DMP_ID_GPIOS_BID1	(0)
#define	ICX_DMP_ID_GPIOS_BID2	(1)
#define	ICX_DMP_ID_GPIOS_BID3	(2)
#define	ICX_DMP_ID_GPIOS_BID_BASE	(ICX_DMP_ID_GPIOS_BID1)
#define	ICX_DMP_ID_GPIOS_BID_NUM	(3)

#define	ICX_DMP_ID_GPIOS_SID0	(3)
#define	ICX_DMP_ID_GPIOS_SID1	(4)
#define	ICX_DMP_ID_GPIOS_SID_BASE	(ICX_DMP_ID_GPIOS_SID0)
#define	ICX_DMP_ID_GPIOS_SID_NUM	(2)
#define	ICX_DMP_ID_GPIOS_NUMS	(5)

/* icx_dmp_board_id.cd_ins_level
 * Card Detect (Insert) pin levels.
 * Note that DEFAULT also means this board is not ICX DMP.
 */
#define	ICX_DMP_CD_INS_LEVEL_DEFAULT	(0)
#define	ICX_DMP_CD_INS_LEVEL_LOW	(1)
#define	ICX_DMP_CD_INS_LEVEL_HIGH	(2)

/* icx_dmp_board_id.cd_ins_pull
 * Card Detect (Insert) pull resistor switch selection.
 * Note that DEFAULT also means this board is not ICX DMP.
 */
#define	ICX_DMP_CD_INS_PULL_DEFAULT	(0)
#define	ICX_DMP_CD_INS_PULL_NONE	(1)
#define	ICX_DMP_CD_INS_PULL_DOWN	(2)
#define	ICX_DMP_CD_INS_PULL_UP		(3)

struct icx_dmp_board_id {
	struct platform_device	*pdev;

	atomic_t		init;
	struct gpio_descs	*gpios;
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pinctrl_default;
	/*! Configuration bitmap. T.B.D. */
	unsigned long long	config;
	unsigned long		setid;
	unsigned long	bid;	/*!< Board ID */
	int		sid0;	/*!< SID0 pin level. */
	int		sid1;	/*!< SID1 pin level. */
	int		cd_ins_level;
	int		cd_ins_pull;
	int		sysfs_ret;
};

extern struct icx_dmp_board_id	icx_dmp_board_id;

static inline struct device *icx_dmp_board_id_dev(
	struct icx_dmp_board_id *dmp)
{	if (!dmp)
		return NULL;

	return &(dmp->pdev->dev);
}
#endif /* (defined(__KERNEL__)) */

#endif /* (!defined(ICX_DMP_BOARD_ID_H_INCLUDED)) */
