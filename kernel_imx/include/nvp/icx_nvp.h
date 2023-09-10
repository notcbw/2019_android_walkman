//　SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2011,2012,2013,2014,2015,2016,2017,2018,2019 Sony Video & Sound Products Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef _ICX_NVP_H_
#define _ICX_NVP_H_

#ifdef CONFIG_ICX_NVP_WRAPPER
#include <linux/icx_nvp_wrapper.h>
#endif /* CONFIG_ICX_NVP_WRAPPER */

typedef struct {
	int           sector;
	int           page;
	unsigned char oob[256];
	unsigned char dummy[12];
	unsigned char data[4096];
}icx_nvp_ioc_t;

#define ICX_NVP_IOC_MAGIC 'n'

/* Common to NVP for eMMC */
#define ICX_NVP_IOC_SHOW_STAT    _IOW (ICX_NVP_IOC_MAGIC,0,int)
#define ICX_NVP_IOC_ERASE_ALL    _IO  (ICX_NVP_IOC_MAGIC,1)
#define ICX_NVP_IOC_WRITE_SECTOR _IOW (ICX_NVP_IOC_MAGIC,2,icx_nvp_ioc_t *)
#define ICX_NVP_IOC_READ_SECTOR  _IOWR(ICX_NVP_IOC_MAGIC,3,icx_nvp_ioc_t *)
/* Only for NVP for NAND */
#define ICX_NVP_IOC_GET_KSB       _IOR (ICX_NVP_IOC_MAGIC, 4, int)
#define ICX_NVP_IOC_GET_KBC       _IOR (ICX_NVP_IOC_MAGIC, 5, int)
#define ICX_NVP_IOC_GET_PSB       _IOR (ICX_NVP_IOC_MAGIC, 6, int)
#define ICX_NVP_IOC_GET_PBC       _IOR (ICX_NVP_IOC_MAGIC, 7, int)
#define ICX_NVP_IOC_GET_NSB       _IOR (ICX_NVP_IOC_MAGIC, 8, int)
#define ICX_NVP_IOC_GET_NBC       _IOR (ICX_NVP_IOC_MAGIC, 9, int)
#define ICX_NVP_IOC_GET_CSB       _IOR (ICX_NVP_IOC_MAGIC,10, int)
#define ICX_NVP_IOC_GET_NAND_INFO _IOWR(ICX_NVP_IOC_MAGIC,12, unsigned long)
#define ICX_NVP_IOC_ERASE         _IOW (ICX_NVP_IOC_MAGIC, 13, icx_nvp_ioc_t *)

#define ICX_NVP_IOC_MAXNR 14

