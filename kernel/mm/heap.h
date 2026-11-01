/* heap.h - kernel heap (kmalloc / kfree) */
#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t total;     /* bytes managed by the heap (including headers) */
    uint32_t used;      /* bytes handed out to callers */
    uint32_t free;      /* bytes available in free blocks */
    uint32_t blocks;    /* number of blocks (used + free) */
    uint32_t regions;   /* number of regions obtained from the PMM */
} heap_stats_t;

void  heap_init(void);
void* kmalloc(size_t size);
void* kzalloc(size_t size);                 /* zero-filled */
void* krealloc(void* ptr, size_t size);
void  kfree(void* ptr);
void  heap_get_stats(heap_stats_t* st);

#endif
