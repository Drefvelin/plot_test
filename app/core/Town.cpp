#include "Town.h"

#include "Logger.h"
#include "TerrainAtlas.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

bool nearPoint(const Vec2& a, const Vec2& b, float eps = 0.08f) {
    return (a - b).length() <= eps;
}

bool orientRoadSegmentForCell(const Road& road, int cellId, Vec2& a, Vec2& b) {
    if (road.cellA == cellId) {
        a = road.a;
        b = road.b;
        return true;
    }
    if (road.cellB == cellId) {
        a = road.b;
        b = road.a;
        return true;
    }
    return false;
}

void appendBoundaryVertex(std::vector<Vec2>& boundary, const Vec2& point) {
    if (boundary.empty() || !nearPoint(boundary.back(), point)) {
        boundary.push_back(point);
    }
}

bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& polygon);

Vec2 snapToJunction(const Vec2& p, const std::vector<Junction>& junctions, float eps = 0.08f) {
    for (const Junction& junction : junctions) {
        if ((junction.pos - p).length() <= eps) {
            return junction.pos;
        }
    }
    return p;
}

float cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

float signedTurn(const Vec2& incoming, const Vec2& outgoing) {
    const float inLen  = incoming.length();
    const float outLen = outgoing.length();
    if (inLen < 1e-6f || outLen < 1e-6f) {
        return 1e9f;
    }
    const Vec2 inN  = incoming * (1.f / inLen);
    const Vec2 outN = outgoing * (1.f / outLen);
    return std::atan2(cross2(inN, outN), inN.dot(outN));
}

bool pointsEqual(const Vec2& a, const Vec2& b, float eps = 0.08f) {
    return (a - b).length() <= eps;
}

float orient2d(const Vec2& a, const Vec2& b, const Vec2& c) {
    return cross2(b - a, c - a);
}

bool segmentsProperlyIntersect(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1) {
    const float o1 = orient2d(a0, a1, b0);
    const float o2 = orient2d(a0, a1, b1);
    const float o3 = orient2d(b0, b1, a0);
    const float o4 = orient2d(b0, b1, a1);

    if (((o1 > 0.f) != (o2 > 0.f)) && ((o3 > 0.f) != (o4 > 0.f))) {
        return true;
    }

    return false;
}

// True when segA->segB crosses the infinite line through lineA->lineB.
// Ignores hits at segA or lineA (sight-line start and the current junction).
bool segmentCrossesLine(const Vec2& segA, const Vec2& segB, const Vec2& lineA, const Vec2& lineB,
                        float eps = 0.08f) {
    const float sideA = orient2d(lineA, lineB, segA);
    const float sideB = orient2d(lineA, lineB, segB);

    if (sideA > eps && sideB > eps) {
        return false;
    }
    if (sideA < -eps && sideB < -eps) {
        return false;
    }

    const Vec2  r   = segB - segA;
    const Vec2  s   = lineB - lineA;
    const float rxs = cross2(r, s);
    if (std::abs(rxs) < 1e-9f) {
        return false;
    }

    const Vec2  qp = lineA - segA;
    const float t  = cross2(qp, s) / rxs;
    if (t < -eps || t > 1.f + eps) {
        return false;
    }

    const Vec2 hit = segA + r * t;
    if (nearPoint(hit, segA, eps) || nearPoint(hit, lineA, eps)) {
        return false;
    }

    return true;
}

bool polygonSelfIntersects(const std::vector<Vec2>& polygon) {
    const std::size_t n = polygon.size();
    if (n < 4) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const Vec2& a0 = polygon[i];
        const Vec2& a1 = polygon[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i || j == i + 1 || (i == 0 && j + 1 == n)) {
                continue;
            }
            const Vec2& b0 = polygon[j];
            const Vec2& b1 = polygon[(j + 1) % n];
            if (segmentsProperlyIntersect(a0, a1, b0, b1)) {
                return true;
            }
        }
    }
    return false;
}

bool siteOnLeftOfEdge(const Vec2& from, const Vec2& to, const Vec2& site) {
    const Vec2 edge = to - from;
    const float len = edge.length();
    if (len < 1e-6f) {
        return false;
    }
    const Vec2 left = perpendicular(edge * (1.f / len));
    const Vec2 mid  = (from + to) * 0.5f;
    return left.dot(site - mid) > 1e-4f;
}

bool allEdgesHaveSiteOnLeft(const std::vector<Vec2>& boundary, const Vec2& site) {
    if (boundary.size() < 3) {
        return false;
    }
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const Vec2& a = boundary[i];
        const Vec2& b = boundary[(i + 1) % boundary.size()];
        if (!siteOnLeftOfEdge(a, b, site)) {
            return false;
        }
    }
    return true;
}

bool allEdgesHaveSiteOnRight(const std::vector<Vec2>& boundary, const Vec2& site) {
    if (boundary.size() < 3) {
        return false;
    }
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const Vec2& a = boundary[i];
        const Vec2& b = boundary[(i + 1) % boundary.size()];
        if (siteOnLeftOfEdge(a, b, site)) {
            return false;
        }
    }
    return true;
}

bool isValidCellBoundary(const std::vector<Vec2>& boundary, const Vec2& site) {
    if (boundary.size() < 3) {
        return false;
    }
    if (polygonSelfIntersects(boundary)) {
        return false;
    }
    return allEdgesHaveSiteOnLeft(boundary, site) || allEdgesHaveSiteOnRight(boundary, site);
}

bool siteOnInsideOfDirectedEdge(const Vec2& from, const Vec2& to, const Vec2& site, bool clockwise) {
    return clockwise ? !siteOnLeftOfEdge(from, to, site) : siteOnLeftOfEdge(from, to, site);
}

struct IndexedBoundarySegment {
    int  junctionA = -1;
    int  junctionB = -1;
    bool used      = false;
};