#define ICX_NVP_NODE_NO_DBG 0
#define ICX_NVP_NODE_NO_SYI 1
#define ICX_NVP_NODE_NO_UBP 2
#define ICX_NVP_NODE_NO_BMD 3
#define ICX_NVP_NODE_NO_PRK 4
#define ICX_NVP_NODE_NO_HLD 5
#define ICX_NVP_NODE_NO_PWD 6
#define ICX_NVP_NODE_NO_MID 7
#define ICX_NVP_NODE_NO_PCD 8
#define ICX_NVP_NODE_NO_SER 9
#define ICX_NVP_NODE_NO_UFN 10
#define ICX_NVP_NODE_NO_KAS 11
#define ICX_NVP_NODE_NO_SHP 12
#define ICX_NVP_NODE_NO_TST 13
#define ICX_NVP_NODE_NO_GTY 14
#define ICX_NVP_NODE_NO_CLG 15
#define ICX_NVP_NODE_NO_SE2 16
#define ICX_NVP_NODE_NO_NCP 17
#define ICX_NVP_NODE_NO_PSK 18
#define ICX_NVP_NODE_NO_NVR 77
#define ICX_NVP_NODE_NO_SHE 84
#define ICX_NVP_NODE_NO_BTC 85
#define ICX_NVP_NODE_NO_INS 89
#define ICX_NVP_NODE_NO_CTR 90
#define ICX_NVP_NODE_NO_SKU 91
#define ICX_NVP_NODE_NO_BPR 19
#define ICX_NVP_NODE_NO_BFP 20
#define ICX_NVP_NODE_NO_BFD 21
#define ICX_NVP_NODE_NO_BML 22
#define ICX_NVP_NODE_NO_APD 78
#define ICX_NVP_NODE_NO_BLF 79
#define ICX_NVP_NODE_NO_SLP 80
#define ICX_NVP_NODE_NO_VRT 81
#define ICX_NVP_NODE_NO_FNI 82
#define ICX_NVP_NODE_NO_SID 83
#define ICX_NVP_NODE_NO_MSO 86
#define ICX_NVP_NODE_NO_DGS 92
#define ICX_NVP_NODE_NO_ATF 23
#define ICX_NVP_NODE_NO_LYR 24
#define ICX_NVP_NODE_NO_DBV 25
#define ICX_NVP_NODE_NO_FUR 26
#define ICX_NVP_NODE_NO_UMS 27
#define ICX_NVP_NODE_NO_SKD 28
#define ICX_NVP_NODE_NO_UPS 29
#define ICX_NVP_NODE_NO_AWS 30
#define ICX_NVP_NODE_NO_FVI 31
#define ICX_NVP_NODE_NO_MAC 32
#define ICX_NVP_NODE_NO_FPI 33
#define ICX_NVP_NODE_NO_SSK 34
#define ICX_NVP_NODE_NO_TR1 35
#define ICX_NVP_NODE_NO_E00 36
#define ICX_NVP_NODE_NO_E01 37
#define ICX_NVP_NODE_NO_E02 38
#define ICX_NVP_NODE_NO_E03 39
#define ICX_NVP_NODE_NO_E04 40
#define ICX_NVP_NODE_NO_E05 41
#define ICX_NVP_NODE_NO_E06 42
#define ICX_NVP_NODE_NO_E07 43
#define ICX_NVP_NODE_NO_E08 44
#define ICX_NVP_NODE_NO_E09 45
#define ICX_NVP_NODE_NO_E10 46
#define ICX_NVP_NODE_NO_E11 47
#define ICX_NVP_NODE_NO_E12 48
#define ICX_NVP_NODE_NO_E13 49
#define ICX_NVP_NODE_NO_E14 50
#define ICX_NVP_NODE_NO_E15 51
#define ICX_NVP_NODE_NO_E16 52
#define ICX_NVP_NODE_NO_E17 53
#define ICX_NVP_NODE_NO_E18 54
#define ICX_NVP_NODE_NO_E19 55
#define ICX_NVP_NODE_NO_E20 56
#define ICX_NVP_NODE_NO_E21 57
#define ICX_NVP_NODE_NO_E22 58
#define ICX_NVP_NODE_NO_E23 59
#define ICX_NVP_NODE_NO_E24 60
#define ICX_NVP_NODE_NO_E25 61
#define ICX_NVP_NODE_NO_E26 62
#define ICX_NVP_NODE_NO_E27 63
#define ICX_NVP_NODE_NO_E28 64
#define ICX_NVP_NODE_NO_E29 65
#define ICX_NVP_NODE_NO_E30 66
#define ICX_NVP_NODE_NO_E31 67
#define ICX_NVP_NODE_NO_CLV 68
#define ICX_NVP_NODE_NO_SPS 69
#define ICX_NVP_NODE_NO_RBT 70
#define ICX_NVP_NODE_NO_EDW 71
#define ICX_NVP_NODE_NO_BTI 72
#define ICX_NVP_NODE_NO_HDI 73
#define ICX_NVP_NODE_NO_LBI 74
#define ICX_NVP_NODE_NO_FUI 75
#define ICX_NVP_NODE_NO_ERI 76
#define ICX_NVP_NODE_NO_PCI 87
#define ICX_NVP_NODE_NO_DBI 88
#define ICX_NVP_NODE_NO_INS 89
#define ICX_NVP_NODE_NO_CTR 90
#define ICX_NVP_NODE_NO_SKU 91
#define ICX_NVP_NODE_NO_DGS 92
#define ICX_NVP_NODE_NO_A00 93
#define ICX_NVP_NODE_NO_A01 94
#define ICX_NVP_NODE_NO_A02 95
#define ICX_NVP_NODE_NO_A03 96
#define ICX_NVP_NODE_NO_A04 97
#define ICX_NVP_NODE_NO_A05 98
#define ICX_NVP_NODE_NO_A06 99
#define ICX_NVP_NODE_NO_A07 100
#define ICX_NVP_NODE_NO_A08 101
#define ICX_NVP_NODE_NO_A09 102
#define ICX_NVP_NODE_NO_A10 103
#define ICX_NVP_NODE_NO_A11 104
#define ICX_NVP_NODE_NO_A12 105
#define ICX_NVP_NODE_NO_A13 106
#define ICX_NVP_NODE_NO_A14 107
#define ICX_NVP_NODE_NO_A15 108
#define ICX_NVP_NODE_NO_A16 109
#define ICX_NVP_NODE_NO_A17 110
#define ICX_NVP_NODE_NO_A18 111
#define ICX_NVP_NODE_NO_A19 112
#define ICX_NVP_NODE_NO_A20 113
#define ICX_NVP_NODE_NO_A21 114
#define ICX_NVP_NODE_NO_A22 115
#define ICX_NVP_NODE_NO_A23 116
#define ICX_NVP_NODE_NO_A24 117
#define ICX_NVP_NODE_NO_A25 118
#define ICX_NVP_NODE_NO_A26 119
#define ICX_NVP_NODE_NO_A27 120
#define ICX_NVP_NODE_NO_A28 121
#define ICX_NVP_NODE_NO_A29 122
#define ICX_NVP_NODE_NO_A30 123
#define ICX_NVP_NODE_NO_A31 124
#define ICX_NVP_NODE_NO_A32 125
#define ICX_NVP_NODE_NO_A33 126
#define ICX_NVP_NODE_NO_A34 127
#define ICX_NVP_NODE_NO_A35 128
#define ICX_NVP_NODE_NO_A36 129
#define ICX_NVP_NODE_NO_A37 130
#define ICX_NVP_NODE_NO_A38 131
#define ICX_NVP_NODE_NO_A39 132
#define ICX_NVP_NODE_NO_A40 133
#define ICX_NVP_NODE_NO_A41 134
#define ICX_NVP_NODE_NO_A42 135
#define ICX_NVP_NODE_NO_A43 136
#define ICX_NVP_NODE_NO_A44 137
#define ICX_NVP_NODE_NO_A45 138
#define ICX_NVP_NODE_NO_A46 139
#define ICX_NVP_NODE_NO_A47 140
#define ICX_NVP_NODE_NO_A48 141
#define ICX_NVP_NODE_NO_A49 142
#define ICX_NVP_NODE_NO_A50 143
#define ICX_NVP_NODE_NO_A51 144
#define ICX_NVP_NODE_NO_A52 145
#define ICX_NVP_NODE_NO_A53 146
#define ICX_NVP_NODE_NO_A54 147
#define ICX_NVP_NODE_NO_A55 148
#define ICX_NVP_NODE_NO_A56 149
#define ICX_NVP_NODE_NO_A57 150
#define ICX_NVP_NODE_NO_A58 151
#define ICX_NVP_NODE_NO_A59 152
#define ICX_NVP_NODE_NO_A60 153
#define ICX_NVP_NODE_NO_A61 154
#define ICX_NVP_NODE_NO_A62 155
#define ICX_NVP_NODE_NO_A63 156
#define ICX_NVP_NODE_NO_A64 157
#define ICX_NVP_NODE_NO_A65 158
#define ICX_NVP_NODE_NO_A66 159
#define ICX_NVP_NODE_NO_A67 160
#define ICX_NVP_NODE_NO_A68 161
#define ICX_NVP_NODE_NO_A69 162
#define ICX_NVP_NODE_NO_A70 163
#define ICX_NVP_NODE_NO_A71 164
#define ICX_NVP_NODE_NO_A72 165
#define ICX_NVP_NODE_NO_A73 166
#define ICX_NVP_NODE_NO_A74 167
#define ICX_NVP_NODE_NO_A75 168
#define ICX_NVP_NODE_NO_A76 169
#define ICX_NVP_NODE_NO_A77 170
#define ICX_NVP_NODE_NO_A78 171
#define ICX_NVP_NODE_NO_A79 172
#define ICX_NVP_NODE_NO_A80 173
#define ICX_NVP_NODE_NO_A81 174
#define ICX_NVP_NODE_NO_A82 175
#define ICX_NVP_NODE_NO_A83 176
#define ICX_NVP_NODE_NO_A84 177
#define ICX_NVP_NODE_NO_A85 178
#define ICX_NVP_NODE_NO_A86 179
#define ICX_NVP_NODE_NO_A87 180
#define ICX_NVP_NODE_NO_A88 181
#define ICX_NVP_NODE_NO_A89 182
#define ICX_NVP_NODE_NO_A90 183
#define ICX_NVP_NODE_NO_A91 184
#define ICX_NVP_NODE_NO_A92 185
#define ICX_NVP_NODE_NO_A93 186
#define ICX_NVP_NODE_NO_A94 187
#define ICX_NVP_NODE_NO_A95 188
#define ICX_NVP_NODE_NO_A96 189
#define ICX_NVP_NODE_NO_A97 190
#define ICX_NVP_NODE_NO_A98 191
#define ICX_NVP_NODE_NO_A99 192
#define ICX_NVP_NODE_NO_B00 193
#define ICX_NVP_NODE_NO_B01 194
#define ICX_NVP_NODE_NO_B02 195
#define ICX_NVP_NODE_NO_B03 196
#define ICX_NVP_NODE_NO_B04 197
#define ICX_NVP_NODE_NO_B05 198
#define ICX_NVP_NODE_NO_B06 199
#define ICX_NVP_NODE_NO_B07 200
#define ICX_NVP_NODE_NO_B08 201
#define ICX_NVP_NODE_NO_B09 202
#define ICX_NVP_NODE_NO_B10 203
#define ICX_NVP_NODE_NO_B11 204
#define ICX_NVP_NODE_NO_B12 205
#define ICX_NVP_NODE_NO_B13 206
#define ICX_NVP_NODE_NO_B14 207
#define ICX_NVP_NODE_NO_B15 208
#define ICX_NVP_NODE_NO_B16 209
#define ICX_NVP_NODE_NO_B17 210
#define ICX_NVP_NODE_NO_B18 211
#define ICX_NVP_NODE_NO_B19 212
#define ICX_NVP_NODE_NO_B20 213
#define ICX_NVP_NODE_NO_B21 214
#define ICX_NVP_NODE_NO_B22 215
#define ICX_NVP_NODE_NO_B23 216
#define ICX_NVP_NODE_NO_B24 217
#define ICX_NVP_NODE_NO_B25 218
#define ICX_NVP_NODE_NO_B26 219
#define ICX_NVP_NODE_NO_B27 220
#define ICX_NVP_NODE_NO_B28 221
#define ICX_NVP_NODE_NO_B29 222
#define ICX_NVP_NODE_NO_B30 223
#define ICX_NVP_NODE_NO_B31 224
#define ICX_NVP_NODE_NO_B32 225
#define ICX_NVP_NODE_NO_B33 226
#define ICX_NVP_NODE_NO_B34 227
#define ICX_NVP_NODE_NO_B35 228
#define ICX_NVP_NODE_NO_B36 229
#define ICX_NVP_NODE_NO_B37 230
#define ICX_NVP_NODE_NO_B38 231
#define ICX_NVP_NODE_NO_B39 232
#define ICX_NVP_NODE_NO_B40 233
#define ICX_NVP_NODE_NO_B41 234
#define ICX_NVP_NODE_NO_B42 235
#define ICX_NVP_NODE_NO_B43 236
#define ICX_NVP_NODE_NO_B44 237
#define ICX_NVP_NODE_NO_B45 238
#define ICX_NVP_NODE_NO_B46 239
#define ICX_NVP_NODE_NO_B47 240
#define ICX_NVP_NODE_NO_B48 241
#define ICX_NVP_NODE_NO_B49 242

