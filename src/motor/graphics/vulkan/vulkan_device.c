#include <motor/graphics/vulkan/vulkan_device.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <motor/base/api_types.h>
#include <motor/base/log.h>
#include <motor/base/allocator.h>
#include <motor/graphics/window.h>

#include "internal.h"
#include "vk_mem_alloc.h"

static void device_wait_idle(MtDevice *dev);
static void submit_cmd(MtDevice *dev, SubmitInfo *info);

static void allocate_cmd_buffers(
    MtDevice *dev, MtQueueType queue_type, uint32_t count, MtCmdBuffer **cmd_buffers);
static void
free_cmd_buffers(MtDevice *dev, MtQueueType queue_type, uint32_t count, MtCmdBuffer **cmd_buffers);

#include "conversions.inl"
#include "hashing.inl"
#include "buffer.inl"
#include "buffer_pool.inl"
#include "descriptor_pool.inl"
#include "pipeline.inl"
#include "image.inl"
#include "sampler.inl"
#include "cmd_buffer.inl"

#include "swapchain.inl"

#include "graph.inl"

// clang-format off
#if !(defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201102L)) && !defined(_Thread_local)
    #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
        #define _Thread_local __thread
    #else
        #define _Thread_local __declspec(thread)
    #endif
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) &&                                              \
    (((__GNUC__ << 8) | __GNUC_MINOR__) < ((4 << 8) | 9))
    #define _Thread_local __thread
#endif
// clang-format on

#define MT_THREAD_LOCAL _Thread_local

static MT_THREAD_LOCAL uint32_t renderer_thread_id = 0;

#if !defined(NDEBUG)
// Debug mode
#define MT_ENABLE_VALIDATION
#endif

#ifdef MT_ENABLE_VALIDATION
static const char *VALIDATION_LAYERS[1] = {
    "VK_LAYER_KHRONOS_validation",
};
static const char *INSTANCE_EXTENSIONS[1] = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};
#else
/* static const char *VALIDATION_LAYERS[0] = {}; */
/* static const char *INSTANCE_EXTENSIONS[0] = {}; */
#endif

static const char *DEVICE_EXTENSIONS[1] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// Setup {{{
static VkBool32 debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
    void *p_user_data)
{
    if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        mt_log("Validation layer: %s\n", p_callback_data->pMessage);
    }
    return VK_FALSE;
}

static bool are_indices_complete(MtDevice *dev, QueueFamilyIndices *indices)
{
    return indices->graphics != UINT32_MAX && indices->transfer != UINT32_MAX &&
           indices->compute != UINT32_MAX;
}

#ifdef MT_ENABLE_VALIDATION
static bool check_validation_layer_support(MtDevice *device)
{
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);

    VkLayerProperties *available_layers =
        mt_alloc(device->alloc, sizeof(VkLayerProperties) * layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

    for (uint32_t l = 0; l < MT_LENGTH(VALIDATION_LAYERS); l++)
    {
        const char *layer_name = VALIDATION_LAYERS[l];

        bool layer_found = false;
        for (uint32_t a = 0; a < layer_count; a++)
        {
            VkLayerProperties *layer_properties = &available_layers[a];
            if (strcmp(layer_name, layer_properties->layerName) == 0)
            {
                layer_found = true;
                break;
            }
        }

        if (!layer_found)
        {
            mt_free(device->alloc, available_layers);
            return false;
        }
    }

    mt_free(device->alloc, available_layers);
    return true;
}
#endif

static void create_instance(MtDevice *dev)
{
#ifdef MT_ENABLE_VALIDATION
    if (!check_validation_layer_support(dev))
    {
        mt_log_fatal("Application wants to enable validation layers but does not "
                     "support "
                     "them\n");
        exit(1);
    }
#endif

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Motor",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Motor",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };

#ifdef MT_ENABLE_VALIDATION
    create_info.enabledLayerCount = MT_LENGTH(VALIDATION_LAYERS);
    create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

    const char **extensions = NULL;
    uint32_t extension_count = 0;

#ifdef MT_ENABLE_VALIDATION
    if (MT_LENGTH(INSTANCE_EXTENSIONS) > 0)
    {
        extension_count = MT_LENGTH(INSTANCE_EXTENSIONS);
        extensions = mt_alloc(dev->alloc, sizeof(char *) * extension_count);
        memcpy(extensions, INSTANCE_EXTENSIONS, sizeof(char *) * MT_LENGTH(INSTANCE_EXTENSIONS));
    }
