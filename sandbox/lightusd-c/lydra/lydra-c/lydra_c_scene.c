/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024 Light Transport Entertainment Inc.
 *
 * lydra-c scene module — pure C11 implementation.
 *
 * Port of lydra_scene.cc: value materialization from LusdLayer_T's raw buffer.
 */
#include "lydra_c_scene.h"

#include "internal/lusd_layer_internal.h"
#include "internal/lusd_value_rep.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Format tags */
#ifndef LUSD_FORMAT_USDC
#  define LUSD_FORMAT_USDC 0
#  define LUSD_FORMAT_USDA 1
#endif

/* LZ4 decompressor (from lightusd-c/src/lusd_lz4.c) */
extern uint64_t lusd__lz4_decompress(const uint8_t* comp, uint64_t comp_size,
                                     uint8_t* out, uint64_t max_out);

/* Integer decompression (from lusd_usdc_reader.c) */
extern int lusd__decode_compressed_int_array(const uint8_t* data, uint64_t data_size,
                                              uint64_t offset,
                                              int32_t** out_data, uint64_t* out_count);

/* ================================================================
 * Helpers
 * ================================================================ */

static int is_usda_layer(const LusdLayer_T* L) {
    return L && L->format == LUSD_FORMAT_USDA;
}

static LusdValueRep find_field(const LusdLayer_T* L, const LusdPrim_T* P,
                               const char* name) {
    return lusd__layer_find_field(L, P, name);
}

/* ================================================================
 * USDA text array parser
 * ================================================================ */

static const uint8_t* read_usda_array_bytes(
        const LusdLayer_T* L, uint64_t text_offset,
        size_t element_size, int as_int,
        uint64_t* count_out, uint8_t** tmp_buf_out) {
    *count_out   = 0;
    *tmp_buf_out = NULL;

    if (!L || !L->file_data) return NULL;
    if (text_offset >= L->file_size) return NULL;

    const char* p   = (const char*)L->file_data + text_offset;
    const char* end = (const char*)L->file_data + L->file_size;

    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end || *p != '[') return NULL;
    p++;

    /* Check for empty array */
    {
        const char* scan = p;
        while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\n' || *scan == '\r')) scan++;
        if (scan >= end || *scan == ']') { *count_out = 0; return NULL; }
    }

    /* First pass: count elements */
    uint64_t count = 1;
    {
        const char* scan = p;
        int depth = 0;
        while (scan < end) {
            char c = *scan++;
            if (c == '(' || c == '[') depth++;
            else if (c == ')') { if (depth > 0) depth--; }
            else if (c == ']') { if (depth == 0) break; depth--; }
            else if (c == ',' && depth == 0) count++;
        }
    }

    size_t total_bytes = (size_t)count * element_size;
    uint8_t* buf = (uint8_t*)malloc(total_bytes);
    if (!buf) return NULL;
    memset(buf, 0, total_bytes);

    /* Second pass: parse values */
    uint64_t elem = 0;
    while (p < end && elem < count) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;
        if (p >= end || *p == ']') break;

        if (element_size == sizeof(int32_t) && as_int) {
            char* endp;
            long v = strtol(p, &endp, 10);
            int32_t iv = (int32_t)v;
            memcpy(buf + elem * sizeof(int32_t), &iv, sizeof(int32_t));
            p = endp;
        } else if (element_size == sizeof(float)) {
            char* endp;
            float fv = strtof(p, &endp);
            memcpy(buf + elem * sizeof(float), &fv, sizeof(float));
            p = endp;
        } else {
            if (*p == '(') p++;
            uint32_t ncomp = (uint32_t)(element_size / sizeof(float));
            for (uint32_t comp = 0; comp < ncomp; comp++) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
                char* endp;
                float fv = strtof(p, &endp);
                size_t dst_off = (size_t)elem * element_size + comp * sizeof(float);
                memcpy(buf + dst_off, &fv, sizeof(float));
                p = endp;
            }
            while (p < end && *p != ')' && *p != ',' && *p != ']') p++;
            if (p < end && *p == ')') p++;
        }
        elem++;
    }

    *count_out   = elem;
    *tmp_buf_out = buf;
    return buf;
}

/* ================================================================
 * USDC binary array reader
 * ================================================================ */

static const uint8_t* read_array_bytes(const LusdLayer_T* L,
                                        uint64_t offset, int is_compressed,
                                        size_t element_size,
                                        uint64_t* count_out,
                                        uint8_t** tmp_buf) {
    *tmp_buf   = NULL;
    *count_out = 0;

    if (offset + 8 > L->file_size) return NULL;

    uint64_t numElements;
    memcpy(&numElements, L->file_data + offset, 8);
    offset += 8;

    if (numElements == 0) { *count_out = 0; return NULL; }

    uint64_t uncompressed_bytes = numElements * element_size;

    if (is_compressed) {
        if (offset + 8 > L->file_size) return NULL;
        uint64_t compByteSize;
        memcpy(&compByteSize, L->file_data + offset, 8);
        offset += 8;

        if (compByteSize == 0 || offset + compByteSize > L->file_size) return NULL;

        uint8_t* out = (uint8_t*)malloc((size_t)uncompressed_bytes);
        if (!out) return NULL;

        uint64_t nDec = lusd__lz4_decompress(L->file_data + offset,
                                              compByteSize, out,
                                              uncompressed_bytes);
        if (nDec != uncompressed_bytes) { free(out); return NULL; }

        *tmp_buf   = out;
        *count_out = numElements;
        return out;
    } else {
        if (offset + uncompressed_bytes > L->file_size) return NULL;
        *count_out = numElements;
        return L->file_data + offset;
    }
}

/* ================================================================
 * Time-samples helpers
 * ================================================================ */

static uint32_t ts_start(uint64_t payload) { return (uint32_t)(payload >> 24); }
static uint32_t ts_count(uint64_t payload) { return (uint32_t)(payload & 0xFFFFFFu); }

static const LusdTimeSample* find_time_sample(const LusdLayer_T* L,
                                               uint64_t payload,
                                               double time_code) {
    uint32_t start = ts_start(payload);
    uint32_t count = ts_count(payload);
    if (count == 0 || !L->time_samples) return NULL;

    const LusdTimeSample* best = &L->time_samples[start];
    for (uint32_t i = 0; i < count; i++) {
        const LusdTimeSample* s = &L->time_samples[start + i];
        if (s->time <= time_code)
            best = s;
        else if (i == 0)
            best = s;
        else
            break;
    }
    uint32_t canon = best->canonical_idx;
    if (canon < start || canon >= start + count)
        canon = (uint32_t)(best - L->time_samples);
    return &L->time_samples[canon];
}

/* ================================================================
 * materialize_* — read typed arrays from layer
 *
 * Returns malloc'd buffer via *out_data and count via *out_count.
 * Returns 0 on success, -1 on failure. Caller must free *out_data.
 * ================================================================ */

static int materialize_float3_array_at(const LusdLayer_T* L, LusdValueRep rep,
                                       double time_code,
                                       float** out_data, uint64_t* out_count) {
    *out_data = NULL; *out_count = 0;
    if (!L || rep.data == 0) return -1;

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return -1;

    uint64_t count = 0;
    uint8_t* tmp = NULL;
    const uint8_t* src = read_usda_array_bytes(L, s->text_offset,
                                                3 * sizeof(float), 0,
                                                &count, &tmp);
    if (!src) { free(tmp); return -1; }
    float* result = (float*)malloc((size_t)count * 3 * sizeof(float));
    if (!result) { free(tmp); return -1; }
    if (count > 0) memcpy(result, src, count * 3 * sizeof(float));
    free(tmp);
    *out_data = result; *out_count = count;
    return 0;
}

