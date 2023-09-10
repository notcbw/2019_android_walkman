/*
 * cxd3778gf.h
 *
 * CXD3778GF CODEC driver
 *
 * Copyright 2013, 2015, 2016, 2017, 2018 Sony Corporation
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
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

#ifndef _CXD3778GF_HEADER_
#define _CXD3778GF_HEADER_

#define CXD3778GF_DEVICE_NAME  "CODEC_CXD3778GF"
#define CXD3778GF_CODEC_NAME_I2C0   CXD3778GF_DEVICE_NAME ".0-004e"
#define CXD3778GF_CODEC_NAME_I2C2   CXD3778GF_DEVICE_NAME ".2-004e"
#define CXD3778GF_STD_DAI_NAME "DAI_CXD3778GF_STD"
#define CXD3778GF_ICX_DAI_NAME "DAI_CXD3778GF_ICX"
#define CXD3778GF_DAC_DAI_NAME "DAI_CXD3778GF_DAC"
#define CXD3778GF_EXT_DEVICE_NAME "CODEC_CXD3778GF_EXT"

#define TYPE_A	1
#define TYPE_Z	2

#define BTL_RELAY_MUTE  0
#define BTL_FET_MUTE    1

#define CONTROL_NEGATIVE_VOLTAGE       0
#define NO_CONTROL_NEGATIVE_VOLTAGE    1

struct cxd3778gf_platform_data{

	char * regulator_180;
	char * regulator_285;
	int port_i2s0_data_out;
	int port_i2s0_data_in;
	int port_i2s0_data_bck;
	int port_i2s0_data_lrck;
	int port_i2s1_data_out;
	int port_i2s1_data_in;
	int port_i2s1_data_bck;
	int port_i2s1_data_lrck;
	int port_ada_ldo_en;
	int port_ada_xint;
	int port_se_xshtd;
	int port_btl_xshtd;
	int port_ada_xdet;
	int port_ada_xrst;
	int port_hp_xmute;
	int port_au_vl_en;
	int port_au_avdd_en;
	int port_au_btl_5v_en;
	int port_au_btl_7v_en;
        int port_au_se_pos_en;
        int port_au_se_neg_en;
	int port_au_se_3v_en;
	int port_hp_btl_det_l;
	int port_hp_btl_det_r;
	int port_nc_cmpout;
	int port_nc_det;
	int port_hp_det;
	int port_hp_se_mute_on;
	int port_hp_se_mute_off;
	int port_hp_se_mute_cp_on;
	int port_hp_btl_mute_on;
	int port_hp_btl_mute_off;
	int port_hp_fm_series_xmute;
	int port_ada_fs480_en;;
	int port_hp_xmute4;
	int port_ada_fs441_en;
	int port_ext_ck1;
	int type1;
	int type2;
	unsigned int i2c_timing;
};

struct cxd3778gf_ext_platform_data {
	int (*hwinit)(void);
	void (*enable_bus)(int);
	void (*reset)(void);
	void (*force_disable)(void);
};

#define CXD3778GF_MIC_BIAS_MODE_OFF     0
#define CXD3778GF_MIC_BIAS_MODE_NORMAL  1
#define CXD3778GF_MIC_BIAS_MODE_MIC_DET 2
#define CXD3778GF_MIC_BIAS_MODE_NC_ON   3

/* CANVOL selection */
#define CANVOL0 0
#define CANVOL1 1

/* status struction */
struct cxd3778gf_status {
	int noise_cancel_mode;
	int noise_cancel_active;
	int noise_cancel_path;

	int sound_effect;

	int playback_mute;
	int capture_mute;
	int dsd_remastering_mute;
	int master_volume;
	int balance_volume_l;
	int balance_volume_r;
	int master_gain;

	int analog_playback_mute;
	int analog_stream_mute;
	int icx_playback_active;
	int std_playback_active;
	int capture_active;

	int mix_timed_mute;
	int std_timed_mute;
	int icx_timed_mute;
	int dsd_timed_mute;

	int uncg_mute;
	int fader_mute_sdin1;
	int fader_mute_sdin2;

	int output_device;
	int input_device;
	int headphone_amp;
	int headphone_smaster_se_gain_mode;
	int headphone_smaster_btl_gain_mode;
	int headphone_type;
	int jack_mode;
	int jack_status_se;
	int jack_status_btl;

	int pcm_monitoring;

	unsigned int sample_rate;
	unsigned int format;
	unsigned int osc;
	unsigned int board_type;
	unsigned int power_type;

	int icx_i2s_mode;
	int std_i2s_mode;

	int usb_dac_mode;
	int playback_latency;

	int dai_format;

	int headphone_detect_mode;
	int nc_gain;
	int ambient_gain;
	int nc_ignore_jack_state;
	int inactive_dnc_block;
};

struct cxd3778gf_dnc_interface {

	int (*initialize)(void);
	int (*shutdown)(void);
	int (*prepare)(void);
	int (*cleanup)(void);
	int (*judge)(struct cxd3778gf_status *status);
	int (*off)(struct cxd3778gf_status *status);
	int (*set_user_nc_gain)(int index, int path);
	int (*get_user_nc_gain)(int *index);
	int (*set_base_gain)(int left, int right);
	int (*get_base_gain)(int * left, int * right);
	int (*exit_base_gain_adjustment)(int save);
	int (*set_user_ambient_gain)(int index, int path);
	void (*mute_canvol)(void);
	void (*set_canvol)(struct cxd3778gf_status *status, int active);

	int (*set_mic_bias_mode)(int mode);
	void (*enable_sd2_clk)(int enable, int active);
	int (*switch_dnc_power)(int value);
	int (*switch_ncmic_amp_power)(int value);

	int (*modify_reg)(unsigned int address, unsigned int   value, unsigned int mask);
	int (*write_reg) (unsigned int address, unsigned int   value);
	int (*read_reg)  (unsigned int address, unsigned int * value);
	int (*write_buf) (unsigned int address, unsigned char * value, int size);
	int (*read_buf)  (unsigned int address, unsigned char * value, int size);

	int (*use_ext_bus)(bool enable);
	void (*ext_reset)(void);
	void (*ext_start_fmonitor)(void);
	void (*ext_stop_fmonitor)(void);
	void (*ext_set_gain_index)(int index);
	void (*ext_restore_preamp)(void);

	struct mutex * global_mutex;
};

int cxd3778gf_dnc_register_module(struct cxd3778gf_dnc_interface * interface);
int cxd3778gf_set_icx_hw_free(void);
#endif
