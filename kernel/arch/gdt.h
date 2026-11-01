/* gdt.h - Global Descriptor Table */
#ifndef ARCH_GDT_H
#define ARCH_GDT_H

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10

void gdt_init(void);

#endif
