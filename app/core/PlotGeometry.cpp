#include "PlotGeometry.h"

#include "FrontageZones.h"
#include "PlacementFrontier.h"
#include "RoadExhaustion.h"

#include "DefCache.h"
#include "TerrainAtlas.h"
#include "TerrainPlacement.h"
#include "TownConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace {

constexpr float kDepthCacheTStep   = 0.05f;
constexpr float kDepthCacheDimStep = 0.1f;

float quantizeDepthCacheT(float value) {
    return std::round(value / kDepthCacheTStep) * kDepthCacheTStep;
}

float quantizeDepthCacheDim(float value) {
    return std::round(value / kDepthCacheDimStep) * kDepthCacheDimStep;
}

bool edgeHasNonBuildableSample(const Vec2& a, const Vec2& b, const TerrainAtlas& terrain,
                               float step) {
    const Vec2  delta = b - a;
    const float len   = delta.length();
    if (len < 1e-4f) {
        return !terrain.isBuildable(a);
    }
    for (float dist = step * 0.5f; dist < len - step * 0.5f; dist += step) {
        if (!terrain.isBuildable(a + delta * (dist / len))) {
            return true;
        }
    }
    return false;
}

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

bool polygonBuildableImpl(const Vec2 corners[4], const TerrainAtlas& terrain, float edgeStep) {
    if (!terrain.valid) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!terrain.isBuildable(corners[i])) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (edgeHasNonBuildableSample(corners[i], corners[(i + 1) % 4], terrain, edgeStep)) {
            return false;
        }
    }
    return true;
}

bool cornersValid(const Vec2 corners[4], const Town& town, const TerrainAtlas* terrain) {
    for (int i = 0; i < 4; ++i) {
        if (!pointInsideTownDisc(town, corners[i])) {
            return false;
        }
    }
    if (terrain != nullptr && terrain->valid && !polygonBuildableImpl(corners, *terrain, 0.5f)) {
        return false;
    }
    return true;
}

BuildingFootprint plotAsFootprint(const Plot& plot) {
    BuildingFootprint fp{};
    for (int i = 0; i < 4; ++i) {
        fp.corners[i] = plot.corners[i];
    }
    return fp;
}

bool instanceOnRoadBank(const BuildingInstance& instance, int roadId, int bankIndex) {
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
        return instance.roadId == roadId && (bankIndex < 0 || instance.roadBank == bankIndex);
    }
    return instance.plot.roadId == roadId && (bankIndex < 0 || instance.plot.roadBank == bankIndex);
}

}  // namespace

float distancePointToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2  ab    = b - a;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-8f) {
        return (p - a).length();
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f);
    return (p - (a + ab * t)).length();
}

bool bankHasBuildingOnSide(const Town& town, int roadId, int bankIndex) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (instance.roadId == roadId
                && (bankIndex < 0 || instance.roadBank == bankIndex)) {
                return true;
            }
        } else if (instance.plot.roadId == roadId
                   && (bankIndex < 0 || instance.plot.roadBank == bankIndex)) {
            return true;
        }
    }
    return false;
}

bool polygonBuildable(const Vec2 corners[4], const TerrainAtlas& terrain, float edgeStep) {
    return polygonBuildableImpl(corners, terrain, edgeStep);
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

void buildRoadPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& inward, float setback,
                   float frontage, float depth, Plot& plot) {
    plot.frontage = frontage;
    plot.depth    = depth;
    plot.area     = frontage * depth;
    plot.corners[0] = roadStart + inward * setback;
    plot.corners[1] = plot.corners[0] + edgeDir * frontage;
    plot.corners[2] = plot.corners[1] + inward * depth;
    plot.corners[3] = plot.corners[0] + inward * depth;
    plot.outlineTangent = {};
    plot.outlineInward  = {};
}

float quadAreaFromCorners(const Vec2 corners[4]) {
    const Vec2 ab = corners[1] - corners[0];
    const Vec2 ac = corners[2] - corners[0];
    const Vec2 ad = corners[3] - corners[0];
    return 0.5f * (std::abs(ab.x * ac.y - ab.y * ac.x) + std::abs(ab.x * ad.y - ab.y * ad.x));
}

bool buildBorderHugPlot(const Vec2& roadStart, const Vec2& edgeDir, const Vec2& bankInward,
                        const Vec2& plotInward, float frontageSetback, float frontage,
                        TerrainId prefer, const TerrainAtlas& terrain, const SizeBand& plotBand,
                        Plot& plot) {
    if (!terrain.valid || frontage < 1e-3f) {
        return false;
    }
    const Vec2 edge     = edgeDir.normalized();
    const Vec2 bankIn   = bankInward.normalized();
    const Vec2 plotIn   = plotInward.normalized();
    plot.corners[0]     = roadStart + bankIn * frontageSetback;
    plot.corners[1]     = plot.corners[0] + edge * frontage;

    if (!projectToPreferOutline(plot.corners[0], plotIn, prefer, terrain, 250.f, plot.corners[3])) {
        return false;
    }
    if (!projectToPreferOutline(plot.corners[1], plotIn, prefer, terrain, 250.f, plot.corners[2])) {
        return false;
    }

    plot.frontage = frontage;
    const float depthA = (plot.corners[3] - plot.corners[0]).dot(plotIn);
    const float depthB = (plot.corners[2] - plot.corners[1]).dot(plotIn);
    plot.depth         = (depthA + depthB) * 0.5f;
    plot.area          = quadAreaFromCorners(plot.corners);
    if (plot.area + 1e-3f < plotBand.minArea || plot.area > plotBand.maxArea + 1e-3f) {
        return false;
    }

    const Vec2 backMid  = (plot.corners[2] + plot.corners[3]) * 0.5f;
    const Vec2 frontMid = (plot.corners[0] + plot.corners[1]) * 0.5f;
    OutlineSnap snap{};
    if (snapToPreferOutline(backMid, prefer, terrain, frontMid, snap)) {
        plot.outlineTangent = snap.tangent;
        plot.outlineInward  = snap.featureInward;
    } else {
        plot.outlineTangent = (plot.corners[2] - plot.corners[3]).normalized();
        plot.outlineInward  = plotIn;
    }
    return plot.depth > frontageSetback + 0.5f;
}

