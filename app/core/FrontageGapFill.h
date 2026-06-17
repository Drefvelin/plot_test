#pragma once

#include "GrowthRings.h"

#include "DefCache.h"
#include "PlacementLogging.h"
#include "PlacementPrep.h"
#include "Town.h"

#include <string>

struct TerrainAtlas;

bool bankHasMainWallGapAtLeast(Town& town, int roadId, int bankIndex, float minWidth);

bool tryPlaceSegmentMain(Town& town, const std::string& buildingType, const DefCache& defs,
                         const PlotConfig& plots, BuildingInstance& out, const PlacementPrep& prep,
                         int townSeed, int maxBuildings, PlacementSearchLog& searchLog,
                         const TerrainAtlas* terrain = nullptr,
                         const BandFilter& bandFilter = BandFilter::none(), int roadFilter = -1);
