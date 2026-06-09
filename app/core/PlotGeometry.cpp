#include "PlotGeometry.h"

#include "DefCache.h"

#include <algorithm>
#include <limits>

bool pointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b, float eps = 0.08f) {
    const Vec2 ab  = b - a;
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

bool plotInsideCell(const Plot& plot, const Cell& cell) {
    if (cell.boundary.size() < 3) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!pointInPolygon(plot.corners[i], cell.boundary)) {
            return false;
        }
    }
    return true;
}

void buildRoadPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& inward, float setback,
                   float frontage, float depth, Plot& plot);

// Max depth for a plot with frontage setback; binary-search using the full rectangle test.
float maxPlotDepthInCell(const Vec2& roadStart, const Vec2& edgeDir, float frontage,
                         const Vec2& inward, float setback, const Cell& cell) {
    if (frontage < 1e-3f || setback < 0.f) {
        return 0.f;
    }

    const Vec2 front0 = roadStart + inward * setback;
    const Vec2 front1 = front0 + edgeDir * frontage;
    if (!pointInPolygon(front0, cell.boundary) || !pointInPolygon(front1, cell.boundary)) {
        return 0.f;
    }

    float lo = 0.f;
    float hi = 512.f;
    for (int i = 0; i < 18; ++i) {
        const float mid = (lo + hi) * 0.5f;
        Plot        probe{};
        buildRoadPlot(roadStart, edgeDir, inward, setback, frontage, mid, probe);
        if (plotInsideCell(probe, cell)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
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

bool plotsOverlap(const Plot& a, const Plot& b) {
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = a.corners[(i + 1) % 4] - a.corners[i];
        if (edge.length() < 1e-6f) {
            continue;
        }
        const Vec2 axis = perpendicular(edge.normalized());
        if (axisSeparates(a.corners, b.corners, axis)) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = b.corners[(i + 1) % 4] - b.corners[i];
        if (edge.length() < 1e-6f) {
            continue;
        }
        const Vec2 axis = perpendicular(edge.normalized());
        if (axisSeparates(a.corners, b.corners, axis)) {
            return false;
        }
    }
    return true;
}

bool footprintsOverlap(const BuildingFootprint& a, const BuildingFootprint& b);

bool overlapsInstances(const Plot& plot, const std::vector<BuildingInstance>& instances) {
    BuildingFootprint plotFootprint{};
    for (int i = 0; i < 4; ++i) {
        plotFootprint.corners[i] = plot.corners[i];
    }

    for (const BuildingInstance& existing : instances) {
        if (existing.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (existing.footprints.empty()) {
                continue;
            }
            if (footprintsOverlap(plotFootprint, existing.footprints[0])) {
                return true;
            }
            continue;
        }
        if (existing.plot.id < 0) {
            continue;
        }
        if (plotsOverlap(plot, existing.plot)) {
            return true;
        }
    }
    return false;
}

bool footprintsOverlap(const BuildingFootprint& a, const BuildingFootprint& b) {
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = a.corners[(i + 1) % 4] - a.corners[i];
        if (edge.length() < 1e-6f) {
            continue;
        }
        const Vec2 axis = perpendicular(edge.normalized());
        if (axisSeparates(a.corners, b.corners, axis)) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = b.corners[(i + 1) % 4] - b.corners[i];
        if (edge.length() < 1e-6f) {
            continue;
        }
        const Vec2 axis = perpendicular(edge.normalized());
        if (axisSeparates(a.corners, b.corners, axis)) {
            return false;
        }
    }
    return true;
}

bool footprintInsideCell(const BuildingFootprint& footprint, const Cell& cell) {
    if (cell.boundary.size() < 3) {
        return false;
    }
    for (const Vec2& corner : footprint.corners) {
        if (!pointInPolygon(corner, cell.boundary)) {
            return false;
        }
    }
    return true;
}

Vec2 instancePlacementPoint(const BuildingInstance& instance) {
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill
        && !instance.footprints.empty()) {
        Vec2 center{};
        for (const Vec2& corner : instance.footprints[0].corners) {
            center = center + corner;
        }
        return center * 0.25f;
    }
    return plotCenter(instance.plot);
}

