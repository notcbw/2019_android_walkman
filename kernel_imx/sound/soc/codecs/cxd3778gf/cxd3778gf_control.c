/*
 * cxd3778gf_control.c
 *
 * CXD3778GF CODEC driver
 *
 * Copyright 2013,2014,2015,2016,2017,2018,2019 Sony Video & Sound Products Inc.
 * Copyright 2019,2020 Sony Home Entertainment & Sound Products Inc.
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

#include <linux/module.h>
#include <sound/soc.h>
#include <linux/debugfs.h>

#define ICX_ENABLE_AU2CLK
#define STD_PATH_CLK_CHANGE

/****************/
/*@ definitions */
/****************/

#define STUP_REG_BLK_ON0	0x0F
#define STUP_REG_BLK_ON0_TP_Z	0x0A
#define STUP_REG_CLK_EN0	0x17
#define STUP_REG_CLK_EN2	0x40
#define STUP_REG_CLK_EN2_TP_Z	0x10

#define PLUG_DET_MODE_DISABLE   false
#define PLUG_DET_MODE_ENABLE    true

struct cxd3778gf_control_data {
	struct cxd3778gf_driver_data *driver_data;

	struct work_struct  resume_work;
	struct delayed_work pcm_event_work;
	struct delayed_work hp_det_event_work;
};

struct timed_mute_data {
	int                 port;
	int                 busy;
	unsigned long       timeout;
	struct delayed_work work;
};

/***************/
/*@ prototypes */
/***************/

static void do_resume_work(struct work_struct * work);

static int  get_timed_mute(int port, int * value);
static void do_timed_mute_work(struct work_struct * work);
static void do_dsd_mute_termination_work(struct work_struct * work);
static int  cancel_timed_mute(void);
static int  flush_timed_mute(void);

static void do_pcm_event_work(struct work_struct * work);
static void do_hp_det_event_work(struct work_struct * work);

static int suspend_core(bool plug_det_mode);
static int resume_core(struct cxd3778gf_control_data *cdata,
		       bool plug_det_mode);

static int change_output_device(
	int output_device,
	int headphone_amp,
	int analog_playback_mute,
	int icx_playback_active,
	int std_playback_active,
	int capture_active,
	unsigned int sample_rate
);

static int change_input_device(
	int input_device
);

static int change_sample_rate(
	unsigned int rate
);

static int change_sample_rate_std(
        unsigned int rate
);

static int judge_pcm_monitoring(
	int output_device,
	int headphone_amp,
	int noise_cancel_active,
	int analog_playback_mute
);

static int adjust_tone_control(
	int output_device,
	int headphone_amp,
	int headphone_type,
	int jack_status_se
);

static int adjust_device_gain(
	int input_device
);

static int report_jack_status_se(
	int old_status,
	int new_status,
	int force
);

static int report_jack_status_btl(
        int old_status,
        int new_status,
        int force
);
#ifdef ICX_ENABLE_AU2CLK

static int change_external_osc(
	int pwm_nowait
);

static unsigned int suitable_external_osc(void);

#endif

static int startup_chip(unsigned int rate);
static int shutdown_chip(void);
static int switch_cpclk(int value);
static int switch_dac(int value);
static int switch_smaster(int value);
static int switch_hpout2(int value);
static int switch_hpout3(int value);
static int switch_classh(int value);
static int switch_lineout(int value);
static int switch_speaker(int value);
static int switch_speaker_power(int value);
static int switch_linein(int value);
static int switch_tuner(int value);
static int switch_dmic(int value);
static int get_jack_status_se(int noise_cancel_active, int jack_mode);
static int get_jack_status_btl(void);
static int set_mic_bias_mode(int mode);
static int switch_dnc_power(int value);
static int switch_ncmic_amp_power(int value);
static void enable_sd2_clock(int enable, int active);
static int show_device_id(void);
static int dsdif_pass_change(void);
static int pcmif_pass_change(void);
static void disable_dnc1_block(void);
static void disable_dnc2_block(void);
static void enable_dnc1_block(void);
static void enable_dnc2_block(void);
static int reverse_smaster_lr_output(int reverse, int flag);
static int power_off_inactive_dnc_block(void);
static int power_on_inactive_dnc_block(void);

/**************/
/*@ variables */
/**************/

static int initialized = FALSE;
static struct mutex * global_mutex = NULL;
static int core_active = FALSE;
static struct work_struct  dsd_mute_termination_work;
struct workqueue_struct *dsd_mute_wq_high = NULL;

static struct mutex early_suspend_mutex;
static int deep_early_suspend;
static int early_suspend_state;
static int suspend_state;

static int (*repair_electrostatic_damage)(void) = NULL;

static struct mutex timed_mute_mutex;
struct timed_mute_data timed_mute[5]={
	{ 
		.port    = 0,
		.busy    = FALSE,
		.timeout = 0, 
	},
	{ 
		.port    = 1,
		.busy    = FALSE,
		.timeout = 0, 
	},
	{ 
		.port    = 2,
		.busy    = FALSE,
		.timeout = 0, 
	},
	{ 
		.port    = 3,
		.busy    = FALSE,
		.timeout = 0, 
	},
        {
                .port    = 4,
                .busy    = FALSE,
                .timeout = 0,
        },
};

/* initial device status */
static struct cxd3778gf_status present = {

	.noise_cancel_mode           = NOISE_CANCEL_MODE_OFF,
	.noise_cancel_active         = NC_NON_ACTIVE,
	.noise_cancel_path	     = CANVOL0,

	.sound_effect                = OFF,

	.playback_mute               = ON,
	.capture_mute                = ON,
	.master_volume               = 0,
	.balance_volume_l            = 0,
	.balance_volume_r            = 0,
	.master_gain                 = 0,

	.analog_playback_mute        = ON,
	.analog_stream_mute          = ON,
	.dsd_remastering_mute        = ON,
	.icx_playback_active         = 0,
	.std_playback_active         = 0,
	.capture_active              = 0,

	.mix_timed_mute              = OFF,
	.std_timed_mute              = OFF,
	.icx_timed_mute              = OFF,
	.dsd_timed_mute              = OFF,

	.uncg_mute                   = OFF,

	.output_device               = OUTPUT_DEVICE_NONE,
	.input_device                = INPUT_DEVICE_NONE,
	.headphone_amp               = HEADPHONE_AMP_NORMAL,
	.headphone_smaster_se_gain_mode  = HEADPHONE_SMASTER_SE_GAIN_MODE_NORMAL,
	.headphone_smaster_btl_gain_mode = HEADPHONE_SMASTER_BTL_GAIN_MODE_NORMAL,
	.headphone_type              = NCHP_TYPE_NW510N,
	.jack_mode                   = JACK_MODE_HEADPHONE,
	.jack_status_se              = JACK_STATUS_SE_NONE,
	.jack_status_btl             = JACK_STATUS_BTL_NONE,

	.pcm_monitoring              = OFF,

	.sample_rate                 = 44100,
	.format                      = PCM_MODE,
	.osc			     = EXTERNAL_OSC_441,
	.board_type		     = TYPE_A,
	.power_type                  = CONTROL_NEGATIVE_VOLTAGE,

	.icx_i2s_mode		     = SND_SOC_DAIFMT_CBS_CFS,
	.std_i2s_mode                = SND_SOC_DAIFMT_CBS_CFS,

	.usb_dac_mode                = OFF,
	.playback_latency	     = PLAYBACK_LATENCY_NORMAL,

	.dai_format		     = 0,
	.headphone_detect_mode       = HEADPHONE_DETECT_INTERRUPT,
	.nc_gain                     = USER_DNC_GAIN_INDEX_DEFAULT,
	.ambient_gain                = USER_DNC_GAIN_INDEX_DEFAULT,
	.nc_ignore_jack_state        = OFF,
	.inactive_dnc_block          = DNC_INACTIVE_OFF,
};

static struct cxd3778gf_status back_up;

/**************************/
/*@ initialize / finalize */
/**************************/

int cxd3778gf_core_initialize(struct cxd3778gf_driver_data *ddata)
{
	struct device *dev = ddata->codec->dev;
	struct cxd3778gf_control_data *cdata;

	print_trace("%s()\n",__FUNCTION__);

	global_mutex=&ddata->mutex;

	mutex_lock(global_mutex);

	cdata = devm_kzalloc(dev,
			     sizeof(struct cxd3778gf_control_data),
			     GFP_KERNEL);
	if (!cdata) {
		dev_err(dev, "failed to allocate state\n");
		mutex_unlock(global_mutex);
		return -ENOMEM;
	}

	cdata->driver_data = ddata;
	ddata->control_data = cdata;

	INIT_WORK(&cdata->resume_work, do_resume_work);
	INIT_DELAYED_WORK(&cdata->pcm_event_work, do_pcm_event_work);
	INIT_DELAYED_WORK(&cdata->hp_det_event_work, do_hp_det_event_work);

	mutex_init(&timed_mute_mutex);
	INIT_DELAYED_WORK(&timed_mute[0].work, do_timed_mute_work);
	INIT_DELAYED_WORK(&timed_mute[1].work, do_timed_mute_work);
	INIT_DELAYED_WORK(&timed_mute[2].work, do_timed_mute_work);
	INIT_DELAYED_WORK(&timed_mute[3].work, do_timed_mute_work);
	INIT_DELAYED_WORK(&timed_mute[4].work, do_timed_mute_work);

	dsd_mute_wq_high = alloc_workqueue("dsd_mute_high_pri", WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (NULL == dsd_mute_wq_high) {
		print_error("Unable to create dsd mute workqueues\n");
		destroy_workqueue(dsd_mute_wq_high);
		dsd_mute_wq_high = NULL;
	}
	INIT_WORK(&dsd_mute_termination_work, do_dsd_mute_termination_work);

	cxd3778gf_switch_smaster_mute(ON,HEADPHONE_AMP_SMASTER_SE);
	cxd3778gf_switch_smaster_mute(ON,HEADPHONE_AMP_SMASTER_BTL);
	cxd3778gf_switch_class_h_mute(ON);
	msleep(50);
//	digiamp_switch_shunt_mute(ON);

	/* dummy 1.8V/2.85V default on */
	cxd3778gf_switch_logic_ldo(ON);
	cxd3778gf_switch_180_power(ON);
	cxd3778gf_switch_285_power(ON);
//	digiamp_power_on();
	msleep(50);

	startup_chip(present.sample_rate);

	show_device_id();

//	digiamp_initialize();

	/* dummy, dnc driver is not loaded. */
	cxd3778gf_dnc_initialize();

	/********************/

	cxd3778gf_set_mix_timed_mute(&present,OFF);
	present.mix_timed_mute = OFF;
	cxd3778gf_set_std_timed_mute(&present,OFF);
	present.std_timed_mute = OFF;
	cxd3778gf_set_icx_timed_mute(&present,OFF);
	present.icx_timed_mute = OFF;
	cxd3778gf_set_dsd_timed_mute(&present,OFF);
	present.dsd_timed_mute = OFF;
	cxd3778gf_set_uncg_mute(&present,OFF);
	present.uncg_mute = OFF;

	cxd3778gf_set_dsd_remastering_mute(&present,ON);
	present.dsd_remastering_mute = ON;

	cxd3778gf_set_analog_stream_mute(&present,OFF);
	present.analog_stream_mute = OFF;

	cxd3778gf_set_analog_playback_mute(&present,present.analog_playback_mute);

	cxd3778gf_set_playback_mute(&present,OFF);
	present.playback_mute = OFF;

	cxd3778gf_set_capture_mute(&present,OFF);
	present.capture_mute = OFF;

	change_output_device(
		present.output_device,
		HEADPHONE_AMP_SMASTER_SE,
		present.analog_playback_mute,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);
	present.headphone_amp = HEADPHONE_AMP_SMASTER_SE;

	change_input_device(
		present.input_device
	);

	/* TYPE_Z don't have NC function */
	if(present.board_type==TYPE_Z)
		present.headphone_type = NCHP_TYPE_OTHER;

	/* dummy, dnc driver is not loaded. */
	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_master_volume(&present,present.master_volume);

	cxd3778gf_set_master_gain(&present,30);
	present.master_gain=30;

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	adjust_device_gain(present.input_device);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	mutex_init(&early_suspend_mutex);
	deep_early_suspend = FALSE;
	early_suspend_state = FALSE;
	suspend_state = FALSE;

	/********************/

	cxd3778gf_set_output_device_mute(&present,OFF,TRUE);
	cxd3778gf_set_input_device_mute(&present,OFF);

	core_active=TRUE;

	initialized=TRUE;

	mutex_unlock(global_mutex);

	/* Headphone status is once checked at interrupt mode */
	if (present.headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT)
		schedule_delayed_work(&cdata->hp_det_event_work, msecs_to_jiffies(100));

	return(0);
}

int cxd3778gf_core_finalize(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n",__FUNCTION__);

	cancel_work_sync(&cdata->resume_work);
	cancel_delayed_work_sync(&cdata->pcm_event_work);
	cancel_delayed_work_sync(&cdata->hp_det_event_work);
	cancel_timed_mute();
	flush_work(&cdata->resume_work);
	flush_delayed_work(&cdata->pcm_event_work);
	flush_delayed_work(&cdata->hp_det_event_work);
	flush_timed_mute();

	flush_workqueue(dsd_mute_wq_high);

	mutex_lock(global_mutex);

	if(!initialized){
		mutex_unlock(global_mutex);
		return(0);
	}

	initialized=FALSE;

	if(!core_active){
		mutex_unlock(global_mutex);
		return(0);
	}

	core_active=FALSE;

	cxd3778gf_set_output_device_mute(&present,ON,TRUE);
	cxd3778gf_set_input_device_mute(&present,ON);

	/********************/

	cxd3778gf_set_mix_timed_mute(&present,OFF);
	present.mix_timed_mute = OFF;
	cxd3778gf_set_std_timed_mute(&present,OFF);
	present.std_timed_mute = OFF;
	cxd3778gf_set_icx_timed_mute(&present,OFF);
	present.icx_timed_mute = OFF;
	cxd3778gf_set_dsd_timed_mute(&present,OFF);
	present.dsd_timed_mute = OFF;
	cxd3778gf_set_uncg_mute(&present,OFF);
	present.uncg_mute = OFF;

	cxd3778gf_set_dsd_remastering_mute(&present,ON);
        present.dsd_remastering_mute = ON;

	cxd3778gf_set_analog_stream_mute(&present,ON);
	present.analog_stream_mute=ON;

	cxd3778gf_set_analog_playback_mute(&present,ON);
	present.analog_playback_mute=ON;

	cxd3778gf_set_playback_mute(&present,ON);
	present.playback_mute=ON;

	cxd3778gf_set_capture_mute(&present,ON);
	present.capture_mute=ON;

	change_output_device(
		OUTPUT_DEVICE_NONE,
		HEADPHONE_AMP_NORMAL,
		present.analog_playback_mute,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);
	present.output_device=OUTPUT_DEVICE_NONE;
	present.headphone_amp=HEADPHONE_AMP_NORMAL;

	change_input_device(
		INPUT_DEVICE_NONE
	);
	present.input_device=INPUT_DEVICE_NONE;

	present.noise_cancel_mode=NOISE_CANCEL_MODE_OFF;
	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_master_volume(&present,0);
	present.master_gain=0;

	cxd3778gf_set_master_gain(&present,0);
	present.master_volume=0;

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	adjust_device_gain(present.input_device);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	/********************/

	cxd3778gf_dnc_shutdown();

//	digiamp_shutdown();

	shutdown_chip();

//	digiamp_power_off();
	cxd3778gf_switch_285_power(OFF);
	cxd3778gf_switch_180_power(OFF);
	msleep(20);
	cxd3778gf_switch_logic_ldo(OFF);

	cxd3778gf_switch_smaster_mute(ON,HEADPHONE_AMP_SMASTER_SE);
	cxd3778gf_switch_smaster_mute(ON,HEADPHONE_AMP_SMASTER_BTL);
	cxd3778gf_switch_class_h_mute(ON);
	msleep(50);
//	digiamp_switch_shunt_mute(OFF);

	mutex_unlock(global_mutex);

	return(0);
}

/*******************/
/*@ resume/suspend */
/*******************/

int cxd3778gf_suspend(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n",__FUNCTION__);

	cxd3778gf_interrupt_enable_wake(
		ddata,
		present.board_type,
		present.headphone_detect_mode);

	if (present.icx_playback_active)
		return 0;

	mutex_lock(&early_suspend_mutex);

	flush_delayed_work(&cdata->pcm_event_work);
	flush_delayed_work(&cdata->hp_det_event_work);
	flush_timed_mute();
	flush_workqueue(dsd_mute_wq_high);

	mutex_lock(global_mutex);

	if(initialized)
		suspend_core(PLUG_DET_MODE_ENABLE);

	mutex_unlock(global_mutex);

	suspend_state = TRUE;
	mutex_unlock(&early_suspend_mutex);

	return(0);
}

int cxd3778gf_resume(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n",__FUNCTION__);

	cxd3778gf_interrupt_disable_wake(
		ddata,
		present.board_type,
		present.headphone_detect_mode);

	if (present.icx_playback_active)
		return 0;

	schedule_work(&cdata->resume_work);

	return(0);
}

static void do_resume_work(struct work_struct * work)
{
	struct cxd3778gf_control_data *cdata =
		container_of(work, struct cxd3778gf_control_data, resume_work);

	print_trace("%s()\n",__FUNCTION__);

	mutex_lock(&early_suspend_mutex);
	/* If it needs to resume, resume core. */
	if (!deep_early_suspend || !early_suspend_state) {
		mutex_lock(global_mutex);

		if (initialized)
			resume_core(cdata, PLUG_DET_MODE_ENABLE);

		mutex_unlock(global_mutex);
	}
	suspend_state = FALSE;
	mutex_unlock(&early_suspend_mutex);

	return;
}

int cxd3778gf_early_suspend(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n", __FUNCTION__);

	mutex_lock(global_mutex);
	power_off_inactive_dnc_block();
	mutex_unlock(global_mutex);

	mutex_lock(&early_suspend_mutex);

	if (deep_early_suspend) {
		flush_delayed_work(&cdata->pcm_event_work);
		flush_delayed_work(&cdata->hp_det_event_work);
		flush_timed_mute();
		flush_workqueue(dsd_mute_wq_high);

		mutex_lock(global_mutex);
		if (initialized
		&& present.icx_playback_active == 0
		&& present.std_playback_active == 0
		&& present.capture_active == 0)
			suspend_core(PLUG_DET_MODE_DISABLE);
		mutex_unlock(global_mutex);
	}

	early_suspend_state = TRUE;
	mutex_unlock(&early_suspend_mutex);

	return 0;
}

int cxd3778gf_late_resume(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n", __FUNCTION__);

	mutex_lock(&early_suspend_mutex);
	if (deep_early_suspend) {
		mutex_lock(global_mutex);
		if (initialized)
			resume_core(cdata, PLUG_DET_MODE_DISABLE);
		mutex_unlock(global_mutex);
	}
	early_suspend_state = FALSE;
	mutex_unlock(&early_suspend_mutex);

	mutex_lock(global_mutex);
	power_on_inactive_dnc_block();
	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_put_standby(struct cxd3778gf_driver_data *ddata, int val)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n",__FUNCTION__);

	if(val){
		flush_delayed_work(&cdata->pcm_event_work);
		flush_delayed_work(&cdata->hp_det_event_work);
		flush_timed_mute();
		flush_workqueue(dsd_mute_wq_high);

		mutex_lock(global_mutex);

		if(initialized
		&& present.icx_playback_active==0
		&& present.std_playback_active==0
		&& present.capture_active==0)
			suspend_core(PLUG_DET_MODE_DISABLE);

		mutex_unlock(global_mutex);
	}
	else{
		mutex_lock(global_mutex);

		if (initialized)
			resume_core(cdata, PLUG_DET_MODE_DISABLE);

		mutex_unlock(global_mutex);
	}

	return 0;
}

int cxd3778gf_get_standby(int * val)
{
	*val=core_active?0:1;
	return 0;
}

int cxd3778gf_put_deep_early_suspend(struct cxd3778gf_driver_data *ddata, int val)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s()\n",__FUNCTION__);

	mutex_lock(&early_suspend_mutex);

	if (val) {
		/*
		 * if deep early suspend is enabled at early suspend state,
		 *  suspend core.
		 */
		if (!deep_early_suspend
		&& early_suspend_state
		&& !suspend_state) {
			flush_delayed_work(&cdata->pcm_event_work);
			flush_delayed_work(&cdata->hp_det_event_work);
			flush_timed_mute();
			flush_workqueue(dsd_mute_wq_high);

			mutex_lock(global_mutex);
			if (initialized
			&& present.icx_playback_active == 0
			&& present.std_playback_active == 0
			&& present.capture_active == 0)
				suspend_core(PLUG_DET_MODE_DISABLE);
			mutex_unlock(global_mutex);
		}
		deep_early_suspend = TRUE;

	} else {
		/*
		 * if deep early suspend is disabled at early suspend state,
		 * resume core.
		 */
		if (deep_early_suspend
		&& early_suspend_state
		&& !suspend_state) {
			mutex_lock(global_mutex);
			if (initialized)
				resume_core(cdata, PLUG_DET_MODE_DISABLE);
			mutex_unlock(global_mutex);
		}
		deep_early_suspend = FALSE;
	}

	mutex_unlock(&early_suspend_mutex);

	return 0;
}

