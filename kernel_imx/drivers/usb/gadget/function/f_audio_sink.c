// SPDX-License-Identifier: GPL-2.0+
/*
 * f_audio_sink.c -- USB Audio Class 2.0 Function
 *
 * Copied from f_uac2.c
 *
 * Copyright (C) 2011
 *    Yadwinder Singh (yadi.brar01@gmail.com)
 *    Jaswinder Singh (jaswinder.singh@linaro.org)
 */
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */
/*
 * Copyright (C) 2016
 * Author: Ruslan Bilovol <ruslan.bilovol@gmail.com>
 */
/*
 * Copyright (C) 2020 Sony Home Entertainment & Sound Products Inc.
 * Copyright 2021 Sony Corporation
 */
/*
 * Copyright 2022 Pegatron Corporation
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/composite.h>
#include <linux/module.h>

#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define DSD_ALT 7

#define UAC2_DEF_CCHMASK 0x3
#define UAC2_DEF_CSRATE 64000
#define UAC2_DEF_CSSIZE 2
#define UAC2_DEF_REQ_NUM 120

/* Keep everyone on toes */
#define USB_XFERS	8

#define UAC2_GADGET_DELAYED_DATA	0x7ffe

#define UAC2_GADGET_DELAYED_DATA_INTERVAL	30		/* ms */
#define UAC2_GADGET_DELAYED_DATA_TIMEOUT	350		/* ms */

#define UAC2_GADGET_ISO_DATA_SIZE_MAX		1024
#define UAC2_GADGET_ISO_FB_SIZE_MAX			4

/*
 * The driver implements a simple UAC_2 topology.
 * USB-OUT -> IT_1 -> OT_3 -> ALSA_Capture
 * ALSA_Playback -> IT_2 -> OT_4 -> USB-IN
 * Capture and Playback sampling rates are independently
 *  controlled by two clock sources :
 *    CLK_5 := c_srate, and CLK_6 := p_srate
 */

#define CONTROL_ABSENT	0
#define CONTROL_RDONLY	1
#define CONTROL_RDWR	3

#define CLK_FREQ_CTRL	0
#define CLK_VLD_CTRL	2

#define COPY_CTRL	0
#define CONN_CTRL	2
#define OVRLD_CTRL	4
#define CLSTR_CTRL	6
#define UNFLW_CTRL	8
#define OVFLW_CTRL	10

#define USB_OUT_IT_ID	1
#define IO_IN_IT_ID	2
#define IO_OUT_OT_ID	3
#define USB_IN_OT_ID	4
#define USB_OUT_CLK_ID	41
#define USB_IN_CLK_ID	6
#define USB_OUT_CLK_SEL_ID	40
#define USB_FEATURE_UNIT_ID	8

#define BUFF_SIZE_MAX	(PAGE_SIZE * 1024)
#define PRD_SIZE_MAX	(PAGE_SIZE * 8)
#define MIN_PERIODS	4

#define USB_FB_XFERS 1
#define USB_FB_MAX_PSIZE sizeof(__le32)
#define USB_FB_MAX_PSIZE_FS 3

#define FB_MAX    5 /* 0.0005 */
#define FB_MID    3 /* 0.0003 */
#define FB_MIN    1 /* 0.0001 */
#define FB_ZERO   0
#define FEED_DEFAULT FB_ZERO
#define FEED_PLUS FEED_DEFAULT
#define FEED_MINUS FEED_DEFAULT
#define FEED_THRESH 50 /* % */
#define FEED_START 180 /* ms */

static unsigned int f_plus = FEED_PLUS;
static unsigned int f_minus = FEED_MINUS;
static unsigned int f_thresh = FEED_THRESH;
static unsigned int f_start = FEED_START;
static unsigned int f_allow = 1;
static unsigned int f_valid = 1;

static struct class *sysfs_audio_class;
static struct device *sysfs_audio_device;

struct uac2_params {
	/* capture */
	int c_chmask;	/* channel mask */
	int c_srate;	/* rate in Hz */
	int c_ssize;	/* sample size */

	int req_number; /* number of preallocated requests */
};

struct g_audio {
	struct usb_function func;
	struct usb_gadget *gadget;

	struct usb_ep *in_ep;
	struct usb_ep *out_ep;

	/* Max packet size for all in_ep possible speeds */
	unsigned int in_ep_maxpsize;
	/* Max packet size for all out_ep possible speeds */
	unsigned int out_ep_maxpsize;

	/* The ALSA Sound Card it represents on the USB-Client side */
	struct snd_uac2_chip *uac;

	struct uac2_params params;

	spinlock_t capture_mutex;
	unsigned int alt_0_to_n;
	unsigned int clock_valid;
	unsigned int capturing;

	struct work_struct work_delayed_data;
	atomic_t delayed_data_enable;
	atomic_t delayed_data_pending;
};

struct f_uac2_opts {
	struct usb_function_instance	func_inst;
	int				c_chmask;
	int				c_srate;
	int				c_ssize;
	int				req_number;
	bool				bound;

	spinlock_t lock;
	int				refcnt;

	void *rbuf;
	void *rbuf_fb;
	struct uac2_req *ureq;
	struct uac2_req *ureq_fb;
};

struct f_uac2 {
	struct g_audio g_audio;
	u8 ac_intf, as_in_intf, as_out_intf;
	u8 ac_alt, as_in_alt, as_out_alt;	/* needed for get_alt() */
};

static void uac2_audio_work_delayed_data(struct work_struct *data);
static void stop_capture(struct g_audio *audio_dev);

static inline struct g_audio *func_to_g_audio(struct usb_function *f)
{
	return container_of(f, struct g_audio, func);
}

static inline uint num_channels(uint chanmask)
{
	uint num = 0;

	while (chanmask) {
		num += (chanmask & 1);
		chanmask >>= 1;
	}

	return num;
}

static inline struct f_uac2 *func_to_uac2(struct usb_function *f)
{
	return container_of(f, struct f_uac2, g_audio.func);
}

static inline
struct f_uac2_opts *g_audio_to_uac2_opts(struct g_audio *agdev)
{
	return container_of(agdev->func.fi, struct f_uac2_opts, func_inst);
}

static inline struct f_uac2 *g_audio_to_uac2(struct g_audio *agdev)
{
	return container_of(agdev, struct f_uac2, g_audio);
}

/* --------- USB Function Interface ------------- */

enum {
	STR_ASSOC,
	STR_IF_CTRL,
	STR_CLKSRC_IN,
	STR_CLKSRC_OUT,
	STR_USB_IT,
	STR_IO_IT,
	STR_USB_OT,
	STR_IO_OT,
	STR_AS_OUT_ALT0,
	STR_AS_OUT_ALT1,
	STR_AS_IN_ALT0,
	STR_AS_IN_ALT1,
};

enum {
	SUBSLOT_2 = 2,
	SUBSLOT_3 = 3,
	SUBSLOT_4 = 4,
	SUBSLOT_NONE,
	STATE_PLAY,
	STATE_STOP,
	STATE_NONE,
	FORMAT_PCM,
	FORMAT_DSD,
	FORMAT_NONE,
	BIT_1,
	BIT_16 = 16,
	BIT_24 = 24,
	BIT_32 = 32,
	BIT_NONE,
	FREQ_44100 = 44100,
	FREQ_48000 = 48000,
	FREQ_88200 = 88200,
	FREQ_96000 = 96000,
	FREQ_176400 = 176400,
	FREQ_192000 = 192000,
	FREQ_352800 = 352800,
	FREQ_384000 = 384000,
	FREQ_NONE,
};

enum {
	EVENT_SUBSLOT_2,
	EVENT_SUBSLOT_3,
	EVENT_SUBSLOT_4,
	EVENT_SUBSLOT_NONE,
	EVENT_STATE_PLAY,
	EVENT_STATE_STOP,
	EVENT_STATE_NONE,
	EVENT_FORMAT_PCM,
	EVENT_FORMAT_DSD,
	EVENT_FORMAT_NONE,
	EVENT_BIT_1,
	EVENT_BIT_16,
	EVENT_BIT_24,
	EVENT_BIT_32,
	EVENT_BIT_NONE,
	EVENT_FREQ_44100,
	EVENT_FREQ_48000,
	EVENT_FREQ_88200,
	EVENT_FREQ_96000,
	EVENT_FREQ_176400,
	EVENT_FREQ_192000,
	EVENT_FREQ_352800,
	EVENT_FREQ_384000,
	EVENT_FREQ_NONE,
};

static struct usb_string UAC2EVENT[] = {
	[EVENT_SUBSLOT_2].s = "SUBSLOT=2",
	[EVENT_SUBSLOT_3].s = "SUBSLOT=3",
	[EVENT_SUBSLOT_4].s = "SUBSLOT=4",
	[EVENT_SUBSLOT_NONE].s = "SUBSLOT=NONE",
	[EVENT_STATE_PLAY].s = "STATE=PLAY",
	[EVENT_STATE_STOP].s = "STATE=STOP",
	[EVENT_STATE_NONE].s = "STATE=NONE",
	[EVENT_FORMAT_PCM].s = "FORMAT=PCM",
	[EVENT_FORMAT_DSD].s = "FORMAT=DSD",
	[EVENT_FORMAT_NONE].s = "FORMAT=NONE",
	[EVENT_BIT_1].s = "BITWIDTH=1",
	[EVENT_BIT_16].s = "BITWIDTH=16",
	[EVENT_BIT_24].s = "BITWIDTH=24",
	[EVENT_BIT_32].s = "BITWIDTH=32",
	[EVENT_BIT_NONE].s = "BITWIDTH=NONE",
	[EVENT_FREQ_44100].s = "FREQ=44100",
	[EVENT_FREQ_48000].s = "FREQ=48000",
	[EVENT_FREQ_88200].s = "FREQ=88200",
	[EVENT_FREQ_96000].s = "FREQ=96000",
	[EVENT_FREQ_176400].s = "FREQ=176400",
	[EVENT_FREQ_192000].s = "FREQ=192000",
	[EVENT_FREQ_352800].s = "FREQ=352800",
	[EVENT_FREQ_384000].s = "FREQ=384000",
	[EVENT_FREQ_NONE].s = "FREQ=NONE",
};

