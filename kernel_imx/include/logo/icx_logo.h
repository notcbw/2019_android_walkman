/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019 Sony Video & Sound Products Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *
 */

#ifndef ICX_LOGO_H
#define ICX_LOGO_H
#include <linux/types.h>

/* Reserve for kernel logo from boot loader (4MB) */
#define ICX_LOGO_SIZE (4 * 1024 * 1024)
#define ICX_LOGO_ADDR_2G  0xb5b00000
#define ICX_LOGO_ADDR_4G  0x135b00000

void __init reserve_kernel_logo_data(phys_addr_t top_addr);

int load_fb_from_boot_loader(void __iomem *fb_dst, ulong screen_size);

#endif /* ICX_LOGO_H */
