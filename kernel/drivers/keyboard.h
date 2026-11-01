/* keyboard.h - PS/2 keyboard driver */
#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

/* Non-ASCII keys are returned as codes above 0xFF */
#define KEY_UP      0x101
#define KEY_DOWN    0x102
#define KEY_LEFT    0x103
#define KEY_RIGHT   0x104

#define KEY_CTRL(c) ((c) & 0x1F)    /* e.g. KEY_CTRL('c') == 3 */

void keyboard_init(void);

/* Block the calling task until a key is available, then return it. */
int  keyboard_getchar(void);

#endif