bool footprintOverlapsMains(const BuildingFootprint& footprint, const Town& town,
                            const DefCache& defs) {
    Plot probe{};
    for (int i = 0; i < 4; ++i) {
        probe.corners[i] = footprint.corners[i];
    }

    for (const BuildingInstance& instance : town.buildingInstances) {
        if (instance.placementMode == BuildingPlacementMode::PlotLot && instance.plot.id >= 0) {
            const BuildingDef* def = defs.building(instance.buildingType);
            if (def != nullptr && !def->allowPlotFill && plotsOverlap(probe, instance.plot)) {
                return true;
            }
        }
        for (const BuildingFootprint& existing : instance.footprints) {
            if (!existing.mainBuilding) {
                continue;
            }
            if (footprintsOverlap(footprint, existing)) {
                return true;
            }
        }
    }
    return false;
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

bool instanceOnRoadSideBank(const BuildingInstance& instance, int roadId, int cellId,
                            int bankIndex) {
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
        if (instance.roadId != roadId || instance.cellId != cellId) {
            return false;
        }
        if (bankIndex >= 0 && instance.roadBank >= 0) {
            return instance.roadBank == bankIndex;
        }
        return true;
    }
    if (instance.plot.roadId != roadId || instance.plot.cellId != cellId) {
        return false;
    }
    if (bankIndex >= 0 && instance.plot.roadBank >= 0) {
        return instance.plot.roadBank == bankIndex;
    }
    return true;
}

void collectBuildingWallSpansOnSide(const Town& town, int roadId, int cellId, int bankIndex,
                                    const Vec2& origin, const Vec2& edgeDir,
                                    std::vector<RoadWallSpan>& out) {
    constexpr float kWallEps = 0.08f;
    out.clear();
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSideBank(instance, roadId, cellId, bankIndex)) {
            continue;
        }

        bool addedSpan = false;
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding) {
                continue;
            }
            RoadWallSpan span;
            span.tMin = footprintTMin(footprint, origin, edgeDir);
            span.tMax = footprintTMax(footprint, origin, edgeDir);
            if (span.tMax > span.tMin + 1e-3f) {
                out.push_back(span);
                addedSpan = true;
            }
        }

        if (!addedSpan && instance.placementMode == BuildingPlacementMode::PlotLot) {
            RoadWallSpan span;
            span.tMin = plotTMin(instance.plot, origin, edgeDir);
            span.tMax = plotTMax(instance.plot, origin, edgeDir);
            if (span.tMax > span.tMin + 1e-3f) {
                out.push_back(span);
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const RoadWallSpan& lhs, const RoadWallSpan& rhs) { return lhs.tMin < rhs.tMin; });

    std::vector<RoadWallSpan> merged;
    for (const RoadWallSpan& span : out) {
        if (merged.empty() || span.tMin > merged.back().tMax + kWallEps) {
            merged.push_back(span);
            continue;
        }
        merged.back().tMax = std::max(merged.back().tMax, span.tMax);
    }
    out.swap(merged);
}

void collectWallGapsOnSide(const Town& town, int roadId, int cellId, int bankIndex,
                           const Vec2& origin, const Vec2& edgeDir, float roadLen,
                           float minGapWidth, std::vector<RoadWallSpan>& outGaps) {
    (void)origin;
    (void)edgeDir;
    outGaps.clear();

    std::vector<RoadWallSpan> spans;
    collectBuildingWallSpansOnSide(town, roadId, cellId, bankIndex, origin, edgeDir, spans);

    float prevEnd = 0.f;
    for (const RoadWallSpan& span : spans) {
        if (span.tMin > prevEnd + minGapWidth - 1e-3f) {
            outGaps.push_back({prevEnd, span.tMin});
        }
        prevEnd = std::max(prevEnd, span.tMax);
    }
    if (roadLen > prevEnd + minGapWidth - 1e-3f) {
        outGaps.push_back({prevEnd, roadLen});
    }
}

namespace {

bool gapNearExistingSecondary(const Town& town, int cellId, const Vec2& gapPt, float eps = 5.f) {
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        if (rec.hostCellId != cellId) {
            continue;
        }
        if ((rec.a - gapPt).length() < eps) {
            return true;
        }
    }
    for (const Road& road : town.roads) {
        if (!road.isSecondary || road.hostCellId != cellId) {
            continue;
        }
        if ((road.a - gapPt).length() < eps) {
            return true;
        }
    }
    return false;
}

