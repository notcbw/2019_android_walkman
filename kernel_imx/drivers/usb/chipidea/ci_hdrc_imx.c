/*
 * Copyright 2012-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 * on behalf of DENX Software Engineering GmbH
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/usb/chipidea.h>
#include <linux/usb/of.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <linux/busfreq-imx.h>
#include <linux/pm_qos.h>
#include <linux/usb/of.h>
#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))
#include <linux/spinlock.h>
#include <linux/power_supply.h>
#include <linux/extcon.h>
#include <linux/usb/phy.h>
#include <linux/usb/charger.h>
#include <uapi/linux/usb/charger.h>
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */

#include "ci.h"
#include "ci_hdrc_imx.h"

struct ci_hdrc_imx_platform_flag {
	unsigned int flags;
	bool runtime_pm;
};

static const struct ci_hdrc_imx_platform_flag imx23_usb_data = {
	.flags = CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx27_usb_data = {
		CI_HDRC_DISABLE_STREAMING,
};

static const struct ci_hdrc_imx_platform_flag imx28_usb_data = {
	.flags = CI_HDRC_IMX28_WRITE_FIX |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6q_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6sl_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_HOST_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6sx_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON |
		CI_HDRC_DISABLE_HOST_STREAMING |
		CI_HDRC_IMX_EHCI_QUIRK,
};

static const struct ci_hdrc_imx_platform_flag imx6ul_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_TURN_VBUS_EARLY_ON,
};

static const struct ci_hdrc_imx_platform_flag imx7d_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM,
};

static const struct ci_hdrc_imx_platform_flag imx7ulp_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM |
		CI_HDRC_IMX_EHCI_QUIRK |
		CI_HDRC_PMQOS,
};

static const struct ci_hdrc_imx_platform_flag imx8qm_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM,
};

static const struct ci_hdrc_imx_platform_flag imx8mm_usb_data = {
	.flags = CI_HDRC_SUPPORTS_RUNTIME_PM,
};

