#include "GrowthRings.h"

#include "BuildingGrowthQueue.h"
#include "BuildingLayout.h"
#include "FrontageGapFill.h"
#include "FrontagePlacement.h"
#include "FrontageZones.h"
#include "Logger.h"
#include "PlacementFrontier.h"
#include "PlacementPrep.h"
#include "PlotGeometry.h"
#include "Profile.h"
#include "RoadExhaustion.h"
#include "SecondaryRoadPlacement.h"
#include "Units.h"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

bool instanceOnAlleyRoad(const BuildingInstance& instance, int roadId) {
    if (roadId < 0) {
        return false;
    }
    if (instance.placementMode == BuildingPlacementMode::SegmentGapFill) {
        return instance.roadId == roadId;
    }
    if (instance.placementMode == BuildingPlacementMode::PlotLot) {
        return instance.plot.roadId == roadId;
    }
    return false;
}

}  // namespace

BandFilter BandFilter::suburban(const Town& town) {
    BandFilter f;
    f.enabled          = true;
    f.minDistInclusive = 0.f;
    f.maxDistInclusive = suburbanMaxDist(town);
    return f;
}

BandFilter BandFilter::rural(const Town& town) {
    BandFilter f;
    f.enabled          = true;
    f.minDistInclusive = suburbanMaxDist(town);
    f.maxDistInclusive = 1e9f;
    f.exclusiveMin     = true;
    return f;
}

BandFilter BandFilter::urbanCore(const Town& town) {
    BandFilter f;
    f.enabled          = true;
    f.minDistInclusive = 0.f;
    f.maxDistInclusive = urbanCoreMaxDist(town);
    return f;
}

bool distInFilter(float dist, const BandFilter& filter) {
    if (!filter.enabled) {
        return true;
    }
    if (filter.exclusiveMin) {
        if (dist <= filter.minDistInclusive + 1e-3f) {
            return false;
        }
    } else if (dist + 1e-3f < filter.minDistInclusive) {
        return false;
    }
    return dist <= filter.maxDistInclusive + 1e-3f;
}

bool mayGapFillOnRoad(const Town& town, const DefCache& defs, const std::string& buildingType,
                      int roadId) {
    if (!isGapFillBuildingType(defs, buildingType)) {
        return false;
    }
    if (town.urbanCoreMaxHop < 0) {
        return false;
    }
    if (town.ringPhase != RingPhase::DensifyCore) {
        return false;
    }
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return false;
    }
    const float dist = roadMidpointCenterDist(town, town.roads[static_cast<std::size_t>(roadId)]);
    return dist <= urbanCoreMaxDist(town) + 1e-3f;
}

bool skipRoadForPlacement(Town& town, const DefCache& defs, const std::string& buildingType,
                          int roadId, const std::vector<int>& junctionHops) {
    if (roadId < 0 || roadId >= static_cast<int>(town.roads.size())) {
        return true;
    }
    const bool plotDead =
        bankPlotExhaustedVerified(town, roadId, 0) && bankPlotExhaustedVerified(town, roadId, 1);
    if (!plotDead) {
        return false;
    }
    if (!mayGapFillOnRoad(town, defs, buildingType, roadId)) {
        return true;
    }
    const bool wallGapDead =
        bankGapExhaustedVerified(town, roadId, 0) && bankGapExhaustedVerified(town, roadId, 1);
    if (!wallGapDead) {
        return false;
    }
    return !bankHasSegmentGapFillPotential(town, roadId, 0, town.syncMinGapWidth)
           && !bankHasSegmentGapFillPotential(town, roadId, 1, town.syncMinGapWidth);
}

bool tryPlaceOnTownRoad(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, int townSeed, int maxBuildings,
                        PlacementSearchLog& searchLog, const TerrainAtlas* terrain, int roadId,
                        const BandFilter& bandFilter, const std::vector<int>& junctionHops,
                        const PlacementPrep& prep, RoadAttemptMemo& memo) {
    memo.syncContext(town);
    if (memo.shouldSkipRoad(roadId)) {
        return false;
    }
    if (skipRoadForPlacement(town, defs, instance.buildingType, roadId, junctionHops)) {
        return false;
    }

    if (tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance, prep, townSeed,
                         maxBuildings, searchLog, terrain, bandFilter, roadId)) {
        return true;
    }

    if (mayGapFillOnRoad(town, defs, instance.buildingType, roadId)) {
        if (tryPlaceSegmentMain(town, instance.buildingType, defs, plots, instance, prep, townSeed,
                                maxBuildings, searchLog, terrain, bandFilter, roadId)) {
            return true;
        }
    }

    memo.recordFailure(roadId);
    return false;
}