int secondaryCountInCell(const Town& town, int cellId) {
    int count = 0;
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        if (rec.hostCellId == cellId) {
            ++count;
        }
    }
    return count;
}

}  // namespace

bool wallGapNearExistingSecondary(const Town& town, int cellId, const Vec2& gapPt, float eps) {
    return gapNearExistingSecondary(town, cellId, gapPt, eps);
}

int secondaryRoadCountInCell(const Town& town, int cellId) {
    return secondaryCountInCell(town, cellId);
}

void collectAllPrimaryWallGaps(const Town& town, float minGapWidth, std::vector<WallGap>& out) {
    out.clear();
    int nextId = 0;

    const auto collectRoadGaps = [&](const Road& road) {
        const float roadLen = road.length();
        if (roadLen < 1e-3f) {
            return;
        }

        if (road.isSameCellSecondary()) {
            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForCell(road, road.hostCellId, origin, farEnd, edgeDir)) {
                return;
            }
            for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
                const RoadSideFrontage* side = road.sideBank(bankIndex);
                if (side->cellId != road.hostCellId || side->inward.length() < 1e-4f) {
                    continue;
                }
                std::vector<RoadWallSpan> spans;
                collectWallGapsOnSide(town, road.id, road.hostCellId, bankIndex, origin, edgeDir,
                                      roadLen, minGapWidth, spans);
                for (const RoadWallSpan& span : spans) {
                    WallGap gap;
                    gap.id        = nextId++;
                    gap.roadId    = road.id;
                    gap.cellId    = road.hostCellId;
                    gap.bankIndex = bankIndex;
                    gap.tMin      = span.tMin;
                    gap.tMax      = span.tMax;
                    gap.origin    = origin;
                    gap.edgeDir   = edgeDir;
                    gap.inward    = side->inward;
                    out.push_back(gap);
                }
            }
            return;
        }

        for (const RoadSideFrontage* side : {&road.sideA, &road.sideB}) {
            if (side->cellId < 0 || side->inward.length() < 1e-4f) {
                continue;
            }

            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForCell(road, side->cellId, origin, farEnd, edgeDir)) {
                continue;
            }

            const int bankIndex = (side == &road.sideB) ? 1 : 0;
            std::vector<RoadWallSpan> spans;
            collectWallGapsOnSide(town, road.id, side->cellId, bankIndex, origin, edgeDir, roadLen,
                                  minGapWidth, spans);

            for (const RoadWallSpan& span : spans) {
                WallGap gap;
                gap.id        = nextId++;
                gap.roadId    = road.id;
                gap.cellId    = side->cellId;
                gap.bankIndex = bankIndex;
                gap.tMin      = span.tMin;
                gap.tMax      = span.tMax;
                gap.origin    = origin;
                gap.edgeDir   = edgeDir;
                gap.inward    = side->inward;
                out.push_back(gap);
            }
        }
    };

    for (const Road& road : town.roads) {
        if (road.isSecondary) {
            continue;
        }
        collectRoadGaps(road);
    }

    for (const Road& road : town.roads) {
        if (!road.isSecondary) {
            continue;
        }
        collectRoadGaps(road);
    }
}

WallGapKey wallGapKey(const WallGap& gap) {
    WallGapKey key;
    key.roadId    = gap.roadId;
    key.cellId    = gap.cellId;
    key.bankIndex = gap.bankIndex;
    key.tMin      = gap.tMin;
    key.tMax      = gap.tMax;
    return key;
}

bool isAlleyGapChecked(const Town& town, const WallGap& gap) {
    return town.checkedAlleyGaps.find(wallGapKey(gap)) != town.checkedAlleyGaps.end();
}

void markAlleyGapChecked(Town& town, const WallGap& gap) {
    town.checkedAlleyGaps.insert(wallGapKey(gap));
}

bool cellHasUncheckedAlleyGaps(const Town& town, int cellId,
                               const std::vector<WallGap>& allGaps) {
    for (const WallGap& gap : allGaps) {
        if (gap.cellId == cellId && !isAlleyGapChecked(town, gap)) {
            return true;
        }
    }
    return false;
}

