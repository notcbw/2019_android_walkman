/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 */
/*
 * cxd3778gf_platform.c
 *
 * CXD3778GF CODEC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* #define TRACE_PRINT_ON */
/* #define DEBUG_PRINT_ON */
#define TRACE_TAG "------- "
#define DEBUG_TAG "        "

#include "cxd3778gf_common.h"
#include <linux/icx_dmp_board_id.h>

#define CHK_RV(_msg) \
	if(rv<0){ \
		print_fail(_msg,rv); \
		return(-1); \
	}

static int initialized = FALSE;
static unsigned int board_type = TYPE_A;

int  board_set_flag = 0;

static struct regulator * regulator_285 = NULL;
static struct regulator * regulator_180 = NULL;

int mute_type = BTL_RELAY_MUTE;
int power_type = CONTROL_NEGATIVE_VOLTAGE;

static int global_da_xrst;
static int global_da_ldo_en;
static int global_hp_se_xmute;
static int global_au_avdd_pwr_en;
static int global_au_vmb_en;
static int global_da_hp_xdet;
static int global_hp_btl_det;
static int global_osc_fs480_en;
static int global_osc_fs441_en;
static int global_au_btl_pwr5v_en;
static int global_au_btl_pwr7v_en;
static int global_hp_se_mute_cp_on;
static int global_hp_btl_xmute;
static int global_au_board_id1;
static unsigned int global_pwm_phdly_btl;
static unsigned int global_pwm_phdly_se;
static unsigned int global_hp_debounce_interval;

int cxd3778gf_setup_platform(struct cxd3778gf_driver_data * data, unsigned int * type, unsigned int *ptype)
{
	int rv;
	struct device *dev;
	struct device_node *np;

	dev = &(data->i2c->dev);
	np = dev->of_node;

	if (icx_dmp_board_id.setid == ICX_DMP_SETID_ICX_1295) {
		*type = TYPE_Z;
		*ptype = NO_CONTROL_NEGATIVE_VOLTAGE;
		if (icx_dmp_board_id.bid != ICX_DMP_BID_BB)
			board_set_flag |= OUTPUT_BTL_LR_REVERSE_FLAG |
						HW_MUTE_CONSTANT_30MS_FLAG;
	} else {
		*type = TYPE_A;
		if (icx_dmp_board_id.bid != ICX_DMP_BID_BB)
			board_set_flag |= OUTPUT_SE_LR_REVERSE_FLAG |
						HW_MUTE_CONSTANT_30MS_FLAG;
	}

	board_type = *type;
	power_type = *ptype;

	global_da_xrst = of_get_named_gpio(np, "da_xrst-gpio", 0);
	if (!gpio_is_valid(global_da_xrst))
		return -1;

	global_da_ldo_en = of_get_named_gpio(np, "da_ldo_en-gpio", 0);
	if (!gpio_is_valid(global_da_ldo_en))
		return -1;

	global_hp_se_xmute = of_get_named_gpio(np, "hp_se_xmute-gpio", 0);
	if (!gpio_is_valid(global_hp_se_xmute))
		return -1;

	global_au_avdd_pwr_en = of_get_named_gpio(np, "au_avdd_pwr_en-gpio", 0);
	if (!gpio_is_valid(global_au_avdd_pwr_en))
		return -1;

	global_au_vmb_en = of_get_named_gpio(np, "au_vmb_en-gpio", 0);
	if (!gpio_is_valid(global_au_vmb_en))
		return -1;

	global_da_hp_xdet = of_get_named_gpio(np, "da_hp_xdet-gpio", 0);
	if (!gpio_is_valid(global_da_hp_xdet))
		return -1;

        global_hp_btl_det = of_get_named_gpio(np, "hp_btl_det-gpio", 0);
	if (!gpio_is_valid(global_hp_btl_det))
		return -1;

	global_au_btl_pwr5v_en = of_get_named_gpio(np, "au_btl_pwr5v_en-gpio", 0);
	if (!gpio_is_valid(global_au_btl_pwr5v_en))
		return -1;

	global_au_btl_pwr7v_en = of_get_named_gpio(np, "au_btl_pwr7v_en-gpio", 0);
	if (!gpio_is_valid(global_au_btl_pwr7v_en))
		return -1;

	global_hp_se_mute_cp_on = of_get_named_gpio(np, "hp_se_mute_cp_on-gpio", 0);
	if (!gpio_is_valid(global_hp_se_mute_cp_on))
		return -1;

	global_hp_btl_xmute = of_get_named_gpio(np, "hp_btl_xmute-gpio", 0);
	if (!gpio_is_valid(global_hp_btl_xmute))
		return -1;

	global_osc_fs441_en = of_get_named_gpio(np, "osc_fs441_en-gpio", 0);
	if (!gpio_is_valid(global_osc_fs441_en))
		return -1;

	global_osc_fs480_en = of_get_named_gpio(np, "osc_fs480_en-gpio", 0);
	if (!gpio_is_valid(global_osc_fs480_en))
		return -1;

	gpio_direction_output(global_au_avdd_pwr_en, 0);

	if (board_type == TYPE_A)
		gpio_direction_output(global_au_vmb_en, 0);

	if (board_type == TYPE_Z)
		gpio_direction_output(global_au_btl_pwr5v_en, 0);

	gpio_direction_output(global_da_ldo_en, 0);
	gpio_direction_output(global_da_xrst, 0);
	gpio_direction_output(global_hp_se_xmute, 0);

	if (board_type == TYPE_Z) {
		gpio_direction_output(global_au_btl_pwr7v_en, 0);
		gpio_direction_output(global_hp_se_mute_cp_on, 0);
		gpio_direction_output(global_osc_fs441_en, 0);
		gpio_direction_output(global_osc_fs480_en, 0);
		gpio_direction_output(global_hp_btl_xmute, 0);
		gpio_direction_input(global_hp_btl_det);
	}

	gpio_direction_input(global_da_hp_xdet);

	if (of_property_read_u32(np, "pwm_phdly_btl", &global_pwm_phdly_btl))
		global_pwm_phdly_btl = 0x00;

	if (of_property_read_u32(np, "pwm_phdly_se", &global_pwm_phdly_se))
		global_pwm_phdly_se = 0x00;

	if (of_property_read_u32(np,
				 "hp-debounce-interval",
				 &global_hp_debounce_interval))
		global_hp_debounce_interval = 0;

	initialized = TRUE;

	return(0);
}

