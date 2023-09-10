// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Sony Video & Sound Products Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>

#include <sound/soc.h>

#include <sound/lif-md6000-rme.h>

/* #define TRACE_PRINT_ON */
#define TRACE_TAG "####### "

/* trace print macro */
#ifdef TRACE_PRINT_ON
	#define print_trace(fmt, args...) pr_info(TRACE_TAG "" fmt, ##args)
#else
	#define print_trace(fmt, args...) pr_debug(TRACE_TAG "" fmt, ##args)
#endif

struct md6000_driver_data {
	struct i2c_client *client;
	struct mutex mutex;
	int gpio_rme_wkup;
};

static struct md6000_driver_data *md6000_drvdata;

static int md6000_register_read(struct md6000_driver_data *ddata,
				unsigned int address,
				unsigned char *value)
{
	struct i2c_msg msg[2];
	int rv = 0;

	msg[0].addr = ddata->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = (unsigned char *)&address;
	msg[1].addr = ddata->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = value;
	rv = i2c_transfer(ddata->client->adapter, msg, 2);
	if (rv < 0) {
		dev_err(&ddata->client->dev, "%s() failed, rv:%d, addr:%x, value:%x\n",
			__func__, rv, address, *value);
		return rv;
	}

	return 0;
}

static int md6000_register_write(struct md6000_driver_data *ddata,
				 unsigned int address,
				 unsigned char value)
{
	struct i2c_msg msg;
	unsigned char data[2];
	int rv = 0;

	data[0] = address;
	data[1] = value;

	msg.addr = ddata->client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = data;
	rv = i2c_transfer(ddata->client->adapter, &msg, 1);
	if (rv < 0) {
		dev_err(&ddata->client->dev, "%s() failed, rv:%d, addr:%x, value:%x\n",
			__func__, rv, address, value);
		return rv;
	}

	return 0;
}

static int md6000_update_bits(struct md6000_driver_data *ddata,
			      unsigned int address,
			      unsigned int mask,
			      unsigned int value)
{
	unsigned char rd;
	unsigned char old;
	unsigned char new;

	dev_dbg(&ddata->client->dev, "%s(0x%02X, 0x%02X, 0x%02X)\n",
				__func__, address, mask, value);

	mutex_lock(&ddata->mutex);

	md6000_register_read(ddata, address, &rd);
	old = rd;
	new = (old & ~mask) | (value & mask);
	md6000_register_write(ddata, address, new);

	mutex_unlock(&ddata->mutex);

	return 0;
}

int md6000_wake(unsigned int wake)
{
	struct md6000_driver_data *ddata = md6000_drvdata;

	print_trace("%s(%d)\n", __func__, wake);

	if (!ddata)
		return -ENOMEM;

	if (wake) {
		if (gpio_is_valid(ddata->gpio_rme_wkup)) {
			gpio_set_value(ddata->gpio_rme_wkup, 1);
			usleep_range(1000, 1200);
			gpio_set_value(ddata->gpio_rme_wkup, 0);
			usleep_range(1000, 1200);
		}
	} else {
		md6000_update_bits(ddata, MD6000_REG_CLK_DIV,
			MD6000_REG_CLK_DIV_SLEEP, 1 << 5);
	}

	return 0;
}
EXPORT_SYMBOL(md6000_wake);

