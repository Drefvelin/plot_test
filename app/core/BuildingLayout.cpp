#include "BuildingLayout.h"

#include "FrontierManager.h"
#include "PlotGeometry.h"
#include "TerrainAtlas.h"
#include "TerrainPlacement.h"

#include "Logger.h"
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace {

constexpr float kBuildingAspectMax = 2.f;
constexpr float kPlotAreaMargin    = 1.25f;
constexpr float kEdgeInset         = 0.75f;
constexpr float kDiagAngleRad      = 0.78539816339f;
constexpr float kMainMaxDepthShare = 0.58f;

struct BuildingSides {
    float shortSide = 0.f;
    float longSide  = 0.f;
};

float sampleAreaFromBand(const SizeBand& band, int townSeed, int buildingId, int salt) {
    if (band.maxArea <= band.minArea + 1e-3f) {
        return band.minArea;
    }
    const std::seed_seq seed{townSeed, buildingId, 6100 + salt};
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> dist(band.minArea, band.maxArea);
    return dist(rng);
}

float sampleAspectRatio(int townSeed, int buildingId, int salt) {
    const std::seed_seq seed{townSeed, buildingId, 6200 + salt};
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> dist(1.f, kBuildingAspectMax);
    return dist(rng);
}

BuildingSides sidesFromArea(float area, float aspectRatio) {
    BuildingSides sides;
    if (area < 1e-3f) {
        return sides;
    }
    const float aspect = std::clamp(aspectRatio, 1.f, kBuildingAspectMax);
    sides.shortSide    = std::sqrt(area / aspect);
    sides.longSide     = area / sides.shortSide;
    return sides;
}

float cross2d(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInConvexQuad(const Vec2& p, const Vec2 corners[4]) {
    float sign = 0.f;
    for (int i = 0; i < 4; ++i) {
        const float c = cross2d(corners[i], corners[(i + 1) % 4], p);
        if (std::abs(c) <= 1e-4f) {
            continue;
        }
        if (sign == 0.f) {
            sign = c;
        } else if (sign * c < 0.f) {
            return false;
        }
    }
    return true;
}

bool footprintInsidePlot(const BuildingFootprint& footprint, const Plot& plot) {
    for (const Vec2& corner : footprint.corners) {
        if (!pointInConvexQuad(corner, plot.corners)) {
            return false;
        }
    }
    return true;
}

void projectOntoAxis(const Vec2 axis, const Vec2 corners[4], float& outMin, float& outMax) {
    outMin = outMax = corners[0].dot(axis);
    for (int i = 1; i < 4; ++i) {
        const float value = corners[i].dot(axis);
        outMin            = std::min(outMin, value);
        outMax            = std::max(outMax, value);
    }
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
        projectOntoAxis(axis, a, aMin, aMax);
        projectOntoAxis(axis, b, bMin, bMax);
        if (aMax < bMin - 1e-3f || bMax < aMin - 1e-3f) {
            return false;
        }
    }
    return true;
}

bool overlapsAny(const BuildingFootprint& candidate,
                 const std::vector<BuildingFootprint>& placed) {
    for (const BuildingFootprint& existing : placed) {
        if (convexQuadsOverlap(candidate.corners, existing.corners)) {
            return true;
        }
    }
    return false;
}

void setFootprintCorners(BuildingFootprint& footprint, const Vec2& p0, const Vec2& p1,
                         const Vec2& p2, const Vec2& p3) {
    footprint.corners[0] = p0;
    footprint.corners[1] = p1;
    footprint.corners[2] = p2;
    footprint.corners[3] = p3;
}

void makeAxisAlignedRect(const Vec2& origin, const Vec2& axisU, const Vec2& axisV, float width,
                         float height, BuildingFootprint& out) {
    setFootprintCorners(out, origin, origin + axisU * width, origin + axisU * width + axisV * height,
                        origin + axisV * height);
}

void makeAxisAlignedRectCentered(const Vec2& center, const Vec2& axisU, const Vec2& axisV,
                                 float width, float height, BuildingFootprint& out) {
    const Vec2 halfU = axisU * (width * 0.5f);
    const Vec2 halfV = axisV * (height * 0.5f);
    setFootprintCorners(out, center - halfU - halfV, center + halfU - halfV, center + halfU + halfV,
                        center - halfU + halfV);
}

void makeRotatedRect(const Vec2& center, float width, float height, float angleRad,
                     BuildingFootprint& out) {
    const float halfW = width * 0.5f;
    const float halfH = height * 0.5f;
    const Vec2  u{std::cos(angleRad), std::sin(angleRad)};
    const Vec2  v{-u.y, u.x};
    const Vec2  p0 = center - u * halfW - v * halfH;
    const Vec2  p1 = center + u * halfW - v * halfH;
    const Vec2  p2 = center + u * halfW + v * halfH;
    const Vec2  p3 = center - u * halfW + v * halfH;
    setFootprintCorners(out, p0, p1, p2, p3);
}

void makeOrthogonalRectCentered(const Vec2& center, const Vec2& axisU, float width, float height,
                                BuildingFootprint& out) {
    const Vec2 u = axisU.normalized();
    const Vec2 v = perpendicular(u);
    makeAxisAlignedRectCentered(center, u, v, width, height, out);
}

void rotateVec(Vec2& v, float angleRad) {
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float x = v.x * c - v.y * s;
    const float y = v.x * s + v.y * c;
    v             = {x, y};
}

Vec2 plotRoadAxisU(const Plot& plot) {
    const Vec2  u   = plot.corners[1] - plot.corners[0];
    const float len = u.length();
    if (len < 1e-4f) {
        return {1.f, 0.f};
    }
    return u * (1.f / len);
}

void plotOrthogonalAxes(const Plot& plot, float rotationRad, Vec2& axisU, Vec2& axisV) {
    axisU = plotRoadAxisU(plot);
    axisV = perpendicular(axisU);
    if (std::abs(rotationRad) > 1e-4f) {
        rotateVec(axisU, rotationRad);
        rotateVec(axisV, rotationRad);
    }
}

Vec2 plotCenterFromCorners(const Plot& plot) {
    return {(plot.corners[0].x + plot.corners[1].x + plot.corners[2].x + plot.corners[3].x) * 0.25f,
            (plot.corners[0].y + plot.corners[1].y + plot.corners[2].y + plot.corners[3].y) * 0.25f};
}

Vec2 plotBankInward(const Plot& plot) {
    Vec2 bankIn = perpendicular(plotRoadAxisU(plot));
    const Vec2 frontMid = (plot.corners[0] + plot.corners[1]) * 0.5f;
    const Vec2 plotCtr  = plotCenterFromCorners(plot);
    if (bankIn.dot(plotCtr - frontMid) < 0.f) {
        bankIn = bankIn * -1.f;
    }
    return bankIn;
}

void ensureAxisVAlong(const Vec2& direction, Vec2& axisU, Vec2& axisV) {
    if (axisV.dot(direction) < 0.f) {
        axisU = axisU * -1.f;
        axisV = axisV * -1.f;
    }
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

Vec2 plotCenterToRoadDir(const Plot& plot, const Town& town) {
    const Vec2 plotCenter = plotCenterFromCorners(plot);

    if (plot.roadId >= 0 && plot.roadId < static_cast<int>(town.roads.size())) {
        const Road& road = town.roads[static_cast<std::size_t>(plot.roadId)];
        Vec2        origin{};
        Vec2        farEnd{};
        Vec2        edgeDir{};
        if (roadFrameForBank(road, plot.roadBank, origin, farEnd, edgeDir)) {
            const Vec2 nearest = nearestPointOnSegment(plotCenter, origin, farEnd);
            const Vec2 dir     = nearest - plotCenter;
            if (dir.length() >= 1e-4f) {
                return dir.normalized();
            }
        }

        if (const RoadSideFrontage* side = road.sideBank(plot.roadBank)) {
            if (side->inward.length() >= 1e-4f) {
                return (side->inward * -1.f).normalized();
            }
        }
    }

    const Vec2 frontMid = (plot.corners[0] + plot.corners[1]) * 0.5f;
    const Vec2 backMid  = (plot.corners[2] + plot.corners[3]) * 0.5f;
    return (frontMid - backMid).normalized();
}

Vec2 footprintCenter(const BuildingFootprint& footprint) {
    Vec2 center{};
    for (const Vec2& corner : footprint.corners) {
        center = center + corner;
    }
    return center * 0.25f;
}

Vec2 outwardNormalForEdge(const BuildingFootprint& footprint, int edgeIndex, const Vec2& center) {
    const Vec2& a       = footprint.corners[edgeIndex];
    const Vec2& b       = footprint.corners[(edgeIndex + 1) % 4];
    const Vec2  edgeDir = (b - a).normalized();
    Vec2        normal  = perpendicular(edgeDir);
    const Vec2  edgeMid = (a + b) * 0.5f;
    if (normal.dot(edgeMid - center) < 0.f) {
        normal = normal * -1.f;
    }
    return normal;
}

bool edgeIsLongSide(float edgeLen, float placedLongLen, float placedShortLen, float maxLen) {
    if (placedLongLen > placedShortLen + 1e-3f) {
        return edgeLen >= placedLongLen - 1e-3f;
    }
    return edgeLen >= maxLen - 1e-3f;
}

bool edgeIsShortSide(float edgeLen, float placedLongLen, float placedShortLen, float minLen) {
    if (placedLongLen > placedShortLen + 1e-3f) {
        return edgeLen <= placedShortLen + 1e-3f;
    }
    return edgeLen <= minLen + 1e-3f;
}

void preserveFootprintMeta(const BuildingFootprint& meta, BuildingFootprint& out) {
    out.sizeCategory           = meta.sizeCategory;
    out.mainBuilding           = meta.mainBuilding;
    out.labelId                = meta.labelId;
    out.tmplDoorLong           = meta.tmplDoorLong;
    out.tmplDoorShort          = meta.tmplDoorShort;
    out.tmplLongFacingMiddle   = meta.tmplLongFacingMiddle;
    out.tmplEdgePlacement      = meta.tmplEdgePlacement;
    out.tmplMiddlePlacement    = meta.tmplMiddlePlacement;
    out.tmplCornerPlacement    = meta.tmplCornerPlacement;
}

BuildingTemplateRules templateRulesFromFootprint(const BuildingFootprint& footprint) {
    BuildingTemplateRules rules;
    rules.doorLong          = footprint.tmplDoorLong;
    rules.doorShort         = footprint.tmplDoorShort;
    rules.longFacingMiddle  = footprint.tmplLongFacingMiddle;
    rules.edgePlacement     = footprint.tmplEdgePlacement;
    rules.middlePlacement   = footprint.tmplMiddlePlacement;
    rules.cornerPlacement   = footprint.tmplCornerPlacement;
    return rules;
}

void assignDoorEdge(BuildingFootprint& footprint, const Plot& plot, const Town& town,
                    const BuildingTemplateRules& rules, int plotInstanceId) {
    const Vec2 plotCenter     = plotCenterFromCorners(plot);
    const Vec2 towardRoad     = plotCenterToRoadDir(plot, town);
    const Vec2 buildingCenter = footprintCenter(footprint);
    const bool forceRoadDoor  = footprint.mainBuilding && rules.edgePlacement &&
                                !rules.middlePlacement;

    float edgeLens[4];
    float maxLen = 0.f;
    float minLen = 1e9f;
    for (int i = 0; i < 4; ++i) {
        edgeLens[i] = (footprint.corners[(i + 1) % 4] - footprint.corners[i]).length();
        maxLen      = std::max(maxLen, edgeLens[i]);
        minLen      = std::min(minLen, edgeLens[i]);
    }

    Vec2 faceHint = towardRoad;
    if (forceRoadDoor) {
        faceHint = towardRoad;
    } else if (rules.longFacingMiddle) {
        faceHint = (plotCenter - buildingCenter).normalized();
    }

    int   bestEdge  = -1;
    float bestScore = -1e9f;
    for (int i = 0; i < 4; ++i) {
        if (edgeLens[i] < 1e-4f) {
            continue;
        }

        if (rules.doorLong &&
            !edgeIsLongSide(edgeLens[i], footprint.placedLongLen, footprint.placedShortLen, maxLen)) {
            continue;
        }
        if (rules.doorShort &&
            !edgeIsShortSide(edgeLens[i], footprint.placedLongLen, footprint.placedShortLen, minLen)) {
            continue;
        }

        const Vec2  normal = outwardNormalForEdge(footprint, i, buildingCenter);
        const float score  = normal.dot(faceHint);
        if (score > bestScore) {
            bestScore = score;
            bestEdge  = i;
        }
    }

    if (bestEdge < 0) {
        bestEdge = 0;
    }
    footprint.doorEdge = bestEdge;

    Logger::log("layout",
                "door: plot=" + std::to_string(plotInstanceId) + " building=" +
                    std::to_string(footprint.labelId) + " size=" + footprint.sizeCategory +
                    " doorLong=" + (rules.doorLong ? "1" : "0") + " doorShort=" +
                    (rules.doorShort ? "1" : "0") + " longFacingMiddle=" +
                    (rules.longFacingMiddle ? "1" : "0") + " edge=" + std::to_string(bestEdge) +
                    " edgeLen=" + std::to_string(edgeLens[bestEdge]) + " maxLen=" +
                    std::to_string(maxLen) + " placedLong=" + std::to_string(footprint.placedLongLen) +
                    " placedShort=" + std::to_string(footprint.placedShortLen) + " score=" +
                    std::to_string(bestScore));
}

BuildingTemplateRules defaultTemplateRules() {
    BuildingTemplateRules rules;
    rules.edgePlacement = true;
    return rules;
}

void pickAxes(const Plot& plot, const Vec2& anchor, const BuildingTemplateRules& rules,
              float axisRotation, Vec2& axisU, Vec2& axisV) {
    if (rules.longFacingMiddle) {
        const Vec2 plotCtr  = plotCenterFromCorners(plot);
        Vec2       toCenter = plotCtr - anchor;
        if (toCenter.length() < 1e-4f) {
            toCenter = plotBankInward(plot);
        } else {
            toCenter = toCenter.normalized();
        }
        axisU = toCenter;
        axisV = perpendicular(axisU);
    } else {
        plotOrthogonalAxes(plot, axisRotation, axisU, axisV);
    }
}

void appendThickSegment(sf::VertexArray& tris, const Vec2& a, const Vec2& b, float thickness,
                        const sf::Color& color);

void appendDoorMarker(sf::VertexArray& mesh, const BuildingFootprint& footprint, float ppu) {
    if (footprint.doorEdge < 0 || footprint.doorEdge >= 4) {
        return;
    }

    const int   i     = footprint.doorEdge;
    const Vec2& a     = footprint.corners[i];
    const Vec2& b     = footprint.corners[(i + 1) % 4];
    const Vec2  mid   = (a + b) * 0.5f;
    const Vec2  dir   = (b - a).normalized();
    const float edgeLen = (b - a).length();

    constexpr float kDoorMarkerLengthUnits    = 7.5f;
    constexpr float kDoorMarkerThicknessUnits = 0.45f;
    const float     markerHalf =
        std::min(kDoorMarkerLengthUnits * 0.5f, edgeLen * 0.45f);
    const Vec2 markerA = mid - dir * markerHalf;
    const Vec2 markerB = mid + dir * markerHalf;

    const float thicknessPx = units::toPixels(kDoorMarkerThicknessUnits, ppu);
    appendThickSegment(mesh,
                       {units::toPixels(markerA.x, ppu), units::toPixels(markerA.y, ppu)},
                       {units::toPixels(markerB.x, ppu), units::toPixels(markerB.y, ppu)},
                       thicknessPx, sf::Color(40, 120, 255));
}

bool tryMainRoadFootprint(const Plot& plot, const BuildingSides& sides, BuildingFootprint& out) {
    Vec2 axisU{};
    Vec2 axisV{};
    plotOrthogonalAxes(plot, 0.f, axisU, axisV);
    ensureAxisVAlong(plotBankInward(plot), axisU, axisV);

    const float maxDepth     = plot.depth * kMainMaxDepthShare;
    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f};
    for (const float scale : scaleSteps) {
        const float frontage = sides.longSide * scale;
        const float depth    = std::min(sides.shortSide * scale, maxDepth * scale);
        if (frontage < 2.f || depth < 2.f) {
            continue;
        }
        if (frontage > plot.frontage + 1e-3f || depth > plot.depth + 1e-3f) {
            continue;
        }

        const Vec2 frontMid = (plot.corners[0] + plot.corners[1]) * 0.5f;
        const Vec2 anchor   = frontMid + axisV * (depth * 0.5f);

        BuildingFootprint candidate;
        makeOrthogonalRectCentered(anchor, axisU, frontage, depth, candidate);
        if (footprintInsidePlot(candidate, plot)) {
            const BuildingFootprint meta = out;
            out                            = candidate;
            out.placedShortLen             = depth;
            out.placedLongLen              = frontage;
            preserveFootprintMeta(meta, out);
            return true;
        }
    }

    return false;
}

float roadEdgeHugScore(const BuildingFootprint& footprint, const Plot& plot) {
    const Vec2& roadA = plot.corners[0];
    const Vec2& roadB = plot.corners[1];
    const Vec2  ab    = roadB - roadA;
    const float lenSq = ab.x * ab.x + ab.y * ab.y;

    float best = 1e9f;
    for (const Vec2& corner : footprint.corners) {
        if (lenSq < 1e-6f) {
            best = std::min(best, (corner - roadA).length());
            continue;
        }
        const float t = std::clamp(((corner.x - roadA.x) * ab.x + (corner.y - roadA.y) * ab.y) / lenSq,
                                   0.f, 1.f);
        const Vec2  proj{roadA.x + ab.x * t, roadA.y + ab.y * t};
        best = std::min(best, (corner - proj).length());
    }
    return best;
}

float edgeHugScore(const BuildingFootprint& footprint, const Plot& plot) {
    float best = 1e9f;
    for (const Vec2& corner : footprint.corners) {
        for (int i = 0; i < 4; ++i) {
            const Vec2& a = plot.corners[i];
            const Vec2& b = plot.corners[(i + 1) % 4];
            const Vec2  ab    = b - a;
            const float lenSq = ab.x * ab.x + ab.y * ab.y;
            if (lenSq < 1e-6f) {
                continue;
            }
            const float t = std::clamp(((corner.x - a.x) * ab.x + (corner.y - a.y) * ab.y) / lenSq,
                                       0.f, 1.f);
            const Vec2  proj{a.x + ab.x * t, a.y + ab.y * t};
            best = std::min(best, (corner - proj).length());
        }
    }
    return best;
}

float middlePlacementScore(const BuildingFootprint& footprint, const Plot& plot) {
    return (footprintCenter(footprint) - plotCenterFromCorners(plot)).length();
}

float cornerPlacementScore(const BuildingFootprint& footprint, const Plot& plot, bool roadFrontOnly) {
    const int cornerCount = roadFrontOnly ? 2 : 4;
    float     best        = 1e9f;
    for (int c = 0; c < cornerCount; ++c) {
        const Vec2& plotCorner = plot.corners[c];
        for (const Vec2& fpCorner : footprint.corners) {
            best = std::min(best, (fpCorner - plotCorner).length());
        }
    }
    return best;
}

struct PlacementCandidate {
    Vec2  anchor;
    float axisRotation = 0.f;
    bool  centered     = false;
};

void appendCornerCandidates(const Plot& plot, const BuildingTemplateRules& rules, bool roadFrontOnly,
                            std::vector<PlacementCandidate>& candidates) {
    const Vec2 edgeDir = plotRoadAxisU(plot);
    const Vec2 inward  = plotBankInward(plot);

    const Vec2 anchors[4] = {
        plot.corners[0] + edgeDir * kEdgeInset + inward * kEdgeInset,
        plot.corners[1] - edgeDir * kEdgeInset + inward * kEdgeInset,
        plot.corners[2] - edgeDir * kEdgeInset - inward * kEdgeInset,
        plot.corners[3] + edgeDir * kEdgeInset - inward * kEdgeInset,
    };

    const int start = 0;
    const int end   = roadFrontOnly ? 2 : 4;
    for (int i = start; i < end; ++i) {
        candidates.push_back({anchors[i], 0.f, false});
        if (rules.diagonalAllowed) {
            candidates.push_back({anchors[i], kDiagAngleRad, false});
            candidates.push_back({anchors[i], -kDiagAngleRad, false});
        }
    }
}

bool tryMainCornerFootprint(const Plot& plot, const BuildingSides& sides, BuildingFootprint& out) {
    Vec2 axisU{};
    Vec2 axisV{};
    plotOrthogonalAxes(plot, 0.f, axisU, axisV);
    ensureAxisVAlong(plotBankInward(plot), axisU, axisV);

    const float maxDepth     = plot.depth * kMainMaxDepthShare;
    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f};

    bool              found     = false;
    float             bestScore = 1e9f;
    BuildingFootprint best;

    for (const float scale : scaleSteps) {
        const float frontage = sides.longSide * scale;
        const float depth    = std::min(sides.shortSide * scale, maxDepth * scale);
        if (frontage < 2.f || depth < 2.f) {
            continue;
        }
        if (frontage > plot.frontage + 1e-3f || depth > plot.depth + 1e-3f) {
            continue;
        }

        struct CornerTry {
            Vec2 origin;
        };

        const CornerTry corners[] = {
            {plot.corners[0] + axisU * kEdgeInset + axisV * kEdgeInset},
            {plot.corners[1] - axisU * (frontage + kEdgeInset) + axisV * kEdgeInset},
        };

        for (const CornerTry& corner : corners) {
            BuildingFootprint candidate;
            makeAxisAlignedRect(corner.origin, axisU, axisV, frontage, depth, candidate);
            if (!footprintInsidePlot(candidate, plot)) {
                continue;
            }

            const float score = cornerPlacementScore(candidate, plot, true);
            if (score < bestScore) {
                bestScore           = score;
                best                = candidate;
                best.placedShortLen = depth;
                best.placedLongLen  = frontage;
                found               = true;
            }
        }
    }

    if (found) {
        const BuildingFootprint meta = out;
        out                          = best;
        preserveFootprintMeta(meta, out);
        return true;
    }

    return false;
}