bool isSecondaryHostGap(const Town& town, const WallGap& gap) {
    if (gap.roadId < 0 || gap.roadId >= static_cast<int>(town.roads.size())) {
        return false;
    }
    return town.roads[static_cast<std::size_t>(gap.roadId)].isSecondary;
}

bool cellHasUncheckedPrimaryAlleyGaps(const Town& town, int cellId,
                                      const std::vector<WallGap>& allGaps) {
    for (const WallGap& gap : allGaps) {
        if (gap.cellId == cellId && !isAlleyGapChecked(town, gap)
            && !isSecondaryHostGap(town, gap)) {
            return true;
        }
    }
    return false;
}

bool cellHasUncheckedSecondaryHostAlleyGaps(const Town& town, int cellId,
                                            const std::vector<WallGap>& allGaps) {
    for (const WallGap& gap : allGaps) {
        if (gap.cellId == cellId && !isAlleyGapChecked(town, gap)
            && isSecondaryHostGap(town, gap)) {
            return true;
        }
    }
    return false;
}

float footprintTMin(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir) {
    float tMin = std::numeric_limits<float>::max();
    for (const Vec2& corner : footprint.corners) {
        tMin = std::min(tMin, (corner - origin).dot(edgeDir));
    }
    return tMin;
}

float footprintTMax(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir) {
    float tMax = -std::numeric_limits<float>::max();
    for (const Vec2& corner : footprint.corners) {
        tMax = std::max(tMax, (corner - origin).dot(edgeDir));
    }
    return tMax;
}

bool roadFrameForCell(const Road& road, int cellId, Vec2& origin, Vec2& farEnd, Vec2& edgeDir) {
    if (road.cellA == cellId) {
        origin = road.a;
        farEnd = road.b;
    } else if (road.cellB == cellId) {
        origin = road.b;
        farEnd = road.a;
    } else {
        return false;
    }

    edgeDir = (farEnd - origin).normalized();
    return edgeDir.length() >= 1e-4f;
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

double polygonSignedArea(const std::vector<Vec2>& polygon) {
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& p0 = polygon[i];
        const Vec2& p1 = polygon[(i + 1) % polygon.size()];
        area += static_cast<double>(p0.x) * p1.y - static_cast<double>(p1.x) * p0.y;
    }
    return area * 0.5;
}

bool setbackInside(const Vec2& roadPoint, const Vec2& dir, float setback,
                   const std::vector<Vec2>& boundary) {
    return pointInPolygon(roadPoint + dir * setback, boundary);
}

bool inwardFromBoundaryEdge(const Vec2& roadPoint, const Vec2& edgeDir, const Cell& cell,
                            Vec2& inward) {
    if (cell.boundary.size() < 3) {
        return false;
    }

    constexpr float kOnEdgeEps = 0.35f;
    constexpr float kAlignDot  = 0.75f;

    const std::size_t n         = cell.boundary.size();
    const bool        ccw       = polygonSignedArea(cell.boundary) >= 0.0;
    int               bestEdge  = -1;
    float             bestDist  = kOnEdgeEps;

    for (std::size_t i = 0; i < n; ++i) {
        const Vec2& p0    = cell.boundary[i];
        const Vec2& p1    = cell.boundary[(i + 1) % n];
        const float dist  = distanceToSegment(roadPoint, p0, p1);
        if (dist > bestDist) {
            continue;
        }

        Vec2 segDir = (p1 - p0).normalized();
        if (segDir.length() < 1e-4f) {
            continue;
        }
        const float align = std::abs(edgeDir.normalized().dot(segDir));
        if (align < kAlignDot) {
            continue;
        }

        bestDist  = dist;
        bestEdge  = static_cast<int>(i);
    }

    if (bestEdge < 0) {
        return false;
    }

    const Vec2& p0    = cell.boundary[static_cast<std::size_t>(bestEdge)];
    const Vec2& p1    = cell.boundary[static_cast<std::size_t>((bestEdge + 1) % n)];
    Vec2        eDir  = (p1 - p0).normalized();
    if (edgeDir.normalized().dot(eDir) < 0.f) {
        eDir = eDir * -1.f;
    }

    const Vec2 left = perpendicular(eDir);
    inward          = ccw ? left : left * -1.f;

    constexpr float kProbe = 0.15f;
    if (!pointInPolygon(roadPoint + inward * kProbe, cell.boundary)) {
        inward = inward * -1.f;
    }
    return true;
}