static short uac2_event_map(int item)
{
	switch (item) {
	case SUBSLOT_2:
		return EVENT_SUBSLOT_2;
	case SUBSLOT_3:
		return EVENT_SUBSLOT_3;
	case SUBSLOT_4:
		return EVENT_SUBSLOT_4;
	case SUBSLOT_NONE:
		return EVENT_SUBSLOT_NONE;
	case STATE_PLAY:
		return EVENT_STATE_PLAY;
	case STATE_STOP:
		return EVENT_STATE_STOP;
	case STATE_NONE:
		return EVENT_STATE_NONE;
	case FORMAT_PCM:
		return EVENT_FORMAT_PCM;
	case FORMAT_DSD:
		return EVENT_FORMAT_DSD;
	case FORMAT_NONE:
		return EVENT_FORMAT_NONE;
	case BIT_1:
		return EVENT_BIT_1;
	case BIT_16:
		return EVENT_BIT_16;
	case BIT_24:
		return EVENT_BIT_24;
	case BIT_32:
		return EVENT_BIT_32;
	case BIT_NONE:
		return EVENT_BIT_NONE;
	case FREQ_44100:
		return EVENT_FREQ_44100;
	case FREQ_48000:
		return EVENT_FREQ_48000;
	case FREQ_88200:
		return EVENT_FREQ_88200;
	case FREQ_96000:
		return EVENT_FREQ_96000;
	case FREQ_176400:
		return EVENT_FREQ_176400;
	case FREQ_192000:
		return EVENT_FREQ_192000;
	case FREQ_352800:
		return EVENT_FREQ_352800;
	case FREQ_384000:
		return EVENT_FREQ_384000;
	case FREQ_NONE:
		return EVENT_FREQ_NONE;

	default:
		return EVENT_FREQ_NONE;
	}
}

struct uac2_uevent {
	short action;
	short format;
	short bitwidth;
	short subslot;
	int freq;
};

struct uac2_uevent_work_data {
	struct g_audio * g_audio;
	struct uac2_uevent uevent;
	struct work_struct  work;
};

static const char product_string[256] = "WALKMAN";

static struct usb_string strings_fn[] = {
	[STR_ASSOC].s = &product_string[0],
	[STR_IF_CTRL].s = &product_string[0],
	[STR_USB_IT].s = "USBH Out",
	[STR_IO_IT].s = "USBD Out",
	[STR_USB_OT].s = "USBH In",
	[STR_IO_OT].s = "USBD In",
	[STR_AS_OUT_ALT0].s = &product_string[0],
	[STR_AS_OUT_ALT1].s = &product_string[0],
	[STR_AS_IN_ALT0].s = "Capture Inactive",
	[STR_AS_IN_ALT1].s = "Capture Active",
	{ },
};

static struct usb_gadget_strings str_fn = {
	.language = 0x0409,	/* en-us */
	.strings = strings_fn,
};

static struct usb_gadget_strings *fn_strings[] = {
	&str_fn,
	NULL,
};

static struct usb_ss_ep_comp_descriptor ss_epout_comp_desc = {
	.bLength =		 USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	 USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_epin_comp_desc = {
	.bLength =		 USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	 USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(4),
};

static struct usb_interface_assoc_descriptor iad_desc = {
	.bLength = sizeof(iad_desc),
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,

	.bFirstInterface = 0,
	.bInterfaceCount = 2,
	.bFunctionClass = USB_CLASS_AUDIO,
	.bFunctionSubClass = UAC2_FUNCTION_SUBCLASS_UNDEFINED,
	.bFunctionProtocol = UAC_VERSION_2,
};

/* Audio Control Interface */
static struct usb_interface_descriptor std_ac_if_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Clock source for OUT traffic */
static struct uac_clock_source_descriptor out_clk_src_desc = {
	.bLength = sizeof(out_clk_src_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	.bClockID = USB_OUT_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_PROG,
	.bmControls = 7,
	.bAssocTerminal = 0,
};

/* Clock Selector for OUT traffic */
struct uac2_clock_selector_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bClockID;
	__u8 bNrInPins;
	__u8 baCSourceID[3];
	/* bmControls and iClockSource omitted */
} __attribute__((packed));

static struct uac2_clock_selector_descriptor out_clk_sel_desc = {
	.bLength = sizeof(out_clk_sel_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC2_CLOCK_SELECTOR,
	.bClockID = USB_OUT_CLK_SEL_ID,
	.bNrInPins = 0x01,
	/* .baCSourceID[] = */
	{ USB_OUT_CLK_ID, 0x03, 0x00 },
};

/* Input Terminal for USB_OUT */
static struct uac2_input_terminal_descriptor usb_out_it_desc = {
	.bLength = sizeof(usb_out_it_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	.bTerminalID = USB_OUT_IT_ID,
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = 0,
	.bCSourceID = USB_OUT_CLK_SEL_ID,
	.iChannelNames = 0,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

/* Output Terminal for USB_IN */
static struct uac2_output_terminal_descriptor usb_in_ot_desc = {
	.bLength = sizeof(usb_in_ot_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	.bTerminalID = USB_IN_OT_ID,
	.wTerminalType = cpu_to_le16(UAC_OUTPUT_TERMINAL_SPEAKER),
	.bAssocTerminal = 0,
	.bSourceID = USB_FEATURE_UNIT_ID,
	.bCSourceID = USB_OUT_CLK_ID,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

struct uac2_bmaControls_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bUnitID;
	__u8 bSourceID;
	/* bmaControls is actually u32,
	 * but u8 is needed for the hybrid parser */
	__u8 bmaControls[13]; /* variable length */
} __attribute__((packed));

static struct uac2_bmaControls_descriptor funit_desc = {
	.bLength = sizeof(funit_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FEATURE_UNIT,
	.bUnitID = USB_FEATURE_UNIT_ID,
	.bSourceID = USB_OUT_IT_ID,
	/* .bmaControls[]= */
	{0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00,
	 0x00}, /* variable length */
};

static struct uac2_ac_header_descriptor ac_hdr_desc = {
	.bLength = sizeof(ac_hdr_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_MS_HEADER,
	.bcdADC = cpu_to_le16(0x200),
	.bCategory = UAC2_FUNCTION_IO_BOX,
	.wTotalLength = sizeof(ac_hdr_desc) + sizeof(out_clk_src_desc)
			+ sizeof(funit_desc) + sizeof(out_clk_sel_desc)
			+ sizeof(usb_out_it_desc) + sizeof(usb_in_ot_desc),
	.bmControls = 0,
};

/* Audio Streaming OUT Interface - Alt0 */
static struct usb_interface_descriptor std_as_out_if0_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Audio Streaming OUT Interface - Alt1-7 */
#define USB_IF_STD_AS_OUT_IF_DESC(alt_n) \
	static struct usb_interface_descriptor std_as_out_if##alt_n##_desc = { \
		.bLength = USB_DT_INTERFACE_SIZE, \
		.bDescriptorType = USB_DT_INTERFACE, \
					\
		.bAlternateSetting = ( alt_n ), \
		.bNumEndpoints = 2, \
		.bInterfaceClass = USB_CLASS_AUDIO, \
		.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING, \
		.bInterfaceProtocol = UAC_VERSION_2, \
	} \

USB_IF_STD_AS_OUT_IF_DESC(1);
USB_IF_STD_AS_OUT_IF_DESC(2);
USB_IF_STD_AS_OUT_IF_DESC(3);
USB_IF_STD_AS_OUT_IF_DESC(4);
USB_IF_STD_AS_OUT_IF_DESC(5);
USB_IF_STD_AS_OUT_IF_DESC(6);
USB_IF_STD_AS_OUT_IF_DESC(7);

/* Audio Stream OUT Intface Desc */
static struct uac2_as_header_descriptor as_out_hdr_desc = {
	.bLength = sizeof(as_out_hdr_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	.bTerminalLink = USB_OUT_IT_ID,
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = cpu_to_le32(UAC_FORMAT_TYPE_I_PCM),
	.iChannelNames = 0,
};

/* Audio Stream OUT Interface Desc for RAW data */
static struct uac2_as_header_descriptor as_out_raw_hdr_desc = {
	.bLength = sizeof(as_out_raw_hdr_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	.bTerminalLink = USB_OUT_IT_ID,
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = 0x80000000, /* cpu_to_le32(UAC_FORMAT_TYPE_I_PCM), */
	.iChannelNames = 0,
};

/* Audio USB_OUT Format - Alt1-7 */
#define UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(alt_n, slot, bres) \
	static struct uac2_format_type_i_descriptor as_out_fmt##alt_n##_desc = { \
		.bLength = sizeof as_out_fmt##alt_n##_desc, \
		.bDescriptorType = USB_DT_CS_INTERFACE, \
							\
		.bDescriptorSubtype = UAC_FORMAT_TYPE, \
		.bFormatType = UAC_FORMAT_TYPE_I, \
		.bSubslotSize = ( slot ), \
		.bBitResolution = ( bres ), \
	} \

UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(1, 4, 32);
UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(2, 4, 24);
UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(3, 4, 16);
UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(4, 3, 24);
UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(5, 3, 16);
UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(6, 2, 16);
UAC2_FORMAT_TYPE_I_AS_OUT_FMT_DESC(7, 4, 32);

/* STD AS ISO OUT Endpoint */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize = cpu_to_le16(1023),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize = cpu_to_le16(1024),
	.bInterval = 1,
};


/* CS AS ISO OUT Endpoint */
static struct uac2_iso_endpoint_descriptor as_iso_out_desc = {
	.bLength = sizeof(as_iso_out_desc),
	.bDescriptorType = USB_DT_CS_ENDPOINT,

	.bDescriptorSubtype = UAC_EP_GENERAL,
	.bmAttributes = 0,
	.bmControls = 0,
	.bLockDelayUnits = 0x02,
	.wLockDelay = 0x08,
};


/* STD AS ISO IN Endpoint */
static struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_USAGE_FEEDBACK,
	.wMaxPacketSize = 4,
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_USAGE_FEEDBACK,
	.wMaxPacketSize = 4,
	.bInterval = 4,
};

static struct uac2_format_type_i_descriptor *AltDes[] = {
	NULL,
	&as_out_fmt1_desc,
	&as_out_fmt2_desc,
	&as_out_fmt3_desc,
	&as_out_fmt4_desc,
	&as_out_fmt5_desc,
	&as_out_fmt6_desc,
	&as_out_fmt7_desc,
	NULL,
};

#define MAX_ALT_NUM ((sizeof(AltDes)/sizeof(struct uac2_format_type_i_descriptor *)) - 2)

#define AUDIO_DESC_ALT( speed, alt_n, hdr ) \
(struct usb_descriptor_header *)&std_as_out_if##alt_n##_desc, \
(struct usb_descriptor_header *)&as_out_##hdr##_desc, \
(struct usb_descriptor_header *)&as_out_fmt##alt_n##_desc, \
(struct usb_descriptor_header *)&speed##_epout_desc, \
(struct usb_descriptor_header *)&as_iso_out_desc, \
(struct usb_descriptor_header *)&speed##_epin_desc

static struct usb_descriptor_header *fs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_sel_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&funit_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	AUDIO_DESC_ALT(fs, 1, hdr),
	AUDIO_DESC_ALT(fs, 2, hdr),
	AUDIO_DESC_ALT(fs, 3, hdr),
	AUDIO_DESC_ALT(fs, 4, hdr),
	AUDIO_DESC_ALT(fs, 5, hdr),
	AUDIO_DESC_ALT(fs, 6, hdr),
	AUDIO_DESC_ALT(fs, 7, raw_hdr),

	NULL,
};

static struct usb_descriptor_header *hs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_sel_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&funit_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	AUDIO_DESC_ALT(hs, 1, hdr),
	AUDIO_DESC_ALT(hs, 2, hdr),
	AUDIO_DESC_ALT(hs, 3, hdr),
	AUDIO_DESC_ALT(hs, 4, hdr),
	AUDIO_DESC_ALT(hs, 5, hdr),
	AUDIO_DESC_ALT(hs, 6, hdr),
	AUDIO_DESC_ALT(hs, 7, raw_hdr),

	NULL,
};

#define AUDIO_SS_DESC_ALT( alt_n, hdr ) \
(struct usb_descriptor_header *)&std_as_out_if##alt_n##_desc, \
(struct usb_descriptor_header *)&as_out_##hdr##_desc, \
(struct usb_descriptor_header *)&as_out_fmt##alt_n##_desc, \
(struct usb_descriptor_header *)&hs_epout_desc, \
(struct usb_descriptor_header *)&ss_epout_comp_desc, \
(struct usb_descriptor_header *)&as_iso_out_desc, \
(struct usb_descriptor_header *)&hs_epin_desc, \
(struct usb_descriptor_header *)&ss_epin_comp_desc

static struct usb_descriptor_header *ss_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_sel_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&funit_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	AUDIO_SS_DESC_ALT(1, hdr),
	AUDIO_SS_DESC_ALT(2, hdr),
	AUDIO_SS_DESC_ALT(3, hdr),
	AUDIO_SS_DESC_ALT(4, hdr),
	AUDIO_SS_DESC_ALT(5, hdr),
	AUDIO_SS_DESC_ALT(6, hdr),
	AUDIO_SS_DESC_ALT(7, raw_hdr),