bool tryPlaceAtAnchor(const Plot& plot, const Vec2& anchor, bool centered,
                      const BuildingSides& sides, const BuildingTemplateRules& rules,
                      float axisRotation, const std::vector<BuildingFootprint>& placed,
                      BuildingFootprint& out) {
    Vec2 axisU{};
    Vec2 axisV{};
    pickAxes(plot, anchor, rules, axisRotation, axisU, axisV);

    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f, 0.52f};
    for (const float scale : scaleSteps) {
        const float shortLen = sides.shortSide * scale;
        const float longLen  = sides.longSide * scale;
        if (shortLen < 2.f || longLen < 2.f) {
            continue;
        }

        BuildingFootprint footprint;
        if (centered) {
            makeAxisAlignedRectCentered(anchor, axisU, axisV, shortLen, longLen, footprint);
        } else {
            makeAxisAlignedRect(anchor, axisU, axisV, shortLen, longLen, footprint);
        }
        if (!footprintInsidePlot(footprint, plot) || overlapsAny(footprint, placed)) {
            continue;
        }

        out = footprint;
        out.placedShortLen = shortLen;
        out.placedLongLen  = longLen;
        return true;
    }
    return false;
}

bool tryMainBackEdgeFootprint(const Plot& plot, const BuildingSides& sides, BuildingFootprint& out) {
    if (!isHugTrapezoidPlot(plot)) {
        return false;
    }
    const Vec2 backMid     = (plot.corners[2] + plot.corners[3]) * 0.5f;
    const Vec2 frontMid    = (plot.corners[0] + plot.corners[1]) * 0.5f;
    Vec2       towardBack  = backMid - frontMid;
    if (towardBack.length() < 1e-4f) {
        towardBack = plotBankInward(plot);
    } else {
        towardBack = towardBack.normalized();
    }

    constexpr float kQuarterTurnRad = 1.57079632679f;
    const float     rotations[]     = {0.f, kQuarterTurnRad};

    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f};
    float       bestScore    = 1e30f;
    BuildingFootprint best;
    bool found = false;

    for (const float rotation : rotations) {
        Vec2 axisU{};
        Vec2 axisV{};
        plotOrthogonalAxes(plot, rotation, axisU, axisV);
        ensureAxisVAlong(towardBack, axisU, axisV);

        for (const float scale : scaleSteps) {
            const float frontage = sides.longSide * scale;
            const float depth    = sides.shortSide * scale;
            if (frontage < 2.f || depth < 2.f) {
                continue;
            }
            const Vec2 anchor = backMid - axisV * depth * 0.5f;
            BuildingFootprint candidate;
            makeOrthogonalRectCentered(anchor, axisU, frontage, depth, candidate);
            if (!footprintInsidePlot(candidate, plot)) {
                continue;
            }
            const Vec2 fpCenter = footprintCenter(candidate);
            const float score   = (plotCenterFromCorners(plot) - fpCenter).length();
            if (score < bestScore) {
                bestScore           = score;
                best                = candidate;
                best.placedShortLen = depth;
                best.placedLongLen  = frontage;
                found               = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    const BuildingFootprint meta = out;
    out                          = best;
    preserveFootprintMeta(meta, out);
    return true;
}

std::vector<PlacementCandidate> collectPlacementCandidates(const Plot& plot,
                                                           const BuildingTemplateRules& rules,
                                                           bool isMain) {
    const Vec2 edgeDir = plotRoadAxisU(plot);
    const Vec2 inward  = plotBankInward(plot);
    const Vec2 plotCtr = plotCenterFromCorners(plot);

    std::vector<PlacementCandidate> candidates;

    if (rules.cornerPlacement) {
        appendCornerCandidates(plot, rules, isMain, candidates);
    }

    if (rules.middlePlacement) {
        candidates.push_back({plotCtr, 0.f, true});
        candidates.push_back({plotCtr + inward * (plot.depth * 0.08f), 0.f, true});
        candidates.push_back({plotCtr - inward * (plot.depth * 0.08f), 0.f, true});
        candidates.push_back({plotCtr + edgeDir * (plot.frontage * 0.08f), 0.f, true});
        candidates.push_back({plotCtr - edgeDir * (plot.frontage * 0.08f), 0.f, true});
    }

    if (rules.edgePlacement) {
        if (isMain && rules.cornerPlacement) {
            // Main road-edge candidates are handled by tryMainCornerFootprint / tryMainRoadFootprint.
        } else if (isMain) {
            candidates.push_back({(plot.corners[0] + plot.corners[1]) * 0.5f + inward * kEdgeInset,
                                  0.f, true});
            candidates.push_back(
                {plot.corners[0] + edgeDir * kEdgeInset + inward * kEdgeInset, 0.f, false});
            candidates.push_back(
                {plot.corners[1] - edgeDir * kEdgeInset + inward * kEdgeInset, 0.f, false});
            for (float frontT = 0.2f; frontT <= 0.8f; frontT += 0.2f) {
                const Vec2 anchor =
                    plot.corners[0] + edgeDir * (plot.frontage * frontT) + inward * kEdgeInset;
                candidates.push_back({anchor, 0.f, true});
            }
        } else {
            candidates.push_back(
                {plot.corners[0] + edgeDir * kEdgeInset + inward * kEdgeInset, 0.f, false});
            candidates.push_back(
                {plot.corners[1] - edgeDir * kEdgeInset + inward * kEdgeInset, 0.f, false});
            candidates.push_back(
                {plot.corners[2] - edgeDir * kEdgeInset - inward * kEdgeInset, 0.f, false});
            candidates.push_back(
                {plot.corners[3] + edgeDir * kEdgeInset - inward * kEdgeInset, 0.f, false});
            candidates.push_back({(plot.corners[0] + plot.corners[1]) * 0.5f + inward * kEdgeInset,
                                  0.f, false});
            candidates.push_back({(plot.corners[2] + plot.corners[3]) * 0.5f - inward * kEdgeInset,
                                  0.f, false});
            candidates.push_back({(plot.corners[0] + plot.corners[3]) * 0.5f + edgeDir * kEdgeInset,
                                  0.f, false});
            candidates.push_back({(plot.corners[1] + plot.corners[2]) * 0.5f - edgeDir * kEdgeInset,
                                  0.f, false});

            for (float depthT = 0.2f; depthT <= 0.92f; depthT += 0.12f) {
                for (float frontT = 0.15f; frontT <= 0.85f; frontT += 0.25f) {
                    const Vec2 anchor = plot.corners[0] + edgeDir * (plot.frontage * frontT) +
                                        inward * (plot.depth * depthT);
                    candidates.push_back({anchor, 0.f, true});
                }
            }
        }
    }

    if (!rules.middlePlacement && !rules.edgePlacement && !rules.cornerPlacement) {
        candidates.push_back({plotCtr, 0.f, true});
    }

    return candidates;
}

float scorePlacement(const BuildingFootprint& footprint, const Plot& plot,
                     const BuildingTemplateRules& rules, bool isMain) {
    if (rules.cornerPlacement) {
        float score = cornerPlacementScore(footprint, plot, isMain);
        if (rules.edgePlacement && !isMain) {
            score += edgeHugScore(footprint, plot) * 0.2f;
        } else if (rules.edgePlacement && isMain) {
            score += roadEdgeHugScore(footprint, plot) * 0.2f;
        }
        return score;
    }
    if (rules.middlePlacement) {
        return middlePlacementScore(footprint, plot);
    }
    if (rules.edgePlacement) {
        return isMain ? roadEdgeHugScore(footprint, plot) : edgeHugScore(footprint, plot);
    }
    return 0.f;
}

bool placeFootprintFromTemplate(const Plot& plot, const BuildingSides& sides,
                                const BuildingTemplateRules& rules,
                                const std::vector<BuildingFootprint>& placed, int buildingId,
                                int townSeed, int salt, bool isMain, BuildingFootprint& out) {
    if (isMain && rules.backEdgePlacement && isHugTrapezoidPlot(plot)) {
        return tryMainBackEdgeFootprint(plot, sides, out);
    }
    if (isMain && rules.edgePlacement && !rules.middlePlacement) {
        if (rules.cornerPlacement) {
            if (tryMainCornerFootprint(plot, sides, out)) {
                return true;
            }
        }
        return tryMainRoadFootprint(plot, sides, out);
    }

    std::vector<PlacementCandidate> candidates = collectPlacementCandidates(plot, rules, isMain);

    const std::seed_seq  seed{townSeed, buildingId, 6300 + salt};
    std::mt19937         rng(seed);
    std::shuffle(candidates.begin(), candidates.end(), rng);

    std::vector<float> rotations = {0.f};
    if (rules.diagonalAllowed) {
        rotations.push_back(kDiagAngleRad);
    }

    float             bestScore = 1e9f;
    bool              found     = false;
    BuildingFootprint best;

    for (const PlacementCandidate& candidate : candidates) {
        for (const float rotation : rotations) {
            BuildingFootprint footprint;
            if (!tryPlaceAtAnchor(plot, candidate.anchor, candidate.centered, sides, rules,
                                  candidate.axisRotation + rotation, placed, footprint)) {
                continue;
            }
            const float score = scorePlacement(footprint, plot, rules, isMain);
            if (score < bestScore) {
                bestScore = score;
                best      = footprint;
                found     = true;
            }
        }
    }

    if (found) {
        const BuildingFootprint meta = out;
        out                          = best;
        preserveFootprintMeta(meta, out);
    }
    return found;
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

}  // namespace

PlotAreaBand computePlotAreaBand(const DefCache& defs, const std::string& buildingType,
                                 int /*buildingId*/, int /*townSeed*/) {
    PlotAreaBand band;
    const SizeBand* plotBand = defs.plotSizeBandForBuilding(buildingType);
    if (!plotBand) {
        return band;
    }
    band.minArea = plotBand->minArea * 1.05f;
    band.maxArea = plotBand->maxArea * kPlotAreaMargin;
    return band;
}

float samplePlotTargetArea(const DefCache& defs, const std::string& buildingType, int buildingId,
                           int townSeed) {
    const PlotAreaBand band = computePlotAreaBand(defs, buildingType, buildingId, townSeed);
    if (band.maxArea <= 1e-3f) {
        return 0.f;
    }
    if (band.maxArea <= band.minArea + 1e-3f) {
        return band.minArea;
    }
    const std::seed_seq seed{townSeed, buildingId, 4242};
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> dist(band.minArea, band.maxArea);
    return dist(rng);
}

std::vector<ResolvedBuildingSpec> resolveBuildingSpecs(const DefCache& defs,
                                                       const std::string& buildingType,
                                                       int buildingId, int townSeed) {
    std::vector<ResolvedBuildingSpec> specs;
    const BuildingDef*                def = defs.building(buildingType);
    if (!def) {
        return specs;
    }

    struct MixEntry {
        std::string sizeCategory;
        int         count = 0;
        float       maxArea = 0.f;
    };

    std::vector<MixEntry> mix;
    int                   salt = 0;
    for (const auto& [sizeCategory, range] : def->buildingsOnPlot) {
        const SizeBand* band = defs.buildingSizeBand(sizeCategory);
        if (!band || !range.isValid()) {
            continue;
        }
        int count = range.minCount;
        if (range.maxCount > range.minCount) {
            const std::seed_seq seed{townSeed, buildingId, 9000 + salt++};
            std::mt19937                            rng(seed);
            std::uniform_int_distribution<int> pick(range.minCount, range.maxCount);
            count = pick(rng);
        }
        mix.push_back({sizeCategory, count, band->maxArea});
    }

    std::sort(mix.begin(), mix.end(),
              [](const MixEntry& lhs, const MixEntry& rhs) { return lhs.maxArea > rhs.maxArea; });

    int  subSalt = 0;
    bool first   = true;
    for (const MixEntry& entry : mix) {
        const SizeBand* band = defs.buildingSizeBand(entry.sizeCategory);
        if (!band) {
            continue;
        }
        for (int i = 0; i < entry.count; ++i) {
            ResolvedBuildingSpec spec;
            spec.sizeCategory = entry.sizeCategory;
            spec.isMain       = first;
            spec.area         = sampleAreaFromBand(*band, townSeed, buildingId, subSalt++);
            if (const BuildingTemplate* tmpl = defs.buildingTemplate(entry.sizeCategory)) {
                spec.rules = tmpl->rules;
            } else {
                spec.rules = defaultTemplateRules();
            }
            specs.push_back(spec);
            first = false;
        }
    }

    return specs;
}

bool resolveMainBuildingSpec(const DefCache& defs, const std::string& buildingType, int buildingId,
                             int townSeed, ResolvedBuildingSpec& out) {
    const std::vector<ResolvedBuildingSpec> specs =
        resolveBuildingSpecs(defs, buildingType, buildingId, townSeed);
    if (specs.empty()) {
        return false;
    }
    out = specs.front();
    return true;
}

void copyTemplateRulesToFootprint(const BuildingTemplateRules& rules, BuildingFootprint& footprint) {
    footprint.tmplDoorLong         = rules.doorLong;
    footprint.tmplDoorShort        = rules.doorShort;
    footprint.tmplLongFacingMiddle = rules.longFacingMiddle;
    footprint.tmplEdgePlacement    = rules.edgePlacement;
    footprint.tmplMiddlePlacement  = rules.middlePlacement;
    footprint.tmplCornerPlacement  = rules.cornerPlacement;
    footprint.tmplBackEdgePlacement = rules.backEdgePlacement;
}

bool layoutBuildingsOnPlot(const Plot& plot, const Town& town,
                           const std::vector<ResolvedBuildingSpec>& specs, int buildingId,
                           int townSeed, std::vector<BuildingFootprint>& out) {
    out.clear();
    if (specs.empty()) {
        return false;
    }

    int salt    = 0;
    int labelId = 0;
    for (const ResolvedBuildingSpec& spec : specs) {
        const float         aspect = sampleAspectRatio(townSeed, buildingId, salt);
        const BuildingSides sides  = sidesFromArea(spec.area, aspect);
        BuildingFootprint   footprint;
        footprint.sizeCategory = spec.sizeCategory;
        footprint.mainBuilding = spec.isMain;
        copyTemplateRulesToFootprint(spec.rules, footprint);

        const bool placed = placeFootprintFromTemplate(plot, sides, spec.rules, out, buildingId,
                                                       townSeed, salt++, spec.isMain, footprint);
        if (!placed) {
            if (spec.isMain) {
                Logger::log("layout", "layout_fail: main building size=" + spec.sizeCategory
                                         + " reason=does_not_fit_plot");
                return false;
            }
            Logger::log("layout", "layout_skip: building index=" + std::to_string(labelId) + " size="
                                     + spec.sizeCategory + " reason=no_room_in_plot");
            continue;
        }

        footprint.labelId = labelId++;
        assignDoorEdge(footprint, plot, town, spec.rules, buildingId);
        out.push_back(footprint);
    }

    return !out.empty();
}

void refreshBuildingDoorEdges(Town& town, const DefCache& /*defs*/) {
    for (BuildingInstance& instance : town.buildingInstances) {
        for (BuildingFootprint& footprint : instance.footprints) {
            if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
                assignGapFillDoorEdge(footprint, instance.roadId, instance.roadBank, town);
                continue;
            }
            const BuildingTemplateRules rules = templateRulesFromFootprint(footprint);
            assignDoorEdge(footprint, instance.plot, town, rules, instance.id);
        }
    }
}

Vec2 buildingCenterToRoadOnSegment(const BuildingFootprint& footprint, int roadId, int bankIndex,
                                   const Town& town) {
    Vec2 buildingCenter = footprintCenter(footprint);
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return {};
    }

    const Road& road = town.roads[static_cast<std::size_t>(roadId)];
    Vec2        origin{};
    Vec2        farEnd{};
    Vec2        edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return {};
    }

    const Vec2 nearest = nearestPointOnSegment(buildingCenter, origin, farEnd);
    const Vec2 dir     = nearest - buildingCenter;
    if (dir.length() < 1e-4f) {
        return {};
    }
    return dir.normalized();
}

