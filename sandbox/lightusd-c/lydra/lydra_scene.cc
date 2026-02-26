// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Scene module implementation
//
// Value materialization: reads typed arrays from LusdLayer_T's raw file buffer.
// Includes the lightusd-c internal headers directly (privileged access) to
// avoid an additional abstraction layer between Lydra and the parsed tables.

#include "lydra_scene.hh"

// Internal lightusd-c headers (added to private includes via CMakeLists.txt)
#include "internal/lusd_layer_internal.h"
#include "internal/lusd_value_rep.h"

// Format tag constants
#ifndef LUSD_FORMAT_USDC
#  define LUSD_FORMAT_USDC 0
#  define LUSD_FORMAT_USDA 1
#endif

// LZ4 decompressor (vendored copy in lightusd-c/src/lz4/)
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-conversion"
#  pragma GCC diagnostic ignored "-Wconversion"
#endif
#define LZ4_DISABLE_DEPRECATE_WARNINGS
#include "lz4/lz4.h"
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <cstring>
#include <cstdlib>
#include <string>

namespace lydra {

// ============================================================================
// Format helpers
// ============================================================================

static bool is_usda_layer(const LusdLayer_T* L) {
    return L && L->format == LUSD_FORMAT_USDA;
}

// ============================================================================
// USDA text array parser
//
// Parses ASCII array text starting at file_data + text_offset.
// Supports:
//   "[int, int, ...]"                    element_size=4 (int32)
//   "[float, float, ...]"                element_size=4 (float)
//   "[(x,y), ...]"                       element_size=8 (float2)
//   "[(x,y,z), ...]"                     element_size=12 (float3)
//   "[(x,y,z,w), ...]"                   element_size=16 (float4)
// Returns heap-allocated binary buffer; *count_out = element count.
// *tmp_buf_out receives the same pointer (caller must free).
// Returns nullptr on failure.
// ============================================================================

static const uint8_t* read_usda_array_bytes(
        const LusdLayer_T* L,
        uint64_t            text_offset,
        size_t              element_size,
        bool                as_int,        /* true → parse scalars with strtol */
        uint64_t*           count_out,
        uint8_t**           tmp_buf_out) {
    *count_out   = 0;
    *tmp_buf_out = nullptr;

    if (!L || !L->file_data) return nullptr;
    if (text_offset >= L->file_size) return nullptr;

    const char* p   = reinterpret_cast<const char*>(L->file_data) + text_offset;
    const char* end = reinterpret_cast<const char*>(L->file_data) + L->file_size;

    // skip whitespace
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end || *p != '[') return nullptr;
    p++; // consume '['

    // Check for empty array
    {
        const char* scan = p;
        while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\n' || *scan == '\r')) scan++;
        if (scan >= end || *scan == ']') {
            *count_out = 0;
            return nullptr;
        }
    }

    // First pass: count top-level commas → elements = commas_at_depth_0 + 1
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

    // Allocate output buffer
    size_t total_bytes = static_cast<size_t>(count) * element_size;
    uint8_t* buf = static_cast<uint8_t*>(malloc(total_bytes));
    if (!buf) return nullptr;
    memset(buf, 0, total_bytes);

    // Second pass: parse values
    uint64_t elem = 0;
    while (p < end && elem < count) {
        // skip whitespace and commas between elements
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;
        if (p >= end || *p == ']') break;

        if (element_size == sizeof(int32_t) && as_int) {
            // integer scalar: use strtol to get the proper binary int value
            char* endp;
            long v = strtol(p, &endp, 10);
            int32_t iv = (int32_t)v;
            memcpy(buf + elem * sizeof(int32_t), &iv, sizeof(int32_t));
            p = endp;
        } else if (element_size == sizeof(float)) {
            // float scalar
            char* endp;
            float fv = strtof(p, &endp);
            memcpy(buf + elem * sizeof(float), &fv, sizeof(float));
            p = endp;
        } else {
            // tuple: expect '(' x, y [, z [, w]] ')'
            if (*p == '(') p++;
            uint32_t ncomp = (uint32_t)(element_size / sizeof(float));
            for (uint32_t comp = 0; comp < ncomp; comp++) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
                char* endp;
                float fv = strtof(p, &endp);
                size_t dst_off = elem * element_size + comp * sizeof(float);
                memcpy(buf + dst_off, &fv, sizeof(float));
                p = endp;
            }
            // consume closing ')'
            while (p < end && *p != ')' && *p != ',' && *p != ']') p++;
            if (p < end && *p == ')') p++;
        }
        elem++;
    }

    *count_out   = elem;
    *tmp_buf_out = buf;
    return buf;
}

