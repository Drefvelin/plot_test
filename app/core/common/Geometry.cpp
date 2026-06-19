#include "common/Geometry.h"

#include <algorithm>
#include <cmath>

bool nearPoint(const Vec2& a, const Vec2& b, float eps) {
    return (a - b).length() <= eps;
}

Vec2 lerpVec(const Vec2& a, const Vec2& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

void rotateVecRad(Vec2& v, float angleRad) {
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float x = v.x * c - v.y * s;
    const float y = v.x * s + v.y * c;
    v             = {x, y};
}

Vec2 rotateVecDeg(const Vec2& v, float degrees) {
    const float rad = degrees * kPi / 180.f;
    const float c   = std::cos(rad);
    const float s   = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

float distancePointToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-8f) {
        return (p - a).length();
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
    return (p - (a + ab * t)).length();
}

Vec2 nearestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-8f) {
        return a;
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
    return {a.x + ab.x * t, a.y + ab.y * t};
}

void projectPolygon(const Vec2 corners[4], const Vec2& axis, float& minOut, float& maxOut) {
    minOut = maxOut = corners[0].dot(axis);
    for (int i = 1; i < 4; ++i) {
        const float p = corners[i].dot(axis);
        minOut        = std::min(minOut, p);
        maxOut        = std::max(maxOut, p);
    }
}

bool axisSeparates(const Vec2 a[4], const Vec2 b[4], const Vec2& axis) {
    float minA = 0.f;
    float maxA = 0.f;
    float minB = 0.f;
    float maxB = 0.f;
    projectPolygon(a, axis, minA, maxA);
    projectPolygon(b, axis, minB, maxB);
    return maxA <= minB || maxB <= minA;
}

bool quadsOverlapSAT(const Vec2 a[4], const Vec2 b[4]) {
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = a[(i + 1) % 4] - a[i];
        if (edge.length() > 1e-6f && axisSeparates(a, b, perpendicular(edge.normalized()))) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = b[(i + 1) % 4] - b[i];
        if (edge.length() > 1e-6f && axisSeparates(a, b, perpendicular(edge.normalized()))) {
            return false;
        }
    }
    return true;
}

bool convexQuadsOverlap(const Vec2 a[4], const Vec2 b[4]) {
    const Vec2 axes[8] = {
        a[1] - a[0], a[2] - a[1], a[3] - a[2], a[0] - a[3],
        b[1] - b[0], b[2] - b[1], b[3] - b[2], b[0] - b[3],
    };

    for (const Vec2& axisRaw : axes) {
        if (axisRaw.length() < 1e-5f) {
            continue;
        }
        const Vec2 axis = axisRaw.normalized();
        float      aMin = 0.f;
        float      aMax = 0.f;
        float      bMin = 0.f;
        float      bMax = 0.f;
        projectPolygon(a, axis, aMin, aMax);
        projectPolygon(b, axis, bMin, bMax);
        if (aMax < bMin - 1e-3f || bMax < aMin - 1e-3f) {
            return false;
        }
    }
    return true;
}

float raySegmentHitDist(const Vec2& origin, const Vec2& dir, const Vec2& segA, const Vec2& segB,
                        float maxDist) {
    const Vec2  r     = dir;
    const Vec2  s     = segB - segA;
    const float denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-8f) {
        return -1.f;
    }
    const Vec2  qp = segA - origin;
    const float t  = (qp.x * s.y - qp.y * s.x) / denom;
    const float u  = (qp.x * r.y - qp.y * r.x) / denom;
    if (t < 0.f || t > maxDist || u < -1e-4f || u > 1.f + 1e-4f) {
        return -1.f;
    }
    return t;
}
