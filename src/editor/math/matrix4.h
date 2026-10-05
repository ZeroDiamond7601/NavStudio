#ifndef MATRIX4_H
#define MATRIX4_H

#include <cmath>
#include <cstring>
#include "math/vector3.h"

// Column-major 4x4 matrix for OpenGL compatibility
struct Matrix4 {
    float m[16];

    Matrix4() {
        Identity();
    }

    void Identity() {
        std::memset(m, 0, sizeof(m));
        m[0] = 1.0f;
        m[5] = 1.0f;
        m[10] = 1.0f;
        m[15] = 1.0f;
    }

    static Matrix4 MakeIdentity() {
        Matrix4 res;
        return res;
    }

    static Matrix4 Perspective(float fovRadians, float aspect, float zNear, float zFar) {
        Matrix4 res;
        std::memset(res.m, 0, sizeof(res.m));
        float tanHalfFov = std::tan(fovRadians * 0.5f);
        if (std::abs(tanHalfFov) < 1e-6f || std::abs(zNear - zFar) < 1e-6f) {
            return res;
        }

        res.m[0] = 1.0f / (aspect * tanHalfFov);
        res.m[5] = 1.0f / tanHalfFov;
        res.m[10] = -(zFar + zNear) / (zFar - zNear);
        res.m[11] = -1.0f;
        res.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
        return res;
    }

