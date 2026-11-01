/* panic.h - fatal kernel errors */
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include "arch/idt.h"

/* Print a red error screen and halt the machine. `r` may be NULL. */
void kpanic(const char* msg, regs_t* r) __attribute__((noreturn));

#endif
