/* pmm.c - bitmap-based physical page frame allocator
 *
 * One bit per 4 KiB page of the 32-bit address space (128 KiB bitmap).
 * A set bit means "used or not RAM". At boot every page starts as used;
 * the regions the bootloader reports as available RAM are then freed,
 * except for the first 1 MiB (BIOS, VGA memory) and the kernel image.
 */

#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "arch/io.h"

#define MAX_PAGES   (1024 * 1024)               /* 4 GiB / 4 KiB */
#define LOW_PAGES   256                         /* first 1 MiB is reserved */

extern char kernel_start[], kernel_end[];       /* from linker.ld */

static uint32_t bitmap[MAX_PAGES / 32];
static uint32_t max_page = 0;                   /* highest usable page + 1 */
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;

static inline int  page_used(uint32_t p) { return bitmap[p / 32] & (1u << (p % 32)); }
static inline void set_used(uint32_t p)  { bitmap[p / 32] |=  (1u << (p % 32)); }
static inline void set_free(uint32_t p)  { bitmap[p / 32] &= ~(1u << (p % 32)); }

static void free_region(uint64_t addr, uint64_t len) {
    if (addr >= 0x100000000ULL) return;
    uint64_t end = addr + len;
    if (end > 0x100000000ULL) end = 0x100000000ULL;

    uint32_t first = (uint32_t)((addr + PAGE_SIZE - 1) >> 12);   /* round up */
    uint32_t last  = (uint32_t)(end >> 12);                      /* round down */
    for (uint32_t p = first; p < last; p++) {
        if (page_used(p)) {
            set_free(p);
            total_pages++;
        }
    }
    if (last > max_page) max_page = last;
}

static void reserve_region(uint32_t start, uint32_t end) {
    for (uint32_t p = start / PAGE_SIZE; p < (end + PAGE_SIZE - 1) / PAGE_SIZE && p < max_page; p++) {
        if (!page_used(p)) {
            set_used(p);
            used_pages++;
        }
    }
}

void pmm_init(multiboot_info_t* mbi) {
    memset(bitmap, 0xFF, sizeof(bitmap));

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        uint32_t addr = mbi->mmap_addr;
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;
        while (addr < end) {
            multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)addr;
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE)
                free_region(e->addr, e->len);
            addr += e->size + sizeof(e->size);
        }
    } else if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        free_region(0x100000, (uint64_t)mbi->mem_upper * 1024);
    }

    reserve_region(0, LOW_PAGES * PAGE_SIZE);
    reserve_region((uint32_t)kernel_start, (uint32_t)kernel_end);

    klog("pmm: %u KiB usable, kernel at %p-%p\n",
         total_pages * 4, kernel_start, kernel_end);
}

void* pmm_alloc_pages(uint32_t count) {
    if (count == 0) return NULL;
    uint32_t f = irq_save();
    uint32_t run = 0;
    for (uint32_t p = LOW_PAGES; p < max_page; p++) {
        run = page_used(p) ? 0 : run + 1;
        if (run == count) {
            uint32_t first = p - count + 1;
            for (uint32_t q = first; q <= p; q++) set_used(q);
            used_pages += count;
            irq_restore(f);
            return (void*)(first * PAGE_SIZE);
        }
    }
    irq_restore(f);
    return NULL;
}

void pmm_free_pages(void* addr, uint32_t count) {
    uint32_t f = irq_save();
    uint32_t first = (uint32_t)addr / PAGE_SIZE;
    for (uint32_t p = first; p < first + count && p < max_page; p++) {
        if (page_used(p)) {
            set_free(p);
            used_pages--;
        }
    }
    irq_restore(f);
}

uint32_t pmm_total_pages(void) { return total_pages; }
uint32_t pmm_used_pages(void)  { return used_pages; }