static int materialize_float3_array(const LusdLayer_T* L, LusdValueRep rep,
                                    float** out_data, uint64_t* out_count) {
    *out_data = NULL; *out_count = 0;
    if (!L || rep.data == 0) return -1;

    /* Transparent forward for time-sampled attributes */
    if ((int)((rep.data & LUSD_VREP_TYPE_MASK) >> LUSD_VREP_TYPE_SHIFT)
            == LUSD_CRATE_TIME_SAMPLES) {
        return materialize_float3_array_at(L, rep, 0.0, out_data, out_count);
    }

    if (!(rep.data & LUSD_VREP_IS_ARRAY_BIT)) return -1;
    if (rep.data & LUSD_VREP_IS_INLINED_BIT) return -1;

    uint64_t offset     = rep.data & LUSD_VREP_PAYLOAD_MASK;
    int      compressed = (rep.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0;

    uint64_t count = 0;
    uint8_t* tmp = NULL;
    const uint8_t* src;
    if (is_usda_layer(L))
        src = read_usda_array_bytes(L, offset, 3 * sizeof(float), 0, &count, &tmp);
    else
        src = read_array_bytes(L, offset, compressed, 3 * sizeof(float), &count, &tmp);

    if (!src && count != 0) { free(tmp); return -1; }

    float* result = (float*)malloc((size_t)count * 3 * sizeof(float));
    if (!result) { free(tmp); return -1; }
    if (count > 0) memcpy(result, src, count * 3 * sizeof(float));
    free(tmp);
    *out_data = result; *out_count = count;
    return 0;
}

static int materialize_float2_array(const LusdLayer_T* L, LusdValueRep rep,
                                    float** out_data, uint64_t* out_count) {
    *out_data = NULL; *out_count = 0;
    if (!L || rep.data == 0) return -1;
    if (!(rep.data & LUSD_VREP_IS_ARRAY_BIT)) return -1;
    if (rep.data & LUSD_VREP_IS_INLINED_BIT) return -1;

    uint64_t offset     = rep.data & LUSD_VREP_PAYLOAD_MASK;
    int      compressed = (rep.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0;

    uint64_t count = 0;
    uint8_t* tmp = NULL;
    const uint8_t* src;
    if (is_usda_layer(L))
        src = read_usda_array_bytes(L, offset, 2 * sizeof(float), 0, &count, &tmp);
    else
        src = read_array_bytes(L, offset, compressed, 2 * sizeof(float), &count, &tmp);

    if (!src && count != 0) { free(tmp); return -1; }

    float* result = (float*)malloc((size_t)count * 2 * sizeof(float));
    if (!result) { free(tmp); return -1; }
    if (count > 0) memcpy(result, src, count * 2 * sizeof(float));
    free(tmp);
    *out_data = result; *out_count = count;
    return 0;
}

static int materialize_int_array(const LusdLayer_T* L, LusdValueRep rep,
                                 int32_t** out_data, uint64_t* out_count) {
    *out_data = NULL; *out_count = 0;
    if (!L || rep.data == 0) return -1;
    if (!(rep.data & LUSD_VREP_IS_ARRAY_BIT)) return -1;
    if (rep.data & LUSD_VREP_IS_INLINED_BIT) return -1;

    uint64_t offset     = rep.data & LUSD_VREP_PAYLOAD_MASK;
    int      compressed = (rep.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0;

    if (is_usda_layer(L)) {
        uint64_t count = 0;
        uint8_t* tmp = NULL;
        const uint8_t* src = read_usda_array_bytes(L, offset, sizeof(int32_t), 1, &count, &tmp);
        if (!src && count != 0) { free(tmp); return -1; }
        int32_t* result = (int32_t*)malloc((size_t)count * sizeof(int32_t));
        if (!result) { free(tmp); return -1; }
        if (count > 0) memcpy(result, src, count * sizeof(int32_t));
        free(tmp);
        *out_data = result; *out_count = count;
        return 0;
    }

    /* USDC: compressed int arrays use Crate integer compression, not raw LZ4 */
    if (compressed) {
        return lusd__decode_compressed_int_array(L->file_data, L->file_size,
                                                  offset, out_data, out_count);
    }

    /* Uncompressed USDC int array */
    uint64_t count = 0;
    uint8_t* tmp = NULL;
    const uint8_t* src = read_array_bytes(L, offset, 0, sizeof(int32_t), &count, &tmp);
    if (!src && count != 0) { free(tmp); return -1; }
    int32_t* result = (int32_t*)malloc((size_t)count * sizeof(int32_t));
    if (!result) { free(tmp); return -1; }
    if (count > 0) memcpy(result, src, count * sizeof(int32_t));
    free(tmp);
    *out_data = result; *out_count = count;
    return 0;
}

/* ================================================================
 * Scalar value materialization (for UsdPreviewSurface attrs)
 * ================================================================ */

static int materialize_float(const LusdLayer_T* L, LusdValueRep rep, float* out) {
    if (!L || rep.data == 0) return -1;
    int type_id = lusd_vrep_type(rep);

    if (lusd_vrep_is_inlined(rep)) {
        if (type_id == LUSD_CRATE_FLOAT) {
            uint32_t bits = (uint32_t)(rep.data & LUSD_VREP_PAYLOAD_MASK);
            memcpy(out, &bits, sizeof(float));
            return 0;
        }
        if (type_id == LUSD_CRATE_DOUBLE) {
            uint64_t bits = rep.data & LUSD_VREP_PAYLOAD_MASK;
            double d;
            memcpy(&d, &bits, sizeof(double));
            *out = (float)d;
            return 0;
        }
        if (type_id == LUSD_CRATE_INT) {
            int32_t iv = (int32_t)(rep.data & LUSD_VREP_PAYLOAD_MASK);
            *out = (float)iv;
            return 0;
        }
        return -1;
    }

    /* Non-inlined: read from file offset */
    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        /* USDA: parse text at offset */
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        char* endp;
        *out = strtof(p, &endp);
        return (endp != p) ? 0 : -1;
    }

    if (type_id == LUSD_CRATE_FLOAT) {
        if (offset + 4 > L->file_size) return -1;
        memcpy(out, L->file_data + offset, sizeof(float));
        return 0;
    }
    if (type_id == LUSD_CRATE_DOUBLE) {
        if (offset + 8 > L->file_size) return -1;
        double d;
        memcpy(&d, L->file_data + offset, sizeof(double));
        *out = (float)d;
        return 0;
    }
    return -1;
}

static int materialize_float3(const LusdLayer_T* L, LusdValueRep rep, float out[3]) {
    if (!L || rep.data == 0) return -1;

    /* float3 is never inlined (12 bytes > 6 byte payload) */
    if (lusd_vrep_is_inlined(rep)) return -1;

    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        /* USDA: parse (x, y, z) text at offset */
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '(')) p++;
        for (int i = 0; i < 3; i++) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
            char* endp;
            out[i] = strtof(p, &endp);
            if (endp == p) return -1;
            p = endp;
        }
        return 0;
    }

    int type_id = lusd_vrep_type(rep);
    if (type_id == LUSD_CRATE_VEC3F || type_id == LUSD_CRATE_FLOAT) {
        if (offset + 12 > L->file_size) return -1;
        memcpy(out, L->file_data + offset, 12);
        return 0;
    }
    if (type_id == LUSD_CRATE_VEC3D) {
        if (offset + 24 > L->file_size) return -1;
        double d[3];
        memcpy(d, L->file_data + offset, 24);
        out[0] = (float)d[0]; out[1] = (float)d[1]; out[2] = (float)d[2];
        return 0;
    }
    return -1;
}

static const char* materialize_token(const LusdLayer_T* L, LusdValueRep rep) {
    if (!L || rep.data == 0) return NULL;
    if (lusd_vrep_is_inlined(rep)) {
        uint32_t ti = (uint32_t)(rep.data & LUSD_VREP_PAYLOAD_MASK);
        if (ti < L->token_count) return L->tokens[ti];
        return NULL;
    }
    /* Non-inlined token: read token index from file */
    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        /* For USDA, non-inlined tokens aren't common, but handle gracefully */
        return NULL;
    }

    if (offset + 4 > L->file_size) return NULL;
    uint32_t ti;
    memcpy(&ti, L->file_data + offset, sizeof(uint32_t));
    if (ti < L->token_count) return L->tokens[ti];
    return NULL;
}

/* ================================================================
 * material:binding resolution
 * ================================================================ */

/* Forward declaration (implemented in lusd_usdc_reader.c) */
extern const char* lusd__find_relationship_target(const LusdLayer_T* layer,
                                                   const LusdPrim_T* prim,
                                                   const char* rel_name);

/* Forward declaration (defined later in this file) */
static const char* materialize_asset_path(const LusdLayer_T* L, LusdValueRep rep);

/* Forward declaration (implemented in lusd_usdc_reader.c) */
extern const char* lusd__find_connection_target(const LusdLayer_T* layer,
                                                 const LusdPrim_T* prim,
                                                 const char* attr_name);

const char* lydra_c_resolve_material_binding(LusdLayer layer, LusdPrim prim) {
    if (!layer || !prim) return NULL;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    return lusd__find_relationship_target(L, P, "material:binding");
}

/* ================================================================
 * UsdPreviewSurface extraction
 * ================================================================ */

static void init_material_defaults(LydraCMaterialData* m) {
    m->diffuse_color[0] = 0.18f; m->diffuse_color[1] = 0.18f; m->diffuse_color[2] = 0.18f;
    m->metallic = 0.0f;
    m->roughness = 0.5f;
    m->ior = 1.5f;
    m->opacity = 1.0f;
    m->specular_color[0] = 0.0f; m->specular_color[1] = 0.0f; m->specular_color[2] = 0.0f;
    m->clearcoat = 0.0f;
    m->clearcoat_roughness = 0.01f;
    m->emissive_color[0] = 0.0f; m->emissive_color[1] = 0.0f; m->emissive_color[2] = 0.0f;
}

/* Find a descendant prim by type (recursive DFS) */
static const LusdPrim_T* find_descendant_by_type(const LusdLayer_T* L,
                                                   const LusdPrim_T* parent,
                                                   const char* type_name) {
    for (uint32_t i = 0; i < parent->child_count; i++) {
        uint32_t idx = parent->child_spec_indices[i];
        if (idx >= L->prim_node_count) continue;
        const LusdPrim_T* child = &L->prim_nodes[idx];
        if (child->type_name && strcmp(child->type_name, type_name) == 0)
            return child;
        /* Recurse into children (e.g. Material → Scope → Shader) */
        const LusdPrim_T* found = find_descendant_by_type(L, child, type_name);
        if (found) return found;
    }
    return NULL;
}

/* Try to read a shader attribute: first as scalar, then as "inputs:name" */
static void read_shader_float(const LusdLayer_T* L, const LusdPrim_T* shader,
                               const char* name, float* out) {
    LusdValueRep rep = find_field(L, shader, name);
    if (!lusd_vrep_is_null(rep)) materialize_float(L, rep, out);
}

static void read_shader_float3(const LusdLayer_T* L, const LusdPrim_T* shader,
                                const char* name, float out[3]) {
    LusdValueRep rep = find_field(L, shader, name);
    if (!lusd_vrep_is_null(rep)) materialize_float3(L, rep, out);
}

LusdResult lydra_c_extract_material(LusdLayer layer, LusdPrim material_prim,
                                     LydraCMaterialData* out) {
    if (!layer || !material_prim || !out)
        return LUSD_ERROR_INVALID_HANDLE;

    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)material_prim;

    init_material_defaults(out);

    /* Find Shader descendant prim (may be nested under Scope etc.) */
    const LusdPrim_T* shader = find_descendant_by_type(L, P, "Shader");
    if (!shader) {
        /* No Shader child — return defaults (still success, just default mat) */
        return LUSD_SUCCESS;
    }

    /* Check info:id == "UsdPreviewSurface" */
    {
        LusdValueRep id_rep = find_field(L, shader, "info:id");
        if (!lusd_vrep_is_null(id_rep)) {
            const char* shader_id = materialize_token(L, id_rep);
            if (!shader_id || strcmp(shader_id, "UsdPreviewSurface") != 0) {
                /* Not UsdPreviewSurface — return defaults */
                return LUSD_SUCCESS;
            }
        }
    }

    /* Read UsdPreviewSurface inputs */
    read_shader_float3(L, shader, "inputs:diffuseColor", out->diffuse_color);
    read_shader_float(L, shader, "inputs:metallic", &out->metallic);
    read_shader_float(L, shader, "inputs:roughness", &out->roughness);
    read_shader_float(L, shader, "inputs:ior", &out->ior);
    read_shader_float(L, shader, "inputs:opacity", &out->opacity);
    read_shader_float3(L, shader, "inputs:specularColor", out->specular_color);
    read_shader_float(L, shader, "inputs:clearcoat", &out->clearcoat);
    read_shader_float(L, shader, "inputs:clearcoatRoughness", &out->clearcoat_roughness);
    read_shader_float3(L, shader, "inputs:emissiveColor", out->emissive_color);

    return LUSD_SUCCESS;
}