int cxd3778gf_get_deep_early_suspend(int * val)
{
	mutex_lock(&early_suspend_mutex);
	*val = deep_early_suspend ? 1 : 0;
	mutex_unlock(&early_suspend_mutex);

	return 0;
}

/*********************/
/*@ startup/shutdown */
/*********************/

int cxd3778gf_startup(struct cxd3778gf_driver_data *ddata,
		      int icx_playback,
		      int std_playback,
		      int capture)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s(%d,%d,%d)\n",__FUNCTION__,icx_playback,std_playback,capture);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	resume_core(cdata, PLUG_DET_MODE_DISABLE);

	if(present.output_device==OUTPUT_DEVICE_SPEAKER){
		/* if mute is off, speaker is alrady used. */
		if(present.analog_playback_mute==ON){
			if( (icx_playback || std_playback)
			&& present.icx_playback_active==0
			&& present.std_playback_active==0)
				switch_speaker_power(ON);
		}
	}

	if( (icx_playback || std_playback)
		&& present.icx_playback_active==0
		&& present.std_playback_active==0)
			cxd3778gf_register_write(CXD3778GF_CODEC_SR_PLAY3,0x7F);

	if(icx_playback)
		present.icx_playback_active++;

	if(std_playback)
		present.std_playback_active++;

	if(capture)
		present.capture_active++;

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_shutdown(int icx_playback, int std_playback, int capture)
{
	print_trace("%s(%d,%d,%d)\n",__FUNCTION__,icx_playback,std_playback,capture);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(icx_playback){
		present.icx_playback_active--;
		/* SD1's BCK and LRCK change slave mode */
		if(present.icx_i2s_mode == SND_SOC_DAIFMT_CBM_CFM)
			cxd3778gf_register_modify(CXD3778GF_CLK_OUTPUT,0x00,0x11);
	}
	if (std_playback) {
		present.std_playback_active--;
		if(present.std_i2s_mode == SND_SOC_DAIFMT_CBM_CFM)
			enable_sd2_clock(0, present.noise_cancel_active);
	}

	if(capture) {
		present.capture_active--;
		enable_sd2_clock(0, present.noise_cancel_active);
	}

#ifdef ICX_ENABLE_AU2CLK
	change_external_osc(ON);
#endif

	if(present.output_device==OUTPUT_DEVICE_SPEAKER){
		/* if mute is off, speaker is still used. */
		if(present.analog_playback_mute==ON){
			if( (icx_playback || std_playback)
			&& present.icx_playback_active==0
			&& present.std_playback_active==0)
				switch_speaker_power(OFF);
		}
	}

	if( (icx_playback || std_playback)
		&& present.icx_playback_active==0
		&& present.std_playback_active==0)
			cxd3778gf_register_write(CXD3778GF_CODEC_SR_PLAY3,0x01);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_set_icx_pcm_setting(void)
{
	print_trace("%s(%u)\n",__FUNCTION__);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	present.format=PCM_MODE;
	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_set_icx_dsd_setting(void)
{
	print_trace("%s(%u)\n",__FUNCTION__);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	present.format=DSD_MODE;
	mutex_unlock(global_mutex);

	return(0);
}

/******************/
/*@ board type    */
/******************/

int cxd3778gf_set_board_type(unsigned int type, unsigned int ptype)
{
        print_trace("%s(%u)\n",__FUNCTION__);

        present.board_type=type;
	present.power_type=ptype;

	return(0);
}

int cxd3778gf_get_board_type(unsigned int * type, unsigned int * ptype)
{
        print_trace("%s(%u)\n",__FUNCTION__);

        *type = present.board_type;
	*ptype = present.power_type;
        return(0);
}

int cxd3778gf_set_icx_i2s_mode(int mode)
{
	print_trace("%s(%u)\n",__FUNCTION__);

	mutex_lock(global_mutex);
	present.icx_i2s_mode=mode;
	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_set_std_i2s_mode(int mode)
{
	print_trace("%s(%u)\n", __func__);

	mutex_lock(global_mutex);
	present.std_i2s_mode = mode;
	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_set_usb_dac_mode(int mode)
{
	print_trace("%s(%u)\n", __func__);

	mutex_lock(global_mutex);
	present.usb_dac_mode = mode;
	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_set_icx_dai_fmt_setting(int format)
{
	print_trace("%s(%u)\n", __func__);

	mutex_lock(global_mutex);
	present.dai_format = format;
	mutex_unlock(global_mutex);

	return 0;
}

/******************/
/*@ sampling_rate */
/******************/

int cxd3778gf_set_icx_playback_dai_rate(unsigned int rate)
{
	print_trace("%s(%u)\n",__FUNCTION__,rate);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	change_sample_rate(rate);
	present.sample_rate=rate;

	if (present.format == PCM_MODE)
		pcmif_pass_change();

#ifdef ICX_ENABLE_AU2CLK
	change_external_osc(ON);
#endif

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_set_std_playback_dai_rate(unsigned int rate)
{
	print_trace("%s(%u)\n",__FUNCTION__,rate);

	mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

	change_sample_rate_std(rate);

#ifdef STD_PATH_CLK_CHANGE
	present.sample_rate = rate;
#endif

#ifdef ICX_ENABLE_AU2CLK
	change_external_osc(ON);
#endif

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_set_capture_dai_rate(unsigned int rate)
{
	print_trace("%s(%u)\n",__FUNCTION__,rate);

	mutex_lock(global_mutex);

#ifdef ICX_ENABLE_AU2CLK
	change_external_osc(ON);
#endif

	mutex_unlock(global_mutex);

	return(0);
}

/******************/
/*@ format */
/******************/

int cxd3778gf_set_icx_playback_dai_format(unsigned int format)
{

	int now_osc;
	int dsd_enable;
	int tmp;

	mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

	if (present.board_type == TYPE_Z && !(board_set_flag&NO_MCLK_DIRECTLY_INPUT_FLAG)){
                if(cxd3778gf_get_441clk_value())
                        now_osc = EXTERNAL_OSC_441;
                else if(cxd3778gf_get_480clk_value())
                        now_osc = EXTERNAL_OSC_480;
                else
                        now_osc = -1;
	} else {
		cxd3778gf_register_read(CXD3778GF_OSC_ON,&now_osc);
		if (now_osc == 0x10)
                        now_osc = EXTERNAL_OSC_441;
                else if (now_osc == 0x20)
                        now_osc = EXTERNAL_OSC_480;
                else
                        now_osc = -1;
	}

	cxd3778gf_register_read(CXD3778GF_DSD_ENABLE,&dsd_enable);

	if (present.format==PCM_MODE) {
//		cxd3778gf_register_modify(CXD3778GF_CLK_EN0,0x01,0x01);
		switch (format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			cxd3778gf_register_modify(CXD3778GF_SD1_MASK,0xF0,0xFF);
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			cxd3778gf_register_modify(CXD3778GF_SD1_MASK,0xFC,0xFF);
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			cxd3778gf_register_modify(CXD3778GF_SD1_MASK,0xFF,0xFF);
			break;
		}

		if (present.board_type == TYPE_Z)
			cxd3778gf_set_master_volume(&present, present.master_volume);
	}

	if(present.format==DSD_MODE) {
//		msleep(50);
//		cxd3778gf_set_output_device_mute(&present,ON,TRUE);
		if(!(dsd_enable&0x01)){
			cxd3778gf_set_output_device_mute(&present,ON,TRUE);
			dsdif_pass_change();
			cxd3778gf_set_output_device_mute(&present,OFF,TRUE);
                }

		if(present.sample_rate==88200)
			cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1,0x00,0x0C);
		else if(present.sample_rate==176400)
			cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1,0x04,0x0C);
		else if(present.sample_rate==352800)
			cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1,0x08,0x0C);
		else
			print_error("not support format.\n");

		cxd3778gf_set_master_volume(&present,present.master_volume);
//		cxd3778gf_set_output_device_mute(&present,OFF,TRUE);
	}

	mutex_unlock(global_mutex);

        return(0);
}

int cxd3778gf_set_std_playback_dai_format(unsigned int format)
{
	int dsd_enable;

        mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

//	cxd3778gf_register_modify(CXD3778GF_CLK_EN0,0x02,0x02);

	switch (format) {
        case SNDRV_PCM_FORMAT_S16_LE:
                cxd3778gf_register_modify(CXD3778GF_SD2_MASK,0xF0,0xFF);
                break;
        case SNDRV_PCM_FORMAT_S24_LE:
                cxd3778gf_register_modify(CXD3778GF_SD2_MASK,0xFC,0xFF);
                break;
        case SNDRV_PCM_FORMAT_S32_LE:
                cxd3778gf_register_modify(CXD3778GF_SD2_MASK,0xFF,0xFF);
                break;
	}

	cxd3778gf_register_read(CXD3778GF_DSD_ENABLE,&dsd_enable);

	if(dsd_enable&0x01){
		cxd3778gf_set_phv_mute(&present, TRUE, TRUE);
		cxd3778gf_set_line_mute(&present,TRUE);
		cxd3778gf_register_write(CXD3778GF_SMS_DSD_PMUTE, 0x80);
		cxd3778gf_register_modify(CXD3778GF_DSD_ENABLE,0x00,0x01);
		if (present.playback_latency == PLAYBACK_LATENCY_LOW)
			usleep_range(1000, 1100);
		else
			msleep(1);

//              cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x10,0x70);
		cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL1,0x00,0x80);
		cxd3778gf_register_modify(CXD3778GF_NS_DAC,0x00,0x01);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x40,0xD4);
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x00,0x80);
		//cxd3778gf_register_modify(CXD3778GF_CLK_EN0,0x10,0x10);
		cxd3778gf_set_line_mute(&present,FALSE);
		cxd3778gf_set_phv_mute(&present, FALSE, TRUE);
		cxd3778gf_set_master_volume(&present, present.master_volume);
	}

        mutex_unlock(global_mutex);

        return(0);
}
int cxd3778gf_set_icx_master_slave_change(void)
{
	print_trace("%s(%u)\n",__FUNCTION__);

	mutex_lock(global_mutex);

	if(present.icx_i2s_mode == SND_SOC_DAIFMT_CBM_CFM){
		/* SD1's BCK and LRCK is master mode */
		cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x00, 0x01);
		cxd3778gf_register_modify(CXD3778GF_CLK_OUTPUT, 0x11, 0x11);
		cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x01, 0x01);
	}else{
		/* SD1's BCK and LRCK is slave mode */
		cxd3778gf_register_modify(CXD3778GF_CLK_OUTPUT,0x00,0x11);
	}

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_set_std_master_slave_change(void)
{
	print_trace("%s\n", __func__);

	mutex_lock(global_mutex);

	if (present.std_i2s_mode == SND_SOC_DAIFMT_CBM_CFM) {
		/* Set BCK and LRCK of SD2 to master mode */
		enable_sd2_clock(1, present.noise_cancel_active);
	}

	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_set_icx_playback_dai_free(void)
{
	print_trace("%s(%u)\n",__FUNCTION__);

        return(0);
}

int cxd3778gf_set_std_playback_dai_free(void)
{
        mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

	mutex_unlock(global_mutex);

	return(0);
}
/***********************************/
/*@ noise_cancel_mode              */
/*    NOISE_CANCEL_MODE_OFF      0 */
/*    NOISE_CANCEL_MODE_OFFICE   1 */
/*    NOISE_CANCEL_MODE_TRAIN    2 */
/*    NOISE_CANCEL_MODE_AIRPLANE 3 */
/***********************************/

int cxd3778gf_put_noise_cancel_mode(int value)
{
	int switching;

	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>NOISE_CANCEL_MODE_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.noise_cancel_mode=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if (OUTPUT_DEVICE_HEADPHONE != present.output_device) {
		print_warning("This command can be used only while output_device is headphone\n");
		mutex_unlock(global_mutex);
		return 0;
	}

	if(value==present.noise_cancel_mode){
		mutex_unlock(global_mutex);
		return(0);
	}

	if (present.inactive_dnc_block == DNC1_INACTIVE) {
		if (value == NOISE_CANCEL_MODE_AIRPLANE ||
		    value == NOISE_CANCEL_MODE_TRAIN ||
		    value == NOISE_CANCEL_MODE_OFFICE ||
		    value == NOISE_CANCEL_MODE_AINC) {
			print_error("Cannot use DNC1 block\n");
			mutex_unlock(global_mutex);
			return -EPERM;
		}
	}

	if (present.inactive_dnc_block == DNC2_INACTIVE) {
		if (value == NOISE_CANCEL_MODE_AMBIENT1 ||
		    value == NOISE_CANCEL_MODE_AMBIENT2 ||
		    value == NOISE_CANCEL_MODE_AMBIENT3) {
			print_error("Cannot use DNC2 block\n");
			mutex_unlock(global_mutex);
			return -EPERM;
		}
	}

	if((present.noise_cancel_mode==NOISE_CANCEL_MODE_OFF && value!=NOISE_CANCEL_MODE_OFF)
	|| (present.noise_cancel_mode!=NOISE_CANCEL_MODE_OFF && value==NOISE_CANCEL_MODE_OFF)
	|| (present.input_device != INPUT_DEVICE_NONE &&
	    present.noise_cancel_active != NC_ACTIVE)) {
		switching=TRUE;
		if (present.icx_playback_active != 0)
			print_warning("switching during playback %d\n",
					   present.icx_playback_active);
		if (present.capture_active != 0)
			print_warning("switching during capture %d\n",
					       present.capture_active);
	} else {
		switching=FALSE;
	}

	if(switching){
		if (NOISE_CANCEL_MODE_OFF == value)
			cxd3778gf_dnc_mute_dnc1monvol(ON);
		if (NC_ACTIVE == present.noise_cancel_active)
			msleep(200);

		cxd3778gf_set_output_device_mute(&present,ON,TRUE);
	}
	/********************/

	present.noise_cancel_mode = value;
	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	/********************/

#ifdef ICX_ENABLE_AU2CLK
	if (present.icx_playback_active == 0 && present.capture_active == 0) {
		if (switching)
			change_external_osc(OFF);
		else
			change_external_osc(ON);
	}
#endif

	if(switching){
		present.pcm_monitoring=judge_pcm_monitoring(
			present.output_device,
			present.headphone_amp,
			present.noise_cancel_active,
			present.analog_playback_mute
		);

		cxd3778gf_set_master_volume(&present,present.master_volume);

		cxd3778gf_set_output_device_mute(&present,OFF,TRUE);

		/* For crossfade, unmute DNC1 monvol also in ambient mode */
		if (present.noise_cancel_active == NC_ACTIVE ||
		    present.noise_cancel_active == AMBIENT) {
			cxd3778gf_dnc_mute_dnc1monvol(OFF);
			msleep(200);
		}
	}

	if (present.inactive_dnc_block == DNC1_INACTIVE)
		disable_dnc1_block();

	if (present.inactive_dnc_block == DNC2_INACTIVE)
		disable_dnc2_block();

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_noise_cancel_mode(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.noise_cancel_mode;
	else
		*value=back_up.noise_cancel_mode;

	mutex_unlock(global_mutex);

	return(0);
}

/************************/
/*@ noise_cancel_status */
/************************/

int cxd3778gf_get_noise_cancel_status(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.noise_cancel_active;

	mutex_unlock(global_mutex);

	return(0);
}

/***************************/
/*@ user_noise_cancel_gain */
/*    0 - 30               */
/***************************/

int cxd3778gf_put_user_noise_cancel_gain(int index)
{
	int timeout;
	int rv;
	unsigned int buf = 0;

	print_trace("%s(%d)\n",__FUNCTION__,index);

	if (!initialized) {
		print_error("not initialized.\n");
		return -EBUSY;
	}

	if (TYPE_Z == present.board_type) {
		pr_notice("%s: function not supported\n", __func__);
		return 0;
	}

	mutex_lock(global_mutex);

	cxd3778gf_register_read(CXD3778GF_DNC1_AVFCAN, &buf);
	cxd3778gf_register_write(CXD3778GF_DNC1_AVFCAN, 0x11);

	rv = cxd3778gf_dnc_set_user_gain(index, present.noise_cancel_path);
	if (rv < 0) {
		back_trace();
		goto out_setgain;
	}

	present.nc_gain = index;

	msleep(160);

out_setgain:
	cxd3778gf_register_write(CXD3778GF_DNC1_AVFCAN, buf);

	mutex_unlock(global_mutex);

	return rv;
}

int cxd3778gf_get_user_noise_cancel_gain(int * index)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*index=0;

	cxd3778gf_dnc_get_user_gain(index);

	mutex_unlock(global_mutex);

	return(0);
}

/***************************/
/*@ base_noise_cancel_gain */
/*    0 - 50               */
/***************************/

int cxd3778gf_put_base_noise_cancel_gain(int left, int right)
{
	int rv;

	print_trace("%s(%d,%d)\n",__FUNCTION__,left,right);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	rv=cxd3778gf_dnc_set_base_gain(left,right);
	if(rv<0){
		back_trace();
		mutex_unlock(global_mutex);
		return(rv);
	}

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_base_noise_cancel_gain(int * left, int * right)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*left=0;
	*right=0;

	cxd3778gf_dnc_get_base_gain(left,right);

	mutex_unlock(global_mutex);

	return(0);
}

/*******************************************/
/*@ exit_base_noise_cancel_gain_adjustment */
/*******************************************/

int cxd3778gf_exit_base_noise_cancel_gain_adjustment(int save)
{
	int rv;

	print_trace("%s(%d)\n",__FUNCTION__,save);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	rv=cxd3778gf_dnc_exit_base_gain_adjustment(save);
	if(rv<0){
		back_trace();
		mutex_unlock(global_mutex);
		return(rv);
	}

	mutex_unlock(global_mutex);

	return(0);
}

/* user_ambient_gain */
int cxd3778gf_put_user_ambient_gain(int index)
{
	int rv;
	unsigned int buf = 0;

	if (!initialized) {
		pr_err("%s: not initialized\n", __func__);
		return -EBUSY;
	}

	if (TYPE_Z == present.board_type) {
		pr_notice("%s: function not supported\n", __func__);
		return 0;
	}

	mutex_lock(global_mutex);

	cxd3778gf_register_read(CXD3778GF_DNC2_AVFCAN, &buf);
	cxd3778gf_register_write(CXD3778GF_DNC2_AVFCAN, 0x11);

	rv = cxd3778gf_dnc_set_user_ambient_gain(index,
						 present.noise_cancel_path);
	if (rv < 0) {
		pr_err("%s: fail\n", __func__);
		goto out_setgain;
	}

	present.ambient_gain = index;

	msleep(160);

out_setgain:
	cxd3778gf_register_write(CXD3778GF_DNC2_AVFCAN, buf);

	mutex_unlock(global_mutex);

	return rv;
}

int cxd3778gf_get_user_ambient_gain(int *index)
{
	mutex_lock(global_mutex);

	if (!initialized) {
		pr_err("not initialized.\n");
		mutex_unlock(global_mutex);
		return -EBUSY;
	}

	*index = present.ambient_gain;

	mutex_unlock(global_mutex);

	return 0;
}

/* nc_ignore_jack_state */
int cxd3778gf_put_nc_ignore_jack_state(int index)
{
	mutex_lock(global_mutex);

	present.nc_ignore_jack_state = index;

	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_get_nc_ignore_jack_state(int *index)
{
	mutex_lock(global_mutex);

	if (!initialized) {
		pr_err("not initialized.\n");
		mutex_unlock(global_mutex);
		return -EBUSY;
	}

	*index = present.nc_ignore_jack_state;

	mutex_unlock(global_mutex);

	return 0;
}

/*****************/
/*@ sound_effect */
/*    OFF 0      */
/*    ON  1      */
/*****************/

int cxd3778gf_put_sound_effect(int value)
{
	unsigned int codec_sr_play1;

	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.sound_effect=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.sound_effect){
		mutex_unlock(global_mutex);
		return(0);
	}

        if(present.sample_rate>192000 || present.format==DSD_MODE){
		/* volume table don't change, so only reserve sound effect value */
		present.sound_effect=value;
                mutex_unlock(global_mutex);
                return(0);
        }

	/* Cannot find zero-cross because upper layer service close the */
	/* stream when changing sound effect. So Disable CODEC_PLAYVOL  */
	/* soft-ramp function to run fader immediately.                 */
	cxd3778gf_register_read(CXD3778GF_CODEC_SR_PLAY1, &codec_sr_play1);
	cxd3778gf_register_modify(CXD3778GF_CODEC_SR_PLAY1,
				  0x00,
				  0x10);

	cxd3778gf_set_output_device_mute(&present,ON,TRUE);

	/*
	 * Adjust wait time of hw_mute
	 * because of changing volume table of Eq/ALC too
	 */
	if (board_set_flag & HW_MUTE_CONSTANT_30MS_FLAG)
		msleep(150);

	cxd3778gf_register_modify(CXD3778GF_CODEC_SR_PLAY1,
				  codec_sr_play1,
				  0x10);

	/********************/

	present.sound_effect=value;

	/********************/

	cxd3778gf_set_master_volume(&present,present.master_volume);

	cxd3778gf_set_output_device_mute(&present,OFF,TRUE);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_sound_effect(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.sound_effect;
	else
		*value=back_up.sound_effect;

	mutex_unlock(global_mutex);

	return(0);
}

/******************/
/*@ master_volume */
/*    0 - 30      */
/******************/

int cxd3778gf_put_master_volume(int value)
{
	int now;
	unsigned int phv_ctrl0 = 0;

	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>MASTER_VOLUME_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.master_volume=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.master_volume){
		mutex_unlock(global_mutex);
		return(0);
	}

#if 0
/* No need for PHV_CTRL0's Zero-cross detection and volume control */
	/********************/

	now=present.master_volume;

	while(value!=now){

#ifdef ICX_ENABLE_VOL60STP
		if(value>now)
			now=minimum(now+3,value);
		else
			now=maximum(now-3,value);
#else
		if(value>now)
			now=minimum(now+1,value);
		else
			now=maximum(now-1,value);
#endif

		cxd3778gf_set_master_volume(&present,now);
		present.master_volume=now;
	}

	/* cxd3778gf_set_master_volume(&present,value); */
	/* present.master_volume=value; */

	/********************/
#endif
	cxd3778gf_register_read(CXD3778GF_PHV_CTRL0, &phv_ctrl0);
	/* Temporal mask time change as countermeasure for pop noise */
	cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0, 0x01, 0x03);

	cxd3778gf_set_master_volume(&present,value);
	present.master_volume=value;

	usleep_range(4000, 4400);
	cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0, phv_ctrl0 & 0x03, 0x03);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_master_volume(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.master_volume;
	else
		*value=back_up.master_volume;

	mutex_unlock(global_mutex);

	return(0);
}

