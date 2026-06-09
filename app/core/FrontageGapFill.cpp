#include "FrontageGapFill.h"

#include "BuildingLayout.h"
#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "Logger.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float kWallEps        = 0.08f;
constexpr float kSlotDedupeEps  = 0.35f;
constexpr int   kWallGapSegment = -1000;
constexpr int   kMaxGapFillSlotsTried = 48;

struct MainSpan {
    float tMin = 0.f;
    float tMax = 0.f;
};

bool mainFootprintMeetsSizeBand(float frontage, float depth, const SizeBand& band) {
    if (frontage < 2.f || depth < 2.f) {
        return false;
    }
    const float area = frontage * depth;
    if (area < band.minArea - 1e-3f || area > band.maxArea + 1e-3f) {
        return false;
    }
    return aspectRatioOk(frontage, depth, kBuildingAspectMax);
}

void setFootprintCorners(BuildingFootprint& footprint, const Vec2& p0, const Vec2& p1,
                         const Vec2& p2, const Vec2& p3) {
    footprint.corners[0] = p0;
    footprint.corners[1] = p1;
    footprint.corners[2] = p2;
    footprint.corners[3] = p3;
}

bool instanceOnRoadSide(const BuildingInstance& instance, int roadId, int cellId,
                        int bankIndex = -1) {
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

void collectMainSpansOnSide(const Town& town, int roadId, int cellId, int bankIndex,
                            const Vec2& origin, const Vec2& edgeDir, std::vector<MainSpan>& out) {
    out.clear();
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSide(instance, roadId, cellId, bankIndex)) {
            continue;
        }
        bool addedSpan = false;
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding) {
                continue;
            }
            MainSpan span;
            span.tMin = footprintTMin(footprint, origin, edgeDir);
            span.tMax = footprintTMax(footprint, origin, edgeDir);
            if (span.tMax > span.tMin + 1e-3f) {
                out.push_back(span);
                addedSpan = true;
            }
        }
        if (!addedSpan && instance.placementMode == BuildingPlacementMode::PlotLot) {
            MainSpan span;
            span.tMin = plotTMin(instance.plot, origin, edgeDir);
            span.tMax = plotTMax(instance.plot, origin, edgeDir);
            if (span.tMax > span.tMin + 1e-3f) {
                out.push_back(span);
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const MainSpan& lhs, const MainSpan& rhs) { return lhs.tMin < rhs.tMin; });

    std::vector<MainSpan> merged;
    for (const MainSpan& span : out) {
        if (merged.empty() || span.tMin > merged.back().tMax + kWallEps) {
            merged.push_back(span);
            continue;
        }
        merged.back().tMax = std::max(merged.back().tMax, span.tMax);
    }
    out.swap(merged);
}

void clipGapToMainWalls(const Town& town, int roadId, int cellId, int bankIndex,
                        const Vec2& origin, const Vec2& edgeDir, float segmentStart,
                        float segmentEnd, float& outStart, float& outEnd) {
    outStart = segmentStart;
    outEnd   = segmentEnd;

    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSide(instance, roadId, cellId, bankIndex)) {
            continue;
        }
        for (const BuildingFootprint& footprint : instance.footprints) {
            if (!footprint.mainBuilding) {
                continue;
            }
            const float tMin = footprintTMin(footprint, origin, edgeDir);
            const float tMax = footprintTMax(footprint, origin, edgeDir);
            if (tMax <= segmentStart + kWallEps) {
                outStart = std::max(outStart, tMax);
            }
            if (tMin >= segmentEnd - kWallEps) {
                outEnd = std::min(outEnd, tMin);
            }
        }
    }
}