int icx_nvp_write_data(int zn, unsigned char *buf, int size);
int icx_nvp_read_data(int zn, unsigned char *buf, int size);

#define icx_nvp_write_syi(_a,_b) icx_nvp_write_data(1,(_a),(_b))
#define icx_nvp_read_syi(_a,_b)  icx_nvp_read_data(1,(_a),(_b))

#define icx_nvp_write_ubp(_a,_b) icx_nvp_write_data(2,(_a),(_b))
#define icx_nvp_read_ubp(_a,_b)  icx_nvp_read_data(2,(_a),(_b))

#define icx_nvp_write_bmd(_a,_b) icx_nvp_write_data(3,(_a),(_b))
#define icx_nvp_read_bmd(_a,_b)  icx_nvp_read_data(3,(_a),(_b))

#define icx_nvp_write_prk(_a,_b) icx_nvp_write_data(4,(_a),(_b))
#define icx_nvp_read_prk(_a,_b)  icx_nvp_read_data(4,(_a),(_b))

#define icx_nvp_write_hld(_a,_b) icx_nvp_write_data(5,(_a),(_b))
#define icx_nvp_read_hld(_a,_b)  icx_nvp_read_data(5,(_a),(_b))

#define icx_nvp_write_pwd(_a,_b) icx_nvp_write_data(6,(_a),(_b))
#define icx_nvp_read_pwd(_a,_b)  icx_nvp_read_data(6,(_a),(_b))

#define icx_nvp_write_mid(_a,_b) icx_nvp_write_data(7,(_a),(_b))
#define icx_nvp_read_mid(_a,_b)  icx_nvp_read_data(7,(_a),(_b))

#define icx_nvp_write_pcd(_a,_b) icx_nvp_write_data(8,(_a),(_b))
#define icx_nvp_read_pcd(_a,_b)  icx_nvp_read_data(8,(_a),(_b))

#define icx_nvp_write_ser(_a,_b) icx_nvp_write_data(9,(_a),(_b))
#define icx_nvp_read_ser(_a,_b)  icx_nvp_read_data(9,(_a),(_b))

#define icx_nvp_write_ufn(_a,_b) icx_nvp_write_data(10,(_a),(_b))
#define icx_nvp_read_ufn(_a,_b)  icx_nvp_read_data(10,(_a),(_b))

#define icx_nvp_write_kas(_a,_b) icx_nvp_write_data(11,(_a),(_b))
#define icx_nvp_read_kas(_a,_b)  icx_nvp_read_data(11,(_a),(_b))

#define icx_nvp_write_shp(_a,_b) icx_nvp_write_data(12,(_a),(_b))
#define icx_nvp_read_shp(_a,_b)  icx_nvp_read_data(12,(_a),(_b))

#define icx_nvp_write_tst(_a,_b) icx_nvp_write_data(13,(_a),(_b))
#define icx_nvp_read_tst(_a,_b)  icx_nvp_read_data(13,(_a),(_b))

#define icx_nvp_write_gty(_a,_b) icx_nvp_write_data(14,(_a),(_b))
#define icx_nvp_read_gty(_a,_b)  icx_nvp_read_data(14,(_a),(_b))

#define icx_nvp_write_clg(_a,_b) icx_nvp_write_data(15,(_a),(_b))
#define icx_nvp_read_clg(_a,_b)  icx_nvp_read_data(15,(_a),(_b))

#define icx_nvp_write_se2(_a,_b) icx_nvp_write_data(16,(_a),(_b))
#define icx_nvp_read_se2(_a,_b)  icx_nvp_read_data(16,(_a),(_b))

#define icx_nvp_write_ncp(_a,_b) icx_nvp_write_data(17,(_a),(_b))
#define icx_nvp_read_ncp(_a,_b)  icx_nvp_read_data(17,(_a),(_b))

#define icx_nvp_write_psk(_a,_b) icx_nvp_write_data(18,(_a),(_b))
#define icx_nvp_read_psk(_a,_b)  icx_nvp_read_data(18,(_a),(_b))

#define icx_nvp_write_nvr(_a,_b) icx_nvp_write_data(77,(_a),(_b))
#define icx_nvp_read_nvr(_a,_b)  icx_nvp_read_data(77,(_a),(_b))

#define icx_nvp_write_she(_a,_b) icx_nvp_write_data(84,(_a),(_b))
#define icx_nvp_read_she(_a,_b)  icx_nvp_read_data(84,(_a),(_b))

#define icx_nvp_write_btc(_a,_b) icx_nvp_write_data(85,(_a),(_b))
#define icx_nvp_read_btc(_a,_b)  icx_nvp_read_data(85,(_a),(_b))

#define icx_nvp_write_ins(_a,_b) icx_nvp_write_data(89,(_a),(_b))
#define icx_nvp_read_ins(_a,_b)  icx_nvp_read_data(89,(_a),(_b))

#define icx_nvp_write_ctr(_a,_b) icx_nvp_write_data(90,(_a),(_b))
#define icx_nvp_read_ctr(_a,_b)  icx_nvp_read_data(90,(_a),(_b))

#define icx_nvp_write_sku(_a,_b) icx_nvp_write_data(91,(_a),(_b))
#define icx_nvp_read_sku(_a,_b)  icx_nvp_read_data(91,(_a),(_b))

#define icx_nvp_write_bpr(_a,_b) icx_nvp_write_data(19,(_a),(_b))
#define icx_nvp_read_bpr(_a,_b)  icx_nvp_read_data(19,(_a),(_b))