/* ================================================================
 * OpenPBR material extraction
 * ================================================================ */

static void init_openpbr_defaults(LydraCOpenPBRData* m) {
    /* Base */
    m->base_weight = 1.0f;
    m->base_color[0] = 0.8f; m->base_color[1] = 0.8f; m->base_color[2] = 0.8f;
    m->base_roughness = 0.0f;
    m->base_metalness = 0.0f;
    m->base_diffuse_roughness = 0.0f;
    /* Specular */
    m->specular_weight = 1.0f;
    m->specular_color[0] = 1.0f; m->specular_color[1] = 1.0f; m->specular_color[2] = 1.0f;
    m->specular_roughness = 0.3f;
    m->specular_ior = 1.5f;
    m->specular_ior_level = 0.5f;
    m->specular_anisotropy = 0.0f;
    m->specular_rotation = 0.0f;
    /* Transmission */
    m->transmission_weight = 0.0f;
    m->transmission_color[0] = 1.0f; m->transmission_color[1] = 1.0f; m->transmission_color[2] = 1.0f;
    m->transmission_depth = 0.0f;
    m->transmission_scatter[0] = 0.0f; m->transmission_scatter[1] = 0.0f; m->transmission_scatter[2] = 0.0f;
    m->transmission_scatter_anisotropy = 0.0f;
    m->transmission_dispersion = 0.0f;
    /* Subsurface */
    m->subsurface_weight = 0.0f;
    m->subsurface_color[0] = 0.8f; m->subsurface_color[1] = 0.8f; m->subsurface_color[2] = 0.8f;
    m->subsurface_radius = 1.0f;
    m->subsurface_radius_scale[0] = 1.0f; m->subsurface_radius_scale[1] = 1.0f; m->subsurface_radius_scale[2] = 1.0f;
    m->subsurface_scale = 1.0f;
    m->subsurface_anisotropy = 0.0f;
    /* Sheen */
    m->sheen_weight = 0.0f;
    m->sheen_color[0] = 1.0f; m->sheen_color[1] = 1.0f; m->sheen_color[2] = 1.0f;
    m->sheen_roughness = 0.3f;
    /* Fuzz */
    m->fuzz_weight = 0.0f;
    m->fuzz_color[0] = 1.0f; m->fuzz_color[1] = 1.0f; m->fuzz_color[2] = 1.0f;
    m->fuzz_roughness = 0.5f;
    /* Thin Film */
    m->thin_film_weight = 0.0f;
    m->thin_film_thickness = 0.5f;
    m->thin_film_ior = 1.5f;
    /* Coat */
    m->coat_weight = 0.0f;
    m->coat_color[0] = 1.0f; m->coat_color[1] = 1.0f; m->coat_color[2] = 1.0f;
    m->coat_roughness = 0.1f;
    m->coat_anisotropy = 0.0f;
    m->coat_rotation = 0.0f;
    m->coat_ior = 1.6f;
    m->coat_affect_color[0] = 0.0f; m->coat_affect_color[1] = 0.0f; m->coat_affect_color[2] = 0.0f;
    m->coat_affect_roughness = 0.0f;
    /* Emission */
    m->emission_luminance = 0.0f;
    m->emission_color[0] = 0.0f; m->emission_color[1] = 0.0f; m->emission_color[2] = 0.0f;
    /* Geometry */
    m->opacity = 1.0f;
    m->is_openpbr = 0;
    /* Texture paths */
    m->base_color_tex = NULL;
    m->metalness_tex = NULL;
    m->roughness_tex = NULL;
    m->normal_tex = NULL;
    m->emissive_tex = NULL;
    m->opacity_tex = NULL;
}

static void read_openpbr_inputs(const LusdLayer_T* L, const LusdPrim_T* shader,
                                 LydraCOpenPBRData* out) {
    /* Base */
    read_shader_float(L, shader, "inputs:base_weight", &out->base_weight);
    read_shader_float3(L, shader, "inputs:base_color", out->base_color);
    read_shader_float(L, shader, "inputs:base_roughness", &out->base_roughness);
    read_shader_float(L, shader, "inputs:base_metalness", &out->base_metalness);
    read_shader_float(L, shader, "inputs:base_diffuse_roughness", &out->base_diffuse_roughness);
    /* Specular */
    read_shader_float(L, shader, "inputs:specular_weight", &out->specular_weight);
    read_shader_float3(L, shader, "inputs:specular_color", out->specular_color);
    read_shader_float(L, shader, "inputs:specular_roughness", &out->specular_roughness);
    read_shader_float(L, shader, "inputs:specular_ior", &out->specular_ior);
    read_shader_float(L, shader, "inputs:specular_ior_level", &out->specular_ior_level);
    read_shader_float(L, shader, "inputs:specular_anisotropy", &out->specular_anisotropy);
    read_shader_float(L, shader, "inputs:specular_rotation", &out->specular_rotation);
    /* Transmission */
    read_shader_float(L, shader, "inputs:transmission_weight", &out->transmission_weight);
    read_shader_float3(L, shader, "inputs:transmission_color", out->transmission_color);
    read_shader_float(L, shader, "inputs:transmission_depth", &out->transmission_depth);
    read_shader_float3(L, shader, "inputs:transmission_scatter", out->transmission_scatter);
    read_shader_float(L, shader, "inputs:transmission_scatter_anisotropy", &out->transmission_scatter_anisotropy);
    read_shader_float(L, shader, "inputs:transmission_dispersion", &out->transmission_dispersion);
    /* Subsurface */
    read_shader_float(L, shader, "inputs:subsurface_weight", &out->subsurface_weight);
    read_shader_float3(L, shader, "inputs:subsurface_color", out->subsurface_color);
    read_shader_float(L, shader, "inputs:subsurface_radius", &out->subsurface_radius);
    read_shader_float3(L, shader, "inputs:subsurface_radius_scale", out->subsurface_radius_scale);
    read_shader_float(L, shader, "inputs:subsurface_scale", &out->subsurface_scale);
    read_shader_float(L, shader, "inputs:subsurface_anisotropy", &out->subsurface_anisotropy);
    /* Sheen */
    read_shader_float(L, shader, "inputs:sheen_weight", &out->sheen_weight);
    read_shader_float3(L, shader, "inputs:sheen_color", out->sheen_color);
    read_shader_float(L, shader, "inputs:sheen_roughness", &out->sheen_roughness);
    /* Fuzz */
    read_shader_float(L, shader, "inputs:fuzz_weight", &out->fuzz_weight);
    read_shader_float3(L, shader, "inputs:fuzz_color", out->fuzz_color);
    read_shader_float(L, shader, "inputs:fuzz_roughness", &out->fuzz_roughness);
    /* Thin Film */
    read_shader_float(L, shader, "inputs:thin_film_weight", &out->thin_film_weight);
    read_shader_float(L, shader, "inputs:thin_film_thickness", &out->thin_film_thickness);
    read_shader_float(L, shader, "inputs:thin_film_ior", &out->thin_film_ior);
    /* Coat */
    read_shader_float(L, shader, "inputs:coat_weight", &out->coat_weight);
    read_shader_float3(L, shader, "inputs:coat_color", out->coat_color);
    read_shader_float(L, shader, "inputs:coat_roughness", &out->coat_roughness);
    read_shader_float(L, shader, "inputs:coat_anisotropy", &out->coat_anisotropy);
    read_shader_float(L, shader, "inputs:coat_rotation", &out->coat_rotation);
    read_shader_float(L, shader, "inputs:coat_ior", &out->coat_ior);
    read_shader_float3(L, shader, "inputs:coat_affect_color", out->coat_affect_color);
    read_shader_float(L, shader, "inputs:coat_affect_roughness", &out->coat_affect_roughness);
    /* Emission */
    read_shader_float(L, shader, "inputs:emission_luminance", &out->emission_luminance);
    read_shader_float3(L, shader, "inputs:emission_color", out->emission_color);
    /* Geometry */
    read_shader_float(L, shader, "inputs:opacity", &out->opacity);
}

static void read_usdpreview_as_openpbr(const LusdLayer_T* L, const LusdPrim_T* shader,
                                        LydraCOpenPBRData* out) {
    read_shader_float3(L, shader, "inputs:diffuseColor", out->base_color);
    read_shader_float(L, shader, "inputs:metallic", &out->base_metalness);
    read_shader_float(L, shader, "inputs:roughness", &out->specular_roughness);
    read_shader_float3(L, shader, "inputs:specularColor", out->specular_color);
    read_shader_float(L, shader, "inputs:ior", &out->specular_ior);
    read_shader_float(L, shader, "inputs:clearcoat", &out->coat_weight);
    read_shader_float(L, shader, "inputs:clearcoatRoughness", &out->coat_roughness);
    read_shader_float3(L, shader, "inputs:emissiveColor", out->emission_color);
    read_shader_float(L, shader, "inputs:opacity", &out->opacity);
    /* Set emission_luminance if emissive color is non-zero */
    if (out->emission_color[0] + out->emission_color[1] + out->emission_color[2] > 0.0f)
        out->emission_luminance = 1.0f;
}

