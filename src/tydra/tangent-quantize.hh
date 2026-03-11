// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Tangent quantization for WebGL2 and GPU-friendly compact storage.
//
// Three packed formats:
//
//   1. INT_2_10_10_10_REV  (4 bytes)  — the WebGL2/GL standard for tangent
//      10-bit SNORM for xyz + 2-bit for handedness sign in w.
//      Precision: ~0.1° angular resolution, sufficient for normal mapping.
//      WebGL2: use with vertexAttribPointer(loc, 4, GL_INT_2_10_10_10_REV, true, ...)
//
//   2. SNORM8x4  (4 bytes)  — simplest, widest hardware support
//      8-bit SNORM for xyz + 8-bit for handedness sign in w.
//      Precision: ~0.7° angular resolution, acceptable for mobile/low-end.
//      WebGL2: use with vertexAttribPointer(loc, 4, GL_BYTE, true, ...)
//
//   3. FP16x4  (8 bytes)  — highest precision packed format
//      IEEE 754 half-float for xyz + sign in w.
//      Precision: ~0.006° angular resolution, exceeds normal map precision.
//      WebGL2: requires OES_texture_half_float or use with custom decode.
//
// All formats store tangent as vec4(T.x, T.y, T.z, sign) where:
//   sign = +1 or -1 (handedness / bitangent direction)
//   bitangent = cross(normal, tangent.xyz) * sign
//
// This eliminates the need to store binormals separately, cutting
// tangent frame storage from 24 bytes (float3+float3) to 4-8 bytes.
//
// GLSL reconstruction (all formats):
//   vec3 tangent = a_tangent.xyz;  // after attrib normalization
//   float sign = a_tangent.w;
//   vec3 bitangent = cross(v_normal, tangent) * sign;
//
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "value-types.hh"

namespace tinyusdz {
namespace tydra {
namespace tangent_quantize {

// ============================================================================
// Packed tangent types
// ============================================================================

/// INT_2_10_10_10_REV packed tangent (4 bytes).
/// Layout (LSB to MSB): x[10] y[10] z[10] w[2]
/// Each component is signed: x,y,z in [-511,511] mapped to [-1,1],
/// w in [-1,1] mapped from {-1,0,1} (only -1 and +1 used).
struct PackedTangent1010102 {
  uint32_t packed;
};

/// SNORM8x4 packed tangent (4 bytes).
/// Each component in [-127,127] mapped to [-1,1].
struct PackedTangentSNorm8x4 {
  int8_t x, y, z, w;
};

/// FP16x4 packed tangent (8 bytes).
/// IEEE 754 binary16 half-float for each component.
struct PackedTangentFp16x4 {
  uint16_t x, y, z, w;
};

// ============================================================================
// Float ↔ Half conversion (IEEE 754 binary16)
// ============================================================================

/// Convert float to IEEE 754 half-float (round-to-nearest-even).
inline uint16_t float_to_half(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);

  uint32_t sign = (bits >> 16) & 0x8000;
  uint32_t uexp = (bits >> 23) & 0xFF;
  uint32_t frac = bits & 0x7FFFFF;

  // Handle special cases
  if (uexp == 0xFF) {
    // Inf or NaN → fp16 inf or NaN
    if (frac == 0) return uint16_t(sign | 0x7C00); // inf
    return uint16_t(sign | 0x7E00); // quiet NaN
  }
  if (uexp == 0) {
    // fp32 zero or denorm → fp16 zero (too small)
    return uint16_t(sign);
  }

  int32_t exp = int32_t(uexp) - 127;