void assignGapFillDoorEdge(BuildingFootprint& footprint, int roadId, int bankIndex, const Town& town) {
    const Vec2 towardRoad     = buildingCenterToRoadOnSegment(footprint, roadId, bankIndex, town);
    const Vec2 buildingCenter = footprintCenter(footprint);

    int   bestEdge  = 0;
    float bestScore = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const Vec2& a = footprint.corners[i];
        const Vec2& b = footprint.corners[(i + 1) % 4];
        if ((b - a).length() < 1e-4f) {
            continue;
        }
        const Vec2  normal = outwardNormalForEdge(footprint, i, buildingCenter);
        const float score  = normal.dot(towardRoad);
        if (score > bestScore) {
            bestScore = score;
            bestEdge  = i;
        }
    }
    footprint.doorEdge = bestEdge;
}

void removeSecondariesOverlappingMain(Town& town, const BuildingFootprint& newMain, int sourcePlotId) {
    for (BuildingInstance& instance : town.buildingInstances) {
        if (instance.id == sourcePlotId) {
            continue;
        }
        for (auto it = instance.footprints.begin(); it != instance.footprints.end();) {
            if (it->mainBuilding) {
                ++it;
                continue;
            }
            if (!footprintsOverlap(newMain, *it)) {
                ++it;
                continue;
            }
            Logger::log("layout",
                        "secondary_removed: plot=" + std::to_string(instance.id) + " building=" +
                            std::to_string(it->labelId) + " reason=gap_fill_overlap");
            it = instance.footprints.erase(it);
        }
    }
}