int findJunctionIndex(const Vec2& p, const std::vector<Junction>& junctions, float eps = 0.08f) {
    for (std::size_t i = 0; i < junctions.size(); ++i) {
        if ((junctions[i].pos - p).length() <= eps) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<IndexedBoundarySegment> collectIndexedBoundarySegments(
    const Cell& cell, const std::vector<Road>& roads, const std::vector<Junction>& junctions) {
    std::vector<IndexedBoundarySegment> segments;
    segments.reserve(cell.roadIds.size());
    std::vector<std::pair<int, int>> seen;

    for (const int roadId : cell.roadIds) {
        if (roadId < 0 || roadId >= static_cast<int>(roads.size())) {
            continue;
        }
        const Road& road = roads[static_cast<std::size_t>(roadId)];
        const int   ja   = findJunctionIndex(snapToJunction(road.a, junctions), junctions);
        const int   jb   = findJunctionIndex(snapToJunction(road.b, junctions), junctions);
        if (ja < 0 || jb < 0 || ja == jb) {
            continue;
        }
        const int lo = std::min(ja, jb);
        const int hi = std::max(ja, jb);
        const auto key = std::make_pair(lo, hi);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        segments.push_back({ja, jb, false});
    }
    return segments;
}

float ccwAngleDelta(float fromRad, float toRad) {
    float delta = toRad - fromRad;
    while (delta <= 1e-6f) {
        delta += 6.283185307179586f;
    }
    while (delta > 6.283185307179586f) {
        delta -= 6.283185307179586f;
    }
    return delta;
}

float cwAngleDelta(float fromRad, float toRad) {
    float delta = fromRad - toRad;
    while (delta <= 1e-6f) {
        delta += 6.283185307179586f;
    }
    while (delta > 6.283185307179586f) {
        delta -= 6.283185307179586f;
    }
    return delta;
}

bool siteVisibleFromExit(int curJ, int nextJ, int prevJ,
                         const std::vector<IndexedBoundarySegment>& segments,
                         const std::vector<Junction>& junctions, const Vec2& site) {
    const Vec2& exitPos = junctions[static_cast<std::size_t>(nextJ)].pos;
    const Vec2& curPos  = junctions[static_cast<std::size_t>(curJ)].pos;
    for (const IndexedBoundarySegment& segment : segments) {
        int otherJ = -1;
        if (segment.junctionA == curJ) {
            otherJ = segment.junctionB;
        } else if (segment.junctionB == curJ) {
            otherJ = segment.junctionA;
        } else {
            continue;
        }
        if (otherJ == prevJ || otherJ == nextJ) {
            continue;
        }

        const Vec2& otherPos = junctions[static_cast<std::size_t>(otherJ)].pos;
        if (segmentCrossesLine(exitPos, site, curPos, otherPos)) {
            return false;
        }
    }
    return true;
}

bool walkIndexedBoundary(std::vector<IndexedBoundarySegment>& segments,
                         const std::vector<Junction>& junctions, int startIdx, int fromJunction,
                         int toJunction, const Vec2& site, std::vector<Vec2>& out) {
    for (IndexedBoundarySegment& segment : segments) {
        segment.used = false;
    }

    out.clear();
    out.push_back(junctions[static_cast<std::size_t>(fromJunction)].pos);
    segments[static_cast<std::size_t>(startIdx)].used = true;

    int prevJunction = fromJunction;
    int curJunction  = toJunction;
    int usedCount    = 1;

    appendBoundaryVertex(out, junctions[static_cast<std::size_t>(curJunction)].pos);

    while (usedCount < static_cast<int>(segments.size())) {
        int visibleIdx   = -1;
        int visibleNextJ = -1;
        int visibleCount = 0;

        for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
            if (segments[static_cast<std::size_t>(i)].used) {
                continue;
            }

            const IndexedBoundarySegment& segment = segments[static_cast<std::size_t>(i)];
            for (int forward = 0; forward < 2; ++forward) {
                const int connectJ = forward ? segment.junctionA : segment.junctionB;
                const int nextJ    = forward ? segment.junctionB : segment.junctionA;
                if (connectJ != curJunction || nextJ == prevJunction) {
                    continue;
                }

                if (!siteVisibleFromExit(curJunction, nextJ, prevJunction, segments, junctions,
                                         site)) {
                    continue;
                }

                visibleIdx   = i;
                visibleNextJ = nextJ;
                ++visibleCount;
            }
        }

        if (visibleCount != 1) {
            return false;
        }

        appendBoundaryVertex(out, junctions[static_cast<std::size_t>(visibleNextJ)].pos);
        segments[static_cast<std::size_t>(visibleIdx)].used = true;
        ++usedCount;
        prevJunction = curJunction;
        curJunction  = visibleNextJ;
    }

    if (out.size() >= 3 && pointsEqual(out.front(), out.back())) {
        out.pop_back();
    }

    return usedCount == static_cast<int>(segments.size()) && out.size() >= 3;
}

bool hasUndirectedEdge(const std::vector<IndexedBoundarySegment>& segments, int junctionA,
                       int junctionB) {
    for (const IndexedBoundarySegment& segment : segments) {
        if ((segment.junctionA == junctionA && segment.junctionB == junctionB)
            || (segment.junctionA == junctionB && segment.junctionB == junctionA)) {
            return true;
        }
    }
    return false;
}

float segmentSortAngle(const Vec2& a, const Vec2& b, const Vec2& site) {
    const float midX  = (a.x + b.x) * 0.5f;
    const float midY  = (a.y + b.y) * 0.5f;
    const float diffY = midY - site.y;
    float       angle = std::atan2(diffY, midX - site.x);
    if (diffY < 0.f) {
        angle += 6.283185307179586f;
    }
    return angle;
}

bool buildBoundaryFromAngleSortedSegments(const std::vector<IndexedBoundarySegment>& segments,
                                          const std::vector<Junction>& junctions,
                                          const Cell& cell, std::vector<Vec2>& out) {
    if (segments.size() < 3) {
        return false;
    }

    struct ScoredSegment {
        int   junctionA = -1;
        int   junctionB = -1;
        float angle     = 0.f;
    };

    std::vector<ScoredSegment> ordered;
    ordered.reserve(segments.size());
    for (const IndexedBoundarySegment& segment : segments) {
        const Vec2& pa = junctions[static_cast<std::size_t>(segment.junctionA)].pos;
        const Vec2& pb = junctions[static_cast<std::size_t>(segment.junctionB)].pos;
        ordered.push_back({segment.junctionA, segment.junctionB, segmentSortAngle(pa, pb, cell.site)});
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const ScoredSegment& a, const ScoredSegment& b) { return a.angle < b.angle; });

    out.clear();
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const ScoredSegment& segment = ordered[i];
        const Vec2&          aPos = junctions[static_cast<std::size_t>(segment.junctionA)].pos;
        const Vec2&          bPos = junctions[static_cast<std::size_t>(segment.junctionB)].pos;

        if (out.empty()) {
            appendBoundaryVertex(out, aPos);
            appendBoundaryVertex(out, bPos);
            continue;
        }

        if (pointsEqual(out.back(), aPos)) {
            appendBoundaryVertex(out, bPos);
        } else if (pointsEqual(out.back(), bPos)) {
            appendBoundaryVertex(out, aPos);
        } else {
            return false;
        }
    }

    if (out.size() >= 3 && pointsEqual(out.front(), out.back())) {
        out.pop_back();
    }

    return isValidCellBoundary(out, cell.site);
}