std::string ringPhaseLabel(RingPhase phase) {
    switch (phase) {
    case RingPhase::Normal:
        return "Normal";
    case RingPhase::DensifyCore:
        return "DensifyCore";
    default:
        return "Unknown";
    }
}

void logRingState(const Town& town) {
    Logger::log("layout",
                "ring_state: suburban_max=" + std::to_string(town.suburbanMaxHop)
                    + " suburban_dist=" + std::to_string(static_cast<int>(suburbanMaxDist(town)))
                    + " urban_core=" + std::to_string(town.urbanCoreMaxHop) + " phase="
                    + ringPhaseLabel(town.ringPhase));
}

void bumpGrowthRings(Town& town) {
    PROFILE_SCOPE(ProfileScopeId::RingBump);
    const float prevSuburbanDist = suburbanMaxDist(town);
    const float prevCoreDist =
        town.urbanCoreMaxHop >= 0 ? urbanCoreMaxDist(town) : -1.f;

    town.urbanCoreMaxHop = town.suburbanMaxHop - 1;
    town.suburbanMaxHop += 1;
    town.ringPhase       = RingPhase::DensifyCore;

    Logger::log("layout",
                "ring_bump: urban_core=0-" + std::to_string(town.urbanCoreMaxHop)
                    + " suburban=0-" + std::to_string(town.suburbanMaxHop)
                    + " suburban_dist=" + std::to_string(static_cast<int>(suburbanMaxDist(town)))
                    + " phase=DensifyCore wall_resync=on_zone_change");
    frontierExtendBands(town, prevSuburbanDist, prevCoreDist);
}

void clearAlleyStateForRoad(Town& town, int roadId) {
    clearAlleyGapStateForRoad(town, roadId);
}

int maxRoadHopInTown(const Town& town, const std::vector<int>& junctionHops) {
    int maxHop = 0;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const int hops = roadHop(town, road.id, junctionHops);
        if (hops >= 0) {
            maxHop = std::max(maxHop, hops);
        }
    }
    return maxHop;
}

std::vector<int> collectSuburbanRoadIds(const Town& town) {
    struct RoadEntry {
        int   roadId     = -1;
        float centerDist = 0.f;
    };
    const float maxDist = suburbanMaxDist(town);
    std::vector<RoadEntry> entries;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const float dist = roadMidpointCenterDist(town, road);
        if (dist > maxDist + 1e-3f) {
            continue;
        }
        entries.push_back({road.id, dist});
    }
    std::sort(entries.begin(), entries.end(), [](const RoadEntry& lhs, const RoadEntry& rhs) {
        if (std::abs(lhs.centerDist - rhs.centerDist) > 1e-3f) {
            return lhs.centerDist < rhs.centerDist;
        }
        return lhs.roadId < rhs.roadId;
    });
    std::vector<int> out;
    out.reserve(entries.size());
    for (const RoadEntry& entry : entries) {
        out.push_back(entry.roadId);
    }
    return out;
}

std::vector<int> collectCoreRoadIds(const Town& town) {
    if (town.urbanCoreMaxHop < 0) {
        return {};
    }
    struct RoadEntry {
        int   roadId     = -1;
        float centerDist = 0.f;
    };
    const float maxDist = urbanCoreMaxDist(town);
    std::vector<RoadEntry> entries;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const float dist = roadMidpointCenterDist(town, road);
        if (dist > maxDist + 1e-3f) {
            continue;
        }
        entries.push_back({road.id, dist});
    }
    std::sort(entries.begin(), entries.end(), [](const RoadEntry& lhs, const RoadEntry& rhs) {
        if (std::abs(lhs.centerDist - rhs.centerDist) > 1e-3f) {
            return lhs.centerDist < rhs.centerDist;
        }
        return lhs.roadId < rhs.roadId;
    });
    std::vector<int> out;
    out.reserve(entries.size());
    for (const RoadEntry& entry : entries) {
        out.push_back(entry.roadId);
    }
    return out;
}

