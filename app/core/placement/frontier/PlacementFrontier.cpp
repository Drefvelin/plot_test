#include "placement/frontier/PlacementFrontier.h"

#include "placement/frontage/FrontagePlacement.h"
#include "placement/zones/FrontageZones.h"
#include "util/Logger.h"
#include "placement/geometry/PlotGeometry.h"
#include "util/Profile.h"
#include "roads/RoadExhaustion.h"
#include "town/Town.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

bool resolveFrontierRefSegment(const Town& town, const FrontierRef& ref, bool wallBucket,
                               RoadFrontageSegment& outSegment);
bool resolveAlleyFrontierRefSegment(const Town& town, const AlleyFrontierRef& ref,
                                      RoadFrontageSegment& outSegment);

namespace {

constexpr float kDistEps = 1e-3f;

bool refLess(const FrontierRef& lhs, const FrontierRef& rhs) {
    if (std::abs(lhs.centerDist - rhs.centerDist) > kDistEps) {
        return lhs.centerDist < rhs.centerDist;
    }
    if (lhs.roadId != rhs.roadId) {
        return lhs.roadId < rhs.roadId;
    }
    if (lhs.bankIndex != rhs.bankIndex) {
        return lhs.bankIndex < rhs.bankIndex;
    }
    return lhs.startT < rhs.startT;
}

bool relocatingShufflePeers(const Town& town) {
    return town.relocatingInstanceId != 0xFFFFFFFFu;
}

void shuffleRelocateRefs(const Town& town, std::vector<FrontierRef>& refs) {
    if (refs.size() <= 1) {
        return;
    }
    std::seed_seq seed{static_cast<int>(town.relocatingInstanceId), town.suburbanMaxHop,
                       static_cast<int>(town.frontierManager.generation), 7919};
    std::mt19937 rng(seed);
    std::shuffle(refs.begin(), refs.end(), rng);
}

void insertSortedPlot(std::vector<FrontierRef>& bucket, FrontierRef ref) {
    const auto it = std::lower_bound(bucket.begin(), bucket.end(), ref, refLess);
    bucket.insert(it, ref);
}

void insertSortedAlley(std::vector<AlleyFrontierRef>& bucket, AlleyFrontierRef ref) {
    const auto cmp = [](const AlleyFrontierRef& lhs, const AlleyFrontierRef& rhs) {
        if (std::abs(lhs.centerDist - rhs.centerDist) > kDistEps) {
            return lhs.centerDist < rhs.centerDist;
        }
        if (lhs.roadId != rhs.roadId) {
            return lhs.roadId < rhs.roadId;
        }
        return lhs.tMin < rhs.tMin;
    };
    const auto it = std::lower_bound(bucket.begin(), bucket.end(), ref, cmp);
    bucket.insert(it, ref);
}

FrontierRef refFromSegment(const Town& town, int roadId, int bankIndex,
                           const RoadFrontageSegment& segment) {
    FrontierRef ref;
    ref.roadId     = roadId;
    ref.bankIndex  = bankIndex;
    ref.segmentId  = segment.id;
    ref.centerDist = roadCenterDist(town, roadId);
    ref.startT     = segment.startT;
    ref.endT       = segment.endT;
    return ref;
}

bool segmentEligibleForPlot(const Town& town, const Road& road, int bankIndex,
                            const RoadFrontageSegment& segment) {
    if (road.isBridge) {
        return false;
    }
    if (segment.width() + kDistEps < town.syncMinPlotFrontage) {
        return false;
    }
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return false;
    }
    if (bankPlotExhaustedVerified(town, road.id, bankIndex)) {
        return false;
    }
    return true;
}

bool segmentEligibleForWall(const Town& town, const Road& road, int bankIndex,
                            const RoadFrontageSegment& segment, float minWidth) {
    if (road.isBridge) {
        return false;
    }
    if (segment.width() + kDistEps < minWidth) {
        return false;
    }
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return false;
    }
    return true;
}

