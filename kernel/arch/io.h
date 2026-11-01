/* io.h - x86 port I/O and interrupt-flag helpers */
#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* A write to an unused port takes ~1 microsecond: used to let slow devices
 * (like the legacy PIC) catch up between commands. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

/* Disable interrupts and return the previous EFLAGS. Together with
 * irq_restore() this is the kernel's only locking primitive: on a single
 * CPU, a section that runs with interrupts disabled cannot be preempted. */
static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    if (flags & 0x200)  /* IF bit was set before irq_save() */
        __asm__ volatile ("sti" : : : "memory");
}

static inline void cpu_halt(void) {
    __asm__ volatile ("hlt");
}

#endif