bool trySegmentMainAtT(const Town& town, const Cell& cell, const Vec2& origin, const Vec2& edgeDir,
                       const Vec2& inward, float setback, float t, float frontage, float depth,
                       const DefCache& defs, BuildingFootprint& out, int excludeAlleyRoadId = -1) {
    if (frontage < 2.f || depth < 2.f) {
        return false;
    }

    const Vec2 roadStart  = origin + edgeDir * t;
    const Vec2 frontLeft  = roadStart + inward * setback;
    const Vec2 frontRight = frontLeft + edgeDir * frontage;
    const Vec2 backRight  = frontRight + inward * depth;
    const Vec2 backLeft   = frontLeft + inward * depth;

    setFootprintCorners(out, frontLeft, frontRight, backRight, backLeft);
    if (!footprintInsideCell(out, cell)) {
        return false;
    }
    if (footprintOverlapsMains(out, town, defs)) {
        return false;
    }
    if (footprintOverlapsAlleys(out, town, setback, excludeAlleyRoadId)) {
        return false;
    }

    out.placedLongLen  = std::max(frontage, depth);
    out.placedShortLen = std::min(frontage, depth);
    return true;
}

bool computeGapFillDepth(float frontage, float targetArea, const SizeBand& band,
                         float maxDepthInCell, bool shortSideAlongRoad, float& outDepth) {
    if (frontage < 2.f || maxDepthInCell < 2.f) {
        return false;
    }

    float depthLo = band.minArea / frontage;
    float depthHi = band.maxArea / frontage;

    depthLo = std::max(depthLo, frontage / kBuildingAspectMax);
    depthHi = std::min(depthHi, frontage * kBuildingAspectMax);
    if (shortSideAlongRoad) {
        depthLo = std::max(depthLo, frontage);
    } else {
        depthHi = std::min(depthHi, frontage);
    }

    depthHi = std::min(depthHi, maxDepthInCell);
    depthLo = std::max(depthLo, 2.f);

    if (depthLo > depthHi + 1e-3f) {
        return false;
    }

    const float depthTarget = targetArea / frontage;
    outDepth                = std::clamp(depthTarget, depthLo, depthHi);
    return mainFootprintMeetsSizeBand(frontage, outDepth, band);
}

float minGapSegmentWidth(const SizeBand& band) {
    return std::max(2.f, std::sqrt(band.minArea / kBuildingAspectMax));
}

bool tryFillAtFrontage(const Town& town, const Cell& cell, const Vec2& origin, const Vec2& edgeDir,
                       const Vec2& inward, float setback, float t, float frontage, float targetArea,
                       const SizeBand& sizeBand, float maxDepth, const DefCache& defs,
                       BuildingFootprint& out, const char*& orientUsed, int excludeAlleyRoadId) {
    struct OrientTry {
        const char* name;
        bool        shortSideAlongRoad;
    };

    const OrientTry shortFirst[] = {
        {"short", true},
        {"long", false},
    };
    const OrientTry longFirst[] = {
        {"long", false},
        {"short", true},
    };

    const bool preferShortFirst = frontage < std::sqrt(sizeBand.maxArea);
    const OrientTry* order      = preferShortFirst ? shortFirst : longFirst;

    for (int i = 0; i < 2; ++i) {
        const OrientTry& orient = order[i];
        float            depth  = 0.f;
        if (!computeGapFillDepth(frontage, targetArea, sizeBand, maxDepth, orient.shortSideAlongRoad,
                                 depth)) {
            continue;
        }
        if (trySegmentMainAtT(town, cell, origin, edgeDir, inward, setback, t, frontage, depth, defs,
                              out, excludeAlleyRoadId)) {
            out.placedLongLen  = std::max(frontage, depth);
            out.placedShortLen = std::min(frontage, depth);
            orientUsed         = orient.name;
            return true;
        }
    }
    return false;
}

void buildFrontageCandidates(float gapWidth, float minWidth, std::vector<float>& out) {
    out.clear();
    if (gapWidth < minWidth - 1e-3f) {
        return;
    }
    for (float frontage = gapWidth; frontage >= minWidth - 1e-3f; frontage *= 0.88f) {
        out.push_back(frontage);
        if (out.size() >= 14) {
            break;
        }
    }
    std::sort(out.begin(), out.end(), std::greater<float>());
    out.erase(std::unique(out.begin(), out.end(),
                          [](float lhs, float rhs) { return std::abs(lhs - rhs) < 0.25f; }),
              out.end());
}