#endif

    if (!(dev->flags & MT_DEVICE_HEADLESS))
    {
        uint32_t window_extension_count;
        const char **window_extensions =
            swapchain_get_required_instance_extensions(&window_extension_count);

        extension_count += window_extension_count;
        extensions = mt_realloc(dev->alloc, extensions, sizeof(char *) * extension_count);

        memcpy(
            &extensions[extension_count - window_extension_count],
            window_extensions,
            window_extension_count * sizeof(char *));
    }

    create_info.enabledExtensionCount = extension_count;
    create_info.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&create_info, NULL, &dev->instance));

    volkLoadInstance(dev->instance);

    mt_free(dev->alloc, extensions);
}

static void create_debug_messenger(MtDevice *dev)
{
    VkDebugUtilsMessengerCreateInfoEXT create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = &debug_callback;

    VK_CHECK(
        vkCreateDebugUtilsMessengerEXT(dev->instance, &create_info, NULL, &dev->debug_messenger));
}

static QueueFamilyIndices find_queue_families(MtDevice *dev, VkPhysicalDevice physical_device)
{
    QueueFamilyIndices indices;
    indices.graphics = UINT32_MAX;
    indices.transfer = UINT32_MAX;
    indices.compute = UINT32_MAX;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);

    VkQueueFamilyProperties *queue_families =
        mt_alloc(dev->alloc, sizeof(VkQueueFamilyProperties) * queue_family_count);
    memset(queue_families, 0, sizeof(VkQueueFamilyProperties) * queue_family_count);

    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; i++)
    {
        VkQueueFamilyProperties *queue_family = &queue_families[i];
        if (queue_family->queueCount > 0 && queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphics = i;
        }

        if (queue_family->queueCount > 0 && queue_family->queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            indices.transfer = i;
        }

        if (queue_family->queueCount > 0 && queue_family->queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            indices.compute = i;
        }

        if (are_indices_complete(dev, &indices)) break;
    }

    mt_free(dev->alloc, queue_families);
    return indices;
}

