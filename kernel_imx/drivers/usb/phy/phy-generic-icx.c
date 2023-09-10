/*
 * NOP USB transceiver with extcon notifier for all USB transceiver which are
 * either built-in into USB IP or which are mostly autonomous.
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Ajay Kumar Gupta <ajay.gupta@ti.com>
 * Copyright 2019 Sony Video & Sound Products Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Current status:
 *	This provides a "nop" transceiver for PHYs which are
 *	autonomous such as isp1504, isp1707, etc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include "phy-generic.h"

#define VBUS_IRQ_FLAGS \
	(IRQF_SHARED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | \
		IRQF_ONESHOT)

static const unsigned int nop_icx_extcon_cables[] = {
	EXTCON_CHG_USB_SDP,
	EXTCON_NONE,	/*!< Terminator. */
};

static int nop_icx_set_suspend(struct usb_phy *x, int suspend)
{
	struct usb_phy_generic *nop = dev_get_drvdata(x->dev);

	if (!IS_ERR(nop->clk)) {
		if (suspend)
			clk_disable_unprepare(nop->clk);
		else
			clk_prepare_enable(nop->clk);
	}

	return 0;
}

static void nop_icx_reset(struct usb_phy_generic *nop)
{
	if (IS_ERR_OR_NULL(nop->gpiod_reset))
		return;

	gpiod_set_value(nop->gpiod_reset, 1);
	usleep_range(10000, 20000);
	gpiod_set_value(nop->gpiod_reset, 0);
}

/* interface to regulator framework */
static void nop_icx_set_vbus_draw_regulator(struct usb_phy_generic *nop,
	unsigned mA)
{
	struct regulator *vbus_draw = nop->vbus_draw;
	int enabled;
	int ret;


	if (IS_ERR_OR_NULL(vbus_draw)) {
		/* There is no vbus_draw regulator. */
		return;
	}

	enabled = nop->vbus_draw_enabled;
	if (mA) {
		regulator_set_current_limit(vbus_draw, 0, 1000 * mA);
		if (!enabled) {
			ret = regulator_enable(vbus_draw);
			if (ret < 0)
				return;
			nop->vbus_draw_enabled = 1;
		}
	} else {
		if (enabled) {
			ret = regulator_disable(vbus_draw);
			if (ret < 0)
				return;
			nop->vbus_draw_enabled = 0;
		}
	}
}

/* interface to extcon framework */
static void nop_icx_set_vbus_draw_extcon(struct usb_phy_generic *nop)
{
	int	ret;
	bool	state;
	long	mA;
	long	uA;
	union  extcon_property_value	prop;
	unsigned long	flags;

	if (IS_ERR_OR_NULL(nop->edev_vbus)) {
		/* There is no extcon device. */
		return;
	}
	spin_lock_irqsave(&(nop->edev_lock), flags);
	mA = nop->mA;
	spin_unlock_irqrestore(&(nop->edev_lock), flags);
	state = (mA > 0);
	ret = extcon_set_state(nop->edev_vbus, EXTCON_CHG_USB_SDP, state);
	if (ret != 0)
		dev_warn(nop->dev,
			"Fail to set state CHG_USB_SDP. ret=%d\n",
			ret
		);

	uA = mA * 1000;
	if (uA > INT_MAX) {
		/* Too large value. */
		uA = INT_MAX;
	}
	prop.intval = (__force int)uA;
	ret = extcon_set_property_sync(nop->edev_vbus, EXTCON_CHG_USB_SDP,
		EXTCON_PROP_CHG_MIN, prop
	);
	if (ret != 0)
		dev_warn(nop->dev,
			"Fail to set property PROP_CHG_MIN. ret=%d\n",
			ret
		);
	spin_lock_irqsave(&(nop->edev_lock), flags);
	if (mA != nop->mA) {
		/* Some one changes state. */
		if (test_bit(USB_PHY_GENERIC_FLAGS_NO_MORE_WORK,
			&(nop->edev_flags)) == 0) {
			/* Still alive. */
			dev_notice(nop->dev, "Changed configuration, restart work. mA=%ld, new_mA=%ld\n",
				mA, nop->mA
			);
			/* Re-queue work. */
			schedule_work(&(nop->edev_work));
		}
	}
	spin_unlock_irqrestore(&(nop->edev_lock), flags);
}