	NULL,
};

struct cntrl_cur_lay3 {
	__le32	dCUR;
};

struct uac2_req {
	struct uac2_rtd_params *pp; /* parent param */
	struct usb_request *req;
};

/* Runtime data params for one stream */
struct uac2_rtd_params {
	struct snd_uac2_chip *uac; /* parent chip */
	bool ep_enabled; /* if the ep is enabled */
	bool ep_fb_enabled; /* if the ep is enabled */

	struct snd_pcm_substream *ss;

	/* Ring buffer */
	ssize_t hw_ptr;

	void *rbuf;
	void *rbuf_fb;

	unsigned int max_psize;	/* MaxPacketSize of endpoint */
	unsigned int max_psize_fb;	/* MaxPacketSize of endpoint feedback in */
	struct uac2_req *ureq;
	struct uac2_req *ureq_fb;

	unsigned int default_fb_value;
	int firstCount;

	spinlock_t lock;
};

struct snd_uac2_chip {
	struct g_audio *audio_dev;

	struct uac2_rtd_params c_prm;

	struct snd_card *card;
	struct snd_pcm *pcm;

	struct uac2_uevent uevent;
};

static const struct snd_pcm_hardware uac2_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER
		 | SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID
		 | SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.periods_max = BUFF_SIZE_MAX / PRD_SIZE_MAX,
	.buffer_bytes_max = BUFF_SIZE_MAX,
	.period_bytes_max = PRD_SIZE_MAX,
	.periods_min = MIN_PERIODS,
};

static void u_audio_iso_complete(struct usb_ep *ep, struct usb_request *req)
{
	unsigned int pending;
	unsigned long flags, flags2;
	unsigned int hw_ptr;
	int status = req->status;
	struct uac2_req *ur = req->context;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct uac2_rtd_params *prm = ur->pp;
	struct snd_uac2_chip *uac = prm->uac;

	/* i/f shutting down */
	if (!prm->ep_enabled) {
		usb_ep_free_request(ep, req);
		return;
	}

	if (req->status == -ESHUTDOWN)
		return;

	/*
	 * We can't really do much about bad xfers.
	 * Afterall, the ISOCH xfers could fail legitimately.
	 */
	if (status)
		pr_debug("%s: iso_complete status(%d) %d/%d\n",
			__func__, status, req->actual, req->length);

	substream = prm->ss;

	/* Do nothing if ALSA isn't active */
	if (!substream)
		goto exit;

	snd_pcm_stream_lock_irqsave(substream, flags2);

	runtime = substream->runtime;
	if (!runtime || !snd_pcm_running(substream)) {
		snd_pcm_stream_unlock_irqrestore(substream, flags2);
		goto exit;
	}

	spin_lock_irqsave(&prm->lock, flags);

	hw_ptr = prm->hw_ptr;

	spin_unlock_irqrestore(&prm->lock, flags);

	/* Pack USB load in ALSA ring buffer */
	pending = runtime->dma_bytes - hw_ptr;

	if (unlikely(pending < req->actual)) {
		memcpy(runtime->dma_area + hw_ptr, req->buf, pending);
		memcpy(runtime->dma_area, req->buf + pending,
		       req->actual - pending);
	} else {
		memcpy(runtime->dma_area + hw_ptr, req->buf,
		       req->actual);
	}

	spin_lock_irqsave(&prm->lock, flags);
	/* update hw_ptr after data is copied to memory */
	prm->hw_ptr = (hw_ptr + req->actual) % runtime->dma_bytes;
	hw_ptr = prm->hw_ptr;
	spin_unlock_irqrestore(&prm->lock, flags);
	snd_pcm_stream_unlock_irqrestore(substream, flags2);

	if ((hw_ptr % snd_pcm_lib_period_bytes(substream)) < req->actual)
		snd_pcm_period_elapsed(substream);

exit:
	if (usb_ep_queue(ep, req, GFP_ATOMIC))
		dev_err(uac->card->dev, "%d Error!\n", __LINE__);
}

static unsigned int get_default_fb_value(enum usb_device_speed speed, unsigned int rate)
{
	switch (speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		/* feadback : (rate / 1000) << 14 */
		return ((rate / 100) << 14) / 10;
	case USB_SPEED_SUPER_PLUS:
	case USB_SPEED_SUPER:
	case USB_SPEED_HIGH:
	default:
		/* feadback : (rate / 1000) << 13 */
		return ((rate / 100) << 13) / 10;
	}
}

static void u_audio_iso_fb_complete(struct usb_ep *ep, struct usb_request *req)
{
	unsigned long flags2;
	int status = req->status;
	struct uac2_req *ur = req->context;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct uac2_rtd_params *prm = ur->pp;
	struct snd_uac2_chip *uac = prm->uac;

	unsigned int OriginalClock = 0;
	unsigned int CurrentClock = 0;
	__le32 feedback;

	/* i/f shutting down */
	if (!prm->ep_fb_enabled) {
		usb_ep_free_request(ep, req);
		return;
	}

	if (req->status == -ESHUTDOWN)
		return;

	/*
	 * We can't really do much about bad xfers.
	 * Afterall, the ISOCH xfers could fail legitimately.
	 */
	if (status) {
		pr_debug("%s: iso_fb_complete status(%d) %d/%d\n",
			__func__, status, req->actual, req->length);
	}

	if (prm->firstCount > 0) {
		prm->firstCount--;
		goto exit;
	}

	OriginalClock = prm->default_fb_value;
	CurrentClock = OriginalClock;

	substream = prm->ss;

	/* Do nothing if ALSA isn't active */
	if (!substream) {
		goto exit;
	}

	snd_pcm_stream_lock_irqsave(substream, flags2);

	runtime = substream->runtime;
	if (!runtime || !snd_pcm_running(substream)) {
		snd_pcm_stream_unlock_irqrestore(substream, flags2);
		goto exit;
	}

	snd_pcm_stream_unlock_irqrestore(substream, flags2);

	if (f_plus > 0) {
		CurrentClock = OriginalClock + (OriginalClock * f_plus) / 10000;
		if (CurrentClock >= (OriginalClock + 0xFFFF)) {
			CurrentClock = OriginalClock + 0xFFFF;
		}
	} else if (f_minus > 0) {
		CurrentClock = OriginalClock - (OriginalClock * f_minus) / 10000;
		if (CurrentClock <= (OriginalClock - 0xFFFF)) {
			CurrentClock = OriginalClock - 0xFFFF;
		}
	}

exit:

	feedback = cpu_to_le32(CurrentClock);

	memcpy(req->buf, (void*)&feedback, prm->max_psize_fb);

	if (usb_ep_queue(ep, req, GFP_ATOMIC))
		dev_err(uac->card->dev, "%d Error!\n", __LINE__);
}

static int uac2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_uac2_chip *uac = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;
	struct g_audio *audio_dev;
	struct uac2_params *params;
	unsigned long flags;
	int err = 0;