void appendBuildingFootprintOutlines(
    sf::VertexArray& mesh, const Town& town, float pixelsPerUnit,
    const std::function<bool(const BuildingInstance&)>& includeInstance) {
    constexpr float kOutlineUnits = 0.35f;
    const sf::Color outlineColor(255, 0, 0);
    const float     thicknessPx = units::toPixels(kOutlineUnits, pixelsPerUnit);

    for (const BuildingInstance& instance : town.buildingInstances) {
        if (includeInstance && !includeInstance(instance)) {
            continue;
        }
        for (const BuildingFootprint& footprint : instance.footprints) {
            Vec2 pxCorners[4];
            for (int i = 0; i < 4; ++i) {
                pxCorners[i] = {units::toPixels(footprint.corners[i].x, pixelsPerUnit),
                                units::toPixels(footprint.corners[i].y, pixelsPerUnit)};
            }
            for (int i = 0; i < 4; ++i) {
                appendThickSegment(mesh, pxCorners[i], pxCorners[(i + 1) % 4], thicknessPx,
                                   outlineColor);
            }
            appendDoorMarker(mesh, footprint, pixelsPerUnit);
        }
    }
}

void appendBuildingFootprintOutlines(Town& town, float pixelsPerUnit) {
    appendBuildingFootprintOutlines(town.buildingOutlineMesh, town, pixelsPerUnit);
}

