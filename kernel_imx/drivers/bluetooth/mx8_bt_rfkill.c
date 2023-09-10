/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2017 NXP
 * Copyright 2019 Sony Video & Sound Products Inc.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*!
 * @file mx8_bt_rfkill.c
 *
 * @brief This driver is implement a rfkill control interface of bluetooth
 * chip on i.MX serial boards. Register the power regulator function and
 * reset function in platform data.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/unistd.h>

#define BT_WAKE_HOST_GPIO_PORT_NUM 40
#define RFKILL_STATE_ON 1
#define RFKILL_STATE_OFF 0

static int system_in_suspend;
static int current_rfkill_status = RFKILL_STATE_OFF;


typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;


// HCI Command opcode
#define HCI_GRP_HOST_CONT_BASEBAND_CMDS (0x03 << 10)
#define HCI_GRP_VENDOR_SPECIFIC_CMDS (0x3F << 10)
#define HCI_VSC_LOW_POWER_MODE (0x0080 | HCI_GRP_VENDOR_SPECIFIC_CMDS)
#define HCI_SET_EVENT_MASK (0x0001 | HCI_GRP_HOST_CONT_BASEBAND_CMDS)
#define HCI_SET_EVENT_MASK_PAGE_2 (0x0063 | HCI_GRP_HOST_CONT_BASEBAND_CMDS)