Vec2 inwardTowardSite(const Vec2& roadPoint, const Vec2& edgeDir, const Vec2& site) {
    const Vec2 edgeNorm = edgeDir.normalized();
    const Vec2 left     = perpendicular(edgeNorm);
    const Vec2 right    = left * -1.f;
    const Vec2 toSite   = site - roadPoint;
    return (left.dot(toSite) >= right.dot(toSite)) ? left : right;
}

bool inwardAtRoadPoint(const Vec2& roadPoint, const Vec2& edgeDir, const Cell& cell, float setback,
                       Vec2& inward) {
    if (cell.boundary.size() < 3 || edgeDir.length() < 1e-4f) {
        return false;
    }

    const Vec2 edgeNorm = edgeDir.normalized();
    const Vec2 left     = perpendicular(edgeNorm);
    const Vec2 right    = left * -1.f;
    const Vec2 toSite   = cell.site - roadPoint;

    const bool leftIn  = setbackInside(roadPoint, left, setback, cell.boundary);
    const bool rightIn = setbackInside(roadPoint, right, setback, cell.boundary);

    if (leftIn && !rightIn) {
        inward = left;
        return true;
    }
    if (rightIn && !leftIn) {
        inward = right;
        return true;
    }
    if (leftIn && rightIn) {
        inward = (left.dot(toSite) >= right.dot(toSite)) ? left : right;
        return true;
    }

    if (inwardFromBoundaryEdge(roadPoint, edgeNorm, cell, inward)) {
        if (setbackInside(roadPoint, inward, setback, cell.boundary)) {
            return true;
        }
        const Vec2 flipped = inward * -1.f;
        if (setbackInside(roadPoint, flipped, setback, cell.boundary)) {
            inward = flipped;
            return true;
        }
    }

    inward = inwardTowardSite(roadPoint, edgeNorm, cell.site);
    if (setbackInside(roadPoint, inward, setback, cell.boundary)) {
        return true;
    }
    const Vec2 flipped = inward * -1.f;
    if (setbackInside(roadPoint, flipped, setback, cell.boundary)) {
        inward = flipped;
        return true;
    }

    inward = flipped;
    return false;
}

bool orientRoadForCell(const Road& road, int cellId, Vec2& a, Vec2& b, Vec2& edgeDir,
                       Vec2& inward, const Cell& cell, float /*setback*/) {
    if (!roadFrameForCell(road, cellId, a, b, edgeDir)) {
        return false;
    }

    if (const RoadSideFrontage* side = road.sideForCell(cellId)) {
        if (side->inward.length() > 1e-4f) {
            inward = side->inward;
            return true;
        }
    }

    const Vec2 mid  = (road.a + road.b) * 0.5f;
    const Vec2 left = perpendicular(edgeDir);
    inward          = (left.dot(cell.site - mid) >= 0.f) ? left : left * -1.f;
    return true;
}

void buildRoadPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& inward, float setback,
                   float frontage, float depth, Plot& plot) {
    plot.corners[0] = roadStart + inward * setback;
    plot.corners[1] = plot.corners[0] + edgeDir * frontage;
    plot.corners[2] = plot.corners[1] + inward * depth;
    plot.corners[3] = plot.corners[0] + inward * depth;
    plot.frontage   = frontage;
    plot.depth      = depth;
    plot.area       = frontage * depth;
}

Vec2 plotCenter(const Plot& plot) {
    return {(plot.corners[0].x + plot.corners[1].x + plot.corners[2].x + plot.corners[3].x) * 0.25f,
            (plot.corners[0].y + plot.corners[1].y + plot.corners[2].y + plot.corners[3].y) * 0.25f};
}

namespace {

float cross2D(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

bool pointInMainFootprint(const Vec2& p, const BuildingFootprint& fp) {
    return pointInPolygon(p, std::vector<Vec2>{fp.corners[0], fp.corners[1], fp.corners[2],
                                               fp.corners[3]});
}

}  // namespace

bool segmentCrossingParams(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float& outT, float& outU) {
    const Vec2  r     = a1 - a0;
    const Vec2  s     = b1 - b0;
    const float denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-8f) {
        return false;
    }
    const Vec2 qp = b0 - a0;
    outT          = (qp.x * s.y - qp.y * s.x) / denom;
    outU          = (qp.x * r.y - qp.y * r.x) / denom;
    return outT >= -1e-4f && outT <= 1.f + 1e-4f && outU >= -1e-4f && outU <= 1.f + 1e-4f;
}