bool polygonBuildableHugPlot(const Vec2 corners[4], const TerrainAtlas& terrain,
                             TerrainId prefer) {
    if (!terrain.valid) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (!terrain.isBuildable(corners[i])) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        if (i < 2 || i == 3) {
            if (edgeHasNonBuildableSample(corners[i], corners[j], terrain, 0.5f)) {
                return false;
            }
        }
    }
    for (int i = 2; i < 4; ++i) {
        if (distToPreferEdge(corners[i], prefer, terrain) > kBorderHugEdgeEpsilon + 1e-3f
            && !terrain.isBuildable(corners[i])) {
            return false;
        }
    }
    return true;
}

bool isHugTrapezoidPlot(const Plot& plot) {
    return plot.outlineTangent.length() > 1e-4f;
}

void hugPlotLayoutFrame(const Plot& plot, Vec2& axisU, Vec2& axisV) {
    axisU = plot.outlineTangent.normalized();
    axisV = plot.outlineInward.normalized();
    if (axisV.length() < 1e-4f) {
        axisV = (plot.corners[0] - plot.corners[3]).normalized();
    }
}

bool footprintHasRightAngles(const BuildingFootprint& footprint) {
    constexpr float kMaxCos = 0.05f;
    for (int i = 0; i < 4; ++i) {
        const Vec2 e0 = footprint.corners[(i + 1) % 4] - footprint.corners[i];
        const Vec2 e1 = footprint.corners[(i + 2) % 4] - footprint.corners[(i + 1) % 4];
        if (e0.length() < 1e-4f || e1.length() < 1e-4f) {
            return false;
        }
        if (std::abs(e0.normalized().dot(e1.normalized())) > kMaxCos) {
            return false;
        }
    }
    return true;
}

namespace {

void projectPlotOnAxes(const Plot& plot, const Vec2& axisU, const Vec2& axisV, float& minU,
                       float& maxU, float& minV, float& maxV) {
    minU = maxU = plot.corners[0].dot(axisU);
    minV = maxV = plot.corners[0].dot(axisV);
    for (int i = 1; i < 4; ++i) {
        const float u = plot.corners[i].dot(axisU);
        const float v = plot.corners[i].dot(axisV);
        minU          = std::min(minU, u);
        maxU          = std::max(maxU, u);
        minV          = std::min(minV, v);
        maxV          = std::max(maxV, v);
    }
}

}  // namespace

bool outlineRectFitsInPlot(const Plot& plot, float minFront, float minDepth) {
    if (isHugTrapezoidPlot(plot)) {
        Vec2 axisU{};
        Vec2 axisV{};
        hugPlotLayoutFrame(plot, axisU, axisV);
        float minU = 0.f;
        float maxU = 0.f;
        float minV = 0.f;
        float maxV = 0.f;
        projectPlotOnAxes(plot, axisU, axisV, minU, maxU, minV, maxV);
        return (maxU - minU) + 1e-3f >= minFront && (maxV - minV) + 1e-3f >= minDepth;
    }
    return plot.frontage + 1e-3f >= minFront && plot.depth + 1e-3f >= minDepth;
}

void extendMainFootprintOverhang(std::vector<BuildingFootprint>& footprints, const Plot& plot,
                                 const Vec2& featureInward, float maxOverhang) {
    if (maxOverhang < 1e-3f) {
        return;
    }
    const Vec2 push = featureInward.normalized() * maxOverhang;
    for (BuildingFootprint& footprint : footprints) {
        if (!footprint.mainBuilding) {
            continue;
        }
        footprint.corners[2] = footprint.corners[2] + push;
        footprint.corners[3] = footprint.corners[3] + push;
    }
}

bool footprintPlacementValidWithOverhang(const BuildingFootprint& footprint, const Plot& plot,
                                       const Town& town, const TerrainAtlas* terrain, float setback,
                                       int hostRoadId, TerrainId prefer, float maxOverhang) {
    if (!footprintHasRightAngles(footprint)) {
        return false;
    }
    if (!cornersValid(footprint.corners, town, terrain)) {
        return false;
    }
    if (polygonEdgesCrossRoads(footprint.corners, town, hostRoadId)) {
        return false;
    }
    if (footprintOverlapsAlleys(footprint, town, setback, hostRoadId)) {
        return false;
    }
    if (terrain == nullptr || !terrain->valid) {
        return true;
    }
    const Vec2 backMid = (plot.corners[2] + plot.corners[3]) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        if (terrain->isBuildable(footprint.corners[i])) {
            continue;
        }
        const float edgeDist = distToPreferEdge(footprint.corners[i], prefer, *terrain);
        if (edgeDist > kBorderHugEdgeEpsilon + maxOverhang + 1e-3f) {
            return false;
        }
        if ((footprint.corners[i] - backMid).length() > maxOverhang + plot.depth * 0.6f) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const Vec2 mid = (footprint.corners[i] + footprint.corners[j]) * 0.5f;
        if (terrain->isBuildable(mid)) {
            continue;
        }
        if (distToPreferEdge(mid, prefer, *terrain) > kBorderHugEdgeEpsilon + maxOverhang + 1e-3f) {
            return false;
        }
    }
    return true;
}

bool footprintPlacementValidBorderHug(const BuildingFootprint& footprint, const Plot& plot,
                                      const Town& town, const TerrainAtlas& terrain, float setback,
                                      int hostRoadId, TerrainId prefer, float maxOverhang) {
    if (!footprintHasRightAngles(footprint)) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!pointInsideTownDisc(town, footprint.corners[i])) {
            return false;
        }
    }
    if (polygonEdgesCrossRoads(footprint.corners, town, hostRoadId)) {
        return false;
    }
    if (footprintOverlapsAlleys(footprint, town, setback, hostRoadId)) {
        return false;
    }
    if (!terrain.valid) {
        return true;
    }
    if (!polygonBuildableHugPlot(footprint.corners, terrain, prefer)) {
        return false;
    }
    const Vec2 backMid = (plot.corners[2] + plot.corners[3]) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        if (terrain.isBuildable(footprint.corners[i])) {
            continue;
        }
        const float edgeDist = distToPreferEdge(footprint.corners[i], prefer, terrain);
        if (edgeDist > kBorderHugEdgeEpsilon + maxOverhang + 1e-3f) {
            return false;
        }
        if ((footprint.corners[i] - backMid).length() > maxOverhang + plot.depth * 0.6f) {
            return false;
        }
    }
    return true;
}