// HCI Command raw data
#define HCI_SET_EVENT_MASK_SIZE 8
const uint8_t HCI_SET_EVENT_MASK_DISABLE[HCI_SET_EVENT_MASK_SIZE]
= {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t HCI_SET_EVENT_MASK_PAGE_2_DISABLE[HCI_SET_EVENT_MASK_SIZE]
= {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t HCI_SET_EVENT_MASK_PAGE_2_DEFAULT[HCI_SET_EVENT_MASK_SIZE]
= {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t HCI_SET_EVENT_MASK_DEFAULT[HCI_SET_EVENT_MASK_SIZE]
= {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x11, 0x00, 0x00};
const uint8_t HCI_SET_EVENT_MASK_CUSTOM[HCI_SET_EVENT_MASK_SIZE]
= {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x3D};

#define HCI_HOST_SLEEP_SIZE 1
#define HCI_HOST_WAKEUP_SIZE 1
const uint8_t HCI_HOST_SLEEP[HCI_HOST_SLEEP_SIZE] = {0x01};
const uint8_t HCI_HOST_WAKEUP[HCI_HOST_WAKEUP_SIZE] = {0x00};


#define BT_UART_DEVICE_PATH "/dev/ttymxc0"

#define HCI_SEND_DELAY 5

static int write_to_tty(const uint8_t *cmd, int size)
{
	struct file *file;
	loff_t pos = 0;

	file = filp_open(BT_UART_DEVICE_PATH, O_RDWR, 0);
	if (IS_ERR(file)) {
		pr_err("UART Open error");
		return -1;
	}

	kernel_write(file, cmd, size, &pos);
	pr_debug("Send command %02X %02X %02X %02X %02X",
		cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
	filp_close(file, NULL);
	msleep(HCI_SEND_DELAY);
	return 0;
}

static int hci_send_cmd(uint16_t opcode, const uint8_t *param, uint8_t plen)
{
	uint8_t type = 0x01; //HCI_COMMAND_PKT
	uint8_t hci_command_buf[256] = {0};
	uint8_t *p_buf = &hci_command_buf[0];
	uint8_t head_len = 3;
	uint8_t *ch;

	*p_buf++ = type;
	head_len++;
	*p_buf++ = opcode & 0xFF;
	*p_buf++ = (opcode >> 8) & 0xFF;
	*p_buf++ = plen;

	if (plen)
		memcpy(p_buf, (uint8_t *) param, plen);

	write_to_tty(hci_command_buf, plen + head_len);
	return 0;
}

void btsoc_ibs_wakeup(void)
{
	uint8_t cmd[] = {0xfd};

	write_to_tty(cmd, 1);
}

void btsoc_ibs_sleep(void)
{
	uint8_t cmd[] = {0xfe};

	write_to_tty(cmd, 1);
}

static int host_wakeup_notify_bt_controller(void)
{
	pr_debug("Send host wakeup mode hci");
	btsoc_ibs_wakeup();
	hci_send_cmd(
		HCI_VSC_LOW_POWER_MODE,
		HCI_HOST_WAKEUP,
		HCI_HOST_WAKEUP_SIZE);
	//msleep(HCI_SEND_DELAY);
	//hci_send_cmd(
	//	HCI_SET_EVENT_MASK,
	//	HCI_SET_EVENT_MASK_CUSTOM,
	//	HCI_SET_EVENT_MASK_SIZE);
	return 0;
}

static int host_sleep_notify_bt_controller(void)
{
	pr_debug("Send host sleep mode hci");
	btsoc_ibs_wakeup();
	//hci_send_cmd(
	//	HCI_SET_EVENT_MASK,
	//	HCI_SET_EVENT_MASK_DISABLE,
	//	HCI_SET_EVENT_MASK_SIZE);
	//msleep(HCI_SEND_DELAY);
	hci_send_cmd(
		HCI_VSC_LOW_POWER_MODE,
		HCI_HOST_SLEEP,
		HCI_HOST_SLEEP_SIZE);
	btsoc_ibs_sleep();
	return 0;
}

struct mxc_bt_rfkill_data {
	int bt_power_gpio;
	struct input_dev *input;
};

struct mxc_bt_rfkill_pdata {
};

static int mxc_bt_rfkill_power_change(void *rfkdata, int status)
{
	struct mxc_bt_rfkill_data *data = rfkdata;

	if (gpio_is_valid(data->bt_power_gpio)) {
		pr_info("%s gpio:%d status:%s\n",
			__func__,
			data->bt_power_gpio,
			(status == 1) ? "High" : "Low");
		if ((status == 1) && (gpio_get_value_cansleep(data->bt_power_gpio) == 1)) {
			pr_err("%s gpio:%d status error. Force reset",
				__func__,
				data->bt_power_gpio);
			gpio_set_value_cansleep(data->bt_power_gpio, 0);
			msleep(100);
		}
		current_rfkill_status = status;
		gpio_set_value_cansleep(data->bt_power_gpio, status);
	}
	return 0;
}

static int mxc_bt_set_block(void *rfkdata, bool blocked)
{
	int ret;

	/* Bluetooth stack will reset the bluetooth chip during
	 * resume, since we keep bluetooth's power during suspend,
	 * don't let rfkill to actually reset the chip. */
	if (system_in_suspend)
		return 0;
	pr_info("rfkill: BT RF going to : %s\n", blocked ? "off" : "on");
	if (!blocked)
		ret = mxc_bt_rfkill_power_change(rfkdata, RFKILL_STATE_ON);
	else
		ret = mxc_bt_rfkill_power_change(rfkdata, RFKILL_STATE_OFF);

	return ret;
}

static const struct rfkill_ops mxc_bt_rfkill_ops = {
	.set_block = mxc_bt_set_block,
};

static int mxc_bt_power_event(struct notifier_block *this,
							unsigned long event, void *dummy)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		system_in_suspend = 1;
		if (current_rfkill_status == RFKILL_STATE_ON)
			host_sleep_notify_bt_controller();
		/* going to suspend, don't reset chip */
		break;
	case PM_POST_SUSPEND:
		system_in_suspend = 0;
		if (current_rfkill_status == RFKILL_STATE_ON)
			host_wakeup_notify_bt_controller();
		/* System is resume, can reset chip */
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
static struct notifier_block mxc_bt_power_notifier = {
	.notifier_call = mxc_bt_power_event,
};

#if defined(CONFIG_OF)
static const struct of_device_id mxc_bt_rfkill_dt_ids[] = {
	{ .compatible = "fsl,mxc_bt_rfkill", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxc_bt_rfkill_dt_ids);

static struct mxc_bt_rfkill_pdata *mxc_bt_rfkill_of_populate_pdata(
		struct device *dev)
{
	struct device_node *of_node = dev->of_node;
	struct mxc_bt_rfkill_pdata *pdata = dev->platform_data;

	if (!of_node || pdata)
		return pdata;

	pdata = devm_kzalloc(dev, sizeof(struct mxc_bt_rfkill_pdata),
				GFP_KERNEL);
	if (!pdata)
		return pdata;

	return pdata;
}
#endif

static irqreturn_t bt_wake_host_handler(int irq, void *dev_id)
{
	int  val;
	struct mxc_bt_rfkill_data *pdata = dev_id;

	val = gpio_get_value(BT_WAKE_HOST_GPIO_PORT_NUM);
	pr_info("%s val = %s\n", __func__, (val == 1) ? "HIGH" : "LOW");
	return IRQ_HANDLED;
}

static void bt_wake_host_startup(
	struct platform_device *pdev,
	struct mxc_bt_rfkill_data *data)
{
	int ret = 0;
	int irq;
	unsigned long irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING;
	struct device *dev = &pdev->dev;

	ret = gpio_request(BT_WAKE_HOST_GPIO_PORT_NUM, "wobt_irq");
	if (ret) {
		pr_err("gpio_request %d ret %d",
			BT_WAKE_HOST_GPIO_PORT_NUM, ret);
		goto err_oob_req;
	}

	ret = gpio_direction_input(BT_WAKE_HOST_GPIO_PORT_NUM);
	if (ret) {
		pr_err("gpio_direction_input %d ret %d",
			BT_WAKE_HOST_GPIO_PORT_NUM, ret);
		goto err_oob_int;
	}

	irq = gpio_to_irq(BT_WAKE_HOST_GPIO_PORT_NUM);
	if (irq < 0) {
		pr_err("%s: gpio_to_irq %d ret %d",
			__func__, BT_WAKE_HOST_GPIO_PORT_NUM, ret);
		goto err_oob_int;
	}

	ret = request_irq(irq,
		(void *)bt_wake_host_handler, irq_flags,
		"wobt_irq", (void *)data);
	if (ret) {
		pr_err("%s: request_irq %d ret %d",
			__func__, BT_WAKE_HOST_GPIO_PORT_NUM, ret);
		goto err_oob_int;
	}

	device_set_wakeup_capable(dev, true);
	ret = device_set_wakeup_enable(dev, true);
	if (ret) {
		pr_err("Can not enable device wakeup. ret=%d\n", ret);
		goto err_oob_wakeup;
	}

	ret = enable_irq_wake(irq);
	if (ret) {
		pr_err("enable irq wake failed %d", ret);
		goto err_oob_wakeup;
	}

	return;

err_oob_wakeup:
	disable_irq(irq);
	free_irq(irq, (void *)bt_wake_host_handler);
err_oob_int:
	gpio_free(BT_WAKE_HOST_GPIO_PORT_NUM);
err_oob_req:
	return;
}


static int mxc_bt_rfkill_probe(struct platform_device *pdev)
{
	int rc;
	struct rfkill *rfk;
	struct mxc_bt_rfkill_data *data;
	struct device *dev = &pdev->dev;
	struct mxc_bt_rfkill_pdata *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;

	data = devm_kzalloc(dev, sizeof(struct mxc_bt_rfkill_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		pdata = mxc_bt_rfkill_of_populate_pdata(&pdev->dev);
		if (!pdata)
			return -EINVAL;
	}

	data->bt_power_gpio = of_get_named_gpio(np, "bt-power-gpios", 0);
	if (data->bt_power_gpio == -EPROBE_DEFER) {
		printk(KERN_INFO "mxc_bt_rfkill: gpio not ready, need defer\n");
		return -EPROBE_DEFER;
	}
	if (gpio_is_valid(data->bt_power_gpio)) {
		printk(KERN_INFO "bt power gpio is:%d\n", data->bt_power_gpio);
		rc = devm_gpio_request_one(&pdev->dev,
								data->bt_power_gpio,
								GPIOF_OUT_INIT_LOW,
								"BT power enable");
		if (rc) {
			dev_err(&pdev->dev, "unable to get bt-power-gpios\n");
			goto error_request_gpio;
		}
	} else  {
		printk("bt power gpio not valid (%d)!\n", data->bt_power_gpio);
	}

	rc = register_pm_notifier(&mxc_bt_power_notifier);
	if (rc)
		goto error_check_func;

	rfk = rfkill_alloc("mxc-bt", &pdev->dev, RFKILL_TYPE_BLUETOOTH,
					&mxc_bt_rfkill_ops, data);

	if (!rfk) {
		rc = -ENOMEM;
		goto error_rfk_alloc;
	}

	rfkill_init_sw_state(rfk, true);

	rc = rfkill_register(rfk);
	if (rc)
		goto error_rfkill;

	platform_set_drvdata(pdev, rfk);
	printk(KERN_INFO "mxc_bt_rfkill driver success loaded\n");
	bt_wake_host_startup(pdev, data);
	return 0;

error_rfkill:
	rfkill_destroy(rfk);
error_request_gpio:
error_rfk_alloc:
error_check_func:
	kfree(data);
	return rc;
}

static int  mxc_bt_rfkill_remove(struct platform_device *pdev)
{
	struct mxc_bt_rfkill_data *data = platform_get_drvdata(pdev);
	struct rfkill *rfk = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (rfk) {
		rfkill_unregister(rfk);
		rfkill_destroy(rfk);
	}
	mxc_bt_rfkill_power_change(data, 0);
	kfree(data);
	return 0;
}

static struct platform_driver mxc_bt_rfkill_driver = {
	.probe	= mxc_bt_rfkill_probe,
	.remove	= mxc_bt_rfkill_remove,
	.driver = {
		.name	= "mxc_bt_rfkill",
		.owner	= THIS_MODULE,
		.of_match_table = mxc_bt_rfkill_dt_ids,
	},
};

module_platform_driver(mxc_bt_rfkill_driver)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("RFKill control interface of BT on MX8 Platform");
