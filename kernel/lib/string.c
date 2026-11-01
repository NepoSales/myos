/* string.c - freestanding string and memory helpers
 *
 * memset/memcpy/memmove/memcmp must use their standard names: GCC may emit
 * calls to them on its own, even in freestanding code.
 */

#include "lib/string.h"

void* memset(void* dst, int c, size_t n) {
    unsigned char* d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = a;
    const unsigned char* y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0')
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char* strchr(const char* s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return (char*)s;
    return c == 0 ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    for (; *s; s++)
        if (*s == (char)c) last = s;
    return c == 0 ? (char*)s : (char*)last;
}

size_t strlcpy(char* dst, const char* src, size_t size) {
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

size_t strlcat(char* dst, const char* src, size_t size) {
    size_t dlen = 0;
    while (dlen < size && dst[dlen]) dlen++;
    if (dlen == size) return size + strlen(src);
    return dlen + strlcpy(dst + dlen, src, size - dlen);
}

int parse_int(const char* s, int* out) {
    int neg = 0, val = 0, any = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
        any = 1;
    }
    if (*s != '\0' || !any) return 0;
    *out = neg ? -val : val;
    return 1;
}