Vec2 plotCenter(const Plot& plot) {
    return (plot.corners[0] + plot.corners[1] + plot.corners[2] + plot.corners[3]) * 0.25f;
}

void invalidateRoadTopologyCaches(Town& town) {
    ++town.roadTopologyGeneration;
    for (Road& road : town.roads) {
        road.sideA.depthCacheEntries.clear();
        road.sideA.depthCacheTopologyGen = 0;
        road.sideA.mainOccupancyT.clear();
        road.sideB.depthCacheEntries.clear();
        road.sideB.depthCacheTopologyGen = 0;
        road.sideB.mainOccupancyT.clear();
    }
}

WallGap wallGapFromSegment(const Road& road, int bankIndex, const RoadFrontageSegment& segment) {
    WallGap gap;
    gap.id        = segment.id;
    gap.roadId    = road.id;
    gap.bankIndex = bankIndex;
    gap.tMin      = segment.startT;
    gap.tMax      = segment.endT;
    Vec2 farEnd{};
    roadFrameForBank(road, bankIndex, gap.origin, farEnd, gap.edgeDir);
    gap.inward = road.sideBank(bankIndex)->inward;
    return gap;
}

namespace {

constexpr float kDepthRayMax = 512.f;
constexpr float kDepthHitEps = 0.25f;

float nearestOutlineHitAtSample(const Vec2& sample, const Vec2& inward, const Town& town) {
    const TerrainAtlas* terrain = town.syncTerrainAtlas;
    if (terrain == nullptr || !terrain->valid || inward.length() < 1e-4f) {
        return std::numeric_limits<float>::max();
    }

    float best = std::numeric_limits<float>::max();
    for (TerrainId kind : town.syncTerrainProbes.borderIds) {
        if (!terrain->hasOutline(kind)) {
            continue;
        }
        OutlineRayHit hit{};
        if (!rayHitPreferOutline(sample, inward, kind, *terrain, kDepthRayMax, hit)) {
            continue;
        }
        if (hit.dist > kDepthHitEps) {
            best = std::min(best, hit.dist);
        }
    }
    return best;
}

float maxPlotDepthToRoadHitImpl(const Vec2& roadStart, const Vec2& edgeDir, float frontage,
                                const Vec2& inward, float setback, int hostRoadId,
                                const Town& town) {
    if (frontage < 1e-3f || inward.length() < 1e-4f) {
        return 0.f;
    }

    float roadBest    = std::numeric_limits<float>::max();
    float outlineBest = std::numeric_limits<float>::max();
    const Vec2 samples[] = {
        roadStart + inward * setback,
        roadStart + edgeDir * (frontage * 0.5f) + inward * setback,
        roadStart + edgeDir * frontage + inward * setback,
    };

    const Vec2 unitInward = inward.normalized();
    for (const Vec2& sample : samples) {
        for (const Road& road : town.roads) {
            if (road.id == hostRoadId) {
                continue;
            }
            const float hit =
                raySegmentHitDist(sample, unitInward, road.a, road.b, kDepthRayMax);
            if (hit > kDepthHitEps) {
                roadBest = std::min(roadBest, hit);
            }
        }
        outlineBest = std::min(outlineBest, nearestOutlineHitAtSample(sample, inward, town));
    }

    float cap = 0.f;
    if (roadBest < std::numeric_limits<float>::max()) {
        cap = roadBest * 0.5f;
    }
    if (outlineBest < std::numeric_limits<float>::max()) {
        const float outlineCap = outlineBest;
        cap = cap > 1e-3f ? std::min(cap, outlineCap) : outlineCap;
    }
    return cap;
}

}  // namespace

float maxPlotDepthToRoadHit(const Vec2& roadStart, const Vec2& edgeDir, float frontage,
                            const Vec2& inward, float setback, int hostRoadId, int bankIndex,
                            Town& town) {
    if (hostRoadId < 0 || hostRoadId >= static_cast<int>(town.roads.size())) {
        return maxPlotDepthToRoadHitImpl(roadStart, edgeDir, frontage, inward, setback, hostRoadId,
                                       town);
    }

    RoadSideFrontage* side = town.roads[static_cast<std::size_t>(hostRoadId)].sideBank(bankIndex);
    if (side == nullptr) {
        return maxPlotDepthToRoadHitImpl(roadStart, edgeDir, frontage, inward, setback, hostRoadId,
                                       town);
    }

    if (side->depthCacheTopologyGen != town.roadTopologyGeneration) {
        side->depthCacheEntries.clear();
        side->depthCacheTopologyGen = town.roadTopologyGeneration;
    }

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDirFrame{};
    if (!roadFrameForBank(town.roads[static_cast<std::size_t>(hostRoadId)], bankIndex, origin,
                          farEnd, edgeDirFrame)) {
        return maxPlotDepthToRoadHitImpl(roadStart, edgeDir, frontage, inward, setback, hostRoadId,
                                       town);
    }

    const float tAlong = (roadStart - origin).dot(edgeDirFrame);
    const DepthCacheKey key{quantizeDepthCacheT(tAlong), quantizeDepthCacheDim(frontage),
                            quantizeDepthCacheDim(setback)};
    const auto          found = side->depthCacheEntries.find(key);
    if (found != side->depthCacheEntries.end()) {
        return found->second;
    }

    const float result =
        maxPlotDepthToRoadHitImpl(roadStart, edgeDir, frontage, inward, setback, hostRoadId, town);
    side->depthCacheEntries[key] = result;
    return result;
}

bool plotsOverlap(const Plot& a, const Plot& b) {
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = a.corners[(i + 1) % 4] - a.corners[i];
        if (edge.length() > 1e-6f && axisSeparates(a.corners, b.corners, perpendicular(edge.normalized()))) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = b.corners[(i + 1) % 4] - b.corners[i];
        if (edge.length() > 1e-6f && axisSeparates(a.corners, b.corners, perpendicular(edge.normalized()))) {
            return false;
        }
    }
    return true;
}