	audio_dev = uac->audio_dev;
	params = &audio_dev->params;

	prm = &uac->c_prm;

	spin_lock_irqsave(&prm->lock, flags);

	/* Reset */
	prm->hw_ptr = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		prm->ss = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		prm->ss = NULL;
		break;
	default:
		err = -EINVAL;
	}

	spin_unlock_irqrestore(&prm->lock, flags);

	return err;
}

static snd_pcm_uframes_t uac2_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_uac2_chip *uac = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;

	prm = &uac->c_prm;

	return bytes_to_frames(substream->runtime, prm->hw_ptr);
}

static int uac2_pcm_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
					params_buffer_bytes(hw_params));
}

static int uac2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int uac2_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_uac2_chip *uac = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct g_audio *audio_dev;
	struct f_uac2 *uac2;
	struct uac2_params *params;
	int c_ssize;
	int c_srate;
	int c_chmask;

	audio_dev = uac->audio_dev;
	uac2 = g_audio_to_uac2(audio_dev);
	params = &audio_dev->params;
	c_ssize = params->c_ssize;
	c_srate = params->c_srate;
	c_chmask = params->c_chmask;

	runtime->hw = uac2_pcm_hardware;

	spin_lock_init(&uac->c_prm.lock);
	runtime->hw.rate_min = c_srate;
	if (uac2->as_out_alt == DSD_ALT) {
		runtime->hw.formats = SNDRV_PCM_FMTBIT_DSD_U8;
	} else {
		switch (c_ssize) {
		case 3:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S24_3LE;
			break;
		case 4:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
			break;
		default:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
			break;
		}
	}
	runtime->hw.channels_min = num_channels(c_chmask);
	runtime->hw.period_bytes_min = 2 * uac->c_prm.max_psize
					/ runtime->hw.periods_min;

	runtime->hw.rate_max = runtime->hw.rate_min;
	runtime->hw.channels_max = runtime->hw.channels_min;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

/* ALSA cries without these function pointers */
static int uac2_pcm_null(struct snd_pcm_substream *substream)
{
	return 0;
}

static const struct snd_pcm_ops uac2_pcm_ops = {
	.open = uac2_pcm_open,
	.close = uac2_pcm_null,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = uac2_pcm_hw_params,
	.hw_free = uac2_pcm_hw_free,
	.trigger = uac2_pcm_trigger,
	.pointer = uac2_pcm_pointer,
	.prepare = uac2_pcm_null,
};

static inline void free_ep(struct uac2_rtd_params *prm, struct usb_ep *ep)
{
	struct snd_uac2_chip *uac = prm->uac;
	struct g_audio *audio_dev;
	struct uac2_params *params;
	int i;

	if (!prm->ep_enabled)
		return;

	audio_dev = uac->audio_dev;
	params = &audio_dev->params;

	for (i = 0; i < params->req_number; i++) {
		if (prm->ureq[i].req) {
			if (usb_ep_dequeue(ep, prm->ureq[i].req))
				usb_ep_free_request(ep, prm->ureq[i].req);
			/*
			 * If usb_ep_dequeue() cannot successfully dequeue the
			 * request, the request will be freed by the completion
			 * callback.
			 */

			prm->ureq[i].req = NULL;
		}
	}

	prm->ep_enabled = false;

	if (usb_ep_disable(ep))
		dev_err(uac->card->dev, "%s:%d Error!\n", __func__, __LINE__);
}


static inline void free_ep_fb(struct uac2_rtd_params *prm, struct usb_ep *ep)
{
	struct snd_uac2_chip *uac = prm->uac;
	struct g_audio *audio_dev;
	struct uac2_params *params;
	int i;

	if (!prm->ep_fb_enabled)
		return;

	audio_dev = uac->audio_dev;
	params = &audio_dev->params;

	for (i = 0; i < USB_FB_XFERS; i++) {
		if (prm->ureq_fb[i].req) {
			if (usb_ep_dequeue(ep, prm->ureq_fb[i].req))
				usb_ep_free_request(ep, prm->ureq_fb[i].req);
			/*
			 * If usb_ep_dequeue() cannot successfully dequeue the
			 * request, the request will be freed by the completion
			 * callback.
			 */

			prm->ureq_fb[i].req = NULL;
		}
	}

	prm->ep_fb_enabled = false;

	if (usb_ep_disable(ep))
		dev_err(uac->card->dev, "%s:%d Error!\n", __func__, __LINE__);
}


static int uac2_audio_start_capture(struct g_audio *audio_dev)
{
	struct snd_uac2_chip *uac = audio_dev->uac;
	struct usb_gadget *gadget = audio_dev->gadget;
	struct device *dev = &gadget->dev;
	struct usb_request *req;
	struct usb_ep *ep;
	struct uac2_rtd_params *prm;
	struct uac2_params *params = &audio_dev->params;
	int i;
	int req_number;

	ep = audio_dev->out_ep;
	prm = &uac->c_prm;
	config_ep_by_speed(gadget, &audio_dev->func, ep);
	config_ep_by_speed(gadget, &audio_dev->func, audio_dev->in_ep);

	prm->ep_enabled = true;
	prm->ep_fb_enabled = true;
	usb_ep_enable(ep);
	usb_ep_enable(audio_dev->in_ep);

	prm->default_fb_value = get_default_fb_value(gadget->speed, params->c_srate);
	prm->firstCount = f_start;

	switch (gadget->speed) {
	case USB_SPEED_SUPER_PLUS:
	case USB_SPEED_SUPER:
	case USB_SPEED_HIGH:
		req_number = params->req_number;
		break;
	default:
		req_number = params->req_number / 8;
		break;
	}

	if (req_number < 2)
		req_number = 2;

	for (i = 0; i < req_number; i++) {
		if (!prm->ureq[i].req) {
			req = usb_ep_alloc_request(ep, GFP_ATOMIC);
			if (req == NULL)
				return -ENOMEM;

			prm->ureq[i].req = req;
			prm->ureq[i].pp = prm;

			req->zero = 0;
			req->context = &prm->ureq[i];
			req->length = prm->max_psize;
			req->complete = u_audio_iso_complete;
			req->buf = prm->rbuf + i * prm->max_psize;
		}

		if (usb_ep_queue(ep, prm->ureq[i].req, GFP_ATOMIC))
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
	}

	for (i = 0; i < USB_FB_XFERS; i++) {
		if (!prm->ureq_fb[i].req) {
			req = usb_ep_alloc_request(audio_dev->in_ep, GFP_ATOMIC);
			if (req == NULL)
				return -ENOMEM;

			prm->ureq_fb[i].req = req;
			prm->ureq_fb[i].pp = prm;

			req->zero = 0;
			req->context = &prm->ureq_fb[i];
			req->length = prm->max_psize_fb;
			req->complete = u_audio_iso_fb_complete;
			req->buf = prm->rbuf_fb + i * prm->max_psize_fb;
			memset(req->buf, 0, prm->max_psize_fb);
		}

		if (usb_ep_queue(audio_dev->in_ep, prm->ureq_fb[i].req, GFP_ATOMIC))
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
	}

	return 0;
}

