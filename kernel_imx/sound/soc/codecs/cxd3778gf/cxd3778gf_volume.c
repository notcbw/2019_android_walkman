/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 * Copyright 2019,2020 Sony Home Entertainment & Sound Products Inc.
 */
/*
 * cxd3778gf_volume.c
 *
 * CXD3778GF CODEC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/* #define TRACE_PRINT_ON */
/* #define DEBUG_PRINT_ON */
#define TRACE_TAG "------- "
#define DEBUG_TAG "        "

#include "cxd3778gf_common.h"

#define dgtvolint(_a) ( (_a)>=0x19 ? (int)(0xFFFFFF00|(_a)) : (int)(_a) )
#define anavolint(_a) ( (_a)>=0x40 ? (int)(0xFFFFFF80|(_a)) : (int)(_a) )
#define pgavolint(_a) ( (_a)>=0x20 ? (int)(0xFFFFFFC0|(_a)) : (int)(_a) )
#define adcvolint(_a) ( (_a)>=0x01 ? (int)(0xFFFFFF80|(_a)) : (int)(_a) )

static int fade_volume(unsigned int address, unsigned int volume,
					     unsigned int active);
static int fade_sms_dsd_volume(unsigned int address, int volume, int mask);
static int set_pwm_out_mute(int mute);
static int set_hardware_mute(int output_device, int headphone_amp, int mute);
static int get_table_index(struct cxd3778gf_status *status);
static void set_phv_volume_adjust(struct cxd3778gf_status *now, int effect, int table, int volume);

uint    cxd3778gf_monvol_wait_ms = 150;
module_param_named(monvol_wait_ms, cxd3778gf_monvol_wait_ms, uint, S_IWUSR | S_IRUGO);

int			cxd3778gf_fade_amount = 1;
static const int	FADE_AMOUNT_MAX = 20;
static const int	FADE_AMOUNT_MIN = 1;

#define FADE_AMOUNT_AMBIENT 10

static int set_param_fade_amount(const char *value, const struct kernel_param *kp)
{
	long			value_num;
	int			ret;

	ret = kstrtol(value, 0, &value_num);
	if (ret != 0) {
		return ret;
	}

	/* fade_amount: 1(min) - 20(max) */
	if (FADE_AMOUNT_MIN <= value_num && value_num <= FADE_AMOUNT_MAX) {
		cxd3778gf_fade_amount = value_num;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int get_param_fade_amount(char *value, const struct kernel_param *kp)
{
	return param_get_int(value, kp);
}

static const struct kernel_param_ops	param_ops_fade_amount = {
	.set = set_param_fade_amount,
	.get = get_param_fade_amount,
};

module_param_cb(fade_amount, &param_ops_fade_amount, &cxd3778gf_fade_amount, 0644);

#define OUTPUT_MUTE    0x00000001
#define INPUT_MUTE     0x00000002
#define PLAYBACK_MUTE  0x00000004
#define CAPTURE_MUTE   0x00000008
#define ANALOG_MUTE    0x00000010
#define STREAM_MUTE    0x00000020
#define NO_PCM_MUTE    0x00000040
#define TIMED_MUTE     0x00000080
#define STD_TIMED_MUTE 0x00000100
#define ICX_TIMED_MUTE 0x00000200
#define UNCG_MUTE      0x00000400
#define DSD_TIMED_MUTE 0x00000800

static unsigned int sdout_vol_mute = INPUT_MUTE;
static unsigned int sdin1_vol_mute = 0x00;
static unsigned int sdin2_vol_mute = 0x00;
static unsigned int lin_vol_mute   = INPUT_MUTE;
static unsigned int codec_play_vol_mute   = 0x00;
static unsigned int dgt_vol_mute   = 0x00;
static unsigned int dsdenc_vol_mute  = OUTPUT_MUTE;
static unsigned int hpout_vol_mute = OUTPUT_MUTE;
static unsigned int lout_vol_mute  = OUTPUT_MUTE;
static unsigned int pwm_out_mute   = OUTPUT_MUTE;
static unsigned int hw_mute        = OUTPUT_MUTE;

static int initialized=FALSE;

int cxd3778gf_volume_initialize(void)
{
	initialized=TRUE;

	return(0);
}

int cxd3778gf_volume_finalize(void)
{
	if(!initialized)
		return(0);

	initialized=FALSE;

	return(0);
}

/* msater volume */
int cxd3778gf_set_master_volume(struct cxd3778gf_status * now, int volume)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,volume);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(now->format == PCM_MODE) {
		if (dgt_vol_mute)
			cxd3778gf_dnc_mute_canvol();
		else
			cxd3778gf_dnc_set_canvol(now);
		if(lin_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_AINVOL, 0x33);

		if(sdin1_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_SDIN1VOL, 0x33);
		else
			cxd3778gf_register_write(CXD3778GF_CODEC_SDIN1VOL, cxd3778gf_master_volume_table[effect][table][volume].sdin1);

		if(sdin2_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_SDIN2VOL, 0x33);
		else
			cxd3778gf_register_write(CXD3778GF_CODEC_SDIN2VOL, cxd3778gf_master_volume_table[effect][table][volume].sdin2);

		if(codec_play_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, 0x33);
		else
			cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, cxd3778gf_master_volume_table[effect][table][volume].play);
	} else {
		if (!dsdenc_vol_mute)
			md6000_fader(cxd3778gf_master_volume_dsd_table[table][volume]);
	}

	if(hpout_vol_mute){
		cxd3778gf_register_write(CXD3778GF_PHV_L, 0x00);
		cxd3778gf_register_write(CXD3778GF_PHV_R, 0x00);
	} else {
		set_phv_volume_adjust(now, effect, table, volume);
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL3, cxd3778gf_master_volume_table[effect][table][volume].hpout3_ctrl3, 0x03);
	}

	if(lout_vol_mute)
		cxd3778gf_register_write(CXD3778GF_LINEOUT_VOL, 0x00);

	return(0);
}

