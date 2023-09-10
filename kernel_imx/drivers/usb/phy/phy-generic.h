/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PHY_GENERIC_H_
#define _PHY_GENERIC_H_

#include <linux/usb/usb_phy_generic.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/extcon.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

struct usb_phy_generic {
	struct usb_phy phy;
	struct device *dev;
	struct clk *clk;
	struct regulator *vcc;
	struct gpio_desc *gpiod_reset;
	struct gpio_desc *gpiod_vbus;
	struct regulator *vbus_draw;
	bool vbus_draw_enabled;
	unsigned long mA;		    /* Lock EL */
	unsigned int vbus;
#if (defined(CONFIG_NOP_USB_XCEIV_ICX))
#define	USB_PHY_GENERIC_FLAGS_NO_MORE_WORK	(0)
	unsigned long		edev_flags; /* Lock EL */
	spinlock_t		edev_lock;  /* Lock Symbol: EL */
	struct extcon_dev	*edev_vbus;
	struct work_struct	edev_work;
#endif /* (defined(CONFIG_NOP_USB_XCEIV_ICX)) */
};

int usb_gen_phy_init(struct usb_phy *phy);
void usb_gen_phy_shutdown(struct usb_phy *phy);

int usb_phy_gen_create_phy(struct device *dev, struct usb_phy_generic *nop,
		struct usb_phy_generic_platform_data *pdata);

#endif
