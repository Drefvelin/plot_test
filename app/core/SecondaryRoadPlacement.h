#pragma once

#include "DefCache.h"
#include "PlacementLogging.h"
#include "Town.h"
#include "TownConfig.h"

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
                         int forceRoadId, const std::vector<int>& junctionHops, int townSeed);
bool removeAlleysThroughSecondaryBuildings(Town& town);