    static Matrix4 Ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
        Matrix4 res;
        std::memset(res.m, 0, sizeof(res.m));
        res.m[0] = 2.0f / (right - left);
        res.m[5] = 2.0f / (top - bottom);
        res.m[10] = -2.0f / (zFar - zNear);
        res.m[12] = -(right + left) / (right - left);
        res.m[13] = -(top + bottom) / (top - bottom);
        res.m[14] = -(zFar + zNear) / (zFar - zNear);
        res.m[15] = 1.0f;
        return res;
    }

    static Matrix4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
        Vector3 f = (target - eye).Normalized();
        Vector3 s = f.Cross(up).Normalized();
        Vector3 u = s.Cross(f);

        Matrix4 res;
        res.m[0] = s.x;
        res.m[4] = s.y;
        res.m[8] = s.z;
        res.m[1] = u.x;
        res.m[5] = u.y;
        res.m[9] = u.z;
        res.m[2] = -f.x;
        res.m[6] = -f.y;
        res.m[10] = -f.z;
        res.m[12] = -s.Dot(eye);
        res.m[13] = -u.Dot(eye);
        res.m[14] = f.Dot(eye);
        res.m[15] = 1.0f;
        return res;
    }

    Matrix4 operator*(const Matrix4& rhs) const {
        Matrix4 out;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                out.m[c * 4 + r] =
                    m[0 * 4 + r] * rhs.m[c * 4 + 0] +
                    m[1 * 4 + r] * rhs.m[c * 4 + 1] +
                    m[2 * 4 + r] * rhs.m[c * 4 + 2] +
                    m[3 * 4 + r] * rhs.m[c * 4 + 3];
            }
        }
        return out;
    }

    Vector3 MultiplyPoint(const Vector3& p) const {
        float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (std::abs(w) < 1e-6f) w = 1.0f;
        float invW = 1.0f / w;
        return Vector3(
            (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) * invW,
            (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) * invW,
            (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) * invW
        );
    }

    Matrix4 Inverse() const {
        Matrix4 inv;
        float* o = inv.m;
        const float* src = m;

        o[0] = src[5] * src[10] * src[15] - src[5] * src[11] * src[14] - src[9] * src[6] * src[15] + src[9] * src[7] * src[14] + src[13] * src[6] * src[11] - src[13] * src[7] * src[10];
        o[4] = -src[4] * src[10] * src[15] + src[4] * src[11] * src[14] + src[8] * src[6] * src[15] - src[8] * src[7] * src[14] - src[12] * src[6] * src[11] + src[12] * src[7] * src[10];
        o[8] = src[4] * src[9] * src[15] - src[4] * src[11] * src[13] - src[8] * src[5] * src[15] + src[8] * src[7] * src[13] + src[12] * src[5] * src[11] - src[12] * src[7] * src[9];
        o[12] = -src[4] * src[9] * src[14] + src[4] * src[10] * src[13] + src[8] * src[5] * src[14] - src[8] * src[6] * src[13] - src[12] * src[5] * src[10] + src[12] * src[6] * src[9];

        o[1] = -src[1] * src[10] * src[15] + src[1] * src[11] * src[14] + src[9] * src[2] * src[15] - src[9] * src[3] * src[14] - src[13] * src[2] * src[11] + src[13] * src[3] * src[10];
        o[5] = src[0] * src[10] * src[15] - src[0] * src[11] * src[14] - src[8] * src[2] * src[15] + src[8] * src[3] * src[14] + src[12] * src[2] * src[11] - src[12] * src[3] * src[10];
        o[9] = -src[0] * src[9] * src[15] + src[0] * src[11] * src[13] + src[8] * src[1] * src[15] - src[8] * src[3] * src[13] - src[12] * src[1] * src[11] + src[12] * src[3] * src[9];
        o[13] = src[0] * src[9] * src[14] - src[0] * src[10] * src[13] - src[8] * src[1] * src[14] + src[8] * src[2] * src[13] + src[12] * src[1] * src[10] - src[12] * src[2] * src[9];

        o[2] = src[1] * src[6] * src[15] - src[1] * src[7] * src[14] - src[5] * src[2] * src[15] + src[5] * src[3] * src[14] + src[13] * src[2] * src[7] - src[13] * src[3] * src[6];
        o[6] = -src[0] * src[6] * src[15] + src[0] * src[7] * src[14] + src[4] * src[2] * src[15] - src[4] * src[3] * src[14] - src[12] * src[2] * src[7] + src[12] * src[3] * src[6];
        o[10] = src[0] * src[5] * src[15] - src[0] * src[7] * src[13] - src[4] * src[1] * src[15] + src[4] * src[3] * src[13] + src[12] * src[1] * src[7] - src[12] * src[3] * src[5];
        o[14] = -src[0] * src[5] * src[14] + src[0] * src[6] * src[13] + src[4] * src[1] * src[14] - src[4] * src[2] * src[13] - src[12] * src[1] * src[6] + src[12] * src[2] * src[5];

        o[3] = -src[1] * src[6] * src[11] + src[1] * src[7] * src[10] + src[5] * src[2] * src[11] - src[5] * src[3] * src[10] - src[9] * src[2] * src[7] + src[9] * src[3] * src[6];
        o[7] = src[0] * src[6] * src[11] - src[0] * src[7] * src[10] - src[4] * src[2] * src[11] + src[4] * src[3] * src[10] + src[8] * src[2] * src[7] - src[8] * src[3] * src[6];
        o[11] = -src[0] * src[5] * src[11] + src[0] * src[7] * src[9] + src[4] * src[1] * src[11] - src[4] * src[3] * src[9] - src[8] * src[1] * src[7] + src[8] * src[3] * src[5];
        o[15] = src[0] * src[5] * src[10] - src[0] * src[6] * src[9] - src[4] * src[1] * src[10] + src[4] * src[2] * src[9] + src[8] * src[1] * src[6] - src[8] * src[2] * src[5];

        float det = src[0] * o[0] + src[1] * o[4] + src[2] * o[8] + src[3] * o[12];
        if (std::abs(det) < 1e-9f) {
            return MakeIdentity();
        }

        float invDet = 1.0f / det;
        for (int i = 0; i < 16; ++i) {
            o[i] *= invDet;
        }
        return inv;
    }

    const float* Data() const {
        return m;
    }
};

#endif // MATRIX4_H