void removePlotRefsForBank(Town& town, int roadId, int bankIndex) {
    for (auto& bucket : town.frontierManager.plot) {
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [roadId, bankIndex](const FrontierRef& ref) {
                                        return ref.roadId == roadId && ref.bankIndex == bankIndex;
                                    }),
                     bucket.end());
    }
}

void removeWallRefsForBank(Town& town, int roadId, int bankIndex) {
    for (auto& bucket : town.frontierManager.wall) {
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                    [roadId, bankIndex](const FrontierRef& ref) {
                                        return ref.roadId == roadId && ref.bankIndex == bankIndex;
                                    }),
                     bucket.end());
    }
}

void removeAlleyRefsForBank(Town& town, int roadId, int bankIndex) {
    town.frontierManager.alley.erase(
        std::remove_if(town.frontierManager.alley.begin(), town.frontierManager.alley.end(),
                       [roadId, bankIndex](const AlleyFrontierRef& ref) {
                           return ref.roadId == roadId && ref.bankIndex == bankIndex;
                       }),
        town.frontierManager.alley.end());
}

void addPlotSegmentsForBank(Town& town, int roadId, int bankIndex) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr) {
        return;
    }
    for (const RoadFrontageSegment& segment : side->segments) {
        if (!segmentEligibleForPlot(town, road, bankIndex, segment)) {
            continue;
        }
        const FrontierBand band = roadFrontierBand(town, road.id);
        insertSortedPlot(town.frontierManager.plot[static_cast<std::size_t>(band)],
                         refFromSegment(town, roadId, bankIndex, segment));
    }
}

void addWallSegmentsForBank(Town& town, int roadId, int bankIndex, float minGapWidth) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr) {
        return;
    }
    for (const RoadFrontageSegment& segment : side->wallSegments) {
        if (!segmentEligibleForWall(town, road, bankIndex, segment, minGapWidth)) {
            continue;
        }
        const FrontierBand band = roadFrontierBand(town, road.id);
        insertSortedPlot(town.frontierManager.wall[static_cast<std::size_t>(band)],
                         refFromSegment(town, roadId, bankIndex, segment));
    }
}

void addAlleyGapsForBank(Town& town, int roadId, int bankIndex, float minGapWidth,
                         float maxDistInclusive) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return;
    }
    if (!bankHasBuildingOnSide(town, roadId, bankIndex)) {
        return;
    }
    if (bankAlleyExhaustedVerified(town, roadId, bankIndex, minGapWidth, maxDistInclusive)) {
        return;
    }

    const Road&             road = town.roads[static_cast<std::size_t>(roadId)];
    const RoadSideFrontage* side = road.sideBank(bankIndex);
    if (side == nullptr || side->inward.length() < 1e-4f) {
        return;
    }

    for (const RoadFrontageSegment& segment : side->wallSegments) {
        if (segment.width() + kDistEps < minGapWidth) {
            continue;
        }
        const WallGap gap = wallGapFromSegment(road, bankIndex, segment);
        if (isAlleyGapChecked(town, gap)) {
            continue;
        }
        const float roadDist = roadCenterDist(town, roadId);
        if (roadDist > maxDistInclusive + kDistEps) {
            continue;
        }
        AlleyFrontierRef ref;
        ref.roadId     = roadId;
        ref.bankIndex  = bankIndex;
        ref.segmentId  = segment.id;
        ref.tMin       = segment.startT;
        ref.tMax       = segment.endT;
        ref.centerDist = roadDist;
        insertSortedAlley(town.frontierManager.alley, ref);
    }
}

