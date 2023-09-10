/*
 * cxd3778gf.c
 *
 * CXD3778GF CODEC driver
 *
 * Copyright 2013,2014,2015,2016,2017,2018,2019 Sony Video & Sound Products Inc.
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

/* #define TRACE_PRINT_ON */
/* #define DEBUG_PRINT_ON */
#define TRACE_TAG "####### "
#define DEBUG_TAG "        "

#include "cxd3778gf_common.h"

static const int TIMED_MUTE_MAX = 500;
static const int TIMED_MUTE_MIN = 50;
int cxd3778gf_timed_mute_ms[MUTE_ID_MAX] = {80, 80, 80, 80};

static int set_timed_mute_ms_from_sysfs(const char *value,
		const struct kernel_param *kp, const enum MUTE_ID mute_id)
{
	long value_num;
	int ret;

	value += 4; /* format: [064|128|256],<value> */
	ret = kstrtol(value, 0, &value_num);
	if (0 != ret)
		return ret;

	if (value_num < TIMED_MUTE_MIN ||
			TIMED_MUTE_MAX < value_num) {
		pr_info("%s: %ld is out of range(%d-%d[ms])\n", __func__,
				value_num, TIMED_MUTE_MIN, TIMED_MUTE_MAX);
		return -EINVAL;
	}

	if (MUTE_ID_DEFAULT <= mute_id &&
			mute_id < MUTE_ID_MAX) {
		cxd3778gf_timed_mute_ms[mute_id] = value_num;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int set_param_timed_mute_ms(const char *value,
		const struct kernel_param *kp)
{
	enum MUTE_ID id;

	if (!strncmp(value, "064,", 4))
		id = MUTE_ID_DSD064;
	else if (!strncmp(value, "128,", 4))
		id = MUTE_ID_DSD128;
	else if (!strncmp(value, "256,", 4))
		id = MUTE_ID_DSD256;
	else
		return -EINVAL;

	return set_timed_mute_ms_from_sysfs(value, kp, id);
}

static inline char *enum_to_str(const enum MUTE_ID id)
{
	switch (id) {
	case MUTE_ID_DEFAULT:
		return "DEFAULT";
	case MUTE_ID_DSD064:
		return "DSD064";
	case MUTE_ID_DSD128:
		return "DSD128";
	case MUTE_ID_DSD256:
		return "DSD256";
	default:
		return "none";
	}
}

static int get_param_timed_mute_ms(char *value, const struct kernel_param *kp)
{
	enum MUTE_ID id;

	for (id = MUTE_ID_DEFAULT; id < MUTE_ID_MAX; ++id) {
		pr_info("%s: %s=%d[ms]\n", __func__, enum_to_str(id),
				cxd3778gf_timed_mute_ms[id]);
	}

	return 0;
}

static const struct kernel_param_ops param_ops_timed_mute_ms = {
	.set = set_param_timed_mute_ms,
	.get = get_param_timed_mute_ms,
};

module_param_cb(timed_mute_ms, &param_ops_timed_mute_ms,
		&cxd3778gf_timed_mute_ms[MUTE_ID_DEFAULT], 0644);

/**************/
/*@ prototype */
/**************/

/* entry_routines */

static int  __init cxd3778gf_init(void);
static void __exit cxd3778gf_exit(void);

static int cxd3778gf_i2c_probe(
	      struct i2c_client    * client,
	const struct i2c_device_id * identify
);
static int cxd3778gf_i2c_remove(
	struct i2c_client * client
);
static void cxd3778gf_i2c_poweroff(
	struct i2c_client * client
);

static int cxd3778gf_codec_probe (struct snd_soc_codec * codec);
static int cxd3778gf_codec_remove(struct snd_soc_codec * codec);

/* power_management_routines */

static int cxd3778gf_i2c_suspend(struct device * device);
static int cxd3778gf_i2c_resume(struct device * device);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cxd3778gf_i2c_early_suspend(struct early_suspend *h);
static void cxd3778gf_i2c_late_resume(struct early_suspend *h);
#endif

/* dai_ops */

static int cxd3778gf_icx_dai_startup(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
);
static int cxd3778gf_std_dai_startup(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
);

static int cxd3778gf_dac_dai_startup(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai       *dai
);

static void cxd3778gf_icx_dai_shutdown(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
);
static void cxd3778gf_std_dai_shutdown(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
);

static void cxd3778gf_dac_dai_shutdown(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai       *dai
);

static int cxd3778gf_icx_dai_set_fmt(
	struct snd_soc_dai * codec_dai,
	unsigned int         format
);
static int cxd3778gf_std_dai_set_fmt(
	struct snd_soc_dai * codec_dai,
	unsigned int         format
);

static int cxd3778gf_icx_dai_hw_params(
	struct snd_pcm_substream * substream,
	struct snd_pcm_hw_params * params,
	struct snd_soc_dai       * dai
);
static int cxd3778gf_std_dai_hw_params(
	struct snd_pcm_substream * substream,
	struct snd_pcm_hw_params * params,
	struct snd_soc_dai       * dai
);

static int cxd3778gf_icx_dai_prepare(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai * dai
);

static int cxd3778gf_std_dai_prepare(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai * dai
);

static int cxd3778gf_icx_dai_hw_free(
        struct snd_pcm_substream * substream,
        struct snd_soc_dai       * dai
);

static int cxd3778gf_std_dai_hw_free(
        struct snd_pcm_substream * substream,
        struct snd_soc_dai       * dai
);

static int cxd3778gf_icx_dai_mute(
	struct snd_soc_dai * dai,
	int                  mute
);
static int cxd3778gf_std_dai_mute(
	struct snd_soc_dai * dai,
	int                  mute
);

static int cxd3778gf_icx_dai_trigger(
	struct snd_pcm_substream * substream,
	int cmd,
	struct snd_soc_dai       * dai
);

/* noise_cancel */

static int cxd3778gf_put_noise_cancel_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_noise_cancel_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_noise_cancel_status_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_noise_cancel_status_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_user_noise_cancel_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_user_noise_cancel_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_base_noise_cancel_gain_left_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_base_noise_cancel_gain_left_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_base_noise_cancel_gain_right_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_base_noise_cancel_gain_right_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_base_noise_cancel_gain_save_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_base_noise_cancel_gain_save_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_user_ambient_gain_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
static int cxd3778gf_get_user_ambient_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol);
static int cxd3778gf_put_nc_ignore_jack_state_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
static int cxd3778gf_get_nc_ignore_jack_state_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol);

/* sound_effect */

static int cxd3778gf_put_sound_effect_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_sound_effect_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);

/* volume_and_mute */

static int cxd3778gf_put_master_volume_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_master_volume_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_l_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_l_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_r_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_r_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_master_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_master_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_capture_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_capture_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_analog_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_analog_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_analog_stream_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_analog_stream_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_std_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_std_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_icx_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_icx_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_dsd_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_dsd_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_std_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_std_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_icx_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_icx_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_icx_dsd_remastering_mute_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_icx_dsd_remastering_mute_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);

/* device_control */

