/* idt.h - Interrupt Descriptor Table and interrupt dispatch */
#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <stdint.h>

#define YIELD_VECTOR 0x30   /* software interrupt used by task_yield() */

/* CPU state saved by the assembly stubs in isr.asm, lowest address first.
 * A pointer to this structure is also the saved stack pointer of a task. */
typedef struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;   /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;                                  /* pushed by the CPU */
} __attribute__((packed)) regs_t;

typedef void (*irq_handler_t)(regs_t* r);

void idt_init(void);
void irq_register(int irq, irq_handler_t handler);

#endif