bool peekClosestInBuckets(const Town& town, const FrontierBandSet& bands, float minWidth,
                          const std::unordered_set<int>& skipSegmentIds, bool wallBuckets,
                          FrontierRef& outRef) {
    std::vector<FrontierRef> eligible;

    const auto consider = [&](FrontierBand band) {
        const std::size_t bi = static_cast<std::size_t>(band);
        const std::vector<FrontierRef>& bucket =
            wallBuckets ? town.frontierManager.wall[bi] : town.frontierManager.plot[bi];
        for (const FrontierRef& ref : bucket) {
            if (skipSegmentIds.count(ref.segmentId) != 0) {
                continue;
            }
            if (ref.endT - ref.startT + kDistEps < minWidth) {
                continue;
            }
            if (town.relocatingHostRoadId >= 0 && ref.roadId == town.relocatingHostRoadId) {
                continue;
            }
            eligible.push_back(ref);
        }
    };

    if (bands.core) {
        consider(FrontierBand::Core);
    }
    if (bands.suburban) {
        consider(FrontierBand::Suburban);
    }
    if (bands.rural) {
        consider(FrontierBand::Rural);
    }

    if (eligible.empty()) {
        return false;
    }

    if (relocatingShufflePeers(town)) {
        shuffleRelocateRefs(town, eligible);
        outRef = eligible[0];
        return true;
    }

    outRef = *std::min_element(eligible.begin(), eligible.end(), refLess);
    return true;
}

}  // namespace

bool fillFrontageSlotFromRef(const Town& town, const FrontierRef& ref, bool wallBucket,
                             FrontageSlot& outSlot) {
    RoadFrontageSegment segment;
    if (!resolveFrontierRefSegment(town, ref, wallBucket, segment)) {
        return false;
    }
    outSlot.segmentId  = segment.id;
    outSlot.roadId     = ref.roadId;
    outSlot.bankIndex  = ref.bankIndex;
    outSlot.startT     = segment.startT;
    outSlot.endT       = segment.endT;
    outSlot.centerDist = roadCenterDist(town, ref.roadId);
    outSlot.zoneScore  = 0.f;
    outSlot.isWallGap  = wallBucket;
    return true;
}

void frontierRefreshRoad(Town& town, int roadId) {
    for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
        frontierRefreshPlotBank(town, roadId, bankIndex);
        frontierRefreshWallBank(town, roadId, bankIndex);
        frontierRefreshAlleyBank(town, roadId, bankIndex, town.syncMinAlleyGapWidth);
    }
}

void frontierExtendBands(Town& town, float prevSuburbanMaxDist, float prevCoreMaxDist) {
    constexpr float kDistEps = 1e-3f;
    const float     suburbanDist = suburbanMaxDist(town);
    const float     coreDist     = urbanCoreMaxDist(town);
    const bool      prevCoreEnabled = prevCoreMaxDist >= 0.f;

    const auto classifyAt = [&](float centerDist, float suburbanMax, float coreMax,
                                bool coreBandEnabled) {
        if (centerDist > suburbanMax + kDistEps) {
            return FrontierBand::Rural;
        }
        if (coreBandEnabled && centerDist <= coreMax + kDistEps) {
            return FrontierBand::Core;
        }
        return FrontierBand::Suburban;
    };

    const auto bandLabel = [](FrontierBand band) {
        switch (band) {
        case FrontierBand::Core:
            return "Core";
        case FrontierBand::Suburban:
            return "Suburban";
        case FrontierBand::Rural:
            return "Rural";
        default:
            return "Unknown";
        }
    };

    int resyncCount = 0;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const float      midDist = roadMidpointCenterDist(town, road);
        const FrontierBand oldBand =
            classifyAt(midDist, prevSuburbanMaxDist, prevCoreMaxDist, prevCoreEnabled);
        const FrontierBand newBand = classifyFrontierBand(midDist, town);

        if (oldBand == newBand) {
            continue;
        }

        restoreRoadWallFromInstances(town, road.id, town.syncFrontageSetback);
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            frontierRefreshPlotBank(town, road.id, bankIndex);
        }
        ++resyncCount;
        Logger::log("layout",
                    "wall_resync: roadId=" + std::to_string(road.id) + " band="
                        + bandLabel(oldBand) + "->" + bandLabel(newBand) + " dist="
                        + std::to_string(static_cast<int>(midDist)));
    }

    if (resyncCount > 0) {
        Logger::log("layout",
                    "wall_resync_summary: count=" + std::to_string(resyncCount)
                        + " suburban_dist=" + std::to_string(static_cast<int>(suburbanDist))
                        + " core_dist=" + std::to_string(static_cast<int>(coreDist)));
    }

    ++town.frontierManager.generation;
}