static int cxd3778gf_put_output_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_output_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_input_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_input_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_headphone_amp_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_headphone_amp_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_headphone_smaster_se_gain_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_headphone_smaster_se_gain_mode_control(
	struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_headphone_smaster_btl_gain_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_headphone_smaster_btl_gain_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_headphone_type_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_headphone_type_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_jack_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_jack_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_jack_status_se_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_jack_status_se_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_jack_status_btl_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_jack_status_btl_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_standby_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_standby_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_put_deep_early_suspend_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_deep_early_suspend_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);

static int cxd3778gf_put_playback_latency_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol
);

static int cxd3778gf_get_playback_latency_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol
);

/* debug_test */

static int cxd3778gf_put_headphone_detect_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_headphone_detect_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
);

static int cxd3778gf_put_debug_test_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);
static int cxd3778gf_get_debug_test_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
);

/**************/
/*@ variables */
/**************/

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("CXD3778GF CODEC driver");
MODULE_LICENSE("GPL");

module_init(cxd3778gf_init);
module_exit(cxd3778gf_exit);

static const struct i2c_device_id cxd3778gf_id[] = {
	{CXD3778GF_DEVICE_NAME, 0},
	{}
};

static const struct of_device_id cxd3778gf_i2c_dt_ids[] = {
        { .compatible = "sony,cxd3778gf"},
        { }
};

MODULE_DEVICE_TABLE(of, cxd3778gf_i2c_dt_ids);

static struct dev_pm_ops cxd3778gf_pm_ops = {
	.suspend  = cxd3778gf_i2c_suspend,
	.resume   = cxd3778gf_i2c_resume,
};

static struct i2c_driver cxd3778gf_i2c_driver = {
	.driver   = {
		.name  = CXD3778GF_DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cxd3778gf_i2c_dt_ids),
		.pm    = &cxd3778gf_pm_ops,
	},
	.id_table = cxd3778gf_id,
	.probe    = cxd3778gf_i2c_probe,
	.remove   = cxd3778gf_i2c_remove,
	.shutdown = cxd3778gf_i2c_poweroff,
};

static struct snd_soc_codec_driver cxd3778gf_codec_driver = {
	.probe   = cxd3778gf_codec_probe,
	.remove  = cxd3778gf_codec_remove,
};

static struct snd_soc_dai_ops cxd3778gf_icx_dai_ops = {
	.startup      = cxd3778gf_icx_dai_startup,
	.shutdown     = cxd3778gf_icx_dai_shutdown,
	.set_fmt      = cxd3778gf_icx_dai_set_fmt,
	.hw_params    = cxd3778gf_icx_dai_hw_params,
	.prepare      = cxd3778gf_icx_dai_prepare,
	.hw_free      = cxd3778gf_icx_dai_hw_free,
	.digital_mute = cxd3778gf_icx_dai_mute,
};
static struct snd_soc_dai_ops cxd3778gf_std_dai_ops = {
	.startup      = cxd3778gf_std_dai_startup,
	.shutdown     = cxd3778gf_std_dai_shutdown,
	.set_fmt      = cxd3778gf_std_dai_set_fmt,
	.hw_params    = cxd3778gf_std_dai_hw_params,
	.prepare      = cxd3778gf_std_dai_prepare,
	.hw_free      = cxd3778gf_std_dai_hw_free,
	.digital_mute = cxd3778gf_std_dai_mute,
};
static struct snd_soc_dai_ops cxd3778gf_dac_dai_ops = {
	.startup      = cxd3778gf_dac_dai_startup,
	.shutdown     = cxd3778gf_dac_dai_shutdown,
	.set_fmt      = cxd3778gf_icx_dai_set_fmt,
	.hw_params    = cxd3778gf_icx_dai_hw_params,
	.prepare      = cxd3778gf_icx_dai_prepare,
	.hw_free      = cxd3778gf_icx_dai_hw_free,
	.digital_mute = cxd3778gf_icx_dai_mute,
};

static const unsigned int cxd3778gf_rates[] = {
        8000, 11025,  16000, 22050,
        32000, 44100, 48000, 88200,
        96000, 176400, 192000, 352800,
        384000, 1411200, 2822400,
};

static const struct snd_pcm_hw_constraint_list cxd3778gf_rate_constraints = {
        .count = ARRAY_SIZE(cxd3778gf_rates),
        .list = cxd3778gf_rates,
};

static struct snd_soc_dai_driver cxd3778gf_dai_driver[] = {
	{
		.name            = CXD3778GF_STD_DAI_NAME,
		.playback        = {
			.stream_name  = "Sound Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates        = SNDRV_PCM_RATE_KNOT,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture         = {
			.stream_name  = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates        = SNDRV_PCM_RATE_44100,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE,
		 },
		.ops             = &cxd3778gf_std_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.name            = CXD3778GF_ICX_DAI_NAME,
		.playback        = {
			.stream_name  = "Music Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates        = SNDRV_PCM_RATE_KNOT,
			.formats      = 0
						  | SNDRV_PCM_FMTBIT_S16_LE
						  | SNDRV_PCM_FMTBIT_S24_LE
						  | SNDRV_PCM_FMTBIT_S32_LE
						  | SNDRV_PCM_FMTBIT_DSD_U8
						  | SNDRV_PCM_FMTBIT_DSD_U16_LE
						  | SNDRV_PCM_FMTBIT_DSD_U32_LE
						  | 0,
		},
		.ops             = &cxd3778gf_icx_dai_ops,
		.symmetric_rates = 1,
	},
	{
		.name            = CXD3778GF_DAC_DAI_NAME,
		.playback        = {
			.stream_name  = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates        = SNDRV_PCM_RATE_KNOT,
			.formats      = 0
						  | SNDRV_PCM_FMTBIT_S16_LE
						  | SNDRV_PCM_FMTBIT_S24_LE
						  | SNDRV_PCM_FMTBIT_S32_LE
						  | SNDRV_PCM_FMTBIT_DSD_U8
						  | SNDRV_PCM_FMTBIT_DSD_U16_LE
						  | SNDRV_PCM_FMTBIT_DSD_U32_LE,
		},
		.ops             = &cxd3778gf_dac_dai_ops,
		.symmetric_rates = 1,
	},
};

#define DUMMY_REG   0
#define DUMMY_SFT   0
#define DUMMY_SFT_L 0
#define DUMMY_SFT_R 4
#define DUMMY_INV   0

static const char * noise_cancel_mode_value[] = {
	"off",
	"airplane",
	"train",
	"office",
	"ainc",
	"ambient1",
	"ambient2",
	"ambient3",
};
static const struct soc_enum noise_cancel_mode_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(noise_cancel_mode_value), noise_cancel_mode_value);

static const char * noise_cancel_status_value[] = {
	"off",
	"nc",
	"ambient",
};

static const struct soc_enum noise_cancel_status_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(noise_cancel_status_value),
					 noise_cancel_status_value);

static const char * output_value[] = {
	"off",
	"headphone",
	"line",
	"speaker",
	"fixedline",
};
static const struct soc_enum output_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(output_value), output_value);

static const char * input_value[] = {
	"off",
	"tuner",
	"mic",
	"line",
	"directmic",
};
static const struct soc_enum input_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(input_value), input_value);

