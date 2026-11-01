/* gdt.c - Global Descriptor Table
 *
 * GRUB leaves us with a working GDT, but its location is unknown and it may
 * be overwritten. We install our own flat model: code and data segments
 * that both cover the full 4 GiB address space at ring 0. Memory protection
 * is not done through segmentation (all tasks share the kernel space).
 */

#include <stdint.h>
#include "arch/gdt.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;   /* flags (high nibble) + limit bits 16..19 */
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[3];
static gdt_ptr_t gdt_ptr;

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].access      = access;
    gdt[i].granularity = (uint8_t)((flags << 4) | ((limit >> 16) & 0x0F));
    gdt[i].base_high   = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    gdt_set(0, 0, 0, 0, 0);                 /* mandatory null descriptor */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC);      /* 0x08: ring 0 code, 4 KiB granularity, 32-bit */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC);      /* 0x10: ring 0 data */

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    /* Load the new GDT, then reload CS with a far jump and the data
     * segment registers with the new selectors. */
    __asm__ volatile (
        "lgdt %0\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "m"(gdt_ptr) : "eax", "memory");
}