static void uac2_audio_stop_capture(struct g_audio *audio_dev)
{
	struct snd_uac2_chip *uac = audio_dev->uac;

	free_ep(&uac->c_prm, audio_dev->out_ep);
	free_ep_fb(&uac->c_prm, audio_dev->in_ep);
}

static void uac2_audio_work(struct work_struct *data)
{
	struct uac2_uevent_work_data *p =
		container_of(data, struct uac2_uevent_work_data, work);
	char *uac_event[6]  = { NULL, NULL, NULL, NULL, NULL, NULL };

	uac_event[0] = (char *)UAC2EVENT[uac2_event_map(p->uevent.action)].s;
	uac_event[1] = (char *)UAC2EVENT[uac2_event_map(p->uevent.format)].s;
	uac_event[2] = (char *)UAC2EVENT[uac2_event_map(p->uevent.freq)].s;
	uac_event[3] = (char *)UAC2EVENT[uac2_event_map(p->uevent.bitwidth)].s;
	uac_event[4] = (char *)UAC2EVENT[uac2_event_map(p->uevent.subslot)].s;
	kobject_uevent_env(&sysfs_audio_device->kobj, KOBJ_CHANGE, uac_event);

	kfree(p);
}

static void uevent_send(struct g_audio *g_audio)
{
	struct uac2_uevent_work_data *p;

	p = kzalloc(sizeof(*p), GFP_ATOMIC);
	if (p) {
		INIT_WORK(&p->work, uac2_audio_work);
		p->g_audio = g_audio;
		memcpy((void*)&p->uevent, (void*)&g_audio->uac->uevent,
			sizeof(struct uac2_uevent));
		schedule_work(&p->work);
	}
}

static void uevent_send_play(struct g_audio *g_audio, unsigned alt)
{
	struct snd_uac2_chip *uac = g_audio->uac;

	uac->uevent.action   = STATE_PLAY;
	if (alt == DSD_ALT) {
		uac->uevent.format   = FORMAT_DSD;
		uac->uevent.bitwidth = BIT_1;
	} else {
		uac->uevent.format   = FORMAT_PCM;
		uac->uevent.bitwidth = AltDes[alt]->bBitResolution;
	}
	uac->uevent.subslot  = AltDes[alt]->bSubslotSize;
	uac->uevent.freq     = g_audio->params.c_srate;

	uevent_send(g_audio);
}

static void uevent_send_stop(struct g_audio *g_audio)
{
	struct snd_uac2_chip *uac = g_audio->uac;

	uac->uevent.action   = STATE_STOP;
	uac->uevent.format   = FORMAT_NONE;
	uac->uevent.bitwidth = BIT_NONE;
	uac->uevent.subslot  = SUBSLOT_NONE;
	uac->uevent.freq     = FREQ_NONE;

	uevent_send(g_audio);
}

static ssize_t audio_f_plus_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, 3, "%1d\n", f_plus);
}

static ssize_t audio_f_plus_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int value;
	if (sscanf(buf, "%1d", &value) == 1) {
		if (value <= 5) {
			f_plus = value;
		}
		return size;
	}
	return -1;
}

static DEVICE_ATTR(f_plus, S_IRUGO | S_IWUSR , audio_f_plus_show, audio_f_plus_store);

static ssize_t audio_f_minus_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, 3, "%1d\n", f_minus);
}

static ssize_t audio_f_minus_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int value;
	if (sscanf(buf, "%1d", &value) == 1) {
		if (value <= 5) {
			f_minus = value;
		}
		return size;
	}
	return -1;
}

static DEVICE_ATTR(f_minus, S_IRUGO | S_IWUSR , audio_f_minus_show, audio_f_minus_store);

static ssize_t audio_f_thresh_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, 5, "%3d\n", f_thresh);
}

static ssize_t audio_f_thresh_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int value;
	if (sscanf(buf, "%3d", &value) <= 3) {
		if (value <= 100) {
			f_thresh = value;
		}
		return size;
	}
	return -1;
}

static DEVICE_ATTR(f_thresh, S_IRUGO | S_IWUSR , audio_f_thresh_show, audio_f_thresh_store);

static ssize_t audio_f_start_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return snprintf(buf, 6, "%4d\n", f_start);
}

static ssize_t audio_f_start_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int value;
	if (sscanf(buf, "%4d", &value) <= 4) {
		if (value <= 1000) {
			f_start = value;
		}
		return size;
	}
	return -1;
}

static DEVICE_ATTR(f_start, S_IRUGO | S_IWUSR , audio_f_start_show, audio_f_start_store);

static ssize_t audio_f_valid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 3, "%1d\n", f_valid);
}

static ssize_t audio_f_valid_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int value;
	if (sscanf(buf, "%1d", &value) <= 1) {
		if (value <= 1) {
			f_valid = value;
		}
		return size;
	}
	return -1;
}

static DEVICE_ATTR(f_valid, S_IRUGO | S_IWUSR , audio_f_valid_show, audio_f_valid_store);

static ssize_t audio_f_allow_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 3, "%1d\n", f_allow);
}

static ssize_t audio_f_allow_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int value;
	if (sscanf(buf, "%1d", &value) <= 1) {
		if (value <= 1) {
			f_allow = value;
		}
		return size;
	}
	return -1;
}

static DEVICE_ATTR(f_allow, S_IRUGO | S_IWUSR , audio_f_allow_show, audio_f_allow_store);

static struct device_attribute *audio_function_attributes[] = {
	&dev_attr_f_plus,
	&dev_attr_f_minus,
	&dev_attr_f_thresh,
	&dev_attr_f_start,
	&dev_attr_f_valid,
	&dev_attr_f_allow,
	NULL
};

static int uac2_audio_setup(struct g_audio *g_audio, const char *pcm_name,
					const char *card_name)
{
	struct snd_uac2_chip *uac;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct uac2_params *params;
	struct f_uac2_opts * opts;
	int c_chmask;
	int err;

	if (!g_audio)
		return -EINVAL;

	opts = g_audio_to_uac2_opts(g_audio);
	uac = kzalloc(sizeof(*uac), GFP_KERNEL);
	if (!uac)
		return -ENOMEM;
	g_audio->uac = uac;
	uac->audio_dev = g_audio;

	params = &g_audio->params;
	c_chmask = params->c_chmask;

	if (c_chmask) {
		struct uac2_rtd_params *prm = &uac->c_prm;

		uac->c_prm.uac = uac;
		prm->max_psize = g_audio->out_ep_maxpsize;

		uac->c_prm.ureq = opts->ureq;
		uac->c_prm.rbuf = opts->rbuf;

		switch (g_audio->gadget->speed) {
		case USB_SPEED_FULL:
		case USB_SPEED_LOW:
			prm->max_psize_fb = USB_FB_MAX_PSIZE_FS;
			break;
		case USB_SPEED_SUPER_PLUS:
		case USB_SPEED_SUPER:
		case USB_SPEED_HIGH:
		default:
			prm->max_psize_fb = USB_FB_MAX_PSIZE;
			break;
		}

		uac->c_prm.ureq_fb = opts->ureq_fb;
		uac->c_prm.rbuf_fb = opts->rbuf_fb;
	}

	/* Choose any slot, with no id */
	err = snd_card_new(&g_audio->gadget->dev,
			-1, NULL, THIS_MODULE, 0, &card);
	if (err < 0)
		goto fail;

	uac->card = card;

	/*
	 * Create first PCM device
	 * Create a substream only for non-zero channel streams
	 */
	err = snd_pcm_new(uac->card, pcm_name, 0,
			       0, c_chmask ? 1 : 0, &pcm);
	if (err < 0)
		goto snd_fail;

	strlcpy(pcm->name, pcm_name, sizeof(pcm->name));
	pcm->private_data = uac;
	uac->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &uac2_pcm_ops);

	strlcpy(card->driver, card_name, sizeof(card->driver));
	strlcpy(card->shortname, card_name, sizeof(card->shortname));
	sprintf(card->longname, "%s %i", card_name, card->dev->id);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL), 0, BUFF_SIZE_MAX);

	err = snd_card_register(card);
	if (err < 0)
		goto snd_fail;

	atomic_set(&g_audio->delayed_data_enable, 1);
	atomic_set(&g_audio->delayed_data_pending, 0);
	INIT_WORK(&g_audio->work_delayed_data, uac2_audio_work_delayed_data);

	if (!err)
		return 0;

snd_fail:
	snd_card_free(card);
fail:
	uac->c_prm.ureq = NULL;
	uac->c_prm.ureq_fb = NULL;
	uac->c_prm.rbuf = NULL;
	uac->c_prm.rbuf_fb = NULL;
	kfree(uac);

	return err;
}

static void uac2_audio_cleanup(struct g_audio *g_audio)
{
	struct snd_uac2_chip *uac;
	struct snd_card *card;
	struct usb_function * fn = &g_audio->func;
	struct usb_composite_dev *cdev = fn->config->cdev;
	unsigned long flags;

	if (!g_audio || !g_audio->uac)
		return;

	if (atomic_read(&g_audio->delayed_data_pending)) {
		spin_lock_irqsave(&cdev->lock, flags);
		usb_ep_dequeue(cdev->gadget->ep0, cdev->req);
		spin_unlock_irqrestore(&cdev->lock, flags);
	}

	atomic_set(&g_audio->delayed_data_enable, 0);
	cancel_work_sync(&g_audio->work_delayed_data);

	stop_capture(g_audio);
	flush_scheduled_work();

	uac = g_audio->uac;
	card = uac->card;
	if (card)
		snd_card_free(card);

	uac->c_prm.ureq = NULL;
	uac->c_prm.ureq_fb = NULL;
	uac->c_prm.rbuf = NULL;
	uac->c_prm.rbuf_fb = NULL;
	kfree(uac);
}