bool tryFillSegmentGap(const Town& town, const Cell& cell, const FrontageSlot& slot,
                       const Vec2& origin, const Vec2& edgeDir, const Vec2& inward, float setback,
                       float targetArea, const SizeBand& sizeBand, const DefCache& defs,
                       BuildingFootprint& out, const char*& orientUsed, float& usedFrontage,
                       int excludeAlleyRoadId) {
    float gapStart = slot.startT;
    float gapEnd   = slot.endT;
    clipGapToMainWalls(town, slot.roadId, slot.cellId, slot.bankIndex, origin, edgeDir, gapStart,
                       gapEnd, gapStart, gapEnd);

    const float gapWidth = gapEnd - gapStart;
    const float minWidth = minGapSegmentWidth(sizeBand);
    if (gapWidth < minWidth - 1e-3f) {
        return false;
    }

    const Vec2 roadStartGap = origin + edgeDir * gapStart;
    const float maxDepth =
        maxPlotDepthInCell(roadStartGap, edgeDir, gapWidth, inward, setback, cell);

    std::vector<float> frontages;
    buildFrontageCandidates(gapWidth, minWidth, frontages);

    for (const float frontage : frontages) {
        const float slack = gapWidth - frontage;
        const float tOffsets[] = {slack * 0.5f, 0.f, slack};
        for (const float tOffset : tOffsets) {
            if (tOffset < -1e-3f || tOffset > gapWidth + 1e-3f) {
                continue;
            }
            if (tryFillAtFrontage(town, cell, origin, edgeDir, inward, setback, gapStart + tOffset,
                                  frontage, targetArea, sizeBand, maxDepth, defs, out, orientUsed,
                                  excludeAlleyRoadId)) {
                usedFrontage = frontage;
                return true;
            }
        }
    }

    return false;
}

bool slotsOverlap(const FrontageSlot& a, const FrontageSlot& b) {
    if (a.roadId != b.roadId || a.cellId != b.cellId) {
        return false;
    }
    return !(a.endT < b.startT - kSlotDedupeEps || b.endT < a.startT - kSlotDedupeEps);
}

void appendUniqueGapSlot(std::vector<FrontageSlot>& slots, const FrontageSlot& candidate) {
    for (const FrontageSlot& existing : slots) {
        if (!slotsOverlap(existing, candidate)) {
            continue;
        }
        if (std::abs(existing.startT - candidate.startT) < kSlotDedupeEps
            && std::abs(existing.endT - candidate.endT) < kSlotDedupeEps) {
            return;
        }
    }
    slots.push_back(candidate);
}

void collectMainWallGapSlots(const Town& town, const DefCache& defs,
                             const std::string& buildingType, float minWidth, float townGrowth,
                             std::vector<FrontageSlot>& out, int roadFilter) {
    const char*            zone         = zoneTypeForBuilding(defs, buildingType);
    const std::vector<int> junctionHops = computeJunctionHopDistances(town);
    int                    wallGapId    = kWallGapSegment;

    for (const Road& road : town.roads) {
        if (road.isSecondary) {
            continue;
        }
        if (roadFilter >= 0 && road.id != roadFilter) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (side->cellId < 0) {
                continue;
            }

            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForCell(road, side->cellId, origin, farEnd, edgeDir)) {
                continue;
            }

            const float roadLen = road.length();
            std::vector<MainSpan> spans;
            collectMainSpansOnSide(town, road.id, side->cellId, bankIndex, origin, edgeDir, spans);

            auto addGap = [&](float startT, float endT) {
                if (endT - startT < minWidth - 1e-3f) {
                    return;
                }
                FrontageSlot slot;
                slot.segmentId  = wallGapId--;
                slot.roadId     = road.id;
                slot.cellId     = side->cellId;
                slot.bankIndex  = bankIndex;
                slot.startT     = startT;
                slot.endT       = endT;
                {
                    const Vec2 mid = origin + edgeDir * ((startT + endT) * 0.5f);
                    const Cell& cell = town.cells[static_cast<std::size_t>(side->cellId)];
                    slot.centerDist = (mid - cell.centroid).length();
                }
                slot.zoneScore  = scoreSegmentForZone(town, slot, zone, townGrowth, junctionHops);
                appendUniqueGapSlot(out, slot);
            };

            float prevEnd = 0.f;
            for (const MainSpan& span : spans) {
                if (span.tMin > prevEnd + minWidth - 1e-3f) {
                    addGap(prevEnd, span.tMin);
                }
                prevEnd = std::max(prevEnd, span.tMax);
            }
            if (roadLen > prevEnd + minWidth - 1e-3f) {
                addGap(prevEnd, roadLen);
            }
        }
    }
}