/******************/
/*@ lr_balance_volume */
/*  smaster  0 - 56 (-28db ~ 0db step:0.5db)    */
/*  normal   0 - 88 (-76db ~ 12db step 1db)     */
/******************/

int cxd3778gf_put_l_balance_volume(int value_l)
{
	int now;

        print_trace("%s(L:%d)\n",__FUNCTION__,value_l);

        if(value_l<0 || value_l>L_BALANCE_VOLUME_MAX){
                print_error("invalid parameter Lch, value = %d\n",value_l);
                return(-EINVAL);
        }

	mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

	if(value_l==present.balance_volume_l){
                mutex_unlock(global_mutex);
                return(0);
        }

	present.balance_volume_l=value_l;
	cxd3778gf_set_master_volume(&present,present.master_volume);

        mutex_unlock(global_mutex);

        return(0);
}

int cxd3778gf_put_r_balance_volume(int value_r)
{
	int now;

	print_trace("%s(R:%d)\n",__FUNCTION__,value_r);

	if(value_r<0 || value_r>R_BALANCE_VOLUME_MAX){
		print_error("invalid parameter Rch, value = %d\n",value_r);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(value_r==present.balance_volume_r){
		mutex_unlock(global_mutex);
		return(0);
	}

	present.balance_volume_r=value_r;
	cxd3778gf_set_master_volume(&present,present.master_volume);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_l_balance_volume(int * value_l)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value_l=present.balance_volume_l;

	mutex_unlock(global_mutex);

	return(0);
}


int cxd3778gf_get_r_balance_volume(int * value_r)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value_r=present.balance_volume_r;

	mutex_unlock(global_mutex);

	return(0);
}

/****************/
/*@ master_gain */
/*    0 - 30    */
/****************/

int cxd3778gf_put_master_gain(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>MASTER_GAIN_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.master_gain=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.master_gain){
		mutex_unlock(global_mutex);
		return(0);
	}

	/********************/

	cxd3778gf_set_master_gain(&present,value);
	present.master_gain=value;

	/********************/

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_master_gain(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.master_gain;
	else
		*value=back_up.master_gain;

	mutex_unlock(global_mutex);

	return(0);
}

/******************/
/*@ playback_mute */
/*    OFF 0       */
/*    ON  1       */
/******************/

int cxd3778gf_put_playback_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.playback_mute=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.playback_mute){
		mutex_unlock(global_mutex);
		return(0);
	}

	/********************/

	cxd3778gf_set_playback_mute(&present,value);
	present.playback_mute=value;

	/********************/

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_playback_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.playback_mute;
	else
		*value=back_up.playback_mute;

	mutex_unlock(global_mutex);

	return(0);
}

/*****************/
/*@ capture_mute */
/*    OFF 0      */
/*    ON  1      */
/*****************/

int cxd3778gf_put_capture_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.capture_mute=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.capture_mute){
		mutex_unlock(global_mutex);
		return(0);
	}

	/********************/

	cxd3778gf_set_capture_mute(&present,value);
	present.capture_mute=value;

	/********************/

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_capture_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.capture_mute;
	else
		*value=back_up.capture_mute;

	mutex_unlock(global_mutex);

	return(0);
}

/*************************/
/*@ analog_playback_mute */
/*    OFF 0              */
/*    ON  1              */
/*************************/

int cxd3778gf_put_analog_playback_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.analog_playback_mute=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.analog_playback_mute){
		mutex_unlock(global_mutex);
		return(0);
	}

	/********************/

	if(present.output_device==OUTPUT_DEVICE_SPEAKER
	&& present.icx_playback_active==0
	&& present.std_playback_active==0){
		if(value==OFF)
			switch_speaker_power(ON);
	}

	cxd3778gf_set_analog_playback_mute(&present,value);
	present.analog_playback_mute=value;

	if(present.output_device==OUTPUT_DEVICE_SPEAKER
	&& present.icx_playback_active==0
	&& present.std_playback_active==0){
		if(value==ON)
			switch_speaker_power(OFF);
	}

	/********************/

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_analog_playback_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.analog_playback_mute;
	else
		*value=back_up.analog_playback_mute;

	mutex_unlock(global_mutex);

	return(0);
}

/***********************/
/*@ analog_stream_mute */
/*    OFF 0            */
/*    ON  1            */
/***********************/

int cxd3778gf_put_analog_stream_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.analog_stream_mute=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.analog_stream_mute){
		mutex_unlock(global_mutex);
		return(0);
	}

	/********************/

	cxd3778gf_set_analog_stream_mute(&present,value);
	present.analog_stream_mute=value;

	/********************/

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_analog_stream_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.analog_stream_mute;
	else
		*value=back_up.analog_stream_mute;

	mutex_unlock(global_mutex);

	return(0);
}

/*@ fader mute */
int cxd3778gf_put_icx_fader_mute(int value)
{
	print_trace("%s()\n", __FUNCTION__);
	
	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.fader_mute_sdin1=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.fader_mute_sdin1){
		mutex_unlock(global_mutex);
		return(0);
	}

	cxd3778gf_set_icx_fader_mute(&present,value);
	present.fader_mute_sdin1=value;

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_icx_fader_mute(int * value)
{
	print_trace("%s()\n", __FUNCTION__);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.fader_mute_sdin1;
	else
		*value=back_up.fader_mute_sdin1;

	mutex_unlock(global_mutex);
	return(0);
}

int cxd3778gf_put_std_fader_mute(int value)
{
	print_trace("%s()\n", __FUNCTION__);
	
	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.fader_mute_sdin2=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.fader_mute_sdin2){
		mutex_unlock(global_mutex);
		return(0);
	}

	cxd3778gf_set_std_fader_mute(&present,value);
	present.fader_mute_sdin2=value;

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_std_fader_mute(int * value)
{
	print_trace("%s()\n", __FUNCTION__);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.fader_mute_sdin2;
	else
		*value=back_up.fader_mute_sdin2;

	mutex_unlock(global_mutex);
	return(0);
}

/***********************/
/*@ timed_mute         */
/*    OFF 0            */
/*    TIMEOUT  1- [ms] */
/***********************/

int cxd3778gf_put_timed_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(set_timed_mute(0,value));
}

int cxd3778gf_get_timed_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	return(get_timed_mute(0,value));
}

int cxd3778gf_put_std_timed_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	return(set_timed_mute(1,value));
}

int cxd3778gf_get_std_timed_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	return(get_timed_mute(1,value));
}

int cxd3778gf_put_icx_timed_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);


	return(set_timed_mute(2,value));
}

int cxd3778gf_get_icx_timed_mute(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	return(get_timed_mute(2,value));
}

int cxd3778gf_put_dsd_timed_mute(int value)
{
        print_trace("%s(%d)\n",__FUNCTION__,value);


        return(set_timed_mute(4,value));
}

int cxd3778gf_get_dsd_timed_mute(int * value)
{
        /* print_trace("%s()\n",__FUNCTION__); */

        return(get_timed_mute(4,value));
}

