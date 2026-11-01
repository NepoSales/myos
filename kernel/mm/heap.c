/* heap.c - first-fit free-list allocator on top of the page allocator
 *
 * The heap is a list of blocks sorted by address. Each block starts with a
 * header followed by its payload. Allocation walks the list for the first
 * free block that is large enough and splits it; freeing marks the block
 * free and merges it with free neighbours to fight fragmentation. When no
 * block fits, the heap requests another region from the PMM.
 */

#include "mm/heap.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "arch/io.h"
#include "panic.h"

#define HEAP_MAGIC       0xC0FFEE42
#define ALIGN            16
#define MIN_SPLIT        32         /* don't create free blocks smaller than this */
#define GROW_PAGES       256        /* grow by at least 1 MiB at a time */

typedef struct block {
    uint32_t magic;
    uint32_t size;                  /* payload size in bytes */
    uint32_t free;
    struct block* next;
} block_t;                          /* 16 bytes: keeps payloads 16-byte aligned */

static block_t* head = NULL;
static uint32_t regions = 0;

#define PAYLOAD(b)  ((void*)((char*)(b) + sizeof(block_t)))
#define BLOCK_END(b) ((char*)(b) + sizeof(block_t) + (b)->size)

static int grow(size_t min_payload) {
    uint32_t pages = (uint32_t)((min_payload + sizeof(block_t) + PAGE_SIZE - 1) / PAGE_SIZE);
    if (pages < GROW_PAGES) pages = GROW_PAGES;

    block_t* b = pmm_alloc_pages(pages);
    if (!b) return 0;
    b->magic = HEAP_MAGIC;
    b->size = pages * PAGE_SIZE - sizeof(block_t);
    b->free = 1;

    /* insert sorted by address so adjacent blocks can be merged */
    if (!head || b < head) {
        b->next = head;
        head = b;
    } else {
        block_t* cur = head;
        while (cur->next && cur->next < b) cur = cur->next;
        b->next = cur->next;
        cur->next = b;
    }
    regions++;
    return 1;
}

static void coalesce(void) {
    for (block_t* b = head; b && b->next; ) {
        if (b->free && b->next->free && BLOCK_END(b) == (char*)b->next) {
            b->size += sizeof(block_t) + b->next->size;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
}

void heap_init(void) {
    if (!grow(0)) kpanic("heap: no memory for the initial heap region", NULL);
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    uint32_t f = irq_save();
    for (int attempt = 0; attempt < 2; attempt++) {
        for (block_t* b = head; b; b = b->next) {
            if (!b->free || b->size < size) continue;

            if (b->size >= size + sizeof(block_t) + MIN_SPLIT) {
                block_t* rest = (block_t*)((char*)PAYLOAD(b) + size);
                rest->magic = HEAP_MAGIC;
                rest->size = b->size - size - sizeof(block_t);
                rest->free = 1;
                rest->next = b->next;
                b->next = rest;
                b->size = size;
            }
            b->free = 0;
            irq_restore(f);
            return PAYLOAD(b);
        }
        if (!grow(size)) break;
    }
    irq_restore(f);
    return NULL;
}

void* kzalloc(size_t size) {
    void* p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void* ptr) {
    if (!ptr) return;
    block_t* b = (block_t*)((char*)ptr - sizeof(block_t));
    if (b->magic != HEAP_MAGIC || b->free)
        kpanic("heap: kfree() on an invalid or already freed pointer", NULL);

    uint32_t f = irq_save();
    b->free = 1;
    coalesce();
    irq_restore(f);
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    block_t* b = (block_t*)((char*)ptr - sizeof(block_t));
    if (b->size >= size) return ptr;

    void* p = kmalloc(size);
    if (!p) return NULL;
    memcpy(p, ptr, b->size);
    kfree(ptr);
    return p;
}

void heap_get_stats(heap_stats_t* st) {
    memset(st, 0, sizeof(*st));
    uint32_t f = irq_save();
    for (block_t* b = head; b; b = b->next) {
        st->total += sizeof(block_t) + b->size;
        st->blocks++;
        if (b->free) st->free += b->size;
        else st->used += b->size;
    }
    st->regions = regions;
    irq_restore(f);
}