bool buildBoundaryFromAngularJunctionRing(const std::vector<IndexedBoundarySegment>& segments,
                                          const std::vector<Junction>& junctions,
                                          const Cell& cell, std::vector<Vec2>& out) {
    std::vector<int> junctionIds;
    junctionIds.reserve(segments.size() * 2);
    for (const IndexedBoundarySegment& segment : segments) {
        junctionIds.push_back(segment.junctionA);
        junctionIds.push_back(segment.junctionB);
    }
    std::sort(junctionIds.begin(), junctionIds.end());
    junctionIds.erase(std::unique(junctionIds.begin(), junctionIds.end()), junctionIds.end());
    if (junctionIds.size() < 3) {
        return false;
    }

    std::sort(junctionIds.begin(), junctionIds.end(), [&](int a, int b) {
        const Vec2& pa = junctions[static_cast<std::size_t>(a)].pos;
        const Vec2& pb = junctions[static_cast<std::size_t>(b)].pos;
        return std::atan2(pa.y - cell.site.y, pa.x - cell.site.x)
             < std::atan2(pb.y - cell.site.y, pb.x - cell.site.x);
    });

    out.clear();
    out.reserve(junctionIds.size());
    for (std::size_t i = 0; i < junctionIds.size(); ++i) {
        const int a = junctionIds[i];
        const int b = junctionIds[(i + 1) % junctionIds.size()];
        if (!hasUndirectedEdge(segments, a, b)) {
            return false;
        }
        out.push_back(junctions[static_cast<std::size_t>(a)].pos);
    }

    return isValidCellBoundary(out, cell.site);
}

bool walkCellFace(const std::vector<IndexedBoundarySegment>& sourceSegments,
                  const std::vector<Junction>& junctions, const Cell& cell, std::vector<Vec2>& out,
                  bool requireValid = true) {
    if (sourceSegments.size() < 3) {
        return false;
    }

    for (int startIdx = 0; startIdx < static_cast<int>(sourceSegments.size()); ++startIdx) {
        for (int forward = 0; forward < 2; ++forward) {
            const int fromJ = forward ? sourceSegments[static_cast<std::size_t>(startIdx)].junctionA
                                      : sourceSegments[static_cast<std::size_t>(startIdx)].junctionB;
            const int toJ   = forward ? sourceSegments[static_cast<std::size_t>(startIdx)].junctionB
                                      : sourceSegments[static_cast<std::size_t>(startIdx)].junctionA;

            std::vector<IndexedBoundarySegment> segments = sourceSegments;
            std::vector<Vec2>                   candidate;
            if (!walkIndexedBoundary(segments, junctions, startIdx, fromJ, toJ, cell.site,
                                     candidate)) {
                continue;
            }
            if (requireValid && !isValidCellBoundary(candidate, cell.site)) {
                continue;
            }
            out = std::move(candidate);
            return true;
        }
    }

    return false;
}

bool buildCellBoundaryPolygon(const Cell& cell, const std::vector<Road>& roads,
                              const std::vector<Junction>& junctions, std::vector<Vec2>& out) {
    const std::vector<IndexedBoundarySegment> segments =
        collectIndexedBoundarySegments(cell, roads, junctions);
    if (segments.size() < 3) {
        out.clear();
        return false;
    }
    if (walkCellFace(segments, junctions, cell, out, false)) {
        return true;
    }
    if (buildBoundaryFromAngleSortedSegments(segments, junctions, cell, out)) {
        return true;
    }
    if (buildBoundaryFromAngularJunctionRing(segments, junctions, cell, out)) {
        return true;
    }
    out.clear();
    return false;
}

void logRoadBoundaryDiagnostics(const Town& town) {
    int tooFewSegments = 0;
    int angleSortedOk  = 0;
    int faceWalkOk     = 0;
    int failed         = 0;
    for (const Cell& cell : town.cells) {
        const std::vector<IndexedBoundarySegment> segments =
            collectIndexedBoundarySegments(cell, town.roads, town.junctions);
        if (segments.size() < 3) {
            ++tooFewSegments;
            continue;
        }
        std::vector<Vec2> candidate;
        if (walkCellFace(segments, town.junctions, cell, candidate)) {
            ++faceWalkOk;
        } else if (buildBoundaryFromAngleSortedSegments(segments, town.junctions, cell, candidate)) {
            ++angleSortedOk;
        } else {
            ++failed;
        }
    }
    Logger::log("voronoi",
                "road_boundary_diag too_few_segments=" + std::to_string(tooFewSegments)
                    + " angle_sorted_ok=" + std::to_string(angleSortedOk) + " face_walk_ok="
                    + std::to_string(faceWalkOk) + " failed=" + std::to_string(failed));
}

bool roadFrameForCell(const Road& road, int cellId, Vec2& origin, Vec2& edgeDir) {
    const float len = road.length();
    if (len < 1e-6f) {
        return false;
    }
    if (cellId == road.cellA) {
        origin  = road.a;
        edgeDir = (road.b - road.a) * (1.f / len);
        return true;
    }
    if (cellId == road.cellB) {
        origin  = road.b;
        edgeDir = (road.a - road.b) * (1.f / len);
        return true;
    }
    return false;
}

float segmentCenterDist(const Vec2& origin, const Vec2& edgeDir, float startT, float endT,
                        const Vec2& center) {
    const Vec2 mid = origin + edgeDir * ((startT + endT) * 0.5f);
    return (mid - center).length();
}

float plotTMin(const Plot& plot, const Vec2& origin, const Vec2& edgeDir) {
    const float t0 = (plot.corners[0] - origin).dot(edgeDir);
    const float t1 = (plot.corners[1] - origin).dot(edgeDir);
    return std::min(t0, t1);
}

