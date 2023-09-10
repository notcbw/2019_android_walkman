/*
 * cxd3778gf_control.h
 *
 * CXD3778GF CODEC driver
 *
 * Copyright 2013, 2014, 2015, 2016, 2017, 2018 Sony Corporation
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

#ifndef _CXD3778GF_CONTROL_HEADER_
#define _CXD3778GF_CONTROL_HEADER_

int cxd3778gf_core_initialize(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_core_finalize(struct cxd3778gf_driver_data *ddata);

int cxd3778gf_suspend(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_resume(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_early_suspend(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_late_resume(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_put_standby(struct cxd3778gf_driver_data *ddata, int val);
int cxd3778gf_get_standby(int * val);
int cxd3778gf_put_deep_early_suspend(struct cxd3778gf_driver_data *ddata, int val);
int cxd3778gf_get_deep_early_suspend(int *val);

int cxd3778gf_startup(struct cxd3778gf_driver_data *ddata,
		      int icx_playback,
		      int std_playback,
		      int capture);
int cxd3778gf_shutdown(int icx_playback, int std_playback, int capture);

int cxd3778gf_set_icx_pcm_setting(void);
int cxd3778gf_set_icx_dsd_setting(void);

int cxd3778gf_set_icx_dai_fmt_setting(int format);

int cxd3778gf_set_board_type(unsigned int board_type, unsigned int power_type);
int cxd3778gf_get_board_type(unsigned int * board_type, unsigned int *power_type);

int cxd3778gf_set_icx_i2s_mode(int mode);
int cxd3778gf_set_std_i2s_mode(int mode);
int cxd3778gf_set_usb_dac_mode(int mode);

int cxd3778gf_set_icx_playback_dai_rate(unsigned int rate);
int cxd3778gf_set_std_playback_dai_rate(unsigned int rate);
int cxd3778gf_set_capture_dai_rate(unsigned int rate);

int cxd3778gf_set_icx_playback_dai_format(unsigned int format);
int cxd3778gf_set_std_playback_dai_format(unsigned int format);

int cxd3778gf_set_icx_playback_dai_free(void);
int cxd3778gf_set_std_playback_dai_free(void);

int cxd3778gf_set_icx_master_slave_change(void);
int cxd3778gf_set_std_master_slave_change(void);

int cxd3778gf_put_noise_cancel_mode(int value);
int cxd3778gf_get_noise_cancel_mode(int * value);

int cxd3778gf_get_noise_cancel_status(int * value);

int cxd3778gf_put_user_noise_cancel_gain(int index);
int cxd3778gf_get_user_noise_cancel_gain(int * index);

int cxd3778gf_put_base_noise_cancel_gain(int left, int right);
int cxd3778gf_get_base_noise_cancel_gain(int * left, int * right);

int cxd3778gf_exit_base_noise_cancel_gain_adjustment(int save);

int cxd3778gf_put_user_ambient_gain(int index);
int cxd3778gf_get_user_ambient_gain(int *index);

int cxd3778gf_put_nc_ignore_jack_state(int index);
int cxd3778gf_get_nc_ignore_jack_state(int *index);

int cxd3778gf_put_sound_effect(int value);
int cxd3778gf_get_sound_effect(int * value);



int cxd3778gf_put_master_volume(int value);
int cxd3778gf_get_master_volume(int * value);

int cxd3778gf_put_l_balance_volume(int value_l);
int cxd3778gf_get_l_balance_volume(int * value_l);

int cxd3778gf_put_r_balance_volume(int value_r);
int cxd3778gf_get_r_balance_volume(int * value_r);

int cxd3778gf_put_master_gain(int value);
int cxd3778gf_get_master_gain(int * value);

int cxd3778gf_put_playback_mute(int value);
int cxd3778gf_get_playback_mute(int * value);

int cxd3778gf_put_capture_mute(int value);
int cxd3778gf_get_capture_mute(int * value);

int cxd3778gf_put_analog_playback_mute(int value);
int cxd3778gf_get_analog_playback_mute(int * value);

int cxd3778gf_put_analog_stream_mute(int value);
int cxd3778gf_get_analog_stream_mute(int * value);

int cxd3778gf_put_timed_mute(int value);
int cxd3778gf_get_timed_mute(int * value);

int cxd3778gf_put_std_timed_mute(int value);
int cxd3778gf_get_std_timed_mute(int * value);

int cxd3778gf_put_icx_timed_mute(int value);
int cxd3778gf_get_icx_timed_mute(int * value);

int cxd3778gf_put_dsd_timed_mute(int value);
int cxd3778gf_get_dsd_timed_mute(int * value);

int cxd3778gf_put_std_fader_mute(int value);
int cxd3778gf_get_std_fader_mute(int * value);

int cxd3778gf_put_icx_fader_mute(int value);
int cxd3778gf_get_icx_fader_mute(int * value);

int cxd3778gf_put_dsd_remastering_mute(int value);
int cxd3778gf_get_dsd_remastering_mute(int * value);

int cxd3778gf_put_input_device(struct cxd3778gf_driver_data *ddata, int value);
int cxd3778gf_get_input_device(int * value);

int cxd3778gf_put_output_device(int value);
int cxd3778gf_get_output_device(int * value);

int cxd3778gf_put_headphone_amp(int value);
int cxd3778gf_get_headphone_amp(int * value);

int cxd3778gf_put_headphone_smaster_se_gain_mode(int value);
int cxd3778gf_get_headphone_smaster_se_gain_mode(int * value);

int cxd3778gf_put_headphone_smaster_btl_gain_mode(int value);
int cxd3778gf_get_headphone_smaster_btl_gain_mode(int * value);

int cxd3778gf_put_headphone_type(int value);
int cxd3778gf_get_headphone_type(int * value);

int cxd3778gf_put_jack_mode(int value);
int cxd3778gf_get_jack_mode(int * value);

int cxd3778gf_get_jack_status_se(int * value);
int cxd3778gf_get_jack_status_btl(int * value);

int cxd3778gf_register_dnc_module(struct cxd3778gf_dnc_interface * interface);
int cxd3778gf_check_jack_status_se(int force);
int cxd3778gf_check_jack_status_btl(int force);
int cxd3778gf_handle_pcm_event(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_handle_hp_det_event(struct cxd3778gf_driver_data *ddata);
int cxd3778gf_apply_table_change(int id);

int cxd3778gf_put_headphone_detect_mode(struct cxd3778gf_driver_data *ddata, int value);
int cxd3778gf_get_headphone_detect_mode(int * value);

int cxd3778gf_put_debug_test(int value);
int cxd3778gf_get_debug_test(int * value);

int cxd3778gf_put_playback_latency(int value);
int cxd3778gf_get_playback_latency(int *value);

int set_timed_mute(int port, int value);
int set_timed_mute_dsd_termination(void);

int cxd3778gf_set_icx_dsd_mode(int dsd);

#endif
