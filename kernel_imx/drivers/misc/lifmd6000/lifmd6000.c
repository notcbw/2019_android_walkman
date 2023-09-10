// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Sony Video & Sound Products Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *
 */


#include "lifmd6000.h"
#include <sound/lifmd6000.h>
#include <sound/lif-md6000-rme.h>

/*
 * Since the same line is used for RME RW writing using ecspi2 and the
 * touch panel, exclusion control is necessary when performing CS
 * operation. For models not requiring exclusion, turn this configuration
 * off.
 */
#define LOCAL_CONFIG_EXC_RME_TP

static int rme_parse_dt(struct lifmd6000_writer *);
static int rme_gpio_init(struct lifmd6000_writer *);
static int rme_gpio_en(struct lifmd6000_writer *, int);
static int rme_gpio_cs(struct lifmd6000_writer *, int);
static int rme_gpio_reset(struct lifmd6000_writer *, int);
static int rme_gpio_wkup(struct lifmd6000_writer *, int);
static int rme_spi_read(struct lifmd6000_writer *, const uint8_t *,
					uint32_t, uint8_t *, uint32_t);
static int rme_spi_write(struct lifmd6000_writer *, const uint8_t *,
							 uint32_t);
static int rme_spi_write_fw(struct lifmd6000_writer *);
static int rme_fw_read_command(struct lifmd6000_writer *,
					int, uint8_t *, uint32_t);
static int rme_fw_write_command(struct lifmd6000_writer *, int);
static int rme_fw_writer_chk_status(uint8_t);
static int rme_fw_writer_exec(struct lifmd6000_writer *);
static void rme_fw_writer(struct work_struct *);
static int rme_spi_read_fw(struct lifmd6000_writer *);
static int rme_fw_writer_init(struct lifmd6000_writer *, unsigned int);

static int audio_lock;
void lifmd6000_set_lock(int lock)
{
	audio_lock = lock;
	return;
}
EXPORT_SYMBOL(lifmd6000_set_lock);

static int rme_parse_dt(struct lifmd6000_writer *fw_wr)
{
	struct device_node *dt = NULL;

	if ((fw_wr == NULL) || (fw_wr->dev == NULL)) {
		pr_err("parse_dt parameter error\n");
		return false;
	}

	dt = fw_wr->dev->of_node;
	if (!dt)
		return false;

	fw_wr->gpio_cs = of_get_named_gpio(dt, "cs-gpio", 0);
	if (fw_wr->gpio_cs < 0)
		return false;

	fw_wr->gpio_en = of_get_named_gpio(dt, "en-gpio", 0);
	if (fw_wr->gpio_en < 0)
		return false;

	fw_wr->gpio_wkup = of_get_named_gpio(dt, "wkup-gpio", 0);
	if (fw_wr->gpio_wkup < 0)
		return false;

	fw_wr->gpio_rst = of_get_named_gpio(dt, "rst-gpio", 0);
	if (fw_wr->gpio_rst < 0)
		return false;

	return true;
}

static int rme_gpio_init(struct lifmd6000_writer *fw_wr)
{
	int ret = false;

	if (fw_wr == NULL) {
		pr_err("gpio_init parameter error\n");
		return false;
	}

	ret = rme_parse_dt(fw_wr);
	if (ret != true)
		return false;

	gpio_direction_output(fw_wr->gpio_en, DSD_RME_POWER_ON);
	gpio_direction_output(fw_wr->gpio_cs, CS_HIGH);
	gpio_direction_output(fw_wr->gpio_rst, DSD_RME_RST_NORMAL);

	return true;
}

static int rme_gpio_en(struct lifmd6000_writer *fw_wr, int value)
{
	if ((fw_wr == NULL) || ((value != DSD_RME_POWER_ON) &&
					(value != DSD_RME_POWER_OFF))) {
		pr_err("can't set rme power : parameter error\n");
		return false;
	}
	if (!gpio_is_valid(fw_wr->gpio_en)) {
		pr_err("can't set rme power : gpio invalid\n");
		return false;
	}
	gpio_set_value(fw_wr->gpio_en, value);

	return true;
}

