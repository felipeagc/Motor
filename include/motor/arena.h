#pragma once

#include <stdint.h>

typedef struct MtArenaBlock MtArenaBlock;

typedef struct MtArena {
    MtArenaBlock *base_block;
    MtArenaBlock *last_block;
    uint32_t base_block_size;
} MtArena;

void mt_arena_init(MtArena *arena, uint32_t base_block_size);

void mt_arena_destroy(MtArena *arena);

void *mt_alloc(MtArena *arena, uint32_t size);

void *mt_calloc(MtArena *arena, uint32_t size);

void *mt_realloc(MtArena *arena, void *ptr, uint32_t size);

char *mt_strdup(MtArena *arena, char *str);

void mt_free(MtArena *arena, void *ptr);