int cxd3778gf_reset_platform(void)
{
	print_trace("%s()\n",__FUNCTION__);

	gpio_direction_input(global_da_hp_xdet);
	gpio_free(global_da_hp_xdet);
	gpio_direction_input(global_hp_btl_det);
	gpio_free(global_hp_btl_det);
	gpio_direction_input(global_osc_fs480_en);
	gpio_free(global_osc_fs480_en);
	gpio_direction_input(global_osc_fs441_en);
	gpio_free(global_osc_fs441_en);
	gpio_direction_input(global_hp_se_mute_cp_on);
	gpio_free(global_hp_se_mute_cp_on);
	gpio_direction_input(global_au_btl_pwr7v_en);
	gpio_free(global_au_btl_pwr7v_en);
	gpio_direction_input(global_hp_se_xmute);
	gpio_free(global_hp_se_xmute);
	gpio_direction_input(global_da_xrst);
	gpio_free(global_da_xrst);
	gpio_direction_input(global_da_ldo_en);
	gpio_free(global_da_ldo_en);
	gpio_direction_input(global_au_btl_pwr5v_en);
	gpio_free(global_au_btl_pwr5v_en);
	gpio_direction_input(global_au_vmb_en);
	gpio_free(global_au_vmb_en);
	gpio_direction_input(global_au_avdd_pwr_en);
	gpio_free(global_au_avdd_pwr_en);

	return(0);
}

int cxd3778gf_switch_180_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(0);
}

int cxd3778gf_switch_285_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(0);
}