void BorderHugRejectStats::record(BorderHugReject reason) {
    switch (reason) {
    case BorderHugReject::TooSmall:
        ++tooSmall;
        break;
    case BorderHugReject::BorderMaxDist:
        ++borderMaxDist;
        break;
    case BorderHugReject::RoadCross:
        ++roadCross;
        break;
    case BorderHugReject::Overlap:
        ++overlap;
        break;
    case BorderHugReject::FrontageTooWide:
        ++frontageTooWide;
        break;
    case BorderHugReject::SegmentTFit:
        ++segmentTFit;
        break;
    default:
        break;
    }
}

BorderHugReject BorderHugRejectStats::primary() const {
    int             bestCount = 0;
    BorderHugReject best      = BorderHugReject::None;
    auto            consider  = [&](int count, BorderHugReject reason) {
        if (count > bestCount) {
            bestCount = count;
            best      = reason;
        }
    };
    consider(roadCross, BorderHugReject::RoadCross);
    consider(overlap, BorderHugReject::Overlap);
    consider(borderMaxDist, BorderHugReject::BorderMaxDist);
    consider(segmentTFit, BorderHugReject::SegmentTFit);
    consider(frontageTooWide, BorderHugReject::FrontageTooWide);
    consider(tooSmall, BorderHugReject::TooSmall);
    return best;
}

