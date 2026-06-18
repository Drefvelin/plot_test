#pragma once

#include <cmath>

struct Vec2 {
    float x = 0.f;
    float y = 0.f;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }

    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    float length() const { return std::sqrt(x * x + y * y); }

    Vec2 normalized() const {
        const float len = length();
        if (len < 1e-6f) {
            return {};
        }
        return {x / len, y / len};
    }
};

inline Vec2 perpendicular(const Vec2& v) { return {-v.y, v.x}; }
