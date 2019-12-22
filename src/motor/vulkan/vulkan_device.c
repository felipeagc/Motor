#include "../../../include/motor/vulkan/vulkan_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../include/motor/util.h"
#include "../../../include/motor/arena.h"
#include "../../../include/motor/window.h"
#include "conversions.h"
#include "internal.h"
#include "vk_mem_alloc.h"

typedef struct _MtVulkanDevice {
  MtArena *arena;

  MtVulkanDeviceFlags flags;
  MtVulkanWindowSystem *window_system;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;

  VkPhysicalDevice physical_device;
  VkDevice device;

  MtQueueFamilyIndices indices;

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
} _MtVulkanDevice;

static void _mt_vulkan_device_destroy(_MtVulkanDevice *dev);

static MtDeviceVT _MT_VULKAN_DEVICE_VT = (MtDeviceVT){
    .destroy = (void *)_mt_vulkan_device_destroy,
};

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
static const char *VALIDATION_LAYERS[0]   = {};
static const char *INSTANCE_EXTENSIONS[0] = {};
#endif

static const char *DEVICE_EXTENSIONS[1] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

VkBool32 debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
    void *p_user_data) {
  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    printf("Validation layer: %s\n", p_callback_data->pMessage);
  }
  return VK_FALSE;
}

static bool
are_indices_complete(_MtVulkanDevice *dev, MtQueueFamilyIndices *indices) {
  return indices->graphics != UINT32_MAX &&
         ((dev->flags & MT_DEVICE_HEADLESS) ||
          indices->present != UINT32_MAX) &&
         indices->transfer != UINT32_MAX && indices->compute != UINT32_MAX;
}

static bool check_validation_layer_support(_MtVulkanDevice *device) {
  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, NULL);

  VkLayerProperties *available_layers =
      mt_alloc(device->arena, sizeof(VkLayerProperties) * layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

  for (uint32_t l = 0; l < MT_LENGTH(VALIDATION_LAYERS); l++) {
    const char *layer_name = VALIDATION_LAYERS[l];

    bool layer_found = false;
    for (uint32_t a = 0; a < layer_count; a++) {
      VkLayerProperties *layer_properties = &available_layers[a];
      if (strcmp(layer_name, layer_properties->layerName) == 0) {
        layer_found = true;
        break;
      }
    }

    if (!layer_found) {
      mt_free(device->arena, available_layers);
      return false;
    }
  }

  mt_free(device->arena, available_layers);
  return true;
}

static void create_instance(_MtVulkanDevice *dev) {
#ifdef MT_ENABLE_VALIDATION
  if (!check_validation_layer_support(dev)) {
    printf("Application wants to enable validation layers but does not support "
           "them\n");
    exit(1);
  }
#endif

  VkApplicationInfo app_info = {
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = "Motor",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .pEngineName        = "Motor",
      .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
      .apiVersion         = VK_API_VERSION_1_1,
  };

  VkInstanceCreateInfo create_info = {
      .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
  };

#ifdef MT_ENABLE_VALIDATION
  create_info.enabledLayerCount   = MT_LENGTH(VALIDATION_LAYERS);
  create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

  const char **extensions  = NULL;
  uint32_t extension_count = 0;
  if (MT_LENGTH(INSTANCE_EXTENSIONS) > 0) {
    extension_count = MT_LENGTH(INSTANCE_EXTENSIONS);
    extensions      = mt_alloc(dev->arena, sizeof(char *) * extension_count);
    memcpy(
        extensions,
        INSTANCE_EXTENSIONS,
        sizeof(char *) * MT_LENGTH(INSTANCE_EXTENSIONS));
  }

  if (!(dev->flags & MT_DEVICE_HEADLESS)) {
    assert(dev->window_system);
    uint32_t window_extension_count;
    const char **window_extensions =
        dev->window_system->get_vulkan_instance_extensions(
            &window_extension_count);

    extension_count += window_extension_count;
    extensions =
        mt_realloc(dev->arena, extensions, sizeof(char *) * extension_count);

    memcpy(
        &extensions[extension_count - window_extension_count],
        window_extensions,
        window_extension_count * sizeof(char *));
  }

  create_info.enabledExtensionCount   = extension_count;
  create_info.ppEnabledExtensionNames = extensions;

  VK_CHECK(vkCreateInstance(&create_info, NULL, &dev->instance));

  volkLoadInstance(dev->instance);

  mt_free(dev->arena, extensions);
}

