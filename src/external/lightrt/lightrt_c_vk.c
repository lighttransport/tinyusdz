/*
 * lightrt_c_vk.c — Vulkan GPU interop implementation.
 *
 * Engine (instance/device/compute-queue/command-pool, host-visible storage
 * buffers, compute pipelines), Path A (CPU build -> GPU trace) and Path B
 * (GPU build front end -> CPU trace). Uses lightrt_vkew (runtime dlopen of
 * libvulkan; no Vulkan SDK headers, no link-time -lvulkan).
 *
 * v1 keeps it simple and correct: all buffers are HOST_VISIBLE | HOST_COHERENT
 * and uploaded/downloaded by map+memcpy (no staging/DEVICE_LOCAL copies yet).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightrt_c_vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lightrt_vkew.h"

/* Embedded SPIR-V (checked in; regenerate with scripts/compile_shaders.sh). */
#include "vk/shaders/trace_bvh.spv.h"        /* trace_bvh_spv[]       */
#include "vk/shaders/build_morton.spv.h"     /* build_morton_spv[]    */
#include "vk/shaders/trace_ray_query.spv.h"  /* trace_ray_query_spv[] */

/* Host-read sync flags not declared in lightrt_vkew.h. */
#ifndef VK_PIPELINE_STAGE_HOST_BIT
#define VK_PIPELINE_STAGE_HOST_BIT 0x00004000
#endif
#ifndef VK_ACCESS_HOST_READ_BIT
#define VK_ACCESS_HOST_READ_BIT 0x00002000
#endif
#ifndef VK_PIPELINE_STAGE_TRANSFER_BIT
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x00001000
#endif
#ifndef VK_ACCESS_TRANSFER_READ_BIT
#define VK_ACCESS_TRANSFER_READ_BIT 0x00000800
#endif
#ifndef VK_ACCESS_TRANSFER_WRITE_BIT
#define VK_ACCESS_TRANSFER_WRITE_BIT 0x00001000
#endif

/* GPU-assisted build hook implemented in lightrt_c_tri.c (not a public ABI). */
extern lrt_tri_scene *lrt_tri_scene_build_lbvh_morton(const float *vertices,
                                                      size_t ntris,
                                                      const uint32_t *morton,
                                                      lrt_tri_layout layout,
                                                      unsigned max_leaf_size,
                                                      lrt_result *err);

/* ------------------------------------------------------------------------- */
/* LRTS serialization header (must match lightrt_c_tri.c).                   */
/* ------------------------------------------------------------------------- */
typedef struct vk_lrts_header {
    char magic[4];
    uint32_t version;
    uint32_t endian;
    uint32_t flags;
    uint32_t layout;
    uint32_t prim_kind;
    uint32_t node_count;
    uint32_t block_count;
    uint32_t root;
    uint32_t node_stride;
    uint32_t block_stride;
    uint32_t reserved0;
    float root_lo[3];
    float root_hi[3];
    uint64_t node_offset;
    uint64_t block_offset;
    uint64_t file_size;
} vk_lrts_header;

/* ------------------------------------------------------------------------- */
/* Engine.                                                                   */
/* ------------------------------------------------------------------------- */
#define VK_TRACE_PIPE_CACHE 8

typedef struct vk_pipeline {
    int valid;
    uint32_t key_w;
    uint32_t key_stack;
    VkShaderModule module;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline pipe;
} vk_pipeline;

struct lrt_vk_engine {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    VkCommandPool cmd_pool;
    VkPhysicalDeviceMemoryProperties mem_props;
    uint32_t caps;
    char device_name[256];
    char err[512];

    vk_pipeline trace_pipes[VK_TRACE_PIPE_CACHE];
    vk_pipeline build_pipe; /* build_morton: no spec constants */
    vk_pipeline rtx_pipe;   /* trace_ray_query: AS + 2 SSBO bindings */
};

static void vk_set_err(lrt_vk_engine *e, const char *msg) {
    if (!e) return;
    snprintf(e->err, sizeof(e->err), "%s", msg);
}

static void vk_set_errf(lrt_vk_engine *e, const char *what, VkResult r) {
    if (!e) return;
    snprintf(e->err, sizeof(e->err), "%s: %s", what, vkewResultToString(r));
}