int set_timed_mute(int port, int value)
{
	unsigned long now;
	unsigned long delta;
	int mute;
	int changed;

	print_trace("%s(%d,%d)\n",__FUNCTION__,port,value);

	if(port<0 || port>4){
		print_error("port %d is invalid.\n",port);
		return(-1);
	}

	if(value==0)
		mute=OFF;
	else
		mute=ON;

	mutex_lock(&timed_mute_mutex);

	timed_mute[port].busy=TRUE;

	/* cancel_delayed_work_sync(&timed_mute[port].work); */
	flush_delayed_work(&timed_mute[port].work);

	timed_mute[port].busy=FALSE;

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	changed=FALSE;

	if(!core_active){

		switch(port){

			case 0:
				if(back_up.mix_timed_mute!=mute){
					back_up.mix_timed_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;

			case 1:
				if(back_up.std_timed_mute!=mute){
					back_up.std_timed_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;

			case 2:
				if(back_up.icx_timed_mute!=mute){
					back_up.icx_timed_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;

			case 3:
				if(back_up.uncg_mute!=mute){
					back_up.uncg_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;
                        case 4:
                                if(back_up.dsd_timed_mute!=mute){
                                        back_up.dsd_timed_mute=mute;
                                        changed=TRUE;
                                        print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
                                }
                                break;

		}
	}

	else{

		switch(port){

			case 0:
				if(present.mix_timed_mute!=mute){
					cxd3778gf_set_mix_timed_mute(&present,mute);
					present.mix_timed_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;

			case 1:
				if(present.std_timed_mute!=mute){
					cxd3778gf_set_std_timed_mute(&present,mute);
					present.std_timed_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;

			case 2:
				if(present.icx_timed_mute!=mute){
					cxd3778gf_set_icx_timed_mute(&present,mute);
					present.icx_timed_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;

			case 3:
				if(present.uncg_mute!=mute){
					cxd3778gf_set_uncg_mute(&present,mute);
					present.uncg_mute=mute;
					changed=TRUE;
					print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
				}
				break;
                        case 4:
                                if(present.dsd_timed_mute!=mute){
                                        cxd3778gf_set_dsd_timed_mute(&present,mute);
                                        present.dsd_timed_mute=mute;
                                        changed=TRUE;
                                        print_debug("timed mute %d = %s\n",port,mute?"ON":"OFF");
                                }
                                break;
		}
	}

	mutex_unlock(global_mutex);

	if(value==0){

		timed_mute[port].timeout=0;
		print_debug("timed_mute %d timeout = %lu\n",port,timed_mute[port].timeout);
	}

	else{

		delta=msecs_to_jiffies(value);
		now=jiffies;
		print_debug("now = %lu\n",now);

		print_debug("timed_mute %d timeout = %lu\n",port,timed_mute[port].timeout);

		if(changed || delta > timed_mute[port].timeout-now)
			timed_mute[port].timeout=now+delta;

		print_debug("timed_mute %d timeout = %lu\n",port,timed_mute[port].timeout);

		schedule_delayed_work(&timed_mute[port].work,timed_mute[port].timeout-now);

		print_debug("timed mute %d delayed = %lu\n",port,timed_mute[port].timeout-now);
	}

	mutex_unlock(&timed_mute_mutex);

	return(0);
}

int set_timed_mute_dsd_termination(void)
{
        print_trace("%s\n",__FUNCTION__);

	queue_work(dsd_mute_wq_high, &dsd_mute_termination_work);

	return(0);
}

int cxd3778gf_set_icx_dsd_mode(int dsd)
{
	print_trace("%s()\n", __func__);

	if (dsd)
		cxd3778gf_register_modify(CXD3778GF_SYSTEM, 0x80, 0x80);
	else
		cxd3778gf_register_modify(CXD3778GF_SYSTEM, 0x00, 0x80);

	return 0;
}

static void do_timed_mute_work(struct work_struct * work)
{
	struct timed_mute_data * tmd;
	struct delayed_work * dw;
	int port;

	dw=container_of(work, struct delayed_work, work);
	tmd=container_of(dw, struct timed_mute_data, work);
	port=tmd->port;

	print_trace("%s(%d)\n",__FUNCTION__,port);

	if(tmd->busy){
		print_debug("timed mute %d = busy\n",port);
		return;
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return;
	}

	if(!core_active){

		switch(port){

			case 0:
				if(back_up.mix_timed_mute!=OFF){
					back_up.mix_timed_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;

			case 1:
				if(back_up.std_timed_mute!=OFF){
					back_up.std_timed_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;

			case 2:
				if(back_up.icx_timed_mute!=OFF){
					back_up.icx_timed_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;

			case 3:
				if(back_up.uncg_mute!=OFF){
					back_up.uncg_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;
			case 4:
                                if(back_up.dsd_timed_mute!=OFF){
                                        back_up.dsd_timed_mute=OFF;
                                        print_debug("timed mute %d = OFF\n",port);
                                }
                                break;

		}
	}

	else{

		switch(port){

			case 0:
				if(present.mix_timed_mute!=OFF){
					cxd3778gf_set_mix_timed_mute(&present,OFF);
					present.mix_timed_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;

			case 1:
				if(present.std_timed_mute!=OFF){
					cxd3778gf_set_std_timed_mute(&present,OFF);
					present.std_timed_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;

			case 2:
				if(present.icx_timed_mute!=OFF){
					cxd3778gf_set_icx_timed_mute(&present,OFF);
					present.icx_timed_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;

			case 3:
				if(present.uncg_mute!=OFF){
					cxd3778gf_set_uncg_mute(&present,OFF);
					present.uncg_mute=OFF;
					print_debug("timed mute %d = OFF\n",port);
				}
				break;
                        case 4:
                                if(present.dsd_timed_mute!=OFF){
                                        cxd3778gf_set_dsd_timed_mute(&present,OFF);
                                        present.dsd_timed_mute=OFF;
                                        print_debug("timed mute %d = OFF\n",port);
                                }
                                break;
		}
	}

	mutex_unlock(global_mutex);

	return;
}

static void do_dsd_mute_termination_work(struct work_struct * work)
{
	print_trace("%s\n",__FUNCTION__);

	/*
	 * Adjust mute time for latency low mode
	 * Low latency mode shorten termination mute time (200 -> 100)
	 * So, should delay mute timing
	 */
	if (present.playback_latency == PLAYBACK_LATENCY_LOW)
		msleep(80);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}
	cxd3778gf_set_dsd_remastering_mute(&present,ON);
	present.dsd_remastering_mute = ON;

	mutex_unlock(global_mutex);

	return;
}

static int get_timed_mute(int port, int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */


	if(port<0 || port>4){
		print_error("port %d is invalid.\n",port);
		return(-1);
	}

	mutex_lock(&timed_mute_mutex);
	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		mutex_unlock(&timed_mute_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		if(port==0){
			if(back_up.mix_timed_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else if(port==1){
			if(back_up.std_timed_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else if(port==2){
			if(back_up.icx_timed_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else if(port==3){
			if(back_up.uncg_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else {
                        if(back_up.dsd_timed_mute==OFF)
                                *value=0;
                        else
                                *value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
	}
	else{
		if(port==0){
			if(present.mix_timed_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else if(port==1){
			if(present.std_timed_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else if(port==2){
			if(present.icx_timed_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else if(port==3){
			if(present.uncg_mute==OFF)
				*value=0;
			else
				*value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
		else{
			if(present.dsd_timed_mute==OFF)
                                *value=0;
                        else
                                *value=jiffies_to_msecs(timed_mute[port].timeout-jiffies);
		}
	}

	mutex_unlock(global_mutex);
	mutex_unlock(&timed_mute_mutex);

	return(0);
}

static int cancel_timed_mute(void)
{
	print_trace("%s()\n",__FUNCTION__);

	cancel_delayed_work(&timed_mute[0].work);
	cancel_delayed_work(&timed_mute[1].work);
	cancel_delayed_work(&timed_mute[2].work);
	cancel_delayed_work(&timed_mute[3].work);
	cancel_delayed_work(&timed_mute[4].work);

	return(0);
}

static int flush_timed_mute(void)
{
	print_trace("%s()\n",__FUNCTION__);

	flush_delayed_work(&timed_mute[0].work);
	flush_delayed_work(&timed_mute[1].work);
	flush_delayed_work(&timed_mute[2].work);
	flush_delayed_work(&timed_mute[3].work);
	flush_delayed_work(&timed_mute[4].work);

	return(0);
}

/***********************/
/*@ dsd remastering_mute */
/*    OFF 0            */
/*    ON  1            */
/***********************/

int cxd3778gf_put_dsd_remastering_mute(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.dsd_remastering_mute=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.dsd_remastering_mute){
		mutex_unlock(global_mutex);
		return(0);
	}

	/********************/
	cxd3778gf_set_dsd_remastering_mute(&present,value);
	present.dsd_remastering_mute=value;
	/********************/

	mutex_unlock(global_mutex);

	return(0);
}


int cxd3778gf_get_dsd_remastering_mute(int * value)
{
        /* print_trace("%s()\n",__FUNCTION__); */

        mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

        if(core_active)
                *value=present.dsd_remastering_mute;
        else
                *value=back_up.dsd_remastering_mute;

        mutex_unlock(global_mutex);

        return(0);
}

/*******************************/
/*@ input_device               */
/*    INPUT_DEVICE_NONE      0 */
/*    INPUT_DEVICE_TUNER     1 */
/*    INPUT_DEVICE_MIC       2 */
/*    INPUT_DEVICE_LINE      3 */
/*    INPUT_DEVICE_DIRECTMIC 4 */
/*******************************/

int cxd3778gf_put_input_device(struct cxd3778gf_driver_data *ddata, int value)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>INPUT_DEVICE_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(value!=INPUT_DEVICE_NONE)
		resume_core(cdata, PLUG_DET_MODE_DISABLE);

	if(value==present.input_device){
		mutex_unlock(global_mutex);
		return(0);
	}

	present.input_device = value;

	if (NOISE_CANCEL_MODE_AMBIENT1 == present.noise_cancel_mode ||
	    NOISE_CANCEL_MODE_AMBIENT2 == present.noise_cancel_mode ||
	    NOISE_CANCEL_MODE_AMBIENT3 == present.noise_cancel_mode)
		cxd3778gf_set_output_device_mute(&present, ON, TRUE);

	/* Disable ambient mode */
	if (AMBIENT == present.noise_cancel_active)
		present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_input_device_mute(&present,ON);

	/********************/

	change_input_device(present.input_device);

#ifdef ICX_ENABLE_AU2CLK
	change_external_osc(ON);
#endif
	/********************/

	cxd3778gf_set_master_gain(&present,present.master_gain);

	adjust_device_gain(present.input_device);

	cxd3778gf_set_input_device_mute(&present,OFF);

	if (NOISE_CANCEL_MODE_AMBIENT1 == present.noise_cancel_mode ||
	    NOISE_CANCEL_MODE_AMBIENT2 == present.noise_cancel_mode ||
	    NOISE_CANCEL_MODE_AMBIENT3 == present.noise_cancel_mode) {
		/* Restore DNC ambient if input device is none */
		if (INPUT_DEVICE_NONE == present.input_device) {
			present.noise_cancel_active =
						  cxd3778gf_dnc_judge(&present);
			cxd3778gf_set_master_volume(&present,present.master_volume);
		}

		cxd3778gf_set_output_device_mute(&present, OFF, TRUE);
	}

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_input_device(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.input_device;

	mutex_unlock(global_mutex);

	return(0);
}

/********************************/
/*@ output_device               */
/*    OUTPUT_DEVICE_NONE      0 */
/*    OUTPUT_DEVICE_HEADPHONE 1 */
/*    OUTPUT_DEVICE_LINE      2 */
/*    OUTPUT_DEVICE_SPEAKER   3 */
/*    OUTPUT_DEVICE_FIXEDLINE 4 */
/********************************/

int cxd3778gf_put_output_device(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>OUTPUT_DEVICE_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(value==present.output_device){
		mutex_unlock(global_mutex);
		return(0);
	}

	cxd3778gf_set_output_device_mute(&present,ON,TRUE);

	/********************/

	change_output_device(
		value,
		present.headphone_amp,
		present.analog_playback_mute,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);
	present.output_device=value;

	/********************/

	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_master_volume(&present,present.master_volume);

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

#ifdef ICX_ENABLE_AU2CLK
	if (present.board_type == TYPE_A)
		change_external_osc(OFF);
#endif

	cxd3778gf_set_output_device_mute(&present,OFF,TRUE);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_output_device(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.output_device;

	mutex_unlock(global_mutex);

	return(0);
}

/*******************************/
/*@ headphone_amp                 */
/*    HEADPHONE_AMP_NORMAL      0 */
/*    HEADPHONE_AMP_SMASTER_SE  1 */
/*    HEADPHONE_AMP_SMASTER_BTL 2 */
/*******************************/

int cxd3778gf_put_headphone_amp(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>HEADPHONE_AMP_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.headphone_amp=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.headphone_amp){
		mutex_unlock(global_mutex);
		return(0);
	}

	if(present.output_device!=OUTPUT_DEVICE_HEADPHONE){
		present.headphone_amp=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	cxd3778gf_set_output_device_mute(&present,ON,TRUE);

	/********************/

	change_output_device(
		present.output_device,
		value,
		present.analog_playback_mute,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);
	present.headphone_amp=value;

	/********************/

	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_master_volume(&present,present.master_volume);

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	cxd3778gf_set_output_device_mute(&present,OFF,TRUE);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_headphone_amp(int * value)
{
        /* print_trace("%s()\n",__FUNCTION__); */

        mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

        if(core_active)
                *value=present.headphone_amp;
        else
                *value=back_up.headphone_amp;

        mutex_unlock(global_mutex);

        return(0);
}

/*******************************/
/*@ headphone_smaster_gain_mode            */
/*    HEADPHONE_SMASTER_SE_GAIN_MODE_NORMAL 0 */
/*    HEADPHONE_SMASTER_SE_GAIN_MODE_HIGH   1 */
/*******************************/

int cxd3778gf_put_headphone_smaster_se_gain_mode(int value)
{
        print_trace("%s(%d)\n",__FUNCTION__,value);

        if(value<0 || value>HEADPHONE_SMASTER_SE_GAIN_MODE_MAX){
                print_error("invalid parameter, value = %d\n",value);
                return(-EINVAL);
        }

        mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

        if(value==present.headphone_smaster_se_gain_mode){
                mutex_unlock(global_mutex);
                return(0);
        }

        if(present.output_device!=OUTPUT_DEVICE_HEADPHONE){
                present.headphone_smaster_se_gain_mode=value;
                mutex_unlock(global_mutex);
                return(0);
        }

	present.headphone_smaster_se_gain_mode=value;
	cxd3778gf_set_master_volume(&present,present.master_volume);
        mutex_unlock(global_mutex);

        return(0);
}

int cxd3778gf_get_headphone_smaster_se_gain_mode(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.headphone_smaster_se_gain_mode;

	mutex_unlock(global_mutex);

	return(0);
}

/*******************************/
/*@ headphone_smaster_gain_mode            */
/*    HEADPHONE_SMASTER_BTL_GAIN_MODE_NORMAL 0 */
/*    HEADPHONE_SMASTER_BTL_GAIN_MODE_HIGH   1 */
/*******************************/

int cxd3778gf_put_headphone_smaster_btl_gain_mode(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>HEADPHONE_SMASTER_BTL_GAIN_MODE_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(value==present.headphone_smaster_btl_gain_mode){
		mutex_unlock(global_mutex);
		return(0);
	}

	if(present.output_device!=OUTPUT_DEVICE_HEADPHONE){
		present.headphone_smaster_btl_gain_mode=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	present.headphone_smaster_btl_gain_mode=value;
	cxd3778gf_set_master_volume(&present,present.master_volume);
	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_headphone_smaster_btl_gain_mode(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.headphone_smaster_btl_gain_mode;

	mutex_unlock(global_mutex);

	return(0);
}

/*************************/
/*@ headphone_type       */
/*    NCHP_TYPE_NW510N 0 */
/*    NCHP_TYPE_OTHER  1 */
/****************************/

int cxd3778gf_put_headphone_type(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>NCHP_TYPE_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.headphone_type=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.headphone_type){
		mutex_unlock(global_mutex);
		return(0);
	}

	cxd3778gf_set_output_device_mute(&present,ON,TRUE);

	/********************/

	present.headphone_type=value;

	/********************/

	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	cxd3778gf_set_output_device_mute(&present,OFF,TRUE);

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_headphone_type(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.headphone_type;
	else
		*value=back_up.headphone_type;

	mutex_unlock(global_mutex);

	return(0);
}

/****************************/
/*@ jack_mode               */
/*    JACK_MODE_HEADPHONE 0 */
/*    JACK_MDOE_ANTENNA   1 */
/****************************/

int cxd3778gf_put_jack_mode(int value)
{
	int status;

	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value<0 || value>JACK_MODE_MAX){
		print_error("invalid parameter, value = %d\n",value);
		return(-EINVAL);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(!core_active){
		back_up.jack_mode=value;
		mutex_unlock(global_mutex);
		return(0);
	}

	if(value==present.jack_mode){
		mutex_unlock(global_mutex);
		return(0);
	}

	present.jack_mode=value;

	status=get_jack_status_se(present.noise_cancel_active,present.jack_mode); 

	if (status >= 0){
		report_jack_status_se(
			present.jack_status_se,
			status,
			FALSE
		);

		present.jack_status_se=status;
	}

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_jack_mode(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(core_active)
		*value=present.jack_mode;
	else
		*value=back_up.jack_mode;

	mutex_unlock(global_mutex);

	return(0);
}

/****************/
/*@ jack_status_se */
/****************/

int cxd3778gf_get_jack_status_se(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.jack_status_se;

	mutex_unlock(global_mutex);

	return(0);
}

/****************/
/*@ jack_status_btl */
/****************/

int cxd3778gf_get_jack_status_btl(int * value)
{
        /* print_trace("%s()\n",__FUNCTION__); */

        mutex_lock(global_mutex);

        if(!initialized){
                print_error("not initialized.\n");
                mutex_unlock(global_mutex);
                return(-EBUSY);
        }

        *value=present.jack_status_btl;

        mutex_unlock(global_mutex);

        return(0);
}
/************************/
/*@ register_dnc_module */
/************************/

int cxd3778gf_register_dnc_module(struct cxd3778gf_dnc_interface * interface)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(global_mutex==NULL){
		/* print_error("not initialized."); */
		return(-EBUSY);
	}

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	/* TYPE_Z don't have NC function */
	if(present.board_type==TYPE_Z){
		print_error("not support dnc.\n");
		mutex_unlock(global_mutex);
		return(-ENODEV);
	}

	if(interface!=NULL){

		interface->set_mic_bias_mode      = set_mic_bias_mode;
		interface->enable_sd2_clk         = enable_sd2_clock;
		interface->switch_dnc_power       = switch_dnc_power;
		interface->switch_ncmic_amp_power = switch_ncmic_amp_power;
		interface->modify_reg             = cxd3778gf_register_modify;
		interface->write_reg              = cxd3778gf_register_write;
		interface->read_reg               = cxd3778gf_register_read;
		interface->write_buf              = cxd3778gf_register_write_multiple;
		interface->read_buf               = cxd3778gf_register_read_multiple;
		interface->global_mutex           = global_mutex;
		interface->use_ext_bus            = cxd3778gf_register_use_ext_bus;
		interface->ext_reset              = cxd3778gf_ext_reset;
		interface->ext_start_fmonitor     = cxd3778gf_ext_start_fmonitor;
		interface->ext_stop_fmonitor      = cxd3778gf_ext_stop_fmonitor;
		interface->ext_set_gain_index     = cxd3778gf_ext_set_gain_index;
		interface->ext_restore_preamp     = cxd3778gf_ext_restore_preamp;

		cxd3778gf_dnc_register_module(interface);

		rv=cxd3778gf_dnc_initialize();
		if(rv<0){
			back_trace();
			mutex_unlock(global_mutex);
			return(rv);
		}

		/********/

		present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

		cxd3778gf_set_master_volume(&present,present.master_volume);

		cxd3778gf_set_master_gain(&present,present.master_gain);

		adjust_tone_control(
			present.output_device,
			present.headphone_amp,
			present.headphone_type,
			present.jack_status_se
		);

		present.pcm_monitoring=judge_pcm_monitoring(
			present.output_device,
			present.headphone_amp,
			present.noise_cancel_active,
			present.analog_playback_mute
		);

		/********/
	}

	else{
		cxd3778gf_dnc_shutdown();

		cxd3778gf_dnc_register_module(interface);

		/********/

		present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

		cxd3778gf_set_master_volume(&present,present.master_volume);

		cxd3778gf_set_master_gain(&present,present.master_gain);

		adjust_tone_control(
			present.output_device,
			present.headphone_amp,
			present.headphone_type,
			present.jack_status_se
		);

		present.pcm_monitoring=judge_pcm_monitoring(
			present.output_device,
			present.headphone_amp,
			present.noise_cancel_active,
			present.analog_playback_mute
		);

		/********/
	}

	mutex_unlock(global_mutex);

	return(0);
}
EXPORT_SYMBOL(cxd3778gf_register_dnc_module);

/**********************/
/*@ check_jack_status_se */
/**********************/

int cxd3778gf_check_jack_status_se(int force)
{
	int status;
	int backup;
	int rv;
	unsigned int ui;

	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	status=get_jack_status_se(present.noise_cancel_active,present.jack_mode);

	/* for electrostatic damege. */
#if 0
	while(1){
		rv=cxd3778gf_register_read(CXD3778GF_DAC,&ui);
		if(rv!=0 || (ui&0x40)==0x40)
			break;

		print_info("detect electrostatic damages.\n");
		suspend_core();
		resume_core();
		if(repair_electrostatic_damage!=NULL)
			repair_electrostatic_damage();
		status=get_jack_status_se(present.noise_cancel_active,present.jack_mode);
	}
#endif
	if (status >= 0){
		report_jack_status_se(
			present.jack_status_se,
			status,
			force
		);

		backup=present.jack_status_se;
		present.jack_status_se=status;

//			if( (present.jack_status_se==JACK_STATUS_SE_5PIN && (backup==JACK_STATUS_SE_3PIN || backup==JACK_STATUS_SE_4PIN))
//			||  ((present.jack_status_se==JACK_STATUS_SE_3PIN || present.jack_status_se==JACK_STATUS_SE_4PIN) && backup==JACK_STATUS_SE_5PIN) ){

		if(backup != status) {
			if(present.output_device==OUTPUT_DEVICE_HEADPHONE){
				int muted = 0;
				if(present.noise_cancel_mode) {
					if ((present.noise_cancel_active && status != JACK_STATUS_SE_5PIN) ||
						(!present.noise_cancel_active && status == JACK_STATUS_SE_5PIN)) {
						cxd3778gf_set_output_device_mute(&present,ON,TRUE);
						muted = 1;
					}
				}

				adjust_tone_control(
					present.output_device,
					present.headphone_amp,
					present.headphone_type,
					present.jack_status_se
				);

				cxd3778gf_set_master_volume(&present,present.master_volume);

				present.pcm_monitoring=judge_pcm_monitoring(
					present.output_device,
					present.headphone_amp,
					present.noise_cancel_active,
					present.analog_playback_mute
				);

				if(muted) {
					cxd3778gf_set_output_device_mute(&present,OFF,TRUE);
				}
			}
			if(present.jack_status_se!=JACK_STATUS_SE_NONE || present.jack_status_btl!=JACK_STATUS_BTL_NONE)
				cxd3778gf_set_no_pcm_mute(OFF);
			else
				cxd3778gf_set_no_pcm_mute(ON);

			if (present.inactive_dnc_block == DNC1_INACTIVE)
				disable_dnc1_block();

			if (present.inactive_dnc_block == DNC2_INACTIVE)
				disable_dnc2_block();
		}
	}

	mutex_unlock(global_mutex);

	return(0);
}

/**********************/
/*@ check_jack_status_se */
/**********************/

int cxd3778gf_check_jack_status_btl(int force)
{
	int status;
	int backup;
	int rv;
	unsigned int ui;

	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	status=get_jack_status_btl();

	if (status >= 0){
		report_jack_status_btl(
			present.jack_status_btl,
			status,
			force
		);

		backup=present.jack_status_btl;
		present.jack_status_btl=status;

		if(backup != status) {
			if(present.jack_status_se!=JACK_STATUS_SE_NONE || present.jack_status_btl!=JACK_STATUS_BTL_NONE)
				cxd3778gf_set_no_pcm_mute(OFF);
			else
				cxd3778gf_set_no_pcm_mute(ON);
		}
	}

	mutex_unlock(global_mutex);

	return(0);
}

/*********************/
/*@ handle_pcm_event */
/*********************/

int cxd3778gf_handle_pcm_event(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	if(present.pcm_monitoring==OFF)
		return(0);

	rv=cxd3778gf_get_xpcm_det_value();
	if(rv){
		/* mute off */
		schedule_delayed_work(&cdata->pcm_event_work, 0);
	}
	else{
		/* mute on */
		schedule_delayed_work(&cdata->pcm_event_work,
				      msecs_to_jiffies(PWM_OUT_MUTE_DEALY_2));
	}

	return(0);
}

static void do_pcm_event_work(struct work_struct * work)
{
	int rv;

	print_trace("%s()\n",__FUNCTION__);

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return;
	}

	if(!core_active){
		mutex_unlock(global_mutex);
		return;
	}

	if(present.pcm_monitoring==OFF){
		mutex_unlock(global_mutex);
		return;
	}

	if(present.output_device==OUTPUT_DEVICE_HEADPHONE && (present.headphone_amp==HEADPHONE_AMP_SMASTER_SE || present.headphone_amp==HEADPHONE_AMP_SMASTER_BTL)){
		rv=cxd3778gf_get_xpcm_det_value();
		if(rv)
			cxd3778gf_set_no_pcm_mute(OFF);
		else
			cxd3778gf_set_no_pcm_mute(ON);
	}
	else{
		cxd3778gf_set_no_pcm_mute(ON);
	}

	mutex_unlock(global_mutex);

	return;
}

/************************/
/*@ handle_hp_det_event */
/************************/
int cxd3778gf_handle_hp_det_event(struct cxd3778gf_driver_data *ddata)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;
        int rv;

        print_trace("%s()\n",__FUNCTION__);

	pm_wakeup_event(ddata->codec->dev, 10000);

	/* hp det */
	schedule_delayed_work(
		&cdata->hp_det_event_work,
		msecs_to_jiffies(cxd3778gf_get_hp_debounce_interval()));

        return(0);
}


static void do_hp_det_event_work(struct work_struct * work)
{
	struct cxd3778gf_control_data *cdata =
		container_of(work,
			     struct cxd3778gf_control_data,
			     hp_det_event_work.work);
	struct cxd3778gf_driver_data *ddata = cdata->driver_data;
	int rv;
	int force=TRUE;

	print_trace("%s()\n",__FUNCTION__);

	if(!initialized){
		print_error("not initialized.\n");
		return;
	}

	cxd3778gf_check_jack_status_se(force);

	if (present.board_type == TYPE_Z)
		cxd3778gf_check_jack_status_btl(force);

	return;
}
/***********************/
/*@ apply_table_change */
/***********************/

int cxd3778gf_apply_table_change(int id)
{
	print_trace("%s(%d)\n",__FUNCTION__,id);

	if(!core_active)
		return(0);

	if(id==TABLE_ID_MASTER_VOLUME){
		cxd3778gf_set_master_volume(&present,present.master_volume);
	}
	else if(id==TABLE_ID_DEVICE_GAIN){
		adjust_device_gain(present.input_device);
	}
	else if(id==TABLE_ID_TONE_CONTROL){
		adjust_tone_control(
			present.output_device,
			present.headphone_amp,
			present.headphone_type,
			present.jack_status_se
		);
	}

	return(0);
}

/*************************/
/*@ electrostatic_damage */
/*************************/

void cxd3778gf_register_electrostatic_damage_repairer(int (*function)(void))
{
	print_trace("%s(0x%08X)\n",__FUNCTION__,(unsigned int)function);

	if(global_mutex==NULL)
		return;

	mutex_lock(global_mutex);

	repair_electrostatic_damage=function;

	mutex_unlock(global_mutex);

	return;
}
EXPORT_SYMBOL(cxd3778gf_register_electrostatic_damage_repairer);

/***************/
/*@ debug_test */
/***************/

int cxd3778gf_put_headphone_detect_mode(struct cxd3778gf_driver_data *ddata,
					int value)
{
	struct cxd3778gf_control_data *cdata =
		(struct cxd3778gf_control_data *)ddata->control_data;

	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	if(value==HEADPHONE_DETECT_POLLING || value==HEADPHONE_DETECT_SELECT){
		cxd3778gf_interrupt_finalize(ddata,
					     present.board_type,
					     present.headphone_detect_mode);
		cxd3778gf_timer_initialize(present.board_type, value);
	}
	else{
		cancel_delayed_work_sync(&cdata->hp_det_event_work);
		cxd3778gf_timer_finalize(present.board_type, present.headphone_detect_mode);
		cxd3778gf_interrupt_initialize(ddata,
					       present.board_type,
					       value);
		cxd3778gf_check_jack_status_se(TRUE);
		if (present.board_type == TYPE_Z)
			cxd3778gf_check_jack_status_btl(TRUE);
	}

	present.headphone_detect_mode=value;

	return(0);
}

int cxd3778gf_get_headphone_detect_mode(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	if(!initialized){
		print_error("not initialized.\n");
		mutex_unlock(global_mutex);
		return(-EBUSY);
	}

	*value=present.headphone_detect_mode;

	mutex_unlock(global_mutex);

	return(0);
}

/* @playback latency */

int cxd3778gf_put_playback_latency(int value)
{
	int ret;
	pr_debug("%s: called(%d)\n", __func__, value);

	mutex_lock(global_mutex);
	present.playback_latency = value;
	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_get_playback_latency(int *value)
{
	pr_debug("%s: called\n", __func__);

	mutex_lock(global_mutex);
	*value = present.playback_latency;
	pr_debug("%s: cur_value=%d\n", __func__, *value);
	mutex_unlock(global_mutex);

	return 0;
}

int cxd3778gf_debug_test = 0;

int cxd3778gf_put_debug_test(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	mutex_lock(global_mutex);

	cxd3778gf_debug_test=value;

	mutex_unlock(global_mutex);

	return(0);
}

int cxd3778gf_get_debug_test(int * value)
{
	/* print_trace("%s()\n",__FUNCTION__); */

	mutex_lock(global_mutex);

	*value=cxd3778gf_debug_test;

	mutex_unlock(global_mutex);

	return(0);
}

/********************/
/*@ common_routines */
/********************/

static int suspend_core(bool plug_det_mode)
{
	print_trace("%s()\n",__FUNCTION__);

	if(!core_active)
		return(0);

	core_active=FALSE;

	/**********/

	back_up=present;

	cxd3778gf_set_output_device_mute(&present,ON,TRUE);
	cxd3778gf_set_input_device_mute(&present,ON);

	cxd3778gf_set_mix_timed_mute(&present,ON);
	present.mix_timed_mute = ON;
	cxd3778gf_set_std_timed_mute(&present,ON);
	present.std_timed_mute = ON;
	cxd3778gf_set_icx_timed_mute(&present,ON);
	present.icx_timed_mute = ON;
	cxd3778gf_set_dsd_timed_mute(&present,ON);
	present.dsd_timed_mute = ON;
	cxd3778gf_set_uncg_mute(&present,ON);
	present.uncg_mute = ON;

	cxd3778gf_set_dsd_remastering_mute(&present,ON);
        present.dsd_remastering_mute = ON;

	cxd3778gf_set_analog_stream_mute(&present,ON);
	present.analog_stream_mute=ON;

	cxd3778gf_set_analog_playback_mute(&present,ON);
	present.analog_playback_mute=ON;

	cxd3778gf_set_playback_mute(&present,ON);
	present.playback_mute=ON;

	cxd3778gf_set_capture_mute(&present,ON);
	present.capture_mute=ON;

	change_output_device(
		OUTPUT_DEVICE_NONE,
		HEADPHONE_AMP_NORMAL,
		present.analog_playback_mute,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);
	present.output_device=OUTPUT_DEVICE_NONE;
	present.headphone_amp=HEADPHONE_AMP_NORMAL;

	change_input_device(
		INPUT_DEVICE_NONE
	);
	present.input_device=INPUT_DEVICE_NONE;

	present.noise_cancel_mode = NOISE_CANCEL_MODE_OFF;
	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_master_volume(&present,0);
	present.master_gain=0;

	cxd3778gf_set_master_gain(&present,0);
	present.master_volume=0;

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	adjust_device_gain(present.input_device);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	/**********/

	cxd3778gf_dnc_cleanup();

	if (!plug_det_mode)
		shutdown_chip();


//	digiamp_switch_shunt_mute(OFF);

	return(0);
}

static int resume_core(struct cxd3778gf_control_data *cdata,
		       bool plug_det_mode)
{
	print_trace("%s()\n",__FUNCTION__);
	int status;
	int backup;

	if(core_active)
		return(0);

//	digiamp_switch_shunt_mute(ON);

	if (!plug_det_mode)
		startup_chip(present.sample_rate);

	cxd3778gf_dnc_prepare();

	/**********/

	present.sound_effect = back_up.sound_effect;
	/* present.jack_mode = back_up.jack_mode; */

	cxd3778gf_set_mix_timed_mute(&present,back_up.mix_timed_mute);
	present.mix_timed_mute = back_up.mix_timed_mute;
	cxd3778gf_set_std_timed_mute(&present,back_up.std_timed_mute);
	present.std_timed_mute = back_up.std_timed_mute;
	cxd3778gf_set_icx_timed_mute(&present,back_up.icx_timed_mute);
	present.icx_timed_mute = back_up.icx_timed_mute;
	cxd3778gf_set_dsd_timed_mute(&present,back_up.dsd_timed_mute);
	present.dsd_timed_mute = back_up.dsd_timed_mute;
	cxd3778gf_set_uncg_mute(&present,back_up.uncg_mute);
	present.uncg_mute = back_up.uncg_mute;

	cxd3778gf_set_dsd_remastering_mute(&present,back_up.dsd_remastering_mute);
        present.dsd_remastering_mute = back_up.dsd_remastering_mute;

	cxd3778gf_set_analog_stream_mute(&present,back_up.analog_stream_mute);
	present.analog_stream_mute=back_up.analog_stream_mute;

	cxd3778gf_set_analog_playback_mute(&present,back_up.analog_playback_mute);
	present.analog_playback_mute=back_up.analog_playback_mute;

	cxd3778gf_set_playback_mute(&present,back_up.playback_mute);
	present.playback_mute=back_up.playback_mute;

	cxd3778gf_set_capture_mute(&present,back_up.capture_mute);
	present.capture_mute=back_up.capture_mute;

	change_output_device(
		back_up.output_device,
		back_up.headphone_amp,
		present.analog_playback_mute,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);
	present.output_device=back_up.output_device;
	present.headphone_amp=back_up.headphone_amp;

	change_input_device(
		back_up.input_device
	);
	present.input_device=back_up.input_device;

	adjust_tone_control(
		present.output_device,
		present.headphone_amp,
		present.headphone_type,
		present.jack_status_se
	);

	present.noise_cancel_mode = back_up.noise_cancel_mode;

	present.noise_cancel_active = cxd3778gf_dnc_judge(&present);

	cxd3778gf_set_master_volume(&present,back_up.master_volume);
	present.master_volume=back_up.master_volume;

	cxd3778gf_set_master_gain(&present,back_up.master_gain);
	present.master_gain=back_up.master_gain;

	adjust_device_gain(present.input_device);

	present.pcm_monitoring=judge_pcm_monitoring(
		present.output_device,
		present.headphone_amp,
		present.noise_cancel_active,
		present.analog_playback_mute
	);

	/* previous setting before playback for reducing hardware mute */
	if(present.format==DSD_MODE)
		dsdif_pass_change();

#ifdef ICX_ENABLE_AU2CLK
	if (present.board_type == TYPE_A)
		change_external_osc(OFF);
#endif

	if (present.inactive_dnc_block == DNC1_INACTIVE)
		disable_dnc1_block();

	if (present.inactive_dnc_block == DNC2_INACTIVE)
		disable_dnc2_block();

	cxd3778gf_set_input_device_mute(&present,OFF);
	cxd3778gf_set_output_device_mute(&present,OFF,TRUE);

	if (NC_ACTIVE == present.noise_cancel_active ||
	    AMBIENT == present.noise_cancel_active) {
		cxd3778gf_dnc_mute_dnc1monvol(OFF);
		msleep(200);
	}

	/**********/

	core_active=TRUE;

	if (present.headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT &&
	    !plug_det_mode) {
		schedule_delayed_work(&cdata->hp_det_event_work, 10);
	}

	return(0);
}

static int wakeup_chip(unsigned int mode)
{
	print_trace("%s() mode:%d\n", __func__, mode);

	if (mode) {
		/* set RC, MCLK1 OSC on */
		if (present.board_type == TYPE_Z)
			cxd3778gf_set_480clk_enable();
		else
			cxd3778gf_register_modify(CXD3778GF_OSC_ON, 0x11, 0x31);
		/* disable ADC clock */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x00, 0x01);
		msleep(30);
		/* enable RC, MCLK1 Xtal OSC */
		cxd3778gf_register_modify(CXD3778GF_OSC_EN, 0x11, 0x31);
		/* select MCLK1, Xtal OSC */
		cxd3778gf_register_modify(CXD3778GF_OSC_SEL, 0x01, 0x11);
		/* disable RC OSC */
		cxd3778gf_register_modify(CXD3778GF_OSC_EN, 0x00, 0x01);
		cxd3778gf_register_modify(CXD3778GF_OSC_ON, 0x00, 0x01);
		/* power on */
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_BLK_ON0,
						  STUP_REG_BLK_ON0_TP_Z, 0x0F);
		else
			cxd3778gf_register_modify(CXD3778GF_BLK_ON0,
						  STUP_REG_BLK_ON0, 0x0F);
		/* enable clock */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0,
					  STUP_REG_CLK_EN0, 0x17);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2,
					  STUP_REG_CLK_EN2, 0x40);
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_CLK_EN2,
						  STUP_REG_CLK_EN2_TP_Z, 0x10);
	} else {
		/* enable RC OSC */
		cxd3778gf_register_modify(CXD3778GF_OSC_ON, 0x01, 0x01);
		msleep(30);
		cxd3778gf_register_modify(CXD3778GF_OSC_EN, 0x01, 0x01);
		/* disable all clocks */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x00, 0xFF);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x00, 0xFF);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2, 0x00, 0xFF);
		/* disable power except for mic bias */
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0, 0x01, 0xFF);
		/* select RC OSC */
		cxd3778gf_register_modify(CXD3778GF_OSC_SEL, 0x00, 0x01);
		/* disable Xtal OSC */
		cxd3778gf_register_modify(CXD3778GF_OSC_EN, 0x00, 0x30);
		cxd3778gf_register_modify(CXD3778GF_OSC_ON, 0x00, 0x30);
		if (present.board_type == TYPE_Z)
			cxd3778gf_set_441_480clk_disable();
		/* enable 8bit ADC */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x01, 0x01);
	}

	return 0;
}

static int change_output_device(
	int output_device,
	int headphone_amp,
	int analog_playback_mute,
	int icx_playback_active,
	int std_playback_active,
	int capture_active,
	unsigned int sample_rate
)
{
#ifdef ICX_ENABLE_AU2CLK
	unsigned int osc;
#endif

	print_trace(
		"%s(%d,%d,%d,%d,%d,%d,%u)\n",
		__FUNCTION__,
		output_device,
		headphone_amp,
		analog_playback_mute,
		icx_playback_active,
		std_playback_active,
		capture_active,
		sample_rate
	);

	/* for MCLK change */
	cxd3778gf_dnc_off(&present);

	if (output_device == OUTPUT_DEVICE_HEADPHONE) {
		wakeup_chip(1);

		cxd3778gf_register_write(CXD3778GF_SMS_PWM_PHDLY,
			 cxd3778gf_get_pwm_phdly(headphone_amp));

		if(headphone_amp==HEADPHONE_AMP_SMASTER_SE){
			/* --- OFF --- */
			switch_speaker_power(OFF);
			switch_speaker(OFF);
			switch_lineout(OFF);
			switch_classh(OFF);
			switch_dac(OFF);
			switch_cpclk(OFF);
			switch_hpout3(OFF);
			/* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
			change_external_osc(OFF);
#endif
			/* --- ON --- */
			switch_smaster(ON);
			switch_hpout2(ON);
		}else if(headphone_amp==HEADPHONE_AMP_SMASTER_BTL){
			/* --- OFF --- */
                        switch_speaker_power(OFF);
                        switch_speaker(OFF);
                        switch_lineout(OFF);
                        switch_classh(OFF);
                        switch_dac(OFF);
                        switch_cpclk(OFF);
                        switch_hpout2(OFF);
                        /* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
			change_external_osc(OFF);
#endif
                        /* --- ON --- */
                        switch_smaster(ON);
                        switch_hpout3(ON);
		}else{
			/* --- OFF --- */
			switch_speaker_power(OFF);
			switch_speaker(OFF);
			switch_lineout(OFF);
			switch_smaster(OFF);
			switch_hpout2(OFF);
			switch_hpout3(OFF);
			/* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
			change_external_osc(OFF);
#endif
			/* --- ON --- */
			switch_cpclk(ON);
			switch_dac(ON);
			switch_classh(ON);
		}
	} else if (output_device == OUTPUT_DEVICE_SPEAKER) {
		/* TODO: implement wake up sequence */
		pr_warn("Unsupported output_device:OUTPUT_DEVICE_SPEAKER\n");
		/* --- OFF --- */
		if(icx_playback_active==0 && std_playback_active==0 && analog_playback_mute==ON)
			switch_speaker_power(OFF);
		switch_lineout(OFF);
		switch_classh(OFF);
		switch_smaster(OFF);
		switch_hpout2(OFF);
                switch_hpout3(OFF);
		/* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
		change_external_osc(OFF);
#endif
		/* --- ON --- */
		switch_cpclk(ON);
		switch_dac(ON);
		switch_speaker(ON);
		if(icx_playback_active>0 || std_playback_active>0 || analog_playback_mute==OFF)
			switch_speaker_power(ON);
	} else if (output_device == OUTPUT_DEVICE_LINE) {
		/* TODO: implement wake up sequence */
		pr_warn("Unsupported output_device:OUTPUT_DEVICE_LINE\n");
		/* --- OFF --- */
		switch_speaker_power(OFF);
		switch_speaker(OFF);
		switch_classh(OFF);
		switch_smaster(OFF);
		switch_hpout2(OFF);
                switch_hpout3(OFF);
		/* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
		change_external_osc(OFF);
#endif
		/* --- ON --- */
		switch_cpclk(ON);
		switch_dac(ON);
		switch_lineout(ON);
	} else if (output_device == OUTPUT_DEVICE_FIXEDLINE) {
		/* TODO: implement wake up sequence */
		pr_warn("Unsupported output_device:OUTPUT_DEVICE_FIXEDLINE\n");
		/* --- OFF --- */
		switch_speaker_power(OFF);
		switch_speaker(OFF);
		switch_classh(OFF);
		switch_smaster(OFF);
		switch_hpout2(OFF);
                switch_hpout3(OFF);
                /* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
		change_external_osc(OFF);
#endif
		/* --- ON --- */
		switch_cpclk(ON);
		switch_dac(ON);
		switch_lineout(ON);
	} else { /* NONE */
		/* --- OFF --- */
		switch_speaker_power(OFF);
		switch_speaker(OFF);
		switch_lineout(OFF);
		switch_classh(OFF);
		switch_smaster(OFF);
		switch_hpout2(OFF);
                switch_hpout3(OFF);
		switch_dac(OFF);
		switch_cpclk(OFF);
		/* --- CLK --- */
#ifdef ICX_ENABLE_AU2CLK
		change_external_osc(OFF);
#endif

		cxd3778gf_register_write(CXD3778GF_SMS_PWM_PHDLY,
				     cxd3778gf_get_pwm_phdly(-1));

		pcmif_pass_change();

		wakeup_chip(0);
	}

	return(0);
}

static int change_input_device(int input_device)
{
	print_trace("%s(%d)\n",__FUNCTION__,input_device);

	switch(input_device){

		case INPUT_DEVICE_LINE:
			switch_tuner(OFF);
			switch_dmic(OFF);
			switch_linein(ON);
			break;

		case INPUT_DEVICE_MIC:
			switch_tuner(OFF);
			switch_linein(OFF);
			switch_dmic(ON);
			break;

		case INPUT_DEVICE_TUNER:
			switch_linein(OFF);
			switch_dmic(OFF);
			switch_tuner(ON);
			break;

		case INPUT_DEVICE_DIRECTMIC:
			switch_tuner(OFF);
			switch_linein(OFF);
			switch_dmic(ON);
			break;

		/* NONE */
		default:
			switch_tuner(OFF);
			switch_linein(OFF);
			switch_dmic(OFF);
			break;
	}

	return(0);
}

static int change_sample_rate(unsigned int rate)
{
	print_trace("%s(%u)\n",__FUNCTION__,rate);

	cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,0x00,0x04);

	if(present.icx_i2s_mode == SND_SOC_DAIFMT_CBM_CFM){
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x00,0x01);
		cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x00,0x01);
	}

	if (rate <= 48000) {
		cxd3778gf_register_modify(CXD3778GF_SD1_MODE,
							0x00, 0x07);
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_DNC_INTP,
							0x02, 0x06);
	} else if (rate <= 96000) {
		cxd3778gf_register_modify(CXD3778GF_SD1_MODE,
							0x01, 0x07);
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_DNC_INTP,
							0x02, 0x06);
	} else if (rate <= 192000) {
		cxd3778gf_register_modify(CXD3778GF_SD1_MODE,
							0x02, 0x07);
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_DNC_INTP,
							0x02, 0x06);
	} else {
		cxd3778gf_register_modify(CXD3778GF_SD1_MODE,
							0x03, 0x07);
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_DNC_INTP,
							0x04, 0x06);
	}

	if(present.icx_i2s_mode == SND_SOC_DAIFMT_CBM_CFM){
		cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x01,0x01);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x01,0x01);
	}

	cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,0x04,0x04);
	return(0);
}

static int change_sample_rate_std(unsigned int rate)
{
        print_trace("%s(%u)\n",__FUNCTION__,rate);

	cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,0x00,0x40);

	if (present.std_i2s_mode == SND_SOC_DAIFMT_CBM_CFM) {
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x00,0x02);
		cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x00,0x02);
	}

	if (rate <= 48000)
		cxd3778gf_register_modify(CXD3778GF_SD2_MODE, 0x00, 0x07);
	else if (rate <= 96000)
		cxd3778gf_register_modify(CXD3778GF_SD2_MODE, 0x01, 0x07);
	else if (rate <= 192000)
		cxd3778gf_register_modify(CXD3778GF_SD2_MODE, 0x02, 0x07);
	else
		cxd3778gf_register_modify(CXD3778GF_SD2_MODE, 0x03, 0x07);

	if (present.std_i2s_mode == SND_SOC_DAIFMT_CBM_CFM) {
		cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x02,0x02);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x02,0x02);
	}

	cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,0x40,0x40);
        return(0);
}

static int judge_pcm_monitoring(
	int output_device,
	int headphone_amp,
	int noise_cancel_active,
	int analog_playback_mute
)
{
	int judge=0;
	int rv;

	print_trace(
		"%s(%d,%d,%d,%d)\n",
		__FUNCTION__,
		output_device,
		headphone_amp,
		noise_cancel_active,
		analog_playback_mute
	);
#if 0
	judge=OFF;

	if(output_device!=OUTPUT_DEVICE_HEADPHONE)
		judge=OFF;

	if(headphone_amp!=HEADPHONE_AMP_SMASTER_SE && headphone_amp!=HEADPHONE_AMP_SMASTER_BTL)
		judge=OFF;

	if(analog_playback_mute==OFF)
		judge=OFF;

	if(noise_cancel_active==ON)
		judge=OFF;

	if(judge==ON){

		print_debug("pcm monitoring = on\n");

		rv=cxd3778gf_get_xpcm_det_value();
		if(rv)
			cxd3778gf_set_no_pcm_mute(OFF);
		else
			cxd3778gf_set_no_pcm_mute(ON);
	}

	else{
		cancel_delayed_work(&pcm_event_work);

		print_debug("pcm monitoring = off\n");

		if(output_device==OUTPUT_DEVICE_HEADPHONE && (headphone_amp==HEADPHONE_AMP_SMASTER_SE || headphone_amp==HEADPHONE_AMP_SMASTER_BTL))
			cxd3778gf_set_no_pcm_mute(OFF);
		else
			cxd3778gf_set_no_pcm_mute(ON);
	}
#endif
	return(judge);
}

static int adjust_tone_control(int output_device, int headphone_amp,
			     int headphone_type, int jack_status_se)
{
	int table;
	int rv;
	int n;

	print_trace("%s(%d,%d,%d,%d)\n", __func__,
		    output_device, headphone_amp,
		    headphone_type, jack_status_se);

	if (present.board_type != TYPE_A)
		return 0;

	switch (output_device) {
	case OUTPUT_DEVICE_HEADPHONE:
		switch (jack_status_se) {
		case JACK_STATUS_SE_5PIN:
			if (headphone_amp == HEADPHONE_AMP_SMASTER_SE ||
			    headphone_amp == HEADPHONE_AMP_SMASTER_BTL) {
				if (headphone_type == NCHP_TYPE_NW510N)
					table = TONE_CONTROL_TABLE_SAMP_NW510N_NCHP;
				else
					table = TONE_CONTROL_TABLE_SAMP_GENERAL_HP;
			}
			break;
		case JACK_STATUS_SE_ANTENNA:
		case JACK_STATUS_SE_3PIN:
		case JACK_STATUS_SE_4PIN:
		case JACK_STATUS_SE_NONE:
		default:
			if (headphone_amp == HEADPHONE_AMP_SMASTER_SE ||
			    headphone_amp == HEADPHONE_AMP_SMASTER_BTL)
				table = TONE_CONTROL_TABLE_SAMP_GENERAL_HP;
			break;
		}
		break;
	case OUTPUT_DEVICE_SPEAKER:
	case OUTPUT_DEVICE_LINE:
	case OUTPUT_DEVICE_FIXEDLINE:
	case OUTPUT_DEVICE_NONE:
	default:
		table = TONE_CONTROL_TABLE_NO_HP;
		break;
	}

	cxd3778gf_register_modify(CXD3778GF_CODEC_EN, 0x00, 0x02);
	cxd3778gf_register_modify(CXD3778GF_CLK_HALT, 0x00, 0x08);

	rv = cxd3778gf_register_modify(CXD3778GF_MEM_CTRL, 0x4C, 0xDF);
	if (rv < 0) {
		back_trace();
		return -1;
	}

	/* write data */
	for (n = 0; n < CODEC_RAM_SIZE / (CODEC_RAM_WORD_SIZE * 8); n++) {
		/* set start address */
		rv = cxd3778gf_register_write(CXD3778GF_MEM_ADDR, 8 * n);
		if (rv < 0) {
			back_trace();
			return -1;
		}

		rv = cxd3778gf_register_write_multiple(CXD3778GF_MEM_WDAT,
			((unsigned char *)&cxd3778gf_tone_control_table[table])
						 + CODEC_RAM_WORD_SIZE * 8 * n,
						       CODEC_RAM_WORD_SIZE * 8);
		if (rv < 0) {
			back_trace();
			return -1;
		}
	}

	/* set mode to normal */
	rv = cxd3778gf_register_modify(CXD3778GF_MEM_CTRL, 0x0C, 0xDF);
	if (rv < 0) {
		back_trace();
		return -1;
	}

	cxd3778gf_register_modify(CXD3778GF_CLK_HALT, 0x08, 0x08);
	cxd3778gf_register_modify(CXD3778GF_CODEC_EN, 0x02, 0x02);

	return 0;
}

static int adjust_device_gain(int input_device)
{
	print_trace("%s(%d)\n",__FUNCTION__,input_device);
#if 0
	cxd3778gf_register_write(CXD3778GF_PGA1_VOLL,cxd3778gf_device_gain_table[input_device].pga);
	cxd3778gf_register_write(CXD3778GF_PGA1_VOLR,cxd3778gf_device_gain_table[input_device].pga);
	cxd3778gf_register_write(CXD3778GF_MIC1L_VOL,cxd3778gf_device_gain_table[input_device].adc);
	cxd3778gf_register_write(CXD3778GF_MIC1R_VOL,cxd3778gf_device_gain_table[input_device].adc);
#endif
	return(0);
}

static int report_jack_status_se(
	int old_status,
	int new_status,
	int force
)
{
	/* print_trace("%s(%d,%d,%d)\n",__FUNCTION__,old_status,new_status,force); */

	if(new_status!=old_status){

		if(new_status==JACK_STATUS_SE_5PIN){
			print_info("hp(5pin) detect.\n");
		}
		else if(new_status==JACK_STATUS_SE_3PIN){
			print_info("hp(3pin) detect.\n");
		}
		else if(new_status==JACK_STATUS_SE_4PIN){
			print_info("hp(4pin) detect.\n");
                }
		else if(new_status==JACK_STATUS_SE_ANTENNA){
			print_info("antenna detect.\n");
		}
		else{
			print_info("no hp.\n");
		}

		if((force || old_status==JACK_STATUS_SE_NONE || old_status==JACK_STATUS_SE_ANTENNA)
		&& (         new_status==JACK_STATUS_SE_3PIN || new_status==JACK_STATUS_SE_5PIN || new_status==JACK_STATUS_SE_4PIN   )){
				cxd3778gf_extcon_set_headphone_value(1);
		}
		if((force || old_status==JACK_STATUS_SE_3PIN || old_status==JACK_STATUS_SE_5PIN || old_status==JACK_STATUS_SE_4PIN  )
		&& (         new_status==JACK_STATUS_SE_NONE || new_status==JACK_STATUS_SE_ANTENNA)){
				cxd3778gf_extcon_set_headphone_value(0);
		}

		if (present.board_type == TYPE_A){
			if((old_status==JACK_STATUS_SE_3PIN) && (new_status==JACK_STATUS_SE_5PIN)) {
				cxd3778gf_extcon_set_headphone_value(0);
				cxd3778gf_extcon_set_headphone_value(1);
			}
			if((old_status==JACK_STATUS_SE_5PIN) && (new_status==JACK_STATUS_SE_3PIN)) {
				cxd3778gf_extcon_set_headphone_value(0);
				cxd3778gf_extcon_set_headphone_value(1);
			}
		}
	}

	return(0);
}

static int report_jack_status_btl(
        int old_status,
        int new_status,
        int force
)
{
        /* print_trace("%s(%d,%d,%d)\n",__FUNCTION__,old_status,new_status,force); */

        if(new_status!=old_status){

                if(new_status==JACK_STATUS_BTL){
                        print_info("btl detect.\n");
                }
                else{
                        print_info("no btl.\n");
                }

                if((force || old_status==JACK_STATUS_BTL_NONE)
                && (         new_status==JACK_STATUS_BTL   )){
			cxd3778gf_extcon_set_headphone_value(3);
                }
                if((force || old_status==JACK_STATUS_BTL)
                && (         new_status==JACK_STATUS_BTL_NONE)){
			cxd3778gf_extcon_set_headphone_value(2);
                }
        }

        return(0);
}
#ifdef ICX_ENABLE_AU2CLK
static int change_external_osc(int pwm_nowait)
{
	unsigned int now;
	unsigned int req;
	unsigned int now_osc;
	unsigned int clk0_temp=0;
	unsigned int clk1_temp=0;
	unsigned int clk2_temp=0;
	unsigned int dsd_enable = 0;

	print_trace("%s()\n",__FUNCTION__);
	int current_mode;

//	now=cxd3778gf_get_external_osc();

	if (present.board_type == TYPE_Z && !(board_set_flag&NO_MCLK_DIRECTLY_INPUT_FLAG)){
		if(cxd3778gf_get_441clk_value())
			now_osc = EXTERNAL_OSC_441;
		else if(cxd3778gf_get_480clk_value())
			now_osc = EXTERNAL_OSC_480;
		else
			now_osc = -1;

		req = suitable_external_osc();

		if(req==EXTERNAL_OSC_KEEP){
			print_debug("external OSC = KEEP\n");
			return(0);
		}

		present.osc = req;

		if(req!=now_osc){

			cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0,0x00,0x80);
			if (present.playback_latency == PLAYBACK_LATENCY_LOW) {
				if (pwm_nowait) {
					if (present.format != DSD_MODE)
						cxd3778gf_set_playback_mute(&present, ON);

					if (board_set_flag & HW_MUTE_CONSTANT_30MS_FLAG) {
						cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,
							0x80, 0x80);
						cxd3778gf_switch_smaster_mute(ON,
							present.headphone_amp);
						msleep(26);
						cxd3778gf_set_phv_mute(&present, TRUE, FALSE);
						usleep_range(4000, 5000);
					} else {
						if (present.headphone_amp == HEADPHONE_AMP_SMASTER_SE) {
							cxd3778gf_switch_smaster_mute(ON,
								present.headphone_amp);
							msleep(100);
							cxd3778gf_set_phv_mute(&present, TRUE, TRUE);
							cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,
								0x80, 0x80);
						} else {
							cxd3778gf_set_phv_mute(&present, TRUE, TRUE);
							cxd3778gf_switch_smaster_mute(ON,
								present.headphone_amp);
							msleep(50);
							cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,
								0x80, 0x80);
						}
					}
				}
			} else {
				if (pwm_nowait)
					cxd3778gf_set_output_device_mute(&present,
								     ON, FALSE);
			}

			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x00, 0x44);

			/* Stop all clock except SMS_CLK and PHV_CLK for avoiding Glitch */
			cxd3778gf_register_read(CXD3778GF_CLK_EN0,&clk0_temp);
			cxd3778gf_register_read(CXD3778GF_CLK_EN1,&clk1_temp);
			cxd3778gf_register_read(CXD3778GF_CLK_EN2,&clk2_temp);
			cxd3778gf_register_write(CXD3778GF_CLK_EN0, (clk0_temp)&(0x00));
			cxd3778gf_register_write(CXD3778GF_CLK_EN1, (clk1_temp)&(0x0A));
			cxd3778gf_register_write(CXD3778GF_CLK_EN2, (clk2_temp)&(0x20));

			cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x00,0x03);

			if (req == EXTERNAL_OSC_441)
				cxd3778gf_set_441clk_enable();
			else if(req == EXTERNAL_OSC_480)
				cxd3778gf_set_480clk_enable();

			if (present.playback_latency == PLAYBACK_LATENCY_NORMAL)
				msleep(10);

			cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x03,0x03);

			cxd3778gf_register_write(CXD3778GF_CLK_EN0, clk0_temp);
			cxd3778gf_register_write(CXD3778GF_CLK_EN1, clk1_temp);
			cxd3778gf_register_write(CXD3778GF_CLK_EN2, clk2_temp);

			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x04, 0x04);

			if (present.playback_latency == PLAYBACK_LATENCY_LOW) {
				if (present.format == DSD_MODE) {
					cxd3778gf_register_read(CXD3778GF_DSD_ENABLE, &dsd_enable);
					if (!(dsd_enable & 0x01))
						dsdif_pass_change();
				}
				if (pwm_nowait) {
					if (board_set_flag & HW_MUTE_CONSTANT_30MS_FLAG) {
						cxd3778gf_set_phv_mute(&present, FALSE, FALSE);
						cxd3778gf_switch_smaster_mute(OFF,
							present.headphone_amp);
						usleep_range(10000, 11000);
						cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE, 0x00, 0x80);
					} else {
						if (present.headphone_amp == HEADPHONE_AMP_SMASTER_SE) {
							cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,
								0x00, 0x80);
							cxd3778gf_set_phv_mute(&present, FALSE, TRUE);
							cxd3778gf_switch_smaster_mute(OFF,
								present.headphone_amp);
							msleep(100);
						} else {
							cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,
								0x00, 0x80);
							cxd3778gf_switch_smaster_mute(OFF,
								present.headphone_amp);
							msleep(50);
							cxd3778gf_set_phv_mute(&present, FALSE, TRUE);
						}
					}

					if (present.format != DSD_MODE)
						cxd3778gf_set_playback_mute(&present, OFF);
				}
			} else {
				if (pwm_nowait)
					cxd3778gf_set_output_device_mute(&present,
								    OFF, FALSE);
			}

			cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0,0x80,0x80);

		} else {
			print_debug("external OSC = SAME\n");
		}
	}

	else {
		cxd3778gf_register_read(CXD3778GF_OSC_ON,&now);
		if (now == 0x10)
			now_osc = EXTERNAL_OSC_441;
		else if (now == 0x20)
			now_osc = EXTERNAL_OSC_480;
		else
			now_osc = -1;

		req = suitable_external_osc();

		if(req==EXTERNAL_OSC_KEEP){
			print_debug("external OSC = KEEP\n");
			return(0);
		}

		present.osc = req;

		if(req!=now_osc){
			if (present.noise_cancel_active) {
				if(pwm_nowait)
					cxd3778gf_set_output_device_mute(&present,
								     ON, FALSE);
			}
			if (req == EXTERNAL_OSC_441) {
				cxd3778gf_register_modify(CXD3778GF_OSC_ON,0x10,0x10);
				cxd3778gf_register_modify(CXD3778GF_OSC_EN,0x10,0x10);
			} else if (req == EXTERNAL_OSC_480) {
                                cxd3778gf_register_modify(CXD3778GF_OSC_ON,0x20,0x20);
                                cxd3778gf_register_modify(CXD3778GF_OSC_EN,0x20,0x20);
			}

			if (present.board_type == TYPE_A)
				cxd3778gf_register_modify(CXD3778GF_CODEC_EN,0x00,0x03);

			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x00, 0x44);

//                        if(present.noise_cancel_active == TRUE){
//                                cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0,0x00,0x80);
//				cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_H,0x00);
//				cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_L,0x00);
//			}

			if (present.noise_cancel_active == NC_NON_ACTIVE) {
				cxd3778gf_set_playback_mute(&present, ON);
				cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
				msleep(5);
				cxd3778gf_set_line_mute(&present, ON);
			}

			if (present.noise_cancel_active) {
				current_mode = present.noise_cancel_mode;
				present.noise_cancel_mode =
							NOISE_CANCEL_MODE_OFF;
				present.osc = now_osc;
				cxd3778gf_dnc_judge(&present);
			}

			/* Stop all clock except SMS_CLK and PHV_CLK for avoiding Glitch */
			cxd3778gf_register_read(CXD3778GF_CLK_EN0,&clk0_temp);
			cxd3778gf_register_read(CXD3778GF_CLK_EN1,&clk1_temp);
			cxd3778gf_register_read(CXD3778GF_CLK_EN2,&clk2_temp);
			/* for avoid glitch, clk off except CPCTL_CLK PHV_CLK_SMS_CLK */
			cxd3778gf_register_write(CXD3778GF_CLK_EN0, (clk0_temp)&(0xC0));
			cxd3778gf_register_write(CXD3778GF_CLK_EN1, (clk1_temp)&(0x0A));
			cxd3778gf_register_write(CXD3778GF_CLK_EN2, (clk2_temp)&(0x20));

			cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x00,0x03);
