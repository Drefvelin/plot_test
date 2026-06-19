#include "terrain/TerrainAtlas.h"

#include "common/RenderPrimitives.h"
#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "util/Profile.h"
#include "terrain/TerrainColors.h"
#include "common/Units.h"

#include <SFML/Graphics/Image.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace {

int gridIndex(int x, int y, int w) { return y * w + x; }

constexpr float kNoRegionOutlineDist = 1e30f;

TerrainId computeMajorityLandId(TerrainId plainsId, TerrainId forestId, TerrainId hillsId,
                                TerrainId mountainId, int countPlains, int countForest,
                                int countHills, int countMountain) {
    struct Entry {
        TerrainId kind;
        int         count;
        int         tieRank;
    };
    Entry entries[] = {
        {plainsId, countPlains, 0},
        {forestId, countForest, 1},
        {hillsId, countHills, 2},
        {mountainId, countMountain, 3},
    };
    Entry* best = &entries[0];
    for (int i = 1; i < 4; ++i) {
        if (entries[i].count > best->count
            || (entries[i].count == best->count && entries[i].tieRank < best->tieRank)) {
            best = &entries[i];
        }
    }
    if (best->count <= 0) {
        return plainsId;
    }
    return best->kind;
}

const std::vector<std::vector<Vec2>>* regionOutlineGraphs(const TerrainAtlas& atlas,
                                                          TerrainId kind) {
    return atlas.outlineGraphs(kind);
}

float distToOutlineGraphs(const Vec2& p, const std::vector<std::vector<Vec2>>& graphs) {
    float best = kNoRegionOutlineDist;
    for (const std::vector<Vec2>& graph : graphs) {
        if (graph.size() < 2) {
            continue;
        }
        for (std::size_t i = 1; i < graph.size(); ++i) {
            const float dist = distancePointToSegment(p, graph[i - 1], graph[i]);
            if (dist < best) {
                best = dist;
                if (best <= 0.f) {
                    return 0.f;
                }
            }
        }
    }
    return best;
}

struct IPoint {
    int x = 0;
    int y = 0;

    bool operator==(const IPoint& other) const { return x == other.x && y == other.y; }
    bool operator<(const IPoint& other) const {
        if (y != other.y) {
            return y < other.y;
        }
        return x < other.x;
    }
};

Vec2 cornerToWorld(int cx, int cy, int w, int h, float diagramW, float diagramH) {
    return {static_cast<float>(cx) / static_cast<float>(w) * diagramW,
            static_cast<float>(cy) / static_cast<float>(h) * diagramH};
}

int worldUnitsToPixelRadius(float units, float diagramW, int sourceW) {
    if (units <= 1e-4f || sourceW <= 0 || diagramW <= 1e-4f) {
        return 0;
    }
    const float unitsPerPixel = diagramW / static_cast<float>(sourceW);
    return std::max(0, static_cast<int>(std::ceil(units / unitsPerPixel)));
}

std::vector<uint8_t> buildMask(const std::function<bool(int, int)>& inside, int w, int h) {
    std::vector<uint8_t> mask(static_cast<std::size_t>(w * h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (inside(x, y)) {
                mask[static_cast<std::size_t>(gridIndex(x, y, w))] = 1;
            }
        }
    }
    return mask;
}

std::vector<uint8_t> dilateMask(const std::vector<uint8_t>& mask, int w, int h, int radiusPx) {
    if (radiusPx <= 0) {
        return mask;
    }
    std::vector<uint8_t> out = mask;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (mask[static_cast<std::size_t>(gridIndex(x, y, w))] == 0) {
                continue;
            }
            for (int dy = -radiusPx; dy <= radiusPx; ++dy) {
                for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    out[static_cast<std::size_t>(gridIndex(nx, ny, w))] = 1;
                }
            }
        }
    }
    return out;
}

std::function<bool(int, int)> maskPredicate(const std::vector<uint8_t>& mask, int w, int h) {
    return [mask, w, h](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return false;
        }
        return mask[static_cast<std::size_t>(gridIndex(x, y, w))] != 0;
    };
}

bool sampleRasterForbidden(const std::vector<TerrainId>& raster, int w, int h, float diagramW,
                           float diagramH, Vec2 worldPos, const TerrainCatalog& catalog) {
    if (worldPos.x < 0.f || worldPos.y < 0.f || worldPos.x > diagramW || worldPos.y > diagramH) {
        return false;
    }
    const int px = std::clamp(static_cast<int>(worldPos.x / diagramW * static_cast<float>(w)), 0,
                              w - 1);
    const int py = std::clamp(static_cast<int>(worldPos.y / diagramH * static_cast<float>(h)), 0,
                              h - 1);
    return terrainIdIsForbidden(raster[static_cast<std::size_t>(gridIndex(px, py, w))], catalog);
}

