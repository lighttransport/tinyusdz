// SPDX-License-Identifier: MIT
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// vkew - Vulkan Extension Wrangler Implementation
//

#include "lightrt_vkew.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>

//------------------------------------------------------------------------------
// Platform-specific dynamic library loading
//------------------------------------------------------------------------------

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HMODULE g_vkLibrary = NULL;

static void* vkewLoadLibrary(void) {
    g_vkLibrary = LoadLibraryA("vulkan-1.dll");
    return g_vkLibrary;
}

static void vkewUnloadLibrary(void) {
    if (g_vkLibrary) {
        FreeLibrary(g_vkLibrary);
        g_vkLibrary = NULL;
    }
}

static void* vkewGetProcAddress(const char* name) {
    if (!g_vkLibrary) return NULL;
    return (void*)GetProcAddress(g_vkLibrary, name);
}

#elif defined(__APPLE__)
#include <dlfcn.h>

static void* g_vkLibrary = NULL;

static void* vkewLoadLibrary(void) {
    // Try MoltenVK paths first, then standard Vulkan SDK
    const char* libPaths[] = {
        "libvulkan.1.dylib",
        "libvulkan.dylib",
        "/usr/local/lib/libvulkan.dylib",
        "/usr/local/lib/libvulkan.1.dylib",
        "libMoltenVK.dylib",
        NULL
    };

    for (int i = 0; libPaths[i]; i++) {
        g_vkLibrary = dlopen(libPaths[i], RTLD_NOW | RTLD_LOCAL);
        if (g_vkLibrary) break;
    }
    return g_vkLibrary;
}

static void vkewUnloadLibrary(void) {
    if (g_vkLibrary) {
        dlclose(g_vkLibrary);
        g_vkLibrary = NULL;
    }
}

static void* vkewGetProcAddress(const char* name) {
    if (!g_vkLibrary) return NULL;
    return dlsym(g_vkLibrary, name);
}

#else // Linux / Unix
#include <dlfcn.h>

static void* g_vkLibrary = NULL;

static void* vkewLoadLibrary(void) {
    // Try standard paths
    const char* libPaths[] = {
        "libvulkan.so.1",
        "libvulkan.so",
        "/usr/lib/x86_64-linux-gnu/libvulkan.so.1",
        "/usr/lib/libvulkan.so.1",
        "/usr/local/lib/libvulkan.so.1",
        NULL
    };

    for (int i = 0; libPaths[i]; i++) {
        g_vkLibrary = dlopen(libPaths[i], RTLD_NOW | RTLD_LOCAL);
        if (g_vkLibrary) break;
    }
    return g_vkLibrary;
}

static void vkewUnloadLibrary(void) {
    if (g_vkLibrary) {
        dlclose(g_vkLibrary);
        g_vkLibrary = NULL;
    }
}

static void* vkewGetProcAddress(const char* name) {
    if (!g_vkLibrary) return NULL;
    return dlsym(g_vkLibrary, name);
}

#endif

//------------------------------------------------------------------------------
// Global state
//------------------------------------------------------------------------------

static bool g_initialized = false;
static char g_errorMessage[512] = "";

static void setError(const char* msg) {
    strncpy(g_errorMessage, msg, sizeof(g_errorMessage) - 1);
    g_errorMessage[sizeof(g_errorMessage) - 1] = '\0';
}

//------------------------------------------------------------------------------
// Function pointer definitions
//------------------------------------------------------------------------------

// Global/Loader functions
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;
PFN_vkCreateInstance vkCreateInstance = NULL;

// Instance functions
PFN_vkDestroyInstance vkDestroyInstance = NULL;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = NULL;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = NULL;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = NULL;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = NULL;
PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = NULL;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = NULL;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = NULL;
PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR = NULL;
PFN_vkCreateDevice vkCreateDevice = NULL;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = NULL;

// Device functions
PFN_vkDestroyDevice vkDestroyDevice = NULL;
PFN_vkGetDeviceQueue vkGetDeviceQueue = NULL;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle = NULL;
PFN_vkQueueWaitIdle vkQueueWaitIdle = NULL;
PFN_vkCreateFence vkCreateFence = NULL;
PFN_vkDestroyFence vkDestroyFence = NULL;
PFN_vkWaitForFences vkWaitForFences = NULL;
PFN_vkResetFences vkResetFences = NULL;
PFN_vkCmdFillBuffer vkCmdFillBuffer = NULL;