static bool check_device_extension_support(MtDevice *dev, VkPhysicalDevice physical_device)
{
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(physical_device, NULL, &extension_count, NULL);

    VkExtensionProperties *available_extensions =
        mt_alloc(dev->alloc, sizeof(VkExtensionProperties) * extension_count);

    vkEnumerateDeviceExtensionProperties(
        physical_device, NULL, &extension_count, available_extensions);

    bool found_all = true;

    for (uint32_t i = 0; i < MT_LENGTH(DEVICE_EXTENSIONS); i++)
    {
        const char *required_ext = DEVICE_EXTENSIONS[i];
        bool found = false;
        for (uint32_t j = 0; j < extension_count; j++)
        {
            VkExtensionProperties *available_ext = &available_extensions[j];
            if (strcmp(available_ext->extensionName, required_ext) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found) found_all = false;
    }

    mt_free(dev->alloc, available_extensions);
    return found_all;
}

static bool is_device_suitable(MtDevice *dev, VkPhysicalDevice physical_device)
{
    QueueFamilyIndices indices = find_queue_families(dev, physical_device);

    bool extensions_supported = check_device_extension_support(dev, physical_device);

    return are_indices_complete(dev, &indices) && extensions_supported;
}

static void pick_physical_device(MtDevice *dev)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(dev->instance, &device_count, NULL);

    if (device_count == 0)
    {
        mt_log_fatal("No vulkan capable devices found\n");
        exit(1);
    }

    VkPhysicalDevice *devices = mt_alloc(dev->alloc, sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(dev->instance, &device_count, devices);

    for (uint32_t i = 0; i < device_count; i++)
    {
        if (is_device_suitable(dev, devices[i]))
        {
            dev->physical_device = devices[i];
            break;
        }
    }

    if (dev->physical_device == VK_NULL_HANDLE)
    {
        mt_log_fatal("Could not find a physical device that suits the application "
                     "requirements\n");
        exit(1);
    }

    vkGetPhysicalDeviceProperties(dev->physical_device, &dev->physical_device_properties);

    mt_free(dev->alloc, devices);
}

static void create_device(MtDevice *dev)
{
    dev->indices = find_queue_families(dev, dev->physical_device);

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_infos[3] = {0};
    uint32_t queue_create_info_count = 1;

    queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[0].queueFamilyIndex = dev->indices.graphics;
    queue_create_infos[0].queueCount = 1;
    queue_create_infos[0].pQueuePriorities = &queue_priority;

    if (dev->indices.graphics != dev->indices.transfer)
    {
        queue_create_info_count++;
        queue_create_infos[queue_create_info_count - 1].sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[queue_create_info_count - 1].queueFamilyIndex = dev->indices.transfer;
        queue_create_infos[queue_create_info_count - 1].queueCount = 1;
        queue_create_infos[queue_create_info_count - 1].pQueuePriorities = &queue_priority;
    }

    if (dev->indices.graphics != dev->indices.compute)
    {
        queue_create_info_count++;
        queue_create_infos[queue_create_info_count - 1].sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[queue_create_info_count - 1].queueFamilyIndex = dev->indices.compute;
        queue_create_infos[queue_create_info_count - 1].queueCount = 1;
        queue_create_infos[queue_create_info_count - 1].pQueuePriorities = &queue_priority;
    }

    VkPhysicalDeviceFeatures device_features = {0};
    vkGetPhysicalDeviceFeatures(dev->physical_device, &device_features);

    if (!device_features.fillModeNonSolid || !device_features.samplerAnisotropy ||
        !device_features.textureCompressionBC)
    {
        mt_log_fatal("Vulkan physical device missing required features");
        abort();
    }

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queue_create_infos,
        .queueCreateInfoCount = queue_create_info_count,
        .pEnabledFeatures = &device_features,
    };

#ifdef MT_ENABLE_VALIDATION
    create_info.enabledLayerCount = MT_LENGTH(VALIDATION_LAYERS);
    create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

    if (!(dev->flags & MT_DEVICE_HEADLESS))
    {
        create_info.enabledExtensionCount = MT_LENGTH(DEVICE_EXTENSIONS);
        create_info.ppEnabledExtensionNames = DEVICE_EXTENSIONS;
    }

    VK_CHECK(vkCreateDevice(dev->physical_device, &create_info, NULL, &dev->device));

    vkGetDeviceQueue(dev->device, dev->indices.graphics, 0, &dev->graphics_queue);
    vkGetDeviceQueue(dev->device, dev->indices.transfer, 0, &dev->transfer_queue);
    vkGetDeviceQueue(dev->device, dev->indices.compute, 0, &dev->compute_queue);
}

static void create_allocator(MtDevice *dev)
{
    VmaAllocatorCreateInfo allocator_info = {0};
    allocator_info.physicalDevice = dev->physical_device;
    allocator_info.device = dev->device;

    VmaVulkanFunctions vk_functions = {
        .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory = vkAllocateMemory,
        .vkFreeMemory = vkFreeMemory,
        .vkMapMemory = vkMapMemory,
        .vkUnmapMemory = vkUnmapMemory,
        .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory = vkBindBufferMemory,
        .vkBindImageMemory = vkBindImageMemory,
        .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
        .vkCreateBuffer = vkCreateBuffer,
        .vkDestroyBuffer = vkDestroyBuffer,
        .vkCreateImage = vkCreateImage,
        .vkDestroyImage = vkDestroyImage,
        .vkCmdCopyBuffer = vkCmdCopyBuffer,
    };

    allocator_info.pVulkanFunctions = &vk_functions;

    VK_CHECK(vmaCreateAllocator(&allocator_info, &dev->gpu_allocator));
}