bool segmentsCrossInInterior(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float interiorEps) {
    float t = 0.f;
    float u = 0.f;
    if (!segmentCrossingParams(a0, a1, b0, b1, t, u)) {
        return false;
    }
    return t > interiorEps && t < 1.f - interiorEps && u > interiorEps && u < 1.f - interiorEps;
}

bool segmentParallelOverlapMetrics(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                                   float minParallelCos, float& outMinSep, float& outOverlapLen) {
    const Vec2  dirA  = a1 - a0;
    const float lenA  = dirA.length();
    const Vec2  dirB  = b1 - b0;
    const float lenB  = dirB.length();
    if (lenA < 1e-4f || lenB < 1e-4f) {
        return false;
    }

    const Vec2 unitA = dirA * (1.f / lenA);
    const Vec2 unitB = dirB * (1.f / lenB);
    if (std::abs(unitA.dot(unitB)) < minParallelCos) {
        return false;
    }

    const auto projectT = [&](const Vec2& p) -> float { return (p - a0).dot(unitA); };
    const float tb0     = projectT(b0);
    const float tb1     = projectT(b1);
    const float bMin    = std::min(tb0, tb1);
    const float bMax    = std::max(tb0, tb1);
    const float overlapStart = std::max(0.f, bMin);
    const float overlapEnd   = std::min(lenA, bMax);
    outOverlapLen            = std::max(0.f, overlapEnd - overlapStart);
    if (outOverlapLen <= 1e-4f) {
        return false;
    }

    const Vec2 perpA{-unitA.y, unitA.x};
    outMinSep = std::abs((b0 - a0).dot(perpA));
    return true;
}

bool segmentNearParallelRoad(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float minParallelCos, float maxSep, float minOverlapFrac) {
    float minSep      = 0.f;
    float overlapLen  = 0.f;
    if (!segmentParallelOverlapMetrics(a0, a1, b0, b1, minParallelCos, minSep, overlapLen)) {
        return false;
    }

    const float minLen = std::min((a1 - a0).length(), (b1 - b0).length());
    if (minLen < 1e-4f) {
        return false;
    }
    return minSep <= maxSep && overlapLen / minLen >= minOverlapFrac;
}

bool segmentsIntersect2D(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1) {
    const Vec2 r = a1 - a0;
    const Vec2 s = b1 - b0;
    const float denom = cross2D(r, s);
    const Vec2  qp    = b0 - a0;
    if (std::abs(denom) < 1e-8f) {
        return false;
    }
    const float t = cross2D(qp, s) / denom;
    const float u = cross2D(qp, r) / denom;
    return t >= -1e-4f && t <= 1.f + 1e-4f && u >= -1e-4f && u <= 1.f + 1e-4f;
}

float raySegmentHitDist(const Vec2& origin, const Vec2& dir, const Vec2& segA, const Vec2& segB,
                        float maxDist) {
    const Vec2  r     = dir * maxDist;
    const Vec2  s     = segB - segA;
    const float denom = cross2D(r, s);
    if (std::abs(denom) < 1e-8f) {
        return -1.f;
    }
    const Vec2  qp    = segA - origin;
    const float t     = cross2D(qp, s) / denom;
    const float u     = cross2D(qp, r) / denom;
    if (t < 1e-4f || t > 1.f + 1e-4f || u < -1e-4f || u > 1.f + 1e-4f) {
        return -1.f;
    }
    return t * maxDist;
}

namespace {

bool segmentIntersectsFootprint(const Vec2& a, const Vec2& b, const BuildingFootprint& fp) {
    for (int i = 0; i < 4; ++i) {
        const Vec2& c0 = fp.corners[i];
        const Vec2& c1 = fp.corners[(i + 1) % 4];
        if (segmentsIntersect2D(a, b, c0, c1)) {
            return true;
        }
    }
    const Vec2 mid = (a + b) * 0.5f;
    if (pointInMainFootprint(mid, fp)) {
        return true;
    }
    return false;
}

float distancePointToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.dot(ab);
    if (lenSq < 1e-8f) {
        return (p - a).length();
    }
    const float t = std::clamp((p - a).dot(ab) / lenSq, 0.f, 1.f);
    const Vec2  closest = a + ab * t;
    return (p - closest).length();
}