//			if(status->output_device==OUTPUT_DEVICE_HEADPHONE
//			&& (status->headphone_amp==HEADPHONE_AMP_SMASTER_SE || status->headphone_amp==HEADPHONE_AMP_SMASTER_BTL)){

				/* PCMCLKO=off */
			//	cxd3778gf_register_modify(CXD3778GF_DAC,0x80,0x80);

				/* disable digital amp */
			//	digiamp_disable(DAMP_CTL_LEVEL_RESET);
//			}
#if 0
			/* MCLK=off */
			cxd3778gf_register_modify(CXD3778GF_MODE_CONTROL,0x00,0x01);

			/* change master clock */
			cxd3778gf_switch_external_osc(req);

			/* MCLK=on */
			cxd3778gf_register_modify(CXD3778GF_MODE_CONTROL,0x01,0x01);
#endif
//                        cxd3778gf_register_modify(CXD3778GF_OSC_ON, 0x01, 0x01);
//                        msleep(2);
//                        cxd3778gf_register_modify(CXD3778GF_OSC_EN, 0x01, 0x01);
//                        cxd3778gf_register_modify(CXD3778GF_OSC_SEL, 0x00, 0x00);

                        if (req == EXTERNAL_OSC_441) {
				cxd3778gf_register_modify(CXD3778GF_OSC_SEL,0x01,0x11);
				cxd3778gf_register_modify(CXD3778GF_OSC_EN,0x00,0x20);
				cxd3778gf_register_modify(CXD3778GF_OSC_ON,0x00,0x20);
			} else if (req == EXTERNAL_OSC_480) {
				cxd3778gf_register_modify(CXD3778GF_OSC_SEL,0x11,0x11);
				cxd3778gf_register_modify(CXD3778GF_OSC_EN,0x00,0x10);
				cxd3778gf_register_modify(CXD3778GF_OSC_ON,0x00,0x10);
			}