static int start_capture(struct g_audio *audio_dev, unsigned alt, bool set_alt_0_to_n)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&audio_dev->capture_mutex, flags);

	if (set_alt_0_to_n)
		audio_dev->alt_0_to_n = 1;

	pr_debug("%s: flags: alt_0_to_n %u, clock_valid %u, capturing %u\n",
		__func__, audio_dev->alt_0_to_n, audio_dev->clock_valid, audio_dev->capturing);

	if (!audio_dev->alt_0_to_n  ||
	    !audio_dev->clock_valid ||
	    audio_dev->capturing) {
		pr_debug("%s: SKIP \n", __func__);
	} else {
		pr_debug("%s: CAPTURE \n", __func__);
		audio_dev->capturing = 1;
		audio_dev->params.c_ssize = AltDes[alt]->bSubslotSize;
		ret = uac2_audio_start_capture(audio_dev);
		uevent_send_play(audio_dev, alt);
	}

	spin_unlock_irqrestore(&audio_dev->capture_mutex, flags);

	return ret;
}

static void stop_capture(struct g_audio *audio_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&audio_dev->capture_mutex, flags);

	audio_dev->alt_0_to_n = 0;

	pr_debug("%s: flags: alt_0_to_n %u, clock_valid %u, capturing %u\n",
		__func__, audio_dev->alt_0_to_n, audio_dev->clock_valid, audio_dev->capturing);

	if (audio_dev->capturing) {
		uevent_send_stop(audio_dev);
		uac2_audio_stop_capture(audio_dev);
		audio_dev->capturing = 0;
		pr_debug("CAPTURE STOP\n");
	}

	spin_unlock_irqrestore(&audio_dev->capture_mutex, flags);
}

static void set_ep_max_packet_size(const struct f_uac2_opts *uac2_opts,
	struct usb_endpoint_descriptor *ep_desc,
	unsigned int factor)
{
	int chmask, srate, ssize;
	u16 max_packet_size;

	chmask = uac2_opts->c_chmask;
	srate = uac2_opts->c_srate;
	ssize = uac2_opts->c_ssize;

	max_packet_size = num_channels(chmask) * ssize *
		DIV_ROUND_UP(srate, factor / (1 << (ep_desc->bInterval - 1)));
	ep_desc->wMaxPacketSize = le16_to_cpu(ep_desc->wMaxPacketSize);
}

/* Use macro to overcome line length limitation */
#define USBDHDR(p) (struct usb_descriptor_header *)(p)

static void setup_descriptor(struct f_uac2_opts *opts)
{
	return;
}

static int
afunc_bind(struct usb_configuration *cfg, struct usb_function *fn)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct g_audio *agdev = func_to_g_audio(fn);
	struct usb_composite_dev *cdev = cfg->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &gadget->dev;
	struct f_uac2_opts *uac2_opts;
	struct usb_string *us;
	int ret;

	dev_dbg(dev, "%s:%d\n", __func__, __LINE__);
	uac2_opts = container_of(fn->fi, struct f_uac2_opts, func_inst);

	us = usb_gstrings_attach(cdev, fn_strings, ARRAY_SIZE(strings_fn));
	if (IS_ERR(us)) {
		dev_err(dev, "%s:%d Error usb_gstrings_attach()\n", __func__, __LINE__);
		return PTR_ERR(us);
	}
	iad_desc.iFunction = us[STR_ASSOC].id;
	std_ac_if_desc.iInterface = us[STR_IF_CTRL].id;
	out_clk_src_desc.iClockSource = us[STR_CLKSRC_OUT].id;
	usb_out_it_desc.iTerminal = us[STR_USB_IT].id;
	usb_in_ot_desc.iTerminal = us[STR_USB_OT].id;
	std_as_out_if0_desc.iInterface = us[STR_AS_OUT_ALT0].id;
	std_as_out_if1_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_out_if2_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_out_if3_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_out_if4_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_out_if5_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_out_if6_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_out_if7_desc.iInterface = us[STR_AS_OUT_ALT1].id;

	/* Initialize the configurable parameters */
	usb_out_it_desc.bNrChannels = num_channels(uac2_opts->c_chmask);
	usb_out_it_desc.bmChannelConfig = cpu_to_le32(uac2_opts->c_chmask);
	as_out_hdr_desc.bNrChannels = num_channels(uac2_opts->c_chmask);
	as_out_hdr_desc.bmChannelConfig = cpu_to_le32(uac2_opts->c_chmask);
	as_out_raw_hdr_desc.bNrChannels = num_channels(uac2_opts->c_chmask);
	as_out_raw_hdr_desc.bmChannelConfig = cpu_to_le32(uac2_opts->c_chmask);

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}

	std_ac_if_desc.bInterfaceNumber = ret;
	iad_desc.bFirstInterface = ret;
	uac2->ac_intf = ret;
	uac2->ac_alt = 0;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_as_out_if0_desc.bInterfaceNumber = ret;
	std_as_out_if1_desc.bInterfaceNumber = ret;
	std_as_out_if2_desc.bInterfaceNumber = ret;
	std_as_out_if3_desc.bInterfaceNumber = ret;
	std_as_out_if4_desc.bInterfaceNumber = ret;
	std_as_out_if5_desc.bInterfaceNumber = ret;
	std_as_out_if6_desc.bInterfaceNumber = ret;
	std_as_out_if7_desc.bInterfaceNumber = ret;
	uac2->as_out_intf = ret;
	uac2->as_out_alt = 0;

	/* Calculate wMaxPacketSize according to audio bandwidth */
	set_ep_max_packet_size(uac2_opts, &fs_epout_desc, 1000);
	set_ep_max_packet_size(uac2_opts, &hs_epout_desc, 8000);

	agdev->out_ep = usb_ep_autoconfig(gadget, &hs_epout_desc);
	if (!agdev->out_ep) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -ENODEV;
	}

	agdev->in_ep = usb_ep_autoconfig(gadget, &hs_epin_desc);
	if (!agdev->in_ep) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -ENODEV;
	}

	fs_epout_desc.bEndpointAddress = hs_epout_desc.bEndpointAddress;
	fs_epin_desc.bEndpointAddress = hs_epin_desc.bEndpointAddress;

	fs_epin_desc.wMaxPacketSize = USB_FB_MAX_PSIZE_FS;
	hs_epin_desc.wMaxPacketSize = USB_FB_MAX_PSIZE;

	agdev->in_ep_maxpsize = max_t(u16,
				le16_to_cpu(fs_epin_desc.wMaxPacketSize),
				le16_to_cpu(hs_epin_desc.wMaxPacketSize));
	agdev->out_ep_maxpsize = max_t(u16,
				le16_to_cpu(fs_epout_desc.wMaxPacketSize),
				le16_to_cpu(hs_epout_desc.wMaxPacketSize));

	if ((agdev->in_ep_maxpsize > UAC2_GADGET_ISO_FB_SIZE_MAX) ||
		(agdev->out_ep_maxpsize > UAC2_GADGET_ISO_DATA_SIZE_MAX)) {
		dev_err(dev, "%s:%d Error Invalid wMaxPacketSize settings.\n", __func__, __LINE__);
		return -EINVAL;
	}

	setup_descriptor(uac2_opts);

	ret = usb_assign_descriptors(fn, fs_audio_desc, hs_audio_desc,
					ss_audio_desc, NULL);
	if (ret) {
		dev_err(dev, "%s:%d Error usb_assign_descriptors()\n", __func__, __LINE__);
		return ret;
	}

	agdev->gadget = gadget;

	agdev->params.c_chmask = uac2_opts->c_chmask;
	agdev->params.c_srate = uac2_opts->c_srate;
	agdev->params.c_ssize = uac2_opts->c_ssize;
	agdev->params.req_number = uac2_opts->req_number;
	ret = uac2_audio_setup(agdev, "UAC2 PCM", "UAC2_Gadget");
	if (ret) {
		dev_err(dev, "%s:%d Error uac2_audio_setup()\n", __func__, __LINE__);
		goto err_free_descs;
	}
	return 0;

err_free_descs:
	usb_free_all_descriptors(fn);
	agdev->gadget = NULL;
	return ret;
}

static int
change_alt(struct g_audio *agdev, unsigned alt)
{
	static unsigned last_alt;
	int ret = 0;

	pr_debug("#### alt%u --> alt%u\n", last_alt, alt);

	if (!last_alt && alt) {
		ret = start_capture(agdev, alt, true);
	} else {
		stop_capture(agdev);
	}

	last_alt = alt;

	return ret;
}