static VkFormat find_supported_format(
    MtDevice *dev,
    VkFormat *candidates,
    uint32_t candidate_count,
    VkImageTiling tiling,
    VkFormatFeatureFlags features)
{
    for (uint32_t i = 0; i < candidate_count; i++)
    {
        VkFormat format = candidates[i];

        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(dev->physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (
            tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    return VK_FORMAT_UNDEFINED;
}

static void find_supported_depth_format(MtDevice *dev)
{
    VkFormat candidates[3] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
    };

    dev->preferred_depth_format = find_supported_format(
        dev,
        candidates,
        MT_LENGTH(candidates),
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static void create_command_pools(MtDevice *dev)
{
    dev->graphics_cmd_pools = mt_alloc(dev->alloc, sizeof(VkCommandPool) * dev->num_threads);

    for (uint32_t i = 0; i < dev->num_threads; i++)
    {
        VkCommandPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = dev->indices.graphics,
        };
        VK_CHECK(vkCreateCommandPool(dev->device, &create_info, NULL, &dev->graphics_cmd_pools[i]));
    }

    dev->compute_cmd_pools = dev->graphics_cmd_pools;
    if (dev->indices.graphics != dev->indices.compute)
    {
        dev->compute_cmd_pools = mt_alloc(dev->alloc, sizeof(VkCommandPool) * dev->num_threads);

        for (uint32_t i = 0; i < dev->num_threads; i++)
        {
            VkCommandPoolCreateInfo create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = dev->indices.compute,
            };
            VK_CHECK(
                vkCreateCommandPool(dev->device, &create_info, NULL, &dev->compute_cmd_pools[i]));
        }
    }

    dev->transfer_cmd_pools = mt_alloc(dev->alloc, sizeof(VkCommandPool) * dev->num_threads);
    for (uint32_t i = 0; i < dev->num_threads; i++)
    {
        VkCommandPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = dev->indices.transfer,
        };
        VK_CHECK(vkCreateCommandPool(dev->device, &create_info, NULL, &dev->transfer_cmd_pools[i]));
    }
}
// }}}

// Device functions {{{
static void allocate_cmd_buffers(
    MtDevice *dev, MtQueueType queue_type, uint32_t count, MtCmdBuffer **cmd_buffers)
{
    VkCommandPool pool = VK_NULL_HANDLE;

    switch (queue_type)
    {
        case MT_QUEUE_GRAPHICS: {
            pool = dev->graphics_cmd_pools[renderer_thread_id];
            break;
        }
        case MT_QUEUE_COMPUTE: {
            pool = dev->compute_cmd_pools[renderer_thread_id];
            break;
        }
        case MT_QUEUE_TRANSFER: {
            pool = dev->transfer_cmd_pools[renderer_thread_id];
            break;
        }
    }

    assert(pool);

    VkCommandBuffer *command_buffers = mt_alloc(dev->alloc, sizeof(VkCommandBuffer) * count);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = count,
    };

    VK_CHECK(vkAllocateCommandBuffers(dev->device, &alloc_info, command_buffers));

    for (uint32_t i = 0; i < count; i++)
    {
        cmd_buffers[i] = mt_alloc(dev->alloc, sizeof(*cmd_buffers[i]));
        memset(cmd_buffers[i], 0, sizeof(*cmd_buffers[i]));
        cmd_buffers[i]->dev = dev;
        cmd_buffers[i]->cmd_buffer = command_buffers[i];
        cmd_buffers[i]->queue_type = queue_type;
    }

    mt_free(dev->alloc, command_buffers);
}

static void
free_cmd_buffers(MtDevice *dev, MtQueueType queue_type, uint32_t count, MtCmdBuffer **cmd_buffers)
{
    device_wait_idle(dev);

    VkCommandPool pool = VK_NULL_HANDLE;

    switch (queue_type)
    {
        case MT_QUEUE_GRAPHICS: {
            pool = dev->graphics_cmd_pools[renderer_thread_id];
            break;
        }
        case MT_QUEUE_COMPUTE: {
            pool = dev->compute_cmd_pools[renderer_thread_id];
            break;
        }
        case MT_QUEUE_TRANSFER: {
            pool = dev->transfer_cmd_pools[renderer_thread_id];
            break;
        }
    }

    assert(pool);

    VkCommandBuffer *command_buffers = mt_alloc(dev->alloc, sizeof(VkCommandBuffer) * count);

    for (uint32_t i = 0; i < count; i++)
    {
        command_buffers[i] = cmd_buffers[i]->cmd_buffer;
    }

    vkFreeCommandBuffers(dev->device, pool, count, command_buffers);

    for (uint32_t i = 0; i < count; i++)
    {
        MtCmdBuffer *cb = cmd_buffers[i];

        for (BufferBlock *block = cb->ubo_blocks;
             block != cb->ubo_blocks + mt_array_size(cb->ubo_blocks);
             ++block)
        {
            buffer_pool_recycle(&dev->ubo_pool, block);
        }

        for (BufferBlock *block = cb->vbo_blocks;
             block != cb->vbo_blocks + mt_array_size(cb->vbo_blocks);
             ++block)
        {
            buffer_pool_recycle(&dev->vbo_pool, block);
        }

        for (BufferBlock *block = cb->ibo_blocks;
             block != cb->ibo_blocks + mt_array_size(cb->ibo_blocks);
             ++block)
        {
            buffer_pool_recycle(&dev->ibo_pool, block);
        }

        mt_array_free(dev->alloc, cb->ubo_blocks);
        mt_array_free(dev->alloc, cb->vbo_blocks);
        mt_array_free(dev->alloc, cb->ibo_blocks);

        mt_free(dev->alloc, cb);
    }

    mt_free(dev->alloc, command_buffers);
}

