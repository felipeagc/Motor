#pragma once

#include <inttypes.h>
#include <stdlib.h>

enum { MT_ARRAY_INITIAL_CAPACITY = 16 };

typedef struct MtArrayHeader {
    uint32_t size;
    uint32_t capacity;
} MtArrayHeader;

typedef struct MtAllocator MtAllocator;

void *
mt_array_grow(MtAllocator *alloc, void *a, uint32_t item_size, uint32_t cap);

#define mt_array_header(a)                                                     \
    ((MtArrayHeader *)((char *)(a) - sizeof(MtArrayHeader)))

#define mt_array_size(a) ((a) ? mt_array_header(a)->size : 0)

#define mt_array_set_size(a, s) ((a) ? mt_array_header(a)->size = s : 0)

#define mt_array_capacity(a) ((a) ? mt_array_header(a)->capacity : 0)

#define mt_array_full(a)                                                       \
    ((a) ? (mt_array_header(a)->size >= mt_array_header(a)->capacity) : 1)

#define mt_array_push(alloc, a, item)                                          \
    (mt_array_full(a) ? (a) = mt_array_grow(alloc, a, sizeof(*a), 0) : 0,      \
     (a)[mt_array_header(a)->size++] = (item),                                 \
     &(a)[mt_array_header(a)->size - 1])

#define mt_array_pop(a)                                                        \
    (mt_array_size(a) > 0 ? (mt_array_header(a)->size--, &a[mt_array_size(a)]) \
                          : NULL)

#define mt_array_reserve(alloc, a, capacity)                                   \
    (mt_array_full(a) ? a = mt_array_grow(alloc, a, sizeof(*a), capacity) : 0)

#define mt_array_pushn(alloc, a, count)                                        \
    (mt_array_reserve(alloc, a, count), mt_array_header(a)->size = count)

#define mt_array_pushn_zeroed(alloc, a, count)                                 \
    (mt_array_reserve(alloc, a, count),                                        \
     mt_array_header(a)->size = count,                                         \
     memset(a, 0, sizeof(*a) * mt_array_size(a)))

#define mt_array_free(alloc, a) ((a) ? mt_free(alloc, mt_array_header(a)) : 0)

#define mt_array_foreach(item, a)                                              \
    for (item = (a); item != (a) + mt_array_size(a); ++item)