static uint32_t vk_find_memory_type(const lrt_vk_engine *e, uint32_t type_bits,
                                    VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < e->mem_props.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (e->mem_props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------------- */
/* Host-visible storage buffers.                                             */
/* ------------------------------------------------------------------------- */
typedef struct vk_buffer {
    VkBuffer buf;
    VkDeviceMemory mem;
    VkDeviceSize size;
} vk_buffer;

/* Full buffer creation: arbitrary usage, host-visible (mappable) or device-local
 * memory, and optionally device-address-capable (chains VkMemoryAllocateFlagsInfo
 * + adds SHADER_DEVICE_ADDRESS usage) for AS-input / scratch buffers. */
static int vk_buffer_create_ex(lrt_vk_engine *e, VkDeviceSize size,
                               VkBufferUsageFlags usage, int host_visible,
                               int device_addr, vk_buffer *out) {
    memset(out, 0, sizeof(*out));
    if (size == 0) size = 16;
    out->size = size;
    if (device_addr) usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(e->device, &bi, NULL, &out->buf);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateBuffer", r);
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(e->device, out->buf, &req);
    VkMemoryPropertyFlags want =
        host_visible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                     : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t mt = vk_find_memory_type(e, req.memoryTypeBits, want);
    if (mt == UINT32_MAX) {
        vk_set_err(e, "no suitable memory type");
        vkDestroyBuffer(e->device, out->buf, NULL);
        out->buf = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryAllocateFlagsInfo fi;
    memset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext = device_addr ? &fi : NULL;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    r = vkAllocateMemory(e->device, &ai, NULL, &out->mem);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkAllocateMemory", r);
        vkDestroyBuffer(e->device, out->buf, NULL);
        out->buf = VK_NULL_HANDLE;
        return 0;
    }
    vkBindBufferMemory(e->device, out->buf, out->mem, 0);
    return 1;
}

/* Host-visible coherent storage buffer (Path A/B working set). */
static int vk_buffer_create(lrt_vk_engine *e, VkDeviceSize size, vk_buffer *out) {
    return vk_buffer_create_ex(e, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1, 0,
                               out);
}

static void vk_buffer_destroy(lrt_vk_engine *e, vk_buffer *b) {
    if (b->buf) vkDestroyBuffer(e->device, b->buf, NULL);
    if (b->mem) vkFreeMemory(e->device, b->mem, NULL);
    memset(b, 0, sizeof(*b));
}

static int vk_buffer_write(lrt_vk_engine *e, vk_buffer *b, const void *src,
                           size_t bytes) {
    void *p = NULL;
    VkResult r = vkMapMemory(e->device, b->mem, 0, VK_WHOLE_SIZE, 0, &p);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkMapMemory(write)", r);
        return 0;
    }
    if (bytes) memcpy(p, src, bytes);
    vkUnmapMemory(e->device, b->mem);
    return 1;
}

static int vk_buffer_read(lrt_vk_engine *e, vk_buffer *b, void *dst,
                          size_t bytes) {
    void *p = NULL;
    VkResult r = vkMapMemory(e->device, b->mem, 0, VK_WHOLE_SIZE, 0, &p);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkMapMemory(read)", r);
        return 0;
    }
    if (bytes) memcpy(dst, p, bytes);
    vkUnmapMemory(e->device, b->mem);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Compute pipeline construction.                                            */
/* ------------------------------------------------------------------------- */
static int vk_pipeline_build(lrt_vk_engine *e, const uint32_t *spv,
                             size_t spv_bytes, uint32_t num_bindings,
                             const VkDescriptorType *types, uint32_t push_size,
                             const VkSpecializationInfo *spec, vk_pipeline *out) {
    VkResult r;
    VkShaderModuleCreateInfo smi;
    memset(&smi, 0, sizeof(smi));
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = spv_bytes;
    smi.pCode = spv;
    r = vkCreateShaderModule(e->device, &smi, NULL, &out->module);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateShaderModule", r);
        return 0;
    }

    VkDescriptorSetLayoutBinding binds[8];
    memset(binds, 0, sizeof(binds));
    for (uint32_t i = 0; i < num_bindings; i++) {
        binds[i].binding = i;
        binds[i].descriptorType =
            types ? types[i] : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dlci;
    memset(&dlci, 0, sizeof(dlci));
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = num_bindings;
    dlci.pBindings = binds;
    r = vkCreateDescriptorSetLayout(e->device, &dlci, NULL, &out->dsl);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateDescriptorSetLayout", r);
        vkDestroyShaderModule(e->device, out->module, NULL);
        return 0;
    }

    VkPushConstantRange pcr;
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = push_size;
    VkPipelineLayoutCreateInfo plci;
    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &out->dsl;
    plci.pushConstantRangeCount = push_size ? 1 : 0;
    plci.pPushConstantRanges = push_size ? &pcr : NULL;
    r = vkCreatePipelineLayout(e->device, &plci, NULL, &out->layout);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreatePipelineLayout", r);
        vkDestroyDescriptorSetLayout(e->device, out->dsl, NULL);
        vkDestroyShaderModule(e->device, out->module, NULL);
        return 0;
    }

    VkComputePipelineCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = out->module;
    cpci.stage.pName = "main";
    cpci.stage.pSpecializationInfo = spec;
    cpci.layout = out->layout;
    cpci.basePipelineIndex = -1;
    r = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &cpci, NULL,
                                 &out->pipe);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateComputePipelines", r);
        vkDestroyPipelineLayout(e->device, out->layout, NULL);
        vkDestroyDescriptorSetLayout(e->device, out->dsl, NULL);
        vkDestroyShaderModule(e->device, out->module, NULL);
        return 0;
    }
    out->valid = 1;
    return 1;
}

static void vk_pipeline_destroy(lrt_vk_engine *e, vk_pipeline *p) {
    if (!p->valid) return;
    vkDestroyPipeline(e->device, p->pipe, NULL);
    vkDestroyPipelineLayout(e->device, p->layout, NULL);
    vkDestroyDescriptorSetLayout(e->device, p->dsl, NULL);
    vkDestroyShaderModule(e->device, p->module, NULL);
    memset(p, 0, sizeof(*p));
}

/* Allocate a one-shot descriptor pool + set bound to `n` storage buffers. */
static int vk_descriptors_bind(lrt_vk_engine *e, VkDescriptorSetLayout dsl,
                               const vk_buffer *buffers, uint32_t n,
                               VkDescriptorPool *out_pool,
                               VkDescriptorSet *out_set) {
    VkDescriptorPoolSize ps;
    ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps.descriptorCount = n;
    VkDescriptorPoolCreateInfo dpci;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    VkResult r = vkCreateDescriptorPool(e->device, &dpci, NULL, out_pool);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateDescriptorPool", r);
        return 0;
    }
    VkDescriptorSetAllocateInfo dsai;
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = *out_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    r = vkAllocateDescriptorSets(e->device, &dsai, out_set);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkAllocateDescriptorSets", r);
        vkDestroyDescriptorPool(e->device, *out_pool, NULL);
        return 0;
    }
    VkDescriptorBufferInfo bi[8];
    VkWriteDescriptorSet w[8];
    memset(w, 0, sizeof(w));
    for (uint32_t i = 0; i < n; i++) {
        bi[i].buffer = buffers[i].buf;
        bi[i].offset = 0;
        bi[i].range = VK_WHOLE_SIZE;
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = *out_set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i].pBufferInfo = &bi[i];
    }
    vkUpdateDescriptorSets(e->device, n, w, 0, NULL);
    return 1;
}

/* Record and submit a single dispatch, blocking on a fence until it completes.
 * A COMPUTE_SHADER -> HOST barrier makes shader writes visible to a later map. */
static int vk_dispatch(lrt_vk_engine *e, const vk_pipeline *p,
                       VkDescriptorSet set, const void *push, uint32_t push_size,
                       uint32_t groups_x) {
    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = e->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VkResult r = vkAllocateCommandBuffers(e->device, &cbai, &cb);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkAllocateCommandBuffers", r);
        return 0;
    }
    VkCommandBufferBeginInfo bbi;
    memset(&bbi, 0, sizeof(bbi));
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bbi);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1,
                            &set, 0, NULL);
    if (push_size)
        vkCmdPushConstants(cb, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           push_size, push);
    vkCmdDispatch(cb, groups_x, 1, 1);
    VkMemoryBarrier mb;
    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    vkEndCommandBuffer(cb);

    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    r = vkCreateFence(e->device, &fci, NULL, &fence);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateFence", r);
        vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
        return 0;
    }
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    r = vkQueueSubmit(e->queue, 1, &si, fence);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkQueueSubmit", r);
        vkDestroyFence(e->device, fence, NULL);
        vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
        return 0;
    }
    r = vkWaitForFences(e->device, 1, &fence, VK_TRUE, ~0ULL);
    vkDestroyFence(e->device, fence, NULL);
    vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkWaitForFences", r);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Engine create / destroy.                                                  */
/* ------------------------------------------------------------------------- */
static int vk_has_ext(const VkExtensionProperties *props, uint32_t n,
                      const char *name) {
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(props[i].extensionName, name) == 0) return 1;
    return 0;
}