static int
afunc_set_alt(struct usb_function *fn, unsigned intf, unsigned alt)
{
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &gadget->dev;
	struct g_audio *agdev = func_to_g_audio(fn);
	int ret = 0;

	/* No i/f has more than 2 alt settings */
	if (alt > MAX_ALT_NUM) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (intf == uac2->ac_intf) {
		/* Control I/f has only 1 AltSetting - 0 */
		if (alt) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}
		return 0;
	}

	if (intf == uac2->as_out_intf) {
		uac2->as_out_alt = alt;
		ret = change_alt(agdev, alt);
	} else if (intf == uac2->as_in_intf) {
		uac2->as_in_alt = alt;
	} else {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	return ret;
}

static int
afunc_get_alt(struct usb_function *fn, unsigned intf)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct g_audio *agdev = func_to_g_audio(fn);

	if (intf == uac2->ac_intf)
		return uac2->ac_alt;
	else if (intf == uac2->as_out_intf)
		return uac2->as_out_alt;
	else if (intf == uac2->as_in_intf)
		return uac2->as_in_alt;
	else
		dev_err(&agdev->gadget->dev,
			"%s:%d Invalid Interface %d!\n",
			__func__, __LINE__, intf);

	return -EINVAL;
}

static void
afunc_disable(struct usb_function *fn)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);

	uac2->as_in_alt = 0;
	uac2->as_out_alt = 0;
	uac2_audio_stop_capture(&uac2->g_audio);
}

static int
in_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct f_uac2_opts *opts;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;
	int c_srate;
	unsigned long flags;

	opts = g_audio_to_uac2_opts(agdev);
	c_srate = opts->c_srate;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		struct cntrl_cur_lay3 c;
		memset(&c, 0, sizeof(struct cntrl_cur_lay3));

		if (entity_id == USB_IN_CLK_ID)
			dev_err(&agdev->gadget->dev,
				"%s:%d USB_IN_CLK_ID is not supported\n", __func__, __LINE__);
		else if (entity_id == USB_OUT_CLK_ID)
			c.dCUR = cpu_to_le32(c_srate);
		else if (entity_id == USB_OUT_CLK_SEL_ID) {
			*(u8 *)req->buf = 1;
			value = min_t(unsigned, w_length, 1);
			return value;
		} else {
			dev_err(&agdev->gadget->dev,
				"%s:%d unknown entity_id %u\n", __func__, __LINE__, entity_id);
		}

		value = min_t(unsigned, w_length, sizeof(c));
		memcpy(req->buf, &c, value);
	} else if (control_selector == UAC2_CS_CONTROL_CLOCK_VALID) {
		spin_lock_irqsave(&agdev->capture_mutex, flags);
		*(u8 *)req->buf = agdev->clock_valid;
		spin_unlock_irqrestore(&agdev->capture_mutex, flags);
		value = min_t(unsigned, w_length, (f_valid)? 1 : 0);
		start_capture(agdev, uac2->as_out_alt, false);
	} else {
		dev_err(&agdev->gadget->dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
in_rq_range(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct usb_gadget *gadget = agdev->gadget;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;
	u8 r_high[] = {
		0x08, 0x00,		/* wNumSubRanges */

		0x44, 0xAC, 0x00, 0x00,	/* dMIN 44100Hz */
		0x44, 0xAC, 0x00, 0x00,	/* dMAX 44100Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 44100Hz */

		0x80, 0xBB, 0x00, 0x00,	/* dMIN 48000Hz */
		0x80, 0xBB, 0x00, 0x00,	/* dMAX 48000Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 48000Hz */

		0x88, 0x58, 0x01, 0x00,	/* dMIN 88200Hz */
		0x88, 0x58, 0x01, 0x00,	/* dMAX 88200Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 88200Hz */

		0x00, 0x77, 0x01, 0x00,	/* dMIN 96000Hz */
		0x00, 0x77, 0x01, 0x00,	/* dMAX 96000Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 96000Hz */

		0x10, 0xB1, 0x02, 0x00,	/* dMIN 176400Hz */
		0x10, 0xB1, 0x02, 0x00,	/* dMAX 176400Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 176400Hz */

		0x00, 0xEE, 0x02, 0x00,	/* dMIN 192000Hz */
		0x00, 0xEE, 0x02, 0x00,	/* dMAX 192000Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 192000Hz */

		0x20, 0x62, 0x05, 0x00,	/* dMIN 352800Hz */
		0x20, 0x62, 0x05, 0x00,	/* dMAX 352800Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 352800Hz */

		0x00, 0xDC, 0x05, 0x00,	/* dMIN 384000Hz */
		0x00, 0xDC, 0x05, 0x00,	/* dMAX 384000Hz */
		0x00, 0x00, 0x00, 0x00 	/* dRES 384000Hz */
	};
	u8 r_full[]= {
		0x04, 0x00,		/* wNumSubRanges */

		0x44, 0xAC, 0x00, 0x00,	/* dMIN 44100Hz */
		0x44, 0xAC, 0x00, 0x00,	/* dMAX 44100Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 44100Hz */

		0x80, 0xBB, 0x00, 0x00,	/* dMIN 48000Hz */
		0x80, 0xBB, 0x00, 0x00,	/* dMAX 48000Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 48000Hz */

		0x88, 0x58, 0x01, 0x00,	/* dMIN 88200Hz */
		0x88, 0x58, 0x01, 0x00,	/* dMAX 88200Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 88200Hz */

		0x00, 0x77, 0x01, 0x00,	/* dMIN 96000Hz */
		0x00, 0x77, 0x01, 0x00,	/* dMAX 96000Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 96000Hz */
	};
	u8 r_DSD[]= {
		0x03,0x00,		/* wNumSubRanges */

		0x88, 0x58, 0x01, 0x00,	/* dMIN 88200Hz */
		0x88, 0x58, 0x01, 0x00,	/* dMAX 88200Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 88200Hz */

		0x10, 0xB1, 0x02, 0x00,	/* dMIN 176400Hz */
		0x10, 0xB1, 0x02, 0x00,	/* dMAX 176400Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 176400Hz */

		0x20, 0x62, 0x05, 0x00,	/* dMIN 352800Hz */
		0x20, 0x62, 0x05, 0x00,	/* dMAX 352800Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 352800Hz */
	};
	u8 r_full_DSD[]= {
		0x01,0x00,		/* wNumSubRanges */

		0x88, 0x58, 0x01, 0x00,	/* dMIN 88200Hz */
		0x88, 0x58, 0x01, 0x00,	/* dMAX 88200Hz */
		0x00, 0x00, 0x00, 0x00,	/* dRES 88200Hz */
	};
	pr_debug("##############%s:%d control_selector = 0x%0X!!!\n", __func__, __LINE__, control_selector);
	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		switch (gadget->speed) {
		case USB_SPEED_FULL:
		case USB_SPEED_LOW:
			if (DSD_ALT == uac2->as_out_alt) {
				value = min_t(unsigned, w_length, sizeof r_full_DSD);
				memcpy(req->buf, &r_full_DSD, value);
			} else {
				value = min_t(unsigned, w_length, sizeof r_full);
				memcpy(req->buf, &r_full, value);
			}
			break;
		case USB_SPEED_SUPER_PLUS:
		case USB_SPEED_SUPER:
		case USB_SPEED_HIGH:
		default:
			if (DSD_ALT == uac2->as_out_alt) {
				value = min_t(unsigned, w_length, sizeof r_DSD);
				memcpy(req->buf, &r_DSD, value);
			} else {
				value = min_t(unsigned, w_length, sizeof(r_high));
				memcpy(req->buf, &r_high, value);
			}
			break;
		}
	} else {
		dev_err(&agdev->gadget->dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}
	pr_debug("##############%s:%d value = 0x%0X!!!\n", __func__, __LINE__, value);
	return value;
}

static int
ac_rq_in(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	if (cr->bRequest == UAC2_CS_CUR)
		return in_rq_cur(fn, cr);
	else if (cr->bRequest == UAC2_CS_RANGE)
		return in_rq_range(fn, cr);
	else
		return -EOPNOTSUPP;
}

static void uac2_set_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_function *fn = ep->driver_data;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct f_uac2_opts *opts;
	unsigned long flags;

	atomic_set(&agdev->delayed_data_pending, 0);

	if (!atomic_read(&agdev->delayed_data_enable))
		return;

	opts = g_audio_to_uac2_opts(agdev);

	pr_debug("[UAC] %s %dHz! @Line %d\n", __func__, agdev->params.c_srate, __LINE__);
	memcpy(&agdev->params.c_srate, req->buf, req->actual);
	pr_debug("[UAC] %s %dHz! @Line %d\n", __func__, agdev->params.c_srate, __LINE__);

	spin_lock_irqsave(&opts->lock, flags);
	opts->c_srate = uac2->g_audio.params.c_srate;
	spin_unlock_irqrestore(&opts->lock, flags);

	if (uac2->as_out_alt) {
		pr_debug("%s: UAC2: Start on SetCur(SAM_FREQ) compelete.\n", __func__);
		start_capture(agdev, uac2->as_out_alt, true);
	} else {
		/* uevent should not be issued here during ALT0. */
		/* set_alt will do it instead in advance. */
		pr_debug("%s: UAC2: No Start on SetCur(SAM_FREQ) compelete.\n", __func__);
	}
}

static void uac2_audio_work_delayed_data(struct work_struct *data)
{
	struct g_audio *g_audio = container_of(data, struct g_audio, work_delayed_data);
	struct usb_function * fn = &g_audio->func;
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct usb_request *req;
	int value;
	unsigned long flags;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(UAC2_GADGET_DELAYED_DATA_TIMEOUT);
	while (1) {
		if (time_after(jiffies, timeout))
			break;

		if (f_allow)
			break;

		if (!atomic_read(&g_audio->delayed_data_enable))
			return;

		msleep(UAC2_GADGET_DELAYED_DATA_INTERVAL);
	}

	if (!atomic_read(&g_audio->delayed_data_enable))
		return;

	spin_lock_irqsave(&cdev->lock, flags);

	/* Supported only SetCur SAM_FREQ. wLength = 4. */
	req = cdev->req;
	req->length = 4;
	req->zero = 0;
	req->context = cdev;
	value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
	if (value < 0) {
		dev_err(&g_audio->gadget->dev,
			"%s:%d Error!\n", __func__, __LINE__);
		req->status = 0;
	} else {
		atomic_set(&g_audio->delayed_data_pending, 1);
	}

	spin_unlock_irqrestore(&cdev->lock, flags);
}

static int
out_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct usb_request *req = fn->config->cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;

	if (((cr->bRequestType & (0x01 << 7)) == USB_DIR_OUT) &&
		(cr->bRequest == UAC2_CS_CUR))  /* set cur */
		if (entity_id == USB_OUT_CLK_ID)
			if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
				fn->config->cdev->gadget->ep0->driver_data = fn;
				req->complete = uac2_set_complete;
				if (uac2->as_out_alt) {
					pr_debug("%s: UAC2: Stop (and Play) on SetCur(SAM_FREQ).\n", __func__);
					f_allow = 0;
					stop_capture(agdev);
					schedule_work(&agdev->work_delayed_data);
					return UAC2_GADGET_DELAYED_DATA;
				} else {
					pr_debug("%s: UAC2: Walkthrough SetCur(SAM_FREQ).\n", __func__);
					return w_length;
				}
			}
	
	/* If control_selector is same as UAC2_CS_CONTROL_SAM_FREQ
	 * regardless of entity_id, return supported. */
	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ)
		return w_length;

	return -EOPNOTSUPP;
}