bool segmentCrossesForbidden(const Vec2& a, const Vec2& b, const std::vector<TerrainId>& raster,
                             int w, int h, float diagramW, float diagramH,
                             const TerrainCatalog& catalog) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return sampleRasterForbidden(raster, w, h, diagramW, diagramH, a, catalog);
    }
    constexpr float kStep = 0.25f;
    for (float dist = 0.f; dist <= len + 1e-3f; dist += kStep) {
        const float t = std::min(dist / len, 1.f);
        if (sampleRasterForbidden(raster, w, h, diagramW, diagramH, a + delta * t, catalog)) {
            return true;
        }
    }
    return false;
}

Vec2 sampleAtDistance(const std::vector<Vec2>& points, float distance, bool closed) {
    if (points.empty()) {
        return {};
    }
    if (points.size() == 1) {
        return points.front();
    }

    float travelled = 0.f;
    const int segCount = closed ? static_cast<int>(points.size())
                                : static_cast<int>(points.size()) - 1;
    for (int i = 0; i < segCount; ++i) {
        const Vec2& a = points[static_cast<std::size_t>(i)];
        const Vec2& b = points[static_cast<std::size_t>((i + 1) % points.size())];
        const Vec2  ab  = b - a;
        const float len = ab.length();
        if (len < 1e-6f) {
            continue;
        }
        if (travelled + len >= distance - 1e-4f) {
            const float t = std::clamp((distance - travelled) / len, 0.f, 1.f);
            return a + ab * t;
        }
        travelled += len;
    }

    return closed ? points.front() : points.back();
}

float polylineLength(const std::vector<Vec2>& points, bool closed) {
    if (points.size() < 2) {
        return 0.f;
    }
    float total = 0.f;
    const int segCount = closed ? static_cast<int>(points.size())
                                : static_cast<int>(points.size()) - 1;
    for (int i = 0; i < segCount; ++i) {
        total += (points[static_cast<std::size_t>((i + 1) % points.size())]
                  - points[static_cast<std::size_t>(i)])
                     .length();
    }
    return total;
}

std::vector<Vec2> resamplePolylineByDistance(const std::vector<Vec2>& points, float spacing,
                                             bool closed) {
    if (points.size() < 2 || spacing <= 1e-4f) {
        return points;
    }

    const float total = polylineLength(points, closed);
    if (total <= spacing + 1e-4f) {
        return points;
    }

    std::vector<Vec2> out;
    out.push_back(points.front());
    for (float dist = spacing; dist < total - 1e-4f; dist += spacing) {
        out.push_back(sampleAtDistance(points, dist, closed));
    }
    if (!closed) {
        out.push_back(points.back());
    }
    return out;
}

std::vector<Vec2> subdivideContourUntilClear(std::vector<Vec2> poly, bool closed,
                                             const std::vector<TerrainId>& raster, int w, int h,
                                             float diagramW, float diagramH,
                                             const TerrainCatalog& catalog) {
    if (poly.size() < 2) {
        return poly;
    }

    const auto edgeCount = [&]() -> std::size_t {
        return closed ? poly.size() : poly.size() - 1;
    };

    constexpr int kMaxSubdivides = 4096;
    for (int guard = 0; guard < kMaxSubdivides; ++guard) {
        bool subdivided = false;
        for (std::size_t i = 0; i < edgeCount(); ++i) {
            const std::size_t j = closed ? (i + 1) % poly.size() : i + 1;
            if (!segmentCrossesForbidden(poly[i], poly[j], raster, w, h, diagramW, diagramH,
                                         catalog)) {
                continue;
            }
            poly.insert(poly.begin() + static_cast<std::ptrdiff_t>(j),
                        (poly[i] + poly[j]) * 0.5f);
            subdivided = true;
            break;
        }
        if (!subdivided) {
            break;
        }
    }
    return poly;
}

int crossZ(int ax, int ay, int bx, int by);

void removeDirectedEdge(std::map<IPoint, std::vector<IPoint>>& adjacency, const IPoint& from,
                        const IPoint& to);

std::vector<std::vector<Vec2>> buildContourGraphs(
    const std::vector<std::vector<Vec2>>& boundaryPolylines, float edgeSpacing, bool closed,
    bool waterSafe, const std::vector<TerrainId>& raster, int w, int h, float diagramW,
    float diagramH, const TerrainCatalog& catalog) {
    std::vector<std::vector<Vec2>> graphs;
    graphs.reserve(boundaryPolylines.size());
    for (const std::vector<Vec2>& polyline : boundaryPolylines) {
        if (polyline.size() < 2) {
            continue;
        }
        std::vector<Vec2> graph = resamplePolylineByDistance(polyline, edgeSpacing, closed);
        if (waterSafe) {
            graph = subdivideContourUntilClear(std::move(graph), closed, raster, w, h, diagramW,
                                                diagramH, catalog);
        }
        if (graph.size() >= 2) {
            graphs.push_back(std::move(graph));
        }
    }
    return graphs;
}