lrt_vk_engine *lrt_vk_engine_create(const lrt_vk_engine_options *opts,
                                    lrt_result *err) {
    lrt_vk_engine_options o;
    if (opts) {
        o = *opts;
    } else {
        memset(&o, 0, sizeof(o));
        o.device_index = -1;
    }

    if (!vkewInit()) {
        if (err) *err = LRT_RESULT_NOT_BUILT;
        return NULL;
    }

    lrt_vk_engine *e = (lrt_vk_engine *)calloc(1, sizeof(*e));
    if (!e) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return NULL;
    }

    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "lightrt";
    app.pEngineName = "lightrt";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    VkResult r = vkCreateInstance(&ici, NULL, &e->instance);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateInstance", r);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }
    if (!vkewLoadInstance(e->instance)) {
        vk_set_err(e, vkewGetError());
        vkDestroyInstance(e->instance, NULL);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }

    /* Pick a physical device with a compute queue. */
    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(e->instance, &ndev, NULL);
    if (ndev == 0) {
        vk_set_err(e, "no Vulkan physical devices");
        vkDestroyInstance(e->instance, NULL);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }
    if (ndev > 16) ndev = 16;
    VkPhysicalDevice devs[16];
    vkEnumeratePhysicalDevices(e->instance, &ndev, devs);

    int chosen = -1;
    uint32_t chosen_qf = 0;
    int chosen_is_discrete = 0;
    for (uint32_t d = 0; d < ndev; d++) {
        if (o.device_index >= 0 && (uint32_t)o.device_index != d) continue;
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &nq, NULL);
        if (nq == 0) continue;
        if (nq > 32) nq = 32;
        VkQueueFamilyProperties qf[32];
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &nq, qf);
        int qfi = -1;
        for (uint32_t q = 0; q < nq; q++) {
            if (qf[q].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                qfi = (int)q;
                break;
            }
        }
        if (qfi < 0) continue;
        VkPhysicalDeviceProperties pp;
        vkGetPhysicalDeviceProperties(devs[d], &pp);
        int is_discrete = (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        if (chosen < 0 || (o.prefer_discrete && is_discrete && !chosen_is_discrete)) {
            chosen = (int)d;
            chosen_qf = (uint32_t)qfi;
            chosen_is_discrete = is_discrete;
            snprintf(e->device_name, sizeof(e->device_name), "%s", pp.deviceName);
            if (!o.prefer_discrete) break;
        }
    }
    if (chosen < 0) {
        vk_set_err(e, "no compute-capable Vulkan device");
        vkDestroyInstance(e->instance, NULL);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }
    e->phys = devs[chosen];
    e->queue_family = chosen_qf;
    vkGetPhysicalDeviceMemoryProperties(e->phys, &e->mem_props);

    /* Detect ray-tracing extensions (loader hooks only in v1). */
    int want_rt = o.want_ray_tracing;
    int have_as = 0, have_rq = 0, have_bda = 0;
    if (want_rt) {
        uint32_t nx = 0;
        vkEnumerateDeviceExtensionProperties(e->phys, NULL, &nx, NULL);
        if (nx > 0) {
            VkExtensionProperties *xs =
                (VkExtensionProperties *)calloc(nx, sizeof(*xs));
            if (xs) {
                vkEnumerateDeviceExtensionProperties(e->phys, NULL, &nx, xs);
                have_as = vk_has_ext(xs, nx, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
                have_rq = vk_has_ext(xs, nx, VK_KHR_RAY_QUERY_EXTENSION_NAME);
                have_bda = vk_has_ext(xs, nx, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
                free(xs);
            }
        }
    }
    int enable_rt = want_rt && have_as && have_rq;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = e->queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    /* Optional RT feature chain. */
    VkPhysicalDeviceRayQueryFeaturesKHR rqf;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asf;
    VkPhysicalDeviceVulkan12Features v12;
    VkPhysicalDeviceFeatures2 f2;
    memset(&rqf, 0, sizeof(rqf));
    memset(&asf, 0, sizeof(asf));
    memset(&v12, 0, sizeof(v12));
    memset(&f2, 0, sizeof(f2));
    const char *rt_exts[6];
    uint32_t n_rt_exts = 0;
    const void *device_pnext = NULL;
    if (enable_rt) {
        rqf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rqf.rayQuery = VK_TRUE;
        asf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asf.accelerationStructure = VK_TRUE;
        asf.pNext = &rqf;
        v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        v12.bufferDeviceAddress = VK_TRUE;
        v12.pNext = &asf;
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &v12;
        device_pnext = &f2;
        rt_exts[n_rt_exts++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        rt_exts[n_rt_exts++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        rt_exts[n_rt_exts++] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
        rt_exts[n_rt_exts++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        rt_exts[n_rt_exts++] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
        rt_exts[n_rt_exts++] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;
        (void)have_bda;
    }

    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = device_pnext;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = n_rt_exts;
    dci.ppEnabledExtensionNames = n_rt_exts ? rt_exts : NULL;
    r = vkCreateDevice(e->phys, &dci, NULL, &e->device);
    if (r != VK_SUCCESS && enable_rt) {
        /* Retry compute-only. */
        memset(&dci, 0, sizeof(dci));
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        enable_rt = 0;
        r = vkCreateDevice(e->phys, &dci, NULL, &e->device);
    }
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateDevice", r);
        vkDestroyInstance(e->instance, NULL);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }
    if (!vkewLoadDevice(e->device)) {
        vk_set_err(e, vkewGetError());
        vkDestroyDevice(e->device, NULL);
        vkDestroyInstance(e->instance, NULL);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }

    vkGetDeviceQueue(e->device, e->queue_family, 0, &e->queue);

    VkCommandPoolCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = e->queue_family;
    r = vkCreateCommandPool(e->device, &cpci, NULL, &e->cmd_pool);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateCommandPool", r);
        vkDestroyDevice(e->device, NULL);
        vkDestroyInstance(e->instance, NULL);
        if (err) *err = LRT_RESULT_NOT_BUILT;
        free(e);
        return NULL;
    }

    e->caps = LRT_VK_CAP_COMPUTE;
    /* Reflect what actually loaded, not just what was enumerated. */
    if (vkGetBufferDeviceAddressKHR) e->caps |= LRT_VK_CAP_BUFFER_ADDRESS;
    if (vkCreateAccelerationStructureKHR) e->caps |= LRT_VK_CAP_ACCEL_STRUCT;
    if (enable_rt) e->caps |= LRT_VK_CAP_RAY_QUERY;

    vk_set_err(e, "");
    if (err) *err = LRT_RESULT_OK;
    return e;
}

void lrt_vk_engine_destroy(lrt_vk_engine *e) {
    if (!e) return;
    if (e->device) {
        vkDeviceWaitIdle(e->device);
        for (int i = 0; i < VK_TRACE_PIPE_CACHE; i++)
            vk_pipeline_destroy(e, &e->trace_pipes[i]);
        vk_pipeline_destroy(e, &e->build_pipe);
        vk_pipeline_destroy(e, &e->rtx_pipe);
        if (e->cmd_pool) vkDestroyCommandPool(e->device, e->cmd_pool, NULL);
        vkDestroyDevice(e->device, NULL);
    }
    if (e->instance) vkDestroyInstance(e->instance, NULL);
    free(e);
}

uint32_t lrt_vk_engine_caps(const lrt_vk_engine *e) {
    return e ? e->caps : 0u;
}

const char *lrt_vk_engine_device_name(const lrt_vk_engine *e) {
    return e ? e->device_name : "";
}

const char *lrt_vk_engine_last_error(const lrt_vk_engine *e) {
    return e ? e->err : "";
}

/* ------------------------------------------------------------------------- */
/* Path A: CPU build -> GPU trace.                                           */
/* ------------------------------------------------------------------------- */
typedef struct trace_push {
    uint32_t root;
    uint32_t node_count;
    uint32_t block_count;
    uint32_t ray_count;
} trace_push;

/* Bucket the traversal stack depth so pipelines are reused across scenes. */
static uint32_t trace_stack_for(uint32_t max_depth, uint32_t w) {
    uint32_t need = max_depth * (w - 1u) + w + 1u;
    static const uint32_t buckets[] = {32u, 64u, 128u, 256u};
    for (int i = 0; i < 4; i++)
        if (need <= buckets[i]) return buckets[i];
    return 0; /* too deep for the compute path */
}

static vk_pipeline *trace_get_pipeline(lrt_vk_engine *e, uint32_t w,
                                       uint32_t stack) {
    for (int i = 0; i < VK_TRACE_PIPE_CACHE; i++) {
        vk_pipeline *p = &e->trace_pipes[i];
        if (p->valid && p->key_w == w && p->key_stack == stack) return p;
    }
    vk_pipeline *slot = NULL;
    for (int i = 0; i < VK_TRACE_PIPE_CACHE; i++) {
        if (!e->trace_pipes[i].valid) {
            slot = &e->trace_pipes[i];
            break;
        }
    }
    if (!slot) { /* cache full: recycle slot 0 */
        vk_pipeline_destroy(e, &e->trace_pipes[0]);
        slot = &e->trace_pipes[0];
    }
    uint32_t spec_data[2] = {w, stack};
    VkSpecializationMapEntry ents[2];
    ents[0].constantID = 0;
    ents[0].offset = 0;
    ents[0].size = sizeof(uint32_t);
    ents[1].constantID = 1;
    ents[1].offset = sizeof(uint32_t);
    ents[1].size = sizeof(uint32_t);
    VkSpecializationInfo spec;
    spec.mapEntryCount = 2;
    spec.pMapEntries = ents;
    spec.dataSize = sizeof(spec_data);
    spec.pData = spec_data;
    if (!vk_pipeline_build(e, trace_bvh_spv, sizeof(trace_bvh_spv), 4, NULL,
                           (uint32_t)sizeof(trace_push), &spec, slot))
        return NULL;
    slot->key_w = w;
    slot->key_stack = stack;
    return slot;
}

int lrt_vk_trace_scene(lrt_vk_engine *e, const lrt_tri_scene *s,
                       const lrt_ray *rays, uint32_t n, lrt_hit *out,
                       lrt_result *err) {
    if (!e || !s || (n && (!rays || !out))) {
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return -1;
    }
    if (n == 0) {
        if (err) *err = LRT_RESULT_OK;
        return 0;
    }

    void *blob = NULL;
    size_t blob_n = 0;
    lrt_result sr = lrt_tri_scene_save_to_memory(s, &blob, &blob_n);
    if (sr != LRT_RESULT_OK) {
        vk_set_err(e, "scene not GPU-traceable (quantized/curve/user?)");
        if (err) *err = sr;
        return -1;
    }
    const vk_lrts_header *h = (const vk_lrts_header *)blob;
    uint32_t w = h->layout; /* 4 or 8 */

    lrt_tri_stats st;
    lrt_tri_scene_stats(s, &st);
    uint32_t stack = trace_stack_for(st.max_depth, w);
    if (stack == 0) {
        vk_set_err(e, "BVH too deep for the GPU compute stack");
        free(blob);
        if (err) *err = LRT_RESULT_TRAVERSAL_OVERFLOW;
        return -1;
    }

    vk_pipeline *pipe = trace_get_pipeline(e, w, stack);
    if (!pipe) {
        free(blob);
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    size_t nodes_bytes = (size_t)h->node_count * h->node_stride;
    size_t blocks_bytes = (size_t)h->block_count * h->block_stride;
    vk_buffer b_nodes, b_blocks, b_rays, b_hits;
    int ok = vk_buffer_create(e, nodes_bytes, &b_nodes) &&
             vk_buffer_create(e, blocks_bytes, &b_blocks) &&
             vk_buffer_create(e, (VkDeviceSize)n * sizeof(lrt_ray), &b_rays) &&
             vk_buffer_create(e, (VkDeviceSize)n * sizeof(lrt_hit), &b_hits);
    if (!ok) goto fail_buffers;

    if (!vk_buffer_write(e, &b_nodes, (const char *)blob + h->node_offset,
                         nodes_bytes) ||
        !vk_buffer_write(e, &b_blocks, (const char *)blob + h->block_offset,
                         blocks_bytes) ||
        !vk_buffer_write(e, &b_rays, rays, (size_t)n * sizeof(lrt_ray)))
        goto fail_buffers;

    {
        vk_buffer set_bufs[4] = {b_nodes, b_blocks, b_rays, b_hits};
        VkDescriptorPool pool;
        VkDescriptorSet set;
        if (!vk_descriptors_bind(e, pipe->dsl, set_bufs, 4, &pool, &set))
            goto fail_buffers;
        trace_push pc;
        pc.root = h->root;
        pc.node_count = h->node_count;
        pc.block_count = h->block_count;
        pc.ray_count = n;
        uint32_t groups = (n + 63u) / 64u;
        int run_ok =
            vk_dispatch(e, pipe, set, &pc, (uint32_t)sizeof(pc), groups);
        vkDestroyDescriptorPool(e->device, pool, NULL);
        if (!run_ok) goto fail_buffers;
        if (!vk_buffer_read(e, &b_hits, out, (size_t)n * sizeof(lrt_hit)))
            goto fail_buffers;
    }

    vk_buffer_destroy(e, &b_nodes);
    vk_buffer_destroy(e, &b_blocks);
    vk_buffer_destroy(e, &b_rays);
    vk_buffer_destroy(e, &b_hits);
    free(blob);

    int hits = 0;
    for (uint32_t i = 0; i < n; i++)
        if (out[i].prim_id != LRT_TRI_NO_HIT) hits++;
    if (err) *err = LRT_RESULT_OK;
    return hits;

fail_buffers:
    vk_buffer_destroy(e, &b_nodes);
    vk_buffer_destroy(e, &b_blocks);
    vk_buffer_destroy(e, &b_rays);
    vk_buffer_destroy(e, &b_hits);
    free(blob);
    if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Path B: GPU build front end -> CPU trace.                                 */
/* ------------------------------------------------------------------------- */
typedef struct morton_push {
    uint32_t pass;
    uint32_t ntris;
    float base0, base1, base2;
    float scale0, scale1, scale2;
} morton_push;

static vk_pipeline *build_get_pipeline(lrt_vk_engine *e) {
    if (e->build_pipe.valid) return &e->build_pipe;
    if (!vk_pipeline_build(e, build_morton_spv, sizeof(build_morton_spv), 3, NULL,
                           (uint32_t)sizeof(morton_push), NULL, &e->build_pipe))
        return NULL;
    return &e->build_pipe;
}

int lrt_vk_build_scene(lrt_vk_engine *e, const float *vertices, uint32_t ntris,
                       lrt_tri_layout layout, lrt_tri_scene **out,
                       lrt_result *err) {
    if (!e || !vertices || ntris == 0 || !out) {
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return -1;
    }
    if (layout != LRT_TRI_LAYOUT_BVH4 && layout != LRT_TRI_LAYOUT_BVH8) {
        vk_set_err(e, "Path B supports BVH4/BVH8 only");
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return -1;
    }
    *out = NULL;

    vk_pipeline *pipe = build_get_pipeline(e);
    if (!pipe) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    vk_buffer b_verts, b_cen, b_morton;
    int ok = vk_buffer_create(e, (VkDeviceSize)ntris * 9u * sizeof(float),
                              &b_verts) &&
             vk_buffer_create(e, (VkDeviceSize)ntris * 3u * sizeof(float),
                              &b_cen) &&
             vk_buffer_create(e, (VkDeviceSize)ntris * sizeof(uint32_t),
                              &b_morton);
    uint32_t *morton = NULL;
    float *cen = NULL;
    if (!ok) goto fail;
    if (!vk_buffer_write(e, &b_verts, vertices, (size_t)ntris * 9u * sizeof(float)))
        goto fail;

    {
        vk_buffer set_bufs[3] = {b_verts, b_cen, b_morton};
        VkDescriptorPool pool;
        VkDescriptorSet set;
        if (!vk_descriptors_bind(e, pipe->dsl, set_bufs, 3, &pool, &set))
            goto fail;
        uint32_t groups = (ntris + 63u) / 64u;

        /* Pass 0: centroids. */
        morton_push pc;
        memset(&pc, 0, sizeof(pc));
        pc.pass = 0;
        pc.ntris = ntris;
        int run_ok = vk_dispatch(e, pipe, set, &pc, (uint32_t)sizeof(pc), groups);
        if (!run_ok) {
            vkDestroyDescriptorPool(e->device, pool, NULL);
            goto fail;
        }

        /* Read centroids; CPU reduces the centroid AABB (matches
         * tri_morton_encode) and derives base/scale. */
        cen = (float *)malloc((size_t)ntris * 3u * sizeof(float));
        if (!cen) {
            vkDestroyDescriptorPool(e->device, pool, NULL);
            goto fail;
        }
        if (!vk_buffer_read(e, &b_cen, cen, (size_t)ntris * 3u * sizeof(float))) {
            vkDestroyDescriptorPool(e->device, pool, NULL);
            goto fail;
        }
        float clo[3] = {3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f};
        float chi[3] = {-3.402823466e+38f, -3.402823466e+38f, -3.402823466e+38f};
        for (uint32_t i = 0; i < ntris; i++) {
            for (int a = 0; a < 3; a++) {
                float v = cen[(size_t)i * 3 + a];
                if (v < clo[a]) clo[a] = v;
                if (v > chi[a]) chi[a] = v;
            }
        }
        pc.pass = 1;
        for (int a = 0; a < 3; a++) {
            float ext = chi[a] - clo[a];
            float base = clo[a];
            float scale = ext > 0.0f ? 1024.0f / ext : 0.0f;
            if (a == 0) { pc.base0 = base; pc.scale0 = scale; }
            else if (a == 1) { pc.base1 = base; pc.scale1 = scale; }
            else { pc.base2 = base; pc.scale2 = scale; }
        }

        /* Pass 1: Morton codes. */
        run_ok = vk_dispatch(e, pipe, set, &pc, (uint32_t)sizeof(pc), groups);
        vkDestroyDescriptorPool(e->device, pool, NULL);
        if (!run_ok) goto fail;

        morton = (uint32_t *)malloc((size_t)ntris * sizeof(uint32_t));
        if (!morton) goto fail;
        if (!vk_buffer_read(e, &b_morton, morton,
                            (size_t)ntris * sizeof(uint32_t)))
            goto fail;
    }

    vk_buffer_destroy(e, &b_verts);
    vk_buffer_destroy(e, &b_cen);
    vk_buffer_destroy(e, &b_morton);
    free(cen);

    /* Finish the LBVH on the CPU using the GPU-computed Morton codes. */
    lrt_result br = LRT_RESULT_OK;
    lrt_tri_scene *scene =
        lrt_tri_scene_build_lbvh_morton(vertices, ntris, morton, layout, 0, &br);
    free(morton);
    if (!scene) {
        vk_set_err(e, "CPU LBVH finish failed");
        if (err) *err = br;
        return -1;
    }
    *out = scene;
    if (err) *err = LRT_RESULT_OK;
    return 0;

fail:
    vk_buffer_destroy(e, &b_verts);
    vk_buffer_destroy(e, &b_cen);
    vk_buffer_destroy(e, &b_morton);
    free(cen);
    free(morton);
    if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Hardware ray tracing (VK_KHR_ray_query): build a real acceleration         */
/* structure on the GPU and trace it. Separate from Path A/B (the AS is        */
/* vendor-opaque, so it can only serve the trace direction).                  */
/* ------------------------------------------------------------------------- */

static uint64_t vk_device_address(lrt_vk_engine *e, const vk_buffer *b) {
    VkBufferDeviceAddressInfo i;
    memset(&i, 0, sizeof(i));
    i.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    i.buffer = b->buf;
    return (uint64_t)vkGetBufferDeviceAddressKHR(e->device, &i);
}

/* One-shot command buffer helpers (for AS builds; the trace reuses vk_dispatch). */
static VkCommandBuffer vk_cmd_begin(lrt_vk_engine *e) {
    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = e->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(e->device, &cbai, &cb) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bbi;
    memset(&bbi, 0, sizeof(bbi));
    bbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bbi);
    return cb;
}

static int vk_cmd_end_submit(lrt_vk_engine *e, VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    if (vkCreateFence(e->device, &fci, NULL, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
        return 0;
    }
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkResult r = vkQueueSubmit(e->queue, 1, &si, fence);
    if (r == VK_SUCCESS) {
        /* 5 second timeout — ~0ULL (infinite) would hang the process on a
         * device-lost or misconfigured AS build. */
        r = vkWaitForFences(e->device, 1, &fence, VK_TRUE, 5000000000ULL);
        if (r == VK_TIMEOUT) {
            vk_set_err(e, "GPU command timed out after 5 s (device lost or "
                          "misconfigured acceleration-structure build)");
            vkDestroyFence(e->device, fence, NULL);
            vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
            return 0;
        }
    }
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkQueueSubmit/vkWaitForFences", r);
        vkDestroyFence(e->device, fence, NULL);
        vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
        return 0;
    }
    vkDestroyFence(e->device, fence, NULL);
    vkFreeCommandBuffers(e->device, e->cmd_pool, 1, &cb);
    return 1;
}

typedef struct vk_accel {
    VkAccelerationStructureKHR as;
    vk_buffer storage;
    uint64_t address;
} vk_accel;

/* Build one acceleration structure (BLAS or TLAS) on the device from a single
 * geometry. Allocates the AS backing store + a transient scratch buffer, records
 * the build, and resolves the AS device address. */
static int vk_build_accel(lrt_vk_engine *e, VkAccelerationStructureTypeKHR type,
                          const VkAccelerationStructureGeometryKHR *geom,
                          uint32_t prim_count, vk_accel *out) {
    memset(out, 0, sizeof(*out));
    VkAccelerationStructureBuildGeometryInfoKHR bgi;
    memset(&bgi, 0, sizeof(bgi));
    bgi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bgi.type = type;
    bgi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries = geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes;
    memset(&sizes, 0, sizeof(sizes));
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        e->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bgi,
        &prim_count, &sizes);

    if (!vk_buffer_create_ex(e, sizes.accelerationStructureSize,
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                             0, 0, &out->storage))
        return 0;

    VkAccelerationStructureCreateInfoKHR ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    ci.buffer = out->storage.buf;
    ci.offset = 0;
    ci.size = sizes.accelerationStructureSize;
    ci.type = type;
    VkResult r = vkCreateAccelerationStructureKHR(e->device, &ci, NULL, &out->as);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateAccelerationStructureKHR", r);
        vk_buffer_destroy(e, &out->storage);
        return 0;
    }

    /* Scratch: over-allocate 256 bytes so we can 256-align the device address
     * (a safe upper bound for minAccelerationStructureScratchOffsetAlignment). */
    vk_buffer scratch;
    if (!vk_buffer_create_ex(e, sizes.buildScratchSize + 256u,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 1, &scratch)) {
        vkDestroyAccelerationStructureKHR(e->device, out->as, NULL);
        vk_buffer_destroy(e, &out->storage);
        return 0;
    }
    uint64_t scratch_addr = vk_device_address(e, &scratch);
    scratch_addr = (scratch_addr + 255u) & ~(uint64_t)255u;

    bgi.dstAccelerationStructure = out->as;
    bgi.scratchData.deviceAddress = scratch_addr;
    VkAccelerationStructureBuildRangeInfoKHR range;
    range.primitiveCount = prim_count;
    range.primitiveOffset = 0;
    range.firstVertex = 0;
    range.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR *pranges = &range;

    VkCommandBuffer cb = vk_cmd_begin(e);
    int ok = cb != VK_NULL_HANDLE;
    if (ok) {
        vkCmdBuildAccelerationStructuresKHR(cb, 1, &bgi, &pranges);
        ok = vk_cmd_end_submit(e, cb);
    }
    vk_buffer_destroy(e, &scratch);
    if (!ok) {
        vk_set_err(e, "acceleration-structure build submit failed");
        vkDestroyAccelerationStructureKHR(e->device, out->as, NULL);
        vk_buffer_destroy(e, &out->storage);
        return 0;
    }

    VkAccelerationStructureDeviceAddressInfoKHR ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    ai.accelerationStructure = out->as;
    out->address =
        (uint64_t)vkGetAccelerationStructureDeviceAddressKHR(e->device, &ai);
    return 1;
}

static void vk_accel_destroy(lrt_vk_engine *e, vk_accel *a) {
    if (a->as) vkDestroyAccelerationStructureKHR(e->device, a->as, NULL);
    vk_buffer_destroy(e, &a->storage);
    memset(a, 0, sizeof(*a));
}

typedef struct rtx_push {
    uint32_t ray_count;
} rtx_push;

static vk_pipeline *rtx_get_pipeline(lrt_vk_engine *e) {
    if (e->rtx_pipe.valid) return &e->rtx_pipe;
    VkDescriptorType types[3] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    if (!vk_pipeline_build(e, trace_ray_query_spv, sizeof(trace_ray_query_spv), 3,
                           types, (uint32_t)sizeof(rtx_push), NULL, &e->rtx_pipe))
        return NULL;
    return &e->rtx_pipe;
}

/* TLAS + 2 storage buffers (rays, hits). The AS write is chained via pNext. */
static int vk_descriptors_bind_rtx(lrt_vk_engine *e, VkDescriptorSetLayout dsl,
                                   VkAccelerationStructureKHR tlas,
                                   const vk_buffer *rays, const vk_buffer *hits,
                                   VkDescriptorPool *out_pool,
                                   VkDescriptorSet *out_set) {
    VkDescriptorPoolSize ps[2];
    ps[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[1].descriptorCount = 2;
    VkDescriptorPoolCreateInfo dpci;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = ps;
    VkResult r = vkCreateDescriptorPool(e->device, &dpci, NULL, out_pool);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkCreateDescriptorPool(rtx)", r);
        return 0;
    }
    VkDescriptorSetAllocateInfo dsai;
    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = *out_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    r = vkAllocateDescriptorSets(e->device, &dsai, out_set);
    if (r != VK_SUCCESS) {
        vk_set_errf(e, "vkAllocateDescriptorSets(rtx)", r);
        vkDestroyDescriptorPool(e->device, *out_pool, NULL);
        return 0;
    }
    VkWriteDescriptorSetAccelerationStructureKHR asw;
    memset(&asw, 0, sizeof(asw));
    asw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asw.accelerationStructureCount = 1;
    asw.pAccelerationStructures = &tlas;
    VkDescriptorBufferInfo bi[2];
    bi[0].buffer = rays->buf;
    bi[0].offset = 0;
    bi[0].range = VK_WHOLE_SIZE;
    bi[1].buffer = hits->buf;
    bi[1].offset = 0;
    bi[1].range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet w[3];
    memset(w, 0, sizeof(w));
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].pNext = &asw;
    w[0].dstSet = *out_set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = *out_set;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &bi[0];
    w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[2].dstSet = *out_set;
    w[2].dstBinding = 2;
    w[2].descriptorCount = 1;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[2].pBufferInfo = &bi[1];
    vkUpdateDescriptorSets(e->device, 3, w, 0, NULL);
    return 1;
}

/* Resident ray-tracing scene: the BLAS+TLAS are built once and kept on the
 * device; the trace working set (device-local ray/hit buffers + host-visible
 * staging) is allocated lazily and reused/grown across traces. This is what
 * makes lrt_vk_rtx_scene_trace measure traversal, not per-call AS build. */
struct lrt_vk_rtx_scene {
    vk_accel blas;
    vk_accel tlas;
    vk_buffer rays_dev;   /* device-local STORAGE | TRANSFER_DST */
    vk_buffer hits_dev;   /* device-local STORAGE | TRANSFER_SRC */
    vk_buffer rays_stage; /* host-visible TRANSFER_SRC           */
    vk_buffer hits_stage; /* host-visible TRANSFER_DST           */
    uint32_t cap;         /* trace-buffer capacity in rays       */
};

/* Shared BLAS+TLAS build behind the soup and indexed entry points. When
 * `indices` is NULL the BLAS is built from a de-indexed triangle soup
 * (`vertices` = 9*ntris floats, nverts == 3*ntris, no index buffer). When
 * `indices` is non-NULL the BLAS uses a real VK_INDEX_TYPE_UINT32 index buffer
 * (`vertices` = 3*nverts floats of unique positions, `indices` = 3*ntris vertex
 * ids), so callers with shared vertices need not de-index. Either way the
 * primitiveIndex is the triangle build order, so lrt_hit.prim_id stays
 * 0..ntris-1. */
static lrt_vk_rtx_scene *rtx_scene_build_core(lrt_vk_engine *e,
                                              const float *vertices,
                                              uint32_t nverts,
                                              const uint32_t *indices,
                                              uint32_t ntris, lrt_result *err) {
    if (!e || !vertices || ntris == 0 || nverts == 0) {
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return NULL;
    }
    if (!(e->caps & LRT_VK_CAP_RAY_QUERY) || !vkCreateAccelerationStructureKHR) {
        vk_set_err(e, "ray_query unavailable (create the engine with "
                      "want_ray_tracing=1 on an RT-capable device)");
        if (err) *err = LRT_RESULT_NOT_BUILT;
        return NULL;
    }
    if (!rtx_get_pipeline(e)) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return NULL;
    }

    lrt_vk_rtx_scene *s = (lrt_vk_rtx_scene *)calloc(1, sizeof(*s));
    if (!s) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return NULL;
    }

    /* The vertex/index/instance build inputs are only needed during the build —
     * once vkCmdBuildAccelerationStructures completes, the AS is self-contained,
     * so they are freed here and the resident scene holds only the BLAS + TLAS.
     * The vertex/index build inputs are staged into device-local memory: the AS
     * build input must be device-local on many implementations (NVIDIA requires
     * it). */
    vk_buffer vbuf = {0}, idxbuf = {0}, instbuf = {0};
    vk_buffer vstage = {0}, istage = {0};
    int ok = 0;
    do {
        VkDeviceSize vbytes = (VkDeviceSize)nverts * 3u * sizeof(float);
        VkDeviceSize ibytes = (VkDeviceSize)ntris * 3u * sizeof(uint32_t);
        /* Vertex staging (host-visible) + device-local AS build input. */
        if (!vk_buffer_create_ex(e, vbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1, 0,
                                 &vstage))
            break;
        if (!vk_buffer_write(e, &vstage, vertices, (size_t)vbytes))
            break;
        if (!vk_buffer_create_ex(
                e, vbytes,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                0, 1, &vbuf))
            break;
        /* Optional index staging + device-local AS build input. */
        if (indices) {
            if (!vk_buffer_create_ex(e, ibytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1,
                                     0, &istage))
                break;
            if (!vk_buffer_write(e, &istage, indices, (size_t)ibytes))
                break;
            if (!vk_buffer_create_ex(
                    e, ibytes,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    0, 1, &idxbuf))
                break;
        }
        /* One command buffer stages both vertex and (optional) index data. */
        VkCommandBuffer cb = vk_cmd_begin(e);
        if (!cb) break;
        VkBufferCopy vcopy = {0, 0, vbytes};
        vkCmdCopyBuffer(cb, vstage.buf, vbuf.buf, 1, &vcopy);
        if (indices) {
            VkBufferCopy icopy = {0, 0, ibytes};
            vkCmdCopyBuffer(cb, istage.buf, idxbuf.buf, 1, &icopy);
        }
        if (!vk_cmd_end_submit(e, cb)) break;
        uint64_t vaddr = vk_device_address(e, &vbuf);
        uint64_t idxaddr = indices ? vk_device_address(e, &idxbuf) : 0;

        VkAccelerationStructureGeometryKHR tgeom;
        memset(&tgeom, 0, sizeof(tgeom));
        tgeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        tgeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        tgeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        tgeom.geometry.triangles.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tgeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tgeom.geometry.triangles.vertexData.deviceAddress = vaddr;
        tgeom.geometry.triangles.vertexStride = 3u * sizeof(float);
        tgeom.geometry.triangles.maxVertex = nverts - 1u;
        if (indices) {
            tgeom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            tgeom.geometry.triangles.indexData.deviceAddress = idxaddr;
        } else {
            tgeom.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
        }
        if (!vk_build_accel(e, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                            &tgeom, ntris, &s->blas))
            break;

        VkAccelerationStructureInstanceKHR inst;
        memset(&inst, 0, sizeof(inst));
        inst.transform.matrix[0][0] = 1.0f;
        inst.transform.matrix[1][1] = 1.0f;
        inst.transform.matrix[2][2] = 1.0f;
        inst.mask = 0xFFu;
        inst.accelerationStructureReference = s->blas.address;
        if (!vk_buffer_create_ex(
                e, sizeof(inst),
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                1, 1, &instbuf))
            break;
        if (!vk_buffer_write(e, &instbuf, &inst, sizeof(inst))) break;
        uint64_t iaddr = vk_device_address(e, &instbuf);

        VkAccelerationStructureGeometryKHR igeom;
        memset(&igeom, 0, sizeof(igeom));
        igeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        igeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        igeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        igeom.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        igeom.geometry.instances.arrayOfPointers = VK_FALSE;
        igeom.geometry.instances.data.deviceAddress = iaddr;
        if (!vk_build_accel(e, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &igeom,
                            1, &s->tlas))
            break;
        ok = 1;
    } while (0);

    vk_buffer_destroy(e, &vstage);
    vk_buffer_destroy(e, &istage);
    vk_buffer_destroy(e, &vbuf);
    vk_buffer_destroy(e, &idxbuf);
    vk_buffer_destroy(e, &instbuf);
    if (!ok) {
        vk_accel_destroy(e, &s->tlas);
        vk_accel_destroy(e, &s->blas);
        free(s);
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return NULL;
    }
    if (err) *err = LRT_RESULT_OK;
    return s;
}