static int rme_gpio_cs(struct lifmd6000_writer *fw_wr, int value)
{
	if ((fw_wr == NULL) || ((value != CS_LOW) && (value != CS_HIGH))) {
		pr_err("can't set chip select : parameter error\n");
		return false;
	}
	if (!gpio_is_valid(fw_wr->gpio_cs)) {
		pr_err("can't set chip select : gpio invalid\n");
		return false;
	}
	gpio_set_value(fw_wr->gpio_cs, value);

	return true;
}

static int rme_gpio_reset(struct lifmd6000_writer *fw_wr, int value)
{
	if ((fw_wr == NULL) || ((value != DSD_RME_RST_RESET) &&
					(value != DSD_RME_RST_NORMAL))) {
		pr_err("can't set reset : parameter error\n");
		return false;
	}
	if (!gpio_is_valid(fw_wr->gpio_rst)) {
		pr_err("can't set reset : gpio invalid\n");
		return false;
	}
	gpio_set_value(fw_wr->gpio_rst, value);

	return true;
}

static int rme_gpio_wkup(struct lifmd6000_writer *fw_wr, int value)
{
	if ((fw_wr == NULL) || ((value != DSD_RME_WKUP_HIGH) &&
					(value != DSD_RME_WKUP_LOW))) {
		pr_err("can't set wkup : parameter error\n");
		return false;
	}
	if (!gpio_is_valid(fw_wr->gpio_wkup)) {
		pr_err("can't set wkup : gpio invalid\n");
		return false;
	}
	gpio_direction_output(fw_wr->gpio_wkup, value);

	return true;
}

static int rme_spi_read(struct lifmd6000_writer *fw_wr,
		const uint8_t *command, uint32_t command_len,
		uint8_t *data, uint32_t length)
{
	struct spi_transfer	t = {
			.rx_buf		= data,
			.len		= length,
		};
	struct spi_message	m;
	int ret = false;
	int error = 0;

	if ((fw_wr == NULL) ||  (!fw_wr->spi) ||
			(command == NULL) || (command_len <= 0) ||
			(data == NULL) || (length <= 0))
		return false;

	// read command write
	ret = rme_spi_write(fw_wr, command, command_len);
	if (ret != true)
		return ret;

	// data read
	mutex_lock(&(fw_wr->spi_lock));

	t.speed_hz = fw_wr->spi->max_speed_hz;
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	error = spi_sync(fw_wr->spi, &m);
	if (error == 0) {
		error = m.status;
		if (error == 0) {
			error = m.actual_length;
			ret = true;
		} else {
			pr_err("error : spi_sync ...%d\n", error);
			ret = false;
		}
	} else {
		pr_err("error : spi_sync ...%d\n", error);
		ret = false;
	}

	mutex_unlock(&(fw_wr->spi_lock));
	return ret;
}

static int rme_spi_write(struct lifmd6000_writer *fw_wr,
					const uint8_t *buf, uint32_t length)
{
	struct spi_transfer t = {
		.tx_buf		= buf,
		.len		= length,
	};
	struct spi_message m;
	int error = 0;
	int ret = false;

	if ((fw_wr == NULL) || (!fw_wr->spi) || (buf == NULL) ||
							(length <= 0))
		return false;

	mutex_lock(&(fw_wr->spi_lock));

	t.speed_hz = fw_wr->spi->max_speed_hz;
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	error = spi_sync(fw_wr->spi, &m);
	if (error == 0) {
		error = m.status;
		if (error == 0) {
			error = m.actual_length;
			ret = true;
		} else {
			pr_err("error : spi_sync ...%d\n", error);
			ret = false;
		}
	} else {
		pr_err("error : spi_sync ...%d\n", error);
		ret = false;
	}

	mutex_unlock(&(fw_wr->spi_lock));

	return ret;
}