//                        cxd3778gf_register_modify(CXD3778GF_OSC_EN, 0x00, 0x01);
//                        cxd3778gf_register_modify(CXD3778GF_OSC_ON, 0x00, 0x01);

//			if(status->output_device==OUTPUT_DEVICE_HEADPHONE
//			&& (status->headphone_amp==HEADPHONE_AMP_SMASTER_SE || status->headphone_amp==HEADPHONE_AMP_SMASTER_BTL)){

			/* enable digital amp */
			//	digiamp_enable(DAMP_CTL_LEVEL_RESET);

			/* PCMCLKO=on */
			//	cxd3778gf_register_modify(CXD3778GF_DAC,0x00,0x80);
//			}

			cxd3778gf_register_modify(CXD3778GF_SW_XRST0, 0x03,0x03);
			cxd3778gf_register_write(CXD3778GF_CLK_EN0, clk0_temp);
			cxd3778gf_register_write(CXD3778GF_CLK_EN1, clk1_temp);
			cxd3778gf_register_write(CXD3778GF_CLK_EN2, clk2_temp);


			if (present.noise_cancel_active) {
				present.noise_cancel_mode = current_mode;
				present.osc = req;
				cxd3778gf_dnc_judge(&present);
			}
			if (present.noise_cancel_active == NC_NON_ACTIVE) {
				cxd3778gf_set_line_mute(&present, OFF);
				cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x00,0x80);
				msleep(5);
				cxd3778gf_set_playback_mute(&present, OFF);
			}
//			if(present.noise_cancel_active == TRUE){
//				cxd3778gf_register_modify(CXD3778GF_PHV_CTRL0,0x80,0x80);
//				cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_H,0x20);
//				cxd3778gf_register_write(CXD3778GF_DNC1_MONVOL0_L,0x00);
//			}

			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x04, 0x04);
			if (present.board_type == TYPE_A)
				cxd3778gf_register_modify(CXD3778GF_CODEC_EN,0x02,0x03);

			if (present.noise_cancel_active) {
				if(pwm_nowait)
					cxd3778gf_set_output_device_mute(&present,
								    OFF, FALSE);
			}
		}
		else{
			print_debug("external OSC = SAME\n");
		}
	}
	return(0);
}

static unsigned int suitable_external_osc(void)
{
	unsigned int osc;

	print_trace(
		"%s(%d,%d,%d,%d,%d,%d,%u)\n",
		__func__,
		present.output_device,
		present.input_device,
		present.headphone_amp,
		present.icx_playback_active,
		present.std_playback_active,
		present.capture_active,
		present.sample_rate
	);

	if (present.noise_cancel_active != NC_NON_ACTIVE ||
		  present.input_device != INPUT_DEVICE_NONE) {
		osc = EXTERNAL_OSC_480;
#ifdef STD_PATH_CLK_CHANGE
	} else if (present.icx_playback_active > 0 || present.std_playback_active > 0) {
#else
	} else if (present.icx_playback_active > 0) {
#endif
		switch (present.sample_rate) {
		case   8000:
		case  16000:
		case  32000:
		case  48000:
		case  64000:
		case  96000:
		case 192000:
		case 384000:
			osc = EXTERNAL_OSC_480;
			break;
		case   5512:
		case  11025:
		case  22050:
		case  44100:
		case  88200:
		case 176400:
		case 352800:
		default:
			osc = EXTERNAL_OSC_441;
			break;
		}
	} else {
		osc = EXTERNAL_OSC_KEEP;
	}

	return osc;
}

#endif

/*******************/
/*@ basic_routines */
/*******************/

static int startup_chip(unsigned int rate)
{
	print_trace("%s()\n",__FUNCTION__);

	/* external OSC on */
#ifdef ICX_ENABLE_AU2CLK
	cxd3778gf_switch_external_osc(EXTERNAL_OSC_441);
#else
	cxd3778gf_switch_external_osc(EXTERNAL_OSC_480);
#endif

	/* unreset hardware */
	cxd3778gf_unreset();
	/* IREFTRM and MICBIAS trimming */
	cxd3778gf_register_write(CXD3778GF_TRIM0,0x36);
	/* BGR_TRM=0 */
	cxd3778gf_register_write(CXD3778GF_TRIM1,0x07);
	/* HPIB enale */
	cxd3778gf_register_write(CXD3778GF_TRIM2,0x1F);
#if 0
	/* REG_TRM=4, BGR_TRM=5 */
	cxd3778gf_register_modify(CXD3778GF_TRIM_2,0x94,0xFC);

	/* PWMOUT_DS=8mA, PCMCLKO_DS=10mA */
	cxd3778gf_register_modify(CXD3778GF_IO_DRIVE,0x05,0x0F);
#endif
	/* REF=on, VCOM=on, MICDET=on, REGL=on */
//	cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x00,0xF0);
	/* BGR=on, VCOM=on, REGL_EN=off, MBS=on */
	if(present.board_type==TYPE_Z)
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0,STUP_REG_BLK_ON0_TP_Z,0x0F);
	else
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0,STUP_REG_BLK_ON0,0x0F);

	/* MCK=TCXO */
//	cxd3778gf_register_modify(CXD3778GF_DAC,0x40,0x40);
	/* Normal mode MCV_SYSCLK_RATIO=0, SYSCLK_FS=0 */

	if(present.board_type==TYPE_A)
		cxd3778gf_register_modify(CXD3778GF_SYSTEM,0x03,0x03);
	if(present.board_type==TYPE_Z)
		cxd3778gf_register_modify(CXD3778GF_SYSTEM,0x01,0x03);
#if 0
	/* low power mode MCV_SYSCLK_RATIO=1, SYSCLK_FS=1 */
        cxd3778gf_register_modify(CXD3778GF_SYSTEM,0x03,0x03);
	cxd3778gf_register_modify(CXD3778GF_TRIM1,0x**,0x07);
	cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0x04,0x04);
#endif
	/*MCLK1_OE=1*/
	cxd3778gf_register_modify(CXD3778GF_OSC_ON,0x10,0x10);
	/*SEL_MCK48=0, SEL_RC_X=1 (x'tal osc)*/
	cxd3778gf_register_modify(CXD3778GF_OSC_SEL,0x01,0x11);
	/*MCLK1_EN=1*/
	msleep(1); /* TBD */
	cxd3778gf_register_modify(CXD3778GF_OSC_EN,0x10,0x10);
#if 0
	/*MCLK2_OE=1*/
	cxd3778gf_register_modify(CXD3778GF_OSC_ON,0x20,0x20);
	cxd3778gf_register_modify(CXD3778GF_OSC_SEL,0x11,0x11);
	msleep(1); /* TBD */
	cxd3778gf_register_modify(CXD3778GF_OSC_EN,0x20,0x20);
