#include "shady/runtime.h"

#include "vulkan/vulkan.h"

#include "../log.h"
#include "../portability.h"

#include <stdlib.h>
#include <assert.h>

static void expect_success(VkResult result) {
    assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
}

struct Runtime_ {
    VkInstance instance;
    VkDevice device;
};

static const char* necessary_device_extensions[] = { "VK_EXT_descriptor_indexing" };

static VkPhysicalDevice pick_device(SHADY_UNUSED Runtime* runtime, uint32_t devices_count, VkPhysicalDevice available_devices[]) {
    for (uint32_t i = 0; i < devices_count; i++) {
        VkPhysicalDevice physical_device = available_devices[i];
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);

        if (device_properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 1, 0))
            continue;

        return available_devices[i];
    }

    return NULL;
}

static VkDevice initialize_device(SHADY_UNUSED Runtime* runtime, VkPhysicalDevice physical_device) {
    uint32_t queue_families_count;
    vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &queue_families_count, NULL);
    LARRAY(VkQueueFamilyProperties2, queue_families_properties, queue_families_count);
    for (size_t i = 0; i < queue_families_count; i++) {
        queue_families_properties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        queue_families_properties[i].pNext = NULL;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &queue_families_count, queue_families_properties);

    uint32_t compute_queue_family = -1;
    for (uint32_t i = 0; i < queue_families_count; i++) {
        VkQueueFamilyProperties2 queue_family_properties = queue_families_properties[i];
        if (queue_family_properties.queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            compute_queue_family = i;
            break;
        }
    }

    if (compute_queue_family == -1)
        return NULL;

    VkDevice device;
    expect_success(vkCreateDevice(physical_device, &(VkDeviceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = (const VkDeviceQueueCreateInfo []) {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .flags = 0,
                .pQueuePriorities = NULL,
                .queueCount = 1,
                .queueFamilyIndex = compute_queue_family,
                .pNext = NULL,
            }
        },
        .enabledLayerCount = 0,
        .enabledExtensionCount = sizeof(necessary_device_extensions) / sizeof(const char*),
        .ppEnabledExtensionNames = necessary_device_extensions,
        .pNext = NULL,
    }, NULL, &device));
    return device;
}

Runtime* initialize_runtime() {
    Runtime* runtime = malloc(sizeof(Runtime));
    if (vkCreateInstance(&(VkInstanceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &(VkApplicationInfo) {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pEngineName = "shady",
            .pApplicationName = "business",
            .pNext = NULL,
            .engineVersion = 1,
            .applicationVersion = 1,
            .apiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0)
        },
        .flags = 0,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = (const char* []) {},
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .pNext = NULL
    }, NULL, &runtime->instance) != VK_SUCCESS) {
        error("Failed to initialise Vulkan instance");
        goto init_fail_free;
    }

#define CHECK(x) if (x != VK_SUCCESS) { error(#x " failed\n"); goto init_fail; }

    uint32_t devices_count;
    CHECK(vkEnumeratePhysicalDevices(runtime->instance, &devices_count, NULL))
    LARRAY(VkPhysicalDevice, devices, devices_count);
    CHECK(vkEnumeratePhysicalDevices(runtime->instance, &devices_count, devices))

    assert(devices_count > 0 && "No Vulkan devices found !");

    VkPhysicalDevice physical_device = pick_device(runtime, devices_count, devices);
    runtime->device = initialize_device(runtime, physical_device);

    info_print("Shady runtime successfully initialized !\n");
    return runtime;
#undef CHECK

    init_fail:
    vkDestroyInstance(runtime->instance, NULL);

    init_fail_free:
    free(runtime);
    return NULL;
}

void shutdown_runtime(Runtime* runtime) {
    assert(runtime);

    vkDestroyDevice(runtime->device, NULL);
    vkDestroyInstance(runtime->instance, NULL);

    free(runtime);
}

Program* load_program(Runtime*, const char* program_src);
void launch_kernel(Program*, int dimx, int dimy, int dimz, int extra_args_count, void** extra_args);
