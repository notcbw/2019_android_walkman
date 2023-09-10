/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 */
/*
 * cxd3778gf_platform.h
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

#ifndef _CXD3778GF_PLATFORM_HEADER_
#define _CXD3778GF_PLATFORM_HEADER_

int cxd3778gf_setup_platform(struct cxd3778gf_driver_data * data, unsigned int * type, unsigned int * ptype);
int cxd3778gf_reset_platform(void);

int cxd3778gf_switch_180_power(int value);
int cxd3778gf_switch_285_power(int value);
int cxd3778gf_switch_hp3x_power(int value);
int cxd3778gf_switch_logic_ldo(int value);
int cxd3778gf_switch_external_osc(int value);
int cxd3778gf_get_external_osc(void);

int cxd3778gf_reset(void);
int cxd3778gf_unreset(void);

int cxd3778gf_switch_smaster_mute(int value, int amp);
int cxd3778gf_switch_class_h_mute(int value);

int cxd3778gf_switch_speaker_power(int value);

int cxd3778gf_get_hp_det_se_value(void);
int cxd3778gf_get_hp_det_btl_value(void);
int cxd3778gf_get_mic_det_value(void);
int cxd3778gf_get_xpcm_det_value(void);
int cxd3778gf_get_xpcm_det_irq(void);
int cxd3778gf_get_btl_det_irq(void);

int cxd3778gf_set_se_cp_mute(void);
int cxd3778gf_set_se_cp_unmute(void);

int cxd3778gf_set_441clk_enable(void);
int cxd3778gf_set_480clk_enable(void);
int cxd3778gf_get_441clk_value(void);
int cxd3778gf_get_480clk_value(void);
int cxd3778gf_set_441_480clk_disable(void);

unsigned int cxd3778gf_get_pwm_phdly(int headphone_amp);

int cxd3778gf_get_hp_debounce_interval(void);

extern int board_set_flag;
#define BTL_HEADPHONE_PLUG44_FLAG 	(0x00000001) /* change BTL plag 3.5pin x 2 or 4.4pin x 1 */
#define VPA_ON_FLAG 			(0x00000002) /* VPA power on or off */
#define NO_MCLK_DIRECTLY_INPUT_FLAG 	(0x00000004) /* OSC -> Ext_clk or OSC -> cxd3778gf -> Ext_clk */
#define OUTPUT_SE_LR_REVERSE_FLAG	(0x00000008) /* Headphone SE LR output reverse or not */
#define OUTPUT_LINEOUT2 		(0x00000010) /* OUTPUT LINEOUT2 */
#define NO_USE_SLAVE_USE_ASRC_FLAG	(0x00000020) /* don't use mt8590's slave asrc flag */
#define HW_MUTE_CONSTANT_30MS_FLAG	(0x00000040) /* hw_mute shoten (time constant 30msec) flag */
#define OUTPUT_BTL_LR_REVERSE_FLAG	(0x00000080) /* Headphone BTL LR output reverse or not */
#endif
