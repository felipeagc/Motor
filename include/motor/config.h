#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "allocator.h"
#include "string_builder.h"

typedef struct MtConfigEntry MtConfigEntry;

typedef struct MtConfigObject {
    /*array*/ MtConfigEntry *entries;
} MtConfigObject;

typedef enum MtConfigValueType {
    MT_CONFIG_VALUE_STRING,
    MT_CONFIG_VALUE_INT,
    MT_CONFIG_VALUE_FLOAT,
    MT_CONFIG_VALUE_BOOL,
    MT_CONFIG_VALUE_OBJECT,
} MtConfigValueType;

typedef struct MtConfigValue {
    MtConfigValueType type;
    union {
        char *string;
        int64_t i64;
        double f64;
        bool boolean;
        MtConfigObject object;
    };
} MtConfigValue;

typedef struct MtConfigEntry {
    char *key;
    MtConfigValue value;
} MtConfigEntry;

typedef struct MtConfig MtConfig;

MtConfig *mt_config_parse(char *input, uint64_t input_size);

MtConfigObject *mt_config_get_root(MtConfig *config);

void mt_config_destroy(MtConfig *config);
