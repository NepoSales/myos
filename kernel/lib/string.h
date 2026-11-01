/* string.h - freestanding replacements for the C string functions */
#ifndef LIB_STRING_H
#define LIB_STRING_H

#include <stddef.h>

void*  memset(void* dst, int c, size_t n);
void*  memcpy(void* dst, const void* src, size_t n);
void*  memmove(void* dst, const void* src, size_t n);
int    memcmp(const void* a, const void* b, size_t n);

size_t strlen(const char* s);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);

/* Copy/concatenate into a buffer of `size` bytes, always NUL-terminating.
 * Return the length of the string they tried to create. */
size_t strlcpy(char* dst, const char* src, size_t size);
size_t strlcat(char* dst, const char* src, size_t size);

/* Parse a decimal integer (optional leading '-'). Returns 1 on success. */
int    parse_int(const char* s, int* out);

#endif
