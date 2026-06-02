#pragma once

#include "light3d.h"
#include <cmath>
#include <cstring>

namespace light3d {

// --- Vec3 free-function operators ---

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline Vec3 operator*(float s, const Vec3& v) {
    return {v.x * s, v.y * s, v.z * s};
}

inline Vec3 operator-(const Vec3& v) {
    return {-v.x, -v.y, -v.z};
}

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    if (len < 1e-8f) return {0.0f, 0.0f, 0.0f};
    float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv};
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return a + (b - a) * t;
}

// --- Vec4 free-function operators ---

inline Vec4 operator+(const Vec4& a, const Vec4& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

inline Vec4 operator*(const Vec4& v, float s) {
    return {v.x * s, v.y * s, v.z * s, v.w * s};
}

// --- Quaternion ---

struct Quat {
    float x, y, z, w;

    Quat() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

    static Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    static Quat fromAxisAngle(const Vec3& axis, float angleRadians);
    static Quat fromEuler(float pitchX, float yawY, float rollZ);

    Quat conjugate() const { return {-x, -y, -z, w}; }

    float lengthSq() const { return x * x + y * y + z * z + w * w; }

    Quat normalized() const {
        float len = std::sqrt(lengthSq());
        if (len < 1e-8f) return identity();
        float inv = 1.0f / len;
        return {x * inv, y * inv, z * inv, w * inv};
    }
};

inline Quat operator*(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

Quat slerp(const Quat& a, const Quat& b, float t);

// --- 4x4 Column-Major Matrix ---

struct Mat4 {
    // Column-major: m[col][row], or flat m[col*4+row]
    float m[16];

    Mat4() { std::memset(m, 0, sizeof(m)); }

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 translate(const Vec3& t) {
        Mat4 r = identity();
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 fromQuat(const Quat& q);
    static Mat4 trs(const Vec3& translation, const Quat& rotation, const Vec3& scale);

    Mat4 inverse() const;

    const float* data() const { return m; }
    float* data() { return m; }

    // Access element by row and column: m[col*4 + row]
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return {x, y, z};
}

inline Vec3 transformVector(const Mat4& m, const Vec3& v) {
    float x = m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z;
    float y = m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z;
    float z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z;
    return {x, y, z};
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
Mat4 perspective(float fovYRadians, float aspect, float near, float far);
// Vulkan/WebGPU perspective: Z maps to [0, 1], Y is NOT flipped
Mat4 perspectiveZeroOne(float fovYRadians, float aspect, float near, float far);

} // namespace light3d