std::vector<std::vector<Vec2>> extractInterfacePolylines(const std::function<bool(int, int)>& inside,
                                                         int w, int h, float diagramW,
                                                         float diagramH) {
    std::map<IPoint, std::vector<IPoint>> adjacency;
    const auto addEdge = [&](int x0, int y0, int x1, int y1) {
        const IPoint a{x0, y0};
        const IPoint b{x1, y1};
        adjacency[a].push_back(b);
        adjacency[b].push_back(a);
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!inside(x, y)) {
                continue;
            }
            if (y == 0 || !inside(x, y - 1)) {
                addEdge(x, y, x + 1, y);
            }
            if (y == h - 1 || !inside(x, y + 1)) {
                addEdge(x + 1, y + 1, x, y + 1);
            }
            if (x == 0 || !inside(x - 1, y)) {
                addEdge(x, y, x, y + 1);
            }
            if (x == w - 1 || !inside(x + 1, y)) {
                addEdge(x + 1, y + 1, x + 1, y);
            }
        }
    }

    std::vector<std::vector<Vec2>> polylines;
    const auto degree = [&](const IPoint& p) -> int {
        auto it = adjacency.find(p);
        return it == adjacency.end() ? 0 : static_cast<int>(it->second.size());
    };

    while (!adjacency.empty()) {
        auto pickStart = [&]() -> IPoint {
            for (const auto& [point, neighbors] : adjacency) {
                if (neighbors.empty()) {
                    continue;
                }
                if (degree(point) == 1) {
                    return point;
                }
            }
            return adjacency.begin()->first;
        };

        IPoint start = pickStart();
        if (adjacency.find(start) == adjacency.end() || adjacency[start].empty()) {
            break;
        }

        std::vector<Vec2> polyline;
        IPoint            prev = start;
        IPoint            cur  = adjacency[start].front();
        removeDirectedEdge(adjacency, start, cur);
        removeDirectedEdge(adjacency, cur, start);
        polyline.push_back(cornerToWorld(start.x, start.y, w, h, diagramW, diagramH));

        for (int guard = 0; guard < static_cast<int>(w * h) * 4 + 8; ++guard) {
            polyline.push_back(cornerToWorld(cur.x, cur.y, w, h, diagramW, diagramH));
            auto nextIt = adjacency.find(cur);
            if (nextIt == adjacency.end() || nextIt->second.empty()) {
                break;
            }

            IPoint next = nextIt->second.front();
            for (const IPoint& candidate : nextIt->second) {
                if (!(candidate.x == prev.x && candidate.y == prev.y)) {
                    next = candidate;
                    break;
                }
            }
            if (next.x == prev.x && next.y == prev.y) {
                break;
            }

            removeDirectedEdge(adjacency, cur, next);
            removeDirectedEdge(adjacency, next, cur);
            prev = cur;
            cur  = next;
            if (cur.x == start.x && cur.y == start.y) {
                break;
            }
        }

        if (polyline.size() >= 2) {
            polylines.push_back(std::move(polyline));
        }
    }

    return polylines;
}

std::vector<std::vector<Vec2>> buildWaterContourGraph(
    const std::function<bool(int, int)>& insideMask, int w, int h, float diagramW, float diagramH,
    float insetWorld, float edgeSpacing, const std::vector<TerrainId>& raster,
    const TerrainCatalog& catalog) {
    const int insetPx = worldUnitsToPixelRadius(insetWorld, diagramW, w);
    const std::vector<uint8_t> rawMask = buildMask(insideMask, w, h);
    const std::vector<uint8_t> dilated = dilateMask(rawMask, w, h, insetPx);
    const auto                 trace   = maskPredicate(dilated, w, h);
    const auto loops = extractInterfacePolylines(trace, w, h, diagramW, diagramH);
    return buildContourGraphs(loops, edgeSpacing, false, true, raster, w, h, diagramW, diagramH,
                              catalog);
}

std::vector<Vec2> simplifyPolylineMinEdgeLength(const std::vector<Vec2>& polyline, float minLen) {
    if (polyline.size() < 2 || minLen <= 0.f) {
        return polyline;
    }

    std::vector<Vec2> simplified;
    simplified.push_back(polyline.front());
    for (std::size_t i = 1; i < polyline.size(); ++i) {
        const float dist = (polyline[i] - simplified.back()).length();
        if (dist >= minLen || i + 1 == polyline.size()) {
            if (dist >= minLen * 0.25f || simplified.size() == 1) {
                simplified.push_back(polyline[i]);
            }
        }
    }

    if (simplified.size() < 2 && polyline.size() >= 2) {
        simplified = {polyline.front(), polyline.back()};
    }
    return simplified;
}

std::vector<std::vector<Vec2>> simplifyContourGraphs(const std::vector<std::vector<Vec2>>& graphs,
                                                     float minLen) {
    std::vector<std::vector<Vec2>> out;
    out.reserve(graphs.size());
    for (const std::vector<Vec2>& graph : graphs) {
        std::vector<Vec2> simplified = simplifyPolylineMinEdgeLength(graph, minLen);
        if (simplified.size() >= 2) {
            out.push_back(std::move(simplified));
        }
    }
    return out;
}

