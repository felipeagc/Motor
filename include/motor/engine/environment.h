#pragma once

#include "api_types.h"
#include <motor/base/math_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MtCmdBuffer MtCmdBuffer;
typedef struct MtImage MtImage;
typedef struct MtSampler MtSampler;
typedef struct MtEngine MtEngine;
typedef struct MtImageAsset MtImageAsset;
typedef struct MtRenderGraph MtRenderGraph;

#define MT_MAX_POINT_LIGHTS 64

typedef struct MtPointLight
{
    Vec4 pos;
    Vec3 color;
    float radius;
} MtPointLight;

typedef struct MtEnvironmentUniform
{
    Vec3 sun_direction;
    float exposure;

    Vec3 sun_color;
    float sun_intensity;

    Mat4 light_space_matrix;

    uint32_t point_light_count;

    float pad1;
    float pad2;
    float pad3;

    MtPointLight point_lights[MT_MAX_POINT_LIGHTS];
} MtEnvironmentUniform;

typedef struct MtEnvironment
{
    MtEngine *engine;

    MtImageAsset *skybox_asset;

    MtImage *skybox_image;
    MtImage *irradiance_image;
    MtImage *radiance_image;

    MtImage *brdf_image;

    MtSampler *skybox_sampler;
    MtSampler *radiance_sampler;

    MtEnvironmentUniform uniform;

    float radiance_mip_levels;
} MtEnvironment;

MT_ENGINE_API void mt_environment_init(MtEnvironment *env, MtEngine *engine);

MT_ENGINE_API void mt_environment_set_skybox(MtEnvironment *env, MtImageAsset *skybox_asset);

MT_ENGINE_API void mt_environment_draw_skybox(MtEnvironment *env, MtCmdBuffer *cb);

MT_ENGINE_API void mt_environment_bind(MtEnvironment *env, MtCmdBuffer *cb, uint32_t set);

MT_ENGINE_API void mt_environment_destroy(MtEnvironment *env);

#ifdef __cplusplus
}
#endif
