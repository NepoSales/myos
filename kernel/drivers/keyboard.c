/* keyboard.c - interrupt-driven PS/2 keyboard driver (scan code set 1)
 *
 * IRQ 1 fires on every key press/release. The handler translates scan codes
 * to characters, stores them in a ring buffer, and wakes up the task that
 * is waiting for input (if any). Readers sleep instead of busy-polling.
 */

#include <stddef.h>
#include <stdint.h>
#include "drivers/keyboard.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "proc/task.h"

#define KBD_DATA    0x60
#define KBD_STATUS  0x64
#define BUF_SIZE    64

static const char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ',
};

static const char keymap_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ',
};

#define SC_LCTRL    0x1D
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CAPSLOCK 0x3A
#define SC_EXTENDED 0xE0

static uint16_t buffer[BUF_SIZE];
static volatile int head = 0, tail = 0;
static int shift = 0, ctrl = 0, capslock = 0, extended = 0;
static task_t* waiter = NULL;

static void push_key(int key) {
    int next = (head + 1) % BUF_SIZE;
    if (next == tail) return;       /* buffer full: drop the key */
    buffer[head] = (uint16_t)key;
    head = next;
    if (waiter) {
        task_unblock(waiter);
        waiter = NULL;
    }
}

static void keyboard_irq(regs_t* r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA);

    if (sc == SC_EXTENDED) { extended = 1; return; }

    int released = sc & 0x80;
    sc &= 0x7F;

    if (extended) {
        extended = 0;
        if (sc == SC_LCTRL) { ctrl = !released; return; }   /* right ctrl */
        if (released) return;
        switch (sc) {
        case 0x48: push_key(KEY_UP);    break;
        case 0x50: push_key(KEY_DOWN);  break;
        case 0x4B: push_key(KEY_LEFT);  break;
        case 0x4D: push_key(KEY_RIGHT); break;
        }
        return;
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) { shift = !released; return; }
    if (sc == SC_LCTRL) { ctrl = !released; return; }
    if (released) return;
    if (sc == SC_CAPSLOCK) { capslock = !capslock; return; }

    char c = shift ? keymap_shift[sc] : keymap[sc];
    if (!c) return;
    if (capslock && c >= 'a' && c <= 'z' && !shift) c -= 32;
    else if (capslock && c >= 'A' && c <= 'Z' && shift) c += 32;
    if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c = KEY_CTRL(c);
    push_key((unsigned char)c);
}

void keyboard_init(void) {
    while (inb(KBD_STATUS) & 1) inb(KBD_DATA);   /* flush pending bytes */
    irq_register(1, keyboard_irq);
}

int keyboard_getchar(void) {
    for (;;) {
        uint32_t f = irq_save();
        if (head != tail) {
            int key = buffer[tail];
            tail = (tail + 1) % BUF_SIZE;
            irq_restore(f);
            return key;
        }
        /* Nothing buffered: sleep until the IRQ handler wakes us up.
         * Interrupts stay disabled between the check and the block, so a
         * key press cannot be missed in between. */
        waiter = task_current();
        task_block();
        irq_restore(f);
    }
}
