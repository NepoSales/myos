/* pmm.h - physical memory manager (4 KiB page frames) */
#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>
#include "multiboot.h"

#define PAGE_SIZE 4096

void     pmm_init(multiboot_info_t* mbi);

/* Allocate `count` physically contiguous pages. Returns NULL on failure. */
void*    pmm_alloc_pages(uint32_t count);
void     pmm_free_pages(void* addr, uint32_t count);

uint32_t pmm_total_pages(void);     /* usable RAM detected at boot */
uint32_t pmm_used_pages(void);      /* includes the kernel image */

#endif
