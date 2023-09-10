/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Sony Video & Sound Products Inc.
 */

#ifndef _LIF_MD6000_RME_HEADER_
#define _LIF_MD6000_RME_HEADER_

/* device name */
#define MD6000_DEVICE_NAME      "LIF_MD6000_RME"

/* registers */
#define MD6000_REG_DEVICE_ID     0x00
#define MD6000_REG_REVISION      0x01
#define MD6000_REG_SYSTEM        0x02
#define MD6000_REG_MUTE          0x03
#define MD6000_REG_AUDIO_FORMAT  0x04
#define MD6000_REG_CLK_DIV       0x05
#define MD6000_REG_FADER0_H      0x06
#define MD6000_REG_FADER0_M      0x07
#define MD6000_REG_FADER0_L      0x08
#define MD6000_REG_FADER1_H      0x09
#define MD6000_REG_FADER1_M      0x0A
#define MD6000_REG_FADER1_L      0x0B
#define MD6000_REG_STATUS_LEFT   0x0C
#define MD6000_REG_STATUS_RIGHT  0x0D

#define MD6000_REG_SYSTEM_RME_FS_MSK              0x0F
#define MD6000_REG_SYSTEM_STOP_MSK                0x10
#define MD6000_REG_SYSTEM_DS_DLY_SEL_MSK          0x60

#define MD6000_REG_MUTE_SMUTE_MSK                 0x01
#define MD6000_REG_MUTE_EMUTE_MSK                 0x02
#define MD6000_REG_MUTE_FADER_SEL_MSK             0x80

#define MD6000_REG_AUDIO_FORMAT_OUT_PCM_DSD_MSK   0x01
#define MD6000_REG_AUDIO_FORMAT_OUT_I2S_MSK       0x02
#define MD6000_REG_AUDIO_FORMAT_OUT_POLARITY_MSK  0x04
#define MD6000_REG_AUDIO_FORMAT_OUT_ENABLE_MSK    0x08
#define MD6000_REG_AUDIO_FORMAT_IN_PCM_DSD_MSK    0x10
#define MD6000_REG_AUDIO_FORMAT_IN_I2S_MSK        0x20
#define MD6000_REG_AUDIO_FORMAT_IN_POLARITY_MSK   0x40
#define MD6000_REG_AUDIO_FORMAT_IN_ENABLE_MSK     0x80

#define MD6000_REG_CLK_DIV_AUDIO_IN_FS_MSK        0x03
#define MD6000_REG_CLK_DIV_SLEEP                  0x20
#define MD6000_REG_CLK_DIV_BCK_EN_MSK             0x40
#define MD6000_REG_CLK_DIV_MCK_EN_MSK             0x80

#define MD6000_REG_FADER0_H_MASK                  0x0F
#define MD6000_REG_FADER0_M_MASK                  0xFF
#define MD6000_REG_FADER0_L_MASK                  0xFF
#define MD6000_REG_FADER1_H_MASK                  0x0F
#define MD6000_REG_FADER1_M_MASK                  0xFF
#define MD6000_REG_FADER1_L_MASK                  0xFF

#if defined(CONFIG_SND_SOC_IMX_LIF_MD6000_RME)
extern int md6000_wake(unsigned int wake);
extern int md6000_setup_params(unsigned int format, unsigned int rate);
extern int md6000_mute(unsigned int mute);
extern int md6000_fader(unsigned int volume);
extern int md6000_setup_free(void);
#else
static inline int md6000_wake(unsigned int wake)
{
	return 0;
}
static inline int md6000_setup_params(unsigned int format, unsigned int rate)
{
	return 0;
}
static inline int md6000_mute(unsigned int mute)
{
	return 0;
}
static inline int md6000_fader(unsigned int volume)
{
	return 0;
}
static inline int md6000_setup_free(void)
{
	return 0;
}
#endif

#endif
