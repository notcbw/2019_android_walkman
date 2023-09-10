/*
 * Copyright 2013,2014,2015,2016,2017,2018,2019 Sony Video & Sound Products Inc.
 */
/*
 * cxd3778gf_table.h
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

#ifndef _CXD3778GF_TABLE_HEADER_
#define _CXD3778GF_TABLE_HEADER_

struct cxd3778gf_master_volume {
	unsigned char sdin1;
	unsigned char sdin2;
	unsigned char play;
	unsigned char hpout;
	unsigned char hpout3_ctrl3;
};

struct cxd3778gf_device_gain {
	unsigned char pga;
	unsigned char adc;
};

struct cxd3778gf_deq_coefficient {
	unsigned char b0[3];
	unsigned char b1[3];
	unsigned char b2[3];
	unsigned char a1[3];
	unsigned char a2[3];
};

#define TABLE_ID_MASTER_VOLUME 0
#define TABLE_ID_DEVICE_GAIN   1
#define TABLE_ID_TONE_CONTROL  2

#define MASTER_VOLUME_TABLE_OFF                    0
#define MASTER_VOLUME_TABLE_SE_PCM_LG              1
#define MASTER_VOLUME_TABLE_SE_PCM_HG              2
#define MASTER_VOLUME_TABLE_SE_PCM_NC              3
#define MASTER_VOLUME_TABLE_SE_PCM_AM              4
#define MASTER_VOLUME_TABLE_BTL_PCM_LG             5
#define MASTER_VOLUME_TABLE_BTL_PCM_HG             6
#define MASTER_VOLUME_TABLE_BTL_DSD64_LG           7
#define MASTER_VOLUME_TABLE_BTL_DSD64_HG           8
#define MASTER_VOLUME_TABLE_BTL_DSD128_LG          9
#define MASTER_VOLUME_TABLE_BTL_DSD128_HG          10
#define MASTER_VOLUME_TABLE_BTL_DSD256_LG          11
#define MASTER_VOLUME_TABLE_BTL_DSD256_HG          12
#define MASTER_VOLUME_TABLE_MAX                    12

#define TONE_CONTROL_TABLE_NO_HP            0
#define TONE_CONTROL_TABLE_SAMP_GENERAL_HP  1
#define TONE_CONTROL_TABLE_SAMP_NW510N_NCHP 2
#define TONE_CONTROL_TABLE_MAX              2

extern struct cxd3778gf_master_volume cxd3778gf_master_volume_table[2][MASTER_VOLUME_TABLE_MAX+1][MASTER_VOLUME_MAX+1];
extern unsigned char                  cxd3778gf_master_gain_table[MASTER_GAIN_MAX+1];
extern struct cxd3778gf_device_gain   cxd3778gf_device_gain_table[INPUT_DEVICE_MAX+1];
extern unsigned char cxd3778gf_tone_control_table[(TONE_CONTROL_TABLE_MAX+1)][CODEC_RAM_SIZE];
extern unsigned int cxd3778gf_master_volume_dsd_table[MASTER_VOLUME_TABLE_MAX+1][MASTER_VOLUME_MAX+1];

int cxd3778gf_table_initialize(struct mutex * mutex);
int cxd3778gf_table_finalize(void);

#endif