static const struct of_device_id ci_hdrc_imx_dt_ids[] = {
	{ .compatible = "fsl,imx23-usb", .data = &imx23_usb_data},
	{ .compatible = "fsl,imx28-usb", .data = &imx28_usb_data},
	{ .compatible = "fsl,imx27-usb", .data = &imx27_usb_data},
	{ .compatible = "fsl,imx6q-usb", .data = &imx6q_usb_data},
	{ .compatible = "fsl,imx6sl-usb", .data = &imx6sl_usb_data},
	{ .compatible = "fsl,imx6sx-usb", .data = &imx6sx_usb_data},
	{ .compatible = "fsl,imx6ul-usb", .data = &imx6ul_usb_data},
	{ .compatible = "fsl,imx7d-usb", .data = &imx7d_usb_data},
	{ .compatible = "fsl,imx7ulp-usb", .data = &imx7ulp_usb_data},
	{ .compatible = "fsl,imx8qm-usb", .data = &imx8qm_usb_data},
	{ .compatible = "fsl,imx8mm-usb", .data = &imx8mm_usb_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ci_hdrc_imx_dt_ids);

struct ci_hdrc_imx_data {
	struct usb_phy *phy;
	struct platform_device *ci_pdev;
	struct clk *clk;
	struct imx_usbmisc_data *usbmisc_data;
	bool supports_runtime_pm;
	bool in_lpm;
	struct regmap *anatop;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_hsic_active;
	struct regulator *hsic_pad_regulator;
	const struct ci_hdrc_imx_platform_flag *data;
	/* SoC before i.mx6 (except imx23/imx28) needs three clks */
	bool need_three_clks;
	struct clk *clk_ipg;
	struct clk *clk_ahb;
	struct clk *clk_per;
	/* --------------------------------- */
	struct pm_qos_request pm_qos_req;
#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))
#define	CI_HDRC_IMX_FLAGS_WORK_NO_MORE	(0)
	unsigned long		edev_flags;	/* Lock: edev_lock */
	spinlock_t		edev_lock;
	struct device		*dev;
	struct extcon_dev	*edev_chg;
	struct work_struct	edev_chg_work;
	enum usb_charger_type	edev_type;	/* Lock: edev_lock */
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */
};

#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))
static const unsigned int ci_hdrc_extcon_cables[] = {
	EXTCON_CHG_USB_SDP,	/* least preferred type. */
	EXTCON_CHG_USB_ACA,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_SLOW,
	EXTCON_CHG_USB_FAST,	/* most preferred type. */
	EXTCON_NONE,	/*!< Terminator. */
};
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */


/* Common functions shared by usbmisc drivers */

static struct imx_usbmisc_data *usbmisc_get_init_data(struct device *dev)
{
	struct platform_device *misc_pdev;
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	struct imx_usbmisc_data *data;
	int ret;

	/*
	 * In case the fsl,usbmisc property is not present this device doesn't
	 * need usbmisc. Return NULL (which is no error here)
	 */
	if (!of_get_property(np, "fsl,usbmisc", NULL))
		return NULL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	ret = of_parse_phandle_with_args(np, "fsl,usbmisc", "#index-cells",
					0, &args);
	if (ret) {
		dev_err(dev, "Failed to parse property fsl,usbmisc, errno %d\n",
			ret);
		return ERR_PTR(ret);
	}

	data->index = args.args[0];

	misc_pdev = of_find_device_by_node(args.np);
	of_node_put(args.np);

	if (!misc_pdev || !platform_get_drvdata(misc_pdev))
		return ERR_PTR(-EPROBE_DEFER);

	data->dev = &misc_pdev->dev;

	if (of_find_property(np, "disable-over-current", NULL))
		data->disable_oc = 1;

	if (of_find_property(np, "over-current-active-high", NULL))
		data->oc_polarity = 1;

	if (of_find_property(np, "power-polarity-active-high", NULL))
		data->pwr_polarity = 1;

	if (of_find_property(np, "external-vbus-divider", NULL))
		data->evdo = 1;

	if (of_usb_get_phy_mode(np) == USBPHY_INTERFACE_MODE_ULPI)
		data->ulpi = 1;

	if (of_find_property(np, "osc-clkgate-delay", NULL)) {
		ret = of_property_read_u32(np, "osc-clkgate-delay",
			&data->osc_clkgate_delay);
		if (ret) {
			dev_err(dev,
				"failed to get osc-clkgate-delay value\n");
			return ERR_PTR(ret);
		}
		/*
		 * 0 <= osc_clkgate_delay <=7
		 * - 0x0 (default) is 0.5ms,
		 * - 0x1-0x7: 1-7ms
		 */
		if (data->osc_clkgate_delay > 7) {
			dev_err(dev,
				"value of osc-clkgate-delay is incorrect\n");
			return ERR_PTR(-EINVAL);
		}
	}

	of_property_read_u32(np, "picophy,pre-emp-curr-control",
			&data->emp_curr_control);
	of_property_read_u32(np, "picophy,dc-vol-level-adjust",
			&data->dc_vol_level_adjust);

	return data;
}

/* End of common functions shared by usbmisc drivers*/
static int imx_get_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	data->clk_ipg = devm_clk_get(dev, "ipg");
	if (IS_ERR(data->clk_ipg)) {
		/* If the platform only needs one clocks */
		data->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(data->clk)) {
			ret = PTR_ERR(data->clk);
			dev_err(dev,
				"Failed to get clks, err=%ld,%ld\n",
				PTR_ERR(data->clk), PTR_ERR(data->clk_ipg));
			return ret;
		}
		return ret;
	}

	data->clk_ahb = devm_clk_get(dev, "ahb");
	if (IS_ERR(data->clk_ahb)) {
		ret = PTR_ERR(data->clk_ahb);
		dev_err(dev,
			"Failed to get ahb clock, err=%d\n", ret);
		return ret;
	}

	data->clk_per = devm_clk_get(dev, "per");
	if (IS_ERR(data->clk_per)) {
		ret = PTR_ERR(data->clk_per);
		dev_err(dev,
			"Failed to get per clock, err=%d\n", ret);
		return ret;
	}

	data->need_three_clks = true;
	return ret;
}