static void submit_cmd(MtDevice *dev, SubmitInfo *info)
{
    if (info->wait_semaphores && info->wait_semaphore_count == 0)
    {
        info->wait_semaphore_count = 1;
    }

    if (info->signal_semaphores && info->signal_semaphore_count == 0)
    {
        info->signal_semaphore_count = 1;
    }

    VkQueue queue = VK_NULL_HANDLE;
    switch (info->cmd_buffer->queue_type)
    {
        case MT_QUEUE_GRAPHICS: queue = dev->graphics_queue; break;
        case MT_QUEUE_COMPUTE: queue = dev->compute_queue; break;
        case MT_QUEUE_TRANSFER: queue = dev->transfer_queue; break;
        default: assert(0);
    }
    assert(queue);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .commandBufferCount = 1,
        .pCommandBuffers = &info->cmd_buffer->cmd_buffer,

        .waitSemaphoreCount = info->wait_semaphore_count,
        .pWaitSemaphores = info->wait_semaphores,
        .pWaitDstStageMask = info->wait_stages,

        .signalSemaphoreCount = info->signal_semaphore_count,
        .pSignalSemaphores = info->signal_semaphores,
    };

    mt_mutex_lock(&dev->device_mutex);
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, info->fence));
    mt_mutex_unlock(&dev->device_mutex);
}

static void
transfer_to_buffer(MtDevice *dev, MtBuffer *buffer, size_t offset, size_t size, const void *data)
{
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_create_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(dev->device, &fence_create_info, NULL, &fence));

    MtBuffer *staging = create_buffer(
        dev,
        &(MtBufferCreateInfo){
            .usage = MT_BUFFER_USAGE_TRANSFER,
            .memory = MT_BUFFER_MEMORY_HOST,
            .size = size,
        });

    void *mapping = map_buffer(dev, staging);
    memcpy(mapping, data, size);

    MtCmdBuffer *cb;
    allocate_cmd_buffers(dev, MT_QUEUE_TRANSFER, 1, &cb);

    begin_cmd_buffer(cb);

    // TODO: maybe we need barriers here

    cmd_copy_buffer_to_buffer(cb, staging, 0, buffer, 0, size);

    end_cmd_buffer(cb);

    submit_cmd(dev, &(SubmitInfo){.cmd_buffer = cb, .fence = fence});

    vkWaitForFences(dev->device, 1, &fence, VK_TRUE, UINT64_MAX);

    free_cmd_buffers(dev, MT_QUEUE_TRANSFER, 1, &cb);

    unmap_buffer(dev, staging);
    destroy_buffer(dev, staging);

    vkDestroyFence(dev->device, fence, NULL);
}

