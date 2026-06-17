#include "FrontageGapFill.h"

#include "GrowthRings.h"

#include "BuildingLayout.h"
#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "Logger.h"
#include "PlotDimensions.h"
#include "PlotGeometry.h"
#include "RoadExhaustion.h"
#include "Profile.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
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

bool instanceOnRoadSide(const BuildingInstance& instance, int roadId, int bankIndex = -1) {
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

void collectMainSpansOnSide(const Town& town, int roadId, int bankIndex,
                            const Vec2& origin, const Vec2& edgeDir, std::vector<MainSpan>& out) {
    out.clear();
    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSide(instance, roadId, bankIndex)) {
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

void clipGapToMainWalls(const Town& town, int roadId, int bankIndex,
                        const Vec2& origin, const Vec2& edgeDir, float segmentStart,
                        float segmentEnd, float& outStart, float& outEnd) {
    outStart = segmentStart;
    outEnd   = segmentEnd;

    for (const BuildingInstance& instance : town.buildingInstances) {
        if (!instanceOnRoadSide(instance, roadId, bankIndex)) {
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

bool trySegmentMainAtT(const Town& town, const Vec2& origin, const Vec2& edgeDir,
                       const Vec2& inward, float setback, float t, float frontage, float depth,
                       const DefCache& defs, const TerrainAtlas* terrain, BuildingFootprint& out,
                       int excludeAlleyRoadId = -1) {
    if (frontage < 2.f || depth < 2.f) {
        return false;
    }

    const Vec2 roadStart  = origin + edgeDir * t;
    const Vec2 frontLeft  = roadStart + inward * setback;
    const Vec2 frontRight = frontLeft + edgeDir * frontage;
    const Vec2 backRight  = frontRight + inward * depth;
    const Vec2 backLeft   = frontLeft + inward * depth;

    setFootprintCorners(out, frontLeft, frontRight, backRight, backLeft);
    if (!footprintPlacementValid(out, town, terrain, setback, excludeAlleyRoadId)) {
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
                         float maxDepth, bool shortSideAlongRoad, float& outDepth) {
    if (frontage < 2.f || maxDepth < 2.f) {
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

    depthHi = std::min(depthHi, maxDepth);
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

bool tryFillAtFrontage(const Town& town, const Vec2& origin, const Vec2& edgeDir,
                       const Vec2& inward, float setback, float t, float frontage, float targetArea,
                       const SizeBand& sizeBand, float maxDepth, const DefCache& defs,
                       const TerrainAtlas* terrain, BuildingFootprint& out, const char*& orientUsed,
                       int excludeAlleyRoadId) {
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
        if (trySegmentMainAtT(town, origin, edgeDir, inward, setback, t, frontage, depth, defs,
                              terrain, out, excludeAlleyRoadId)) {
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

bool tryFillSegmentGap(Town& town, const FrontageSlot& slot,
                       const Vec2& origin, const Vec2& edgeDir, const Vec2& inward, float setback,
                       float targetArea, const SizeBand& sizeBand, const DefCache& defs,
                       const TerrainAtlas* terrain, BuildingFootprint& out, const char*& orientUsed,
                       float& usedFrontage, int excludeAlleyRoadId, const char** failReason) {
    if (failReason != nullptr) {
        *failReason = "unknown";
    }

    float gapStart = slot.startT;
    float gapEnd   = slot.endT;
    clipGapToMainWalls(town, slot.roadId, slot.bankIndex, origin, edgeDir, gapStart,
                       gapEnd, gapStart, gapEnd);

    const float gapWidth = gapEnd - gapStart;
    const float minWidth = minGapSegmentWidth(sizeBand);
    if (gapWidth < minWidth - 1e-3f) {
        if (failReason != nullptr) {
            *failReason = "gap_too_narrow";
        }
        return false;
    }

    const Vec2 roadStartGap = origin + edgeDir * gapStart;
    const float maxDepth =
        maxPlotDepthToRoadHit(roadStartGap, edgeDir, gapWidth, inward, setback, slot.roadId,
                              slot.bankIndex, town);
    if (maxDepth < 2.f - 1e-3f) {
        if (failReason != nullptr) {
            *failReason = "zero_depth_cap";
        }
        return false;
    }

    std::vector<float> frontages;
    buildFrontageCandidates(gapWidth, minWidth, frontages);
    if (frontages.empty()) {
        if (failReason != nullptr) {
            *failReason = "no_frontage_candidates";
        }
        return false;
    }

    for (const float frontage : frontages) {
        const float slack = gapWidth - frontage;
        const float tOffsets[] = {slack * 0.5f, 0.f, slack};
        for (const float tOffset : tOffsets) {
            if (tOffset < -1e-3f || tOffset > gapWidth + 1e-3f) {
                continue;
            }
            if (tryFillAtFrontage(town, origin, edgeDir, inward, setback, gapStart + tOffset,
                                  frontage, targetArea, sizeBand, maxDepth, defs, terrain, out,
                                  orientUsed, excludeAlleyRoadId)) {
                usedFrontage = frontage;
                return true;
            }
        }
    }

    if (failReason != nullptr) {
        *failReason = "footprint_invalid";
    }
    return false;
}

bool slotsOverlap(const FrontageSlot& a, const FrontageSlot& b) {
    if (a.roadId != b.roadId || a.bankIndex != b.bankIndex) {
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

void collectMainWallGapSlots(Town& town, const DefCache& defs,
                             const std::string& buildingType, float minWidth, float townGrowth,
                             const BandFilter& bandFilter, std::vector<FrontageSlot>& out,
                             int roadFilter) {
    const char* zone      = zoneTypeForBuilding(defs, buildingType);
    int         wallGapId = kWallGapSegment;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        if (roadFilter >= 0 && road.id != roadFilter) {
            continue;
        }
        if (bandFilter.enabled) {
            const float dist = roadMidpointCenterDist(town, road);
            if (!distInFilter(dist, bandFilter)) {
                continue;
            }
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            if (bankGapExhaustedVerified(town, road.id, bankIndex)) {
                continue;
            }

            Vec2 origin{};
            Vec2 farEnd{};
            Vec2 edgeDir{};
            if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
                continue;
            }

            const float roadLen = road.length();
            std::vector<RoadWallSpan> spans;
            getCachedBuildingWallSpans(town, road.id, bankIndex, origin, edgeDir, roadLen, spans);

            auto addGap = [&](float startT, float endT) {
                if (endT - startT < minWidth - 1e-3f) {
                    return;
                }
                FrontageSlot slot;
                slot.segmentId  = wallGapId--;
                slot.roadId     = road.id;
                slot.bankIndex  = bankIndex;
                slot.startT     = startT;
                slot.endT       = endT;
                {
                    const Vec2 mid = origin + edgeDir * ((startT + endT) * 0.5f);
                    slot.centerDist = (mid - town.center).length();
                }
                slot.zoneScore  = scoreSegmentForZone(town, slot, zone, townGrowth);
                appendUniqueGapSlot(out, slot);
            };

            float prevEnd = 0.f;
            for (const RoadWallSpan& span : spans) {
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

void collectAllGapFillSlots(Town& town, const DefCache& defs, const std::string& buildingType,
                            float townGrowth, float minWidth, const BandFilter& bandFilter,
                            std::vector<FrontageSlot>& out, int roadFilter) {
    out.clear();
    collectFrontageSlots(town, defs, buildingType, townGrowth, out, bandFilter, roadFilter, false);
    if (roadFilter < 0) {
        collectMainWallGapSlots(town, defs, buildingType, minWidth, townGrowth, bandFilter, out,
                                roadFilter);
    }
}

}  // namespace

bool bankHasMainWallGapAtLeast(Town& town, int roadId, int bankIndex, float minWidth) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size()) || minWidth <= 0.f) {
        return false;
    }
    const Road& road = town.roads[static_cast<std::size_t>(roadId)];
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return false;
    }

    Vec2 origin{};
    Vec2 farEnd{};
    Vec2 edgeDir{};
    if (!roadFrameForBank(road, bankIndex, origin, farEnd, edgeDir)) {
        return false;
    }

    const float             roadLen = road.length();
    std::vector<RoadWallSpan> spans;
    getCachedBuildingWallSpans(town, roadId, bankIndex, origin, edgeDir, roadLen, spans);

    std::vector<RoadWallSpan> gaps;
    gapsFromOccupiedSpans(spans, roadLen, minWidth, gaps);
    return !gaps.empty();
}

bool tryPlaceSegmentMain(Town& town, const std::string& buildingType, const DefCache& defs,
                         const PlotConfig& plots, BuildingInstance& out, const PlacementPrep& prep,
                         int townSeed, int maxBuildings, PlacementSearchLog& searchLog,
                         const TerrainAtlas* terrain, const BandFilter& bandFilter, int roadFilter) {
    PROFILE_SCOPE(ProfileScopeId::PlaceGapFill);
    if (!prep.gapFillReady) {
        Logger::log("layout", "gap_fill_fail: queueIndex=" + std::to_string(out.id) + " type="
                                  + buildingType + " reason=no_main_spec");
        return false;
    }

    const SizeBand* sizeBand = defs.buildingSizeBand(prep.mainSpec.sizeCategory);
    if (!sizeBand) {
        Logger::log("layout", "gap_fill_fail: queueIndex=" + std::to_string(out.id) + " type="
                                  + buildingType + " reason=no_size_band");
        return false;
    }

    const float minSegmentWidth = prep.minSegmentWidth;

    searchLog.buildingId   = out.id;
    searchLog.buildingType = buildingType;
    searchLog.targetArea   = prep.mainSpec.area;
    searchLog.townGrowth   = prep.townGrowth;
    searchLog.zoneType     = prep.zoneType;

    std::vector<FrontageSlot> slots;
    collectAllGapFillSlots(town, defs, buildingType, prep.townGrowth, minSegmentWidth, bandFilter,
                           slots, roadFilter);

    int wallGapSlots = 0;
    for (const FrontageSlot& slot : slots) {
        if (slot.segmentId <= kWallGapSegment) {
            ++wallGapSlots;
        }
    }

    Logger::log("layout",
                "gap_fill_diag: begin queueIndex=" + std::to_string(out.id) + " type="
                    + buildingType + " road_filter=" + std::to_string(roadFilter) + " slots="
                    + std::to_string(slots.size()) + " wall_gaps=" + std::to_string(wallGapSlots)
                    + " segments=" + std::to_string(slots.size() - wallGapSlots) + " min_width="
                    + std::to_string(minSegmentWidth) + " target_area="
                    + std::to_string(prep.mainSpec.area));

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
    int slotsSkippedNarrow = 0;
    int slotsSkippedNoInward = 0;
    int slotsSkippedRoadFilter = 0;
    int slotsSkippedBadRoad = 0;
    std::unordered_map<std::string, int> fillFailCounts;

    for (const FrontageSlot& slot : slots) {
        if (slot.width() < minSegmentWidth - 1e-3f) {
            ++slotsSkippedNarrow;
            continue;
        }
        if (slot.roadId < 0 || slot.roadId >= static_cast<int>(town.roads.size())) {
            ++slotsSkippedBadRoad;
            continue;
        }
        if (roadFilter >= 0 && slot.roadId != roadFilter) {
            ++slotsSkippedRoadFilter;
            continue;
        }
        if (slot.zoneScore > 1e8f) {
            ++slotsSkippedZone;
            continue;
        }

        const Road& road = town.roads[static_cast<std::size_t>(slot.roadId)];

        const RoadSideFrontage* sideData = road.sideBank(slot.bankIndex);
        if (sideData == nullptr || sideData->inward.length() < 1e-4f) {
            ++slotsSkippedNoInward;
            continue;
        }

        Vec2 origin{};
        Vec2 farEnd{};
        Vec2 edgeDir{};
        if (!roadFrameForBank(road, slot.bankIndex, origin, farEnd, edgeDir)) {
            ++slotsSkippedBadRoad;
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
        const char*     fillFailReason = "unknown";
        if (!tryFillSegmentGap(town, slot, origin, edgeDir, sideData->inward,
                               plots.frontageSetback, prep.mainSpec.area, *sizeBand, defs, terrain,
                               footprint, orientUsed, usedFrontage, excludeAlleyRoadId,
                               &fillFailReason)) {
            ++fillFailCounts[fillFailReason != nullptr ? fillFailReason : "unknown"];
            if (slotsTried <= 5) {
                const char* slotKind = slot.segmentId <= kWallGapSegment ? "wall_gap" : "segment";
                Logger::log("layout",
                            "gap_fill_diag: slot_fail queueIndex=" + std::to_string(out.id)
                                + " slot=" + slotKind + " road=" + std::to_string(slot.roadId)
                                + " bank=" + std::to_string(slot.bankIndex) + " width="
                                + std::to_string(slot.width()) + " reason=" + fillFailReason);
            }
            continue;
        }

        footprint.sizeCategory = prep.mainSpec.sizeCategory;
        footprint.mainBuilding = true;
        footprint.labelId      = 0;
        copyTemplateRulesToFootprint(prep.mainSpec.rules, footprint);

        removeSecondariesOverlappingMain(town, footprint, out.id);
        assignGapFillDoorEdge(footprint, slot.roadId, -1, town);

        out.placementMode = BuildingPlacementMode::SegmentGapFill;
        out.roadId        = slot.roadId;
        out.roadBank      = slot.bankIndex;
        out.plot          = Plot{};
        out.footprints    = {footprint};

        carveRoadFrontageForFootprint(town, slot.roadId, slot.bankIndex, footprint);

        const char* slotKind = slot.segmentId <= kWallGapSegment ? "wall_gap" : "segment";
        Logger::log("layout",
                    "gap_fill: queueIndex=" + std::to_string(out.id) + " type=" + buildingType
                        + " size=" + prep.mainSpec.sizeCategory + " area="
                        + std::to_string(footprint.placedLongLen * footprint.placedShortLen)
                        + " road=" + std::to_string(slot.roadId) + " slot=" + slotKind + " id="
                        + std::to_string(slot.segmentId) + " frontage="
                        + std::to_string(usedFrontage) + " gap=" + std::to_string(slot.width())
                        + " orient=" + orientUsed);
        return true;
    }

    std::string failSummary;
    for (const auto& [reason, count] : fillFailCounts) {
        failSummary += " " + reason + "=" + std::to_string(count);
    }

    Logger::log("layout",
                "gap_fill_diag: fail queueIndex=" + std::to_string(out.id) + " type="
                    + buildingType + " reason=no_room slots_total="
                    + std::to_string(slots.size()) + " tried=" + std::to_string(slotsTried)
                    + " narrow=" + std::to_string(slotsSkippedNarrow) + " zone="
                    + std::to_string(slotsSkippedZone) + " no_inward="
                    + std::to_string(slotsSkippedNoInward) + " road_filter="
                    + std::to_string(slotsSkippedRoadFilter) + " bad_road="
                    + std::to_string(slotsSkippedBadRoad) + failSummary);

    Logger::log("layout", "gap_fill_fail: queueIndex=" + std::to_string(out.id) + " type=" +
                              buildingType + " reason=no_room slots_tried="
                              + std::to_string(slotsTried) + " zone_skipped="
                              + std::to_string(slotsSkippedZone));
    return false;
}
