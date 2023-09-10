/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 */
/*
 * cxd3778gf_register.h
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

#ifndef _CXD3778GF_REGISTER_HEADER_
#define _CXD3778GF_REGISTER_HEADER_

#define CXD3778GF_DEVICE_ID         0x00
#define CXD3778GF_REVISION_ID       0x01
#define CXD3778GF_SYSTEM            0x03
#define CXD3778GF_OSC_ON            0x04
#define CXD3778GF_OSC_SEL           0x05
#define CXD3778GF_OSC_EN            0x06
#define CXD3778GF_PLUG_DET          0x07
#define CXD3778GF_MICBIAS           0x08
#define CXD3778GF_TRIM0             0x09
#define CXD3778GF_TRIM1             0x0A
#define CXD3778GF_TRIM2             0x0B
#define CXD3778GF_AD8CTRL0          0x0C
#define CXD3778GF_AD8CTRL1          0x0D
#define CXD3778GF_AD8L_CAPTURED     0x0E
#define CXD3778GF_AD8R_CAPTURED     0x0F
#define CXD3778GF_BLK_FREQ0         0x10
#define CXD3778GF_BLK_FREQ1         0x11
#define CXD3778GF_BLK_ON0           0x12
#define CXD3778GF_BLK_ON1           0x13
#define CXD3778GF_CLK_EN0           0x14
#define CXD3778GF_CLK_EN1           0x15
#define CXD3778GF_CLK_EN2           0x16
#define CXD3778GF_CLK_HALT          0x17
#define CXD3778GF_SW_XRST0          0x18
#define CXD3778GF_SW_XRST1          0x19
#define CXD3778GF_SD_ENABLE         0x1A
#define CXD3778GF_SD1_MODE          0x1B
#define CXD3778GF_SD1_MASK          0x1C
#define CXD3778GF_SD1_MISC          0x1D
#define CXD3778GF_SD2_MODE          0x1E
#define CXD3778GF_SD2_MASK          0x1F
#define CXD3778GF_SD2_MISC          0x20
#define CXD3778GF_DSD_ENABLE        0x21
#define CXD3778GF_CLK_OUTPUT        0x22
#define CXD3778GF_I2S_INT_EN        0x23
#define CXD3778GF_CODEC_DATA_SEL    0x24
#define CXD3778GF_CODEC_PLAYVOL     0x25
#define CXD3778GF_CODEC_RECVOL      0x26
#define CXD3778GF_CODEC_AINVOL      0x27
#define CXD3778GF_CODEC_SDIN1VOL    0x28
#define CXD3778GF_CODEC_SDIN2VOL    0x29
#define CXD3778GF_CODEC_SR_PLAY1    0x2A
#define CXD3778GF_CODEC_SR_PLAY2    0x2B
#define CXD3778GF_CODEC_SR_PLAY3    0x2C
#define CXD3778GF_CODEC_SR_REC1     0x2D
#define CXD3778GF_CODEC_SR_REC2     0x2E
#define CXD3778GF_CODEC_SR_REC3     0x2F
#define CXD3778GF_CODEC_EN          0x30
#define CXD3778GF_CODEC_CS_VOL      0x36
#define CXD3778GF_MIC1L_VOL         0x37
#define CXD3778GF_MIC1R_VOL         0x38
#define CXD3778GF_MIC1_REG0         0x39
#define CXD3778GF_MIC1_REG1         0x3A
#define CXD3778GF_MIC2L_VOL         0x3B
#define CXD3778GF_MIC2R_VOL         0x3C
#define CXD3778GF_MIC2_REG0         0x3D
#define CXD3778GF_MIC2_REG1         0x3E
#define CXD3778GF_NS_DAC            0x3F
#define CXD3778GF_BEEP0             0x40
#define CXD3778GF_BEEP1             0x41
#define CXD3778GF_HPRM_CTRL0        0x42
#define CXD3778GF_HPRM_CTRL1        0x43
#define CXD3778GF_HPRM_MEAS         0x44
#define CXD3778GF_HPRM_DATA2        0x45
#define CXD3778GF_HPRM_DATA1        0x46
#define CXD3778GF_HPRM_DATA0        0x47
#define CXD3778GF_PHV_SEL           0x48
#define CXD3778GF_PHV_L             0x49
#define CXD3778GF_PHV_R             0x4B
#define CXD3778GF_PHV_CTRL0         0x4C
#define CXD3778GF_PHV_CTRL1         0x4D
#define CXD3778GF_PHV_TEST1         0x4E
#define CXD3778GF_PHV_TEST0         0x4F
#define CXD3778GF_CPCTL1            0x51
#define CXD3778GF_CPCTL2            0x52
#define CXD3778GF_CPCTL3            0x53
#define CXD3778GF_DITH_LEV1         0x54
#define CXD3778GF_DITH_LEV2         0x55
#define CXD3778GF_DITH              0x56
#define CXD3778GF_PGA1_VOLL         0x59
#define CXD3778GF_PGA1_VOLR         0x5A
#define CXD3778GF_PGA2_VOLL         0x5B
#define CXD3778GF_PGA2_VOLR         0x5C
#define CXD3778GF_AD8_CTRL3         0x5D
#define CXD3778GF_AIN1_RSV          0x5E
#define CXD3778GF_AIN2_RSV          0x5F
#define CXD3778GF_AIN_PREAMP        0x60
#define CXD3778GF_DAC_RSV           0x61
#define CXD3778GF_LINEOUT_VOL       0x62
#define CXD3778GF_DAMP_REF          0x63
#define CXD3778GF_DAMP_VOL_CTRL1    0x64
#define CXD3778GF_DAMP_VOL_CTRL2    0x65
#define CXD3778GF_DAMP_VOL_CTRL3    0x66
#define CXD3778GF_HPOUT2_CTRL1      0x67
#define CXD3778GF_HPOUT2_CTRL2      0x68
#define CXD3778GF_HPOUT2_CTRL3      0x69
#define CXD3778GF_HPOUT2_CTRL4      0x6A
#define CXD3778GF_HPOUT3_CTRL1      0x6B
#define CXD3778GF_HPOUT3_CTRL2      0x6C
#define CXD3778GF_HPOUT3_CTRL3      0x6D
#define CXD3778GF_HPOUT3_CTRL4      0x6E
#define CXD3778GF_HPOUT3_CTRL5      0x6F
#define CXD3778GF_MEM_CTRL          0x70
#define CXD3778GF_MEM_ADDR          0x71
#define CXD3778GF_MEM_WDAT          0x72
#define CXD3778GF_MEM_RDAT          0x77
#define CXD3778GF_MEM_INIT          0x7C
#define CXD3778GF_MEM_ISTA          0x7D
#define CXD3778GF_ANALOG_RSV        0x7E
#define CXD3778GF_HPCLK_CTRL        0x7F
#define CXD3778GF_DNC_INTIM         0x80
#define CXD3778GF_DNC_INTP          0x81
#define CXD3778GF_DNC1_LIMITA       0x82
#define CXD3778GF_DNC1_LIMITR       0x83
#define CXD3778GF_DNC1_LIMITY       0x84
#define CXD3778GF_DNC2_LIMITA       0x85
#define CXD3778GF_DNC2_LIMITR       0x86
#define CXD3778GF_DNC2_LIMITY       0x87
#define CXD3778GF_DNC1_START        0x88
#define CXD3778GF_DNC2_START        0x89
#define CXD3778GF_DNC1_IKMD         0x8A
#define CXD3778GF_DNC2_IKMD         0x8B
#define CXD3778GF_DNC1_PTLEN        0x8C
#define CXD3778GF_DNC2_PTLEN        0x8D
#define CXD3778GF_DNC1_MONIEN       0x8E
#define CXD3778GF_DNC2_MONIEN       0x8F
#define CXD3778GF_DNC1_CANVOL0_H    0x90
#define CXD3778GF_DNC1_CANVOL0_L    0x91
#define CXD3778GF_DNC1_CANVOL1_H    0x92
#define CXD3778GF_DNC1_CANVOL1_L    0x93
#define CXD3778GF_DNC2_CANVOL0_H    0x94
#define CXD3778GF_DNC2_CANVOL0_L    0x95
#define CXD3778GF_DNC2_CANVOL1_H    0x96
#define CXD3778GF_DNC2_CANVOL1_L    0x97
#define CXD3778GF_DNC1_MONVOL0_H    0x98
#define CXD3778GF_DNC1_MONVOL0_L    0x99
#define CXD3778GF_DNC1_MONVOL1_H    0x9A
#define CXD3778GF_DNC1_MONVOL1_L    0x9B
#define CXD3778GF_DNC2_MONVOL0_H    0x9C
#define CXD3778GF_DNC2_MONVOL0_L    0x9D
#define CXD3778GF_DNC2_MONVOL1_H    0x9E
#define CXD3778GF_DNC2_MONVOL1_L    0x9F
#define CXD3778GF_DNC1_ALGAIN0_H    0xA0
#define CXD3778GF_DNC1_ALGAIN0_L    0xA1
#define CXD3778GF_DNC1_ALGAIN1_H    0xA2
#define CXD3778GF_DNC1_ALGAIN1_L    0xA3
#define CXD3778GF_DNC2_ALGAIN0_H    0xA4
#define CXD3778GF_DNC2_ALGAIN0_L    0xA5
#define CXD3778GF_DNC2_ALGAIN1_H    0xA6
#define CXD3778GF_DNC2_ALGAIN1_L    0xA7
#define CXD3778GF_DNC1_AVFCAN       0xA8
#define CXD3778GF_DNC2_AVFCAN       0xA9
#define CXD3778GF_DNC1_AVFMON       0xAA
#define CXD3778GF_DNC2_AVFMON       0xAB
#define CXD3778GF_DNC_PHD_H         0xAC
#define CXD3778GF_DNC_PHD_L         0xAD
#define CXD3778GF_AINC_CTRL0        0xAE
#define CXD3778GF_AINC_CTRL1        0xAF
#define CXD3778GF_AINC_CTRL2        0xB0
#define CXD3778GF_AINC_CTRL3        0xB1
#define CXD3778GF_AINC_CTRL4        0xB2
#define CXD3778GF_AINC_CTRL5        0xB3
#define CXD3778GF_AINC_CTRL6        0xB4
#define CXD3778GF_AINC_CTRL7        0xB5
#define CXD3778GF_AINC_CTRL8        0xB6
#define CXD3778GF_AINC_CTRL9        0xB7
#define CXD3778GF_AINC_CTRL10       0xB8
#define CXD3778GF_AINC_CTRL11       0xB9
#define CXD3778GF_AINC_CTRL12       0xBA
#define CXD3778GF_UC_DMCNT          0xBB
#define CXD3778GF_UC_DMA            0xBC
#define CXD3778GF_UC_DMD_4          0xBD
#define CXD3778GF_UC_DMD_3          0xBE
#define CXD3778GF_UC_DMD_2          0xBF
#define CXD3778GF_UC_DMD_1          0xC0
#define CXD3778GF_UC_DMD_0          0xC1
#define CXD3778GF_SMS_NS_PMUTE      0xC2
#define CXD3778GF_SMS_NS_CTRL       0xC3
#define CXD3778GF_SMS_NS_LMT        0xC4
#define CXD3778GF_SMS_NS_ADJ        0xC5
#define CXD3778GF_SMS_DITHER_CTRL0  0xC6
#define CXD3778GF_SMS_DITHER_CTRL1  0xC7
#define CXD3778GF_SMS_DITHER_CTRL2  0xC8
#define CXD3778GF_SMS_DITHER_CTRL3  0xC9
#define CXD3778GF_SMS_DITHER_CTRL4  0xCA
#define CXD3778GF_SMS_DITHER_CTRL5  0xCB
#define CXD3778GF_SMS_DITHER_CTRL6  0xCC
#define CXD3778GF_SMS_DITHER_CTRL7  0xCD
#define CXD3778GF_SMS_BEEP_CTRL0    0xCE
#define CXD3778GF_SMS_BEEP_CTRL1    0xCF
#define CXD3778GF_SMS_NS_AMMD       0xD0
#define CXD3778GF_SMS_PWM_PHDLY     0xD1
#define CXD3778GF_SMS_PWM_CTRL0     0xD2
#define CXD3778GF_SMS_PWM_CTRL1     0xD3
#define CXD3778GF_SMS_OUT2DLY       0xD4
#define CXD3778GF_SMS_DSD_PMUTE     0xD5
#define CXD3778GF_SMS_DSD_CTRL      0xD6
#define CXD3778GF_SMS_RSV0          0xD7
#define CXD3778GF_SMS_SFTRMP        0xD8
#define CXD3778GF_SMS_RSV1          0xD9
#define CXD3778GF_SMS_RSV2          0xDA
#define CXD3778GF_DNC_REQ           0xDB
#define CXD3778GF_BUT_TH            0xF0
#define CXD3778GF_OCDDET_DSE        0xF4
#define CXD3778GF_OCDDET_DBTL       0xF5
#define CXD3778GF_INT_EN0           0xF8
#define CXD3778GF_INT_EN1           0xF9
#define CXD3778GF_INT_EN2           0xFA
#define CXD3778GF_INT0              0xFC
#define CXD3778GF_INT1              0xFD
#define CXD3778GF_INT2              0xFE


int cxd3778gf_register_initialize(struct i2c_client * client);
int cxd3778gf_register_finalize(void);
int cxd3778gf_ext_register_initialize(struct i2c_client *);
int cxd3778gf_ext_register_finalize(void);
int cxd3778gf_register_use_ext_bus(bool);

int cxd3778gf_register_write_multiple(
	unsigned int    address,
	unsigned char * value,
	int             size
);

int cxd3778gf_register_read_multiple(
	unsigned int    address,
	unsigned char * value,
	int             size
);

int cxd3778gf_register_modify(
	unsigned int address,
	unsigned int value,
	unsigned int mask
);

int cxd3778gf_register_write(
	unsigned int address,
	unsigned int value
);

int cxd3778gf_register_read(
	unsigned int   address,
	unsigned int * value
);

#endif
