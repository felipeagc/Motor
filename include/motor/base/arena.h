#pragma once

#include "api_types.h"

#ifdef __cpluspus
extern "C" {
#endif

typedef struct MtAllocator MtAllocator;

MT_BASE_API void mt_arena_init(MtAllocator *alloc, uint64_t base_block_size);

MT_BASE_API void mt_arena_destroy(MtAllocator *alloc);

#ifdef __cpluspus
}
#endif
