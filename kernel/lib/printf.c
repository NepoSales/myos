/* printf.c - a small printf implementation shared by every output target */

#include "lib/printf.h"
#include "lib/string.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "mm/heap.h"
#include "proc/task.h"

typedef void (*emit_fn)(char c, void* ctx);

static void emit_number(emit_fn emit, void* ctx, uint32_t val, int base, int upper,
                        int neg, int width, int zero, int left) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[12];
    int n = 0;
    do { tmp[n++] = digits[val % base]; val /= base; } while (val);

    int len = n + neg;
    int pad = width > len ? width - len : 0;

    if (!left && !zero) while (pad-- > 0) emit(' ', ctx);
    if (neg) emit('-', ctx);
    if (!left && zero) while (pad-- > 0) emit('0', ctx);
    while (n) emit(tmp[--n], ctx);
    if (left) while (pad-- > 0) emit(' ', ctx);
}

static void vformat(emit_fn emit, void* ctx, const char* fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(*fmt, ctx); continue; }
        fmt++;

        int left = 0, zero = 0, width = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') fmt++;

        switch (*fmt) {
        case 'd': case 'i': {
            int v = va_arg(ap, int);
            uint32_t u = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
            emit_number(emit, ctx, u, 10, 0, v < 0, width, zero, left);
            break;
        }
        case 'u':
            emit_number(emit, ctx, va_arg(ap, uint32_t), 10, 0, 0, width, zero, left);
            break;
        case 'x': case 'X':
            emit_number(emit, ctx, va_arg(ap, uint32_t), 16, *fmt == 'X', 0, width, zero, left);
            break;
        case 'p':
            emit('0', ctx); emit('x', ctx);
            emit_number(emit, ctx, (uint32_t)va_arg(ap, void*), 16, 0, 0, 8, 1, 0);
            break;
        case 'c':
            emit((char)va_arg(ap, int), ctx);
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            int pad = width > len ? width - len : 0;
            if (!left) while (pad-- > 0) emit(' ', ctx);
            while (*s) emit(*s++, ctx);
            if (left) while (pad-- > 0) emit(' ', ctx);
            break;
        }
        case '%':
            emit('%', ctx);
            break;
        case '\0':
            return;
        default:
            emit('%', ctx);
            emit(*fmt, ctx);
        }
    }
}

/* ---------- in-memory output buffers ---------- */

void outbuf_append(outbuf_t* b, const char* s, uint32_t len) {
    if (b->len + len + 1 > b->cap) {
        uint32_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + len + 1) cap *= 2;
        char* p = krealloc(b->data, cap);
        if (!p) return;                 /* out of memory: drop output */
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, len);
    b->len += len;
    b->data[b->len] = '\0';
}

void outbuf_free(outbuf_t* b) {
    kfree(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---------- standard output ---------- */

void kputc(char c) {
    outbuf_t* out = task_stdout();
    if (out) outbuf_append(out, &c, 1);
    else console_putc(c);
}

void kputs(const char* s) {
    outbuf_t* out = task_stdout();
    if (out) outbuf_append(out, s, strlen(s));
    else console_write(s);
}

/* Formatted into a buffer first, so each call reaches the console as one
 * write (longer output is truncated at 511 characters). */
int kprintf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kputs(buf);
    return n;
}

/* ---------- string formatting ---------- */

typedef struct { char* buf; size_t size; size_t pos; } strbuf_t;

static void emit_buf(char c, void* ctx) {
    strbuf_t* sb = ctx;
    if (sb->pos + 1 < sb->size) sb->buf[sb->pos] = c;
    sb->pos++;
}

int kvsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    strbuf_t sb = { buf, size, 0 };
    vformat(emit_buf, &sb, fmt, ap);
    if (size) buf[sb.pos < size ? sb.pos : size - 1] = '\0';
    return (int)sb.pos;
}

int ksnprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ---------- kernel log ---------- */

static void emit_serial(char c, void* ctx) {
    (void)ctx;
    serial_putc(c);
}

void klog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vformat(emit_serial, NULL, fmt, ap);
    va_end(ap);
}