lrt_vk_rtx_scene *lrt_vk_rtx_scene_build(lrt_vk_engine *e, const float *vertices,
                                         uint32_t ntris, lrt_result *err) {
    return rtx_scene_build_core(e, vertices, ntris * 3u, NULL, ntris, err);
}

lrt_vk_rtx_scene *lrt_vk_rtx_scene_build_indexed(lrt_vk_engine *e,
                                                 const float *vertices,
                                                 uint32_t nverts,
                                                 const uint32_t *indices,
                                                 uint32_t ntris,
                                                 lrt_result *err) {
    if (!indices) {
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return NULL;
    }
    return rtx_scene_build_core(e, vertices, nverts, indices, ntris, err);
}

/* Grow the device-local + staging trace buffers to hold at least n rays. */
static int rtx_scene_ensure(lrt_vk_engine *e, lrt_vk_rtx_scene *s, uint32_t n) {
    if (s->rays_dev.buf && n <= s->cap) return 1;
    vk_buffer_destroy(e, &s->rays_dev);
    vk_buffer_destroy(e, &s->hits_dev);
    vk_buffer_destroy(e, &s->rays_stage);
    vk_buffer_destroy(e, &s->hits_stage);
    s->cap = 0;
    VkDeviceSize rb = (VkDeviceSize)n * sizeof(lrt_ray);
    VkDeviceSize hb = (VkDeviceSize)n * sizeof(lrt_hit);
    if (!vk_buffer_create_ex(e, rb,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             0, 0, &s->rays_dev) ||
        !vk_buffer_create_ex(e, hb,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             0, 0, &s->hits_dev) ||
        !vk_buffer_create_ex(e, rb, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1, 0,
                             &s->rays_stage) ||
        !vk_buffer_create_ex(e, hb, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1, 0,
                             &s->hits_stage))
        return 0;
    s->cap = n;
    return 1;
}