int cxd3778gf_switch_hp3x_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if (value) {
		if (board_type == TYPE_Z)
			gpio_set_value(global_au_btl_pwr7v_en, 1);
	} else {
		if (board_type == TYPE_Z)
			gpio_set_value(global_au_btl_pwr7v_en, 0);
	}

	return(0);
}

int cxd3778gf_switch_logic_ldo(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(0);
}

int cxd3778gf_switch_external_osc(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(0);
}

int cxd3778gf_get_external_osc(void)
{
	return(0);
}

int cxd3778gf_reset(void)
{
        if (board_type == TYPE_Z) {
                gpio_set_value(global_au_btl_pwr7v_en, 0);
                usleep_range(1000, 1100);
                gpio_set_value(global_hp_se_mute_cp_on, 0);
                gpio_set_value(global_osc_fs441_en, 0);
                gpio_set_value(global_osc_fs480_en, 0);
        }

	usleep_range(10000, 11000);
	gpio_set_value(global_da_xrst, 0);
	usleep_range(1000, 1100);
	gpio_set_value(global_da_ldo_en, 0);

	if (board_type == TYPE_Z) {
		gpio_set_value(global_au_btl_pwr5v_en, 0);
		usleep_range(1000, 1100);
	}

	if (board_type == TYPE_A) {
		gpio_set_value(global_au_vmb_en, 0);
		usleep_range(1000, 1100);
	}

	gpio_set_value(global_au_avdd_pwr_en, 0);

	return(0);
}

int cxd3778gf_unreset(void)
{
	print_trace("%s()\n",__FUNCTION__);

	gpio_set_value(global_au_avdd_pwr_en, 1);
	usleep_range(1000, 1100);

	if (board_type == TYPE_A) {
		gpio_set_value(global_au_vmb_en, 1);
		usleep_range(1000, 1100);
	}

	if (board_type == TYPE_Z) {
		gpio_set_value(global_au_btl_pwr5v_en, 1);
		usleep_range(1000, 1100);
	}

	gpio_set_value(global_da_ldo_en, 1);
	usleep_range(10000, 11000);
	gpio_set_value(global_da_xrst, 1);
	usleep_range(1000, 1100);

	if (board_type == TYPE_Z) {
		gpio_set_value(global_hp_se_mute_cp_on, 0);
		gpio_set_value(global_au_btl_pwr7v_en, 0);
		gpio_set_value(global_osc_fs441_en, 1);
		gpio_set_value(global_osc_fs480_en, 0);
	}

	return(0);
}

int cxd3778gf_switch_smaster_mute(int value, int amp)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value) {
		if (board_type == TYPE_Z) {
			if (amp == HEADPHONE_AMP_SMASTER_SE)
				gpio_set_value(global_hp_se_xmute, 0);

			if (amp == HEADPHONE_AMP_SMASTER_BTL)
				gpio_set_value(global_hp_btl_xmute, 0);
		}
		if (board_type == TYPE_A)
			gpio_set_value(global_hp_se_xmute, 0);
	} else {
		if (board_type == TYPE_Z) {
			if (amp == HEADPHONE_AMP_SMASTER_SE)
				gpio_set_value(global_hp_se_xmute, 1);

			if (amp == HEADPHONE_AMP_SMASTER_BTL)
				gpio_set_value(global_hp_btl_xmute, 1);
		}
		if (board_type == TYPE_A)
			gpio_set_value(global_hp_se_xmute, 1);
	}
	return(0);
}

int cxd3778gf_switch_class_h_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(0);
}

int cxd3778gf_switch_speaker_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(0);
}

int cxd3778gf_get_hp_det_se_value(void)
{
	int rv=0;

	print_trace("%s()\n",__FUNCTION__);

	if (global_da_hp_xdet >= 0)
		rv |= (1 ^ gpio_get_value(global_da_hp_xdet));

	return(rv);
}

int cxd3778gf_get_hp_det_btl_value(void)
{
	int rv=0;

	print_trace("%s()\n",__FUNCTION__);

	if (board_type == TYPE_Z)
		rv |= (1 ^ gpio_get_value(global_hp_btl_det));

	return(rv);
}