static int rme_spi_write_fw(struct lifmd6000_writer *fw_wr)
{
	int ret = false;
	const struct spi_command_code *comm = NULL;
	uint32_t lsiz = 0;
	uint32_t wsiz = 0;
	const uint8_t *wptr = NULL;

	if (fw_wr == NULL)
		return false;

	comm = &spi_wr_command[SPI_WR_COMMAND_LSC_BITSTREAM_BURST];
	if (!comm)
		return false;

	// check firmware data readed
	if (!fw_wr->save_firmware_data) {
		// first time FW write read firmware file
		pr_info("RME FW write first entry : read FW file\n");
		ret = rme_spi_read_fw(fw_wr);
		if (ret != true) {
			pr_err("can't read RME FW file\n");
			return ret;
		}
	}

	// Chip Select LOW
	ret = rme_gpio_cs(fw_wr, CS_LOW);
	if (ret != true)
		return false;

	// Send Command
	ret = rme_spi_write(fw_wr, comm->command, comm->len);
	if (ret != true) {
		rme_gpio_cs(fw_wr, CS_HIGH);
		return false;
	}

	wptr = fw_wr->save_firmware_data;
	lsiz = fw_wr->save_firmware_size;

	while (lsiz) {
		if (lsiz < DSD_RME_DEVID_LEN)
			wsiz = lsiz;
		else
			wsiz = DSD_RME_DEVID_LEN;

		ret = rme_spi_write(fw_wr, wptr, wsiz);
		if (ret != true) {
			pr_err("can't write spi data : FW data\n");
			rme_gpio_cs(fw_wr, CS_HIGH);
			return false;
		}
		wptr += wsiz;
		if (lsiz >= wsiz)
			lsiz -= wsiz;
		else
			lsiz = 0;
	}

	// Chip Select HIGH
	ret = rme_gpio_cs(fw_wr, CS_HIGH);
	if (ret != true)
		return false;

	return true;
}

static int rme_fw_read_command(struct lifmd6000_writer *fw_wr, int id,
					uint8_t *buf, uint32_t length)
{
	const struct spi_command_code *comm = NULL;
	int ret = false;

	if ((fw_wr == NULL) || (id < SPI_RD_COMMAND_IDCODE_PUB) ||
		(id >= SPI_RD_COMMAND_END) || (buf == NULL) ||
		(length <= 0))
		return false;

	comm = &spi_rd_command[id];
	if (!comm)
		return false;

	// Chip Select LOW
	ret = rme_gpio_cs(fw_wr, CS_LOW);
	if (ret != true)
		return false;

	// send command
	ret = rme_spi_read(fw_wr, comm->command, comm->len, buf, length);
	if (ret != true) {
		pr_err("can't read spi data : %d\n", id);
		rme_gpio_cs(fw_wr, CS_HIGH);
		return false;
	}

	// Chip Select HIGH
	ret = rme_gpio_cs(fw_wr, CS_HIGH);
	if (ret != true)
		return false;

	return true;
}

static int rme_fw_write_command(struct lifmd6000_writer *fw_wr, int id)
{
	const struct spi_command_code *comm = NULL;
	int ret = true;

	if ((fw_wr == NULL) || (id < SPI_WR_COMMAND_KEY_ACTIVATION) ||
		(id >= SPI_WR_COMMAND_END))
		return false;

	comm = &spi_wr_command[id];
	if (!comm)
		return false;

	// Chip Select LOW
	ret = rme_gpio_cs(fw_wr, CS_LOW);
	if (ret != true)
		return false;

	// send command
	ret = rme_spi_write(fw_wr, comm->command, comm->len);
	if (ret != true) {
		pr_err("can't write spi data : %d\n", id);
		rme_gpio_cs(fw_wr, CS_HIGH);
		return false;
	}

	// Chip Select HIGH
	ret = rme_gpio_cs(fw_wr, CS_HIGH);
	if (ret != true)
		return false;

	return true;
}

static int rme_fw_writer_chk_status(uint8_t status)
{
	int ret = false;
	uint8_t done = 0;
	uint8_t busy = 0;
	uint8_t fail = 0;

	done = status & DSD_RME_STATUS_DONE;
	busy = status & DSD_RME_STATUS_BUSY;
	fail = status & DSD_RME_STATUS_FAIL;

	if (done && !busy && !fail)
		ret = true;
	else
		pr_err("status is invalid [done:%d, busy:%d, fail:%d]\n",
			done, busy, fail);

	return ret;
}

