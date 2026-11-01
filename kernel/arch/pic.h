/* pic.h - 8259A Programmable Interrupt Controller */
#ifndef ARCH_PIC_H
#define ARCH_PIC_H

#include <stdint.h>

#define PIC_IRQ_BASE 0x20   /* IRQ 0..15 are remapped to vectors 0x20..0x2F */

void pic_init(void);
void pic_send_eoi(int irq);
void pic_unmask(int irq);
void pic_mask(int irq);
int  pic_is_spurious(int irq);

#endif
