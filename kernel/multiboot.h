/* multiboot.h - the parts of the Multiboot 1 information structure we use */
#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC  0x2BADB002

#define MULTIBOOT_INFO_MEMORY       (1 << 0)
#define MULTIBOOT_INFO_CMDLINE      (1 << 2)
#define MULTIBOOT_INFO_MEM_MAP      (1 << 6)
#define MULTIBOOT_INFO_LOADER_NAME  (1 << 9)

#define MULTIBOOT_MEMORY_AVAILABLE  1

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;         /* KiB of memory below 1 MiB */
    uint32_t mem_upper;         /* KiB of memory above 1 MiB */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
} __attribute__((packed)) multiboot_info_t;

typedef struct {
    uint32_t size;              /* size of this entry, not counting this field */
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

#endif