static const char * headphone_amp_value[] = {
	"normal",
	"smaster-se",
	"smaster-btl",
};
static const struct soc_enum headphone_amp_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(headphone_amp_value), headphone_amp_value);

static const char * headphone_smaster_se_gain_mode_value[] = {
        "normal",
        "high",
};
static const struct soc_enum headphone_smaster_se_gain_mode_enum
        = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(headphone_smaster_se_gain_mode_value), headphone_smaster_se_gain_mode_value);

static const char * headphone_smaster_btl_gain_mode_value[] = {
        "normal",
        "high",
};
static const struct soc_enum headphone_smaster_btl_gain_mode_enum
        = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(headphone_smaster_btl_gain_mode_value), headphone_smaster_btl_gain_mode_value);

static const char * headphone_type_value[] = {
	"nw510n",
	"other",
};
static const struct soc_enum headphone_type_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(headphone_type_value), headphone_type_value);

static const char * jack_mode_value[] = {
	"headphone",
	"antenna",
};
static const struct soc_enum jack_mode_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_mode_value), jack_mode_value);

static const char * jack_status_se_value[] = {
	"none",
	"3pin",
	"4pin",
	"5pin",
	"antenna",
};
static const struct soc_enum jack_status_se_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_status_se_value), jack_status_se_value);

static const char * jack_status_btl_value[] = {
        "none",
        "btl",
};
static const struct soc_enum jack_status_btl_enum
        = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_status_btl_value), jack_status_btl_value);

static const char * headphone_detect_mode_value[] = {
	"polling",
	"select",
	"interrupt",
};
static const struct soc_enum headphone_detect_mode_enum
        = SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(headphone_detect_mode_value), headphone_detect_mode_value);

static const char * fader_mute_mode_value[] = {
	"0", //off
	"1", //on
};

static const struct soc_enum fader_mute_mode_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(fader_mute_mode_value), fader_mute_mode_value);

static const char *const playback_latency_value[] = {
	"normal",
	"low",
};

static const struct soc_enum playback_latency_enum
	= SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(playback_latency_value),
				playback_latency_value);

static const struct snd_kcontrol_new cxd3778gf_snd_controls[] =
{
	/* noise_cancel */

	SOC_ENUM_EXT(
		"noise cancel mode",
		noise_cancel_mode_enum,
		cxd3778gf_get_noise_cancel_mode_control,
		cxd3778gf_put_noise_cancel_mode_control
	),
	SOC_ENUM_EXT(
		"noise cancel status",
		noise_cancel_status_enum,
		cxd3778gf_get_noise_cancel_status_control,
		cxd3778gf_put_noise_cancel_status_control
	),
	SOC_SINGLE_EXT(
		"user noise cancel gain",
		DUMMY_REG,
		DUMMY_SFT,
		USER_DNC_GAIN_INDEX_MAX,
		DUMMY_INV,
		cxd3778gf_get_user_noise_cancel_gain_control,
		cxd3778gf_put_user_noise_cancel_gain_control
	),
	SOC_SINGLE_EXT(
		"base noise cancel gain left",
		DUMMY_REG,
		DUMMY_SFT,
		BASE_NOISE_CANCEL_GAIN_INDEX_MAX,
		DUMMY_INV,
		cxd3778gf_get_base_noise_cancel_gain_left_control,
		cxd3778gf_put_base_noise_cancel_gain_left_control
	),
	SOC_SINGLE_EXT(
		"base noise cancel gain right",
		DUMMY_REG,
		DUMMY_SFT,
		BASE_NOISE_CANCEL_GAIN_INDEX_MAX,
		DUMMY_INV,
		cxd3778gf_get_base_noise_cancel_gain_right_control,
		cxd3778gf_put_base_noise_cancel_gain_right_control
	),
	SOC_SINGLE_EXT(
		"base noise cancel gain save",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_base_noise_cancel_gain_save_control,
		cxd3778gf_put_base_noise_cancel_gain_save_control
	),
	SOC_SINGLE_EXT(
		"user ambient gain",
		DUMMY_REG,
		DUMMY_SFT,
		USER_DNC_GAIN_INDEX_MAX,
		DUMMY_INV,
		cxd3778gf_get_user_ambient_gain_control,
		cxd3778gf_put_user_ambient_gain_control
	),
	SOC_SINGLE_EXT(
		"nc ignore jack state",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_nc_ignore_jack_state_control,
		cxd3778gf_put_nc_ignore_jack_state_control
	),


	/* sound_effect */

	SOC_SINGLE_EXT(
		"sound effect",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_sound_effect_control,
		cxd3778gf_put_sound_effect_control
	),

	/* volume_and_mute */

	SOC_SINGLE_EXT(
		"master volume",
		DUMMY_REG,
		DUMMY_SFT,
		MASTER_VOLUME_MAX,
		DUMMY_INV,
		cxd3778gf_get_master_volume_control,
		cxd3778gf_put_master_volume_control
	),
	SOC_SINGLE_EXT(
		"l balance volume",
		DUMMY_REG,
		DUMMY_SFT,
		L_BALANCE_VOLUME_MAX,
		DUMMY_INV,
		cxd3778gf_get_l_balance_volume_control,
		cxd3778gf_put_l_balance_volume_control
	),
        SOC_SINGLE_EXT(
                "r balance volume",
                DUMMY_REG,
                DUMMY_SFT,
                R_BALANCE_VOLUME_MAX,
                DUMMY_INV,
                cxd3778gf_get_r_balance_volume_control,
                cxd3778gf_put_r_balance_volume_control
        ),
	SOC_SINGLE_EXT(
		"master gain",
		DUMMY_REG,
		DUMMY_SFT,
		MASTER_GAIN_MAX,
		DUMMY_INV,
		cxd3778gf_get_master_gain_control,
		cxd3778gf_put_master_gain_control
	),
	SOC_SINGLE_EXT(
		"playback mute",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_playback_mute_control,
		cxd3778gf_put_playback_mute_control
	),
	SOC_SINGLE_EXT(
		"capture mute",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_capture_mute_control,
		cxd3778gf_put_capture_mute_control
	),
	SOC_SINGLE_EXT(
		"analog playback mute",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_analog_playback_mute_control,
		cxd3778gf_put_analog_playback_mute_control
	),
	SOC_SINGLE_EXT(
		"analog stream mute",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_analog_stream_mute_control,
		cxd3778gf_put_analog_stream_mute_control
	),
	SOC_SINGLE_EXT(
		"timed mute",
		DUMMY_REG,
		DUMMY_SFT,
		10000,
		DUMMY_INV,
		cxd3778gf_get_timed_mute_control,
		cxd3778gf_put_timed_mute_control
	),
	SOC_SINGLE_EXT(
		"std timed mute",
		DUMMY_REG,
		DUMMY_SFT,
		10000,
		DUMMY_INV,
		cxd3778gf_get_std_timed_mute_control,
		cxd3778gf_put_std_timed_mute_control
	),
	SOC_SINGLE_EXT(
		"icx timed mute",
		DUMMY_REG,
		DUMMY_SFT,
		10000,
		DUMMY_INV,
		cxd3778gf_get_icx_timed_mute_control,
		cxd3778gf_put_icx_timed_mute_control
	),
	SOC_SINGLE_EXT(
		"dsd timed mute",
		DUMMY_REG,
		DUMMY_SFT,
		10000,
		DUMMY_INV,
		cxd3778gf_get_dsd_timed_mute_control,
		cxd3778gf_put_dsd_timed_mute_control
        ),
	SOC_ENUM_EXT(
		"fader mute sdin1",
		fader_mute_mode_enum,
		cxd3778gf_get_icx_fader_mute_control,
		cxd3778gf_put_icx_fader_mute_control
	),
	SOC_ENUM_EXT(
		"fader mute sdin2",
		fader_mute_mode_enum,
		cxd3778gf_get_std_fader_mute_control,
		cxd3778gf_put_std_fader_mute_control
	),
        SOC_SINGLE_EXT(
                "dsd remastering mute",
                DUMMY_REG,
                DUMMY_SFT,
                1,
                DUMMY_INV,
                cxd3778gf_get_icx_dsd_remastering_mute_control,
                cxd3778gf_put_icx_dsd_remastering_mute_control
        ),

	/* device_control */

	SOC_ENUM_EXT(
		"output device",
		output_enum,
		cxd3778gf_get_output_device_control,
		cxd3778gf_put_output_device_control
	),
	SOC_ENUM_EXT(
		"analog input device",
		input_enum,
		cxd3778gf_get_input_device_control,
		cxd3778gf_put_input_device_control
	),
	SOC_ENUM_EXT(
		"headphone amp",
		headphone_amp_enum,
		cxd3778gf_get_headphone_amp_control,
		cxd3778gf_put_headphone_amp_control
	),
	/* This is deprecated. this API will erase after playerservice's migrate */
        SOC_ENUM_EXT(
                "headphone smaster gain mode",
                headphone_smaster_btl_gain_mode_enum,
                cxd3778gf_get_headphone_smaster_btl_gain_mode_control,
                cxd3778gf_put_headphone_smaster_btl_gain_mode_control
        ),
	SOC_ENUM_EXT(
		"headphone smaster se gain mode",
		headphone_smaster_se_gain_mode_enum,
		cxd3778gf_get_headphone_smaster_se_gain_mode_control,
		cxd3778gf_put_headphone_smaster_se_gain_mode_control
	),
	SOC_ENUM_EXT(
		"headphone smaster btl gain mode",
		headphone_smaster_btl_gain_mode_enum,
		cxd3778gf_get_headphone_smaster_btl_gain_mode_control,
		cxd3778gf_put_headphone_smaster_btl_gain_mode_control
	),
	SOC_ENUM_EXT(
		"noise cancel headphone type",
		headphone_type_enum,
		cxd3778gf_get_headphone_type_control,
		cxd3778gf_put_headphone_type_control
	),
	SOC_ENUM_EXT(
		"jack mode",
		jack_mode_enum,
		cxd3778gf_get_jack_mode_control,
		cxd3778gf_put_jack_mode_control
	),
	SOC_ENUM_EXT(
		"jack status se",
		jack_status_se_enum,
		cxd3778gf_get_jack_status_se_control,
		cxd3778gf_put_jack_status_se_control
	),
        SOC_ENUM_EXT(
		"jack status btl",
		jack_status_btl_enum,
		cxd3778gf_get_jack_status_btl_control,
		cxd3778gf_put_jack_status_btl_control
        ),
	SOC_SINGLE_EXT(
		"standby",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_standby_control,
		cxd3778gf_put_standby_control
	),
	SOC_SINGLE_EXT(
		"deep early suspend",
		DUMMY_REG,
		DUMMY_SFT,
		1,
		DUMMY_INV,
		cxd3778gf_get_deep_early_suspend_control,
		cxd3778gf_put_deep_early_suspend_control
	),

	/* debug_test */

	SOC_ENUM_EXT(
		"headphone detect mode",
		headphone_detect_mode_enum,
		cxd3778gf_get_headphone_detect_mode_control,
		cxd3778gf_put_headphone_detect_mode_control
        ),

	SOC_SINGLE_EXT(
		"debug test",
		DUMMY_REG,
		DUMMY_SFT,
		100,
		DUMMY_INV,
		cxd3778gf_get_debug_test_control,
		cxd3778gf_put_debug_test_control
	),

	SOC_ENUM_EXT(
		"playback latency",
		playback_latency_enum,
		cxd3778gf_get_playback_latency_control,
		cxd3778gf_put_playback_latency_control
	),
};