LusdResult lydra_c_extract_openpbr(LusdLayer layer, LusdPrim material_prim,
                                    LydraCOpenPBRData* out) {
    if (!layer || !material_prim || !out)
        return LUSD_ERROR_INVALID_HANDLE;

    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)material_prim;

    init_openpbr_defaults(out);

    /* Find the surface shader (UsdPreviewSurface or OpenPBR_Surface)
     * among all Shader descendants. find_descendant_by_type returns the first
     * Shader, which may be a UsdUVTexture or other non-surface shader. */
    const LusdPrim_T* shader = NULL;
    const char* shader_id = NULL;
    {
        /* DFS stack to find all Shader prims */
        const LusdPrim_T* stack[64];
        int sp = 0;
        for (uint32_t ci = 0; ci < P->child_count && sp < 64; ci++) {
            uint32_t idx = P->child_spec_indices[ci];
            if (idx < L->prim_node_count)
                stack[sp++] = &L->prim_nodes[idx];
        }
        while (sp > 0 && !shader) {
            const LusdPrim_T* node = stack[--sp];
            if (node->type_name && strcmp(node->type_name, "Shader") == 0) {
                LusdValueRep id_rep = find_field(L, node, "info:id");
                if (!lusd_vrep_is_null(id_rep)) {
                    const char* sid = materialize_token(L, id_rep);
                    if (sid && (strcmp(sid, "UsdPreviewSurface") == 0 ||
                                strcmp(sid, "OpenPBR_Surface") == 0 ||
                                strcmp(sid, "OpenPBRSurface") == 0 ||
                                strcmp(sid, "ND_standard_surface_surfaceshader") == 0 ||
                                strcmp(sid, "ND_open_pbr_surface_surfaceshader") == 0)) {
                        shader = node;
                        shader_id = sid;
                    }
                }
            }
            /* Push children */
            for (uint32_t j = 0; j < node->child_count && sp < 63; j++) {
                uint32_t cidx = node->child_spec_indices[j];
                if (cidx < L->prim_node_count)
                    stack[sp++] = &L->prim_nodes[cidx];
            }
        }
    }
    if (!shader || !shader_id)
        return LUSD_SUCCESS;

    if (strcmp(shader_id, "OpenPBR_Surface") == 0 ||
        strcmp(shader_id, "OpenPBRSurface") == 0 ||
        strcmp(shader_id, "ND_standard_surface_surfaceshader") == 0 ||
        strcmp(shader_id, "ND_open_pbr_surface_surfaceshader") == 0) {
        out->is_openpbr = 1;
        read_openpbr_inputs(L, shader, out);
        /* MaterialX standard_surface: base_color is often authored in sRGB.
         * Apply sRGB->linear unless a texture is connected (handled at sample time). */
        if (strcmp(shader_id, "ND_standard_surface_surfaceshader") == 0) {
            /* standard_surface uses 'metalness' not 'base_metalness' */
            read_shader_float(L, shader, "inputs:metalness", &out->base_metalness);
            /* standard_surface uses 'emission' (weight) + 'emission_color' */
            float emission_weight = 0.0f;
            read_shader_float(L, shader, "inputs:emission", &emission_weight);
            if (emission_weight > 0.0f && out->emission_luminance <= 0.0f)
                out->emission_luminance = emission_weight;
            for (int ch = 0; ch < 3; ch++) {
                float c = out->base_color[ch];
                /* sRGB -> linear (piecewise) */
                out->base_color[ch] = (c <= 0.04045f)
                    ? c / 12.92f
                    : powf((c + 0.055f) / 1.055f, 2.4f);
            }
        }
    } else if (strcmp(shader_id, "UsdPreviewSurface") == 0) {
        out->is_openpbr = 0;
        read_usdpreview_as_openpbr(L, shader, out);
    }
    /* Other shader types: return defaults */

    /* ── Texture connection walking ──
     * For each known input, check if the surface shader has a connection
     * (inputs:X.connect) pointing to a UsdUVTexture prim. If so, read
     * inputs:file from that texture prim. */
    {
        /* Input names for UsdPreviewSurface and OpenPBR */
        static const char* const tex_input_names[][2] = {
            {"inputs:diffuseColor", "inputs:base_color"},       /* base_color_tex */
            {"inputs:metallic",     "inputs:base_metalness"},   /* metalness_tex */
            {"inputs:roughness",    "inputs:specular_roughness"},/* roughness_tex */
            {"inputs:normal",       "inputs:geometry_normal"},  /* normal_tex */
            {"inputs:emissiveColor","inputs:emission_color"},   /* emissive_tex */
            {"inputs:opacity",      "inputs:opacity"},          /* opacity_tex */
        };
        const char** tex_slots[] = {
            &out->base_color_tex, &out->metalness_tex, &out->roughness_tex,
            &out->normal_tex, &out->emissive_tex, &out->opacity_tex
        };

        for (int ti = 0; ti < 6; ti++) {
            /* Try both naming conventions */
            const char* conn_target = NULL;
            for (int ni = 0; ni < 2 && !conn_target; ni++) {
                /* Build connection field name: "inputs:X.connect" */
                char conn_name[128];
                int len = snprintf(conn_name, sizeof(conn_name), "%s.connect",
                                   tex_input_names[ti][ni]);
                if (len <= 0 || len >= (int)sizeof(conn_name)) continue;

                /* Read the connection on the surface shader */
                conn_target = lusd__find_connection_target(L, shader, conn_name);
            }
            if (!conn_target) continue;

            /* conn_target is like "</Mat/Tex.outputs:rgb>" or "</Mat/Tex>"
             * Find the referenced prim among Material descendants */
            /* Extract prim path: strip leading < and trailing .outputs:...> */
            const char* path_start = conn_target;
            if (*path_start == '<') path_start++;
            /* Find end: either '.' or '>' */
            char prim_path[256];
            {
                const char* dot = path_start;
                while (*dot && *dot != '.' && *dot != '>') dot++;
                size_t plen = (size_t)(dot - path_start);
                if (plen == 0 || plen >= sizeof(prim_path)) continue;
                memcpy(prim_path, path_start, plen);
                prim_path[plen] = '\0';
            }

            /* Search all Shader descendants of the Material for matching path */
            for (uint32_t ci = 0; ci < P->child_count; ci++) {
                uint32_t idx = P->child_spec_indices[ci];
                if (idx >= L->prim_node_count) continue;
                const LusdPrim_T* child = &L->prim_nodes[idx];

                /* Check this child and its descendants for UsdUVTexture shaders */
                /* We do a simple recursive search for any Shader with matching path */
                const LusdPrim_T* stack[32];
                int sp = 0;
                stack[sp++] = child;
                while (sp > 0) {
                    const LusdPrim_T* node = stack[--sp];
                    /* Check if this prim's path matches */
                    int path_match = 0;
                    if (node->spec_index < L->spec_count) {
                        uint32_t pi = L->specs[node->spec_index].path_index;
                        if (pi < L->path_count && L->paths[pi]) {
                            if (strcmp(L->paths[pi], prim_path) == 0)
                                path_match = 1;
                        }
                    }
                    if (path_match) {
                        /* Check if it's a UsdUVTexture shader */
                        int found_file = 0;
                        LusdValueRep tex_id_rep = find_field(L, node, "info:id");
                        if (!lusd_vrep_is_null(tex_id_rep)) {
                            const char* tex_shader_id = materialize_token(L, tex_id_rep);
                            if (tex_shader_id && strcmp(tex_shader_id, "UsdUVTexture") == 0) {
                                LusdValueRep file_rep = find_field(L, node, "inputs:file");
                                if (!lusd_vrep_is_null(file_rep)) {
                                    const char* fpath = materialize_asset_path(L, file_rep);
                                    if (fpath && fpath[0] != '\0') {
                                        *tex_slots[ti] = fpath;
                                        found_file = 1;
                                    }
                                }
                            }
                        }
                        if (!found_file) {
                            /* Not a direct UsdUVTexture (e.g. NodeGraph) —
                             * search descendants for the first UsdUVTexture with inputs:file */
                            const LusdPrim_T* inner_stack[32];
                            int isp = 0;
                            for (uint32_t j = 0; j < node->child_count && isp < 32; j++) {
                                uint32_t cidx2 = node->child_spec_indices[j];
                                if (cidx2 < L->prim_node_count)
                                    inner_stack[isp++] = &L->prim_nodes[cidx2];
                            }
                            while (isp > 0 && !found_file) {
                                const LusdPrim_T* inode = inner_stack[--isp];
                                LusdValueRep iid = find_field(L, inode, "info:id");
                                if (!lusd_vrep_is_null(iid)) {
                                    const char* isid = materialize_token(L, iid);
                                    if (isid && (strcmp(isid, "UsdUVTexture") == 0 ||
                                             strncmp(isid, "ND_image_", 9) == 0)) {
                                        LusdValueRep fr = find_field(L, inode, "inputs:file");
                                        if (!lusd_vrep_is_null(fr)) {
                                            const char* fp = materialize_asset_path(L, fr);
                                            if (fp && fp[0] != '\0') {
                                                *tex_slots[ti] = fp;
                                                found_file = 1;
                                            }
                                        }
                                    }
                                }
                                if (!found_file) {
                                    for (uint32_t j = 0; j < inode->child_count && isp < 31; j++) {
                                        uint32_t cidx2 = inode->child_spec_indices[j];
                                        if (cidx2 < L->prim_node_count)
                                            inner_stack[isp++] = &L->prim_nodes[cidx2];
                                    }
                                }
                            }
                        }
                        break;
                    }
                    /* Push children */
                    for (uint32_t j = 0; j < node->child_count && sp < 31; j++) {
                        uint32_t cidx = node->child_spec_indices[j];
                        if (cidx < L->prim_node_count)
                            stack[sp++] = &L->prim_nodes[cidx];
                    }
                }
            }
        }
    }

    return LUSD_SUCCESS;
}

/* ================================================================
 * extract_mesh
 * ================================================================ */