FrontierBandSet FrontierBandSet::townRing() {
    return {true, true, false};
}

FrontierBandSet FrontierBandSet::coreOnly() {
    return {true, false, false};
}

FrontierBandSet FrontierBandSet::ruralOnly() {
    return {false, false, true};
}

FrontierBand classifyFrontierBand(float centerDist, const Town& town) {
    constexpr float kDistEps = 1e-3f;
    const float suburbanDist = suburbanMaxDist(town);
    if (centerDist > suburbanDist + kDistEps) {
        return FrontierBand::Rural;
    }
    if (town.urbanCoreMaxHop >= 0 && centerDist <= urbanCoreMaxDist(town) + kDistEps) {
        return FrontierBand::Core;
    }
    return FrontierBand::Suburban;
}

FrontierBand roadFrontierBand(const Town& town, int roadId) {
    return classifyFrontierBand(roadCenterDist(town, roadId), town);
}

FrontierBandSet frontierBandsFromDistFilter(const Town& town, float minDistInclusive,
                                            float maxDistInclusive, bool filterEnabled) {
    constexpr float kDistEps = 1e-3f;
    if (!filterEnabled) {
        return {true, true, true};
    }

    FrontierBandSet bands;
    const float suburbanDist = suburbanMaxDist(town);
    const float coreDist     = urbanCoreMaxDist(town);

    if (maxDistInclusive + kDistEps >= 0.f && maxDistInclusive <= coreDist + kDistEps
        && town.urbanCoreMaxHop >= 0) {
        bands.core = true;
        return bands;
    }

    if (minDistInclusive >= suburbanDist - kDistEps) {
        bands.rural = true;
        return bands;
    }

    if (town.urbanCoreMaxHop >= 0) {
        bands.core = true;
    }
    bands.suburban = true;
    return bands;
}

void rebuildPlacementFrontier(Town& town) {
    PROFILE_SCOPE(ProfileScopeId::RebuildPlacementFrontier);
    for (auto& bucket : town.frontierManager.plot) {
        bucket.clear();
    }
    for (auto& bucket : town.frontierManager.wall) {
        bucket.clear();
    }
    town.frontierManager.alley.clear();

    const float minGapWidth = town.syncMinGapWidth;
    const float coreMaxDist = urbanCoreMaxDist(town);

    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            addPlotSegmentsForBank(town, road.id, bankIndex);
            addWallSegmentsForBank(town, road.id, bankIndex, minGapWidth);
            if (coreMaxDist >= 0.f) {
                addAlleyGapsForBank(town, road.id, bankIndex, town.syncMinAlleyGapWidth,
                                    coreMaxDist);
            }
        }
    }

    ++town.frontierManager.generation;
}

void frontierRefreshPlotBank(Town& town, int roadId, int bankIndex) {
    removePlotRefsForBank(town, roadId, bankIndex);
    addPlotSegmentsForBank(town, roadId, bankIndex);
}

void frontierRefreshWallBank(Town& town, int roadId, int bankIndex) {
    removeWallRefsForBank(town, roadId, bankIndex);
    addWallSegmentsForBank(town, roadId, bankIndex, town.syncMinGapWidth);
}

void frontierRefreshAlleyBank(Town& town, int roadId, int bankIndex, float minAlleyGapWidth) {
    removeAlleyRefsForBank(town, roadId, bankIndex);
    const float coreMaxDist = urbanCoreMaxDist(town);
    if (coreMaxDist >= 0.f) {
        addAlleyGapsForBank(town, roadId, bankIndex, minAlleyGapWidth, coreMaxDist);
    }
}