static void nop_icx_phy_edev_work(struct work_struct *work)
{	struct usb_phy_generic *nop =
		 container_of(work, struct usb_phy_generic, edev_work);
	nop_icx_set_vbus_draw_extcon(nop);
}

/* interface to regulator and extcon framework */
static void nop_icx_set_vbus_draw(struct usb_phy_generic *nop, unsigned mA)
{	unsigned long	flags;

	nop_icx_set_vbus_draw_regulator(nop, mA);
	spin_lock_irqsave(&(nop->edev_lock), flags);
	nop->mA = mA;
	schedule_work(&(nop->edev_work));
	spin_unlock_irqrestore(&(nop->edev_lock), flags);
}

static irqreturn_t nop_icx_gpio_vbus_thread(int irq, void *data)
{
	struct usb_phy_generic *nop = data;
	struct usb_otg *otg = nop->phy.otg;
	int vbus, status;

	vbus = gpiod_get_value(nop->gpiod_vbus);
	if ((vbus ^ nop->vbus) == 0)
		return IRQ_HANDLED;
	nop->vbus = vbus;

	if (vbus) {
		status = USB_EVENT_VBUS;
		otg->state = OTG_STATE_B_PERIPHERAL;
		nop->phy.last_event = status;

		/* drawing a "unit load" is *always* OK, except for OTG */
		nop_icx_set_vbus_draw(nop, 100);

		atomic_notifier_call_chain(&nop->phy.notifier, status,
					   otg->gadget);
	} else {
		nop_icx_set_vbus_draw(nop, 0);

		status = USB_EVENT_NONE;
		otg->state = OTG_STATE_B_IDLE;
		nop->phy.last_event = status;

		atomic_notifier_call_chain(&nop->phy.notifier, status,
					   otg->gadget);
	}
	return IRQ_HANDLED;
}

static int nop_icx_phy_init(struct usb_phy *phy)
{
	struct usb_phy_generic *nop = dev_get_drvdata(phy->dev);
	int ret;

	if (!IS_ERR(nop->vcc)) {
		if (regulator_enable(nop->vcc))
			dev_err(phy->dev, "Failed to enable power\n");
	}

	if (!IS_ERR(nop->clk)) {
		ret = clk_prepare_enable(nop->clk);
		if (ret)
			return ret;
	}

	nop_icx_reset(nop);

	return 0;
}

static void nop_icx_phy_shutdown(struct usb_phy *phy)
{
	int		ret;
	unsigned long	flags;

	struct usb_phy_generic *nop = dev_get_drvdata(phy->dev);

	if (!IS_ERR_OR_NULL(nop->gpiod_reset))
		gpiod_set_value(nop->gpiod_reset, 1);

	if (!IS_ERR_OR_NULL(nop->clk))
		clk_disable_unprepare(nop->clk);

	if (!IS_ERR_OR_NULL(nop->vcc)) {
		ret = regulator_disable(nop->vcc);
		if (ret) {
			/* Can not disable regulator */
			dev_err(phy->dev, "Failed to disable power. ret=%d\n",
				ret
			);
		}
	}
	spin_lock_irqsave(&(nop->edev_lock), flags);
	set_bit(USB_PHY_GENERIC_FLAGS_NO_MORE_WORK, &(nop->edev_flags));
	spin_unlock_irqrestore(&(nop->edev_lock), flags);
	cancel_work_sync(&(nop->edev_work));
}

static int nop_icx_phy_set_power(struct usb_phy *phy, unsigned mA)
{	struct usb_phy_generic *nop = dev_get_drvdata(phy->dev);

	nop_icx_set_vbus_draw(nop, mA);
	return 0;
}

static int nop_icx_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -ENODEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -ENODEV;
	}

	otg->gadget = gadget;
	if (otg->state == OTG_STATE_B_PERIPHERAL)
		atomic_notifier_call_chain(&otg->usb_phy->notifier,
					   USB_EVENT_VBUS, otg->gadget);
	else
		otg->state = OTG_STATE_B_IDLE;
	return 0;
}