LusdResult lydra_c_extract_mesh(LusdLayer layer, LusdPrim prim,
                                LydraCMeshData* out) {
    memset(out, 0, sizeof(*out));

    if (!layer || !prim) return LUSD_ERROR_INVALID_HANDLE;

    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    /* points (required) */
    {
        LusdValueRep rep = find_field(L, P, "points");
        if (rep.data == 0) return LUSD_ERROR_NOT_FOUND;
        float* data = NULL; uint64_t count = 0;
        if (materialize_float3_array(L, rep, &data, &count) != 0)
            return LUSD_ERROR_PARSE_FAILED;
        out->points = data;
        out->point_count = (uint32_t)count;
    }

    /* faceVertexCounts (required) */
    {
        LusdValueRep rep = find_field(L, P, "faceVertexCounts");
        if (rep.data == 0) { lydra_c_free_mesh_data(out); return LUSD_ERROR_NOT_FOUND; }
        int32_t* data = NULL; uint64_t count = 0;
        if (materialize_int_array(L, rep, &data, &count) != 0) {
            lydra_c_free_mesh_data(out); return LUSD_ERROR_PARSE_FAILED;
        }
        out->face_vertex_counts = data;
        out->fvc_count = (uint32_t)count;
    }

    /* faceVertexIndices (required) */
    {
        LusdValueRep rep = find_field(L, P, "faceVertexIndices");
        if (rep.data == 0) { lydra_c_free_mesh_data(out); return LUSD_ERROR_NOT_FOUND; }
        int32_t* data = NULL; uint64_t count = 0;
        if (materialize_int_array(L, rep, &data, &count) != 0) {
            lydra_c_free_mesh_data(out); return LUSD_ERROR_PARSE_FAILED;
        }
        out->face_vertex_indices = data;
        out->fvi_count = (uint32_t)count;
    }

    /* normals (optional) — also check primvars:normals and handle indices */
    {
        static const char* const nrm_names[] = {
            "normals", "primvars:normals", NULL
        };
        for (int ni = 0; nrm_names[ni] && !out->normals; ni++) {
            LusdValueRep rep = find_field(L, P, nrm_names[ni]);
            if (lusd_vrep_is_null(rep)) continue;
            float* nrm_data = NULL; uint64_t nrm_cnt = 0;
            if (materialize_float3_array(L, rep, &nrm_data, &nrm_cnt) != 0) continue;

            /* Check for index array */
            char idx_name[128];
            snprintf(idx_name, sizeof(idx_name), "%s:indices", nrm_names[ni]);
            LusdValueRep idx_rep = find_field(L, P, idx_name);
            if (!lusd_vrep_is_null(idx_rep)) {
                int32_t* indices = NULL; uint64_t idx_cnt = 0;
                if (materialize_int_array(L, idx_rep, &indices, &idx_cnt) == 0 &&
                    idx_cnt > 0) {
                    float* expanded = (float*)malloc(idx_cnt * 3 * sizeof(float));
                    if (expanded) {
                        for (uint64_t ii = 0; ii < idx_cnt; ii++) {
                            int32_t idx = indices[ii];
                            if (idx >= 0 && (uint64_t)idx < nrm_cnt) {
                                expanded[ii*3+0] = nrm_data[idx*3+0];
                                expanded[ii*3+1] = nrm_data[idx*3+1];
                                expanded[ii*3+2] = nrm_data[idx*3+2];
                            } else {
                                expanded[ii*3+0] = 0.0f;
                                expanded[ii*3+1] = 1.0f;
                                expanded[ii*3+2] = 0.0f;
                            }
                        }
                        free(nrm_data);
                        free(indices);
                        out->normals = expanded;
                        out->normal_count = (uint32_t)idx_cnt;
                        continue;
                    }
                }
                free(indices);
            }
            out->normals = nrm_data;
            out->normal_count = (uint32_t)nrm_cnt;
        }
    }

    /* UV primvars — try several common names in priority order.
     * Also handle indexed primvars (primvars:X:indices array). */
    {
        static const char* const uv_names[] = {
            "primvars:st", "primvars:UVMap", "primvars:uv",
            "primvars:UV", "primvars:map1", NULL
        };
        for (int ui = 0; uv_names[ui] && !out->uvs; ui++) {
            LusdValueRep rep = find_field(L, P, uv_names[ui]);
            if (lusd_vrep_is_null(rep)) continue;
            float* uv_data = NULL; uint64_t uv_cnt = 0;
            if (materialize_float2_array(L, rep, &uv_data, &uv_cnt) != 0) continue;

            /* Check for index array: primvars:X:indices */
            char idx_name[128];
            snprintf(idx_name, sizeof(idx_name), "%s:indices", uv_names[ui]);
            LusdValueRep idx_rep = find_field(L, P, idx_name);
            if (!lusd_vrep_is_null(idx_rep)) {
                int32_t* indices = NULL; uint64_t idx_cnt = 0;
                if (materialize_int_array(L, idx_rep, &indices, &idx_cnt) == 0 &&
                    idx_cnt > 0) {
                    /* Expand indexed UVs: result has idx_cnt UV pairs */
                    float* expanded = (float*)malloc(idx_cnt * 2 * sizeof(float));
                    if (expanded) {
                        for (uint64_t ii = 0; ii < idx_cnt; ii++) {
                            int32_t idx = indices[ii];
                            if (idx >= 0 && (uint64_t)idx < uv_cnt) {
                                expanded[ii*2+0] = uv_data[idx*2+0];
                                expanded[ii*2+1] = uv_data[idx*2+1];
                            } else {
                                expanded[ii*2+0] = 0.0f;
                                expanded[ii*2+1] = 0.0f;
                            }
                        }
                        free(uv_data);
                        free(indices);
                        out->uvs = expanded;
                        out->uv_count = (uint32_t)idx_cnt;
                        continue; /* outer for loop: break (uvs is set) */
                    }
                }
                free(indices);
            }
            /* No index array or expansion failed — use raw UVs as-is */
            out->uvs = uv_data;
            out->uv_count = (uint32_t)uv_cnt;
        }
    }

    return LUSD_SUCCESS;
}

void lydra_c_free_mesh_data(LydraCMeshData* data) {
    if (!data) return;
    free(data->points);
    free(data->face_vertex_counts);
    free(data->face_vertex_indices);
    free(data->normals);
    free(data->uvs);
    memset(data, 0, sizeof(*data));
}

/* ================================================================
 * matrix4d materialization
 * ================================================================ */

static int materialize_matrix4d(const LusdLayer_T* L, LusdValueRep rep,
                                 double out[16]) {
    if (!L || rep.data == 0) return -1;
    if (lusd_vrep_is_inlined(rep)) return -1; /* 128 bytes can't be inlined */

    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        /* USDA: parse ( (a,b,c,d), (e,f,g,h), (i,j,k,l), (m,n,o,p) ) */
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;
        int count = 0;
        while (p < end && count < 16) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'
                   || *p == '(' || *p == ')' || *p == ',')) p++;
            if (p >= end) break;
            if (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9')) {
                char* endp;
                out[count++] = strtod(p, &endp);
                if (endp == p) return -1;
                p = endp;
            } else {
                break;
            }
        }
        return (count == 16) ? 0 : -1;
    }

    /* USDC: 16 doubles at file offset */
    if (offset + 128 > L->file_size) return -1;
    memcpy(out, L->file_data + offset, 128);
    return 0;
}

static int materialize_float2(const LusdLayer_T* L, LusdValueRep rep,
                               float out[2]) {
    if (!L || rep.data == 0) return -1;
    if (lusd_vrep_is_inlined(rep)) return -1; /* 8 bytes > 6 byte payload */

    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '(')) p++;
        for (int i = 0; i < 2; i++) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
            char* endp;
            out[i] = strtof(p, &endp);
            if (endp == p) return -1;
            p = endp;
        }
        return 0;
    }

    int type_id = lusd_vrep_type(rep);
    if (type_id == LUSD_CRATE_VEC2F) {
        if (offset + 8 > L->file_size) return -1;
        memcpy(out, L->file_data + offset, 8);
        return 0;
    }
    if (type_id == LUSD_CRATE_VEC2D) {
        if (offset + 16 > L->file_size) return -1;
        double d[2];
        memcpy(d, L->file_data + offset, 16);
        out[0] = (float)d[0]; out[1] = (float)d[1];
        return 0;
    }
    return -1;
}

/* ================================================================
 * Xform extraction — xformOpOrder + individual ops
 * ================================================================ */

/* Read a double3 (vec3d or vec3f) into double[3] */
static int materialize_double3(const LusdLayer_T* L, LusdValueRep rep, double out[3]) {
    if (!L || rep.data == 0) return -1;
    if (lusd_vrep_is_inlined(rep)) return -1;
    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '(')) p++;
        for (int i = 0; i < 3; i++) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
            char* endp;
            out[i] = strtod(p, &endp);
            if (endp == p) return -1;
            p = endp;
        }
        return 0;
    }

    int type_id = lusd_vrep_type(rep);
    if (type_id == LUSD_CRATE_VEC3D) {
        if (offset + 24 > L->file_size) return -1;
        memcpy(out, L->file_data + offset, 24);
        return 0;
    }
    if (type_id == LUSD_CRATE_VEC3F || type_id == LUSD_CRATE_FLOAT) {
        if (offset + 12 > L->file_size) return -1;
        float f[3];
        memcpy(f, L->file_data + offset, 12);
        out[0] = f[0]; out[1] = f[1]; out[2] = f[2];
        return 0;
    }
    return -1;
}

/* Read a quatf (w,x,y,z) or quatd into double[4] */
static int materialize_quat(const LusdLayer_T* L, LusdValueRep rep, double out[4]) {
    if (!L || rep.data == 0) return -1;
    if (lusd_vrep_is_inlined(rep)) return -1;
    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '(')) p++;
        for (int i = 0; i < 4; i++) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
            char* endp;
            out[i] = strtod(p, &endp);
            if (endp == p) return -1;
            p = endp;
        }
        return 0;
    }

    int type_id = lusd_vrep_type(rep);
    if (type_id == LUSD_CRATE_QUATF) {
        if (offset + 16 > L->file_size) return -1;
        float f[4];
        memcpy(f, L->file_data + offset, 16);
        out[0] = f[0]; out[1] = f[1]; out[2] = f[2]; out[3] = f[3];
        return 0;
    }
    if (type_id == LUSD_CRATE_QUATD) {
        if (offset + 32 > L->file_size) return -1;
        memcpy(out, L->file_data + offset, 32);
        return 0;
    }
    return -1;
}

/* Read token array (xformOpOrder is token[] / TOKEN_VECTOR).
 * Returns count of tokens. out_tokens[] are pointers into L->tokens[].
 * Caller must free *out_tokens. */