bool footprintsOverlap(const BuildingFootprint& a, const BuildingFootprint& b) {
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = a.corners[(i + 1) % 4] - a.corners[i];
        if (edge.length() > 1e-6f && axisSeparates(a.corners, b.corners, perpendicular(edge.normalized()))) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Vec2 edge = b.corners[(i + 1) % 4] - b.corners[i];
        if (edge.length() > 1e-6f && axisSeparates(a.corners, b.corners, perpendicular(edge.normalized()))) {
            return false;
        }
    }
    return true;
}

bool footprintPlacementValid(const BuildingFootprint& footprint, const Town& town,
                             const TerrainAtlas* terrain, float setback, int hostRoadId) {
    if (!footprintHasRightAngles(footprint)) {
        return false;
    }
    if (!cornersValid(footprint.corners, town, terrain)) {
        return false;
    }
    if (polygonEdgesCrossRoads(footprint.corners, town, hostRoadId)) {
        return false;
    }
    if (footprintOverlapsAlleys(footprint, town, setback, hostRoadId)) {
        return false;
    }
    return true;
}

bool plotPlacementValid(const Plot& plot, const Town& town, const TerrainAtlas* terrain,
                        float setback, int hostRoadId) {
    if (!cornersValid(plot.corners, town, terrain)) {
        return false;
    }
    if (polygonEdgesCrossRoads(plot.corners, town, hostRoadId)) {
        return false;
    }
    if (overlapsInstances(plot, town.buildingInstances, town.relocatingInstanceId)) {
        return false;
    }
    return !plotOverlapsAlleys(plot, town, setback, hostRoadId);
}

bool plotPlacementValid(const Plot& plot, const Town& town, const TerrainAtlas* terrain,
                        float setback, int hostRoadId, const BuildingTerrainRules* borderRules) {
    for (int i = 0; i < 4; ++i) {
        if (!pointInsideTownDisc(town, plot.corners[i])) {
            return false;
        }
    }
    if (terrain != nullptr && terrain->valid) {
        const bool hugBorder = borderRules != nullptr
                               && borderRules->placement == TerrainPlacementMode::Border
                               && borderRules->borderStyle == BorderStyle::Hug;
        if (hugBorder) {
            if (!polygonBuildableHugPlot(plot.corners, *terrain, borderRules->prefer)) {
                return false;
            }
        } else if (!polygonBuildableImpl(plot.corners, *terrain, 0.5f)) {
            return false;
        }
    }
    if (polygonEdgesCrossRoads(plot.corners, town, hostRoadId)) {
        return false;
    }
    if (overlapsInstances(plot, town.buildingInstances, town.relocatingInstanceId)) {
        return false;
    }
    if (plotOverlapsAlleys(plot, town, setback, hostRoadId)) {
        return false;
    }
    if (borderRules == nullptr || terrain == nullptr || !terrain->valid) {
        return true;
    }
    if (borderRules->placement != TerrainPlacementMode::Border) {
        return true;
    }
    if (borderRules->borderStyle == BorderStyle::Hug) {
        return plotMeetsBorderHug(plot, *borderRules, *terrain);
    }
    return plotMeetsBorderBand(plot, *borderRules, *terrain);
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

bool overlapsInstances(const Plot& plot, const std::vector<BuildingInstance>& instances,
                       std::uint32_t skipInstanceId) {
    const BuildingFootprint plotFootprint = plotAsFootprint(plot);
    for (const BuildingInstance& existing : instances) {
        if (existing.id == skipInstanceId) {
            continue;
        }
        if (existing.placementMode == BuildingPlacementMode::SegmentGapFill) {
            if (!existing.footprints.empty() && footprintsOverlap(plotFootprint, existing.footprints[0])) {
                return true;
            }
            continue;
        }
        if (existing.plot.id >= 0 && plotsOverlap(plot, existing.plot)) {
            return true;
        }
    }
    return false;
}

bool footprintOverlapsInstances(const BuildingFootprint& footprint,
                                const std::vector<BuildingInstance>& instances) {
    Plot probe{};
    for (int i = 0; i < 4; ++i) {
        probe.corners[i] = footprint.corners[i];
    }
    return overlapsInstances(probe, instances);
}

bool polygonEdgesCrossRoads(const Vec2 corners[4], const Town& town, int hostRoadId) {
    for (const Road& road : town.roads) {
        if (road.id == hostRoadId) {
            continue;
        }
        for (int i = 0; i < 4; ++i) {
            const Vec2& a = corners[i];
            const Vec2& b = corners[(i + 1) % 4];
            if (segmentsCrossInInterior(a, b, road.a, road.b, 0.03f)) {
                return true;
            }
        }
    }
    return false;
}

bool footprintWithinTownAndRoads(const BuildingFootprint& footprint, const Town& town,
                                 int hostRoadId) {
    for (int i = 0; i < 4; ++i) {
        if (!pointInsideTownDisc(town, footprint.corners[i])) {
            return false;
        }
    }
    return !polygonEdgesCrossRoads(footprint.corners, town, hostRoadId);
}

bool footprintOverlapsMains(const BuildingFootprint& footprint, const Town& town,
                            const DefCache& defs) {
    Plot probe{};
    for (int i = 0; i < 4; ++i) {
        probe.corners[i] = footprint.corners[i];
    }
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (instance.placementMode == BuildingPlacementMode::PlotLot && instance.plot.id >= 0) {
            const BuildingDef* def = defs.building(defs.typeName(instance.typeId));
            if (def != nullptr && !def->allowPlotFill && plotsOverlap(probe, instance.plot)) {
                return true;
            }
        }
        for (const BuildingFootprint& existing : instance.footprints) {
            if (existing.mainBuilding && footprintsOverlap(footprint, existing)) {
                return true;
            }
        }
    }
    return false;
}

namespace {

bool instanceOnRoadSide(const BuildingInstance& instance, int roadId, int bankIndex) {
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
        if (instance.roadId != roadId) {
            return false;
        }
        if (bankIndex >= 0 && instance.roadBank >= 0) {
            return instance.roadBank == bankIndex;
        }
        return true;
    }
    if (instance.plot.roadId != roadId) {
        return false;
    }
    if (bankIndex >= 0 && instance.plot.roadBank >= 0) {
        return instance.plot.roadBank == bankIndex;
    }
    return true;
}