std::vector<int> collectRuralRoadIds(const Town& town) {
    struct RoadEntry {
        int   roadId     = -1;
        float centerDist = 0.f;
    };
    const float minDist = suburbanMaxDist(town);
    std::vector<RoadEntry> entries;
    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const float dist = roadMidpointCenterDist(town, road);
        if (dist <= minDist + 1e-3f) {
            continue;
        }
        entries.push_back({road.id, dist});
    }
    std::sort(entries.begin(), entries.end(), [](const RoadEntry& lhs, const RoadEntry& rhs) {
        if (std::abs(lhs.centerDist - rhs.centerDist) > 1e-3f) {
            return lhs.centerDist < rhs.centerDist;
        }
        return lhs.roadId < rhs.roadId;
    });
    std::vector<int> out;
    out.reserve(entries.size());
    for (const RoadEntry& entry : entries) {
        out.push_back(entry.roadId);
    }
    return out;
}

const std::vector<int>& getCachedSuburbanRoadIds(Town& town, int suburbanMaxHop) {
    getJunctionHops(town);
    if (town.suburbanRoadListMaxHop == suburbanMaxHop) {
        return town.suburbanRoadListCache;
    }
    town.suburbanRoadListCache = collectSuburbanRoadIds(town);
    town.suburbanRoadListMaxHop = suburbanMaxHop;
    return town.suburbanRoadListCache;
}

const std::vector<int>& getCachedRuralRoadIds(Town& town, int suburbanMaxHop) {
    getJunctionHops(town);
    if (town.ruralRoadListMaxHop == suburbanMaxHop) {
        return town.ruralRoadListCache;
    }
    town.ruralRoadListCache = collectRuralRoadIds(town);
    town.ruralRoadListMaxHop = suburbanMaxHop;
    return town.ruralRoadListCache;
}

namespace {

bool tryPlaceOnSuburbanRoad(Town& town, BuildingInstance& instance, const DefCache& defs,
                            const PlotConfig& plots, int townSeed, int maxBuildings,
                            PlacementSearchLog& searchLog, const TerrainAtlas* terrain, int roadId,
                            const std::vector<int>& junctionHops, const PlacementPrep& prep,
                            RoadAttemptMemo& memo) {
    const BandFilter suburbanFilter = BandFilter::suburban(town);

    if (!tryPlaceOnTownRoad(town, instance, defs, plots, townSeed, maxBuildings, searchLog,
                            terrain, roadId, suburbanFilter, junctionHops, prep, memo)) {
        return false;
    }

    const int logRoadId = instance.placementMode == BuildingPlacementMode::SegmentGapFill
                              ? instance.roadId
                              : instance.plot.roadId;
    const int hops = roadHop(town, logRoadId, junctionHops);
    const float roadDist =
        (roadId >= 0 && roadId < static_cast<int>(town.roads.size()))
            ? roadMidpointCenterDist(town, town.roads[static_cast<std::size_t>(roadId)])
            : 0.f;
    const char* mode =
        instance.placementMode == BuildingPlacementMode::SegmentGapFill ? "gap_fill" : "plot_lot";
    Logger::log("layout",
                "ring_place: queueIndex=" + std::to_string(instance.id) + " type="
                    + instance.buildingType + " roadHop=" + std::to_string(hops) + " roadDist="
                    + std::to_string(static_cast<int>(roadDist)) + " roadId="
                    + std::to_string(roadId) + " suburban_max="
                    + std::to_string(town.suburbanMaxHop) + " urban_core="
                    + std::to_string(town.urbanCoreMaxHop) + " phase="
                    + ringPhaseLabel(town.ringPhase) + " mode=" + mode + " band=suburban");
    return true;
}

bool tryPlaceOnCoreRoad(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, int townSeed, int maxBuildings,
                        PlacementSearchLog& searchLog, const TerrainAtlas* terrain, int roadId,
                        const std::vector<int>& junctionHops, const PlacementPrep& prep,
                        RoadAttemptMemo& memo) {
    const BandFilter coreFilter = BandFilter::urbanCore(town);
    return tryPlaceOnTownRoad(town, instance, defs, plots, townSeed, maxBuildings, searchLog,
                              terrain, roadId, coreFilter, junctionHops, prep, memo);
}

}  // namespace