static int imx_prepare_enable_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	if (data->need_three_clks) {
		ret = clk_prepare_enable(data->clk_ipg);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable ipg clk, err=%d\n",
				ret);
			return ret;
		}

		ret = clk_prepare_enable(data->clk_ahb);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable ahb clk, err=%d\n",
				ret);
			clk_disable_unprepare(data->clk_ipg);
			return ret;
		}

		ret = clk_prepare_enable(data->clk_per);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable per clk, err=%d\n",
				ret);
			clk_disable_unprepare(data->clk_ahb);
			clk_disable_unprepare(data->clk_ipg);
			return ret;
		}
	} else {
		ret = clk_prepare_enable(data->clk);
		if (ret) {
			dev_err(dev,
				"Failed to prepare/enable clk, err=%d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

static void imx_disable_unprepare_clks(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);

	if (data->need_three_clks) {
		clk_disable_unprepare(data->clk_per);
		clk_disable_unprepare(data->clk_ahb);
		clk_disable_unprepare(data->clk_ipg);
	} else {
		clk_disable_unprepare(data->clk);
	}
}

#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))

/* interface to extcon framework */
static void ci_hdrc_imx_edev_chg_notify(struct ci_hdrc_imx_data *data)
{
	int	ret = 0;
	unsigned int		id_det;
	const unsigned int	*cables;
	unsigned int		id;
	enum usb_charger_type	type;
	unsigned long		flags;

	if (IS_ERR_OR_NULL(data->edev_chg)) {
		/* There is no extcon device. */
		return;
	}
	spin_lock_irqsave(&(data->edev_lock), flags);
	type = data->edev_type;
	spin_unlock_irqrestore(&(data->edev_lock), flags);

	id_det = EXTCON_NONE;
	switch (type) {
	case  SDP_TYPE:
		id_det = EXTCON_CHG_USB_SDP;
		break;
	case  DCP_TYPE:
		id_det = EXTCON_CHG_USB_DCP;
		break;
	case  CDP_TYPE:
		id_det = EXTCON_CHG_USB_CDP;
		break;
	case  ACA_TYPE:
		id_det = EXTCON_CHG_USB_ACA;
		break;
	default:
		/* keep id_det */
		break;
	}

	/* Set state(s) to false, notify only it is changed. */
	cables = ci_hdrc_extcon_cables;
	while ((id = *cables) != EXTCON_NONE) {
		if (id != id_det) {
			/* cable is not detected USB-AC charger. */
			ret = extcon_get_state(data->edev_chg, id);
			if (ret < 0) {
				/* Can't get state. */
				dev_warn(data->dev,
					"Fail to get state. id=%d, ret=%d\n",
					id, ret
				);
				cables++;
				continue;
			}
			if (((bool)ret) == true) {
				/* charger is detected,
				 * currently not connected.
				 */
				ret = extcon_set_state_sync(data->edev_chg,
					id, false
				);
				if (ret < 0) {
					/* Can't set state. */
					dev_warn(data->dev,
						"Fail to set state false. id=%d, ret=%d\n",
						id, ret
					);
				}
			}
		}
		cables++;
	}

	/* Set state to true, notify only it is changed. */
	cables = ci_hdrc_extcon_cables;
	while ((id = *cables) != EXTCON_NONE) {
		if (id == id_det) {
			ret = extcon_get_state(data->edev_chg, id);
			if (ret < 0) {
				/* Can't get state. */
				dev_warn(data->dev,
					"Fail to get state. id=%d, ret=%d\n",
					id, ret
				);
				cables++;
				continue;
			}
			if (((bool)ret) == false) {
				/* charger is not detected,
				 * currently connected.
				 */
				ret = extcon_set_state_sync(data->edev_chg,
					id, true);
				if (ret < 0) {
					/* Can't get state. */
					dev_warn(data->dev,
						"Fail to set state true. id=%d, ret=%d\n",
						id, ret
					);
				}
			}
		}
		cables++;
	}
	spin_lock_irqsave(&(data->edev_lock), flags);
	if (type != data->edev_type) {
		/* Some one change state. */
		if (test_bit(CI_HDRC_IMX_FLAGS_WORK_NO_MORE,
			&(data->edev_flags)) == 0) {
			dev_notice(data->dev,
				"Charger type changes, restart work. type=%d, edev_type=%d\n",
				(int)type, (int)(data->edev_type)
			);
			schedule_work(&(data->edev_chg_work));
		}
	}
	spin_unlock_irqrestore(&(data->edev_lock), flags);
}

static void ci_hdrc_imx_edev_chg_work(struct work_struct *work)
{	struct ci_hdrc_imx_data *data =
		 container_of(work, struct ci_hdrc_imx_data, edev_chg_work);
	ci_hdrc_imx_edev_chg_notify(data);
}

static void ci_hdrc_imx_edev_chg_queue(struct ci_hdrc_imx_data *data)
{
	struct usb_phy *usb_phy = data->phy;
	unsigned long		flags;

	spin_lock_irqsave(&(data->edev_lock), flags);
	data->edev_type = usb_phy->chg_type;
	dev_info(data->dev,
		"Detected charger. edev_type=%d\n",
		(int)(data->edev_type)
	);
	schedule_work(&(data->edev_chg_work));
	spin_unlock_irqrestore(&(data->edev_lock), flags);
}
#else  /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */
static void ci_hdrc_imx_edev_chg_queue(struct ci_hdrc_imx_data *data)
{	/* do nothing. */
}
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */

static int ci_hdrc_imx_notify_event(struct ci_hdrc *ci, unsigned event)
{
	struct device *dev = ci->dev->parent;
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;
	struct imx_usbmisc_data *mdata = data->usbmisc_data;

	switch (event) {
	case CI_HDRC_CONTROLLER_VBUS_EVENT:
		if (ci->vbus_active)
			ret = imx_usbmisc_charger_detection(mdata, true);
		else
			ret = imx_usbmisc_charger_detection(mdata, false);
		ci_hdrc_imx_edev_chg_queue(data);
		break;
	case CI_HDRC_IMX_HSIC_ACTIVE_EVENT:
		if (!IS_ERR(data->pinctrl) &&
			!IS_ERR(data->pinctrl_hsic_active)) {
			ret = pinctrl_select_state(data->pinctrl,
					data->pinctrl_hsic_active);
			if (ret)
				dev_err(dev,
					"hsic_active select failed, err=%d\n",
					ret);
			return ret;
		}
		break;
	case CI_HDRC_IMX_HSIC_SUSPEND_EVENT:
		if (data->usbmisc_data) {
			ret = imx_usbmisc_hsic_set_connect(data->usbmisc_data);
			if (ret)
				dev_err(dev,
					"hsic_set_connect failed, err=%d\n",
					ret);
			return ret;
		}
		break;
	case CI_HDRC_IMX_TERM_SELECT_OVERRIDE_FS:
		if (data->usbmisc_data)
			return imx_usbmisc_term_select_override(
					data->usbmisc_data, true, 1);
		break;
	case CI_HDRC_IMX_TERM_SELECT_OVERRIDE_OFF:
		if (data->usbmisc_data)
			return imx_usbmisc_term_select_override(
					data->usbmisc_data, false, 0);
		break;
	default:
		dev_dbg(dev, "unknown event\n");
	}

	return ret;
}

#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))
static int ci_hdrc_imx_extcon_probe(struct ci_hdrc_imx_data *data,
	struct device *dev)
{	int	result = 0;
	int	ret = 0;
	const unsigned int	*cables;

	data->dev = dev;
	spin_lock_init(&(data->edev_lock));
	INIT_WORK(&(data->edev_chg_work), ci_hdrc_imx_edev_chg_work);
	/* Allocate extcon device */
	cables = ci_hdrc_extcon_cables;
	data->edev_chg = devm_extcon_dev_allocate(dev, cables);
	if (IS_ERR_OR_NULL(data->edev_chg)) {
		result = PTR_ERR(data->edev_chg);
		dev_err(dev, "Failed to allocate extcon device. result=%d\n",
			result
		);
		goto out;
	}

	/* Register extcon device. */
	ret = devm_extcon_dev_register(dev, data->edev_chg);
	if (ret != 0) {
		result = ret;
		dev_err(dev, "Failed to register extcon device. result=%d\n",
			result
		);
		goto out;
	}
out:
	return result;
}
#else  /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */
static int ci_hdrc_imx_extcon_probe(struct ci_hdrc_imx_data *data,
	struct device *dev)
{	/* do nothing. */
	return 0;
}
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */


static int ci_hdrc_imx_probe(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data;
	struct ci_hdrc_platform_data pdata = {
		.name		= dev_name(&pdev->dev),
		.capoffset	= DEF_CAPOFFSET,
		.notify_event	= ci_hdrc_imx_notify_event,
	};
	int ret;
	const struct of_device_id *of_id;
	const struct ci_hdrc_imx_platform_flag *imx_platform_flag;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pinctrl_state *pinctrl_hsic_idle;

	of_id = of_match_device(ci_hdrc_imx_dt_ids, dev);
	if (!of_id)
		return -ENODEV;

	imx_platform_flag = of_id->data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	(void) ci_hdrc_imx_extcon_probe(data, dev);
	data->data = imx_platform_flag;
	pdata.flags |= imx_platform_flag->flags;
	data->usbmisc_data = usbmisc_get_init_data(dev);
	if (IS_ERR(data->usbmisc_data))
		return PTR_ERR(data->usbmisc_data);

	data->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(data->pinctrl)) {
		dev_dbg(dev, "pinctrl get failed, err=%ld\n",
						PTR_ERR(data->pinctrl));
	} else {
		pinctrl_hsic_idle = pinctrl_lookup_state(data->pinctrl, "idle");
		if (IS_ERR(pinctrl_hsic_idle)) {
			dev_dbg(dev,
				"pinctrl_hsic_idle lookup failed, err=%ld\n",
						PTR_ERR(pinctrl_hsic_idle));
		} else {
			ret = pinctrl_select_state(data->pinctrl,
						pinctrl_hsic_idle);
			if (ret) {
				dev_err(dev,
					"hsic_idle select failed, err=%d\n",
									ret);
				return ret;
			}
		}

		data->pinctrl_hsic_active = pinctrl_lookup_state(data->pinctrl,
								"active");
		if (IS_ERR(data->pinctrl_hsic_active))
			dev_dbg(dev,
				"pinctrl_hsic_active lookup failed, err=%ld\n",
					PTR_ERR(data->pinctrl_hsic_active));
	}

	ret = imx_get_clks(dev);
	if (ret)
		return ret;

	request_bus_freq(BUS_FREQ_HIGH);
	if (pdata.flags & CI_HDRC_PMQOS)
		pm_qos_add_request(&data->pm_qos_req,
			PM_QOS_CPU_DMA_LATENCY, 0);

	ret = imx_prepare_enable_clks(dev);
	if (ret)
		goto err_bus_freq;

	data->phy = devm_usb_get_phy_by_phandle(dev, "fsl,usbphy", 0);
	if (IS_ERR(data->phy)) {
		ret = PTR_ERR(data->phy);
		/* Return -EINVAL if no usbphy is available */
		if (ret == -ENODEV)
			ret = -EINVAL;
		goto err_clk;
	}

	pdata.usb_phy = data->phy;
	data->usbmisc_data->usb_phy = data->phy;
	if (pdata.flags & CI_HDRC_SUPPORTS_RUNTIME_PM)
		data->supports_runtime_pm = true;

	if (of_find_property(np, "ci-disable-lpm", NULL)) {
		data->supports_runtime_pm = false;
		pdata.flags &= ~CI_HDRC_SUPPORTS_RUNTIME_PM;
	}

	if (of_usb_get_phy_mode(dev->of_node) == USBPHY_INTERFACE_MODE_HSIC) {
		pdata.flags |= CI_HDRC_IMX_IS_HSIC;
		data->usbmisc_data->hsic = 1;
		data->hsic_pad_regulator = devm_regulator_get(dev, "pad");
		if (PTR_ERR(data->hsic_pad_regulator) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_clk;
		} else if (PTR_ERR(data->hsic_pad_regulator) == -ENODEV) {
			/* no pad regualator is needed */
			data->hsic_pad_regulator = NULL;
		} else if (IS_ERR(data->hsic_pad_regulator)) {
			dev_err(dev, "Get hsic pad regulator error: %ld\n",
					PTR_ERR(data->hsic_pad_regulator));
			ret = PTR_ERR(data->hsic_pad_regulator);
			goto err_clk;
		}

		if (data->hsic_pad_regulator) {
			ret = regulator_enable(data->hsic_pad_regulator);
			if (ret) {
				dev_err(dev,
					"Fail to enable hsic pad regulator\n");
				goto err_clk;
			}
		}
	}

	if (of_find_property(np, "fsl,anatop", NULL) && data->usbmisc_data) {
		data->anatop = syscon_regmap_lookup_by_phandle(np,
							"fsl,anatop");
		if (IS_ERR(data->anatop)) {
			dev_dbg(dev, "failed to find regmap for anatop\n");
			ret = PTR_ERR(data->anatop);
			goto disable_hsic_regulator;
		}
		data->usbmisc_data->anatop = data->anatop;
	}

#if (defined(CONFIG_USB_ICX_VBUS_SUSPEND_OFF))
	if (of_find_property(np, "shes,vbus-suspend-off", NULL)) {
		dev_notice(dev, "VBUS will be off while suspending.\n");
		pdata.flags |=  CI_HDRC_VBUS_SUSPEND_OFF;
	} else {
		dev_notice(dev, "VBUS will be kept on while suspending.\n");
		pdata.flags &= ~CI_HDRC_VBUS_SUSPEND_OFF;
	}
#endif /* (defined(CONFIG_USB_ICX_VBUS_SUSPEND_OFF)) */

	ret = imx_usbmisc_init(data->usbmisc_data);
	if (ret) {
		dev_err(dev, "usbmisc init failed, ret=%d\n", ret);
		goto disable_hsic_regulator;
	}

	data->ci_pdev = ci_hdrc_add_device(dev,
				pdev->resource, pdev->num_resources,
				&pdata);
	if (IS_ERR(data->ci_pdev)) {
		ret = PTR_ERR(data->ci_pdev);
		if (ret != -EPROBE_DEFER)
			dev_err(dev,
				"ci_hdrc_add_device failed, err=%d\n", ret);
		goto disable_hsic_regulator;
	}

	ret = imx_usbmisc_init_post(data->usbmisc_data);
	if (ret) {
		dev_err(dev, "usbmisc post failed, ret=%d\n", ret);
		goto disable_device;
	}

	ret = imx_usbmisc_set_wakeup(data->usbmisc_data, false);
	if (ret) {
		dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n", ret);
		goto disable_device;
	}

	/* usbmisc needs to know dr mode to choose wakeup setting */
	if (data->usbmisc_data)
		data->usbmisc_data->available_role =
			ci_hdrc_query_available_role(data->ci_pdev);

	if (data->supports_runtime_pm) {
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	device_set_wakeup_capable(dev, true);

	return 0;

disable_device:
	ci_hdrc_remove_device(data->ci_pdev);
disable_hsic_regulator:
	if (data->hsic_pad_regulator)
		ret = regulator_disable(data->hsic_pad_regulator);
err_clk:
	imx_disable_unprepare_clks(&pdev->dev);
err_bus_freq:
	if (pdata.flags & CI_HDRC_PMQOS)
		pm_qos_remove_request(&data->pm_qos_req);
	release_bus_freq(BUS_FREQ_HIGH);
	return ret;
}

#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))
static void ci_hdrc_imx_edev_shutdown(struct ci_hdrc_imx_data *data)
{	unsigned long	flags;
	spin_lock_irqsave(&(data->edev_lock), flags);
	set_bit(CI_HDRC_IMX_FLAGS_WORK_NO_MORE, &(data->edev_flags));
	spin_unlock_irqrestore(&(data->edev_lock), flags);
	cancel_work_sync(&(data->edev_chg_work));
}
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */

static int ci_hdrc_imx_remove(struct platform_device *pdev)
{
	struct ci_hdrc_imx_data *data = platform_get_drvdata(pdev);

	if (data->supports_runtime_pm) {
		pm_runtime_get_sync(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);
	}
	ci_hdrc_remove_device(data->ci_pdev);
	imx_disable_unprepare_clks(&pdev->dev);
	if (data->data->flags & CI_HDRC_PMQOS)
		pm_qos_remove_request(&data->pm_qos_req);
	release_bus_freq(BUS_FREQ_HIGH);
	if (data->hsic_pad_regulator)
		regulator_disable(data->hsic_pad_regulator);

#if (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON))
	ci_hdrc_imx_edev_shutdown(data);
#endif /* (defined(CONFIG_USB_CHIPIDEA_CHAGER_DETECT_EXTCON)) */
	return 0;
}

static void ci_hdrc_imx_shutdown(struct platform_device *pdev)
{
	ci_hdrc_imx_remove(pdev);
}

#ifdef CONFIG_PM
static int imx_controller_suspend(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "at %s\n", __func__);

	if (data->usbmisc_data) {
		ret = imx_usbmisc_hsic_set_clk(data->usbmisc_data, false);
		if (ret) {
			dev_err(dev,
				"usbmisc hsic_set_clk failed, ret=%d\n", ret);
			return ret;
		}
	}
    ret = imx_usbmisc_suspend(data->usbmisc_data);
    if (ret) {
        dev_err(dev,"usbmisc suspend failed, ret=%d\n", ret);
        return ret;
    }

	imx_disable_unprepare_clks(dev);
	if (data->data->flags & CI_HDRC_PMQOS)
		pm_qos_remove_request(&data->pm_qos_req);
	release_bus_freq(BUS_FREQ_HIGH);
	data->in_lpm = true;

	return 0;
}

static int imx_controller_resume(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(dev, "at %s\n", __func__);

	if (!data->in_lpm)
		return 0;

	request_bus_freq(BUS_FREQ_HIGH);
	if (data->data->flags & CI_HDRC_PMQOS)
		pm_qos_add_request(&data->pm_qos_req,
			PM_QOS_CPU_DMA_LATENCY, 0);
	ret = imx_prepare_enable_clks(dev);
	if (ret)
		goto err_bus_freq;

	data->in_lpm = false;

	ret = imx_usbmisc_power_lost_check(data->usbmisc_data);
	/* re-init if resume from power lost */
	if (ret > 0) {
		ret = imx_usbmisc_init(data->usbmisc_data);
		if (ret) {
			dev_err(dev, "usbmisc init failed, ret=%d\n", ret);
			goto clk_disable;
		}
	}

	ret = imx_usbmisc_set_wakeup(data->usbmisc_data, false);
	if (ret) {
		dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n", ret);
		goto clk_disable;
	}

	ret = imx_usbmisc_hsic_set_clk(data->usbmisc_data, true);
	if (ret) {
		dev_err(dev, "usbmisc hsic_set_clk failed, ret=%d\n", ret);
		goto hsic_set_clk_fail;
	}

    ret = imx_usbmisc_resume(data->usbmisc_data);
    if (ret) {
        dev_err(dev,"usbmisc resume failed, ret=%d\n", ret);
        return ret;
    }


	return 0;

hsic_set_clk_fail:
	imx_usbmisc_set_wakeup(data->usbmisc_data, true);
clk_disable:
	imx_disable_unprepare_clks(dev);
err_bus_freq:
	if (data->data->flags & CI_HDRC_PMQOS)
		pm_qos_remove_request(&data->pm_qos_req);
	release_bus_freq(BUS_FREQ_HIGH);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int ci_hdrc_imx_suspend(struct device *dev)
{
	int ret;

	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);

	if (data->in_lpm)
		/* The core's suspend doesn't run */
		return 0;

	if (device_may_wakeup(dev)) {
		ret = imx_usbmisc_set_wakeup(data->usbmisc_data, true);
		if (ret) {
			dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n",
					ret);
			return ret;
		}
	}

	ret = imx_controller_suspend(dev);
	if (ret)
		return ret;

	pinctrl_pm_select_sleep_state(dev);
	return ret;
}