const char* borderHugRejectName(BorderHugReject reason) {
    switch (reason) {
    case BorderHugReject::TooSmall:
        return "too_small";
    case BorderHugReject::BorderMaxDist:
        return "border_max_dist";
    case BorderHugReject::RoadCross:
        return "road_cross";
    case BorderHugReject::Overlap:
        return "overlap";
    case BorderHugReject::FrontageTooWide:
        return "frontage_too_wide";
    case BorderHugReject::SegmentTFit:
        return "segment_t_fit";
    default:
        return "none";
    }
}

std::string formatBorderHugRejectStats(const BorderHugRejectStats& stats) {
    const BorderHugReject primary = stats.primary();
    if (primary == BorderHugReject::None) {
        return "no_candidate";
    }
    std::ostringstream oss;
    oss << borderHugRejectName(primary);
    const int primaryCount = [&]() {
        switch (primary) {
        case BorderHugReject::TooSmall:
            return stats.tooSmall;
        case BorderHugReject::BorderMaxDist:
            return stats.borderMaxDist;
        case BorderHugReject::RoadCross:
            return stats.roadCross;
        case BorderHugReject::Overlap:
            return stats.overlap;
        case BorderHugReject::FrontageTooWide:
            return stats.frontageTooWide;
        case BorderHugReject::SegmentTFit:
            return stats.segmentTFit;
        default:
            return 0;
        }
    }();
    oss << '(' << primaryCount << ')';
    return oss.str();
}