#define icx_nvp_write_bfp(_a,_b) icx_nvp_write_data(20,(_a),(_b))
#define icx_nvp_read_bfp(_a,_b)  icx_nvp_read_data(20,(_a),(_b))

#define icx_nvp_write_bfd(_a,_b) icx_nvp_write_data(21,(_a),(_b))
#define icx_nvp_read_bfd(_a,_b)  icx_nvp_read_data(21,(_a),(_b))

#define icx_nvp_write_bml(_a,_b) icx_nvp_write_data(22,(_a),(_b))
#define icx_nvp_read_bml(_a,_b)  icx_nvp_read_data(22,(_a),(_b))

#define icx_nvp_write_apd(_a,_b) icx_nvp_write_data(78,(_a),(_b))
#define icx_nvp_read_apd(_a,_b)  icx_nvp_read_data(78,(_a),(_b))

#define icx_nvp_write_blf(_a,_b) icx_nvp_write_data(79,(_a),(_b))
#define icx_nvp_read_blf(_a,_b)  icx_nvp_read_data(79,(_a),(_b))

#define icx_nvp_write_slp(_a,_b) icx_nvp_write_data(80,(_a),(_b))
#define icx_nvp_read_slp(_a,_b)  icx_nvp_read_data(80,(_a),(_b))

#define icx_nvp_write_vrt(_a,_b) icx_nvp_write_data(81,(_a),(_b))
#define icx_nvp_read_vrt(_a,_b)  icx_nvp_read_data(81,(_a),(_b))

#define icx_nvp_write_fni(_a,_b) icx_nvp_write_data(82,(_a),(_b))
#define icx_nvp_read_fni(_a,_b)  icx_nvp_read_data(82,(_a),(_b))

#define icx_nvp_write_sid(_a,_b) icx_nvp_write_data(83,(_a),(_b))
#define icx_nvp_read_sid(_a,_b)  icx_nvp_read_data(83,(_a),(_b))

#define icx_nvp_write_mso(_a,_b) icx_nvp_write_data(86,(_a),(_b))
#define icx_nvp_read_mso(_a,_b)  icx_nvp_read_data(86,(_a),(_b))

#define icx_nvp_write_dgs(_a,_b) icx_nvp_write_data(ICX_NVP_NODE_NO_DGS,(_a),(_b))
#define icx_nvp_read_dgs(_a,_b)  icx_nvp_read_data(ICX_NVP_NODE_NO_DGS,(_a),(_b))

#define icx_nvp_write_cng(_a,_b) icx_nvp_write_data(ICX_NVP_NODE_NO_ATF,(_a),(_b))
#define icx_nvp_read_cng(_a,_b)  icx_nvp_read_data(ICX_NVP_NODE_NO_ATF,(_a),(_b))

#define icx_nvp_write_lyr(_a,_b) icx_nvp_write_data(24,(_a),(_b))
#define icx_nvp_read_lyr(_a,_b)  icx_nvp_read_data(24,(_a),(_b))

#define icx_nvp_write_dbv(_a,_b) icx_nvp_write_data(25,(_a),(_b))
#define icx_nvp_read_dbv(_a,_b)  icx_nvp_read_data(25,(_a),(_b))

#define icx_nvp_write_fur(_a,_b) icx_nvp_write_data(26,(_a),(_b))
#define icx_nvp_read_fur(_a,_b)  icx_nvp_read_data(26,(_a),(_b))

#define icx_nvp_write_ums(_a,_b) icx_nvp_write_data(27,(_a),(_b))
#define icx_nvp_read_ums(_a,_b)  icx_nvp_read_data(27,(_a),(_b))

#define icx_nvp_write_skd(_a,_b) icx_nvp_write_data(28,(_a),(_b))
#define icx_nvp_read_skd(_a,_b)  icx_nvp_read_data(28,(_a),(_b))

#define icx_nvp_write_ups(_a,_b) icx_nvp_write_data(29,(_a),(_b))
#define icx_nvp_read_ups(_a,_b)  icx_nvp_read_data(29,(_a),(_b))

#define icx_nvp_write_aws(_a,_b) icx_nvp_write_data(30,(_a),(_b))
#define icx_nvp_read_aws(_a,_b)  icx_nvp_read_data(30,(_a),(_b))

#define icx_nvp_write_fvi(_a,_b) icx_nvp_write_data(31,(_a),(_b))
#define icx_nvp_read_fvi(_a,_b)  icx_nvp_read_data(31,(_a),(_b))

#define icx_nvp_write_mac(_a,_b) icx_nvp_write_data(32,(_a),(_b))
#define icx_nvp_read_mac(_a,_b)  icx_nvp_read_data(32,(_a),(_b))

#define icx_nvp_write_fpi(_a,_b) icx_nvp_write_data(33,(_a),(_b))
#define icx_nvp_read_fpi(_a,_b)  icx_nvp_read_data(33,(_a),(_b))

#define icx_nvp_write_tr0(_a,_b) icx_nvp_write_data(ICX_NVP_NODE_NO_SSK,(_a),(_b))
#define icx_nvp_read_tr0(_a,_b)  icx_nvp_read_data(ICX_NVP_NODE_NO_SSK,(_a),(_b))

#define icx_nvp_write_tr1(_a,_b) icx_nvp_write_data(35,(_a),(_b))
#define icx_nvp_read_tr1(_a,_b)  icx_nvp_read_data(35,(_a),(_b))

#define icx_nvp_write_e00(_a,_b) icx_nvp_write_data(36,(_a),(_b))
#define icx_nvp_read_e00(_a,_b)  icx_nvp_read_data(36,(_a),(_b))

#define icx_nvp_write_e01(_a,_b) icx_nvp_write_data(37,(_a),(_b))
#define icx_nvp_read_e01(_a,_b)  icx_nvp_read_data(37,(_a),(_b))

#define icx_nvp_write_e02(_a,_b) icx_nvp_write_data(38,(_a),(_b))
#define icx_nvp_read_e02(_a,_b)  icx_nvp_read_data(38,(_a),(_b))

#define icx_nvp_write_e03(_a,_b) icx_nvp_write_data(39,(_a),(_b))
#define icx_nvp_read_e03(_a,_b)  icx_nvp_read_data(39,(_a),(_b))

#define icx_nvp_write_e04(_a,_b) icx_nvp_write_data(40,(_a),(_b))
#define icx_nvp_read_e04(_a,_b)  icx_nvp_read_data(40,(_a),(_b))

#define icx_nvp_write_e05(_a,_b) icx_nvp_write_data(41,(_a),(_b))
#define icx_nvp_read_e05(_a,_b)  icx_nvp_read_data(41,(_a),(_b))

#define icx_nvp_write_e06(_a,_b) icx_nvp_write_data(42,(_a),(_b))
#define icx_nvp_read_e06(_a,_b)  icx_nvp_read_data(42,(_a),(_b))

#define icx_nvp_write_e07(_a,_b) icx_nvp_write_data(43,(_a),(_b))
#define icx_nvp_read_e07(_a,_b)  icx_nvp_read_data(43,(_a),(_b))

#define icx_nvp_write_e08(_a,_b) icx_nvp_write_data(44,(_a),(_b))
#define icx_nvp_read_e08(_a,_b)  icx_nvp_read_data(44,(_a),(_b))

