// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Transform utilities
// Mat4 operations, AABB transformation

#pragma once

#include "lydra.hh"
#include "lydra_mesh.hh"  // for AABB

#include <cstdint>

namespace lydra {

struct Mat4 {
    float m[16];  // column-major (OpenGL/Vulkan convention)

    static Mat4 identity();
    static Mat4 translate(float x, float y, float z);
    static Mat4 scale(float x, float y, float z);
    static Mat4 rotate_x(float radians);
    static Mat4 rotate_y(float radians);
    static Mat4 rotate_z(float radians);
    static Mat4 from_quaternion(float x, float y, float z, float w);

    Mat4 operator*(const Mat4& rhs) const;

    // Transform vec3 positions (with perspective divide)
    void transform_points(Span<const float> in, float* out, uint32_t count) const;

    // Transform vec3 normals (upper-left 3x3, re-normalized)
    void transform_normals(Span<const float> in, float* out, uint32_t count) const;
};

AABB transform_aabb(const AABB& box, const Mat4& m);

}  // namespace lydra
