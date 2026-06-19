#pragma once

#include "config/DefCache.h"
#include "placement/logging/PlacementLogging.h"
#include "terrain/TerrainAtlas.h"
#include "town/Town.h"
#include "config/TownConfig.h"

int resolveSecondaryRoadId(const Town& town, int addedAtQueueIndex);
void syncPendingAlleyFills(Town& town, int targetCount);
bool hasBlockingPendingFills(const Town& town, int failLimit);
int  frontPendingAlleyIndex(const Town& town, int failLimit);
int  pendingAlleyIndexByQueueIndex(const Town& town, int addedAtQueueIndex);
void recordAlleyFillSuccess(Town& town, int pendingIndex);
void recordAlleyFillFailure(Town& town, int pendingIndex, int failLimit);
void enqueuePendingAlleyFill(Town& town, int addedAtQueueIndex, int hostRoadId);

bool tryAddSecondaryRoad(Town& town, int queueIndex, float setback, const TownConfig& townCfg,
                         const DefCache& defs, PlacementSearchLog& searchLog, int& outRoadId,
                         int forceRoadId, const std::vector<int>& junctionHops, int townSeed,
                         const TerrainAtlas* terrain = nullptr);
bool removeSecondaryRecordsBlockedByMainFootprint(Town& town, const BuildingFootprint& footprint,
                                                  int hostRoadId,
                                                  const TerrainAtlas* terrain = nullptr);
// Debug/recovery: full town reconcile (not called from normal sync).
bool removeAlleysThroughSecondaryBuildings(Town& town);