static int materialize_token_array(const LusdLayer_T* L, LusdValueRep rep,
                                    const char*** out_tokens, uint32_t* out_count) {
    *out_tokens = NULL;
    *out_count = 0;
    if (!L || rep.data == 0) return -1;

    int type_id = lusd_vrep_type(rep);

    if (is_usda_layer(L)) {
        /* USDA: parse ["tok1", "tok2", ...] */
        uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;
        if (offset >= L->file_size) return -1;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;

        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (p >= end || *p != '[') return -1;
        p++;

        /* First pass: count tokens */
        uint32_t count = 0;
        {
            const char* scan = p;
            while (scan < end && *scan != ']') {
                if (*scan == '"') { count++; scan++; while (scan < end && *scan != '"') scan++; if (scan < end) scan++; }
                else scan++;
            }
        }
        if (count == 0) return -1;

        const char** tokens = (const char**)malloc(count * sizeof(const char*));
        if (!tokens) return -1;

        /* Second pass: extract. Match against L->tokens[] for stable pointers */
        uint32_t idx = 0;
        while (p < end && *p != ']' && idx < count) {
            while (p < end && *p != '"' && *p != ']') p++;
            if (p >= end || *p == ']') break;
            p++; /* skip opening " */
            const char* tok_start = p;
            while (p < end && *p != '"') p++;
            size_t tok_len = (size_t)(p - tok_start);
            if (p < end) p++; /* skip closing " */

            /* Try to find in token table for stable pointer */
            const char* found = NULL;
            for (uint32_t ti = 0; ti < L->token_count; ti++) {
                if (strlen(L->tokens[ti]) == tok_len &&
                    memcmp(L->tokens[ti], tok_start, tok_len) == 0) {
                    found = L->tokens[ti];
                    break;
                }
            }
            tokens[idx++] = found; /* NULL if not found */
        }
        *out_tokens = tokens;
        *out_count = idx;
        return 0;
    }

    /* USDC: token[] array or TOKEN_VECTOR */
    if (type_id == LUSD_CRATE_TOKEN_VECTOR ||
        (type_id == LUSD_CRATE_TOKEN && lusd_vrep_is_array(rep))) {
        uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;
        if (offset + 8 > L->file_size) return -1;

        uint64_t numElements;
        memcpy(&numElements, L->file_data + offset, 8);
        offset += 8;
        if (numElements == 0) return -1;

        const char** tokens = (const char**)malloc((size_t)numElements * sizeof(const char*));
        if (!tokens) return -1;

        for (uint64_t i = 0; i < numElements; i++) {
            if (offset + 4 > L->file_size) { free(tokens); return -1; }
            uint32_t ti;
            memcpy(&ti, L->file_data + offset, 4);
            offset += 4;
            tokens[i] = (ti < L->token_count) ? L->tokens[ti] : NULL;
        }
        *out_tokens = tokens;
        *out_count = (uint32_t)numElements;
        return 0;
    }

    return -1;
}

/* 4x4 identity matrix */
static void mat4d_identity(double m[16]) {
    memset(m, 0, 16 * sizeof(double));
    m[0] = m[5] = m[10] = m[15] = 1.0;
}

/* 4x4 row-major matrix multiply: C = A * B */
static void mat4d_multiply(const double A[16], const double B[16], double C[16]) {
    double tmp[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double s = 0.0;
            for (int k = 0; k < 4; k++) s += A[r*4+k] * B[k*4+c];
            tmp[r*4+c] = s;
        }
    memcpy(C, tmp, 16 * sizeof(double));
}

/* Build translation matrix */
static void make_translate_matrix(const double t[3], double out[16]) {
    mat4d_identity(out);
    out[3] = t[0]; out[7] = t[1]; out[11] = t[2];
}

/* Build scale matrix */
static void make_scale_matrix(const double s[3], double out[16]) {
    mat4d_identity(out);
    out[0] = s[0]; out[5] = s[1]; out[10] = s[2];
}

/* Build rotation matrix from euler angles (degrees) with specified axis order.
 * Orders: 0=XYZ, 1=XZY, 2=YXZ, 3=YZX, 4=ZXY, 5=ZYX */
static void make_rotate_matrix(const double angles_deg[3], int order, double out[16]) {
    double a[3];
    double Rx[16], Ry[16], Rz[16];
    static const double DEG2RAD = 3.14159265358979323846 / 180.0;

    for (int i = 0; i < 3; i++) a[i] = angles_deg[i] * DEG2RAD;

    double cx = cos(a[0]), sx = sin(a[0]);
    double cy = cos(a[1]), sy = sin(a[1]);
    double cz = cos(a[2]), sz = sin(a[2]);

    mat4d_identity(Rx);
    Rx[5] = cx;  Rx[6] = -sx; Rx[9] = sx;  Rx[10] = cx;

    mat4d_identity(Ry);
    Ry[0] = cy;  Ry[2] = sy;  Ry[8] = -sy; Ry[10] = cy;

    mat4d_identity(Rz);
    Rz[0] = cz;  Rz[1] = -sz; Rz[4] = sz;  Rz[5] = cz;

    double tmp[16];
    switch (order) {
        case 0: /* XYZ: Rx * Ry * Rz */
            mat4d_multiply(Rx, Ry, tmp);
            mat4d_multiply(tmp, Rz, out);
            break;
        case 1: /* XZY: Rx * Rz * Ry */
            mat4d_multiply(Rx, Rz, tmp);
            mat4d_multiply(tmp, Ry, out);
            break;
        case 2: /* YXZ: Ry * Rx * Rz */
            mat4d_multiply(Ry, Rx, tmp);
            mat4d_multiply(tmp, Rz, out);
            break;
        case 3: /* YZX: Ry * Rz * Rx */
            mat4d_multiply(Ry, Rz, tmp);
            mat4d_multiply(tmp, Rx, out);
            break;
        case 4: /* ZXY: Rz * Rx * Ry */
            mat4d_multiply(Rz, Rx, tmp);
            mat4d_multiply(tmp, Ry, out);
            break;
        case 5: /* ZYX: Rz * Ry * Rx */
            mat4d_multiply(Rz, Ry, tmp);
            mat4d_multiply(tmp, Rx, out);
            break;
        default:
            mat4d_multiply(Rx, Ry, tmp);
            mat4d_multiply(tmp, Rz, out);
            break;
    }
}

/* Build rotation matrix from quaternion (real, i, j, k) = (w, x, y, z) */
static void make_quat_matrix(const double q[4], double out[16]) {
    double w = q[0], x = q[1], y = q[2], z = q[3];
    double n = w*w + x*x + y*y + z*z;
    double s = (n > 0.0) ? 2.0 / n : 0.0;

    double wx = s*w*x, wy = s*w*y, wz = s*w*z;
    double xx = s*x*x, xy = s*x*y, xz = s*x*z;
    double yy = s*y*y, yz = s*y*z, zz = s*z*z;

    mat4d_identity(out);
    out[0]  = 1.0 - (yy + zz);  out[1]  = xy - wz;            out[2]  = xz + wy;
    out[4]  = xy + wz;           out[5]  = 1.0 - (xx + zz);    out[6]  = yz - wx;
    out[8]  = xz - wy;           out[9]  = yz + wx;             out[10] = 1.0 - (xx + yy);
}

/* Build single-axis rotation matrix from angle in degrees */
static void make_single_rotate_matrix(double angle_deg, int axis, double out[16]) {
    double angles[3] = {0, 0, 0};
    angles[axis] = angle_deg;
    make_rotate_matrix(angles, 0, out); /* order irrelevant for single axis */
}

/* Determine rotation order from xformOp name suffix.
 * Returns axis order enum (0=XYZ, etc.) or -1 for single-axis rotations. */
static int parse_rotate_order(const char* op_name) {
    /* Look for rotateXYZ, rotateXZY, etc. after "xformOp:rotate" prefix */
    const char* suffix = op_name + strlen("xformOp:rotate");
    if (strcmp(suffix, "XYZ") == 0) return 0;
    if (strcmp(suffix, "XZY") == 0) return 1;
    if (strcmp(suffix, "YXZ") == 0) return 2;
    if (strcmp(suffix, "YZX") == 0) return 3;
    if (strcmp(suffix, "ZXY") == 0) return 4;
    if (strcmp(suffix, "ZYX") == 0) return 5;
    if (strcmp(suffix, "X") == 0)   return -1; /* single X */
    if (strcmp(suffix, "Y") == 0)   return -2; /* single Y */
    if (strcmp(suffix, "Z") == 0)   return -3; /* single Z */
    return 0; /* default XYZ */
}

/* Apply a single xformOp to the accumulator matrix.
 * Returns 0 on success, -1 if op not found/supported. */
static int apply_xform_op(const LusdLayer_T* L, const LusdPrim_T* P,
                           const char* op_name, double accum[16]) {
    if (!op_name) return -1;

    /* Strip suffix for field lookup: "xformOp:translate:pivot" → field name as-is */
    LusdValueRep rep = find_field(L, P, op_name);
    if (lusd_vrep_is_null(rep)) return -1;

    double op_mat[16];

    if (strncmp(op_name, "xformOp:transform", 17) == 0) {
        if (materialize_matrix4d(L, rep, op_mat) != 0) return -1;
    } else if (strncmp(op_name, "xformOp:translate", 17) == 0) {
        double t[3];
        if (materialize_double3(L, rep, t) != 0) return -1;
        make_translate_matrix(t, op_mat);
    } else if (strncmp(op_name, "xformOp:scale", 13) == 0) {
        double s[3];
        if (materialize_double3(L, rep, s) != 0) return -1;
        make_scale_matrix(s, op_mat);
    } else if (strncmp(op_name, "xformOp:rotate", 14) == 0) {
        /* Determine if it's a triple-axis or single-axis rotation */
        /* Check for suffix after "xformOp:rotate" but before optional ":name" */
        /* Extract the rotation type part (X, Y, Z, XYZ, etc.) */
        const char* after_rotate = op_name + 14;
        /* Find end of rotation type (before optional ":suffix") */
        const char* colon = strchr(after_rotate, ':');
        size_t type_len = colon ? (size_t)(colon - after_rotate) : strlen(after_rotate);

        if (type_len == 1 && (after_rotate[0] == 'X' || after_rotate[0] == 'Y' || after_rotate[0] == 'Z')) {
            /* Single-axis rotation: value is a single float/double */
            float angle_f;
            if (materialize_float(L, rep, &angle_f) != 0) return -1;
            int axis = (after_rotate[0] == 'X') ? 0 : (after_rotate[0] == 'Y') ? 1 : 2;
            make_single_rotate_matrix((double)angle_f, axis, op_mat);
        } else {
            /* Triple-axis rotation (XYZ, XZY, etc.) */
            double angles[3];
            if (materialize_double3(L, rep, angles) != 0) return -1;
            /* Build a temporary name for parse_rotate_order */
            char tmp_name[32];
            snprintf(tmp_name, sizeof(tmp_name), "xformOp:rotate%.*s", (int)type_len, after_rotate);
            int order = parse_rotate_order(tmp_name);
            if (order < 0) return -1; /* shouldn't happen for triple-axis */
            make_rotate_matrix(angles, order, op_mat);
        }
    } else if (strncmp(op_name, "xformOp:orient", 14) == 0) {
        double q[4];
        if (materialize_quat(L, rep, q) != 0) return -1;
        make_quat_matrix(q, op_mat);
    } else {
        return -1; /* unsupported op */
    }

    mat4d_multiply(accum, op_mat, accum);
    return 0;
}

