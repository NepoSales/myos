/* serial.c - COM1 serial port driver (output only, polling)
 *
 * Everything printed on the console is mirrored here, so running QEMU with
 * "-serial stdio" shows the kernel log in the host terminal.
 */

#include "drivers/serial.h"
#include "arch/io.h"

#define COM1 0x3F8

static int serial_ok = 0;

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable UART interrupts */
    outb(COM1 + 3, 0x80);   /* enable DLAB to set the baud rate divisor */
    outb(COM1 + 0, 0x03);   /* divisor 3 -> 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 data bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7);   /* enable and clear FIFOs */
    outb(COM1 + 4, 0x0B);   /* DTR + RTS + OUT2 */

    /* loopback self-test: a missing port reads back garbage */
    outb(COM1 + 4, 0x1E);
    outb(COM1 + 0, 0xAE);
    if (inb(COM1 + 0) != 0xAE) return;
    outb(COM1 + 4, 0x0F);
    serial_ok = 1;
}

void serial_putc(char c) {
    if (!serial_ok) return;
    if (c == '\n') serial_putc('\r');
    for (int spin = 0; spin < 100000 && !(inb(COM1 + 5) & 0x20); spin++)
        ;   /* wait for the transmit buffer to be empty */
    outb(COM1, (uint8_t)c);
}

void serial_write(const char* s) {
    while (*s) serial_putc(*s++);
}