/* master gain */
int cxd3778gf_set_master_gain(struct cxd3778gf_status * now, int gain)
{
	print_trace("%s(%d)\n",__FUNCTION__,gain);

	if (AMBIENT != now->noise_cancel_active) {
		if (sdout_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_RECVOL, 0x33);
		else
			cxd3778gf_register_write(CXD3778GF_CODEC_RECVOL, 0x00);
	}

	return(0);
}

/* for ??? */
int cxd3778gf_set_playback_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		codec_play_vol_mute = codec_play_vol_mute |  PLAYBACK_MUTE;
	}
	else{
		codec_play_vol_mute = codec_play_vol_mute & ~PLAYBACK_MUTE;
	}

	if(codec_play_vol_mute)
		cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, 0x33);
	else
		cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, cxd3778gf_master_volume_table[effect][table][now->master_volume].play);

	return(0);
}

int cxd3778gf_set_phv_mute(struct cxd3778gf_status *now, int mute, int wait)
{
        int effect;
        int table;

        print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

        if(mute){
		hpout_vol_mute = hpout_vol_mute | PLAYBACK_MUTE;
        }
        else{
                hpout_vol_mute = hpout_vol_mute & ~PLAYBACK_MUTE;
        }

        if(hpout_vol_mute){
		cxd3778gf_register_write(CXD3778GF_PHV_L, 0x00);
		cxd3778gf_register_write(CXD3778GF_PHV_R, 0x00);
		if (wait)
			msleep(20);
	}
        else{
		set_phv_volume_adjust(now, effect, table, now->master_volume);
		if (wait)
			msleep(20);
	}

	return(0);
}

int cxd3778gf_set_line_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		lout_vol_mute = lout_vol_mute | PLAYBACK_MUTE;
	}
	else{
		lout_vol_mute = lout_vol_mute & ~PLAYBACK_MUTE;
	}

	if(lout_vol_mute)
		cxd3778gf_register_write(CXD3778GF_LINEOUT_VOL, 0x00);

	return(0);
}

/* mic mute */
int cxd3778gf_set_capture_mute(struct cxd3778gf_status * now, int mute)
{
	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(mute){
		sdout_vol_mute = sdout_vol_mute |  INPUT_MUTE;
	}
	else{
		sdout_vol_mute = sdout_vol_mute & ~INPUT_MUTE;
	}

	if (AMBIENT != now->noise_cancel_active) {
		if (sdout_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_RECVOL, 0x33);
		else
			cxd3778gf_register_write(CXD3778GF_CODEC_RECVOL, 0x00);
	}

	return(0);
}