void footprintAabb(const BuildingFootprint& footprint, float& minX, float& minY, float& maxX,
                   float& maxY) {
    minX = maxX = footprint.corners[0].x;
    minY = maxY = footprint.corners[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, footprint.corners[i].x);
        minY = std::min(minY, footprint.corners[i].y);
        maxX = std::max(maxX, footprint.corners[i].x);
        maxY = std::max(maxY, footprint.corners[i].y);
    }
}

void plotAabb(const Plot& plot, float& minX, float& minY, float& maxX, float& maxY) {
    minX = maxX = plot.corners[0].x;
    minY = maxY = plot.corners[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, plot.corners[i].x);
        minY = std::min(minY, plot.corners[i].y);
        maxX = std::max(maxX, plot.corners[i].x);
        maxY = std::max(maxY, plot.corners[i].y);
    }
}

bool aabbOverlap(float minAx, float minAy, float maxAx, float maxAy, float minBx, float minBy,
                 float maxBx, float maxBy) {
    return !(maxAx < minBx - 1e-3f || maxBx < minAx - 1e-3f || maxAy < minBy - 1e-3f
             || maxBy < minAy - 1e-3f);
}

}  // namespace

bool footprintOverlapsMainsOnBank(const BuildingFootprint& footprint, const Town& town,
                                  const DefCache& defs, int roadId, int bankIndex,
                                  const std::vector<int>* otherRoadPlotCandidates) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSide(instance, roadId, bankIndex)) {
            continue;
        }
        if (instance.placementMode == BuildingPlacementMode::PlotLot && instance.plot.id >= 0) {
            const BuildingDef* def = defs.building(defs.typeName(instance.typeId));
            if (def != nullptr && !def->allowPlotFill) {
                Plot probe{};
                for (int i = 0; i < 4; ++i) {
                    probe.corners[i] = footprint.corners[i];
                }
                if (plotsOverlap(probe, instance.plot)) {
                    return true;
                }
            }
        }
        for (const BuildingFootprint& existing : instance.footprints) {
            if (existing.mainBuilding && footprintsOverlap(footprint, existing)) {
                return true;
            }
        }
    }

    if (otherRoadPlotCandidates != nullptr) {
        Plot probe{};
        for (int i = 0; i < 4; ++i) {
            probe.corners[i] = footprint.corners[i];
        }
        for (const int index : *otherRoadPlotCandidates) {
            if (index < 0 || index >= static_cast<int>(town.buildingInstances.size())) {
                continue;
            }
            const BuildingInstance& instance = town.buildingInstances[static_cast<std::size_t>(index)];
            if (instance.placementMode != BuildingPlacementMode::PlotLot || instance.plot.id < 0) {
                continue;
            }
            const BuildingDef* def = defs.building(defs.typeName(instance.typeId));
            if (def == nullptr || def->allowPlotFill) {
                continue;
            }
            if (plotsOverlap(probe, instance.plot)) {
                return true;
            }
            for (const BuildingFootprint& existing : instance.footprints) {
                if (existing.mainBuilding && footprintsOverlap(footprint, existing)) {
                    return true;
                }
            }
        }
    }

    return false;
}

void collectOtherRoadPlotCandidatesForGap(const Town& town, int roadId, int bankIndex,
                                          const Vec2& origin, const Vec2& edgeDir,
                                          const Vec2& inward, float gapStart, float gapEnd,
                                          float maxDepth, std::vector<int>& out) {
    out.clear();
    const Vec2 p0 = origin + edgeDir * gapStart;
    const Vec2 p1 = origin + edgeDir * gapEnd;
    const Vec2 p2 = p0 + inward * maxDepth;
    const Vec2 p3 = p1 + inward * maxDepth;

    float gapMinX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    float gapMaxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    float gapMinY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    float gapMaxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));

    for (int i = 0; i < static_cast<int>(town.buildingInstances.size()); ++i) {
        const BuildingInstance& instance = town.buildingInstances[static_cast<std::size_t>(i)];
        if (instance.placementMode != BuildingPlacementMode::PlotLot || instance.plot.id < 0) {
            continue;
        }
        if (instance.plot.roadId == roadId && instance.plot.roadBank == bankIndex) {
            continue;
        }
        float plotMinX = 0.f;
        float plotMinY = 0.f;
        float plotMaxX = 0.f;
        float plotMaxY = 0.f;
        plotAabb(instance.plot, plotMinX, plotMinY, plotMaxX, plotMaxY);
        if (!aabbOverlap(gapMinX, gapMinY, gapMaxX, gapMaxY, plotMinX, plotMinY, plotMaxX,
                         plotMaxY)) {
            continue;
        }
        out.push_back(i);
    }
}

void rebuildMainOccupancyForBank(Town& town, int roadId, int bankIndex) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr) {
        return;
    }
    side->mainOccupancyT.clear();

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return;
    }

    std::vector<std::pair<float, float>> spans;
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSide(instance, roadId, bankIndex)) {
            continue;
        }
        bool addedSpan = false;
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding) {
                continue;
            }
            const float tMin = footprintTMin(footprint, origin, edgeDir);
            const float tMax = footprintTMax(footprint, origin, edgeDir);
            if (tMax > tMin + 1e-3f) {
                spans.emplace_back(tMin, tMax);
                addedSpan = true;
            }
        }
        if (!addedSpan && instance.placementMode == BuildingPlacementMode::PlotLot) {
            const float tMin = plotTMin(instance.plot, origin, edgeDir);
            const float tMax = plotTMax(instance.plot, origin, edgeDir);
            if (tMax > tMin + 1e-3f) {
                spans.emplace_back(tMin, tMax);
            }
        }
    }

    std::sort(spans.begin(), spans.end(),
              [](const std::pair<float, float>& lhs, const std::pair<float, float>& rhs) {
                  return lhs.first < rhs.first;
              });

    constexpr float kWallEps = 0.08f;
    for (const auto& span : spans) {
        if (side->mainOccupancyT.empty() || span.first > side->mainOccupancyT.back().second + kWallEps) {
            side->mainOccupancyT.push_back(span);
            continue;
        }
        side->mainOccupancyT.back().second =
            std::max(side->mainOccupancyT.back().second, span.second);
    }
}

void rebuildAllMainOccupancyT(Town& town) {
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        rebuildMainOccupancyForBank(town, road.id, 0);
        rebuildMainOccupancyForBank(town, road.id, 1);
    }
}

