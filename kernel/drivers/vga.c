/* vga.c - VGA text-mode console driver
 *
 * The VGA text buffer at 0xB8000 holds 80x25 cells of (character, color).
 * Output is also mirrored to the serial port.
 */

#include <stddef.h>
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "arch/io.h"
#include "proc/task.h"

#define VGA_MEMORY  ((volatile uint16_t*)0xB8000)
#define CON_TOP     1               /* first console row (row 0 = status bar) */
#define TAB_WIDTH   4

static volatile uint16_t* const vga = VGA_MEMORY;
static int row = CON_TOP;
static int col = 0;
static uint8_t color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);

static void (*input_redraw)(void) = NULL;
static task_t* input_owner = NULL;
static int redrawing = 0;

static inline uint16_t cell(char c, uint8_t attr) {
    return (uint16_t)(unsigned char)c | (uint16_t)attr << 8;
}

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F); outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E); outb(0x3D5, (pos >> 8) & 0xFF);
}

static void scroll(void) {
    for (int y = CON_TOP + 1; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
    for (int x = 0; x < VGA_WIDTH; x++)
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = cell(' ', color);
    row = VGA_HEIGHT - 1;
}

static void newline(void) {
    col = 0;
    if (++row == VGA_HEIGHT) scroll();
}

static void putc_locked(char c) {
    serial_putc(c);
    switch (c) {
    case '\n':
        newline();
        return;
    case '\r':
        col = 0;
        return;
    case '\t':
        do { putc_locked(' '); } while (col % TAB_WIDTH);
        return;
    case '\b':      /* move back one cell (across line wraps) and erase it */
        if (col > 0) {
            col--;
        } else if (row > CON_TOP) {
            row--;
            col = VGA_WIDTH - 1;
        }
        vga[row * VGA_WIDTH + col] = cell(' ', color);
        return;
    }
    vga[row * VGA_WIDTH + col] = cell(c, color);
    if (++col == VGA_WIDTH) newline();
}

void console_init(void) {
    /* enable a block-shaped hardware cursor on scanlines 13..15 */
    outb(0x3D4, 0x0A); outb(0x3D5, (inb(0x3D5) & 0xC0) | 13);
    outb(0x3D4, 0x0B); outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
    for (int x = 0; x < VGA_WIDTH; x++)
        vga[x] = cell(' ', VGA_COLOR(VGA_BLACK, VGA_LIGHT_GREY));
    console_clear();
}

void console_clear(void) {
    uint32_t f = irq_save();
    for (int y = CON_TOP; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga[y * VGA_WIDTH + x] = cell(' ', color);
    row = CON_TOP;
    col = 0;
    update_cursor();
    irq_restore(f);
}

/* Restore the input line of the waiting task after foreign output. */
static void after_output(void) {
    if (!input_redraw || redrawing || task_current() == input_owner) return;
    redrawing = 1;
    if (col != 0) newline();
    input_redraw();
    redrawing = 0;
}

void console_putc(char c) {
    uint32_t f = irq_save();
    putc_locked(c);
    update_cursor();
    after_output();
    irq_restore(f);
}

void console_write(const char* s) {
    uint32_t f = irq_save();
    while (*s) putc_locked(*s++);
    update_cursor();
    after_output();
    irq_restore(f);
}

void console_set_input_hook(void (*redraw)(void)) {
    uint32_t f = irq_save();
    input_redraw = redraw;
    input_owner = redraw ? task_current() : NULL;
    irq_restore(f);
}

void console_set_color(uint8_t c) { color = c; }
uint8_t console_get_color(void) { return color; }

void console_set_fg(enum vga_color fg) {
    color = (uint8_t)((color & 0xF0) | fg);
}

void status_write(int x, const char* s, uint8_t attr) {
    uint32_t f = irq_save();
    for (; *s && x < VGA_WIDTH; s++, x++)
        if (x >= 0) vga[x] = cell(*s, attr);
    irq_restore(f);
}

void status_fill(int x, int len, uint8_t attr) {
    uint32_t f = irq_save();
    for (int i = 0; i < len && x + i < VGA_WIDTH; i++)
        vga[x + i] = cell(' ', attr);
    irq_restore(f);
}