#endif

	/* codec mode=high, I2S=slave, format=I2S, fs=1024 */
//	cxd3778gf_register_modify(CXD3778GF_MODE_CONTROL,0xC2,0xFE);
	/* MCLOUT_ON */
	if(present.board_type!=TYPE_A && (board_set_flag&NO_MCLK_DIRECTLY_INPUT_FLAG))
		cxd3778gf_register_modify(CXD3778GF_CLK_OUTPUT,0x04,0x04);

	/* Drive Strength Change*/
	cxd3778gf_register_write(0xE8,0x03);

	/* S_16fs_clk(xpcm_zero) on, codec_clk_en SRC clk on*/
	cxd3778gf_register_modify(CXD3778GF_CLK_EN0, STUP_REG_CLK_EN0, 0x17);

	/* USM_CLK_EN enable */
	if(present.board_type==TYPE_Z){
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2, STUP_REG_CLK_EN2_TP_Z, 0x10);
		cxd3778gf_register_modify(CXD3778GF_DNC_INTP,0x00,0x08);
	}

	/* MTB_CLK_EN use codec block */
	cxd3778gf_register_modify(CXD3778GF_CLK_EN2, STUP_REG_CLK_EN2, 0x40);

	/* I2S mode */
	cxd3778gf_register_modify(CXD3778GF_SD1_MISC,0x00,0xD1);
	/* DSPC=on, DSPS1=on, DSPS2=on */
//	cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0xE0);

	/* DSPRAM_S, SRESET */
//	cxd3778gf_register_modify(CXD3778GF_TEST_1,0x03,0x03);

	/* wait clear */
//	msleep(20);

	/* SRC2IN=ADC, SRC2 mode=low, SRC1IN=ADC, SRC1 mode=XXX */

	cxd3778gf_register_write(CXD3778GF_I2S_INT_EN, 0x03);

	cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,0x00,0x44);

	if(rate<=48000){
                cxd3778gf_register_modify(CXD3778GF_SD1_MODE,0x0C,0x0F);
        }
        else if(rate<=96000){
                cxd3778gf_register_modify(CXD3778GF_SD1_MODE,0x0D,0x0F);
        }
        else if(rate<=192000){
                cxd3778gf_register_modify(CXD3778GF_SD1_MODE,0x0E,0x0F);
        }
        else{
                cxd3778gf_register_modify(CXD3778GF_SD1_MODE,0x0B,0x0F);
        }

	/* LRCK and BCK's driver strength is x6 */
	cxd3778gf_register_write(0xE7,0x33);

	cxd3778gf_register_modify(CXD3778GF_SD2_MODE,0x58,0x5F);

	/* UPS_MIX_MUTE_B =off */
	cxd3778gf_register_modify(CXD3778GF_DNC_INTP,0x00,0x01);

	cxd3778gf_register_modify(CXD3778GF_CODEC_EN,0x80,0x80);
	cxd3778gf_register_modify(CXD3778GF_MEM_INIT,0x08,0x08);
	msleep(1); /* TBD */
	/* mute_b=off, sdout2=on, sdout1=off, sdin2=on, sdin2=on */
//	cxd3778gf_register_modify(CXD3778GF_SDO_CONTROL,0x0B,0x1F);


	/* DITHER */
//	cxd3778gf_register_write(CXD3778GF_S_MASTER_DITHER_1,0x00);
	cxd3778gf_register_write(CXD3778GF_SMS_DITHER_CTRL1,0x7F);
	cxd3778gf_register_write(CXD3778GF_SMS_DITHER_CTRL2,0xB5);
//	cxd3778gf_register_write(CXD3778GF_DITHER_MIC,0x8A);
//	cxd3778gf_register_write(CXD3778GF_DITHER_LINE,0x8A);
	cxd3778gf_register_write(CXD3778GF_DITH_LEV1,0xAA);
	cxd3778gf_register_write(CXD3778GF_DITH_LEV2,0xAA);
	cxd3778gf_register_write(CXD3778GF_DITH,0x03);

	/* src ram clear */
	cxd3778gf_register_modify(CXD3778GF_MEM_INIT, 0x08, 0x08);

#if 0
	/* CHGFREQ to 4*/
	cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x04,0x04);

	/* PDN_CP to 0 */
	cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x00,0x01);
	msleep(30);
#endif
	cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,0x05, 0x05);

	/* ARC=0dB fixed, HPF1=1.71Hz */
//	cxd3778gf_register_modify(CXD3778GF_LINEIN_1,0x84,0xFC);

	/* CIC1IN=zero */
//	cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x00,0xC0);

	/* CHGFREQ=564.5KHz, ADPTPWR=instantaneous value from nsin */
//	cxd3778gf_register_modify(CXD3778GF_CHARGE_PUMP,0x46,0xF7);

	/* NS_OUT=RZ */
//	cxd3778gf_register_modify(CXD3778GF_NS_DAC,0x00,0x10);

	/* ANLGZC=off, analog zero cross off */
//	cxd3778gf_register_modify(CXD3778GF_SOFT_RAMP_1,0x00,0x20);

	/* Zero closs detection */
	cxd3778gf_register_write(CXD3778GF_PHV_CTRL0,0x80);

	/* soft ramp enable */
	cxd3778gf_register_write(CXD3778GF_CODEC_SR_PLAY1,0x1C);
	cxd3778gf_register_write(CXD3778GF_CODEC_SR_PLAY3,0x01);
	cxd3778gf_register_write(CXD3778GF_CODEC_SR_REC1,0x10);

	/* ADC1 for DNC 64fs and ADC2 for FM Linein 128fs */
	cxd3778gf_register_write(CXD3778GF_BLK_FREQ0, 0x60);

	/* CIC1 4fs and CIC2 8fs */
	cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1, 0x50,0xF0);

	/* Enable DC CMT filter for noise dnc mode */
	cxd3778gf_register_write(CXD3778GF_MIC1_REG1,0x01);

	/* PLUG DET */
	if(present.board_type==TYPE_A)
		cxd3778gf_register_write(CXD3778GF_PLUG_DET,0x10);

	/* ADC1 CLIP Enable */
	if(present.board_type==TYPE_A)
		cxd3778gf_register_modify(CXD3778GF_MIC1_REG0,0x10,0x10);

	/* mic bias */
	set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_NORMAL);

	return(0);
}

static int shutdown_chip(void)
{
	print_trace("%s()\n",__FUNCTION__);

	/* mute_b=on, sdout2=off, sdout1=off, sdin2=off, sdin2=off */
//	cxd3778gf_register_modify(CXD3778GF_SDO_CONTROL,0x10,0x1F);

	/* SRC clock OFF */
	cxd3778gf_register_modify(CXD3778GF_CLK_EN0,0x00,0x03);

	/* all off */
//	cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0xFF,0xFF);

	/* all off */
//	cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0xFF,0xFF);

	/* all off */
//	cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0xFF,0xFF);

	cxd3778gf_reset();

	/* external OSC off (only 1240/1265) */
	cxd3778gf_switch_external_osc(EXTERNAL_OSC_OFF);

	return(0);
}

static int switch_cpclk(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
		/* CPCLK=on */
//		cxd3778gf_register_modify(CXD3778GF_CLASS_H_2,0x04,0x04);
//		msleep(30);
	}
	else{
		/* CPCLK=off */
//		cxd3778gf_register_modify(CXD3778GF_CLASS_H_2,0x00,0x04);
	}

	return(0);
}

static int switch_dac(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
		/* DAC L/R=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0x00,0xC0);
//		msleep(10);
	}
	else{
		/* DAC L/R=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0xC0,0xC0);
	}

	return(0);
}

static int switch_smaster(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){

		/* enable digital amp */
//		digiamp_enable(DAMP_CTL_LEVEL_POWER);
		/* sms_clk on */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x20,0x20);
		/* phy_clk on */
		if (present.board_type == TYPE_A)
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x62,0x62);
		else
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x42,0x42);

		/* CPCLK_EN en */
		if (present.board_type == TYPE_A ||
			present.power_type == NO_CONTROL_NEGATIVE_VOLTAGE) {
			/* Wait 30ms for the charge pump to power up */
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x08,0x08);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1, 0x04, 0x0F);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x80,0x80);
			msleep(30);
			cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x00,0x01);
		}

		/* HPBGR enable  Wait 100ms for the charge pump to power up */
                cxd3778gf_register_modify(CXD3778GF_DAMP_REF,0x03,0x03);
		msleep(100);
		/* HPBGR_STUP off */
		cxd3778gf_register_modify(CXD3778GF_DAMP_REF,0x00,0x02);

		/* HPVOL_ON enale */
		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL1,0x10,0x10);

		/* SMASTER=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x10);

		/* PCMCLKO=on */
//		cxd3778gf_register_modify(CXD3778GF_DAC,0x00,0x80);

		/* mute=off, NSX2I=1, NSADJON=1 */
//		cxd3778gf_register_modify(CXD3778GF_S_MASTER_CONTROL,0x60,0xE0);
	}
	else{
		/* CPCLK_EN off */
		if (present.board_type == TYPE_A ||
			present.power_type == NO_CONTROL_NEGATIVE_VOLTAGE) {
			cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x01,0x01);
			msleep(30);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x00,0x80);
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x08);
		}

		/* mute=on, NSX2I=0, NSADJON=0 */
//		cxd3778gf_register_modify(CXD3778GF_S_MASTER_CONTROL,0x80,0xE0);

		/* PCMCLKO=off */
//		cxd3778gf_register_modify(CXD3778GF_DAC,0x80,0x80);

		/* SMASTER=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x10,0x10);

		/* disable digital amp */
//		digiamp_disable(DAMP_CTL_LEVEL_POWER);
	}

	return(0);
}

static int switch_hpout2(int value)
{
        print_trace("%s(%d)\n",__FUNCTION__,value);

        if(value){

		if(present.board_type==TYPE_Z)
			cxd3778gf_set_se_cp_unmute();
		/* HP2L,HP2R_HPVOL_ON */
		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL1,0x03,0x03);

		/* DAMP_VOL_CTRL2 HPVOL_FFSEL enable to change form x'tal to BCK for zero-cross detection */
		if(present.board_type==TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL2,0x50,0x50);
		else
			cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL2,0x45,0x45);

		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL3,0x03,0x03);

		/* HPVOL_LPF:heavy */
		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL3,0x03,0x03);

		/* HPOUT2_CTRL1 LDO_ON -> DRY_ON  */
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
		cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL1,0x03,0x03);
		/* Wait 5ms for LDO strat-up */
		msleep(5);

		if(present.board_type==TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL2,0x40,0x40);
		else
			cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL2,0x10,0x10);
		/* HP2_DELAYSEL short */
		cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL3,0x01,0x01);

                /* INCP_ON disable, INCP_SEL= external */
		if (present.board_type == TYPE_Z && present.power_type == CONTROL_NEGATIVE_VOLTAGE)
			cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL4, 0x00, 0x0F);
		else
			cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL4, 0x0F, 0x0F);

		cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL0,0x01,0x83);

		cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL1,0x0C,0x0C);
		msleep(20);
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x00,0x80);

		if(present.board_type==TYPE_Z){
			cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x01,0x01);
			/* DSD THRU */
			cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x20,0x70);
		}

		if(present.board_type==TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_SMS_NS_CTRL,0xA2,0xE3);
		else
			cxd3778gf_register_modify(CXD3778GF_SMS_NS_CTRL,0x40,0xE0);

		/* smaster LR reverse */
		reverse_smaster_lr_output(ON, OUTPUT_SE_LR_REVERSE_FLAG);
                /* SMASTER=on */
//              cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x10);

                /* PCMCLKO=on */
//              cxd3778gf_register_modify(CXD3778GF_DAC,0x00,0x80);

                /* mute=off, NSX2I=1, NSADJON=1 */
//              cxd3778gf_register_modify(CXD3778GF_S_MASTER_CONTROL,0x60,0xE0);

                msleep(10);
        }
        else{
		reverse_smaster_lr_output(OFF, OUTPUT_SE_LR_REVERSE_FLAG);

		cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL0,0x81,0x83);

		/* HPOUT2_CTRL1 DRY_OFF,->  LDO_OFF */
		cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL1,0x00,0x0C);
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
		cxd3778gf_register_modify(CXD3778GF_HPOUT2_CTRL1,0x00,0x03);

		/* HP2L,HP2R_HPVOL_ON */
                cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL1,0x00,0x03);

		if(present.board_type==TYPE_Z)
			cxd3778gf_set_se_cp_mute();
                /* mute=on, NSX2I=0, NSADJON=0 */
//              cxd3778gf_register_modify(CXD3778GF_S_MASTER_CONTROL,0x80,0xE0);

                /* PCMCLKO=off */
//              cxd3778gf_register_modify(CXD3778GF_DAC,0x80,0x80);

                /* SMASTER=off */
//              cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x10,0x10);

                /* disable digital amp */
//              digiamp_disable(DAMP_CTL_LEVEL_POWER);
        }

        return(0);
}
static int switch_hpout3(int value)
{
        print_trace("%s(%d)\n",__FUNCTION__,value);

        if(value){

		/* HP3L,HP3R_HPVOL_ON */
                cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL1,0x0C,0x0C);

		/* DAMP_VOL_CTRL2 HPVOL_FFSEL enable to change form x'tal to BCK for zero-cross detection */
		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL2,0x40,0x40);

		/* HPVOL_LPF:heavy */
		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL3,0x03,0x03);

		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
		/* HPOUT3_CTRL1 LDO_ON -> DRY_ON */
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL1,0x03,0x03);
		/* Wait 20ms for LDO strat-up */
		msleep(20);
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x00,0x80);
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL1,0x0C,0x0C);
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL2,0x40,0x40);
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL3,0x03,0x03);

		cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL0,0x02,0x83);

		if(present.board_type==TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x01,0x01);

		if(present.board_type==TYPE_Z){
			cxd3778gf_register_modify(CXD3778GF_SMS_NS_CTRL,0xA1,0xE7);
			/* DSD THRU */
			cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x00,0x70);
		}

		cxd3778gf_switch_hp3x_power(ON);

                /* SMASTER=on */
//              cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x10);

                /* PCMCLKO=on */
//              cxd3778gf_register_modify(CXD3778GF_DAC,0x00,0x80);

                /* mute=off, NSX2I=1, NSADJON=1 */
//              cxd3778gf_register_modify(CXD3778GF_S_MASTER_CONTROL,0x60,0xE0);

		cxd3778gf_register_write(CXD3778GF_SMS_DITHER_CTRL1,0x3F);
		cxd3778gf_register_write(CXD3778GF_SMS_DITHER_CTRL2,0xDA);

		if (present.power_type == NO_CONTROL_NEGATIVE_VOLTAGE)
			cxd3778gf_register_modify(CXD3778GF_CPCTL1,
								0x08, 0x0F);

		reverse_smaster_lr_output(ON, OUTPUT_BTL_LR_REVERSE_FLAG);

                msleep(10);
        }
        else{
		reverse_smaster_lr_output(OFF, OUTPUT_BTL_LR_REVERSE_FLAG);

		if (present.power_type == NO_CONTROL_NEGATIVE_VOLTAGE)
			cxd3778gf_register_modify(CXD3778GF_CPCTL1,
								0x04, 0x0F);

		cxd3778gf_register_write(CXD3778GF_SMS_DITHER_CTRL1,0x7F);
		cxd3778gf_register_write(CXD3778GF_SMS_DITHER_CTRL2,0xB5);

		cxd3778gf_switch_hp3x_power(OFF);
		cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL0,0x82,0x83);

		/* HPOUT3_CTRL1 DRY_OFF,->  LDO_OFF */
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL1,0x00,0x0C);
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
		cxd3778gf_register_modify(CXD3778GF_HPOUT3_CTRL1,0x00,0x03);

		cxd3778gf_register_modify(CXD3778GF_DAMP_VOL_CTRL1,0x00,0x0C);
                /* mute=on, NSX2I=0, NSADJON=0 */
//              cxd3778gf_register_modify(CXD3778GF_S_MASTER_CONTROL,0x80,0xE0);

                /* PCMCLKO=off */
//              cxd3778gf_register_modify(CXD3778GF_DAC,0x80,0x80);

                /* SMASTER=off */
//              cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x10,0x10);

                /* disable digital amp */
//              digiamp_disable(DAMP_CTL_LEVEL_POWER);
        }

        return(0);
}
static int switch_classh(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){

		/* NC_CLK_EN,  PHV_CLK_EN enable*/
                cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x62,0x62);

		if(present.board_type==TYPE_A){
			/* Wait 30ms for the charge pump to power up */
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x08,0x08);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1, 0x04, 0x0F);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x80,0x80);
			msleep(30);
			/* PDN_CP to 0 */
			cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x00,0x01);
		}

		/* sms_clk_en */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x20,0x20);

		/* PHV_CLK_EN */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x02,0x02);

		/* L,Rch DAC reverse */
		cxd3778gf_register_modify(CXD3778GF_NS_DAC,0xD8,0xD8);

		/* dac and HP on */
		cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0xF0,0xF0);

#if 0
		/* HPOUT L/R=on */
		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0x00,0x30);
#endif

		/* REGL_EN disable */
//		if(present.board_type==TYPE_A)
//			cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0x00,0x04);

		msleep(100);
	}
	else{
		/* HPOUT L/R=off */
		//cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0x30,0x30);
		/* NC_CLK_EN, CPCTL_CLK_EN */
                //cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x48);

                /* sms_clk_en disable */
                cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x00,0x20);

                /* PHV_CLK_Disable */
                cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x02);

                /* dac and HP off */
                cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0x00,0xF0);

		/* REGL_EN enable */
//		if(present.board_type==TYPE_A)
//			cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0x04,0x04);

		/* CPCLK_EN off */
                if(present.board_type==TYPE_A){
			cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x01,0x01);
			msleep(30);
                        cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x00,0x80);
                        cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x08);
                }


	}
	return(0);
}

static int switch_lineout(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
		/* NC_CLK_EN,  PHV_CLK_EN enable*/
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x62,0x62);

		if(present.board_type==TYPE_A){
			/* Wait 30ms for the charge pump to power up */
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x08,0x08);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1, 0x04, 0x0F);
			cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x80,0x80);
			msleep(30);
			/* PDN_CP to 0 */
			cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x00,0x01);
		}

		/* sms_clk_en */
                cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x20,0x20);

		if(board_set_flag&OUTPUT_LINEOUT2){
			/* DACL&R LINEOUT2 L&R enable */
			cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0xCC,0xCC);
		} else {
			/* DACL&R LINEOUT1 L&R enable */
			cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0xC3,0xC3);
		}

		/* L,Rch DAC reverse */
		cxd3778gf_register_modify(CXD3778GF_NS_DAC,0xD8,0xD8);

		/* improve S/N rate */
		cxd3778gf_register_write(CXD3778GF_TRIM0,0x76);

		msleep(100);
	}
	else{
		/* back to normal trim value */
		cxd3778gf_register_write(CXD3778GF_TRIM0,0x36);

		if(board_set_flag&OUTPUT_LINEOUT2){
			/* DACL&R LINEOUT2 L&R disable */
			cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0x00,0xCC);
		} else {
			/* DACL&R LINEOUT1 L&R disable */
			cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0x00,0xC3);
		}

		if(present.board_type==TYPE_A){
			/* PDN_CP to 1 */
			cxd3778gf_register_modify(CXD3778GF_CPCTL3,0x01,0x01);
			msleep(30);
			/* CPCLK_EN disable */
                        cxd3778gf_register_modify(CXD3778GF_CPCTL1,0x00,0x80);
                        cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x08);
                }
	}

	return(0);
}

static int switch_speaker(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);
#if 0
	if(value){
		/* lineout 2 L=on */
		/* NS_CLK_EN on, CPCTL_CLK_EN on, PHV_CLK_EN on,  SMASTER????*/
	        cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x4A,0x4A);
		/* SMS_CLK_EN on ???? */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x20,0x20);
		/* DACL&R on, LINEOUT2L&2R on */
                cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0xCC,0xCC);
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0x00,0x03);
		msleep(10);
	}
	else{
		/* lineout 2 L=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_3,0x03,0x03);

		cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x4A);

                cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x00,0x20);

                cxd3778gf_register_modify(CXD3778GF_BLK_ON1,0x00,0xCC);
	}
#endif
	return(0);
}

static int switch_speaker_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
		msleep(5);

		/* speaker on */
		cxd3778gf_switch_speaker_power(ON);

		msleep(50);
	}
	else{
		/* speaker off */
		cxd3778gf_switch_speaker_power(OFF);

		msleep(5);
	}

	return(0);
}