static int ci_hdrc_imx_resume(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	pinctrl_pm_select_default_state(dev);
	ret = imx_controller_resume(dev);
	if (!ret && data->supports_runtime_pm) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static int ci_hdrc_imx_runtime_suspend(struct device *dev)
{
	struct ci_hdrc_imx_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->in_lpm)
		return 0;

	ret = imx_usbmisc_set_wakeup(data->usbmisc_data, true);
	if (ret) {
		dev_err(dev, "usbmisc set_wakeup failed, ret=%d\n", ret);
		return ret;
	}

	return imx_controller_suspend(dev);
}

static int ci_hdrc_imx_runtime_resume(struct device *dev)
{
	return imx_controller_resume(dev);
}

#endif /* CONFIG_PM */

static const struct dev_pm_ops ci_hdrc_imx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ci_hdrc_imx_suspend, ci_hdrc_imx_resume)
	SET_RUNTIME_PM_OPS(ci_hdrc_imx_runtime_suspend,
			ci_hdrc_imx_runtime_resume, NULL)
};
static struct platform_driver ci_hdrc_imx_driver = {
	.probe = ci_hdrc_imx_probe,
	.remove = ci_hdrc_imx_remove,
	.shutdown = ci_hdrc_imx_shutdown,
	.driver = {
		.name = "imx_usb",
		.of_match_table = ci_hdrc_imx_dt_ids,
		.pm = &ci_hdrc_imx_pm_ops,
	 },
};

module_platform_driver(ci_hdrc_imx_driver);

MODULE_ALIAS("platform:imx-usb");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CI HDRC i.MX USB binding");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