int cxd3778gf_get_mic_det_value(void)
{
	return(0);
}

int cxd3778gf_get_xpcm_det_value(void)
{
	print_trace("%s()\n",__FUNCTION__);

	return(0);
}

int cxd3778gf_get_xpcm_det_irq(void)
{
	int rv = 0;

	print_trace("%s()\n",__FUNCTION__);

	rv = gpio_to_irq(global_da_hp_xdet);

	return(rv);
}

int cxd3778gf_get_btl_det_irq(void)
{
	int rv = 0;

	print_trace("%s()\n",__FUNCTION__);

	rv = gpio_to_irq(global_hp_btl_det);

	return(rv);
}

int cxd3778gf_set_se_cp_mute(void)
{
        print_trace("%s()\n",__FUNCTION__);

	gpio_set_value(global_hp_se_mute_cp_on, 0);

        return(0);
}

int cxd3778gf_set_se_cp_unmute(void)
{
        print_trace("%s()\n",__FUNCTION__);

	gpio_set_value(global_hp_se_mute_cp_on, 1);

        return(0);
}


int cxd3778gf_set_441clk_enable(void)
{
	print_trace("%s()\n",__FUNCTION__);

	if (board_type == TYPE_Z){
		gpio_set_value(global_osc_fs480_en, 0);
		gpio_set_value(global_osc_fs441_en, 1);
	}

	return(0);
}

int cxd3778gf_set_480clk_enable(void)
{
	print_trace("%s()\n",__FUNCTION__);

	if (board_type == TYPE_Z) {
		gpio_set_value(global_osc_fs441_en, 0);
		gpio_set_value(global_osc_fs480_en, 1);
	}

	return(0);
}

int cxd3778gf_get_441clk_value(void)
{
	int rv = 0;

	print_trace("%s()\n",__FUNCTION__);

	if (board_type == TYPE_Z)
                rv= gpio_get_value(global_osc_fs441_en);

        return(rv);
}

int cxd3778gf_get_480clk_value(void)
{
	int rv = 0;

	print_trace("%s()\n",__FUNCTION__);

        if (board_type == TYPE_Z)
		rv= gpio_get_value(global_osc_fs480_en);

        return(rv);
}

int cxd3778gf_set_441_480clk_disable(void)
{
	print_trace("%s()\n", __func__);

	gpio_set_value(global_osc_fs441_en, 0);
	gpio_set_value(global_osc_fs480_en, 0);

	return 0;
}

unsigned int cxd3778gf_get_pwm_phdly(int headphone_amp)
{
	unsigned int dly = 0x00;

	switch (headphone_amp) {
	case HEADPHONE_AMP_SMASTER_BTL:
		dly = global_pwm_phdly_btl;
		break;
	case HEADPHONE_AMP_SMASTER_SE:
		dly = global_pwm_phdly_se;
		break;
	default:
		break;
	}

	return dly;
}

int cxd3778gf_get_hp_debounce_interval(void)
{
	print_trace("%s()\n",__FUNCTION__);

	return global_hp_debounce_interval;
}

/* for debug */

#if 0

/* if use, please modified PINMUX. */

void debug_gpio119(int value)
{
	static int init=0;
	int rv;

	if(init==0){
		rv=gpio_request(119,"DEBUG119");
		if(rv<0){
			printk(KERN_ERR "gpio_request(DEBUG119): code %d error occurred.\n",rv);
			printk(KERN_ERR "%s():\n",__FUNCTION__);
			return;
		}

		rv=gpio_direction_output(119,0);
		if(rv<0){
			printk(KERN_ERR "gpio_direction_output(DEBUG119): code %d error occurred.\n",rv);
			printk(KERN_ERR "%s():\n",__FUNCTION__);
			gpio_free(119);
			return;
		}

		init=1;
	}

	printk("GPIO119:%d\n",value?1:0);
	gpio_set_value(119,value?1:0);

	return;
}

#endif