static void
transfer_to_image(MtDevice *dev, const MtImageCopyView *dst, size_t size, const void *data)
{
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_create_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(dev->device, &fence_create_info, NULL, &fence));

    MtBuffer *staging = create_buffer(
        dev,
        &(MtBufferCreateInfo){
            .usage = MT_BUFFER_USAGE_TRANSFER,
            .memory = MT_BUFFER_MEMORY_HOST,
            .size = size,
        });

    void *mapping = map_buffer(dev, staging);
    memcpy(mapping, data, size);

    MtCmdBuffer *cb;
    allocate_cmd_buffers(dev, MT_QUEUE_TRANSFER, 1, &cb);

    begin_cmd_buffer(cb);

    VkImageSubresourceRange subresource_range = {
        .aspectMask = dst->image->aspect,
        .baseMipLevel = 0,
        .levelCount = dst->image->mip_count,
        .baseArrayLayer = 0,
        .layerCount = dst->image->layer_count,
    };

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = dst->image->image,
        .subresourceRange = subresource_range,
    };

    vkCmdPipelineBarrier(
        cb->cmd_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &barrier);

    cmd_copy_buffer_to_image(
        cb,
        &(MtBufferCopyView){
            .buffer = staging,
            .offset = 0,
            .row_length = 0,
            .image_height = 0,
        },
        dst,
        (MtExtent3D){
            .width = dst->image->width >> dst->mip_level,
            .height = dst->image->height >> dst->mip_level,
            .depth = dst->image->depth,
        });

    barrier = (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = dst->image->image,
        .subresourceRange = subresource_range,
    };

    vkCmdPipelineBarrier(
        cb->cmd_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &barrier);

    end_cmd_buffer(cb);

    submit_cmd(dev, &(SubmitInfo){.cmd_buffer = cb, .fence = fence});

    vkWaitForFences(dev->device, 1, &fence, VK_TRUE, UINT64_MAX);

    free_cmd_buffers(dev, MT_QUEUE_TRANSFER, 1, &cb);

    unmap_buffer(dev, staging);
    destroy_buffer(dev, staging);

    vkDestroyFence(dev->device, fence, NULL);
}

static void device_wait_idle(MtDevice *dev)
{
    mt_mutex_lock(&dev->device_mutex);
    VK_CHECK(vkDeviceWaitIdle(dev->device));
    mt_mutex_unlock(&dev->device_mutex);
}

static void destroy_device(MtDevice *dev)
{
    MtAllocator *alloc = dev->alloc;
    device_wait_idle(dev);

    buffer_pool_destroy(&dev->ubo_pool);
    buffer_pool_destroy(&dev->vbo_pool);
    buffer_pool_destroy(&dev->ibo_pool);

    mt_hash_destroy(&dev->pipeline_layout_map);

    // Destroy transfer command pools
    for (uint32_t i = 0; i < dev->num_threads; i++)
    {
        vkDestroyCommandPool(dev->device, dev->transfer_cmd_pools[i], NULL);
    }
    mt_free(dev->alloc, dev->transfer_cmd_pools);
    dev->transfer_cmd_pools = NULL;

    // Destroy compute command pools if they're not the same as the graphics
    // command pools
    if (dev->graphics_cmd_pools != dev->compute_cmd_pools)
    {
        for (uint32_t i = 0; i < dev->num_threads; i++)
        {
            vkDestroyCommandPool(dev->device, dev->compute_cmd_pools[i], NULL);
        }
        mt_free(dev->alloc, dev->compute_cmd_pools);
        dev->compute_cmd_pools = NULL;
    }

    // Destroy graphics command pools
    for (uint32_t i = 0; i < dev->num_threads; i++)
    {
        vkDestroyCommandPool(dev->device, dev->graphics_cmd_pools[i], NULL);
    }
    mt_free(dev->alloc, dev->graphics_cmd_pools);
    dev->graphics_cmd_pools = NULL;

    vmaDestroyAllocator(dev->gpu_allocator);

    vkDestroyDevice(dev->device, NULL);

#ifdef MT_ENABLE_VALIDATION
    vkDestroyDebugUtilsMessengerEXT(dev->instance, dev->debug_messenger, NULL);
#endif

    vkDestroyInstance(dev->instance, NULL);

    mt_mutex_destroy(&dev->device_mutex);

    mt_free(alloc, dev);
}

static void set_thread_id(uint32_t thread_id)
{
    renderer_thread_id = thread_id;
}

static uint32_t get_thread_id(void)
{
    return renderer_thread_id;
}
// }}}

