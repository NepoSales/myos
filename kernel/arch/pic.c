/* pic.c - 8259A Programmable Interrupt Controller
 *
 * By default the PIC delivers IRQ 0..7 on vectors 0x08..0x0F, which collide
 * with CPU exceptions. We remap master IRQs to 0x20..0x27 and slave IRQs to
 * 0x28..0x2F, then mask everything until a driver asks for its line.
 */

#include "arch/pic.h"
#include "arch/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_init(void) {
    outb(PIC1_CMD, 0x11);  io_wait();     /* ICW1: init + expect ICW4 */
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, PIC_IRQ_BASE);     io_wait();   /* ICW2: vector offsets */
    outb(PIC2_DATA, PIC_IRQ_BASE + 8); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();     /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();     /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);                /* mask all lines */
    outb(PIC2_DATA, 0xFF);
    pic_unmask(2);                        /* cascade line to the slave PIC */
}

void pic_send_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_unmask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

void pic_mask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (1 << (irq & 7)));
}

/* IRQ 7 and 15 can fire spuriously (e.g. electrical noise). A real
 * interrupt has its bit set in the In-Service Register. */
int pic_is_spurious(int irq) {
    if (irq != 7 && irq != 15) return 0;
    uint16_t cmd = irq == 7 ? PIC1_CMD : PIC2_CMD;
    outb(cmd, 0x0B);                      /* OCW3: read ISR */
    if (inb(cmd) & 0x80) return 0;
    if (irq == 15) outb(PIC1_CMD, PIC_EOI);   /* master still saw IRQ2 */
    return 1;
}