int md6000_setup_params(unsigned int format, unsigned int rate)
{
	struct md6000_driver_data *ddata = md6000_drvdata;
	u8 pdout = 0;
	u8 pdin = 0;
	u8 fs = 0;
	u8 rme_fs = 0;
	u8 bclk = 0;
	u8 mclk = 0;

	print_trace("%s() format=%d, rate=%d\n", __func__, format, rate);

	if (!ddata)
		return -ENOMEM;

	md6000_wake(1);

	/* AUDIO IN/OUT disable */
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_OUT_ENABLE_MSK, 0 << 3);
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_IN_ENABLE_MSK, 0 << 7);

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		switch (rate) {
		case 44100:
		case 48000:
			fs = 0;
			break;
		case 88200:
		case 96000:
			fs = 1;
			break;
		case 176400:
		case 192000:
			fs = 2;
			break;
		case 352800:
		case 384000:
			fs = 3;
			break;
		default:
			return -EINVAL;
		}
		pdout = 1;
		pdin = 1;
		bclk = 0;
		mclk = 0;
		/* I2S mode */
		md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
				MD6000_REG_AUDIO_FORMAT_OUT_I2S_MSK, 1 << 1);
		md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
				MD6000_REG_AUDIO_FORMAT_IN_I2S_MSK, 1 << 5);
		break;
	case SND_SOC_DAIFMT_PDM:
		switch (rate) {
		case 88200:  /* DSD 2.8MHz */
			fs = 0;
			rme_fs = 0;
			break;
		case 176400: /* DSD 5.6MHz */
			fs = 1;
			rme_fs = 5;
			break;
		case 352800: /* DSD 11.2MHz */
			fs = 2;
			rme_fs = 10;
			break;
		default:
			return -EINVAL;
		}
		pdout = 0;
		pdin = 0;
		bclk = 1;
		mclk = 1;
		/* FADER */
		md6000_update_bits(ddata, MD6000_REG_FADER0_H,
				MD6000_REG_FADER0_H_MASK, 0);
		md6000_update_bits(ddata, MD6000_REG_FADER0_M,
				MD6000_REG_FADER0_M_MASK, 0);
		md6000_update_bits(ddata, MD6000_REG_FADER0_L,
				MD6000_REG_FADER0_L_MASK, 0);
		/* RME FS */
		md6000_update_bits(ddata, MD6000_REG_SYSTEM,
				MD6000_REG_SYSTEM_RME_FS_MSK, rme_fs << 0);
		/* RME enable */
		md6000_update_bits(ddata, MD6000_REG_SYSTEM,
				MD6000_REG_SYSTEM_STOP_MSK, 0 << 4);
		break;
	default:
		return -EINVAL;
	}

	/* PCM/DSD mode */
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_OUT_PCM_DSD_MSK, pdout << 0);
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_IN_PCM_DSD_MSK, pdin << 4);
	/* AUDIO IN FS */
	md6000_update_bits(ddata, MD6000_REG_CLK_DIV,
			MD6000_REG_CLK_DIV_AUDIO_IN_FS_MSK, fs << 0);
	/* BCK enable */
	md6000_update_bits(ddata, MD6000_REG_CLK_DIV,
			MD6000_REG_CLK_DIV_BCK_EN_MSK, bclk << 6);
	/* MCK enable */
	md6000_update_bits(ddata, MD6000_REG_CLK_DIV,
			MD6000_REG_CLK_DIV_MCK_EN_MSK, mclk << 7);
	/* AUDIO IN/OUT enable */
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_OUT_ENABLE_MSK, 1 << 3);
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_IN_ENABLE_MSK, 1 << 7);
	/* EMUTE normal */
	md6000_update_bits(ddata, MD6000_REG_MUTE,
			MD6000_REG_MUTE_EMUTE_MSK, 0 << 1);

	return 0;
}
EXPORT_SYMBOL(md6000_setup_params);

int md6000_mute(unsigned int mute)
{
	struct md6000_driver_data *ddata = md6000_drvdata;

	print_trace("%s(%d)\n", __func__, mute);

	if (!ddata)
		return -ENOMEM;

	if (mute == 0) {
		/* unmute */
		md6000_update_bits(ddata, MD6000_REG_MUTE,
				MD6000_REG_MUTE_SMUTE_MSK, 0 << 0);
	} else {
		/* mute */
		md6000_update_bits(ddata, MD6000_REG_MUTE,
				MD6000_REG_MUTE_SMUTE_MSK, 1 << 0);
	}

	return 0;
}
EXPORT_SYMBOL(md6000_mute);

int md6000_fader(unsigned int volume)
{
	struct md6000_driver_data *ddata = md6000_drvdata;

	print_trace("%s(%x)\n", __func__, volume);

	if (!ddata)
		return -ENOMEM;

	md6000_update_bits(ddata, MD6000_REG_FADER0_H,
				MD6000_REG_FADER0_H_MASK, 0x00);
	md6000_update_bits(ddata, MD6000_REG_FADER0_M,
				MD6000_REG_FADER0_M_MASK, (volume & 0xff00) >> 8);
	md6000_update_bits(ddata, MD6000_REG_FADER0_L,
				MD6000_REG_FADER0_L_MASK, (volume & 0x00ff));

	return 0;
}
EXPORT_SYMBOL(md6000_fader);

