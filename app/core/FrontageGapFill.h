#pragma once

#include "DefCache.h"
#include "PlacementLogging.h"
#include "Town.h"

#include <string>

bool tryPlaceSegmentMain(Town& town, const std::string& buildingType, const DefCache& defs,
                         const PlotConfig& plots, BuildingInstance& out, int townSeed,
                         int maxBuildings, PlacementSearchLog& searchLog, int roadFilter = -1,
                         bool useCellCentroid = false);