static void create_debug_messenger(_MtVulkanDevice *dev) {
  VkDebugUtilsMessengerCreateInfoEXT create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = &debug_callback;

  VK_CHECK(vkCreateDebugUtilsMessengerEXT(
      dev->instance, &create_info, NULL, &dev->debug_messenger));
}

MtQueueFamilyIndices
find_queue_families(_MtVulkanDevice *dev, VkPhysicalDevice physical_device) {
  MtQueueFamilyIndices indices;
  indices.graphics = UINT32_MAX;
  indices.present  = UINT32_MAX;
  indices.transfer = UINT32_MAX;
  indices.compute  = UINT32_MAX;

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(
      physical_device, &queue_family_count, NULL);

  VkQueueFamilyProperties *queue_families = mt_calloc(
      dev->arena, sizeof(VkQueueFamilyProperties) * queue_family_count);

  vkGetPhysicalDeviceQueueFamilyProperties(
      physical_device, &queue_family_count, queue_families);

  for (uint32_t i = 0; i < queue_family_count; i++) {
    VkQueueFamilyProperties *queue_family = &queue_families[i];
    if (queue_family->queueCount > 0 &&
        queue_family->queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphics = i;
    }

    if (queue_family->queueCount > 0 &&
        queue_family->queueFlags & VK_QUEUE_TRANSFER_BIT) {
      indices.transfer = i;
    }

    if (queue_family->queueCount > 0 &&
        queue_family->queueFlags & VK_QUEUE_COMPUTE_BIT) {
      indices.compute = i;
    }

    if (!dev->flags & MT_DEVICE_HEADLESS) {
      if (queue_family->queueCount > 0 &&
          dev->window_system->get_physical_device_presentation_support(
              dev->instance, physical_device, i)) {
        indices.present = i;
      }
    }

    if (are_indices_complete(dev, &indices)) break;
  }

  mt_free(dev->arena, queue_families);
  return indices;
}

bool check_device_extension_support(
    _MtVulkanDevice *dev, VkPhysicalDevice physical_device) {
  uint32_t extension_count;
  vkEnumerateDeviceExtensionProperties(
      physical_device, NULL, &extension_count, NULL);

  VkExtensionProperties *available_extensions =
      mt_alloc(dev->arena, sizeof(VkExtensionProperties) * extension_count);

  vkEnumerateDeviceExtensionProperties(
      physical_device, NULL, &extension_count, available_extensions);

  bool found_all = true;

  for (uint32_t i = 0; i < MT_LENGTH(DEVICE_EXTENSIONS); i++) {
    const char *required_ext = DEVICE_EXTENSIONS[i];
    bool found               = false;
    for (uint32_t j = 0; j < extension_count; j++) {
      VkExtensionProperties *available_ext = &available_extensions[j];
      if (strcmp(available_ext->extensionName, required_ext) == 0) {
        found = true;
        break;
      }
    }

    if (!found) found_all = false;
  }

  mt_free(dev->arena, available_extensions);
  return found_all;
}

bool is_device_suitable(
    _MtVulkanDevice *dev, VkPhysicalDevice physical_device) {
  MtQueueFamilyIndices indices = find_queue_families(dev, physical_device);

  bool extensions_supported =
      check_device_extension_support(dev, physical_device);

  return are_indices_complete(dev, &indices) && extensions_supported;
}

static void pick_physical_device(_MtVulkanDevice *dev) {
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(dev->instance, &device_count, NULL);

  if (device_count == 0) {
    printf("No vulkan capable devices found\n");
    exit(1);
  }

  VkPhysicalDevice *devices =
      mt_alloc(dev->arena, sizeof(VkPhysicalDevice) * device_count);
  vkEnumeratePhysicalDevices(dev->instance, &device_count, devices);

  for (uint32_t i = 0; i < device_count; i++) {
    if (is_device_suitable(dev, devices[i])) {
      dev->physical_device = devices[i];
      break;
    }
  }

  if (dev->physical_device == VK_NULL_HANDLE) {
    printf("Could not find a physical device that suits the application "
           "requirements\n");
    exit(1);
  }

  vkGetPhysicalDeviceProperties(
      dev->physical_device, &dev->physical_device_properties);

  mt_free(dev->arena, devices);
}