/* for application */
int cxd3778gf_set_analog_playback_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		lin_vol_mute = lin_vol_mute |  ANALOG_MUTE;
	}
	else{
		lin_vol_mute = lin_vol_mute & ~ANALOG_MUTE;
	}

	if(lin_vol_mute)
		fade_volume(CXD3778GF_CODEC_AINVOL, 0x33,
				now->noise_cancel_active);

	return(0);
}

/* for audio policy manager */
int cxd3778gf_set_analog_stream_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		lin_vol_mute = lin_vol_mute |  STREAM_MUTE;
	}
	else{
		lin_vol_mute = lin_vol_mute & ~STREAM_MUTE;
	}

	if(lin_vol_mute)
		cxd3778gf_register_write(CXD3778GF_CODEC_AINVOL, 0x33);

	return(0);
}

/* for device switch */
int cxd3778gf_set_mix_timed_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		codec_play_vol_mute = codec_play_vol_mute |  TIMED_MUTE;
	}
	else{
		codec_play_vol_mute = codec_play_vol_mute & ~TIMED_MUTE;
	}

	if(codec_play_vol_mute)
		cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, 0x33);
	else
		cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, cxd3778gf_master_volume_table[effect][table][now->master_volume].play);

	return(0);
}

/* for STD sound effect */
int cxd3778gf_set_std_timed_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		sdin2_vol_mute = sdin2_vol_mute |  STD_TIMED_MUTE;
	}
	else{
		sdin2_vol_mute = sdin2_vol_mute & ~STD_TIMED_MUTE;
	}

	if(sdin2_vol_mute)
		cxd3778gf_register_write(CXD3778GF_CODEC_SDIN2VOL, 0x33);
	else
		cxd3778gf_register_write(CXD3778GF_CODEC_SDIN2VOL, cxd3778gf_master_volume_table[effect][table][now->master_volume].sdin2);

	return(0);
}

/* for ICX sound effect */
int cxd3778gf_set_icx_timed_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		sdin1_vol_mute = sdin1_vol_mute |  ICX_TIMED_MUTE;
	}
	else{
		sdin1_vol_mute = sdin1_vol_mute & ~ICX_TIMED_MUTE;
	}

	if(sdin1_vol_mute)
		cxd3778gf_register_write(CXD3778GF_CODEC_SDIN1VOL, 0x33);
	else
		cxd3778gf_register_write(CXD3778GF_CODEC_SDIN1VOL, cxd3778gf_master_volume_table[effect][table][now->master_volume].sdin1);

	return(0);
}

/* for ICX fader mute */
int cxd3778gf_set_icx_fader_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;
	const int MUTE_VOL = 0x33;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		sdin1_vol_mute = sdin1_vol_mute |  PLAYBACK_MUTE;
	}
	else{
		sdin1_vol_mute = sdin1_vol_mute & ~PLAYBACK_MUTE;
	}

	if(sdin1_vol_mute)
		fade_volume(CXD3778GF_CODEC_SDIN1VOL, MUTE_VOL,
				      now->noise_cancel_active);
	else
		fade_volume(CXD3778GF_CODEC_SDIN1VOL,
			    cxd3778gf_master_volume_table[effect][table][now->master_volume].sdin1,
			    now->noise_cancel_active);

	return(0);
}

/* for STD fader mute */
int cxd3778gf_set_std_fader_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;
	const int MUTE_VOL = 0x33;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		sdin2_vol_mute = sdin2_vol_mute |  PLAYBACK_MUTE;
	}
	else{
		sdin2_vol_mute = sdin2_vol_mute & ~PLAYBACK_MUTE;
	}

	if(sdin2_vol_mute)
		fade_volume(CXD3778GF_CODEC_SDIN2VOL, MUTE_VOL,
				      now->noise_cancel_active);
	else
		fade_volume(CXD3778GF_CODEC_SDIN2VOL,
			    cxd3778gf_master_volume_table[effect][table][now->master_volume].sdin2,
			    now->noise_cancel_active);

	return(0);
}

