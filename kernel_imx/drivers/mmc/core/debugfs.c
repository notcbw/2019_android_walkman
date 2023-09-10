/*
 * Debugfs support for hosts and cards
 *
 * Copyright (C) 2008 Atmel Corporation
 * Copyright 2018,2019 Sony Video & Sound Products Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/fault-inject.h>
#include <linux/uaccess.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "core.h"
#include "card.h"
#include "host.h"
#include "mmc_ops.h"
#include "sd_ops.h"

#ifdef CONFIG_FAIL_MMC_REQUEST

static DECLARE_FAULT_ATTR(fail_default_attr);
static char *fail_request;
module_param(fail_request, charp, 0);

#endif /* CONFIG_FAIL_MMC_REQUEST */

/* The debugfs functions are optimized away when CONFIG_DEBUG_FS isn't set. */
static int mmc_ios_show(struct seq_file *s, void *data)
{
	static const char *vdd_str[] = {
		[8]	= "2.0",
		[9]	= "2.1",
		[10]	= "2.2",
		[11]	= "2.3",
		[12]	= "2.4",
		[13]	= "2.5",
		[14]	= "2.6",
		[15]	= "2.7",
		[16]	= "2.8",
		[17]	= "2.9",
		[18]	= "3.0",
		[19]	= "3.1",
		[20]	= "3.2",
		[21]	= "3.3",
		[22]	= "3.4",
		[23]	= "3.5",
		[24]	= "3.6",
	};
	struct mmc_host	*host = s->private;
	struct mmc_ios	*ios = &host->ios;
	const char *str;

	if (host->card)
		mmc_get_card(host->card, NULL);

	seq_printf(s, "clock:\t\t%u Hz\n", ios->clock);
	if (host->actual_clock)
		seq_printf(s, "actual clock:\t%u Hz\n", host->actual_clock);
	seq_printf(s, "vdd:\t\t%u ", ios->vdd);
	if ((1 << ios->vdd) & MMC_VDD_165_195)
		seq_printf(s, "(1.65 - 1.95 V)\n");
	else if (ios->vdd < (ARRAY_SIZE(vdd_str) - 1)
			&& vdd_str[ios->vdd] && vdd_str[ios->vdd + 1])
		seq_printf(s, "(%s ~ %s V)\n", vdd_str[ios->vdd],
				vdd_str[ios->vdd + 1]);
	else
		seq_printf(s, "(invalid)\n");

	switch (ios->bus_mode) {
	case MMC_BUSMODE_OPENDRAIN:
		str = "open drain";
		break;
	case MMC_BUSMODE_PUSHPULL:
		str = "push-pull";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "bus mode:\t%u (%s)\n", ios->bus_mode, str);

	switch (ios->chip_select) {
	case MMC_CS_DONTCARE:
		str = "don't care";
		break;
	case MMC_CS_HIGH:
		str = "active high";
		break;
	case MMC_CS_LOW:
		str = "active low";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "chip select:\t%u (%s)\n", ios->chip_select, str);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		str = "off";
		break;
	case MMC_POWER_UP:
		str = "up";
		break;
	case MMC_POWER_ON:
		str = "on";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "power mode:\t%u (%s)\n", ios->power_mode, str);
	seq_printf(s, "bus width:\t%u (%u bits)\n",
			ios->bus_width, 1 << ios->bus_width);

	switch (ios->timing) {
	case MMC_TIMING_LEGACY:
		str = "legacy";
		break;
	case MMC_TIMING_MMC_HS:
		str = "mmc high-speed";
		break;
	case MMC_TIMING_SD_HS:
		str = "sd high-speed";
		break;
	case MMC_TIMING_UHS_SDR12:
		str = "sd uhs SDR12";
		break;
	case MMC_TIMING_UHS_SDR25:
		str = "sd uhs SDR25";
		break;
	case MMC_TIMING_UHS_SDR50:
		str = "sd uhs SDR50";
		break;
	case MMC_TIMING_UHS_SDR104:
		str = "sd uhs SDR104";
		break;
	case MMC_TIMING_UHS_DDR50:
		str = "sd uhs DDR50";
		break;
	case MMC_TIMING_MMC_DDR52:
		str = "mmc DDR52";
		break;
	case MMC_TIMING_MMC_HS200:
		str = "mmc HS200";
		break;
	case MMC_TIMING_MMC_HS400:
		str = mmc_card_hs400es(host->card) ?
			"mmc HS400 enhanced strobe" : "mmc HS400";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "timing spec:\t%u (%s)\n", ios->timing, str);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		str = "3.30 V";
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		str = "1.80 V";
		break;
	case MMC_SIGNAL_VOLTAGE_120:
		str = "1.20 V";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "signal voltage:\t%u (%s)\n", ios->signal_voltage, str);

	switch (ios->drv_type) {
	case MMC_SET_DRIVER_TYPE_A:
		str = "driver type A";
		break;
	case MMC_SET_DRIVER_TYPE_B:
		str = "driver type B";
		break;
	case MMC_SET_DRIVER_TYPE_C:
		str = "driver type C";
		break;
	case MMC_SET_DRIVER_TYPE_D:
		str = "driver type D";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "driver type:\t%u (%s)\n", ios->drv_type, str);

	if (host->card)
		mmc_put_card(host->card, NULL);

	return 0;
}