std::size_t countContourEdges(const std::vector<std::vector<Vec2>>& contours, bool closed) {
    std::size_t total = 0;
    for (const std::vector<Vec2>& contour : contours) {
        if (contour.size() < 2) {
            continue;
        }
        total += closed ? contour.size() : contour.size() - 1;
    }
    return total;
}

std::size_t countContourVertices(const std::vector<std::vector<Vec2>>& contours) {
    std::size_t total = 0;
    for (const std::vector<Vec2>& contour : contours) {
        total += contour.size();
    }
    return total;
}

void appendContourGraphMesh(sf::VertexArray& mesh, const std::vector<std::vector<Vec2>>& contours,
                            float pixelsPerUnit, float widthWorldUnits, const sf::Color& color,
                            bool closed) {
    const float thickness = widthWorldUnits * pixelsPerUnit;
    for (const std::vector<Vec2>& contour : contours) {
        if (contour.size() < 2) {
            continue;
        }
        const std::size_t edgeCount = closed ? contour.size() : contour.size() - 1;
        for (std::size_t i = 0; i < edgeCount; ++i) {
            const std::size_t j = closed ? (i + 1) % contour.size() : i + 1;
            const Vec2        a{units::toPixels(contour[i].x, pixelsPerUnit),
                       units::toPixels(contour[i].y, pixelsPerUnit)};
            const Vec2 b{units::toPixels(contour[j].x, pixelsPerUnit),
                         units::toPixels(contour[j].y, pixelsPerUnit)};
            appendThickSegment(mesh, a, b, thickness, color);
        }
    }
}

int crossZ(int ax, int ay, int bx, int by) { return ax * by - ay * bx; }

void removeDirectedEdge(std::map<IPoint, std::vector<IPoint>>& adjacency, const IPoint& from,
                        const IPoint& to) {
    auto it = adjacency.find(from);
    if (it == adjacency.end()) {
        return;
    }
    auto& neighbors = it->second;
    neighbors.erase(std::remove(neighbors.begin(), neighbors.end(), to), neighbors.end());
    if (neighbors.empty()) {
        adjacency.erase(it);
    }
}

IPoint chooseNextBoundaryCorner(const IPoint& prev, const IPoint& cur,
                                const std::vector<IPoint>& neighbors) {
    const int inX = cur.x - prev.x;
    const int inY = cur.y - prev.y;
    IPoint    best  = prev;
    int       bestZ = std::numeric_limits<int>::min();
    for (const IPoint& next : neighbors) {
        if (next.x == prev.x && next.y == prev.y) {
            continue;
        }
        const int outX = next.x - cur.x;
        const int outY = next.y - cur.y;
        const int z    = crossZ(inX, inY, outX, outY);
        if (z > bestZ || (z == bestZ && next < best)) {
            bestZ = z;
            best  = next;
        }
    }
    return best;
}

std::vector<std::vector<Vec2>> traceExactContours(const std::function<bool(int, int)>& inside,
                                                 int w, int h, float diagramW, float diagramH) {
    using Edge = std::pair<IPoint, IPoint>;
    std::vector<Edge> edges;
    edges.reserve(static_cast<std::size_t>(w * h));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!inside(x, y)) {
                continue;
            }
            if (x == 0 || !inside(x - 1, y)) {
                edges.push_back({{x, y}, {x, y + 1}});
            }
            if (x == w - 1 || !inside(x + 1, y)) {
                edges.push_back({{x + 1, y + 1}, {x + 1, y}});
            }
            if (y == 0 || !inside(x, y - 1)) {
                edges.push_back({{x, y}, {x + 1, y}});
            }
            if (y == h - 1 || !inside(x, y + 1)) {
                edges.push_back({{x + 1, y + 1}, {x, y + 1}});
            }
        }
    }

    std::map<IPoint, std::vector<IPoint>> adjacency;
    for (const Edge& edge : edges) {
        adjacency[edge.first].push_back(edge.second);
    }

    std::vector<std::vector<Vec2>> contours;
    while (!adjacency.empty()) {
        auto it = adjacency.begin();
        while (it != adjacency.end() && it->second.empty()) {
            it = adjacency.erase(it);
        }
        if (it == adjacency.end()) {
            break;
        }

        const IPoint start  = it->first;
        const IPoint first  = it->second.front();
        IPoint       prev   = start;
        IPoint       current = first;
        removeDirectedEdge(adjacency, start, first);
        removeDirectedEdge(adjacency, first, start);

        std::vector<Vec2> loop;
        loop.push_back(cornerToWorld(start.x, start.y, w, h, diagramW, diagramH));

        for (int guard = 0; guard < static_cast<int>(edges.size()) + 8; ++guard) {
            loop.push_back(cornerToWorld(current.x, current.y, w, h, diagramW, diagramH));
            auto nextIt = adjacency.find(current);
            if (nextIt == adjacency.end() || nextIt->second.empty()) {
                break;
            }

            const IPoint next = chooseNextBoundaryCorner(prev, current, nextIt->second);
            if (next.x == prev.x && next.y == prev.y) {
                break;
            }

            removeDirectedEdge(adjacency, current, next);
            removeDirectedEdge(adjacency, next, current);

            prev    = current;
            current = next;
            if (current == start) {
                break;
            }
        }

        if (loop.size() >= 3) {
            contours.push_back(loop);
        }
    }

    return contours;
}