bool peekClosestPlotRef(const Town& town, const FrontierBandSet& bands, float minWidth,
                        const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef) {
    return peekClosestInBuckets(town, bands, minWidth, skipSegmentIds, false, outRef);
}

bool peekClosestWallGapRef(const Town& town, const FrontierBandSet& bands, float minWidth,
                           const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef) {
    return peekClosestInBuckets(town, bands, minWidth, skipSegmentIds, true, outRef);
}

bool peekClosestPlotSlot(const Town& town, const FrontierBandSet& bands, float minWidth,
                         const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef,
                         FrontageSlot& outSlot) {
    if (!peekClosestPlotRef(town, bands, minWidth, skipSegmentIds, outRef)) {
        return false;
    }
    return fillFrontageSlotFromRef(town, outRef, false, outSlot);
}

bool peekClosestWallGapSlot(const Town& town, const FrontierBandSet& bands, float minWidth,
                            const std::unordered_set<int>& skipSegmentIds, FrontierRef& outRef,
                            FrontageSlot& outSlot) {
    if (!peekClosestWallGapRef(town, bands, minWidth, skipSegmentIds, outRef)) {
        return false;
    }
    return fillFrontageSlotFromRef(town, outRef, true, outSlot);
}

bool peekClosestAlleyGap(const Town& town, float maxDistInclusive, float minGapWidth,
                         const std::unordered_set<int>& skipSegmentIds, AlleyFrontierRef& outRef,
                         WallGap& outGap) {
    bool        found     = false;
    float       bestDist  = 1e30f;
    std::size_t bestIndex = 0;

    for (std::size_t i = 0; i < town.frontierManager.alley.size(); ++i) {
        const AlleyFrontierRef& ref = town.frontierManager.alley[i];
        if (skipSegmentIds.count(ref.segmentId) != 0) {
            continue;
        }
        if (ref.centerDist > maxDistInclusive + kDistEps) {
            continue;
        }
        if (ref.tMax - ref.tMin + kDistEps < minGapWidth) {
            continue;
        }
        if (!found || ref.centerDist + kDistEps < bestDist) {
            found     = true;
            bestDist  = ref.centerDist;
            bestIndex = i;
            outRef    = ref;
        }
    }

    if (!found) {
        return false;
    }

    outRef = town.frontierManager.alley[bestIndex];
    if (outRef.roadId < 0 || outRef.roadId >= static_cast<int>(town.roads.size())) {
        return false;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(outRef.roadId)];
    const RoadSideFrontage* side = road.sideBank(outRef.bankIndex);
    if (side == nullptr) {
        return false;
    }
    for (const RoadFrontageSegment& segment : side->wallSegments) {
        if (segment.id != outRef.segmentId) {
            continue;
        }
        outGap = wallGapFromSegment(road, outRef.bankIndex, segment);
        return true;
    }
    return false;
}

void consumeAlleyGap(Town& town, const AlleyFrontierRef& ref) {
    town.frontierManager.alley.erase(
        std::remove_if(town.frontierManager.alley.begin(), town.frontierManager.alley.end(),
                       [&](const AlleyFrontierRef& entry) {
                           return entry.roadId == ref.roadId && entry.bankIndex == ref.bankIndex
                                  && entry.segmentId == ref.segmentId;
                       }),
        town.frontierManager.alley.end());
}

void frontierRemoveAlleyGap(Town& town, int roadId, int bankIndex, float tMin, float tMax) {
    town.frontierManager.alley.erase(
        std::remove_if(town.frontierManager.alley.begin(), town.frontierManager.alley.end(),
                       [&](const AlleyFrontierRef& entry) {
                           return entry.roadId == roadId && entry.bankIndex == bankIndex
                                  && std::abs(entry.tMin - tMin) < 0.1f
                                  && std::abs(entry.tMax - tMax) < 0.1f;
                       }),
        town.frontierManager.alley.end());
}

