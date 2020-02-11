#pragma once

#include "asset_manager.h"
#include "entities.h"
#include "nuklear_impl.h"
#include <motor/base/thread_pool.h>

typedef struct MtDevice MtDevice;
typedef struct MtWindow MtWindow;
typedef struct MtSwapchain MtSwapchain;
typedef struct MtImage MtImage;
typedef struct MtSampler MtSampler;
typedef struct MtFileWatcher MtFileWatcher;
typedef struct shaderc_compiler *shaderc_compiler_t;

typedef struct MtEngine
{
    MtDevice *device;
    MtWindow *window;
    MtSwapchain *swapchain;

    MtAllocator *alloc;
    MtThreadPool thread_pool;
    MtAssetManager asset_manager;
    MtEntityManager entity_manager;

    MtNuklearContext *nk_ctx;
    MtFileWatcher *watcher;

    shaderc_compiler_t compiler;

    MtImage *white_image;
    MtImage *black_image;
    MtImage *default_cubemap;
    MtSampler *default_sampler;
} MtEngine;

void mt_engine_init(MtEngine *engine);

void mt_engine_destroy(MtEngine *engine);