void rebuildSecondaryRoadIdList(Town& town) {
    town.secondaryRoadIds.clear();
    for (const Road& road : town.roads) {
        if (road.isSecondary) {
            town.secondaryRoadIds.push_back(road.id);
        }
    }
}

float footprintTMin(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir) {
    float best = (footprint.corners[0] - origin).dot(edgeDir);
    for (int i = 1; i < 4; ++i) {
        best = std::min(best, (footprint.corners[i] - origin).dot(edgeDir));
    }
    return best;
}

float footprintTMax(const BuildingFootprint& footprint, const Vec2& origin, const Vec2& edgeDir) {
    float best = (footprint.corners[0] - origin).dot(edgeDir);
    for (int i = 1; i < 4; ++i) {
        best = std::max(best, (footprint.corners[i] - origin).dot(edgeDir));
    }
    return best;
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

void collectAllPrimaryWallGaps(Town& town, float minGapWidth, std::vector<WallGap>& out) {
    collectWallGapsInDistRange(town, minGapWidth, 1e9f, out);
}

void collectWallGapsInDistRange(Town& town, float minGapWidth, float maxDistInclusive,
                                std::vector<WallGap>& out) {
    out.clear();
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const float dist = roadMidpointCenterDist(town, road);
        if (dist > maxDistInclusive + 1e-3f) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (bankAlleyExhaustedVerified(town, road.id, bankIndex, minGapWidth,
                                           maxDistInclusive)) {
                continue;
            }
            if (side->inward.length() < 1e-4f) {
                continue;
            }
            for (const RoadFrontageSegment& segment : side->wallSegments) {
                if (segment.width() + 1e-3f < minGapWidth) {
                    continue;
                }
                out.push_back(wallGapFromSegment(road, bankIndex, segment));
            }
        }
    }
}

void collectWallGapsInHopRange(Town& town, float minGapWidth, int /*minHopInclusive*/,
                               int maxHopInclusive, const std::vector<int>& /*junctionHops*/,
                               std::vector<WallGap>& out) {
    const float maxDist = ringDistAtHop(town, maxHopInclusive);
    collectWallGapsInDistRange(town, minGapWidth, maxDist, out);
}

void clearAlleyGapStateForRoad(Town& town, int roadId) {
    town.alleyCompleteRoadIds.erase(roadId);
    for (auto it = town.checkedAlleyGaps.begin(); it != town.checkedAlleyGaps.end();) {
        if (it->roadId == roadId) {
            it = town.checkedAlleyGaps.erase(it);
        } else {
            ++it;
        }
    }
}

WallGapKey wallGapKey(const WallGap& gap) {
    WallGapKey key;
    key.roadId = gap.roadId;
    key.bankIndex = gap.bankIndex;
    key.tMin = gap.tMin;
    key.tMax = gap.tMax;
    return key;
}

bool isAlleyGapChecked(const Town& town, const WallGap& gap) {
    return town.checkedAlleyGaps.count(wallGapKey(gap)) != 0;
}

void markAlleyGapChecked(Town& town, const WallGap& gap) {
    town.checkedAlleyGaps.insert(wallGapKey(gap));
    frontierRemoveAlleyGap(town, gap.roadId, gap.bankIndex, gap.tMin, gap.tMax);
}

bool bankHasUncheckedAlleyGaps(Town& town, int roadId, int bankIndex, float minGapWidth,
                                 float maxDistInclusive) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size()) || minGapWidth <= 0.f) {
        return false;
    }
    const Road& road = town.roads[static_cast<std::size_t>(roadId)];
    if (road.isBridge) {
        return false;
    }
    const float dist = roadMidpointCenterDist(town, road);
    if (dist > maxDistInclusive + 1e-3f) {
        return false;
    }
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return false;
    }

    for (const RoadFrontageSegment& segment : side->wallSegments) {
        if (segment.width() + 1e-3f < minGapWidth) {
            continue;
        }
        WallGap gap;
        gap.roadId    = roadId;
        gap.bankIndex = bankIndex;
        gap.tMin      = segment.startT;
        gap.tMax      = segment.endT;
        if (!isAlleyGapChecked(town, gap)) {
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

bool wallGapNearExistingSecondary(const Town& town, int roadId, const Vec2& gapPt, float eps) {
    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        if (rec.hostRoadId == roadId
            && distancePointToSegment(gapPt, rec.a, rec.b) <= eps) {
            return true;
        }
    }
    return false;
}

bool segmentIntersects2D(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1) {
    return segmentsIntersect2D(a0, a1, b0, b1);
}

bool segmentIntersectsFootprint(const Vec2& a, const Vec2& b, const BuildingFootprint& fp) {
    for (int i = 0; i < 4; ++i) {
        if (segmentsIntersect2D(a, b, fp.corners[i], fp.corners[(i + 1) % 4])) {
            return true;
        }
    }
    return false;
}

bool segmentIntersectsMainFootprints(const Vec2& a, const Vec2& b, const Town& town) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (footprint.mainBuilding && segmentIntersectsFootprint(a, b, footprint)) {
                return true;
            }
        }
    }
    return false;
}

bool segmentIntersectsSecondaryFootprints(const Vec2& a, const Vec2& b, const Town& town) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding && segmentIntersectsFootprint(a, b, footprint)) {
                return true;
            }
        }
    }
    return false;
}

bool alleyCorridorHitsFootprint(const Vec2& a, const Vec2& b, float roadHalfWidth,
                                const BuildingFootprint& fp) {
    if (segmentIntersectsFootprint(a, b, fp)) {
        return true;
    }
    for (const Vec2& corner : fp.corners) {
        if (distancePointToSegment(corner, a, b) <= roadHalfWidth) {
            return true;
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
        const BuildingDef* def = defs.building(defs.typeName(instance.typeId));
        if (def == nullptr || def->allowPlotFill) {
            continue;
        }
        if (alleyCorridorHitsFootprint(a, b, roadHalfWidth, plotAsFootprint(instance.plot))) {
            return true;
        }
    }
    return false;
}

bool alleySegmentBlockedByMain(const Vec2& a, const Vec2& b, float roadHalfWidth, const Town& town,
                               const DefCache& defs) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding) {
                continue;
            }
            if (alleyCorridorHitsFootprint(a, b, roadHalfWidth, footprint)) {
                return true;
            }
        }
        if (instance.placementMode != BuildingPlacementMode::PlotLot || instance.plot.id < 0) {
            continue;
        }
        const BuildingDef* def = defs.building(defs.typeName(instance.typeId));
        if (def == nullptr || def->allowPlotFill) {
            continue;
        }
        if (alleyCorridorHitsFootprint(a, b, roadHalfWidth, plotAsFootprint(instance.plot))) {
            return true;
        }
    }
    return false;
}

