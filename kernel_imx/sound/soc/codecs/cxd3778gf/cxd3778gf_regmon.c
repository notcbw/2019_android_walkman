/*
 * Copyright 2016 Sony Corporation
 * File changed on 2016-01-20
 */
/*
 * cxd3778gf_timer.c
 *
 * CXD3778GF CODEC driver
 *
 * Copyright (c) 2013-2016 Sony Corporation
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

#ifdef CONFIG_REGMON_DEBUG

static int cxd3778gf_regmon_write_reg(
	void         * private_data,
	unsigned int   address,
	unsigned int   value
);

static int cxd3778gf_regmon_read_reg(
	void         * private_data,
	unsigned int   address,
	unsigned int * value
);

static regmon_reg_info_t cxd3778gf_regmon_reg_info[] =
{
	{"DEVICE_ID", 			0x00},
	{"REVISION_ID", 		0x01},
	{"SYSTEM", 			0x03},
	{"OSC_ON", 			0x04},
	{"OSC_SEL", 			0x05},
	{"OSC_EN", 			0x06},
	{"PLUG_DET", 			0x07},
	{"MICBIAS", 			0x08},
	{"TRIM0", 			0x09},
	{"TRIM1", 		0x0A},
	{"RSV", 		0x0B},
	{"AD8CTRL0", 		0x0C},
	{"AD8CTRL1", 		0x0D},
	{"AD8L_CAPTURED", 		0x0E},
	{"AD8R_CAPTURED", 		0x0F},
	{"BLK_FREQ0", 		0x10},
	{"BLK_FREQ1", 		0x11},
	{"BLK_ON0", 		0x12},
	{"BLK_ON1", 		0x13},
	{"CLK_EN0", 		0x14},
	{"CLK_EN1", 		0x15},
	{"CLK_EN2", 		0x16},
	{"CLK_HALT", 		0x17},
	{"SW_XRST0", 		0x18},
	{"SW_XRST1", 		0x19},
	{"SD_ENABLE", 		0x1A},
	{"SD1_MODE", 		0x1B},
	{"SD1_MASK", 		0x1C},
	{"SD1_MISC", 		0x1D},
	{"SD2_MODE", 		0x1E},
	{"SD2_MASK", 		0x1F},
	{"SD2_MISC", 		0x20},
	{"DSD_ENABLE", 		0x21},
	{"CLK_OUTPUT", 		0x22},
	{"I2S_INT_EN", 		0x23},
	{"CODEC_DATA_SEL", 		0x24},
	{"CODEC_PLAYVOL", 		0x25},
	{"CODEC_RECVOL", 		0x26},
	{"CODEC_AINVOL", 		0x27},
	{"CODEC_SDIN1VOL", 		0x28},
	{"CODEC_SDIN2VOL", 		0x29},
	{"CODEC_SR_PLAY1", 		0x2A},
	{"CODEC_SR_PLAY2", 		0x2B},
	{"CODEC_SR_PLAY3", 		0x2C},
	{"CODEC_SR_REC1", 		0x2D},
	{"CODEC_SR_REC2", 		0x2E},
	{"CODEC_SR_REC3", 		0x2F},
	{"CODEC_EN", 		0x30},
	{"CODEC_CS_VOL", 		0x36},
	{"MIC1L_VOL", 		0x37},
	{"MIC1R_VOL", 		0x38},
	{"MIC1_REG0", 		0x39},
	{"MIC1_REG1", 		0x3A},
	{"MIC2L_VOL", 		0x3B},
	{"MIC2R_VOL", 		0x3C},
	{"MIC2_REG0", 		0x3D},
	{"MIC2_REG1", 		0x3E},
	{"NS_DAC", 		0x3F},
	{"BEEP0", 		0x40},
	{"BEEP1", 		0x41},
	{"HPRM_CTRL0", 		0x42},
	{"HPRM_CTRL1", 		0x43},
	{"HPRM_MEAS", 		0x44},
	{"HPRM_DATA2", 		0x45},
	{"HPRM_DATA1", 		0x46},
	{"HPRM_DATA0", 		0x47},
	{"PHV_SEL", 		0x48},
	{"PHV_L", 		0x49},
	{"PHV_R", 		0x4B},
	{"PHV_CTRL0", 		0x4C},
	{"PHV_CTRL1", 		0x4D},
	{"PHV_TEST1", 		0x4E},
	{"PHV_TEST0", 		0x4F},
	{"CPCTL1", 		0x51},
	{"CPCTL2", 		0x52},
	{"CPCTL3", 		0x53},
	{"DITH_LEV1", 		0x54},
	{"DITH_LEV2", 		0x55},
	{"DITH", 		0x56},
	{"PGA1_VOLL", 		0x59},
	{"PGA1_VOLR", 		0x5A},
	{"PGA2_VOLL", 		0x5B},
	{"PGA2_VOLR", 		0x5C},
	{"AD8_CTRL3", 		0x5D},
	{"AIN1_RSV", 		0x5E},
	{"AIN2_RSV", 		0x5F},
	{"AIN_PREAMP", 		0x60},
	{"DAC_RSV", 		0x61},
	{"LINEOUT_VOL", 		0x62},
	{"DAMP_REF", 		0x63},
	{"DAMP_VOL_CTRL1", 		0x64},
	{"DAMP_VOL_CTRL2", 		0x65},
	{"DAMP_VOL_CTRL3", 		0x66},
	{"HPOUT2_CTRL1", 		0x67},
	{"HPOUT2_CTRL2", 		0x68},
	{"HPOUT2_CTRL3", 		0x69},
	{"HPOUT2_CTRL4", 		0x6A},
	{"HPOUT3_CTRL1", 		0x6B},
	{"HPOUT3_CTRL2", 		0x6C},
	{"HPOUT3_CTRL3", 		0x6D},
	{"HPOUT3_CTRL4", 		0x6E},
	{"HPOUT3_CTRL5", 		0x6F},
	{"MEM_CTRL", 		0x70},
	{"MEM_ADDR", 		0x71},
	{"MEM_RDAT", 		0x75},
	{"MEM_INIT", 		0x7C},
	{"MEM_ISTA", 		0x7D},
	{"ANALOG_RSV", 		0x7E},
	{"HPCLK_CTRL", 		0x7F},
	{"DNC_INTIM", 		0x80},
	{"DNC_INTP", 		0x81},
	{"DNC1_LIMITA", 		0x82},
	{"DNC1_LIMITR", 		0x83},
	{"DNC1_LIMITY", 		0x84},
	{"DNC2_LIMITA", 		0x85},
	{"DNC2_LIMITR", 		0x86},
	{"DNC2_LIMITY", 		0x87},
	{"DNC1_START", 		0x88},
	{"DNC2_START", 		0x89},
	{"DNC1_IKMD", 		0x8A},
	{"DNC2_IKMD", 		0x8B},
	{"DNC1_PTLEN", 		0x8C},
	{"DNC2_PTLEN", 		0x8D},
	{"DNC1_MONIEN", 		0x8E},
	{"DNC2_MONIEN", 		0x8F},
	{"DNC1_CANVOL0_H", 		0x90},
	{"DNC1_CANVOL0_L", 		0x91},
	{"DNC1_CANVOL1_H", 		0x92},
	{"DNC1_CANVOL1_L", 		0x93},
	{"DNC2_CANVOL0_H", 		0x94},
	{"DNC2_CANVOL0_L", 		0x95},
	{"DNC2_CANVOL1_H", 		0x96},
	{"DNC2_CANVOL1_L", 		0x97},
	{"DNC1_MONVOL0_H", 		0x98},
	{"DNC1_MONVOL0_L", 		0x99},
	{"DNC1_MONVOL1_H", 		0x9A},
	{"DNC1_MONVOL1_L", 		0x9B},
	{"DNC2_MONVOL0_H", 		0x9C},
	{"DNC2_MONVOL0_L", 		0x9D},
	{"DNC2_MONVOL1_H", 		0x9E},
	{"DNC2_MONVOL1_L", 		0x9F},
	{"DNC1_ALGAIN0_H", 		0xA0},
	{"DNC1_ALGAIN0_L", 		0xA1},
	{"DNC1_ALGAIN1_H", 		0xA2},
	{"DNC1_ALGAIN1_L", 		0xA3},
	{"DNC2_ALGAIN0_H", 		0xA4},
	{"DNC2_ALGAIN0_L", 		0xA5},
	{"DNC2_ALGAIN1_H", 		0xA6},
	{"DNC2_ALGAIN1_L", 		0xA7},
	{"DNC1_AVFCAN", 		0xA8},
	{"DNC2_AVFCAN", 		0xA9},
	{"DNC1_AVFMON", 		0xAA},
	{"DNC2_AVFMON", 		0xAB},
	{"DNC_PHD_H", 		0xAC},
	{"DNC_PHD_L", 		0xAD},
	{"AINC_CTRL0", 		0xAE},
	{"AINC_CTRL1", 		0xAF},
	{"AINC_CTRL2", 		0xB0},
	{"AINC_CTRL3", 		0xB1},
	{"AINC_CTRL4", 		0xB2},
	{"AINC_CTRL5", 		0xB3},
	{"AINC_CTRL6", 		0xB4},
	{"AINC_CTRL7", 		0xB5},
	{"AINC_CTRL8", 		0xB6},
	{"AINC_CTRL9", 		0xB7},
	{"AINC_CTRL10", 		0xB8},
	{"AINC_CTRL11", 		0xB9},
	{"AINC_CTRL12", 		0xBA},
	{"UC_DMCNT", 		0xBB},
	{"UC_DMA", 		0xBC},
	{"UC_DMD_4", 		0xBD},
	{"UC_DMD_3", 		0xBE},
	{"UC_DMD_2", 		0xBF},
	{"UC_DMD_1", 		0xC0},
	{"UC_DMD_0", 		0xC1},
	{"SMS_NS_PMUTE", 		0xC2},
	{"SMS_NS_CTRL", 		0xC3},
	{"SMS_NS_LMT", 		0xC4},
	{"SMS_NS_ADJ", 		0xC5},
	{"SMS_DITHER_CTRL0", 		0xC6},
	{"SMS_DITHER_CTRL1", 		0xC7},
	{"SMS_DITHER_CTRL2", 		0xC8},
	{"SMS_DITHER_CTRL3", 		0xC9},
	{"SMS_DITHER_CTRL4", 		0xCA},
	{"SMS_DITHER_CTRL5", 		0xCB},
	{"SMS_DITHER_CTRL6", 		0xCC},
	{"SMS_DITHER_CTRL7", 		0xCD},
	{"SMS_BEEP_CTRL0", 		0xCE},
	{"SMS_BEEP_CTRL1", 		0xCF},
	{"SMS_NS_AMMD", 		0xD0},
	{"SMS_PWM_PHDLY", 		0xD1},
	{"SMS_PWM_CTRL0", 		0xD2},
	{"SMS_PWM_CTRL1", 		0xD3},
	{"SMS_OUT2DLY", 		0xD4},
	{"SMS_DSD_CTRL0", 		0xD5},
	{"SMS_DSD_CTRL1", 		0xD6},
	{"SMS_RSV0", 		0xD7},
	{"SMS_SFTRMP", 		0xD8},
	{"SMS_RSV1", 		0xD9},
	{"SMS_RSV2", 		0xDA},
	{"DNC_REQ", 		0xDB},
	{"BUT_TH", 		0xF0},
	{"OCDDET_DSE", 		0xF4},
	{"OCDDET_DBTL", 		0xF5},
	{"INT_EN0", 		0xF8},
	{"INT_EN1", 		0xF9},
	{"INT_EN2", 		0xFA},
	{"INT0", 		0xFC},
	{"INT1", 		0xFD},
	{"INT2", 		0xFE},
};

static regmon_customer_info_t cxd3778gf_customer_info =
{
	.name           = "cxd3778gf",
	.reg_info       = cxd3778gf_regmon_reg_info,
	.reg_info_count = sizeof(cxd3778gf_regmon_reg_info)/sizeof(regmon_reg_info_t),
	.write_reg      = cxd3778gf_regmon_write_reg,
	.read_reg       = cxd3778gf_regmon_read_reg,
	.private_data   = NULL,
};

#endif

static int initialized = FALSE;

int cxd3778gf_regmon_initialize(void)
{
	print_trace("%s()\n",__FUNCTION__);

#ifdef CONFIG_REGMON_DEBUG
	regmon_add(&cxd3778gf_customer_info);
#endif

	initialized=TRUE;

	return(0);
}

int cxd3778gf_regmon_finalize(void)
{
	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

#ifdef CONFIG_REGMON_DEBUG
	regmon_del(&cxd3778gf_customer_info);
#endif

	initialized=FALSE;

	return(0);
}

#ifdef CONFIG_REGMON_DEBUG

static int cxd3778gf_regmon_write_reg(
	void         * private_data,
	unsigned int   address,
	unsigned int   value
)
{
	int rv;

	rv=cxd3778gf_register_write(address,value);

	return(rv);
}

static int cxd3778gf_regmon_read_reg(
	void         * private_data,
	unsigned int   address,
	unsigned int * value
)
{
	int rv;

	rv=cxd3778gf_register_read(address,value);

	return(rv);
}

#endif
