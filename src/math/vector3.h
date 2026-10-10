#pragma once

#include <cmath>
#include <algorithm>

struct Vector3 {
    float x;
    float y;
    float z;

    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    inline float& operator[](int i) { return (&x)[i]; }
    inline const float& operator[](int i) const { return (&x)[i]; }

    inline Vector3 operator-() const { return Vector3(-x, -y, -z); }
    inline Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    inline Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    inline Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    inline Vector3 operator/(float s) const { float inv = 1.0f / s; return Vector3(x * inv, y * inv, z * inv); }

    inline Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    inline Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    inline Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    inline Vector3& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }

    inline bool operator==(const Vector3& o) const {
        return (std::fabs(x - o.x) < 0.001f && std::fabs(y - o.y) < 0.001f && std::fabs(z - o.z) < 0.001f);
    }
    inline bool operator!=(const Vector3& o) const { return !(*this == o); }

    inline float Length() const { return std::sqrt(x * x + y * y + z * z); }
    inline float LengthSquared() const { return x * x + y * y + z * z; }
    inline float LengthSq() const { return LengthSquared(); }
    inline float Length2D() const { return std::sqrt(x * x + y * y); }
    inline float Length2DSquared() const { return x * x + y * y; }

    inline float Dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    inline Vector3 Cross(const Vector3& o) const {
        return Vector3(
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x
        );
    }

    inline float DistTo(const Vector3& o) const { return (*this - o).Length(); }
    inline float DistToSq(const Vector3& o) const { return (*this - o).LengthSquared(); }
    inline float DistToSqr(const Vector3& o) const { return DistToSq(o); }
    inline float DistTo2D(const Vector3& o) const { return (*this - o).Length2D(); }
    inline float DistTo2DSq(const Vector3& o) const { return (*this - o).Length2DSquared(); }

    inline float Normalize() {
        float len = Length();
        if (len > 0.00001f) {
            float inv = 1.0f / len;
            x *= inv;
            y *= inv;
            z *= inv;
        } else {
            x = y = z = 0.0f;
        }
        return len;
    }

    inline Vector3 Normalized() const {
        Vector3 v = *this;
        v.Normalize();
        return v;
    }
};

inline Vector3 operator*(float s, const Vector3& v) {
    return v * s;
}
