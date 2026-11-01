/* panic.c - fatal kernel errors */

#include "panic.h"
#include "drivers/vga.h"
#include "lib/printf.h"

/* Write directly to the console: a panic must never be redirected. */
static void pprintf(const char* fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_write(buf);
}

void kpanic(const char* msg, regs_t* r) {
    __asm__ volatile ("cli");
    console_set_input_hook(NULL);
    console_set_color(VGA_COLOR(VGA_WHITE, VGA_RED));
    console_clear();
    pprintf("\n  *** KERNEL PANIC ***\n\n  %s\n\n", msg);
    if (r) {
        pprintf("  vector=%u  error=%x  eip=%p  cs=%x  eflags=%x\n",
                r->int_no, r->err_code, (void*)r->eip, r->cs, r->eflags);
        pprintf("  eax=%p  ebx=%p  ecx=%p  edx=%p\n",
                (void*)r->eax, (void*)r->ebx, (void*)r->ecx, (void*)r->edx);
        pprintf("  esi=%p  edi=%p  ebp=%p\n",
                (void*)r->esi, (void*)r->edi, (void*)r->ebp);
    }
    pprintf("\n  The system has been halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
