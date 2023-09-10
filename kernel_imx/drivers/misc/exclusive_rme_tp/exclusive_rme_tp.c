// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Sony Video & Sound Products Inc.
 *
 */


#include <linux/mutex.h>

static DEFINE_MUTEX(exclusive_rme_tp);

void exc_rme_tp_lock(void)
{
#ifdef CONFIG_EXCLUSIVE_RME_TP
	mutex_lock(&exclusive_rme_tp);
#endif /* CONFIG_EXCLUSIVE_RME_TP */
}

void exc_rme_tp_unlock(void)
{
#ifdef CONFIG_EXCLUSIVE_RME_TP
	mutex_unlock(&exclusive_rme_tp);
#endif /* CONFIG_EXCLUSIVE_RME_TP */
}