static int switch_linein(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
		/* BLF_CLK_EN */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x08,0x08);

		/* SDOUT2_ON */
		cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x20,0x20);

		/* BLF2 1/2fs */
		if(present.board_type==TYPE_A)
			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x80,0x80);

		/* ADCCTL,PGA, ChargePump, VOLCON = Enable*/
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x7A,0x7A);

		/* AIN2 input = AINP2x1, AIN2_PRAMP=0db */
                cxd3778gf_register_modify(CXD3778GF_AIN_PREAMP, 0x00,0x1C);

		/* AIN = AIN2 */
		cxd3778gf_register_modify(CXD3778GF_CODEC_DATA_SEL, 0x00,0x04);
		/* 0db */
		cxd3778gf_register_write(CXD3778GF_MIC2L_VOL, 0x80);
		cxd3778gf_register_write(CXD3778GF_MIC2R_VOL, 0x80);
                /* -4db */
		cxd3778gf_register_write(CXD3778GF_PGA2_VOLL, 0xF8);
		cxd3778gf_register_write(CXD3778GF_PGA2_VOLR, 0xF8);

		/* MIC2INSEL = ADC */
		cxd3778gf_register_modify(CXD3778GF_MIC2_REG0, 0x00,0x01);
		/* CIC2 HPF = 1.71Hz */
		cxd3778gf_register_modify(CXD3778GF_MIC2_REG1, 0x01,0x03);

		/* CIC2_CLE_EN */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x80,0x80);
                /* ADC2_ON */
                cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0xCA,0xCA);
		/* port=LINE1 */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x00,0x30);

		/* cic1in=LINE */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_3,0xC0,0xC0);

		/* BL DSP=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x02);

		/* linein L/R=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x00,0x03);
	}
	else{

		/* SDOUT2_OFF */
                cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x00,0x20);

		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x00,0x88);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x40,0x70);
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0, 0x00,0xC0);
		/* linein L/R=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x03,0x03);

		/* BL DSP=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x02,0x02);

		/* cic1in=zero */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_3,0x00,0xC0);

		/* port=LINE1 */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x00,0x30);
	}

	return(0);
}

static int switch_tuner(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){

		/* BLF_CLK_EN */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x08,0x08);

		/* SDOUT2_ON */
		cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x20,0x20);

                /* BLF2 1/2fs */
		if(present.board_type==TYPE_A)
			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x80,0x80);

		/* ADCCTL,PGA, ChargePump, VOLCON = Enable*/
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x6A,0x6A);

                /* AIN2 input = AINP2x2, AIN2_PRAMP=0db */
                cxd3778gf_register_modify(CXD3778GF_AIN_PREAMP, 0x10,0x1C);

		/* AIN = AIN2 */
		cxd3778gf_register_modify(CXD3778GF_CODEC_DATA_SEL, 0x00,0x04);

		/* 0db */
		cxd3778gf_register_write(CXD3778GF_MIC2L_VOL, 0x80);
		cxd3778gf_register_write(CXD3778GF_MIC2R_VOL, 0x80);
		/* +3db */
		cxd3778gf_register_write(CXD3778GF_PGA2_VOLL, 0x06);
		cxd3778gf_register_write(CXD3778GF_PGA2_VOLR, 0x06);

		/* MIC2INSEL = ADC */
		cxd3778gf_register_modify(CXD3778GF_MIC2_REG0, 0x00,0x01);
		/* CIC2 HPF = 1.71Hz */
		cxd3778gf_register_modify(CXD3778GF_MIC2_REG1, 0x01,0x03);

		/* CIC2_CLE_EN */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x80,0x80);

		/* ADC2_ON */
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0xCA,0xCA);

		/* port=LINE2 */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x20,0x30);

		/* cic1in=LINE */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_3,0xC0,0xC0);

		/* BL DSP=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x02);

		/* linein L/R=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x00,0x03);
	}
	else{

		/* SDOUT2_OFF */
		cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x00,0x20);

		cxd3778gf_register_modify(CXD3778GF_CLK_EN0, 0x00,0x88);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x40,0x40);
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0, 0x00,0xC0);

		/* linein L/R=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x03,0x03);

		/* BL DSP=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x02,0x02);

		/* cic1in=zero */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_3,0x00,0xC0);

		/* port=LINE1 */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x00,0x30);
	}

	return(0);
}

static int switch_dmic(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
		/* cic1in=DMIC */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_3,0x80,0xC0);
		/* MIC1IN_SEL = DMIC */
		cxd3778gf_register_modify(CXD3778GF_MIC1_REG0,0x01,0x01);
		/* BL DSP=on, DMIC=on */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x03);

		/* ADC1_BOOST=on */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x04,0x04);
	}
	else{
		/* BL DSP=off, DMIC=off */
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x03,0x03);
		cxd3778gf_register_modify(CXD3778GF_MIC1_REG0,0x00,0x01);
		/* cic1in=zero */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_3,0x00,0xC0);

		/* ADC1_BOOST=off */
//		cxd3778gf_register_modify(CXD3778GF_LINEIN_2,0x00,0x04);
	}

	return(0);
}

#define TH_VS0 13
#define TH_VS4 231
static int get_jack_status_se(int noise_cancel_active, int jack_mode)
{

	int status=0;
	int temp_status[3]={0};
	int hp;
	int nc;
	int val_l=0, val_r=0;
	int i;
	/* print_trace("%s(%d,%d)\n",__FUNCTION__,noise_cancel_active,jack_mode); */

	if (present.board_type == TYPE_A) {
		if (present.output_device == OUTPUT_DEVICE_NONE)
			wakeup_chip(1);
		hp = cxd3778gf_get_hp_det_se_value();
		if(present.headphone_detect_mode == HEADPHONE_DETECT_INTERRUPT){
			if (noise_cancel_active == NC_NON_ACTIVE) {
				set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_MIC_DET);
				/* (tentative) wait comparetor statble */
				msleep(1000);
			}
			else
				msleep(1000);
//			nc=cxd3778gf_get_mic_det_value();
			/* AD8_CLK_EN enable */
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x01,0x01);
			msleep(1);
			/* AD8_INBUF_EN enable */
			cxd3778gf_register_write(CXD3778GF_AD8_CTRL3,0x01);
			/* ADC_RUN */
			cxd3778gf_register_write(CXD3778GF_AD8CTRL1,0x01);

			while(1){
				for (i=0; i<3; i++){
					/* read 8bit ADC */
					msleep(10);
					cxd3778gf_register_read(CXD3778GF_AD8L_CAPTURED,&val_l);
					cxd3778gf_register_read(CXD3778GF_AD8R_CAPTURED,&val_r);

					if(hp & (0x01)){
						if(TH_VS0 <=val_l && val_l <= TH_VS4 && TH_VS0 <= val_r && val_r <= TH_VS4)
							temp_status[i]=JACK_STATUS_SE_5PIN;
						else
							temp_status[i]=JACK_STATUS_SE_3PIN;
					} else{
						temp_status[i]=JACK_STATUS_SE_NONE;
						msleep(90);
					}
					hp = cxd3778gf_get_hp_det_se_value();
				}
				if ((temp_status[0] == temp_status[1]) && (temp_status[0] == temp_status[2])){
					status = temp_status[0];
					break;
				}
				else {
					print_error("not the same as HP detect status: first %d, second %d third %d.\n",temp_status[0],temp_status[1],temp_status[2]);
					status = -1;
				}
			}
			/* ADC_STOP */
			cxd3778gf_register_write(CXD3778GF_AD8CTRL1,0x00);
			/* AD8_INBUF_EN disable */
			cxd3778gf_register_write(CXD3778GF_AD8_CTRL3,0x00);
			/* AD8_CLK_EN disable */
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x01);

			if (noise_cancel_active == NC_NON_ACTIVE) {
				set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_NORMAL);
			}
		} else if(present.headphone_detect_mode == HEADPHONE_DETECT_POLLING){
			if (noise_cancel_active == NC_NON_ACTIVE) {
                                set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_MIC_DET);
                                /* (tentative) wait comparetor statble */
                                msleep(1000);
                        }

//                      nc=cxd3778gf_get_mic_det_value();
                        /* AD8_CLK_EN enable */
                        cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x01,0x01);
                        msleep(1);
                        /* AD8_INBUF_EN enable */
                        cxd3778gf_register_write(CXD3778GF_AD8_CTRL3,0x01);
                        /* ADC_RUN */
                        cxd3778gf_register_write(CXD3778GF_AD8CTRL1,0x01);
                        /* read 8bit ADC */
                        msleep(1);

			cxd3778gf_register_read(CXD3778GF_AD8L_CAPTURED,&val_l);
                        cxd3778gf_register_read(CXD3778GF_AD8R_CAPTURED,&val_r);

			if(hp & (0x01)){
				if(TH_VS0 <=val_l && val_l <= TH_VS4 && TH_VS0 <= val_r && val_r <= TH_VS4)
					status=JACK_STATUS_SE_5PIN;
				else
					status=JACK_STATUS_SE_3PIN;
			} else{
				status=JACK_STATUS_SE_NONE;
                        }

			cxd3778gf_register_write(CXD3778GF_AD8CTRL1,0x00);
			/* AD8_INBUF_EN disable */
			cxd3778gf_register_write(CXD3778GF_AD8_CTRL3,0x00);
			/* AD8_CLK_EN disable */
			cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x01);

			if (noise_cancel_active == NC_NON_ACTIVE) {
				set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_NORMAL);
			}
		} else{
			if (present.headphone_type == NCHP_TYPE_NW510N) {
				if (noise_cancel_active == NC_NON_ACTIVE) {
					set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_MIC_DET);
					/* (tentative) wait comparetor statble */
					msleep(1000);
				}

//                      	nc=cxd3778gf_get_mic_det_value();
				/* AD8_CLK_EN enable */
				cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x01,0x01);
				msleep(1);
				/* AD8_INBUF_EN enable */
				cxd3778gf_register_write(CXD3778GF_AD8_CTRL3,0x01);
				/* ADC_RUN */
				cxd3778gf_register_write(CXD3778GF_AD8CTRL1,0x01);
				/* read 8bit ADC */
				msleep(1);

				cxd3778gf_register_read(CXD3778GF_AD8L_CAPTURED,&val_l);
				cxd3778gf_register_read(CXD3778GF_AD8R_CAPTURED,&val_r);

				if(hp & (0x01)){
					if(TH_VS0 <=val_l && val_l <= TH_VS4 && TH_VS0 <= val_r && val_r <= TH_VS4)
						status=JACK_STATUS_SE_5PIN;
					else
						status=JACK_STATUS_SE_3PIN;
				} else{
					status=JACK_STATUS_SE_NONE;
				}

				cxd3778gf_register_write(CXD3778GF_AD8CTRL1,0x00);
				/* AD8_INBUF_EN disable */
				cxd3778gf_register_write(CXD3778GF_AD8_CTRL3,0x00);
				/* AD8_CLK_EN disable */
				cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x01);

				if (noise_cancel_active == NC_NON_ACTIVE) {
					set_mic_bias_mode(CXD3778GF_MIC_BIAS_MODE_NORMAL);
				}
			} else {
				if(hp & (0x01)){
					status=JACK_STATUS_SE_3PIN;
				} else{
					status=JACK_STATUS_SE_NONE;
				}
			}
		}
		if (present.output_device == OUTPUT_DEVICE_NONE)
			wakeup_chip(0);
	} else if (present.board_type == TYPE_Z) {
		hp = cxd3778gf_get_hp_det_se_value();
		if (hp&(0x01)){
			status=JACK_STATUS_SE_3PIN;
		} else{
			status=JACK_STATUS_SE_NONE;
		}
	}

	if(jack_mode==JACK_MODE_ANTENNA){
		if(status!=JACK_STATUS_SE_NONE)
			status=JACK_STATUS_SE_ANTENNA;
	}

	return(status);
}

static int get_jack_status_btl(void)
{
	int status = 0;
	int hp;

	if (present.board_type == TYPE_Z){
		hp=cxd3778gf_get_hp_det_btl_value();
		if (!(hp&0x01))
			status=JACK_STATUS_BTL;
		else
			status=JACK_STATUS_BTL_NONE;
	}

	return(status);
}

static int set_mic_bias_mode(int mode)
{
	/* print_trace("%s(%d)\n",__FUNCTION__,mode); */

	if(mode==CXD3778GF_MIC_BIAS_MODE_NC_ON){
		/* B=2V A=2V pull-none */
		cxd3778gf_register_modify(CXD3778GF_MICBIAS,0x0A,0x0F);
	}
	else if(mode==CXD3778GF_MIC_BIAS_MODE_MIC_DET){
		/* B=2V A=hiz pull-none */
		cxd3778gf_register_modify(CXD3778GF_MICBIAS,0x0A,0x0F);
	}
	else if(mode==CXD3778GF_MIC_BIAS_MODE_NORMAL){
		/* B=2V A=hiz pull-up */
		cxd3778gf_register_modify(CXD3778GF_MICBIAS,0x00,0x0F);
	}
	else {
		/* B=hiz A=hiz pull-up */
		cxd3778gf_register_modify(CXD3778GF_MICBIAS,0x00,0x0F);
	}

	return(0);
}

static int switch_dnc_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x00,0x08);
		/* DNC1_CLK_EN enable  and DNC2_CLK_EN disable */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN2, 0x16, 0x16);

	}else{
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_1,0x08,0x08);
		if (present.board_type == TYPE_Z)
			cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x10,0x16);
		else
			cxd3778gf_register_modify(CXD3778GF_CLK_EN2,0x00,0x16);
	}
	return(0);
}

static int switch_ncmic_amp_power(int value)
{
	print_trace("%s(%d)\n",__FUNCTION__,value);

	if(value){
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x00,0x0C);
		/* CIC1_CLK_EN */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN0,0x40,0x40);
		/* ADCCTL ON */
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x10,0x10);
		/* ADC1R_ON nad ADC1L_ON */
		cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0x30,0x30);
	}else{
//		cxd3778gf_register_modify(CXD3778GF_POWER_CONTROL_2,0x0C,0x0C);
                cxd3778gf_register_modify(CXD3778GF_CLK_EN0,0x00,0x40);

		cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0x00,0x10);

		cxd3778gf_register_modify(CXD3778GF_BLK_ON0,0x00,0x30);
	}
	return(0);
}

static void enable_sd2_clock(int enable, int active)
{
	if (enable) {
		cxd3778gf_register_modify(CXD3778GF_CLK_OUTPUT, 0x22, 0x22);
		cxd3778gf_register_modify(CXD3778GF_SD_ENABLE, 0x50, 0x50);
		pr_debug("%s: enable BCK2 and LRCK2\n", __func__);
	} else {
		if (AMBIENT != active && !present.std_playback_active &&
					      !present.capture_active) {
			cxd3778gf_register_modify(CXD3778GF_SD_ENABLE,
							    0x00, 0x50);
			cxd3778gf_register_modify(CXD3778GF_CLK_OUTPUT,
							    0x00, 0x22);
			pr_debug("%s: disable BCK2 and LRCK2\n", __func__);
		} else {
			pr_debug("%s: skip disabling SD2\n", __func__);
		}
	}

	return;
}

static int show_device_id(void)
{
	unsigned int val;
	int rv;

	rv=cxd3778gf_register_read(CXD3778GF_DEVICE_ID,&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	print_info("device ID = 0x%02X\n",val);

	rv=cxd3778gf_register_read(CXD3778GF_REVISION_ID,&val);
	if(rv<0){
		back_trace();
		return(rv);
	}

	print_info("revision = 0x%02X\n",val);

	return(0);
}

static int dsdif_pass_change(void)
{
	cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE,0x80,0x80);
	cxd3778gf_register_modify(CXD3778GF_CLK_EN1,0xC4,0xD4);
	cxd3778gf_set_icx_dsd_mode(1);
	/* SEL_DAC_DSD */
	cxd3778gf_register_modify(CXD3778GF_NS_DAC,0x01,0x01);

	cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL1,0x80,0x80);

	/* DSD THRU */
	cxd3778gf_register_modify(CXD3778GF_SMS_DSD_CTRL,0x0F,0x7F);

	if(present.headphone_amp==HEADPHONE_AMP_SMASTER_SE)
		print_error("Cannot use DSD native by SE because of cxd3778gf's hardware limitation\n");

	if(present.sample_rate==88200)
		cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1,0x00,0x0C);
	else if(present.sample_rate==176400)
		cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1,0x04,0x0C);
	else if(present.sample_rate==352800)
		cxd3778gf_register_modify(CXD3778GF_BLK_FREQ1,0x08,0x0C);
	else
		print_error("not support format.\n");

	cxd3778gf_register_modify(CXD3778GF_DSD_ENABLE,0x01,0x01);

	return(0);
}

static int pcmif_pass_change(void)
{
	int dsd_enable;

	cxd3778gf_set_icx_dsd_mode(0);
	cxd3778gf_register_read(CXD3778GF_DSD_ENABLE, &dsd_enable);

	/* avoid pop-noise by disable dsd-path before clk-change */
	if (dsd_enable & 0x01) {
		cxd3778gf_set_output_device_mute(&present, ON, TRUE);
		cxd3778gf_register_write(CXD3778GF_SMS_DSD_PMUTE, 0x80);
		cxd3778gf_register_modify(CXD3778GF_DSD_ENABLE, 0x00, 0x01);
		usleep_range(1000, 1100);
		cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL1, 0x00, 0x80);
		cxd3778gf_register_modify(CXD3778GF_NS_DAC, 0x00, 0x01);
		cxd3778gf_register_modify(CXD3778GF_CLK_EN1, 0x40, 0xD4);
		cxd3778gf_register_modify(CXD3778GF_SMS_NS_PMUTE, 0x00, 0x80);
#ifdef ICX_ENABLE_AU2CLK
		if (present.playback_latency == PLAYBACK_LATENCY_LOW)
			change_external_osc(OFF);
#endif
		cxd3778gf_set_output_device_mute(&present, OFF, TRUE);
	}

	return 0;
}

static void disable_dnc1_block(void)
{
	cxd3778gf_register_modify(CXD3778GF_DNC1_START, 0x40, 0x40);
	cxd3778gf_register_modify(CXD3778GF_DNC1_START, 0x00, 0x80);
	cxd3778gf_register_modify(CXD3778GF_CLK_EN2, 0x00, 0x02);
}

static void disable_dnc2_block(void)
{
	cxd3778gf_register_modify(CXD3778GF_DNC2_START, 0x40, 0x40);
	cxd3778gf_register_modify(CXD3778GF_DNC2_START, 0x00, 0x80);
	cxd3778gf_register_modify(CXD3778GF_CLK_EN2, 0x00, 0x04);
}

static void enable_dnc1_block(void)
{
	cxd3778gf_register_modify(CXD3778GF_CLK_EN2, 0x02, 0x02);
	cxd3778gf_register_modify(CXD3778GF_DNC1_START, 0xD0, 0xD0);
	cxd3778gf_register_modify(CXD3778GF_DNC1_START, 0x00, 0x50);
}

static void enable_dnc2_block(void)
{
	cxd3778gf_register_modify(CXD3778GF_CLK_EN2, 0x04, 0x04);
	cxd3778gf_register_modify(CXD3778GF_DNC2_START, 0xD0, 0xD0);
	cxd3778gf_register_modify(CXD3778GF_DNC2_START, 0x00, 0x50);
}

static int power_off_inactive_dnc_block(void)
{
	if (present.board_type != TYPE_A)
		return 0;

	if (present.noise_cancel_active == NC_ACTIVE) {
		disable_dnc2_block();
		present.inactive_dnc_block = DNC2_INACTIVE;
	} else if (present.noise_cancel_active == AMBIENT) {
		disable_dnc1_block();
		present.inactive_dnc_block = DNC1_INACTIVE;
	}

	return 0;
}

static int power_on_inactive_dnc_block(void)
{
	if (present.board_type != TYPE_A)
		return 0;

	if (present.noise_cancel_active == NC_ACTIVE)
		enable_dnc2_block();
	else if (present.noise_cancel_active == AMBIENT)
		enable_dnc1_block();

	present.inactive_dnc_block = DNC_INACTIVE_OFF;

	return 0;
}

static int reverse_smaster_lr_output(int reverse, int flag)
{
	if (board_set_flag & flag) {
		if (reverse)
			cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL1,
								0x20, 0x20);
		else
			cxd3778gf_register_modify(CXD3778GF_SMS_PWM_CTRL1,
								0x00, 0x20);
	}

	return 0;
}
