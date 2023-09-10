/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Sony Video & Sound Products Inc.
 * Copyright 2019 Sony Home Entertainment & Sound Products Inc.
 *
 */

#include <linux/memblock.h>
#include <asm/memory.h>
#include <logo/icx_logo.h>

static phys_addr_t kenrel_logo_addr = 0;

void __init reserve_kernel_logo_data(phys_addr_t top_addr)
{
	if (top_addr <= ICX_LOGO_SIZE) {
		pr_err("[kenrel logo] invalid top addr\n");
		return;
	}

	if (top_addr > 0xC0000000) {
		kenrel_logo_addr = ICX_LOGO_ADDR_4G;
	}
	else {
		kenrel_logo_addr = ICX_LOGO_ADDR_2G;
	}

	if (!memblock_is_region_memory(kenrel_logo_addr, ICX_LOGO_SIZE)) {
		pr_err("[kernel logo] 0x%lx+0x%08x is not a memory region\n",
			(ulong)kenrel_logo_addr, ICX_LOGO_SIZE);
		return;
	}
	if (memblock_is_region_reserved(kenrel_logo_addr, ICX_LOGO_SIZE)) {
		pr_err("[kernel logo] logo fb is overlapped\n");
		return;
	}
	if (memblock_reserve(kenrel_logo_addr, ICX_LOGO_SIZE) < 0) {
		pr_err("[kernel logo] memblock_reserve failed\n");
		return;
	}
	pr_info("[kernel logo] Reserved %dKB at 0x%lx for kernel logo\n",
		ICX_LOGO_SIZE >> 10, (ulong)kenrel_logo_addr);
}

int load_fb_from_boot_loader(void __iomem *fb_dst, ulong screen_size)
{
	void *src_virt;

	if (kenrel_logo_addr == 0) {
		pr_err("[kernel logo] invalid kernel logo addr\n");
		return -1;
	}

	if (!memblock_is_region_memory(kenrel_logo_addr, ICX_LOGO_SIZE)) {
		pr_err("[kernel logo] 0x%08lx+0x%08x is not a memory region\n",
			(ulong)kenrel_logo_addr, ICX_LOGO_SIZE);
		return -1;
	}

	if (!memblock_is_region_reserved(kenrel_logo_addr, ICX_LOGO_SIZE)) {
		pr_err("[kernel logo] Memory for logo not reserved.\n");
		return -1;
	}

	src_virt = phys_to_virt(kenrel_logo_addr);
	memcpy((void *)fb_dst, src_virt, screen_size);

	memblock_free(kenrel_logo_addr, ICX_LOGO_SIZE);

	pr_info("[kernel logo] Loaded fb from boot loader.\n");

	return 0;
}
