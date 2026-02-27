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

#include <stdlib.h>
#include <string.h>

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

    /* normals (optional) */
    {
        LusdValueRep rep = find_field(L, P, "normals");
        if (rep.data != 0) {
            float* data = NULL; uint64_t count = 0;
            if (materialize_float3_array(L, rep, &data, &count) == 0) {
                out->normals = data;
                out->normal_count = (uint32_t)count;
            }
        }
    }

    /* primvars:st (optional UV) */
    {
        LusdValueRep rep = find_field(L, P, "primvars:st");
        if (rep.data != 0) {
            float* data = NULL; uint64_t count = 0;
            if (materialize_float2_array(L, rep, &data, &count) == 0) {
                out->uvs = data;
                out->uv_count = (uint32_t)count;
            }
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
