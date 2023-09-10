/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 */
/*
 * cxd3778gf_volume.h
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

#ifndef _CXD3778GF_VOLUME_HEADER_
#define _CXD3778GF_VOLUME_HEADER_

int cxd3778gf_volume_initialize(void);
int cxd3778gf_volume_finalize(void);

/* master volume */
int cxd3778gf_set_master_volume(struct cxd3778gf_status * now, int volume);

/* master gain */
int cxd3778gf_set_master_gain(struct cxd3778gf_status * now, int gain);

/* for ??? */
int cxd3778gf_set_playback_mute(struct cxd3778gf_status * now, int mute);

/* analog volume */
int cxd3778gf_set_phv_mute(struct cxd3778gf_status *now, int mute, int wait);

/* line */
int cxd3778gf_set_line_mute(struct cxd3778gf_status * now, int mute);

/* mic mute */
int cxd3778gf_set_capture_mute(struct cxd3778gf_status * now, int mute);

/* for application */
int cxd3778gf_set_analog_playback_mute(struct cxd3778gf_status * now, int mute);

/* for audio policy manager */
int cxd3778gf_set_analog_stream_mute(struct cxd3778gf_status * now, int mute);

/* for device switch */
int cxd3778gf_set_mix_timed_mute(struct cxd3778gf_status * now, int mute);

/* for STD sound effect */
int cxd3778gf_set_std_timed_mute(struct cxd3778gf_status * now, int mute);

/* for Hires sound effect */
int cxd3778gf_set_icx_timed_mute(struct cxd3778gf_status * now, int mute);

/* for DSD sound effect */
int cxd3778gf_set_dsd_timed_mute(struct cxd3778gf_status * now, int mute);

/* for no pcm */
int cxd3778gf_set_no_pcm_mute(int mute);

/* for uncg */
int cxd3778gf_set_uncg_mute(struct cxd3778gf_status * now, int mute);

/* for driver */
int cxd3778gf_set_output_device_mute(struct cxd3778gf_status * now, int mute, int pwm_wait);

/* for driver */
int cxd3778gf_set_input_device_mute(struct cxd3778gf_status * now, int mute);

/* for ICX fader mute */
int cxd3778gf_set_icx_fader_mute(struct cxd3778gf_status * now, int mute);

/* for STD fader mute */
int cxd3778gf_set_std_fader_mute(struct cxd3778gf_status * now, int mute);

/* for dsd-remastering */
int cxd3778gf_set_dsd_remastering_mute(struct cxd3778gf_status * now, int mute);
#endif