/* for dsd-remastering */
int cxd3778gf_set_dsd_timed_mute(struct cxd3778gf_status * now, int mute)
{
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(mute){
		dsdenc_vol_mute  = dsdenc_vol_mute  |  DSD_TIMED_MUTE;
	}
	else{
		dsdenc_vol_mute  = dsdenc_vol_mute  &  ~DSD_TIMED_MUTE;
	}

	if(now->format == DSD_MODE) {
		table = get_table_index(now);

		if(dsdenc_vol_mute){
			msleep(20);
			fade_sms_dsd_volume(CXD3778GF_SMS_DSD_CTRL,0x00,0x0F);
			cxd3778gf_register_write(CXD3778GF_SMS_DSD_PMUTE, 0x80);
		}
		else{
			if(now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL)
				cxd3778gf_set_phv_mute(now, TRUE, TRUE);
			cxd3778gf_register_write(CXD3778GF_SMS_DSD_PMUTE, 0x00);
			if (now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL){
				cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x0F,0x0F);
				cxd3778gf_set_phv_mute(now, FALSE, TRUE);
			}
			else{
				fade_sms_dsd_volume(CXD3778GF_SMS_DSD_CTRL,0x0F,0x0F);
				msleep(20);
			}
		}
	}
	return(0);
}

/* for no pcm */
int cxd3778gf_set_no_pcm_mute(int mute)
{
	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(mute){
		pwm_out_mute = pwm_out_mute |  NO_PCM_MUTE;
	}
	else{
		pwm_out_mute = pwm_out_mute & ~NO_PCM_MUTE;
	}

	if(pwm_out_mute)
		set_pwm_out_mute(ON);
	else
		set_pwm_out_mute(OFF);

	return(0);
}

int cxd3778gf_set_uncg_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		dgt_vol_mute = dgt_vol_mute |  UNCG_MUTE;
	}
	else{
		dgt_vol_mute = dgt_vol_mute & ~UNCG_MUTE;
	}

	if (dgt_vol_mute) {
		cxd3778gf_dnc_mute_canvol();
		if (now->noise_cancel_active)
			msleep(cxd3778gf_monvol_wait_ms);
	} else {
		cxd3778gf_dnc_set_canvol(now);
		if (now->noise_cancel_active)
			msleep(cxd3778gf_monvol_wait_ms);
	}

	return(0);
}

/* for driver */
int cxd3778gf_set_output_device_mute(struct cxd3778gf_status *now,
				     int mute,
				     int pwm_wait)
{
	int effect;
	int table;
	unsigned int timeout_tmp = 0x7F;

	print_trace("%s(%d,%d)\n", __FUNCTION__, mute, pwm_wait);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect = 1;
	else
		effect = 0;

	if (mute) {
		dgt_vol_mute |= OUTPUT_MUTE;
		codec_play_vol_mute |= OUTPUT_MUTE;
		hpout_vol_mute |= OUTPUT_MUTE;
		lout_vol_mute |= OUTPUT_MUTE;
		dsdenc_vol_mute |= OUTPUT_MUTE;
		pwm_out_mute |= OUTPUT_MUTE;
		hw_mute |= OUTPUT_MUTE;
	} else {
		dgt_vol_mute &= ~OUTPUT_MUTE;
		codec_play_vol_mute &= ~OUTPUT_MUTE;
		dsdenc_vol_mute &= ~OUTPUT_MUTE;
		hpout_vol_mute &= ~OUTPUT_MUTE;
		lout_vol_mute &= ~OUTPUT_MUTE;
		pwm_out_mute &= ~OUTPUT_MUTE;
		hw_mute &= ~OUTPUT_MUTE;
	}

	/* Change codec playvol's fader timeout value for lineout vol mute */
	if (lout_vol_mute && now->output_device == OUTPUT_DEVICE_LINE) {
		cxd3778gf_register_read(CXD3778GF_CODEC_SR_PLAY3, &timeout_tmp);
		cxd3778gf_register_write(CXD3778GF_CODEC_SR_PLAY3, 0x03);
	}

	if (dgt_vol_mute && now->format == PCM_MODE)
		cxd3778gf_dnc_mute_canvol();

	if (hw_mute)
		set_hardware_mute(now->output_device,now->headphone_amp,ON);

	if (pwm_out_mute)
		set_pwm_out_mute(ON);

	if (codec_play_vol_mute && now->format == PCM_MODE)
		cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL, 0x33);

	if (hpout_vol_mute) {
		/*
		 * Fader off in case of BTL for low latency.
		 * BTL PHV vol changing pop noise is very small.
		 * So, can disbale fader function.
		 */
		if (now->board_type == TYPE_Z &&
		    now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL &&
		    now->playback_latency == PLAYBACK_LATENCY_LOW)
			cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0,
						  0x00,
						  0x80);

		cxd3778gf_register_write(CXD3778GF_PHV_L, 0x00);
		cxd3778gf_register_write(CXD3778GF_PHV_R, 0x00);
		msleep(20);

		if (now->board_type == TYPE_Z &&
		    now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL &&
		    now->playback_latency == PLAYBACK_LATENCY_LOW)
			cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0,
						  0x80,
						  0x80);
	}

	if (lout_vol_mute) {
		/*
		 * Lineout doesn't connect hw_mute and doesn't have soft_ramp.
		 * So, need to wait to finish preceding soft_ramp vol
		 * (CODEC_PLAYVOL) and should set temporarily faster timeout
		 * value for avoiding discontinuity.
		 */
		if (now->output_device == OUTPUT_DEVICE_LINE) {
			msleep(30);
			cxd3778gf_register_write(CXD3778GF_LINEOUT_VOL, 0x00);
			cxd3778gf_register_write(CXD3778GF_CODEC_SR_PLAY3,
						 timeout_tmp);
		} else {
			cxd3778gf_register_write(CXD3778GF_LINEOUT_VOL, 0x00);
		}
	}

	if (!hpout_vol_mute)
		set_phv_volume_adjust(now, effect, table, now->master_volume);

	if (!codec_play_vol_mute && now->format == PCM_MODE)
		cxd3778gf_register_write(CXD3778GF_CODEC_PLAYVOL,
			cxd3778gf_master_volume_table[effect][table][now->master_volume].play);

	if (!pwm_out_mute)
		set_pwm_out_mute(OFF);

	if (!hw_mute) {
		msleep(50);
		set_hardware_mute(now->output_device, now->headphone_amp, OFF);
	}

	if (!dgt_vol_mute && now->format == PCM_MODE)
		cxd3778gf_dnc_set_canvol(now);

	return 0;
}