float plotTMax(const Plot& plot, const Vec2& origin, const Vec2& edgeDir) {
    const float t0 = (plot.corners[0] - origin).dot(edgeDir);
    const float t1 = (plot.corners[1] - origin).dot(edgeDir);
    return std::max(t0, t1);
}

void recomputeSegmentDist(RoadFrontageSegment& segment, const Vec2& origin, const Vec2& edgeDir,
                          const Vec2& center) {
    segment.centerDist = segmentCenterDist(origin, edgeDir, segment.startT, segment.endT, center);
}

void carveSideFrontage(RoadSideFrontage& side, const Vec2& origin, const Vec2& edgeDir,
                       float usedStart, float usedEnd, const Vec2& center, int& nextSegmentId) {
    constexpr float kEps = 0.08f;

    std::vector<RoadFrontageSegment> remaining;
    remaining.reserve(side.segments.size() + 1);

    for (const RoadFrontageSegment& segment : side.segments) {
        if (usedEnd <= segment.startT + kEps || usedStart >= segment.endT - kEps) {
            remaining.push_back(segment);
            continue;
        }
        if (usedStart > segment.startT + kEps) {
            RoadFrontageSegment left{nextSegmentId++, segment.startT, usedStart, 0.f};
            recomputeSegmentDist(left, origin, edgeDir, center);
            if (left.width() > kEps) {
                remaining.push_back(left);
            }
        }
        if (usedEnd < segment.endT - kEps) {
            RoadFrontageSegment right{nextSegmentId++, usedEnd, segment.endT, 0.f};
            recomputeSegmentDist(right, origin, edgeDir, center);
            if (right.width() > kEps) {
                remaining.push_back(right);
            }
        }
    }

    side.segments = std::move(remaining);
}

void appendThickSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                        const sf::Color& color) {
    const float dx    = b.x - a.x;
    const float dy    = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-4f) {
        return;
    }
    const float len = std::sqrt(lenSq);
    const float nx  = -dy / len * thickness * 0.5f;
    const float ny  = dx / len * thickness * 0.5f;

    tris.append({{a.x + nx, a.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x + nx, b.y + ny}, color});

    tris.append({{b.x + nx, b.y + ny}, color});
    tris.append({{a.x - nx, a.y - ny}, color});
    tris.append({{b.x - nx, b.y - ny}, color});
}

bool computeInwardForRoadSide(const Road& road, int cellId, const Cell& cell, Vec2& inwardOut) {
    Vec2 origin{};
    Vec2 edgeDir{};
    if (!roadFrameForCell(road, cellId, origin, edgeDir)) {
        return false;
    }

    const float len = road.length();
    if (len < 1e-4f) {
        return false;
    }

    const Vec2 mid        = origin + edgeDir * (len * 0.5f);
    const Vec2 left       = perpendicular(edgeDir);
    const Vec2 toCentroid = cell.centroid - mid;
    inwardOut             = (left.dot(toCentroid) >= 0.f) ? left : left * -1.f;
    inwardOut             = inwardOut.normalized();
    return inwardOut.length() > 1e-4f;
}

float distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-8f) {
        return (p - a).length();
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
    const Vec2  proj{a.x + ab.x * t, a.y + ab.y * t};
    return (p - proj).length();
}

float minDistanceToRoads(const Vec2& p, const std::vector<Road>& roads) {
    float best = 1e9f;
    for (const Road& road : roads) {
        best = std::min(best, distanceToSegment(p, road.a, road.b));
    }
    return best;
}

bool pointInsideCellBoundary(const Vec2& p, const Cell& cell, const std::vector<Road>& roads) {
    return ::pointInCellBoundary(p, cell, roads);
}

std::string fmtCoord(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

bool findProbeBaseFromRoadEnd(const Vec2& start, const Vec2& walkDir, float maxDist,
                              const Vec2& inward, float setback, const Cell& cell,
                              const std::vector<Road>& roads, int frontageRoadId, Vec2& outBase,
                              float& outWalkDist) {
    const float step = std::max(setback * 0.1f, 0.05f);
    for (float t = 0.f; t <= maxDist + 1e-3f; t += step) {
        const Vec2 roadPoint = start + walkDir * t;
        if (validSetbackProbe(roadPoint, inward, setback, cell, roads, frontageRoadId)) {
            outBase     = roadPoint;
            outWalkDist = t;
            return true;
        }
    }
    outWalkDist = 0.f;
    return false;
}

}  // namespace

bool validSetbackProbe(const Vec2& roadPoint, const Vec2& inward, float setback, const Cell& cell,
                       const std::vector<Road>& roads, int excludeRoadId) {
    if (inward.length() < 1e-4f || setback <= 0.f) {
        return false;
    }
    const Vec2 tip = roadPoint + inward * setback;
    if (!pointInCellBoundary(tip, cell, roads)) {
        return false;
    }
    float best = 1e9f;
    for (const Road& road : roads) {
        if (road.id == excludeRoadId) {
            continue;
        }
        const Vec2  ab    = road.b - road.a;
        const float lenSq = ab.x * ab.x + ab.y * ab.y;
        if (lenSq < 1e-8f) {
            best = std::min(best, (tip - road.a).length());
            continue;
        }
        const float t = std::clamp(((tip.x - road.a.x) * ab.x + (tip.y - road.a.y) * ab.y) / lenSq,
                                   0.f, 1.f);
        const Vec2 proj{road.a.x + ab.x * t, road.a.y + ab.y * t};
        best = std::min(best, (tip - proj).length());
    }
    return best >= setback - 1e-3f;
}

namespace {

bool pointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b, float eps = 0.08f) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < eps * eps) {
        return (p - a).length() <= eps;
    }
    const float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq;
    if (t < -eps || t > 1.f + eps) {
        return false;
    }
    const Vec2 proj{a.x + ab.x * t, a.y + ab.y * t};
    return (p - proj).length() <= eps;
}

bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& polygon) {
    if (polygon.size() < 3) {
        return false;
    }

    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        if (pointOnSegment(p, polygon[i], polygon[j])) {
            return true;
        }
    }

    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[j];
        if (((a.y > p.y) != (b.y > p.y))
            && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-12f) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

}  // namespace

std::size_t Town::plotCount() const {
    std::size_t count = 0;
    for (const auto& cell : cells) {
        count += cell.plots.size();
    }
    return count;
}

