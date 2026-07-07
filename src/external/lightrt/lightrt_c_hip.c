/*
 * lightrt_c_hip.c — HIP/ROCm backend for the LightRT triangle kernel (Path A).
 *
 * Mirrors lightrt_c_vk.c's lrt_vk_trace_scene: serialize the CPU-built scene with
 * lrt_tri_scene_save_to_memory(), upload the node/block regions verbatim, and run
 * a HIP kernel (trace_bvh_hip.h, a port of trace_bvh.comp) that walks the same
 * memory image. HIP + hiprtc are loaded at runtime via hipew (no SDK link).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightrt_c_hip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hipew.h"
#include "hip/trace_bvh_hip.h"

/* On-disk / in-memory LRTS header produced by lrt_tri_scene_save_to_memory().
 * Layout-identical to the replica used by lightrt_c_vk.c. */
typedef struct hip_lrts_header {
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
} hip_lrts_header;

struct lrt_hip_engine {
    int device;
    hipModule_t module;
    hipFunction_t kernel;
    char device_name[256];
    char err[512];
};

/* trace_stack_for: matches lightrt_c_vk.c. The HIP kernel uses a fixed 256-entry
 * stack, so any bucket <= 256 is fine; deeper trees are rejected. */
static uint32_t hip_trace_stack_for(uint32_t max_depth, uint32_t w) {
    uint32_t need = max_depth * (w - 1u) + w + 1u;
    static const uint32_t buckets[] = {32u, 64u, 128u, 256u};
    for (int i = 0; i < 4; i++)
        if (need <= buckets[i]) return buckets[i];
    return 0;
}

static void hip_set_err(lrt_hip_engine *e, const char *msg) {
    if (!e) return;
    snprintf(e->err, sizeof(e->err), "%s", msg ? msg : "");
}

static int hip_ok(hipError_t r) { return r == hipSuccess; }

lrt_hip_engine *lrt_hip_engine_create(const lrt_hip_engine_options *opts,
                                      lrt_result *err) {
    if (hipewInit(HIPEW_INIT_HIP | HIPEW_INIT_HIPRTC) != HIPEW_SUCCESS) {
        if (err) *err = LRT_RESULT_UNSUPPORTED;
        return NULL;
    }

    lrt_hip_engine *e = (lrt_hip_engine *)calloc(1, sizeof(lrt_hip_engine));
    if (!e) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return NULL;
    }

    if (!hip_ok(hipInit(0))) {
        hip_set_err(e, "hipInit failed");
        goto fail;
    }
    int count = 0;
    if (!hip_ok(hipGetDeviceCount(&count)) || count <= 0) {
        hip_set_err(e, "no HIP device");
        goto fail;
    }
    int idx = (opts && opts->device_index >= 0) ? opts->device_index : 0;
    if (idx >= count) idx = 0;
    if (!hip_ok(hipSetDevice(idx))) {
        hip_set_err(e, "hipSetDevice failed");
        goto fail;
    }
    e->device = idx;

    hipDevice_t dev = 0;
    if (hip_ok(hipDeviceGet(&dev, idx)) &&
        hip_ok(hipDeviceGetName(e->device_name, (int)sizeof(e->device_name),
                                dev))) {
        /* device_name populated */
    } else {
        snprintf(e->device_name, sizeof(e->device_name), "HIP device %d", idx);
    }

    /* Compile the trace kernel with hiprtc. No --offload-arch is passed: hiprtc
     * targets the current device by default. */
    hiprtcProgram prog = NULL;
    if (hiprtcCreateProgram(&prog, k_lrt_hip_trace_bvh_src,
                            "lrt_hip_trace_bvh.hip", 0, NULL, NULL) !=
        HIPRTC_SUCCESS) {
        hip_set_err(e, "hiprtcCreateProgram failed");
        goto fail;
    }
    hiprtcResult cres = hiprtcCompileProgram(prog, 0, NULL);
    if (cres != HIPRTC_SUCCESS) {
        size_t log_n = 0;
        hiprtcGetProgramLogSize(prog, &log_n);
        char *log = (log_n > 1) ? (char *)malloc(log_n) : NULL;
        if (log) {
            hiprtcGetProgramLog(prog, log);
            snprintf(e->err, sizeof(e->err), "hiprtc compile failed: %s", log);
            free(log);
        } else {
            hip_set_err(e, "hiprtc compile failed");
        }
        hiprtcDestroyProgram(&prog);
        goto fail;
    }
    size_t code_n = 0;
    if (hiprtcGetCodeSize(prog, &code_n) != HIPRTC_SUCCESS || code_n == 0) {
        hip_set_err(e, "hiprtcGetCodeSize failed");
        hiprtcDestroyProgram(&prog);
        goto fail;
    }
    char *code = (char *)malloc(code_n);
    if (!code) {
        hiprtcDestroyProgram(&prog);
        hip_set_err(e, "out of memory (code object)");
        goto fail;
    }
    if (hiprtcGetCode(prog, code) != HIPRTC_SUCCESS) {
        free(code);
        hiprtcDestroyProgram(&prog);
        hip_set_err(e, "hiprtcGetCode failed");
        goto fail;
    }
    hiprtcDestroyProgram(&prog);

    if (!hip_ok(hipModuleLoadData(&e->module, code))) {
        free(code);
        hip_set_err(e, "hipModuleLoadData failed");
        goto fail;
    }
    free(code);
    if (!hip_ok(hipModuleGetFunction(&e->kernel, e->module,
                                     "lrt_hip_trace_bvh"))) {
        hip_set_err(e, "hipModuleGetFunction failed");
        goto fail;
    }

    if (err) *err = LRT_RESULT_OK;
    return e;

