/* idt.c - Interrupt Descriptor Table and the central interrupt dispatcher */

#include <stdint.h>
#include "arch/idt.h"
#include "arch/gdt.h"
#include "arch/pic.h"
#include "proc/task.h"

typedef struct {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

#define NUM_STUBS 49                /* 32 exceptions + 16 IRQs + yield */

extern uint32_t isr_stub_table[NUM_STUBS];

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;
static irq_handler_t irq_handlers[16];

static const char* exception_names[32] = {
    "Divide by zero", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating-point exception", "Alignment check", "Machine check", "SIMD floating-point exception",
    "Virtualization exception", "Control protection exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection exception", "VMM communication exception", "Security exception", "Reserved",
};

static void idt_set_gate(int vec, uint32_t handler, uint8_t flags) {
    idt[vec].base_low  = handler & 0xFFFF;
    idt[vec].selector  = GDT_KERNEL_CODE;
    idt[vec].zero      = 0;
    idt[vec].flags     = flags;
    idt[vec].base_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void) {
    for (int i = 0; i < NUM_STUBS; i++)
        idt_set_gate(i, isr_stub_table[i], 0x8E);   /* present, ring 0, 32-bit interrupt gate */

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}

void irq_register(int irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

/* Called from isr_common with interrupts disabled. Returns the stack
 * pointer that isr_common must switch to before returning with iret. */
uint32_t interrupt_dispatch(regs_t* r) {
    uint32_t vec = r->int_no;

    if (vec < 32) {
        task_fault(r, exception_names[vec]);
    } else if (vec < 48) {
        int irq = (int)vec - 32;
        if (pic_is_spurious(irq)) return (uint32_t)r;
        if (irq_handlers[irq]) irq_handlers[irq](r);
        pic_send_eoi(irq);
    } else if (vec == YIELD_VECTOR) {
        task_request_resched();
    }

    return task_schedule((uint32_t)r);
}
