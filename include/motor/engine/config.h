#pragma once

#include "api_types.h"
#include <motor/base/hashmap.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MtAllocator MtAllocator;
typedef struct MtConfigEntry MtConfigEntry;

typedef struct MtConfigObject
{
    /*array*/ MtConfigEntry *entries;
    MtHashMap map;
} MtConfigObject;

typedef enum MtConfigValueType {
    MT_CONFIG_VALUE_STRING,
    MT_CONFIG_VALUE_INT,
    MT_CONFIG_VALUE_FLOAT,
    MT_CONFIG_VALUE_BOOL,
    MT_CONFIG_VALUE_OBJECT,
} MtConfigValueType;

typedef struct MtConfigValue
{
    MtConfigValueType type;
    union
    {
        struct
        {
            const char *string;
            uint32_t length;
        };
        int64_t i64;
        double f64;
        bool boolean;
        MtConfigObject object;
    };
} MtConfigValue;

typedef struct MtConfigEntry
{
    const char *key;
    uint32_t key_length;
    MtConfigValue value;
} MtConfigEntry;

typedef struct MtConfig MtConfig;

MT_ENGINE_API MtConfig *mt_config_parse(MtAllocator *alloc, const char *input, uint64_t input_size);

MT_ENGINE_API MtConfigObject *mt_config_get_root(MtConfig *config);

MT_ENGINE_API void mt_config_destroy(MtConfig *config);

#ifdef __cplusplus
}
#endif