bool hasUncheckedAlleyGapsInCore(Town& town, const TownConfig& townCfg,
                                 const std::vector<int>& junctionHops) {
    (void)junctionHops;
    if (town.urbanCoreMaxHop < 0) {
        return false;
    }
    return placementFrontierHasUncheckedAlleyInCore(town, townCfg.minWallGapForAlley,
                                                    urbanCoreMaxDist(town));
}

bool isUrbanCoreSaturated(Town& town, const TownConfig& townCfg, const DefCache& defs,
                          const std::vector<int>& junctionHops) {
    if (town.urbanCoreMaxHop < 0) {
        return true;
    }

    if (hasBlockingPendingFills(town, townCfg.alleyFillFailLimit)) {
        return false;
    }

    return !hasUncheckedAlleyGapsInCore(town, townCfg, junctionHops);
}

bool tryFillBlockingPendingAlleys(Town& town, BuildingInstance& instance, const DefCache& defs,
                                  const PlotConfig& plots, int townSeed, int maxBuildings,
                                  PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                                  int urbanCoreMaxHop, int suburbanMaxHop,
                                  const std::vector<int>& junctionHops, int failLimit,
                                  const PlacementPrep& prep, RoadAttemptMemo& memo) {
    if (urbanCoreMaxHop < 0 || !hasBlockingPendingFills(town, failLimit)) {
        return false;
    }

    const int pendingIndex = frontPendingAlleyIndex(town, failLimit);
    if (pendingIndex < 0) {
        return false;
    }

    const int alleyRoadId = resolveSecondaryRoadId(
        town, town.pendingAlleyFills[static_cast<std::size_t>(pendingIndex)].addedAtQueueIndex);
    if (alleyRoadId < 0) {
        recordAlleyFillFailure(town, pendingIndex, failLimit);
        return false;
    }

    const BandFilter coreFilter = BandFilter::urbanCore(town);

    const bool placed = tryPlaceOnTownRoad(
        town, instance, defs, plots, townSeed, maxBuildings, searchLog, terrain, alleyRoadId,
        coreFilter, junctionHops, prep, memo);

    if (placed && instanceOnAlleyRoad(instance, alleyRoadId)) {
        recordAlleyFillSuccess(town, pendingIndex);
        const int hops = roadHop(town, alleyRoadId, junctionHops);
        Logger::log("layout",
                    "ring_place: queueIndex=" + std::to_string(instance.id) + " type="
                        + instance.buildingType + " roadHop=" + std::to_string(hops)
                        + " roadId=" + std::to_string(alleyRoadId) + " mode=pending_alley_fill");
    } else {
        recordAlleyFillFailure(town, pendingIndex, failLimit);
    }

    return placed;
}