void rebuildCellBoundaryFromRoads(Cell& cell, const std::vector<Road>& roads,
                                   const std::vector<Junction>& junctions) {
    if (!buildCellBoundaryPolygon(cell, roads, junctions, cell.boundary)) {
        cell.boundary.clear();
    }
    cell.centroid = polygonCentroid(cell.boundary, cell.site);
}

void rebuildAllCellBoundaries(Town& town, int& boundaryOk, int& boundaryFail) {
    boundaryOk   = 0;
    boundaryFail = 0;
    for (Cell& cell : town.cells) {
        rebuildCellBoundaryFromRoads(cell, town.roads, town.junctions);
        if (cell.boundary.size() >= 3) {
            ++boundaryOk;
        } else {
            ++boundaryFail;
        }
    }
    logRoadBoundaryDiagnostics(town);
}

bool pointInCell(const Vec2& p, const Cell& cell, const std::vector<Road>& roads,
                 const std::vector<Junction>& junctions) {
    std::vector<Vec2> boundary;
    if (!buildCellBoundaryPolygon(cell, roads, junctions, boundary)) {
        return false;
    }
    return pointInPolygon(p, boundary);
}

bool pointInCellBoundary(const Vec2& p, const Cell& cell, const std::vector<Road>& roads) {
    if (cell.boundary.size() >= 3) {
        return pointInPolygon(p, cell.boundary);
    }
    return pointInCell(p, cell, roads);
}

