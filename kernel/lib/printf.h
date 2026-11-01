/* printf.h - formatted output
 *
 * Supported conversions: %d %i %u %x %X %p %c %s %%
 * Supported modifiers:   '-' (left align), '0' (zero pad), width, 'l' (ignored)
 */
#ifndef LIB_PRINTF_H
#define LIB_PRINTF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* A growable in-memory buffer. When a task has one attached as its
 * standard output, kprintf() writes there instead of the screen (this is
 * how the shell implements "command > file"). */
typedef struct outbuf {
    char*    data;
    uint32_t len;
    uint32_t cap;
} outbuf_t;

void outbuf_append(outbuf_t* b, const char* s, uint32_t len);
void outbuf_free(outbuf_t* b);

/* Standard output of the current task (screen or redirected buffer) */
void kputc(char c);
void kputs(const char* s);
int  kprintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/* Formatting into a caller-provided buffer (always NUL-terminated) */
int  ksnprintf(char* buf, size_t size, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int  kvsnprintf(char* buf, size_t size, const char* fmt, va_list ap);

/* Kernel log: serial port only, never redirected */
void klog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