bool tryPlaceInUrbanCore(Town& town, BuildingInstance& instance, const DefCache& defs,
                         const PlotConfig& plots, const TownConfig& townCfg, int townSeed,
                         int maxBuildings, PlacementSearchLog& searchLog,
                         const TerrainAtlas* terrain, const std::vector<int>& junctionHops,
                         const PlacementPrep& prep, RoadAttemptMemo& memo) {
    if (town.urbanCoreMaxHop < 0) {
        return false;
    }

    const int   failLimit  = townCfg.alleyFillFailLimit;
    const BandFilter coreFilter = BandFilter::urbanCore(town);

    const auto tryAlleyAddAndFill = [&](int hostRoadId) -> bool {
        int newRoadIdLocal = -1;
        if (!tryAddSecondaryRoad(town, instance.id, plots.frontageSetback, townCfg, defs, searchLog,
                                 newRoadIdLocal, hostRoadId, junctionHops, townSeed, terrain)) {
            return false;
        }
        indexJunctions(town);
        return tryPlaceOnTownRoad(town, instance, defs, plots, townSeed, maxBuildings, searchLog,
                                  terrain, newRoadIdLocal, coreFilter, junctionHops, prep, memo);
    };

    bool placed                = false;
    int  pendingIndex          = -1;
    int  alleyRoadIdForPending = -1;

    if (!hasBlockingPendingFills(town, failLimit)
        && resolveSecondaryRoadId(town, instance.id) < 0
        && hasUncheckedAlleyGapsInCore(town, townCfg, junctionHops)) {
        placed = tryAlleyAddAndFill(-1);
        pendingIndex = pendingAlleyIndexByQueueIndex(town, instance.id);
        if (pendingIndex >= 0) {
            alleyRoadIdForPending = resolveSecondaryRoadId(town, instance.id);
        }
    }

    if (!placed) {
        const std::vector<int> coreRoads = collectCoreRoadIds(town);
        for (int roadId : coreRoads) {
            if (skipRoadForPlacement(town, defs, instance.buildingType, roadId, junctionHops)) {
                continue;
            }
            resetPlacementSearchLog(searchLog);
            if (tryPlaceOnCoreRoad(town, instance, defs, plots, townSeed, maxBuildings,
                                   searchLog, terrain, roadId, junctionHops, prep, memo)) {
                placed = true;
                break;
            }
        }
    }

    if (pendingIndex >= 0 && alleyRoadIdForPending >= 0) {
        if (placed && instanceOnAlleyRoad(instance, alleyRoadIdForPending)) {
            recordAlleyFillSuccess(town, pendingIndex);
        } else if (!placed) {
            recordAlleyFillFailure(town, pendingIndex, failLimit);
        }
    }

    if (placed) {
        const int roadId = instance.placementMode == BuildingPlacementMode::SegmentGapFill
                               ? instance.roadId
                               : instance.plot.roadId;
        const int hops = roadHop(town, roadId, junctionHops);
        Logger::log("layout",
                    "ring_place: queueIndex=" + std::to_string(instance.id) + " type="
                        + instance.buildingType + " roadHop=" + std::to_string(hops)
                        + " suburban_max=" + std::to_string(town.suburbanMaxHop) + " urban_core="
                        + std::to_string(town.urbanCoreMaxHop) + " phase=DensifyCore mode="
                        + (instance.placementMode == BuildingPlacementMode::SegmentGapFill
                               ? "gap_fill"
                               : "plot_lot"));
    }

    return placed;
}

bool tryPlaceSuburbanOnRoads(Town& town, BuildingInstance& instance, const DefCache& defs,
                             const PlotConfig& plots, int townSeed, int maxBuildings,
                             PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                             const std::vector<int>& junctionHops, const PlacementPrep& prep,
                             RoadAttemptMemo& memo) {
    (void)junctionHops;
    (void)memo;

    Logger::log("layout",
                "ring_road_list: queueIndex=" + std::to_string(instance.id) + " suburban_max="
                    + std::to_string(town.suburbanMaxHop) + " suburban_dist="
                    + std::to_string(static_cast<int>(suburbanMaxDist(town)))
                    + " mode=frontier");

    const BandFilter suburbanFilter = BandFilter::suburban(town);
    resetPlacementSearchLog(searchLog);
    if (!tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance, prep, townSeed,
                          maxBuildings, searchLog, terrain, suburbanFilter, -1)) {
        const std::string summary = searchLog.resultSummary.empty()
                                        ? "placement_failed"
                                        : searchLog.resultSummary;
        Logger::log("layout",
                    "ring_road_fail: queueIndex=" + std::to_string(instance.id)
                        + " summary=" + summary);
        return false;
    }

    const int logRoadId = instance.placementMode == BuildingPlacementMode::SegmentGapFill
                              ? instance.roadId
                              : instance.plot.roadId;
    const int hops = roadHop(town, logRoadId, junctionHops);
    const float roadDist =
        (logRoadId >= 0 && logRoadId < static_cast<int>(town.roads.size()))
            ? roadMidpointCenterDist(town, town.roads[static_cast<std::size_t>(logRoadId)])
            : 0.f;
    const char* mode =
        instance.placementMode == BuildingPlacementMode::SegmentGapFill ? "gap_fill" : "plot_lot";
    Logger::log("layout",
                "ring_place: queueIndex=" + std::to_string(instance.id) + " type="
                    + instance.buildingType + " roadHop=" + std::to_string(hops) + " roadDist="
                    + std::to_string(static_cast<int>(roadDist)) + " roadId="
                    + std::to_string(logRoadId) + " suburban_max="
                    + std::to_string(town.suburbanMaxHop) + " urban_core="
                    + std::to_string(town.urbanCoreMaxHop) + " phase="
                    + ringPhaseLabel(town.ringPhase) + " mode=" + mode + " band=suburban");
    return true;
}

