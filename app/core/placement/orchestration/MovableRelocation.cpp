#include "placement/orchestration/MovableRelocation.h"

#include "placement/frontier/FrontierManager.h"
#include "placement/zones/FrontageZones.h"
#include "placement/orchestration/GrowthRings.h"
#include "util/Logger.h"
#include "placement/frontier/PlacementFrontier.h"
#include "placement/orchestration/PlacementPrep.h"
#include "util/Profile.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

struct RelocateCandidate {
    std::uint32_t  instanceId = 0;
    BuildingTypeId typeId     = kInvalidBuildingTypeId;
    int            oldRoadId  = -1;
};

const char* frontierBandLabel(FrontierBand band) {
    switch (band) {
    case FrontierBand::Core:
        return "Core";
    case FrontierBand::Suburban:
        return "Suburban";
    case FrontierBand::Rural:
        return "Rural";
    default:
        return "?";
    }
}

std::string bandChangeLabel(const Town& town, const Road& road, float prevSuburbanDist,
                            float prevCoreDist) {
    const float midDist         = roadMidpointCenterDist(town, road);
    const bool  prevCoreEnabled = prevCoreDist >= 0.f;
    constexpr float kDistEps    = 1e-3f;
    FrontierBand oldBand        = FrontierBand::Suburban;
    if (midDist > prevSuburbanDist + kDistEps) {
        oldBand = FrontierBand::Rural;
    } else if (prevCoreEnabled && midDist <= prevCoreDist + kDistEps) {
        oldBand = FrontierBand::Core;
    }
    const FrontierBand newBand = classifyFrontierBand(midDist, town);
    return std::string(frontierBandLabel(oldBand)) + "->" + frontierBandLabel(newBand);
}

void collectRelocateCandidates(const Town& town, const DefCache& defs,
                               std::vector<RelocateCandidate>& out) {
    out.clear();
    for (const BuildingInstance& inst : town.buildingInstances) {
        const BuildingDef* def = defs.building(inst.typeId);
        if (def == nullptr || !def->movable) {
            continue;
        }
        const int roadId = instanceHostRoadId(inst);
        if (roadId < 0) {
            continue;
        }
        if (buildingCompatibleWithRoadBand(defs, inst, town, roadId)) {
            continue;
        }
        out.push_back({inst.id, inst.typeId, roadId});
    }

    std::sort(out.begin(), out.end(),
              [](const RelocateCandidate& a, const RelocateCandidate& b) {
                  return a.instanceId < b.instanceId;
              });
}

bool tryRelocatePlacement(Town& town, BuildingInstance& trial, const DefCache& defs,
                          const PlotConfig& plots, int townSeed, int maxBuildings,
                          PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                          const std::vector<int>& junctionHops, const PlacementPrep& prep,
                          RoadAttemptMemo& memo) {
    const std::string& typeName = defs.typeName(trial.typeId);
    const char*        zone       = zoneTypeForBuilding(defs, typeName);
    if (zone != nullptr && std::strcmp(zone, "rural") == 0) {
        return tryPlaceRuralOnRoads(town, trial, defs, plots, townSeed, maxBuildings, searchLog,
                                    terrain, junctionHops, prep, memo);
    }
    if (zone != nullptr && std::strcmp(zone, "any") == 0) {
        return tryPlaceAnyOnRoads(town, trial, defs, plots, townSeed, maxBuildings, searchLog,
                                  terrain, junctionHops, prep, memo);
    }
    return tryPlaceSuburbanOnRoads(town, trial, defs, plots, townSeed, maxBuildings, searchLog,
                                   terrain, junctionHops, prep, memo);
}

}  // namespace

void relocateMovableBuildingsAfterRingBump(Town& town, const DefCache& defs,
                                           const PlotConfig& plots, const TownConfig& /*townCfg*/,
                                           int townSeed, int maxBuildings,
                                           const TerrainAtlas* terrain, float prevSuburbanDist,
                                           float prevCoreDist) {
    PROFILE_SCOPE(ProfileScopeId::MovableRelocate);

    std::vector<RelocateCandidate> candidates;
    collectRelocateCandidates(town, defs, candidates);
    Logger::log("layout",
                "move_scan: candidates=" + std::to_string(candidates.size()) + " suburban_max="
                    + std::to_string(town.suburbanMaxHop));
    if (candidates.empty()) {
        return;
    }

    const std::vector<int>& junctionHops = getJunctionHops(town);
    int                     moved          = 0;
    int                     failed         = 0;
    std::unordered_set<int> refreshRoadIds;

    pushFrontierNotifySuppress(town);

    for (const RelocateCandidate& candidate : candidates) {
        const std::string& typeName = defs.typeName(candidate.typeId);
        const Road&        oldRoad  = town.roads[static_cast<std::size_t>(candidate.oldRoadId)];
        const std::string  bandChange = bandChangeLabel(town, oldRoad, prevSuburbanDist, prevCoreDist);

        town.relocatingInstanceId = candidate.instanceId;
        town.relocatingHostRoadId = candidate.oldRoadId;

        BuildingInstance trial;
        trial.id     = candidate.instanceId;
        trial.typeId = candidate.typeId;

        PlacementSearchLog searchLog;
        RoadAttemptMemo    memo;
        memo.syncContext(town);
        const PlacementPrep prep =
            buildPlacementPrep(town, defs, typeName, static_cast<int>(candidate.instanceId),
                               townSeed, maxBuildings);

        const bool placed = tryRelocatePlacement(town, trial, defs, plots, townSeed, maxBuildings,
                                               searchLog, terrain, junctionHops, prep, memo);

        town.relocatingInstanceId = 0xFFFFFFFFu;
        town.relocatingHostRoadId = -1;

        if (!placed) {
            ++failed;
            ++town.moveFailureCount;
            Logger::log("layout",
                        "move_fail: queueIndex=" + std::to_string(candidate.instanceId) + " type="
                            + typeName + " oldRoadId=" + std::to_string(candidate.oldRoadId)
                            + " band=" + bandChange);
            continue;
        }

        const int newRoadId = trial.placementMode == BuildingPlacementMode::SegmentGapFill
                                  ? trial.roadId
                                  : trial.plot.roadId;

        removeBuildingInstance(town, static_cast<int>(candidate.instanceId),
                               town.syncFrontageSetback, terrain, &plots);
        town.buildingInstances.push_back(trial);

        refreshRoadIds.insert(candidate.oldRoadId);
        if (newRoadId >= 0) {
            refreshRoadIds.insert(newRoadId);
        }

        ++moved;
        Logger::log("layout",
                    "move_ok: queueIndex=" + std::to_string(candidate.instanceId) + " type="
                        + typeName + " oldRoadId=" + std::to_string(candidate.oldRoadId)
                        + " newRoadId=" + std::to_string(newRoadId) + " band=" + bandChange);
    }

    popFrontierNotifySuppress(town);

    for (int roadId : refreshRoadIds) {
        notifyRoadFrontierRefresh(town, roadId, terrain, town.syncTerrainCatalog,
                                  &town.syncTerrainProbes, &plots);
    }

    Logger::log("layout",
                "move_summary: attempted=" + std::to_string(static_cast<int>(candidates.size()))
                    + " moved=" + std::to_string(moved) + " move_failures=" + std::to_string(failed));
}