bool placementFrontierHasUncheckedAlleyInCore(const Town& town, float minGapWidth,
                                              float maxDistInclusive) {
    constexpr float kDistEps = 1e-3f;
    for (const AlleyFrontierRef& ref : town.frontierManager.alley) {
        if (ref.centerDist > maxDistInclusive + kDistEps) {
            continue;
        }
        if (ref.tMax - ref.tMin + kDistEps < minGapWidth) {
            continue;
        }
        return true;
    }
    return false;
}

bool resolveFrontierRefSegment(const Town& town, const FrontierRef& ref, bool wallBucket,
                               RoadFrontageSegment& outSegment) {
    if (ref.roadId < 0 || ref.roadId >= static_cast<int>(town.roads.size())) {
        return false;
    }
    const Road&             road = town.roads[static_cast<std::size_t>(ref.roadId)];
    const RoadSideFrontage* side = road.sideBank(ref.bankIndex);
    if (side == nullptr) {
        return false;
    }
    const std::vector<RoadFrontageSegment>& segments =
        wallBucket ? side->wallSegments : side->segments;
    for (const RoadFrontageSegment& segment : segments) {
        if (segment.id != ref.segmentId) {
            continue;
        }
        outSegment = segment;
        return true;
    }
    return false;
}

PlotFrontierAudit auditPlotFrontier(const Town& town, const FrontierBandSet& bands) {
    PlotFrontierAudit audit;

    std::unordered_set<int> frontierSegmentIds;
    const auto collectFrontier = [&](FrontierBand band) {
        for (const FrontierRef& ref : town.frontierManager.plot[static_cast<std::size_t>(band)]) {
            ++audit.frontierRefs;
            if (band == FrontierBand::Core) {
                ++audit.coreRefs;
            } else if (band == FrontierBand::Suburban) {
                ++audit.suburbanRefs;
            } else if (band == FrontierBand::Rural) {
                ++audit.ruralRefs;
            }
            frontierSegmentIds.insert(ref.segmentId);
            RoadFrontageSegment segment;
            if (!resolveFrontierRefSegment(town, ref, false, segment)
                || segment.width() + kDistEps < town.syncMinPlotFrontage) {
                ++audit.staleFrontierRefs;
            }
        }
    };
    if (bands.core) {
        collectFrontier(FrontierBand::Core);
    }
    if (bands.suburban) {
        collectFrontier(FrontierBand::Suburban);
    }
    if (bands.rural) {
        collectFrontier(FrontierBand::Rural);
    }

    audit.uniqueFrontierIds = static_cast<int>(frontierSegmentIds.size());

    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        for (int bankIndex = 0; bankIndex < 2; ++bankIndex) {
            const RoadSideFrontage* side = road.sideBank(bankIndex);
            if (side == nullptr) {
                continue;
            }
            for (const RoadFrontageSegment& segment : side->segments) {
                if (!segmentEligibleForPlot(town, road, bankIndex, segment)) {
                    continue;
                }
                const FrontierBand band = roadFrontierBand(town, road.id);
                const bool inBand = (band == FrontierBand::Core && bands.core)
                                    || (band == FrontierBand::Suburban && bands.suburban)
                                    || (band == FrontierBand::Rural && bands.rural);
                if (!inBand) {
                    continue;
                }
                ++audit.geometryEligible;
                if (frontierSegmentIds.count(segment.id) == 0) {
                    ++audit.missingFromFrontier;
                }
            }
        }
    }

    return audit;
}

bool resolveAlleyFrontierRefSegment(const Town& town, const AlleyFrontierRef& ref,
                                      RoadFrontageSegment& outSegment) {
    FrontierRef plotRef;
    plotRef.roadId    = ref.roadId;
    plotRef.bankIndex = ref.bankIndex;
    plotRef.segmentId = ref.segmentId;
    return resolveFrontierRefSegment(town, plotRef, true, outSegment);
}