static int rme_fw_writer_exec(struct lifmd6000_writer *fw_wr)
{
	int ret = false;
	uint8_t rbuf[4];

	if (fw_wr == NULL)
		return ret;

	// CRESETB is Low
	ret = rme_gpio_reset(fw_wr, DSD_RME_RST_RESET);
	if (ret != true)
		return ret;

	// wait 1msec
	usleep_range(1000, 1100);

	// Initialize
	ret = rme_fw_write_command(fw_wr, SPI_WR_COMMAND_KEY_ACTIVATION);
	if (ret != true)
		return ret;

	// CRESETB is High
	ret &= rme_gpio_reset(fw_wr, DSD_RME_RST_NORMAL);
	if (ret != true)
		return ret;

	// wait 10msec
	usleep_range(10000, 11000);

	// Check the IDCODE
	ret = rme_fw_read_command(fw_wr, SPI_RD_COMMAND_IDCODE_PUB,
						rbuf, sizeof(rbuf));
	if (ret != true)
		return ret;

	// Enable Programming Mode
	ret = rme_fw_write_command(fw_wr, SPI_WR_COMMAND_ISC_ENABLE);
	if (ret != true)
		return ret;

	// wait 1msec
	usleep_range(1000, 1100);

	// Erase the device
	ret = rme_fw_write_command(fw_wr, SPI_WR_COMMAND_ISC_ERASE);
	if (ret != true)
		return ret;

	// wait 10msec
	usleep_range(10000, 11000);

	// Program Fuse Map
	ret = rme_fw_write_command(fw_wr, SPI_WR_COMMAND_LSC_INIT_ADDRESS);
	if (ret != true)
		return ret;

	// wait 1msec
	usleep_range(1000, 1100);

	// Write FirmWare File
	ret = rme_spi_write_fw(fw_wr);
	if (ret != true)
		return ret;

	// wait 1msec
	usleep_range(1000, 1100);

	// Read the status bit
	ret = rme_fw_read_command(fw_wr, SPI_RD_COMMAND_LSC_READ_STATUS,
						rbuf, sizeof(rbuf));
	if (ret != true)
		return ret;

	ret = rme_fw_writer_chk_status(rbuf[2]);
	if (ret != true)
		return ret;

	// Exit Programming Mode
	ret = rme_fw_write_command(fw_wr, SPI_WR_COMMAND_ISC_DISABLE);
	if (ret != true)
		return ret;

	// wait 1msec
	usleep_range(1000, 1100);

	// Exit FW Write Process
	ret = rme_fw_write_command(fw_wr, SPI_WR_COMMAND_NOOP);

	return ret;
}

static void rme_fw_writer(struct work_struct *work)
{
	int ret = false;
	struct lifmd6000_writer *fw_wr = NULL;

	if (!work)
		return;

	fw_wr = container_of(to_delayed_work(work),
				 struct lifmd6000_writer, work_update);
	if (!fw_wr)
		return;

	// wake_lock lock
	wake_lock(&fw_wr->rme_write_lock);

#ifdef LOCAL_CONFIG_EXC_RME_TP
	// exclusive spi transfer for tatch panel
	exc_rme_tp_lock();
#endif /* LOCAL_CONFIG_EXC_RME_TP */

	// GPIO Init
	ret = rme_gpio_init(fw_wr);
	if (ret != true) {
		pr_err("GPIO init failed.\n");
		rme_gpio_en(fw_wr, DSD_RME_POWER_OFF);
#ifdef LOCAL_CONFIG_EXC_RME_TP
		exc_rme_tp_unlock();
#endif /* LOCAL_CONFIG_EXC_RME_TP */

		// wake_lock unlock
		wake_unlock(&fw_wr->rme_write_lock);
		return;
	}

	// wait RME wake
	msleep(20);

	// RME FW write
	ret = rme_fw_writer_exec(fw_wr);
	if (ret != true) {
		pr_err("RME FW write failed.\n");
		rme_gpio_en(fw_wr, DSD_RME_POWER_OFF);
#ifdef LOCAL_CONFIG_EXC_RME_TP
		exc_rme_tp_unlock();
#endif /* LOCAL_CONFIG_EXC_RME_TP */

		schedule_delayed_work(work, msecs_to_jiffies(DSD_RME_DELAY_TIM_RETRY));
		pr_err("RME FW write Retry after 1000ms.\n");

		// wake_lock unlock
		wake_unlock(&fw_wr->rme_write_lock);
		return;
	}

	// wait 1msec
	usleep_range(1000, 1100);

	// wkup -> low
	rme_gpio_wkup(fw_wr, DSD_RME_WKUP_LOW);

#ifdef LOCAL_CONFIG_EXC_RME_TP
	// exclusive spi transfer for tatch panel
	exc_rme_tp_unlock();
#endif /* LOCAL_CONFIG_EXC_RME_TP */
	pr_info("RME FW write success\n");

	/* set DSD RME to sleep */
	md6000_wake(0);

	// wake_lock unlock
	wake_unlock(&fw_wr->rme_write_lock);
}