// Memory
PFN_vkAllocateMemory vkAllocateMemory = NULL;
PFN_vkFreeMemory vkFreeMemory = NULL;
PFN_vkMapMemory vkMapMemory = NULL;
PFN_vkUnmapMemory vkUnmapMemory = NULL;

// Buffer
PFN_vkCreateBuffer vkCreateBuffer = NULL;
PFN_vkDestroyBuffer vkDestroyBuffer = NULL;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = NULL;
PFN_vkBindBufferMemory vkBindBufferMemory = NULL;

// Shader
PFN_vkCreateShaderModule vkCreateShaderModule = NULL;
PFN_vkDestroyShaderModule vkDestroyShaderModule = NULL;

// Pipeline
PFN_vkCreateComputePipelines vkCreateComputePipelines = NULL;
PFN_vkDestroyPipeline vkDestroyPipeline = NULL;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout = NULL;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = NULL;

// Descriptor
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = NULL;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = NULL;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool = NULL;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = NULL;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = NULL;
PFN_vkResetDescriptorPool vkResetDescriptorPool = NULL;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = NULL;

// Command pool/buffer
PFN_vkCreateCommandPool vkCreateCommandPool = NULL;
PFN_vkDestroyCommandPool vkDestroyCommandPool = NULL;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = NULL;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers = NULL;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer = NULL;
PFN_vkEndCommandBuffer vkEndCommandBuffer = NULL;

// Queue
PFN_vkQueueSubmit vkQueueSubmit = NULL;

// Commands
PFN_vkCmdBindPipeline vkCmdBindPipeline = NULL;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = NULL;
PFN_vkCmdPushConstants vkCmdPushConstants = NULL;
PFN_vkCmdDispatch vkCmdDispatch = NULL;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer = NULL;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = NULL;

// Ray-tracing / buffer-device-address (KHR). NULL unless the matching device
// extension was enabled at vkCreateDevice time.
PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = NULL;
PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = NULL;
PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = NULL;
PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = NULL;
PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = NULL;
PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = NULL;

//------------------------------------------------------------------------------
// Helper macro for loading functions
//------------------------------------------------------------------------------

