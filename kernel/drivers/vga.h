/* vga.h - VGA text-mode console
 *
 * Screen layout: row 0 is a status bar owned by the status task, rows
 * 1..24 are a scrolling text console.
 */
#ifndef DRIVERS_VGA_H
#define DRIVERS_VGA_H

#include <stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_RED = 4, VGA_MAGENTA = 5, VGA_BROWN = 6, VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11, VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW = 14, VGA_WHITE = 15,
};

#define VGA_COLOR(fg, bg) ((uint8_t)((fg) | ((bg) << 4)))

void    console_init(void);
void    console_clear(void);
void    console_putc(char c);
void    console_write(const char* s);
void    console_set_color(uint8_t color);
uint8_t console_get_color(void);
void    console_set_fg(enum vga_color fg);   /* keep the current background */

/* While a task waits for keyboard input it can register a redraw function.
 * If another task prints to the console in the meantime, the console moves
 * to a fresh line and calls it, so the prompt and typed text reappear
 * below the background output. Pass NULL to unregister. */
void    console_set_input_hook(void (*redraw)(void));

/* Status bar (row 0). Writes are clipped to the screen width. */
void    status_write(int col, const char* s, uint8_t color);
void    status_fill(int col, int len, uint8_t color);

#endif