void indexJunctions(Town& town) {
    town.junctions.clear();

    auto findJunction = [&](const Vec2& pos) -> int {
        constexpr float kEps = 0.08f;
        for (std::size_t i = 0; i < town.junctions.size(); ++i) {
            if ((town.junctions[i].pos - pos).length() <= kEps) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    auto linkRoadToJunction = [&](const Vec2& pos, int roadId) {
        const int junctionId = findJunction(pos);
        if (junctionId < 0) {
            Junction junction;
            junction.id = static_cast<int>(town.junctions.size());
            junction.pos  = pos;
            junction.roadIds.push_back(roadId);
            town.junctions.push_back(junction);
            return;
        }

        Junction& junction = town.junctions[static_cast<std::size_t>(junctionId)];
        if (std::find(junction.roadIds.begin(), junction.roadIds.end(), roadId)
            == junction.roadIds.end()) {
            junction.roadIds.push_back(roadId);
        }
    };

    for (const Road& road : town.roads) {
        if (road.id < 0) {
            continue;
        }
        linkRoadToJunction(road.a, road.id);
        linkRoadToJunction(road.b, road.id);
    }
}

void buildJunctionMesh(Town& town, float pixelsPerUnit, float radiusUnits) {
    constexpr int   kSegments = 24;
    const sf::Color color(255, 0, 0);
    const float     radiusPx = units::toPixels(radiusUnits, pixelsPerUnit);

    town.junctionMesh.setPrimitiveType(sf::Triangles);
    town.junctionMesh.clear();

    for (const Junction& junction : town.junctions) {
        const Vec2 center{units::toPixels(junction.pos.x, pixelsPerUnit),
                          units::toPixels(junction.pos.y, pixelsPerUnit)};

        for (int i = 0; i < kSegments; ++i) {
            const float a0 =
                (static_cast<float>(i) / static_cast<float>(kSegments)) * 6.283185307179586f;
            const float a1 =
                (static_cast<float>(i + 1) / static_cast<float>(kSegments)) * 6.283185307179586f;

            const Vec2 p0{center.x + std::cos(a0) * radiusPx, center.y + std::sin(a0) * radiusPx};
            const Vec2 p1{center.x + std::cos(a1) * radiusPx, center.y + std::sin(a1) * radiusPx};

            town.junctionMesh.append({{center.x, center.y}, color});
            town.junctionMesh.append({{p0.x, p0.y}, color});
            town.junctionMesh.append({{p1.x, p1.y}, color});
        }
    }
}

void buildCellCentroidMesh(Town& town, float pixelsPerUnit, float radiusUnits) {
    constexpr int   kSegments = 24;
    const sf::Color color(255, 165, 0);
    const float     radiusPx = units::toPixels(radiusUnits, pixelsPerUnit);

    town.cellCentroidMesh.setPrimitiveType(sf::Triangles);
    town.cellCentroidMesh.clear();
    town.cellCentroidLabels.clear();

    for (const Cell& cell : town.cells) {
        const Vec2 center{units::toPixels(cell.centroid.x, pixelsPerUnit),
                          units::toPixels(cell.centroid.y, pixelsPerUnit)};
        town.cellCentroidLabels.push_back({cell.id, center.x, center.y});

        for (int i = 0; i < kSegments; ++i) {
            const float a0 =
                (static_cast<float>(i) / static_cast<float>(kSegments)) * 6.283185307179586f;
            const float a1 =
                (static_cast<float>(i + 1) / static_cast<float>(kSegments)) * 6.283185307179586f;

            const Vec2 p0{center.x + std::cos(a0) * radiusPx, center.y + std::sin(a0) * radiusPx};
            const Vec2 p1{center.x + std::cos(a1) * radiusPx, center.y + std::sin(a1) * radiusPx};

            town.cellCentroidMesh.append({{center.x, center.y}, color});
            town.cellCentroidMesh.append({{p0.x, p0.y}, color});
            town.cellCentroidMesh.append({{p1.x, p1.y}, color});
        }
    }
}

void buildCellSiteMesh(Town& town, float pixelsPerUnit, float radiusUnits) {
    constexpr int   kSegments = 24;
    const sf::Color color(255, 0, 255);
    const float     radiusPx = units::toPixels(radiusUnits, pixelsPerUnit);

    town.cellSiteMesh.setPrimitiveType(sf::Triangles);
    town.cellSiteMesh.clear();

    for (const Cell& cell : town.cells) {
        const Vec2 center{units::toPixels(cell.site.x, pixelsPerUnit),
                          units::toPixels(cell.site.y, pixelsPerUnit)};

        for (int i = 0; i < kSegments; ++i) {
            const float a0 =
                (static_cast<float>(i) / static_cast<float>(kSegments)) * 6.283185307179586f;
            const float a1 =
                (static_cast<float>(i + 1) / static_cast<float>(kSegments)) * 6.283185307179586f;

            const Vec2 p0{center.x + std::cos(a0) * radiusPx, center.y + std::sin(a0) * radiusPx};
            const Vec2 p1{center.x + std::cos(a1) * radiusPx, center.y + std::sin(a1) * radiusPx};

            town.cellSiteMesh.append({{center.x, center.y}, color});
            town.cellSiteMesh.append({{p0.x, p0.y}, color});
            town.cellSiteMesh.append({{p1.x, p1.y}, color});
        }
    }
}

void assignRoadSideInwards(Town& town) {
    for (Road& road : town.roads) {
        road.sideA = RoadSideFrontage{};
        road.sideB = RoadSideFrontage{};

        if (road.isBridge) {
            continue;
        }

        if (road.isSameCellSecondary()) {
            assignSecondaryRoadInwards(road, town);
            continue;
        }

        if (road.cellA >= 0 && road.cellA < static_cast<int>(town.cells.size())) {
            road.sideA.cellId = road.cellA;
            computeInwardForRoadSide(road, road.cellA,
                                     town.cells[static_cast<std::size_t>(road.cellA)],
                                     road.sideA.inward);
        }

        if (road.cellB >= 0 && road.cellB < static_cast<int>(town.cells.size())) {
            road.sideB.cellId = road.cellB;
            computeInwardForRoadSide(road, road.cellB,
                                     town.cells[static_cast<std::size_t>(road.cellB)],
                                     road.sideB.inward);
        }
    }
}

void assignSecondaryRoadInwards(Road& road, const Town& town) {
    if (!road.isSameCellSecondary() || road.hostCellId < 0
        || road.hostCellId >= static_cast<int>(town.cells.size())) {
        return;
    }

    const Cell& cell    = town.cells[static_cast<std::size_t>(road.hostCellId)];
    const Vec2  edgeDir = (road.b - road.a).normalized();
    if (edgeDir.length() < 1e-4f) {
        return;
    }

    Vec2 left = perpendicular(edgeDir);
    if (left.dot(cell.centroid - (road.a + road.b) * 0.5f) < 0.f) {
        left = left * -1.f;
    }

    road.sideA.cellId = road.hostCellId;
    road.sideB.cellId = road.hostCellId;
    road.sideA.inward = left.normalized();
    road.sideB.inward = (left * -1.f).normalized();
}

bool findProbeTAtRoadEnd(const Road& road, int cellId, bool atOriginEnd, const Vec2& inward,
                         float setback, const Cell& cell, const std::vector<Road>& roads,
                         float& outT) {
    Vec2 origin{};
    Vec2 edgeDir{};
    if (!roadFrameForCell(road, cellId, origin, edgeDir)) {
        return false;
    }

    const float len = road.length();
    if (len < 1e-3f) {
        return false;
    }

    Vec2  start{};
    Vec2  walkDir{};
    float walkDist = 0.f;
    Vec2  base{};

    if (atOriginEnd) {
        start   = origin;
        walkDir = edgeDir;
    } else {
        start   = origin + edgeDir * len;
        walkDir = edgeDir * -1.f;
    }

    if (!findProbeBaseFromRoadEnd(start, walkDir, len, inward, setback, cell, roads, road.id, base,
                                  walkDist)) {
        return false;
    }

    outT = atOriginEnd ? walkDist : (len - walkDist);
    return true;
}

void buildRoadFrontageSegmentsFromProbes(Town& town, float setback) {
    for (Road& road : town.roads) {
        if (road.isSecondary) {
            continue;
        }
        const float len = road.length();
        if (len < 1e-3f) {
            continue;
        }

        for (RoadSideFrontage* side : {&road.sideA, &road.sideB}) {
            if (side->cellId < 0 || side->cellId >= static_cast<int>(town.cells.size())
                || side->inward.length() < 1e-4f) {
                continue;
            }

            const Cell& cell = town.cells[static_cast<std::size_t>(side->cellId)];
            Vec2        origin{};
            Vec2        edgeDir{};
            if (!roadFrameForCell(road, side->cellId, origin, edgeDir)) {
                continue;
            }

            float tOrigin = 0.f;
            float tFar    = 0.f;
            if (!findProbeTAtRoadEnd(road, side->cellId, true, side->inward, setback, cell,
                                     town.roads, tOrigin)
                || !findProbeTAtRoadEnd(road, side->cellId, false, side->inward, setback, cell,
                                        town.roads, tFar)) {
                continue;
            }

            float startT = std::min(tOrigin, tFar);
            float endT   = std::max(tOrigin, tFar);
            if (endT - startT < 0.02f) {
                continue;
            }

            RoadFrontageSegment segment;
            segment.id       = town.frontageSegmentIdCounter++;
            segment.startT   = startT;
            segment.endT     = endT;
            segment.centerDist =
                segmentCenterDist(origin, edgeDir, segment.startT, segment.endT, town.center);
            side->segments.push_back(segment);
        }
    }
}

void buildRoadEndProbeMesh(Town& town, float pixelsPerUnit, float probeLengthUnits) {
    (void)pixelsPerUnit;

    town.roadEndProbeMesh.setPrimitiveType(sf::Triangles);
    town.roadEndProbeMesh.clear();
    town.roadEndProbeLabels.clear();

    int nextProbeId = 0;

    Logger::log("points", "=== road end probes (setback=" + fmtCoord(probeLengthUnits) + "u) ===");

    for (const Road& road : town.roads) {
        const float len = road.length();
        if (len < 1e-3f) {
            continue;
        }

        for (const RoadSideFrontage* side : {&road.sideA, &road.sideB}) {
            if (side->cellId < 0 || side->cellId >= static_cast<int>(town.cells.size())
                || side->inward.length() < 1e-4f) {
                continue;
            }

            const Cell& cell = town.cells[static_cast<std::size_t>(side->cellId)];
            Vec2        origin{};
            Vec2        edgeDir{};
            if (!roadFrameForCell(road, side->cellId, origin, edgeDir)) {
                continue;
            }

            for (const bool atOriginEnd : {true, false}) {
                float t = 0.f;
                if (!findProbeTAtRoadEnd(road, side->cellId, atOriginEnd, side->inward,
                                         probeLengthUnits, cell, town.roads, t)) {
                    continue;
                }

                const Vec2 base = origin + edgeDir * t;
                const Vec2 tip  = base + side->inward * probeLengthUnits;
                const int  probeId = nextProbeId++;

                const float minRoadDist = minDistanceToRoads(tip, town.roads);
                const bool  inside      = pointInsideCellBoundary(tip, cell, town.roads);

                Logger::log("points",
                            "probe=" + std::to_string(probeId) + " road=" + std::to_string(road.id)
                                + " cell=" + std::to_string(side->cellId) + " end="
                                + (atOriginEnd ? "origin" : "far") + " t=" + fmtCoord(t)
                                + " road_len=" + fmtCoord(len) + " base=(" + fmtCoord(base.x)
                                + "," + fmtCoord(base.y) + ") tip=(" + fmtCoord(tip.x) + ","
                                + fmtCoord(tip.y) + ") min_road_dist=" + fmtCoord(minRoadDist)
                                + " inside=" + (inside ? "yes" : "no"));
            }
        }
    }

    Logger::log("points", "total=" + std::to_string(nextProbeId));
}

void resetRoadFrontageSegments(Town& town, float frontageSetback) {
    town.frontageSegmentIdCounter = 0;
    for (Road& road : town.roads) {
        road.sideA.segments.clear();
        road.sideB.segments.clear();
    }
    for (Road& road : town.roads) {
        if (road.isSecondary) {
            buildSecondaryRoadFrontageSegments(road, town, frontageSetback);
        }
    }
    buildRoadFrontageSegmentsFromProbes(town, frontageSetback);
}

void buildSecondaryRoadFrontageSegments(Road& road, Town& town, float setback) {
    if (!road.isSameCellSecondary()) {
        return;
    }
    const Cell& cell = town.cells[static_cast<std::size_t>(road.hostCellId)];
    const float len  = road.length();
    if (len < setback * 2.f + 0.5f) {
        return;
    }

    Vec2 origin{};
    Vec2 edgeDir{};
    if (!roadFrameForCell(road, road.hostCellId, origin, edgeDir)) {
        return;
    }

    for (RoadSideFrontage* side : {&road.sideA, &road.sideB}) {
        if (side->inward.length() < 1e-4f) {
            continue;
        }

        float startT = setback;
        float endT   = len - setback;
        if (endT - startT < 0.5f) {
            continue;
        }

        const Vec2 startProbe = origin + edgeDir * startT;
        const Vec2 endProbe   = origin + edgeDir * endT;
        const Vec2 midProbe   = origin + edgeDir * ((startT + endT) * 0.5f);
        if (!validSetbackProbe(startProbe, side->inward, setback, cell, town.roads, road.id)
            || !validSetbackProbe(endProbe, side->inward, setback, cell, town.roads, road.id)
            || !validSetbackProbe(midProbe, side->inward, setback, cell, town.roads, road.id)) {
            continue;
        }

        RoadFrontageSegment segment;
        segment.id         = town.frontageSegmentIdCounter++;
        segment.startT     = startT;
        segment.endT       = endT;
        segment.centerDist =
            segmentCenterDist(origin, edgeDir, startT, endT, cell.centroid);
        side->segments.push_back(segment);
    }
}

void syncAlleyCellStates(Town& town) {
    std::vector<int> counts(town.cells.size(), 0);
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        if (rec.hostCellId >= 0 && rec.hostCellId < static_cast<int>(town.cells.size())) {
            ++counts[static_cast<std::size_t>(rec.hostCellId)];
        }
    }

    for (std::size_t i = 0; i < town.cells.size(); ++i) {
        Cell& cell = town.cells[i];
        if (counts[i] > 0) {
            cell.alleyState = AlleyCellState::Expanding;
        } else if (cell.alleyState == AlleyCellState::Expanding) {
            cell.alleyState = AlleyCellState::Pending;
        }
    }
}

void trimSecondaryRoadRecords(Town& town, int targetCount) {
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [targetCount](const SecondaryRoadRecord& rec) {
                           return rec.addedAtQueueIndex >= targetCount;
                       }),
        town.secondaryRoadRecords.end());
}