bool tryPlaceRuralOnRoads(Town& town, BuildingInstance& instance, const DefCache& defs,
                          const PlotConfig& plots, int townSeed, int maxBuildings,
                          PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                          const std::vector<int>& junctionHops, const PlacementPrep& prep,
                          RoadAttemptMemo& memo) {
    (void)memo;

    Logger::log("layout",
                "ring_rural_list: queueIndex=" + std::to_string(instance.id) + " suburban_max="
                    + std::to_string(town.suburbanMaxHop) + " suburban_dist="
                    + std::to_string(static_cast<int>(suburbanMaxDist(town)))
                    + " mode=frontier");

    const BandFilter ruralFilter = BandFilter::rural(town);
    resetPlacementSearchLog(searchLog);
    if (!tryPlaceRoadPlot(town, instance.buildingType, defs, plots, instance, prep, townSeed,
                          maxBuildings, searchLog, terrain, ruralFilter, -1)) {
        const std::string summary = searchLog.resultSummary.empty()
                                        ? "placement_failed"
                                        : searchLog.resultSummary;
        Logger::log("layout",
                    "ring_rural_fail: queueIndex=" + std::to_string(instance.id) + " summary="
                        + summary);
        return false;
    }

    const int placedHop = roadHop(town, instance.plot.roadId, junctionHops);
    const float placedDist = roadMidpointCenterDist(
        town, town.roads[static_cast<std::size_t>(instance.plot.roadId)]);
    Logger::log("layout",
                "ring_place: queueIndex=" + std::to_string(instance.id) + " type="
                    + instance.buildingType + " roadHop=" + std::to_string(placedHop)
                    + " roadDist=" + std::to_string(static_cast<int>(placedDist))
                    + " suburban_max=" + std::to_string(town.suburbanMaxHop) + " mode=rural");
    return true;
}

void rebuildHopDebugRoadMesh(Town& town, const std::vector<int>& junctionHops, float pixelsPerUnit) {
    town.hopDebugRoadMesh.clear();
    town.hopDebugRoadMesh.setPrimitiveType(sf::Triangles);

    const float thicknessPx = 1.f * pixelsPerUnit;
    const float stripePx    = units::toPixels(2.f, pixelsPerUnit);
    const sf::Color primaryAccent(30, 30, 30);
    const sf::Color alleyAccent(40, 200, 80);

    for (const Road& road : town.roads) {
        if (road.isBridge) {
            continue;
        }
        const float dist = roadMidpointCenterDist(town, road);
        const PlacementBand band = classifyPlacementBandByDist(dist, town);
        const sf::Color bandColor = placementBandColor(band);
        const sf::Color accent    = road.isSecondary ? alleyAccent : primaryAccent;

        const Vec2 aPx{units::toPixels(road.a.x, pixelsPerUnit),
                       units::toPixels(road.a.y, pixelsPerUnit)};
        const Vec2 bPx{units::toPixels(road.b.x, pixelsPerUnit),
                       units::toPixels(road.b.y, pixelsPerUnit)};
        appendStripedSegment(town.hopDebugRoadMesh, aPx, bPx, thicknessPx, accent, bandColor,
                             stripePx);
    }
}

void rebuildHopDebugJunctionMesh(Town& town, const std::vector<int>& junctionHops,
                                 float pixelsPerUnit, float radiusUnits) {
    town.hopDebugJunctionMesh.clear();
    town.hopDebugJunctionMesh.setPrimitiveType(sf::Triangles);
    const float radiusPx = units::toPixels(radiusUnits, pixelsPerUnit);

    for (const Junction& junction : town.junctions) {
        const float dist = (junction.pos - town.center).length();
        const PlacementBand band = classifyPlacementBandByDist(dist, town);
        const sf::Vector2f center{units::toPixels(junction.pos.x, pixelsPerUnit),
                                  units::toPixels(junction.pos.y, pixelsPerUnit)};
        appendJunctionDisc(town.hopDebugJunctionMesh, center, radiusPx,
                           placementBandColor(band));
    }
}