static void create_device(_MtVulkanDevice *dev) {
  dev->indices = find_queue_families(dev, dev->physical_device);

  float queue_priority                          = 1.0f;
  VkDeviceQueueCreateInfo queue_create_infos[4] = {0};
  uint32_t queue_create_info_count              = 1;

  queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_infos[0].queueFamilyIndex = dev->indices.graphics;
  queue_create_infos[0].queueCount       = 1;
  queue_create_infos[0].pQueuePriorities = &queue_priority;

  if (dev->indices.graphics != dev->indices.transfer) {
    queue_create_info_count++;
    queue_create_infos[queue_create_info_count - 1].sType =
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[queue_create_info_count - 1].queueFamilyIndex =
        dev->indices.transfer;
    queue_create_infos[queue_create_info_count - 1].queueCount = 1;
    queue_create_infos[queue_create_info_count - 1].pQueuePriorities =
        &queue_priority;
  }

  if (!dev->flags & MT_DEVICE_HEADLESS) {
    if (dev->indices.graphics != dev->indices.present) {
      queue_create_info_count++;
      queue_create_infos[queue_create_info_count - 1].sType =
          VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queue_create_infos[queue_create_info_count - 1].queueFamilyIndex =
          dev->indices.present;
      queue_create_infos[queue_create_info_count - 1].queueCount = 1;
      queue_create_infos[queue_create_info_count - 1].pQueuePriorities =
          &queue_priority;
    }
  }

  if (dev->indices.graphics != dev->indices.compute) {
    queue_create_info_count++;
    queue_create_infos[queue_create_info_count - 1].sType =
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[queue_create_info_count - 1].queueFamilyIndex =
        dev->indices.compute;
    queue_create_infos[queue_create_info_count - 1].queueCount = 1;
    queue_create_infos[queue_create_info_count - 1].pQueuePriorities =
        &queue_priority;
  }

  VkPhysicalDeviceFeatures device_features = {
      .samplerAnisotropy = VK_TRUE,
  };

  VkDeviceCreateInfo create_info = {
      .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pQueueCreateInfos    = queue_create_infos,
      .queueCreateInfoCount = queue_create_info_count,
      .pEnabledFeatures     = &device_features,
  };

#ifdef MT_ENABLE_VALIDATION
  create_info.enabledLayerCount   = MT_LENGTH(VALIDATION_LAYERS);
  create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif

  if (!dev->flags & MT_DEVICE_HEADLESS) {
    create_info.enabledExtensionCount   = MT_LENGTH(DEVICE_EXTENSIONS);
    create_info.ppEnabledExtensionNames = DEVICE_EXTENSIONS;
  }

  VK_CHECK(
      vkCreateDevice(dev->physical_device, &create_info, NULL, &dev->device));

  vkGetDeviceQueue(dev->device, dev->indices.graphics, 0, &dev->graphics_queue);
  vkGetDeviceQueue(dev->device, dev->indices.transfer, 0, &dev->transfer_queue);

  if (!dev->flags & MT_DEVICE_HEADLESS) {
    vkGetDeviceQueue(dev->device, dev->indices.present, 0, &dev->present_queue);
  }

  vkGetDeviceQueue(dev->device, dev->indices.compute, 0, &dev->compute_queue);
}

static void create_allocator(_MtVulkanDevice *dev) {
  VmaAllocatorCreateInfo allocator_info = {0};
  allocator_info.physicalDevice         = dev->physical_device;
  allocator_info.device                 = dev->device;

  VmaVulkanFunctions vk_functions = {
      .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
      .vkGetPhysicalDeviceMemoryProperties =
          vkGetPhysicalDeviceMemoryProperties,
      .vkAllocateMemory               = vkAllocateMemory,
      .vkFreeMemory                   = vkFreeMemory,
      .vkMapMemory                    = vkMapMemory,
      .vkUnmapMemory                  = vkUnmapMemory,
      .vkFlushMappedMemoryRanges      = vkFlushMappedMemoryRanges,
      .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
      .vkBindBufferMemory             = vkBindBufferMemory,
      .vkBindImageMemory              = vkBindImageMemory,
      .vkGetBufferMemoryRequirements  = vkGetBufferMemoryRequirements,
      .vkGetImageMemoryRequirements   = vkGetImageMemoryRequirements,
      .vkCreateBuffer                 = vkCreateBuffer,
      .vkDestroyBuffer                = vkDestroyBuffer,
      .vkCreateImage                  = vkCreateImage,
      .vkDestroyImage                 = vkDestroyImage,
      .vkCmdCopyBuffer                = vkCmdCopyBuffer,
  };

  allocator_info.pVulkanFunctions = &vk_functions;

  VK_CHECK(vmaCreateAllocator(&allocator_info, &dev->gpu_allocator));
}