namespace {

bool demolitionAlreadyListed(const std::vector<AuxiliaryDemolition>& out, int instanceId,
                             int footprintLabelId) {
    for (const AuxiliaryDemolition& entry : out) {
        if (entry.instanceId == instanceId && entry.footprintLabelId == footprintLabelId) {
            return true;
        }
    }
    return false;
}

}  // namespace

void collectAuxiliaryDemolitionsForAlley(const Vec2& a, const Vec2& b, float roadHalfWidth,
                                         const Town& town, std::vector<AuxiliaryDemolition>& out) {
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (instance.placementMode != BuildingPlacementMode::PlotLot) {
            continue;
        }
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (footprint.mainBuilding) {
                continue;
            }
            if (!alleyCorridorHitsFootprint(a, b, roadHalfWidth, footprint)) {
                continue;
            }
            if (demolitionAlreadyListed(out, instance.id, footprint.labelId)) {
                continue;
            }
            out.push_back({static_cast<int>(instance.id), footprint.labelId});
        }
    }
}

void applyAuxiliaryDemolitions(Town& town, const std::vector<AuxiliaryDemolition>& demolitions) {
    for (const AuxiliaryDemolition& demo : demolitions) {
        for (BuildingInstance& instance : town.buildingInstances) {
            if (instance.id != demo.instanceId) {
                continue;
            }
            instance.footprints.erase(
                std::remove_if(instance.footprints.begin(), instance.footprints.end(),
                               [&](const BuildingFootprint& footprint) {
                                   return !footprint.mainBuilding
                                          && footprint.labelId == demo.footprintLabelId;
                               }),
                instance.footprints.end());
            break;
        }
    }
}

bool footprintOverlapsAlleys(const BuildingFootprint& footprint, const Town& town, float setback,
                             int excludeRoadId) {
    for (const int roadId : town.secondaryRoadIds) {
        if (roadId < 0 || roadId >= static_cast<int>(town.roads.size()) || roadId == excludeRoadId) {
            continue;
        }
        const Road& road = town.roads[static_cast<std::size_t>(roadId)];
        if (!road.isSecondary) {
            continue;
        }
        if (alleyCorridorHitsFootprint(road.a, road.b, setback, footprint)) {
            return true;
        }
    }
    return false;
}

bool plotOverlapsAlleys(const Plot& plot, const Town& town, float setback, int excludeRoadId) {
    return footprintOverlapsAlleys(plotAsFootprint(plot), town, setback, excludeRoadId);
}