void collectAllGapFillSlots(const Town& town, const DefCache& defs, const std::string& buildingType,
                            float townGrowth, float minWidth, std::vector<FrontageSlot>& out,
                            int roadFilter) {
    out.clear();
    collectFrontageSlots(town, defs, buildingType, townGrowth, out, roadFilter);
    if (roadFilter < 0) {
        collectMainWallGapSlots(town, defs, buildingType, minWidth, townGrowth, out, roadFilter);
    }
}

}  // namespace

bool tryPlaceSegmentMain(Town& town, const std::string& buildingType, const DefCache& defs,
                         const PlotConfig& plots, BuildingInstance& out, int townSeed,
                         int maxBuildings, PlacementSearchLog& searchLog, int roadFilter,
                         bool useCellCentroid) {
    ResolvedBuildingSpec mainSpec;
    if (!resolveMainBuildingSpec(defs, buildingType, out.id, townSeed, mainSpec)) {
        Logger::log("layout", "gap_fill_fail: queueIndex=" + std::to_string(out.id) + " type=" +
                                  buildingType + " reason=no_main_spec");
        return false;
    }

    const SizeBand* sizeBand = defs.buildingSizeBand(mainSpec.sizeCategory);
    if (!sizeBand) {
        Logger::log("layout", "gap_fill_fail: queueIndex=" + std::to_string(out.id) + " type=" +
                                  buildingType + " reason=no_size_band");
        return false;
    }

    const float minSegmentWidth = minGapSegmentWidth(*sizeBand);

    const float townGrowth =
        maxBuildings > 0
            ? static_cast<float>(town.buildingInstances.size()) / static_cast<float>(maxBuildings)
            : 0.f;

    searchLog.buildingId   = out.id;
    searchLog.buildingType = buildingType;
    searchLog.targetArea   = mainSpec.area;
    searchLog.townGrowth   = townGrowth;
    searchLog.zoneType     = zoneTypeForBuilding(defs, buildingType);

    std::vector<FrontageSlot> slots;
    collectAllGapFillSlots(town, defs, buildingType, townGrowth, minSegmentWidth, slots,
                           roadFilter);

    if (useCellCentroid) {
        for (FrontageSlot& slot : slots) {
            if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
                continue;
            }
            if (slot.cellId < 0 || slot.cellId >= static_cast<int>(town.cells.size())) {
                continue;
            }
            const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
            const Cell& cell = town.cells[static_cast<std::size_t>(slot.cellId)];
            Vec2        origin{};
            Vec2        farEnd{};
            Vec2        edgeDir{};
            if (!roadFrameForCell(road, slot.cellId, origin, farEnd, edgeDir)) {
                continue;
            }
            const Vec2 mid = origin + edgeDir * ((slot.startT + slot.endT) * 0.5f);
            slot.centerDist = (mid - cell.centroid).length();
        }
    }

    std::sort(slots.begin(), slots.end(),
              [](const FrontageSlot& lhs, const FrontageSlot& rhs) {
                  if (std::abs(lhs.centerDist - rhs.centerDist) > 1e-3f) {
                      return lhs.centerDist < rhs.centerDist;
                  }
                  const float lhsWidth = lhs.width();
                  const float rhsWidth = rhs.width();
                  if (std::abs(lhsWidth - rhsWidth) > 1e-3f) {
                      return lhsWidth < rhsWidth;
                  }
                  return lhs.segmentId < rhs.segmentId;
              });

    int slotsTried = 0;
    int slotsSkippedZone = 0;
    for (const FrontageSlot& slot : slots) {
        if (slot.width() < minSegmentWidth - 1e-3f) {
            continue;
        }
        if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
            continue;
        }
        if (slot.cellId < 0 || slot.cellId >= static_cast<int>(town.cells.size())) {
            continue;
        }
        if (roadFilter >= 0 && slot.roadId != roadFilter) {
            continue;
        }
        if (slot.zoneScore > 1e8f) {
            ++slotsSkippedZone;
            continue;
        }

        const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];
        const Cell& cell = town.cells[static_cast<std::size_t>(slot.cellId)];

        const RoadSideFrontage* sideData = road.sideForPlacement(slot.cellId, slot.bankIndex);
        if (sideData == nullptr || sideData->inward.length() < 1e-4f) {
            continue;
        }

        Vec2 origin{};
        Vec2 farEnd{};
        Vec2 edgeDir{};
        if (!roadFrameForCell(road, slot.cellId, origin, farEnd, edgeDir)) {
            continue;
        }

        ++slotsTried;
        if (slotsTried > kMaxGapFillSlotsTried) {
            break;
        }

        BuildingFootprint footprint;
        const char*     orientUsed   = "long";
        float           usedFrontage = 0.f;
        const int excludeAlleyRoadId = road.isSecondary ? road.id : -1;
        if (!tryFillSegmentGap(town, cell, slot, origin, edgeDir, sideData->inward,
                               plots.frontageSetback, mainSpec.area, *sizeBand, defs, footprint,
                               orientUsed, usedFrontage, excludeAlleyRoadId)) {
            continue;
        }

        footprint.sizeCategory = mainSpec.sizeCategory;
        footprint.mainBuilding = true;
        footprint.labelId      = 0;
        copyTemplateRulesToFootprint(mainSpec.rules, footprint);

        removeSecondariesOverlappingMain(town, footprint, out.id);
        assignGapFillDoorEdge(footprint, slot.roadId, slot.cellId, town);

        out.placementMode = BuildingPlacementMode::SegmentGapFill;
        out.roadId        = slot.roadId;
        out.cellId        = slot.cellId;
        out.roadBank      = road.isSameCellSecondary() ? slot.bankIndex : -1;
        out.plot          = Plot{};
        out.footprints    = {footprint};

        carveRoadFrontageForFootprint(town, slot.roadId, slot.cellId, footprint);

        const char* slotKind = slot.segmentId <= kWallGapSegment ? "wall_gap" : "segment";
        Logger::log("layout",
                    "gap_fill: queueIndex=" + std::to_string(out.id) + " type=" + buildingType
                        + " size=" + mainSpec.sizeCategory + " area="
                        + std::to_string(footprint.placedLongLen * footprint.placedShortLen)
                        + " road=" + std::to_string(slot.roadId) + " slot=" + slotKind + " id="
                        + std::to_string(slot.segmentId) + " frontage="
                        + std::to_string(usedFrontage) + " gap=" + std::to_string(slot.width())
                        + " orient=" + orientUsed);
        return true;
    }

    Logger::log("layout", "gap_fill_fail: queueIndex=" + std::to_string(out.id) + " type=" +
                              buildingType + " reason=no_room slots_tried="
                              + std::to_string(slotsTried) + " zone_skipped="
                              + std::to_string(slotsSkippedZone));
    return false;
}
