#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct MtDevice MtDevice;
typedef struct MtRenderPass MtRenderPass;
typedef struct MtPipeline MtPipeline;
typedef struct MtBuffer MtBuffer;
typedef struct MtImage MtImage;
typedef struct MtSampler MtSampler;
typedef struct MtFence MtFence;
typedef struct MtCmdBuffer MtCmdBuffer;

typedef enum MtQueueType
{
    MT_QUEUE_GRAPHICS,
    MT_QUEUE_COMPUTE,
    MT_QUEUE_TRANSFER,
} MtQueueType;

typedef enum MtFormat
{
    MT_FORMAT_UNDEFINED,

    MT_FORMAT_R8_UINT,
    MT_FORMAT_R32_UINT,

    MT_FORMAT_R8_UNORM,
    MT_FORMAT_RG8_UNORM,
    MT_FORMAT_RGB8_UNORM,
    MT_FORMAT_RGBA8_UNORM,

    MT_FORMAT_BGRA8_UNORM,

    MT_FORMAT_R32_SFLOAT,
    MT_FORMAT_RG32_SFLOAT,
    MT_FORMAT_RGB32_SFLOAT,
    MT_FORMAT_RGBA32_SFLOAT,

    MT_FORMAT_RG16_SFLOAT,
    MT_FORMAT_RGBA16_SFLOAT,

    MT_FORMAT_D16_UNORM,
    MT_FORMAT_D16_UNORM_S8_UINT,
    MT_FORMAT_D24_UNORM_S8_UINT,
    MT_FORMAT_D32_SFLOAT,
    MT_FORMAT_D32_SFLOAT_S8_UINT,
} MtFormat;

typedef enum MtIndexType
{
    MT_INDEX_TYPE_UINT32,
    MT_INDEX_TYPE_UINT16,
} MtIndexType;

typedef enum MtCullMode
{
    MT_CULL_MODE_NONE,
    MT_CULL_MODE_BACK,
    MT_CULL_MODE_FRONT,
    MT_CULL_MODE_FRONT_AND_BACK,
} MtCullMode;

typedef enum MtFrontFace
{
    MT_FRONT_FACE_CLOCKWISE,
    MT_FRONT_FACE_COUNTER_CLOCKWISE,
} MtFrontFace;

typedef struct MtGraphicsPipelineCreateInfo
{
    bool blending;
    bool depth_test;
    bool depth_write;
    bool depth_bias;
    MtCullMode cull_mode;
    MtFrontFace front_face;
    float line_width;
} MtGraphicsPipelineCreateInfo;

typedef struct MtViewport
{
    float x;
    float y;
    float width;
    float height;
    float min_depth;
    float max_depth;
} MtViewport;

typedef enum MtBufferUsage
{
    MT_BUFFER_USAGE_VERTEX,
    MT_BUFFER_USAGE_INDEX,
    MT_BUFFER_USAGE_UNIFORM,
    MT_BUFFER_USAGE_STORAGE,
    MT_BUFFER_USAGE_TRANSFER,
} MtBufferUsage;

typedef enum MtBufferMemory
{
    MT_BUFFER_MEMORY_HOST,
    MT_BUFFER_MEMORY_DEVICE,
} MtBufferMemory;

typedef struct MtBufferCreateInfo
{
    MtBufferUsage usage;
    MtBufferMemory memory;
    size_t size;
} MtBufferCreateInfo;

typedef enum MtImageUsage
{
    MT_IMAGE_USAGE_SAMPLED_BIT                  = 1,
    MT_IMAGE_USAGE_STORAGE_BIT                  = 2,
    MT_IMAGE_USAGE_TRANSFER_SRC_BIT             = 4,
    MT_IMAGE_USAGE_TRANSFER_DST_BIT             = 8,
    MT_IMAGE_USAGE_COLOR_ATTACHMENT_BIT         = 16,
    MT_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 32,
} MtImageUsage;

typedef enum MtImageAspect
{
    MT_IMAGE_ASPECT_COLOR_BIT   = 1,
    MT_IMAGE_ASPECT_DEPTH_BIT   = 2,
    MT_IMAGE_ASPECT_STENCIL_BIT = 4,
} MtImageAspect;

typedef struct MtImageCreateInfo
{
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t sample_count;
    uint32_t mip_count;
    uint32_t layer_count;
    MtFormat format;
    MtImageUsage usage;
    MtImageAspect aspect;
} MtImageCreateInfo;

typedef enum MtFilter
{
    MT_FILTER_LINEAR,
    MT_FILTER_NEAREST,
} MtFilter;

typedef enum MtSamplerAddressMode
{
    MT_SAMPLER_ADDRESS_MODE_REPEAT,
    MT_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    MT_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    MT_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    MT_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
} MtSamplerAddressMode;

typedef enum MtBorderColor
{
    MT_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    MT_BORDER_COLOR_INT_TRANSPARENT_BLACK,
    MT_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    MT_BORDER_COLOR_INT_OPAQUE_BLACK,
    MT_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    MT_BORDER_COLOR_INT_OPAQUE_WHITE,
} MtBorderColor;

typedef struct MtSamplerCreateInfo
{
    bool anisotropy;
    float max_lod;
    MtFilter mag_filter;
    MtFilter min_filter;
    MtSamplerAddressMode address_mode;
    MtBorderColor border_color;
} MtSamplerCreateInfo;

