// Clean-room reconstruction — platform-independent math.
#pragma once
#include <cmath>

namespace core {

constexpr float PI = 3.14159265358979323846f;
inline float radians(float deg) { return deg * (PI / 180.0f); }
inline float degrees(float rad) { return rad * (180.0f / PI); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 1e-6f ? v * (1.0f / l) : Vec3{0, 0, 0};
}

// Column-major 4x4, laid out for OpenGL glLoadMatrixf.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return Mat4{}; }

    static Mat4 translate(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static Mat4 rotateY(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }
    static Mat4 rotateZ(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }
    static Mat4 rotateX(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0;
        float f = 1.0f / std::tan(fovY * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zf * zn) / (zn - zf);
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r{};
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        r.m[15] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        return r;
    }
};

// Forward direction from yaw (around Y) and pitch (around X). Right-handed, -Z forward at yaw 0.
inline Vec3 forwardFromYawPitch(float yaw, float pitch) {
    float cp = std::cos(pitch), sp = std::sin(pitch);
    return normalize(Vec3{-std::sin(yaw) * cp, sp, -std::cos(yaw) * cp});
}

struct Quat { float x = 0, y = 0, z = 0, w = 1; };

// --- helpers for glTF node transforms -------------------------------------------------
inline Mat4 mat4FromArray(const float* a) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = a[i];   // glTF matrices are column-major too
    return r;
}

inline Mat4 mat4FromQuat(const Quat& q) {
    Mat4 r{};
    float x = q.x, y = q.y, z = q.z, w = q.w;
    r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + w * z);     r.m[2] = 2 * (x * z - w * y);
    r.m[4] = 2 * (x * y - w * z);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[6] = 2 * (y * z + w * x);
    r.m[8] = 2 * (x * z + w * y);     r.m[9] = 2 * (y * z - w * x);     r.m[10] = 1 - 2 * (x * x + y * y);
    r.m[15] = 1;
    return r;
}

inline Mat4 mat4FromTRS(const Vec3& t, const Quat& q, const Vec3& s) {
    return Mat4::translate(t) * mat4FromQuat(q) * Mat4::scale(s);
}

inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    return {
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

inline Vec3 transformDir(const Mat4& m, const Vec3& d) {
    return {
        m.m[0] * d.x + m.m[4] * d.y + m.m[8]  * d.z,
        m.m[1] * d.x + m.m[5] * d.y + m.m[9]  * d.z,
        m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z};
}

} // namespace core