static int initialized=FALSE;

/*******************/
/*@ entry_routines */
/*******************/

static int __init cxd3778gf_init(void)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	rv=i2c_add_driver(&cxd3778gf_i2c_driver);
	if(rv!=0) {
		print_fail("i2c_add_driver(): code %d error occurred.\n",rv);
		back_trace();
		return(rv);
	}

	return(0);
}

static void __exit cxd3778gf_exit(void)
{
	print_trace("%s()\n",__FUNCTION__);

	i2c_del_driver(&cxd3778gf_i2c_driver);

	return;
}

static int cxd3778gf_i2c_probe(
	      struct i2c_client    * client,
	const struct i2c_device_id * identify
)
{
	struct cxd3778gf_driver_data * driver_data;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	print_info("starting driver...\n");

	print_info("volume step : 120\n");

	driver_data=kzalloc(sizeof(struct cxd3778gf_driver_data),GFP_KERNEL);
	if(driver_data==NULL){
		print_fail("kzalloc(): no memory.\n");
		back_trace();
		return(-ENOMEM);
	}

	dev_set_drvdata(&client->dev,driver_data);
	driver_data->i2c=client;

	rv=snd_soc_register_codec(
		&client->dev,
		&cxd3778gf_codec_driver,
		cxd3778gf_dai_driver,
		ARRAY_SIZE(cxd3778gf_dai_driver)
	);
	if(rv<0){
		print_fail("snd_soc_register_codec(): code %d error occurred.\n",rv);
		back_trace();
		kfree(driver_data);
		return(rv);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	driver_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	driver_data->early_suspend.suspend = cxd3778gf_i2c_early_suspend;
	driver_data->early_suspend.resume = cxd3778gf_i2c_late_resume;
	register_early_suspend(&driver_data->early_suspend);
#endif

	return(0);
}

static int cxd3778gf_i2c_remove(
	struct i2c_client * client
)
{
	struct cxd3778gf_driver_data * driver_data;

	print_trace("%s()\n",__FUNCTION__);

	driver_data=dev_get_drvdata(&client->dev);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&driver_data->early_suspend);
#endif

	snd_soc_unregister_codec(&client->dev);

	kfree(driver_data);

	return(0);
}

static void cxd3778gf_i2c_poweroff(struct i2c_client * client)
{
	struct cxd3778gf_driver_data * driver_data;

	print_trace("%s()\n",__FUNCTION__);

	driver_data=dev_get_drvdata(&client->dev);

	/* snd_soc_unregister_codec(&client->dev); */

	cxd3778gf_codec_remove(driver_data->codec);

	kfree(driver_data);

	return;
}