int lydra_c_extract_xform(LusdLayer layer, LusdPrim prim, double out_matrix[16]) {
    if (!layer || !prim || !out_matrix) return -1;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    /* Try xformOpOrder first */
    LusdValueRep order_rep = find_field(L, P, "xformOpOrder");
    if (!lusd_vrep_is_null(order_rep)) {
        const char** tokens = NULL;
        uint32_t count = 0;
        if (materialize_token_array(L, order_rep, &tokens, &count) == 0 && count > 0) {
            mat4d_identity(out_matrix);
            int any_applied = 0;
            for (uint32_t i = 0; i < count; i++) {
                if (tokens[i] && apply_xform_op(L, P, tokens[i], out_matrix) == 0)
                    any_applied = 1;
            }
            free(tokens);
            return any_applied ? 0 : -1;
        }
        free(tokens);
    }

    /* Fallback: direct xformOp:transform lookup */
    LusdValueRep rep = find_field(L, P, "xformOp:transform");
    if (lusd_vrep_is_null(rep)) return -1;
    return materialize_matrix4d(L, rep, out_matrix);
}

/* ================================================================
 * Camera extraction
 * ================================================================ */

LusdResult lydra_c_extract_camera(LusdLayer layer, LusdPrim prim,
                                   LydraCCameraData* out) {
    if (!layer || !prim || !out) return LUSD_ERROR_INVALID_HANDLE;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    /* Defaults */
    out->focal_length        = 50.0f;
    out->vertical_aperture   = 15.2908f;
    out->horizontal_aperture = 20.965f;
    out->znear               = 0.1f;
    out->zfar                = 1000000.0f;
    out->shutter_open        = 0.0;
    out->shutter_close       = 0.0;

    read_shader_float(L, P, "focalLength", &out->focal_length);
    read_shader_float(L, P, "verticalAperture", &out->vertical_aperture);
    read_shader_float(L, P, "horizontalAperture", &out->horizontal_aperture);

    /* clippingRange is a float2 → znear/zfar */
    {
        LusdValueRep rep = find_field(L, P, "clippingRange");
        if (!lusd_vrep_is_null(rep)) {
            float cr[2];
            if (materialize_float2(L, rep, cr) == 0) {
                out->znear = cr[0];
                out->zfar  = cr[1];
            }
        }
    }

    /* shutter:open and shutter:close */
    {
        float so = 0.0f;
        read_shader_float(L, P, "shutter:open", &so);
        out->shutter_open = (double)so;
    }
    {
        float sc = 0.0f;
        read_shader_float(L, P, "shutter:close", &sc);
        out->shutter_close = (double)sc;
    }

    return LUSD_SUCCESS;
}

/* ================================================================
 * Light extraction
 * ================================================================ */

LusdResult lydra_c_extract_light(LusdLayer layer, LusdPrim prim,
                                  LydraCLightData* out) {
    if (!layer || !prim || !out) return LUSD_ERROR_INVALID_HANDLE;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    /* Defaults */
    out->type      = LYDRA_C_LIGHT_DISTANT;
    out->color[0]  = 1.0f; out->color[1] = 1.0f; out->color[2] = 1.0f;
    out->intensity  = 1.0f;
    out->exposure   = 0.0f;
    out->radius     = 0.5f;
    out->width      = 1.0f;
    out->height     = 1.0f;

    /* Determine type from prim type_name */
    if (P->type_name) {
        if (strcmp(P->type_name, "SphereLight") == 0)
            out->type = LYDRA_C_LIGHT_SPHERE;
        else if (strcmp(P->type_name, "RectLight") == 0)
            out->type = LYDRA_C_LIGHT_RECT;
        else if (strcmp(P->type_name, "DistantLight") == 0)
            out->type = LYDRA_C_LIGHT_DISTANT;
    }

    read_shader_float(L, P, "inputs:intensity", &out->intensity);
    read_shader_float(L, P, "inputs:exposure", &out->exposure);
    read_shader_float3(L, P, "inputs:color", out->color);
    read_shader_float(L, P, "inputs:radius", &out->radius);
    read_shader_float(L, P, "inputs:width", &out->width);
    read_shader_float(L, P, "inputs:height", &out->height);

    return LUSD_SUCCESS;
}

/* ================================================================
 * GeomSubset extraction
 * ================================================================ */

uint32_t lydra_c_extract_geom_subsets(LusdLayer layer, LusdPrim mesh_prim,
                                      LydraCGeomSubset** out_subsets) {
    *out_subsets = NULL;
    if (!layer || !mesh_prim) return 0;

    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)mesh_prim;

    /* First pass: count GeomSubset children */
    uint32_t subset_count = 0;
    for (uint32_t i = 0; i < P->child_count; i++) {
        uint32_t idx = P->child_spec_indices[i];
        if (idx >= L->prim_node_count) continue;
        const LusdPrim_T* child = &L->prim_nodes[idx];
        if (child->type_name && strcmp(child->type_name, "GeomSubset") == 0)
            subset_count++;
    }
    if (subset_count == 0) return 0;

    LydraCGeomSubset* subsets = (LydraCGeomSubset*)calloc(subset_count, sizeof(LydraCGeomSubset));
    if (!subsets) return 0;

    /* Second pass: extract data */
    uint32_t out_idx = 0;
    for (uint32_t i = 0; i < P->child_count && out_idx < subset_count; i++) {
        uint32_t idx = P->child_spec_indices[i];
        if (idx >= L->prim_node_count) continue;
        const LusdPrim_T* child = &L->prim_nodes[idx];
        if (!child->type_name || strcmp(child->type_name, "GeomSubset") != 0)
            continue;

        /* Check elementType == "face" */
        LusdValueRep et_rep = find_field(L, child, "elementType");
        if (!lusd_vrep_is_null(et_rep)) {
            const char* et = materialize_token(L, et_rep);
            if (et && strcmp(et, "face") != 0)
                continue; /* skip non-face subsets */
        }

        /* Read indices */
        LusdValueRep idx_rep = find_field(L, child, "indices");
        if (lusd_vrep_is_null(idx_rep)) continue;

        int32_t* indices = NULL;
        uint64_t count = 0;
        if (materialize_int_array(L, idx_rep, &indices, &count) != 0 || count == 0) {
            free(indices);
            continue;
        }

        /* Resolve material:binding */
        const char* mat_path = lusd__find_relationship_target(L, child, "material:binding");

        subsets[out_idx].face_indices = indices;
        subsets[out_idx].face_count = (uint32_t)count;
        subsets[out_idx].material_path = mat_path;
        out_idx++;
    }

    if (out_idx == 0) {
        free(subsets);
        return 0;
    }

    *out_subsets = subsets;
    return out_idx;
}

void lydra_c_free_geom_subsets(LydraCGeomSubset* subsets, uint32_t count) {
    if (!subsets) return;
    for (uint32_t i = 0; i < count; i++) {
        free(subsets[i].face_indices);
    }
    free(subsets);
}

/* ================================================================
 * DomeLight extraction
 * ================================================================ */

/* Materialize an asset path (@path@).
 * For USDC: token index lookup. For USDA: text between @ delimiters.
 * Returns pointer valid for layer lifetime, or NULL. */
static const char* materialize_asset_path(const LusdLayer_T* L, LusdValueRep rep) {
    if (!L || rep.data == 0) return NULL;

    int type_id = lusd_vrep_type(rep);

    /* Asset paths may be stored as tokens in USDC */
    if (lusd_vrep_is_inlined(rep)) {
        if (type_id == LUSD_CRATE_TOKEN || type_id == LUSD_CRATE_ASSET_PATH) {
            uint32_t ti = (uint32_t)(rep.data & LUSD_VREP_PAYLOAD_MASK);
            if (ti < L->token_count) return L->tokens[ti];
        }
        return NULL;
    }

    uint64_t offset = rep.data & LUSD_VREP_PAYLOAD_MASK;

    if (is_usda_layer(L)) {
        /* USDA: find text between @ delimiters */
        if (offset >= L->file_size) return NULL;
        const char* p = (const char*)L->file_data + offset;
        const char* end = (const char*)L->file_data + L->file_size;
        while (p < end && *p != '@') p++;
        if (p >= end) return NULL;
        p++; /* skip @ */
        const char* start = p;
        while (p < end && *p != '@') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) return NULL;
        /* Try matching token table first for stable pointer */
        for (uint32_t ti = 0; ti < L->token_count; ti++) {
            if (strlen(L->tokens[ti]) == len && memcmp(L->tokens[ti], start, len) == 0)
                return L->tokens[ti];
        }
        /* Fallback: return a copy in a static buffer (single-threaded use) */
        static char s_asset_buf[1024];
        if (len >= sizeof(s_asset_buf)) len = sizeof(s_asset_buf) - 1;
        memcpy(s_asset_buf, start, len);
        s_asset_buf[len] = '\0';
        return s_asset_buf;
    }

    /* USDC: token index at offset */
    if (type_id == LUSD_CRATE_TOKEN || type_id == LUSD_CRATE_ASSET_PATH) {
        if (offset + 4 > L->file_size) return NULL;
        uint32_t ti;
        memcpy(&ti, L->file_data + offset, sizeof(uint32_t));
        if (ti < L->token_count) return L->tokens[ti];
    }
    return NULL;
}