namespace {

constexpr float kBorderDistEps = 1e-3f;

enum class BorderFootprintReject { None, Overlap, RoadCross };

BorderFootprintReject borderFootprintRejectReason(const BuildingFootprint& footprint,
                                                  const Town& town, int hostRoadId) {
    if (footprintOverlapsInstances(footprint, town.buildingInstances)) {
        return BorderFootprintReject::Overlap;
    }
    if (polygonEdgesCrossRoads(footprint.corners, town, hostRoadId)) {
        return BorderFootprintReject::RoadCross;
    }
    return BorderFootprintReject::None;
}

void borderSlotAxes(const BorderSlotRef& slot, Vec2& axisU, Vec2& axisV) {
    axisU = slot.outlineTangent.length() > 1e-4f ? slot.outlineTangent.normalized()
                                               : Vec2{1.f, 0.f};
    axisV = perpendicular(axisU);
    if (slot.outlineInward.length() > 1e-4f && axisV.dot(slot.outlineInward) < 0.f) {
        axisV = axisV * -1.f;
        axisU = axisU * -1.f;
    }
}

bool borderFootprintPlacementValid(const BuildingFootprint& footprint, const Town& town,
                                   int hostRoadId) {
    if (!footprintHasRightAngles(footprint)) {
        return false;
    }
    if (footprintOverlapsInstances(footprint, town.buildingInstances)) {
        return false;
    }
    return !polygonEdgesCrossRoads(footprint.corners, town, hostRoadId);
}

bool footprintFitsRoadSegmentT(const BuildingFootprint& footprint, const Vec2& origin,
                               const Vec2& edgeDir, float segStartT, float segEndT) {
    float minT = 1e30f;
    float maxT = -1e30f;
    for (const Vec2& corner : footprint.corners) {
        const float t = (corner - origin).dot(edgeDir);
        minT          = std::min(minT, t);
        maxT          = std::max(maxT, t);
    }
    return minT >= segStartT - kBorderDistEps && maxT <= segEndT + kBorderDistEps;
}

}  // namespace

bool tryMainBorderBandOnPlot(const Plot& plot, const ResolvedBuildingSpec& spec, int buildingId,
                             int townSeed, BuildingFootprint& out) {
    const float         aspect = sampleAspectRatio(townSeed, buildingId, 0);
    const BuildingSides sides  = sidesFromArea(spec.area, aspect);
    const Vec2          backMid = (plot.corners[2] + plot.corners[3]) * 0.5f;
    const Vec2          frontMid = (plot.corners[0] + plot.corners[1]) * 0.5f;
    Vec2                towardBack = backMid - frontMid;
    if (towardBack.length() < 1e-4f) {
        towardBack = plotBankInward(plot);
    } else {
        towardBack = towardBack.normalized();
    }

    Vec2 axisU{};
    Vec2 axisV{};
    plotOrthogonalAxes(plot, 0.f, axisU, axisV);
    ensureAxisVAlong(towardBack, axisU, axisV);

    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f};
    float       bestScore    = 1e30f;
    BuildingFootprint best;
    bool found = false;

    for (const float scale : scaleSteps) {
        const float frontage = sides.longSide * scale;
        const float depth    = sides.shortSide * scale;
        if (frontage < 2.f || depth < 2.f) {
            continue;
        }
        const Vec2 anchor = backMid - axisV * depth * 0.5f;
        BuildingFootprint candidate;
        makeOrthogonalRectCentered(anchor, axisU, frontage, depth, candidate);
        if (!footprintInsidePlot(candidate, plot)) {
            continue;
        }
        const Vec2 fpCenter = footprintCenter(candidate);
        const float score   = (plotCenterFromCorners(plot) - fpCenter).length();
        if (score < bestScore) {
            bestScore           = score;
            best                = candidate;
            best.placedShortLen = depth;
            best.placedLongLen  = frontage;
            found               = true;
        }
    }

    if (!found) {
        return false;
    }

    out                = best;
    out.sizeCategory   = spec.sizeCategory;
    out.mainBuilding   = true;
    copyTemplateRulesToFootprint(spec.rules, out);
    return true;
}

bool tryMainBorderHugFromPlot(const Plot& plot, const BorderSlotRef& slot,
                              const ResolvedBuildingSpec& spec, int buildingId, int townSeed,
                              const BuildingTerrainRules& rules, const TerrainAtlas& terrain,
                              const Town& town, BuildingFootprint& out,
                              BorderHugRejectStats* rejectStats) {
    const float         aspect = sampleAspectRatio(townSeed, buildingId, 0);
    const BuildingSides sides  = sidesFromArea(spec.area, aspect);

    const Vec2 plotBackMid = (plot.corners[2] + plot.corners[3]) * 0.5f;
    const Vec2 frontMid    = (plot.corners[0] + plot.corners[1]) * 0.5f;
    Vec2       towardFeature = plotBackMid - frontMid;
    if (towardFeature.length() < 1e-4f) {
        if (slot.outlineInward.length() > 1e-4f) {
            towardFeature = slot.outlineInward.normalized() * -1.f;
        } else {
            towardFeature = plotBankInward(plot);
        }
    } else {
        towardFeature = towardFeature.normalized();
    }

    constexpr float kQuarterTurnRad = 1.57079632679f;
    const float     rotations[]     = {0.f, kQuarterTurnRad};
    const float maxSlide   = slot.hitDist + plot.depth + 4.f;
    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f};

    for (const float rotation : rotations) {
        Vec2 axisU{};
        Vec2 axisV{};
        plotOrthogonalAxes(plot, rotation, axisU, axisV);
        ensureAxisVAlong(towardFeature, axisU, axisV);

        for (const float scale : scaleSteps) {
            const float frontage = sides.longSide * scale;
            const float depth    = sides.shortSide * scale;
            if (frontage < 2.f || depth < 2.f) {
                if (rejectStats != nullptr) {
                    rejectStats->record(BorderHugReject::TooSmall);
                }
                continue;
            }

            for (float slide = 0.f; slide <= maxSlide + kBorderDistEps; slide += 0.75f) {
                const Vec2 backEdgeMid = plotBackMid - towardFeature * slide;
                const Vec2 anchor      = backEdgeMid - axisV * depth * 0.5f;
                BuildingFootprint candidate;
                makeOrthogonalRectCentered(anchor, axisU, frontage, depth, candidate);

                const Vec2 backCornerA = candidate.corners[2];
                const Vec2 backCornerB = candidate.corners[3];
                const float edgeA =
                    distToPreferEdge(backCornerA, slot.terrainId, terrain);
                const float edgeB =
                    distToPreferEdge(backCornerB, slot.terrainId, terrain);
                if (edgeA > rules.borderMaxDist + 1.f || edgeB > rules.borderMaxDist + 1.f) {
                    if (rejectStats != nullptr) {
                        rejectStats->record(BorderHugReject::BorderMaxDist);
                    }
                    continue;
                }

                switch (borderFootprintRejectReason(candidate, town, plot.roadId)) {
                case BorderFootprintReject::Overlap:
                    if (rejectStats != nullptr) {
                        rejectStats->record(BorderHugReject::Overlap);
                    }
                    continue;
                case BorderFootprintReject::RoadCross:
                    if (rejectStats != nullptr) {
                        rejectStats->record(BorderHugReject::RoadCross);
                    }
                    continue;
                default:
                    break;
                }

                out                = candidate;
                out.placedShortLen = depth;
                out.placedLongLen  = frontage;
                out.sizeCategory   = spec.sizeCategory;
                out.mainBuilding   = true;
                copyTemplateRulesToFootprint(spec.rules, out);
                return true;
            }
        }
    }

    return false;
}