static int rme_spi_read_fw(struct lifmd6000_writer *fw_wr)
{
	int ret = false;
	const struct firmware *fw_entry = NULL;
	uint8_t *wptr = NULL;

	if (!fw_wr)
		return false;

	// return if the firmware has already been acquired
	if (fw_wr->save_firmware_data != NULL)
		return true;

	// Read FirmWare File(.bit) Data
	ret = request_firmware(&fw_entry, DSD_RME_FW_FILE_NAME, fw_wr->dev);
	if ((ret != 0) || (!fw_entry)) {
		pr_err("request_firmware function error RME FW file\n");
		return false;
	}

	// firmware data check
	if ((fw_entry->data == NULL) || (fw_entry->size <= 0)) {
		release_firmware(fw_entry);
		pr_err("request_firmware data error RME FW file\n");
		return false;
	}

	// allocate FirmWare File save memory
	wptr = kzalloc((size_t)fw_entry->size, GFP_KERNEL);
	if (wptr != NULL) {
		// save firmware data
		memcpy(wptr, fw_entry->data, (size_t)fw_entry->size);

		// set global variable
		fw_wr->save_firmware_data = wptr;
		fw_wr->save_firmware_size = fw_entry->size;

		ret = true;
	} else {
		pr_err("no memory for RME FW file\n");
		ret = false;
	}

	release_firmware(fw_entry);

	return ret;
}

static int rme_fw_writer_init(struct lifmd6000_writer *fw_wr, unsigned int time)
{
	int ret = 0;

	if (!fw_wr)
		return -EINVAL;

	ret = queue_delayed_work(fw_wr->fw_write_wq,
			&fw_wr->work_update, msecs_to_jiffies(time));
	if (ret == 0) {
		pr_err("queue_work failed\n");
		cancel_delayed_work_sync(&fw_wr->work_update);
		destroy_workqueue(fw_wr->fw_write_wq);
		return -EBUSY;
	}

	return 0;
}

static int rme_write_suspend(struct device *dev)
{
	int ret = false;
	struct lifmd6000_writer *fw_wr = NULL;
#ifdef CONFIG_ICX_SILENT_LPA_LOG
	extern bool imx_lpa_is_enabled(void);

	if (!imx_lpa_is_enabled())
#endif
		pr_info("%s: enter\n", __func__);

	if (!dev)
		return -EINVAL;

	if (audio_lock) {
		return 0;
	}

	fw_wr = dev_get_drvdata(dev);
	if (fw_wr) {
		cancel_delayed_work_sync(&fw_wr->work_update);

		ret = rme_gpio_en(fw_wr, DSD_RME_POWER_OFF);
		if (ret != true) {
			pr_err("rme power off failed\n");
			ret = -EBUSY;
		} else
			ret = 0;
	} else {
		pr_err("rme write driver data get failed\n");
		return -EINVAL;
	}

	return ret;
}

int rme_write_resume(struct device *dev)
{
	int ret = 0;
	struct lifmd6000_writer *fw_wr = NULL;

#ifdef CONFIG_ICX_SILENT_LPA_LOG
        extern bool imx_lpa_is_enabled(void);

        if (!imx_lpa_is_enabled())
#endif
		pr_info("%s: enter\n", __func__);

	if (!dev)
		return -EINVAL;

        if (audio_lock) {
                return 0;
        }

	fw_wr = dev_get_drvdata(dev);
	if (fw_wr)
		ret = rme_fw_writer_init(fw_wr, DSD_RME_DELAY_TIM_RESUME);
	else
		ret = -EINVAL;

	return ret;
}

