// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra - Transform utilities implementation

#include "lydra_transform.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace lydra {

// ============================================================================
// Mat4 Implementation (column-major)
// ============================================================================
// Layout: m[col*4 + row], matching OpenGL/Vulkan convention
// m[0]  m[4]  m[8]   m[12]     (col0.x  col1.x  col2.x  col3.x)
// m[1]  m[5]  m[9]   m[13]     (col0.y  col1.y  col2.y  col3.y)
// m[2]  m[6]  m[10]  m[14]     (col0.z  col1.z  col2.z  col3.z)
// m[3]  m[7]  m[11]  m[15]     (col0.w  col1.w  col2.w  col3.w)

Mat4 Mat4::identity() {
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = 1.0f;
    r.m[5] = 1.0f;
    r.m[10] = 1.0f;
    r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 Mat4::scale(float x, float y, float z) {
    Mat4 r = identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

Mat4 Mat4::rotate_x(float radians) {
    Mat4 r = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

Mat4 Mat4::rotate_y(float radians) {
    Mat4 r = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

Mat4 Mat4::rotate_z(float radians) {
    Mat4 r = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

Mat4 Mat4::from_quaternion(float x, float y, float z, float w) {
    Mat4 r = identity();

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;
    float wx = w * x;
    float wy = w * y;
    float wz = w * z;

    r.m[0]  = 1.0f - 2.0f * (yy + zz);
    r.m[1]  = 2.0f * (xy + wz);
    r.m[2]  = 2.0f * (xz - wy);

    r.m[4]  = 2.0f * (xy - wz);
    r.m[5]  = 1.0f - 2.0f * (xx + zz);
    r.m[6]  = 2.0f * (yz + wx);

    r.m[8]  = 2.0f * (xz + wy);
    r.m[9]  = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);

    return r;
}

Mat4 Mat4::operator*(const Mat4& rhs) const {
    Mat4 r;
    // Column-major: result[col][row] = sum_k lhs[k][row] * rhs[col][k]
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += m[k * 4 + row] * rhs.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

void Mat4::transform_points(Span<const float> in, float* out, uint32_t count) const {
    for (uint32_t i = 0; i < count; i++) {
        float px = in[i * 3 + 0];
        float py = in[i * 3 + 1];
        float pz = in[i * 3 + 2];

        float x = m[0] * px + m[4] * py + m[8]  * pz + m[12];
        float y = m[1] * px + m[5] * py + m[9]  * pz + m[13];
        float z = m[2] * px + m[6] * py + m[10] * pz + m[14];
        float w = m[3] * px + m[7] * py + m[11] * pz + m[15];

        if (w != 0.0f && w != 1.0f) {
            x /= w;
            y /= w;
            z /= w;
        }

        out[i * 3 + 0] = x;
        out[i * 3 + 1] = y;
        out[i * 3 + 2] = z;
    }
}

void Mat4::transform_normals(Span<const float> in, float* out, uint32_t count) const {
    // Transform using upper-left 3x3, then normalize
    for (uint32_t i = 0; i < count; i++) {
        float nx = in[i * 3 + 0];
        float ny = in[i * 3 + 1];
        float nz = in[i * 3 + 2];

        float x = m[0] * nx + m[4] * ny + m[8]  * nz;
        float y = m[1] * nx + m[5] * ny + m[9]  * nz;
        float z = m[2] * nx + m[6] * ny + m[10] * nz;

        float len = std::sqrt(x * x + y * y + z * z);
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
        }

        out[i * 3 + 0] = x;
        out[i * 3 + 1] = y;
        out[i * 3 + 2] = z;
    }
}

// ============================================================================
// AABB Transformation
// ============================================================================

AABB transform_aabb(const AABB& box, const Mat4& mat) {
    // Transform all 8 corners of the AABB and compute new bounds
    float corners[8][3] = {
        {box.min[0], box.min[1], box.min[2]},
        {box.max[0], box.min[1], box.min[2]},
        {box.min[0], box.max[1], box.min[2]},
        {box.max[0], box.max[1], box.min[2]},
        {box.min[0], box.min[1], box.max[2]},
        {box.max[0], box.min[1], box.max[2]},
        {box.min[0], box.max[1], box.max[2]},
        {box.max[0], box.max[1], box.max[2]},
    };

    AABB result;
    result.min[0] = result.min[1] = result.min[2] = std::numeric_limits<float>::max();
    result.max[0] = result.max[1] = result.max[2] = -std::numeric_limits<float>::max();

    for (int c = 0; c < 8; c++) {
        float transformed[3];
        Span<const float> in_span(corners[c], 3);
        mat.transform_points(in_span, transformed, 1);

        for (int j = 0; j < 3; j++) {
            result.min[j] = std::min(result.min[j], transformed[j]);
            result.max[j] = std::max(result.max[j], transformed[j]);
        }
    }

    return result;
}

}  // namespace lydra