void rebuildSecondaryRoadsFromRecords(Town& town) {
    town.roads.erase(std::remove_if(town.roads.begin(), town.roads.end(),
                                    [](const Road& r) { return r.isSecondary; }),
                     town.roads.end());

    for (std::size_t i = 0; i < town.roads.size(); ++i) {
        town.roads[i].id = static_cast<int>(i);
    }
    town.primaryRoadCount = static_cast<int>(town.roads.size());

    const int primaryCount = town.primaryRoadCount;
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        Road road;
        road.id                 = static_cast<int>(town.roads.size());
        road.a                  = rec.a;
        road.b                  = rec.b;
        road.cellA              = rec.hostCellId;
        road.cellB              = rec.hostCellId;
        road.hostCellId         = rec.hostCellId;
        road.isSecondary        = true;
        road.addedAtQueueIndex  = rec.addedAtQueueIndex;
        town.roads.push_back(road);
        assignSecondaryRoadInwards(town.roads.back(), town);
    }

    for (BuildingInstance& inst : town.buildingInstances) {
        if (inst.roadId >= primaryCount) {
            for (std::size_t ri = 0; ri < town.secondaryRoadRecords.size(); ++ri) {
                if (town.secondaryRoadRecords[ri].addedAtQueueIndex == inst.id) {
                    inst.roadId = primaryCount + static_cast<int>(ri);
                    break;
                }
            }
        }
        if (inst.plot.roadId >= primaryCount) {
            for (std::size_t ri = 0; ri < town.secondaryRoadRecords.size(); ++ri) {
                if (town.secondaryRoadRecords[ri].addedAtQueueIndex == inst.id) {
                    inst.plot.roadId = primaryCount + static_cast<int>(ri);
                    break;
                }
            }
        }
    }
}

void removeSecondaryRoadAtQueueIndex(Town& town, int queueIndex) {
    town.secondaryRoadRecords.erase(
        std::remove_if(town.secondaryRoadRecords.begin(), town.secondaryRoadRecords.end(),
                       [queueIndex](const SecondaryRoadRecord& rec) {
                           return rec.addedAtQueueIndex == queueIndex;
                       }),
        town.secondaryRoadRecords.end());
    rebuildSecondaryRoadsFromRecords(town);
}