static int mmc_ios_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_ios_show, inode->i_private);
}

static const struct file_operations mmc_ios_fops = {
	.open		= mmc_ios_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int mmc_clock_opt_get(void *data, u64 *val)
{
	struct mmc_host *host = data;

	if (host->card)
		mmc_get_card(host->card, NULL);
	*val = host->ios.clock;
	if (host->card)
		mmc_put_card(host->card, NULL);

	return 0;
}

static int mmc_clock_opt_set(void *data, u64 val)
{
	struct mmc_host *host = data;

	/* We need this check due to input value is u64 */
	if (val != 0 && (val > host->f_max || val < host->f_min)) {
		dev_err(mmc_dev(host), "Clock out of range. max=%u, min=%u\n",
			host->f_max, host->f_min
		);
		return -EINVAL;
	}

	mmc_claim_host(host);
	mmc_set_clock(host, (unsigned int) val);
	mmc_release_host(host);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmc_clock_fops, mmc_clock_opt_get, mmc_clock_opt_set,
	"%llu\n");


static int mmc_clock_limit_opt_get(void *data, u64 *val)
{
	struct mmc_host *host = data;

	if (host->card)
		mmc_get_card(host->card, NULL);
	*val = host->f_max_limit;
	if (host->card)
		mmc_put_card(host->card, NULL);

	return 0;
}

static int mmc_clock_limit_opt_set(void *data, u64 val)
{
	struct mmc_host *host = data;

	/* We need this check due to input value is u64 */
	if (val < host->f_min) {
		dev_err(mmc_dev(host), "Limit to minimum. min=%u\n",
			host->f_min
		);
		val = host->f_min;
	}
	if (val > host->f_max) {
		dev_info(mmc_dev(host), "Unlimited. max=%u\n",
			host->f_max
		);
		val = host->f_max;
	}

	host->f_max_limit = (__force unsigned int) val;

	mmc_claim_host(host);
	/* Recover wanted clock. */
	mmc_set_clock(host, (unsigned int) host->ios.clock);
	mmc_release_host(host);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mmc_clock_limit_fops,
	mmc_clock_limit_opt_get, mmc_clock_limit_opt_set,
	"%llu\n");

static int mmc_pinctrl_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t mmc_pinctrl_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	struct inode *inode = filp->f_inode;
	struct mmc_host *host = inode->i_private;

	return simple_read_from_buffer(ubuf, cnt, ppos,
		host->debug_pinctrl,
		host->debug_pinctrl_len
	);
}

static char *str_skip_to_space(char *p, ssize_t size)
{	while (size > 0) {
		/* Loop until buffer end. */
		if ((unsigned char)(*p) <= (unsigned char)' ') {
			/* "Space" or "Control Codes" */
			break;
		}
		p++;
		size--;
	}
	return p;
}

static const char * const pinctrl_settings[] = {
	[MMC_HOST_DEBUG_PINCTRL_NORMAL] = "normal",
	[MMC_HOST_DEBUG_PINCTRL_SKIP] = "skip",
};

/* Write pinctrl
 * ONE transaction be done by ONE write system call.
 */