int rme_write_probe(struct spi_device *spi)
{
	struct lifmd6000_writer *fw_wr;
	int ret = 0;

	pr_info("Enter %s\n", __func__);

	if ((!spi) || (!spi->master))
		return -EINVAL;

	if (spi->master->flags & SPI_MASTER_HALF_DUPLEX) {
		dev_err(&spi->dev,
				"%s: Full duplex not supported by host\n",
								__func__);
		return -EIO;
	}


	fw_wr = devm_kzalloc(&spi->dev, sizeof(struct lifmd6000_writer),
							GFP_KERNEL);
	if (fw_wr == NULL)
		return -ENOMEM;

	spi->bits_per_word = DSD_RME_BIT_PER_WORD;
	spi->mode = SPI_MODE_0;

	fw_wr->spi = spi;
	mutex_init(&(fw_wr->spi_lock));
	fw_wr->dev = &spi->dev;
	dev_set_drvdata(&spi->dev, fw_wr);
	spi_set_drvdata(spi, fw_wr);

	fw_wr->save_firmware_data = NULL;
	fw_wr->save_firmware_size = 0;

	// wake_lock set
	wake_lock_init(&fw_wr->rme_write_lock, WAKE_LOCK_SUSPEND, DSD_RME_NAME);

	// create work queue
	fw_wr->fw_write_wq =
		create_singlethread_workqueue("LIF-MD6000_update_request");
	if (!fw_wr->fw_write_wq) {
		pr_err("update_wq failed\n");
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&fw_wr->work_update, rme_fw_writer);

	ret = rme_fw_writer_init(fw_wr, DSD_RME_DELAY_TIM_PROBE);
	return ret;

}

int rme_write_remove(struct spi_device *spi)
{
	int ret = false;
	struct lifmd6000_writer *fw_wr = NULL;

	if (!spi)
		return -EINVAL;

	fw_wr = spi_get_drvdata(spi);
	if (fw_wr) {
		cancel_delayed_work_sync(&fw_wr->work_update);
		destroy_workqueue(fw_wr->fw_write_wq);

		ret = rme_gpio_en(fw_wr, DSD_RME_POWER_OFF);
		if (ret != true) {
			pr_err("rme power off failed\n");
			ret = -EBUSY;
		} else
			ret = 0;

		kfree(fw_wr->save_firmware_data);

		// wake_lock release
		wake_lock_destroy(&fw_wr->rme_write_lock);

		fw_wr->spi = NULL;
	} else {
		pr_err("rme write driver data get failed\n");
		return -EINVAL;
	}

	spi_set_drvdata(spi, NULL);

	return ret;
}

void rme_write_shutdown(struct spi_device *spi)
{
	rme_write_remove(spi);
}

static const struct dev_pm_ops rme_write_pm_ops = {
	.suspend = rme_write_suspend,
	.resume  = rme_write_resume,
};

static const struct of_device_id rme_write_match_table[] = {
	{.compatible = "lattice,lifmd6000" },
	{},
};

static struct spi_driver rme_write_driver = {
	.driver = {
		.name =		DSD_RME_NAME,
		.owner =	THIS_MODULE,
		.of_match_table = rme_write_match_table,
		.pm = &rme_write_pm_ops,
	},
	.probe =	rme_write_probe,
	.remove =	rme_write_remove,
	.shutdown =	rme_write_shutdown,
};

static int __init rme_write_init(void)
{
	int ret = 0;

	pr_info("LIF-MD6000 RME FW write driver init\n");

	ret = spi_register_driver(&rme_write_driver);
	if (ret < 0)
		pr_err("RME FW write driver init failed\n");

	return ret;
}

static void __exit rme_write_exit(void)
{
	spi_unregister_driver(&rme_write_driver);
}


module_init(rme_write_init);
module_exit(rme_write_exit);

MODULE_AUTHOR("Sony Video & Sound Products Inc.");
MODULE_DESCRIPTION("RME FW write driver");
MODULE_LICENSE("GPL");