static int cxd3778gf_codec_probe(struct snd_soc_codec * codec)
{
	struct cxd3778gf_driver_data * driver_data;
	int rv;
	unsigned int type = 0, ptype = 0;
	int headphone_detect_mode;

	print_trace("%s()\n",__FUNCTION__);

	driver_data=dev_get_drvdata(codec->dev);

	driver_data->codec=codec;

	mutex_init(&driver_data->mutex);
	wake_lock_init(&driver_data->wake_lock, WAKE_LOCK_SUSPEND, "CXD3778GF");

	rv = cxd3778gf_setup_platform(driver_data, &type, &ptype);
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv = cxd3778gf_set_board_type(type, ptype);

	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv=cxd3778gf_register_initialize(driver_data->i2c);
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv = cxd3778gf_extcon_initialize(codec->dev);

	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv=cxd3778gf_table_initialize(&driver_data->mutex);
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv=cxd3778gf_volume_initialize();
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv=cxd3778gf_core_initialize(driver_data);
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	cxd3778gf_get_headphone_detect_mode(&headphone_detect_mode);

	rv=cxd3778gf_interrupt_initialize(driver_data, type, headphone_detect_mode);
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv=cxd3778gf_timer_initialize(type, headphone_detect_mode);
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	rv=cxd3778gf_regmon_initialize();
	if(rv<0){
		back_trace();
		cxd3778gf_codec_remove(codec);
		return(rv);
	}

	snd_soc_add_codec_controls(
		codec,
		cxd3778gf_snd_controls,
		ARRAY_SIZE(cxd3778gf_snd_controls)
	);

	initialized=TRUE;

	return(0);
}

static int cxd3778gf_codec_remove(struct snd_soc_codec * codec)
{
	struct cxd3778gf_driver_data * driver_data;
	unsigned int type, ptype;
	int headphone_detect_mode;

	print_trace("%s()\n",__FUNCTION__);

	initialized=FALSE;

	driver_data=dev_get_drvdata(codec->dev);
	cxd3778gf_get_board_type(&type, &ptype);
	cxd3778gf_get_headphone_detect_mode(&headphone_detect_mode);

	cxd3778gf_regmon_finalize();
	cxd3778gf_timer_finalize(type,headphone_detect_mode);
	cxd3778gf_interrupt_finalize(driver_data, type, headphone_detect_mode);
	cxd3778gf_core_finalize(driver_data);
	cxd3778gf_volume_finalize();
	cxd3778gf_table_finalize();
	cxd3778gf_register_finalize();
	cxd3778gf_reset_platform();

	wake_lock_destroy(&driver_data->wake_lock);

	return(0);
}

/******************************/
/*@ power_management_routines */
/******************************/

static int cxd3778gf_i2c_suspend(struct device * device)
{
	struct cxd3778gf_driver_data * driver_data;

	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	driver_data=dev_get_drvdata(device);

	cxd3778gf_suspend(driver_data);

	return(0);
}

static int cxd3778gf_i2c_resume(struct device * device)
{
	struct cxd3778gf_driver_data * driver_data;

	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	driver_data=dev_get_drvdata(device);

	cxd3778gf_resume(driver_data);

	return(0);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cxd3778gf_i2c_early_suspend(struct early_suspend *h)
{
	struct cxd3778gf_driver_data *driver_data =
		container_of(h, struct cxd3778gf_driver_data, early_suspend);

	print_trace("%s()\n", __func__);

	cxd3778gf_early_suspend(driver_data);
}

static void cxd3778gf_i2c_late_resume(struct early_suspend *h)
{
	struct cxd3778gf_driver_data *driver_data =
		container_of(h, struct cxd3778gf_driver_data, early_suspend);

	print_trace("%s()\n", __func__);

	cxd3778gf_late_resume(driver_data);
}
#endif

/************/
/*@ dai_ops */
/************/

static int cxd3778gf_icx_dai_startup(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
)
{
	struct cxd3778gf_driver_data * driver_data;
	int playback;
	int capture;
	int ret;

	print_trace("%s(%d)\n",__FUNCTION__,substream->stream);

	if(!initialized)
		return(0);

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &cxd3778gf_rate_constraints);

	if (ret)
		return ret;

	driver_data = dev_get_drvdata(dai->dev);

	wake_lock_timeout(&driver_data->wake_lock, HZ * CXD3778GF_WAKE_LOCK_TIME);

	playback=FALSE;
	capture=FALSE;

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		playback=TRUE;
	}

	if(substream->stream == SNDRV_PCM_STREAM_CAPTURE){
		capture=TRUE;
	}

	cxd3778gf_set_usb_dac_mode(OFF);
	cxd3778gf_startup(driver_data, playback, FALSE, capture);

	return(0);
}

static int cxd3778gf_std_dai_startup(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
)
{
	struct cxd3778gf_driver_data * driver_data;
	int playback;
	int capture;
	int ret;

	print_trace("%s(%d)\n",__FUNCTION__,substream->stream);

	if(!initialized)
		return(0);

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &cxd3778gf_rate_constraints);

	if (ret)
		return ret;

	driver_data=dev_get_drvdata(dai->dev);
	wake_lock_timeout(&driver_data->wake_lock, HZ * CXD3778GF_WAKE_LOCK_TIME);

	playback=FALSE;
	capture=FALSE;

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		playback=TRUE;
	}

	if(substream->stream == SNDRV_PCM_STREAM_CAPTURE){
		capture=TRUE;
	}

	cxd3778gf_startup(driver_data, FALSE, playback, capture);

	return(0);
}

static int cxd3778gf_dac_dai_startup(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai       *dai
)
{
	struct cxd3778gf_driver_data *driver_data;
	int playback;
	int capture;
	int ret;
	print_trace("%s(%d)\n", __func__, substream->stream);

	if (!initialized)
		return 0;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &cxd3778gf_rate_constraints);

	if (ret)
		return ret;

	driver_data = dev_get_drvdata(dai->dev);

	wake_lock_timeout(&driver_data->wake_lock,
				HZ * CXD3778GF_WAKE_LOCK_TIME);

	playback = FALSE;
	capture = FALSE;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		playback = TRUE;

	cxd3778gf_set_usb_dac_mode(ON);

	cxd3778gf_startup(driver_data, playback, FALSE, capture);

	return 0;
}

static void cxd3778gf_icx_dai_shutdown(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
)
{
	struct cxd3778gf_driver_data * driver_data;
	int playback;
	int capture;

	print_trace("%s(%d)\n",__FUNCTION__,substream->stream);

	if(!initialized)
		return;

	driver_data=dev_get_drvdata(dai->dev);
	wake_lock_timeout(&driver_data->wake_lock, HZ * CXD3778GF_WAKE_LOCK_TIME);

	playback=FALSE;
	capture=FALSE;

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		playback=TRUE;
	}

	if(substream->stream == SNDRV_PCM_STREAM_CAPTURE){
		capture=TRUE;
	}

	cxd3778gf_shutdown(playback,FALSE,capture);

	return;
}

