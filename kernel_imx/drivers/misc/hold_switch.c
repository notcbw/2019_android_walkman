// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for hold switch
 * Copyright 2018 Sony Video & Sound Products Corporation
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

struct hold_switch_data {
	struct device *dev;
	struct gpio_desc *gpiod;
	struct delayed_work work;
	int sw_debounce;
	int state;
	int irq;
};

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
								  char *buf)
{
	struct hold_switch_data *ddata = (struct hold_switch_data *)
					 dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", ddata->state);
}

static DEVICE_ATTR(state, 0444, state_show, NULL);

static void hold_switch_report(struct hold_switch_data *ddata)
{
	char *envp[2];
	char *state_buf;
	char event_buf[120];
	int ret;
	int length;
	int env_offset = 0;

	state_buf = (char *)get_zeroed_page(GFP_KERNEL);

	dev_dbg(ddata->dev, "%s: report state = %d\n", __func__, ddata->state);

	if (state_buf) {
		length = state_show(ddata->dev, NULL, state_buf);
		if (length > 0) {
			if (state_buf[length - 1] == '\n')
				state_buf[length - 1] = 0;
			snprintf(event_buf, sizeof(event_buf),
				 "HOLD_STATE=%s", state_buf);
			envp[env_offset++] = event_buf;
		}
	}

	envp[env_offset] = NULL;

	kobject_uevent_env(&ddata->dev->kobj, KOBJ_CHANGE, envp);
	dev_info(ddata->dev, "Report hold state %d\n", ddata->state);

	free_page((unsigned long)state_buf);
}

static void hold_switch_work_func(struct work_struct *work)
{
	struct hold_switch_data *ddata =
		container_of(work, struct hold_switch_data, work.work);
	int cur_state;

	dev_dbg(ddata->dev, "%s\n", __func__);

	cur_state = gpiod_get_value_cansleep(ddata->gpiod);
	if (cur_state < 0) {
		dev_err(ddata->dev, "%s: failed to get gpio value\n", __func__);
		return;
	}

	if (ddata->state != cur_state) {
		ddata->state = cur_state;
		hold_switch_report(ddata);
	}

	pm_wakeup_event(ddata->dev, 5000);
}

static irqreturn_t hold_switch_irq_handler(int irq, void *dev_id)
{
	struct hold_switch_data *ddata = dev_id;

	dev_dbg(ddata->dev, "%s\n", __func__);

	pm_stay_awake(ddata->dev);
	mod_delayed_work(system_wq, &ddata->work,
			 msecs_to_jiffies(ddata->sw_debounce));

	return IRQ_HANDLED;
}

static void hold_switch_cancel(void *data)
{
	struct hold_switch_data *ddata = data;

	dev_dbg(ddata->dev, "%s\n", __func__);

	cancel_delayed_work_sync(&ddata->work);
}

static int hold_switch_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct hold_switch_data *ddata;
	struct device_node *node = dev->of_node;
	int debounce_interval;
	int irq;
	unsigned long irqflags;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata) {
		ret = -ENOMEM;
		goto out;
	}

	ddata->gpiod = devm_gpiod_get(dev, "hold", GPIOD_IN);

	if (of_property_read_u32(node, "debounce-interval",
				 &debounce_interval))
		debounce_interval = 10;

	ret = gpiod_set_debounce(ddata->gpiod,
				 debounce_interval * USEC_PER_MSEC);
	if (ret < 0) {
		dev_info(dev, "%s: set sw_debounce %dms\n",
			 __func__, debounce_interval);
		ddata->sw_debounce = debounce_interval;
	}

	irq = gpiod_to_irq(ddata->gpiod);
	if (irq < 0) {
		ret = irq;
		dev_err(dev, "%s: failed to get irq num of gpio\n", __func__);
		goto out;
	}

	INIT_DELAYED_WORK(&ddata->work, hold_switch_work_func);

	irqflags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

	ret = devm_request_any_context_irq(dev, irq, hold_switch_irq_handler,
					   irqflags, pdev->name, ddata);
	if (ret < 0) {
		dev_err(dev, "%s: failed to request irq\n", __func__);
		goto out;
	}

	ddata->dev = dev;
	dev_set_drvdata(dev, ddata);
	ddata->irq = irq;

	ret = devm_add_action(dev, hold_switch_cancel, ddata);
	if (ret) {
		dev_err(dev, "%s: failed to register cancel action: %d\n",
			__func__, ret);
		goto out;
	}

	ret = device_create_file(ddata->dev, &dev_attr_state);
	if (ret < 0)
		dev_err(dev, "%s: failed to create attr\n", __func__);

	device_init_wakeup(ddata->dev, 1);

	/* Check init state. */
	schedule_delayed_work(&ddata->work, 0);

	return 0;
out:
	return ret;
}

static int hold_switch_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hold_switch_data *ddata = (struct hold_switch_data *)
					 dev_get_drvdata(dev);

	device_init_wakeup(ddata->dev, 0);
	device_remove_file(ddata->dev, &dev_attr_state);
	return 0;
}

static int hold_switch_suspend(struct device *dev)
{
	struct hold_switch_data *ddata = (struct hold_switch_data *)
					 dev_get_drvdata(dev);

	enable_irq_wake(ddata->irq);

	return 0;
}

static int hold_switch_resume(struct device *dev)
{
	struct hold_switch_data *ddata = (struct hold_switch_data *)
					 dev_get_drvdata(dev);

	disable_irq_wake(ddata->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(hold_switch_pm_ops, hold_switch_suspend,
			 hold_switch_resume);

static const struct of_device_id hold_switch_of_match[] = {
	{ .compatible = "sony,hold_switch", },
	{ }
};
MODULE_DEVICE_TABLE(of, hold_switch_of_match);

static struct platform_driver hold_switch_driver = {
	.probe = hold_switch_probe,
	.remove = hold_switch_remove,
	.driver = {
		.name = "hold_switch",
		.pm = &hold_switch_pm_ops,
		.of_match_table = hold_switch_of_match,
	},
};

static int __init hold_switch_init(void)
{
	return platform_driver_register(&hold_switch_driver);
}

static void __exit hold_switch_exit(void)
{
	platform_driver_unregister(&hold_switch_driver);
}

module_init(hold_switch_init);
module_exit(hold_switch_exit);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("Hold Switch driver");
MODULE_LICENSE("GPL");