LusdResult lydra_c_extract_dome_light(LusdLayer layer, LusdPrim prim,
                                       LydraCDomeLightData* out) {
    if (!layer || !prim || !out) return LUSD_ERROR_INVALID_HANDLE;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    /* Defaults */
    out->intensity = 1.0f;
    out->exposure = 0.0f;
    out->color[0] = 1.0f; out->color[1] = 1.0f; out->color[2] = 1.0f;
    out->texture_file = NULL;

    read_shader_float(L, P, "inputs:intensity", &out->intensity);
    read_shader_float(L, P, "inputs:exposure", &out->exposure);
    read_shader_float3(L, P, "inputs:color", out->color);

    /* Try inputs:texture:file directly on DomeLight prim */
    {
        LusdValueRep rep = find_field(L, P, "inputs:texture:file");
        if (!lusd_vrep_is_null(rep)) {
            const char* path = materialize_asset_path(L, rep);
            if (path && path[0] != '\0') out->texture_file = path;
        }
    }

    /* If no direct texture, look for connected UsdUVTexture child */
    if (!out->texture_file) {
        const LusdPrim_T* tex_shader = find_descendant_by_type(L, P, "Shader");
        if (tex_shader) {
            LusdValueRep id_rep = find_field(L, tex_shader, "info:id");
            if (!lusd_vrep_is_null(id_rep)) {
                const char* shader_id = materialize_token(L, id_rep);
                if (shader_id && strcmp(shader_id, "UsdUVTexture") == 0) {
                    LusdValueRep file_rep = find_field(L, tex_shader, "inputs:file");
                    if (!lusd_vrep_is_null(file_rep)) {
                        const char* path = materialize_asset_path(L, file_rep);
                        if (path && path[0] != '\0') out->texture_file = path;
                    }
                }
            }
        }
    }

    return LUSD_SUCCESS;
}

/* ================================================================
 * Time-sampled xform extraction (motion blur)
 * ================================================================ */

/* materialize_matrix4d at a specific time code */
static int materialize_matrix4d_at(const LusdLayer_T* L, LusdValueRep rep,
                                    double time_code, double out[16]) {
    if (!L || rep.data == 0) return -1;
    int type_id = lusd_vrep_type(rep);

    if (type_id != LUSD_CRATE_TIME_SAMPLES)
        return materialize_matrix4d(L, rep, out); /* non-time-sampled fallback */

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return -1;

    /* Use text_offset as binary offset for USDC, text offset for USDA */
    LusdValueRep sample_rep;
    sample_rep.data = ((uint64_t)LUSD_CRATE_MATRIX4D << LUSD_VREP_TYPE_SHIFT)
                      | (s->text_offset & LUSD_VREP_PAYLOAD_MASK);
    return materialize_matrix4d(L, sample_rep, out);
}

/* materialize_double3 at a specific time code */
static int materialize_double3_at(const LusdLayer_T* L, LusdValueRep rep,
                                   double time_code, double out[3]) {
    if (!L || rep.data == 0) return -1;
    int type_id = lusd_vrep_type(rep);

    if (type_id != LUSD_CRATE_TIME_SAMPLES)
        return materialize_double3(L, rep, out);

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return -1;

    LusdValueRep sample_rep;
    sample_rep.data = ((uint64_t)LUSD_CRATE_VEC3D << LUSD_VREP_TYPE_SHIFT)
                      | (s->text_offset & LUSD_VREP_PAYLOAD_MASK);
    return materialize_double3(L, sample_rep, out);
}

/* materialize_float at a specific time code */
static int materialize_float_at(const LusdLayer_T* L, LusdValueRep rep,
                                 double time_code, float* out) {
    if (!L || rep.data == 0) return -1;
    int type_id = lusd_vrep_type(rep);

    if (type_id != LUSD_CRATE_TIME_SAMPLES)
        return materialize_float(L, rep, out);

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return -1;

    LusdValueRep sample_rep;
    sample_rep.data = ((uint64_t)LUSD_CRATE_FLOAT << LUSD_VREP_TYPE_SHIFT)
                      | (s->text_offset & LUSD_VREP_PAYLOAD_MASK);
    return materialize_float(L, sample_rep, out);
}

/* materialize_quat at a specific time code */
static int materialize_quat_at(const LusdLayer_T* L, LusdValueRep rep,
                                double time_code, double out[4]) {
    if (!L || rep.data == 0) return -1;
    int type_id = lusd_vrep_type(rep);

    if (type_id != LUSD_CRATE_TIME_SAMPLES)
        return materialize_quat(L, rep, out);

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return -1;

    LusdValueRep sample_rep;
    sample_rep.data = ((uint64_t)LUSD_CRATE_QUATF << LUSD_VREP_TYPE_SHIFT)
                      | (s->text_offset & LUSD_VREP_PAYLOAD_MASK);
    return materialize_quat(L, sample_rep, out);
}

/* Apply a single xformOp at a specific time code */
static int apply_xform_op_at(const LusdLayer_T* L, const LusdPrim_T* P,
                              const char* op_name, double time_code,
                              double accum[16]) {
    if (!op_name) return -1;

    LusdValueRep rep = find_field(L, P, op_name);
    if (lusd_vrep_is_null(rep)) return -1;

    double op_mat[16];

    if (strncmp(op_name, "xformOp:transform", 17) == 0) {
        if (materialize_matrix4d_at(L, rep, time_code, op_mat) != 0) return -1;
    } else if (strncmp(op_name, "xformOp:translate", 17) == 0) {
        double t[3];
        if (materialize_double3_at(L, rep, time_code, t) != 0) return -1;
        make_translate_matrix(t, op_mat);
    } else if (strncmp(op_name, "xformOp:scale", 13) == 0) {
        double s[3];
        if (materialize_double3_at(L, rep, time_code, s) != 0) return -1;
        make_scale_matrix(s, op_mat);
    } else if (strncmp(op_name, "xformOp:rotate", 14) == 0) {
        const char* after_rotate = op_name + 14;
        const char* colon = strchr(after_rotate, ':');
        size_t type_len = colon ? (size_t)(colon - after_rotate) : strlen(after_rotate);

        if (type_len == 1 && (after_rotate[0] == 'X' || after_rotate[0] == 'Y' || after_rotate[0] == 'Z')) {
            float angle_f;
            if (materialize_float_at(L, rep, time_code, &angle_f) != 0) return -1;
            int axis = (after_rotate[0] == 'X') ? 0 : (after_rotate[0] == 'Y') ? 1 : 2;
            make_single_rotate_matrix((double)angle_f, axis, op_mat);
        } else {
            double angles[3];
            if (materialize_double3_at(L, rep, time_code, angles) != 0) return -1;
            char tmp_name[32];
            snprintf(tmp_name, sizeof(tmp_name), "xformOp:rotate%.*s", (int)type_len, after_rotate);
            int order = parse_rotate_order(tmp_name);
            if (order < 0) return -1;
            make_rotate_matrix(angles, order, op_mat);
        }
    } else if (strncmp(op_name, "xformOp:orient", 14) == 0) {
        double q[4];
        if (materialize_quat_at(L, rep, time_code, q) != 0) return -1;
        make_quat_matrix(q, op_mat);
    } else {
        return -1;
    }

    mat4d_multiply(accum, op_mat, accum);
    return 0;
}

int lydra_c_extract_xform_at(LusdLayer layer, LusdPrim prim,
                              double time_code, double out_matrix[16]) {
    if (!layer || !prim || !out_matrix) return -1;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;

    /* Try xformOpOrder first */
    LusdValueRep order_rep = find_field(L, P, "xformOpOrder");
    if (!lusd_vrep_is_null(order_rep)) {
        const char** tokens = NULL;
        uint32_t count = 0;
        if (materialize_token_array(L, order_rep, &tokens, &count) == 0 && count > 0) {
            mat4d_identity(out_matrix);
            int any_applied = 0;
            for (uint32_t i = 0; i < count; i++) {
                if (tokens[i] && apply_xform_op_at(L, P, tokens[i], time_code, out_matrix) == 0)
                    any_applied = 1;
            }
            free(tokens);
            return any_applied ? 0 : -1;
        }
        free(tokens);
    }

    /* Fallback: direct xformOp:transform lookup with time sample */
    LusdValueRep rep = find_field(L, P, "xformOp:transform");
    if (lusd_vrep_is_null(rep)) return -1;
    return materialize_matrix4d_at(L, rep, time_code, out_matrix);
}

/* ================================================================
 * Attribute scalar read utility
 * ================================================================ */

int lydra_c_read_float_attr(LusdLayer layer, LusdPrim prim,
                             const char* attr_name, float* out) {
    if (!layer || !prim || !attr_name || !out) return -1;
    const LusdLayer_T* L = (const LusdLayer_T*)layer;
    const LusdPrim_T*  P = (const LusdPrim_T*)prim;
    LusdValueRep rep = find_field(L, P, attr_name);
    if (lusd_vrep_is_null(rep)) return -1;
    materialize_float(L, rep, out);
    return 0;
}

/* ================================================================
 * Asset path resolution utility
 * ================================================================ */

const char* lydra_c_resolve_asset_path(const char* base_dir,
                                        const char* asset_path,
                                        char* out_buf, uint32_t buf_size) {
    if (!asset_path || !out_buf || buf_size == 0) return NULL;

    /* Skip "./" prefix */
    if (asset_path[0] == '.' && (asset_path[1] == '/' || asset_path[1] == '\\'))
        asset_path += 2;

    /* Absolute path: copy as-is */
    if (asset_path[0] == '/') {
        size_t len = strlen(asset_path);
        if (len >= buf_size) return NULL;
        memcpy(out_buf, asset_path, len + 1);
    } else if (!base_dir || base_dir[0] == '\0') {
        size_t len = strlen(asset_path);
        if (len >= buf_size) return NULL;
        memcpy(out_buf, asset_path, len + 1);
    } else {
        size_t blen = strlen(base_dir);
        size_t alen = strlen(asset_path);
        if (blen + 1 + alen >= buf_size) return NULL;
        memcpy(out_buf, base_dir, blen);
        out_buf[blen] = '/';
        memcpy(out_buf + blen + 1, asset_path, alen + 1);
    }

    /* Normalize backslashes */
    for (char* p = out_buf; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    return out_buf;
}