static MtRenderer g_vulkan_renderer = {
    .destroy_device = destroy_device,
    .device_wait_idle = device_wait_idle,

    .create_swapchain = create_swapchain,
    .destroy_swapchain = destroy_swapchain,

    .swapchain_get_delta_time = swapchain_get_delta_time,

    .set_thread_id = set_thread_id,
    .get_thread_id = get_thread_id,

    .create_buffer = create_buffer,
    .destroy_buffer = destroy_buffer,

    .map_buffer = map_buffer,
    .unmap_buffer = unmap_buffer,

    .create_image = create_image,
    .destroy_image = destroy_image,

    .create_sampler = create_sampler,
    .destroy_sampler = destroy_sampler,

    .transfer_to_buffer = transfer_to_buffer,
    .transfer_to_image = transfer_to_image,

    .create_graphics_pipeline = create_graphics_pipeline,
    .create_compute_pipeline = create_compute_pipeline,
    .destroy_pipeline = destroy_pipeline,

    .cmd_get_viewport = get_viewport,

    .cmd_copy_buffer_to_buffer = cmd_copy_buffer_to_buffer,
    .cmd_copy_buffer_to_image = cmd_copy_buffer_to_image,
    .cmd_copy_image_to_buffer = cmd_copy_image_to_buffer,
    .cmd_copy_image_to_image = cmd_copy_image_to_image,

    .cmd_fill_buffer = cmd_fill_buffer,

    .cmd_set_viewport = cmd_set_viewport,
    .cmd_set_scissor = cmd_set_scissor,

    .cmd_bind_pipeline = cmd_bind_pipeline,

    .cmd_bind_uniform = cmd_bind_uniform,
    .cmd_bind_image = cmd_bind_image,
    .cmd_bind_sampler = cmd_bind_sampler,
    .cmd_bind_storage_buffer = cmd_bind_storage_buffer,

    .cmd_bind_vertex_buffer = cmd_bind_vertex_buffer,
    .cmd_bind_index_buffer = cmd_bind_index_buffer,

    .cmd_bind_vertex_data = cmd_bind_vertex_data,
    .cmd_bind_index_data = cmd_bind_index_data,

    .cmd_draw = cmd_draw,
    .cmd_draw_indexed = cmd_draw_indexed,

    .cmd_dispatch = cmd_dispatch,

    .create_graph = create_graph,
    .destroy_graph = destroy_graph,

    .graph_execute = graph_execute,
    .graph_wait_all = graph_wait_all,
    .graph_on_resize = graph_on_resize,

    .graph_add_image = graph_add_image,
    .graph_add_buffer = graph_add_buffer,
    .graph_add_external_buffer = graph_add_external_buffer,

    .graph_get_image = graph_get_image,
    .graph_consume_image = graph_consume_image,
    .graph_get_buffer = graph_get_buffer,

    .graph_add_pass = graph_add_pass,
    .pass_set_color_clearer = pass_set_color_clearer,
    .pass_set_depth_stencil_clearer = pass_set_depth_stencil_clearer,

    .pass_read = pass_read,
    .pass_write = pass_write,

    .pass_begin = pass_begin,
    .pass_end = pass_end,
};

MtDevice *mt_vulkan_device_init(MtVulkanDeviceCreateInfo *create_info, MtAllocator *alloc)
{
    mt_render = g_vulkan_renderer;

    MtDevice *dev = mt_alloc(alloc, sizeof(MtDevice));
    memset(dev, 0, sizeof(*dev));

    dev->flags = create_info->flags;
    dev->alloc = alloc;

    dev->num_threads = create_info->num_threads + 1; // NOTE: Number of threads + main thread
    if (dev->num_threads == 0)
    {
        dev->num_threads = 1;
    }

    mt_mutex_init(&dev->device_mutex);

    create_instance(dev);

#ifdef MT_ENABLE_VALIDATION
    create_debug_messenger(dev);
#endif

    pick_physical_device(dev);
    create_device(dev);
    create_allocator(dev);

    find_supported_depth_format(dev);

    create_command_pools(dev);

    mt_hash_init(&dev->pipeline_layout_map, 51, dev->alloc);

    buffer_pool_init(
        dev,
        &dev->ubo_pool,
        65536, /*block size*/
        MT_MAX(
            16u,
            dev->physical_device_properties.limits.minUniformBufferOffsetAlignment), /*alignment*/
        MT_BUFFER_USAGE_UNIFORM);

    buffer_pool_init(
        dev,
        &dev->vbo_pool,
        65536, /*block size*/
        16,    /*alignment*/
        MT_BUFFER_USAGE_VERTEX);

    buffer_pool_init(
        dev,
        &dev->ibo_pool,
        65536, /*block size*/
        16,    /*alignment*/
        MT_BUFFER_USAGE_INDEX);

    return dev;
}
