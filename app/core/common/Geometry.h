#pragma once

#include "common/Vec2.h"

// Shared geometric/math primitives used across town generation, road handling,
// terrain queries and building placement. Centralized here so the various
// subsystems stop carrying private copies of the same helpers.

constexpr float kPi = 3.14159265358979323846f;

// Distance comparison helpers ------------------------------------------------
bool nearPoint(const Vec2& a, const Vec2& b, float eps = 0.08f);

Vec2 lerpVec(const Vec2& a, const Vec2& b, float t);

// Rotation -------------------------------------------------------------------
void rotateVecRad(Vec2& v, float angleRad);  // in-place, radians
Vec2 rotateVecDeg(const Vec2& v, float degrees);

// Point/segment --------------------------------------------------------------
float distancePointToSegment(const Vec2& p, const Vec2& a, const Vec2& b);
Vec2  nearestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b);

// Separating-axis helpers for 4-corner convex quads --------------------------
void projectPolygon(const Vec2 corners[4], const Vec2& axis, float& minOut, float& maxOut);
bool axisSeparates(const Vec2 a[4], const Vec2 b[4], const Vec2& axis);
// Edge-normal SAT test (no epsilon slack) — used for plot/footprint overlap.
bool quadsOverlapSAT(const Vec2 a[4], const Vec2 b[4]);
// Normalized 8-axis SAT test with epsilon slack — used during footprint layout.
bool convexQuadsOverlap(const Vec2 a[4], const Vec2 b[4]);

// Ray vs segment: returns hit distance along dir (<= maxDist) or -1 on miss.
float raySegmentHitDist(const Vec2& origin, const Vec2& dir, const Vec2& segA, const Vec2& segB,
                        float maxDist);