#define icx_nvp_write_e09(_a,_b) icx_nvp_write_data(45,(_a),(_b))
#define icx_nvp_read_e09(_a,_b)  icx_nvp_read_data(45,(_a),(_b))

#define icx_nvp_write_e10(_a,_b) icx_nvp_write_data(46,(_a),(_b))
#define icx_nvp_read_e10(_a,_b)  icx_nvp_read_data(46,(_a),(_b))

#define icx_nvp_write_e11(_a,_b) icx_nvp_write_data(47,(_a),(_b))
#define icx_nvp_read_e11(_a,_b)  icx_nvp_read_data(47,(_a),(_b))

#define icx_nvp_write_e12(_a,_b) icx_nvp_write_data(48,(_a),(_b))
#define icx_nvp_read_e12(_a,_b)  icx_nvp_read_data(48,(_a),(_b))

#define icx_nvp_write_e13(_a,_b) icx_nvp_write_data(49,(_a),(_b))
#define icx_nvp_read_e13(_a,_b)  icx_nvp_read_data(49,(_a),(_b))

#define icx_nvp_write_e14(_a,_b) icx_nvp_write_data(50,(_a),(_b))
#define icx_nvp_read_e14(_a,_b)  icx_nvp_read_data(50,(_a),(_b))

#define icx_nvp_write_e15(_a,_b) icx_nvp_write_data(51,(_a),(_b))
#define icx_nvp_read_e15(_a,_b)  icx_nvp_read_data(51,(_a),(_b))

#define icx_nvp_write_e16(_a,_b) icx_nvp_write_data(52,(_a),(_b))
#define icx_nvp_read_e16(_a,_b)  icx_nvp_read_data(52,(_a),(_b))

#define icx_nvp_write_e17(_a,_b) icx_nvp_write_data(53,(_a),(_b))
#define icx_nvp_read_e17(_a,_b)  icx_nvp_read_data(53,(_a),(_b))

#define icx_nvp_write_e18(_a,_b) icx_nvp_write_data(54,(_a),(_b))
#define icx_nvp_read_e18(_a,_b)  icx_nvp_read_data(54,(_a),(_b))

#define icx_nvp_write_e19(_a,_b) icx_nvp_write_data(55,(_a),(_b))
#define icx_nvp_read_e19(_a,_b)  icx_nvp_read_data(55,(_a),(_b))

#define icx_nvp_write_e20(_a,_b) icx_nvp_write_data(56,(_a),(_b))
#define icx_nvp_read_e20(_a,_b)  icx_nvp_read_data(56,(_a),(_b))

#define icx_nvp_write_e21(_a,_b) icx_nvp_write_data(57,(_a),(_b))
#define icx_nvp_read_e21(_a,_b)  icx_nvp_read_data(57,(_a),(_b))

#define icx_nvp_write_e22(_a,_b) icx_nvp_write_data(58,(_a),(_b))
#define icx_nvp_read_e22(_a,_b)  icx_nvp_read_data(58,(_a),(_b))

#define icx_nvp_write_e23(_a,_b) icx_nvp_write_data(59,(_a),(_b))
#define icx_nvp_read_e23(_a,_b)  icx_nvp_read_data(59,(_a),(_b))

#define icx_nvp_write_e24(_a,_b) icx_nvp_write_data(60,(_a),(_b))
#define icx_nvp_read_e24(_a,_b)  icx_nvp_read_data(60,(_a),(_b))

#define icx_nvp_write_e25(_a,_b) icx_nvp_write_data(61,(_a),(_b))
#define icx_nvp_read_e25(_a,_b)  icx_nvp_read_data(61,(_a),(_b))

#define icx_nvp_write_e26(_a,_b) icx_nvp_write_data(62,(_a),(_b))
#define icx_nvp_read_e26(_a,_b)  icx_nvp_read_data(62,(_a),(_b))

#define icx_nvp_write_e27(_a,_b) icx_nvp_write_data(63,(_a),(_b))
#define icx_nvp_read_e27(_a,_b)  icx_nvp_read_data(63,(_a),(_b))

#define icx_nvp_write_e28(_a,_b) icx_nvp_write_data(64,(_a),(_b))
#define icx_nvp_read_e28(_a,_b)  icx_nvp_read_data(64,(_a),(_b))

#define icx_nvp_write_e29(_a,_b) icx_nvp_write_data(65,(_a),(_b))
#define icx_nvp_read_e29(_a,_b)  icx_nvp_read_data(65,(_a),(_b))

#define icx_nvp_write_e30(_a,_b) icx_nvp_write_data(66,(_a),(_b))
#define icx_nvp_read_e30(_a,_b)  icx_nvp_read_data(66,(_a),(_b))

#define icx_nvp_write_e31(_a,_b) icx_nvp_write_data(67,(_a),(_b))
#define icx_nvp_read_e31(_a,_b)  icx_nvp_read_data(67,(_a),(_b))

#define icx_nvp_write_clv(_a,_b) icx_nvp_write_data(68,(_a),(_b))
#define icx_nvp_read_clv(_a,_b)  icx_nvp_read_data(68,(_a),(_b))

#define icx_nvp_write_sps(_a,_b) icx_nvp_write_data(69,(_a),(_b))
#define icx_nvp_read_sps(_a,_b)  icx_nvp_read_data(69,(_a),(_b))

#define icx_nvp_write_rbt(_a,_b) icx_nvp_write_data(70,(_a),(_b))
#define icx_nvp_read_rbt(_a,_b)  icx_nvp_read_data(70,(_a),(_b))

#define icx_nvp_write_edw(_a,_b) icx_nvp_write_data(71,(_a),(_b))
#define icx_nvp_read_edw(_a,_b)  icx_nvp_read_data(71,(_a),(_b))

#define icx_nvp_write_bti(_a,_b) icx_nvp_write_data(72,(_a),(_b))
#define icx_nvp_read_bti(_a,_b)  icx_nvp_read_data(72,(_a),(_b))

#define icx_nvp_write_hdi(_a,_b) icx_nvp_write_data(73,(_a),(_b))
#define icx_nvp_read_hdi(_a,_b)  icx_nvp_read_data(73,(_a),(_b))

#define icx_nvp_write_lbi(_a,_b) icx_nvp_write_data(74,(_a),(_b))
#define icx_nvp_read_lbi(_a,_b)  icx_nvp_read_data(74,(_a),(_b))

#define icx_nvp_write_fui(_a,_b) icx_nvp_write_data(75,(_a),(_b))
#define icx_nvp_read_fui(_a,_b)  icx_nvp_read_data(75,(_a),(_b))

#define icx_nvp_write_eri(_a,_b) icx_nvp_write_data(76,(_a),(_b))
#define icx_nvp_read_eri(_a,_b)  icx_nvp_read_data(76,(_a),(_b))

#define icx_nvp_write_pci(_a,_b) icx_nvp_write_data(87,(_a),(_b))
#define icx_nvp_read_pci(_a,_b)  icx_nvp_read_data(87,(_a),(_b))

#define icx_nvp_write_dbi(_a,_b) icx_nvp_write_data(88,(_a),(_b))
#define icx_nvp_read_dbi(_a,_b)  icx_nvp_read_data(88,(_a),(_b))

#define ICX_NVP_NODE_BASE "/dev/icx_nvp/"