float segmentSegmentMinDistance(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1) {
    if (segmentsIntersect2D(a0, a1, b0, b1)) {
        return 0.f;
    }
    return std::min(
        {distancePointToSegment(a0, b0, b1), distancePointToSegment(a1, b0, b1),
         distancePointToSegment(b0, a0, a1), distancePointToSegment(b1, a0, a1)});
}

bool alleyCorridorHitsFootprint(const Vec2& a, const Vec2& b, float halfWidth,
                                const BuildingFootprint& fp) {
    if (segmentIntersectsFootprint(a, b, fp)) {
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        if (distancePointToSegment(fp.corners[i], a, b) <= halfWidth + 1e-3f) {
            return true;
        }
        const Vec2& c0 = fp.corners[i];
        const Vec2& c1 = fp.corners[(i + 1) % 4];
        if (segmentSegmentMinDistance(a, b, c0, c1) <= halfWidth + 1e-3f) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool segmentIntersectsMainFootprints(const Vec2& a, const Vec2& b, const Town& town) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding) {
                continue;
            }
            if (segmentIntersectsFootprint(a, b, footprint)) {
                return true;
            }
        }
    }
    return false;
}

bool segmentIntersectsSecondaryFootprints(const Vec2& a, const Vec2& b, const Town& town) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (footprint.mainBuilding) {
                continue;
            }
            if (segmentIntersectsFootprint(a, b, footprint)) {
                return true;
            }
        }
    }
    return false;
}

bool alleySegmentBlocked(const Vec2& a, const Vec2& b, float roadHalfWidth, const Town& town,
                         const DefCache& defs) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (alleyCorridorHitsFootprint(a, b, roadHalfWidth, footprint)) {
                return true;
            }
        }

        if (instance.placementMode != BuildingPlacementMode::PlotLot || instance.plot.id < 0) {
            continue;
        }
        const BuildingDef* def = defs.building(instance.buildingType);
        if (def == nullptr || def->allowPlotFill) {
            continue;
        }

        BuildingFootprint plotFootprint{};
        for (int i = 0; i < 4; ++i) {
            plotFootprint.corners[i] = instance.plot.corners[i];
        }
        if (alleyCorridorHitsFootprint(a, b, roadHalfWidth, plotFootprint)) {
            return true;
        }
    }
    return false;
}

bool footprintOverlapsAlleys(const BuildingFootprint& footprint, const Town& town, float setback,
                             int excludeRoadId) {
    for (const Road& road : town.roads) {
        if (!road.isSecondary || road.id == excludeRoadId) {
            continue;
        }
        if (alleyCorridorHitsFootprint(road.a, road.b, setback, footprint)) {
            return true;
        }
    }
    return false;
}

bool plotOverlapsAlleys(const Plot& plot, const Town& town, float setback, int excludeRoadId) {
    BuildingFootprint footprint{};
    for (int i = 0; i < 4; ++i) {
        footprint.corners[i] = plot.corners[i];
    }
    return footprintOverlapsAlleys(footprint, town, setback, excludeRoadId);
}

bool segmentClearsRoadSetback(const Vec2& a, const Vec2& b, const Town& town, int hostCellId,
                              float setback, int excludeRoadId) {
    if (hostCellId < 0 || hostCellId >= static_cast<int>(town.cells.size())) {
        return false;
    }
    const Cell& cell = town.cells[static_cast<std::size_t>(hostCellId)];
    const Vec2  ab   = b - a;
    const float len  = ab.length();
    if (len < 1e-3f) {
        return false;
    }
    const Vec2  edgeDir = ab * (1.f / len);
    const Vec2  left    = perpendicular(edgeDir);
    const float samples[] = {0.f, len * 0.5f, len};
    for (const float t : samples) {
        const Vec2 center = a + edgeDir * t;
        for (const int sign : {1, -1}) {
            const Vec2 inward = (left * static_cast<float>(sign)).normalized();
            if (!validSetbackProbe(center, inward, setback, cell, town.roads, excludeRoadId)) {
                return false;
            }
        }
    }
    return true;
}