fail:
    if (err) *err = LRT_RESULT_UNSUPPORTED;
    /* Surface the reason to stderr before freeing — engine-create returns NULL
     * (like the Vulkan backend) so the detailed string would otherwise be lost. */
    if (e->err[0]) fprintf(stderr, "lightrt-hip: %s\n", e->err);
    if (e->module) hipModuleUnload(e->module);
    free(e);
    return NULL;
}

void lrt_hip_engine_destroy(lrt_hip_engine *e) {
    if (!e) return;
    if (e->module) hipModuleUnload(e->module);
    free(e);
}

uint32_t lrt_hip_engine_caps(const lrt_hip_engine *e) {
    return e ? LRT_HIP_CAP_COMPUTE : 0u;
}

const char *lrt_hip_engine_device_name(const lrt_hip_engine *e) {
    return e ? e->device_name : "";
}

const char *lrt_hip_engine_last_error(const lrt_hip_engine *e) {
    return e ? e->err : "";
}

int lrt_hip_trace_scene(lrt_hip_engine *e, const lrt_tri_scene *s,
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
        hip_set_err(e, "scene not GPU-traceable (quantized/curve/user?)");
        if (err) *err = sr;
        return -1;
    }
    const hip_lrts_header *h = (const hip_lrts_header *)blob;
    if (h->prim_kind != 0u /* TRI */) {
        free(blob);
        hip_set_err(e, "only triangle scenes are GPU-traceable");
        if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
        return -1;
    }
    uint32_t w = h->layout; /* 4 or 8 */

    lrt_tri_stats st;
    lrt_tri_scene_stats(s, &st);
    uint32_t stack = hip_trace_stack_for(st.max_depth, w);
    if (stack == 0) {
        hip_set_err(e, "BVH too deep for the GPU compute stack");
        free(blob);
        if (err) *err = LRT_RESULT_TRAVERSAL_OVERFLOW;
        return -1;
    }

    size_t nodes_bytes = (size_t)h->node_count * h->node_stride;
    size_t blocks_bytes = (size_t)h->block_count * h->block_stride;
    size_t rays_bytes = (size_t)n * sizeof(lrt_ray);
    size_t hits_bytes = (size_t)n * sizeof(lrt_hit);

    hipDeviceptr_t d_nodes = NULL, d_blocks = NULL, d_rays = NULL, d_hits = NULL;
    int failed = 0;
    if (!hip_ok(hipMalloc(&d_nodes, nodes_bytes ? nodes_bytes : 1)) ||
        !hip_ok(hipMalloc(&d_blocks, blocks_bytes ? blocks_bytes : 1)) ||
        !hip_ok(hipMalloc(&d_rays, rays_bytes)) ||
        !hip_ok(hipMalloc(&d_hits, hits_bytes))) {
        hip_set_err(e, "hipMalloc failed");
        failed = 1;
        goto cleanup;
    }
    if (!hip_ok(hipMemcpyHtoD(d_nodes, (const char *)blob + h->node_offset,
                              nodes_bytes)) ||
        !hip_ok(hipMemcpyHtoD(d_blocks, (const char *)blob + h->block_offset,
                              blocks_bytes)) ||
        !hip_ok(hipMemcpyHtoD(d_rays, rays, rays_bytes))) {
        hip_set_err(e, "host->device copy failed");
        failed = 1;
        goto cleanup;
    }

    {
        uint32_t root = h->root;
        uint32_t ray_count = n;
        uint32_t kw = w;
        uint32_t kstack = stack;
        void *params[] = {&d_nodes, &d_blocks, &d_rays, &d_hits,
                          &root,    &ray_count, &kw,     &kstack};
        unsigned int groups = (n + 63u) / 64u;
        if (!hip_ok(hipModuleLaunchKernel(e->kernel, groups, 1, 1, 64, 1, 1, 0,
                                          NULL, params, NULL))) {
            hip_set_err(e, "hipModuleLaunchKernel failed");
            failed = 1;
            goto cleanup;
        }
        if (!hip_ok(hipDeviceSynchronize())) {
            hip_set_err(e, "hipDeviceSynchronize failed");
            failed = 1;
            goto cleanup;
        }
    }

    if (!hip_ok(hipMemcpyDtoH(out, d_hits, hits_bytes))) {
        hip_set_err(e, "device->host copy failed");
        failed = 1;
        goto cleanup;
    }

cleanup:
    if (d_nodes) hipFree(d_nodes);
    if (d_blocks) hipFree(d_blocks);
    if (d_rays) hipFree(d_rays);
    if (d_hits) hipFree(d_hits);
    free(blob);
    if (failed) {
        if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
        return -1;
    }

    int hits = 0;
    for (uint32_t i = 0; i < n; i++)
        if (out[i].prim_id != LRT_TRI_NO_HIT) hits++;
    if (err) *err = LRT_RESULT_OK;
    return hits;
}