static void cxd3778gf_std_dai_shutdown(
	struct snd_pcm_substream * substream,
	struct snd_soc_dai       * dai
)
{
	struct cxd3778gf_driver_data * driver_data;
	int playback;
	int capture;

	print_trace("%s(%d)\n",__FUNCTION__,substream->stream);

	if(!initialized)
		return;

	driver_data=dev_get_drvdata(dai->dev);
	wake_lock_timeout(&driver_data->wake_lock, HZ * CXD3778GF_WAKE_LOCK_TIME);

	playback=FALSE;
	capture=FALSE;

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		playback=TRUE;
	}

	if(substream->stream == SNDRV_PCM_STREAM_CAPTURE){
		capture=TRUE;
	}

	cxd3778gf_shutdown(FALSE,playback,capture);

	return;
}

static void cxd3778gf_dac_dai_shutdown(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai       *dai
)
{
	struct cxd3778gf_driver_data *driver_data;
	int playback;
	int capture;

	print_trace("%s(%d)\n", __func__, substream->stream);

	if (!initialized)
		return;

	driver_data = dev_get_drvdata(dai->dev);
	wake_lock_timeout(&driver_data->wake_lock,
				HZ * CXD3778GF_WAKE_LOCK_TIME);

	playback = FALSE;
	capture = FALSE;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		playback = TRUE;

	cxd3778gf_shutdown(playback, FALSE, capture);

	return;
}

static int cxd3778gf_icx_dai_set_fmt(
	struct snd_soc_dai * dai,
	unsigned int         format
)
{
	print_trace("%s(%08X)\n",__FUNCTION__,format);

	if(!initialized)
		return(0);

	cxd3778gf_set_icx_dai_fmt_setting(format);

        if((format&SND_SOC_DAIFMT_FORMAT_MASK)==SND_SOC_DAIFMT_I2S)
		cxd3778gf_set_icx_pcm_setting();
	else if((format&SND_SOC_DAIFMT_FORMAT_MASK)==SND_SOC_DAIFMT_PDM)
                cxd3778gf_set_icx_dsd_setting();
	else{
		print_error("format 0x%X is unsupported.\n",(format&SND_SOC_DAIFMT_FORMAT_MASK));
		return(-EINVAL);
	}

	if((format&SND_SOC_DAIFMT_INV_MASK)!=SND_SOC_DAIFMT_NB_NF){
		print_error("polarity 0x%X is unsupported.\n",(format&SND_SOC_DAIFMT_INV_MASK));
		return(-EINVAL);
	}

	if((format&SND_SOC_DAIFMT_MASTER_MASK)==SND_SOC_DAIFMT_CBS_CFS || (format&SND_SOC_DAIFMT_MASTER_MASK)==SND_SOC_DAIFMT_CBM_CFM){
		cxd3778gf_set_icx_i2s_mode((format&SND_SOC_DAIFMT_MASTER_MASK));
	} else {
		print_error("mode 0x%X is unsupported.\n",(format&SND_SOC_DAIFMT_MASTER_MASK));
		return(-EINVAL);
	}


	return(0);
}