typedef struct MtBufferCopyView
{
    MtBuffer *buffer;
    size_t offset;
    uint32_t row_length;
    uint32_t image_height;
} MtBufferCopyView;

typedef struct MtExtent3D
{
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} MtExtent3D;

typedef struct MtOffset3D
{
    int32_t x;
    int32_t y;
    int32_t z;
} MtOffset3D;

typedef struct MtImageCopyView
{
    MtImage *image;
    uint32_t mip_level;
    uint32_t array_layer;
    MtOffset3D offset;
} MtImageCopyView;

typedef struct MtRenderPassCreateInfo
{
    MtImage *color_attachment;
    MtImage *depth_attachment;
} MtRenderPassCreateInfo;

typedef struct MtRenderer
{
    void (*destroy_device)(MtDevice *);
    void (*device_wait_idle)(MtDevice *);

    void (*allocate_cmd_buffers)(MtDevice *, MtQueueType, uint32_t, MtCmdBuffer **);
    void (*free_cmd_buffers)(MtDevice *, MtQueueType, uint32_t, MtCmdBuffer **);

    MtFence *(*create_fence)(MtDevice *);
    void (*destroy_fence)(MtDevice *, MtFence *fence);

    void (*wait_for_fence)(MtDevice *, MtFence *);
    void (*submit)(MtDevice *, MtCmdBuffer *, MtFence *fence);

    MtBuffer *(*create_buffer)(MtDevice *, MtBufferCreateInfo *);
    void (*destroy_buffer)(MtDevice *, MtBuffer *);

    void *(*map_buffer)(MtDevice *, MtBuffer *);
    void (*unmap_buffer)(MtDevice *, MtBuffer *);

    MtImage *(*create_image)(MtDevice *, MtImageCreateInfo *);
    void (*destroy_image)(MtDevice *, MtImage *);

    MtSampler *(*create_sampler)(MtDevice *, MtSamplerCreateInfo *);
    void (*destroy_sampler)(MtDevice *, MtSampler *);

    MtRenderPass *(*create_render_pass)(MtDevice *, MtRenderPassCreateInfo *);
    void (*destroy_render_pass)(MtDevice *, MtRenderPass *);

    void (*transfer_to_buffer)(
        MtDevice *, MtBuffer *, size_t offset, size_t size, const void *data);
    void (*transfer_to_image)(
        MtDevice *, const MtImageCopyView *dst, size_t size, const void *data);

    MtPipeline *(*create_graphics_pipeline)(
        MtDevice *,
        uint8_t *vertex_code,
        size_t vertex_code_size,
        uint8_t *fragment_code,
        size_t fragment_code_size,
        MtGraphicsPipelineCreateInfo *);
    MtPipeline *(*create_compute_pipeline)(MtDevice *, uint8_t *code, size_t code_size);
    void (*destroy_pipeline)(MtDevice *, MtPipeline *);

    void (*begin_cmd_buffer)(MtCmdBuffer *);
    void (*end_cmd_buffer)(MtCmdBuffer *);

    void (*cmd_get_viewport)(MtCmdBuffer *, MtViewport *);

    void (*cmd_copy_buffer_to_buffer)(
        MtCmdBuffer *,
        MtBuffer *src,
        size_t src_offset,
        MtBuffer *dst,
        size_t dst_offset,
        size_t size);
    void (*cmd_copy_buffer_to_image)(
        MtCmdBuffer *, const MtBufferCopyView *src, const MtImageCopyView *dst, MtExtent3D extent);
    void (*cmd_copy_image_to_buffer)(
        MtCmdBuffer *, const MtImageCopyView *src, const MtBufferCopyView *dst, MtExtent3D extent);

    void (*cmd_begin_render_pass)(MtCmdBuffer *, MtRenderPass *);
    void (*cmd_end_render_pass)(MtCmdBuffer *);

    void (*cmd_set_viewport)(MtCmdBuffer *, MtViewport *);
    void (*cmd_set_scissor)(MtCmdBuffer *, int32_t x, int32_t y, uint32_t w, uint32_t h);

    void (*cmd_bind_uniform)(
        MtCmdBuffer *, const void *data, size_t size, uint32_t set, uint32_t binding);
    void (*cmd_bind_image)(MtCmdBuffer *, MtImage *, MtSampler *, uint32_t set, uint32_t binding);

    void (*cmd_bind_pipeline)(MtCmdBuffer *, MtPipeline *pipeline);

    void (*cmd_bind_vertex_buffer)(MtCmdBuffer *, MtBuffer *, size_t offset);
    void (*cmd_bind_index_buffer)(MtCmdBuffer *, MtBuffer *, MtIndexType index_type, size_t offset);

    void (*cmd_bind_vertex_data)(MtCmdBuffer *, void *data, size_t size);
    void (*cmd_bind_index_data)(MtCmdBuffer *, void *data, size_t size, MtIndexType index_type);

    void (*cmd_draw)(
        MtCmdBuffer *,
        uint32_t vertex_count,
        uint32_t instance_count,
        uint32_t first_vertex,
        uint32_t first_instance);
    void (*cmd_draw_indexed)(
        MtCmdBuffer *,
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t vertex_offset,
        uint32_t first_instance);

    void (*cmd_dispatch)(
        MtCmdBuffer *, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
} MtRenderer;

extern MtRenderer mt_render;
