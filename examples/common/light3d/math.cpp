#include "light3d/math.h"
#include <cmath>

namespace light3d {

// --- Quat ---

Quat Quat::fromAxisAngle(const Vec3& axis, float angleRadians) {
    float half = angleRadians * 0.5f;
    float s = std::sin(half);
    Vec3 n = normalize(axis);
    return {n.x * s, n.y * s, n.z * s, std::cos(half)};
}

Quat Quat::fromEuler(float pitchX, float yawY, float rollZ) {
    float cx = std::cos(pitchX * 0.5f);
    float sx = std::sin(pitchX * 0.5f);
    float cy = std::cos(yawY * 0.5f);
    float sy = std::sin(yawY * 0.5f);
    float cz = std::cos(rollZ * 0.5f);
    float sz = std::sin(rollZ * 0.5f);

    return {
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz
    };
}

Quat slerp(const Quat& a, const Quat& b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    // If dot is negative, negate one quat to take shorter arc
    Quat b2 = b;
    if (d < 0.0f) {
        d = -d;
        b2 = {-b.x, -b.y, -b.z, -b.w};
    }

    // If quaternions are very close, use linear interpolation
    if (d > 0.9995f) {
        Quat r = {
            a.x + (b2.x - a.x) * t,
            a.y + (b2.y - a.y) * t,
            a.z + (b2.z - a.z) * t,
            a.w + (b2.w - a.w) * t
        };
        return r.normalized();
    }

    float theta0 = std::acos(d);
    float theta = theta0 * t;
    float sinTheta = std::sin(theta);
    float sinTheta0 = std::sin(theta0);

    float s0 = std::cos(theta) - d * sinTheta / sinTheta0;
    float s1 = sinTheta / sinTheta0;

    return Quat{
        a.x * s0 + b2.x * s1,
        a.y * s0 + b2.y * s1,
        a.z * s0 + b2.z * s1,
        a.w * s0 + b2.w * s1
    }.normalized();
}

// --- Mat4 ---

Mat4 Mat4::fromQuat(const Quat& q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 r = identity();
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

Mat4 Mat4::trs(const Vec3& translation, const Quat& rotation, const Vec3& s) {
    Mat4 r = fromQuat(rotation);
    // Apply scale to rotation columns
    r.m[0]  *= s.x; r.m[1]  *= s.x; r.m[2]  *= s.x;
    r.m[4]  *= s.y; r.m[5]  *= s.y; r.m[6]  *= s.y;
    r.m[8]  *= s.z; r.m[9]  *= s.z; r.m[10] *= s.z;
    // Set translation
    r.m[12] = translation.x;
    r.m[13] = translation.y;
    r.m[14] = translation.z;
    return r;
}

Mat4 Mat4::inverse() const {
    Mat4 inv;
    const float* me = m;

    inv.m[0] = me[5]  * me[10] * me[15] - me[5]  * me[11] * me[14] -
               me[9]  * me[6]  * me[15] + me[9]  * me[7]  * me[14] +
               me[13] * me[6]  * me[11] - me[13] * me[7]  * me[10];

    inv.m[4] = -me[4] * me[10] * me[15] + me[4]  * me[11] * me[14] +
                me[8] * me[6]  * me[15] - me[8]  * me[7]  * me[14] -
               me[12] * me[6]  * me[11] + me[12] * me[7]  * me[10];

    inv.m[8] = me[4]  * me[9]  * me[15] - me[4]  * me[11] * me[13] -
               me[8]  * me[5]  * me[15] + me[8]  * me[7]  * me[13] +
               me[12] * me[5]  * me[11] - me[12] * me[7]  * me[9];

    inv.m[12] = -me[4] * me[9]  * me[14] + me[4]  * me[10] * me[13] +
                 me[8] * me[5]  * me[14] - me[8]  * me[6]  * me[13] -
                me[12] * me[5]  * me[10] + me[12] * me[6]  * me[9];

    inv.m[1] = -me[1] * me[10] * me[15] + me[1]  * me[11] * me[14] +
                me[9] * me[2]  * me[15] - me[9]  * me[3]  * me[14] -
               me[13] * me[2]  * me[11] + me[13] * me[3]  * me[10];

    inv.m[5] = me[0]  * me[10] * me[15] - me[0]  * me[11] * me[14] -
               me[8]  * me[2]  * me[15] + me[8]  * me[3]  * me[14] +
               me[12] * me[2]  * me[11] - me[12] * me[3]  * me[10];

    inv.m[9] = -me[0] * me[9]  * me[15] + me[0]  * me[11] * me[13] +
                me[8] * me[1]  * me[15] - me[8]  * me[3]  * me[13] -
               me[12] * me[1]  * me[11] + me[12] * me[3]  * me[9];

    inv.m[13] = me[0]  * me[9]  * me[14] - me[0]  * me[10] * me[13] -
                me[8]  * me[1]  * me[14] + me[8]  * me[2]  * me[13] +
                me[12] * me[1]  * me[10] - me[12] * me[2]  * me[9];

    inv.m[2] = me[1]  * me[6]  * me[15] - me[1]  * me[7]  * me[14] -
               me[5]  * me[2]  * me[15] + me[5]  * me[3]  * me[14] +
               me[13] * me[2]  * me[7]  - me[13] * me[3]  * me[6];

    inv.m[6] = -me[0] * me[6]  * me[15] + me[0]  * me[7]  * me[14] +
                me[4] * me[2]  * me[15] - me[4]  * me[3]  * me[14] -
               me[12] * me[2]  * me[7]  + me[12] * me[3]  * me[6];

    inv.m[10] = me[0]  * me[5]  * me[15] - me[0]  * me[7]  * me[13] -
                me[4]  * me[1]  * me[15] + me[4]  * me[3]  * me[13] +
                me[12] * me[1]  * me[7]  - me[12] * me[3]  * me[5];

    inv.m[14] = -me[0] * me[5]  * me[14] + me[0]  * me[6]  * me[13] +
                 me[4] * me[1]  * me[14] - me[4]  * me[2]  * me[13] -
                me[12] * me[1]  * me[6]  + me[12] * me[2]  * me[5];

    inv.m[3] = -me[1] * me[6]  * me[11] + me[1]  * me[7]  * me[10] +
                me[5] * me[2]  * me[11] - me[5]  * me[3]  * me[10] -
                me[9] * me[2]  * me[7]  + me[9]  * me[3]  * me[6];

    inv.m[7] = me[0]  * me[6]  * me[11] - me[0]  * me[7]  * me[10] -
               me[4]  * me[2]  * me[11] + me[4]  * me[3]  * me[10] +
               me[8]  * me[2]  * me[7]  - me[8]  * me[3]  * me[6];

    inv.m[11] = -me[0] * me[5]  * me[11] + me[0]  * me[7]  * me[9] +
                 me[4] * me[1]  * me[11] - me[4]  * me[3]  * me[9] -
                 me[8] * me[1]  * me[7]  + me[8]  * me[3]  * me[5];

    inv.m[15] = me[0]  * me[5]  * me[10] - me[0]  * me[6]  * me[9] -
                me[4]  * me[1]  * me[10] + me[4]  * me[2]  * me[9] +
                me[8]  * me[1]  * me[6]  - me[8]  * me[2]  * me[5];

    float det = me[0] * inv.m[0] + me[1] * inv.m[4] + me[2] * inv.m[8] + me[3] * inv.m[12];
    if (std::abs(det) < 1e-12f) {
        return identity();
    }

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) {
        inv.m[i] *= invDet;
    }
    return inv;
}

// --- View / Projection ---

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Vec3 f = normalize(target - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);

    Mat4 r = Mat4::identity();
    r.m[0]  = s.x;   r.m[4]  = s.y;   r.m[8]  = s.z;
    r.m[1]  = u.x;   r.m[5]  = u.y;   r.m[9]  = u.z;
    r.m[2]  = -f.x;  r.m[6]  = -f.y;  r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] =  dot(f, eye);
    return r;
}

Mat4 perspective(float fovYRadians, float aspect, float near, float far) {
    float tanHalf = std::tan(fovYRadians * 0.5f);
    Mat4 r;
    r.m[0]  = 1.0f / (aspect * tanHalf);
    r.m[5]  = 1.0f / tanHalf;
    r.m[10] = -(far + near) / (far - near);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * far * near) / (far - near);
    return r;
}

Mat4 perspectiveZeroOne(float fovYRadians, float aspect, float near, float far) {
    float tanHalf = std::tan(fovYRadians * 0.5f);
    Mat4 r;
    r.m[0]  = 1.0f / (aspect * tanHalf);
    r.m[5]  = 1.0f / tanHalf;
    r.m[10] = far / (near - far);
    r.m[11] = -1.0f;
    r.m[14] = (far * near) / (near - far);
    return r;
}

} // namespace light3d