int countMaskComponents(const std::function<bool(int, int)>& inside, int w, int h) {
    std::vector<int> labels(static_cast<std::size_t>(w * h), -1);
    int              count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = gridIndex(x, y, w);
            if (labels[static_cast<std::size_t>(idx)] >= 0 || !inside(x, y)) {
                continue;
            }
            ++count;
            std::vector<std::pair<int, int>> stack = {{x, y}};
            labels[static_cast<std::size_t>(idx)] = count;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                constexpr int kDx[4] = {1, -1, 0, 0};
                constexpr int kDy[4] = {0, 0, 1, -1};
                for (int i = 0; i < 4; ++i) {
                    const int nx = cx + kDx[i];
                    const int ny = cy + kDy[i];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    const int nidx = gridIndex(nx, ny, w);
                    if (labels[static_cast<std::size_t>(nidx)] >= 0 || !inside(nx, ny)) {
                        continue;
                    }
                    labels[static_cast<std::size_t>(nidx)] = count;
                    stack.push_back({nx, ny});
                }
            }
        }
    }
    return count;
}

}  // namespace

const char* terrainOverlayModeLabel(TerrainOverlayMode mode) {
    switch (mode) {
    case TerrainOverlayMode::TerrainAndDebug:
        return "terrain+debug";
    case TerrainOverlayMode::DebugOnly:
        return "debug";
    case TerrainOverlayMode::HopDebug:
        return "hop";
    case TerrainOverlayMode::Off:
        return "off";
    }
    return "off";
}

bool TerrainAtlas::isForbidden(Vec2 worldPos) const {
    if (sourceW <= 0 || sourceH <= 0) {
        return false;
    }
    if (worldPos.x < 0.f || worldPos.y < 0.f || worldPos.x > diagramW || worldPos.y > diagramH) {
        return false;
    }
    if (!forbiddenDilated.empty()) {
        const int px = std::clamp(
            static_cast<int>(worldPos.x / diagramW * static_cast<float>(sourceW)), 0, sourceW - 1);
        const int py = std::clamp(
            static_cast<int>(worldPos.y / diagramH * static_cast<float>(sourceH)), 0, sourceH - 1);
        return forbiddenDilated[static_cast<std::size_t>(py * sourceW + px)] != 0;
    }
    return catalog != nullptr && terrainIdIsForbidden(sample(worldPos), *catalog);
}

bool TerrainAtlas::isBuildable(Vec2 worldPos) const {
    return !isForbidden(worldPos);
}

TerrainId TerrainAtlas::sample(Vec2 worldPos) const {
    if (sourceW <= 0 || sourceH <= 0 || raster.empty()) {
        return kTerrainUnknown;
    }
    if (worldPos.x < 0.f || worldPos.y < 0.f || worldPos.x > diagramW || worldPos.y > diagramH) {
        return kTerrainUnknown;
    }
    const int px = std::clamp(static_cast<int>(worldPos.x / diagramW * static_cast<float>(sourceW)),
                              0, sourceW - 1);
    const int py = std::clamp(static_cast<int>(worldPos.y / diagramH * static_cast<float>(sourceH)),
                              0, sourceH - 1);
    return raster[static_cast<std::size_t>(gridIndex(px, py, sourceW))];
}

const std::vector<std::vector<Vec2>>* TerrainAtlas::outlineGraphs(TerrainId id) const {
    const auto it = outlinesByTerrainId.find(id);
    if (it == outlinesByTerrainId.end()) {
        return nullptr;
    }
    return &it->second;
}

bool TerrainAtlas::hasOutline(TerrainId id) const {
    const auto* graphs = outlineGraphs(id);
    return graphs != nullptr && !graphs->empty();
}

bool TerrainAtlas::hasRegionOutline(TerrainId kind) const {
    if (!valid || kind == majorityLandId) {
        return false;
    }
    const std::vector<std::vector<Vec2>>* graphs = regionOutlineGraphs(*this, kind);
    return graphs != nullptr && !graphs->empty();
}

float TerrainAtlas::distToRegionEdge(Vec2 worldPos, TerrainId kind) const {
    if (!hasRegionOutline(kind)) {
        return kNoRegionOutlineDist;
    }
    const std::vector<std::vector<Vec2>>* graphs = regionOutlineGraphs(*this, kind);
    if (graphs == nullptr || graphs->empty()) {
        return kNoRegionOutlineDist;
    }
    return distToOutlineGraphs(worldPos, *graphs);
}