static void vk_buf_barrier(VkCommandBuffer cb, VkFlags src_stage, VkFlags dst_stage,
                           VkFlags src_access, VkFlags dst_access) {
    VkMemoryBarrier mb;
    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = src_access;
    mb.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 1, &mb, 0, NULL, 0, NULL);
}

int lrt_vk_rtx_scene_trace(lrt_vk_engine *e, lrt_vk_rtx_scene *s,
                           const lrt_ray *rays, uint32_t n, lrt_hit *out,
                           lrt_result *err) {
    if (!e || !s || (n && (!rays || !out))) {
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return -1;
    }
    if (n == 0) {
        if (err) *err = LRT_RESULT_OK;
        return 0;
    }
    vk_pipeline *pipe = rtx_get_pipeline(e);
    if (!pipe || !rtx_scene_ensure(e, s, n)) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    /* Host -> staging (bulk memcpy), then one command buffer does staging ->
     * device-local rays, dispatch (reads/writes VRAM), device-local hits ->
     * staging — all with barriers, a single submit. */
    if (!vk_buffer_write(e, &s->rays_stage, rays, (size_t)n * sizeof(lrt_ray))) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    VkDescriptorPool pool;
    VkDescriptorSet set;
    if (!vk_descriptors_bind_rtx(e, pipe->dsl, s->tlas.as, &s->rays_dev,
                                 &s->hits_dev, &pool, &set)) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    int run_ok = 0;
    VkCommandBuffer cb = vk_cmd_begin(e);
    if (cb != VK_NULL_HANDLE) {
        VkBufferCopy up = {0, 0, (VkDeviceSize)n * sizeof(lrt_ray)};
        vkCmdCopyBuffer(cb, s->rays_stage.buf, s->rays_dev.buf, 1, &up);
        vk_buf_barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        rtx_push pc;
        pc.ray_count = n;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->layout, 0,
                                1, &set, 0, NULL);
        vkCmdPushConstants(cb, pipe->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           (uint32_t)sizeof(pc), &pc);
        vkCmdDispatch(cb, (n + 63u) / 64u, 1, 1);
        vk_buf_barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferCopy down = {0, 0, (VkDeviceSize)n * sizeof(lrt_hit)};
        vkCmdCopyBuffer(cb, s->hits_dev.buf, s->hits_stage.buf, 1, &down);
        vk_buf_barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_HOST_READ_BIT);
        run_ok = vk_cmd_end_submit(e, cb);
    }
    vkDestroyDescriptorPool(e->device, pool, NULL);
    if (!run_ok) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }
    if (!vk_buffer_read(e, &s->hits_stage, out, (size_t)n * sizeof(lrt_hit))) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    int hits = 0;
    for (uint32_t i = 0; i < n; i++)
        if (out[i].prim_id != LRT_TRI_NO_HIT) hits++;
    if (err) *err = LRT_RESULT_OK;
    return hits;
}