static VkFormat find_supported_format(
    _MtVulkanDevice *dev,
    VkFormat *candidates,
    uint32_t candidate_count,
    VkImageTiling tiling,
    VkFormatFeatureFlags features) {
  for (uint32_t i = 0; i < candidate_count; i++) {
    VkFormat format = candidates[i];

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(dev->physical_device, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (
        tiling == VK_IMAGE_TILING_OPTIMAL &&
        (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  return VK_FORMAT_UNDEFINED;
}

static void find_supported_depth_format(_MtVulkanDevice *dev) {
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

static void create_command_pools(_MtVulkanDevice *dev) {
  VkCommandPoolCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  dev->graphics_cmd_pools =
      mt_alloc(dev->arena, sizeof(VkCommandPool) * dev->num_threads);

  for (uint32_t i = 0; i < dev->num_threads; i++) {
    create_info.queueFamilyIndex = dev->indices.graphics;
    VK_CHECK(vkCreateCommandPool(
        dev->device, &create_info, NULL, &dev->graphics_cmd_pools[i]));
  }

  dev->compute_cmd_pools = dev->graphics_cmd_pools;

  if (dev->indices.graphics != dev->indices.compute) {
    dev->compute_cmd_pools =
        mt_alloc(dev->arena, sizeof(VkCommandPool) * dev->num_threads);

    for (uint32_t i = 0; i < dev->num_threads; i++) {
      create_info.queueFamilyIndex = dev->indices.compute;
      VK_CHECK(vkCreateCommandPool(
          dev->device, &create_info, NULL, &dev->compute_cmd_pools[i]));
    }
  }
}

void mt_vulkan_device_init(
    MtIDevice *interface,
    MtVulkanDeviceDescriptor *descriptor,
    MtArena *arena) {
  _MtVulkanDevice *dev = mt_calloc(arena, sizeof(_MtVulkanDevice));
  dev->flags           = descriptor->flags;
  dev->arena           = arena;

  dev->window_system = (MtVulkanWindowSystem *)descriptor->window_system->inst;

  dev->num_threads = descriptor->num_threads;
  if (dev->num_threads == 0) {
    dev->num_threads = 1;
  }

  create_instance(dev);

#ifdef MT_ENABLE_VALIDATION
  create_debug_messenger(dev);
#endif

  pick_physical_device(dev);
  create_device(dev);
  create_allocator(dev);

  find_supported_depth_format(dev);

  create_command_pools(dev);

  interface->inst = (MtDevice *)dev;
  interface->vt   = &_MT_VULKAN_DEVICE_VT;
}

static void _mt_vulkan_device_destroy(_MtVulkanDevice *dev) {
  MtArena *arena = dev->arena;
  vkDeviceWaitIdle(dev->device);

  if (dev->graphics_cmd_pools != dev->compute_cmd_pools) {
    for (uint32_t i = 0; i < dev->num_threads; i++) {
      vkDestroyCommandPool(dev->device, dev->compute_cmd_pools[i], NULL);
    }
    mt_free(dev->arena, dev->compute_cmd_pools);
    dev->compute_cmd_pools = NULL;
  }

  for (uint32_t i = 0; i < dev->num_threads; i++) {
    vkDestroyCommandPool(dev->device, dev->graphics_cmd_pools[i], NULL);
  }
  mt_free(dev->arena, dev->graphics_cmd_pools);
  dev->graphics_cmd_pools = NULL;

  vmaDestroyAllocator(dev->gpu_allocator);

  vkDestroyDevice(dev->device, NULL);

#ifdef MT_ENABLE_VALIDATION
  vkDestroyDebugUtilsMessengerEXT(dev->instance, dev->debug_messenger, NULL);
#endif

  vkDestroyInstance(dev->instance, NULL);

  mt_free(arena, dev);
}