void buildTerrainDebugMeshes(TerrainAtlas& atlas, float pixelsPerUnit, float contourWidth) {
    atlas.debugForbiddenMesh.setPrimitiveType(sf::Triangles);
    atlas.debugForbiddenMesh.clear();
    atlas.debugRiverMesh.setPrimitiveType(sf::Triangles);
    atlas.debugRiverMesh.clear();
    atlas.debugShoreMesh.setPrimitiveType(sf::Triangles);
    atlas.debugShoreMesh.clear();
    atlas.debugForestMesh.setPrimitiveType(sf::Triangles);
    atlas.debugForestMesh.clear();
    atlas.debugHillsMesh.setPrimitiveType(sf::Triangles);
    atlas.debugHillsMesh.clear();

    appendContourGraphMesh(atlas.debugForbiddenMesh, atlas.forbiddenPolygons, pixelsPerUnit,
                           contourWidth, sf::Color(255, 0, 255), false);
    const TerrainId seaId = atlas.catalog != nullptr ? atlas.catalog->resolveKey("sea")
                                                     : kTerrainUnknown;
    const TerrainId riverId = atlas.catalog != nullptr ? atlas.catalog->resolveKey("river")
                                                       : kTerrainUnknown;
    const TerrainId forestId = atlas.catalog != nullptr ? atlas.catalog->resolveKey("forest")
                                                        : kTerrainUnknown;
    const TerrainId hillsId = atlas.catalog != nullptr ? atlas.catalog->resolveKey("hills")
                                                       : kTerrainUnknown;
    const auto* river = atlas.outlineGraphs(riverId);
    const auto* sea = atlas.outlineGraphs(seaId);
    const auto* forest = atlas.outlineGraphs(forestId);
    const auto* hills = atlas.outlineGraphs(hillsId);
    if (river != nullptr) {
        appendContourGraphMesh(atlas.debugRiverMesh, *river, pixelsPerUnit, contourWidth,
                               sf::Color(0, 255, 255), false);
    }
    if (sea != nullptr) {
        appendContourGraphMesh(atlas.debugShoreMesh, *sea, pixelsPerUnit, contourWidth,
                               sf::Color(255, 255, 0), false);
    }
    if (forest != nullptr) {
        appendContourGraphMesh(atlas.debugForestMesh, *forest, pixelsPerUnit, contourWidth,
                               sf::Color(0, 220, 0), false);
    }
    if (hills != nullptr) {
        appendContourGraphMesh(atlas.debugHillsMesh, *hills, pixelsPerUnit, contourWidth,
                               sf::Color(140, 120, 90), false);
    }
}

