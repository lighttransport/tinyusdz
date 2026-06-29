#include <light3d/camera.h>

namespace light3d {

// ---------------------------------------------------------------------------
// Frustum — Gribb-Hartmann extraction from a VP matrix (column-major)
// ---------------------------------------------------------------------------
// Row i of the matrix (in column-major storage):
//   row[i] = { m[0*4+i], m[1*4+i], m[2*4+i], m[3*4+i] }
// Planes are: Left  = row3 + row0
//             Right = row3 - row0
//             Bottom= row3 + row1
//             Top   = row3 - row1
//             Near  = row3 + row2
//             Far   = row3 - row2

Frustum Frustum::fromViewProjection(const Mat4& vp) {
    Frustum f;
    const float* m = vp.m;

    // Left
    f.planes[0] = {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]};
    // Right
    f.planes[1] = {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]};
    // Bottom
    f.planes[2] = {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]};
    // Top
    f.planes[3] = {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]};
    // Near
    f.planes[4] = {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]};
    // Far
    f.planes[5] = {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]};

    for (int i = 0; i < 6; ++i)
        f.planes[i].normalize();

    return f;
}

// ---------------------------------------------------------------------------
// Frustum::testAABB — p-vertex / n-vertex method
// ---------------------------------------------------------------------------
CullResult Frustum::testAABB(const Vec3& mn, const Vec3& mx) const {
    CullResult result = CullResult::Inside;

    for (int i = 0; i < 6; ++i) {
        const Plane& pl = planes[i];

        // p-vertex: the AABB corner most along the plane normal
        Vec3 pv;
        pv.x = (pl.a >= 0.0f) ? mx.x : mn.x;
        pv.y = (pl.b >= 0.0f) ? mx.y : mn.y;
        pv.z = (pl.c >= 0.0f) ? mx.z : mn.z;

        // n-vertex: the AABB corner least along the plane normal
        Vec3 nv;
        nv.x = (pl.a >= 0.0f) ? mn.x : mx.x;
        nv.y = (pl.b >= 0.0f) ? mn.y : mx.y;
        nv.z = (pl.c >= 0.0f) ? mn.z : mx.z;

        // If the p-vertex is outside, the entire box is outside
        if (pl.distanceTo(pv) < 0.0f)
            return CullResult::Outside;

        // If the n-vertex is outside, the box intersects this plane
        if (pl.distanceTo(nv) < 0.0f)
            result = CullResult::Intersecting;
    }

    return result;
}

// ---------------------------------------------------------------------------
// CameraView
// ---------------------------------------------------------------------------
Mat4 CameraView::getViewMatrix() const {
    return lookAt(position, target, up);
}

Mat4 CameraView::getProjectionMatrix() const {
    float fovRad = fovYDeg * (3.14159265f / 180.0f);
    return perspective(fovRad, aspectRatio, nearPlane, farPlane);
}

Mat4 CameraView::getProjectionMatrixZeroOne() const {
    float fovRad = fovYDeg * (3.14159265f / 180.0f);
    return perspectiveZeroOne(fovRad, aspectRatio, nearPlane, farPlane);
}

Mat4 CameraView::getViewProjectionMatrix() const {
    return getProjectionMatrix() * getViewMatrix();
}

Mat4 CameraView::getViewProjectionMatrixZeroOne() const {
    return getProjectionMatrixZeroOne() * getViewMatrix();
}

Frustum CameraView::getFrustum() const {
    return Frustum::fromViewProjection(getViewProjectionMatrix());
}

} // namespace light3d
