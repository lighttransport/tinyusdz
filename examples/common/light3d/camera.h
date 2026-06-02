#pragma once

#include "math.h"
#include <cmath>

namespace light3d {

// ---------------------------------------------------------------------------
// Plane (ax + by + cz + d = 0)
// ---------------------------------------------------------------------------
struct Plane {
    float a, b, c, d;

    float distanceTo(const Vec3& p) const {
        return a * p.x + b * p.y + c * p.z + d;
    }

    void normalize() {
        float len = std::sqrt(a * a + b * b + c * c);
        if (len < 1e-8f) return;
        float inv = 1.0f / len;
        a *= inv;
        b *= inv;
        c *= inv;
        d *= inv;
    }
};

// ---------------------------------------------------------------------------
// CullResult
// ---------------------------------------------------------------------------
enum class CullResult { Outside, Intersecting, Inside };

// ---------------------------------------------------------------------------
// Frustum — 6 planes: Left, Right, Bottom, Top, Near, Far
// ---------------------------------------------------------------------------
struct Frustum {
    Plane planes[6];

    // Gribb-Hartmann method: extract frustum planes from a view-projection matrix.
    // The matrix must be column-major (OpenGL convention).
    static Frustum fromViewProjection(const Mat4& vp);

    // Test an axis-aligned bounding box against the frustum (p-vertex/n-vertex).
    CullResult testAABB(const Vec3& mn, const Vec3& mx) const;
};

// ---------------------------------------------------------------------------
// CameraView — wraps view/projection parameters
// ---------------------------------------------------------------------------
class CameraView {
public:
    float fovYDeg = 60.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    Vec3 position{0.0f, 0.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};

    Mat4 getViewMatrix() const;
    Mat4 getProjectionMatrix() const;          // OpenGL: Z → [-1, 1]
    Mat4 getProjectionMatrixZeroOne() const;    // WebGPU/Vulkan: Z → [0, 1]
    Mat4 getViewProjectionMatrix() const;
    Mat4 getViewProjectionMatrixZeroOne() const;
    Frustum getFrustum() const;
};

} // namespace light3d