void lrt_vk_rtx_scene_free(lrt_vk_engine *e, lrt_vk_rtx_scene *s) {
    if (!e || !s) return;
    vkDeviceWaitIdle(e->device);
    vk_buffer_destroy(e, &s->rays_dev);
    vk_buffer_destroy(e, &s->hits_dev);
    vk_buffer_destroy(e, &s->rays_stage);
    vk_buffer_destroy(e, &s->hits_stage);
    vk_accel_destroy(e, &s->tlas);
    vk_accel_destroy(e, &s->blas);
    free(s);
}

/* One-shot convenience: build a resident scene, trace once, free. NOTE: this
 * rebuilds the whole acceleration structure on every call — fine for a single
 * batch, but do NOT call it once per ray/pixel in a render loop. For many
 * batches against the same geometry, build a resident lrt_vk_rtx_scene once and
 * call lrt_vk_rtx_scene_trace per batch. */
int lrt_vk_trace_scene_rtx(lrt_vk_engine *e, const float *vertices, uint32_t ntris,
                           const lrt_ray *rays, uint32_t n, lrt_hit *out,
                           lrt_result *err) {
    lrt_vk_rtx_scene *s = lrt_vk_rtx_scene_build(e, vertices, ntris, err);
    if (!s) return -1;
    int rc = lrt_vk_rtx_scene_trace(e, s, rays, n, out, err);
    lrt_vk_rtx_scene_free(e, s);
    return rc;
}

/* One-shot convenience for indexed geometry (see lrt_vk_rtx_scene_build_indexed).
 * Same per-call AS rebuild caveat as lrt_vk_trace_scene_rtx above. */
int lrt_vk_trace_scene_rtx_indexed(lrt_vk_engine *e, const float *vertices,
                                   uint32_t nverts, const uint32_t *indices,
                                   uint32_t ntris, const lrt_ray *rays, uint32_t n,
                                   lrt_hit *out, lrt_result *err) {
    lrt_vk_rtx_scene *s =
        lrt_vk_rtx_scene_build_indexed(e, vertices, nverts, indices, ntris, err);
    if (!s) return -1;
    int rc = lrt_vk_rtx_scene_trace(e, s, rays, n, out, err);
    lrt_vk_rtx_scene_free(e, s);
    return rc;
}
