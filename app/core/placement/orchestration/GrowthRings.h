#pragma once

#include "town/Town.h"
#include "config/TownConfig.h"

#include "config/DefCache.h"
#include "placement/logging/PlacementLogging.h"
#include "placement/orchestration/PlacementPrep.h"

#include <string>
#include <vector>

struct PlotConfig;
struct TerrainAtlas;

struct BandFilter {
    float minDistInclusive = 0.f;
    float maxDistInclusive = 1e9f;
    bool  enabled          = false;
    bool  exclusiveMin     = false;

    static BandFilter none() { return {}; }
    static BandFilter suburban(const Town& town);
    static BandFilter rural(const Town& town);
    static BandFilter urbanCore(const Town& town);
};

bool distInFilter(float dist, const BandFilter& filter);
bool mayGapFillOnRoad(const Town& town, const DefCache& defs, const std::string& buildingType,
                      int roadId);
bool tryPlaceOnTownRoad(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, int townSeed, int maxBuildings,
                        PlacementSearchLog& searchLog, const TerrainAtlas* terrain, int roadId,
                        const BandFilter& bandFilter, const std::vector<int>& junctionHops,
                        const PlacementPrep& prep, RoadAttemptMemo& memo);
void bumpGrowthRings(Town& town);
void clearAlleyStateForRoad(Town& town, int roadId);
int  maxRoadHopInTown(const Town& town, const std::vector<int>& junctionHops);
std::vector<int> collectCoreRoadIds(const Town& town);
bool tryFillBlockingPendingAlleys(Town& town, BuildingInstance& instance, const DefCache& defs,
                                  const PlotConfig& plots, int townSeed, int maxBuildings,
                                  PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                                  int urbanCoreMaxHop, int suburbanMaxHop,
                                  const std::vector<int>& junctionHops, int failLimit,
                                  const PlacementPrep& prep, RoadAttemptMemo& memo);
bool hasUncheckedAlleyGapsInCore(Town& town, const TownConfig& townCfg,
                                 const std::vector<int>& junctionHops);
bool isUrbanCoreSaturated(Town& town, const TownConfig& townCfg, const DefCache& defs,
                          const std::vector<int>& junctionHops);
bool tryPlaceInUrbanCore(Town& town, BuildingInstance& instance, const DefCache& defs,
                         const PlotConfig& plots, const TownConfig& townCfg, int townSeed,
                         int maxBuildings, PlacementSearchLog& searchLog,
                         const TerrainAtlas* terrain, const std::vector<int>& junctionHops,
                         const PlacementPrep& prep, RoadAttemptMemo& memo);
bool tryPlaceSuburbanOnRoads(Town& town, BuildingInstance& instance, const DefCache& defs,
                             const PlotConfig& plots, int townSeed, int maxBuildings,
                             PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                             const std::vector<int>& junctionHops, const PlacementPrep& prep,
                             RoadAttemptMemo& memo);
bool tryPlaceRuralOnRoads(Town& town, BuildingInstance& instance, const DefCache& defs,
                          const PlotConfig& plots, int townSeed, int maxBuildings,
                          PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                          const std::vector<int>& junctionHops, const PlacementPrep& prep,
                          RoadAttemptMemo& memo);
bool tryPlaceAnyOnRoads(Town& town, BuildingInstance& instance, const DefCache& defs,
                        const PlotConfig& plots, int townSeed, int maxBuildings,
                        PlacementSearchLog& searchLog, const TerrainAtlas* terrain,
                        const std::vector<int>& junctionHops, const PlacementPrep& prep,
                        RoadAttemptMemo& memo);
void logRingState(const Town& town);
void rebuildHopDebugRoadMesh(Town& town, const std::vector<int>& junctionHops, float pixelsPerUnit);
void rebuildHopDebugJunctionMesh(Town& town, const std::vector<int>& junctionHops,
                                 float pixelsPerUnit, float radiusUnits = 1.f);
std::string ringPhaseLabel(RingPhase phase);