// ============================================================================
// find_field — thin wrapper around lusd__layer_find_field
// ============================================================================

LusdValueRep_t find_field(const LusdLayer_T* L, const LusdPrim_T* P,
                           const char* name) {
    LusdValueRep r = lusd__layer_find_field(L, P, name);
    return LusdValueRep_t{ r.data };
}

// ============================================================================
// Internal: decompress TfFastCompression / LZ4 block data
//
// The lightusd-c lusd_lz4.c wrapper handles the nChunks header byte.
// We declare it here as an extern C function rather than pulling in
// lusd_lz4.c's private header.
// ============================================================================

extern "C" uint64_t lusd__lz4_decompress(const uint8_t* comp,
                                          uint64_t       comp_size,
                                          uint8_t*       out,
                                          uint64_t       max_out);

// ============================================================================
// Internal: read the array header and return the data pointer + element count
//
// Layout at `offset` in file_data:
//   uint64_t numElements
//   [if compressed] uint64_t compressedByteSize
//   [if compressed] compressedByteSize bytes  (TfFastCompression)
//   [if !compressed] numElements * element_size  raw bytes
//
// On success: *count_out = numElements, return pointer to element bytes
//             (either directly into file_data or into *tmp_buf which is
//              malloc'd by this function and must be free'd by the caller)
// On failure: returns nullptr; *tmp_buf is always nullptr on failure
// ============================================================================

static const uint8_t* read_array_bytes(const LusdLayer_T* L,
                                        uint64_t            offset,
                                        bool                is_compressed,
                                        size_t              element_size,
                                        uint64_t*           count_out,
                                        uint8_t**           tmp_buf) {
    *tmp_buf   = nullptr;
    *count_out = 0;

    // Bounds-check: need at least the count field
    if (offset + 8 > L->file_size) return nullptr;

    uint64_t numElements;
    memcpy(&numElements, L->file_data + offset, 8);
    offset += 8;

    if (numElements == 0) {
        *count_out = 0;
        return nullptr;  // callers guard with 'if (count > 0)' before memcpy
    }

    uint64_t uncompressed_bytes = numElements * element_size;

    if (is_compressed) {
        // Read compressed byte count
        if (offset + 8 > L->file_size) return nullptr;
        uint64_t compByteSize;
        memcpy(&compByteSize, L->file_data + offset, 8);
        offset += 8;

        if (compByteSize == 0 || offset + compByteSize > L->file_size) return nullptr;

        // Allocate output buffer
        uint8_t* out = static_cast<uint8_t*>(malloc(static_cast<size_t>(uncompressed_bytes)));
        if (!out) return nullptr;

        uint64_t nDec = lusd__lz4_decompress(L->file_data + offset,
                                              compByteSize,
                                              out,
                                              uncompressed_bytes);
        if (nDec != uncompressed_bytes) {
            free(out);
            return nullptr;
        }

        *tmp_buf   = out;
        *count_out = numElements;
        return out;
    } else {
        // Raw bytes
        if (offset + uncompressed_bytes > L->file_size) return nullptr;
        *count_out = numElements;
        return L->file_data + offset;
    }
}

// ============================================================================
// materialize_float3_array
// ============================================================================