static int nop_icx_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	if (!otg)
		return -ENODEV;

	if (!host) {
		otg->host = NULL;
		return -ENODEV;
	}

	otg->host = host;
	return 0;
}

#define	DEFER_COUNT_MAX	(20)
static int defer_count = 0;

static int nop_icx_gen_create_phy(struct device *dev,
	struct usb_phy_generic *nop,
	struct usb_phy_generic_platform_data *pdata)
{
	enum usb_phy_type type = USB_PHY_TYPE_USB2;
	int err = 0;

	u32 clk_rate = 0;
	bool needs_vcc = false;

	if (dev->of_node) {
		struct device_node *node = dev->of_node;

		if (of_property_read_u32(node, "clock-frequency", &clk_rate))
			clk_rate = 0;

		needs_vcc = of_property_read_bool(node, "vcc-supply");
		nop->gpiod_reset = devm_gpiod_get_optional(dev, "reset",
							   GPIOD_ASIS);
		err = PTR_ERR_OR_ZERO(nop->gpiod_reset);
		if (!err) {
			nop->gpiod_vbus = devm_gpiod_get_optional(dev,
							 "vbus-detect",
							 GPIOD_ASIS);
			err = PTR_ERR_OR_ZERO(nop->gpiod_vbus);
		}
	} else if (pdata) {
		type = pdata->type;
		clk_rate = pdata->clk_rate;
		needs_vcc = pdata->needs_vcc;
		if (gpio_is_valid(pdata->gpio_reset)) {
			err = devm_gpio_request_one(dev, pdata->gpio_reset,
						    GPIOF_ACTIVE_LOW,
						    dev_name(dev));
			if (!err)
				nop->gpiod_reset =
					gpio_to_desc(pdata->gpio_reset);
		}
		nop->gpiod_vbus = pdata->gpiod_vbus;
	}

	if (err == -EPROBE_DEFER) {
		/* defer probe, wait other device ready. */
		goto out_defer;
	}

	if (err) {
		dev_err(dev, "Error requesting RESET or VBUS GPIO. err=%d\n",
			err
		);
		return err;
	}
	if (!IS_ERR_OR_NULL(nop->gpiod_reset))
		gpiod_direction_output(nop->gpiod_reset, 1);

	nop->phy.otg = devm_kzalloc(dev, sizeof(*nop->phy.otg),
			GFP_KERNEL);
	if (!nop->phy.otg)
		return -ENOMEM;

	nop->clk = devm_clk_get(dev, "main_clk");
	if (IS_ERR(nop->clk)) {
		dev_dbg(dev, "Can't get phy clock: %ld\n",
					PTR_ERR(nop->clk));
	}

	if (!IS_ERR(nop->clk) && clk_rate) {
		err = clk_set_rate(nop->clk, clk_rate);
		if (err) {
			dev_err(dev, "Error setting clock rate. err=%d, clk_rate=%u\n",
				err, (unsigned)(clk_rate)
			);
			return err;
		}
	}

	nop->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(nop->vcc)) {
		dev_dbg(dev, "Error getting vcc regulator: %ld\n",
					PTR_ERR(nop->vcc));
		if (needs_vcc) {
			/* We need vcc control. */
			goto out_defer;
		}
	}

	nop->dev		= dev;
	nop->phy.dev		= nop->dev;
	nop->phy.label		= "nop-xceiv-icx";
	nop->phy.set_suspend	= nop_icx_set_suspend;
	nop->phy.type		= type;

	nop->phy.otg->state		= OTG_STATE_UNDEFINED;
	nop->phy.otg->usb_phy		= &nop->phy;
	nop->phy.otg->set_host		= nop_icx_set_host;
	nop->phy.otg->set_peripheral	= nop_icx_set_peripheral;

	return 0;

out_defer:
	if (defer_count >= DEFER_COUNT_MAX) {
		/* Too many defer. */
		dev_warn(dev, "Abandon defer. defer_count=%d\n", defer_count);
		return -ENODEV;
	}

	dev_notice(dev, "Defer probe. defer_count=%d\n", defer_count);
	defer_count++;
	return -EPROBE_DEFER;
}