  if (exp > 15) {
    // Overflow → infinity
    return uint16_t(sign | 0x7C00);
  } else if (exp >= -14) {
    // Normal fp16 range
    uint32_t hfrac = (frac + 0x1000) >> 13; // round (add 0.5 ULP of fp16)
    if (hfrac > 0x3FF) { hfrac = 0; exp++; } // mantissa overflow
    if (exp > 15) return uint16_t(sign | 0x7C00); // promoted to inf
    return uint16_t(sign | uint32_t((exp + 15) << 10) | hfrac);
  } else if (exp >= -24) {
    // Denormalized fp16: value = frac16 * 2^(-14)
    // significand32 has implicit 1-bit: (1.frac) = (0x800000 | frac)
    uint32_t sig = 0x800000 | frac;
    int shift = -exp - 1; // shift right amount: 14 for exp=-15, 23 for exp=-24
    uint32_t round_bit = 1u << (shift - 1);
    uint32_t hfrac = (sig + round_bit) >> shift;
    if (hfrac > 0x3FF) hfrac = 0x3FF; // clamp
    return uint16_t(sign | hfrac);
  }
  // Underflow → zero
  return uint16_t(sign);
}

/// Convert IEEE 754 half-float to float.
inline float half_to_float(uint16_t h) {
  uint32_t sign = (uint32_t(h) & 0x8000) << 16;
  uint32_t exp = (h >> 10) & 0x1F;
  uint32_t frac = h & 0x3FF;

  uint32_t bits;
  if (exp == 0) {
    if (frac == 0) {
      bits = sign; // ±0
    } else {
      // Denormalized → normalize
      exp = 1;
      while (!(frac & 0x400)) { frac <<= 1; exp--; }
      frac &= 0x3FF;
      bits = sign | (uint32_t(exp + 127 - 15) << 23) | (frac << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000 | (frac << 13); // inf/nan
  } else {
    bits = sign | (uint32_t(exp + 127 - 15) << 23) | (frac << 13);
  }

  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// ============================================================================
// SNORM conversion helpers
// ============================================================================

/// Encode float [-1,1] to 10-bit signed integer [-511,511].
inline int32_t float_to_snorm10(float f) {
  f = f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
  // GL spec: round(clamp(f, -1, 1) * 511)
  return int32_t(std::round(f * 511.0f));
}

/// Encode float [-1,1] to 2-bit signed integer [-1,1].
inline int32_t float_to_snorm2(float f) {
  // Only -1 and +1 are meaningful for handedness
  return f >= 0.0f ? 1 : -1;
}

/// Encode float [-1,1] to 8-bit signed integer [-127,127].
inline int8_t float_to_snorm8(float f) {
  f = f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
  return int8_t(std::round(f * 127.0f));
}

/// Decode 10-bit SNORM to float.
inline float snorm10_to_float(int32_t v) {
  return float(v) / 511.0f;
}

/// Decode 8-bit SNORM to float.
inline float snorm8_to_float(int8_t v) {
  return float(v) / 127.0f;
}

// ============================================================================
// Pack / Unpack: INT_2_10_10_10_REV
// ============================================================================

/// Pack tangent vec3 + sign into INT_2_10_10_10_REV.
inline PackedTangent1010102 pack_tangent_1010102(float tx, float ty, float tz,
                                                  float sign) {
  // Encode each component as signed 10-bit / 2-bit
  int32_t ix = float_to_snorm10(tx) & 0x3FF;
  int32_t iy = float_to_snorm10(ty) & 0x3FF;
  int32_t iz = float_to_snorm10(tz) & 0x3FF;
  int32_t iw = float_to_snorm2(sign) & 0x3;

  return {uint32_t(ix) | (uint32_t(iy) << 10) | (uint32_t(iz) << 20) |
          (uint32_t(iw) << 30)};
}

/// Unpack INT_2_10_10_10_REV to tangent vec3 + sign.
inline void unpack_tangent_1010102(PackedTangent1010102 p, float &tx, float &ty,
                                    float &tz, float &sign) {
  // Sign-extend 10-bit values
  int32_t ix = int32_t(p.packed << 22) >> 22;
  int32_t iy = int32_t(p.packed << 12) >> 22;
  int32_t iz = int32_t(p.packed << 2) >> 22;
  int32_t iw = int32_t(p.packed) >> 30; // sign-extends 2-bit

  tx = snorm10_to_float(ix);
  ty = snorm10_to_float(iy);
  tz = snorm10_to_float(iz);
  sign = iw >= 0 ? 1.0f : -1.0f;
}

// ============================================================================
// Pack / Unpack: SNORM8x4
// ============================================================================

inline PackedTangentSNorm8x4 pack_tangent_snorm8(float tx, float ty, float tz,
                                                   float sign) {
  return {float_to_snorm8(tx), float_to_snorm8(ty), float_to_snorm8(tz),
          int8_t(sign >= 0.0f ? 127 : -127)};
}

inline void unpack_tangent_snorm8(PackedTangentSNorm8x4 p, float &tx,
                                   float &ty, float &tz, float &sign) {
  tx = snorm8_to_float(p.x);
  ty = snorm8_to_float(p.y);
  tz = snorm8_to_float(p.z);
  sign = p.w >= 0 ? 1.0f : -1.0f;
}

// ============================================================================
// Pack / Unpack: FP16x4
// ============================================================================

inline PackedTangentFp16x4 pack_tangent_fp16(float tx, float ty, float tz,
                                              float sign) {
  return {float_to_half(tx), float_to_half(ty), float_to_half(tz),
          float_to_half(sign >= 0.0f ? 1.0f : -1.0f)};
}

inline void unpack_tangent_fp16(PackedTangentFp16x4 p, float &tx, float &ty,
                                 float &tz, float &sign) {
  tx = half_to_float(p.x);
  ty = half_to_float(p.y);
  tz = half_to_float(p.z);
  sign = half_to_float(p.w) >= 0.0f ? 1.0f : -1.0f;
}

// ============================================================================
// Compute handedness sign from tangent, binormal, and normal
// ============================================================================

/// Compute the handedness sign: +1 if (T × B) · N > 0, else -1.
inline float compute_tangent_sign(const value::float3 &tangent,
                                   const value::float3 &binormal,
                                   const value::float3 &normal) {
  // cross(tangent, binormal)
  float cx = tangent[1] * binormal[2] - tangent[2] * binormal[1];
  float cy = tangent[2] * binormal[0] - tangent[0] * binormal[2];
  float cz = tangent[0] * binormal[1] - tangent[1] * binormal[0];
  // dot with normal
  float dot = cx * normal[0] + cy * normal[1] + cz * normal[2];
  return dot >= 0.0f ? 1.0f : -1.0f;
}

// ============================================================================
// Batch conversion: float3 tangent + float3 binormal + float3 normal
//                   → packed tangent vec4 (tangent.xyz + sign)
// ============================================================================

/// Quantize tangent frame to INT_2_10_10_10_REV (4 bytes per vertex).
/// Eliminates binormal storage; reconstruct in shader as cross(N,T)*sign.
inline bool QuantizeTangents1010102(
    const std::vector<value::float3> &tangents,
    const std::vector<value::float3> &binormals,
    const std::vector<value::float3> &normals,
    std::vector<PackedTangent1010102> *out,
    std::string *err) {

  if (tangents.size() != binormals.size() ||
      tangents.size() != normals.size()) {
    if (err) *err = "Input array size mismatch.";
    return false;
  }

  size_t n = tangents.size();
  out->resize(n);

  for (size_t i = 0; i < n; i++) {
    float sign = compute_tangent_sign(tangents[i], binormals[i], normals[i]);
    (*out)[i] = pack_tangent_1010102(tangents[i][0], tangents[i][1],
                                      tangents[i][2], sign);
  }

  return true;
}

/// Quantize tangent frame to SNORM8x4 (4 bytes per vertex).
inline bool QuantizeTangentsSNorm8(
    const std::vector<value::float3> &tangents,
    const std::vector<value::float3> &binormals,
    const std::vector<value::float3> &normals,
    std::vector<PackedTangentSNorm8x4> *out,
    std::string *err) {

  if (tangents.size() != binormals.size() ||
      tangents.size() != normals.size()) {
    if (err) *err = "Input array size mismatch.";
    return false;
  }

  size_t n = tangents.size();
  out->resize(n);

  for (size_t i = 0; i < n; i++) {
    float sign = compute_tangent_sign(tangents[i], binormals[i], normals[i]);
    (*out)[i] = pack_tangent_snorm8(tangents[i][0], tangents[i][1],
                                     tangents[i][2], sign);
  }

  return true;
}

/// Quantize tangent frame to FP16x4 (8 bytes per vertex).
inline bool QuantizeTangentsFp16(
    const std::vector<value::float3> &tangents,
    const std::vector<value::float3> &binormals,
    const std::vector<value::float3> &normals,
    std::vector<PackedTangentFp16x4> *out,
    std::string *err) {

  if (tangents.size() != binormals.size() ||
      tangents.size() != normals.size()) {
    if (err) *err = "Input array size mismatch.";
    return false;
  }

  size_t n = tangents.size();
  out->resize(n);

  for (size_t i = 0; i < n; i++) {
    float sign = compute_tangent_sign(tangents[i], binormals[i], normals[i]);
    (*out)[i] = pack_tangent_fp16(tangents[i][0], tangents[i][1],
                                   tangents[i][2], sign);
  }

  return true;
}

// PackToVertexAttribute helpers — only available when render-data.hh is included.
// Guard with the VertexAttribute struct existence.
#ifdef TINYUSDZ_TYDRA_RENDER_DATA_HH_

/// Store packed tangents into a VertexAttribute for use in RenderMesh.
/// Format: Uint (single uint32 per vertex) for 10_10_10_2.
inline VertexAttribute PackToVertexAttribute(
    const std::vector<PackedTangent1010102> &packed) {
  VertexAttribute attr;
  attr.format = VertexAttributeFormat::Uint;
  attr.data.resize(packed.size() * sizeof(uint32_t));
  std::memcpy(attr.data.data(), packed.data(),
              packed.size() * sizeof(uint32_t));
  return attr;
}

/// Store packed tangents into a VertexAttribute for use in RenderMesh.
/// Format: Char4 (4 × int8) for SNORM8x4.
inline VertexAttribute PackToVertexAttribute(
    const std::vector<PackedTangentSNorm8x4> &packed) {
  VertexAttribute attr;
  attr.format = VertexAttributeFormat::Char4;
  attr.data.resize(packed.size() * 4);
  std::memcpy(attr.data.data(), packed.data(), packed.size() * 4);
  return attr;
}

/// Store packed tangents into a VertexAttribute for use in RenderMesh.
/// Format: Half4 (4 × fp16) for FP16x4.
inline VertexAttribute PackToVertexAttribute(
    const std::vector<PackedTangentFp16x4> &packed) {
  VertexAttribute attr;
  attr.format = VertexAttributeFormat::Half4;
  attr.data.resize(packed.size() * sizeof(PackedTangentFp16x4));
  std::memcpy(attr.data.data(), packed.data(),
              packed.size() * sizeof(PackedTangentFp16x4));
  return attr;
}

#endif // TINYUSDZ_TYDRA_RENDER_DATA_HH_

// ============================================================================
// Quality measurement: angular error from round-trip quantization
// ============================================================================

struct QuantizeQuality {
  double max_angle_deg;
  double avg_angle_deg;
  double rms_angle_deg;
  size_t count;
  size_t sign_mismatches; // number of vertices where sign was lost
};

/// Measure round-trip quantization error.
template <typename PackedT, typename PackFn, typename UnpackFn>
inline QuantizeQuality MeasureQuantizeError(
    const std::vector<value::float3> &tangents,
    const std::vector<value::float3> &binormals,
    const std::vector<value::float3> &normals,
    PackFn packFn, UnpackFn unpackFn) {

  QuantizeQuality q = {0, 0, 0, 0, 0};
  size_t n = tangents.size();

  double sumAngle = 0, sumAngle2 = 0;
  for (size_t i = 0; i < n; i++) {
    float sign = compute_tangent_sign(tangents[i], binormals[i], normals[i]);
    PackedT packed = packFn(tangents[i][0], tangents[i][1], tangents[i][2], sign);
    float rx, ry, rz, rsign;
    unpackFn(packed, rx, ry, rz, rsign);

    // Skip if round-trip produced NaN/inf
    if (std::isnan(rx) || std::isnan(ry) || std::isnan(rz) ||
        std::isinf(rx) || std::isinf(ry) || std::isinf(rz)) {
      q.sign_mismatches++;
      continue;
    }

    // Direction error
    float lenR = std::sqrt(tangents[i][0]*tangents[i][0] +
                            tangents[i][1]*tangents[i][1] +
                            tangents[i][2]*tangents[i][2]);
    float lenQ = std::sqrt(rx*rx + ry*ry + rz*rz);
    float dot;
    if (lenR < 1e-10f || lenQ < 1e-10f) {
      dot = 1.0f;
    } else {
      dot = (tangents[i][0]*rx + tangents[i][1]*ry + tangents[i][2]*rz) /
            (lenR * lenQ);
      dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);
    }
    double angle = std::acos(double(dot)) * (180.0 / 3.14159265358979);
    if (angle > q.max_angle_deg) q.max_angle_deg = angle;
    sumAngle += angle;
    sumAngle2 += angle * angle;

    // Sign error
    if ((sign >= 0.0f) != (rsign >= 0.0f)) q.sign_mismatches++;
  }

  q.count = n;
  q.avg_angle_deg = n > 0 ? sumAngle / double(n) : 0;
  q.rms_angle_deg = n > 0 ? std::sqrt(sumAngle2 / double(n)) : 0;
  return q;
}

}  // namespace tangent_quantize
}  // namespace tydra
}  // namespace tinyusdz