TerrainAtlas bakeTerrain(const Config& config, const TerrainCatalog& catalog,
                         const std::filesystem::path& projectRoot) {
    PROFILE_SCOPE(ProfileScopeId::TerrainBake);
    TerrainAtlas atlas;

    const auto imagePath  = projectRoot / config.terrain.imagePath;
    const auto colorsPath = projectRoot / config.terrain.colorsPath;

    const std::vector<TerrainColorEntry> colorMap = loadTerrainColorMap(colorsPath, catalog);
    if (colorMap.empty()) {
        Logger::log("terrain", "bake failed: no color map at " + colorsPath.string());
        return atlas;
    }

    sf::Image image;
    if (!image.loadFromFile(imagePath.string())) {
        Logger::log("terrain", "bake failed: could not load image " + imagePath.string());
        return atlas;
    }

    const int   w        = static_cast<int>(image.getSize().x);
    const int   h        = static_cast<int>(image.getSize().y);
    const float diagramW = config.diagram.width;
    const float diagramH = config.diagram.height;
    const float simplify       = config.terrain.simplifyTolerance;
    const float waterInset     = config.terrain.waterInset;
    const float shoreInset     = config.terrain.shoreInset;
    const float riverInset     = config.terrain.riverInset;
    const float contourWidth   = config.terrain.contourWidth;
    const int   waterInsetPx   = worldUnitsToPixelRadius(waterInset, diagramW, w);
    const int   shoreInsetPx   = worldUnitsToPixelRadius(shoreInset, diagramW, w);
    const int   riverInsetPx   = worldUnitsToPixelRadius(riverInset, diagramW, w);

    atlas.sourceW   = w;
    atlas.sourceH   = h;
    atlas.diagramW  = diagramW;
    atlas.diagramH  = diagramH;
    atlas.waterInset = waterInset;
    atlas.shoreInset = shoreInset;
    atlas.catalog    = &catalog;
    atlas.raster.assign(static_cast<std::size_t>(w * h), kTerrainUnknown);

    const TerrainId seaId      = catalog.resolveKey("sea");
    const TerrainId riverId    = catalog.resolveKey("river");
    const TerrainId plainsId   = catalog.resolveKey("plains");
    const TerrainId forestId   = catalog.resolveKey("forest");
    const TerrainId hillsId    = catalog.resolveKey("hills");
    const TerrainId mountainId = catalog.resolveKey("mountain");

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const sf::Color px = image.getPixel(static_cast<unsigned>(x), static_cast<unsigned>(y));
            atlas.raster[static_cast<std::size_t>(gridIndex(x, y, w))] =
                classifyTerrainColor(px.r, px.g, px.b, colorMap);
        }
    }

    const auto kindAt = [&](int x, int y) -> TerrainId {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return kTerrainUnknown;
        }
        return atlas.raster[static_cast<std::size_t>(gridIndex(x, y, w))];
    };

    const auto insideForbidden = [&](int x, int y) -> bool {
        return terrainIdIsForbidden(kindAt(x, y), catalog);
    };
    const auto insideSea = [&](int x, int y) -> bool { return kindAt(x, y) == seaId; };
    const auto insideRiver = [&](int x, int y) -> bool { return kindAt(x, y) == riverId; };
    const auto insideForest = [&](int x, int y) -> bool { return kindAt(x, y) == forestId; };
    const auto insideHills = [&](int x, int y) -> bool {
        const TerrainId id = kindAt(x, y);
        return id == hillsId || id == mountainId;
    };

    const auto insidePlains = [&](int x, int y) -> bool { return kindAt(x, y) == plainsId; };

    int countPlains = 0;
    int countForest = 0;
    int countHills  = 0;
    int countMountain = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const TerrainId id = kindAt(x, y);
            if (id == plainsId) {
                ++countPlains;
            } else if (id == forestId) {
                ++countForest;
            } else if (id == hillsId) {
                ++countHills;
            } else if (id == mountainId) {
                ++countMountain;
            }
        }
    }
    atlas.majorityLandId = computeMajorityLandId(plainsId, forestId, hillsId, mountainId,
                                                  countPlains, countForest, countHills,
                                                  countMountain);

    const auto buildLandOutlines =
        [&](const std::function<bool(int, int)>& inside) -> std::vector<std::vector<Vec2>> {
        const auto polylines = extractInterfacePolylines(inside, w, h, diagramW, diagramH);
        return buildContourGraphs(polylines, simplify, false, true, atlas.raster, w, h, diagramW,
                                  diagramH, catalog);
    };

    const std::vector<uint8_t> rawForbiddenMask = buildMask(insideForbidden, w, h);
    atlas.forbiddenDilated = dilateMask(rawForbiddenMask, w, h, waterInsetPx);

    const std::vector<uint8_t> rawSeaMask = buildMask(insideSea, w, h);
    const std::vector<uint8_t> shoreTraceMask =
        dilateMask(rawSeaMask, w, h, shoreInsetPx);
    const std::vector<uint8_t> rawRiverMask = buildMask(insideRiver, w, h);
    const std::vector<uint8_t> riverTraceMask =
        dilateMask(rawRiverMask, w, h, riverInsetPx);

    const auto traceForbidden = maskPredicate(atlas.forbiddenDilated, w, h);
    const auto traceShore     = maskPredicate(shoreTraceMask, w, h);
    const auto traceRiverBank = maskPredicate(riverTraceMask, w, h);

    const auto forbiddenLoops = extractInterfacePolylines(traceForbidden, w, h, diagramW, diagramH);
    const auto shoreLoops     = extractInterfacePolylines(traceShore, w, h, diagramW, diagramH);
    const auto riverPolylines = extractInterfacePolylines(traceRiverBank, w, h, diagramW, diagramH);

    atlas.forbiddenPolygons =
        buildContourGraphs(forbiddenLoops, simplify, false, true, atlas.raster, w, h, diagramW,
                           diagramH, catalog);
    atlas.outlinesByTerrainId[seaId] = buildContourGraphs(shoreLoops, simplify, false, true,
                                                          atlas.raster, w, h, diagramW, diagramH,
                                                          catalog);
    atlas.outlinesByTerrainId[riverId] =
        buildContourGraphs(riverPolylines, simplify, false, true, atlas.raster, w, h, diagramW,
                           diagramH, catalog);

    if (atlas.majorityLandId != forestId) {
        atlas.outlinesByTerrainId[forestId] = buildLandOutlines(insideForest);
    }
    if (atlas.majorityLandId != hillsId && atlas.majorityLandId != mountainId) {
        atlas.outlinesByTerrainId[hillsId] = buildLandOutlines(insideHills);
    }
    if (atlas.majorityLandId != plainsId) {
        atlas.outlinesByTerrainId[plainsId] = buildLandOutlines(insidePlains);
    }

    if (config.terrain.corridorRoadsEnabled) {
        const float shoreRoadInset     = config.terrain.shoreRoadInset;
        const float riverRoadInset     = config.terrain.riverRoadInset;
        const float corridorEdgeSpacing = config.terrain.corridorEdgeSpacing;
        atlas.shoreRoadGraph = simplifyContourGraphs(
            buildWaterContourGraph(insideSea, w, h, diagramW, diagramH, shoreRoadInset,
                                   corridorEdgeSpacing, atlas.raster, catalog),
            0.5f);
        atlas.riverRoadGraph = simplifyContourGraphs(
            buildWaterContourGraph(insideRiver, w, h, diagramW, diagramH, riverRoadInset,
                                   corridorEdgeSpacing, atlas.raster, catalog),
            0.5f);
    }

    const int forbiddenComponents = countMaskComponents(insideForbidden, w, h);
    const int seaComponents       = countMaskComponents(insideSea, w, h);
    const int riverComponents     = countMaskComponents(insideRiver, w, h);
    const int forestComponents    = countMaskComponents(insideForest, w, h);
    const int hillsComponents     = countMaskComponents(insideHills, w, h);

    if (!atlas.overlayTexture.loadFromFile(imagePath.string())) {
        Logger::log("terrain", "bake warning: overlay texture reload failed");
        return atlas;
    }
    atlas.overlayTexture.setSmooth(false);

    buildTerrainDebugMeshes(atlas, config.world.pixelsPerUnit, contourWidth);

    atlas.valid = true;

    Logger::log("terrain",
                "bake ok: image=" + imagePath.filename().string() + " size=" + std::to_string(w)
                    + "x" + std::to_string(h) + " forbidden="
                    + std::to_string(forbiddenComponents) + " sea=" + std::to_string(seaComponents)
                    + " river=" + std::to_string(riverComponents) + " forest="
                    + std::to_string(forestComponents) + " hills="
                    + std::to_string(hillsComponents) + " edge_spacing=" + std::to_string(simplify)
                    + " water_inset=" + std::to_string(waterInset) + " shore_inset="
                    + std::to_string(shoreInset) + " river_inset="
                    + std::to_string(riverInset) + " forbidden_nodes="
                    + std::to_string(countContourVertices(atlas.forbiddenPolygons))
                    + " forbidden_edges="
                    + std::to_string(countContourEdges(atlas.forbiddenPolygons, false))
                    + " shore_nodes=" + std::to_string(countContourVertices(
                                          atlas.outlinesByTerrainId[seaId]))
                    + " shore_edges=" + std::to_string(countContourEdges(
                                          atlas.outlinesByTerrainId[seaId], false))
                    + " river_nodes=" + std::to_string(countContourVertices(
                                          atlas.outlinesByTerrainId[riverId]))
                    + " river_edges=" + std::to_string(countContourEdges(
                                          atlas.outlinesByTerrainId[riverId], false))
                    + " majority_land="
                    + std::string(terrainIdName(atlas.majorityLandId, catalog))
                    + " forest_nodes=" + std::to_string(countContourVertices(
                                          atlas.outlinesByTerrainId[forestId]))
                    + " hills_nodes=" + std::to_string(countContourVertices(
                                          atlas.outlinesByTerrainId[hillsId]))
                    + " plains_nodes=" + std::to_string(countContourVertices(
                                          atlas.outlinesByTerrainId[plainsId]))
                    + (config.terrain.corridorRoadsEnabled
                           ? " shore_road_inset=" + std::to_string(config.terrain.shoreRoadInset)
                                 + " river_road_inset="
                                 + std::to_string(config.terrain.riverRoadInset)
                                 + " corridor_edge_spacing="
                                 + std::to_string(config.terrain.corridorEdgeSpacing)
                                 + " shore_road_nodes="
                                 + std::to_string(countContourVertices(atlas.shoreRoadGraph))
                                 + " river_road_nodes="
                                 + std::to_string(countContourVertices(atlas.riverRoadGraph))
                           : std::string(" corridor_roads=off")));

    const int riverNodes =
        static_cast<int>(countContourVertices(atlas.outlinesByTerrainId[riverId]));
    if (riverComponents > 0 && riverNodes == 0) {
        Logger::log("terrain",
                    "bake warning: river_components=" + std::to_string(riverComponents)
                        + " but river_nodes=0 (check river_inset / simplify_tolerance)");
    }
    if (riverComponents == 0) {
        Logger::log("terrain",
                    "bake warning: river_components=0 (no classified river pixels in raster)");
    }

    return atlas;
}

std::vector<std::pair<float, float>> clipRoadSegmentToLand(const Vec2& a, const Vec2& b,
                                                           const TerrainAtlas& terrain) {
    std::vector<std::pair<float, float>> intervals;
    const Vec2                           delta = b - a;
    const float                          len   = delta.length();
    if (len < 1e-4f) {
        if (terrain.isBuildable(a)) {
            intervals.push_back({0.f, 1.f});
        }
        return intervals;
    }

    constexpr float kStep = 0.25f;
    float           tOpen = -1.f;
    for (float dist = 0.f; dist <= len + 1e-3f; dist += kStep) {
        const float t     = std::min(dist / len, 1.f);
        const Vec2  point = a + delta * t;
        const bool  land  = terrain.isBuildable(point);
        if (land && tOpen < 0.f) {
            tOpen = t;
        } else if (!land && tOpen >= 0.f) {
            intervals.push_back({tOpen, t});
            tOpen = -1.f;
        }
    }
    if (tOpen >= 0.f) {
        intervals.push_back({tOpen, 1.f});
    }
    return intervals;
}