/* for driver */
int cxd3778gf_set_input_device_mute(struct cxd3778gf_status * now, int mute)
{
	int effect;
	int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	table = get_table_index(now);

	if (now->sound_effect && now->sample_rate <= 192000)
		effect=1;
	else
		effect=0;

	if(mute){
		lin_vol_mute   = lin_vol_mute   |  INPUT_MUTE;
		sdout_vol_mute = sdout_vol_mute |  INPUT_MUTE;
	}
	else{
		lin_vol_mute   = lin_vol_mute   & ~INPUT_MUTE;
		sdout_vol_mute = sdout_vol_mute & ~INPUT_MUTE;
	}

	if(lin_vol_mute)
		cxd3778gf_register_write(CXD3778GF_CODEC_AINVOL, 0x33);

	if (AMBIENT != now->noise_cancel_active) {
		if (sdout_vol_mute)
			cxd3778gf_register_write(CXD3778GF_CODEC_RECVOL, 0x33);
		else
			cxd3778gf_register_write(CXD3778GF_CODEC_RECVOL,
				 cxd3778gf_master_gain_table[now->master_gain]);
	}

	return(0);
}

/*@fade func */
static int fade_volume(unsigned int address, unsigned int volume,
					     unsigned int active)
{
	unsigned int tmp;
	int req;
	int now;
	int amount;
	int rv;

	/* cxd3778gf_register_write(address,volume); */

	req=dgtvolint(volume);

	rv=cxd3778gf_register_read(address,&tmp);
	if(rv<0)
		tmp=0x33; /* for safe */

	/*
	 * To reduce the influence of delay in each i2c access,
	 * fix amount to 10 if ambient feature is enabled.
	 */
	if (AMBIENT == active)
		amount = FADE_AMOUNT_AMBIENT;
	else
		amount = cxd3778gf_fade_amount;

	now=dgtvolint(tmp);

	if(now<req){ // mute off
		while(now!=req){
			now = minimum(now + amount, req);
			cxd3778gf_register_write(address,now);
			usleep_range(100,120);
		}
	}
	else if(now>req){ // mute on
		while(now!=req){
			now = maximum(now - amount, req);
			cxd3778gf_register_write(address,now);
			usleep_range(100,120);
		}
	}

	return(0);
}