Result<std::vector<float>> materialize_float3_array(const LusdLayer_T* L,
                                                     LusdValueRep_t     rep) {
    using R = Result<std::vector<float>>;

    if (!L || rep.data == 0)
        return R::err("null layer or null ValueRep");

    // Transparent forward for time-sampled attributes: use time 0.0
    if (static_cast<int>((rep.data & LUSD_VREP_TYPE_MASK) >> LUSD_VREP_TYPE_SHIFT)
            == LUSD_CRATE_TIME_SAMPLES) {
        return materialize_float3_array_at(L, rep, 0.0);
    }

    if (!(rep.data & LUSD_VREP_IS_ARRAY_BIT))
        return R::err("ValueRep is not an array");

    if (rep.data & LUSD_VREP_IS_INLINED_BIT)
        return R::err("float3[] cannot be inlined");

    uint64_t offset      = rep.data & LUSD_VREP_PAYLOAD_MASK;
    bool     compressed  = (rep.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0;

    uint64_t count = 0;
    uint8_t* tmp   = nullptr;
    const uint8_t* src;
    if (is_usda_layer(L)) {
        src = read_usda_array_bytes(L, offset, 3 * sizeof(float), false, &count, &tmp);
    } else {
        src = read_array_bytes(L, offset, compressed, 3 * sizeof(float), &count, &tmp);
    }
    if (!src && count != 0)
        return R::err("failed to read float3 array from file");

    std::vector<float> result(static_cast<size_t>(count) * 3);
    if (count > 0)
        memcpy(result.data(), src, count * 3 * sizeof(float));

    free(tmp);
    return R::ok_value(std::move(result));
}

// ============================================================================
// materialize_float2_array
// ============================================================================

Result<std::vector<float>> materialize_float2_array(const LusdLayer_T* L,
                                                     LusdValueRep_t     rep) {
    using R = Result<std::vector<float>>;

    if (!L || rep.data == 0)
        return R::err("null layer or null ValueRep");

    if (!(rep.data & LUSD_VREP_IS_ARRAY_BIT))
        return R::err("ValueRep is not an array");

    if (rep.data & LUSD_VREP_IS_INLINED_BIT)
        return R::err("float2[] cannot be inlined");

    uint64_t offset     = rep.data & LUSD_VREP_PAYLOAD_MASK;
    bool     compressed = (rep.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0;

    uint64_t count = 0;
    uint8_t* tmp   = nullptr;
    const uint8_t* src;
    if (is_usda_layer(L)) {
        src = read_usda_array_bytes(L, offset, 2 * sizeof(float), false, &count, &tmp);
    } else {
        src = read_array_bytes(L, offset, compressed, 2 * sizeof(float), &count, &tmp);
    }
    if (!src && count != 0)
        return R::err("failed to read float2 array from file");

    std::vector<float> result(static_cast<size_t>(count) * 2);
    if (count > 0)
        memcpy(result.data(), src, count * 2 * sizeof(float));

    free(tmp);
    return R::ok_value(std::move(result));
}

// ============================================================================
// materialize_int_array
// ============================================================================

Result<std::vector<int32_t>> materialize_int_array(const LusdLayer_T* L,
                                                    LusdValueRep_t     rep) {
    using R = Result<std::vector<int32_t>>;

    if (!L || rep.data == 0)
        return R::err("null layer or null ValueRep");

    if (!(rep.data & LUSD_VREP_IS_ARRAY_BIT))
        return R::err("ValueRep is not an array");

    if (rep.data & LUSD_VREP_IS_INLINED_BIT)
        return R::err("int[] cannot be inlined");

    uint64_t offset     = rep.data & LUSD_VREP_PAYLOAD_MASK;
    bool     compressed = (rep.data & LUSD_VREP_IS_COMPRESSED_BIT) != 0;

    uint64_t count = 0;
    uint8_t* tmp   = nullptr;
    const uint8_t* src;
    if (is_usda_layer(L)) {
        src = read_usda_array_bytes(L, offset, sizeof(int32_t), true, &count, &tmp);
    } else {
        src = read_array_bytes(L, offset, compressed, sizeof(int32_t), &count, &tmp);
    }
    if (!src && count != 0)
        return R::err("failed to read int array from file");

    std::vector<int32_t> result(static_cast<size_t>(count));
    if (count > 0)
        memcpy(result.data(), src, count * sizeof(int32_t));

    free(tmp);
    return R::ok_value(std::move(result));
}

// ============================================================================
// extract_mesh
// ============================================================================

// ============================================================================
// Time-samples helpers
// ============================================================================

// Decode start / count from a LUSD_CRATE_TIME_SAMPLES ValueRep payload.
static inline uint32_t ts_start(uint64_t payload) {
    return static_cast<uint32_t>(payload >> 24);
}
static inline uint32_t ts_count(uint64_t payload) {
    return static_cast<uint32_t>(payload & 0xFFFFFFu);
}

// Find the canonical LusdTimeSample for a given time_code (hold-last semantics).
// Returns nullptr if the attribute has no samples or the table is missing.
static const LusdTimeSample* find_time_sample(const LusdLayer_T* L,
                                               uint64_t           payload,
                                               double             time_code) {
    uint32_t start = ts_start(payload);
    uint32_t count = ts_count(payload);
    if (count == 0 || !L->time_samples) return nullptr;

    // Hold-first: pick the last sample whose time <= time_code.
    // Fall back to the earliest sample if time_code precedes all samples.
    const LusdTimeSample* best = &L->time_samples[start];
    for (uint32_t i = 0; i < count; i++) {
        const LusdTimeSample* s = &L->time_samples[start + i];
        if (s->time <= time_code)
            best = s;
        else if (i == 0)
            best = s; // hold-first before all samples
        else
            break;
    }
    // Resolve dedup: use the canonical entry's data
    uint32_t canon = best->canonical_idx;
    // Safety: canonical_idx must be in [start, start+count)
    if (canon < start || canon >= start + count)
        canon = static_cast<uint32_t>(best - L->time_samples);
    return &L->time_samples[canon];
}

Result<std::vector<double>> get_time_codes(const LusdLayer_T* L,
                                            LusdValueRep_t     rep) {
    using R = Result<std::vector<double>>;
    if (!L || rep.data == 0) return R::err("null layer or ValueRep");
    int tid = static_cast<int>((rep.data & LUSD_VREP_TYPE_MASK) >> LUSD_VREP_TYPE_SHIFT);
    if (tid != LUSD_CRATE_TIME_SAMPLES)
        return R::err("ValueRep is not a time-samples field");

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    uint32_t start   = ts_start(payload);
    uint32_t count   = ts_count(payload);

    std::vector<double> result(count);
    for (uint32_t i = 0; i < count; i++)
        result[i] = L->time_samples[start + i].time;
    return R::ok_value(std::move(result));
}

Result<std::vector<float>> materialize_float3_array_at(const LusdLayer_T* L,
                                                        LusdValueRep_t     rep,
                                                        double             time_code) {
    using R = Result<std::vector<float>>;
    if (!L || rep.data == 0) return R::err("null layer or ValueRep");

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return R::err("no time samples");

    uint64_t count = 0;
    uint8_t* tmp   = nullptr;
    const uint8_t* src = read_usda_array_bytes(L, s->text_offset,
                                                3 * sizeof(float), false,
                                                &count, &tmp);
    if (!src) return R::err("failed to parse float3[] at time");
    std::vector<float> result(static_cast<size_t>(count) * 3);
    if (count > 0) memcpy(result.data(), src, count * 3 * sizeof(float));
    free(tmp);
    return R::ok_value(std::move(result));
}

Result<std::vector<float>> materialize_float4_array_at(const LusdLayer_T* L,
                                                        LusdValueRep_t     rep,
                                                        double             time_code) {
    using R = Result<std::vector<float>>;
    if (!L || rep.data == 0) return R::err("null layer or ValueRep");

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return R::err("no time samples");

    uint64_t count = 0;
    uint8_t* tmp   = nullptr;
    const uint8_t* src = read_usda_array_bytes(L, s->text_offset,
                                                4 * sizeof(float), false,
                                                &count, &tmp);
    if (!src) return R::err("failed to parse float4[] at time");
    std::vector<float> result(static_cast<size_t>(count) * 4);
    if (count > 0) memcpy(result.data(), src, count * 4 * sizeof(float));
    free(tmp);
    return R::ok_value(std::move(result));
}

Result<std::vector<int32_t>> materialize_int_array_at(const LusdLayer_T* L,
                                                       LusdValueRep_t     rep,
                                                       double             time_code) {
    using R = Result<std::vector<int32_t>>;
    if (!L || rep.data == 0) return R::err("null layer or ValueRep");

    uint64_t payload = rep.data & LUSD_VREP_PAYLOAD_MASK;
    const LusdTimeSample* s = find_time_sample(L, payload, time_code);
    if (!s) return R::err("no time samples");

    uint64_t count = 0;
    uint8_t* tmp   = nullptr;
    const uint8_t* src = read_usda_array_bytes(L, s->text_offset,
                                                sizeof(int32_t), true,
                                                &count, &tmp);
    if (!src) return R::err("failed to parse int[] at time");
    std::vector<int32_t> result(static_cast<size_t>(count));
    if (count > 0) memcpy(result.data(), src, count * sizeof(int32_t));
    free(tmp);
    return R::ok_value(std::move(result));
}

Result<MeshData> extract_mesh(LusdLayer layer, LusdPrim prim) {
    using R = Result<MeshData>;

    if (!layer || !prim) return R::err("null layer or prim handle");

    const LusdLayer_T* L = static_cast<const LusdLayer_T*>(layer);
    const LusdPrim_T*  P = static_cast<const LusdPrim_T*>(prim);

    MeshData mesh;

    // --- points (required) -----------------------------------------------
    {
        auto rep = find_field(L, P, "points");
        if (rep.data == 0) return R::err("Mesh prim missing 'points' field");
        auto res = materialize_float3_array(L, rep);
        if (!res.ok()) return R::err("points: " + res.error->message);
        mesh.points = std::move(*res);
    }

    // --- faceVertexCounts (required) -------------------------------------
    {
        auto rep = find_field(L, P, "faceVertexCounts");
        if (rep.data == 0) return R::err("Mesh prim missing 'faceVertexCounts'");
        auto res = materialize_int_array(L, rep);
        if (!res.ok()) return R::err("faceVertexCounts: " + res.error->message);
        mesh.face_vertex_counts = std::move(*res);
    }

    // --- faceVertexIndices (required) ------------------------------------
    {
        auto rep = find_field(L, P, "faceVertexIndices");
        if (rep.data == 0) return R::err("Mesh prim missing 'faceVertexIndices'");
        auto res = materialize_int_array(L, rep);
        if (!res.ok()) return R::err("faceVertexIndices: " + res.error->message);
        mesh.face_vertex_indices = std::move(*res);
    }

    // --- normals (optional) ---------------------------------------------
    {
        auto rep = find_field(L, P, "normals");
        if (rep.data != 0) {
            auto res = materialize_float3_array(L, rep);
            if (res.ok()) mesh.normals = std::move(*res);
            // Non-fatal if missing or decode fails
        }
    }

    // --- primvars:st / primvars:st:indices (optional UV) ----------------
    {
        auto rep = find_field(L, P, "primvars:st");
        if (rep.data != 0) {
            auto res = materialize_float2_array(L, rep);
            if (res.ok()) mesh.uvs = std::move(*res);
        }
    }

    return R::ok_value(std::move(mesh));
}

// ============================================================================
// extract_material — returns a default FlatMaterial; full PBR extraction
// requires walking the material binding relationship (future work)
// ============================================================================

Result<FlatMaterial> extract_material(LusdLayer layer, LusdPrim prim) {
    using R = Result<FlatMaterial>;
    (void)layer; (void)prim;
    // Return a default grey PBR material until full material binding is wired
    return R::ok_value(FlatMaterial{});
}

}  // namespace lydra