bool segmentClearsRoadSetback(const Vec2& a, const Vec2& b, const Town& town, int hostRoadId,
                              float setback, int excludeRoadId) {
    const Vec2  ab  = b - a;
    const float len = ab.length();
    if (len < 1e-3f) {
        return false;
    }
    const Vec2 edgeDir = ab * (1.f / len);
    const Vec2 left = perpendicular(edgeDir).normalized();
    for (float t : {0.f, len * 0.5f, len}) {
        const Vec2 center = a + edgeDir * t;
        for (float sign : {1.f, -1.f}) {
            const Vec2 p = center + left * (sign * setback);
            if (!pointInsideTownDisc(town, p)) {
                return false;
            }
            for (const Road& road : town.roads) {
                if (road.id == hostRoadId || road.id == excludeRoadId) {
                    continue;
                }
                if (distancePointToSegment(p, road.a, road.b) < setback - 1e-3f) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool segmentsIntersect2D(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1) {
    float t = 0.f;
    float u = 0.f;
    return segmentCrossingParams(a0, a1, b0, b1, t, u);
}

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
    const Vec2  dirA = a1 - a0;
    const Vec2  dirB = b1 - b0;
    const float lenA = dirA.length();
    const float lenB = dirB.length();
    if (lenA < 1e-4f || lenB < 1e-4f) {
        return false;
    }
    const Vec2 unitA = dirA * (1.f / lenA);
    const Vec2 unitB = dirB * (1.f / lenB);
    if (std::abs(unitA.dot(unitB)) < minParallelCos) {
        return false;
    }
    const auto projectT = [&](const Vec2& p) { return (p - a0).dot(unitA); };
    const float bMin = std::min(projectT(b0), projectT(b1));
    const float bMax = std::max(projectT(b0), projectT(b1));
    outOverlapLen = std::max(0.f, std::min(lenA, bMax) - std::max(0.f, bMin));
    outMinSep = std::abs((b0 - a0).dot(perpendicular(unitA)));
    return outOverlapLen > 1e-4f;
}

bool segmentNearParallelRoad(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float minParallelCos, float maxSep, float minOverlapFrac) {
    float minSep = 0.f;
    float overlapLen = 0.f;
    if (!segmentParallelOverlapMetrics(a0, a1, b0, b1, minParallelCos, minSep, overlapLen)) {
        return false;
    }
    const float minLen = std::min((a1 - a0).length(), (b1 - b0).length());
    return minSep <= maxSep && minLen > 1e-4f && overlapLen / minLen >= minOverlapFrac;
}

float raySegmentHitDist(const Vec2& origin, const Vec2& dir, const Vec2& segA, const Vec2& segB,
                        float maxDist) {
    const Vec2  r     = dir;
    const Vec2  s     = segB - segA;
    const float denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-8f) {
        return -1.f;
    }
    const Vec2 qp = segA - origin;
    const float t = (qp.x * s.y - qp.y * s.x) / denom;
    const float u = (qp.x * r.y - qp.y * r.x) / denom;
    if (t < 0.f || t > maxDist || u < -1e-4f || u > 1.f + 1e-4f) {
        return -1.f;
    }
    return t;
}

namespace {

constexpr float kPi = 3.14159265358979323846f;

float nearestRoadRayDist(const Town& town, const Vec2& origin, const Vec2& dir, float maxDist,
                           const std::unordered_set<int>& excludeRoadIds) {
    float best = maxDist + 1.f;
    for (const Road& road : town.roads) {
        if (excludeRoadIds.count(road.id) != 0) {
            continue;
        }
        const float hit = raySegmentHitDist(origin, dir, road.a, road.b, maxDist);
        if (hit > 1e-3f && hit < best) {
            best = hit;
        }
    }
    return best;
}

}  // namespace

bool minSideRoadClearanceAlongSegment(const Town& town, const Vec2& a, const Vec2& b,
                                      int hostRoadId, int destRoadId, float minDist,
                                      int sampleCount, float setback) {
    if (minDist <= 1e-3f) {
        return true;
    }

    const Vec2  ab  = b - a;
    const float len = ab.length();
    if (len < setback * 2.f + 1e-3f) {
        return false;
    }

    const Vec2 dir  = ab * (1.f / len);
    const Vec2 perp = perpendicular(dir).normalized();
    const int  samples = std::max(2, sampleCount);
    const float maxRay = std::max(len * 2.f, 128.f);

    std::unordered_set<int> exclude;
    exclude.insert(hostRoadId);
    exclude.insert(destRoadId);

    for (int i = 0; i < samples; ++i) {
        const float u = static_cast<float>(i + 1) / static_cast<float>(samples + 1);
        const Vec2  point = a + dir * (len * u);
        for (float sign : {1.f, -1.f}) {
            const float sideDist =
                nearestRoadRayDist(town, point, perp * sign, maxRay, exclude);
            if (sideDist < minDist) {
                return false;
            }
        }
    }
    return true;
}

float alleyCrossingAngleDeg(const Town& town, const Vec2& segmentStart, const Vec2& segmentEnd,
                            int destRoadId) {
    if (destRoadId < 0 || destRoadId >= static_cast<int>(town.roads.size())) {
        return 0.f;
    }
    const Road& road    = town.roads[static_cast<std::size_t>(destRoadId)];
    const Vec2  alleyDir = (segmentEnd - segmentStart).normalized();
    const Vec2  roadDir  = (road.b - road.a).normalized();
    if (alleyDir.length() < 1e-4f || roadDir.length() < 1e-4f) {
        return 0.f;
    }
    float dot = std::abs(alleyDir.dot(roadDir));
    dot       = std::min(1.f, std::max(0.f, dot));
    return std::acos(dot) * 180.f / kPi;
}

bool alleyCrossingAngleOk(const Town& town, const Vec2& segmentStart, const Vec2& segmentEnd,
                          int destRoadId, float minAngleDeg) {
    if (minAngleDeg <= 1e-3f) {
        return true;
    }
    return alleyCrossingAngleDeg(town, segmentStart, segmentEnd, destRoadId) >= minAngleDeg;
}

bool alleyEndpointTooClose(const Town& town, const Vec2& start, const Vec2& end, float minSpacing) {
    if (minSpacing <= 1e-3f) {
        return false;
    }

    constexpr float kCoincident = 0.15f;

    const auto tooClose = [&](const Vec2& endpoint, const Vec2& other) {
        const float dist = (endpoint - other).length();
        return dist >= kCoincident && dist < minSpacing;
    };

    for (const Junction& junction : town.junctions) {
        if (tooClose(start, junction.pos) || tooClose(end, junction.pos)) {
            return true;
        }
    }

    for (const Road& road : town.roads) {
        if (!road.isSecondary) {
            continue;
        }
        if (tooClose(start, road.a) || tooClose(start, road.b) || tooClose(end, road.a)
            || tooClose(end, road.b)) {
            return true;
        }
    }

    return false;
}

bool alleyBankDirectionOk(const Town& town, const WallGap& gap, const Vec2& alleyDir,
                          float minSepDeg) {
    if (minSepDeg <= 1e-3f) {
        return true;
    }
    if (alleyDir.length() < 1e-4f) {
        return true;
    }

    const Vec2  dNew   = alleyDir.normalized();
    const float maxDot = std::cos(minSepDeg * kPi / 180.f);

    for (const SecondaryRoadRecord& rec : town.secondaryRoadRecords) {
        if (rec.hostRoadId != gap.roadId || rec.hostBankIndex != gap.bankIndex) {
            continue;
        }
        const Vec2 seg = rec.b - rec.a;
        if (seg.length() < 1e-4f) {
            continue;
        }
        const Vec2 dExisting = seg.normalized();
        if (std::abs(dNew.dot(dExisting)) > maxDot) {
            return false;
        }
    }
    return true;
}

bool validateAlleyProbe(const Town& town, const AlleyProbeCandidate& probe, const TownConfig& cfg,
                        float setback, AlleyQualityReject* outReject) {
    if (probe.segments.empty()) {
        if (outReject != nullptr) {
            *outReject = AlleyQualityReject::ThinSide;
        }
        return false;
    }

    const int hostRoadId = probe.gap.roadId;

    for (const AlleySegmentCandidate& segment : probe.segments) {
        if (!minSideRoadClearanceAlongSegment(town, segment.start, segment.end, hostRoadId,
                                              segment.destRoadId, cfg.minAlleySideRoadDist,
                                              cfg.alleySideRoadSampleCount, setback)) {
            if (outReject != nullptr) {
                *outReject = AlleyQualityReject::ThinSide;
            }
            return false;
        }
    }

    const AlleySegmentCandidate& terminal = probe.segments.back();
    if (terminal.destRoadId >= 0
        && !alleyCrossingAngleOk(town, terminal.start, terminal.end, terminal.destRoadId,
                                 cfg.minAlleyCrossingAngleDeg)) {
        if (outReject != nullptr) {
            *outReject = AlleyQualityReject::BadAngle;
        }
        return false;
    }

    if (alleyEndpointTooClose(town, probe.segments.front().start, probe.segments.back().end,
                              cfg.minAlleyEndpointSpacing)) {
        if (outReject != nullptr) {
            *outReject = AlleyQualityReject::EndpointSpacing;
        }
        return false;
    }

    if (!alleyBankDirectionOk(town, probe.gap, probe.probeDir, cfg.minAlleyBankAngleSepDeg)) {
        if (outReject != nullptr) {
            *outReject = AlleyQualityReject::BankParallel;
        }
        return false;
    }

    if (outReject != nullptr) {
        *outReject = AlleyQualityReject::None;
    }
    return true;
}