#define VKEW_LOAD_GLOBAL(func) \
    func = (PFN_##func)vkewGetProcAddress(#func); \
    if (!func) { \
        setError("Failed to load " #func); \
        return false; \
    }

#define VKEW_LOAD_INSTANCE(func) \
    func = (PFN_##func)vkGetInstanceProcAddr(instance, #func); \
    if (!func) { \
        setError("Failed to load instance function " #func); \
        return false; \
    }

#define VKEW_LOAD_DEVICE(func) \
    func = (PFN_##func)vkGetDeviceProcAddr(device, #func); \
    if (!func) { \
        setError("Failed to load device function " #func); \
        return false; \
    }

// Optional device function: assign whatever vkGetDeviceProcAddr returns (NULL
// when the extension was not enabled), never failing the load.
#define VKEW_LOAD_DEVICE_OPT(func) \
    func = (PFN_##func)vkGetDeviceProcAddr(device, #func);

//------------------------------------------------------------------------------
// VKEW API Implementation
//------------------------------------------------------------------------------

bool vkewInit(void) {
    if (g_initialized) {
        return true;
    }

    g_errorMessage[0] = '\0';

    // Load Vulkan library
    if (!vkewLoadLibrary()) {
        setError("Failed to load Vulkan library. Is Vulkan installed?");
        return false;
    }

    // Get vkGetInstanceProcAddr from the library
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)vkewGetProcAddress("vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) {
        setError("Failed to get vkGetInstanceProcAddr");
        vkewUnloadLibrary();
        return false;
    }

    // Load global functions (can be loaded without an instance)
    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!vkCreateInstance) {
        setError("Failed to load vkCreateInstance");
        vkewUnloadLibrary();
        return false;
    }

    g_initialized = true;
    return true;
}

void vkewShutdown(void) {
    if (!g_initialized) {
        return;
    }

    // Reset all function pointers
    vkGetInstanceProcAddr = NULL;
    vkCreateInstance = NULL;
    vkDestroyInstance = NULL;
    vkEnumeratePhysicalDevices = NULL;
    vkEnumerateDeviceExtensionProperties = NULL;
    vkGetPhysicalDeviceProperties = NULL;
    vkGetPhysicalDeviceFeatures = NULL;
    vkGetPhysicalDeviceFeatures2 = NULL;
    vkGetPhysicalDeviceMemoryProperties = NULL;
    vkGetPhysicalDeviceQueueFamilyProperties = NULL;
    vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR = NULL;
    vkCreateDevice = NULL;
    vkGetDeviceProcAddr = NULL;
    vkDestroyDevice = NULL;
    vkGetDeviceQueue = NULL;
    vkDeviceWaitIdle = NULL;
    vkQueueWaitIdle = NULL;
    vkCreateFence = NULL;
    vkDestroyFence = NULL;
    vkWaitForFences = NULL;
    vkResetFences = NULL;
    vkCmdFillBuffer = NULL;
    vkAllocateMemory = NULL;
    vkFreeMemory = NULL;
    vkMapMemory = NULL;
    vkUnmapMemory = NULL;
    vkCreateBuffer = NULL;
    vkDestroyBuffer = NULL;
    vkGetBufferMemoryRequirements = NULL;
    vkBindBufferMemory = NULL;
    vkCreateShaderModule = NULL;
    vkDestroyShaderModule = NULL;
    vkCreateComputePipelines = NULL;
    vkDestroyPipeline = NULL;
    vkCreatePipelineLayout = NULL;
    vkDestroyPipelineLayout = NULL;
    vkCreateDescriptorSetLayout = NULL;
    vkDestroyDescriptorSetLayout = NULL;
    vkCreateDescriptorPool = NULL;
    vkDestroyDescriptorPool = NULL;
    vkAllocateDescriptorSets = NULL;
    vkResetDescriptorPool = NULL;
    vkUpdateDescriptorSets = NULL;
    vkCreateCommandPool = NULL;
    vkDestroyCommandPool = NULL;
    vkAllocateCommandBuffers = NULL;
    vkFreeCommandBuffers = NULL;
    vkBeginCommandBuffer = NULL;
    vkEndCommandBuffer = NULL;
    vkQueueSubmit = NULL;
    vkCmdBindPipeline = NULL;
    vkCmdBindDescriptorSets = NULL;
    vkCmdPushConstants = NULL;
    vkCmdDispatch = NULL;
    vkCmdCopyBuffer = NULL;
    vkCmdPipelineBarrier = NULL;
    vkGetBufferDeviceAddressKHR = NULL;
    vkGetAccelerationStructureBuildSizesKHR = NULL;
    vkCreateAccelerationStructureKHR = NULL;
    vkDestroyAccelerationStructureKHR = NULL;
    vkCmdBuildAccelerationStructuresKHR = NULL;
    vkGetAccelerationStructureDeviceAddressKHR = NULL;

    vkewUnloadLibrary();
    g_initialized = false;
}

bool vkewIsInitialized(void) {
    return g_initialized;
}

bool vkewLoadInstance(VkInstance instance) {
    if (!g_initialized) {
        setError("vkew not initialized. Call vkewInit() first.");
        return false;
    }

    if (!instance) {
        setError("Invalid VkInstance (null)");
        return false;
    }

    // Load instance-level functions
    VKEW_LOAD_INSTANCE(vkDestroyInstance);
    VKEW_LOAD_INSTANCE(vkEnumeratePhysicalDevices);
    VKEW_LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties);
    VKEW_LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
    VKEW_LOAD_INSTANCE(vkGetPhysicalDeviceFeatures);
    vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2");
    if (!vkGetPhysicalDeviceFeatures2) {
        vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR");
    }
    VKEW_LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    VKEW_LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR =
        (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)vkGetInstanceProcAddr(
            instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
    VKEW_LOAD_INSTANCE(vkCreateDevice);
    VKEW_LOAD_INSTANCE(vkGetDeviceProcAddr);

    return true;
}

bool vkewLoadDevice(VkDevice device) {
    if (!g_initialized) {
        setError("vkew not initialized. Call vkewInit() first.");
        return false;
    }

    if (!device) {
        setError("Invalid VkDevice (null)");
        return false;
    }

    // Load device-level functions
    VKEW_LOAD_DEVICE(vkDestroyDevice);
    VKEW_LOAD_DEVICE(vkGetDeviceQueue);
    VKEW_LOAD_DEVICE(vkDeviceWaitIdle);
    VKEW_LOAD_DEVICE(vkQueueWaitIdle);
    VKEW_LOAD_DEVICE(vkCreateFence);
    VKEW_LOAD_DEVICE(vkDestroyFence);
    VKEW_LOAD_DEVICE(vkWaitForFences);
    VKEW_LOAD_DEVICE(vkResetFences);
    VKEW_LOAD_DEVICE(vkCmdFillBuffer);

    // Memory
    VKEW_LOAD_DEVICE(vkAllocateMemory);
    VKEW_LOAD_DEVICE(vkFreeMemory);
    VKEW_LOAD_DEVICE(vkMapMemory);
    VKEW_LOAD_DEVICE(vkUnmapMemory);

    // Buffer
    VKEW_LOAD_DEVICE(vkCreateBuffer);
    VKEW_LOAD_DEVICE(vkDestroyBuffer);
    VKEW_LOAD_DEVICE(vkGetBufferMemoryRequirements);
    VKEW_LOAD_DEVICE(vkBindBufferMemory);

    // Shader
    VKEW_LOAD_DEVICE(vkCreateShaderModule);
    VKEW_LOAD_DEVICE(vkDestroyShaderModule);

    // Pipeline
    VKEW_LOAD_DEVICE(vkCreateComputePipelines);
    VKEW_LOAD_DEVICE(vkDestroyPipeline);
    VKEW_LOAD_DEVICE(vkCreatePipelineLayout);
    VKEW_LOAD_DEVICE(vkDestroyPipelineLayout);

    // Descriptor
    VKEW_LOAD_DEVICE(vkCreateDescriptorSetLayout);
    VKEW_LOAD_DEVICE(vkDestroyDescriptorSetLayout);
    VKEW_LOAD_DEVICE(vkCreateDescriptorPool);
    VKEW_LOAD_DEVICE(vkDestroyDescriptorPool);
    VKEW_LOAD_DEVICE(vkAllocateDescriptorSets);
    VKEW_LOAD_DEVICE(vkResetDescriptorPool);
    VKEW_LOAD_DEVICE(vkUpdateDescriptorSets);

    // Command pool/buffer
    VKEW_LOAD_DEVICE(vkCreateCommandPool);
    VKEW_LOAD_DEVICE(vkDestroyCommandPool);
    VKEW_LOAD_DEVICE(vkAllocateCommandBuffers);
    VKEW_LOAD_DEVICE(vkFreeCommandBuffers);
    VKEW_LOAD_DEVICE(vkBeginCommandBuffer);
    VKEW_LOAD_DEVICE(vkEndCommandBuffer);

    // Queue
    VKEW_LOAD_DEVICE(vkQueueSubmit);

    // Commands
    VKEW_LOAD_DEVICE(vkCmdBindPipeline);
    VKEW_LOAD_DEVICE(vkCmdBindDescriptorSets);
    VKEW_LOAD_DEVICE(vkCmdPushConstants);
    VKEW_LOAD_DEVICE(vkCmdDispatch);
    VKEW_LOAD_DEVICE(vkCmdCopyBuffer);
    VKEW_LOAD_DEVICE(vkCmdPipelineBarrier);

    // Optional ray-tracing / buffer-device-address entry points. These stay
    // NULL when their extension was not enabled; vkGetDeviceProcAddr returns
    // NULL for them and that is not an error. The core 1.2 spelling of
    // vkGetBufferDeviceAddress is tried as a fallback for the KHR alias.
    VKEW_LOAD_DEVICE_OPT(vkGetBufferDeviceAddressKHR);
    if (!vkGetBufferDeviceAddressKHR) {
        vkGetBufferDeviceAddressKHR =
            (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(
                device, "vkGetBufferDeviceAddress");
    }
    VKEW_LOAD_DEVICE_OPT(vkGetAccelerationStructureBuildSizesKHR);
    VKEW_LOAD_DEVICE_OPT(vkCreateAccelerationStructureKHR);
    VKEW_LOAD_DEVICE_OPT(vkDestroyAccelerationStructureKHR);
    VKEW_LOAD_DEVICE_OPT(vkCmdBuildAccelerationStructuresKHR);
    VKEW_LOAD_DEVICE_OPT(vkGetAccelerationStructureDeviceAddressKHR);

    return true;
}

const char* vkewGetError(void) {
    return g_errorMessage;
}

const char* vkewResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VK_UNKNOWN_RESULT";
    }
}