static int
setup_rq_inf(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct g_audio *agdev = func_to_g_audio(fn);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u8 intf = w_index & 0xff;

	if (intf != uac2->ac_intf) {
		dev_err(&agdev->gadget->dev,
			"%s:%d Error!\n", __func__, __LINE__);
		return -EOPNOTSUPP;
	}

	if (cr->bRequestType & USB_DIR_IN)
		return ac_rq_in(fn, cr);
	else if (cr->bRequest == UAC2_CS_CUR)
		return out_rq_cur(fn, cr);

	return -EOPNOTSUPP;
}

static int
afunc_setup(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	int value = -EOPNOTSUPP;

	/* Only Class specific requests are supposed to reach here */
	if ((cr->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
		return -EOPNOTSUPP;

	if ((cr->bRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE)
		value = setup_rq_inf(fn, cr);
	else
		dev_err(&agdev->gadget->dev, "%s:%d Error!\n",
				__func__, __LINE__);

	if (value == UAC2_GADGET_DELAYED_DATA) {
		return w_length;
	} else if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			dev_err(&agdev->gadget->dev,
				"%s:%d Error!\n", __func__, __LINE__);
			req->status = 0;
		}
	}

	return value;
}

static inline struct f_uac2_opts *to_f_uac2_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uac2_opts,
			    func_inst.group);
}

static void f_uac2_attr_release(struct config_item *item)
{
	struct f_uac2_opts *opts = to_f_uac2_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations f_uac2_item_ops = {
	.release	= f_uac2_attr_release,
};

static struct configfs_attribute *f_uac2_attrs[] = {
	NULL,
};

static const struct config_item_type f_uac2_func_type = {
	.ct_item_ops	= &f_uac2_item_ops,
	.ct_attrs	= f_uac2_attrs,
	.ct_owner	= THIS_MODULE,
};

static void afunc_free_inst(struct usb_function_instance *f)
{
	struct f_uac2_opts *opts;

	if (sysfs_audio_class && !IS_ERR(sysfs_audio_class)) {
		if (sysfs_audio_device && !IS_ERR(sysfs_audio_device)) {
			device_destroy(sysfs_audio_class, MKDEV(0, 0));
			sysfs_audio_device = NULL;
		}
		class_destroy(sysfs_audio_class);
		sysfs_audio_class = NULL;
	}

	opts = container_of(f, struct f_uac2_opts, func_inst);
	kfree(opts->ureq);
	kfree(opts->rbuf);
	kfree(opts->ureq_fb);
	kfree(opts->rbuf_fb);
	kfree(opts);
}

static struct usb_function_instance *afunc_alloc_inst(void)
{
	struct f_uac2_opts *opts;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&opts->lock);

	opts->func_inst.free_func_inst = afunc_free_inst;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_uac2_func_type);

	opts->c_chmask = UAC2_DEF_CCHMASK;
	opts->c_srate = UAC2_DEF_CSRATE;
	opts->c_ssize = UAC2_DEF_CSSIZE;
	opts->req_number = UAC2_DEF_REQ_NUM;

	opts->ureq = kcalloc(opts->req_number, sizeof(struct uac2_req),
			GFP_KERNEL);
	if (!opts->ureq) {
		pr_err("%s: ERROR UAC2 kcalloc ureq\n", __func__);
		goto fail;
	}

	opts->rbuf = kcalloc(opts->req_number, UAC2_GADGET_ISO_DATA_SIZE_MAX,
			GFP_KERNEL);
	if (!opts->rbuf) {
		pr_err("%s: ERROR UAC2 kcalloc rbuf\n", __func__);
		goto fail;
	}

	opts->ureq_fb = kcalloc(USB_FB_XFERS, sizeof(struct uac2_req),
			GFP_KERNEL);
	if (!opts->ureq_fb) {
		pr_err("%s: ERROR UAC2 kcalloc ureq_fb\n", __func__);
		goto fail;
	}

	opts->rbuf_fb = kcalloc(USB_FB_XFERS, UAC2_GADGET_ISO_FB_SIZE_MAX,
			GFP_KERNEL);
	if (!opts->rbuf_fb) {
		pr_err("%s: ERROR UAC2 kcalloc rbuf_fb\n", __func__);
		goto fail;
	}

	sysfs_audio_class = class_create(THIS_MODULE, "uac2_audio");
	if (IS_ERR(sysfs_audio_class)) {
		pr_err("%s: ERROR UAC2 class_create\n", __func__);
		sysfs_audio_class = NULL;
	} else {
		sysfs_audio_device = device_create(sysfs_audio_class, NULL, MKDEV(0, 0), NULL,
										"UAC2_Gadget 0");
		if (IS_ERR(sysfs_audio_device)) {
			pr_err("%s: ERROR UAC2 device_create\n", __func__);
			sysfs_audio_device = NULL;
		} else {
			attrs = audio_function_attributes;
			if (attrs) {
				while ((attr = *attrs++)) {
					if (device_create_file(sysfs_audio_device, attr)) {
						pr_err("%s: ERROR UAC2 device_create_file\n", __func__);
						break;
					}
				}
			}
		}
	}

	return &opts->func_inst;

fail:
	kfree(opts->ureq);
	kfree(opts->rbuf);
	kfree(opts->ureq_fb);
	kfree(opts->rbuf_fb);
	kfree(opts);

	return ERR_PTR(-ENOMEM);
}

static void afunc_free(struct usb_function *f)
{
	struct g_audio *agdev;
	struct f_uac2_opts *opts;
	unsigned long flags;

	agdev = func_to_g_audio(f);
	opts = container_of(f->fi, struct f_uac2_opts, func_inst);
	spin_lock_irqsave(&opts->lock, flags);
	--opts->refcnt;
	spin_unlock_irqrestore(&opts->lock, flags);

	kfree(agdev);
	agdev = NULL;
}

static void afunc_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct g_audio *agdev = func_to_g_audio(f);

	uac2_audio_cleanup(agdev);
	usb_free_all_descriptors(f);

	agdev->gadget = NULL;
}

static struct usb_function *afunc_alloc(struct usb_function_instance *fi)
{
	struct f_uac2	*uac2;
	struct f_uac2_opts *opts;
	unsigned long flags;

	uac2 = kzalloc(sizeof(*uac2), GFP_KERNEL);
	if (uac2 == NULL)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_uac2_opts, func_inst);
	spin_lock_irqsave(&opts->lock, flags);
	++opts->refcnt;
	spin_unlock_irqrestore(&opts->lock, flags);

	spin_lock_init(&uac2->g_audio.capture_mutex);

	uac2->g_audio.clock_valid = 1;

	uac2->g_audio.func.name = "uac2_func";
	uac2->g_audio.func.bind = afunc_bind;
	uac2->g_audio.func.unbind = afunc_unbind;
	uac2->g_audio.func.set_alt = afunc_set_alt;
	uac2->g_audio.func.get_alt = afunc_get_alt;
	uac2->g_audio.func.disable = afunc_disable;
	uac2->g_audio.func.setup = afunc_setup;
	uac2->g_audio.func.free_func = afunc_free;

	return &uac2->g_audio.func;
}

DECLARE_USB_FUNCTION_INIT(uac2, afunc_alloc_inst, afunc_alloc);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yadwinder Singh"); /* f_uac2.c */
MODULE_AUTHOR("Jaswinder Singh"); /* f_uac2.c */
MODULE_AUTHOR("Ruslan Bilovol"); /* u_audio.c */