bool tryMainBorderHugFromSlot(const BorderSlotRef& slot, float /*roadT*/, const Vec2& /*roadStart*/,
                              const Vec2& /*edgeDir*/, const Vec2& /*bankInward*/,
                              const ResolvedBuildingSpec& spec, int buildingId, int townSeed,
                              const BuildingTerrainRules& /*rules*/, const TerrainAtlas& /*terrain*/,
                              const Town& town, BuildingFootprint& out,
                              BorderHugRejectStats* rejectStats) {
    const float         aspect = sampleAspectRatio(townSeed, buildingId, 0);
    const BuildingSides sides  = sidesFromArea(spec.area, aspect);
    Vec2                axisU{};
    Vec2                axisV{};
    borderSlotAxes(slot, axisU, axisV);

    const float scaleSteps[] = {1.f, 0.92f, 0.84f, 0.76f, 0.68f, 0.6f};

    const Road& road = town.roads[static_cast<std::size_t>(slot.base.roadId)];
    Vec2        frameOrigin{};
    Vec2        farEnd{};
    Vec2        frameEdgeDir{};
    if (!roadFrameForBank(road, slot.base.bankIndex, frameOrigin, farEnd, frameEdgeDir)) {
        return false;
    }

    for (const float scale : scaleSteps) {
        const float frontage = sides.longSide * scale;
        const float depth    = sides.shortSide * scale;
        if (frontage < 2.f || depth < 2.f) {
            if (rejectStats != nullptr) {
                rejectStats->record(BorderHugReject::TooSmall);
            }
            continue;
        }
        if (frontage > (slot.base.endT - slot.base.startT) + kBorderDistEps) {
            if (rejectStats != nullptr) {
                rejectStats->record(BorderHugReject::FrontageTooWide);
            }
            continue;
        }

        const Vec2 backMid = slot.outlinePoint;
        const Vec2 anchor  = backMid - axisV * depth * 0.5f;
        BuildingFootprint candidate;
        makeOrthogonalRectCentered(anchor, axisU, frontage, depth, candidate);

        if (!footprintFitsRoadSegmentT(candidate, frameOrigin, frameEdgeDir, slot.base.startT,
                                       slot.base.endT)) {
            if (rejectStats != nullptr) {
                rejectStats->record(BorderHugReject::SegmentTFit);
            }
            continue;
        }
        switch (borderFootprintRejectReason(candidate, town, slot.base.roadId)) {
        case BorderFootprintReject::Overlap:
            if (rejectStats != nullptr) {
                rejectStats->record(BorderHugReject::Overlap);
            }
            continue;
        case BorderFootprintReject::RoadCross:
            if (rejectStats != nullptr) {
                rejectStats->record(BorderHugReject::RoadCross);
            }
            continue;
        default:
            break;
        }

        out                = candidate;
        out.placedShortLen = depth;
        out.placedLongLen  = frontage;
        out.sizeCategory   = spec.sizeCategory;
        out.mainBuilding   = true;
        copyTemplateRulesToFootprint(spec.rules, out);
        return true;
    }

    return false;
}

void assignDoorEdgeTowardHint(BuildingFootprint& footprint, const Vec2& faceHint,
                              const BuildingTemplateRules& rules, int buildingId) {
    const Vec2 buildingCenter = footprintCenter(footprint);
    Vec2       hint           = faceHint;
    if (hint.length() < 1e-4f) {
        hint = {0.f, 1.f};
    } else {
        hint = hint.normalized();
    }

    float edgeLens[4];
    float maxLen = 0.f;
    float minLen = 1e9f;
    for (int i = 0; i < 4; ++i) {
        edgeLens[i] = (footprint.corners[(i + 1) % 4] - footprint.corners[i]).length();
        maxLen      = std::max(maxLen, edgeLens[i]);
        minLen      = std::min(minLen, edgeLens[i]);
    }

    int   bestEdge  = -1;
    float bestScore = -1e9f;
    for (int i = 0; i < 4; ++i) {
        if (edgeLens[i] < 1e-4f) {
            continue;
        }
        if (rules.doorLong
            && !edgeIsLongSide(edgeLens[i], footprint.placedLongLen, footprint.placedShortLen,
                               maxLen)) {
            continue;
        }
        if (rules.doorShort
            && !edgeIsShortSide(edgeLens[i], footprint.placedLongLen, footprint.placedShortLen,
                                minLen)) {
            continue;
        }
        const Vec2  normal = outwardNormalForEdge(footprint, i, buildingCenter);
        const float score  = normal.dot(hint);
        if (score > bestScore) {
            bestScore = score;
            bestEdge  = i;
        }
    }
    if (bestEdge < 0) {
        bestEdge = 0;
    }
    footprint.doorEdge = bestEdge;

    Logger::log("layout",
                "door: plot=" + std::to_string(buildingId) + " building="
                    + std::to_string(footprint.labelId) + " size=" + footprint.sizeCategory
                    + " door_hint=1 edge=" + std::to_string(bestEdge));
}

bool layoutSecondaryBuildingsOnPlot(const Plot& plot, const Town& town,
                                    const std::vector<ResolvedBuildingSpec>& specs, int buildingId,
                                    int townSeed, std::vector<BuildingFootprint>& ioFootprints) {
    int salt    = 1;
    int labelId = static_cast<int>(ioFootprints.size());
    for (const ResolvedBuildingSpec& spec : specs) {
        if (spec.isMain) {
            continue;
        }
        const float         aspect = sampleAspectRatio(townSeed, buildingId, salt);
        const BuildingSides sides  = sidesFromArea(spec.area, aspect);
        BuildingFootprint   footprint;
        footprint.sizeCategory = spec.sizeCategory;
        footprint.mainBuilding = false;
        copyTemplateRulesToFootprint(spec.rules, footprint);

        if (!placeFootprintFromTemplate(plot, sides, spec.rules, ioFootprints, buildingId, townSeed,
                                        salt++, false, footprint)) {
            Logger::log("layout", "layout_skip: building index=" + std::to_string(labelId)
                                     + " size=" + spec.sizeCategory + " reason=no_room_in_plot");
            continue;
        }

        footprint.labelId = labelId++;
        assignDoorEdge(footprint, plot, town, spec.rules, buildingId);
        ioFootprints.push_back(footprint);
    }
    return true;
}

void assignBuildingDoorEdge(BuildingFootprint& footprint, const Plot& plot, const Town& town,
                            const BuildingTemplateRules& rules, int buildingId) {
    assignDoorEdge(footprint, plot, town, rules, buildingId);
}