static int nop_icx_generic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_phy_generic	*nop;
	int	err;

	nop = devm_kzalloc(dev, sizeof(*nop), GFP_KERNEL);
	if (!nop)
		return -ENOMEM;

	spin_lock_init(&(nop->edev_lock));
	INIT_WORK(&(nop->edev_work), nop_icx_phy_edev_work);
	nop->edev_vbus = devm_extcon_dev_allocate(dev, nop_icx_extcon_cables);

	if (IS_ERR_OR_NULL(nop->edev_vbus)) {
		err = PTR_ERR(nop->edev_vbus);
		dev_err(dev, "Failed to allocate extcon device. err=%d\n",
			err
		);
		return err;
	}

	/* Register extcon device. */
	err = devm_extcon_dev_register(dev, nop->edev_vbus);
	if (err != 0) {
		dev_err(dev, "Failed to register extcon device. err=%d\n",
			err
		);
		return err;
	}

	/* Set property supports */
	err = extcon_set_property_capability(nop->edev_vbus,
		EXTCON_CHG_USB_SDP, EXTCON_PROP_CHG_MIN
	);
	if (err != 0) {
		dev_err(dev, "Failed to set property capability. err=%d\n",
			err
		);
		return err;
	}

	err = nop_icx_gen_create_phy(dev, nop, dev_get_platdata(&pdev->dev));
	if (err)
		return err;
	if (nop->gpiod_vbus) {
		err = devm_request_threaded_irq(&pdev->dev,
			gpiod_to_irq(nop->gpiod_vbus),
			NULL, nop_icx_gpio_vbus_thread,
			VBUS_IRQ_FLAGS, "vbus_detect",
			nop
		);
		if (err) {
			dev_err(&pdev->dev, "can't request irq %i, err: %d\n",
				gpiod_to_irq(nop->gpiod_vbus), err);
			return err;
		}
		nop->phy.otg->state = gpiod_get_value(nop->gpiod_vbus) ?
			OTG_STATE_B_PERIPHERAL : OTG_STATE_B_IDLE;
	}

	nop->phy.init		= nop_icx_phy_init;
	nop->phy.shutdown	= nop_icx_phy_shutdown;
	nop->phy.set_power      = nop_icx_phy_set_power;

	err = usb_add_phy_dev(&nop->phy);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, nop);
	dev_info(nop->dev, "Registered phy-generic-icx device. edev_vbus=0x%p\n",
		nop->edev_vbus
	);
	return 0;
}

static int nop_icx_generic_remove(struct platform_device *pdev)
{
	struct usb_phy_generic *nop = platform_get_drvdata(pdev);
	unsigned long	flags;

	usb_remove_phy(&nop->phy);
	spin_lock_irqsave(&(nop->edev_lock), flags);
	set_bit(USB_PHY_GENERIC_FLAGS_NO_MORE_WORK, &(nop->edev_flags));
	spin_unlock_irqrestore(&(nop->edev_lock), flags);
	cancel_work_sync(&(nop->edev_work));

	return 0;
}

static const struct of_device_id nop_icx_xceiv_dt_ids[] = {
	{ .compatible = "svs,usb-nop-xceiv-icx" },
	{ }
};

MODULE_DEVICE_TABLE(of, nop_icx_xceiv_dt_ids);

static struct platform_driver nop_icx_generic_driver = {
	.probe		= nop_icx_generic_probe,
	.remove		= nop_icx_generic_remove,
	.driver		= {
		.name	= "usb_phy_generic_icx",
		.of_match_table = nop_icx_xceiv_dt_ids,
	},
};

static int __init nop_icx_generic_init(void)
{
	return platform_driver_register(&nop_icx_generic_driver);
}
subsys_initcall(nop_icx_generic_init);

static void __exit nop_icx_generic_exit(void)
{
	platform_driver_unregister(&nop_icx_generic_driver);
}
module_exit(nop_icx_generic_exit);

MODULE_ALIAS("platform:usb_phy_generic_icx");
MODULE_AUTHOR("Texas Instruments Inc");
MODULE_DESCRIPTION("NOP USB Transceiver driver with extcon notifier");
MODULE_LICENSE("GPL");