int md6000_setup_free(void)
{
	struct md6000_driver_data *ddata = md6000_drvdata;

	print_trace("%s()\n", __func__);

	if (!ddata)
		return -ENOMEM;

	/* PCM mode */
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_OUT_PCM_DSD_MSK, 1 << 0);
	md6000_update_bits(ddata, MD6000_REG_AUDIO_FORMAT,
			MD6000_REG_AUDIO_FORMAT_IN_PCM_DSD_MSK, 1 << 4);
	/* RME stop */
	md6000_update_bits(ddata, MD6000_REG_SYSTEM,
			MD6000_REG_SYSTEM_STOP_MSK, 1 << 4);
	/* BCK disable */
	md6000_update_bits(ddata, MD6000_REG_CLK_DIV,
			MD6000_REG_CLK_DIV_BCK_EN_MSK, 0 << 6);
	/* MCK disable */
	md6000_update_bits(ddata, MD6000_REG_CLK_DIV,
			MD6000_REG_CLK_DIV_MCK_EN_MSK, 0 << 7);

	md6000_wake(0);

	return 0;
}
EXPORT_SYMBOL(md6000_setup_free);

static int md6000_i2c_suspend(struct device *device)
{
	print_trace("%s()\n", __func__);

	return 0;
}

static int md6000_i2c_resume(struct device *device)
{
	print_trace("%s()\n", __func__);

	return 0;
}

static int md6000_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *identify)
{
	struct md6000_driver_data *driver_data;

	print_trace("%s()\n", __func__);

	driver_data = devm_kzalloc(&client->dev,
			sizeof(struct md6000_driver_data),
			GFP_KERNEL);
	if (!driver_data)
		return -ENOMEM;

	mutex_init(&driver_data->mutex);
	driver_data->client = client;
	dev_set_drvdata(&client->dev, driver_data);

	md6000_drvdata = driver_data;

	driver_data->gpio_rme_wkup = of_get_named_gpio(client->dev.of_node,
						       "rme_wkup-gpio", 0);
	if (!gpio_is_valid(driver_data->gpio_rme_wkup))
		driver_data->gpio_rme_wkup = -EINVAL;
	else
		gpio_direction_output(driver_data->gpio_rme_wkup, 0);

	return 0;
}

static int md6000_i2c_remove(struct i2c_client *client)
{
	struct md6000_driver_data *driver_data;

	print_trace("%s()\n", __func__);

	driver_data = dev_get_drvdata(&client->dev);

	if (!strncmp(client->name, MD6000_DEVICE_NAME, sizeof(client->name)))
		driver_data->client = NULL;

	return 0;
}

static void md6000_i2c_poweroff(struct i2c_client *client)
{
	print_trace("%s()\n", __func__);

	return;
}

/* i2c driver */
static const struct i2c_device_id md6000_i2c_id[] = {
	{MD6000_DEVICE_NAME, 0},
	{}
};

static const struct of_device_id md6000_i2c_dt_ids[] = {
	{ .compatible = "sony,lif-md6000-rme" },
	{}
};

MODULE_DEVICE_TABLE(of, md6000_i2c_dt_ids);

static const struct dev_pm_ops md6000_pm_ops = {
	.suspend  = md6000_i2c_suspend,
	.resume   = md6000_i2c_resume,
};

static struct i2c_driver md6000_i2c_driver = {
	.driver = {
		.name  = MD6000_DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(md6000_i2c_dt_ids),
		.pm    = &md6000_pm_ops,
	},
	.id_table = md6000_i2c_id,
	.probe    = md6000_i2c_probe,
	.remove   = md6000_i2c_remove,
	.shutdown = md6000_i2c_poweroff,
};

static int __init md6000_init(void)
{
	int rv;

	print_trace("%s()\n", __func__);

	rv = i2c_add_driver(&md6000_i2c_driver);
	if (rv != 0) {
		pr_err("%s() called i2c_add_driver() code %d error occurred\n",
			__func__, rv);
		return rv;
	}

	return 0;
}

static void __exit md6000_exit(void)
{
	print_trace("%s()\n", __func__);

	i2c_del_driver(&md6000_i2c_driver);

	return;
}

late_initcall(md6000_init);
module_exit(md6000_exit);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("LIF-MD6000-RME driver");
MODULE_LICENSE("GPL");