static ssize_t mmc_pinctrl_write(struct file *filp,
	const char __user *ubuf, size_t wlen, loff_t *ppos)
{
	struct inode *inode = filp->f_inode;
	struct mmc_host *host = inode->i_private;
	const ssize_t	max_size = MMC_HOST_DEBUG_PINCTRL_SIZE;
	char	*p;
	int	select;

	if (!capable(CAP_SYS_ADMIN)) {
		dev_err(host->parent, "Should be administrator.\n");
		return -EPERM;
	}
	if (wlen >= sizeof(host->debug_pinctrl)) {
		dev_err(host->parent, "Too long request. wlen=%ld\n",
			(long)wlen
		);
		return -EINVAL;
	}
	host->debug_pinctrl[0] = 0;
	if (copy_from_user(host->debug_pinctrl, ubuf, wlen)) {
		dev_err(host->parent, "May using invalid buffer. ubuf=0x%p\n",
			ubuf
		);
		return -EFAULT;
	}

	host->debug_pinctrl[max_size - 1] = 0;
	p = str_skip_to_space(host->debug_pinctrl,
		sizeof(host->debug_pinctrl) - 1);
	*p = 0;
	/* Pointer subtraction. */
	host->debug_pinctrl_len = p - &(host->debug_pinctrl[0]);
	select = MMC_HOST_DEBUG_PINCTRL_NORMAL;
	if (host->debug_pinctrl_len != 0) {
		/* Set some strings. */
		select = match_string(pinctrl_settings,
			ARRAY_SIZE(pinctrl_settings),
			host->debug_pinctrl
		);
		if (select < 0) {
			/* Invalid pinctrl setting. */
			dev_err(host->parent, "Invalid pinctrl. select=%d\n",
				select
			);
			return select;
		}
	}
	host->debug_pinctrl_select = select;
	dev_notice(host->parent, "Set pinctrl. pinctrl=%s\n",
		host->debug_pinctrl
	);
	/* Consume all written bytes. */
	*ppos = *ppos + wlen;
	return wlen;
}

static int mmc_pinctrl_release(struct inode *inode, struct file *file)
{
	return 0;
}


static const struct file_operations mmc_dbg_pinctrl_fops = {
	.open		= mmc_pinctrl_open,
	.read		= mmc_pinctrl_read,
	.write		= mmc_pinctrl_write,
	.release	= mmc_pinctrl_release,
	.llseek		= default_llseek,
};

static int mmc_cd_status_opt_get(void *data, u64 *val)
{
	struct mmc_host *host = data;

	if (host->ops && host->ops->get_cd) {
		*val = host->ops->get_cd(host);
		return 0;
	} else {
		*val = 0;
		return -ENOSYS;
	}
}

DEFINE_SIMPLE_ATTRIBUTE(mmc_cd_status_fops,
	mmc_cd_status_opt_get, NULL,
	"%llu\n");

void mmc_add_host_debugfs(struct mmc_host *host)
{
	struct dentry *root;

	root = debugfs_create_dir(mmc_hostname(host), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	host->debugfs_root = root;

	if (!debugfs_create_file("ios", S_IRUSR, root, host, &mmc_ios_fops))
		goto err_node;

	if (!debugfs_create_file("clock", S_IRUSR | S_IWUSR, root, host,
			&mmc_clock_fops))
		goto err_node;

	if (!debugfs_create_file("clock_limit", S_IRUSR | S_IWUSR, root, host,
			&mmc_clock_limit_fops))
		goto err_node;

	if (!debugfs_create_file("pinctrl", S_IRUSR | S_IWUSR, root, host,
			&mmc_dbg_pinctrl_fops))
		goto err_node;

	if (!debugfs_create_file("cd_status", S_IRUSR, root, host,
			&mmc_cd_status_fops))
		goto err_node;

#ifdef CONFIG_FAIL_MMC_REQUEST
	if (fail_request)
		setup_fault_attr(&fail_default_attr, fail_request);
	host->fail_mmc_request = fail_default_attr;
	if (IS_ERR(fault_create_debugfs_attr("fail_mmc_request",
					     root,
					     &host->fail_mmc_request)))
		goto err_node;
#endif
	return;

err_node:
	debugfs_remove_recursive(root);
	host->debugfs_root = NULL;
err_root:
	dev_err(&host->class_dev, "failed to initialize debugfs\n");
}

void mmc_remove_host_debugfs(struct mmc_host *host)
{
	debugfs_remove_recursive(host->debugfs_root);
}

void mmc_add_card_debugfs(struct mmc_card *card)
{
	struct mmc_host	*host = card->host;
	struct dentry	*root;

	if (!host->debugfs_root)
		return;

	root = debugfs_create_dir(mmc_card_id(card), host->debugfs_root);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err;

	card->debugfs_root = root;

	if (!debugfs_create_x32("state", S_IRUSR, root, &card->state))
		goto err;

	return;

err:
	debugfs_remove_recursive(root);
	card->debugfs_root = NULL;
	dev_err(&card->dev, "failed to initialize debugfs\n");
}

void mmc_remove_card_debugfs(struct mmc_card *card)
{
	debugfs_remove_recursive(card->debugfs_root);
	card->debugfs_root = NULL;
}
