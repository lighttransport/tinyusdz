// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Scene module
//
// Value materialization helpers that read USD typed arrays from a
// LusdLayer_T's raw file buffer using the lazy LusdValueRep model.
//
// lightusd-c (C11) owns the raw tables; Lydra (C++17) owns typed extraction.
// The functions below cast opaque C handles to their internal *_T types and
// read directly from layer->file_data at the offsets stored in ValueRep.

#pragma once

#include "lydra.hh"
#include "lydra_mesh.hh"
#include "lydra_material.hh"

#include <cstdint>
#include <vector>

// Forward-declare internal C types so this header compiles without pulling in
// the full internal headers.  The .cc file includes them.
struct LusdLayer_T;
struct LusdPrim_T;

// Opaque C handle typedefs (match lightusd/lusd_handles.h)
typedef struct LusdLayer_T* LusdLayer;
typedef struct LusdPrim_T*  LusdPrim;

namespace lydra {

// ============================================================================
// MeshData — raw USD Mesh attribute arrays
// Positions, topology, and optional normals / UV coordinates.
// All float data is xyz-interleaved; indices are per-polygon-vertex.
// ============================================================================
struct MeshData {
    std::vector<float>   points;               // vec3f[] — N*3 floats
    std::vector<int32_t> face_vertex_counts;   // int[]
    std::vector<int32_t> face_vertex_indices;  // int[]
    std::vector<float>   normals;              // vec3f[] (optional)
    std::vector<float>   uvs;                  // vec2f[] (optional, primvar:st)
};

// ============================================================================
// find_field
//
// Find a named field in a prim's fieldset.
// Returns a null ValueRep (data == 0) if not found.
// ============================================================================
struct LusdValueRep_t { uint64_t data; };
LusdValueRep_t find_field(const LusdLayer_T* L, const LusdPrim_T* P,
                           const char* name);

// ============================================================================
// Array materialization
//
// Each function reads from L->file_data at the offset stored in the ValueRep
// payload, decompressing with LZ4 if the IsCompressed bit is set.
//
// The on-disk format at the payload offset is:
//   uint64_t numElements
//   [if IsCompressed] uint64_t compressedByteSize
//   [if IsCompressed] compressedByteSize bytes  (TfFastCompression / LZ4)
//   [if !IsCompressed] numElements * sizeof(T) raw bytes
// ============================================================================

// Materialize a float array from a VEC3F/NORMAL3F/POINT3F ValueRep.
// Returns a vector of numElements*3 floats on success.
Result<std::vector<float>> materialize_float3_array(const LusdLayer_T* L,
                                                     LusdValueRep_t     rep);

// Materialize an int32 array from an INT ValueRep.
// Returns a vector of numElements int32_t values on success.
Result<std::vector<int32_t>> materialize_int_array(const LusdLayer_T* L,
                                                    LusdValueRep_t     rep);

// Materialize a float2 (UV) array from a TEXCOORD2F/VEC2F ValueRep.
// Returns a vector of numElements*2 floats on success.
Result<std::vector<float>> materialize_float2_array(const LusdLayer_T* L,
                                                     LusdValueRep_t     rep);

// ============================================================================
// Time-samples helpers
//
// get_time_codes — returns all time codes for a LUSD_CRATE_TIME_SAMPLES field.
// materialize_*_at — materialize the typed array at the requested time code.
//   Uses hold-last semantics: if time_code < first sample, the first sample
//   is returned; if time_code > last sample, the last sample is returned.
//   Dedup-canonical entries are resolved transparently so that identical
//   arrays at different time codes are parsed only once.
// ============================================================================

Result<std::vector<double>> get_time_codes(const LusdLayer_T* L,
                                            LusdValueRep_t     rep);

Result<std::vector<float>> materialize_float3_array_at(const LusdLayer_T* L,
                                                        LusdValueRep_t     rep,
                                                        double             time_code);

Result<std::vector<float>> materialize_float4_array_at(const LusdLayer_T* L,
                                                        LusdValueRep_t     rep,
                                                        double             time_code);

Result<std::vector<int32_t>> materialize_int_array_at(const LusdLayer_T* L,
                                                       LusdValueRep_t     rep,
                                                       double             time_code);

// ============================================================================
// High-level mesh extraction
// ============================================================================
Result<MeshData> extract_mesh(LusdLayer layer, LusdPrim prim);

// ============================================================================
// High-level material extraction (returns a default FlatMaterial if
// no recognisable material fields are found on the prim)
// ============================================================================
Result<FlatMaterial> extract_material(LusdLayer layer, LusdPrim prim);

}  // namespace lydra
