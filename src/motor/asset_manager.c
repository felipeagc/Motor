#include "../../include/motor/asset_manager.h"

#include <stdio.h>
#include <string.h>
#include "../../include/motor/allocator.h"
#include "../../include/motor/array.h"
#include "../../include/motor/engine.h"

#include "../../include/motor/assets/image_asset.h"
#include "../../include/motor/assets/pipeline_asset.h"
#include "../../include/motor/assets/font_asset.h"

void mt_asset_manager_init(MtAssetManager *am, MtEngine *engine) {
    memset(am, 0, sizeof(*am));
    am->engine = engine;
    am->alloc  = engine->alloc;

    mt_hash_init(&am->asset_map, 51, am->alloc);

    mt_array_push(am->alloc, am->asset_types, mt_image_asset_vt);
    mt_array_push(am->alloc, am->asset_types, mt_pipeline_asset_vt);
    mt_array_push(am->alloc, am->asset_types, mt_font_asset_vt);
}

MtAsset *mt_asset_manager_load(MtAssetManager *am, const char *path) {
    for (uint32_t i = 0; i < mt_array_size(am->asset_types); i++) {
        MtAssetVT *vt = am->asset_types[i];

        for (uint32_t j = 0; j < vt->extension_count; j++) {
            const char *ext = vt->extensions[j];
            size_t ext_len  = strlen(ext);

            size_t path_len = strlen(path);

            if (ext_len >= path_len) continue;

            const char *path_ext = &path[path_len - ext_len];
            if (strncmp(ext, path_ext, ext_len) == 0) {
                uint64_t path_hash = mt_hash_str(path);
                MtIAsset *existing = mt_hash_get_ptr(&am->asset_map, path_hash);

                printf("Loading asset: %s\n", path);

                MtAsset *temp_asset_ptr = mt_alloc(am->alloc, vt->size);

                bool initialized = vt->init(am, temp_asset_ptr, path);
                if (!initialized) {
                    printf("Failed to load asset: %s\n", path);
                    mt_free(am->alloc, temp_asset_ptr);
                    temp_asset_ptr = NULL;
                }

                if (existing) {
                    if (temp_asset_ptr) {
                        existing->vt->destroy(existing->inst);
                        memcpy(existing->inst, temp_asset_ptr, vt->size);
                        mt_free(am->alloc, temp_asset_ptr);
                    }

                    return existing->inst;
                } else {
                    if (temp_asset_ptr) {
                        MtIAsset iasset = {
                            .vt   = vt,
                            .inst = temp_asset_ptr,
                        };
                        MtIAsset *iasset_ptr =
                            mt_array_push(am->alloc, am->assets, iasset);
                        mt_hash_set_ptr(&am->asset_map, path_hash, iasset_ptr);
                        return iasset_ptr->inst;
                    }

                    return NULL;
                }

                return NULL;
            }
        }
    }

    printf("No asset loader found for file: %s\n", path);

    return NULL;
}

void mt_asset_manager_destroy(MtAssetManager *am) {
    mt_hash_destroy(&am->asset_map);

    for (uint32_t i = 0; i < mt_array_size(am->assets); i++) {
        MtIAsset *asset = &am->assets[i];
        asset->vt->destroy(asset->inst);
        mt_free(am->alloc, asset->inst);
    }
    mt_array_free(am->alloc, am->assets);
}