static int cxd3778gf_std_dai_set_fmt(
	struct snd_soc_dai * dai,
	unsigned int         format
)
{
	print_trace("%s(%08X)\n",__FUNCTION__,format);

	if(!initialized)
		return(0);

        if((format&SND_SOC_DAIFMT_FORMAT_MASK)==SND_SOC_DAIFMT_I2S)
                cxd3778gf_set_icx_pcm_setting();
	else{
		print_error("format 0x%X is unsupported.\n",(format&SND_SOC_DAIFMT_FORMAT_MASK));
		return(-EINVAL);
	}

	if((format&SND_SOC_DAIFMT_INV_MASK)!=SND_SOC_DAIFMT_NB_NF){
		print_error("polarity 0x%X is unsupported.\n",(format&SND_SOC_DAIFMT_INV_MASK));
		return(-EINVAL);
	}

	if ((format & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBS_CFS ||
	      (format & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM) {
		cxd3778gf_set_std_i2s_mode(format & SND_SOC_DAIFMT_MASTER_MASK);
	} else {
		print_error("mode 0x%X is unsupported.\n", (format&SND_SOC_DAIFMT_MASTER_MASK));
		return(-EINVAL);
	}

	return(0);
}

static int cxd3778gf_icx_dai_hw_params(
	struct snd_pcm_substream * substream,
	struct snd_pcm_hw_params * params,
	struct snd_soc_dai       * dai
)
{
	unsigned int rate;
	unsigned int format;

	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	rate=params_rate(params);

	cxd3778gf_set_icx_playback_dai_rate(rate);

	format = params_format(params);

	cxd3778gf_set_icx_playback_dai_format(format);
	return(0);
}

static int cxd3778gf_std_dai_hw_params(
	struct snd_pcm_substream * substream,
	struct snd_pcm_hw_params * params,
	struct snd_soc_dai       * dai
)
{
	unsigned int rate;
	unsigned int format;

	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	rate=params_rate(params);

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cxd3778gf_set_std_playback_dai_rate(rate);

	if(substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		cxd3778gf_set_capture_dai_rate(rate);

	format = params_format(params);

	cxd3778gf_set_std_playback_dai_format(format);

	return(0);
}

static int cxd3778gf_icx_dai_prepare(
	struct snd_pcm_substream * substream,
        struct snd_soc_dai * dai
)
{
	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	cxd3778gf_set_icx_master_slave_change();

	return(0);
}

static int cxd3778gf_std_dai_prepare(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	print_trace("%s()\n", __func__);

	if (!initialized)
		return 0;

	cxd3778gf_set_std_master_slave_change();

	return 0;
}

static int cxd3778gf_icx_dai_hw_free(
        struct snd_pcm_substream * substream,
        struct snd_soc_dai       * dai
)
{
        unsigned int rate;
        unsigned int format;

        print_trace("%s()\n",__FUNCTION__);

        if(!initialized)
                return(0);

        cxd3778gf_set_icx_playback_dai_free();

        return(0);
}

static int cxd3778gf_std_dai_hw_free(
        struct snd_pcm_substream * substream,
        struct snd_soc_dai       * dai
)
{
        unsigned int rate;
        unsigned int format;

        print_trace("%s()\n",__FUNCTION__);

        if(!initialized)
                return(0);

        cxd3778gf_set_std_playback_dai_free();

        return(0);
}

static int cxd3778gf_icx_dai_mute(struct snd_soc_dai * dai, int mute)
{
	int rv;

	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(!initialized)
		return(0);

	/* in case of DSD,
	*                    ----|mute packet(100msec)|audio stream|mute packet(100msec)|----
	*                        trigger(start)'s rme unmute ----   drain's rme mute-------
	*<---------snd_soc_dai_ops mute                                            snd_soc_dai_ops mute------------->
	*
	*<------- dsd mute section -------                                     ------- dsd mute section ------------>
	*
	* dsd's mute is only release in case of dsd mute packet
	*/

	cxd3778gf_put_dsd_remastering_mute(mute);

	return(0) ;
}

static int cxd3778gf_std_dai_mute(struct snd_soc_dai * dai, int mute)
{
	print_trace("%s(%d)\n",__FUNCTION__,mute);

	if(!initialized)
		return(0);

	return(0) ;
}

static int cxd3778gf_icx_dai_trigger(
	struct snd_pcm_substream * substream,
	int cmd,
        struct snd_soc_dai       * dai
)
{
	int rv;

	print_trace("%s(%d)\n",__FUNCTION__,cmd);

	if(!initialized)
		return(0);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
//		rv=cxd3778gf_put_dsd_remastering_mute(OFF); /* do by dsd timed mute */
		if(rv<0){
			back_trace();
			return(rv);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
//		rv=cxd3778gf_put_dsd_remastering_mute(ON); /* do by dsd timed mute */
		if(rv<0){
			back_trace();
			return(rv);
		}
		break;

	default:
		rv = -EINVAL;
		return(rv);
		break;
	}
        return(0);
}

/****************/
/* noise_cancel */
/****************/

/*@ noise_cancel_mode */

static int cxd3778gf_put_noise_cancel_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_noise_cancel_mode(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_noise_cancel_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_noise_cancel_mode(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ noise_cancel_status */

static int cxd3778gf_put_noise_cancel_status_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	return(0);
}

static int cxd3778gf_get_noise_cancel_status_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_noise_cancel_status(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ user_noise_cancel_gain */

static int cxd3778gf_put_user_noise_cancel_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_user_noise_cancel_gain(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_user_noise_cancel_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_user_noise_cancel_gain(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ base_noise_cancel_gain_left */

static int cxd3778gf_put_base_noise_cancel_gain_left_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	struct soc_mixer_control * control;
	unsigned int vall;
	unsigned int valr;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	control = (struct soc_mixer_control *)kcontrol->private_value;

	vall=ucontrol->value.integer.value[0];
	if(vall>control->max)
		vall=control->max;

	valr=-1;

	rv=cxd3778gf_put_base_noise_cancel_gain(vall,valr);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_base_noise_cancel_gain_left_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int vall=0;
	unsigned int valr=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_base_noise_cancel_gain(&vall,&valr);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=vall;

	return(0);
}

/*@ base_noise_cancel_gain_right */

static int cxd3778gf_put_base_noise_cancel_gain_right_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	struct soc_mixer_control * control;
	unsigned int vall;
	unsigned int valr;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	control = (struct soc_mixer_control *)kcontrol->private_value;

	vall=-1;

	valr=ucontrol->value.integer.value[0];
	if(valr>control->max)
		valr=control->max;

	rv=cxd3778gf_put_base_noise_cancel_gain(vall,valr);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_base_noise_cancel_gain_right_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int vall=0;
	unsigned int valr=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_base_noise_cancel_gain(&vall,&valr);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=valr;

	return(0);
}

/*@ base_noise_cancel_gain_save */

static int cxd3778gf_put_base_noise_cancel_gain_save_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_exit_base_noise_cancel_gain_adjustment(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_base_noise_cancel_gain_save_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	ucontrol->value.integer.value[0]=0;

	return(0);
}

/* user_ambient_gain */
static int cxd3778gf_put_user_ambient_gain_control(
	struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;
	int rv;

	pr_debug("%s()\n", __func__);

	val = ucontrol->value.integer.value[0];

	rv = cxd3778gf_put_user_ambient_gain(val);
	if (rv < 0)
		return rv;

	return 0;
}

static int cxd3778gf_get_user_ambient_gain_control(
	struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val = 0;
	int rv;

	pr_debug("%s()\n", __func__);

	rv = cxd3778gf_get_user_ambient_gain(&val);
	if (rv < 0)
		return rv;

	ucontrol->value.integer.value[0] = val;

	return 0;
}

/* nc_ignore_jack_state */
static int cxd3778gf_put_nc_ignore_jack_state_control(
	struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;
	int rv;

	pr_debug("%s()\n", __func__);

	val = ucontrol->value.integer.value[0];

	rv = cxd3778gf_put_nc_ignore_jack_state(val);
	if (rv < 0)
		return rv;

	return 0;
}

static int cxd3778gf_get_nc_ignore_jack_state_control(
	struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val = 0;
	int rv;

	pr_debug("%s()\n", __func__);

	rv = cxd3778gf_get_nc_ignore_jack_state(&val);
	if (rv < 0)
		return rv;

	ucontrol->value.integer.value[0] = val;

	return 0;
}

/****************/
/* sound_effect */
/****************/

/*@ sound_effect */

static int cxd3778gf_put_sound_effect_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_sound_effect(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_sound_effect_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_sound_effect(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*******************/
/* volume_and_mute */
/*******************/

/*@ master_volume */

static int cxd3778gf_put_master_volume_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_master_volume(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_master_volume_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_master_volume(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ lr balance volume */

static int cxd3778gf_put_l_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val_l;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val_l=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_l_balance_volume(val_l);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_l_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val_l=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_l_balance_volume(&val_l);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val_l;

        return(0);
}

static int cxd3778gf_put_r_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val_r;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

        val_r=ucontrol->value.integer.value[0];

        rv=cxd3778gf_put_r_balance_volume(val_r);
        if(rv<0){
                back_trace();
                return(rv);
        }

        return(0);
}

static int cxd3778gf_get_r_balance_volume_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val_r=0;
        int rv;

        /* print_trace("%s()\n",__FUNCTION__); */

        rv=cxd3778gf_get_r_balance_volume(&val_r);
        if(rv<0){
                back_trace();
                return(rv);
        }

        ucontrol->value.integer.value[0]=val_r;

        return(0);
}

/*@ master_gain */

static int cxd3778gf_put_master_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_master_gain(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_master_gain_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_master_gain(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ playback_mute */

static int cxd3778gf_put_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_playback_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_playback_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ capture_mute */

static int cxd3778gf_put_capture_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_capture_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_capture_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_capture_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ analog_playback_mute */

static int cxd3778gf_put_analog_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_analog_playback_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_analog_playback_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_analog_playback_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ analog_stream_mute */

static int cxd3778gf_put_analog_stream_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_analog_stream_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_analog_stream_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_analog_stream_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ timed_mute */

static int cxd3778gf_put_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_timed_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_timed_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ std_timed_mute */

static int cxd3778gf_put_std_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_std_timed_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_std_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_std_timed_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ icx_timed_mute */

static int cxd3778gf_put_icx_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_icx_timed_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_icx_timed_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_icx_timed_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ dsd_timed_mute */

static int cxd3778gf_put_dsd_timed_mute_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

        val=ucontrol->value.integer.value[0];

        rv=cxd3778gf_put_dsd_timed_mute(val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        return(0);
}

static int cxd3778gf_get_dsd_timed_mute_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val=0;
        int rv;

        /* print_trace("%s()\n",__FUNCTION__); */

        rv=cxd3778gf_get_dsd_timed_mute(&val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        ucontrol->value.integer.value[0]=val;

        return(0);
}

/*@ fader mute */

static int cxd3778gf_put_icx_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	print_trace("%s()\n", __FUNCTION__);

	val=ucontrol->value.integer.value[0];
	
	rv=cxd3778gf_put_icx_fader_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_icx_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	print_trace("%s()\n", __FUNCTION__);

	rv=cxd3778gf_get_icx_fader_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

static int cxd3778gf_put_std_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	print_trace("%s()\n", __FUNCTION__);

	val=ucontrol->value.integer.value[0];
	
	rv=cxd3778gf_put_std_fader_mute(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_std_fader_mute_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	print_trace("%s()\n", __FUNCTION__);

	rv=cxd3778gf_get_std_fader_mute(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ dsd remastering mute */

static int cxd3778gf_put_icx_dsd_remastering_mute_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

        val=ucontrol->value.integer.value[0];

        rv=cxd3778gf_put_dsd_remastering_mute(val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        return(0);
}

static int cxd3778gf_get_icx_dsd_remastering_mute_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val=0;
        int rv;

        /* print_trace("%s()\n",__FUNCTION__); */

        rv=cxd3778gf_get_dsd_remastering_mute(&val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        ucontrol->value.integer.value[0]=val;

        return(0);
}

/******************/
/* device_control */
/******************/

/*@ output_device */

static int cxd3778gf_put_output_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_output_device(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_output_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_output_device(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ input_device */

static int cxd3778gf_put_input_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct cxd3778gf_driver_data *driver_data = dev_get_drvdata(comp->dev);
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_input_device(driver_data, val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_input_device_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_input_device(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ headphone_amp */

static int cxd3778gf_put_headphone_amp_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_headphone_amp(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_headphone_amp_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_headphone_amp(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ headphone_smaster gain mode */

static int cxd3778gf_put_headphone_smaster_se_gain_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

        val=ucontrol->value.integer.value[0];

        rv=cxd3778gf_put_headphone_smaster_se_gain_mode(val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        return(0);
}

static int cxd3778gf_get_headphone_smaster_se_gain_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val=0;
        int rv;

        /* print_trace("%s()\n",__FUNCTION__); */

        rv=cxd3778gf_get_headphone_smaster_se_gain_mode(&val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        ucontrol->value.integer.value[0]=val;

        return(0);
}

static int cxd3778gf_put_headphone_smaster_btl_gain_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

        val=ucontrol->value.integer.value[0];

        rv=cxd3778gf_put_headphone_smaster_btl_gain_mode(val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        return(0);
}

static int cxd3778gf_get_headphone_smaster_btl_gain_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val=0;
        int rv;

        /* print_trace("%s()\n",__FUNCTION__); */

        rv=cxd3778gf_get_headphone_smaster_btl_gain_mode(&val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        ucontrol->value.integer.value[0]=val;

        return(0);
}
/*@ headphone_type */

static int cxd3778gf_put_headphone_type_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_headphone_type(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_headphone_type_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_headphone_type(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ jack_mode */

static int cxd3778gf_put_jack_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_jack_mode(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_jack_mode_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_jack_mode(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

/*@ jack_status */

static int cxd3778gf_put_jack_status_se_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	return(0);
}

static int cxd3778gf_get_jack_status_se_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;

	/* print_trace("%s()\n",__FUNCTION__); */

	cxd3778gf_get_jack_status_se(&val);

	ucontrol->value.integer.value[0]=val;

	return(0);
}

static int cxd3778gf_put_jack_status_btl_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val;

        print_trace("%s()\n",__FUNCTION__);

        val=ucontrol->value.integer.value[0];

        return(0);
}

static int cxd3778gf_get_jack_status_btl_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val=0;

        /* print_trace("%s()\n",__FUNCTION__); */

        cxd3778gf_get_jack_status_btl(&val);

        ucontrol->value.integer.value[0]=val;

        return(0);
}

static int cxd3778gf_put_standby_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct cxd3778gf_driver_data *driver_data = dev_get_drvdata(comp->dev);
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_standby(driver_data, val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_standby_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_standby(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

static int cxd3778gf_put_deep_early_suspend_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol
)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct cxd3778gf_driver_data *driver_data = dev_get_drvdata(comp->dev);
	unsigned int val;
	int rv;

	print_trace("%s()\n", __FUNCTION__);

	val = ucontrol->value.integer.value[0];

	rv = cxd3778gf_put_deep_early_suspend(driver_data, val);
	if (rv < 0) {
		back_trace();
		return rv;
	}

	return 0;
}

static int cxd3778gf_get_deep_early_suspend_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol
)
{
	unsigned int val = 0;
	int rv;

	rv = cxd3778gf_get_deep_early_suspend(&val);
	if (rv < 0) {
		back_trace();
		return rv;
	}

	ucontrol->value.integer.value[0] = val;

	return 0;
}

/**************/
/* debug_test */
/**************/

/*@ headphone detect mode select */
static int cxd3778gf_put_headphone_detect_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct cxd3778gf_driver_data *driver_data = dev_get_drvdata(comp->dev);
        unsigned int val;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

        val=ucontrol->value.integer.value[0];

        rv=cxd3778gf_put_headphone_detect_mode(driver_data, val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        return(0);
}

static int cxd3778gf_get_headphone_detect_mode_control(
        struct snd_kcontrol       * kcontrol,
        struct snd_ctl_elem_value * ucontrol
)
{
        unsigned int val=0;
        int rv;

        /* print_trace("%s()\n",__FUNCTION__); */

        rv=cxd3778gf_get_headphone_detect_mode(&val);
        if(rv<0){
                back_trace();
                return(rv);
        }

        ucontrol->value.integer.value[0]=val;

        return(0);
}

/*@playback latency */

static int cxd3778gf_put_playback_latency_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol
)
{
	int ret;
	unsigned int req_value;

	pr_debug("%s: called\n", __func__);

	req_value = ucontrol->value.integer.value[0];
	ret = cxd3778gf_put_playback_latency(req_value);
	if (0 < ret) {
		back_trace();
		return ret;
	}

	return 0;
}

static int cxd3778gf_get_playback_latency_control(
	struct snd_kcontrol       *kcontrol,
	struct snd_ctl_elem_value *ucontrol
)
{
	int ret;
	unsigned cur_value = 0;

	pr_debug("%s: called\n", __func__);

	ret = cxd3778gf_get_playback_latency(&cur_value);
	if (0 < ret) {
		back_trace();
		return ret;
	}
	ucontrol->value.integer.value[0] = cur_value;

	return 0;
}

/*@ debug_test */

static int cxd3778gf_put_debug_test_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	val=ucontrol->value.integer.value[0];

	rv=cxd3778gf_put_debug_test(val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	return(0);
}

static int cxd3778gf_get_debug_test_control(
	struct snd_kcontrol       * kcontrol,
	struct snd_ctl_elem_value * ucontrol
)
{
	unsigned int val=0;
	int rv;

	/* print_trace("%s()\n",__FUNCTION__); */

	rv=cxd3778gf_get_debug_test(&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	ucontrol->value.integer.value[0]=val;

	return(0);
}

int cxd3778gf_set_icx_hw_free(void)
{
	cxd3778gf_set_icx_i2s_mode(SND_SOC_DAIFMT_CBS_CFS);
	cxd3778gf_set_icx_master_slave_change();
	cxd3778gf_set_icx_dsd_mode(1);

	return 0;
}