static int fade_sms_dsd_volume(unsigned int address, int volume, int mask)
{
	int now;
	int req;
	int rv;
	unsigned int tmp;

	rv=cxd3778gf_register_read(address,&tmp);
	if(rv<0)
		tmp=0x00; /* for safe */

	now=(tmp & mask);

	if(now<volume){
		while(now!=volume){
			/* now++; */
			now=minimum(now+3,volume);
			cxd3778gf_register_modify(address,now,mask);
			usleep_range(1000,1200);
                }
        }
	else if(now>volume){
		while(now!=volume){
			/* now--; */
			now=maximum(now-3,volume);
			cxd3778gf_register_modify(address,now,mask);
			usleep_range(1000,1200);
		}
	}

        return(0);
}

static int set_pwm_out_mute(int mute)
{
	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(mute){
		print_debug("pwm out mute = ON\n");
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
	}
	else{
		print_debug("pwm out mute = OFF\n");
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x00,0x80);
	}

	return(0);
}

static int set_hardware_mute(int output_device, int headphone_amp, int mute)
{
	print_trace("%s(%d,%d,%d)\n",
		    __FUNCTION__, output_device, headphone_amp, mute);

	if (mute) {
		cxd3778gf_switch_smaster_mute(ON, headphone_amp);

		msleep(30);
	} else {
		if (output_device == OUTPUT_DEVICE_HEADPHONE)
			cxd3778gf_switch_smaster_mute(OFF, headphone_amp);
		else
			cxd3778gf_switch_smaster_mute(ON, headphone_amp);

		msleep(30);
	}

	return(0);
}

/* for dsd-remastering */
int cxd3778gf_set_dsd_remastering_mute(struct cxd3778gf_status * now, int mute)
{
        int table;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(mute){
		dsdenc_vol_mute  = dsdenc_vol_mute  |  PLAYBACK_MUTE;
	}
	else{
		dsdenc_vol_mute  = dsdenc_vol_mute  &  ~PLAYBACK_MUTE;
	}

	if(now->format == DSD_MODE) {
		table = get_table_index(now);

		if(dsdenc_vol_mute){
			if(now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL){
				cxd3778gf_set_phv_mute(now, TRUE, TRUE);
				cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x00,0x0F);
				cxd3778gf_set_phv_mute(now, FALSE, TRUE);
			} else {
				fade_sms_dsd_volume(CXD3778GF_SMS_DSD_CTRL,0x00,0x0F);
			}
			cxd3778gf_register_write(CXD3778GF_SMS_DSD_PMUTE,0x80);
		}
		else{
			if(now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL)
				cxd3778gf_set_phv_mute(now, TRUE, TRUE);
				cxd3778gf_register_write(CXD3778GF_SMS_DSD_PMUTE,0x00);
				md6000_fader(cxd3778gf_master_volume_dsd_table[table][now->master_volume]);
			if (now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL){
				cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x0F,0x0F);
				cxd3778gf_set_phv_mute(now, FALSE, TRUE);
			}
			else{
				fade_sms_dsd_volume(CXD3778GF_SMS_DSD_CTRL,0x0F,0x0F);
				//msleep(20); /* no need */
			}
		}
	}
        return(0);
}