void carveRoadFrontageForPlot(Town& town, const Plot& plot, float /*frontageSetback*/) {
    if (plot.roadId < 0 || plot.roadId >= static_cast<int>(town.roads.size())) {
        return;
    }

    Road& road = town.roads[static_cast<std::size_t>(plot.roadId)];
    RoadSideFrontage* side =
        plot.roadBank >= 0 ? road.sideForPlacement(plot.cellId, plot.roadBank)
                           : road.sideForCell(plot.cellId);
    if (side == nullptr) {
        return;
    }

    Vec2 origin{};
    Vec2 edgeDir{};
    if (!roadFrameForCell(road, plot.cellId, origin, edgeDir)) {
        return;
    }

    const Vec2 center =
        road.isSameCellSecondary()
            ? town.cells[static_cast<std::size_t>(plot.cellId)].centroid
            : town.center;

    carveSideFrontage(*side, origin, edgeDir, plotTMin(plot, origin, edgeDir),
                      plotTMax(plot, origin, edgeDir), center, town.frontageSegmentIdCounter);
}

void carveRoadFrontageForFootprint(Town& town, int roadId, int cellId,
                                   const BuildingFootprint& mainFootprint) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }

    Road& road = town.roads[static_cast<std::size_t>(roadId)];

    Vec2 origin{};
    Vec2 edgeDir{};
    if (!roadFrameForCell(road, cellId, origin, edgeDir)) {
        return;
    }

    Vec2 buildingCenter{};
    for (const Vec2& corner : mainFootprint.corners) {
        buildingCenter = buildingCenter + corner;
    }
    buildingCenter = buildingCenter * 0.25f;

    RoadSideFrontage* side = road.sideForCell(cellId);
    if (road.isSameCellSecondary()) {
        const float roadLen = road.length();
        const Vec2  farEnd  = origin + edgeDir * roadLen;
        const Vec2  ab      = farEnd - origin;
        const float lenSq   = ab.x * ab.x + ab.y * ab.y;
        if (lenSq >= 1e-8f) {
            const float t =
                std::clamp(((buildingCenter.x - origin.x) * ab.x
                            + (buildingCenter.y - origin.y) * ab.y)
                               / lenSq,
                           0.f, 1.f);
            const Vec2 nearest{origin.x + ab.x * t, origin.y + ab.y * t};
            const Vec2 toCenter = buildingCenter - nearest;
            const int  bank =
                toCenter.dot(road.sideA.inward) >= toCenter.dot(road.sideB.inward) ? 0 : 1;
            side = road.sideBank(bank);
        }
    }
    if (side == nullptr) {
        return;
    }

    const float roadLen = road.length();
    const Vec2  farEnd  = origin + edgeDir * roadLen;
    const Vec2  ab      = farEnd - origin;
    const float lenSq   = ab.x * ab.x + ab.y * ab.y;

    Vec2 towardRoad{};
    if (lenSq >= 1e-8f) {
        const float t =
            std::clamp(((buildingCenter.x - origin.x) * ab.x + (buildingCenter.y - origin.y) * ab.y)
                           / lenSq,
                       0.f, 1.f);
        const Vec2 nearest{origin.x + ab.x * t, origin.y + ab.y * t};
        const Vec2 dir = nearest - buildingCenter;
        if (dir.length() >= 1e-4f) {
            towardRoad = dir.normalized();
        }
    }

    int   frontEdge = 0;
    float bestScore = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const Vec2& a = mainFootprint.corners[i];
        const Vec2& b = mainFootprint.corners[(i + 1) % 4];
        if ((b - a).length() < 1e-4f) {
            continue;
        }
        const Vec2 edgeDirN = (b - a).normalized();
        Vec2       normal   = perpendicular(edgeDirN);
        const Vec2 edgeMid  = (a + b) * 0.5f;
        if (normal.dot(edgeMid - buildingCenter) < 0.f) {
            normal = normal * -1.f;
        }
        const float score = normal.dot(towardRoad);
        if (score > bestScore) {
            bestScore = score;
            frontEdge = i;
        }
    }

    const Vec2& fa = mainFootprint.corners[frontEdge];
    const Vec2& fb = mainFootprint.corners[(frontEdge + 1) % 4];
    const float t0        = (fa - origin).dot(edgeDir);
    const float t1        = (fb - origin).dot(edgeDir);
    const float usedStart = std::min(t0, t1);
    const float usedEnd   = std::max(t0, t1);

    const Vec2 center =
        road.isSameCellSecondary()
            ? town.cells[static_cast<std::size_t>(cellId)].centroid
            : town.center;

    carveSideFrontage(*side, origin, edgeDir, usedStart, usedEnd, center,
                      town.frontageSegmentIdCounter);
}

void rebuildRoadMesh(Town& town, const std::array<uint8_t, 3>& primaryColor,
                     const std::array<uint8_t, 3>& secondaryColor,
                     const std::array<uint8_t, 3>& bridgeColor, float pixelsPerUnit,
                     const TerrainAtlas* terrain, bool clipRoadsAtWater) {
    const sf::Color primarySf(primaryColor[0], primaryColor[1], primaryColor[2]);
    const sf::Color secondarySf(secondaryColor[0], secondaryColor[1], secondaryColor[2]);
    const sf::Color bridgeSf(bridgeColor[0], bridgeColor[1], bridgeColor[2]);
    const float     thickness = 1.f * pixelsPerUnit;

    town.roadMesh.setPrimitiveType(sf::Triangles);
    town.roadMesh.clear();

    const bool clip = terrain != nullptr && terrain->valid && clipRoadsAtWater;

    for (const Road& road : town.roads) {
        const sf::Color& color =
            road.isBridge ? bridgeSf : (road.isSecondary ? secondarySf : primarySf);

        const Vec2 aPx{road.a.x * pixelsPerUnit, road.a.y * pixelsPerUnit};
        const Vec2 bPx{road.b.x * pixelsPerUnit, road.b.y * pixelsPerUnit};

        if (road.isBridge || !clip) {
            appendThickSegment(town.roadMesh, aPx, bPx, thickness, color);
            continue;
        }

        const std::vector<std::pair<float, float>> intervals =
            clipRoadSegmentToLand(road.a, road.b, *terrain);
        const Vec2 delta = road.b - road.a;
        for (const auto& interval : intervals) {
            const Vec2 landA = road.a + delta * interval.first;
            const Vec2 landB = road.a + delta * interval.second;
            const Vec2 a{landA.x * pixelsPerUnit, landA.y * pixelsPerUnit};
            const Vec2 b{landB.x * pixelsPerUnit, landB.y * pixelsPerUnit};
            appendThickSegment(town.roadMesh, a, b, thickness, color);
        }
    }
}
