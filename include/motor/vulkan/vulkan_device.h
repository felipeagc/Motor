#ifndef MT_VULKAN_DEVICE_H
#define MT_VULKAN_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct MtArena MtArena;
typedef struct MtIDevice MtIDevice;
typedef struct MtIWindowSystem MtIWindowSystem;

typedef uint32_t MtVulkanDeviceFlags;
typedef enum MtVulkanDeviceFlagBits {
  MT_DEVICE_HEADLESS = 1,
} MtVulkanDeviceFlagBits;

typedef struct MtVulkanDeviceDescriptor {
  MtVulkanDeviceFlags flags;
  uint32_t num_threads;
  MtIWindowSystem *window_system;
} MtVulkanDeviceDescriptor;

void mt_vulkan_device_init(
    MtIDevice *device, MtVulkanDeviceDescriptor *descriptor, MtArena *arena);

#endif