static int get_table_index(struct cxd3778gf_status *status)
{
	int table;

	if (status->format == PCM_MODE) {
		switch (status->output_device) {
		case OUTPUT_DEVICE_HEADPHONE:
			if (status->headphone_amp == HEADPHONE_AMP_SMASTER_SE) {
				switch (status->noise_cancel_active) {
				case NC_ACTIVE:
					table = MASTER_VOLUME_TABLE_SE_PCM_NC;
					break;
				case AMBIENT:
					table = MASTER_VOLUME_TABLE_SE_PCM_AM;
					break;
				case NC_NON_ACTIVE:
				default:
					if (status->headphone_smaster_se_gain_mode == HEADPHONE_SMASTER_SE_GAIN_MODE_NORMAL
							&& status->board_type == TYPE_Z)
						table = MASTER_VOLUME_TABLE_SE_PCM_LG;
					else
						table = MASTER_VOLUME_TABLE_SE_PCM_HG;
					break;
				}
			} else if (status->headphone_amp == HEADPHONE_AMP_SMASTER_BTL) {
				if (status->headphone_smaster_btl_gain_mode == HEADPHONE_SMASTER_BTL_GAIN_MODE_HIGH)
					table = MASTER_VOLUME_TABLE_BTL_PCM_HG;
				else
					table = MASTER_VOLUME_TABLE_BTL_PCM_LG;
			} else {
				table = MASTER_VOLUME_TABLE_OFF;
			}
			break;
		case OUTPUT_DEVICE_NONE:
		default:
			table = MASTER_VOLUME_TABLE_OFF;
			break;
		}
	} else {
		switch (status->output_device) {
		case OUTPUT_DEVICE_HEADPHONE:
			if (status->headphone_amp == HEADPHONE_AMP_SMASTER_BTL) {
				if (status->headphone_smaster_btl_gain_mode == HEADPHONE_SMASTER_BTL_GAIN_MODE_HIGH) {
					if (status->sample_rate == 88200)
						table = MASTER_VOLUME_TABLE_BTL_DSD64_HG;
					else if (status->sample_rate == 176400)
						table = MASTER_VOLUME_TABLE_BTL_DSD128_HG;
					else
						table = MASTER_VOLUME_TABLE_BTL_DSD256_HG;
				} else {
					if (status->sample_rate == 88200)
						table = MASTER_VOLUME_TABLE_BTL_DSD64_LG;
					else if (status->sample_rate == 176400)
						table = MASTER_VOLUME_TABLE_BTL_DSD128_LG;
					else
						table = MASTER_VOLUME_TABLE_BTL_DSD256_LG;
				}
			} else {
				table = MASTER_VOLUME_TABLE_OFF;
			}
			break;
		case OUTPUT_DEVICE_NONE:
		default:
			table = MASTER_VOLUME_TABLE_OFF;
			break;
		}
	}

	return table;
}

static void set_phv_volume_adjust(struct cxd3778gf_status *now, int effect, int table, int volume)
{
	if (now->format != DSD_MODE) {
		if (now->headphone_amp == HEADPHONE_AMP_NORMAL) {
			if (cxd3778gf_master_volume_table[effect][table][volume].hpout - now->balance_volume_l < 0x00)
				cxd3778gf_register_write(CXD3778GF_PHV_L, 0x00);
			else
				cxd3778gf_register_write(CXD3778GF_PHV_L, cxd3778gf_master_volume_table[effect][table][volume].hpout - now->balance_volume_l);
			if (cxd3778gf_master_volume_table[effect][table][volume].hpout - now->balance_volume_r < 0x00)
				cxd3778gf_register_write(CXD3778GF_PHV_R, 0x00);
			else
				cxd3778gf_register_write(CXD3778GF_PHV_R, cxd3778gf_master_volume_table[effect][table][volume].hpout - now->balance_volume_r);
		} else if (((now->headphone_amp == HEADPHONE_AMP_SMASTER_SE) && (board_set_flag & OUTPUT_SE_LR_REVERSE_FLAG)) ||
				((now->headphone_amp == HEADPHONE_AMP_SMASTER_BTL) && (board_set_flag & OUTPUT_BTL_LR_REVERSE_FLAG))) {
			if (cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_l * 4) < 0x00)
				cxd3778gf_register_write(CXD3778GF_PHV_R, 0x00);
			else
				cxd3778gf_register_write(CXD3778GF_PHV_R, cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_l * 4));
			if (cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_r * 4) < 0x00)
				cxd3778gf_register_write(CXD3778GF_PHV_L, 0x00);
			else
				cxd3778gf_register_write(CXD3778GF_PHV_L, cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_r * 4));
		} else {
			if (cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_l * 4) < 0x00)
				cxd3778gf_register_write(CXD3778GF_PHV_L, 0x00);
			else
				cxd3778gf_register_write(CXD3778GF_PHV_L, cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_l * 4));
			if (cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_r * 4) < 0x00)
				cxd3778gf_register_write(CXD3778GF_PHV_R, 0x00);
			else
				cxd3778gf_register_write(CXD3778GF_PHV_R, cxd3778gf_master_volume_table[effect][table][volume].hpout - (now->balance_volume_r * 4));
		}
	} else {
		cxd3778gf_register_write(CXD3778GF_PHV_L, cxd3778gf_master_volume_table[effect][table][volume].hpout);
		cxd3778gf_register_write(CXD3778GF_PHV_R, cxd3778gf_master_volume_table[effect][table][volume].hpout);
	}

	return;
}
