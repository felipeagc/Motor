#pragma once

#include <assert.h>
#include <stdint.h>
#include "volk.h"
#include <motor/base/array.h>
#include <motor/base/hashmap.h>
#include <motor/base/bitset.h>
#include <motor/graphics/renderer.h>
#include <motor/graphics/vulkan/vulkan_device.h>

enum { FRAMES_IN_FLIGHT = 2 };

#define VK_CHECK(exp)                                                          \
    do {                                                                       \
        VkResult result = exp;                                                 \
        assert(result == VK_SUCCESS);                                          \
    } while (0)

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

typedef struct QueueFamilyIndices {
    uint32_t graphics;
    uint32_t present;
    uint32_t transfer;
    uint32_t compute;
} QueueFamilyIndices;

typedef struct MtWindowSystem {
    const char **(*get_vulkan_instance_extensions)(uint32_t *count);
    int32_t (*get_physical_device_presentation_support)(
        VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
} MtWindowSystem;

typedef struct BufferBlockAllocation {
    uint8_t *mapping;
    size_t offset;
    size_t padded_size;
} BufferBlockAllocation;

typedef struct BufferBlock {
    MtBuffer *buffer;
    size_t offset;
    size_t alignment;
    size_t spill_size;
    size_t size;
    uint8_t *mapping;
} BufferBlock;

typedef struct BufferPool {
    MtDevice *dev;
    size_t block_size;
    size_t alignment;
    size_t spill_size;
    MtBufferUsage usage;
    /*array*/ BufferBlock *blocks;
} BufferPool;

typedef struct MtDevice {
    MtAllocator *alloc;

    MtVulkanDeviceFlags flags;
    MtWindowSystem *window_system;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    VkPhysicalDevice physical_device;
    VkDevice device;

    QueueFamilyIndices indices;

    VkQueue graphics_queue;
    VkQueue transfer_queue;
    VkQueue present_queue;
    VkQueue compute_queue;

    VmaAllocator gpu_allocator;

    VkPhysicalDeviceProperties physical_device_properties;

    VkFormat preferred_depth_format;

    uint32_t num_threads;
    VkCommandPool *graphics_cmd_pools;
    VkCommandPool *compute_cmd_pools;
    VkCommandPool *transfer_cmd_pools;

    MtHashMap pipeline_layout_map;

    BufferPool ubo_pool;
    BufferPool vbo_pool;
    BufferPool ibo_pool;
} MtDevice;

typedef struct MtRenderPass {
    VkRenderPass renderpass;
    VkExtent2D extent;
    VkFramebuffer current_framebuffer;
    uint64_t hash;
} MtRenderPass;

typedef struct SetInfo {
    uint32_t index;
    /*array*/ VkDescriptorSetLayoutBinding *bindings;
} SetInfo;

typedef struct VertexAttribute {
    VkFormat format;
    uint32_t size;
} VertexAttribute;

typedef struct Shader {
    VkShaderModule mod;
    VkShaderStageFlagBits stage;

    /*array*/ VkPushConstantRange *push_constants;
    /*array*/ SetInfo *sets;

    /*array*/ VertexAttribute *vertex_attributes;
} Shader;

typedef union Descriptor {
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
} Descriptor;

enum { SETS_PER_PAGE = 16 };

typedef struct DescriptorPool {
    /*array*/ VkDescriptorPool *pools;
    /*array*/ VkDescriptorSet **set_arrays;
    /*array*/ uint32_t *allocated_set_counts;
    /*array*/ MtHashMap *pool_hashmaps;

    VkDescriptorSetLayout set_layout;
    VkDescriptorUpdateTemplate update_template;
    /*array*/ VkDescriptorPoolSize *pool_sizes;
} DescriptorPool;

typedef struct PipelineLayout {
    VkPipelineLayout layout;
    VkPipelineBindPoint bind_point;

    /*array*/ DescriptorPool *pools;

    /*array*/ SetInfo *sets;
    /*array*/ VkPushConstantRange *push_constants;

    uint64_t hash;
    uint32_t ref_count;
} PipelineLayout;

typedef struct PipelineInstance {
    VkPipeline vk_pipeline;
    MtPipeline *pipeline;
    VkPipelineBindPoint bind_point;
    uint64_t hash;
} PipelineInstance;

typedef struct MtPipeline {
    VkPipelineBindPoint bind_point;
    MtGraphicsPipelineCreateInfo create_info;
    /*array*/ Shader *shaders;
    uint64_t hash;
    PipelineLayout *layout;
    MtHashMap instances;
} MtPipeline;

typedef struct MtFence {
    VkFence fence;
} MtFence;

enum { MAX_DESCRIPTOR_BINDINGS = 8 };
enum { MAX_DESCRIPTOR_SETS = 8 };

typedef struct MtCmdBuffer {
    MtDevice *dev;
    VkCommandBuffer cmd_buffer;
    PipelineInstance *bound_pipeline_instance;

    MtRenderPass current_renderpass;

    MtViewport current_viewport;

    uint32_t queue_type;

    Descriptor bound_descriptors[MAX_DESCRIPTOR_BINDINGS][MAX_DESCRIPTOR_SETS];
    uint64_t bound_descriptor_set_hashes[MAX_DESCRIPTOR_SETS];

    BufferBlock ubo_block;
    BufferBlock vbo_block;
    BufferBlock ibo_block;
} MtCmdBuffer;

typedef struct MtBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    size_t size;
    MtBufferUsage usage;
    MtBufferMemory memory;
} MtBuffer;

typedef struct MtImage {
    VkImage image;
    VmaAllocation allocation;
    VkImageView image_view;

    VkSampleCountFlags sample_count;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mip_count;
    uint32_t layer_count;
    VkImageAspectFlags aspect;
    VkFormat format;
    VkImageLayout layout;
} MtImage;

typedef struct MtSampler {
    VkSampler sampler;
} MtSampler;