#define ICX_NVP_NODE_DBG ICX_NVP_NODE_BASE "000"
#define ICX_NVP_NODE_SYI ICX_NVP_NODE_BASE "001"
#define ICX_NVP_NODE_UBP ICX_NVP_NODE_BASE "002"
#define ICX_NVP_NODE_BMD ICX_NVP_NODE_BASE "003"
#define ICX_NVP_NODE_PRK ICX_NVP_NODE_BASE "004"
#define ICX_NVP_NODE_HLD ICX_NVP_NODE_BASE "005"
#define ICX_NVP_NODE_PWD ICX_NVP_NODE_BASE "006"
#define ICX_NVP_NODE_MID ICX_NVP_NODE_BASE "007"
#define ICX_NVP_NODE_PCD ICX_NVP_NODE_BASE "008"
#define ICX_NVP_NODE_SER ICX_NVP_NODE_BASE "009"
#define ICX_NVP_NODE_UFN ICX_NVP_NODE_BASE "010"
#define ICX_NVP_NODE_KAS ICX_NVP_NODE_BASE "011"
#define ICX_NVP_NODE_SHP ICX_NVP_NODE_BASE "012"
#define ICX_NVP_NODE_TST ICX_NVP_NODE_BASE "013"
#define ICX_NVP_NODE_GTY ICX_NVP_NODE_BASE "014"
#define ICX_NVP_NODE_CLG ICX_NVP_NODE_BASE "015"
#define ICX_NVP_NODE_SE2 ICX_NVP_NODE_BASE "016"
#define ICX_NVP_NODE_NCP ICX_NVP_NODE_BASE "017"
#define ICX_NVP_NODE_PSK ICX_NVP_NODE_BASE "018"
#define ICX_NVP_NODE_NVR ICX_NVP_NODE_BASE "077"
#define ICX_NVP_NODE_SHE ICX_NVP_NODE_BASE "084"
#define ICX_NVP_NODE_BTC ICX_NVP_NODE_BASE "085"
#define ICX_NVP_NODE_INS ICX_NVP_NODE_BASE "089"
#define ICX_NVP_NODE_CTR ICX_NVP_NODE_BASE "090"
#define ICX_NVP_NODE_SKU ICX_NVP_NODE_BASE "091"
#define ICX_NVP_NODE_BPR ICX_NVP_NODE_BASE "019"
#define ICX_NVP_NODE_BFP ICX_NVP_NODE_BASE "020"
#define ICX_NVP_NODE_BFD ICX_NVP_NODE_BASE "021"
#define ICX_NVP_NODE_BML ICX_NVP_NODE_BASE "022"
#define ICX_NVP_NODE_APD ICX_NVP_NODE_BASE "078"
#define ICX_NVP_NODE_BLF ICX_NVP_NODE_BASE "079"
#define ICX_NVP_NODE_SLP ICX_NVP_NODE_BASE "080"
#define ICX_NVP_NODE_VRT ICX_NVP_NODE_BASE "081"
#define ICX_NVP_NODE_FNI ICX_NVP_NODE_BASE "082"
#define ICX_NVP_NODE_SID ICX_NVP_NODE_BASE "083"
#define ICX_NVP_NODE_MSO ICX_NVP_NODE_BASE "086"
#define ICX_NVP_NODE_DGS ICX_NVP_NODE_BASE "092"
#define ICX_NVP_NODE_ATF ICX_NVP_NODE_BASE "023"
#define ICX_NVP_NODE_LYR ICX_NVP_NODE_BASE "024"
#define ICX_NVP_NODE_DBV ICX_NVP_NODE_BASE "025"
#define ICX_NVP_NODE_FUR ICX_NVP_NODE_BASE "026"
#define ICX_NVP_NODE_UMS ICX_NVP_NODE_BASE "027"
#define ICX_NVP_NODE_SKD ICX_NVP_NODE_BASE "028"
#define ICX_NVP_NODE_UPS ICX_NVP_NODE_BASE "029"
#define ICX_NVP_NODE_AWS ICX_NVP_NODE_BASE "030"
#define ICX_NVP_NODE_FVI ICX_NVP_NODE_BASE "031"
#define ICX_NVP_NODE_MAC ICX_NVP_NODE_BASE "032"
#define ICX_NVP_NODE_FPI ICX_NVP_NODE_BASE "033"
#define ICX_NVP_NODE_SSK ICX_NVP_NODE_BASE "034"
#define ICX_NVP_NODE_TR1 ICX_NVP_NODE_BASE "035"
#define ICX_NVP_NODE_E00 ICX_NVP_NODE_BASE "036"
#define ICX_NVP_NODE_E01 ICX_NVP_NODE_BASE "037"
#define ICX_NVP_NODE_E02 ICX_NVP_NODE_BASE "038"
#define ICX_NVP_NODE_E03 ICX_NVP_NODE_BASE "039"
#define ICX_NVP_NODE_E04 ICX_NVP_NODE_BASE "040"
#define ICX_NVP_NODE_E05 ICX_NVP_NODE_BASE "041"
#define ICX_NVP_NODE_E06 ICX_NVP_NODE_BASE "042"
#define ICX_NVP_NODE_E07 ICX_NVP_NODE_BASE "043"
#define ICX_NVP_NODE_E08 ICX_NVP_NODE_BASE "044"
#define ICX_NVP_NODE_E09 ICX_NVP_NODE_BASE "045"
#define ICX_NVP_NODE_E10 ICX_NVP_NODE_BASE "046"
#define ICX_NVP_NODE_E11 ICX_NVP_NODE_BASE "047"
#define ICX_NVP_NODE_E12 ICX_NVP_NODE_BASE "048"
#define ICX_NVP_NODE_E13 ICX_NVP_NODE_BASE "049"
#define ICX_NVP_NODE_E14 ICX_NVP_NODE_BASE "050"
#define ICX_NVP_NODE_E15 ICX_NVP_NODE_BASE "051"
#define ICX_NVP_NODE_E16 ICX_NVP_NODE_BASE "052"
#define ICX_NVP_NODE_E17 ICX_NVP_NODE_BASE "053"
#define ICX_NVP_NODE_E18 ICX_NVP_NODE_BASE "054"
#define ICX_NVP_NODE_E19 ICX_NVP_NODE_BASE "055"
#define ICX_NVP_NODE_E20 ICX_NVP_NODE_BASE "056"
#define ICX_NVP_NODE_E21 ICX_NVP_NODE_BASE "057"
#define ICX_NVP_NODE_E22 ICX_NVP_NODE_BASE "058"
#define ICX_NVP_NODE_E23 ICX_NVP_NODE_BASE "059"
#define ICX_NVP_NODE_E24 ICX_NVP_NODE_BASE "060"
#define ICX_NVP_NODE_E25 ICX_NVP_NODE_BASE "061"
#define ICX_NVP_NODE_E26 ICX_NVP_NODE_BASE "062"
#define ICX_NVP_NODE_E27 ICX_NVP_NODE_BASE "063"
#define ICX_NVP_NODE_E28 ICX_NVP_NODE_BASE "064"
#define ICX_NVP_NODE_E29 ICX_NVP_NODE_BASE "065"
#define ICX_NVP_NODE_E30 ICX_NVP_NODE_BASE "066"
#define ICX_NVP_NODE_E31 ICX_NVP_NODE_BASE "067"
#define ICX_NVP_NODE_CLV ICX_NVP_NODE_BASE "068"
#define ICX_NVP_NODE_SPS ICX_NVP_NODE_BASE "069"
#define ICX_NVP_NODE_RBT ICX_NVP_NODE_BASE "070"
#define ICX_NVP_NODE_EDW ICX_NVP_NODE_BASE "071"
#define ICX_NVP_NODE_BTI ICX_NVP_NODE_BASE "072"
#define ICX_NVP_NODE_HDI ICX_NVP_NODE_BASE "073"
#define ICX_NVP_NODE_LBI ICX_NVP_NODE_BASE "074"
#define ICX_NVP_NODE_FUI ICX_NVP_NODE_BASE "075"
#define ICX_NVP_NODE_ERI ICX_NVP_NODE_BASE "076"
#define ICX_NVP_NODE_PCI ICX_NVP_NODE_BASE "087"
#define ICX_NVP_NODE_DBI ICX_NVP_NODE_BASE "088"
#define ICX_NVP_NODE_A00 ICX_NVP_NODE_BASE "093"
#define ICX_NVP_NODE_A01 ICX_NVP_NODE_BASE "094"
#define ICX_NVP_NODE_A02 ICX_NVP_NODE_BASE "095"
#define ICX_NVP_NODE_A03 ICX_NVP_NODE_BASE "096"
#define ICX_NVP_NODE_A04 ICX_NVP_NODE_BASE "097"
#define ICX_NVP_NODE_A05 ICX_NVP_NODE_BASE "098"
#define ICX_NVP_NODE_A06 ICX_NVP_NODE_BASE "099"
#define ICX_NVP_NODE_A07 ICX_NVP_NODE_BASE "100"
#define ICX_NVP_NODE_A08 ICX_NVP_NODE_BASE "101"
#define ICX_NVP_NODE_A09 ICX_NVP_NODE_BASE "102"
#define ICX_NVP_NODE_A10 ICX_NVP_NODE_BASE "103"
#define ICX_NVP_NODE_A11 ICX_NVP_NODE_BASE "104"
#define ICX_NVP_NODE_A12 ICX_NVP_NODE_BASE "105"
#define ICX_NVP_NODE_A13 ICX_NVP_NODE_BASE "106"
#define ICX_NVP_NODE_A14 ICX_NVP_NODE_BASE "107"
#define ICX_NVP_NODE_A15 ICX_NVP_NODE_BASE "108"
#define ICX_NVP_NODE_A16 ICX_NVP_NODE_BASE "109"
#define ICX_NVP_NODE_A17 ICX_NVP_NODE_BASE "110"
#define ICX_NVP_NODE_A18 ICX_NVP_NODE_BASE "111"
#define ICX_NVP_NODE_A19 ICX_NVP_NODE_BASE "112"
#define ICX_NVP_NODE_A20 ICX_NVP_NODE_BASE "113"
#define ICX_NVP_NODE_A21 ICX_NVP_NODE_BASE "114"
#define ICX_NVP_NODE_A22 ICX_NVP_NODE_BASE "115"
#define ICX_NVP_NODE_A23 ICX_NVP_NODE_BASE "116"
#define ICX_NVP_NODE_A24 ICX_NVP_NODE_BASE "117"
#define ICX_NVP_NODE_A25 ICX_NVP_NODE_BASE "118"
#define ICX_NVP_NODE_A26 ICX_NVP_NODE_BASE "119"
#define ICX_NVP_NODE_A27 ICX_NVP_NODE_BASE "120"
#define ICX_NVP_NODE_A28 ICX_NVP_NODE_BASE "121"
#define ICX_NVP_NODE_A29 ICX_NVP_NODE_BASE "122"
#define ICX_NVP_NODE_A30 ICX_NVP_NODE_BASE "123"
#define ICX_NVP_NODE_A31 ICX_NVP_NODE_BASE "124"
#define ICX_NVP_NODE_A32 ICX_NVP_NODE_BASE "125"
#define ICX_NVP_NODE_A33 ICX_NVP_NODE_BASE "126"
#define ICX_NVP_NODE_A34 ICX_NVP_NODE_BASE "127"
#define ICX_NVP_NODE_A35 ICX_NVP_NODE_BASE "128"
#define ICX_NVP_NODE_A36 ICX_NVP_NODE_BASE "129"
#define ICX_NVP_NODE_A37 ICX_NVP_NODE_BASE "130"
#define ICX_NVP_NODE_A38 ICX_NVP_NODE_BASE "131"
#define ICX_NVP_NODE_A39 ICX_NVP_NODE_BASE "132"
#define ICX_NVP_NODE_A40 ICX_NVP_NODE_BASE "133"
#define ICX_NVP_NODE_A41 ICX_NVP_NODE_BASE "134"
#define ICX_NVP_NODE_A42 ICX_NVP_NODE_BASE "135"
#define ICX_NVP_NODE_A43 ICX_NVP_NODE_BASE "136"
#define ICX_NVP_NODE_A44 ICX_NVP_NODE_BASE "137"
#define ICX_NVP_NODE_A45 ICX_NVP_NODE_BASE "138"
#define ICX_NVP_NODE_A46 ICX_NVP_NODE_BASE "139"
#define ICX_NVP_NODE_A47 ICX_NVP_NODE_BASE "140"
#define ICX_NVP_NODE_A48 ICX_NVP_NODE_BASE "141"
#define ICX_NVP_NODE_A49 ICX_NVP_NODE_BASE "142"
#define ICX_NVP_NODE_A50 ICX_NVP_NODE_BASE "143"
#define ICX_NVP_NODE_A51 ICX_NVP_NODE_BASE "144"
#define ICX_NVP_NODE_A52 ICX_NVP_NODE_BASE "145"
#define ICX_NVP_NODE_A53 ICX_NVP_NODE_BASE "146"
#define ICX_NVP_NODE_A54 ICX_NVP_NODE_BASE "147"
#define ICX_NVP_NODE_A55 ICX_NVP_NODE_BASE "148"
#define ICX_NVP_NODE_A56 ICX_NVP_NODE_BASE "149"
#define ICX_NVP_NODE_A57 ICX_NVP_NODE_BASE "150"
#define ICX_NVP_NODE_A58 ICX_NVP_NODE_BASE "151"
#define ICX_NVP_NODE_A59 ICX_NVP_NODE_BASE "152"
#define ICX_NVP_NODE_A60 ICX_NVP_NODE_BASE "153"
#define ICX_NVP_NODE_A61 ICX_NVP_NODE_BASE "154"
#define ICX_NVP_NODE_A62 ICX_NVP_NODE_BASE "155"
#define ICX_NVP_NODE_A63 ICX_NVP_NODE_BASE "156"
#define ICX_NVP_NODE_A64 ICX_NVP_NODE_BASE "157"
#define ICX_NVP_NODE_A65 ICX_NVP_NODE_BASE "158"
#define ICX_NVP_NODE_A66 ICX_NVP_NODE_BASE "159"
#define ICX_NVP_NODE_A67 ICX_NVP_NODE_BASE "160"
#define ICX_NVP_NODE_A68 ICX_NVP_NODE_BASE "161"
#define ICX_NVP_NODE_A69 ICX_NVP_NODE_BASE "162"
#define ICX_NVP_NODE_A70 ICX_NVP_NODE_BASE "163"
#define ICX_NVP_NODE_A71 ICX_NVP_NODE_BASE "164"
#define ICX_NVP_NODE_A72 ICX_NVP_NODE_BASE "165"
#define ICX_NVP_NODE_A73 ICX_NVP_NODE_BASE "166"
#define ICX_NVP_NODE_A74 ICX_NVP_NODE_BASE "167"
#define ICX_NVP_NODE_A75 ICX_NVP_NODE_BASE "168"
#define ICX_NVP_NODE_A76 ICX_NVP_NODE_BASE "169"
#define ICX_NVP_NODE_A77 ICX_NVP_NODE_BASE "170"
#define ICX_NVP_NODE_A78 ICX_NVP_NODE_BASE "171"
#define ICX_NVP_NODE_A79 ICX_NVP_NODE_BASE "172"
#define ICX_NVP_NODE_A80 ICX_NVP_NODE_BASE "173"
#define ICX_NVP_NODE_A81 ICX_NVP_NODE_BASE "174"
#define ICX_NVP_NODE_A82 ICX_NVP_NODE_BASE "175"
#define ICX_NVP_NODE_A83 ICX_NVP_NODE_BASE "176"
#define ICX_NVP_NODE_A84 ICX_NVP_NODE_BASE "177"
#define ICX_NVP_NODE_A85 ICX_NVP_NODE_BASE "178"
#define ICX_NVP_NODE_A86 ICX_NVP_NODE_BASE "179"
#define ICX_NVP_NODE_A87 ICX_NVP_NODE_BASE "180"
#define ICX_NVP_NODE_A88 ICX_NVP_NODE_BASE "181"
#define ICX_NVP_NODE_A89 ICX_NVP_NODE_BASE "182"
#define ICX_NVP_NODE_A90 ICX_NVP_NODE_BASE "183"
#define ICX_NVP_NODE_A91 ICX_NVP_NODE_BASE "184"
#define ICX_NVP_NODE_A92 ICX_NVP_NODE_BASE "185"
#define ICX_NVP_NODE_A93 ICX_NVP_NODE_BASE "186"
#define ICX_NVP_NODE_A94 ICX_NVP_NODE_BASE "187"
#define ICX_NVP_NODE_A95 ICX_NVP_NODE_BASE "188"
#define ICX_NVP_NODE_A96 ICX_NVP_NODE_BASE "189"
#define ICX_NVP_NODE_A97 ICX_NVP_NODE_BASE "190"
#define ICX_NVP_NODE_A98 ICX_NVP_NODE_BASE "191"
#define ICX_NVP_NODE_A99 ICX_NVP_NODE_BASE "192"
#define ICX_NVP_NODE_B00 ICX_NVP_NODE_BASE "193"
#define ICX_NVP_NODE_B01 ICX_NVP_NODE_BASE "194"
#define ICX_NVP_NODE_B02 ICX_NVP_NODE_BASE "195"
#define ICX_NVP_NODE_B03 ICX_NVP_NODE_BASE "196"
#define ICX_NVP_NODE_B04 ICX_NVP_NODE_BASE "197"
#define ICX_NVP_NODE_B05 ICX_NVP_NODE_BASE "198"
#define ICX_NVP_NODE_B06 ICX_NVP_NODE_BASE "199"
#define ICX_NVP_NODE_B07 ICX_NVP_NODE_BASE "200"
#define ICX_NVP_NODE_B08 ICX_NVP_NODE_BASE "201"
#define ICX_NVP_NODE_B09 ICX_NVP_NODE_BASE "202"
#define ICX_NVP_NODE_B10 ICX_NVP_NODE_BASE "203"
#define ICX_NVP_NODE_B11 ICX_NVP_NODE_BASE "204"
#define ICX_NVP_NODE_B12 ICX_NVP_NODE_BASE "205"
#define ICX_NVP_NODE_B13 ICX_NVP_NODE_BASE "206"
#define ICX_NVP_NODE_B14 ICX_NVP_NODE_BASE "207"
#define ICX_NVP_NODE_B15 ICX_NVP_NODE_BASE "208"
#define ICX_NVP_NODE_B16 ICX_NVP_NODE_BASE "209"
#define ICX_NVP_NODE_B17 ICX_NVP_NODE_BASE "210"
#define ICX_NVP_NODE_B18 ICX_NVP_NODE_BASE "211"
#define ICX_NVP_NODE_B19 ICX_NVP_NODE_BASE "212"
#define ICX_NVP_NODE_B20 ICX_NVP_NODE_BASE "213"
#define ICX_NVP_NODE_B21 ICX_NVP_NODE_BASE "214"
#define ICX_NVP_NODE_B22 ICX_NVP_NODE_BASE "215"
#define ICX_NVP_NODE_B23 ICX_NVP_NODE_BASE "216"
#define ICX_NVP_NODE_B24 ICX_NVP_NODE_BASE "217"
#define ICX_NVP_NODE_B25 ICX_NVP_NODE_BASE "218"
#define ICX_NVP_NODE_B26 ICX_NVP_NODE_BASE "219"
#define ICX_NVP_NODE_B27 ICX_NVP_NODE_BASE "220"
#define ICX_NVP_NODE_B28 ICX_NVP_NODE_BASE "221"
#define ICX_NVP_NODE_B29 ICX_NVP_NODE_BASE "222"
#define ICX_NVP_NODE_B30 ICX_NVP_NODE_BASE "223"
#define ICX_NVP_NODE_B31 ICX_NVP_NODE_BASE "224"
#define ICX_NVP_NODE_B32 ICX_NVP_NODE_BASE "225"
#define ICX_NVP_NODE_B33 ICX_NVP_NODE_BASE "226"
#define ICX_NVP_NODE_B34 ICX_NVP_NODE_BASE "227"
#define ICX_NVP_NODE_B35 ICX_NVP_NODE_BASE "228"
#define ICX_NVP_NODE_B36 ICX_NVP_NODE_BASE "229"
#define ICX_NVP_NODE_B37 ICX_NVP_NODE_BASE "230"
#define ICX_NVP_NODE_B38 ICX_NVP_NODE_BASE "231"
#define ICX_NVP_NODE_B39 ICX_NVP_NODE_BASE "232"
#define ICX_NVP_NODE_B40 ICX_NVP_NODE_BASE "233"
#define ICX_NVP_NODE_B41 ICX_NVP_NODE_BASE "234"
#define ICX_NVP_NODE_B42 ICX_NVP_NODE_BASE "235"
#define ICX_NVP_NODE_B43 ICX_NVP_NODE_BASE "236"
#define ICX_NVP_NODE_B44 ICX_NVP_NODE_BASE "237"
#define ICX_NVP_NODE_B45 ICX_NVP_NODE_BASE "238"
#define ICX_NVP_NODE_B46 ICX_NVP_NODE_BASE "239"
#define ICX_NVP_NODE_B47 ICX_NVP_NODE_BASE "240"
#define ICX_NVP_NODE_B48 ICX_NVP_NODE_BASE "241"
#define ICX_NVP_NODE_B49 ICX_NVP_NODE_BASE "242"

#endif /* _ICX_NVP_H_ */
